/*
 * SPDX-FileCopyrightText: 2026 XH Lab
 * SPDX-License-Identifier: Apache-2.0
 * 中文：微信读书本机授权、书架笔记同步与只读在线发现；设备自己连接官方接口。
 * English: Device-owned WeRead authorization, shelf/note sync and read-only discovery against official APIs.
 * 冻结：不下载正文；不跟随重定向或跳过TLS；新代文件全部完成后才发布索引，保留旧代与阅读位置。
 * Frozen: No body download, redirects or insecure TLS; publish an index only after its generation is complete,
 * retaining old generations and positions.
 */
#define _POSIX_C_SOURCE 200809L
#include "pico_weread.h"
#include "pico_weread_cover.h"
#include "../../apps/include/weread_snapshot.h"
#include "../../components/read_pico_home_link/home_link_json.h"
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#if defined(PICO_NATIVE_PREVIEW)
#include "nvs.h"
#include "read_pico_connectivity.h"
#include "read_pico_sd.h"
#include <poll.h>
#include <signal.h>
#include <spawn.h>
#include <sys/wait.h>
#endif
#ifdef ESP_PLATFORM
#include "esp_crt_bundle.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_random.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "nvs.h"
#include "read_pico_connectivity.h"
#include "read_pico_sd.h"
static SemaphoreHandle_t s_lock;
#define LOCK() xSemaphoreTake(s_lock, portMAX_DELAY)
#define UNLOCK() xSemaphoreGive(s_lock)
static void *wr_alloc(size_t n) { return heap_caps_malloc(n, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT); }
#else
#include <pthread.h>
static pthread_mutex_t s_lock = PTHREAD_MUTEX_INITIALIZER;
#define LOCK() pthread_mutex_lock(&s_lock)
#define UNLOCK() pthread_mutex_unlock(&s_lock)
static void *wr_alloc(size_t n) { return malloc(n); }
#ifdef PICO_WEREAD_HOST_TEST
extern bool wr_test_wifi(void);
extern bool wr_test_media(uint32_t *generation);
extern bool wr_test_nv_get(const char *name, void *out, size_t *size);
extern bool wr_test_nv_set(const char *name, const void *data, size_t size);
extern bool wr_test_nv_clear(void);
extern bool wr_test_nv_authorize(const char *key);
extern const char *wr_test_directory(void);
extern const char *wr_test_http(const char *key, const char *request, char *response, size_t capacity, size_t *used,
                                int *status);
extern uint32_t wr_test_random(void);
#endif
#endif
#define WR_RESPONSE_MAX (512u * 1024u)
#define WR_TOTAL_MAX (32u * 1024u * 1024u)
#define WR_NOTE_MAX 10000u
#define WR_SELECTION_BYTES (4u + WRS_BOOK_MAX * 64u)
static ps_weread_status_t *s_status;
static atomic_bool s_busy, s_cancel;
static uint16_t s_pause_leases;
static uint32_t s_auth_epoch = 1;
static char s_selected[WRS_BOOK_MAX][64];
static uint32_t s_selected_count;
static bool s_job_sync;
static ps_weread_query_t s_job_query;
static ps_weread_query_status_t s_query;
static char *s_query_data;
static const char *const s_query_names[] = {"", "search", "info", "chapters", "progress", "recommend", "similar", "stats", "highlights", "reviews"};
static const char *const s_query_apis[] = {"", "/store/search", "/book/info", "/book/chapterinfo", "/book/getprogress", "/book/recommend", "/book/similar", "/readdata/detail", "/book/bestbookmarks", "/review/list"};

/* ---- 平台边界 / Platform boundary ---- */
static void wipe(void *data, size_t n) {
    volatile unsigned char *p = data;
    while (n--)
        *p++ = 0;
}
static bool wifi_ready(void) {
#if defined(ESP_PLATFORM) || defined(PICO_NATIVE_PREVIEW)
    read_pico_connectivity_status_t s = {0};
    read_pico_connectivity_get_status(&s);
    return s.connected;
#elif defined(PICO_WEREAD_HOST_TEST)
    return wr_test_wifi();
#else
    return false;
#endif
}
static bool media(uint32_t *generation) {
#if defined(ESP_PLATFORM) || defined(PICO_NATIVE_PREVIEW)
    read_pico_sd_info_t i = {0};
    bool ok = read_pico_sd_get_info(&i) == ESP_OK && i.present && i.mounted && !i.exported;
    if (generation)
        *generation = i.media_generation;
    return ok;
#elif defined(PICO_WEREAD_HOST_TEST)
    return wr_test_media(generation);
#else
    (void)generation;
    return false;
#endif
}
static bool nv_get(const char *name, void *out, size_t *size) {
#if defined(ESP_PLATFORM) || defined(PICO_NATIVE_PREVIEW)
    nvs_handle_t h;
    if (nvs_open("pico_weread", NVS_READONLY, &h) != ESP_OK)
        return false;
    esp_err_t e = nvs_get_blob(h, name, out, size);
    nvs_close(h);
    return e == ESP_OK;
#elif defined(PICO_WEREAD_HOST_TEST)
    return wr_test_nv_get(name, out, size);
#else
    (void)name;
    (void)out;
    (void)size;
    return false;
#endif
}
static bool nv_set(const char *name, const void *data, size_t size) {
#if defined(ESP_PLATFORM) || defined(PICO_NATIVE_PREVIEW)
    nvs_handle_t h;
    if (nvs_open("pico_weread", NVS_READWRITE, &h) != ESP_OK)
        return false;
    bool ok = nvs_set_blob(h, name, data, size) == ESP_OK && nvs_commit(h) == ESP_OK;
    nvs_close(h);
    return ok;
#elif defined(PICO_WEREAD_HOST_TEST)
    return wr_test_nv_set(name, data, size);
#else
    (void)name;
    (void)data;
    (void)size;
    return false;
#endif
}
static bool nv_clear(void) {
#if defined(ESP_PLATFORM) || defined(PICO_NATIVE_PREVIEW)
    nvs_handle_t h;
    if (nvs_open("pico_weread", NVS_READWRITE, &h) != ESP_OK)
        return false;
    esp_err_t e = nvs_erase_key(h, "api_key"), other = nvs_erase_key(h, "selected");
    bool ok = (e == ESP_OK || e == ESP_ERR_NVS_NOT_FOUND) && (other == ESP_OK || other == ESP_ERR_NVS_NOT_FOUND) &&
              nvs_commit(h) == ESP_OK;
    nvs_close(h);
    return ok;
#elif defined(PICO_WEREAD_HOST_TEST)
    return wr_test_nv_clear();
#else
    return false;
#endif
}
static bool nv_authorize(const char *key) {
#if defined(ESP_PLATFORM) || defined(PICO_NATIVE_PREVIEW)
    nvs_handle_t h;
    if (nvs_open("pico_weread", NVS_READWRITE, &h) != ESP_OK)
        return false;
    esp_err_t e = nvs_erase_key(h, "selected");
    bool ok = (e == ESP_OK || e == ESP_ERR_NVS_NOT_FOUND) &&
              nvs_set_blob(h, "api_key", key, strlen(key) + 1) == ESP_OK && nvs_commit(h) == ESP_OK;
    nvs_close(h);
    return ok;
#elif defined(PICO_WEREAD_HOST_TEST)
    return wr_test_nv_authorize(key);
#else
    (void)key;
    return false;
#endif
}
static const char *directory(void) {
#ifdef PICO_WEREAD_HOST_TEST
    return wr_test_directory();
#else
    return "/sdcard/apps/data/weread";
#endif
}
static uint32_t random_generation(void) {
#ifdef ESP_PLATFORM
    return esp_random();
#elif defined(PICO_WEREAD_HOST_TEST)
    return wr_test_random();
#else
    return (uint32_t)time(NULL);
#endif
}
static void state_error(const char *e) {
    LOCK();
    if (s_status && !atomic_load(&s_busy)) {
        snprintf(s_status->state, sizeof(s_status->state), "error");
        snprintf(s_status->error, sizeof(s_status->error), "%s", e);
        s_status->revision++;
    }
    UNLOCK();
}
static bool same_media(uint32_t generation) {
    uint32_t current = 0;
    return media(&current) && current == generation;
}
static const char *checkpoint(uint32_t generation) {
    if (atomic_load(&s_cancel))
        return "cancelled";
    uint32_t current = 0;
    if (!media(&current) || current != generation)
        return "storage";
    return NULL;
}

/* ---- 严格数据解析 / Strict data parsing ---- */
static cJSON *member(const cJSON *o, const char *name) { return cJSON_GetObjectItemCaseSensitive(o, name); }
static bool json_bounded(const char *text, size_t bytes) {
    unsigned depth = 0;
    bool quoted = false;
    for (size_t i = 0; i < bytes; i++) {
        unsigned char c = (unsigned char)text[i];
        if (!c)
            return false;
        if (quoted) {
            if (c < 32)
                return false;
            if (c == '\\') {
                if (++i >= bytes)
                    return false;
                if (text[i] == 'u' && bytes - i >= 5 && !memcmp(text + i, "u0000", 5))
                    return false;
            } else if (c == '"')
                quoted = false;
        } else if (c == '"')
            quoted = true;
        else if (c == '{' || c == '[') {
            if (++depth > 32)
                return false;
        } else if (c == '}' || c == ']') {
            if (!depth)
                return false;
            depth--;
        }
    }
    return !quoted && !depth;
}
static bool json_unique(const cJSON *o) {
    const cJSON *item = NULL;
    cJSON_ArrayForEach(item, o) {
        if (cJSON_IsObject(o))
            for (const cJSON *prior = o->child; prior != item; prior = prior->next)
                if (!strcmp(prior->string, item->string))
                    return false;
        if ((cJSON_IsObject(item) || cJSON_IsArray(item)) && !json_unique(item))
            return false;
    }
    return true;
}
static bool integer(const cJSON *v, int64_t *out) {
    if (cJSON_IsNumber(v) && isfinite(v->valuedouble) && floor(v->valuedouble) == v->valuedouble &&
        fabs(v->valuedouble) <= 9007199254740991.0) {
        *out = (int64_t)v->valuedouble;
        return true;
    }
    if (cJSON_IsString(v) && v->valuestring && *v->valuestring) {
        char *end = NULL;
        errno = 0;
        long long n = strtoll(v->valuestring, &end, 10);
        if (!errno && end && !*end) {
            *out = n;
            return true;
        }
    }
    return false;
}
static bool utf8(const char *s, size_t n) {
    for (size_t i = 0; i < n;) {
        unsigned char a = (unsigned char)s[i++];
        if (a < 0x80) {
            if ((a < 0x20 && a != '\n' && a != '\t') || a == 0x7f)
                return false;
            continue;
        }
        unsigned count;
        uint32_t code, min;
        if (a >= 0xc2 && a <= 0xdf) {
            count = 1;
            code = a & 31;
            min = 0x80;
        } else if (a >= 0xe0 && a <= 0xef) {
            count = 2;
            code = a & 15;
            min = 0x800;
        } else if (a >= 0xf0 && a <= 0xf4) {
            count = 3;
            code = a & 7;
            min = 0x10000;
        } else
            return false;
        if (n - i < count)
            return false;
        while (count--) {
            unsigned char b = (unsigned char)s[i++];
            if ((b & 0xc0) != 0x80)
                return false;
            code = (code << 6) | (b & 63);
        }
        if (code < min || code > 0x10ffff || (code >= 0xd800 && code <= 0xdfff) || (code >= 0x80 && code <= 0x9f))
            return false;
    }
    return true;
}
static bool id_valid(const char *s) {
    size_t n = s ? strlen(s) : 0;
    if (!n || n >= 64)
        return false;
    for (size_t i = 0; i < n; i++) {
        char c = s[i];
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-'))
            return false;
    }
    return true;
}
static bool copy_text(char *out, size_t capacity, const cJSON *v, bool optional) {
    if (optional && (!v || cJSON_IsNull(v))) {
        out[0] = 0;
        return true;
    }
    if (!cJSON_IsString(v) || !v->valuestring)
        return false;
    size_t n = strlen(v->valuestring);
    if (n >= capacity || !utf8(v->valuestring, n))
        return false;
    memcpy(out, v->valuestring, n + 1);
    return true;
}
static bool copy_display(char *out, size_t capacity, const cJSON *v, bool optional) {
    if (optional && (!v || cJSON_IsNull(v))) {
        out[0] = 0;
        return true;
    }
    if (!cJSON_IsString(v) || !v->valuestring)
        return false;
    size_t bytes = strlen(v->valuestring), n = bytes < capacity ? bytes : capacity - 1;
    if (n < bytes)
        while (n && ((unsigned char)v->valuestring[n] & 0xc0) == 0x80)
            n--;
    memcpy(out, v->valuestring, n);
    out[n] = 0;
    for (size_t i = 0; i < n; i++)
        if (out[i] == '\r' || out[i] == '\n' || out[i] == '\t')
            out[i] = ' ';
    return utf8(out, n);
}
static bool key_valid(const char *key) {
    size_t n = key ? strlen(key) : 0;
    if (n < 8 || n > PS_WEREAD_KEY_MAX || strncmp(key, "wrk-", 4))
        return false;
    for (size_t i = 0; i < n; i++)
        if ((unsigned char)key[i] < 33 || (unsigned char)key[i] > 126)
            return false;
    return true;
}
static cJSON *parse(const void *body, size_t bytes) {
    if (!body || !bytes || bytes > 12288 || !json_bounded(body, bytes))
        return NULL;
    const char *end = NULL;
    cJSON *o = cJSON_ParseWithLengthOpts(body, bytes, &end, false);
    if (!cJSON_IsObject(o) || !json_unique(o)) {
        cJSON_Delete(o);
        return NULL;
    }
    while (end < (const char *)body + bytes && (*end == ' ' || *end == '\t' || *end == '\r' || *end == '\n'))
        end++;
    if (end != (const char *)body + bytes) {
        cJSON_Delete(o);
        return NULL;
    }
    return o;
}

#ifdef PICO_NATIVE_PREVIEW
static bool curl_line(FILE *f, const char *name, const char *value) {
    if (fprintf(f, "%s = \"", name) < 0)
        return false;
    for (const unsigned char *p = (const unsigned char *)value; *p; p++) {
        if (*p == '\r' || *p == '\n')
            return false;
        if ((*p == '\\' || *p == '\"') && fputc('\\', f) == EOF)
            return false;
        if (fputc(*p, f) == EOF)
            return false;
    }
    return fputs("\"\n", f) != EOF;
}
static const char *native_http(const char *key, const char *body, char *response, size_t capacity, size_t *used,
                               int *status) {
    // 本机预览的显式合成夹具不发网络；缺失文件也不得回退到真实账户。
    // Explicit native fixtures never send network requests, even when a fixture file is missing.
    const char *fixtures = getenv("RPICO_WEREAD_FIXTURE_ROOT");
    if (fixtures && *fixtures) {
        cJSON *q = cJSON_Parse(body);
        const char *api = q ? cJSON_GetStringValue(member(q, "api_name")) : NULL;
        const char *name = NULL;
        for (unsigned i = PS_WEREAD_SEARCH; api && i <= PS_WEREAD_REVIEWS; i++)
            if (!strcmp(api, s_query_apis[i]))
                name = s_query_names[i];
        if (api && !strcmp(api, "/shelf/sync"))
            name = "shelf";
        else if (api && !strcmp(api, "/book/bookmarklist"))
            name = "bookmarks";
        else if (api && !strcmp(api, "/review/list/mine"))
            name = "mine";
        char path[1024];
        bool valid = name && snprintf(path, sizeof(path), "%s/%s.json", fixtures, name) < (int)sizeof(path);
        cJSON_Delete(q);
        FILE *f = valid ? fopen(path, "rb") : NULL;
        if (!f)
            return "service";
        size_t n = fread(response, 1, capacity, f);
        bool ok = !ferror(f);
        fclose(f);
        if (!ok)
            return "service";
        if (n >= capacity)
            return "too_large";
        response[n] = 0;
        *used = n;
        *status = 200;
        return NULL;
    }
    const char *offline = getenv("RPICO_WEREAD_OFFLINE");
    if (offline && strcmp(offline, "0"))
        return "offline";
    char *config = NULL;
    size_t config_bytes = 0;
    FILE *f = open_memstream(&config, &config_bytes);
    if (!f)
        return "memory";
    char auth[PS_WEREAD_KEY_MAX + 24];
    snprintf(auth, sizeof(auth), "Authorization: Bearer %s", key);
    bool ok =
        fputs("silent\nmax-time = 20\nconnect-timeout = 8\nmax-filesize = 524287\nwrite-out = \"\\n%{http_code}\"\n",
              f) != EOF &&
        curl_line(f, "url", "https://i.weread.qq.com/api/agent/gateway") && curl_line(f, "request", "POST") &&
        curl_line(f, "header", auth) && curl_line(f, "header", "Content-Type: application/json") &&
        curl_line(f, "data-binary", body);
    wipe(auth, sizeof(auth));
    if (fclose(f))
        ok = false;
    if (!ok) {
        if (config) {
            wipe(config, config_bytes);
            free(config);
        }
        return "memory";
    }
    int input[2] = {-1, -1}, output[2] = {-1, -1};
    pid_t pid = 0;
    posix_spawn_file_actions_t actions;
    bool actions_live = false;
    if (pipe(input) || pipe(output))
        ok = false;
    if (ok) {
        posix_spawn_file_actions_init(&actions);
        actions_live = true;
        posix_spawn_file_actions_adddup2(&actions, input[0], STDIN_FILENO);
        posix_spawn_file_actions_adddup2(&actions, output[1], STDOUT_FILENO);
        posix_spawn_file_actions_addopen(&actions, STDERR_FILENO, "/dev/null", O_WRONLY, 0600);
        posix_spawn_file_actions_addclose(&actions, input[1]);
        posix_spawn_file_actions_addclose(&actions, output[0]);
        char *argv[] = {"/usr/bin/curl", "--disable", "--proto", "=https", "--config", "-", NULL};
        extern char **environ;
        ok = posix_spawn(&pid, argv[0], &actions, NULL, argv, environ) == 0;
    }
    if (actions_live)
        posix_spawn_file_actions_destroy(&actions);
    if (input[0] >= 0)
        close(input[0]);
    if (output[1] >= 0)
        close(output[1]);
#ifdef F_SETNOSIGPIPE
    if (input[1] >= 0)
        fcntl(input[1], F_SETNOSIGPIPE, 1);
#endif
    size_t sent = 0;
    if (ok)
        while (sent < config_bytes) {
            ssize_t n = write(input[1], config + sent, config_bytes - sent);
            if (n < 0 && errno == EINTR)
                continue;
            if (n <= 0) {
                ok = false;
                break;
            }
            sent += (size_t)n;
        }
    if (input[1] >= 0)
        close(input[1]);
    wipe(config, config_bytes);
    free(config);
    const char *error = ok ? NULL : "offline";
    if (ok) {
        fcntl(output[0], F_SETFL, fcntl(output[0], F_GETFL) | O_NONBLOCK);
        while (true) {
            if (atomic_load(&s_cancel)) {
                error = "cancelled";
                break;
            }
            struct pollfd pollfd = {.fd = output[0], .events = POLLIN | POLLHUP};
            int ready = poll(&pollfd, 1, 100);
            if (ready < 0) {
                if (errno == EINTR)
                    continue;
                error = "offline";
                break;
            }
            if (!ready)
                continue;
            char block[2048];
            ssize_t n = read(output[0], block, sizeof(block));
            if (n < 0) {
                if (errno == EAGAIN || errno == EINTR)
                    continue;
                error = "offline";
                break;
            }
            if (!n)
                break;
            if ((size_t)n >= capacity - *used) {
                error = "too_large";
                break;
            }
            memcpy(response + *used, block, (size_t)n);
            *used += (size_t)n;
        }
    }
    if (output[0] >= 0)
        close(output[0]);
    if (error && pid > 0)
        kill(pid, SIGTERM);
    int exit_status = 0;
    if (pid > 0)
        while (waitpid(pid, &exit_status, 0) < 0 && errno == EINTR) {
        }
    if (!error && (!WIFEXITED(exit_status) || WEXITSTATUS(exit_status))) {
        int e = WIFEXITED(exit_status) ? WEXITSTATUS(exit_status) : -1;
        error = e == 60 ? "tls" : e == 63 ? "too_large" : "offline";
    }
    if (!error) {
        if (*used < 4 || response[*used - 4] != '\n')
            error = "malformed";
        else {
            *status = (response[*used - 3] - '0') * 100 + (response[*used - 2] - '0') * 10 + response[*used - 1] - '0';
            *used -= 4;
            response[*used] = 0;
        }
    }
    return error;
}
#endif

/* ---- 官方HTTPS / Official HTTPS ---- */
static const char *http_post(const char *key, const char *body, char *response, size_t capacity, size_t *used,
                             int *status) {
    *used = 0;
    *status = 0;
#ifdef ESP_PLATFORM
    time_t now = time(NULL);
    if (now < 1700000000 || now > 4102444799LL)
        return "time";
    esp_http_client_config_t cfg = {.url = "https://i.weread.qq.com/api/agent/gateway",
                                    .timeout_ms = 12000,
                                    .buffer_size = 1024,
                                    .crt_bundle_attach = esp_crt_bundle_attach,
                                    .disable_auto_redirect = true,
                                    .method = HTTP_METHOD_POST};
    esp_http_client_handle_t h = esp_http_client_init(&cfg);
    if (!h)
        return "memory";
    char auth[PS_WEREAD_KEY_MAX + 8];
    snprintf(auth, sizeof(auth), "Bearer %s", key);
    esp_http_client_set_header(h, "Authorization", auth);
    wipe(auth, sizeof(auth));
    esp_http_client_set_header(h, "Content-Type", "application/json");
    esp_http_client_set_header(h, "Accept", "application/json");
    const char *error = "offline";
    size_t length = strlen(body);
    esp_err_t opened = esp_http_client_open(h, (int)length);
    if (opened == ESP_OK) {
        size_t sent = 0;
        while (sent < length && !atomic_load(&s_cancel)) {
            int n = esp_http_client_write(h, body + sent, (int)(length - sent));
            if (n <= 0)
                break;
            sent += (size_t)n;
        }
        if (sent == length) {
            int64_t declared = esp_http_client_fetch_headers(h);
            *status = esp_http_client_get_status_code(h);
            if (declared >= (int64_t)capacity)
                error = "too_large";
            else {
                while (*used < capacity - 1 && !atomic_load(&s_cancel)) {
                    int n = esp_http_client_read(h, response + *used, (int)(capacity - 1 - *used));
                    if (n <= 0)
                        break;
                    *used += (size_t)n;
                }
                response[*used] = 0;
                error = esp_http_client_is_complete_data_received(h) ? NULL
                        : *used == capacity - 1                      ? "too_large"
                                                                     : "offline";
            }
        }
        esp_http_client_close(h);
    } else {
        int code = 0, flags = 0;
        esp_err_t e = esp_http_client_get_and_clear_last_tls_error(h, &code, &flags);
        if (flags || code)
            error = "tls";
        if (opened == ESP_ERR_NO_MEM || e == ESP_ERR_NO_MEM)
            error = "memory";
    }
    esp_http_client_cleanup(h);
    return atomic_load(&s_cancel) ? "cancelled" : error;
#elif defined(PICO_WEREAD_HOST_TEST)
    return wr_test_http(key, body, response, capacity, used, status);
#elif defined(PICO_NATIVE_PREVIEW)
    return native_http(key, body, response, capacity, used, status);
#else
    (void)key;
    (void)body;
    (void)response;
    (void)capacity;
    return "offline";
#endif
}
static cJSON *request_object(const char *key, cJSON *q, const char **error) {
    if (atomic_load(&s_cancel)) {
        *error = "cancelled";
        cJSON_Delete(q);
        return NULL;
    }
    if (!wifi_ready()) {
        *error = "offline";
        cJSON_Delete(q);
        return NULL;
    }
    if (!q) {
        *error = "memory";
        return NULL;
    }
    char *body = cJSON_PrintUnformatted(q);
    cJSON_Delete(q);
    if (!body) {
        *error = "memory";
        return NULL;
    }
    char *response = wr_alloc(WR_RESPONSE_MAX);
    if (!response) {
        cJSON_free(body);
        *error = "memory";
        return NULL;
    }
    size_t used = 0;
    int status = 0;
    *error = http_post(key, body, response, WR_RESPONSE_MAX, &used, &status);
    cJSON_free(body);
    if (!*error) {
        if (status == 401 || status == 403)
            *error = "auth";
        else if (status == 429)
            *error = "rate";
        else if (status != 200)
            *error = "service";
    }
    cJSON *root = NULL;
    if (!*error) {
        const char *end = NULL;
        if (used < WR_RESPONSE_MAX && json_bounded(response, used))
            root = cJSON_ParseWithLengthOpts(response, used + 1, &end, true);
        if (!cJSON_IsObject(root) || !json_unique(root))
            *error = "malformed";
    }
    wipe(response, used);
    free(response);
    if (!*error) {
        cJSON *payload = member(root, "data");
        if (payload && !cJSON_IsObject(payload))
            *error = "malformed";
        if (!payload)
            payload = root;
        cJSON *upgrade = member(root, "upgrade_info"), *up2 = member(payload, "upgrade_info");
        if ((upgrade && !cJSON_IsNull(upgrade)) || (up2 && !cJSON_IsNull(up2)))
            *error = "upgrade";
        int64_t code = 0;
        cJSON *errcode = member(root, "errcode");
        if (errcode && (!integer(errcode, &code) || code))
            *error = "service";
        if (!*error && payload != root) {
            cJSON *detached = cJSON_DetachItemFromObjectCaseSensitive(root, "data");
            cJSON_Delete(root);
            root = detached;
        }
    }
    if (*error) {
        cJSON_Delete(root);
        return NULL;
    }
    return root;
}
static cJSON *request(const char *key, const char *api, const char *book, int64_t cursor, const char **error) {
    cJSON *q = cJSON_CreateObject();
    bool ok = q && cJSON_AddStringToObject(q, "api_name", api) && cJSON_AddStringToObject(q, "skill_version", "1.0.5");
    if (book)
        ok = ok && cJSON_AddStringToObject(q, !strcmp(api, "/review/list/mine") ? "bookid" : "bookId", book);
    if (!strcmp(api, "/review/list/mine"))
        ok = ok && cJSON_AddNumberToObject(q, "synckey", (double)cursor) && cJSON_AddNumberToObject(q, "count", 100);
    if (!ok) {
        cJSON_Delete(q);
        q = NULL;
    }
    return request_object(key, q, error);
}
static const char *fetch_shelf(const char *key, ps_weread_book_t *books, uint32_t *count) {
    const char *e = NULL;
    cJSON *root = request(key, "/shelf/sync", NULL, 0, &e);
    if (!root)
        return e;
    cJSON *list = member(root, "books");
    *count = 0;
    if (!cJSON_IsArray(list)) {
        cJSON_Delete(root);
        return "malformed";
    }
    if (cJSON_GetArraySize(list) > (int)WRS_BOOK_MAX) {
        cJSON_Delete(root);
        return "too_large";
    }
    cJSON *b = NULL;
    cJSON_ArrayForEach(b, list) {
        ps_weread_book_t *out = &books[*count];
        if (!copy_text(out->id, sizeof(out->id), member(b, "bookId"), false) || !id_valid(out->id) ||
            !copy_display(out->title, sizeof(out->title), member(b, "title"), false) || !out->title[0] ||
            !copy_display(out->author, sizeof(out->author), member(b, "author"), true)) {
            e = "too_large";
            break;
        }
        for (uint32_t i = 0; i < *count; i++)
            if (!strcmp(books[i].id, out->id)) {
                e = "malformed";
                break;
            }
        if (e)
            break;
        (*count)++;
    }
    cJSON_Delete(root);
    return e;
}

/* ---- WRS原子存储 / Atomic WRS storage ---- */
static void put32(uint8_t *p, uint32_t n) {
    p[0] = n;
    p[1] = n >> 8;
    p[2] = n >> 16;
    p[3] = n >> 24;
}
static uint32_t get32(const uint8_t *p) {
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}
static uint32_t crc32(const void *data, size_t size) {
    const uint8_t *p = data;
    uint32_t c = UINT32_MAX;
    while (size--) {
        c ^= *p++;
        for (unsigned j = 0; j < 8; j++)
            c = (c >> 1) ^ (0xedb88320u & -(c & 1u));
    }
    return ~c;
}
static bool path_for(char *out, size_t size, const char *name) {
    return snprintf(out, size, "%s/%s", directory(), name) < (int)size;
}
static bool flush_file(FILE *f) {
    return fflush(f) == 0 && (fsync(fileno(f)) == 0 || errno == ENOSYS || errno == EINVAL);
}
static bool read_state(const char *path, void *out, size_t size) {
    FILE *f = fopen(path, "rb");
    if (!f)
        return false;
    uint8_t h[12], b[1024];
    uint32_t c = UINT32_MAX;
    size_t done = 0;
    bool ok = fread(h, 1, 12, f) == 12 && get32(h) == 0x31535058u && get32(h + 4) == size;
    while (ok && done < size) {
        size_t n = size - done;
        if (n > sizeof(b))
            n = sizeof(b);
        if (fread(b, 1, n, f) != n) {
            ok = false;
            break;
        }
        for (size_t i = 0; i < n; i++) {
            c ^= b[i];
            for (unsigned j = 0; j < 8; j++)
                c = (c >> 1) ^ (0xedb88320u & -(c & 1u));
        }
        if (out)
            memcpy((uint8_t *)out + done, b, n);
        done += n;
    }
    ok = ok && ~c == get32(h + 8) && fgetc(f) == EOF;
    if (fclose(f))
        ok = false;
    return ok;
}
static bool write_state(const char *path, const void *data, size_t size, bool exclusive, uint32_t generation) {
    if (checkpoint(generation))
        return false;
    int fd = open(path, O_WRONLY | O_CREAT | (exclusive ? O_EXCL : O_TRUNC), 0600);
    if (fd < 0)
        return false;
    FILE *f = fdopen(fd, "wb");
    if (!f) {
        close(fd);
        return false;
    }
    uint8_t h[12];
    put32(h, 0x31535058u);
    put32(h + 4, (uint32_t)size);
    put32(h + 8, crc32(data, size));
    bool ok = fwrite(h, 1, 12, f) == 12 && fwrite(data, 1, size, f) == size && flush_file(f);
    if (fclose(f))
        ok = false;
    return ok && !checkpoint(generation) && read_state(path, NULL, size);
}
static bool ensure_directory(void) {
#ifndef PICO_WEREAD_HOST_TEST
    if (mkdir("/sdcard/apps", 0700) && errno != EEXIST)
        return false;
    if (mkdir("/sdcard/apps/data", 0700) && errno != EEXIST)
        return false;
#endif
    return mkdir(directory(), 0700) == 0 || errno == EEXIST;
}
static bool zeros(const uint8_t *p, size_t n) {
    while (n--)
        if (*p++)
            return false;
    return true;
}
static bool padded_text(const uint8_t *p, size_t n, bool required) {
    const uint8_t *end = memchr(p, 0, n);
    if (!end || (required && end == p))
        return false;
    size_t bytes = (size_t)(end - p);
    for (size_t i = 0; i < bytes; i++)
        if (p[i] == 9 || p[i] == 10)
            return false;
    return utf8((const char *)p, bytes) && zeros(end, n - bytes);
}
static bool valid_index(const char *path) {
    uint8_t *p = wr_alloc(WRS_INDEX_BYTES);
    if (!p)
        return false;
    bool ok = read_state(path, p, WRS_INDEX_BYTES);
    uint32_t books = ok ? get32(p + WRS_I_BOOKS) : 0, notes = 0;
    ok = ok && !memcmp(p, WRS_INDEX_MAGIC, 8) && get32(p + WRS_I_SCHEMA) == WRS_SCHEMA &&
         get32(p + WRS_I_BYTES) == WRS_INDEX_BYTES && get32(p + WRS_I_GENERATION) && books <= WRS_BOOK_MAX &&
         zeros(p + WRS_I_RESERVED, 64 - WRS_I_RESERVED);
    for (uint32_t i = 0; ok && i < books; i++) {
        const uint8_t *b = p + 64 + i * WRS_BOOK_BYTES;
        uint32_t count = get32(b + WRS_B_NOTES), chunks = get32(b + WRS_B_CHUNKS);
        ok = padded_text(b, 64, true) && id_valid((const char *)b) && padded_text(b + 64, 128, true) &&
             padded_text(b + 192, 48, false) && count <= WR_NOTE_MAX && chunks <= WRS_CHUNK_MAX &&
             ((count == 0) == (chunks == 0)) && zeros(b + 248, 8);
        for (uint32_t j = 0; ok && j < i; j++)
            if (!strcmp((const char *)b, (const char *)p + 64 + j * WRS_BOOK_BYTES))
                ok = false;
        notes += count;
    }
    ok = ok && notes == get32(p + WRS_I_NOTES) &&
         zeros(p + 64 + books * WRS_BOOK_BYTES, (WRS_BOOK_MAX - books) * WRS_BOOK_BYTES);
    free(p);
    return ok;
}
static uint32_t unused_generation(void) {
    for (unsigned attempt = 0; attempt < 16; attempt++) {
        uint32_t generation = random_generation();
        if (!generation)
            continue;
        char prefix[12];
        snprintf(prefix, sizeof(prefix), "g%08x-", (unsigned)generation);
        DIR *dir = opendir(directory());
        if (!dir)
            return 0;
        bool found = false;
        struct dirent *entry;
        while ((entry = readdir(dir)))
            if (!strncmp(entry->d_name, prefix, 10)) {
                found = true;
                break;
            }
        closedir(dir);
        if (!found)
            return generation;
    }
    return 0;
}
static const char *publish_index(const uint8_t *data, uint32_t generation) {
    char path[240], tmp[240], backup[240];
    path_for(path, sizeof(path), "index.dat");
    path_for(tmp, sizeof(tmp), "index.tmp");
    path_for(backup, sizeof(backup), "index.bak");
    if (!write_state(tmp, data, WRS_INDEX_BYTES, false, generation))
        return checkpoint(generation) ? checkpoint(generation) : "storage";
    const char *e = checkpoint(generation);
    if (e)
        return e;
    struct stat st;
    bool exists = stat(path, &st) == 0, valid = exists && valid_index(path);
    if ((e = checkpoint(generation)))
        return e;
    if (valid) {
        if (unlink(backup) && errno != ENOENT)
            return "storage";
        if ((e = checkpoint(generation)))
            return e;
        if (rename(path, backup))
            return "storage";
    } else if (exists) {
        if ((e = checkpoint(generation)))
            return e;
        if (unlink(path))
            return "storage";
    }
    e = checkpoint(generation);
    if (e) {
        if (valid && same_media(generation))
            (void)rename(backup, path);
        return e;
    }
    if (rename(tmp, path)) {
        if (valid && same_media(generation))
            (void)rename(backup, path);
        return "storage";
    }
    return NULL;
}
static const char *append_text(FILE *spool, const char *text, size_t *bytes, size_t *total) {
    char *normalized = NULL;
    size_t n = strlen(text);
    if (strchr(text, '\r')) {
        normalized = wr_alloc(n + 1);
        if (!normalized)
            return "memory";
        size_t used = 0;
        for (size_t i = 0; i < n; i++) {
            if (text[i] == '\r') {
                normalized[used++] = '\n';
                if (text[i + 1] == '\n')
                    i++;
            } else
                normalized[used++] = text[i];
        }
        normalized[used] = 0;
        text = normalized;
        n = used;
    }
    const char *error = NULL;
    if (!utf8(text, n))
        error = "malformed";
    else if (n > WRS_TEXT_BYTES * WRS_CHUNK_MAX - *bytes || n > WR_TOTAL_MAX - *total)
        error = "too_large";
    else if (n && fwrite(text, 1, n, spool) != n)
        error = "storage";
    else {
        *bytes += n;
        *total += n;
    }
    free(normalized);
    return error;
}
static const char *append_value(FILE *spool, const cJSON *v, bool optional, size_t *bytes, size_t *total) {
    if (optional && (!v || cJSON_IsNull(v)))
        return NULL;
    if (!cJSON_IsString(v) || !v->valuestring)
        return "malformed";
    return append_text(spool, v->valuestring, bytes, total);
}
static const char *append_note(FILE *f, const char *kind, const cJSON *chapter, const cJSON *quote, const cJSON *text,
                               size_t *bytes, size_t *total) {
    const char *e = append_text(f, kind, bytes, total);
    if (!e)
        e = append_text(f, "\n", bytes, total);
    if (!e && cJSON_IsString(chapter) && chapter->valuestring[0]) {
        e = append_value(f, chapter, true, bytes, total);
        if (!e)
            e = append_text(f, "\n", bytes, total);
    }
    if (!e)
        e = append_value(f, quote, true, bytes, total);
    if (!e && cJSON_IsString(text) && text->valuestring[0]) {
        e = append_text(f, "\n", bytes, total);
        if (!e)
            e = append_value(f, text, false, bytes, total);
    }
    if (!e)
        e = append_text(f, "\n\n", bytes, total);
    return e;
}
static const char *fetch_notes(const char *key, const char *book, FILE *spool, size_t *bytes, size_t *total,
                               uint32_t *notes, uint32_t generation) {
    const char *e = checkpoint(generation);
    if (e)
        return e;
    cJSON *root = request(key, "/book/bookmarklist", book, 0, &e);
    if (!root)
        return e;
    cJSON *updated = member(root, "updated"), *chapters = member(root, "chapters"), *item = NULL;
    if (!cJSON_IsArray(updated)) {
        cJSON_Delete(root);
        return "malformed";
    }
    cJSON_ArrayForEach(item, updated) {
        if ((e = checkpoint(generation)))
            break;
        int64_t type = -1;
        if (!integer(member(item, "type"), &type)) {
            e = "malformed";
            break;
        }
        if (type != 1)
            continue;
        cJSON *quote = member(item, "markText");
        if (!cJSON_IsString(quote)) {
            e = "malformed";
            break;
        }
        cJSON *chapter = NULL, *ch = NULL;
        int64_t uid = 0, other = 0;
        if (integer(member(item, "chapterUid"), &uid))
            cJSON_ArrayForEach(ch, chapters) if (integer(member(ch, "chapterUid"), &other) && other == uid) {
                chapter = member(ch, "title");
                break;
            }
        if (*notes >= WR_NOTE_MAX) {
            e = "too_large";
            break;
        }
        e = append_note(spool, "【划线】", chapter, quote, NULL, bytes, total);
        if (e)
            break;
        (*notes)++;
    }
    cJSON_Delete(root);
    if (e)
        return e;
    int64_t cursors[256], cursor = 0;
    unsigned pages = 0;
    while (pages < 256) {
        if ((e = checkpoint(generation)))
            return e;
        root = request(key, "/review/list/mine", book, cursor, &e);
        if (!root)
            return e;
        cJSON *list = member(root, "reviews");
        int64_t more = -1;
        if (!cJSON_IsArray(list) || !integer(member(root, "hasMore"), &more) || (more != 0 && more != 1)) {
            cJSON_Delete(root);
            return "malformed";
        }
        cJSON_ArrayForEach(item, list) {
            if ((e = checkpoint(generation)))
                break;
            cJSON *r = member(item, "review");
            if (!cJSON_IsObject(r) || !cJSON_IsString(member(r, "content"))) {
                e = "malformed";
                break;
            }
            if (*notes >= WR_NOTE_MAX) {
                e = "too_large";
                break;
            }
            e = append_note(spool, "【想法】", member(r, "chapterName"), member(r, "abstract"), member(r, "content"),
                            bytes, total);
            if (e)
                break;
            (*notes)++;
        }
        int64_t next = 0;
        if (!e && more) {
            if (!cJSON_GetArraySize(list) || !integer(member(root, "synckey"), &next) || next < 0 || next == cursor)
                e = "malformed";
            for (unsigned i = 0; !e && i < pages; i++)
                if (cursors[i] == next)
                    e = "malformed";
        }
        cJSON_Delete(root);
        if (e)
            return e;
        if (!more)
            return NULL;
        cursors[pages++] = cursor = next;
    }
    return "too_large";
}
static const char *write_chunks(FILE *spool, size_t text_bytes, uint32_t generation, uint32_t book_slot,
                                uint32_t *chunks, uint32_t media_generation) {
    if (fseek(spool, 0, SEEK_SET))
        return "storage";
    uint8_t *chunk = wr_alloc(WRS_CHUNK_BYTES);
    if (!chunk)
        return "memory";
    // 最大块数按实际UTF-8边界计算，避免多字节字形跨块断开。
    // Count actual UTF-8 boundaries so multibyte glyphs never straddle chunks.
    size_t remaining = text_bytes;
    uint32_t count = 0;
    const char *e = NULL;
    while (remaining) {
        size_t n = remaining > WRS_TEXT_BYTES ? WRS_TEXT_BYTES : remaining;
        if (fread(chunk, 1, n, spool) != n) {
            e = "storage";
            break;
        }
        if (remaining > n) {
            while (n && ((chunk[n - 1] & 0xc0) == 0x80))
                n--;
            if (n && chunk[n - 1] >= 0xc0)
                n--;
            size_t back = (remaining > WRS_TEXT_BYTES ? WRS_TEXT_BYTES : remaining) - n;
            if (back && fseek(spool, -(long)back, SEEK_CUR)) {
                e = "storage";
                break;
            }
        }
        if (!n || ++count > WRS_CHUNK_MAX) {
            e = "too_large";
            break;
        }
        remaining -= n;
    }
    if (!e && fseek(spool, 0, SEEK_SET))
        e = "storage";
    remaining = text_bytes;
    for (uint32_t index = 0; !e && index < count; index++) {
        if ((e = checkpoint(media_generation)))
            break;
        memset(chunk, 0, WRS_CHUNK_BYTES);
        size_t n = remaining > WRS_TEXT_BYTES ? WRS_TEXT_BYTES : remaining;
        if (fread(chunk + 64, 1, n, spool) != n) {
            e = "storage";
            break;
        }
        if (remaining > n) {
            size_t cut = n;
            while (cut && ((chunk[64 + cut - 1] & 0xc0) == 0x80))
                cut--;
            if (cut && chunk[64 + cut - 1] >= 0xc0)
                cut--;
            size_t back = n - cut;
            if (back && fseek(spool, -(long)back, SEEK_CUR)) {
                e = "storage";
                break;
            }
            n = cut;
            memset(chunk + 64 + n, 0, WRS_TEXT_BYTES - n);
        }
        memcpy(chunk, WRS_CHUNK_MAGIC, 8);
        put32(chunk + WRS_C_SCHEMA, WRS_SCHEMA);
        put32(chunk + WRS_C_BYTES, WRS_CHUNK_BYTES);
        put32(chunk + WRS_C_GENERATION, generation);
        put32(chunk + WRS_C_BOOK, book_slot);
        put32(chunk + WRS_C_INDEX, index);
        put32(chunk + WRS_C_COUNT, count);
        put32(chunk + WRS_C_TEXT, (uint32_t)n);
        char name[64], path[240];
        snprintf(name, sizeof(name), "g%08x-b%02x-c%03x.dat", (unsigned)generation, (unsigned)book_slot,
                 (unsigned)index);
        path_for(path, sizeof(path), name);
        if (!write_state(path, chunk, WRS_CHUNK_BYTES, true, media_generation))
            e = checkpoint(media_generation) ? checkpoint(media_generation) : "storage";
        remaining -= n;
    }
    free(chunk);
    *chunks = count;
    return e;
}

/* ---- 工作准入与任务 / Admission and work ---- */
static void query_reset_locked(void) {
    ps_weread_cover_clear();
    if (s_query_data) {
        wipe(s_query_data, s_query.total_bytes);
        free(s_query_data);
        s_query_data = NULL;
    }
    s_query.total_bytes = 0;
    s_query.ready = s_query.busy = false;
    s_query.error[0] = 0;
    s_query.revision++;
    if (!++s_query.request_id)
        s_query.request_id++;
}
static cJSON *query_object(const ps_weread_query_t *q) {
    cJSON *o = cJSON_CreateObject();
    bool ok = o && cJSON_AddStringToObject(o, "api_name", s_query_apis[q->kind]) &&
              cJSON_AddStringToObject(o, "skill_version", "1.0.5");
#define QSTR(name, value) (ok = ok && cJSON_AddStringToObject(o, name, value))
#define QNUM(name, value) (ok = ok && cJSON_AddNumberToObject(o, name, (double)(value)))
    if (q->kind == PS_WEREAD_SEARCH) {
        QSTR("keyword", q->keyword);
        QNUM("scope", q->scope);
    }
    if (q->book_id[0])
        QSTR("bookId", q->book_id);
    if (q->kind == PS_WEREAD_SEARCH || q->kind == PS_WEREAD_RECOMMEND ||
        q->kind == PS_WEREAD_SIMILAR || q->kind == PS_WEREAD_REVIEWS) {
        QNUM("count", q->count ? q->count : 12);
        QNUM("maxIdx", q->cursor);
    }
    if (q->kind == PS_WEREAD_SIMILAR && q->session_id[0])
        QSTR("sessionId", q->session_id);
    if (q->kind == PS_WEREAD_STATS) {
        static const char *const modes[] = {"weekly", "monthly", "annually", "overall"};
        QSTR("mode", modes[q->mode]);
        QNUM("baseTime", q->mode == PS_WEREAD_OVERALL ? 0 : q->base_time);
    }
    if (q->kind == PS_WEREAD_HIGHLIGHTS) {
        QNUM("chapterUid", q->chapter_uid);
        QNUM("synckey", q->synckey);
    }
    if (q->kind == PS_WEREAD_REVIEWS) {
        QNUM("reviewListType", q->review_type);
        QNUM("synckey", q->synckey);
    }
#undef QSTR
#undef QNUM
    if (!ok) {
        cJSON_Delete(o);
        o = NULL;
    }
    return o;
}
static const char *query_validate(const ps_weread_query_t *q) {
    if (q->bytes < sizeof(*q) || q->kind < PS_WEREAD_SEARCH || q->kind > PS_WEREAD_REVIEWS ||
        q->mode > PS_WEREAD_OVERALL || q->review_type > 4 || q->count > 12 ||
        q->cursor < 0 || q->cursor > 9007199254740991LL || q->synckey < 0 || q->synckey > 9007199254740991LL ||
        q->chapter_uid < 0 || q->chapter_uid > 9007199254740991LL || q->base_time < 0 || q->base_time > 9007199254740991LL ||
        !memchr(q->book_id, 0, sizeof(q->book_id)) || !memchr(q->keyword, 0, sizeof(q->keyword)) ||
        !memchr(q->session_id, 0, sizeof(q->session_id)))
        return "invalid_query";
    if (q->book_id[0] && !id_valid(q->book_id))
        return "invalid_query";
    if (q->kind != PS_WEREAD_SEARCH && q->kind != PS_WEREAD_RECOMMEND && q->kind != PS_WEREAD_STATS && !q->book_id[0])
        return "invalid_query";
    size_t n = strlen(q->keyword);
    if (!utf8(q->keyword, n) || (q->kind == PS_WEREAD_SEARCH && !n))
        return "invalid_query";
    for (size_t i = 0; i < n; i++)
        if ((unsigned char)q->keyword[i] < 32)
            return "invalid_query";
    for (const unsigned char *p = (const unsigned char *)q->session_id; *p; p++)
        if (*p < 33 || *p > 126)
            return "invalid_query";
    if (q->kind == PS_WEREAD_SEARCH && q->scope != 0 && q->scope != 10 && q->scope != 16 && q->scope != 14 &&
        q->scope != 6 && q->scope != 12 && q->scope != 13 && q->scope != 2 && q->scope != 4)
        return "invalid_query";
    return NULL;
}
static void finish(const char *error) {
    LOCK();
    if (error && !strcmp(error, "auth"))
        s_status->verified = false;
    snprintf(s_status->state, sizeof(s_status->state), "%s", error ? "error" : "done");
    snprintf(s_status->error, sizeof(s_status->error), "%s", error ? error : "");
    s_status->revision++;
    s_status->busy = false;
    atomic_store(&s_busy, false);
    UNLOCK();
}
static void worker(void *unused) {
    (void)unused;
    char key[PS_WEREAD_KEY_MAX + 1] = {0};
    size_t key_bytes = sizeof(key);
    const char *e = NULL;
    if (s_job_query.kind) {
        ps_weread_query_t q = s_job_query;
        cJSON *root = NULL;
        char *data = NULL;
        size_t n = 0;
        if (!nv_get("api_key", key, &key_bytes) || !key_bytes || key_bytes > sizeof(key) || key[key_bytes - 1] || !key_valid(key))
            e = "missing_key";
        else
            root = request_object(key, query_object(&q), &e);
        if (!e) {
            data = wr_alloc(WR_RESPONSE_MAX + 8u);
            if (!data)
                e = "memory";
            else if (!cJSON_PrintPreallocated(root, data, WR_RESPONSE_MAX + 8u, false) ||
                     (n = strlen(data)) >= WR_RESPONSE_MAX)
                e = "too_large";
            else if (!utf8(data, n))
                e = "malformed";
        }
        LOCK();
        if (atomic_load(&s_cancel) && !e)
            e = "cancelled";
        if (!e) {
            s_query_data = data;
            data = NULL;
            s_query.total_bytes = (uint32_t)n;
            s_query.ready = true;
            s_status->verified = true;
            s_query.revision++;
        }
        uint32_t request_id = s_query.request_id;
        UNLOCK();
        // 先交付资料；可选封面仍由同一任务下载，不延后文字显示。
        // Publish information first; the same worker then prepares the optional cover.
        if (!e && q.kind == PS_WEREAD_INFO) {
            const cJSON *cover = member(root, "cover");
            if (cJSON_IsString(cover) && cover->valuestring)
                ps_weread_cover_prepare(request_id, cover->valuestring, &s_cancel);
        }
        cJSON_Delete(root);
        LOCK();
        if (atomic_load(&s_cancel) && !e)
            e = "cancelled";
        if (e) {
            if (s_query_data) {
                wipe(s_query_data, s_query.total_bytes);
                free(s_query_data);
                s_query_data = NULL;
            }
            s_query.total_bytes = 0;
            s_query.ready = false;
            ps_weread_cover_clear();
        }
        s_query.busy = false;
        snprintf(s_query.error, sizeof(s_query.error), "%s", e ? e : "");
        s_query.revision++;
        UNLOCK();
        if (data) {
            wipe(data, n);
            free(data);
        }
        wipe(key, sizeof(key));
        finish(e);
#ifdef ESP_PLATFORM
        vTaskDelete(NULL);
#endif
        return;
    }
    ps_weread_book_t *books = wr_alloc(sizeof(ps_weread_book_t) * WRS_BOOK_MAX);
    uint32_t count = 0;
    if (!books)
        e = "memory";
    else if (!nv_get("api_key", key, &key_bytes) || !key_bytes || key_bytes > sizeof(key) || key[key_bytes - 1] ||
             !key_valid(key))
        e = "missing_key";
    if (!e)
        e = fetch_shelf(key, books, &count);
    if (!e) {
        LOCK();
        memcpy(s_status->books, books, count * sizeof(*books));
        s_status->books_count = count;
        s_status->verified = true;
        s_status->revision++;
        UNLOCK();
    }
    uint32_t media_generation = 0, generation = 0;
    uint8_t *index = NULL;
    size_t total_bytes = 0;
    bool committed = false;
    if (!e && s_job_sync) {
        if (!media(&media_generation) || !ensure_directory())
            e = "storage";
        if (!e) {
            generation = unused_generation();
            if (!generation)
                e = "storage";
            else {
                index = wr_alloc(WRS_INDEX_BYTES);
                if (!index)
                    e = "memory";
            }
        }
        if (!e) {
            memset(index, 0, WRS_INDEX_BYTES);
            memcpy(index, WRS_INDEX_MAGIC, 8);
            put32(index + WRS_I_SCHEMA, WRS_SCHEMA);
            put32(index + WRS_I_BYTES, WRS_INDEX_BYTES);
            put32(index + WRS_I_GENERATION, generation);
            put32(index + WRS_I_BOOKS, s_selected_count);
            uint64_t now = (uint64_t)time(NULL);
            put32(index + WRS_I_CREATED, (uint32_t)now);
            put32(index + WRS_I_CREATED + 4, (uint32_t)(now >> 32));
        }
        uint32_t all_notes = 0;
        for (uint32_t i = 0; !e && i < s_selected_count; i++) {
            e = checkpoint(media_generation);
            if (e)
                break;
            const ps_weread_book_t *book = NULL;
            for (uint32_t j = 0; j < count; j++)
                if (!strcmp(s_selected[i], books[j].id)) {
                    book = &books[j];
                    break;
                }
            if (!book) {
                e = "selection";
                break;
            }
            char name[64], spool_path[240];
            snprintf(name, sizeof(name), "g%08x-b%02x.tmp", (unsigned)generation, (unsigned)i);
            path_for(spool_path, sizeof(spool_path), name);
            int fd = open(spool_path, O_RDWR | O_CREAT | O_EXCL, 0600);
            if (fd < 0) {
                e = "storage";
                break;
            }
            FILE *spool = fdopen(fd, "w+b");
            if (!spool) {
                close(fd);
                e = "storage";
                break;
            }
            size_t bytes = 0;
            uint32_t notes = 0, chunks = 0;
            e = fetch_notes(key, book->id, spool, &bytes, &total_bytes, &notes, media_generation);
            if (!e && !flush_file(spool))
                e = "storage";
            if (!e)
                e = write_chunks(spool, bytes, generation, i, &chunks, media_generation);
            if (fclose(spool) && !e)
                e = "storage";
            // 移除本任务临时文件前重新确认同一张卡；不清理其他代文件。
            // Recheck card identity before removing this job's temporary; other generations stay untouched.
            if (same_media(media_generation))
                (void)unlink(spool_path);
            if (e)
                break;
            uint8_t *record = index + 64 + i * WRS_BOOK_BYTES;
            memcpy(record + WRS_B_ID, book->id, strlen(book->id));
            memcpy(record + WRS_B_TITLE, book->title, strlen(book->title));
            memcpy(record + WRS_B_AUTHOR, book->author, strlen(book->author));
            put32(record + WRS_B_NOTES, notes);
            put32(record + WRS_B_CHUNKS, chunks);
            all_notes += notes;
            LOCK();
            s_status->progress = i + 1;
            s_status->revision++;
            UNLOCK();
        }
        if (!e) {
            put32(index + WRS_I_NOTES, all_notes);
            e = publish_index(index, media_generation);
            if (!e) {
                committed = true;
                LOCK();
                s_status->synced_books = s_selected_count;
                s_status->synced_notes = all_notes;
                UNLOCK();
            }
        }
        if (!e) {
            uint8_t *selection = wr_alloc(WR_SELECTION_BYTES);
            if (selection) {
                memset(selection, 0, WR_SELECTION_BYTES);
                put32(selection, s_selected_count);
                memcpy(selection + 4, s_selected, s_selected_count * 64u);
                (void)nv_set("selected", selection, 4 + s_selected_count * 64u);
                wipe(selection, WR_SELECTION_BYTES);
                free(selection);
            }
        }
    }
    if (atomic_load(&s_cancel) && !e && !committed)
        e = "cancelled";
    free(index);
    free(books);
    wipe(key, sizeof(key));
    finish(e);
#ifdef ESP_PLATFORM
    vTaskDelete(NULL);
#endif
}
#if !defined(ESP_PLATFORM) && (defined(PICO_WEREAD_HOST_TEST) || defined(PICO_NATIVE_PREVIEW))
static void *thread_worker(void *arg) {
    worker(arg);
    return NULL;
}
#endif
static bool launch(void) {
#ifdef ESP_PLATFORM
    return xTaskCreate(worker, "weread_sync", 8192, NULL, 3, NULL) == pdPASS;
#elif defined(PICO_WEREAD_HOST_TEST) || defined(PICO_NATIVE_PREVIEW)
    pthread_t thread;
    if (pthread_create(&thread, NULL, thread_worker, NULL))
        return false;
    pthread_detach(thread);
    return true;
#else
    return false;
#endif
}
static bool admit(bool sync, const char ids[WRS_BOOK_MAX][64], uint32_t count, uint32_t auth_epoch) {
    LOCK();
    if (!s_status || s_pause_leases || atomic_load(&s_busy)) {
        UNLOCK();
        return false;
    }
    if (auth_epoch && auth_epoch != s_auth_epoch) {
        UNLOCK();
        state_error("selection");
        return false;
    }
    if (!s_status->configured) {
        UNLOCK();
        state_error("missing_key");
        return false;
    }
    if (sync && !count) {
        UNLOCK();
        state_error("selection");
        return false;
    }
    if (!wifi_ready()) {
        UNLOCK();
        state_error("offline");
        return false;
    }
    if (sync && !media(NULL)) {
        UNLOCK();
        state_error("storage");
        return false;
    }
    s_job_sync = sync;
    s_job_query.kind = 0;
    s_selected_count = count;
    if (count && ids)
        memcpy(s_selected, ids, count * 64u);
    atomic_store(&s_cancel, false);
    atomic_store(&s_busy, true);
    s_status->busy = true;
    snprintf(s_status->state, sizeof(s_status->state), "%s", sync ? "sync" : "shelf");
    s_status->error[0] = 0;
    s_status->progress = 0;
    s_status->total = count;
    s_status->synced_books = 0;
    s_status->synced_notes = 0;
    s_status->revision++;
    UNLOCK();
    if (!launch()) {
        finish("memory");
        return false;
    }
    return true;
}
void ps_weread_init(void) {
#ifdef ESP_PLATFORM
    if (!s_lock)
        s_lock = xSemaphoreCreateMutex();
    if (!s_lock)
        return;
#endif
    LOCK();
    if (!s_status) {
        s_status = wr_alloc(sizeof(*s_status));
        if (s_status) {
            memset(s_status, 0, sizeof(*s_status));
            snprintf(s_status->state, sizeof(s_status->state), "idle");
            size_t n = 0;
            s_status->configured = nv_get("api_key", NULL, &n) && n >= 9 && n <= PS_WEREAD_KEY_MAX + 1;
            s_status->revision = 1;
        }
    }
    UNLOCK();
}
bool ps_weread_busy(void) { return atomic_load(&s_busy); }
void ps_weread_cancel(void) {
#ifdef ESP_PLATFORM
    if (!s_lock) {
        atomic_store(&s_cancel, true);
        return;
    }
#endif
    LOCK();
    atomic_store(&s_cancel, true);
    if (s_query.busy && s_query.ready) {
        wipe(s_query_data, s_query.total_bytes);
        free(s_query_data);
        s_query_data = NULL;
        s_query.total_bytes = 0;
        s_query.ready = false;
        snprintf(s_query.error, sizeof(s_query.error), "cancelled");
        s_query.revision++;
        ps_weread_cover_clear();
    }
    UNLOCK();
}
bool ps_weread_pause(void) {
#ifdef ESP_PLATFORM
    if (!s_lock)
        return false;
#endif
    LOCK();
    bool ok = !atomic_load(&s_busy) && s_pause_leases < UINT16_MAX;
    if (ok)
        s_pause_leases++;
    UNLOCK();
    return ok;
}
void ps_weread_resume(void) {
#ifdef ESP_PLATFORM
    if (!s_lock)
        return;
#endif
    LOCK();
    if (s_pause_leases)
        s_pause_leases--;
    UNLOCK();
}
void ps_weread_get_status(ps_weread_status_t *out) {
    if (!out)
        return;
    memset(out, 0, sizeof(*out));
#ifdef ESP_PLATFORM
    if (!s_lock)
        return;
#endif
    LOCK();
    if (s_status)
        memcpy(out, s_status, sizeof(*out));
    UNLOCK();
    out->wifi = wifi_ready();
}
bool ps_weread_sync_saved(void) {
    uint8_t *data = wr_alloc(WR_SELECTION_BYTES);
    if (!data) {
        state_error("memory");
        return false;
    }
    size_t n = WR_SELECTION_BYTES;
    char (*ids)[64] = (char (*)[64])(data + 4);
    LOCK();
    if (s_pause_leases || atomic_load(&s_busy)) {
        UNLOCK();
        wipe(data, WR_SELECTION_BYTES);
        free(data);
        return false;
    }
    uint32_t auth_epoch = s_auth_epoch;
    bool valid = nv_get("selected", data, &n) && n >= 4;
    UNLOCK();
    uint32_t count = valid ? get32(data) : 0;
    valid = valid && count && count <= WRS_BOOK_MAX && n == 4 + count * 64u;
    for (uint32_t i = 0; valid && i < count; i++)
        if (!memchr(ids[i], 0, 64) || !id_valid(ids[i]))
            valid = false;
    for (uint32_t i = 0; valid && i < count; i++)
        for (uint32_t j = 0; j < i; j++)
            if (!strcmp(ids[i], ids[j]))
                valid = false;
    bool ok = valid && admit(true, (const char (*)[64])ids, count, auth_epoch);
    wipe(data, WR_SELECTION_BYTES);
    free(data);
    if (!valid)
        state_error("selection");
    return ok;
}

bool ps_weread_query(const ps_weread_query_t *request, ps_weread_query_status_t *out) {
    if (!out)
        return false;
    ps_weread_init();
    if (!s_status) {
        memset(out, 0, sizeof(*out));
        out->bytes = sizeof(*out);
        snprintf(out->error, sizeof(out->error), "memory");
        return false;
    }
    const char *error = request ? query_validate(request) : NULL;
    bool started = false;
    LOCK();
    if (request && !error) {
        if (!s_status)
            error = "memory";
        else if (s_pause_leases || atomic_load(&s_busy))
            error = "busy";
        else if (!s_status->configured)
            error = "missing_key";
        else if (!wifi_ready())
            error = "offline";
        else {
            query_reset_locked();
            s_query.kind = request->kind;
            s_query.busy = true;
            s_job_query = *request;
            atomic_store(&s_cancel, false);
            atomic_store(&s_busy, true);
            s_status->busy = true;
            s_status->error[0] = 0;
            snprintf(s_status->state, sizeof(s_status->state), "query");
            s_status->revision++;
            started = true;
        }
    }
    *out = s_query;
    out->bytes = sizeof(*out);
    if (error)
        snprintf(out->error, sizeof(out->error), "%s", error);
    UNLOCK();
    if (started && !launch()) {
        LOCK();
        s_query.busy = false;
        snprintf(s_query.error, sizeof(s_query.error), "memory");
        s_query.revision++;
        *out = s_query;
        out->bytes = sizeof(*out);
        UNLOCK();
        finish("memory");
        return false;
    }
    return !error;
}
bool ps_weread_query_read(uint32_t request_id, uint32_t offset, void *out, size_t capacity, size_t *read) {
    if (read)
        *read = 0;
    if (!out || !read || !capacity || !request_id)
        return false;
    ps_weread_init();
    if (!s_status)
        return false;
    LOCK();
    bool ok = request_id == s_query.request_id && s_query.ready && s_query_data && offset <= s_query.total_bytes;
    if (ok) {
        size_t n = s_query.total_bytes - offset;
        if (n > capacity)
            n = capacity;
        memcpy(out, s_query_data + offset, n);
        *read = n;
    }
    UNLOCK();
    return ok;
}

/* ---- 已配对管理API / Paired management API ---- */
static int print_json(cJSON *o, char *out, size_t capacity, int status) {
    bool ok = o && cJSON_PrintPreallocated(o, out, (int)capacity, false);
    cJSON_Delete(o);
    if (!ok) {
        snprintf(out, capacity, "{\"error\":\"output_limit\"}");
        return 507;
    }
    return status;
}
static int query_status_json(const ps_weread_query_status_t *s, char *out, size_t capacity, int status) {
    cJSON *o = cJSON_CreateObject();
    if (o) {
        cJSON_AddNumberToObject(o, "request_id", s->request_id);
        cJSON_AddNumberToObject(o, "revision", s->revision);
        cJSON_AddNumberToObject(o, "kind", s->kind);
        cJSON_AddNumberToObject(o, "total_bytes", s->total_bytes);
        cJSON_AddBoolToObject(o, "busy", s->busy);
        cJSON_AddBoolToObject(o, "ready", s->ready);
        cJSON_AddStringToObject(o, "error", s->error);
    }
    return print_json(o, out, capacity, status);
}
static bool query_fields(const cJSON *o, const char *const *allowed, size_t count) {
    const cJSON *v = NULL;
    cJSON_ArrayForEach(v, o) {
        size_t at;
        for (at = 0; at < count && strcmp(v->string, allowed[at]); at++) {}
        if (at == count)
            return false;
    }
    return true;
}
static bool query_number(const cJSON *o, const char *name, int64_t *out) {
    const cJSON *v = member(o, name);
    return !v || (cJSON_IsNumber(v) && integer(v, out) && *out >= 0);
}
static bool query_parse(const cJSON *o, ps_weread_query_t *q) {
    static const char *const fields[] = {"kind", "book_id", "keyword", "session_id", "scope", "count", "mode", "review_type", "cursor", "synckey", "chapter_uid", "base_time"};
    memset(q, 0, sizeof(*q));
    q->bytes = sizeof(*q);
    q->mode = PS_WEREAD_MONTHLY;
    const cJSON *kind = member(o, "kind");
    if (!query_fields(o, fields, sizeof(fields) / sizeof(*fields)) || !cJSON_IsString(kind))
        return false;
    for (uint32_t i = PS_WEREAD_SEARCH; i <= PS_WEREAD_REVIEWS; i++)
        if (!strcmp(kind->valuestring, s_query_names[i]))
            q->kind = i;
    const cJSON *v = member(o, "book_id");
    if (v && !copy_text(q->book_id, sizeof(q->book_id), v, false))
        return false;
    v = member(o, "keyword");
    if (v && !copy_text(q->keyword, sizeof(q->keyword), v, false))
        return false;
    v = member(o, "session_id");
    if (v && !copy_text(q->session_id, sizeof(q->session_id), v, false))
        return false;
    for (size_t i = 4; i < 8; i++) {
        int64_t n = i == 6 ? PS_WEREAD_MONTHLY : 0;
        if (!query_number(o, fields[i], &n) || n > UINT32_MAX)
            return false;
        uint32_t *dest[] = {&q->scope, &q->count, &q->mode, &q->review_type};
        *dest[i - 4] = (uint32_t)n;
    }
    return query_number(o, "cursor", &q->cursor) && query_number(o, "synckey", &q->synckey) &&
           query_number(o, "chapter_uid", &q->chapter_uid) && query_number(o, "base_time", &q->base_time) && !query_validate(q);
}
static int query_result_json(const cJSON *o, char *out, size_t capacity) {
    static const char *const fields[] = {"request_id", "offset", "limit"};
    int64_t id = 0, offset = 0, limit = 4096;
    const char *error = NULL;
    int status = 400;
    if (!query_fields(o, fields, 3) || !query_number(o, "request_id", &id) || !id || id > UINT32_MAX ||
        !query_number(o, "offset", &offset) || offset > UINT32_MAX || !query_number(o, "limit", &limit) || !limit || limit > 8192)
        error = "invalid_query";
    char *chunk = !error ? wr_alloc((size_t)limit + 1) : NULL;
    if (!error && !chunk) {
        error = "memory";
        status = 503;
    }
    uint32_t next = (uint32_t)offset, total = 0;
    LOCK();
    if (!error && ((uint32_t)id != s_query.request_id || !s_query.ready || !s_query_data)) {
        error = "stale_query";
        status = 409;
    }
    if (!error) {
        total = s_query.total_bytes;
        if ((uint64_t)offset > total || (offset < total && ((unsigned char)s_query_data[offset] & 0xc0) == 0x80))
            error = "invalid_query";
        else {
            next = (uint32_t)offset + ((uint64_t)offset + (uint64_t)limit < total ? (uint32_t)limit : total - (uint32_t)offset);
            while (next < total && next > (uint32_t)offset && ((unsigned char)s_query_data[next] & 0xc0) == 0x80)
                next--;
            if (next == (uint32_t)offset && next != total)
                error = "invalid_query";
            else {
                memcpy(chunk, s_query_data + offset, next - (uint32_t)offset);
                chunk[next - (uint32_t)offset] = 0;
            }
        }
    }
    UNLOCK();
    cJSON *result = cJSON_CreateObject();
    if (result) {
        if (error)
            cJSON_AddStringToObject(result, "error", error);
        else {
            cJSON_AddNumberToObject(result, "request_id", (double)id);
            cJSON_AddNumberToObject(result, "offset", (double)offset);
            cJSON_AddNumberToObject(result, "next_offset", next);
            cJSON_AddNumberToObject(result, "total_bytes", total);
            cJSON_AddStringToObject(result, "chunk", chunk);
        }
    }
    free(chunk);
    return print_json(result, out, capacity, error ? status : 200);
}
static int status_json(char *out, size_t capacity, int status, const char *error) {
    cJSON *o = cJSON_CreateObject();
    if (!o) {
        snprintf(out, capacity, "{\"error\":\"memory\"}");
        return 503;
    }
    LOCK();
    if (s_status) {
        cJSON_AddBoolToObject(o, "configured", s_status->configured);
        cJSON_AddBoolToObject(o, "verified", s_status->verified);
        cJSON_AddBoolToObject(o, "busy", atomic_load(&s_busy));
        cJSON_AddStringToObject(o, "state", s_status->state);
        cJSON_AddStringToObject(o, "error", error ? error : s_status->error);
        cJSON_AddNumberToObject(o, "revision", s_status->revision);
        cJSON_AddNumberToObject(o, "books_count", s_status->books_count);
        cJSON_AddNumberToObject(o, "synced_books", s_status->synced_books);
        cJSON_AddNumberToObject(o, "synced_notes", s_status->synced_notes);
        cJSON_AddNumberToObject(o, "progress", s_status->progress);
        cJSON_AddNumberToObject(o, "total", s_status->total);
        cJSON *books = cJSON_AddArrayToObject(o, "books");
        for (uint32_t i = 0; books && i < s_status->books_count; i++) {
            cJSON *b = cJSON_CreateObject();
            if (!b)
                break;
            cJSON_AddStringToObject(b, "id", s_status->books[i].id);
            cJSON_AddStringToObject(b, "title", s_status->books[i].title);
            cJSON_AddStringToObject(b, "author", s_status->books[i].author);
            cJSON_AddItemToArray(books, b);
        }
    }
    UNLOCK();
    cJSON_AddBoolToObject(o, "wifi", wifi_ready());
    bool ok = cJSON_PrintPreallocated(o, out, (int)capacity, false);
    cJSON_Delete(o);
    if (!ok) {
        snprintf(out, capacity, "{\"error\":\"too_large\"}");
        return 507;
    }
    return status;
}
int ps_weread_handle(const char *method, const char *path, const char *type, const void *body, size_t bytes, char *out,
                     size_t capacity) {
    const char *prefix = "/api/manage/pico/weread/";
    if (!path || strncmp(path, prefix, strlen(prefix)))
        return 0;
    if (!method || !out || capacity < 64)
        return 500;
    ps_weread_init();
    if (!s_status) {
        snprintf(out, capacity, "{\"error\":\"memory\"}");
        return 503;
    }
    const char *action = path + strlen(prefix);
    if (!strcmp(action, "status"))
        return status_json(out, capacity, !strcmp(method, "GET") ? 200 : 405, !strcmp(method, "GET") ? NULL : "method");
    if (!strcmp(action, "query") && !strcmp(method, "GET")) {
        ps_weread_query_status_t query;
        (void)ps_weread_query(NULL, &query);
        return query_status_json(&query, out, capacity, 200);
    }
    if (strcmp(method, "POST"))
        return status_json(out, capacity, 405, "method");
    if (!type || strncmp(type, "application/json", 16) || (type[16] && type[16] != ';' && type[16] != ' '))
        return status_json(out, capacity, 415, "content_type");
    cJSON *o = parse(body, bytes);
    if (!o)
        return status_json(out, capacity, 400, "malformed");
    if (!strcmp(action, "query") || !strcmp(action, "result")) {
        int result;
        if (!strcmp(action, "result"))
            result = query_result_json(o, out, capacity);
        else {
            ps_weread_query_t q;
            ps_weread_query_status_t query;
            bool valid = query_parse(o, &q);
            bool accepted = valid && ps_weread_query(&q, &query);
            if (!valid) {
                (void)ps_weread_query(NULL, &query);
                snprintf(query.error, sizeof(query.error), "invalid_query");
            }
            result = query_status_json(&query, out, capacity, accepted ? 202 : !strcmp(query.error, "busy") ? 409 :
                                       !strcmp(query.error, "memory") ? 503 : 400);
        }
        cJSON *secret = member(o, "api_key");
        if (cJSON_IsString(secret) && secret->valuestring)
            wipe(secret->valuestring, strlen(secret->valuestring));
        cJSON_Delete(o);
        return result;
    }
    int status = 200;
    char admission_error[24] = {0};
    const char *error = NULL;
    if (!strcmp(action, "authorize") || !strcmp(action, "config")) {
        cJSON *v = member(o, "api_key");
        char key[PS_WEREAD_KEY_MAX + 1] = {0};
        if (!cJSON_IsString(v) || !v->valuestring)
            error = "invalid_key";
        else {
            const char *s = v->valuestring;
            while (*s == ' ' || *s == '\t' || *s == '\r' || *s == '\n')
                s++;
            size_t n = strlen(s);
            while (n && (s[n - 1] == ' ' || s[n - 1] == '\t' || s[n - 1] == '\r' || s[n - 1] == '\n'))
                n--;
            if (n > PS_WEREAD_KEY_MAX)
                error = "invalid_key";
            else {
                memcpy(key, s, n);
                if (!key_valid(key))
                    error = "invalid_key";
            }
        }
        if (!error) {
            LOCK();
            if (s_pause_leases || atomic_load(&s_busy))
                error = "busy";
            else if (!nv_authorize(key))
                error = "storage";
            else {
                s_auth_epoch++;
                query_reset_locked();
                s_status->configured = true;
                s_status->verified = false;
                s_status->books_count = 0;
                s_status->error[0] = 0;
                snprintf(s_status->state, sizeof(s_status->state), "idle");
                s_status->revision++;
            }
            UNLOCK();
        }
        wipe(key, sizeof(key));
    } else if (!strcmp(action, "clear")) {
        LOCK();
        if (s_pause_leases || atomic_load(&s_busy))
            error = "busy";
        else if (!nv_clear())
            error = "storage";
        else {
            s_auth_epoch++;
            query_reset_locked();
            s_status->configured = false;
            s_status->verified = false;
            s_status->books_count = 0;
            snprintf(s_status->state, sizeof(s_status->state), "idle");
            s_status->error[0] = 0;
            s_status->revision++;
        }
        UNLOCK();
    } else if (!strcmp(action, "cancel")) {
        ps_weread_cancel();
    } else if (!strcmp(action, "shelf")) {
        if (!admit(false, NULL, 0, 0)) {
            LOCK();
            snprintf(admission_error, sizeof(admission_error), "%s",
                     s_pause_leases || atomic_load(&s_busy) ? "busy"
                     : s_status->error[0]                   ? s_status->error
                                                            : "selection");
            UNLOCK();
            error = admission_error;
        }
    } else if (!strcmp(action, "sync")) {
        cJSON *list = member(o, "book_ids");
        char (*ids)[64] = wr_alloc(WRS_BOOK_MAX * 64u);
        uint32_t count = 0;
        if (!ids)
            error = "memory";
        else if (!cJSON_IsArray(list) || cJSON_GetArraySize(list) < 1 || cJSON_GetArraySize(list) > (int)WRS_BOOK_MAX)
            error = "selection";
        else {
            memset(ids, 0, WRS_BOOK_MAX * 64u);
            cJSON *id = NULL;
            cJSON_ArrayForEach(id, list) {
                if (!copy_text(ids[count], 64, id, false) || !id_valid(ids[count])) {
                    error = "selection";
                    break;
                }
                for (uint32_t j = 0; j < count; j++)
                    if (!strcmp(ids[j], ids[count]))
                        error = "selection";
                if (error)
                    break;
                count++;
            }
            if (!error && !admit(true, (const char (*)[64])ids, count, 0)) {
                LOCK();
                snprintf(admission_error, sizeof(admission_error), "%s",
                         s_pause_leases || atomic_load(&s_busy) ? "busy"
                         : s_status->error[0]                   ? s_status->error
                                                                : "selection");
                UNLOCK();
                error = admission_error;
            }
        }
        free(ids);
    } else {
        status = 404;
        error = "route";
    }
    // cJSON含手机发来的密钥，释放前清零值；响应从不回显密钥。
    // Wipe the submitted key before freeing cJSON; responses never echo it.
    cJSON *secret = member(o, "api_key");
    if (cJSON_IsString(secret) && secret->valuestring)
        wipe(secret->valuestring, strlen(secret->valuestring));
    cJSON_Delete(o);
    if (error && status == 200)
        status = !strcmp(error, "busy") ? 409 : !strcmp(error, "storage") ? 503 : 400;
    return status_json(out, capacity, status, error);
}

/*
 * SPDX-FileCopyrightText: 2026 XH Lab
 * SPDX-License-Identifier: Apache-2.0
 * 懒启动网络工作器，使用ESP-IDF TLS；电脑端以独立curl/OpenSSL进程复用同一模型。
 * Lazy network worker with ESP-IDF TLS; host curl/OpenSSL processes use the identical shared model.
 * 冻结：不写TF凭据或保存账户密码；必须验证CA/主机名，禁止重定向、伪登录和打印控制。
 * Frozen: Never store TF credentials/passwords; verify CA/hostname, reject redirects, fabricated login and printer control.
 * 修复：系统时间未就绪与网络/TLS故障分开报告，证书校验始终保留。
 * Fix: Report an unset system clock separately from network/TLS failures; certificate checks remain enabled.
 */
#define _POSIX_C_SOURCE 200809L
#define _DARWIN_C_SOURCE 1
#define _DEFAULT_SOURCE 1
#include "read_pico_home_link.h"
#include "bambu_auth_internal.h"
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef ESP_PLATFORM
#include "esp_crt_bundle.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_tls.h"
#include "mbedtls/ssl.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "nvs.h"
#else
#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <signal.h>
#include <spawn.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
extern char **environ;
#endif

typedef struct {
    rp_home_config_t config;
    rp_home_snapshot_t snapshot;
    uint32_t generation;
    bool initialized, started, queued, suspended, online, at_home, busy, control_pending;
    rp_home_entity_t control_entity;
    char control_action[24];
    float control_value;
    rp_home_bambu_session_t auth;
    rp_home_bambu_input_t auth_input;
    bool auth_pending;
    uint8_t auth_previous_phase;
    int64_t auth_expires, auth_retry;
} runtime_t;
static runtime_t *state;
#ifdef ESP_PLATFORM
static SemaphoreHandle_t state_lock;
static TaskHandle_t worker_handle;
#else
static pthread_mutex_t state_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t worker_ready = PTHREAD_COND_INITIALIZER;
#endif
static void *memory(size_t bytes) {
#ifdef ESP_PLATFORM
    return heap_caps_calloc(1, bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
#else
    return calloc(1, bytes);
#endif
}
static void lock(void) {
#ifdef ESP_PLATFORM
    xSemaphoreTake(state_lock, portMAX_DELAY);
#else
    pthread_mutex_lock(&state_lock);
#endif
}
static void unlock(void) {
#ifdef ESP_PLATFORM
    xSemaphoreGive(state_lock);
#else
    pthread_mutex_unlock(&state_lock);
#endif
}
static void pause_ms(unsigned n) {
#ifdef ESP_PLATFORM
    vTaskDelay(pdMS_TO_TICKS(n));
#else
    struct timespec t = {n / 1000, (long)(n % 1000) * 1000000};
    while (nanosleep(&t, &t) && errno == EINTR) {
    }
#endif
}
// 空闲及休眠时不轮询，避免后台任务每100ms唤醒CPU。
// Block while idle/suspended rather than waking the CPU every 100 ms.
static int64_t now_ms(void);
static void auth_expire_locked(void);
static void wait_for_work(void) {
#ifdef ESP_PLATFORM
    lock(); auth_expire_locked(); int64_t left = state->auth_expires ? state->auth_expires - now_ms() : 0; unlock();
    TickType_t wait = left > 0 ? pdMS_TO_TICKS((uint32_t)left + portTICK_PERIOD_MS - 1u) : portMAX_DELAY;
    ulTaskNotifyTake(pdTRUE, wait);
#else
    lock();
    while (state->suspended || (!state->queued && !state->auth_pending)) {
        auth_expire_locked();
        if (state->auth_expires) {
            int64_t left = state->auth_expires - now_ms();
            if (left <= 0) continue;
            struct timespec deadline; clock_gettime(CLOCK_REALTIME, &deadline);
            deadline.tv_sec += (time_t)(left / 1000); deadline.tv_nsec += (long)(left % 1000) * 1000000L;
            if (deadline.tv_nsec >= 1000000000L) { deadline.tv_sec++; deadline.tv_nsec -= 1000000000L; }
            (void)pthread_cond_timedwait(&worker_ready, &state_lock, &deadline);
        } else pthread_cond_wait(&worker_ready, &state_lock);
    }
    unlock();
#endif
}
static void notify_worker(void) {
#ifdef ESP_PLATFORM
    if (worker_handle)
        xTaskNotifyGive(worker_handle);
#else
    pthread_cond_signal(&worker_ready);
#endif
}
static int64_t now_ms(void) {
#ifdef ESP_PLATFORM
    return esp_timer_get_time() / 1000;
#else
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (int64_t)t.tv_sec * 1000 + t.tv_nsec / 1000000;
#endif
}
#define BAMBU_SESSION_MS (5 * 60 * 1000)
#define BAMBU_RESEND_MS (60 * 1000)
// 退出、到期和重开账户均换会话；在途TLS结果只能提交到其原会话。
// Exit, expiry and new-account login advance the session; in-flight TLS results can commit only to that session.
static void auth_clear_locked(unsigned phase) {
    uint32_t session = state->auth.public.session + 1u, revision = state->auth.public.revision + 1u;
    rp_home_bambu_wipe(&state->auth, sizeof(state->auth));
    rp_home_bambu_wipe(&state->auth_input, sizeof(state->auth_input));
    state->auth_pending = false; state->auth_expires = state->auth_retry = 0;
    state->auth.public.session = session ? session : 1u; state->auth.public.revision = revision;
    state->auth.public.phase = (uint8_t)phase; state->auth.public.result = RP_HOME_EMPTY;
}
static void auth_expire_locked(void) {
    if (state->auth_expires && now_ms() >= state->auth_expires) auth_clear_locked(RP_BAMBU_EXPIRED);
}
static bool auth_same_account(const rp_home_printer_config_t *a, const rp_home_printer_config_t *b) {
    return a->source == RP_HOME_BAMBU_CLOUD && b->source == RP_HOME_BAMBU_CLOUD && a->region == b->region && !strcmp(a->username, b->username);
}
// 同区域/账号只公开一个稳定代表，不将令牌差异变成重复账户或回传凭据。
// Expose one stable representative per region/identity, without splitting token variants or returning credentials.
static void auth_saved_accounts_locked(rp_home_bambu_state_t *out) {
    out->config_revision = state->generation; out->saved_count = 0; memset(out->saved_accounts, 0, sizeof(out->saved_accounts));
    for (unsigned i = 0; i < RP_HOME_PRINTERS; ++i) {
        const rp_home_printer_config_t *p = &state->config.printers[i];
        if (p->source != RP_HOME_BAMBU_CLOUD) continue;
        unsigned group = 0;
        while (group < out->saved_count && !auth_same_account(p, &state->config.printers[out->saved_accounts[group].slot])) ++group;
        if (group < out->saved_count) { out->saved_accounts[group].printer_count++; continue; }
        rp_home_bambu_saved_account_t *a = &out->saved_accounts[out->saved_count++];
        a->slot = (uint8_t)i; a->region = p->region; a->printer_count = 1;
        snprintf(a->name, sizeof(a->name), "%s", *p->name ? p->name : p->serial);
    }
}
static void auth_changed_locked(void) {
    auth_clear_locked(RP_BAMBU_ERROR); state->auth.public.result = RP_HOME_INVALID; state->auth.public.error = RP_BAMBU_ERROR_ACCOUNT_CHANGED;
}
static bool settings(bool writing, rp_home_config_t *c, uint32_t *stored_version) {
#ifdef ESP_PLATFORM
    nvs_handle_t h;
    if (nvs_open("pico_home_link", writing ? NVS_READWRITE : NVS_READONLY, &h) != ESP_OK)
        return false;
    unsigned char *packed = memory(RP_HOME_CONFIG_PACK_LIMIT);
    if (!packed) {
        nvs_close(h);
        return false;
    }
    size_t bytes = writing ? rp_home_config_pack(c, packed, RP_HOME_CONFIG_PACK_LIMIT) : RP_HOME_CONFIG_PACK_LIMIT;
    esp_err_t e =
        bytes ? (writing ? nvs_set_blob(h, "settings", packed, bytes) : nvs_get_blob(h, "settings", packed, &bytes))
              : ESP_ERR_INVALID_SIZE;
    if (writing && e == ESP_OK)
        e = nvs_commit(h);
    nvs_close(h);
    bool ok = e == ESP_OK && (writing || rp_home_config_unpack_version(packed, bytes, c, stored_version));
    memset(packed, 0, RP_HOME_CONFIG_PACK_LIMIT);
    free(packed);
    return ok;
#else
    // 预览可指定会话私有文件；不使用TF沙箱或公共临时目录保存凭据。
    // Preview can specify a private session file; never persist credentials in TF sandboxes or public temporary files.
    const char *path = getenv("RPICO_HOME_LINK_CONFIG");
    if (!path || !*path)
        return writing;
    if (writing) {
        char temp[PATH_MAX];
        int n = snprintf(temp, sizeof(temp), "%s.tmp", path);
        if (n < 0 || (size_t)n >= sizeof(temp))
            return false;
        int fd = open(temp, O_WRONLY | O_CREAT | O_TRUNC | O_NOFOLLOW, 0600);
        if (fd < 0)
            return false;
        bool ok = fchmod(fd, 0600) == 0;
        size_t done = 0;
        while (ok && done < sizeof(*c)) {
            ssize_t nwrite = write(fd, (const char *)c + done, sizeof(*c) - done);
            if (nwrite <= 0) {
                ok = false;
                break;
            }
            done += (size_t)nwrite;
        }
        if (ok && fsync(fd) != 0)
            ok = false;
        if (close(fd) != 0)
            ok = false;
        if (ok)
            ok = rename(temp, path) == 0;
        if (!ok)
            unlink(temp);
        return ok;
    }
    int fd = open(path, O_RDONLY | O_NOFOLLOW);
    if (fd < 0)
        return false;
    struct stat st;
    bool ok = fstat(fd, &st) == 0 && S_ISREG(st.st_mode) && st.st_size == (off_t)sizeof(*c) && (st.st_mode & 0077) == 0;
    size_t done = 0;
    while (ok && done < sizeof(*c)) {
        ssize_t nread = read(fd, (char *)c + done, sizeof(*c) - done);
        if (nread <= 0) {
            ok = false;
            break;
        }
        done += (size_t)nread;
    }
    close(fd);
    if (ok && stored_version) *stored_version = c->version;
    return ok;
#endif
}

#ifndef ESP_PLATFORM
// 离线验收显式禁用宿主网络；共享控制器与工作线程仍返回真实离线结果。
// Explicit offline acceptance disables host networking; the shared controller/worker still reports offline.
static bool host_transport_offline(void) {
    const char *offline = getenv("RPICO_HOME_LINK_OFFLINE");
    return offline && !strcmp(offline, "1");
}
typedef struct {
    pid_t pid;
    int input, output, error;
} process_t;
static bool spawn_process(process_t *p, const char *program, char *const argv[]) {
    int in[2], out[2], error[2];
    if (pipe(in) != 0)
        return false;
    if (pipe(out) != 0) {
        close(in[0]);
        close(in[1]);
        return false;
    }
    if (pipe(error) != 0) {
        close(in[0]);
        close(in[1]);
        close(out[0]);
        close(out[1]);
        return false;
    }
    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    posix_spawn_file_actions_adddup2(&actions, in[0], STDIN_FILENO);
    posix_spawn_file_actions_adddup2(&actions, out[1], STDOUT_FILENO);
    posix_spawn_file_actions_adddup2(&actions, error[1], STDERR_FILENO);
    posix_spawn_file_actions_addclose(&actions, error[0]);
    posix_spawn_file_actions_addclose(&actions, error[1]);
    posix_spawn_file_actions_addclose(&actions, in[1]);
    posix_spawn_file_actions_addclose(&actions, out[0]);
    posix_spawn_file_actions_addclose(&actions, in[0]);
    posix_spawn_file_actions_addclose(&actions, out[1]);
    int result = posix_spawnp(&p->pid, program, &actions, NULL, argv, environ);
    posix_spawn_file_actions_destroy(&actions);
    close(in[0]);
    close(out[1]);
    close(error[1]);
    if (result) {
        close(in[1]);
        close(out[0]);
        close(error[0]);
        return false;
    }
    p->input = in[1];
    p->output = out[0];
    p->error = error[0];
    fcntl(p->error, F_SETFL, fcntl(p->error, F_GETFL) | O_NONBLOCK);
#ifdef F_SETNOSIGPIPE
    fcntl(p->input, F_SETNOSIGPIPE, 1);
#endif
    return true;
}
static bool write_all(int fd, const void *data, size_t bytes) {
    const char *p = data;
    while (bytes) {
        ssize_t n = write(fd, p, bytes);
        if (n < 0 && errno == EINTR)
            continue;
        if (n <= 0)
            return false;
        p += n;
        bytes -= (size_t)n;
    }
    return true;
}
static int process_close(process_t *p, bool stop) {
    if (p->input >= 0)
        close(p->input);
    if (p->output >= 0)
        close(p->output);
    if (p->error >= 0)
        close(p->error);
    if (stop)
        kill(p->pid, SIGTERM);
    int status = 0;
    int64_t deadline = now_ms() + 1000;
    pid_t done = 0;
    while ((done = waitpid(p->pid, &status, WNOHANG)) == 0 && now_ms() < deadline)
        pause_ms(10);
    if (done == 0) {
        kill(p->pid, SIGKILL);
        while (waitpid(p->pid, &status, 0) < 0 && errno == EINTR) {
        }
    }
    return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}
static bool config_line(FILE *f, const char *key, const char *value) {
    if (fprintf(f, "%s = \"", key) < 0)
        return false;
    for (const unsigned char *p = (const unsigned char *)value; *p; p++) {
        if (*p == '\\' || *p == '"') {
            if (fputc('\\', f) == EOF)
                return false;
        }
        if (*p == '\r' || *p == '\n')
            return false;
        if (fputc(*p, f) == EOF)
            return false;
    }
    return fputs("\"\n", f) != EOF;
}
#endif
/* ---- 安全连接错误 / Secure transport errors ---- */
#ifdef ESP_PLATFORM
static bool tls_clock_ready(void) {
    time_t now = time(NULL);
    return now >= 1700000000 && now <= 4102444799LL;
}
static rp_home_result_t tls_failure(int error, int code, int flags) {
    if (flags)
        return RP_HOME_TLS;
    if (error == ESP_ERR_NO_MEM || code == MBEDTLS_ERR_SSL_ALLOC_FAILED)
        return RP_HOME_NOMEM;
    if (code == MBEDTLS_ERR_SSL_TIMEOUT || error == ESP_ERR_ESP_TLS_CANNOT_RESOLVE_HOSTNAME ||
        error == ESP_ERR_ESP_TLS_CANNOT_CREATE_SOCKET || error == ESP_ERR_ESP_TLS_FAILED_CONNECT_TO_HOST ||
        error == ESP_ERR_ESP_TLS_SOCKET_SETOPT_FAILED || error == ESP_ERR_ESP_TLS_CONNECTION_TIMEOUT ||
        error == ESP_ERR_ESP_TLS_TCP_CLOSED_FIN || error == ESP_ERR_ESP_TLS_SERVER_HANDSHAKE_TIMEOUT)
        return RP_HOME_OFFLINE;
    if (code || (error >= ESP_ERR_MBEDTLS_CERT_PARTLY_OK && error <= ESP_ERR_MBEDTLS_SSL_READ_FAILED))
        return RP_HOME_TLS;
    return RP_HOME_OFFLINE;
}
#endif
static rp_home_result_t http_request(void *unused, const rp_home_http_request_t *r, char *response, size_t capacity,
                                     size_t *used, int *status) {
    (void)unused;
    *used = 0;
    *status = 0;
    bool bambu = !strncmp(r->url, "https://api.bambulab.com/", 25) || !strncmp(r->url, "https://api.bambulab.cn/", 24) ||
        !strcmp(r->url, "https://bambulab.cn/api/v1/user-service/user/sendsmscode");
#ifdef ESP_PLATFORM
    // 正确的证书有效期校验需要已同步的时间；失败不回退不安全连接。
    // Certificate validity requires synchronized time; failure never falls back to insecure TLS.
    if (!strncmp(r->url, "https://", 8) && !tls_clock_ready())
        return RP_HOME_TIME;
    esp_http_client_config_t cfg = {.url = r->url,
                                    .timeout_ms = 8000,
                                    .buffer_size = 1024,
                                    .crt_bundle_attach = esp_crt_bundle_attach,
                                    .disable_auto_redirect = true,
                                    .method = !strcmp(r->method, "POST") ? HTTP_METHOD_POST : HTTP_METHOD_GET};
    esp_http_client_handle_t h = esp_http_client_init(&cfg);
    if (!h)
        return RP_HOME_NOMEM;
    if (bambu) { esp_http_client_set_header(h, "User-Agent", "ReadPico/0.25"); esp_http_client_set_header(h, "Accept", "application/json"); }
    char auth[1040];
    if (r->bearer && *r->bearer) {
        snprintf(auth, sizeof(auth), "Bearer %s", r->bearer);
        esp_http_client_set_header(h, "Authorization", auth);
    }
    memset(auth, 0, sizeof(auth));
    if (r->content_type)
        esp_http_client_set_header(h, "Content-Type", r->content_type);
    if (r->cf_access_client_id && *r->cf_access_client_id && r->cf_access_client_secret && *r->cf_access_client_secret) {
        esp_http_client_set_header(h, "CF-Access-Client-Id", r->cf_access_client_id);
        esp_http_client_set_header(h, "CF-Access-Client-Secret", r->cf_access_client_secret);
    }
    size_t length = r->body ? strlen(r->body) : 0;
    rp_home_result_t result = RP_HOME_OFFLINE;
    esp_err_t opened = esp_http_client_open(h, (int)length);
    if (opened == ESP_OK) {
        size_t sent = 0;
        while (sent < length) {
            int n = esp_http_client_write(h, r->body + sent, (int)(length - sent));
            if (n <= 0)
                break;
            sent += (size_t)n;
        }
        if (sent == length) {
            int64_t declared = esp_http_client_fetch_headers(h);
            *status = esp_http_client_get_status_code(h);
            if (declared >= (int64_t)capacity)
                result = RP_HOME_TOO_LARGE;
            else {
                int n = 0;
                while (*used < capacity - 1 &&
                       (n = esp_http_client_read(h, response + *used, (int)(capacity - 1 - *used))) > 0)
                    *used += (size_t)n;
                response[*used] = 0;
                result = esp_http_client_is_complete_data_received(h) ? RP_HOME_OK
                         : *used == capacity - 1                      ? RP_HOME_TOO_LARGE
                                                                      : RP_HOME_OFFLINE;
            }
        }
        esp_http_client_close(h);
    } else if (!strncmp(r->url, "https://", 8)) {
        int code = 0, flags = 0;
        esp_err_t error = esp_http_client_get_and_clear_last_tls_error(h, &code, &flags);
        result = tls_failure(error, code, flags);
        if (opened == ESP_ERR_NO_MEM) result = RP_HOME_NOMEM;
        // 仅输出固定错误码，不输出URL、账户、请求体或授权。/ Fixed codes only, never URLs, accounts, request bodies or authorization.
        ESP_LOGW("home_link", "HTTPS open failed: transport=%x tls=%x verify=%x result=%d",
                 (unsigned)error, (unsigned)code, (unsigned)flags, result);
    }
    esp_http_client_cleanup(h);
    return result;
#else
    if (host_transport_offline())
        return RP_HOME_OFFLINE;
    // 凭据只经stdin配置传递，不出现在命令行、shell、日志或URL。
    // Send credentials only through stdin configuration, never arguments, shells, logs, or URLs.
    char *script = NULL;
    size_t script_size = 0;
    FILE *f = open_memstream(&script, &script_size);
    if (!f)
        return RP_HOME_NOMEM;
    bool ok = fputs("silent\nshow-error\nmax-time = 12\nconnect-timeout = 6\nmax-filesize = 196608\nwrite-out = "
                    "\"\\n%{http_code}\"\n",
                    f) != EOF;
    ok = ok && config_line(f, "url", r->url) && config_line(f, "request", r->method);
    if (bambu) ok = ok && config_line(f, "header", "User-Agent: ReadPico/0.25") && config_line(f, "header", "Accept: application/json");
    if (r->bearer && *r->bearer) {
        char auth[1040];
        snprintf(auth, sizeof(auth), "Authorization: Bearer %s", r->bearer);
        ok = ok && config_line(f, "header", auth);
        memset(auth, 0, sizeof(auth));
    }
    if (r->content_type) {
        char type[96];
        snprintf(type, sizeof(type), "Content-Type: %s", r->content_type);
        ok = ok && config_line(f, "header", type);
    }
    if (r->cf_access_client_id && *r->cf_access_client_id && r->cf_access_client_secret && *r->cf_access_client_secret) {
        char cf[192];
        snprintf(cf, sizeof(cf), "CF-Access-Client-Id: %s", r->cf_access_client_id);
        ok = ok && config_line(f, "header", cf);
        snprintf(cf, sizeof(cf), "CF-Access-Client-Secret: %s", r->cf_access_client_secret);
        ok = ok && config_line(f, "header", cf);
        memset(cf, 0, sizeof(cf));
    }
    if (r->body)
        ok = ok && config_line(f, "data-binary", r->body);
    if (fclose(f) != 0)
        ok = false;
    process_t p = {.input = -1, .output = -1};
    char *args[] = {"/usr/bin/curl", "--disable", "--proto", "=http,https", "--config", "-", NULL};
    if (!ok || !spawn_process(&p, args[0], args)) {
        if (script) {
            memset(script, 0, script_size);
            free(script);
        }
        return RP_HOME_UNSUPPORTED;
    }
    ok = write_all(p.input, script, script_size);
    memset(script, 0, script_size);
    free(script);
    close(p.input);
    p.input = -1;
    int64_t deadline = now_ms() + 14000;
    bool overflow = false;
    while (ok && now_ms() < deadline) {
        struct pollfd fd = {p.output, POLLIN, 0};
        int ready = poll(&fd, 1, 250);
        if (ready < 0 && errno == EINTR)
            continue;
        if (ready < 0) {
            ok = false;
            break;
        }
        if (!ready)
            continue;
        char chunk[1024];
        ssize_t n = read(p.output, chunk, sizeof(chunk));
        if (n == 0)
            break;
        if (n < 0) {
            if (errno == EINTR)
                continue;
            ok = false;
            break;
        }
        if (*used + (size_t)n >= capacity) {
            overflow = true;
            break;
        }
        memcpy(response + *used, chunk, (size_t)n);
        *used += (size_t)n;
    }
    bool timed_out = now_ms() >= deadline;
    int exit_code = process_close(&p, overflow || !ok || timed_out);
    if (overflow)
        return RP_HOME_TOO_LARGE;
    if (!ok || timed_out)
        return RP_HOME_OFFLINE;
    if (exit_code == 60 || exit_code == 35 || exit_code == 51 || exit_code == 58 || exit_code == 77)
        return RP_HOME_TLS;
    if (exit_code == 63)
        return RP_HOME_TOO_LARGE;
    if (exit_code)
        return RP_HOME_OFFLINE;
    if (*used < 4 || response[*used - 4] != '\n')
        return RP_HOME_INVALID;
    *status = (response[*used - 3] - '0') * 100 + (response[*used - 2] - '0') * 10 + response[*used - 1] - '0';
    *used -= 4;
    response[*used] = 0;
    return RP_HOME_OK;
#endif
}

typedef struct {
#ifdef ESP_PLATFORM
    esp_tls_t *tls;
#else
    process_t process;
    char certificate[128];
#endif
    int64_t deadline;
    rp_home_result_t failure;
} mqtt_io_t;
static bool mqtt_open(mqtt_io_t *io, const rp_home_printer_config_t *p) {
    const char *host =
        p->source == RP_HOME_BAMBU_CLOUD ? (p->region ? "cn.mqtt.bambulab.com" : "us.mqtt.bambulab.com") : p->host;
    int port = p->source == RP_HOME_BAMBU_CLOUD ? 8883 : p->port ? p->port : 8883;
    io->deadline = now_ms() + 12000;
    io->failure = RP_HOME_OFFLINE;
#ifdef ESP_PLATFORM
    if (!tls_clock_ready()) {
        io->failure = RP_HOME_TIME;
        return false;
    }
    io->tls = esp_tls_init();
    if (!io->tls) {
        io->failure = RP_HOME_NOMEM;
        return false;
    }
    esp_tls_cfg_t cfg = {.timeout_ms = 4000, .crt_bundle_attach = esp_crt_bundle_attach};
    if (p->source == RP_HOME_BAMBU_LAN) {
        cfg.crt_bundle_attach = NULL;
        cfg.cacert_buf = (const unsigned char *)p->certificate;
        cfg.cacert_bytes = (unsigned)strlen(p->certificate) + 1;
    }
    if (esp_tls_conn_new_sync(host, (int)strlen(host), port, &cfg, io->tls) != 1) {
        esp_tls_error_handle_t error;
        int code = 0, flags = 0;
        if (esp_tls_get_error_handle(io->tls, &error) == ESP_OK) {
            int last_error = esp_tls_get_and_clear_last_error(error, &code, &flags);
            io->failure = tls_failure(last_error, code, flags);
        }
        esp_tls_conn_destroy(io->tls);
        io->tls = NULL;
        return false;
    }
    return true;
#else
    if (host_transport_offline())
        return false;
    io->certificate[0] = 0;
    const char *ca = "/etc/ssl/cert.pem";
    if (p->source == RP_HOME_BAMBU_LAN) {
        snprintf(io->certificate, sizeof(io->certificate), "/tmp/pico-home-ca-XXXXXX");
        int fd = mkstemp(io->certificate);
        if (fd < 0)
            return false;
        bool ok = fchmod(fd, 0600) == 0 && write_all(fd, p->certificate, strlen(p->certificate));
        close(fd);
        if (!ok) {
            unlink(io->certificate);
            return false;
        }
        ca = io->certificate;
    }
    char endpoint[160];
    snprintf(endpoint, sizeof(endpoint), "%s:%d", host, port);
    const char *openssl = "/usr/bin/openssl";
#ifdef __APPLE__
    // 系统LibreSSL不支持verify_hostname，必须使用提供该校验的OpenSSL。
    // System LibreSSL lacks verify_hostname; require OpenSSL that provides the check.
    if (access("/opt/homebrew/opt/openssl@3/bin/openssl", X_OK) == 0)
        openssl = "/opt/homebrew/opt/openssl@3/bin/openssl";
    else if (access("/usr/local/opt/openssl@3/bin/openssl", X_OK) == 0)
        openssl = "/usr/local/opt/openssl@3/bin/openssl";
    else {
        if (*io->certificate)
            unlink(io->certificate);
        io->failure = RP_HOME_UNSUPPORTED;
        return false;
    }
#endif
    char *args[] = {(char *)openssl,
                    "s_client",
                    "-quiet",
                    "-verify_return_error",
                    "-verify_hostname",
                    (char *)host,
                    "-servername",
                    (char *)host,
                    "-CAfile",
                    (char *)ca,
                    "-connect",
                    endpoint,
                    NULL};
    if (!spawn_process(&io->process, args[0], args)) {
        if (*io->certificate)
            unlink(io->certificate);
        return false;
    }
    return true;
#endif
}
static void mqtt_close(mqtt_io_t *io) {
#ifdef ESP_PLATFORM
    if (io->tls)
        esp_tls_conn_destroy(io->tls);
#else
    process_close(&io->process, true);
    if (*io->certificate)
        unlink(io->certificate);
#endif
}
static rp_home_result_t mqtt_failure(mqtt_io_t *io) {
#ifndef ESP_PLATFORM
    char diagnostic[4096];
    ssize_t n = read(io->process.error, diagnostic, sizeof(diagnostic) - 1);
    if (n > 0) {
        diagnostic[n] = 0;
        if (strstr(diagnostic, "verify error") || strstr(diagnostic, "certificate verify failed") ||
            strstr(diagnostic, "hostname mismatch"))
            io->failure = RP_HOME_TLS;
        memset(diagnostic, 0, sizeof(diagnostic));
    }
#endif
    return io->failure;
}
static bool mqtt_write(mqtt_io_t *io, const void *data, size_t bytes) {
#ifdef ESP_PLATFORM
    const char *p = data;
    while (bytes && now_ms() < io->deadline) {
        ssize_t n = esp_tls_conn_write(io->tls, p, bytes);
        if (n <= 0)
            return false;
        p += n;
        bytes -= (size_t)n;
    }
    return !bytes;
#else
    return now_ms() < io->deadline && write_all(io->process.input, data, bytes);
#endif
}
static bool mqtt_read(mqtt_io_t *io, void *data, size_t bytes) {
    char *p = data;
    while (bytes && now_ms() < io->deadline) {
#ifdef ESP_PLATFORM
        ssize_t n = esp_tls_conn_read(io->tls, p, bytes);
#else
        struct pollfd fd = {io->process.output, POLLIN, 0};
        int ready = poll(&fd, 1, 250);
        if (ready < 0 && errno == EINTR)
            continue;
        if (ready < 0)
            return false;
        if (!ready)
            continue;
        ssize_t n = read(io->process.output, p, bytes);
#endif
        if (n <= 0)
            return false;
        p += n;
        bytes -= (size_t)n;
    }
    return !bytes;
}
static bool mqtt_string(unsigned char *packet, size_t capacity, size_t *used, const char *s) {
    size_t n = strlen(s);
    if (n > 65535 || *used + n + 2 > capacity)
        return false;
    packet[(*used)++] = (unsigned char)(n >> 8);
    packet[(*used)++] = (unsigned char)n;
    memcpy(packet + *used, s, n);
    *used += n;
    return true;
}
static bool mqtt_packet(mqtt_io_t *io, unsigned header, const unsigned char *body, size_t bytes) {
    unsigned char h[5];
    size_t used = 1, n = bytes;
    h[0] = (unsigned char)header;
    do {
        if (used >= sizeof(h))
            return false;
        h[used] = (unsigned char)(n % 128);
        n /= 128;
        if (n)
            h[used] |= 128;
        used++;
    } while (n);
    return mqtt_write(io, h, used) && mqtt_write(io, body, bytes);
}
static rp_home_result_t mqtt_report(void *unused, const rp_home_printer_config_t *p, char *response, size_t capacity,
                                    size_t *bytes) {
    (void)unused;
    *bytes = 0;
    mqtt_io_t io = {0};
    if (!mqtt_open(&io, p))
        return io.failure;
    unsigned char packet[1280] = {0, 4, 'M', 'Q', 'T', 'T', 4, 0xc2, 0, 30};
    size_t used = 10;
    char client[48];
    snprintf(client, sizeof(client), "pico-%llx", (unsigned long long)now_ms());
    const char *username = p->source == RP_HOME_BAMBU_CLOUD ? p->username : "bblp";
    rp_home_result_t result = RP_HOME_INVALID;
    if (!mqtt_string(packet, sizeof(packet), &used, client) || !mqtt_string(packet, sizeof(packet), &used, username) ||
        !mqtt_string(packet, sizeof(packet), &used, p->secret) || !mqtt_packet(&io, 0x10, packet, used))
        goto done;
    memset(packet, 0, sizeof(packet));
    bool subscribed = false;
    char topic[96];
    snprintf(topic, sizeof(topic), "device/%s/report", p->serial);
    while (now_ms() < io.deadline) {
        unsigned char header, encoded;
        if (!mqtt_read(&io, &header, 1)) {
            result = mqtt_failure(&io);
            break;
        }
        size_t length = 0, multiplier = 1;
        unsigned i;
        for (i = 0; i < 4; i++) {
            if (!mqtt_read(&io, &encoded, 1)) {
                result = RP_HOME_OFFLINE;
                goto done;
            }
            length += (encoded & 127u) * multiplier;
            if (!(encoded & 128))
                break;
            multiplier *= 128;
        }
        if (i == 4) {
            result = RP_HOME_INVALID;
            break;
        }
        if (length >= capacity) {
            result = RP_HOME_TOO_LARGE;
            break;
        }
        if (!mqtt_read(&io, response, length)) {
            result = RP_HOME_OFFLINE;
            break;
        }
        unsigned type = header >> 4;
        if (type == 2) {
            if (length != 2) {
                result = RP_HOME_INVALID;
                break;
            }
            if (response[1]) {
                result = RP_HOME_AUTH;
                break;
            }
            packet[0] = 0;
            packet[1] = 1;
            used = 2;
            if (!mqtt_string(packet, sizeof(packet), &used, topic)) {
                result = RP_HOME_INVALID;
                break;
            }
            packet[used++] = 0;
            if (!mqtt_packet(&io, 0x82, packet, used)) {
                result = RP_HOME_OFFLINE;
                break;
            }
            subscribed = true;
        } else if (type == 9) {
            if (length < 3 || ((unsigned char)response[2]) == 0x80) {
                result = RP_HOME_AUTH;
                break;
            }
        } else if (type == 3 && subscribed) {
            if (length < 2) {
                result = RP_HOME_INVALID;
                break;
            }
            size_t topic_bytes = ((unsigned char)response[0]) * 256u + (unsigned char)response[1],
                   offset = topic_bytes + 2;
            if (offset > length) {
                result = RP_HOME_INVALID;
                break;
            }
            bool match = topic_bytes == strlen(topic) && !memcmp(response + 2, topic, topic_bytes);
            unsigned qos = (header >> 1) & 3u;
            if (qos > 1 || offset + (qos ? 2u : 0u) > length) {
                result = RP_HOME_INVALID;
                break;
            }
            if (qos) {
                unsigned char ack[2] = {(unsigned char)response[offset], (unsigned char)response[offset + 1]};
                offset += 2;
                if (!mqtt_packet(&io, 0x40, ack, 2)) {
                    result = RP_HOME_OFFLINE;
                    break;
                }
            }
            if (match) {
                *bytes = length - offset;
                memmove(response, response + offset, *bytes);
                response[*bytes] = 0;
                result = RP_HOME_OK;
                break;
            }
        }
    }
done:
    memset(packet, 0, sizeof(packet));
    mqtt_close(&io);
    return result;
}

typedef struct { rp_home_bambu_session_t session; rp_home_bambu_input_t input; } auth_work_t;
static void worker_run(void) {
    rp_home_config_t *config = memory(sizeof(*config));
    rp_home_snapshot_t *snapshot = memory(sizeof(*snapshot));
    rp_home_token_t *tokens = memory(sizeof(*tokens) * RP_HOME_PRINTERS);
    auth_work_t *auth = memory(sizeof(*auth));
    if (!config || !snapshot || !tokens || !auth) {
        free(config);
        free(snapshot);
        free(tokens);
        free(auth);
        lock();
        state->started = false;
        state->busy = false;
        state->snapshot.busy = 0;
        state->snapshot.result = RP_HOME_NOMEM;
        state->snapshot.revision++;
        if (state->auth_pending) {
            rp_home_bambu_wipe(&state->auth_input, sizeof(state->auth_input)); state->auth_pending = false;
            state->auth.public.phase = RP_BAMBU_ERROR; state->auth.public.result = RP_HOME_NOMEM; state->auth.public.revision++;
        }
        unlock();
        return;
    }
    uint32_t token_generation = 0;
    for (;;) {
        lock();
        auth_expire_locked();
        bool auth_run = !state->suspended && state->auth_pending;
        if (auth_run && state->auth_input.op == RP_BAMBU_REUSE && state->auth_input.config_revision != state->generation) {
            auth_changed_locked(); auth_run = false;
        }
        bool run = !auth_run && !state->suspended && state->queued;
        uint32_t generation = state->generation;
        bool online = state->online, at_home = state->at_home, control = state->control_pending;
        rp_home_entity_t entity = state->control_entity;
        char action[24];
        memcpy(action, state->control_action, sizeof(action));
        float value = state->control_value;
        if (auth_run) {
            auth->session = state->auth; auth->session.public.phase = state->auth_previous_phase;
            auth->input = state->auth_input; rp_home_bambu_wipe(&state->auth_input, sizeof(state->auth_input));
            state->auth_pending = false; state->busy = true;
        } else if (run) {
            memcpy(config, &state->config, sizeof(*config));
            memcpy(snapshot, &state->snapshot, sizeof(*snapshot));
            state->queued = state->control_pending = false;
            state->busy = true;
            state->snapshot.busy = 1;
            state->snapshot.revision++;
        }
        unlock();
        if (auth_run) {
            rp_home_transport_t transport = {http_request, mqtt_report, NULL};
            rp_home_bambu_execute(&auth->input, &auth->session, &transport);
            lock(); auth_expire_locked();
            if (!state->suspended && state->auth.public.session == auth->session.public.session) {
                if (auth->input.op == RP_BAMBU_REUSE && auth->input.config_revision != state->generation) auth_changed_locked();
                else {
                    auth->session.public.revision = state->auth.public.revision + 1u;
                    state->auth = auth->session;
                    // 验证挑战不代表本机已发验证码；只有明确发送才启动冷却。
                    // A verification challenge is not a local send; only an explicit send starts the cooldown.
                }
            }
            state->busy = false; unlock();
            rp_home_bambu_wipe(auth, sizeof(*auth));
            continue;
        }
        if (!run) {
            wait_for_work();
            continue;
        }
        if (token_generation != generation) {
            memset(tokens, 0, sizeof(*tokens) * RP_HOME_PRINTERS);
            token_generation = generation;
        }
        rp_home_transport_t transport = {http_request, mqtt_report, NULL};
        int status = 0;
        rp_home_result_t result =
            control ? rp_home_control(config, &entity, action, value, &transport, at_home, &status) : RP_HOME_OK;
        if (result == RP_HOME_OK)
            rp_home_fetch(config, snapshot, tokens, &transport, online, at_home, now_ms());
        else {
            snapshot->result = result;
            snapshot->http_status = status;
        }
        lock();
        if (state->generation == generation && !state->suspended) {
            uint32_t revision = state->snapshot.revision + 1;
            memcpy(&state->snapshot, snapshot, sizeof(*snapshot));
            state->snapshot.revision = revision;
            state->snapshot.busy = 0;
        }
        state->busy = false;
        unlock();
    }
}
#ifdef ESP_PLATFORM
static void worker(void *unused) {
    (void)unused;
    worker_run();
    vTaskDelete(NULL);
}
#else
static void *worker(void *unused) {
    (void)unused;
    worker_run();
    return NULL;
}
#endif
void read_pico_home_link_init(void) {
    if (state)
        return;
#ifdef ESP_PLATFORM
    if (!state_lock)
        state_lock = xSemaphoreCreateMutex();
    if (!state_lock)
        return;
#endif
    state = memory(sizeof(*state));
    if (!state)
        return;
    state->config.version = RP_HOME_CONFIG_VERSION;
    state->auth.public.session = state->auth.public.revision = 1;
    rp_home_config_t *saved = memory(sizeof(*saved));
    if (saved) {
        uint32_t previous_version = RP_HOME_CONFIG_VERSION;
        if (settings(false, saved, &previous_version)) {
            if (rp_home_config_upgrade(saved)) {
                memcpy(&state->config, saved, sizeof(*saved));
                // 成功迁移后清理持久化混字段；写失败仍保留已验证的内存配置。
                // Persist normalized legacy fields; a failed rewrite keeps the verified runtime config usable.
                if (previous_version != RP_HOME_CONFIG_VERSION) (void)settings(true, saved, NULL);
            }
        }
        memset(saved, 0, sizeof(*saved));
        free(saved);
    }
    state->initialized = true;
    state->snapshot.revision = 1;
}
void read_pico_home_link_get_config(rp_home_config_t *out) {
    if (!out)
        return;
    read_pico_home_link_init();
    if (!state) {
        memset(out, 0, sizeof(*out));
        out->version = RP_HOME_CONFIG_VERSION;
        return;
    }
    lock();
    memcpy(out, &state->config, sizeof(*out));
    unlock();
}
bool read_pico_home_link_configure(const rp_home_config_t *config) {
    if (!rp_home_config_valid(config))
        return false;
    read_pico_home_link_init();
    if (!state)
        return false;
    if (!settings(true, (rp_home_config_t *)config, NULL))
        return false;
    lock();
    memcpy(&state->config, config, sizeof(*config));
    state->generation++;
    state->queued = false;
    uint32_t revision = state->snapshot.revision + 1;
    memset(&state->snapshot, 0, sizeof(state->snapshot));
    state->snapshot.revision = revision;
    unlock();
    return true;
}
static bool start_worker(void) {
    if (state->started)
        return true;
    state->started = true;
#ifdef ESP_PLATFORM
    if (xTaskCreate(worker, "pico_home", 6144, NULL, 3, &worker_handle) != pdPASS) {
        state->started = false;
        return false;
    }
#else
    pthread_t thread;
    if (pthread_create(&thread, NULL, worker, NULL) != 0) {
        state->started = false;
        return false;
    }
    pthread_detach(thread);
#endif
    return true;
}
bool read_pico_home_link_request(bool online, bool at_home) {
    read_pico_home_link_init();
    if (!state)
        return false;
    lock();
    state->online = online;
    state->at_home = at_home;
    if (!online && state->auth_expires) auth_clear_locked(RP_BAMBU_IDLE);
    if (state->suspended || !state->config.enabled) {
        unlock();
        return false;
    }
    if (!online) {
        state->queued = state->control_pending = false;
        state->generation++;
        state->snapshot.busy=0;
        state->snapshot.result = RP_HOME_OFFLINE;
        state->snapshot.revision++;
        unlock();
        return false;
    }
    state->online = online;
    state->at_home = at_home;
    state->queued = true;
    bool ok = start_worker();
    if (ok)
        notify_worker();
    if (!ok) {
        state->queued = false;
        state->snapshot.result = RP_HOME_NOMEM;
        state->snapshot.revision++;
    }
    unlock();
    return ok;
}
uint32_t read_pico_home_link_revision(void) {
    if (!state)
        return 0;
    lock();
    uint32_t revision = state->snapshot.revision;
    unlock();
    return revision;
}
void read_pico_home_link_get_snapshot(rp_home_snapshot_t *out) {
    if (!out)
        return;
    if (!state) {
        memset(out, 0, sizeof(*out));
        return;
    }
    lock();
    memcpy(out, &state->snapshot, sizeof(*out));
    unlock();
}
bool read_pico_home_link_control(const rp_home_entity_t *entity, const char *action, float value, bool at_home) {
    if (!state || !entity || !action || strlen(action) >= sizeof(state->control_action))
        return false;
    lock();
    if (state->suspended || state->busy || !state->config.enabled || !state->online) {
        unlock();
        return false;
    }
    state->control_entity = *entity;
    snprintf(state->control_action, sizeof(state->control_action), "%s", action);
    state->control_value = value;
    state->control_pending = state->queued = true;
    state->at_home = at_home;
    bool ok = start_worker();
    if (ok)
        notify_worker();
    if (!ok)
        state->control_pending = state->queued = false;
    unlock();
    return ok;
}
bool read_pico_home_link_suspend(uint32_t timeout) {
    if (!state)
        return true;
    lock();
    state->suspended = true;
    auth_clear_locked(RP_BAMBU_IDLE);
    state->queued = state->control_pending = false;
    state->snapshot.busy = 0;
    unlock();
    int64_t deadline = now_ms() + timeout;
    for (;;) {
        lock();
        bool busy = state->busy;
        unlock();
        if (!busy)
            return true;
        if (now_ms() >= deadline)
            return false;
        pause_ms(10);
    }
}
void read_pico_home_link_resume(void) {
    if (!state)
        return;
    lock();
    state->suspended = false;
    unlock();
}
bool read_pico_bambu_auth_submit(const rp_home_bambu_input_t *input, bool online) {
    if (!rp_home_bambu_input_valid(input)) return false;
    read_pico_home_link_init(); if (!state) return false;
    lock(); auth_expire_locked();
    if (!online || state->suspended || input->session != state->auth.public.session) { unlock(); return false; }
    unsigned phase = state->auth.public.phase;
    bool restart = input->op == RP_BAMBU_LOGIN || input->op == RP_BAMBU_REUSE;
    if (input->op == RP_BAMBU_REUSE) {
        if (input->config_revision != state->generation || state->config.printers[input->saved_slot].source != RP_HOME_BAMBU_CLOUD) { unlock(); return false; }
        for (unsigned i = 0; i < input->saved_slot; ++i) if (auth_same_account(&state->config.printers[i], &state->config.printers[input->saved_slot])) { unlock(); return false; }
    }
    if (input->op != RP_BAMBU_LOGIN &&
        ((input->op == RP_BAMBU_EMAIL_CODE && phase != RP_BAMBU_EMAIL) ||
         (input->op == RP_BAMBU_TFA_CODE && phase != RP_BAMBU_TFA) ||
         (input->op == RP_BAMBU_RESEND && ((phase != RP_BAMBU_EMAIL && phase != RP_BAMBU_TFA) || now_ms() < state->auth_retry)))) {
        unlock(); return false;
    }
    if (restart) {
        auth_clear_locked(RP_BAMBU_IDLE); state->auth_expires = now_ms() + BAMBU_SESSION_MS;
        state->auth.public.region = input->region;
        if (input->op == RP_BAMBU_REUSE) {
            const rp_home_printer_config_t *p = &state->config.printers[input->saved_slot];
            state->auth.public.region = p->region; state->auth.public.reused = 1;
            memcpy(state->auth.username, p->username, sizeof(state->auth.username)); memcpy(state->auth.token, p->secret, sizeof(state->auth.token));
        }
    }
    state->auth_previous_phase = restart ? RP_BAMBU_IDLE : (uint8_t)phase;
    state->auth_input = *input; state->auth_input.session = state->auth.public.session;
    state->auth_pending = true; state->online = online;
    state->auth.public.phase = RP_BAMBU_BUSY; state->auth.public.result = RP_HOME_BUSY;
    state->auth.public.error = RP_BAMBU_ERROR_NONE; state->auth.public.http_status = 0; state->auth.public.revision++;
    if (input->op == RP_BAMBU_RESEND) state->auth_retry = now_ms() + BAMBU_RESEND_MS;
    bool ok = start_worker();
    if (ok) notify_worker();
    else {
        rp_home_bambu_wipe(&state->auth_input, sizeof(state->auth_input)); state->auth_pending = false;
        state->auth.public.phase = RP_BAMBU_ERROR; state->auth.public.result = RP_HOME_NOMEM; state->auth.public.revision++;
    }
    unlock(); return ok;
}
void read_pico_bambu_auth_get(rp_home_bambu_state_t *out) {
    if (!out) return;
    read_pico_home_link_init(); if (!state) { memset(out, 0, sizeof(*out)); return; }
    lock(); auth_expire_locked(); *out = state->auth.public;
    auth_saved_accounts_locked(out);
    int64_t now = now_ms(), left = state->auth_expires - now, retry = state->auth_retry - now;
    out->seconds_left = left > 0 ? (uint32_t)((left + 999) / 1000) : 0;
    out->retry_after = retry > 0 ? (uint32_t)((retry + 999) / 1000) : 0;
    unlock();
}
bool read_pico_bambu_auth_cancel_session(uint32_t session) {
    if (!state) return false;
    lock(); auth_expire_locked(); bool ok = session == state->auth.public.session;
    if (ok) auth_clear_locked(RP_BAMBU_IDLE);
    unlock(); return ok;
}
void read_pico_bambu_auth_cancel(void) {
    if (!state) return;
    lock(); auth_clear_locked(RP_BAMBU_IDLE); unlock();
}
bool read_pico_bambu_auth_prepare_import(uint32_t session, unsigned index, rp_home_printer_config_t *out) {
    if (!out) return false;
    memset(out, 0, sizeof(*out)); if (!state) return false;
    lock(); auth_expire_locked();
    bool ok = session == state->auth.public.session && state->auth.public.phase == RP_BAMBU_READY && index < state->auth.public.count;
    if (ok) {
        const rp_home_bambu_device_t *d = &state->auth.public.devices[index];
        out->source = RP_HOME_BAMBU_CLOUD; out->region = state->auth.public.region;
        memcpy(out->serial, d->serial, sizeof(out->serial)); memcpy(out->name, d->name, sizeof(out->name));
        memcpy(out->username, state->auth.username, sizeof(out->username)); memcpy(out->secret, state->auth.token, sizeof(out->secret));
    }
    unlock(); return ok;
}

/* SPDX-License-Identifier: Apache-2.0
 * 中文：读取网站版本清单，并在用户操作后下载缺失或更新的 PGA 应用包。
 * English: Read the website release feed and, after user action, download missing or newer PGA packages.
 * 冻结：固定 HTTPS 来源、证书验证和响应上限；网络只在后台任务运行，安装事务回到UI线程。
 * Frozen: fixed HTTPS origin, certificate verification and bounded response; networking only runs in workers and package installation returns to the UI thread.
 */
#include "pico_updates.h"
#include "read_pico_connectivity.h"
#include "pico_app_package.h"
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define UPDATE_URL_STABLE "https://xhfo.net/projects/read-pico/updates/stable/version.txt"
#define UPDATE_URL_TEST "https://xhfo.net/projects/read-pico/updates/test/version.txt"
#define APPS_URL "https://xhfo.net/projects/read-pico/updates/apps.txt"
#define PACKAGE_URL "https://xhfo.net/projects/read-pico/apps/packages/"
#define APPS_URL_TEST "https://xhfo.net/projects/read-pico/updates/test/apps.txt"
#define PACKAGE_URL_TEST "https://xhfo.net/projects/read-pico/apps/packages/test/"
#define FIRMWARE_URL_STABLE "https://xhfo.net/projects/read-pico/manage/firmware/stable/read-pico-app.bin"
#define FIRMWARE_URL_TEST "https://xhfo.net/projects/read-pico/manage/firmware/test/read-pico-app.bin"
#define APPS_MAX 12
#define FIRMWARE_MAX (4u * 1024u * 1024u)
#define FIRMWARE_DIRECTORY "/sdcard/readpico/firmware"
#define FIRMWARE_PATH "/sdcard/readpico/firmware/update.bin"
#define FIRMWARE_SHA_PATH "/sdcard/readpico/firmware/update.sha256"
#define FIRMWARE_SPACE_MARGIN (512u * 1024u)

#ifdef ESP_PLATFORM
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "esp_system.h"
#include "esp_vfs_fat.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "psa/crypto.h"
static TaskHandle_t s_task;
// 安装回到 UI 线程后仍保留整批选择模式。/ Preserve the batch selection mode across UI-thread installation.
static unsigned s_dl_job;
#endif

enum { IDLE, BUSY, READY, FAILED };
enum { DL_IDLE, DL_CATALOG, DL_BUSY, DL_READY, DL_INSTALLING, DL_DONE, DL_FAILED };
enum { APP_JOB_NONE, APP_JOB_CATALOG, APP_JOB_ALL, APP_JOB_SELECTED };
enum { FW_IDLE, FW_BUSY, FW_READY, FW_FAILED };
static volatile int s_state = IDLE;
static volatile bool s_newer;
static volatile unsigned s_apps;
static volatile unsigned s_revision, s_seen;
static char s_latest[32] = "等待检查";
static bool s_auto;
static volatile int s_dl_state = DL_IDLE;
static volatile unsigned s_dl_revision, s_dl_seen;
static volatile unsigned s_dl_index, s_dl_installed, s_dl_position, s_dl_total;
static bool s_manifest_loaded;
static bool s_manifest_catalog_ready;
static unsigned s_manifest_count;
static char s_manifest_ids[APPS_MAX][32];
static bool s_manifest_tools[APPS_MAX];
static bool s_manifest_installed[APPS_MAX];
static uint32_t s_manifest_local_version[APPS_MAX], s_manifest_remote_version[APPS_MAX];
static bool s_manifest_selected[APPS_MAX];
static char s_download_id[32];
static volatile int s_fw_state = FW_IDLE;
static volatile unsigned s_fw_revision, s_fw_seen, s_fw_percent;
static volatile unsigned s_fw_size;
static volatile bool s_fw_meta_valid;
static char s_fw_sha[65];
static uint8_t s_channel = PS_UPDATE_CHANNEL_STABLE;
static bool s_channel_loaded;

static void channel_load(void) {
    if (s_channel_loaded) return;
    uint8_t saved;
    if (ps_load("updchan1", &saved, sizeof(saved)) &&
        (saved == PS_UPDATE_CHANNEL_STABLE || saved == PS_UPDATE_CHANNEL_TEST))
        s_channel = saved;
    s_channel_loaded = true;
}

static const char *update_url(void) {
    channel_load();
    return s_channel == PS_UPDATE_CHANNEL_TEST ? UPDATE_URL_TEST : UPDATE_URL_STABLE;
}

static const char *firmware_url(void) {
    channel_load();
    return s_channel == PS_UPDATE_CHANNEL_TEST ? FIRMWARE_URL_TEST : FIRMWARE_URL_STABLE;
}

// 应用跟随频道，稳定设备不会收到测试宿主专用包。/ Keep test-host packages isolated from stable devices.
static const char *apps_url(void) {
    channel_load();
    return s_channel == PS_UPDATE_CHANNEL_TEST ? APPS_URL_TEST : APPS_URL;
}

static const char *package_url(void) {
    channel_load();
    return s_channel == PS_UPDATE_CHANNEL_TEST ? PACKAGE_URL_TEST : PACKAGE_URL;
}

static int suffix(const char *version) {
    const char *dual = version ? strstr(version, "-dual") : NULL;
    if (!dual) return 100000;
    char *end;
    long number = strtol(dual + 5, &end, 10);
    return *end || number < 0 || number > 99999 ? -1 : (int)number;
}

static bool newer_than_current(const char *latest) {
    if (!latest || strncmp(latest, "0.40.0", 6)) return false;
    int remote = suffix(latest), current = suffix(ps_version());
    return remote >= 0 && current >= 0 && remote > current;
}

static bool line_value(const char *body, const char *key, char *out, size_t bytes) {
    size_t length = strlen(key);
    for (const char *line = body; line && *line;) {
        if (!strncmp(line, key, length) && line[length] == '=') {
            size_t n = strcspn(line + length + 1, "\r\n");
            if (!n || n >= bytes) return false;
            memcpy(out, line + length + 1, n);
            out[n] = 0;
            return true;
        }
        line = strchr(line, '\n');
        if (line) ++line;
    }
    return false;
}

static bool sha_hex(const char *text, uint8_t out[32]) {
    if (!text || strlen(text) != 64) return false;
    for (int i = 0; i < 32; i++) {
        char pair[3] = {text[2 * i], text[2 * i + 1], 0};
        if (!isxdigit((unsigned char)pair[0]) ||
            !isxdigit((unsigned char)pair[1])) return false;
        out[i] = (uint8_t)strtoul(pair, NULL, 16);
    }
    return true;
}

static bool parse_firmware_meta(const char *body) {
    char size_text[16] = {0}, digest[65] = {0};
    if (!line_value(body, "firmware_size", size_text, sizeof(size_text)) ||
        !line_value(body, "firmware_sha256", digest, sizeof(digest))) return false;
    char *end = NULL;
    unsigned long size = strtoul(size_text, &end, 10);
    uint8_t parsed[32];
    if (!end || *end || size < 512 || size > FIRMWARE_MAX ||
        !sha_hex(digest, parsed)) return false;
    s_fw_size = (unsigned)size;
    memcpy(s_fw_sha, digest, sizeof(s_fw_sha));
    s_fw_meta_valid = true;
    return true;
}

static const char *app_path(char *out, size_t bytes, const char *id, bool tool) {
    snprintf(out, bytes, "/sdcard/apps/%s/%s.pga", tool ? "tools" : "games", id);
    return out;
}

static bool parse_apps(const char *body) {
    unsigned count = 0;
    memset(s_manifest_remote_version, 0, sizeof(s_manifest_remote_version));
    for (const char *line = body; line && *line;) {
        bool tool = !strncmp(line, "tool=", 5);
        if ((tool || !strncmp(line, "game=", 5)) && count < APPS_MAX) {
            const char *at = line + 5;
            size_t id_bytes = strcspn(at, "\r\n");
            if (id_bytes && id_bytes < sizeof(s_manifest_ids[0])) {
                char id[32] = {0};
                memcpy(id, at, id_bytes);
                if (pico_app_id_valid(id)) {
                    for (unsigned i=0;i<count;i++) if (!strcmp(id,s_manifest_ids[i])) return false;
                    memcpy(s_manifest_ids[count], id, id_bytes + 1);
                    s_manifest_tools[count] = tool;
                    count++;
                }
            }
        }
        line = strchr(line, '\n');
        if (line) line++;
    }
    s_manifest_count = count;
    // 可选版本行对旧固件透明；缺失时由后台读取远端包头。/ Optional version lines are transparent to old firmware; workers probe missing versions.
    for (const char *line=body;line&&*line;) {
        if (!strncmp(line,"app_version=",12)) {
            const char *at=line+12,*comma=strchr(at,',');
            size_t length=strcspn(at,"\r\n");
            if (!comma||comma>=at+length) return false;
            char *end=NULL;unsigned long version=strtoul(comma+1,&end,10);
            if (end!=at+length||comma[1]<'0'||comma[1]>'9'||!version||version>UINT32_MAX) return false;
            for (unsigned i=0;i<count;i++) if (strlen(s_manifest_ids[i])==(size_t)(comma-at)&&!memcmp(at,s_manifest_ids[i],(size_t)(comma-at))) {
                if(s_manifest_remote_version[i])return false;
                s_manifest_remote_version[i]=(uint32_t)version;
            }
        }
        line=strchr(line,'\n');if(line)line++;
    }
    s_manifest_loaded = count != 0;
    return count != 0;
}

static void snapshot_installed_apps(void) {
    for (unsigned i = 0; i < s_manifest_count; i++) {
        char path[96];
        app_path(path,sizeof(path),s_manifest_ids[i],s_manifest_tools[i]);
        s_manifest_installed[i] = access(path, F_OK) == 0;
        pico_app_package_t package;
        s_manifest_local_version[i]=s_manifest_installed[i]&&
            pico_app_package_read(path,&package,false,NULL,0)&&!strcmp(package.id,s_manifest_ids[i])?package.version:0;
    }
}

static bool app_eligible(unsigned i) {
    return !s_manifest_installed[i] || (s_manifest_remote_version[i] &&
        s_manifest_remote_version[i]>s_manifest_local_version[i]);
}

static bool same_digest(const char *a, const char *b) {
    if (!a || !b || strlen(a) != 64 || strlen(b) != 64) return false;
    for (size_t i = 0; i < 64; i++)
        if (tolower((unsigned char)a[i]) != tolower((unsigned char)b[i])) return false;
    return true;
}

static bool read_digest(const char *path, char out[65]) {
    FILE *file = fopen(path, "rb");
    if (!file) return false;
    char text[68];
    size_t used = fread(text, 1, sizeof(text), file);
    int extra = fgetc(file);
    if (fclose(file) != 0 || extra != EOF) return false;
    while (used && isspace((unsigned char)text[used - 1])) used--;
    if (used != 64) return false;
    for (size_t i = 0; i < used; i++)
        if (!isxdigit((unsigned char)text[i])) return false;
    memcpy(out, text, used);
    out[used] = 0;
    return true;
}

static bool firmware_image_version(char out[33]) {
    if (!out) return false;
    uint8_t header[0x70];
    FILE *file = fopen(FIRMWARE_PATH, "rb");
    if (!file) return false;
    size_t used = fread(header, 1, sizeof(header), file);
    uint32_t magic = (uint32_t)header[0x20] | ((uint32_t)header[0x21] << 8) |
        ((uint32_t)header[0x22] << 16) | ((uint32_t)header[0x23] << 24);
    if (fclose(file) != 0 || used != sizeof(header) ||
        header[0] != 0xe9 || magic != 0xabcd5432u)
        return false;
    memcpy(out, header + 0x30, 32);
    out[32] = 0;
    if (!out[0] || memcmp(header + 0x50, "Read_Pico", 10)) return false;
    return true;
}

static void remove_firmware_cache(void) {
    (void)unlink(FIRMWARE_SHA_PATH);
    (void)unlink(FIRMWARE_PATH);
}

static bool firmware_files_ready(size_t *length) {
    struct stat image = {0}, digest_file = {0};
    char digest[65], version[33];
    if (stat(FIRMWARE_PATH, &image) != 0 || image.st_size < 512 ||
        stat(FIRMWARE_SHA_PATH, &digest_file) != 0 ||
        digest_file.st_size < 64 || digest_file.st_size > 66 ||
        !read_digest(FIRMWARE_SHA_PATH, digest)) return false;
    // 网站清单可用时，本地缓存必须与网站大小和 SHA 完全一致；离线包仍可按 TF 内容安装。
    // When the website manifest is available, the local cache must match its size and SHA; offline packages remain supported.
    if (s_fw_meta_valid &&
        ((size_t)s_fw_size != (size_t)image.st_size || !same_digest(digest, s_fw_sha)))
        return false;
    // 新系统已启动且缓存就是当前版本时删除镜像；相同或更旧版本不可再次进入安装器。
    // Once the same image is running, remove it; cached equal/older images can never re-enter the updater.
    if (!firmware_image_version(version) || !newer_than_current(version)) {
        remove_firmware_cache();
        return false;
    }
    if (length) *length = (size_t)image.st_size;
    return true;
}

#ifdef ESP_PLATFORM
static bool firmware_space_available(unsigned length) {
    uint64_t total = 0, available = 0;
    if (esp_vfs_fat_info("/sdcard", &total, &available) != ESP_OK) return false;
    return available >= (uint64_t)length + FIRMWARE_SPACE_MARGIN;
}

static void worker(void *argument) {
    (void)argument;
    char body[513] = {0}, latest[32] = {0}, apps[12] = "0";
    size_t used = 0;
    bool ok = false;
    esp_http_client_config_t config = {
        .url = update_url(),
        .timeout_ms = 8000,
        .buffer_size = 512,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .disable_auto_redirect = true,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client && esp_http_client_open(client, 0) == ESP_OK &&
        esp_http_client_fetch_headers(client) >= -1 && esp_http_client_get_status_code(client) == 200) {
        int n;
        while (used < sizeof(body) - 1 &&
               (n = esp_http_client_read(client, body + used, (int)(sizeof(body) - 1 - used))) > 0)
            used += (size_t)n;
        ok = used && used < sizeof(body) - 1 && esp_http_client_is_complete_data_received(client);
    }
    if (client) esp_http_client_cleanup(client);
    ok = ok && line_value(body, "version", latest, sizeof(latest));
    if (ok) {
        (void)line_value(body, "apps", apps, sizeof(apps));
        char *end;
        unsigned long count = strtoul(apps, &end, 10);
        s_apps = *end || count > 99 ? 0 : (unsigned)count;
        s_newer = newer_than_current(latest);
        snprintf(s_latest, sizeof(s_latest), "%s", latest);
        s_fw_meta_valid = false;
        (void)parse_firmware_meta(body);
    }
    s_state = ok ? READY : FAILED;
    ++s_revision;
    s_task = NULL;
    vTaskDelete(NULL);
}

static void firmware_worker(void *argument) {
    (void)argument;
    char part_path[] = FIRMWARE_DIRECTORY "/update.bin.part";
    char sha_path[] = FIRMWARE_DIRECTORY "/update.sha256.part";
    char block[2048];
    FILE *file = NULL;
    esp_http_client_handle_t client = NULL;
    psa_hash_operation_t hash = PSA_HASH_OPERATION_INIT;
    uint8_t actual[32], expected[32];
    size_t done = 0, digest_bytes = 0;
    bool ok = false;
    (void)mkdir("/sdcard/readpico", 0700);
    if (mkdir(FIRMWARE_DIRECTORY, 0700) == 0 || errno == EEXIST) {
        file = fopen(part_path, "wb");
    }
    if (file && sha_hex(s_fw_sha, expected) &&
        psa_crypto_init() == PSA_SUCCESS &&
        psa_hash_setup(&hash, PSA_ALG_SHA_256) == PSA_SUCCESS) {
        esp_http_client_config_t config = {
            .url = firmware_url(),
            .timeout_ms = 30000,
            .buffer_size = 2048,
            .crt_bundle_attach = esp_crt_bundle_attach,
            .disable_auto_redirect = true,
        };
        client = esp_http_client_init(&config);
        int64_t declared = -1;
        ok = client && esp_http_client_open(client, 0) == ESP_OK &&
            (declared = esp_http_client_fetch_headers(client)) >= 0 &&
            esp_http_client_get_status_code(client) == 200 &&
            declared == (int64_t)s_fw_size;
        while (ok) {
            int bytes = esp_http_client_read(client, block, sizeof(block));
            if (bytes < 0) ok = false;
            else if (!bytes) break;
            else if (fwrite(block, 1, (size_t)bytes, file) != (size_t)bytes ||
                     psa_hash_update(&hash, (uint8_t *)block, (size_t)bytes) != PSA_SUCCESS)
                ok = false;
            else {
                done += (size_t)bytes;
                unsigned bucket = (unsigned)(done * 20 / s_fw_size);
                if (bucket != s_fw_percent) {
                    s_fw_percent = bucket;
                    ++s_fw_revision;
                }
            }
        }
        ok = ok && done == s_fw_size &&
            esp_http_client_is_complete_data_received(client) &&
            fflush(file) == 0 && fsync(fileno(file)) == 0 &&
            psa_hash_finish(&hash, actual, sizeof(actual), &digest_bytes) == PSA_SUCCESS &&
            digest_bytes == sizeof(actual) && !memcmp(actual, expected, sizeof(actual));
    }
    if (client) esp_http_client_cleanup(client);
    if (file && fclose(file) != 0) ok = false;
    if (ok) {
        FILE *sha_file = fopen(sha_path, "wb");
        ok = sha_file && fprintf(sha_file, "%s\n", s_fw_sha) == 65 &&
            fflush(sha_file) == 0 && fsync(fileno(sha_file)) == 0;
        if (sha_file && fclose(sha_file) != 0) ok = false;
    }
    if (ok) {
        // SHA 文件是“已就绪”提交标记：先撤销旧标记，再替换镜像，最后发布新 SHA。
        // The SHA file is the readiness marker: clear the old marker, replace the image, then publish the new SHA last.
        if (unlink(FIRMWARE_SHA_PATH) != 0 && errno != ENOENT) ok = false;
        if (ok && unlink(FIRMWARE_PATH) != 0 && errno != ENOENT) ok = false;
        if (ok) ok = rename(part_path, FIRMWARE_PATH) == 0 &&
            rename(sha_path, FIRMWARE_SHA_PATH) == 0;
    }
    if (!ok) {
        (void)unlink(part_path);
        (void)unlink(sha_path);
    }
    s_fw_percent = ok ? 20 : s_fw_percent;
    s_fw_state = ok ? FW_READY : FW_FAILED;
    ++s_fw_revision;
    s_task = NULL;
    vTaskDelete(NULL);
}

// 旧目录仅探测 512 字节包头，不下载载荷；关闭连接后再进行下一项。/ Probe only the 512-byte header for legacy catalogs, then close before the next item.
static uint32_t remote_app_version(unsigned i) {
    char url[192];uint8_t header[PICO_APP_HEADER_BYTES];pico_app_package_t package;
    snprintf(url,sizeof(url),"%s%s.pga",package_url(),s_manifest_ids[i]);
    esp_http_client_config_t config={.url=url,.timeout_ms=10000,.buffer_size=1024,
        .crt_bundle_attach=esp_crt_bundle_attach,.disable_auto_redirect=true};
    esp_http_client_handle_t client=esp_http_client_init(&config);
    bool ok=client&&esp_http_client_open(client,0)==ESP_OK;
    int64_t bytes=ok?esp_http_client_fetch_headers(client):-1;
    ok=ok&&esp_http_client_get_status_code(client)==200&&bytes>=PICO_APP_HEADER_BYTES&&bytes<=PICO_APP_PACKAGE_MAX;
    unsigned used=0;
    while(ok&&used<sizeof(header)) {
        int n=esp_http_client_read(client,(char *)header+used,(int)(sizeof(header)-used));
        if(n<=0)ok=false;else used+=(unsigned)n;
    }
    if(client)esp_http_client_cleanup(client);
    return ok&&pico_app_package_header(header,sizeof(header),(uint32_t)bytes,&package,NULL,0)&&
        !strcmp(package.id,s_manifest_ids[i])&&((package.kind>=128)==s_manifest_tools[i])?package.version:0;
}

static bool download_package(void) {
    char staging_dir[] = "/sdcard/apps/.staging";
    char path[96], url[192];
    if (mkdir(staging_dir, 0700) != 0 && errno != EEXIST) return false;
    snprintf(path, sizeof(path), "%s/%s.tmp", staging_dir, s_download_id);
    snprintf(url, sizeof(url), "%s%s.pga", package_url(),
             s_manifest_ids[s_dl_index]);
    unlink(path);
    FILE *file = fopen(path, "wb");
    if (!file) return false;
    esp_http_client_config_t config = {
        .url = url,
        .timeout_ms = 30000,
        .buffer_size = 1024,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .disable_auto_redirect = true,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    bool ok = client && esp_http_client_open(client, 0) == ESP_OK;
    int64_t declared = ok ? esp_http_client_fetch_headers(client) : -1;
    ok = ok && esp_http_client_get_status_code(client) == 200 &&
        declared >= (int64_t)PICO_APP_HEADER_BYTES &&
        declared <= (int64_t)PICO_APP_PACKAGE_MAX;
    char block[1024];
    int64_t total = 0;
    while (ok) {
        int bytes = esp_http_client_read(client, block, sizeof(block));
        if (bytes < 0) ok = false;
        else if (!bytes) break;
        else if (fwrite(block, 1, (size_t)bytes, file) != (size_t)bytes) ok = false;
        else total += bytes;
    }
    ok = ok && total == declared && esp_http_client_is_complete_data_received(client) &&
        fflush(file) == 0 && fsync(fileno(file)) == 0;
    if (client) esp_http_client_cleanup(client);
    if (fclose(file) != 0) ok = false;
    // 清单与下载物必须一致，避免发布切换或错误包覆盖现有应用。/ Match the download to its catalog snapshot before replacing an installed app.
    pico_app_package_t package;
    ok=ok&&pico_app_package_read(path,&package,false,NULL,0)&&
        !strcmp(package.id,s_download_id)&&((package.kind>=128)==s_manifest_tools[s_dl_index])&&
        (!s_manifest_remote_version[s_dl_index]||package.version==s_manifest_remote_version[s_dl_index])&&
        (!s_manifest_installed[s_dl_index]||package.version>s_manifest_local_version[s_dl_index]);
    if (!ok) unlink(path);
    return ok;
}

static void apps_worker(void *argument) {
    int job = (int)(intptr_t)argument;
    if (job == APP_JOB_CATALOG || !s_manifest_loaded) {
        static char body[2049];
        size_t used = 0;
        esp_http_client_config_t config = {
            .url = apps_url(),
            .timeout_ms = 10000,
            .buffer_size = 1024,
            .crt_bundle_attach = esp_crt_bundle_attach,
            .disable_auto_redirect = true,
        };
        esp_http_client_handle_t client = esp_http_client_init(&config);
        bool ok = client && esp_http_client_open(client, 0) == ESP_OK &&
            esp_http_client_fetch_headers(client) >= 0 &&
            esp_http_client_get_status_code(client) == 200;
        int bytes;
        while (ok && used < sizeof(body) - 1 &&
               (bytes = esp_http_client_read(client, body + used,
                                             (int)sizeof(body) - 1 - (int)used)) > 0)
            used += (size_t)bytes;
        ok = ok && used && used < sizeof(body) - 1 &&
            esp_http_client_is_complete_data_received(client);
        if (client) esp_http_client_cleanup(client);
        body[used] = 0;
        s_manifest_catalog_ready = false;
        if (!ok || !parse_apps(body)) {
            s_dl_state = DL_FAILED;
            ++s_dl_revision;
            s_task = NULL;
            vTaskDelete(NULL);
        }
        s_apps = s_manifest_count;
        snapshot_installed_apps();
        for(unsigned i=0;i<s_manifest_count;i++)
            if(!s_manifest_remote_version[i])s_manifest_remote_version[i]=remote_app_version(i);
        s_manifest_catalog_ready = true;
        if (job == APP_JOB_ALL) s_dl_total = s_manifest_count;
    }
    if (job == APP_JOB_CATALOG) {
        s_dl_state = DL_IDLE;
        ++s_dl_revision;
        s_task = NULL;
        vTaskDelete(NULL);
    }
    while (s_dl_index < s_manifest_count) {
        unsigned index = s_dl_index;
        if (job == APP_JOB_SELECTED && !s_manifest_selected[index]) {
            s_dl_index++;
            continue;
        }
        if (app_eligible(index)) {
            s_dl_position++;
            snprintf(s_download_id, sizeof(s_download_id), "%s", s_manifest_ids[index]);
            s_dl_state = DL_BUSY;
            ++s_dl_revision;
            bool ok = download_package();
            s_dl_state = ok ? DL_READY : DL_FAILED;
            ++s_dl_revision;
            s_task = NULL;
            vTaskDelete(NULL);
        }
        s_dl_index++;
    }
    s_dl_state = DL_DONE;
    ++s_dl_revision;
    s_task = NULL;
    vTaskDelete(NULL);
}

static bool apps_worker_start(unsigned job) {
    if (s_task || (job != APP_JOB_CATALOG && job != APP_JOB_ALL && job != APP_JOB_SELECTED)) return false;
    s_dl_job=job;
    // These workers read NVS/FAT and write flash while the cache may be disabled.
    // Their stacks must stay in internal RAM; PSRAM stacks trip ESP-IDF's flash-cache guard.
    if (xTaskCreate(apps_worker, "pico-appdl", 8192,
                    (void *)(intptr_t)job, 3, &s_task) != pdPASS) {
        s_task = NULL;
        s_dl_state = DL_FAILED;
        ++s_dl_revision;
        return false;
    }
    return true;
}
#endif

bool ps_updates_check(void) {
    if (!read_pico_connectivity_online()) {
        s_state = FAILED;
        ++s_revision;
        return false;
    }
#ifdef ESP_PLATFORM
    if (s_task || s_dl_state == DL_BUSY || s_dl_state == DL_READY || s_dl_state == DL_INSTALLING) return false;
    s_state = BUSY;
    if (s_fw_state != FW_BUSY) s_fw_state = FW_IDLE;
    s_fw_meta_valid = false;
    s_fw_size = 0;
    s_fw_sha[0] = 0;
    ++s_revision;
    if (xTaskCreate(worker, "pico-update", 8192, NULL, 3, &s_task) != pdPASS) {
        s_task = NULL;
        s_state = FAILED;
        ++s_revision;
        return false;
    }
    return true;
#else
    s_state = FAILED;
    ++s_revision;
    return false;
#endif
}

bool ps_updates_download_apps(void) {
    if (!read_pico_connectivity_online()) return false;
#ifdef ESP_PLATFORM
    if (s_task || s_dl_state == DL_BUSY || s_dl_state == DL_READY || s_dl_state == DL_INSTALLING) return false;
    s_dl_index = s_dl_installed = s_dl_position = 0;
    s_dl_total = s_manifest_count;
    s_manifest_loaded = false;
    s_manifest_catalog_ready = false;
    s_dl_state = DL_BUSY;
    ++s_dl_revision;
    return apps_worker_start(APP_JOB_ALL);
#else
    return false;
#endif
}

uint8_t ps_updates_channel(void) {
    channel_load();
    return s_channel;
}

const char *ps_updates_channel_name(uint8_t channel) {
    return channel == PS_UPDATE_CHANNEL_TEST ? "测试" : "稳定";
}

bool ps_updates_channel_set(uint8_t channel) {
    if (channel != PS_UPDATE_CHANNEL_STABLE && channel != PS_UPDATE_CHANNEL_TEST) return false;
    channel_load();
    if (channel == s_channel) return true;
#ifdef ESP_PLATFORM
    if (s_task || s_fw_state == FW_BUSY || s_dl_state == DL_BUSY || s_dl_state == DL_READY || s_dl_state == DL_INSTALLING)
        return false;
#endif
    if (!ps_store("updchan1", &channel, sizeof(channel))) return false;
    s_channel = channel;
    // 频道切换后不复用旧目录或未完成选择。/ Discard the previous channel's catalog and pending selection.
    s_manifest_loaded = s_manifest_catalog_ready = false;
    s_manifest_count = 0;
    memset(s_manifest_selected, 0, sizeof(s_manifest_selected));
    s_dl_state = DL_IDLE;
    s_dl_index = s_dl_installed = s_dl_position = s_dl_total = 0;
    s_download_id[0] = 0;
    ++s_dl_revision;
    remove_firmware_cache();
    s_fw_state = FW_IDLE;
    s_fw_percent = 0;
    s_fw_size = 0;
    s_fw_meta_valid = false;
    s_fw_sha[0] = 0;
    ++s_fw_revision;
    s_state = IDLE;
    s_newer = false;
    snprintf(s_latest, sizeof(s_latest), "等待检查");
    ++s_revision;
    return true;
}

bool ps_updates_apps_catalog_refresh(void) {
#ifdef ESP_PLATFORM
    if (s_task || s_dl_state == DL_BUSY || s_dl_state == DL_READY || s_dl_state == DL_INSTALLING) return false;
#endif
    if (!read_pico_connectivity_online()) {
        s_dl_state = DL_FAILED;
        ++s_dl_revision;
        return false;
    }
#ifdef ESP_PLATFORM
    s_dl_state = DL_CATALOG;
    ++s_dl_revision;
    return apps_worker_start(APP_JOB_CATALOG);
#elif defined(PICO_NATIVE_PREVIEW)
    // 预览进程用固定应用目录检查真实选择 UI，不访问网络或硬件。
    // The preview process uses a fixed catalog to exercise the real selection UI without network or hardware.
    static const struct { const char *id; bool tool; } preview[] = {
        {"gomoku", false}, {"go", false}, {"xiangqi", false},
        {"chess", false}, {"2048", false}, {"snake", false},
        {"drawing-notes", true}, {"weread", true}, {"home-link", true}, {"printer-link", true}
    };
    s_manifest_count = (unsigned)(sizeof(preview) / sizeof(preview[0]));
    for (unsigned i = 0; i < s_manifest_count; i++) {
        snprintf(s_manifest_ids[i], sizeof(s_manifest_ids[i]), "%s", preview[i].id);
        s_manifest_tools[i] = preview[i].tool;
        s_manifest_remote_version[i]=!strcmp(preview[i].id,"home-link")?10109:
            !strcmp(preview[i].id,"printer-link")?10002:0;
    }
    s_manifest_loaded = true;
    snapshot_installed_apps();
    s_manifest_catalog_ready = true;
    s_apps = s_manifest_count;
    s_dl_state = DL_IDLE;
    ++s_dl_revision;
    return true;
#else
    return false;
#endif
}

unsigned ps_updates_apps_catalog(ps_updates_app_t *out, unsigned capacity) {
    if (!out || !capacity || !s_manifest_loaded || !s_manifest_catalog_ready)
        return 0;
    unsigned count = s_manifest_count < capacity ? s_manifest_count : capacity;
    for (unsigned i = 0; i < count; i++) {
        memset(&out[i], 0, sizeof(out[i]));
        snprintf(out[i].id, sizeof(out[i].id), "%s", s_manifest_ids[i]);
        out[i].tool = s_manifest_tools[i];
        out[i].installed = s_manifest_installed[i];
        out[i].local_version=s_manifest_local_version[i];
        out[i].remote_version=s_manifest_remote_version[i];
        out[i].update_available=s_manifest_installed[i]&&app_eligible(i);
    }
    return count;
}

bool ps_updates_download_apps_selected(const char *const *ids, unsigned count) {
    if (!ids || !count || !s_manifest_loaded || !s_manifest_catalog_ready ||
        !read_pico_connectivity_online()) return false;
#ifdef ESP_PLATFORM
    if (s_task || s_dl_state == DL_BUSY || s_dl_state == DL_READY || s_dl_state == DL_INSTALLING)
        return false;
    unsigned selected = 0;
    memset(s_manifest_selected, 0, sizeof(s_manifest_selected));
    for (unsigned i = 0; i < s_manifest_count; i++) {
        for (unsigned j = 0; j < count; j++) {
            if (ids[j] && !strcmp(ids[j], s_manifest_ids[i]) &&
                app_eligible(i)) {
                s_manifest_selected[i] = true;
                selected++;
                break;
            }
        }
    }
    if (!selected) return false;
    s_dl_index = s_dl_installed = s_dl_position = 0;
    s_dl_total = selected;
    s_dl_state = DL_BUSY;
    ++s_dl_revision;
    return apps_worker_start(APP_JOB_SELECTED);
#else
    return false;
#endif
}

bool ps_updates_download_firmware(void) {
    if (!read_pico_connectivity_online() || !s_newer || !s_fw_meta_valid) return false;
#ifdef ESP_PLATFORM
    if (s_task || s_fw_state == FW_BUSY || s_dl_state == DL_BUSY || s_dl_state == DL_READY ||
        s_dl_state == DL_INSTALLING) return false;
    if (!firmware_space_available(s_fw_size)) {
        s_fw_state = FW_FAILED;
        ++s_fw_revision;
        return false;
    }
    s_fw_state = FW_BUSY;
    s_fw_percent = 0;
    ++s_fw_revision;
    if (xTaskCreate(firmware_worker, "pico-fwdl", 8192, NULL, 3, &s_task) != pdPASS) {
        s_task = NULL;
        s_fw_state = FW_FAILED;
        ++s_fw_revision;
        return false;
    }
    return true;
#else
    return false;
#endif
}

bool ps_updates_firmware_ready(void) {
    return firmware_files_ready(NULL);
}

bool ps_updates_firmware_version(char *out,size_t bytes) {
    char version[33];
    if(!out||bytes<sizeof(version)||!ps_updates_firmware_ready()||!firmware_image_version(version))return false;
    snprintf(out,bytes,"%s",version);return true;
}

bool ps_updates_arm_firmware(void) {
#ifdef ESP_PLATFORM
    if (!ps_updates_firmware_ready() || s_task || s_fw_state == FW_BUSY || s_dl_state == DL_READY ||
        s_dl_state == DL_INSTALLING) return false;
    char version[33];
    if (!firmware_image_version(version) || !newer_than_current(version)) {
        remove_firmware_cache();
        return false;
    }
    const esp_partition_t *updater = esp_partition_find_first(
        ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_OTA_1, NULL);
    if (!updater || esp_ota_set_boot_partition(updater) != ESP_OK) return false;
    esp_restart();
#endif
    return false;
}

bool ps_updates_status(char *latest, size_t latest_bytes, bool *newer, unsigned *apps,
                       char *message, size_t message_bytes) {
    if (!latest || !latest_bytes) return false;
    snprintf(latest, latest_bytes, "%s", s_latest);
    if (newer) *newer = s_newer;
    if (apps) *apps = s_apps;
    if (message && message_bytes) {
        if (s_fw_state == FW_BUSY)
            snprintf(message, message_bytes, "下载固件 %u%% · 原系统保持运行",
                     s_fw_percent * 5);
        else if (s_fw_state == FW_FAILED)
            snprintf(message, message_bytes, "固件下载失败，点按重试");
        else if (ps_updates_firmware_ready())
            snprintf(message, message_bytes, "固件已就绪，点按安装并重启");
        else if (s_newer && !s_fw_meta_valid)
            snprintf(message, message_bytes, "网站缺少固件校验信息");
        else if (s_newer)
            snprintf(message, message_bytes, "发现新固件，点按下载");
        else snprintf(message, message_bytes, "%s", s_state == BUSY ? "正在检查系统更新" :
                      s_state == FAILED ? "检查失败，轻点重试" :
                      s_state == READY ? "系统已是最新版本" : "轻点检查系统更新");
    }
    return s_state != IDLE;
}

bool ps_updates_firmware_progress(unsigned *percent) {
    if (percent) *percent = s_fw_percent * 5;
    return s_fw_state == FW_BUSY && s_fw_size != 0;
}

bool ps_updates_apps_status(unsigned *catalog, char *message, size_t message_bytes) {
    if (catalog) *catalog = s_apps;
    if (message && message_bytes) {
        if (s_dl_state == DL_BUSY)
            snprintf(message, message_bytes, "正在下载 %u / %u · %s",
                     s_dl_position ? s_dl_position : 1, s_dl_total, s_download_id);
        else if (s_dl_state == DL_INSTALLING)
            snprintf(message, message_bytes, "正在安装 %u / %u · %s",
                     s_dl_position ? s_dl_position : 1, s_dl_total, s_download_id);
        else if (s_dl_state == DL_CATALOG)
            snprintf(message, message_bytes, "正在读取 XHFO.NET 应用目录");
        else if (s_dl_state == DL_READY)
            snprintf(message, message_bytes, "应用包已下载，正在准备安装");
        else if (s_dl_state == DL_DONE)
            snprintf(message, message_bytes, "%s", s_dl_installed ?
                     "所选应用已安装或更新，可立即打开" : "没有可安装的更新");
        else if (s_dl_state == DL_FAILED)
            snprintf(message, message_bytes, "应用同步失败，轻点重试");
        else if (s_manifest_loaded && s_manifest_catalog_ready) {
            unsigned updates=0,unknown=0;
            for(unsigned i=0;i<s_manifest_count;i++) {
                if(s_manifest_installed[i]&&app_eligible(i))updates++;
                if(s_manifest_installed[i]&&!s_manifest_remote_version[i])unknown++;
            }
            snprintf(message,message_bytes,unknown?"目录 %u 项 · %u 项可更新 · %u 项版本未确认":"目录 %u 项 · %u 项可更新",
                s_manifest_count,updates,unknown);
        }
        else if (s_apps)
            snprintf(message, message_bytes, "云端目录 %u 项 · 选择应用", s_apps);
        else
            snprintf(message, message_bytes, "连接 XHFO.NET 检查应用目录");
    }
    return s_state != IDLE || s_dl_state != DL_IDLE;
}

bool ps_updates_tick(void) {
    if (s_dl_state == DL_READY) {
        char staged[96];
        snprintf(staged, sizeof(staged), "/sdcard/apps/.staging/%s.tmp", s_download_id);
        s_dl_state = DL_INSTALLING;
        ++s_dl_revision;
        if (!ps_games_install(staged, true)) {
            s_dl_state = DL_FAILED;
        } else {
            s_dl_installed++;
            s_manifest_installed[s_dl_index]=true;
            s_manifest_local_version[s_dl_index]=s_manifest_remote_version[s_dl_index];
            s_dl_index++;
#ifdef ESP_PLATFORM
            s_dl_state = s_dl_index < s_manifest_count ? DL_BUSY : DL_DONE;
            if (s_dl_state == DL_BUSY) (void)apps_worker_start(s_dl_job);
#else
            s_dl_state = DL_DONE;
#endif
        }
        ++s_dl_revision;
        return true;
    }
    if (s_dl_revision != s_dl_seen) {
        s_dl_seen = s_dl_revision;
        return true;
    }
    if (s_fw_revision != s_fw_seen) {
        s_fw_seen = s_fw_revision;
        return true;
    }
    if (s_revision != s_seen) {
        s_seen = s_revision;
        return true;
    }
    if (s_dl_state == DL_IDLE && s_dl_state != DL_DONE &&
        !s_auto && ps_now() >= 8000 && ps_background_io_allowed() &&
        read_pico_connectivity_online()) {
        s_auto = true;
        return ps_updates_check();
    }
    return false;
}

/*
 * SPDX-FileCopyrightText: 2026 XH Lab
 * SPDX-License-Identifier: Apache-2.0
 * 中文：PGA 家庭桥——NVS/HTTP/TLS/快照；不实现 manage web（薄宿主仍 503）。
 * English: PGA home bridge — NVS/HTTP/TLS/snapshot; does not implement manage web (thin host stays 503).
 * 修复：配置+快照+令牌约 52KiB，禁止放内部 BSS 或小栈。dev66 静态放入内部 DRAM 后，
 * USB MSC 的 DMA 分配失败，RPICO STATUS 报 usb_error 257（0x101 ESP_ERR_NO_MEM，不是 262 NOT_SUPPORTED）。
 * 大对象只从 PSRAM 分配；HTTP 工作任务保留小的内部栈（TLS 会关 cache，栈不能在 PSRAM）。
 * Fix: config+snapshot+tokens are ~52KiB and must not sit in internal BSS or on a small stack.
 * dev66's internal DRAM copies made USB MSC DMA allocation fail (usb_error 257 / ESP_ERR_NO_MEM).
 * Large objects come only from PSRAM. The HTTP worker keeps a small internal stack because TLS disables cache.
 */
#include "pico_home_pga.h"
#include "read_pico_home_link.h"
#include "read_pico_management.h"
#include "pico_system.h"
#include "pico_datetime.h"
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "nvs.h"
#include <stdio.h>
#include <string.h>

/* dev66 常驻内部 BSS：config 16276 + snap 33880 + 3*token 1040 = 53276。工作函数再在 8192 栈上复制这两份。 */
/* dev66 internal BSS: config 16276 + snap 33880 + 3*token 1040 = 53276, then copied again on an 8192-byte stack. */
typedef struct {
    rp_home_config_t config;
    rp_home_snapshot_t snap;
    rp_home_config_t job_config;
    rp_home_snapshot_t job_snap;
    rp_home_token_t tokens[RP_HOME_PRINTERS];
    rp_home_config_t edit;
    uint8_t packed[RP_HOME_CONFIG_PACK_LIMIT];
} pga_store_t;

static SemaphoreHandle_t s_lock;
static TaskHandle_t s_worker;
static pga_store_t *s_store;
static bool s_ready, s_queued, s_busy, s_online, s_at_home, s_stop, s_portal_owner;
#define s_config (s_store->config)
#define s_snap (s_store->snap)
#define s_tokens (s_store->tokens)

_Static_assert(sizeof(rp_home_config_t) + sizeof(rp_home_snapshot_t) + 3 * sizeof(rp_home_token_t) > 50000,
               "home PGA state must stay out of internal DRAM");
static char s_input[256];
static bool s_input_ready, s_input_open;
static int s_nav_edit; /* set by games when opening edit — unused here; games drives IME */

static int64_t now_ms(void) { return esp_timer_get_time() / 1000; }

static void lock(void) { if (s_lock) xSemaphoreTake(s_lock, portMAX_DELAY); }
static void unlock(void) { if (s_lock) xSemaphoreGive(s_lock); }

static bool tls_clock_ok(void) {
    ps_datetime_status_t st;
    ps_datetime_get_status(&st);
    return st.source == PS_TIME_NETWORK || st.source == PS_TIME_MANUAL || st.source == PS_TIME_RTC;
}

static bool nvs_blob(bool writing, void *data, size_t *bytes) {
    nvs_handle_t h;
    if (nvs_open("pico_home_pga", writing ? NVS_READWRITE : NVS_READONLY, &h) != ESP_OK) return false;
    esp_err_t err = writing ? nvs_set_blob(h, "settings", data, *bytes)
                            : nvs_get_blob(h, "settings", data, bytes);
    if (writing && err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
    return err == ESP_OK;
}

static bool save_config_locked(void) {
    size_t n = rp_home_config_pack(&s_config, s_store->packed, sizeof(s_store->packed));
    if (!n) return false;
    size_t bytes = n;
    bool ok = nvs_blob(true, s_store->packed, &bytes);
    memset(s_store->packed, 0, sizeof(s_store->packed));
    return ok;
}

static void load_config(void) {
    size_t bytes = sizeof(s_store->packed);
    memset(&s_config, 0, sizeof(s_config));
    s_config.version = RP_HOME_CONFIG_VERSION;
    if (!nvs_blob(false, s_store->packed, &bytes) || !bytes) {
        memset(s_store->packed, 0, sizeof(s_store->packed));
        return;
    }
    if (rp_home_config_unpack(s_store->packed, bytes, &s_store->edit) && rp_home_config_valid(&s_store->edit))
        s_config = s_store->edit;
    memset(s_store->packed, 0, sizeof(s_store->packed));
    memset(&s_store->edit, 0, sizeof(s_store->edit));
}

static bool find_bytes(const char *hay, size_t n, const char *needle) {
    size_t m = strlen(needle);
    if (!hay || !m || n < m) return false;
    for (size_t i = 0; i + m <= n; i++)
        if (!memcmp(hay + i, needle, m)) return true;
    return false;
}

static bool access_challenge(int status, const char *body, size_t n) {
    if ((status == 302 || status == 401 || status == 403) &&
        (find_bytes(body, n, "cloudflareaccess.com") || find_bytes(body, n, "Cloudflare Access") ||
         find_bytes(body, n, "CF_AppSession") || find_bytes(body, n, "access.cloudflare") ||
         (n && body[0] == '<' && (find_bytes(body, n, "cloudflare") || find_bytes(body, n, "login")))))
        return true;
    if (status >= 200 && status < 400 && n > 32 && body[0] == '<' &&
        (find_bytes(body, n, "cloudflareaccess.com") || find_bytes(body, n, "Cloudflare Access")))
        return true;
    return false;
}

static rp_home_result_t http_request(void *unused, const rp_home_http_request_t *r, char *response, size_t capacity,
                                     size_t *used, int *status) {
    (void)unused;
    *used = 0;
    *status = 0;
    if (!r || !r->url || !response || capacity < 2) return RP_HOME_INVALID;
    if (!strncmp(r->url, "https://", 8) && !tls_clock_ok()) return RP_HOME_TIME;
    esp_http_client_config_t cfg = {.url = r->url,
                                    .timeout_ms = 8000,
                                    .buffer_size = 1024,
                                    .crt_bundle_attach = esp_crt_bundle_attach,
                                    .disable_auto_redirect = true,
                                    .method = r->method && !strcmp(r->method, "POST") ? HTTP_METHOD_POST : HTTP_METHOD_GET};
    esp_http_client_handle_t h = esp_http_client_init(&cfg);
    if (!h) return RP_HOME_NOMEM;
    char auth[280];
    if (r->bearer && *r->bearer) {
        snprintf(auth, sizeof(auth), "Bearer %s", r->bearer);
        esp_http_client_set_header(h, "Authorization", auth);
        memset(auth, 0, sizeof(auth));
    }
    if (r->content_type) esp_http_client_set_header(h, "Content-Type", r->content_type);
    if (r->cf_access_client_id && *r->cf_access_client_id && r->cf_access_client_secret && *r->cf_access_client_secret) {
        esp_http_client_set_header(h, "CF-Access-Client-Id", r->cf_access_client_id);
        esp_http_client_set_header(h, "CF-Access-Client-Secret", r->cf_access_client_secret);
    }
    size_t length = r->body ? strlen(r->body) : 0;
    rp_home_result_t result = RP_HOME_OFFLINE;
    if (esp_http_client_open(h, (int)length) == ESP_OK) {
        size_t sent = 0;
        while (sent < length) {
            int n = esp_http_client_write(h, r->body + sent, (int)(length - sent));
            if (n <= 0) break;
            sent += (size_t)n;
        }
        if (sent == length) {
            int64_t declared = esp_http_client_fetch_headers(h);
            *status = esp_http_client_get_status_code(h);
            if (declared >= (int64_t)capacity) result = RP_HOME_TOO_LARGE;
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
    }
    esp_http_client_cleanup(h);
    if (result == RP_HOME_OK && access_challenge(*status, response, *used)) return RP_HOME_ACCESS;
    return result;
}

static rp_home_result_t mqtt_unsupported(void *c, const rp_home_printer_config_t *p, char *b, size_t n, size_t *u) {
    (void)c; (void)p; (void)b; (void)n;
    if (u) *u = 0;
    return RP_HOME_UNSUPPORTED;
}

static void fill_status(pico_app_home_status_t *st) {
    memset(st, 0, sizeof(*st));
    st->bytes = sizeof(*st);
    st->revision = s_snap.revision;
    st->entity_count = s_snap.entity_count;
    st->entity_total = s_snap.entity_total;
    st->printer_count = 0;
    for (unsigned i = 0; i < RP_HOME_PRINTERS; i++)
        if (s_config.printers[i].source != RP_HOME_NONE || s_snap.printers[i].has_data) st->printer_count++;
    st->result = (uint8_t)s_snap.result;
    st->busy = s_busy || s_snap.busy;
    st->external = s_snap.external;
    st->at_home = s_at_home;
    st->enabled = s_config.enabled;
    st->has_token = *s_config.ha_token != 0;
    st->has_cf = *s_config.cf_access_client_id && *s_config.cf_access_client_secret;
    st->http_status = s_snap.http_status;
    int wifi = ps_wifi_connected_index();
    if (wifi >= 0) st->flags |= PGA_HOME_WIFI;
    if (st->has_token || *s_config.local || *s_config.external) st->flags |= PGA_HOME_CFG;
    if (st->busy) st->flags |= PGA_HOME_BUSY;
    if (st->external) st->flags |= PGA_HOME_EXTERNAL;
    if (st->has_cf) st->flags |= PGA_HOME_CF;
    if (s_portal_owner) st->flags |= PGA_HOME_PORTAL;
    if (read_pico_management_active()) st->flags |= PGA_HOME_MANAGE;
    const char *text = rp_home_result_text((rp_home_result_t)st->result);
    snprintf(st->error, sizeof(st->error), "%s", text ? text : "");
}

static void worker(void *unused) {
    (void)unused;
    for (;;) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        if (s_stop) break;
        lock();
        bool run = s_queued;
        bool online = s_online, at_home = s_at_home;
        if (run) memcpy(&s_store->job_config, &s_config, sizeof(s_store->job_config));
        s_queued = false;
        if (run) s_busy = true;
        unlock();
        if (!run) continue;
        memset(&s_store->job_snap, 0, sizeof(s_store->job_snap));
        rp_home_transport_t transport = {http_request, mqtt_unsupported, NULL};
        /* rp_home_fetch allocates its own PSRAM response (RP_HOME_HTTP_LIMIT). Do not malloc a second one from internal RAM. */
        (void)rp_home_fetch(&s_store->job_config, &s_store->job_snap, s_tokens, &transport, online, at_home, now_ms());
        lock();
        uint32_t rev = s_snap.revision + 1;
        s_snap = s_store->job_snap;
        s_snap.revision = rev;
        s_busy = false;
        unlock();
        memset(&s_store->job_config, 0, sizeof(s_store->job_config));
        memset(&s_store->job_snap, 0, sizeof(s_store->job_snap));
    }
    s_worker = NULL;
    vTaskDelete(NULL);
}

void ps_home_pga_init(void) {
    if (s_ready && s_worker) return;
    if (!s_lock) s_lock = xSemaphoreCreateMutex();
    if (!s_lock) return;
    if (!s_store) {
        /* No internal-RAM fallback: a failed PSRAM alloc must not steal the DMA heap USB MSC needs. */
        s_store = heap_caps_calloc(1, sizeof(*s_store), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!s_store) return;
        load_config();
        memset(&s_snap, 0, sizeof(s_snap));
        s_snap.revision = 1;
        s_snap.result = RP_HOME_EMPTY;
        s_ready = true;
    }
    if (!s_worker && xTaskCreate(worker, "home_pga", 8192, NULL, 3, &s_worker) != pdPASS)
        s_worker = NULL;
}

bool ps_home_pga_active(void) { return s_ready; }

bool ps_home_pga_command(uint32_t command, pico_app_home_status_t *status) {
    ps_home_pga_init();
    if (!s_ready || !status || status->bytes < sizeof(*status)) {
        if (status) status->bytes = sizeof(*status);
        return false;
    }
    int wifi = ps_wifi_connected_index();
    bool online = wifi >= 0;
    bool at_home = online && *s_config.home_wifi && !strcmp(s_config.home_wifi, ps_wifi_name(wifi));
    if (command == PGA_HOME_SYNC && s_worker) {
        lock();
        s_online = online;
        s_at_home = at_home;
        s_queued = true;
        s_busy = true;
        unlock();
        xTaskNotifyGive(s_worker);
    } else if (command == PGA_HOME_SYNC) {
        lock();
        s_online = online;
        s_at_home = at_home;
        unlock();
    }
    lock();
    s_at_home = at_home;
    fill_status(status);
    unlock();
    return true;
}

bool ps_home_pga_config(uint32_t command, pico_app_home_config_t *config) {
    ps_home_pga_init();
    if (!s_ready || !config || config->bytes < sizeof(*config)) {
        if (config) config->bytes = sizeof(*config);
        return false;
    }
    if (command == PGA_HOME_CONFIG_GET) {
        lock();
        memset(config, 0, sizeof(*config));
        config->bytes = sizeof(*config);
        config->enabled = s_config.enabled;
        config->external_enabled = s_config.external_enabled;
        config->external_control = s_config.external_control;
        config->has_token = *s_config.ha_token != 0;
        config->has_cf_id = *s_config.cf_access_client_id != 0;
        config->has_cf_secret = *s_config.cf_access_client_secret != 0;
        memcpy(config->local, s_config.local, sizeof(config->local));
        memcpy(config->external, s_config.external, sizeof(config->external));
        memcpy(config->home_wifi, s_config.home_wifi, sizeof(config->home_wifi));
        unlock();
        return true;
    }
    if (command != PGA_HOME_CONFIG_SET) return false;
    lock();
    if (s_portal_owner || read_pico_management_active()) {
        unlock();
        memset(config->ha_token, 0, sizeof(config->ha_token));
        memset(config->cf_access_client_id, 0, sizeof(config->cf_access_client_id));
        memset(config->cf_access_client_secret, 0, sizeof(config->cf_access_client_secret));
        return false;
    }
    rp_home_config_t *next = &s_store->edit;
    *next = s_config;
    next->version = RP_HOME_CONFIG_VERSION;
    next->enabled = config->enabled;
    next->external_enabled = config->external_enabled;
    next->external_control = config->external_control;
    snprintf(next->local, sizeof(next->local), "%s", config->local);
    snprintf(next->external, sizeof(next->external), "%s", config->external);
    snprintf(next->home_wifi, sizeof(next->home_wifi), "%s", config->home_wifi);
    if (config->clear_token) memset(next->ha_token, 0, sizeof(next->ha_token));
    else if (config->ha_token[0]) {
        memset(next->ha_token, 0, sizeof(next->ha_token));
        snprintf(next->ha_token, sizeof(next->ha_token), "%s", config->ha_token);
    }
    if (config->clear_cf) {
        memset(next->cf_access_client_id, 0, sizeof(next->cf_access_client_id));
        memset(next->cf_access_client_secret, 0, sizeof(next->cf_access_client_secret));
    } else {
        if (config->cf_access_client_id[0])
            snprintf(next->cf_access_client_id, sizeof(next->cf_access_client_id), "%s", config->cf_access_client_id);
        if (config->cf_access_client_secret[0])
            snprintf(next->cf_access_client_secret, sizeof(next->cf_access_client_secret), "%s",
                     config->cf_access_client_secret);
    }
    if (!rp_home_config_valid(next)) {
        unlock();
        memset(next, 0, sizeof(*next));
        return false;
    }
    s_config = *next;
    bool ok = save_config_locked();
    unlock();
    memset(next, 0, sizeof(*next));
    memset(config->ha_token, 0, sizeof(config->ha_token));
    memset(config->cf_access_client_id, 0, sizeof(config->cf_access_client_id));
    memset(config->cf_access_client_secret, 0, sizeof(config->cf_access_client_secret));
    return ok;
}

bool ps_home_pga_entity(uint32_t index, pico_app_home_entity_t *out) {
    ps_home_pga_init();
    if (!s_ready || !out || out->bytes < sizeof(*out)) {
        if (out) out->bytes = sizeof(*out);
        return false;
    }
    lock();
    if (index >= s_snap.entity_count) {
        unlock();
        return false;
    }
    const rp_home_entity_t *e = &s_snap.entities[index];
    memset(out, 0, sizeof(*out));
    out->bytes = sizeof(*out);
    out->index = index;
    snprintf(out->id, sizeof(out->id), "%s", e->id);
    snprintf(out->name, sizeof(out->name), "%s", e->name);
    snprintf(out->state, sizeof(out->state), "%s", e->state);
    snprintf(out->unit, sizeof(out->unit), "%s", e->unit);
    snprintf(out->room, sizeof(out->room), "%s", e->room);
    out->kind = e->kind;
    out->available = e->available;
    out->on = e->on;
    out->number = e->number;
    unlock();
    return true;
}

bool ps_home_pga_printer(uint32_t slot, pico_app_home_printer_t *out) {
    ps_home_pga_init();
    if (!s_ready || !out || out->bytes < sizeof(*out) || slot >= RP_HOME_PRINTERS) {
        if (out) out->bytes = sizeof(*out);
        return false;
    }
    lock();
    const rp_home_printer_t *p = &s_snap.printers[slot];
    memset(out, 0, sizeof(*out));
    out->bytes = sizeof(*out);
    out->slot = slot;
    snprintf(out->name, sizeof(out->name), "%s", p->name[0] ? p->name : s_config.printers[slot].name);
    snprintf(out->state, sizeof(out->state), "%s", p->state);
    snprintf(out->task, sizeof(out->task), "%s", p->task);
    out->percent = p->percent;
    out->remaining_minutes = p->remaining_minutes;
    out->source = p->source ? p->source : s_config.printers[slot].source;
    out->connected = p->connected;
    out->has_data = p->has_data;
    out->result = (uint8_t)p->result;
    out->http_status = p->http_status;
    unlock();
    return true;
}

bool ps_home_pga_control(const char *entity_id, const char *action, float value) {
    ps_home_pga_init();
    if (!s_ready || !entity_id || !action || !*entity_id) return false;
    lock();
    rp_home_entity_t entity;
    memset(&entity, 0, sizeof(entity));
    bool found = false;
    for (unsigned i = 0; i < s_snap.entity_count; i++) {
        if (!strcmp(s_snap.entities[i].id, entity_id)) {
            entity = s_snap.entities[i];
            found = true;
            break;
        }
    }
    memcpy(&s_store->edit, &s_config, sizeof(s_store->edit));
    bool at_home = s_at_home;
    unlock();
    if (!found) {
        memset(&s_store->edit, 0, sizeof(s_store->edit));
        return false;
    }
    rp_home_transport_t transport = {http_request, mqtt_unsupported, NULL};
    int status = 0;
    rp_home_result_t result = rp_home_control(&s_store->edit, &entity, action, value, &transport, at_home, &status);
    memset(&s_store->edit, 0, sizeof(s_store->edit));
    if (result == RP_HOME_OK) {
        if (s_worker) {
            lock();
            s_queued = true;
            s_busy = true;
            unlock();
            xTaskNotifyGive(s_worker);
        }
        return true;
    }
    return false;
}

bool ps_home_pga_input(uint32_t command, char *value, size_t capacity) {
    if (command == PGA_HOME_INPUT_CANCEL) {
        s_input_ready = s_input_open = false;
        memset(s_input, 0, sizeof(s_input));
        return true;
    }
    if (!value || !capacity) return false;
    if (command == PGA_HOME_INPUT_OPEN) {
        size_t n = strnlen(value, capacity < sizeof(s_input) ? capacity : sizeof(s_input));
        if (n >= capacity || n >= sizeof(s_input) || s_input_open) return false;
        memcpy(s_input, value, n + 1);
        s_input_ready = false;
        s_input_open = true;
        return true;
    }
    if (command != PGA_HOME_INPUT_TAKE || !s_input_ready || strlen(s_input) >= capacity) return false;
    memcpy(value, s_input, strlen(s_input) + 1);
    s_input_ready = s_input_open = false;
    memset(s_input, 0, sizeof(s_input));
    return true;
}

void ps_home_pga_input_done(const char *text) {
    if (!s_input_open) return;
    snprintf(s_input, sizeof(s_input), "%s", text ? text : "");
    s_input_ready = true;
}

bool ps_home_pga_portal_load(rp_home_config_t *config, rp_home_snapshot_t *snapshot) {
    ps_home_pga_init();
    if (!s_ready || !config || !snapshot) return false;
    lock();
    *config = s_config;
    *snapshot = s_snap;
    unlock();
    return true;
}

bool ps_home_pga_portal_commit(const rp_home_config_t *config, bool *queued) {
    if (queued) *queued = false;
    ps_home_pga_init();
    if (!s_ready || !config || !rp_home_config_valid(config)) return false;
    int wifi = ps_wifi_connected_index();
    bool online = wifi >= 0;
    bool home = online && config->home_wifi[0] && !strcmp(config->home_wifi, ps_wifi_name(wifi));
    lock();
    s_store->edit = s_config;
    s_config = *config;
    bool ok = save_config_locked();
    if (!ok) s_config = s_store->edit;
    memset(&s_store->edit, 0, sizeof(s_store->edit));
    bool queue = ok && s_worker && online && s_config.enabled;
    if (queue) {
        s_online = online;
        s_at_home = home;
        s_queued = true;
        s_busy = true;
    }
    unlock();
    if (queue && s_worker) xTaskNotifyGive(s_worker);
    if (queued) *queued = queue;
    return ok;
}

void ps_home_pga_portal_set_owner(bool portal_owns_writes) {
    ps_home_pga_init();
    if (!s_lock) return;
    lock();
    s_portal_owner = portal_owns_writes;
    unlock();
}

bool ps_home_pga_suspend(uint32_t timeout_ms) {
    int64_t deadline = now_ms() + (timeout_ms ? timeout_ms : 2500);
    while (now_ms() < deadline) {
        lock();
        bool busy = s_busy || s_queued;
        unlock();
        if (!busy) return true;
        vTaskDelay(pdMS_TO_TICKS(50));
    }
    lock();
    bool busy = s_busy || s_queued;
    unlock();
    return !busy;
}

void ps_home_pga_resume(void) {}

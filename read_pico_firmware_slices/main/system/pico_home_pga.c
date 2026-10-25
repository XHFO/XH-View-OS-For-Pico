/*
 * SPDX-FileCopyrightText: 2026 XH Lab
 * SPDX-License-Identifier: Apache-2.0
 * 中文：PGA 家庭桥——NVS/HTTP/TLS/快照；不实现 manage web（薄宿主仍 503）。
 * English: PGA home bridge — NVS/HTTP/TLS/snapshot; does not implement manage web (thin host stays 503).
 */
#include "pico_home_pga.h"
#include "read_pico_home_link.h"
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
#include "pico_app_package.h"
#include "esp_attr.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static SemaphoreHandle_t s_lock;
static TaskHandle_t s_worker;
static EXT_RAM_BSS_ATTR rp_home_config_t s_config;
static EXT_RAM_BSS_ATTR rp_home_snapshot_t s_snap;
static EXT_RAM_BSS_ATTR rp_home_token_t s_tokens[RP_HOME_PRINTERS];
static bool s_ready, s_queued, s_busy, s_online, s_at_home, s_stop;
static bool s_ctrl_pending;
static char s_ctrl_action[24];
static float s_ctrl_value;
static rp_home_entity_t s_ctrl_entity;
static char s_input[256];
static bool s_input_ready, s_input_open;

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

static void *spiram(size_t n) {
    void *p = heap_caps_calloc(1, n, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    return p ? p : calloc(1, n);
}

static bool save_config_locked(void) {
    uint8_t *packed = spiram(RP_HOME_CONFIG_PACK_LIMIT);
    if (!packed) return false;
    size_t n = rp_home_config_pack(&s_config, packed, RP_HOME_CONFIG_PACK_LIMIT);
    size_t bytes = n;
    bool ok = n && nvs_blob(true, packed, &bytes);
    memset(packed, 0, RP_HOME_CONFIG_PACK_LIMIT);
    free(packed);
    return ok;
}

static void load_config(void) {
    memset(&s_config, 0, sizeof(s_config));
    s_config.version = RP_HOME_CONFIG_VERSION;
    uint8_t *packed = spiram(RP_HOME_CONFIG_PACK_LIMIT);
    rp_home_config_t *tmp = spiram(sizeof(*tmp));
    size_t bytes = RP_HOME_CONFIG_PACK_LIMIT;
    if (packed && tmp && nvs_blob(false, packed, &bytes) && bytes && rp_home_config_unpack(packed, bytes, tmp) &&
        rp_home_config_valid(tmp))
        s_config = *tmp;
    if (tmp) {
        memset(tmp, 0, sizeof(*tmp));
        free(tmp);
    }
    if (packed) {
        memset(packed, 0, RP_HOME_CONFIG_PACK_LIMIT);
        free(packed);
    }
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
    /* 模型只在外部/远程 HA 请求上填入 Access 头；本地路径必须保持 NULL。/ Model sets Access headers on remote HA only. */
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

static void sample_network_locked(void) {
    int wifi = ps_wifi_connected_index();
    s_online = wifi >= 0;
    s_at_home = s_online && s_config.home_wifi[0] && !strcmp(s_config.home_wifi, ps_wifi_name(wifi));
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
    const char *text = rp_home_result_text((rp_home_result_t)st->result);
    snprintf(st->error, sizeof(st->error), "%s", text ? text : "");
}

static void worker(void *unused) {
    (void)unused;
    rp_home_config_t *config = spiram(sizeof(*config));
    rp_home_snapshot_t *snap = spiram(sizeof(*snap));
    if (!config || !snap) {
        free(config);
        free(snap);
        lock();
        s_busy = s_queued = s_ctrl_pending = false;
        s_snap.result = RP_HOME_NOMEM;
        s_snap.busy = 0;
        s_snap.revision++;
        unlock();
        s_worker = NULL;
        vTaskDelete(NULL);
        return;
    }
    for (;;) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        for (;;) {
            lock();
            if (s_stop) {
                unlock();
                goto done;
            }
            bool ctrl = s_ctrl_pending;
            bool run = s_queued || ctrl;
            if (!run) {
                s_busy = false;
                s_snap.busy = 0;
                unlock();
                break;
            }
            bool online = s_online, at_home = s_at_home;
            memcpy(config, &s_config, sizeof(*config));
            rp_home_entity_t entity = s_ctrl_entity;
            char action[sizeof(s_ctrl_action)];
            memcpy(action, s_ctrl_action, sizeof(action));
            float value = s_ctrl_value;
            memcpy(snap, &s_snap, sizeof(*snap));
            if (ctrl) s_ctrl_pending = false;
            s_queued = false;
            s_busy = true;
            s_snap.busy = 1;
            unlock();
            rp_home_transport_t transport = {http_request, mqtt_unsupported, NULL};
            int status = 0;
            rp_home_result_t result = RP_HOME_OK;
            if (ctrl)
                result = rp_home_control(config, &entity, action, value, &transport, at_home, &status);
            if (result == RP_HOME_OK)
                rp_home_fetch(config, snap, s_tokens, &transport, online, at_home, now_ms());
            else {
                snap->result = result;
                snap->http_status = status;
            }
            lock();
            if (!s_stop) {
                uint32_t rev = s_snap.revision + 1;
                s_snap = *snap;
                s_snap.revision = rev;
                s_snap.busy = 0;
                s_busy = s_queued || s_ctrl_pending;
            }
            unlock();
            memset(config, 0, sizeof(*config));
            memset(&entity, 0, sizeof(entity));
            memset(action, 0, sizeof(action));
        }
    }
done:
    memset(config, 0, sizeof(*config));
    free(config);
    free(snap);
    s_worker = NULL;
    vTaskDelete(NULL);
}

void ps_home_pga_init(void) {
    if (s_ready) return;
    if (!s_lock) s_lock = xSemaphoreCreateMutex();
    load_config();
    memset(&s_snap, 0, sizeof(s_snap));
    s_snap.revision = 1;
    s_snap.result = RP_HOME_EMPTY;
    for (unsigned i = 0; i < RP_HOME_PRINTERS; i++)
        s_snap.printers[i].percent = s_snap.printers[i].remaining_minutes = s_snap.printers[i].layer =
            s_snap.printers[i].layers = s_snap.printers[i].nozzle = s_snap.printers[i].bed = -1;
    s_ready = true;
    if (!s_worker)
        xTaskCreate(worker, "home_pga", 8192, NULL, 3, &s_worker);
}

bool ps_home_pga_active(void) { return s_ready; }

bool ps_home_link_package_installed(void) {
    pico_app_package_t pkg;
    char reason[80];
    if (!pico_app_package_read(PICO_APP_TOOLS_DIRECTORY "/home-link.pga", &pkg, false, reason, sizeof(reason)))
        return false;
    return pkg.abi == PICO_APP_ABI && pkg.kind == PGA_HOME_LINK && !strcmp(pkg.id, "home-link");
}

bool ps_home_pga_command(uint32_t command, pico_app_home_status_t *status) {
    ps_home_pga_init();
    if (!status || status->bytes < sizeof(*status)) {
        if (status) status->bytes = sizeof(*status);
        return false;
    }
    lock();
    sample_network_locked();
    if (command == PGA_HOME_SYNC) {
        s_queued = true;
        s_busy = true;
        s_snap.busy = 1;
    }
    bool notify = command == PGA_HOME_SYNC;
    fill_status(status);
    unlock();
    if (notify && s_worker) xTaskNotifyGive(s_worker);
    return true;
}

bool ps_home_pga_config(uint32_t command, pico_app_home_config_t *config) {
    ps_home_pga_init();
    if (!config || config->bytes < PICO_APP_HOME_CONFIG_V10100_BYTES) {
        if (config) config->bytes = sizeof(*config);
        return false;
    }
    uint32_t cap = config->bytes;
    if (command == PGA_HOME_CONFIG_GET) {
        pico_app_home_config_t tmp;
        memset(&tmp, 0, sizeof(tmp));
        tmp.bytes = sizeof(tmp);
        lock();
        tmp.enabled = s_config.enabled;
        tmp.external_enabled = s_config.external_enabled;
        tmp.external_control = s_config.external_control;
        tmp.has_token = *s_config.ha_token != 0;
        tmp.has_cf_id = *s_config.cf_access_client_id != 0;
        tmp.has_cf_secret = *s_config.cf_access_client_secret != 0;
        memcpy(tmp.local, s_config.local, sizeof(tmp.local));
        memcpy(tmp.external, s_config.external, sizeof(tmp.external));
        memcpy(tmp.home_wifi, s_config.home_wifi, sizeof(tmp.home_wifi));
        for (unsigned i = 0; i < 3 && i < RP_HOME_PRINTERS; i++) {
            tmp.printer_source[i] = s_config.printers[i].source;
            snprintf(tmp.printer_name[i], sizeof(tmp.printer_name[i]), "%s", s_config.printers[i].name);
            snprintf(tmp.printer_serial[i], sizeof(tmp.printer_serial[i]), "%s", s_config.printers[i].serial);
        }
        unlock();
        size_t n = cap < sizeof(tmp) ? cap : sizeof(tmp);
        memcpy(config, &tmp, n);
        config->bytes = sizeof(tmp);
        memset(&tmp, 0, sizeof(tmp));
        return true;
    }
    if (command != PGA_HOME_CONFIG_SET) return false;
    rp_home_link_update_t upd;
    memset(&upd, 0, sizeof(upd));
    upd.enabled = config->enabled ? 1 : 0;
    upd.external_enabled = config->external_enabled ? 1 : 0;
    upd.external_control = config->external_control ? 1 : 0;
    upd.clear_token = config->clear_token ? 1 : 0;
    upd.clear_cf = config->clear_cf ? 1 : 0;
    upd.local = config->local;
    upd.external = config->external;
    upd.home_wifi = config->home_wifi;
    upd.ha_token = config->ha_token;
    upd.cf_id = config->cf_access_client_id;
    upd.cf_secret = config->cf_access_client_secret;
    upd.has_printers = cap >= sizeof(*config);
    if (upd.has_printers) {
        for (unsigned i = 0; i < RP_HOME_PRINTERS; i++) {
            upd.printer_source[i] = config->printer_source[i];
            snprintf(upd.printer_name[i], sizeof(upd.printer_name[i]), "%s", config->printer_name[i]);
            snprintf(upd.printer_serial[i], sizeof(upd.printer_serial[i]), "%s", config->printer_serial[i]);
        }
    }
    rp_home_config_t *next = spiram(sizeof(*next));
    bool ok = false;
    if (next) {
        lock();
        *next = s_config;
        ok = rp_home_apply_link_update(next, &upd);
        if (ok) {
            rp_home_config_t *previous = spiram(sizeof(*previous));
            if (!previous) ok = false;
            else {
                *previous = s_config;
                s_config = *next;
                ok = save_config_locked();
                if (!ok) s_config = *previous;
                memset(previous, 0, sizeof(*previous));
                free(previous);
            }
        }
        unlock();
        memset(next, 0, sizeof(*next));
        free(next);
    }
    memset(&upd, 0, sizeof(upd));
    memset(config->ha_token, 0, sizeof(config->ha_token));
    memset(config->cf_access_client_id, 0, sizeof(config->cf_access_client_id));
    memset(config->cf_access_client_secret, 0, sizeof(config->cf_access_client_secret));
    return ok;
}

bool ps_home_pga_entity(uint32_t index, pico_app_home_entity_t *out) {
    ps_home_pga_init();
    if (!out || out->bytes < sizeof(*out)) {
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
    if (!out || out->bytes < sizeof(*out) || slot >= RP_HOME_PRINTERS) {
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
    if (!entity_id || !action || !*entity_id || strlen(action) >= sizeof(s_ctrl_action)) return false;
    lock();
    if (s_ctrl_pending) {
        unlock();
        return false;
    }
    bool found = false;
    for (unsigned i = 0; i < s_snap.entity_count; i++) {
        if (!strcmp(s_snap.entities[i].id, entity_id)) {
            s_ctrl_entity = s_snap.entities[i];
            found = true;
            break;
        }
    }
    if (!found) {
        unlock();
        return false;
    }
    snprintf(s_ctrl_action, sizeof(s_ctrl_action), "%s", action);
    s_ctrl_value = value;
    s_ctrl_pending = true;
    s_queued = true;
    s_busy = true;
    s_snap.busy = 1;
    sample_network_locked();
    unlock();
    if (s_worker) xTaskNotifyGive(s_worker);
    return true;
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

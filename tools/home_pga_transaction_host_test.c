/* SPDX-License-Identifier: Apache-2.0
 * Real production PGA transactions plus real model pack/validation; only NVS,
 * scheduler, clock, and other hardware services are deterministic adapters.
 */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "pico_app_package.h"
#include "pico_home_pga_under_test.c"
void ps_toast(const char *s){(void)s;}

enum { FAIL_NONE, FAIL_OPEN, FAIL_SET, FAIL_COMMIT };
static int failure, lock_depth, notifications, allocation_count;
static int legacy_reads, store_writes, read_failure, http_calls, http_code = 200, worker_injection;
static bool http_allowed;
static size_t http_offset;
static const char *http_body = "[]";
static uint8_t legacy[RP_HOME_CONFIG_PACK_LIMIT];
static size_t legacy_bytes;
static char authorization[1100];
static bool management, wifi, inspect_transaction;
static bool wifi_suspended;
static const char *wifi_ssid = "Home WiFi";
static int discovery_calls, discovery_mode;
static uint8_t committed[RP_HOME_CONFIG_PACK_LIMIT], pending[RP_HOME_CONFIG_PACK_LIMIT];
static size_t committed_bytes, pending_bytes;
static rp_home_config_t before, candidate, decoded;
static rp_home_snapshot_t snapshot;

static bool zeroed(const void *data, size_t bytes) {
    const unsigned char *p = data;
    for (size_t i = 0; i < bytes; i++) if (p[i]) return false;
    return true;
}
static void before_publication(void) {
    if (!inspect_transaction) return;
    assert(lock_depth == 1);
    /* Neither SET nor COMMIT may see a temporarily published candidate. */
    assert(!memcmp(&s_config, &before, sizeof(before)));
}
SemaphoreHandle_t xSemaphoreCreateMutex(void) { return (void *)&lock_depth; }
int xSemaphoreTake(SemaphoreHandle_t sem, unsigned timeout) {
    assert(sem && timeout && lock_depth == 0);
    lock_depth = 1;
    return 1;
}
int xSemaphoreGive(SemaphoreHandle_t sem) {
    assert(sem && lock_depth == 1);
    if (inspect_transaction) {
        assert(zeroed(s_store->packed, sizeof(s_store->packed)));
        assert(zeroed(&s_store->edit, sizeof(s_store->edit)));
    }
    lock_depth = 0;
    return 1;
}
void *heap_caps_calloc(size_t count, size_t bytes, unsigned caps) {
    assert(caps == (MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    allocation_count++;
    return calloc(count, bytes);
}
int xTaskCreate(void (*fn)(void *), const char *name, unsigned stack, void *arg, unsigned priority, TaskHandle_t *out) {
    assert(fn && !strcmp(name, "home_pga") && stack == 8192 && !arg && priority == 3);
    *out = (void *)&notifications;
    return pdPASS;
}
unsigned ulTaskNotifyTake(int clear, unsigned timeout) { (void)clear; (void)timeout; assert(0); return 0; }
void xTaskNotifyGive(TaskHandle_t task) { assert(task && !lock_depth); notifications++; }
void vTaskDelete(TaskHandle_t task) { (void)task; assert(0); }
void vTaskDelay(unsigned ticks) { (void)ticks; assert(0); }
int64_t esp_timer_get_time(void) { return 1000000; }
int ps_wifi_connected_index(void) { assert(0 && "HA must not use settings page profile cache"); return -1; }
const char *ps_wifi_name(int index) { (void)index; assert(0 && "HA must read live SSID"); return ""; }
void read_pico_connectivity_get_link(read_pico_connectivity_link_t *out) {
    out->online = wifi && !wifi_suspended;
    snprintf(out->ssid, sizeof(out->ssid), "%s", wifi_ssid);
}
bool rp_home_discover(rp_home_discovery_t *out, uint32_t timeout_ms,
                      bool (*cancel)(void *), void *context) {
    assert(!lock_depth && s_discover_running && timeout_ms == 3000 && cancel);
    discovery_calls++;
    memset(out, 0, sizeof(*out));
    if (cancel(context)) return false;
    if (discovery_mode == 1 || discovery_mode == 2) {
        assert(!ps_home_pga_suspend(0));
        if (discovery_mode == 2) ps_home_pga_resume();
    } else if (discovery_mode == 3) {
        bool queued;
        assert(ps_home_pga_portal_commit(&candidate, &queued));
    } else if (discovery_mode == 4) wifi = false;
    else if (discovery_mode == 5) wifi_ssid = "Other LAN";
    if (discovery_mode >= 1 && discovery_mode <= 5) {
        assert(cancel(context));
        return false;
    }
    if (discovery_mode == 6) return false;
    if (discovery_mode == 7) {
        rp_home_discovery_t d;
        bool busy, failed;
        int notified = notifications;
        assert(ps_home_pga_discover(true, &d, &busy, &failed) && busy);
        assert(notifications == notified);
    }
    out->count = 1;
    snprintf(out->items[0].name, sizeof(out->items[0].name), "Fixture HA");
    snprintf(out->items[0].url, sizeof(out->items[0].url), "http://192.168.1.12:18123");
    if (discovery_mode == 8) wifi = false; /* Link loss just after the transport's final cancel check. */
    return true;
}
bool read_pico_management_active(void) { return management; }
void ps_datetime_get_status(ps_datetime_status_t *out) { out->source = PS_TIME_MANUAL; }
int esp_crt_bundle_attach(void *cfg) { (void)cfg; return ESP_OK; }
esp_http_client_handle_t esp_http_client_init(const esp_http_client_config_t *cfg) {
    assert(http_allowed && (s_running||s_areas_running) && !lock_depth && cfg && cfg->disable_auto_redirect);
    http_calls++;
    http_offset = 0;
    if (worker_injection == 1) {
        worker_injection = 0;
        assert(!ps_home_pga_suspend(0));
    } else if (worker_injection == 3) {
        worker_injection = 0;
        assert(!ps_home_pga_suspend(0));
        ps_home_pga_resume();
    } else if (worker_injection == 2) {
        worker_injection = 0;
        bool queued;
        assert(ps_home_pga_portal_commit(&candidate, &queued));
    }
    return (void *)&http_offset;
}
esp_err_t esp_http_client_set_header(esp_http_client_handle_t h, const char *k, const char *v) {
    assert(h && k && v);
    if (!strcmp(k, "Authorization")) snprintf(authorization, sizeof(authorization), "%s", v);
    return ESP_OK;
}
esp_err_t esp_http_client_open(esp_http_client_handle_t h, int n) { assert(h && n >= 0); return ESP_OK; }
int esp_http_client_write(esp_http_client_handle_t h, const char *p, int n) { assert(h && p && n >= 0); return n; }
int64_t esp_http_client_fetch_headers(esp_http_client_handle_t h) { assert(h); return (int64_t)strlen(http_body); }
int esp_http_client_get_status_code(esp_http_client_handle_t h) { assert(h); return http_code; }
int esp_http_client_read(esp_http_client_handle_t h, char *p, int n) {
    assert(h && p && n > 0);
    size_t bytes = strlen(http_body) - http_offset;
    if (bytes > (size_t)n) bytes = (size_t)n;
    memcpy(p, http_body + http_offset, bytes);
    http_offset += bytes;
    return (int)bytes;
}
bool esp_http_client_is_complete_data_received(esp_http_client_handle_t h) { assert(h); return http_offset == strlen(http_body); }
esp_err_t esp_http_client_close(esp_http_client_handle_t h) { assert(h); return ESP_OK; }
esp_err_t esp_http_client_cleanup(esp_http_client_handle_t h) { assert(h); return ESP_OK; }
bool pico_app_package_read(const char *path, pico_app_package_t *pkg, bool verify, char *reason, size_t n) {
    (void)path; (void)pkg; (void)verify; (void)reason; (void)n; assert(0); return false;
}
static unsigned char area_committed[4096],area_pending[4096];
static size_t area_bytes,area_pending_bytes;
static struct {uint8_t committed[32768],pending[32768];size_t bytes,pending_bytes;} extra[3];
esp_err_t nvs_open(const char *name, int mode, nvs_handle_t *out) {
    const char *names[]={"pico_home_duo","pico_home_draft","pico_home_sel"};
    for(int i=0;i<3;i++)if(!strcmp(name,names[i])){if(mode==NVS_READWRITE){before_publication();if(failure==FAIL_OPEN)return ESP_FAIL;}*out=20+i*2+mode;return ESP_OK;}
    if(!strcmp(name,"pico_home_area")){if(mode==NVS_READWRITE&&failure==FAIL_OPEN)return ESP_FAIL;*out=10+mode;return ESP_OK;}
    if (!strcmp(name, "pico_home_link")) {
        assert(mode == NVS_READONLY);
        *out = 3;
        return ESP_OK;
    }
    assert(!strcmp(name, "pico_home_pga"));
    if (mode == NVS_READWRITE) {
        before_publication();
        if (failure == FAIL_OPEN) return ESP_FAIL;
    }
    *out = mode + 1;
    return ESP_OK;
}
esp_err_t nvs_set_blob(nvs_handle_t handle, const char *key, const void *data, size_t bytes) {
    if(handle>=20){unsigned i=(handle-20)/2;assert(i<3&&handle%2&&bytes<=sizeof(extra[i].pending));before_publication();store_writes++;if(failure==FAIL_SET)return ESP_FAIL;memcpy(extra[i].pending,data,bytes);extra[i].pending_bytes=bytes;return ESP_OK;}
    if(handle==11){assert(bytes<=sizeof(area_pending));if(failure==FAIL_SET)return ESP_FAIL;memcpy(area_pending,data,bytes);area_pending_bytes=bytes;return ESP_OK;}
    assert(handle == NVS_READWRITE + 1 && !strcmp(key, "settings") && bytes <= sizeof(pending));
    before_publication();
    store_writes++;
    if (failure == FAIL_SET) return ESP_FAIL;
    memcpy(pending, data, bytes);
    pending_bytes = bytes;
    return ESP_OK;
}
esp_err_t nvs_get_blob(nvs_handle_t handle, const char *key, void *data, size_t *bytes) {
    if(handle>=20){unsigned i=(handle-20)/2;assert(i<3&&!(handle%2));if(read_failure)return ESP_FAIL;if(!extra[i].bytes)return ESP_ERR_NVS_NOT_FOUND;if(*bytes<extra[i].bytes)return ESP_FAIL;memcpy(data,extra[i].committed,extra[i].bytes);*bytes=extra[i].bytes;return ESP_OK;}
    if(handle==10){if(!area_bytes)return ESP_ERR_NVS_NOT_FOUND;if(*bytes<area_bytes)return ESP_FAIL;memcpy(data,area_committed,area_bytes);*bytes=area_bytes;return ESP_OK;}
    assert(!strcmp(key, "settings"));
    if (handle == 3) {
        legacy_reads++;
        if (!legacy_bytes) return ESP_ERR_NVS_NOT_FOUND;
        assert(*bytes >= legacy_bytes);
        memcpy(data, legacy, legacy_bytes);
        *bytes = legacy_bytes;
        return ESP_OK;
    }
    assert(handle == NVS_READONLY + 1);
    if (read_failure) return ESP_FAIL;
    if (!committed_bytes) return ESP_ERR_NVS_NOT_FOUND;
    if (*bytes < committed_bytes) return ESP_FAIL;
    memcpy(data, committed, committed_bytes);
    *bytes = committed_bytes;
    return ESP_OK;
}
esp_err_t nvs_commit(nvs_handle_t handle) {
    if(handle>=20){unsigned i=(handle-20)/2;assert(i<3&&handle%2);before_publication();if(failure==FAIL_COMMIT)return ESP_FAIL;memcpy(extra[i].committed,extra[i].pending,extra[i].pending_bytes);extra[i].bytes=extra[i].pending_bytes;return ESP_OK;}
    if(handle==11){if(failure==FAIL_COMMIT)return ESP_FAIL;memcpy(area_committed,area_pending,area_pending_bytes);area_bytes=area_pending_bytes;return ESP_OK;}
    assert(handle == NVS_READWRITE + 1 && pending_bytes);
    before_publication();
    if (failure == FAIL_COMMIT) return ESP_FAIL;
    memcpy(committed, pending, pending_bytes);
    committed_bytes = pending_bytes;
    return ESP_OK;
}
void nvs_close(nvs_handle_t handle) {
    if(handle>=20){unsigned i=(handle-20)/2;assert(i<3);memset(extra[i].pending,0,sizeof(extra[i].pending));extra[i].pending_bytes=0;return;}
    if(handle==10||handle==11){area_pending_bytes=0;return;}
    assert(handle == NVS_READONLY + 1 || handle == NVS_READWRITE + 1 || handle == 3);
    memset(pending, 0, sizeof(pending));
    pending_bytes = 0;
}

static void reset(void) {
    inspect_transaction = false;
    memset(extra,0,sizeof(extra));committed_bytes=pending_bytes=legacy_bytes=area_bytes=area_pending_bytes=0;read_failure=0;
    if(s_store)memset(s_store,0,sizeof(*s_store));
    s_share_role=0;s_draft_pending=s_draft_conflict=s_share_deferred=s_share_job=s_share_link=s_home_reading=false;
    s_catalog_queued=s_catalog_running=false;memset(s_share_owner,0,8);share_reset();
    failure = FAIL_NONE;
    wifi = management = false;
    wifi_suspended = false; wifi_ssid = "Home WiFi";
    s_portal_owner = false;
    s_running = s_suspended = s_control_pending = false;
    s_discover_queued = s_discover_running = s_discover_failed = false;
    s_areas_queued=s_areas_running=false;s_areas_result=RP_HOME_EMPTY;
    discovery_calls = discovery_mode = 0;
    http_allowed = false; http_calls = 0; http_code = 200; worker_injection = 0;
    s_queued = s_busy = s_online = s_at_home = false;
    notifications = 0;
    memset(&candidate, 0, sizeof(candidate));
    candidate.version = RP_HOME_CONFIG_VERSION;
    candidate.enabled = 1;
    snprintf(candidate.local, sizeof(candidate.local), "http://old.local:8123");
    snprintf(candidate.external, sizeof(candidate.external), "https://old.example");
    snprintf(candidate.home_wifi, sizeof(candidate.home_wifi), "Home WiFi");
    snprintf(candidate.ha_token, sizeof(candidate.ha_token), "old-token");
    snprintf(candidate.cf_access_client_id, sizeof(candidate.cf_access_client_id), "old-id");
    snprintf(candidate.cf_access_client_secret, sizeof(candidate.cf_access_client_secret), "old-secret");
    bool queued = true;
    assert(ps_home_pga_portal_commit(&candidate, &queued) && !queued);
    s_store->room_record.scope.version=1;s_store->room_record.binding=room_binding();
    assert(allocation_count == 1); /* All large mutable state remains in the one PSRAM store. */
    assert(ps_home_pga_portal_load(&before, &snapshot));
    assert(!memcmp(&before, &candidate, sizeof(candidate)));
    snprintf(candidate.local, sizeof(candidate.local), "http://new.local:8123");
    snprintf(candidate.external, sizeof(candidate.external), "https://new.example");
    snprintf(candidate.ha_token, sizeof(candidate.ha_token), "new-token");
    snprintf(candidate.cf_access_client_id, sizeof(candidate.cf_access_client_id), "new-id");
    snprintf(candidate.cf_access_client_secret, sizeof(candidate.cf_access_client_secret), "new-secret");
    inspect_transaction = true;
}
static void make_app_config(pico_app_home_config_t *out) {
    memset(out, 0, sizeof(*out));
    out->bytes = sizeof(*out);
    out->enabled = candidate.enabled;
    out->external_enabled = candidate.external_enabled;
    out->external_control = candidate.external_control;
    snprintf(out->local, sizeof(out->local), "%s", candidate.local);
    snprintf(out->external, sizeof(out->external), "%s", candidate.external);
    snprintf(out->home_wifi, sizeof(out->home_wifi), "%s", candidate.home_wifi);
    snprintf(out->ha_token, sizeof(out->ha_token), "%s", candidate.ha_token);
    snprintf(out->cf_access_client_id, sizeof(out->cf_access_client_id), "%s", candidate.cf_access_client_id);
    snprintf(out->cf_access_client_secret, sizeof(out->cf_access_client_secret), "%s", candidate.cf_access_client_secret);
}
static void assert_stored(const rp_home_config_t *expected) {
    assert(!lock_depth);
    assert(!memcmp(&s_config, expected, sizeof(*expected)));
    bool inspecting=inspect_transaction;inspect_transaction=false;
    load_config();shared_load();decoded=s_config;inspect_transaction=inspecting;
    assert(!memcmp(&decoded, expected, sizeof(*expected)));
    assert(zeroed(s_store->packed, sizeof(s_store->packed)));
    assert(zeroed(&s_store->edit, sizeof(s_store->edit)));
}
static void pga_transactions(void) {
    for (int f = FAIL_OPEN; f <= FAIL_COMMIT; f++) {
        reset();
        failure = f;
        pico_app_home_config_t cfg;
        make_app_config(&cfg);
        assert(!ps_home_pga_config(PGA_HOME_CONFIG_SET, &cfg));
        assert_stored(&before);
        assert(!cfg.ha_token[0] && !cfg.cf_access_client_id[0] && !cfg.cf_access_client_secret[0]);
        failure = FAIL_NONE;
        make_app_config(&cfg);
        assert(ps_home_pga_config(PGA_HOME_CONFIG_SET, &cfg));
        assert_stored(&candidate);
        assert(!cfg.ha_token[0] && !cfg.cf_access_client_id[0] && !cfg.cf_access_client_secret[0]);
    }
    puts("PASS: real PGA SET preserves live/NVS config at open/set/commit failure, then retries successfully");
}
static void portal_transactions(void) {
    for (int f = FAIL_OPEN; f <= FAIL_COMMIT; f++) {
        reset();
        failure = f;
        wifi = true;
        bool queued = true;
        assert(!ps_home_pga_portal_commit(&candidate, &queued));
        assert(!queued && !notifications && !s_queued && !s_busy);
        assert_stored(&before);
        failure = FAIL_NONE;
        assert(ps_home_pga_portal_commit(&candidate, &queued));
        assert(!queued && !notifications && !s_queued && !s_busy && !scope_ready());
        assert_stored(&candidate);
    }
    puts("PASS: real portal commits publish after persistence; changed HA identity waits for room selection");
}
static void invalid_and_gates(void) {
    reset();
    pico_app_home_config_t cfg;
    make_app_config(&cfg);
    snprintf(cfg.local, sizeof(cfg.local), "invalid endpoint");
    assert(!ps_home_pga_config(PGA_HOME_CONFIG_SET, &cfg));
    assert_stored(&before);
    candidate.version = 9999;
    bool queued = true;
    assert(!ps_home_pga_portal_commit(&candidate, &queued) && !queued);
    assert_stored(&before);
    candidate.version = RP_HOME_CONFIG_VERSION;
    make_app_config(&cfg);
    management = true;
    assert(!ps_home_pga_config(PGA_HOME_CONFIG_SET, &cfg));
    assert_stored(&before);
    assert(!cfg.ha_token[0] && !cfg.cf_access_client_id[0] && !cfg.cf_access_client_secret[0]);
    management = false;
    ps_home_pga_portal_set_owner(true);
    make_app_config(&cfg);
    assert(!ps_home_pga_config(PGA_HOME_CONFIG_SET, &cfg));
    assert_stored(&before);
    puts("PASS: invalid edits and portal/manage ownership preserve committed config and clear PSRAM edit before unlock");
}

static void migration_contract(void) {
    reset();
    inspect_transaction = false;
    legacy_bytes = rp_home_config_pack(&before, legacy, sizeof(legacy));
    assert(legacy_bytes);
    committed_bytes = 0;
    unsigned writes = store_writes;
    load_config();
    assert(legacy_reads && !memcmp(&s_config, &before, sizeof(before)) && store_writes == (int)writes);
    assert(zeroed(s_store->packed, sizeof(s_store->packed)) && zeroed(&s_store->edit, sizeof(s_store->edit)));
    int reads = legacy_reads;
    memset(&candidate, 0, sizeof(candidate)); candidate.version = RP_HOME_CONFIG_VERSION;
    committed_bytes = rp_home_config_pack(&candidate, committed, sizeof(committed));
    load_config();
    assert(legacy_reads == reads && !s_config.ha_token[0] && !s_config.local[0]);
    committed[0] ^= 1;
    load_config();
    assert(legacy_reads == reads && !s_config.ha_token[0]);
    read_failure = 1;
    load_config();
    assert(legacy_reads == reads && !s_config.ha_token[0]);
    read_failure = 0;
    committed_bytes = legacy_bytes = 0;
    load_config();
    assert(!s_config.enabled && s_config.version == RP_HOME_CONFIG_VERSION && !s_config.ha_token[0]);
    assert(store_writes == (int)writes);
    puts("PASS: legacy NVS read fallback only for missing new record; empty/corrupt/error records cannot revive credentials; zero automatic writes");
}
static void queue_entity(void) {
    reset();
    inspect_transaction = false;
    wifi = true;
    s_snap.entity_count = s_snap.entity_total = 1;
    snprintf(s_snap.entities[0].id, sizeof(s_snap.entities[0].id), "light.room");
    s_snap.entities[0].available = 1;
    s_snap.entities[0].kind = RP_HOME_LIGHT;
    snprintf(s_snap.entities[0].state, sizeof(s_snap.entities[0].state), "on");
}
static void worker_contract(void) {
    queue_entity();
    int allocations = allocation_count;
    pico_app_home_status_t st = {.bytes = sizeof(st)};
    assert(ps_home_pga_peek_status(&st) && st.entity_count == 1 && allocations == allocation_count);
    assert(ps_home_pga_control("light.room", "turn_off", 0));
    assert(!http_calls && s_control_pending && s_busy && notifications == 1);
    assert(!ps_home_pga_control("light.room", "turn_on", 0));
    http_allowed = true; http_code = 403;
    assert(run_pending());
    assert(http_calls == 1 && s_snap.result == RP_HOME_AUTH && s_snap.entity_count == 1 && !s_busy);
    assert(!run_pending());
    assert(ps_home_pga_command(PGA_HOME_SYNC, &st));
    assert(ps_home_pga_suspend(0) && !s_busy && !s_queued);
    assert(!ps_home_pga_command(PGA_HOME_SYNC, &st));
    assert(!ps_home_pga_control("light.room", "turn_off", 0));
    assert(!run_pending());
    bool queued = true;
    assert(ps_home_pga_portal_commit(&before, &queued) && !queued);
    ps_home_pga_resume();
    assert(ps_home_pga_command(PGA_HOME_SYNC, &st));
    assert(s_queued && !s_control_pending);
    http_code = 200;
    uint32_t revision = s_snap.revision;
    worker_injection = 1;
    assert(run_pending());
    assert(s_suspended && !s_busy && s_snap.revision == revision);
    queue_entity();
    assert(ps_home_pga_command(PGA_HOME_SYNC, &st));
    revision = s_snap.revision;
    http_allowed = true; worker_injection = 3;
    assert(run_pending() && !s_suspended && s_snap.revision == revision);
    queue_entity();
    memset(s_config.ha_token, 'T', sizeof(s_config.ha_token) - 1);
    s_config.ha_token[sizeof(s_config.ha_token) - 1] = 0;
    assert(!ps_home_pga_command(PGA_HOME_SYNC, &st));
    assert(ps_home_pga_command(PS_HOME_PRINTER_SYNC, &st));
    http_allowed = true;
    assert(run_pending());
    assert(strlen(authorization) == strlen(s_config.ha_token) + 7 && !strncmp(authorization, "Bearer ", 7));
    queue_entity();
    assert(ps_home_pga_command(PGA_HOME_SYNC, &st));
    http_allowed = true; worker_injection = 2;
    assert(run_pending());
    assert(s_snap.result == RP_HOME_EMPTY && s_snap.entity_count == 0 && !s_queued);
    assert(!strcmp(s_config.local, candidate.local));
    assert(!run_pending() && !s_busy);
    queue_entity();
    memset(s_config.ha_token, 0, sizeof(s_config.ha_token));
    memset(s_config.local, 0, sizeof(s_config.local));
    s_config.printers[0].source = RP_HOME_FORMLABS;
    snprintf(s_config.printers[0].name, sizeof(s_config.printers[0].name), "Legacy Formlabs");
    snprintf(s_config.printers[0].serial, sizeof(s_config.printers[0].serial), "fixture-printer");
    snprintf(s_config.printers[0].client_id, sizeof(s_config.printers[0].client_id), "fixture-client");
    snprintf(s_config.printers[0].client_secret, sizeof(s_config.printers[0].client_secret), "fixture-secret");
    assert(ps_home_pga_command(PS_HOME_PRINTER_SYNC, &st));
    assert(run_pending() && !http_calls);
    assert(s_snap.printers[0].result == RP_HOME_UNSUPPORTED && !s_snap.printers[0].has_data);
    assert(!strcmp(s_config.printers[0].client_secret, "fixture-secret"));
    puts("PASS: unsupported legacy printer protocols stay dormant without deleting their saved credentials");
    puts("PASS: control queues without UI HTTP; failures preserve entities; zero-time suspend gates all work; resume works; stale jobs cannot publish; full bearer survives");
}

static void network_contract(void) {
    reset();
    inspect_transaction = false;
    pico_app_home_status_t st = {.bytes = sizeof(st)};
    assert(ps_home_pga_command(PGA_HOME_STATUS, &st) && !(st.flags & PGA_HOME_WIFI));
    uint32_t revision = st.revision;
    wifi = true;
    assert(ps_home_pga_command(PGA_HOME_STATUS, &st) && (st.flags & PGA_HOME_WIFI) && st.at_home);
    assert(st.revision == revision);
    assert(ps_home_pga_command(PGA_HOME_SYNC, &st) && s_online && s_at_home);
    wifi_ssid = "Other LAN";
    assert(ps_home_pga_peek_status(&st) && (st.flags & PGA_HOME_WIFI) && !st.at_home);
    assert(s_online && s_at_home); /* Rendering must not rewrite a queued request's network selection. */
    wifi_suspended = true;
    assert(ps_home_pga_command(PGA_HOME_STATUS, &st) && !(st.flags & PGA_HOME_WIFI) && !st.at_home);
    wifi_suspended = false; wifi_ssid = "Home WiFi";
    assert(ps_home_pga_peek_status(&st) && (st.flags & PGA_HOME_WIFI) && st.at_home);
    wifi = false;
    assert(ps_home_pga_peek_status(&st) && !(st.flags & PGA_HOME_WIFI) && !st.at_home);
    assert(st.revision == revision);
    puts("PASS: HA reads live STA/SSID without saved-profile cache; initial entry, reconnect, other LAN and sleep update independently of HA revision");
}

static void discovery_contract(void) {
    rp_home_discovery_t found;
    bool busy, failed;
    reset(); inspect_transaction = false;
    assert(!ps_home_pga_discover(true, &found, &busy, &failed));
    assert(!busy && !notifications && !discovery_calls);
    wifi = true;
    unsigned writes = store_writes;
    assert(ps_home_pga_discover(true, &found, &busy, &failed) && busy && !found.count);
    assert(notifications == 1 && !discovery_calls);
    assert(ps_home_pga_discover(true, &found, &busy, &failed) && busy && notifications == 1);
    run_discovery();
    assert(ps_home_pga_discover(false, &found, &busy, &failed) && !busy && !failed && found.count == 1);
    assert(discovery_calls == 1 && writes == (unsigned)store_writes);
    assert(!memcmp(&s_config, &before, sizeof(before)));
    for (int mode = 1; mode <= 8; mode++) {
        reset(); inspect_transaction = false; wifi = true; discovery_mode = mode;
        assert(ps_home_pga_discover(true, &found, &busy, &failed) && busy);
        run_discovery();
        assert(ps_home_pga_discover(false, &found, &busy, &failed) && !busy);
        if (mode == 7) assert(!failed && found.count == 1);
        else assert(failed && found.count == 0);
    }
    reset(); inspect_transaction = false; wifi = true;
    assert(ps_home_pga_discover(true, &found, &busy, &failed));
    assert(ps_home_pga_suspend(0)); run_discovery();
    assert(!discovery_calls && !ps_home_pga_discover(true, &found, &busy, &failed));
    ps_home_pga_resume();
    assert(ps_home_pga_discover(true, &found, &busy, &failed)); run_discovery();
    assert(ps_home_pga_discover(false, &found, &busy, &failed) && found.count == 1 && !failed);
    wifi_ssid = "Other LAN";
    assert(ps_home_pga_discover(false, &found, &busy, &failed) && !found.count && failed);
    assert(ps_home_pga_discover(true, &found, &busy, &failed) && busy);
    wifi_ssid = "Home WiFi"; /* Queued work belongs to the LAN where Start was tapped. */
    run_discovery();
    assert(ps_home_pga_discover(false, &found, &busy, &failed) && !busy && failed && !found.count);
    puts("PASS: async discovery deduplicates, never saves config, cancels queued/running work on suspend, config generation or LAN changes, and discards stale cached results");
}

static void printer_transactions(void) {
    reset();
    pico_app_printer_config_t edit={.bytes=sizeof(edit),.slot=0};
    assert(ps_home_pga_printer_config(PGA_PRINTER_GET,&edit)&&!edit.source);
    edit.source=RP_HOME_HA;
    snprintf(edit.name,sizeof(edit.name),"Synthetic printer");
    snprintf(edit.serial,sizeof(edit.serial),"sensor.synthetic_printer");
    for(int f=FAIL_OPEN;f<=FAIL_COMMIT;f++){
        failure=f;
        assert(!ps_home_pga_printer_config(PGA_PRINTER_SET,&edit)&&edit.error[0]);
        assert_stored(&before);
    }
    failure=FAIL_NONE;
    assert(ps_home_pga_printer_config(PGA_PRINTER_SET,&edit));
    candidate=before;candidate.printers[0].source=RP_HOME_HA;
    snprintf(candidate.printers[0].name,sizeof(candidate.printers[0].name),"%s",edit.name);
    snprintf(candidate.printers[0].serial,sizeof(candidate.printers[0].serial),"%s",edit.serial);
    assert_stored(&candidate);before=candidate;
    pico_app_printer_config_t duplicate={.bytes=sizeof(duplicate),.slot=1};
    assert(ps_home_pga_printer_config(PGA_PRINTER_GET,&duplicate));
    duplicate.source=RP_HOME_HA;
    snprintf(duplicate.name,sizeof(duplicate.name),"Duplicate");
    snprintf(duplicate.serial,sizeof(duplicate.serial),"%s",edit.serial);
    assert(!ps_home_pga_printer_config(PGA_PRINTER_SET,&duplicate));assert_stored(&before);
    edit.revision--;
    assert(!ps_home_pga_printer_config(PGA_PRINTER_REMOVE,&edit));assert_stored(&before);
    assert(ps_home_pga_printer_config(PGA_PRINTER_GET,&edit));
    management=true;assert(!ps_home_pga_printer_config(PGA_PRINTER_REMOVE,&edit));management=false;
    s_portal_owner=true;assert(!ps_home_pga_printer_config(PGA_PRINTER_SET,&edit));s_portal_owner=false;
    assert_stored(&before);
    assert(ps_home_pga_printer_config(PGA_PRINTER_REMOVE,&edit));
    memset(&candidate.printers[0],0,sizeof(candidate.printers[0]));assert_stored(&candidate);
    inspect_transaction=false;
    candidate.printers[0].source=RP_HOME_BAMBU_LAN;candidate.printers[0].port=8883;
    snprintf(candidate.printers[0].name,sizeof(candidate.printers[0].name),"Old Bambu");
    snprintf(candidate.printers[0].host,sizeof(candidate.printers[0].host),"192.168.1.99");
    snprintf(candidate.printers[0].serial,sizeof(candidate.printers[0].serial),"SYNTHETIC");
    snprintf(candidate.printers[0].certificate,sizeof(candidate.printers[0].certificate),"SYNTHETIC_PINNED_CERTIFICATE");
    snprintf(candidate.printers[0].secret,sizeof(candidate.printers[0].secret),"SYNTHETIC_ACCESS_CODE");
    bool queued;assert(ps_home_pga_portal_commit(&candidate,&queued));before=candidate;inspect_transaction=true;
    assert(ps_home_pga_printer_config(PGA_PRINTER_GET,&edit));
    snprintf(edit.name,sizeof(edit.name),"Renamed Bambu");
    assert(ps_home_pga_printer_config(PGA_PRINTER_SET,&edit));
    snprintf(candidate.printers[0].name,sizeof(candidate.printers[0].name),"%s",edit.name);
    assert_stored(&candidate);
    puts("PASS: printer add/rename/remove transactions, persistence rollback, duplicate/stale/ownership rejection, HA and legacy credentials retained");
}

static void rooms_contract(void){
    reset();inspect_transaction=false;wifi=true;
    memset(&s_store->room_record,0,sizeof(s_store->room_record));area_bytes=0;
    pico_app_home_status_t st={.bytes=sizeof(st)};
    assert(!ps_home_pga_command(PGA_HOME_SYNC,&st)&&!s_queued&&!http_calls);
    pico_app_home_rooms_t r={.bytes=sizeof(r)};
    assert(ps_home_pga_rooms(PGA_ROOMS_SCAN,&r)&&r.busy&&!http_calls);
    const char *old_body=http_body;http_allowed=true;
    http_body="{\"total\":3,\"items\":[{\"id\":\"office\",\"name\":\"书房\",\"devices\":40},{\"id\":\"living\",\"name\":\"客厅\",\"devices\":30},{\"id\":\"\",\"name\":\"未分配房间\",\"devices\":2}]}";
    run_areas();assert(ps_home_pga_rooms(PGA_ROOMS_GET,&r)&&r.count==3&&!r.busy&&r.result==RP_HOME_OK);
    int allocations=allocation_count,calls=http_calls;
    assert(ps_home_pga_rooms(PGA_ROOMS_GET,&r)&&allocation_count==allocations&&http_calls==calls);
    // 大房间目录允许70项，选择上限只在实体选择API执行。/ Large rooms may contain 70 items; only selected entities are capped.
    r.selected=3;assert(ps_home_pga_rooms(PGA_ROOMS_SET,&r)&&scope_ready());
    memset(&s_store->room_record,0,sizeof(s_store->room_record));
    for(int fail=FAIL_OPEN;fail<=FAIL_COMMIT;fail++){
        failure=fail;r.selected=5;assert(!ps_home_pga_rooms(PGA_ROOMS_SET,&r)&&!scope_ready()&&!area_bytes);
    }
    failure=FAIL_NONE;r.selected=5;uint32_t stale=r.revision;
    assert(ps_home_pga_rooms(PGA_ROOMS_SET,&r)&&scope_ready()&&r.selected==5&&s_store->room_record.scope.count==2);
    r.revision=stale;r.selected=2;assert(!ps_home_pga_rooms(PGA_ROOMS_SET,&r)&&s_store->room_record.scope.count==2);
    memset(&s_store->room_record,0,sizeof(s_store->room_record));shared_load();assert(scope_ready()&&!strcmp(s_store->room_record.scope.ids[0],"office")&&!s_store->room_record.scope.ids[1][0]);
    s_portal_owner=true;r.selected=0;assert(!ps_home_pga_rooms(PGA_ROOMS_SET,&r));s_portal_owner=false;
    r.selected=0;
    assert(ps_home_pga_rooms(PGA_ROOMS_SET,&r)&&scope_ready()&&!s_store->room_record.scope.count);
    assert(ps_home_pga_command(PGA_HOME_SYNC,&st));s_queued=s_busy=false;
    assert(ps_home_pga_rooms(PGA_ROOMS_SCAN,&r));http_code=401;run_areas();
    assert(ps_home_pga_rooms(PGA_ROOMS_GET,&r)&&r.result==RP_HOME_AUTH&&r.configured);
    assert(!ps_home_pga_rooms(PGA_ROOMS_SET,&r));
    http_code=200;bool queued=true;assert(ps_home_pga_portal_commit(&candidate,&queued)&&!queued&&!scope_ready());
    assert(!ps_home_pga_command(PGA_HOME_SYNC,&st));
    memset(&s_store->room_record,0,sizeof(s_store->room_record));shared_load();assert(!scope_ready());
    http_body=old_body;puts("PASS: room-first gate, async lightweight catalog, large-room selection, unassigned/zero selection, NVS rollback, revision/ownership, reboot, auth failure and HA identity change");
}
/* 合成对端走真实帧接收和事务路径。/ Synthetic peer exercises production framing and transactions. */
static void feed_record(const home_shared_t *record,uint32_t seq,bool corrupt,bool finish){
    uint8_t packet[96]={240,0,2,1};uint32_t crc=room_hash(record,sizeof(*record));
    share_put32(packet+4,seq);share_put32(packet+8,record->base);share_put32(packet+12,record->revision);
    packet[16]=(uint8_t)sizeof(*record);packet[17]=(uint8_t)(sizeof(*record)>>8);share_put32(packet+18,crc+(corrupt?1:0));
    assert(ps_home_pga_share_receive(packet,22));
    for(unsigned offset=0;offset<sizeof(*record);offset+=80){
        unsigned n=sizeof(*record)-offset;if(n>80)n=80;packet[1]=1;share_put32(packet+2,seq);
        packet[6]=(uint8_t)offset;packet[7]=(uint8_t)(offset>>8);packet[8]=(uint8_t)n;memcpy(packet+9,(const uint8_t*)record+offset,n);
        assert(ps_home_pga_share_receive(packet,n+9));assert(ps_home_pga_share_receive(packet,n+9));
        if(!finish&&offset>160)break;
    }
    packet[1]=2;share_put32(packet+2,seq);assert(ps_home_pga_share_receive(packet,6));
}
static void share_contract(void){
    reset();inspect_transaction=false;s_share_role=1;s_share_link=true;memset(s_share_owner,7,8);
    memcpy(s_store->shared.owner,s_share_owner,8);s_store->shared.checksum=shared_crc(&s_store->shared);
    home_shared_t record;shared_build(&record,&candidate,NULL,NULL);
    uint32_t revision=s_store->shared.revision;
    feed_record(&record,100,false,false);assert(!s_share_job&&!memcmp(&s_config,&before,sizeof(before)));
    feed_record(&record,101,true,true);assert(!s_share_job&&!memcmp(&s_config,&before,sizeof(before)));
    feed_record(&record,102,false,true);assert(s_share_job);run_share();
    assert(s_share_result==1&&s_store->shared.revision==revision+1&&!memcmp(&s_config,&candidate,HOME_HA_BYTES));
    s_rx_job=false;s_done_seq=102;s_rx_n=0;
    feed_record(&record,102,false,true);assert(!s_share_job&&s_store->shared.revision==revision+1);
    feed_record(&record,103,false,true);assert(s_share_job);run_share();assert(!s_share_result&&s_store->shared.revision==revision+1);s_rx_job=false;s_rx_n=0;
    shared_build(&record,&before,NULL,NULL);failure=FAIL_COMMIT;
    feed_record(&record,104,false,true);run_share();assert(!s_share_result&&!memcmp(&s_config,&candidate,HOME_HA_BYTES));failure=FAIL_NONE;s_rx_job=false;s_rx_n=0;
    // 副机离线编辑仅存草稿，主机变化时保留冲突草稿并停止自动提交。/ Secondary offline edits remain drafts; conflicts persist without auto-submit.
    s_share_role=2;assert(shared_save_locked(&before,NULL,NULL)&&s_draft_pending&&!memcmp(&s_config,&candidate,HOME_HA_BYTES));
    s_store->shared_in=s_store->shared;s_store->shared_in.revision++;s_store->shared_in.checksum=shared_crc(&s_store->shared_in);
    s_share_job=true;run_share();assert(s_share_result==1&&!s_draft_pending&&s_draft_conflict);
    memset(&s_store->shared_draft,0,sizeof(s_store->shared_draft));s_draft_conflict=false;shared_load();
    assert(!s_draft_pending&&s_draft_conflict&&s_store->shared_draft.base==UINT32_MAX);
    rp_home_config_t empty={.version=RP_HOME_CONFIG_VERSION};s_share_role=1;
    assert(shared_save_locked(&empty,NULL,NULL));shared_load();assert(!s_config.ha_token[0]&&!s_config.enabled);
    extra[0].committed[4]^=1;load_config();shared_load();assert(!s_config.ha_token[0]);
    puts("PASS: encrypted-channel payload framing duplicates/interruption/checksum, primary revision conflict, failed commit rollback, durable offline conflict draft and credential clear; no radio hardware");
}
static void selection_contract(void){
    reset();inspect_transaction=false;pico_app_home_selection_t sel={.bytes=sizeof(sel)};
    assert(ps_home_pga_selection(PGA_SELECTION_GET,&sel)&&!sel.configured);
    sel.count=96;for(unsigned i=0;i<96;i++)snprintf(sel.ids[i],96,"light.room_%u",i);
    assert(ps_home_pga_selection(PGA_SELECTION_SET,&sel)&&sel.configured&&sel.count==96);
    uint32_t rev=sel.revision;sel.count=97;assert(!ps_home_pga_selection(PGA_SELECTION_SET,&sel));
    sel.count=95;failure=FAIL_COMMIT;assert(!ps_home_pga_selection(PGA_SELECTION_SET,&sel)&&sel.count==95);failure=FAIL_NONE;
    assert(ps_home_pga_selection(PGA_SELECTION_GET,&sel)&&sel.count==96&&sel.revision==rev);
    memcpy(sel.ids[1],sel.ids[0],96);assert(!ps_home_pga_selection(PGA_SELECTION_SET,&sel));
    assert(ps_home_pga_selection(PGA_SELECTION_GET,&sel));sel.revision--;assert(!ps_home_pga_selection(PGA_SELECTION_SET,&sel));
    // 老应用的实体前缀容量后不得写入。/ Never write beyond an old application's entity prefix.
    union {pico_app_home_entity_t entity;uint8_t raw[sizeof(pico_app_home_entity_t)];} old;
    memset(&old,0xa5,sizeof(old));old.entity.bytes=PICO_HOME_ENTITY_V1_BYTES;
    s_snap.entity_count=1;strcpy(s_snap.entities[0].id,"light.room");assert(ps_home_pga_entity(0,&old.entity));
    for(unsigned i=PICO_HOME_ENTITY_V1_BYTES;i<sizeof(old);i++)assert(old.raw[i]==0xa5);
    printf("PSRAM store=%zu bytes; worker internal stack=8192 bytes; shared configuration raw=%zu packed=%zu bytes\n",sizeof(pga_store_t),sizeof(home_shared_t),extra[0].bytes);
    puts("PASS: 96 selection boundary, duplicate/stale rejection, failed save retains draft and active selection, old entity ABI canary");
}

static void reading_poll_contract(void){
    queue_entity();s_home_reading=true;s_queued=s_busy=true;
    assert(!run_pending()&&!s_busy&&!http_calls);
    s_home_local_reading=false;s_peer_home_reading=true;
    assert(ps_home_pga_control("light.room","turn_off",0));http_allowed=true;http_code=403;
    assert(run_pending()&&http_calls==1&&!s_busy&&s_snap.result==RP_HOME_AUTH);
    assert(!run_pending()&&http_calls==1);
    s_home_reading=s_peer_home_reading=false;pico_app_home_status_t status={.bytes=sizeof(status)};
    assert(ps_home_pga_command(PGA_HOME_SYNC,&status)&&s_busy);
    puts("PASS: reading discards queued polling without stuck busy; explicit control executes once; polling resumes afterwards");
}
int main(int argc, char **argv) {
    assert(argc == 2);
    (void)s_nav_edit;
    pico_app_home_status_t st = {.bytes = sizeof(st)};
    assert(!ps_home_pga_peek_status(&st) && !allocation_count && !s_worker);
    if (!strcmp(argv[1], "all") || !strcmp(argv[1], "migration")) migration_contract();
    if (!strcmp(argv[1], "all") || !strcmp(argv[1], "worker")) worker_contract();
    if (!strcmp(argv[1], "all") || !strcmp(argv[1], "network")) network_contract();
    if (!strcmp(argv[1], "all") || !strcmp(argv[1], "discovery")) discovery_contract();
    if (!strcmp(argv[1], "all") || !strcmp(argv[1], "pga")) pga_transactions();
    if (!strcmp(argv[1], "all") || !strcmp(argv[1], "portal")) portal_transactions();
    if (!strcmp(argv[1], "all") || !strcmp(argv[1], "invalid")) invalid_and_gates();
    if (!strcmp(argv[1], "all") || !strcmp(argv[1], "printer")) printer_transactions();
    if (!strcmp(argv[1], "all") || !strcmp(argv[1], "rooms")) rooms_contract();
    if(!strcmp(argv[1],"all")||!strcmp(argv[1],"share")){share_contract();selection_contract();reading_poll_contract();}
    free(s_store);
    s_store = NULL;
    return 0;
}

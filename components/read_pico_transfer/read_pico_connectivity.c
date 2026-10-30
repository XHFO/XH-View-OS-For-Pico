/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 * 单worker串行管理日常STA、扫描、NVS记忆和原安全传书组件。
 * One worker serializes everyday STA, scans, remembered profiles and the existing safe-transfer component.
 * 冻结：事件回调只记录观察；不格式化、不挂载存储、不在UI任务执行网络命令。
 * Frozen: Event callbacks only record observations; no formatting, mounting or network commands on the UI task.
 */
#include "read_pico_connectivity.h"
#include "read_pico_management.h"
#include "connectivity_profiles.h"
#ifdef READ_PICO_CONNECTIVITY_HOST_TEST
#include "connectivity_host_env.h"
#else
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#endif
#include "wifi_memory_config.h"
#include <stdio.h>
#include <stdlib.h>

enum { CMD_ENABLE, CMD_SCAN, CMD_CONNECT, CMD_SAVED, CMD_FORGET, CMD_AUTO, CMD_TRANSFER, CMD_STOP, CMD_RESUME };
typedef struct {
    int type;
    bool enabled;
    uint32_t seq, epoch;
    transfer_credentials_t credentials;
    read_pico_transfer_cfg_t transfer;
    char root[160], fonts[160];
} connectivity_command_t;

static portMUX_TYPE c_lock = portMUX_INITIALIZER_UNLOCKED;
static QueueHandle_t c_queue;
static bool c_initializing, c_quiesce_requested;
static bool c_resume_retry;
static unsigned c_pending, c_submitting;
static uint32_t c_epoch, c_next_seq;
static read_pico_connectivity_status_t c_status;
static connectivity_profiles_t c_profiles;
static esp_err_t c_profiles_error;
static transfer_credentials_t c_target, c_legacy_before;
static esp_netif_t *c_netif;
static esp_event_handler_instance_t c_wifi_events, c_ip_events;
static bool c_driver, c_started, c_loop_owned, c_transfer, c_transfer_ap;
static bool c_scan, c_scan_pending, c_scan_done, c_sta_ready, c_got_ip, c_lost;
static uint32_t c_scan_event_status;
static char c_ip[48];
static int64_t c_scan_deadline, c_scan_start_deadline, c_scan_retry_at;
static transfer_connection_t c_connection;
static bool c_auto_cycle;
static bool c_legacy_baseline, c_import_pending;
static uint8_t c_tried;
static int64_t c_last_poll;

static int64_t connectivity_now(void) { return esp_timer_get_time() / 1000; }
static bool connectivity_allowed_suspended(int type, bool enabled) {
    return type == CMD_RESUME || type == CMD_STOP || (type == CMD_ENABLE && !enabled);
}

static void connectivity_result(read_pico_connectivity_operation_t operation, esp_err_t err) {
    portENTER_CRITICAL(&c_lock);
    c_status.last_error = err; c_status.last_operation = operation; ++c_status.result_seq;
    portEXIT_CRITICAL(&c_lock);
}
static void connectivity_error(esp_err_t err) { connectivity_result(READ_PICO_CONNECTIVITY_OP_SYSTEM, err); }

static read_pico_connectivity_operation_t connectivity_operation(int command) {
    if (command == CMD_SCAN) return READ_PICO_CONNECTIVITY_OP_SCAN;
    if (command == CMD_ENABLE || command == CMD_RESUME) return READ_PICO_CONNECTIVITY_OP_WIFI;
    if (command == CMD_CONNECT || command == CMD_SAVED) return READ_PICO_CONNECTIVITY_OP_CONNECT;
    if (command == CMD_FORGET || command == CMD_AUTO) return READ_PICO_CONNECTIVITY_OP_PROFILE;
    return READ_PICO_CONNECTIVITY_OP_TRANSFER;
}

static void connectivity_scan_result(esp_err_t err, read_pico_connectivity_scan_failure_t phase) {
    c_scan_pending = false;
    portENTER_CRITICAL(&c_lock);
    c_scan = false;
    c_status.scan_error = err; c_status.scan_failure = phase; ++c_status.scan_completed_seq;
    portEXIT_CRITICAL(&c_lock);
    connectivity_result(READ_PICO_CONNECTIVITY_OP_SCAN, err);
}

static void connectivity_state(read_pico_connectivity_state_t state) {
    portENTER_CRITICAL(&c_lock); c_status.state = state; portEXIT_CRITICAL(&c_lock);
}

void read_pico_connectivity_get_status(read_pico_connectivity_status_t *out) {
    if (!out) return;
    portENTER_CRITICAL(&c_lock); *out = c_status; portEXIT_CRITICAL(&c_lock);
}
bool read_pico_connectivity_online(void) {
    portENTER_CRITICAL(&c_lock);bool online=c_status.connected&&!c_status.suspended;portEXIT_CRITICAL(&c_lock);
    return online;
}
void read_pico_connectivity_get_link(read_pico_connectivity_link_t *out) {
    if (!out) return;
    portENTER_CRITICAL(&c_lock);
    out->online = c_status.connected && !c_status.suspended;
    memcpy(out->ssid, c_status.ssid, sizeof(out->ssid));
    portEXIT_CRITICAL(&c_lock);
    out->ssid[sizeof(out->ssid) - 1] = 0;
}

static void connectivity_publish(void) {
    read_pico_transfer_status_t transfer;
    read_pico_transfer_get_status(&transfer);
    portENTER_CRITICAL(&c_lock);
    c_status.scanning = c_scan || c_scan_pending;
    c_status.transfer_active = c_transfer;
    c_status.wifi_enabled = c_started || c_transfer;
    c_status.connected = c_transfer ? transfer.mode == READ_PICO_TRANSFER_MODE_STA && transfer.network_ready : c_connection.online;
    c_status.connecting = c_transfer ? transfer.mode == READ_PICO_TRANSFER_MODE_STA && transfer.state == READ_PICO_TRANSFER_STARTING : c_target.version == 1 && c_connection.deadline_ms > 0 && !c_connection.online && !c_connection.failed;
    c_status.transfer = transfer;
    c_status.saved_count = c_profiles.count;
    memset(c_status.saved, 0, sizeof(c_status.saved));
    for (unsigned i = 0; i < c_profiles.count; ++i) {
        memcpy(c_status.saved[i].ssid, c_profiles.items[i].credentials.ssid, 33);
        c_status.saved[i].auto_join = c_profiles.items[i].auto_join != 0;
    }
    if (c_transfer) {
        snprintf(c_status.ssid, sizeof(c_status.ssid), "%s", transfer.mode == READ_PICO_TRANSFER_MODE_AP ? transfer.ssid : transfer.wifi_ssid);
        snprintf(c_status.ip, sizeof(c_status.ip), "%.47s", c_status.connected && !strncmp(transfer.url, "http://", 7) ? transfer.url + 7 : "");
        if (transfer.state == READ_PICO_TRANSFER_ERROR) c_status.state = READ_PICO_CONNECTIVITY_ERROR;
        else if (c_status.state != READ_PICO_CONNECTIVITY_STOPPING)
            c_status.state = transfer.state == READ_PICO_TRANSFER_STARTING ? READ_PICO_CONNECTIVITY_CONNECTING : c_status.connected ? READ_PICO_CONNECTIVITY_CONNECTED : READ_PICO_CONNECTIVITY_IDLE;
    } else {
        memcpy(c_status.ssid, c_target.ssid, sizeof(c_status.ssid));
        snprintf(c_status.ip, sizeof(c_status.ip), "%s", c_connection.online ? c_ip : "");
    }
    if (!c_status.connected) c_status.rssi = 0;
    portEXIT_CRITICAL(&c_lock);
}

static void connectivity_event(void *arg, esp_event_base_t base, int32_t id, void *data) {
    (void)arg;
    portENTER_CRITICAL(&c_lock);
    if (c_driver && base == WIFI_EVENT && id == WIFI_EVENT_STA_START) c_sta_ready = true;
    if (c_started) {
        if (base == WIFI_EVENT && id == WIFI_EVENT_SCAN_DONE && c_scan) {
            c_scan_event_status = data ? ((const wifi_event_sta_scan_done_t *)data)->status : 0;
            c_scan_done = true;
        }
        if ((base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) || (base == IP_EVENT && id == IP_EVENT_STA_LOST_IP)) {
            c_lost = true; c_got_ip = false; c_ip[0] = 0;
        }
        if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
            const ip_event_got_ip_t *event = data;
            if (event && event->esp_netif == c_netif) {
                snprintf(c_ip, sizeof(c_ip), IPSTR, IP2STR(&event->ip_info.ip));
                c_got_ip = true; c_lost = false;
            }
        }
    }
    portEXIT_CRITICAL(&c_lock);
}

static esp_err_t connectivity_release_sta(void) {
    read_pico_management_stop();
    bool started = c_started;
    portENTER_CRITICAL(&c_lock); c_started = false; portEXIT_CRITICAL(&c_lock);
    if (c_driver) { esp_wifi_scan_stop(); esp_wifi_clear_ap_list(); }
    if (started) {
        esp_err_t err = esp_wifi_stop();
        if (err != ESP_OK) { portENTER_CRITICAL(&c_lock); c_started = true; portEXIT_CRITICAL(&c_lock); return err; }
    }
    if (c_wifi_events) { esp_event_handler_instance_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, c_wifi_events); c_wifi_events = NULL; }
    if (c_ip_events) { esp_event_handler_instance_unregister(IP_EVENT, ESP_EVENT_ANY_ID, c_ip_events); c_ip_events = NULL; }
    if (c_driver) {
        esp_err_t err = esp_wifi_deinit(); if (err != ESP_OK) return err;
        c_driver = false;
    }
    if (c_netif) { esp_netif_destroy_default_wifi(c_netif); c_netif = NULL; }
    if (c_loop_owned) { esp_event_loop_delete_default(); c_loop_owned = false; }
    if (c_scan || c_scan_pending) connectivity_scan_result(ESP_ERR_INVALID_STATE, READ_PICO_CONNECTIVITY_SCAN_CANCELLED);
    memset(&c_connection, 0, sizeof(c_connection));
    portENTER_CRITICAL(&c_lock);
    c_scan_done = c_sta_ready = c_got_ip = c_lost = false; c_scan_event_status = 0; c_ip[0] = 0;
    portEXIT_CRITICAL(&c_lock);
    return ESP_OK;
}

static esp_err_t connectivity_acquire_sta(void) {
    if (c_started) return ESP_OK;
    if (c_driver || c_netif) return ESP_ERR_INVALID_STATE;
    esp_err_t err = esp_netif_init();
    if (err != ESP_OK) return err;
    err = esp_event_loop_create_default();
    if (err == ESP_OK) c_loop_owned = true;
    else if (err != ESP_ERR_INVALID_STATE) return err;
    c_netif = esp_netif_create_default_wifi_sta();
    if (!c_netif) { err = ESP_ERR_NO_MEM; goto fail; }
    wifi_init_config_t init = read_pico_wifi_init_config();
    err = esp_wifi_init(&init); if (err != ESP_OK) goto fail;
    c_driver = true;
    err = esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, connectivity_event, NULL, &c_wifi_events); if (err != ESP_OK) goto fail;
    err = esp_event_handler_instance_register(IP_EVENT, ESP_EVENT_ANY_ID, connectivity_event, NULL, &c_ip_events); if (err != ESP_OK) goto fail;
    err = esp_wifi_set_storage(WIFI_STORAGE_RAM); if (err != ESP_OK) goto fail;
    err = esp_wifi_set_mode(WIFI_MODE_STA); if (err != ESP_OK) goto fail;
    portENTER_CRITICAL(&c_lock); c_sta_ready = false; portEXIT_CRITICAL(&c_lock);
    err = esp_wifi_start(); if (err != ESP_OK) goto fail;
    portENTER_CRITICAL(&c_lock); c_started = true; portEXIT_CRITICAL(&c_lock);
    return ESP_OK;
fail:
    { esp_err_t cleanup = connectivity_release_sta(); return cleanup == ESP_OK ? err : cleanup; }
}

static esp_err_t connectivity_begin(const transfer_credentials_t *credentials) {
    // 重新创建驱动/回调边界，避免旧连接的延迟断开事件污染新目标。
    // Recreate driver/event ownership so delayed disconnects from an old target cannot poison the new one.
    esp_err_t err = connectivity_release_sta(); if (err != ESP_OK) return err;
    c_target = *credentials;
    err = connectivity_acquire_sta();
    if (err != ESP_OK) return err;
    wifi_config_t wifi = {0};
    memcpy(wifi.sta.ssid, credentials->ssid, strlen(credentials->ssid));
    memcpy(wifi.sta.password, credentials->password, strlen(credentials->password));
    wifi.sta.threshold.authmode = credentials->password[0] ? WIFI_AUTH_WPA2_PSK : WIFI_AUTH_OPEN;
    wifi.sta.pmf_cfg.capable = true; wifi.sta.sae_pwe_h2e = WPA3_SAE_PWE_BOTH;
    err = esp_wifi_set_config(WIFI_IF_STA, &wifi);
    clear_secret(&wifi, sizeof(wifi));
    if (err != ESP_OK) { (void)connectivity_release_sta(); return err; }
    connection_begin(&c_connection, connectivity_now());
    connectivity_state(READ_PICO_CONNECTIVITY_CONNECTING);
    return ESP_OK;
}

static esp_err_t connectivity_auto_next(void) {
    for (unsigned i = 0; i < c_profiles.count; ++i) {
        if (c_profiles.items[i].auto_join && !(c_tried & (1u << i))) {
            c_tried |= (uint8_t)(1u << i);
            return connectivity_begin(&c_profiles.items[i].credentials);
        }
    }
    c_auto_cycle = false; return ESP_ERR_NOT_FOUND;
}

static esp_err_t connectivity_restore(void) {
    if (!c_profiles.enabled) { clear_secret(&c_target, sizeof(c_target)); connectivity_state(READ_PICO_CONNECTIVITY_DISABLED); return ESP_OK; }
    c_tried = 0; c_auto_cycle = true;
    esp_err_t err = connectivity_auto_next();
    if (err == ESP_ERR_NOT_FOUND) {
        clear_secret(&c_target, sizeof(c_target)); err = connectivity_acquire_sta();
        if (err == ESP_OK) connectivity_state(READ_PICO_CONNECTIVITY_IDLE);
    }
    return err;
}

static esp_err_t connectivity_enabled(bool enabled) {
    if (c_profiles.enabled == enabled) return ESP_OK;
    connectivity_profiles_t next = c_profiles; next.enabled = enabled;
    esp_err_t err = profiles_store(&next);
    if (err == ESP_OK) c_profiles = next;
    clear_secret(&next, sizeof(next)); return err;
}

static esp_err_t connectivity_reload_profiles(void) {
    connectivity_profiles_t next;
    c_profiles_error = profiles_load(&next);
    if (c_profiles_error == ESP_OK) c_profiles = next;
    clear_secret(&next, sizeof(next));
    return c_profiles_error;
}

static esp_err_t connectivity_import_legacy(void) {
    transfer_credentials_t legacy;
    esp_err_t err = load_credentials(&legacy);
    if (err == ESP_OK && memcmp(&legacy, &c_legacy_before, sizeof(legacy))) {
        if (legacy.version == 1) err = profiles_remember(&c_profiles, &legacy);
        else if (c_legacy_before.version == 1 && profiles_find(&c_profiles, c_legacy_before.ssid) >= 0)
            err = profiles_forget(&c_profiles, c_legacy_before.ssid);
    }
    clear_secret(&legacy, sizeof(legacy));
    c_import_pending = err != ESP_OK;
    if (err == ESP_OK) clear_secret(&c_legacy_before, sizeof(c_legacy_before));
    return err;
}

static esp_err_t connectivity_stop_transfer(bool restore) {
    if (!c_transfer) return ESP_OK;
    connectivity_state(READ_PICO_CONNECTIVITY_STOPPING);
    esp_err_t err = read_pico_transfer_stop_checked();
    if (err != ESP_OK) return err;
    c_transfer = false;
    esp_err_t imported = c_transfer_ap ? connectivity_import_legacy() : ESP_OK;
    if (imported == ESP_OK) clear_secret(&c_legacy_before, sizeof(c_legacy_before));
    portENTER_CRITICAL(&c_lock); bool suspended = c_status.suspended; portEXIT_CRITICAL(&c_lock);
    if (restore && !suspended) err = connectivity_restore();
    else connectivity_state(READ_PICO_CONNECTIVITY_DISABLED);
    return imported != ESP_OK ? imported : err;
}

static esp_err_t connectivity_execute(const connectivity_command_t *command) {
    esp_err_t err;
    // 凭据读取失败时不把空列表写回；安全停止仍可执行，后续设置会重试读取。
    // Never overwrite unreadable profiles with an empty list; safety stops remain available and later settings retry loading.
    if (c_profiles_error != ESP_OK && command->type != CMD_STOP && command->type != CMD_RESUME && !(command->type == CMD_ENABLE && !command->enabled)) {
        err = connectivity_reload_profiles(); if (err != ESP_OK) return err;
    }
    if (c_import_pending && command->type != CMD_STOP && command->type != CMD_RESUME && command->type != CMD_FORGET && command->type != CMD_AUTO && !(command->type == CMD_ENABLE && !command->enabled)) {
        err = connectivity_import_legacy(); if (err != ESP_OK) return err;
    }
    switch (command->type) {
        case CMD_RESUME:
            portENTER_CRITICAL(&c_lock); bool suspended = c_status.suspended; portEXIT_CRITICAL(&c_lock);
            if (c_transfer && !suspended) return ESP_ERR_INVALID_STATE;
            if (!suspended && !c_resume_retry) return ESP_OK;
            c_resume_retry = true;
            // legacy页可能因HTTP join失败遗留资源；恢复日常网络前再次确认退出。
            // A failed legacy HTTP join may leave resources; confirm shutdown before restoring everyday WiFi.
            err = read_pico_transfer_stop_checked();
            if (err != ESP_OK) {
                read_pico_transfer_status_t legacy; read_pico_transfer_get_status(&legacy);
                c_transfer = true; c_transfer_ap = legacy.mode == READ_PICO_TRANSFER_MODE_AP;
                return err;
            }
            c_transfer = false;
            portENTER_CRITICAL(&c_lock); c_status.suspended = false; portEXIT_CRITICAL(&c_lock);
            if (c_profiles_error != ESP_OK) { err = connectivity_reload_profiles(); if (err != ESP_OK) return err; }
            err = c_legacy_baseline ? connectivity_import_legacy() : ESP_OK;
            if (err == ESP_OK) c_legacy_baseline = false;
            { esp_err_t restored = connectivity_restore();
              err = err == ESP_OK ? restored : err;
              c_resume_retry = err != ESP_OK;
              return err; }
        case CMD_STOP: return connectivity_stop_transfer(true);
        case CMD_ENABLE:
            if (command->enabled && c_profiles.enabled && (c_started || c_transfer)) return ESP_OK;
            err = c_profiles_error == ESP_OK ? connectivity_enabled(command->enabled) : ESP_OK; if (err != ESP_OK) return err;
            if (!command->enabled) {
                err = connectivity_stop_transfer(false); if (err != ESP_OK) return err;
                err = connectivity_release_sta(); if (err != ESP_OK) return err;
                clear_secret(&c_target, sizeof(c_target));
                connectivity_state(READ_PICO_CONNECTIVITY_DISABLED); return ESP_OK;
            }
            if (c_transfer) return ESP_OK;
            return connectivity_restore();
        case CMD_SCAN:
            if (c_transfer || !c_started) {
                connectivity_scan_result(ESP_ERR_INVALID_STATE, READ_PICO_CONNECTIVITY_SCAN_START);
                return ESP_ERR_INVALID_STATE;
            }
            // 接受后由worker等待STA启动/连接结束并有界重试，不把瞬时busy当最终失败。
            // After acceptance, the worker waits for STA/connect readiness and retries transient busy states within a bound.
            if (!c_scan && !c_scan_pending) {
                c_scan_pending = true; c_scan_start_deadline = connectivity_now() + 20000; c_scan_retry_at = 0;
                portENTER_CRITICAL(&c_lock); c_status.scan_error = ESP_OK; c_status.scan_failure = READ_PICO_CONNECTIVITY_SCAN_NONE; portEXIT_CRITICAL(&c_lock);
            }
            return ESP_OK;
        case CMD_CONNECT: case CMD_SAVED: {
            if (c_transfer) return ESP_ERR_INVALID_STATE;
            const transfer_credentials_t *credentials = &command->credentials;
            int at = profiles_find(&c_profiles, credentials->ssid);
            if (command->type == CMD_SAVED) {
                if (at < 0) return ESP_ERR_NOT_FOUND;
                credentials = &c_profiles.items[at].credentials;
            } else if (at < 0 && c_profiles.count == CONNECTIVITY_PROFILE_MAX) return ESP_ERR_NO_MEM;
            err = connectivity_enabled(true); if (err != ESP_OK) return err;
            c_auto_cycle = false; c_tried = 0;
            return connectivity_begin(credentials);
        }
        case CMD_FORGET: {
            if (c_transfer) return ESP_ERR_INVALID_STATE;
            int at = profiles_find(&c_profiles, command->credentials.ssid);
            if (at < 0) return ESP_ERR_NOT_FOUND;
            transfer_credentials_t legacy;
            err = load_credentials(&legacy);
            bool legacy_match = err == ESP_OK && legacy.version == 1 && !strcmp(legacy.ssid, command->credentials.ssid);
            clear_secret(&legacy, sizeof(legacy));
            if (err != ESP_OK) return err;
            bool current = !strcmp(c_target.ssid, command->credentials.ssid);
            // 先确认当前连接已释放，再删除记忆；失败时保留可重试的网络条目。
            // Release the current connection before deleting its profile; failures retain a retryable entry.
            if (current) {
                err = connectivity_release_sta(); if (err != ESP_OK) return err;
                clear_secret(&c_target, sizeof(c_target)); c_auto_cycle = false;
            }
            err = legacy_match ? store_credentials(NULL) : ESP_OK;
            if (err == ESP_OK) err = profiles_forget(&c_profiles, command->credentials.ssid);
            if (err == ESP_OK && at >= 0) {
                unsigned lower = (1u << at) - 1;
                c_tried = (uint8_t)((c_tried & lower) | ((c_tried >> (at + 1)) << at));
            }
            if (current) {
                esp_err_t restored = connectivity_restore();
                if (err == ESP_OK) err = restored;
            }
            return err;
        }
        case CMD_AUTO: {
            int at = profiles_find(&c_profiles, command->credentials.ssid);
            if (at < 0) return ESP_ERR_NOT_FOUND;
            connectivity_profiles_t next = c_profiles; next.items[at].auto_join = command->enabled;
            err = profiles_store(&next); if (err == ESP_OK) c_profiles = next;
            clear_secret(&next, sizeof(next)); return err;
        }
        case CMD_TRANSFER: {
            read_pico_management_stop();
            if (c_transfer) return ESP_ERR_INVALID_STATE;
            err = connectivity_release_sta(); if (err != ESP_OK) return err;
            if (command->transfer.mode == READ_PICO_TRANSFER_MODE_STA) {
                if (!credentials_valid(&c_target)) {
                    if (!c_profiles.count) return ESP_ERR_NOT_FOUND;
                    c_target = c_profiles.items[0].credentials;
                }
                err = read_pico_transfer_save_wifi(c_target.ssid, c_target.password);
                if (err != ESP_OK) { (void)connectivity_restore(); return err; }
            }
            clear_secret(&c_legacy_before, sizeof(c_legacy_before));
            err = load_credentials(&c_legacy_before);
            if (err != ESP_OK) { (void)connectivity_restore(); return err; }
            read_pico_transfer_cfg_t cfg = command->transfer;
            cfg.root_dir = command->root; cfg.font_dir = command->fonts[0] ? command->fonts : NULL;
            c_transfer = true; c_transfer_ap = cfg.mode == READ_PICO_TRANSFER_MODE_AP;
            connectivity_publish();
            err = read_pico_transfer_start(&cfg);
            if (err != ESP_OK) {
                esp_err_t stopped = read_pico_transfer_stop_checked();
                if (stopped == ESP_OK) { c_transfer = false; (void)connectivity_restore(); }
            }
            return err;
        }
    }
    return ESP_ERR_INVALID_ARG;
}

static void connectivity_scan_try(int64_t now) {
    if (!c_scan_pending || c_scan) return;
    if (now >= c_scan_start_deadline) {
        connectivity_scan_result(ESP_ERR_TIMEOUT, READ_PICO_CONNECTIVITY_SCAN_TIMEOUT); return;
    }
    portENTER_CRITICAL(&c_lock); bool ready = c_sta_ready; portEXIT_CRITICAL(&c_lock);
    if (!ready || now < c_scan_retry_at || (!c_connection.online && c_connection.deadline_ms > 0 && !c_connection.failed)) return;
    wifi_scan_config_t scan = {.show_hidden = false, .scan_type = WIFI_SCAN_TYPE_ACTIVE,
        .scan_time.active = {.min = 100, .max = 200}};
    portENTER_CRITICAL(&c_lock); c_scan_done = false; c_scan_event_status = 0; c_scan = true; portEXIT_CRITICAL(&c_lock);
    esp_err_t err = esp_wifi_scan_start(&scan, false);
    if (err == ESP_OK) {
        c_scan_pending = false; c_scan_deadline = now + 10000;
        connectivity_state(READ_PICO_CONNECTIVITY_SCANNING);
    } else if (err == ESP_ERR_WIFI_STATE || err == ESP_ERR_WIFI_NOT_STARTED || err == ESP_ERR_INVALID_STATE) {
        portENTER_CRITICAL(&c_lock); c_scan = false; portEXIT_CRITICAL(&c_lock);
        c_scan_retry_at = now + 500;
    } else connectivity_scan_result(err, READ_PICO_CONNECTIVITY_SCAN_START);
}

static void connectivity_scan_finish(uint32_t event_status) {
    read_pico_transfer_network_t found[READ_PICO_TRANSFER_SCAN_MAX] = {0};
    size_t count = 0; uint16_t total = 0;
    esp_err_t err = event_status ? ESP_FAIL : esp_wifi_scan_get_ap_num(&total);
    for (uint16_t i = 0; err == ESP_OK && i < total; ++i) {
        wifi_ap_record_t ap;
        err = esp_wifi_scan_get_ap_record(&ap); if (err != ESP_OK) break;
        read_pico_transfer_network_t item = {.rssi = ap.rssi, .authmode = (uint8_t)ap.authmode,
            .requires_password = ap.authmode != WIFI_AUTH_OPEN && ap.authmode != WIFI_AUTH_OWE};
        memcpy(item.ssid, ap.ssid, 32);
        item.supported = ap.authmode == WIFI_AUTH_OPEN || ap.authmode == WIFI_AUTH_WPA2_PSK || ap.authmode == WIFI_AUTH_WPA_WPA2_PSK || ap.authmode == WIFI_AUTH_WPA3_PSK || ap.authmode == WIFI_AUTH_WPA2_WPA3_PSK;
        scan_offer(found, &count, &item);
    }
    esp_wifi_clear_ap_list();
    portENTER_CRITICAL(&c_lock);
    if (err == ESP_OK) { memcpy(c_status.networks, found, sizeof(found)); c_status.network_count = count; }
    portEXIT_CRITICAL(&c_lock);
    connectivity_scan_result(err, err == ESP_OK ? READ_PICO_CONNECTIVITY_SCAN_NONE : READ_PICO_CONNECTIVITY_SCAN_RESULT);
    connectivity_state(c_connection.online ? READ_PICO_CONNECTIVITY_CONNECTED : READ_PICO_CONNECTIVITY_IDLE);
}

static void connectivity_poll(void) {
    if (c_transfer) {
        read_pico_transfer_service_poll(); connectivity_publish(); return;
    }
    if (!c_started) return;
    bool got_ip, lost, scan_done; uint32_t scan_event_status;
    portENTER_CRITICAL(&c_lock);
    got_ip = c_got_ip; lost = c_lost; scan_done = c_scan_done; scan_event_status = c_scan_event_status;
    c_got_ip = c_lost = c_scan_done = false;
    portEXIT_CRITICAL(&c_lock);
    int64_t now = connectivity_now();
    if (lost && !c_connection.failed && c_target.version == 1) {
        if (c_connection.online) {
            c_auto_cycle = true; c_tried = 0;
            int current = profiles_find(&c_profiles, c_target.ssid);
            if (current >= 0) c_tried = (uint8_t)(1u << current);
        }
        connection_lost(&c_connection, now); connectivity_state(READ_PICO_CONNECTIVITY_CONNECTING);
    }
    if (got_ip) {
        if (c_connection.failed || c_connection.deadline_ms <= 0 || c_target.version != 1) esp_wifi_disconnect();
        else {
            c_connection.online = true; c_connection.pending = false;
            connectivity_result(READ_PICO_CONNECTIVITY_OP_PROFILE, profiles_remember(&c_profiles, &c_target));
            connectivity_state(READ_PICO_CONNECTIVITY_CONNECTED);
        }
    }
    if (c_scan && scan_done) connectivity_scan_finish(scan_event_status);
    else if (c_scan && now >= c_scan_deadline) {
        esp_wifi_scan_stop(); esp_wifi_clear_ap_list();
        connectivity_scan_result(ESP_ERR_TIMEOUT, READ_PICO_CONNECTIVITY_SCAN_TIMEOUT); connectivity_state(c_connection.online ? READ_PICO_CONNECTIVITY_CONNECTED : READ_PICO_CONNECTIVITY_IDLE);
    }
    if (!c_scan && c_target.version == 1 && c_connection.deadline_ms > 0) {
        int action = connection_poll(&c_connection, now);
        if (action == 1) {
            esp_err_t err = esp_wifi_connect();
            if (err != ESP_OK) { connection_lost(&c_connection, now); connectivity_result(READ_PICO_CONNECTIVITY_OP_CONNECT, err); }
        } else if (action < 0) {
            esp_wifi_disconnect();
            if (!c_auto_cycle || connectivity_auto_next() != ESP_OK) {
                c_auto_cycle = false; connectivity_result(READ_PICO_CONNECTIVITY_OP_CONNECT, ESP_ERR_TIMEOUT); connectivity_state(READ_PICO_CONNECTIVITY_ERROR);
            }
        }
    }
    connectivity_scan_try(now);
    if (c_connection.online) {
        wifi_ap_record_t ap;
        if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK) { portENTER_CRITICAL(&c_lock); c_status.rssi = ap.rssi; portEXIT_CRITICAL(&c_lock); }
    }
    connectivity_publish();
}

static void connectivity_worker_initialize(void) {
    esp_err_t err = connectivity_reload_profiles();
    if (err != ESP_OK) { clear_secret(&c_profiles, sizeof(c_profiles)); c_profiles.version = 1; }
    connectivity_error(err);
    if (err == ESP_OK) connectivity_error(connectivity_restore());
    portENTER_CRITICAL(&c_lock); c_status.ready = true; c_status.busy = c_pending != 0 || c_quiesce_requested; portEXIT_CRITICAL(&c_lock);
    connectivity_publish();
}

static void connectivity_worker_step(void) {
        portENTER_CRITICAL(&c_lock); bool quiesce = c_quiesce_requested; portEXIT_CRITICAL(&c_lock);
        if (quiesce) {
            // 等正在提交的指针入队，再一次排空；禁止静默完成后落入迟到命令。
            // Wait for in-flight sends before draining, so no command arrives after quiescence completes.
            portENTER_CRITICAL(&c_lock); bool submitting = c_submitting != 0; portEXIT_CRITICAL(&c_lock);
            if (submitting) { vTaskDelay(pdMS_TO_TICKS(50)); return; }
            connectivity_command_t *discarded;
            while (xQueueReceive(c_queue, &discarded, 0) == pdTRUE) { clear_secret(discarded, sizeof(*discarded)); free(discarded); }
            esp_err_t err = connectivity_stop_transfer(false);
            if (err == ESP_OK) err = connectivity_release_sta();
            if (err == ESP_OK) {
                clear_secret(&c_target, sizeof(c_target)); c_auto_cycle = false;
                c_legacy_baseline = c_import_pending || load_credentials(&c_legacy_before) == ESP_OK;
                portENTER_CRITICAL(&c_lock);
                c_pending = 0; c_status.busy = false; c_status.suspended = true;
                c_status.completed_seq = c_status.submitted_seq; c_quiesce_requested = false;
                portEXIT_CRITICAL(&c_lock);
                connectivity_state(READ_PICO_CONNECTIVITY_DISABLED);
            } else connectivity_error(err);
            connectivity_publish(); vTaskDelay(pdMS_TO_TICKS(50)); return;
        }
        connectivity_command_t *command;
        if (xQueueReceive(c_queue, &command, pdMS_TO_TICKS(50)) == pdTRUE) {
            portENTER_CRITICAL(&c_lock);
            bool obsolete = command->epoch != c_epoch || c_quiesce_requested || (c_status.suspended && !connectivity_allowed_suspended(command->type, command->enabled));
            portEXIT_CRITICAL(&c_lock);
            uint32_t scan_before = c_status.scan_completed_seq;
            esp_err_t err = obsolete ? ESP_ERR_INVALID_STATE : connectivity_execute(command);
            if (command->type == CMD_SCAN && err != ESP_OK && c_status.scan_completed_seq == scan_before)
                connectivity_scan_result(err, READ_PICO_CONNECTIVITY_SCAN_START);
            connectivity_result(connectivity_operation(command->type), err);
            uint32_t seq = command->seq; clear_secret(command, sizeof(*command)); free(command);
            // 先公开真实资源所有权，再公开命令完成；避免调用方过早恢复介质读取。
            // Publish resource ownership before command completion, so callers cannot resume media reads early.
            if (c_started && !c_transfer) connectivity_scan_try(connectivity_now());
            connectivity_publish();
            portENTER_CRITICAL(&c_lock);
            if (c_pending) --c_pending;
            if (seq > c_status.completed_seq) c_status.completed_seq = seq;
            c_status.busy = c_pending != 0 || c_quiesce_requested;
            portEXIT_CRITICAL(&c_lock);
        }
        if (connectivity_now() - c_last_poll >= 500) { c_last_poll = connectivity_now(); connectivity_poll(); }
}

static void connectivity_worker(void *arg) {
    (void)arg; connectivity_worker_initialize();
    for (;;) connectivity_worker_step();
}

esp_err_t read_pico_connectivity_init(void) {
    portENTER_CRITICAL(&c_lock);
    if (c_queue) { portEXIT_CRITICAL(&c_lock); return ESP_OK; }
    if (c_initializing) { portEXIT_CRITICAL(&c_lock); return ESP_ERR_INVALID_STATE; }
    c_initializing = true; c_status.busy = true;
    portEXIT_CRITICAL(&c_lock);
    QueueHandle_t queue = xQueueCreate(4, sizeof(connectivity_command_t *));
    if (!queue) { portENTER_CRITICAL(&c_lock); c_initializing = c_status.busy = false; portEXIT_CRITICAL(&c_lock); return ESP_ERR_NO_MEM; }
    portENTER_CRITICAL(&c_lock); c_queue = queue; portEXIT_CRITICAL(&c_lock);
    if (xTaskCreate(connectivity_worker, "pico-network", 6144, NULL, 3, NULL) != pdPASS) {
        portENTER_CRITICAL(&c_lock); c_queue = NULL; c_initializing = c_status.busy = false; portEXIT_CRITICAL(&c_lock);
        vQueueDelete(queue); return ESP_ERR_NO_MEM;
    }
    portENTER_CRITICAL(&c_lock); c_initializing = false; portEXIT_CRITICAL(&c_lock);
    return ESP_OK;
}

static esp_err_t connectivity_submit(connectivity_command_t *command) {
    connectivity_command_t *pending = malloc(sizeof(*pending));
    if (!pending) { clear_secret(command, sizeof(*command)); return ESP_ERR_NO_MEM; }
    portENTER_CRITICAL(&c_lock);
    bool accepted = c_queue && !c_initializing && !c_quiesce_requested && (!c_status.suspended || connectivity_allowed_suspended(command->type, command->enabled));
    if (accepted) { command->seq = ++c_next_seq; command->epoch = c_epoch; ++c_pending; ++c_submitting; c_status.busy = true; }
    portEXIT_CRITICAL(&c_lock);
    if (!accepted) { free(pending); clear_secret(command, sizeof(*command)); return ESP_ERR_INVALID_STATE; }
    *pending = *command;
    bool queued = xQueueSend(c_queue, &pending, 0) == pdTRUE;
    portENTER_CRITICAL(&c_lock);
    --c_submitting;
    if (queued) { if (command->seq > c_status.submitted_seq) c_status.submitted_seq = command->seq; }
    else { if (c_pending) --c_pending; c_status.busy = c_pending != 0 || c_quiesce_requested; }
    portEXIT_CRITICAL(&c_lock);
    if (!queued) { clear_secret(pending, sizeof(*pending)); free(pending); }
    clear_secret(command, sizeof(*command));
    return queued ? ESP_OK : ESP_ERR_NO_MEM;
}

esp_err_t read_pico_connectivity_set_enabled(bool enabled) { connectivity_command_t command = {.type = CMD_ENABLE, .enabled = enabled}; return connectivity_submit(&command); }
esp_err_t read_pico_connectivity_scan(void) { connectivity_command_t command = {.type = CMD_SCAN}; return connectivity_submit(&command); }
esp_err_t read_pico_connectivity_stop_transfer(void) { connectivity_command_t command = {.type = CMD_STOP}; return connectivity_submit(&command); }
esp_err_t read_pico_connectivity_resume(void) { connectivity_command_t command = {.type = CMD_RESUME}; return connectivity_submit(&command); }
void read_pico_connectivity_get_resume_status(read_pico_connectivity_resume_status_t *out) {
    if(!out)return;
    portENTER_CRITICAL(&c_lock);
    *out=(read_pico_connectivity_resume_status_t){c_status.suspended,c_status.busy,
        c_status.submitted_seq,c_status.completed_seq,c_status.last_error};
    portEXIT_CRITICAL(&c_lock);
}

static esp_err_t connectivity_ssid_command(int type, const char *ssid, bool enabled) {
    connectivity_command_t command = {.type = type, .enabled = enabled};
    if (!credentials_from_text(ssid, "", &command.credentials)) return ESP_ERR_INVALID_ARG;
    return connectivity_submit(&command);
}

esp_err_t read_pico_connectivity_connect(const char *ssid, const char *password) {
    connectivity_command_t command = {.type = CMD_CONNECT};
    if (!credentials_from_text(ssid, password, &command.credentials)) return ESP_ERR_INVALID_ARG;
    return connectivity_submit(&command);
}
esp_err_t read_pico_connectivity_connect_saved(const char *ssid) { return connectivity_ssid_command(CMD_SAVED, ssid, false); }
esp_err_t read_pico_connectivity_forget(const char *ssid) { return connectivity_ssid_command(CMD_FORGET, ssid, false); }
esp_err_t read_pico_connectivity_set_auto_join(const char *ssid, bool enabled) { return connectivity_ssid_command(CMD_AUTO, ssid, enabled); }

esp_err_t read_pico_connectivity_start_transfer(const read_pico_transfer_cfg_t *cfg) {
    connectivity_command_t command = {.type = CMD_TRANSFER};
    if (!cfg || !cfg->root_dir || !cfg->free_bytes_cb || strnlen(cfg->root_dir, 160) >= 160 ||
        (cfg->font_dir && (cfg->is_flash || !cfg->font_dir[0] || strnlen(cfg->font_dir, 160) >= 160)) ||
        (cfg->mode != READ_PICO_TRANSFER_MODE_AP && cfg->mode != READ_PICO_TRANSFER_MODE_STA)) return ESP_ERR_INVALID_ARG;
    command.transfer = *cfg;
    snprintf(command.root, sizeof(command.root), "%s", cfg->root_dir);
    snprintf(command.fonts, sizeof(command.fonts), "%s", cfg->font_dir ? cfg->font_dir : "");
    command.transfer.root_dir = command.transfer.font_dir = NULL;
    return connectivity_submit(&command);
}

esp_err_t read_pico_connectivity_quiesce(uint32_t timeout_ms) {
    portENTER_CRITICAL(&c_lock);
    if (!c_queue) { portEXIT_CRITICAL(&c_lock); return ESP_OK; }
    if (c_status.suspended && !c_status.busy && !c_status.transfer_active && !c_quiesce_requested) { portEXIT_CRITICAL(&c_lock); return ESP_OK; }
    if (!c_quiesce_requested) ++c_epoch;
    c_quiesce_requested = true; c_status.busy = true;
    portEXIT_CRITICAL(&c_lock);
    int64_t deadline = connectivity_now() + timeout_ms;
    do {
        portENTER_CRITICAL(&c_lock); bool stopped = c_status.suspended && !c_status.busy && !c_status.transfer_active && !c_quiesce_requested; portEXIT_CRITICAL(&c_lock);
        if (stopped) return ESP_OK;
        if (connectivity_now() >= deadline) break;
        vTaskDelay(pdMS_TO_TICKS(10));
    } while (true);
    return ESP_ERR_TIMEOUT;
}

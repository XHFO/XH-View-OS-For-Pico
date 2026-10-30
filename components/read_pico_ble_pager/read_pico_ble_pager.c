/*
 * SPDX-FileCopyrightText: 2026 XH Lab
 * SPDX-License-Identifier: Apache-2.0
 *
 * 仅实现 BLE HID over GATT 主机：有界扫描、配对记忆、重连与事件队列。
 * BLE HID-over-GATT host only: bounded discovery, bonding, reconnect and event queues.
 *
 * 冻结：不绘图、不调用阅读器、不擦除其他 NVS 命名空间、不支持经典蓝牙。
 * Frozen: no drawing, reader calls, unrelated NVS erasure or Classic Bluetooth.
 */
#include "read_pico_ble_pager.h"
#include "pager_hid.h"
#include <string.h>
#include <stdatomic.h>
#include "sdkconfig.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "nvs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#if CONFIG_BT_NIMBLE_ENABLED
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/ble_gap.h"
#include "host/ble_gatt.h"
#include "host/ble_store.h"
#include "host/ble_hs_adv.h"
#include "host/util/util.h"
#include "os/os_mbuf.h"

#define PAGER_UUID_HID 0x1812
#define PAGER_MAX_REPORTS 8
#define PAGER_MAP_SIZE 1024
#define PAGER_COMMAND_COUNT 8
#define PAGER_EVENT_COUNT 8

typedef enum { CMD_ENABLE, CMD_SCAN, CMD_CONNECT, CMD_DISCONNECT, CMD_FORGET, CMD_REVERSE, CMD_RESERVE } command_kind_t;
typedef struct { command_kind_t kind; bool value; read_pico_ble_pager_device_t peer; } command_t;
typedef struct { int8_t direction; int64_t timestamp; } page_event_t;
typedef struct {
    uint16_t value, end, cccd, reference;
    uint8_t id, type;
    bool boot, subscribed;
} input_report_t;

static const char *TAG = "ble_pager";
static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
static read_pico_ble_pager_status_t s_status = {.battery_percent = 255};
static read_pico_ble_pager_device_t s_devices[READ_PICO_BLE_PAGER_MAX_DEVICES];
static read_pico_ble_pager_device_t s_peer, s_remembered;
static QueueHandle_t s_commands, s_events;
static bool s_initialized, s_auto_reconnect, s_discovering, s_want_connection, s_forget_bond;
static bool s_worker_started, s_worker_starting;
static unsigned s_submitters;
static uintptr_t s_connect_generation;
static atomic_bool s_stack_started, s_stopping, s_reserved;
static bool s_resume_enabled;
static struct ble_npl_event s_control_event;
static struct ble_npl_callout s_poll_timer;
static uint16_t s_connection = BLE_HS_CONN_HANDLE_NONE, s_service_start, s_service_end, s_map_handle, s_protocol_handle;
static uint8_t s_own_address_type, s_report_count, s_report_cursor, s_boot_held, s_reconnect_attempts;
static int64_t s_retry_after, s_setup_deadline;
static input_report_t s_reports[PAGER_MAX_REPORTS];
static pager_hid_t s_hid;
static uint8_t s_map[PAGER_MAP_SIZE];
static size_t s_map_length;

void ble_store_config_init(void);
static int gap_event(struct ble_gap_event *event, void *arg);
static void begin_discovery(void);
static void next_report(void);
static void begin_reports(void);
static void control_event(struct ble_npl_event *event);
static void poll_event(struct ble_npl_event *event);
static esp_err_t send_command(command_t command);

static void bump_locked(void) { ++s_status.revision; }

static void set_error(int error)
{
    portENTER_CRITICAL(&s_lock);
    s_status.error = error;
    bump_locked();
    portEXIT_CRITICAL(&s_lock);
}

static void clear_events(void)
{
    page_event_t ignored;
    while (s_events && xQueueReceive(s_events, &ignored, 0) == pdTRUE) { }
}

static void remember_save(void)
{
    nvs_handle_t nvs;
    if (nvs_open("pico_ble", NVS_READWRITE, &nvs) != ESP_OK) return;
    nvs_set_blob(nvs, "peer", &s_remembered, sizeof(s_remembered));
    nvs_commit(nvs);
    nvs_close(nvs);
}

static void connection_failed(int error)
{
    s_want_connection = false;
    ESP_LOGW(TAG, "HID setup failed: %d", error);
    portENTER_CRITICAL(&s_lock);
    s_status.connecting = false;
    s_status.connected = false;
    s_status.error = error;
    bump_locked();
    portEXIT_CRITICAL(&s_lock);
    s_discovering = false;
    if (s_connection != BLE_HS_CONN_HANDLE_NONE) ble_gap_terminate(s_connection, BLE_ERR_REM_USER_CONN_TERM);
    s_retry_after = esp_timer_get_time() + 5000000;
}

static bool current_connection(uint16_t conn) { return s_want_connection && s_status.enabled && conn == s_connection && conn != BLE_HS_CONN_HANDLE_NONE; }

static void connection_ready(void)
{
    if (!current_connection(s_connection)) return;
    unsigned subscribed = 0;
    for (unsigned i = 0; i < s_report_count; ++i) subscribed += s_reports[i].subscribed;
    if (!subscribed) { connection_failed(ESP_ERR_NOT_SUPPORTED); return; }
    struct ble_gap_conn_desc desc;
    if (ble_gap_conn_find(s_connection, &desc) == 0) {
        memcpy(s_peer.address, desc.peer_id_addr.val, 6);
        s_peer.address_type = desc.peer_id_addr.type;
    }
    s_remembered = s_peer;
    remember_save();
    s_reconnect_attempts = 0;
    portENTER_CRITICAL(&s_lock);
    s_status.connecting = false;
    s_status.connected = true;
    s_status.remembered = true;
    s_status.error = 0;
    memcpy(s_status.name, s_peer.name, sizeof(s_status.name));
    bump_locked();
    portEXIT_CRITICAL(&s_lock);
    ESP_LOGI(TAG, "BLE HID ready: %s, %u input reports", s_peer.name, subscribed);
}

static int subscribed_cb(uint16_t conn, const struct ble_gatt_error *error, struct ble_gatt_attr *attr, void *arg)
{
    (void)attr;
    input_report_t *r = arg;
    if (!current_connection(conn)) return 0;
    if (!error->status) r->subscribed = true;
    else ESP_LOGW(TAG, "Input subscription failed: %d", error->status);
    ++s_report_cursor;
    next_report();
    return 0;
}

static void subscribe_report(input_report_t *r)
{
    if (!r->cccd || (!r->boot && r->type != 1)) { ++s_report_cursor; next_report(); return; }
    if (!r->boot) {
        bool supported = false;
        for (unsigned i = 0; i < s_hid.field_count; ++i) if (s_hid.fields[i].report_id == r->id) supported = true;
        if (!supported) { ++s_report_cursor; next_report(); return; }
    }
    uint8_t enable[2] = {1, 0};
    int rc = ble_gattc_write_flat(s_connection, r->cccd, enable, sizeof(enable), subscribed_cb, r);
    if (rc) { ++s_report_cursor; next_report(); }
}

static int reference_cb(uint16_t conn, const struct ble_gatt_error *error, struct ble_gatt_attr *attr, void *arg)
{
    input_report_t *r = arg;
    if (!current_connection(conn)) return 0;
    uint8_t ref[2];
    if (!error->status && attr && OS_MBUF_PKTLEN(attr->om) == 2 && os_mbuf_copydata(attr->om, 0, 2, ref) == 0) {
        r->id = ref[0]; r->type = ref[1];
    }
    subscribe_report(r);
    return 0;
}

static int descriptor_cb(uint16_t conn, const struct ble_gatt_error *error, uint16_t value,
                         const struct ble_gatt_dsc *dsc, void *arg)
{
    (void)value;
    input_report_t *r = arg;
    if (!current_connection(conn)) return 0;
    if (!error->status && dsc) {
        uint16_t uuid = ble_uuid_u16(&dsc->uuid.u);
        if (uuid == 0x2902) r->cccd = dsc->handle;
        else if (uuid == 0x2908) r->reference = dsc->handle;
        return 0;
    }
    if (error->status != BLE_HS_EDONE) { ++s_report_cursor; next_report(); return 0; }
    if (r->boot) { subscribe_report(r); return 0; }
    if (r->reference) {
        int rc = ble_gattc_read(conn, r->reference, reference_cb, r);
        if (!rc) return 0;
    }
    ++s_report_cursor; next_report();
    return 0;
}

static void next_report(void)
{
    if (!current_connection(s_connection)) return;
    while (s_report_cursor < s_report_count) {
        input_report_t *r = &s_reports[s_report_cursor];
        if ((!r->boot && !s_hid.field_count) || (r->boot && s_hid.field_count) || r->end <= r->value) { ++s_report_cursor; continue; }
        int rc = ble_gattc_disc_all_dscs(s_connection, r->value, r->end, descriptor_cb, r);
        if (!rc) return;
        ++s_report_cursor;
    }
    connection_ready();
}

static void begin_reports(void)
{
    if (!current_connection(s_connection)) return;
    if (!s_hid.field_count) {
        if (!s_protocol_handle) { connection_failed(ESP_ERR_NOT_SUPPORTED); return; }
        uint8_t boot_mode = 0;
        int rc = ble_gattc_write_no_rsp_flat(s_connection, s_protocol_handle, &boot_mode, 1);
        if (rc) { connection_failed(rc); return; }
    }
    s_report_cursor = 0;
    next_report();
}

static int map_cb(uint16_t conn, const struct ble_gatt_error *error, struct ble_gatt_attr *attr, void *arg)
{
    (void)arg;
    if (!current_connection(conn)) return 0;
    if (!error->status && attr) {
        size_t n = OS_MBUF_PKTLEN(attr->om);
        if ((size_t)attr->offset + n > sizeof(s_map) || attr->offset != s_map_length) {
            connection_failed(ESP_ERR_INVALID_SIZE); return BLE_HS_EAPP;
        }
        if (os_mbuf_copydata(attr->om, 0, n, s_map + s_map_length)) {
            connection_failed(ESP_FAIL); return BLE_HS_EAPP;
        }
        s_map_length += n;
        return 0;
    }
    bool complete = error->status == BLE_HS_EDONE ||
                    (s_map_length && error->status == BLE_HS_ATT_ERR(BLE_ATT_ERR_INVALID_OFFSET));
    if (!complete || !pager_hid_parse(&s_hid, s_map, s_map_length)) {
        memset(&s_hid, 0, sizeof(s_hid));
    }
    begin_reports();
    return 0;
}

static int characteristic_cb(uint16_t conn, const struct ble_gatt_error *error,
                             const struct ble_gatt_chr *chr, void *arg)
{
    (void)arg;
    if (!current_connection(conn)) return 0;
    if (!error->status && chr) {
        if (s_report_count && !s_reports[s_report_count - 1].end) s_reports[s_report_count - 1].end = chr->def_handle - 1;
        uint16_t uuid = ble_uuid_u16(&chr->uuid.u);
        if (uuid == 0x2a4b) s_map_handle = chr->val_handle;
        if (uuid == 0x2a4e) s_protocol_handle = chr->val_handle;
        if ((uuid == 0x2a4d || uuid == 0x2a22) && (chr->properties & BLE_GATT_CHR_PROP_NOTIFY)) {
            if (s_report_count == PAGER_MAX_REPORTS) { connection_failed(ESP_ERR_NO_MEM); return BLE_HS_EAPP; }
            s_reports[s_report_count++] = (input_report_t){.value = chr->val_handle, .boot = uuid == 0x2a22, .type = uuid == 0x2a22 ? 1 : 0};
        }
        return 0;
    }
    if (error->status != BLE_HS_EDONE) { connection_failed(error->status); return 0; }
    if (s_report_count && !s_reports[s_report_count - 1].end) s_reports[s_report_count - 1].end = s_service_end;
    if (!s_report_count) { connection_failed(ESP_ERR_NOT_SUPPORTED); return 0; }
    if (s_map_handle) {
        int rc = ble_gattc_read_long(conn, s_map_handle, 0, map_cb, NULL);
        if (!rc) return 0;
    }
    begin_reports();
    return 0;
}

static int service_cb(uint16_t conn, const struct ble_gatt_error *error, const struct ble_gatt_svc *svc, void *arg)
{
    (void)arg;
    if (!current_connection(conn)) return 0;
    if (!error->status && svc && !s_service_start) { s_service_start = svc->start_handle; s_service_end = svc->end_handle; return 0; }
    if (!error->status) return 0;
    if (error->status != BLE_HS_EDONE || !s_service_start) { connection_failed(ESP_ERR_NOT_SUPPORTED); return 0; }
    int rc = ble_gattc_disc_all_chrs(conn, s_service_start, s_service_end, characteristic_cb, NULL);
    if (rc) connection_failed(rc);
    return 0;
}

static void begin_discovery(void)
{
    if (s_discovering || !current_connection(s_connection)) return;
    s_discovering = true;
    s_service_start = s_service_end = s_map_handle = s_protocol_handle = 0;
    s_report_count = s_report_cursor = s_boot_held = 0;
    s_map_length = 0;
    memset(s_reports, 0, sizeof(s_reports));
    memset(&s_hid, 0, sizeof(s_hid));
    ble_uuid16_t uuid = BLE_UUID16_INIT(PAGER_UUID_HID);
    int rc = ble_gattc_disc_svc_by_uuid(s_connection, &uuid.u, service_cb, NULL);
    if (rc) connection_failed(rc);
}

static void scan_result(const struct ble_gap_disc_desc *disc)
{
    struct ble_hs_adv_fields fields;
    if (ble_hs_adv_parse_fields(&fields, disc->data, disc->length_data)) return;
    bool hogp = false;
    for (unsigned i = 0; i < fields.num_uuids16; ++i) if (ble_uuid_u16(&fields.uuids16[i].u) == PAGER_UUID_HID) hogp = true;
    portENTER_CRITICAL(&s_lock);
    size_t i;
    for (i = 0; i < s_status.device_count; ++i) {
        if (s_devices[i].address_type == disc->addr.type && !memcmp(s_devices[i].address, disc->addr.val, 6)) break;
    }
    if (i == s_status.device_count) {
        if (!hogp || i == READ_PICO_BLE_PAGER_MAX_DEVICES) { portEXIT_CRITICAL(&s_lock); return; }
        memset(&s_devices[i], 0, sizeof(s_devices[i]));
        memcpy(s_devices[i].address, disc->addr.val, 6);
        s_devices[i].address_type = disc->addr.type;
        strcpy(s_devices[i].name, "BLE HID");
        ++s_status.device_count;
        bump_locked();
    }
    s_devices[i].rssi = disc->rssi;
    if (fields.name && fields.name_len) {
        char name[READ_PICO_BLE_PAGER_NAME_SIZE] = {0};
        size_t n = fields.name_len < sizeof(name) - 1 ? fields.name_len : sizeof(name) - 1;
        for (size_t k = 0; k < n; ++k) name[k] = fields.name[k] >= 32 ? fields.name[k] : ' ';
        if (strcmp(name, s_devices[i].name)) { memcpy(s_devices[i].name, name, sizeof(name)); bump_locked(); }
    }
    portEXIT_CRITICAL(&s_lock);
}

static int gap_event(struct ble_gap_event *event, void *arg)
{
    uintptr_t generation = (uintptr_t)arg;
    switch (event->type) {
    case BLE_GAP_EVENT_DISC:
        scan_result(&event->disc);
        break;
    case BLE_GAP_EVENT_DISC_COMPLETE:
        portENTER_CRITICAL(&s_lock); s_status.scanning = false; bump_locked(); portEXIT_CRITICAL(&s_lock);
        break;
    case BLE_GAP_EVENT_CONNECT:
        if (!s_want_connection || generation != s_connect_generation || !s_status.enabled) {
            if (!event->connect.status) ble_gap_terminate(event->connect.conn_handle, BLE_ERR_REM_USER_CONN_TERM);
            break;
        }
        if (event->connect.status) { connection_failed(event->connect.status); break; }
        s_connection = event->connect.conn_handle;
        if (!s_status.enabled) { ble_gap_terminate(s_connection, BLE_ERR_REM_USER_CONN_TERM); break; }
        {
            int rc = ble_gap_security_initiate(s_connection);
            if (rc == BLE_HS_EALREADY) begin_discovery();
            else if (rc) connection_failed(rc);
        }
        break;
    case BLE_GAP_EVENT_ENC_CHANGE:
        if (generation != s_connect_generation) break;
        if (current_connection(event->enc_change.conn_handle)) {
            if (!event->enc_change.status) begin_discovery();
            else connection_failed(event->enc_change.status);
        }
        break;
    case BLE_GAP_EVENT_DISCONNECT:
        if (event->disconnect.conn.conn_handle != s_connection) break;
        s_connection = BLE_HS_CONN_HANDLE_NONE;
        s_discovering = false;
        memset(&s_hid, 0, sizeof(s_hid)); s_boot_held = 0;
        clear_events();
        portENTER_CRITICAL(&s_lock);
        s_status.connected = s_status.connecting = false; s_status.battery_percent = 255; bump_locked();
        portEXIT_CRITICAL(&s_lock);
        s_retry_after = esp_timer_get_time() + 3000000;
        break;
    case BLE_GAP_EVENT_NOTIFY_RX:
        if (generation != s_connect_generation) break;
        if (!current_connection(event->notify_rx.conn_handle) || !s_status.connected || !s_status.enabled) break;
        for (unsigned i = 0; i < s_report_count; ++i) {
            input_report_t *r = &s_reports[i];
            if (r->value != event->notify_rx.attr_handle || !r->subscribed) continue;
            uint8_t report[64];
            size_t n = OS_MBUF_PKTLEN(event->notify_rx.om);
            if (!n || n > sizeof(report) || os_mbuf_copydata(event->notify_rx.om, 0, n, report)) break;
            int8_t page = r->boot ? pager_hid_boot_input(&s_boot_held, report, n) : pager_hid_input(&s_hid, r->id, report, n);
            if (page) {
                if (s_status.reversed) page = -page;
                page_event_t queued = {.direction = page, .timestamp = esp_timer_get_time()};
                if (xQueueSend(s_events, &queued, 0) != pdTRUE) ESP_LOGW(TAG, "Page-turn queue full; input dropped");
            }
            break;
        }
        break;
    case BLE_GAP_EVENT_REPEAT_PAIRING:
        if (generation != s_connect_generation || !s_want_connection) break;
        // 不静默覆盖旧配对；用户须先在设置里忘记设备。/ Do not replace a bond silently; user must forget it first.
        connection_failed(ESP_ERR_INVALID_STATE);
        return BLE_GAP_REPEAT_PAIRING_IGNORE;
    case BLE_GAP_EVENT_PASSKEY_ACTION:
        if (generation != s_connect_generation || !s_want_connection) break;
        // 仅无输入输出的翻页器；不注入固定 PIN。/ Only no-input/no-output pagers; never inject a fixed PIN.
        connection_failed(ESP_ERR_NOT_SUPPORTED);
        break;
    default: break;
    }
    return 0;
}

static void host_synced(void)
{
    int rc = ble_hs_util_ensure_addr(0);
    if (!rc) rc = ble_hs_id_infer_auto(0, &s_own_address_type);
    portENTER_CRITICAL(&s_lock);
    s_status.ready = rc == 0; s_status.error = rc; bump_locked();
    portEXIT_CRITICAL(&s_lock);
    if (!rc && s_forget_bond) {
        int clear_rc = ble_store_clear();
        if (clear_rc) set_error(clear_rc);
        else {
            s_forget_bond = false;
            nvs_handle_t nvs;
            if (nvs_open("pico_ble", NVS_READWRITE, &nvs) == ESP_OK) { nvs_erase_key(nvs, "forget_bond"); nvs_commit(nvs); nvs_close(nvs); }
        }
    }
}

static void host_reset(int reason)
{
    s_connection = BLE_HS_CONN_HANDLE_NONE;
    s_discovering = false;
    portENTER_CRITICAL(&s_lock);
    s_status.ready = s_status.connected = s_status.connecting = s_status.scanning = false;
    s_status.error = reason; bump_locked();
    portEXIT_CRITICAL(&s_lock);
    clear_events();
}

static void host_task(void *arg)
{
    (void)arg;
    nimble_port_run();
    // 关闭任务在stop回执后统一删除主机任务，避免两个线程重复删除同一handle。
    // The shutdown task owns host deletion after stop acknowledges; never self-delete the shared handle.
    vTaskSuspend(NULL);
}

static esp_err_t start_stack(void)
{
    if (s_stack_started) return ESP_OK;
    esp_err_t rc = nimble_port_init();
    if (rc != ESP_OK) return rc;
    ble_hs_cfg.sync_cb = host_synced;
    ble_hs_cfg.reset_cb = host_reset;
    ble_hs_cfg.sm_io_cap = BLE_HS_IO_NO_INPUT_OUTPUT;
    ble_hs_cfg.sm_bonding = 1;
    ble_hs_cfg.sm_mitm = 0;
    ble_hs_cfg.sm_sc = 1;
    ble_hs_cfg.sm_our_key_dist = BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;
    ble_hs_cfg.sm_their_key_dist = BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;
    ble_hs_cfg.store_status_cb = ble_store_util_status_rr;
    ble_store_config_init();
    nvs_handle_t nvs;
    if (nvs_open("pico_ble", NVS_READWRITE, &nvs) == ESP_OK) {
        uint8_t forget = 0;
        if (nvs_get_u8(nvs, "forget_bond", &forget) == ESP_OK && forget) s_forget_bond = true;
        nvs_close(nvs);
    }
    ble_npl_event_init(&s_control_event, control_event, NULL);
    ble_npl_callout_init(&s_poll_timer, nimble_port_get_dflt_eventq(), poll_event, NULL);
    s_stack_started = true;
    nimble_port_freertos_init(host_task);
    return ESP_OK;
}

static void disconnect_now(void)
{
    s_want_connection = false;
    if (!s_stack_started) return;
    ble_gap_disc_cancel();
    ble_gap_conn_cancel();
    if (s_connection != BLE_HS_CONN_HANDLE_NONE) ble_gap_terminate(s_connection, BLE_ERR_REM_USER_CONN_TERM);
    portENTER_CRITICAL(&s_lock);
    s_status.scanning = s_status.connecting = s_status.connected = false; bump_locked();
    portEXIT_CRITICAL(&s_lock);
    clear_events();
}

static void stop_stack_task(void *arg)
{
    (void)arg;
    // 等提交者和启动任务离开NimBLE队列后再收束，不能从主机回调等待自己停止。
    // Join submitters/bootstrap before freeing the event queue; never stop synchronously on the host thread.
    for (;;) {
        portENTER_CRITICAL(&s_lock);
        bool pending = s_submitters || s_worker_started || s_worker_starting;
        portEXIT_CRITICAL(&s_lock);
        if (!pending) break;
        vTaskDelay(1);
    }
    // 在途提交者可能在最后一次host控制事件之后入队；这些已拒绝的命令不能留到重启BLE时执行。
    // A joined submitter may have queued after the last host event; rejected commands must not survive a restart.
    command_t ignored;
    while (xQueueReceive(s_commands, &ignored, 0) == pdTRUE) { }
    int rc = nimble_port_stop();
    if (!rc) {
        nimble_port_freertos_deinit();
        ble_npl_callout_deinit(&s_poll_timer);
        ble_npl_event_deinit(&s_control_event);
        rc = nimble_port_deinit();
    }
    if (rc) {
        // 失败时保持禁止浅睡及重新启用；重启才能恢复，不能伪报硬件已经释放。
        // Fail closed until reboot; a failed stop/deinit must never claim the radio is released.
        set_error(rc);
    } else {
        s_connection = BLE_HS_CONN_HANDLE_NONE; s_discovering = false;
        memset(&s_hid, 0, sizeof(s_hid)); s_boot_held = 0; clear_events();
        portENTER_CRITICAL(&s_lock);
        s_status.ready = false; s_status.battery_percent = 255; s_status.device_count = 0;
        s_status.error = 0; s_stack_started = false; s_stopping = false; bump_locked();
        portEXIT_CRITICAL(&s_lock);
    }
    vTaskDelete(NULL);
}

static void stop_stack(void)
{
    disconnect_now();
    if (!s_stack_started || s_stopping) return;
    s_stopping = true;
    ble_npl_callout_stop(&s_poll_timer);
    if (xTaskCreate(stop_stack_task, "ble_pager_stop", 4096, NULL, 3, NULL) != pdPASS) {
        s_stopping = false; set_error(ESP_ERR_NO_MEM);
    }
}

static void connect_peer(const read_pico_ble_pager_device_t *peer)
{
    if (!s_status.enabled || !s_status.ready || s_connection != BLE_HS_CONN_HANDLE_NONE || s_status.connecting) { set_error(ESP_ERR_INVALID_STATE); return; }
    ble_gap_disc_cancel();
    s_peer = *peer;
    s_want_connection = true;
    ++s_connect_generation;
    s_discovering = false;
    ble_addr_t addr = {.type = peer->address_type}; memcpy(addr.val, peer->address, 6);
    portENTER_CRITICAL(&s_lock);
    s_status.scanning = false; s_status.connecting = true; s_status.error = 0;
    memcpy(s_status.name, peer->name, sizeof(s_status.name)); bump_locked();
    portEXIT_CRITICAL(&s_lock);
    s_setup_deadline = esp_timer_get_time() + 25000000;
    int rc = ble_gap_connect(s_own_address_type, &addr, 10000, NULL, gap_event, (void *)s_connect_generation);
    if (rc) connection_failed(rc);
}

static void handle_command(const command_t *command)
{
    command_t cmd = *command;
    if (cmd.kind == CMD_RESERVE) { s_auto_reconnect = false; stop_stack(); return; }
    if (s_reserved) return;
            if (cmd.kind == CMD_ENABLE) {
                portENTER_CRITICAL(&s_lock); s_status.enabled = cmd.value; s_status.error = 0; bump_locked(); portEXIT_CRITICAL(&s_lock);
                s_auto_reconnect = cmd.value; s_reconnect_attempts = 0;
                nvs_handle_t nvs;
                if (nvs_open("pico_ble", NVS_READWRITE, &nvs) == ESP_OK) { nvs_set_u8(nvs, "enabled", cmd.value); nvs_commit(nvs); nvs_close(nvs); }
                if (cmd.value) { esp_err_t rc = start_stack(); if (rc) set_error(rc); }
                else stop_stack();
            } else if (cmd.kind == CMD_REVERSE) {
                portENTER_CRITICAL(&s_lock); s_status.reversed = cmd.value; bump_locked(); portEXIT_CRITICAL(&s_lock);
                clear_events();
                nvs_handle_t nvs;
                if (nvs_open("pico_ble", NVS_READWRITE, &nvs) == ESP_OK) { nvs_set_u8(nvs, "reverse", cmd.value); nvs_commit(nvs); nvs_close(nvs); }
            } else if (cmd.kind == CMD_DISCONNECT || cmd.kind == CMD_FORGET) {
                s_auto_reconnect = false;
                disconnect_now();
                if (cmd.kind == CMD_FORGET) {
                    int clear_rc = s_stack_started && s_status.ready ? ble_store_clear() : BLE_HS_ENOTSYNCED;
                    s_forget_bond = clear_rc != 0;
                    if (clear_rc && clear_rc != BLE_HS_ENOTSYNCED) set_error(clear_rc);
                    memset(&s_remembered, 0, sizeof(s_remembered));
                    nvs_handle_t nvs;
                    if (nvs_open("pico_ble", NVS_READWRITE, &nvs) == ESP_OK) {
                        nvs_erase_key(nvs, "peer");
                        if (s_forget_bond) nvs_set_u8(nvs, "forget_bond", 1);
                        else nvs_erase_key(nvs, "forget_bond");
                        nvs_commit(nvs); nvs_close(nvs);
                    }
                    portENTER_CRITICAL(&s_lock); s_status.remembered = false; s_status.name[0] = '\0'; bump_locked(); portEXIT_CRITICAL(&s_lock);
                }
            } else if (cmd.kind == CMD_CONNECT) {
                s_auto_reconnect = true; s_reconnect_attempts = 0;
                connect_peer(&cmd.peer);
            } else if (cmd.kind == CMD_SCAN) {
                s_auto_reconnect = false;
                if (!s_status.enabled || !s_status.ready || s_status.connecting || s_status.connected) { set_error(ESP_ERR_INVALID_STATE); return; }
                ble_gap_disc_cancel();
                struct ble_gap_disc_params params = {.itvl = 0x80, .window = 0x30, .filter_duplicates = 0, .passive = 0};
                portENTER_CRITICAL(&s_lock);
                memset(s_devices, 0, sizeof(s_devices)); s_status.device_count = 0; s_status.error = 0; s_status.scanning = true; bump_locked();
                portEXIT_CRITICAL(&s_lock);
                int rc = ble_gap_disc(s_own_address_type, 8000, &params, gap_event, NULL);
                if (rc) { portENTER_CRITICAL(&s_lock); s_status.scanning = false; portEXIT_CRITICAL(&s_lock); set_error(rc); }
            }
}

static void poll_event(struct ble_npl_event *event)
{
        (void)event;
        if (s_stopping || s_reserved || !s_status.enabled) return;
        int64_t now = esp_timer_get_time();
        if (s_status.connecting && now > s_setup_deadline) { ble_gap_conn_cancel(); connection_failed(ESP_ERR_TIMEOUT); }
        if (s_auto_reconnect && s_status.enabled && s_status.ready && s_status.remembered && !s_status.connecting && !s_status.connected &&
            !s_status.scanning && s_connection == BLE_HS_CONN_HANDLE_NONE && now >= s_retry_after && s_reconnect_attempts < 3) {
            ++s_reconnect_attempts; s_retry_after = now + 15000000;
            connect_peer(&s_remembered);
        }
        if (s_status.enabled) ble_npl_callout_reset(&s_poll_timer, ble_npl_time_ms_to_ticks32(500));
}

static void control_event(struct ble_npl_event *event)
{
    (void)event;
    command_t cmd;
    while (xQueueReceive(s_commands, &cmd, 0) == pdTRUE) if (!s_stopping) handle_command(&cmd);
    if (!s_stopping) {
        if (s_status.enabled) ble_npl_callout_reset(&s_poll_timer, ble_npl_time_ms_to_ticks32(500));
        else ble_npl_callout_stop(&s_poll_timer);
    }
}

static void worker_task(void *arg)
{
    (void)arg;
    command_t cmd;
    // 首次初始化后，全部 BLE 状态及 GATT 操作串行放到 NimBLE 主机线程。
    // After lazy startup, serialize all BLE state and GATT operations on the NimBLE host thread.
    for (;;) {
        if (xQueueReceive(s_commands, &cmd, s_status.enabled && !s_reserved ? portMAX_DELAY : pdMS_TO_TICKS(20)) != pdTRUE) {
            portENTER_CRITICAL(&s_lock);
            bool idle = (!s_status.enabled || s_reserved) && !s_submitters;
            if (idle) s_worker_started = false;
            portEXIT_CRITICAL(&s_lock);
            if (idle) vTaskDelete(NULL);
            continue;
        }
        handle_command(&cmd);
        if (s_stack_started) {
            if (!s_stopping) {
                ble_npl_eventq_put(nimble_port_get_dflt_eventq(), &s_control_event);
                ble_npl_callout_reset(&s_poll_timer, ble_npl_time_ms_to_ticks32(500));
            }
            portENTER_CRITICAL(&s_lock); s_worker_started = false; portEXIT_CRITICAL(&s_lock);
            vTaskDelete(NULL);
        }
    }
}

static esp_err_t ensure_worker(void)
{
    portENTER_CRITICAL(&s_lock);
    if (s_stopping) { portEXIT_CRITICAL(&s_lock); return ESP_ERR_INVALID_STATE; }
    if (s_worker_starting) { portEXIT_CRITICAL(&s_lock); return ESP_ERR_INVALID_STATE; }
    if (s_stack_started || s_worker_started) { portEXIT_CRITICAL(&s_lock); return ESP_OK; }
    s_worker_starting = true; s_worker_started = true;
    portEXIT_CRITICAL(&s_lock);
    // 默认关闭时不保留4 KiB启动任务；申请失败前不能接收命令，避免迟到启用。
    // Keep no 4 KiB bootstrap task while initially disabled; reject before queuing on allocation failure.
    BaseType_t result = xTaskCreate(worker_task, "ble_pager", 4096, NULL, 3, NULL);
    portENTER_CRITICAL(&s_lock);
    s_worker_starting = false;
    if (result != pdPASS) s_worker_started = false;
    portEXIT_CRITICAL(&s_lock);
    return result == pdPASS ? ESP_OK : ESP_ERR_NO_MEM;
}

esp_err_t read_pico_ble_pager_init(void)
{
    if (s_initialized) return ESP_OK;
    s_commands = xQueueCreate(PAGER_COMMAND_COUNT, sizeof(command_t));
    s_events = xQueueCreate(PAGER_EVENT_COUNT, sizeof(page_event_t));
    if (!s_commands || !s_events) goto fail;
    uint8_t saved_enabled = 0;
    nvs_handle_t nvs;
    if (nvs_open("pico_ble", NVS_READONLY, &nvs) == ESP_OK) {
        size_t n = sizeof(s_remembered);
        if (nvs_get_blob(nvs, "peer", &s_remembered, &n) == ESP_OK && n == sizeof(s_remembered)) {
            s_remembered.name[sizeof(s_remembered.name) - 1] = '\0';
            s_status.remembered = true; memcpy(s_status.name, s_remembered.name, sizeof(s_status.name));
        }
        uint8_t reversed = 0; nvs_get_u8(nvs, "reverse", &reversed); s_status.reversed = reversed != 0;
        nvs_get_u8(nvs, "enabled", &saved_enabled);
        nvs_close(nvs);
    }
    s_initialized = true;
    if (saved_enabled) {
        command_t restore = {.kind = CMD_ENABLE, .value = true};
        esp_err_t rc = send_command(restore);
        if (rc != ESP_OK) { set_error(rc); return rc; }
    }
    return ESP_OK;
fail:
    if (s_commands) vQueueDelete(s_commands);
    if (s_events) vQueueDelete(s_events);
    s_commands = s_events = NULL;
    return ESP_ERR_NO_MEM;
}

static esp_err_t send_command(command_t cmd)
{
    if (s_reserved && cmd.kind != CMD_RESERVE) return ESP_ERR_INVALID_STATE;
    esp_err_t rc = read_pico_ble_pager_init();
    if (rc) return rc;
    portENTER_CRITICAL(&s_lock);
    if (s_stopping) { portEXIT_CRITICAL(&s_lock); return ESP_ERR_INVALID_STATE; }
    ++s_submitters;
    portEXIT_CRITICAL(&s_lock);
    rc = ensure_worker();
    if (!rc && xQueueSend(s_commands, &cmd, 0) != pdTRUE) rc = ESP_ERR_TIMEOUT;
    if (!rc && s_stack_started && !s_stopping) ble_npl_eventq_put(nimble_port_get_dflt_eventq(), &s_control_event);
    portENTER_CRITICAL(&s_lock);
    if (s_stopping) rc = ESP_ERR_INVALID_STATE;
    --s_submitters;
    portEXIT_CRITICAL(&s_lock);
    return rc;
}

esp_err_t read_pico_ble_pager_set_enabled(bool enabled) { return send_command((command_t){.kind = CMD_ENABLE, .value = enabled}); }
esp_err_t read_pico_ble_pager_scan(void) { return send_command((command_t){.kind = CMD_SCAN}); }
esp_err_t read_pico_ble_pager_connect(size_t index)
{
    command_t cmd = {.kind = CMD_CONNECT};
    portENTER_CRITICAL(&s_lock);
    if (index >= s_status.device_count) { portEXIT_CRITICAL(&s_lock); return ESP_ERR_INVALID_ARG; }
    cmd.peer = s_devices[index];
    portEXIT_CRITICAL(&s_lock);
    return send_command(cmd);
}
esp_err_t read_pico_ble_pager_disconnect(void) { return send_command((command_t){.kind = CMD_DISCONNECT}); }
esp_err_t read_pico_ble_pager_forget(void) { return send_command((command_t){.kind = CMD_FORGET}); }
esp_err_t read_pico_ble_pager_set_reversed(bool reversed) { return send_command((command_t){.kind = CMD_REVERSE, .value = reversed}); }
void read_pico_ble_pager_get_status(read_pico_ble_pager_status_t *out)
{
    if (!out) return;
    portENTER_CRITICAL(&s_lock); *out = s_status; portEXIT_CRITICAL(&s_lock);
}
bool read_pico_ble_pager_light_sleep_safe(void)
{
    // 完成host stop/deinit后才允许浅睡；关闭开关、排队命令或关闭中的状态都不够。
    // Only completed host/controller teardown permits sleep; the switch alone or an in-flight command does not.
    portENTER_CRITICAL(&s_lock);
    bool safe = !s_reserved && !s_status.enabled && !s_stack_started && !s_worker_started && !s_worker_starting && !s_stopping && !s_submitters;
    portEXIT_CRITICAL(&s_lock);
    return safe;
}
// 双机临时独占同一个控制器，保留用户 HID 开关和已配对设备。
// A temporary dual-reader lease preserves the HID preference and bonds.
esp_err_t read_pico_ble_pager_reserve(bool reserve)
{
    if (reserve) {
        esp_err_t rc = read_pico_ble_pager_init();
        if (rc) return rc;
        if (!s_reserved) {
            // 启动时恢复开关的命令可能还在队列里，保留已保存的偏好。
            // The startup restore may still be queued; preserve the persisted preference.
            uint8_t enabled = s_status.enabled;
            nvs_handle_t nvs;
            if (nvs_open("pico_ble", NVS_READONLY, &nvs) == ESP_OK) {
                nvs_get_u8(nvs, "enabled", &enabled); nvs_close(nvs);
            }
            s_resume_enabled = enabled != 0;
            s_reserved = true;
        }
        if (read_pico_ble_pager_reservation_ready()) return ESP_OK;
        if (s_stopping) return ESP_OK;
        return send_command((command_t){.kind = CMD_RESERVE});
    }
    if (!s_reserved) return ESP_OK;
    // 调用方须先完全停止自己的 NimBLE 栈。/ The caller must first fully stop its own NimBLE host.
    if (!read_pico_ble_pager_reservation_ready()) return ESP_ERR_INVALID_STATE;
    s_reserved = false;
    return s_resume_enabled ? send_command((command_t){.kind = CMD_ENABLE, .value = true}) : ESP_OK;
}
bool read_pico_ble_pager_reservation_ready(void)
{
    portENTER_CRITICAL(&s_lock);
    bool ready = s_reserved && !s_stack_started && !s_stopping && !s_worker_started &&
                 !s_worker_starting && !s_submitters;
    portEXIT_CRITICAL(&s_lock);
    return ready;
}
size_t read_pico_ble_pager_get_devices(read_pico_ble_pager_device_t *out, size_t capacity)
{
    if (!out || !capacity) return 0;
    portENTER_CRITICAL(&s_lock);
    size_t n = capacity < s_status.device_count ? capacity : s_status.device_count;
    memcpy(out, s_devices, n * sizeof(*out));
    portEXIT_CRITICAL(&s_lock);
    return n;
}
int read_pico_ble_pager_poll_event(void)
{
    page_event_t event;
    while (s_events && xQueueReceive(s_events, &event, 0) == pdTRUE) {
        int64_t now = esp_timer_get_time();
        if (now >= event.timestamp && now - event.timestamp <= 2000000) return event.direction;
    }
    return 0;
}
#else
esp_err_t read_pico_ble_pager_reserve(bool reserve) { return reserve ? ESP_ERR_NOT_SUPPORTED : ESP_OK; }
bool read_pico_ble_pager_reservation_ready(void) { return false; }
esp_err_t read_pico_ble_pager_init(void) { return ESP_OK; }
esp_err_t read_pico_ble_pager_set_enabled(bool enabled) { (void)enabled; return ESP_ERR_NOT_SUPPORTED; }
esp_err_t read_pico_ble_pager_scan(void) { return ESP_ERR_NOT_SUPPORTED; }
esp_err_t read_pico_ble_pager_connect(size_t i) { (void)i; return ESP_ERR_NOT_SUPPORTED; }
esp_err_t read_pico_ble_pager_disconnect(void) { return ESP_OK; }
esp_err_t read_pico_ble_pager_forget(void) { return ESP_OK; }
esp_err_t read_pico_ble_pager_set_reversed(bool reversed) { (void)reversed; return ESP_ERR_NOT_SUPPORTED; }
void read_pico_ble_pager_get_status(read_pico_ble_pager_status_t *out) { if (out) { memset(out, 0, sizeof(*out)); out->battery_percent = 255; } }
size_t read_pico_ble_pager_get_devices(read_pico_ble_pager_device_t *out, size_t n) { (void)out; (void)n; return 0; }
int read_pico_ble_pager_poll_event(void) { return 0; }
bool read_pico_ble_pager_light_sleep_safe(void) { return true; }
#endif

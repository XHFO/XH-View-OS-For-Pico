/* SPDX-License-Identifier: Apache-2.0
 * 双机配对页、TF生命周期、阅读联动。/ Pairing page, TF lifecycle and linked reading.
 * 冻结：共享分页使用竖屏及主机完整字体；按用户要求支持同步字号，主机按原正文锚点重排后才放行副机。
 * Frozen: Shared portrait pagination uses the primary's full font; requested shared sizing reflows the primary's text anchor before releasing the secondary.
 * 同步不导出TF，不绕过编辑或电源保护。/ Synchronization never exports TF or bypasses edit/power guards.
 */
#include "pico_dual.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#ifdef ESP_PLATFORM
#include "lwip/sockets.h"
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#endif
#include "../apps/app_book.h"
#include "book_store.h"
#include "dual_link.h"
#include "read_pico_dual_ble.h"
#include "esp_random.h"
#include "pico_connectivity.h"
#include "pico_power.h"
#include "pico_ink.h"
#include "read_pico_connectivity.h"
#ifdef ESP_PLATFORM
#include "esp_wifi.h"
#include "esp_netif.h"
#endif
#include "read_pico_sd.h"
#include "ttf_font.h"
#ifdef PICO_NATIVE_PREVIEW
#include "native_preview_book_vfs.h"
#endif
typedef struct {
    uint32_t magic;
    uint8_t role;
    char host[48], code[7];
} dual_settings_t;
static dual_settings_t settings;
static dual_link_t* dual_session;
static dl_status_t status;
static bool loaded;
static dl_orientation_t orientation = { .inverted = true };
static uint8_t stored_orientation = 0xff;
static uint8_t rotation_locked;
static uint8_t guard_enabled = 1;
typedef struct { uint32_t magic; int16_t x, y, z; } dual_base_t;
static dual_base_t base;
static bool calibrated;
static uint32_t base_applied;
static bool reading_active = true;
static bool reading_known;
static bool local_away;
static bool contact_latched;
static uint32_t annotation_applied;
static int64_t contact_same_since;
static bool manual_rotation;
static int64_t orientation_store_at;
static uint8_t orientation_bits(void) {
    return orientation.inverted | (orientation.primary_flip << 1) | (orientation.secondary_flip << 2);
}
static void orientation_save(void) {
    uint8_t bits = orientation_bits();
    if (bits != stored_orientation && ps_now() >= orientation_store_at) {
        if (ps_store("dualorient1", &bits, sizeof(bits))) stored_orientation = bits;
        else { orientation_store_at = ps_now() + 1000; ps_toast("屏幕方向保存失败，请检查 TF 卡"); }
    }
}
static uint32_t applied, presented, stored, content_revision;
static int64_t last_tick, last_ip, last_work;
static int pair_sock = -1;
static bool pair_scanning, pair_found, pair_failed;
static int anim_gray = 16;
static char pair_ip[48], pair_code[7];
static int64_t pair_started, pair_last_tx;
static bool hotspot_active;
#ifdef ESP_PLATFORM
static esp_netif_t* hotspot_netif;
#endif
static void hotspot_stop(void);
static void pair_stop(void);
static void pair_scan_start(void);
static void pair_scan_update(void);
static unsigned library_revision;
static char primary_ip[48];
static char draft_host[48];
static char draft_local_ip[48], draft_input[16];
static bool draft_short;
static uint32_t ble_generation;
#ifdef PICO_NATIVE_PREVIEW
static bool native_wifi = true;
void ps_dual_native_wifi(bool online) { native_wifi = online; }
#endif
static void load(void) {
    if (loaded) return;
    loaded = true;
    if (!ps_load("dualconfig1", &settings, sizeof(settings)) || settings.magic != 0x44554c31 ||
        settings.role > 2)
        memset(&settings, 0, sizeof(settings));
    settings.host[47] = 0;
    settings.code[6] = 0;
    uint8_t bits;
    if (ps_load("dualorient1", &bits, sizeof(bits)) && bits < 8) {
        orientation = (dl_orientation_t){ !!(bits & 1), !!(bits & 2), !!(bits & 4) };
        stored_orientation = bits;
    }
    uint8_t locked = 0;
    if (ps_load("duallock1", &locked, sizeof(locked))) rotation_locked = locked ? 1 : 0;
    uint8_t guard = 1;
    if (ps_load("duaguard1", &guard, sizeof(guard))) guard_enabled = guard ? 1 : 0;
    dual_base_t stored;
    if (ps_load("dualbase1", &stored, sizeof(stored)) && stored.magic == 0x31425344u) {
        base = stored; calibrated = true;
    }
}
bool ps_dual_enabled(void) {
    load();
    return settings.role != 0;
}
int ps_dual_role(void) {
    load();
    return settings.role;
}
bool ps_dual_holds_awake(void) {
    return ps_dual_enabled() && dual_session &&
        (status.connected || status.data_connected || status.scanning || status.busy);
}
static bool hotspot_start(void) {
#ifdef ESP_PLATFORM
    if (hotspot_active) return true;
    if (read_pico_connectivity_quiesce(2500) != ESP_OK) return false;
    esp_wifi_stop();
    hotspot_netif = esp_netif_create_default_wifi_ap();
    if (!hotspot_netif) { (void)read_pico_connectivity_resume(); return false; }
    wifi_config_t ap = {0};
    snprintf((char*)ap.ap.ssid, sizeof(ap.ap.ssid), "PicoDual");
    snprintf((char*)ap.ap.password, sizeof(ap.ap.password), "readpico");
    ap.ap.ssid_len = (uint8_t)strlen((char*)ap.ap.ssid);
    ap.ap.channel = 1;
    ap.ap.max_connection = 2;
    ap.ap.authmode = WIFI_AUTH_WPA2_PSK;
    esp_err_t err = esp_wifi_set_mode(WIFI_MODE_AP);
    if (!err) err = esp_wifi_set_config(WIFI_IF_AP, &ap);
    if (!err) err = esp_wifi_start();
    if (err != ESP_OK) {
        esp_netif_destroy(hotspot_netif); hotspot_netif = NULL;
        (void)read_pico_connectivity_resume();
        return false;
    }
    hotspot_active = true;
    return true;
#else
    return false;
#endif
}
static void hotspot_stop(void) {
#ifdef ESP_PLATFORM
    if (!hotspot_active) return;
    esp_wifi_stop();
    if (hotspot_netif) { esp_netif_destroy(hotspot_netif); hotspot_netif = NULL; }
    hotspot_active = false;
    (void)read_pico_connectivity_resume();
#else
    hotspot_active = false;
#endif
}
void ps_dual_suspend(void) {
    hotspot_stop();
    pico_dual_ble_stop();
    ble_generation = 0;
    if (dual_session) {
        dl_destroy(dual_session);
        dual_session = NULL;
    }
    memset(&status, 0, sizeof(status));
    applied = presented = stored = content_revision = 0;
}
bool ps_dual_configure(int role, const char* host, const char* code) {
    load();
    if (role < 0 || role > 2 || ps_power_pending() || ps_storage_busy() || ps_usb_held())
        return false;
    dual_settings_t next = { .magic = 0x44554c31, .role = (uint8_t)role };
    if (role) {
        if (!code || strlen(code) != 6) return false;
        for (int i = 0; i < 6; i++)
            if (code[i] < '0' || code[i] > '9') return false;
        if (role == 2 && (!host || !*host || strlen(host) >= sizeof(next.host))) return false;
        snprintf(next.host, sizeof(next.host), "%s", host ? host : "");
        snprintf(next.code, sizeof(next.code), "%s", code);
    }
    if (role == 2 && host && !strncmp(host, "192.168.4.1", 11))
        (void)read_pico_connectivity_connect("PicoDual", "readpico");
    if (!ps_store("dualconfig1", &next, sizeof(next))) {
        ps_toast("设置保存失败，请检查 TF 卡");
        return false;
    }
    ps_dual_suspend();
    settings = next;
    ps_dual_rotation_changed();
    ps_dirty();
    return true;
}
static bool start(void) {
    read_pico_sd_info_t sd = { 0 };
    read_pico_sd_get_info(&sd);
    if (!sd.mounted || !sd.present) return false;
    dl_config_t cfg = { .role = settings.role, .port = DL_PORT };
    snprintf(cfg.host, sizeof(cfg.host), "%s", settings.host);
    snprintf(cfg.code, sizeof(cfg.code), "%s", settings.code);
    book_store_root_t roots[2];
    int n = 0;
    if (book_store_roots(roots, &n) != ESP_OK) return false;
    for (int i = 0; i < n && cfg.root_count < 2; i++)
        if (!roots[i].is_flash) {
            snprintf(cfg.roots[cfg.root_count++], DL_PATH, "%s", roots[i].path);
            if (!cfg.destination[0]) snprintf(cfg.destination, DL_PATH, "%s", roots[i].path);
        }
    if (!cfg.root_count) return false;
    snprintf(cfg.roots[cfg.root_count++], DL_PATH, "/sdcard");
    if (settings.role == 1) {
        const char* font = ttf_font_path();
        if (!font || ttf_font_path_is_builtin(font)) font = TTF_FONT_DEFAULT_SD;
        struct stat st;
        if (stat(font, &st) || !S_ISREG(st.st_mode)) {
            status.error = "primary font unavailable";
            return false;
        }
        snprintf(cfg.font, sizeof(cfg.font), "%s", font);
    }
    dual_session = dl_create(&cfg);
    if (!dual_session) {
        status.error = "allocation or configuration failed";
        return false;
    }
    dl_orientation_restore(dual_session, &orientation);
    if (!guard_enabled) (void)dl_guard(dual_session, false);
    dl_reading_t saved = { 0 };
    if (settings.role == 1) {
        // 旧记录是新结构的76字节前缀；保留原书和页码，并使用原固定48字号。
        // The old record is the 76-byte prefix; retain its book/spread with the former fixed size of 48.
        bool restored = ps_load("dualreading2", &saved, sizeof(saved));
        if (!restored) {
            memset(&saved, 0, sizeof(saved));
            restored = ps_load("dualreading1", &saved, offsetof(dl_reading_t, font_px));
            saved.font_px = DL_FONT_DEFAULT;
        }
        if (restored) dl_restore(dual_session, &saved);
    }
    library_revision = book_store_revision();
    return true;
}
// 主机基准与两端翻转单独存储，不改系统方向偏好。/ Separate the primary base and role flips from system preferences.
int ps_dual_base_rotation(void) {
    load();
    return orientation.inverted ? EPD_ROT_INVERTED_PORTRAIT : EPD_ROT_PORTRAIT;
}
int ps_dual_rotation(void) {
    load();
    bool flip = settings.role == 1 ? orientation.primary_flip : orientation.secondary_flip;
    return (orientation.inverted != flip) ? EPD_ROT_INVERTED_PORTRAIT : EPD_ROT_PORTRAIT;
}
static void orientation_update(void) {
    dl_status_t current;
    dl_status(dual_session, &current);
    orientation = current.orientation;
    orientation_save();
    ps_dirty();
}
bool ps_dual_flip(int role, bool enabled) {
    load();
    if (!settings.role || ps_power_pending() || ps_storage_busy() || ps_usb_held() || ps_content_held()) return false;
    if (!dual_session && !start()) return false;
    if (!dl_flip(dual_session, role, enabled)) return false;
    orientation_update();
    manual_rotation = true;
    return true;
}
bool ps_dual_rotate(void) {
    load();
    return ps_dual_flip(settings.role, !(settings.role == 1 ? orientation.primary_flip : orientation.secondary_flip));
}
bool ps_dual_rotation_locked(void) {
    load();
    return rotation_locked != 0;
}
bool ps_dual_take_manual_rotation(void) {
    bool pending = manual_rotation;
    manual_rotation = false;
    return pending;
}
bool ps_dual_lock_set(bool locked) {
    load();
    if (!settings.role) return false;
    uint8_t next = locked ? 1 : 0;
    if (next == rotation_locked) return true;
    if (!ps_store("duallock1", &next, sizeof(next))) {
        ps_toast("方向锁定保存失败");
        return false;
    }
    rotation_locked = next;
    ps_dirty();
    return true;
}
static int contact_face(void) {
    if (!calibrated) return 0;
    int16_t x, y, z;
    if (!ps_contact_cached(&x, &y, &z)) return 0;
    int64_t dot = (int64_t)x * base.x + (int64_t)y * base.y + (int64_t)z * base.z;
    int64_t nb = (int64_t)base.x * base.x + (int64_t)base.y * base.y + (int64_t)base.z * base.z;
    int64_t nc = (int64_t)x * x + (int64_t)y * y + (int64_t)z * z;
    // 与校准基准夹角小于约 60° 视为展开面，超过 120° 视为翻到另一面。
    // Within ~60 degrees of the desk baseline is the open face; beyond ~120 degrees is the opposite face.
    int64_t limit = nb * nc / 4;
    if (dot > 0 && dot * dot > limit) return 1;
    if (dot < 0 && dot * dot > limit) return -1;
    return 0;
}
int ps_dual_anim_gray(void) { return anim_gray; }
void ps_dual_toggle_anim_gray(void) { anim_gray = anim_gray == 16 ? 8 : 16; }
bool ps_dual_calibrated(void) {
    load();
    return calibrated;
}
void ps_dual_sync_annotation(uint8_t type, uint32_t chapter, uint32_t offset, uint32_t end, const char* text) {
    load();
    if (!settings.role) return;
    if (!dual_session && !start()) return;
    (void)dl_annotation(dual_session, type, chapter, offset, end, text ? text : "");
}
bool ps_dual_guard_enabled(void) {
    load();
    return guard_enabled != 0;
}
int ps_dual_peer_face(void) {
    return status.peer_face;
}
bool ps_dual_left_ready(void) {
    return status.left_ready;
}
bool ps_dual_peer_reading(void) {
    return status.peer_reading;
}
void ps_dual_set_local_away(bool away) {
    local_away = away;
}
bool ps_dual_contact_likely(void) {
    if (!calibrated) {
        contact_latched = false;
        contact_same_since = 0;
        return false;
    }
    int local = contact_face();
    int peer = ps_dual_peer_face();
    if (local && peer) {
        if (local != peer) {
            // 一旦确认相对，短暂的运动/姿态不明不会解除屏蔽。
            // Once opposite faces are confirmed, brief motion or unknown pose cannot release the guard.
            contact_latched = true;
            contact_same_since = 0;
        } else {
            // 只有两端稳定同向一段时间才认为已展开。
            // Only stable same-direction faces for a while count as unfolded.
            int64_t now = ps_now();
            if (!contact_same_since) contact_same_since = now;
            if (now - contact_same_since >= 800) contact_latched = false;
        }
    } else {
        contact_same_since = 0;
    }
    return contact_latched;
}
bool ps_dual_calibrate(void) {
    load();
    if (!settings.role) return false;
    if (!dual_session && !start()) return false;
    if (settings.role == 2) {
        if (!dl_calibration_request(dual_session)) return false;
        ps_toast("已请求主机校准，请保持两台展开平放桌面");
        return true;
    }
    if (!status.connected) {
        ps_toast("请先连接两台设备再校准");
        return false;
    }
    int16_t x, y, z;
    if (!ps_contact_vector(&x, &y, &z) || abs(z) < 500 ||
        (int32_t)abs(x) + abs(y) + abs(z) < 700) {
        ps_toast("请将两台展开平放桌面后再校准");
        return false;
    }
    base = (dual_base_t){.magic = 0x31425344u, .x = x, .y = y, .z = z};
    calibrated = true;
    contact_latched = false;
    contact_same_since = 0;
    base_applied = status.base_seq;
    if (!ps_store("dualbase1", &base, sizeof(base))) {
        calibrated = false;
        ps_toast("校准保存失败");
        return false;
    }
    (void)dl_baseline(dual_session, x, y, z);
    ps_dirty();
    ps_toast("校准完成：展开正常滑动，折叠时屏蔽触摸");
    return true;
}
bool ps_dual_guard_set(bool enabled) {
    load();
    if (!settings.role) return false;
    if (!dual_session && !start()) return false;
    if (!dl_guard(dual_session, enabled)) return false;
    uint8_t next = enabled ? 1 : 0;
    if (!ps_store("duaguard1", &next, sizeof(next)))
        ps_toast("防误触已同步，但本地保存失败");
    guard_enabled = next;
    ps_dirty();
    return true;
}
bool ps_dual_orient(int rotation) {
    if (settings.role != 1 || !dual_session) return false;
    if (!dl_orient(dual_session, rotation == EPD_ROT_INVERTED_PORTRAIT)) return false;
    orientation_update();
    return true;
}
static bool open_request(const char* path, uint32_t page) {
    if (!dual_session || !(page ? dl_open_at(dual_session, path, page) : dl_open_path(dual_session, path))) {
        ps_toast("请等待双机连接和书库同步完成");
        return false;
    }
    dl_status_t current;
    dl_status(dual_session, &current);
    if (!current.synced)
        ps_toast(current.scanning ? "正在校验书库，完成后自动打开"
                 : current.data_connected ? "正在同步图书与字库，完成后自动打开"
                                          : "等待另一台连接，连接后自动打开");
    return true;
}
bool ps_dual_open(const char* path) {
    return !ps_dual_enabled() || open_request(path, 0);
}
bool ps_dual_open_at(const char* path, uint32_t page) {
    return ps_dual_enabled() && page && open_request(path, page);
}
bool ps_dual_turn(int direction) {
    if (!ps_dual_enabled()) return false;
    bool accepted = dual_session && dl_turn(dual_session, direction);
    if (!accepted) ps_toast("另一台尚未就绪，请稍后再翻页");
    return accepted;
}
bool ps_dual_seek(uint32_t page) {
    return ps_dual_enabled() && dual_session && !ps_power_pending() && !ps_storage_busy() &&
        !ps_usb_busy() && !ps_usb_held() && !ps_content_held() && !ps_transfer_prepared() &&
        dl_seek(dual_session, page);
}
bool ps_dual_resize(uint8_t px) {
    return ps_dual_enabled() && dual_session && !ps_power_pending() && !ps_storage_busy() &&
        !ps_usb_busy() && !ps_usb_held() && !ps_content_held() && !ps_transfer_prepared() &&
        dl_resize(dual_session, px);
}
bool ps_dual_reflow_resolved(uint32_t revision, uint32_t spread) {
    return dual_session && dl_reflow_resolved(dual_session, revision, spread);
}
bool ps_dual_reflow_failed(uint32_t revision) {
    return dual_session && dl_reflow_failed(dual_session, revision);
}
bool ps_dual_resume(void) {
    dl_status_t current; dl_status(dual_session, &current);
    if (!current.connected || !current.synced || (settings.role == 2 && !current.left_ready) ||
        !dl_reading_path(dual_session) || !dl_font_path(dual_session)) return false;
    applied = presented = 0;
    return true;
}
void ps_dual_presented(uint32_t page) {
    if (!dual_session || !applied) return;
    dl_status_t current; dl_status(dual_session, &current);
    if (current.reading.reflow_pending || current.reading.revision != applied || page != current.reading.spread + (settings.role == 2 ? 2u : 1u)) return;
    if (settings.role == 1) (void)dl_presented(dual_session, applied);
    presented = applied;
}
void ps_dual_total(uint32_t total) {
    if (dual_session) dl_total(dual_session, total);
}
const char* ps_dual_font(void) {
    return dual_session ? dl_font_path(dual_session) : NULL;
}
const char* ps_dual_path(void) {
    return dual_session ? dl_reading_path(dual_session) : NULL;
}
static bool idle_reading_route(void) {
    switch (ps_route()) {
        case PS_SHELF: case PS_DUAL: case PS_DEVICE: case PS_READING_SETTINGS:
        case PS_INPUT_SETTINGS: case PS_SLEEP_SETTINGS: case PS_QUICK_SETTINGS:
        case PS_POWER: case PS_SOUND: case PS_DATETIME: return true;
        default: return false;
    }
}
bool ps_dual_service(app_ctx_t* ctx, const app_desc_t* app, app_redraw_t* redraw) {
    if (!ps_dual_enabled()) return false;
    bool safe = !ps_storage_busy() && !ps_usb_busy() && !ps_usb_held() && !ps_content_held() &&
                !ps_transfer_prepared() && !ps_power_pending();
    if (!safe) {
        ps_dual_suspend();
        return false;
    }
    if (ctx->now_ms - last_tick < 10) return false;
    last_tick = ctx->now_ms;
    read_pico_connectivity_status_t* net = NULL;
    // 日常联网快照只用于设置页；读书不复制大扫描数组。/ Full Wi-Fi snapshots are only needed by setup, not each reading tick.
    bool online = hotspot_active || read_pico_connectivity_online();
#ifdef PICO_NATIVE_PREVIEW
    online = native_wifi;
#endif
    if (ps_route() == PS_DUAL && ctx->now_ms - last_ip >= 1000) {
        last_ip = ctx->now_ms;
        if (hotspot_active) snprintf(primary_ip, sizeof(primary_ip), "192.168.4.1");
        else {
            net = malloc(sizeof(*net));
            if (net) {
                read_pico_connectivity_get_status(net);
                snprintf(primary_ip, sizeof(primary_ip), "%s", net->ip);
                free(net);
            }
        }
    }
    // 浮层只延后切换正文，不能停止通信心跳；触摸/笔迹仍让路。
    // Overlays defer opening the reader, not link servicing; active touch and ink still take priority.
    bool ui_idle = ps_background_io_allowed();
    if ((ctx->touch && ctx->touch->touched) || ps_reader_editor_ink_active() || ps_ink_pending())
        return false;
    if (!dual_session && !start()) return false;
    dl_status_t before = status;
    esp_err_t ble_result = pico_dual_ble_configure(settings.role, settings.code);
    pico_dual_ble_status_t ble;
    pico_dual_ble_status(&ble);
    if (ble_generation != ble.generation) {
        dl_control_connected(dual_session, false);
        ble_generation = ble.generation;
        applied = 0;
    }
    if (!ble.connected) applied = 0;
    dl_control_connected(dual_session, ble.connected);
    dl_tick(dual_session, ctx->now_ms, online);
    (void)dl_face(dual_session, contact_face());
    bool active = app == &app_book && app_book_dual_engaged();
    if (active && (!reading_known || !reading_active)) {
        reading_known = true;
        reading_active = true;
        local_away = false;
        (void)dl_reading_active(dual_session, true);
        ps_dirty();
    } else if (!active && reading_known && reading_active) {
        reading_active = false;
        local_away = true;
        app_book_dual_pause();
        (void)dl_reading_active(dual_session, false);
        ps_dirty();
    }
    last_work = ctx->now_ms;
    pair_scan_update();
    uint8_t packet[DL_CONTROL_MAX];
    for (unsigned i = 0; i < 4; ++i) {
        size_t n = pico_dual_ble_receive(packet, sizeof(packet));
        if (!n) break;
        (void)dl_control_receive(dual_session, packet, n);
    }
    if (pico_dual_ble_can_send()) {
        size_t n = dl_control_take(dual_session, packet, sizeof(packet));
        if (n) (void)pico_dual_ble_send(packet, n);
    }
    dl_status(dual_session, &status);
    if (settings.role == 1 && status.calibration_request) {
        dl_calibration_clear(dual_session);
        (void)ps_dual_calibrate();
    }
    if (settings.role == 2 && status.base_valid && status.base_seq != base_applied) {
        // 从机也采自己的重力向量，避免两台传感器/物理旋转差异共用同一基准。
        // The secondary samples its own gravity vector instead of assuming identical sensor mounting.
        int16_t x, y, z;
        if (ps_contact_vector(&x, &y, &z) && abs(z) >= 400)
            base = (dual_base_t){.magic = 0x31425344u, .x = x, .y = y, .z = z};
        else
            base = (dual_base_t){.magic = 0x31425344u, .x = status.base_x, .y = status.base_y, .z = status.base_z};
        calibrated = true;
        contact_latched = false;
        contact_same_since = 0;
        base_applied = status.base_seq;
        (void)ps_store("dualbase1", &base, sizeof(base));
        ps_dirty();
        ps_toast("开合校准已同步");
    }
    if (status.annotation_valid && status.annotation_seq != annotation_applied &&
        app_book_dual_annotation(status.annotation_type, status.annotation_chapter,
                                 status.annotation_start, status.annotation_end, status.annotation_text))
        annotation_applied = status.annotation_seq;
    if (memcmp(&orientation, &status.orientation, sizeof(orientation))) {
        orientation = status.orientation;
        ps_dirty();
    }
    if (guard_enabled != (status.guard ? 1 : 0)) {
        guard_enabled = status.guard ? 1 : 0;
        (void)ps_store("duaguard1", &guard_enabled, sizeof(guard_enabled));
        ps_dirty();
    }
    orientation_save();
    if (app != &app_book && ui_idle && !ctx->consumed) ps_dual_rotation_changed();
    if (!status.error && ble_result != ESP_OK) status.error = "Bluetooth startup unavailable";
    else if (!status.error && ble.error && !ble.connected)
        status.error = "Bluetooth pairing pending; check matching code";
    if (status.content_revision != content_revision) {
        content_revision = status.content_revision;
        book_store_notify_changed();
        library_revision = book_store_revision();
    }
    if (ps_route() == PS_DUAL &&
        (before.connected != status.connected || before.data_connected != status.data_connected || before.synced != status.synced ||
         before.scanning != status.scanning || before.books != status.books ||
         before.error != status.error || before.expected != status.expected ||
         (status.expected && (uint64_t)before.received * 20 / status.expected !=
                                 (uint64_t)status.received * 20 / status.expected)))
        ps_dirty();
    if (settings.role == 1 && !status.reading.reflow_pending && status.reading.revision != stored && dl_reading_path(dual_session)) {
        if (ps_store("dualreading2", &status.reading, sizeof(status.reading)))
            stored = status.reading.revision;
    }
    const char *path = dl_reading_path(dual_session), *font = dl_font_path(dual_session);
    if (ui_idle && status.connected && status.synced && !local_away &&
        path && font && status.reading.revision != applied &&
        (app == &app_book || (app->product_shell && idle_reading_route()))) {
        if (app_book_dual_apply(path, font, status.reading.spread + (settings.role == 2 ? 2 : 1),
                status.reading.font_px, status.reading.reflow_pending, status.reading.revision)) {
            applied = status.reading.revision;
            ps_power_activity(ctx->now_ms);
            if (app != &app_book) ps_legacy(&app_book);
        }
    }
    // 只响应书库变化；按时钟重扫会使大字库刚同步完就再次失去就绪状态。
    // Rescan on library changes only; timed rescans can invalidate a slow sync as soon as it completes.
    if (app->product_shell && (ps_route() == PS_DUAL || ps_route() == PS_SHELF) &&
        status.synced && !status.busy && library_revision != book_store_revision()) {
        library_revision = book_store_revision();
        dl_rescan(dual_session);
    }
    (void)redraw;
    return false;
}
void ps_dual_enter(void) {
    load();
}
void ps_dual_render(uint8_t* fb) {
    ps_view_t v = ps_view();
    ps_heading(fb, "双机阅读 · 实验版", "两台小纸 Pico，共同打开一本书");
    int x = 20, w = v.w - 40;
    char text[128];
    ps_button(fb, x, 104, (w - 16) / 3, 40, "关闭", settings.role == 0);
    ps_button(fb, x + (w - 16) / 3 + 8, 104, (w - 16) / 3, 40, "主机", settings.role == 1);
    ps_button(fb, x + 2 * ((w - 16) / 3 + 8), 104, (w - 16) / 3, 40, "从机", settings.role == 2);
    if (!settings.role) {
        if (pair_scanning || pair_found || pair_failed) {
            ps_text(fb, x, 170, 14, pair_found ? "发现主机" : "正在搜索主机…", 1);
            if (pair_found) {
                snprintf(text, sizeof(text), "主机地址  %s", pair_ip);
                ps_text(fb, x, 200, 13, text, 1);
                snprintf(text, sizeof(text), "配对码  %s", pair_code);
                ps_text(fb, x, 228, 20, text, 1);
                ps_text(fb, x, 258, 11, "请确认与主机屏幕显示的六位码相同", 4);
                ps_button(fb, x, 292, w, 40, "确认配对", true);
                ps_button(fb, x, 340, w, 36, "重新搜索", false);
                ps_button(fb, x, 384, w, 36, "取消", false);
            } else if (pair_failed) {
                ps_text(fb, x, 190, 14, "未找到主机", 1);
                ps_text(fb, x, 220, 12, "请确认主机已开启并选择主机", 4);
                ps_button(fb, x, 280, w, 40, "重新搜索", true);
                ps_button(fb, x, 328, w, 36, "取消", false);
            } else {
                ps_text(fb, x, 210, 12, "请让主机停留在双机阅读页", 4);
                ps_button(fb, x, 292, w, 40, "取消搜索", false);
            }
            return;
        }
        ps_text(fb, x, 170, 14, "先将两台设备连接到同一个 Wi-Fi", 1);
        ps_text(fb, x, 200, 12, "一台选择主机，另一台选择从机", 4);
        ps_button(fb, x, 250, w, 42, "自动搜索主机", true);
        ps_button(fb, x, 300, w, 38, "手动输入主机 IP 和配对码", false);
        ps_button(fb, x, 348, w, 36, "打开 Wi-Fi 设置", false);
        return;
    }
    for (int role = 1; role <= 2; ++role) {
        int center = x + role * ((w - 16) / 3 + 8) + (w - 16) / 6;
        bool on = role == 1 ? orientation.primary_flip : orientation.secondary_flip;
        ps_center(fb, center, 153, 10, "旋转屏幕 180°", 2);
        ps_box(fb, center - 19, 171, 38, 22, 11, on ? 1 : 11, true);
        ps_box(fb, center + (on ? 0 : -16), 174, 16, 16, 8, 15, true);
    }
    ps_text(fb, x, 156, 10, "自动方向", 4);
    ps_text(fb, x, 174, 10, "同步主机", 4);
    snprintf(
        text, sizeof(text), "%s  %s", settings.role == 1 ? "主机地址" : "连接地址",
        settings.role == 1 ? (primary_ip[0] ? primary_ip : "等待 Wi-Fi") : settings.host);
    ps_text(fb, x, 224, 13, text, 1);
    snprintf(text, sizeof(text), "配对码  %s", settings.code);
    ps_text(fb, x, 252, 16, text, 1);
    const char* state = status.synced      ? "双机书库一致，可以打开图书"
                        : status.error     ? "请检查网络、TF 卡和配对码"
                        : status.scanning  ? "正在校验本机书库"
                        : status.data_connected ? "正在补齐图书与共享字体"
                                           : "等待另一台设备连接";
    ps_text(fb, x, 290, 12, state, 2);
    snprintf(
        text, sizeof(text), "本机 %u 项 · 对端 %u 项 · 新增 %u 项", status.books, status.peer_books,
        status.imported);
    ps_text(fb, x, 316, 11, text, 5);
    if (status.error) {
        ps_text(fb, x, 340, 10, status.error, 5);
    } else if (status.expected) {
        snprintf(text, sizeof(text), "接收进度 %u%% · %u / %u KB",
                 (unsigned)((uint64_t)status.received*100/status.expected),
                 (unsigned)(status.received/1024), (unsigned)((status.expected+1023)/1024));
        ps_text(fb, x, 340, 10, text, 3);
    }
    snprintf(text, sizeof(text), "Wi-Fi 数据 %s · 蓝牙翻页 %s", status.data_connected ? "已连接" : "未连接",
             status.connected ? "已连接" : "等待配对");
    ps_text(fb, x, 362, 10, text, 3);
    int bottom = 404;
    ps_button(fb, x, bottom, (w - 8) / 2, 38, "重新连接与同步", false);
    ps_button(fb, x + (w + 8) / 2, bottom, (w - 8) / 2, 38, "打开书架", false);
    bool guard = ps_touch_guard_enabled();
    bool locked = ps_dual_rotation_locked();
    int half = (w - 8) / 2;
    ps_button(fb, x, 450, half, 34, guard ? "贴合防误触：开" : "贴合防误触：关", guard);
    ps_button(fb, x + half + 8, 450, w - half - 8, 34, locked ? "方向锁定：开" : "方向锁定：关", locked);
    bool calibrated = ps_dual_calibrated();
    ps_button(fb, x, 492, w, 34, calibrated ? "重新校准开合姿态" : "校准开合姿态（展开平放）", !calibrated);
    snprintf(text, sizeof(text), "动画灰阶：%d 灰（点击切换）", anim_gray);
    ps_button(fb, x, 532, w, 30, text, false);
    ps_text(fb, x, 568, 11, calibrated ? "已校准：折叠时屏蔽触摸，展开时正常滑动" : "未校准：防误触暂不启用", calibrated ? 4 : 5);
    if (!v.landscape) {
        ps_text(fb, x, 594, 13, "主机显示单页，从机显示下一页", 1);
        ps_text(fb, x, 620, 12, "主机电源短按：上一组双页", 3);
        ps_text(fb, x, 646, 12, "从机电源短按：下一组双页", 3);
        ps_text(fb, x, 672, 12, "长按电源：锁屏、关机或重启", 3);
        ps_text(fb, x, 712, 10, "Wi-Fi 同步图书与字库，蓝牙同步翻页。", 5);
        ps_text(fb, x, 734, 10, "书库一致后，Wi-Fi 断开仍可通过蓝牙翻页。", 5);
    }
}
// 只读当前网络，不扫描网段或保存未完成配置。/ Read the current network without scanning hosts or persisting an unfinished setup.
static bool local_address(char out[48]) {
    out[0] = 0;
    read_pico_connectivity_status_t* net = malloc(sizeof(*net));
    if (!net) return false;
    read_pico_connectivity_get_status(net);
    bool online = net->connected && !net->suspended && net->ip[0];
    if (online) snprintf(out, 48, "%s", net->ip);
    free(net);
    return online;
}
static bool parse_ipv4(const char* text, unsigned parts[4]) {
    if (!text || !*text) return false;
    for (int i = 0; i < 4; i++) {
        unsigned n = 0, digits = 0;
        while (*text >= '0' && *text <= '9') {
            n = n * 10 + (unsigned)(*text++ - '0');
            if (++digits > 3 || n > 255) return false;
        }
        if (!digits || (i < 3 ? *text++ != '.' : *text != 0)) return false;
        parts[i] = n;
    }
    return parts[0] != 0 && parts[0] < 224 && parts[3] != 0 && parts[3] != 255;
}
static void pair_stop(void) {
    if (pair_sock >= 0) close(pair_sock);
    pair_sock = -1;
    if (pair_scanning) pico_dual_ble_scan_stop();
    pair_scanning = false;
}
static void pair_scan_start(void) {
    load();
    if (!local_address(draft_local_ip)) {
        ps_toast("请先连接与主机相同的 Wi-Fi");
        return;
    }
    pair_stop();
    pair_found = false;
    pair_failed = false;
    pair_ip[0] = pair_code[0] = 0;
    pair_sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (pair_sock < 0) { ps_toast("无法启动配对搜索"); return; }
    int yes = 1;
    setsockopt(pair_sock, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
    struct sockaddr_in addr = { .sin_family = AF_INET, .sin_port = htons(18772), .sin_addr.s_addr = htonl(INADDR_ANY) };
    if (bind(pair_sock, (struct sockaddr*)&addr, sizeof(addr)) != 0) {
        pair_stop(); ps_toast("配对搜索端口被占用"); return;
    }
    int flags = fcntl(pair_sock, F_GETFL, 0);
    fcntl(pair_sock, F_SETFL, flags | O_NONBLOCK);
    pair_scanning = true;
    pair_started = ps_now();
    (void)pico_dual_ble_scan_start();
    ps_toast("正在通过蓝牙搜索主机");
    ps_dirty();
}
static void pair_scan_update(void) {
    load();
    int64_t now = ps_now();
    if (settings.role == 1) {
        // 不依赖双机页或校验完成；只要 Wi-Fi 有地址就持续广播。
        // Never wait for sync/verification or the dual page; broadcast whenever Wi-Fi has an address.
        if (!primary_ip[0] || now - last_ip >= 1000) {
            char ip[48];
            if (local_address(ip)) snprintf(primary_ip, sizeof(primary_ip), "%s", ip);
            else primary_ip[0] = 0;
            last_ip = now;
        }
        if (primary_ip[0] && settings.code[0]) pico_dual_ble_set_pairing_info(primary_ip, settings.code);
        if (primary_ip[0] && settings.code[0] && now - pair_last_tx >= 500) {
            pair_last_tx = now;
            int s = socket(AF_INET, SOCK_DGRAM, 0);
            if (s >= 0) {
                int yes = 1;
                setsockopt(s, SOL_SOCKET, SO_BROADCAST, &yes, sizeof(yes));
                char msg[80];
                int n = snprintf(msg, sizeof(msg), "PICOPAIR1 %s %s", settings.code, primary_ip);
                struct sockaddr_in to = { .sin_family = AF_INET, .sin_port = htons(18772) };
                inet_pton(AF_INET, "255.255.255.255", &to.sin_addr);
                (void)sendto(s, msg, (size_t)n, 0, (struct sockaddr*)&to, sizeof(to));
                // 部分 AP 丢弃受限广播，补发同网段定向广播。
                // Some APs drop limited broadcast; also send the subnet-directed address.
                char directed[48];
                snprintf(directed, sizeof(directed), "%s", primary_ip);
                char* dot = strrchr(directed, '.');
                if (dot) {
                    snprintf(dot + 1, sizeof(directed) - (size_t)(dot + 1 - directed), "255");
                    inet_pton(AF_INET, directed, &to.sin_addr);
                    (void)sendto(s, msg, (size_t)n, 0, (struct sockaddr*)&to, sizeof(to));
                }
                close(s);
            }
        }
    }
    if (!pair_scanning) return;
    pico_dual_ble_peer_info_t ble;
    pico_dual_ble_peer_info(&ble);
    if (ble.found && ble.ip[0] && strlen(ble.code) == 6) {
        snprintf(pair_ip, sizeof(pair_ip), "%s", ble.ip);
        snprintf(pair_code, sizeof(pair_code), "%s", ble.code);
        pair_found = true; pair_scanning = false;
        pico_dual_ble_scan_stop();
        if (pair_sock >= 0) { close(pair_sock); pair_sock = -1; }
        ps_dirty();
        return;
    }
    char buf[96];
    for (int i = 0; i < 4; i++) {
        struct sockaddr_in from;
        socklen_t len = sizeof(from);
        int n = (int)recvfrom(pair_sock, buf, sizeof(buf) - 1, 0, (struct sockaddr*)&from, &len);
        if (n <= 0) break;
        buf[n] = 0;
        char code[8] = {0}, ip[48] = {0};
        unsigned a, b, c, d;
        if (sscanf(buf, "PICOPAIR1 %7s %47s", code, ip) == 2 && strlen(code) == 6 &&
            sscanf(ip, "%u.%u.%u.%u", &a, &b, &c, &d) == 4) {
            bool digits = true;
            for (int j = 0; j < 6; j++) if (code[j] < '0' || code[j] > '9') digits = false;
            if (digits) {
                snprintf(pair_code, sizeof(pair_code), "%.6s", code);
                snprintf(pair_ip, sizeof(pair_ip), "%s", ip);
                pair_found = true;
                pair_scanning = false;
                ps_dirty();
                break;
            }
        }
    }
    if (pair_scanning && now - pair_started > 30000) {
        pair_stop();
        pair_failed = true;
        ps_toast("未找到主机，请确认蓝牙已开启");
        ps_dirty();
    }
}
static void secondary_host(const char* input);
static void secondary_code(const char* input);
static void edit_host(const char* initial) {
    char hint[96], copy[16];
    snprintf(copy, sizeof(copy), "%s", initial ? initial : "");
    char* last = strrchr(draft_local_ip, '.');
    if (last)
        snprintf(hint, sizeof(hint), "自动补全 %.*s · 也可填完整 IP", (int)(last - draft_local_ip + 1), draft_local_ip);
    else snprintf(hint, sizeof(hint), "输入主机配对页显示的完整 IP");
    ps_edit_numeric("主机地址 1/2", copy, 15, true, "下一步", hint, secondary_host);
}
static void edit_code(const char* initial) {
    char hint[96], copy[7];
    snprintf(copy, sizeof(copy), "%s", initial ? initial : "");
    snprintf(hint, sizeof(hint), "连接 %s · 查看主机六位码", draft_host);
    ps_edit_numeric("配对码 2/2", copy, 6, false, "配对", hint, secondary_code);
}
static void secondary_begin(void) {
    if (!local_address(draft_local_ip)) {
        ps_toast("请先连接与主机相同的 Wi-Fi");
        return;
    }
    draft_input[0] = draft_host[0] = 0;
    edit_host("");
}
static void secondary_host(const char* input) {
    char local[48], candidate[48];
    unsigned parts[4];
    snprintf(draft_input, sizeof(draft_input), "%s", input ? input : "");
    if (!local_address(local)) {
        edit_host(draft_input);ps_toast("Wi-Fi 已断开，请返回连接网络");return;
    }
    snprintf(draft_local_ip, sizeof(draft_local_ip), "%s", local);
    draft_short = !strchr(draft_input, '.');
    if (draft_short) {
        const char* last = strrchr(local, '.');
        if (!last || !draft_input[0] || strlen(draft_input) > 3) {
            edit_host(draft_input);ps_toast("输入地址最后一段：1–254");return;
        }
        snprintf(candidate, sizeof(candidate), "%.*s%s", (int)(last-local+1), local, draft_input);
    } else snprintf(candidate, sizeof(candidate), "%s", draft_input);
    if (!parse_ipv4(candidate, parts)) {
        edit_host(draft_input);ps_toast("地址无效，请检查每一段数字");return;
    }
    snprintf(draft_host, sizeof(draft_host), "%u.%u.%u.%u", parts[0], parts[1], parts[2], parts[3]);
    if (!strcmp(draft_host, local)) {
        edit_host(draft_input);ps_toast("这是本机地址，请填主机地址");return;
    }
    edit_code("");
}
static void secondary_code(const char* input) {
    char code[7], local[48];
    snprintf(code, sizeof(code), "%s", input ? input : "");
    bool valid = strlen(code) == 6;
    for (int i = 0; code[i]; i++) if (code[i] < '0' || code[i] > '9') valid = false;
    if (!valid) {edit_code(code);ps_toast("请输入主机显示的六位数字");return;}
    if (!local_address(local)) {edit_code(code);ps_toast("Wi-Fi 已断开，请返回连接网络");return;}
    if (draft_short && strcmp(local, draft_local_ip)) {
        snprintf(draft_local_ip, sizeof(draft_local_ip), "%s", local);
        edit_host(draft_input);ps_toast("本机地址已变化，请重新核对主机");return;
    }
    if (!ps_dual_configure(2, draft_host, code)) {
        edit_code(code);ps_toast("配置未保存，请检查 TF 卡是否可写");return;
    }
    ps_toast("配置已保存，正在连接主机");
}
bool ps_dual_event(const ps_event_t* e) {
    if (e->type != PS_TAP) return false;
    ps_view_t v = ps_view();
    int x = 20, w = v.w - 40;
    if (e->y >= 104 && e->y < 144) {
        int role = (e->x - x) / ((w - 16) / 3 + 8);
        if (role == 0)
            ps_dual_configure(0, "", "");
        else if (role == 1) {
            if (settings.role == 1) return true;
            char code[7];
            snprintf(code, sizeof(code), "%06u", (unsigned)(esp_random() % 1000000));
            ps_dual_configure(1, "", code);
        } else if (role == 2)
            pair_scan_start();
        return true;
    }
    if (!settings.role && (pair_scanning || pair_found || pair_failed)) {
        if (pair_failed) {
            if (e->y >= 280 && e->y < 320) { pair_scan_start(); return true; }
            if (e->y >= 328 && e->y < 364) { pair_failed = false; ps_dirty(); return true; }
            return true;
        }
        if (pair_found && e->y >= 292 && e->y < 332) {
            if (ps_dual_configure(2, pair_ip, pair_code)) {
                pair_stop(); ps_toast("配对已保存，正在连接主机");
            }
            return true;
        }
        if (pair_found && e->y >= 340 && e->y < 376) { pair_scan_start(); return true; }
        if (pair_found && e->y >= 384 && e->y < 420) { pair_stop(); ps_dirty(); return true; }
        if (!pair_found && e->y >= 292 && e->y < 332) { pair_stop(); ps_dirty(); return true; }
        return true;
    }
    if (!settings.role && e->y >= 250 && e->y < 292) {
        pair_scan_start();
        return true;
    }
    if (!settings.role && e->y >= 300 && e->y < 338) {
        secondary_begin();
        return true;
    }
    if (!settings.role && e->y >= 348 && e->y < 384) {
        ps_navigate(PS_WIFI);
        return true;
    }
    if (settings.role && e->y >= 150 && e->y < 198) {
        for (int role = 1; role <= 2; ++role) {
            int left = x + role * ((w - 16) / 3 + 8);
            if (e->x >= left && e->x < left + (w - 16) / 3) {
                bool on = role == 1 ? orientation.primary_flip : orientation.secondary_flip;
                if (!ps_dual_flip(role, !on)) ps_toast("请先连接主机再调整方向");
                return true;
            }
        }
    }
    if (settings.role && e->y >= 532 && e->y < 562) {
        ps_dual_toggle_anim_gray();
        ps_dirty();
        return true;
    }
    if (settings.role && e->y >= 492 && e->y < 526) {
        (void)ps_dual_calibrate();
        return true;
    }
    if (settings.role && e->y >= 450 && e->y < 484) {
        int half = (w - 8) / 2;
        if (e->x < x + half) {
            bool on = ps_touch_guard_enabled();
            if (ps_touch_guard_set(!on)) ps_toast(on ? "已关闭贴合防误触" : "已开启贴合防误触");
        } else {
            bool on = ps_dual_rotation_locked();
            if (ps_dual_lock_set(!on)) ps_toast(on ? "已解除方向锁定" : "已锁定当前方向");
        }
        return true;
    }
    int bottom = 404;
    if (settings.role && e->y >= bottom && e->y < bottom + 38) {
        if (e->x < v.w / 2) {
            if (dual_session)
                dl_rescan(dual_session);
            else
                start();
            ps_dirty();
        } else
            ps_navigate(PS_SHELF);
        return true;
    }
    return false;
}

void ps_dual_status(dl_status_t* out) {
    if (dual_session)
        dl_status(dual_session, out);
    else {
        *out = status;
        out->orientation = orientation;
    }
}

// 可信USB调试仅操作已校验目录序号和普通翻页；不接收路径、不输出配对码、不清书库。
// Trusted USB diagnostics use verified catalog indices and normal turns, never paths, pairing secrets or erasure.
bool ps_dual_usb_command(const char* line, char* out, size_t capacity) {
    bool primary = !strcmp(line, "RPICO DUAL PRIMARY");
    bool item = !strncmp(line, "RPICO DUAL ITEM ", 16);
    bool open = !strncmp(line, "RPICO DUAL OPEN ", 16);
    bool next = !strcmp(line, "RPICO DUAL NEXT"), previous = !strcmp(line, "RPICO DUAL PREVIOUS");
    bool shelf = !strcmp(line, "RPICO DUAL SHELF");
    bool rescan = !strcmp(line, "RPICO DUAL RESCAN");
    bool rotate = !strcmp(line, "RPICO DUAL ROTATE");
    bool lock = !strncmp(line, "RPICO DUAL LOCK ", 16);
    bool guard = !strncmp(line, "RPICO DUAL GUARD ", 17);
    bool calibrate = !strcmp(line, "RPICO DUAL CALIBRATE");
    bool font = !strncmp(line, "RPICO DUAL FONT ", 16);
    if (!primary && !item && !open && !next && !previous && !shelf && !rescan && !rotate && !lock && !guard && !calibrate && !font && strcmp(line, "RPICO DUAL STATUS")) return false;
    load();
    bool accepted = true;
    if (item || open) {
        const char* digits = line + 16;
        unsigned index = 0;
        accepted = *digits != 0 && strlen(digits) <= 3;
        for (const char* c = digits; accepted && *c; ++c) {
            if (*c < '0' || *c > '9') accepted = false;
            else index = index * 10 + (unsigned)(*c - '0');
        }
        uint8_t id[32]; uint32_t bytes = 0;
        const char* path = accepted ? dl_entry(dual_session, index, id, &bytes) : NULL;
        accepted = path != NULL;
        if (item) {
            char hex[65] = {0};
            if (path) for (unsigned i=0; i<32; ++i) snprintf(hex+2*i,3,"%02x",id[i]);
            const char* ext = path ? strrchr(path, '.') : NULL;
            snprintf(out,capacity,"{\"dual_item\":true,\"accepted\":%s,\"index\":%u,\"id\":\"%s\",\"bytes\":%u,\"font\":%s}",
                accepted?"true":"false",index,hex,(unsigned)bytes,ext&&!strcasecmp(ext,".ttf")?"true":"false");
            return true;
        }
        if (accepted) {
            accepted = ps_background_io_allowed() && !ps_power_pending() && !ps_storage_busy() &&
                !ps_usb_busy() && !ps_usb_held() && !ps_content_held() && !ps_transfer_prepared() &&
                ps_route()!=PS_EDITOR && !app_book_global_has_selection();
            if (accepted) {
                ps_navigate(PS_SHELF);
                accepted = ps_route()==PS_SHELF && ps_dual_open(path);
            }
        }
    } else if (next || previous) {
        accepted = app_book_global_is_open() && ps_background_io_allowed() &&
            !app_book_global_has_selection() && !ps_power_pending() && dual_session &&
            dl_turn(dual_session, next ? 1 : -1);
    } else if (font) {
        const char* px = line + 16;
        accepted = strlen(px) == 2 && px[0] >= '0' && px[0] <= '9' && px[1] >= '0' && px[1] <= '9' &&
            app_book_global_is_open() && ps_background_io_allowed() && !app_book_global_has_selection() &&
            ps_dual_resize((uint8_t)((px[0] - '0') * 10 + px[1] - '0'));
    } else if (rescan || rotate || lock || guard || calibrate) {
        accepted = dual_session && ps_background_io_allowed() && !ps_power_pending() &&
            !ps_storage_busy() && !ps_usb_busy() && !ps_usb_held() && !ps_content_held() &&
            !ps_transfer_prepared() && ps_route()!=PS_EDITOR && !app_book_global_has_selection() && ps_dual_enabled();
        if (accepted) {
            if (rotate) accepted = ps_dual_rotate();
            else if (lock) {
                const char* value = line + 16;
                accepted = !strcmp(value, "ON") || !strcmp(value, "OFF");
                if (accepted) accepted = ps_dual_lock_set(!strcmp(value, "ON"));
            } else if (guard) {
                const char* value = line + 17;
                accepted = !strcmp(value, "ON") || !strcmp(value, "OFF");
                if (accepted) accepted = ps_dual_guard_set(!strcmp(value, "ON"));
            } else if (calibrate) {
                accepted = ps_dual_calibrate();
            } else dl_rescan(dual_session);
            ps_dirty();
        }
    } else if (shelf) {
        accepted = ps_background_io_allowed() && !ps_power_pending() && ps_route()!=PS_EDITOR &&
            !app_book_global_has_selection();
        if (accepted) { ps_navigate(PS_SHELF); accepted = ps_route()==PS_SHELF; }
    }
    if (primary) {
        if (settings.role != 1) {
            char code[7];
            snprintf(code, sizeof(code), "%06u", (unsigned)(esp_random() % 1000000));
            accepted = ps_dual_configure(1, "", code);
        }
        if (accepted) ps_navigate(PS_DUAL);
    }
    dl_status_t state;
    ps_dual_status(&state);
    pico_dual_ble_status_t ble;
    pico_dual_ble_status(&ble);
    app_book_dual_status_t book; app_book_dual_status(&book);
    char book_id[65];
    for (unsigned i=0; i<32; ++i) snprintf(book_id+2*i,3,"%02x",state.reading.book[i]);
    snprintf(
        out, capacity,
        "{\"dual\":true,\"accepted\":%s,\"role\":%u,\"connected\":%s,\"synced\":%s,\"scanning\":%s,"
        "\"books\":%u,\"peer_books\":%u,\"spread\":%u,\"data_connected\":%s,"
        "\"ble_active\":%s,\"ble_error\":%d,\"error\":%s,"
        "\"ui_block\":%u,\"tick_age\":%u,\"work_age\":%u,\"busy\":%s,\"received\":%u,\"expected\":%u,\"sent\":%u,\"receiving\":%s,\"sending\":%s,"
        "\"reused\":%u,\"verified_bytes\":%u,\"resumed\":%u,"
        "\"listed\":%s,\"peer_listed\":%s,\"done\":%s,\"peer_done\":%s,\"catalog_valid\":%s,\"peer_catalog_valid\":%s,"
        "\"revision\":%u,\"applied\":%u,\"presented\":%u,\"left_ready\":%s,\"rotation\":%d,\"rotation_locked\":%s,\"dual_locked\":%s,\"guard\":%s,\"face\":%d,\"peer_face\":%d,\"contact\":%s,\"calibrated\":%s,\"base_seq\":%u,\"base_valid\":%s,\"cal_request\":%s,\"peer_reading\":%s,\"orientation\":%u,"
        "\"font_px\":%u,\"book_font_px\":%u,\"reflow_pending\":%s,\"reading_open\":%s,\"target\":%u,\"page\":%u,\"blank\":%s,\"chapter\":%u,\"start\":%u,\"end\":%u,\"book\":\"%s\"}",
        accepted ? "true" : "false", settings.role, state.connected ? "true" : "false",
        state.synced ? "true" : "false", state.scanning ? "true" : "false", state.books,
        state.peer_books, (unsigned)state.reading.spread, state.data_connected ? "true" : "false",
        ble.active ? "true" : "false", ble.error, state.error ? "true" : "false",
        ps_background_io_blockers(),(unsigned)(ps_now()-last_tick),(unsigned)(ps_now()-last_work),
        state.busy?"true":"false",(unsigned)state.received,(unsigned)state.expected,(unsigned)state.sent,
        state.receiving?"true":"false",state.sending?"true":"false",
        state.reused,(unsigned)state.verified_bytes,(unsigned)state.resumed,
        state.listed?"true":"false",state.peer_listed?"true":"false",
        state.done?"true":"false",state.peer_done?"true":"false",state.catalog_valid?"true":"false",state.peer_catalog_valid?"true":"false",
        (unsigned)state.reading.revision,(unsigned)applied,(unsigned)presented,state.left_ready?"true":"false",
        (int)epd_get_rotation(),ps_rotation_locked()?"true":"false",ps_dual_rotation_locked()?"true":"false",guard_enabled?"true":"false",(int)status.face,(int)status.peer_face,ps_dual_contact_likely()?"true":"false",ps_dual_calibrated()?"true":"false",(unsigned)status.base_seq,status.base_valid?"true":"false",status.calibration_request?"true":"false",status.peer_reading?"true":"false",(unsigned)orientation_bits(),(unsigned)state.reading.font_px,(unsigned)book.font_px,state.reading.reflow_pending?"true":"false",
        book.open?"true":"false",(unsigned)book.target,(unsigned)book.page,
        book.blank?"true":"false",(unsigned)book.chapter,(unsigned)book.start,(unsigned)book.end,book_id);
    return true;
}

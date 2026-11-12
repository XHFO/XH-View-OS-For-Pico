/* SPDX-License-Identifier: Apache-2.0
 * 双机配对页、TF生命周期、阅读联动。/ Pairing page, TF lifecycle and linked reading.
 * 冻结：共享分页使用竖屏及主机完整字体；按用户要求支持同步字号，主机按原正文锚点重排后才放行副机。
 * Frozen: Shared portrait pagination uses the primary's full font; requested shared sizing reflows the primary's text anchor before releasing the secondary.
 * 同步不导出TF，不绕过编辑或电源保护。/ Synchronization never exports TF or bypasses edit/power guards.
 */
#include "pico_dual.h"
#include "pico_home_pga.h"
#include "book_progress.h"
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
#include "pico_theme.h"
#include "read_pico_connectivity.h"
#ifdef ESP_PLATFORM
#include "esp_event.h"
#include "esp_mac.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include "psa/crypto.h"
#include "wifi_memory_config.h"
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
typedef struct {
    uint32_t magic;
    uint8_t role;
    char host[48], code[7];
    uint8_t net_mode;      // 0自动，1同一局域网，2主机热点。/ 0 auto, 1 LAN, 2 primary hotspot.
    uint8_t keep_data;     // 1同步后保持数据通道，0仅保留蓝牙。/ 1 keep data, 0 BLE only.
} dual_settings_v2_t;
typedef struct {
    uint32_t magic;
    uint8_t role;
    char host[48], code[7];
} dual_settings_v1_t;
typedef struct {
    uint32_t magic;
    uint8_t local_id[8], peer_id[8], catalog[32];
} dual_peer_cache_t;
enum { DUAL_NET_AUTO, DUAL_NET_LAN, DUAL_NET_HOTSPOT };
static dual_settings_t settings;
static uint8_t settings_net_mode = DUAL_NET_AUTO;
static uint8_t settings_keep_data = 1;
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
static bool local_away, progress_rejoin;
static uint32_t rejoin_revision;
static bool contact_latched;
static uint32_t annotation_applied;
static int64_t contact_same_since;
static bool manual_rotation;
static int64_t orientation_store_at;
static uint8_t primary_step;
static uint8_t primary_net_draft, primary_keep_draft;
static bool data_finished;
static int64_t auto_shelf_at;
static dual_peer_cache_t peer_cache;
static bool peer_cache_loaded, same_peer;
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
static uint32_t checkpoint_revision;
static int64_t checkpoint_changed;
static int64_t last_tick, last_ip, last_work;
static int pair_sock = -1;
static bool pair_scanning, pair_found, pair_failed, pair_editing;
static int anim_gray = 16;
static bool anim_enabled = true;
static char pair_ip[48], pair_code[7], pair_ap_ssid[33];
static int64_t pair_started, pair_last_tx;
static char auto_ap_ssid[33];
static int64_t auto_ap_retry_at;
static bool hotspot_active;
static char hotspot_ssid[33];
static bool hotspot_driver, hotspot_started, hotspot_loop_owned;
static int64_t hotspot_retry_at;
#ifdef ESP_PLATFORM
static esp_netif_t* hotspot_netif;
#endif
static void hotspot_stop(void);
static void pair_stop(void);
static void pair_scan_start(void);
static void pair_scan_update(void);
static void edit_code(const char* initial);
static bool local_address(char out[48]);
static unsigned library_revision;
static char primary_ip[48];
static char draft_host[48];
static char draft_ap_ssid[33];
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
    dual_settings_v2_t current;
    memset(&settings, 0, sizeof(settings));
    settings_net_mode = DUAL_NET_AUTO;
    settings_keep_data = 1;
    if (ps_load("dualconfig1", &current, sizeof(current)) && current.magic == 0x44554c31 &&
        current.role <= 2 && current.net_mode <= DUAL_NET_HOTSPOT &&
        current.keep_data <= 1) {
        settings.magic = current.magic;
        settings.role = current.role;
        memcpy(settings.host, current.host, sizeof(settings.host));
        memcpy(settings.code, current.code, sizeof(settings.code));
        settings_net_mode = current.net_mode;
        settings_keep_data = current.keep_data;
    } else {
        dual_settings_v1_t legacy;
        if (ps_load("dualconfig1", &legacy, sizeof(legacy)) && legacy.magic == 0x44554c31 &&
            legacy.role <= 2) {
            settings.magic = legacy.magic;
            settings.role = legacy.role;
            memcpy(settings.host, legacy.host, sizeof(settings.host));
            memcpy(settings.code, legacy.code, sizeof(settings.code));
        }
    }
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
    uint8_t anim = 1;
    if (ps_load("animcfg1", &anim, sizeof(anim))) {
        anim_enabled = !!(anim & 1);
        anim_gray = (anim & 2) ? 8 : 16;
    }
    dual_base_t stored;
    if (ps_load("dualbase1", &stored, sizeof(stored)) && stored.magic == 0x31425344u) {
        base = stored; calibrated = true;
    }
}
static bool settings_store(void) {
    dual_settings_v2_t saved = {.magic = settings.magic, .role = settings.role,
                                .net_mode = settings_net_mode, .keep_data = settings_keep_data};
    memcpy(saved.host, settings.host, sizeof(saved.host));
    memcpy(saved.code, settings.code, sizeof(saved.code));
    return ps_store("dualconfig1", &saved, sizeof(saved));
}
bool ps_dual_enabled(void) {
    load();
    return settings.role != 0;
}
int ps_dual_role(void) {
    load();
    return settings.role;
}
bool ps_dual_connected(void) {
    dl_status_t current;
    ps_dual_status(&current);
    return ps_dual_enabled() && current.connected;
}
bool ps_dual_synced(void) {
    dl_status_t current;
    ps_dual_status(&current);
    return ps_dual_enabled() && current.connected && current.synced;
}
bool ps_dual_secondary_shelf(void) {
    return ps_dual_enabled() && ps_dual_role() == 2 && ps_dual_synced();
}
bool ps_dual_has_reading(void) {
    return ps_dual_enabled() && ps_dual_synced() && dual_session &&
        dl_reading_path(dual_session) && dl_font_path(dual_session);
}
bool ps_dual_book_progress(const char* path, uint32_t bytes, bool* started, uint8_t* pct, uint32_t* recent) {
    dl_book_progress_t progress;
    if (!ps_dual_synced() || !dual_session || !dl_book_progress(dual_session, path, bytes, &progress)) return false;
    if (started) *started = progress.started;
    if (pct) *pct = progress.pct;
    if (recent) *recent = progress.recent;
    return true;
}
uint32_t ps_dual_progress_revision(void) {
    return dual_session ? dl_progress_revision(dual_session) : 0;
}
bool ps_dual_holds_awake(void) {
    return ps_dual_enabled() &&
        (hotspot_active || (dual_session &&
        (status.connected || status.data_connected || status.scanning || status.busy)));
}
static void direct_password(const char* code, const char* ssid, char out[33]) {
    char source[80];
    uint8_t digest[32];
    snprintf(source, sizeof(source), "XH-DUAL-AP-2|%s|%s", code ? code : "", ssid ? ssid : "");
#ifdef ESP_PLATFORM
    size_t written = 0;
    if (psa_crypto_init() != PSA_SUCCESS ||
        psa_hash_compute(PSA_ALG_SHA_256, (const uint8_t*)source, strlen(source),
                         digest, sizeof(digest), &written) != PSA_SUCCESS ||
        written != sizeof(digest))
        memset(digest, 0, sizeof(digest));
#else
    uint32_t h = 2166136261u;
    for (const unsigned char* p = (const unsigned char*)source; *p; ++p) h = (h ^ *p) * 16777619u;
    for (unsigned i = 0; i < sizeof(digest); ++i) { h = h * 1664525u + 1013904223u; digest[i] = (uint8_t)(h >> 24); }
#endif
    for (unsigned i = 0; i < 16; ++i) snprintf(out + i * 2, 3, "%02x", digest[i]);
    out[32] = 0;
}
static bool hotspot_start(const char* code) {
#ifdef ESP_PLATFORM
    if (hotspot_active) return true;
    if (read_pico_connectivity_quiesce(5000) != ESP_OK) return false;
    esp_err_t err = esp_netif_init();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) return false;
    err = esp_event_loop_create_default();
    if (err == ESP_OK) hotspot_loop_owned = true;
    else if (err != ESP_ERR_INVALID_STATE) return false;
    hotspot_netif = esp_netif_create_default_wifi_ap();
    if (!hotspot_netif) {
        if (hotspot_loop_owned) { esp_event_loop_delete_default(); hotspot_loop_owned = false; }
        (void)read_pico_connectivity_resume(); return false;
    }
    wifi_init_config_t init = read_pico_wifi_init_config();
    err = esp_wifi_init(&init);
    if (err != ESP_OK) goto fail;
    hotspot_driver = true;
    uint8_t mac[6];
    err = esp_read_mac(mac, ESP_MAC_WIFI_SOFTAP);
    if (err != ESP_OK) goto fail;
    snprintf(hotspot_ssid, sizeof(hotspot_ssid), "PicoDual-%02X%02X", mac[4], mac[5]);
    wifi_config_t ap = {0};
    size_t ssid_len = strlen(hotspot_ssid);
    if (ssid_len > sizeof(ap.ap.ssid)) goto fail;
    memcpy(ap.ap.ssid, hotspot_ssid, ssid_len);
    direct_password(code, hotspot_ssid, (char*)ap.ap.password);
    ap.ap.ssid_len = (uint8_t)ssid_len;
    ap.ap.channel = 1;
    ap.ap.max_connection = 2;
    ap.ap.authmode = WIFI_AUTH_WPA2_PSK;
    err = esp_wifi_set_storage(WIFI_STORAGE_RAM);
    if (!err) err = esp_wifi_set_mode(WIFI_MODE_AP);
    if (!err) err = esp_wifi_set_config(WIFI_IF_AP, &ap);
    if (!err) err = esp_wifi_start();
    if (err != ESP_OK) goto fail;
    hotspot_started = true;
    hotspot_active = true;
    return true;
fail:
    if (hotspot_driver) { (void)esp_wifi_deinit(); hotspot_driver = false; }
    if (hotspot_netif) { esp_netif_destroy_default_wifi(hotspot_netif); hotspot_netif = NULL; }
    if (hotspot_loop_owned) { esp_event_loop_delete_default(); hotspot_loop_owned = false; }
    (void)read_pico_connectivity_resume();
    return false;
#else
    return false;
#endif
}
static void hotspot_stop(void) {
#ifdef ESP_PLATFORM
    if (!hotspot_active) return;
    if (hotspot_started) (void)esp_wifi_stop();
    if (hotspot_driver) (void)esp_wifi_deinit();
    if (hotspot_netif) { esp_netif_destroy_default_wifi(hotspot_netif); hotspot_netif = NULL; }
    if (hotspot_loop_owned) { esp_event_loop_delete_default(); hotspot_loop_owned = false; }
    hotspot_started = hotspot_driver = false;
    hotspot_active = false;
    hotspot_ssid[0] = 0;
    (void)read_pico_connectivity_resume();
#else
    hotspot_active = false;
#endif
}
static void reading_checkpoint(bool force, int64_t now) {
    if (status.reading.revision != checkpoint_revision) {
        checkpoint_revision = status.reading.revision;
        checkpoint_changed = now;
    }
    // 翻页热路径不逐页同步写卡；退出或停翻后保存最新会话，正文锚点仍独立可靠同步。
    // Keep synchronous storage off each turn; checkpoint on exit or idle, independently of anchor ACKs.
    if (settings.role == 1 && dual_session && !status.reading.reflow_pending &&
        status.reading.revision != stored && dl_reading_path(dual_session) &&
        (force || now - checkpoint_changed >= 1500) &&
        ps_store("dualreading2", &status.reading, sizeof(status.reading)))
        stored = status.reading.revision;
}
void ps_dual_suspend(void) {
    reading_checkpoint(true, ps_now());
    hotspot_stop();
    pico_dual_ble_stop();
    ble_generation = 0;
    if (dual_session) {
        dl_destroy(dual_session);
        dual_session = NULL;
    }
    memset(&status, 0, sizeof(status));
    applied = presented = stored = content_revision = 0;
    checkpoint_revision = 0;
    checkpoint_changed = 0;
    reading_known = false;
    reading_active = true;
    local_away = progress_rejoin = false;
}
static bool configure_full(int role, const char* host, const char* code, uint8_t net_mode, uint8_t keep_data) {
    load();
    if (role < 0 || role > 2 || ps_power_pending() || ps_storage_busy() || ps_usb_held())
        return false;
    if (net_mode > DUAL_NET_HOTSPOT || keep_data > 1) return false;
    dual_settings_v2_t next = { .magic = 0x44554c31, .role = (uint8_t)role,
                                .net_mode = role == 1 ? net_mode : DUAL_NET_AUTO,
                                .keep_data = role == 1 ? keep_data : 1 };
    if (role) {
        if (!code || strlen(code) != 6) return false;
        for (int i = 0; i < 6; i++)
            if (code[i] < '0' || code[i] > '9') return false;
        if (role == 2 && (!host || !*host || strlen(host) >= sizeof(next.host))) return false;
        snprintf(next.host, sizeof(next.host), "%s", host ? host : "");
        snprintf(next.code, sizeof(next.code), "%s", code);
    }
    if (!ps_store("dualconfig1", &next, sizeof(next))) {
        ps_toast("设置保存失败，请检查 TF 卡");
        return false;
    }
    pair_stop();
    pair_editing = false;
    primary_step = 0;
    ps_dual_suspend();
    pico_dual_ble_request_restart();
    settings = (dual_settings_t){.magic = next.magic, .role = next.role};
    memcpy(settings.host, next.host, sizeof(settings.host));
    memcpy(settings.code, next.code, sizeof(settings.code));
    settings_net_mode = next.net_mode;
    settings_keep_data = next.keep_data;
    data_finished = false;
    primary_ip[0] = 0;
    last_ip = 0;
    auto_shelf_at = 0;
    auto_ap_ssid[0] = 0;
    auto_ap_retry_at = 0;
    ps_dual_rotation_changed();
    ps_dirty();
    return true;
}
bool ps_dual_configure(int role, const char* host, const char* code) {
    return configure_full(role, host, code, DUAL_NET_AUTO, 1);
}
static bool dual_progress_get(const char* path, uint32_t bytes, dl_book_progress_t* out, void* context) {
    (void)context;
    book_progress_t progress={0},live;
    if(!out)return false;
    bool found=book_progress_load(path,bytes,&progress);
    if(app_book_progress_snapshot(path,bytes,&live)&&(!found||book_progress_ahead(&live,&progress))){progress=live;found=true;}
    if(!found)return false;
    *out = (dl_book_progress_t){.started = true, .pct = progress.pct, .recent = progress.last_open_s,
        .chapter=progress.chapter,.byte_off=progress.byte_off,.px=progress.px};
    return true;
}
static bool dual_progress_set(const char* path,uint32_t bytes,const dl_book_progress_t* in,void* context){
    (void)context;
    book_progress_t saved={.file_size=bytes,.chapter=in->chapter,.byte_off=in->byte_off,.px=in->px,.pct=in->pct};
    book_progress_t durable;
    if(book_progress_load(path,bytes,&durable)&&!book_progress_ahead(&saved,&durable))return true;
    return book_progress_save_mode(path,&saved,true)==ESP_OK;
}
bool ps_dual_book_anchor(const char* path,uint32_t bytes,book_progress_t* out){
    dl_book_progress_t p;
    if(!dual_session||!dl_book_anchor(dual_session,path,bytes,&p)||!p.started)return false;
    *out=(book_progress_t){.file_size=bytes,.chapter=p.chapter,.byte_off=p.byte_off,.px=p.px,.pct=p.pct,.last_open_s=p.recent};
    return true;
}
static void device_id_get(uint8_t out[8]) {
    memset(out, 0, 8);
#ifdef ESP_PLATFORM
    (void)esp_read_mac(out, ESP_MAC_WIFI_STA);
#else
    unsigned pid = (unsigned)getpid();
    out[0] = 0x58; out[1] = 0x50; out[2] = (uint8_t)(pid >> 24);
    out[3] = (uint8_t)(pid >> 16); out[4] = (uint8_t)(pid >> 8); out[5] = (uint8_t)pid;
#endif
    out[6] = 0x44; out[7] = 0x31;
}
static bool peer_cache_update(const dl_status_t* state) {
    if (!state || !state->peer_id_valid) {
        same_peer = false;
        return false;
    }
    if (!peer_cache_loaded) {
        peer_cache_loaded = true;
        if (!ps_load("dualpeer1", &peer_cache, sizeof(peer_cache)) ||
            peer_cache.magic != 0x50445531u) {
            memset(&peer_cache, 0, sizeof(peer_cache));
            peer_cache.magic = 0x50445531u;
        }
    }
    uint8_t local[8];
    device_id_get(local);
    same_peer = !memcmp(peer_cache.local_id, local, 8) &&
        !memcmp(peer_cache.peer_id, state->peer_device_id, 8);
    if (!state->synced) return same_peer;
    bool changed = memcmp(peer_cache.local_id, local, 8) ||
        memcmp(peer_cache.peer_id, state->peer_device_id, 8) ||
        memcmp(peer_cache.catalog, state->catalog, 32);
    if (changed) {
        memcpy(peer_cache.local_id, local, 8);
        memcpy(peer_cache.peer_id, state->peer_device_id, 8);
        memcpy(peer_cache.catalog, state->catalog, 32);
        (void)ps_store("dualpeer1", &peer_cache, sizeof(peer_cache));
    }
    return same_peer;
}
static bool start(void) {
    read_pico_sd_info_t sd = { 0 };
    read_pico_sd_get_info(&sd);
    if (!sd.mounted || !sd.present) return false;
    dl_config_t cfg = { .role = settings.role, .port = DL_PORT };
    device_id_get(cfg.device_id);
    cfg.progress_get = dual_progress_get;
    cfg.progress_set = dual_progress_set;
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
    const char* font = ttf_font_path();
    if (!font || ttf_font_path_is_builtin(font)) font = TTF_FONT_DEFAULT_SD;
    snprintf(cfg.font, sizeof(cfg.font), "%s", font);
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
int ps_dual_anim_gray(void) { load(); return anim_gray; }
static void anim_save(void) {
    uint8_t bits = (anim_enabled ? 1 : 0) | (anim_gray == 8 ? 2 : 0);
    if (!ps_store("animcfg1", &bits, sizeof(bits))) ps_toast("动画设置保存失败");
}
void ps_dual_toggle_anim_gray(void) {
    load(); anim_gray = anim_gray == 16 ? 8 : 16; anim_save();
}
bool ps_dual_anim_enabled(void) { load(); return anim_enabled; }
bool ps_dual_anim_set_enabled(bool enabled) {
    load(); anim_enabled = enabled; anim_save(); return true;
}
void ps_animation_render(uint8_t* fb) {
    ps_view_t v = ps_view();
    ps_heading(fb, "翻页动画", "控制双机阅读与单机阅读的翻页过渡");
    int x = 20, w = v.w - 40;
    ps_toggle(fb, x, 150, w, 44, "翻页动画", anim_enabled, true);
    ps_button(fb, x, 206, w, 40, anim_gray == 16 ? "动画灰阶：16 灰" : "动画灰阶：8 灰", anim_gray == 16);
    ps_text(fb, x, 266, 12, "关闭后翻页直接切换，不做过渡动画。", 4);
    ps_text(fb, x, 292, 12, "灰阶只影响动画渐变，不影响正文清晰度。", 4);
    ps_text(fb, x, 330, 11, "双机阅读中关闭动画可进一步降低翻页延迟。", 5);
}
bool ps_animation_event(const ps_event_t* e) {
    bool next;ps_view_t v=ps_view();
    if(ps_switch_swipe(e,v.w-86,150,66,44,&next)){ps_dual_anim_set_enabled(next);ps_dirty();return true;}
    if (!e || e->type != PS_TAP) return false;
    if (e->y >= 150 && e->y < 194) { ps_dual_anim_set_enabled(!ps_dual_anim_enabled()); ps_dirty(); return true; }
    if (e->y >= 206 && e->y < 246 && ps_dual_anim_enabled()) { ps_dual_toggle_anim_gray(); ps_dirty(); return true; }
    return false;
}
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
uint8_t ps_dual_game_role(void) {
    load();
    if (!settings.role || !dual_session || !status.connected) return 0;
    return settings.role == 1 ? 1 : 2;
}
bool ps_dual_game_send(uint8_t input) {
    return dual_session && dl_game_input(dual_session, input);
}
int ps_dual_game_recv(void) {
    uint8_t input;
    return dual_session && dl_game_take(dual_session, &input) ? (int)input : -1;
}
bool ps_dual_game_open(uint8_t kind) {
    return dual_session && dl_game_open(dual_session, kind);
}
bool ps_dual_game_open_take(uint8_t* kind) {
    return dual_session && dl_game_open_take(dual_session, kind);
}
bool ps_dual_game_packet_send(const void* data,size_t bytes) {
    return dual_session && dl_game_packet(dual_session, data, bytes);
}
bool ps_dual_game_close(void) {
    return dual_session && dl_game_close(dual_session);
}
bool ps_dual_game_close_take(void) {
    return dual_session && dl_game_close_take(dual_session);
}
size_t ps_dual_game_packet_recv(void* data,size_t capacity) {
    return dual_session ? dl_game_packet_take(dual_session, data, capacity) : 0;
}
bool ps_dual_peer_reading(void) {
    return status.connected&&status.synced&&status.peer_reading;
}
void ps_dual_set_local_away(bool away) {
    if (away && dual_session) (void)dl_remote_open_take(dual_session);
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
    if (!ps_dual_enabled()) return true;
    local_away = false;
    return open_request(path, 0);
}
bool ps_dual_open_at(const char* path, uint32_t page) {
    if (!ps_dual_enabled() || !page) return false;
    local_away = false;
    return open_request(path, page);
}
bool ps_dual_activate(const char* path) {
    if (!ps_dual_enabled() || !dual_session || !path) return false;
    local_away = false;
    return dl_activate_path(dual_session, path);
}
static void control_send(void) {
    if (!dual_session || !pico_dual_ble_can_send()) return;
    uint8_t packet[DL_CONTROL_MAX];
    size_t n = dl_control_take(dual_session, packet, sizeof(packet));
    if (!n) n = ps_home_pga_share_take(packet, sizeof(packet));
    if (n) (void)pico_dual_ble_send(packet, n);
}
bool ps_dual_turn(int direction) {
    if (!ps_dual_enabled()) return false;
    bool accepted = dual_session && dl_turn(dual_session, direction);
    if (accepted) control_send();
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
    local_away = false;
    applied = presented = 0;
    return true;
}
void ps_dual_layout_ready(uint32_t page) {
    if (dual_session && page == status.reading.spread + 1 &&
        dl_release(dual_session, applied)) control_send();
}
bool ps_dual_wait_peer(void) {
    // UI tick 已读取最新控制状态；这里只读缓存，不重复复制整份目录状态。/ The UI tick refreshed control state; read its cache without recopying catalog status.
    return status.connected && status.synced && status.peer_reading && !status.peer_presented && !status.relay_ready;
}
void ps_dual_relay_ready(uint32_t page) {
    if (dual_session && page == status.reading.spread + (settings.role == 2 ? 2u : 1u) &&
        dl_relay_ready(dual_session, applied)) control_send();
}
void ps_dual_presented(uint32_t page) {
    if (!dual_session) return;
    dl_status_t current; dl_status(dual_session, &current);
    // 主机单读时 revision 由本地翻页直接生成，不经过远端 apply；成功推屏后仍需释放 left_ready。
    // A primary solo turn creates the revision locally without remote apply; successful paint still releases left_ready.
    if (settings.role == 1 && !current.peer_reading && (page == current.reading.spread + 1 || page == current.reading.spread + 2) &&
        !current.reading.reflow_pending && dl_presented(dual_session, current.reading.revision)) {
        applied = presented = current.reading.revision;
        control_send();
        return;
    }
    if (!applied) return;
    if (current.reading.reflow_pending || current.reading.revision != applied || page != current.reading.spread + (settings.role == 2 ? 2u : 1u)) return;
    (void)dl_presented(dual_session, applied);
    presented = applied;
    control_send();
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
    if (!ps_dual_enabled()) {
        uint8_t empty_owner[8]={0};ps_home_pga_share_tick(0,false,empty_owner,ctx->now_ms,app==&app_book);
        if (pair_scanning) pair_scan_update();
        return false;
    }
    ps_home_pga_share_context(settings.role,app==&app_book);
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
    bool station_online = read_pico_connectivity_online();
    bool online = hotspot_active || station_online;
    bool want_hotspot = settings.role == 1 &&
        (settings_net_mode == DUAL_NET_HOTSPOT ||
         (settings_net_mode == DUAL_NET_AUTO && !station_online));
    if (want_hotspot && !hotspot_active && !data_finished && ps_route() == PS_DUAL &&
        ps_now() >= hotspot_retry_at) {
        if (hotspot_start(settings.code)) {
            online = true;
            ps_dirty();
        } else hotspot_retry_at = ps_now() + 10000;
    }
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
    // 手柄机触摸按住时仍必须服务 BLE，否则方向要等抬手后才发出，形成约一秒延迟。
    // Keep servicing BLE while the controller is touched; otherwise the direction waits for lift-off and adds about a second.
    bool game_pad = ps_dual_game_role() == 2;
    if (!game_pad && ((ctx->touch && ctx->touch->touched) || ps_reader_editor_ink_active() || ps_ink_pending()))
        return false;
    esp_err_t ble_result = pico_dual_ble_configure(settings.role, settings.code);
    pico_dual_ble_status_t ble;
    pico_dual_ble_status(&ble);
    // 发现通道先于书库会话启动，空卡或字体待补时仍可找到主机并手动配置。
    // Bring discovery up before the library session so an empty card or missing font cannot hide the primary.
    pair_scan_update();
    if (ble_generation != ble.generation) {
        if (dual_session) dl_control_connected(dual_session, false);
        ble_generation = ble.generation;
        applied = 0;
    }
    if (!dual_session && !start()) return false;
    dl_status_t before = status;
    if (!ble.connected) applied = 0;
    dl_control_connected(dual_session, ble.connected);
    bool data_available = !data_finished || settings_keep_data;
    dl_tick(dual_session, ctx->now_ms, online && data_available);
    (void)dl_face(dual_session, contact_face());
    bool active = app == &app_book && app_book_dual_engaged();
    if(!reading_known){
        reading_known=true;reading_active=active;
        (void)dl_reading_active(dual_session,active);
    }
    if (active && !reading_active) {
        reading_known = true;
        reading_active = true;
        local_away = false;
        (void)dl_reading_active(dual_session, true);
        ps_dirty();
    } else if (!active && reading_known && reading_active) {
        reading_active = false;
        local_away = true;
        (void)dl_remote_open_take(dual_session);
        app_book_dual_pause();
        (void)dl_reading_active(dual_session, false);
        ps_dirty();
    }
    last_work = ctx->now_ms;
    uint8_t packet[DL_CONTROL_MAX];
    for (unsigned i = 0; i < 8; ++i) {
        size_t n = pico_dual_ble_receive(packet, sizeof(packet));
        if (!n) break;
        if(!ps_home_pga_share_receive(packet,n))(void)dl_control_receive(dual_session, packet, n);
    }
    uint8_t home_owner[8]={0};if(settings.role==1)device_id_get(home_owner);else memcpy(home_owner,status.peer_device_id,8);
    ps_home_pga_share_tick(settings.role,ble.connected&&status.peer_id_valid,home_owner,ctx->now_ms,app==&app_book);
    control_send();
    dl_status(dual_session, &status);
    if(!before.connected&&status.connected){progress_rejoin=true;rejoin_revision=status.reading.revision;dl_prepare_open_reset(dual_session);}
    // 首个重连状态可能已是主机合并后的版本；主机布局就绪即可应用，不能再空等下一个版本。
    // The first reconnect state may already be reconciled; apply once primary layout is ready instead of waiting for another revision.
    if(settings.role==2&&progress_rejoin&&
       (status.reading.revision!=rejoin_revision||status.left_ready))progress_rejoin=false;
    (void)peer_cache_update(&status);
    if (settings.role == 2 && status.host[0] && strcmp(settings.host, status.host)) {
        snprintf(settings.host, sizeof(settings.host), "%s", status.host);
        if (!settings_store()) ps_toast("主机地址更新保存失败");
    }
    bool transfer_idle = status.synced && !status.scanning && !status.receiving &&
        !status.sending && !status.request_pending;
    if (!data_finished && transfer_idle) {
        data_finished = true;
        auto_ap_ssid[0] = 0;
        if (!settings_keep_data) {
            if (settings.role == 1) {
                hotspot_stop();
                pico_dual_ble_set_pairing_info("", "");
            }
            primary_ip[0] = 0;
            ps_toast(settings_net_mode == DUAL_NET_HOTSPOT
                         ? "文件同步完成，主机热点已关闭，仅保留蓝牙"
                         : "文件同步完成，数据通道已关闭，仅保留蓝牙");
        }
        if (ps_route() == PS_DUAL && !primary_step && !pair_editing && ps_background_io_allowed())
            auto_shelf_at = ctx->now_ms + 1200;
        ps_dirty();
    }
    if(app==&app_book)auto_shelf_at=0;
    if (auto_shelf_at && app->product_shell && ctx->now_ms >= auto_shelf_at &&
        ps_route() == PS_DUAL && !primary_step && !pair_editing) {
        auto_shelf_at = 0;
        ps_navigate(PS_SHELF);
    }
    bool explicit_remote_open = dl_remote_open_pending(dual_session) &&
        (ps_background_io_blockers() & ~512u) == 0;
    if (explicit_remote_open) {
        local_away = false;
    }
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
         before.verified_bytes / 1048576 != status.verified_bytes / 1048576 ||
         before.received / 1048576 != status.received / 1048576 ||
         (status.expected && (uint64_t)before.received * 20 / status.expected !=
                                 (uint64_t)status.received * 20 / status.expected)))
        ps_dirty();
    reading_checkpoint(!active, ctx->now_ms);
    const char *path = dl_reading_path(dual_session), *font = dl_font_path(dual_session);
    app_book_dual_status_t book_state = {0};
    if (app == &app_book) app_book_dual_status(&book_state);
    if(settings.role==1&&progress_rejoin&&status.synced&&path){
        if(!active)progress_rejoin=false;
        else{
            if(!dl_prepare_open(dual_session,path))return false;
            if(!app_book_dual_reconcile(path))return false;
            progress_rejoin=false;
            return false;
        }
    }
    bool local_solo = app == &app_book && book_state.solo && !status.peer_reading;
    bool automatic_route = app == &app_book ||
        (app->product_shell && idle_reading_route() && ps_route() != PS_DUAL);
    bool explicit_idle = explicit_remote_open;
    if ((ui_idle || explicit_idle) && !(settings.role==2&&progress_rejoin&&status.peer_reading&&!explicit_remote_open) && (!local_solo||explicit_remote_open||settings.role==1) && status.connected && status.synced && !local_away &&
        (settings.role == 1 || status.left_ready) &&
        path && font && status.reading.revision != applied &&
        (explicit_remote_open || automatic_route)) {
        if (app != &app_book && !ps_games_prepare_storage()) return false;
        if (app_book_dual_apply(path, font, status.reading.spread + (settings.role == 2 ? 2 : 1),
                status.reading.font_px, status.reading.reflow_pending, status.reading.revision)) {
            applied = status.reading.revision;
            if(explicit_remote_open)(void)dl_remote_open_take(dual_session);
            ps_power_activity(ctx->now_ms);
            if (app != &app_book) ps_open_legacy(ctx, &app_book);
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
static const char* dual_net_label(void) {
    if (settings_net_mode == DUAL_NET_LAN) return "同一局域网";
    if (settings_net_mode == DUAL_NET_HOTSPOT) return "主机热点";
    return "自动选择";
}
static const char* dual_sync_title(void) {
    if (status.synced && status.fast_verified) return "书库已同步 · 快速校验";
    if (status.synced) return "书库已同步";
    if (status.error) return "同步遇到问题";
    if (status.scanning) return "正在校验本机文件";
    if (!status.connected) return "等待另一台设备";
    if (!status.peer_listed) return "正在读取对端清单";
    return "正在补齐双方缺少的文件";
}
static const char* dual_sync_error(void) {
    const char* error = status.error;
    if (!error) return "";
    if (strstr(error, "pairing code") || strstr(error, "protocol")) return "配对码不一致，或两台固件版本不同";
    if (strstr(error, "primary unavailable") || strstr(error, "peer timeout")) return "主机数据通道暂不可达，请检查 Wi-Fi / 热点";
    if (strstr(error, "font") || strstr(error, "book directory") || strstr(error, "book changed")) return "TF 卡文件不可读或校验中断";
    if (strstr(error, "library limit")) return "文件数量超过 512 项上限";
    if (strstr(error, "listen")) return "主机无法建立数据通道，请重试";
    return "同步失败，请检查网络、TF 卡和配对码";
}
static unsigned dual_transfer_percent(void) {
    if (status.synced) return 100;
    if (!status.expected || status.received > status.expected) return 0;
    return (unsigned)((uint64_t)status.received * 100 / status.expected);
}
// 向导绘制与命中共用几何，横屏保留所有出口。/ Share wizard geometry and keep all exits visible in landscape.
static EpdRect dual_setup_choice(int row) {
    ps_view_t v=ps_view();
    if(v.landscape)return (EpdRect){v.w/2+8,168+row*54,v.w/2-28,44};
    int y=primary_step==1?238:268;
    if(row==2)y=primary_step==1?386:438;else y+=row*58;
    return (EpdRect){20,y,v.w-40,row==2?40:44};
}
static EpdRect dual_pair_choice(int row) {
    ps_view_t v=ps_view();
    if(v.landscape)return (EpdRect){v.w/2+8,168+row*54,v.w/2-28,44};
    int y=pair_failed?280+row*48:pair_found?292+row*46:pair_scanning?292+row*48:250+row*50;
    return (EpdRect){20,y,v.w-40,42};
}
static bool dual_hit(const ps_event_t *e,EpdRect r) {
    return ps_hit(e->x,e->y,r.x,r.y,r.width,r.height);
}
static EpdRect dual_status_rect(void) {
    ps_view_t v=ps_view();int w=v.w-40;
    return v.landscape?(EpdRect){20,154,(w-12)/2,150}:
        (EpdRect){20,154,w,230};
}
static EpdRect dual_controls_rect(void) {
    ps_view_t v=ps_view();int w=v.w-40;
    return v.landscape?(EpdRect){32+(w-12)/2,154,(w-12)/2,150}:
        (EpdRect){20,394,w,158};
}
static EpdRect dual_control_rect(int row,int column) {
    ps_view_t v=ps_view();EpdRect card=dual_controls_rect();
    if(row==2)return (EpdRect){card.x+16,card.y+(v.landscape?106:118),card.width-32,34};
    int gap=8,half=(card.width-40)/2;
    return (EpdRect){card.x+16+column*(half+gap),card.y+(v.landscape?(row?68:30):(row?78:34)),half,34};
}
static int dual_actions_y(void) {
    return ps_view().landscape?312:562;
}
static void dual_status_render(uint8_t *fb) {
    ps_view_t v=ps_view();EpdRect r=dual_status_rect();char text[128];
    ps_surface(fb,r.x,r.y,r.width,r.height,false);
    const char *role=settings.role==1?"主机":"从机";
    int role_w=settings.role==1?52:52;
    ps_box(fb,r.x+16,r.y+14,role_w,26,8,settings.role==1?1:13,true);
    ps_center(fb,r.x+16+role_w/2,r.y+19,10,role,
              settings.role==1?PS_THEME_ON_SELECTED:PS_THEME_INK);
    const char *state=status.connected?(status.synced?"已同步":"已连接"):"等待连接";
    ps_text(fb,r.x+r.width-88,r.y+18,10,state,status.connected?3:5);
    ps_text(fb,r.x+16,r.y+52,14,dual_sync_title(),status.synced?3:status.error?5:1);
    if(status.error && !status.synced)ps_wrap(fb,r.x+16,r.y+75,r.width-32,9,1,dual_sync_error(),5);
    else if(status.synced){
        if(status.fast_verified)
            snprintf(text,sizeof(text),"%s · 快速校验 · %u 项",
                     same_peer?"上次设备与目录一致":"目录摘要一致",status.books);
        else
            snprintf(text,sizeof(text),"本机 %u 项 · 对端 %u 项 · 补入 %u 项",
                     status.books,status.peer_books,status.imported);
        ps_wrap(fb,r.x+16,r.y+75,r.width-32,9,1,text,4);
    }else if(status.scanning){
        snprintf(text,sizeof(text),"已读取 %u KB · 已确认 %u 项",
                 (unsigned)(status.verified_bytes/1024),status.books);
        ps_wrap(fb,r.x+16,r.y+75,r.width-32,9,1,text,4);
    }else if(status.expected){
        snprintf(text,sizeof(text),"已接收 %u / %u KB · 完成后列表一致",
                 (unsigned)(status.received/1024),(unsigned)((status.expected+1023u)/1024u));
        ps_wrap(fb,r.x+16,r.y+75,r.width-32,9,1,text,4);
    }else{
        snprintf(text,sizeof(text),"本机 %u 项 · 对端 %u 项",status.books,status.peer_books);
        ps_text(fb,r.x+16,r.y+75,9,text,4);
    }
    unsigned percent=dual_transfer_percent();
    int bar_y=r.y+(v.landscape?96:100);
    ps_box(fb,r.x+16,bar_y,r.width-32,10,5,12,false);
    if(percent)ps_box(fb,r.x+18,bar_y+2,(r.width-36)*percent/100,6,3,1,true);
    if(status.synced)snprintf(text,sizeof(text),"100%% · 已完成");
    else if(status.expected)snprintf(text,sizeof(text),"本文件 %u%% · 已补入 %u 项",percent,status.imported);
    else snprintf(text,sizeof(text),"%s",status.scanning?"校验中":"等待同步");
    if(!v.landscape)ps_text(fb,r.x+16,bar_y+16,10,text,status.synced?3:5);
    snprintf(text,sizeof(text),"%s",settings.role==1?(primary_ip[0]?primary_ip:"等待 Wi-Fi"):
             (settings.host[0]?settings.host:"等待主机地址"));
    if(v.landscape){
        char line[96];
        snprintf(line,sizeof(line),"%u%% · %.80s",percent,text);
        ps_wrap(fb,r.x+16,r.y+112,r.width-32,10,1,line,1);
        snprintf(line,sizeof(line),"%s · 配对码 %s",dual_net_label(),settings.code);
        ps_wrap(fb,r.x+16,r.y+132,r.width-32,9,1,line,5);
    }else{
        ps_text(fb,r.x+16,r.y+136,9,"地址",5);ps_text_fixed(fb,r.x+58,r.y+136,10,text,1);
        ps_text(fb,r.x+16,r.y+158,9,"配对码",5);ps_text_fixed(fb,r.x+58,r.y+158,10,settings.code,1);
        snprintf(text,sizeof(text),"网络：%s",dual_net_label());
        ps_text(fb,r.x+16,r.y+182,9,text,4);
        const char *data_state=data_finished&&!settings_keep_data?"仅蓝牙":
            status.data_connected?"数据已连接":hotspot_active?"热点已开启":"数据等待";
        snprintf(text,sizeof(text),"数据：%s · 蓝牙：%s",data_state,
                 status.connected?"蓝牙已连接":"蓝牙等待");
        ps_wrap(fb,r.x+16,r.y+202,r.width-32,9,1,text,status.connected?3:5);
    }
}
static void dual_controls_render(uint8_t *fb) {
    EpdRect r=dual_controls_rect();
    ps_surface(fb,r.x,r.y,r.width,r.height,false);
    ps_text(fb,r.x+16,r.y+10,13,"显示与控制",1);
    bool primary_flip=orientation.primary_flip,secondary_flip=orientation.secondary_flip;
    bool guard=ps_touch_guard_enabled(),locked=ps_dual_rotation_locked(),calibrated=ps_dual_calibrated();
    EpdRect a=dual_control_rect(0,0),b=dual_control_rect(0,1);
    ps_toggle(fb,a.x,a.y,a.width,a.height,"主机翻转",primary_flip,true);
    ps_toggle(fb,b.x,b.y,b.width,b.height,"从机翻转",secondary_flip,true);
    a=dual_control_rect(1,0);b=dual_control_rect(1,1);
    ps_toggle(fb,a.x,a.y,a.width,a.height,"贴合防误触",guard,true);
    ps_toggle(fb,b.x,b.y,b.width,b.height,"方向锁定",locked,true);
    EpdRect c=dual_control_rect(2,0);
    ps_button(fb,c.x,c.y,c.width,c.height,
              calibrated?"重新校准开合姿态":"校准开合姿态",!calibrated);
}
void ps_dual_render(uint8_t* fb) {
    ps_view_t v = ps_view();
    ps_heading(fb, "双机阅读 · 实验版", "两台小纸 Pico，共同打开一本书");
    int x = 20, w = v.w - 40;
    ps_button(fb, x, 104, (w - 16) / 3, 40, "关闭", settings.role == 0 && !primary_step && !pair_editing);
    ps_button(fb, x + (w - 16) / 3 + 8, 104, (w - 16) / 3, 40, primary_step?"主机 · 设置中":"主机", settings.role == 1 || primary_step);
    ps_button(fb, x + 2 * ((w - 16) / 3 + 8), 104, (w - 16) / 3, 40, pair_editing?"从机 · 配对中":"从机", settings.role == 2 || pair_editing);
    if (primary_step) {
        int tw=v.landscape?v.w/2-40:w;
        ps_text(fb,x,166,16,primary_step==1?"设置主机 · 同步方式 1/2":"设置主机 · 同步后连接 2/2",1);
        const char *info=primary_step==1?(read_pico_connectivity_online()?"当前已连接局域网":"当前未连接局域网"):
            (primary_net_draft==DUAL_NET_HOTSPOT?"当前选择：主机热点":"当前选择：同一局域网");
        ps_wrap(fb,x,198,tw,12,1,info,4);
        if(primary_step==2)ps_wrap(fb,x,230,tw,11,v.landscape?3:1,
            primary_net_draft==DUAL_NET_HOTSPOT?"仅蓝牙会关闭热点，双页阅读和翻页继续":"仅蓝牙会停止文件传输通道，双页阅读和翻页继续",5);
        else if(v.landscape)ps_wrap(fb,x,234,tw,12,3,"热点无需路由器。两台靠近即可传输图书和字体。",5);
        const char *labels[2]={primary_step==1?"使用同一局域网同步":"保持数据连接",
                               primary_step==1?"由主机创建直连热点":"仅保留蓝牙交互"};
        for(int i=0;i<2;i++){EpdRect r=dual_setup_choice(i);ps_button(fb,r.x,r.y,r.width,r.height,labels[i],false);}
        EpdRect back=dual_setup_choice(2);
        ps_button(fb,back.x,back.y,back.width,back.height,primary_step==1?"取消":"返回上一步",false);
        if(!v.landscape)ps_wrap(fb,x,primary_step==1?348:378,w,11,2,
            primary_step==1?"热点无需路由器；两台靠近即可同步。":"保持连接便于后续补书；仅蓝牙更省电。之后可在同步设置中修改。",5);
        return;
    }
    if (pair_editing || !settings.role) {
        int tw=v.landscape?v.w/2-40:w;
        const char *title=pair_failed?"未找到主机":pair_found?"发现主机":pair_scanning?"正在搜索主机…":"选择从机连接方式";
        ps_text(fb,x,170,16,title,1);
        const char *detail=pair_failed?"确认主机已开启蓝牙并选择主机，然后重新搜索。":
            pair_found?"输入主机屏幕上的六位配对码，自动完成网络连接。":
            pair_scanning?"请让主机停留在双机阅读页。优先通过蓝牙发现局域网或热点。":
            "支持同一局域网和主机直连热点，无需路由器。";
        ps_wrap(fb,x,202,tw,12,v.landscape?4:2,detail,5);
        const char *labels[3];int count=3;
        if(pair_failed){labels[0]="重新搜索";labels[1]="手动输入主机地址";labels[2]="取消";}
        else if(pair_found){labels[0]="输入配对码";labels[1]="重新搜索";labels[2]="取消";}
        else if(pair_scanning){labels[0]="取消搜索";labels[1]="手动输入主机地址";count=2;}
        else{labels[0]="自动搜索主机";labels[1]="手动输入主机地址";labels[2]="打开 Wi-Fi 设置";}
        for(int i=0;i<count;i++){EpdRect r=dual_pair_choice(i);ps_button(fb,r.x,r.y,r.width,r.height,labels[i],i==0&&!pair_scanning);}
        return;
    }
    dual_status_render(fb);
    dual_controls_render(fb);
    int bottom=dual_actions_y(),bw=(w-16)/3;
    ps_button(fb,x,bottom,bw,36,"重新同步",false);
    ps_button(fb,x+bw+8,bottom,bw,36,settings.role==1?"同步设置":"修改配对",true);
    ps_button(fb,x+2*(bw+8),bottom,bw,36,"打开书架",false);
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
static void pair_edit_begin(void) {
    pair_stop();
    pair_found = pair_failed = false;
    pair_editing = true;
    pair_scan_start();
}
static void primary_setup_begin(void) {
    pair_stop();
    primary_step = 1;
    primary_net_draft = settings.role == 1 ? settings_net_mode : DUAL_NET_LAN;
    primary_keep_draft = settings.role == 1 ? settings_keep_data : 1;
    ps_dirty();
}
static bool primary_setup_finish(void) {
    char code[7];
    if (settings.role == 1 && strlen(settings.code) == 6)
        snprintf(code, sizeof(code), "%s", settings.code);
    else
        snprintf(code, sizeof(code), "%06u", (unsigned)(esp_random() % 1000000));
    if (!configure_full(1, "", code, primary_net_draft, primary_keep_draft)) return false;
    primary_step = 0;
    ps_navigate(PS_DUAL);
    return true;
}
static void pair_scan_start(void) {
    load();
    pair_stop();
    pair_editing = true;
    pair_found = false;
    pair_failed = false;
    pair_ip[0] = pair_code[0] = pair_ap_ssid[0] = 0;
    pair_sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (pair_sock >= 0) {
        int yes = 1;
        setsockopt(pair_sock, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
        struct sockaddr_in addr = { .sin_family = AF_INET, .sin_port = htons(18772), .sin_addr.s_addr = htonl(INADDR_ANY) };
        if (bind(pair_sock, (struct sockaddr*)&addr, sizeof(addr)) != 0) {
            close(pair_sock); pair_sock = -1;
        } else {
            int flags = fcntl(pair_sock, F_GETFL, 0);
            fcntl(pair_sock, F_SETFL, flags | O_NONBLOCK);
        }
    }
    pair_scanning = true;
    pair_started = ps_now();
    (void)pico_dual_ble_scan_start();
    ps_toast("正在搜索主机；无局域网时会使用直连热点");
    ps_dirty();
}
#ifdef PICO_NATIVE_PREVIEW
void ps_dual_native_scan_start(void) { pair_edit_begin(); }
#endif
static void pair_scan_update(void) {
    load();
    int64_t now = ps_now();
    if (settings.role == 1) {
        // 不依赖双机页或校验完成；只要 Wi-Fi 有地址就持续广播。
        // Never wait for sync/verification or the dual page; broadcast whenever Wi-Fi has an address.
        if (!primary_ip[0] || now - last_ip >= 1000) {
            char ip[48];
            if (hotspot_active) snprintf(primary_ip, sizeof(primary_ip), "192.168.4.1");
            else if (local_address(ip)) snprintf(primary_ip, sizeof(primary_ip), "%s", ip);
            else primary_ip[0] = 0;
            last_ip = now;
        }
        if (primary_ip[0]) pico_dual_ble_set_pairing_info(primary_ip, hotspot_active ? hotspot_ssid : "");
#ifndef PICO_NATIVE_PREVIEW
        if (primary_ip[0] && dual_session) dl_endpoint(dual_session, primary_ip);
#endif
        if (primary_ip[0] && !hotspot_active && now - pair_last_tx >= 500) {
            pair_last_tx = now;
            int s = socket(AF_INET, SOCK_DGRAM, 0);
            if (s >= 0) {
                int yes = 1;
                setsockopt(s, SOL_SOCKET, SO_BROADCAST, &yes, sizeof(yes));
                char msg[80];
                int n = snprintf(msg, sizeof(msg), "PICOPAIR2 %s -", primary_ip);
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
    pico_dual_ble_peer_info_t ble;
    pico_dual_ble_peer_info(&ble);
    if (settings.role == 2 && !pair_editing && !data_finished && !status.synced &&
        strlen(settings.code) == 6 &&
        ble.found && ble.direct && ble.ap_ssid[0]) {
        read_pico_connectivity_link_t link;
        read_pico_connectivity_get_link(&link);
        bool needed = !link.online || strcmp(link.ssid, ble.ap_ssid);
        bool due = strcmp(auto_ap_ssid, ble.ap_ssid) || now >= auto_ap_retry_at;
        if (needed && due) {
            char password[33];
            snprintf(auto_ap_ssid, sizeof(auto_ap_ssid), "%s", ble.ap_ssid);
            auto_ap_retry_at = now + 8000;
            direct_password(settings.code, ble.ap_ssid, password);
            if (read_pico_connectivity_connect(ble.ap_ssid, password) == ESP_OK)
                ps_toast("已发现主机热点，正在自动连接");
            else
                ps_toast("发现主机热点，但自动连接未启动");
        }
    }
    if (!pair_scanning) return;
    if (ble.found && ble.ip[0]) {
        snprintf(pair_ip, sizeof(pair_ip), "%s", ble.ip);
        snprintf(pair_ap_ssid, sizeof(pair_ap_ssid), "%s", ble.ap_ssid);
        pair_code[0] = 0;
        pair_found = true;
        pair_stop();
        if (pair_editing) {
            snprintf(draft_host, sizeof(draft_host), "%s", pair_ip);
            snprintf(draft_ap_ssid, sizeof(draft_ap_ssid), "%s", pair_ap_ssid);
            draft_short = false;
            pair_found = false;
            ps_toast(pair_ap_ssid[0] ? "已发现主机热点，请输入六位配对码"
                                     : "已发现同一局域网主机，请输入六位配对码");
            edit_code("");
        } else ps_dirty();
        return;
    }
    char buf[96];
    for (int i = 0; pair_sock >= 0 && i < 4; i++) {
        struct sockaddr_in from;
        socklen_t len = sizeof(from);
        int n = (int)recvfrom(pair_sock, buf, sizeof(buf) - 1, 0, (struct sockaddr*)&from, &len);
        if (n <= 0) break;
        buf[n] = 0;
        char ip[48] = {0}, ap[33] = {0};
        unsigned a, b, c, d;
        if (sscanf(buf, "PICOPAIR2 %47s %32s", ip, ap) == 2 &&
            sscanf(ip, "%u.%u.%u.%u", &a, &b, &c, &d) == 4) {
            snprintf(pair_ip, sizeof(pair_ip), "%s", ip);
            snprintf(pair_ap_ssid, sizeof(pair_ap_ssid), "%s", strcmp(ap, "-") ? ap : "");
            pair_code[0] = 0;
            pair_found = true;
            // 局域网先发现也须退出 BLE 仅搜索模式，否则确认配对后仍无法连接。
            // UDP-first discovery must also leave BLE scan-only mode before pairing can connect.
            pair_stop();
            if (pair_editing) {
                snprintf(draft_host, sizeof(draft_host), "%s", pair_ip);
                snprintf(draft_ap_ssid, sizeof(draft_ap_ssid), "%s", pair_ap_ssid);
                draft_short = false;
                pair_found = false;
                ps_toast(pair_ap_ssid[0] ? "已发现主机热点，请输入六位配对码"
                                         : "已发现同一局域网主机，请输入六位配对码");
                edit_code("");
            } else ps_dirty();
            break;
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
    draft_ap_ssid[0] = 0;
    edit_host("");
}
static void secondary_host(const char* input) {
    char local[48], candidate[48];
    unsigned parts[4];
    snprintf(draft_input, sizeof(draft_input), "%s", input ? input : "");
    draft_ap_ssid[0] = 0;
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
    bool direct = draft_ap_ssid[0] != 0;
    if (!direct && !local_address(local)) {edit_code(code);ps_toast("请重新搜索主机或连接同一 Wi-Fi");return;}
    if (!direct && draft_short && strcmp(local, draft_local_ip)) {
        snprintf(draft_local_ip, sizeof(draft_local_ip), "%s", local);
        edit_host(draft_input);ps_toast("本机地址已变化，请重新核对主机");return;
    }
    if (!ps_dual_configure(2, draft_host, code)) {
        edit_code(code);ps_toast("配置未保存，请检查 TF 卡是否可写");return;
    }
    if (direct) {
        char password[33];
        direct_password(code, draft_ap_ssid, password);
        if (read_pico_connectivity_connect(draft_ap_ssid, password) == ESP_OK)
            ps_toast("配置已保存，正在连接主机直连热点");
        else
            ps_toast("配置已保存；直连热点启动失败，请重试");
    } else {
        ps_toast("配置已保存，正在连接主机");
    }
}
bool ps_dual_event(const ps_event_t* e) {
    if(!primary_step&&!pair_editing&&settings.role&&(e->type==PS_SWIPE_L||e->type==PS_SWIPE_R)){
        for(int i=0;i<4;i++){
            EpdRect r=dual_control_rect(i/2,i%2);bool next;
            bool on=i==0?orientation.primary_flip:i==1?orientation.secondary_flip:i==2?ps_touch_guard_enabled():ps_dual_rotation_locked();
            if(ps_switch_swipe(e,r.x+r.width-54,r.y,54,r.height,&next)){
                if(next==on)return true;
                ps_event_t tap=*e;tap.type=PS_TAP;tap.x=r.x+r.width/2;tap.y=r.y+r.height/2;
                return ps_dual_event(&tap);
            }
        }
    }
    if (e->type != PS_TAP) return false;
    ps_view_t v = ps_view();
    int x = 20, w = v.w - 40;
    bool pairing = pair_editing || !settings.role;
    if (e->y >= 104 && e->y < 144) {
        int role = (e->x - x) / ((w - 16) / 3 + 8);
        if (role == 0) {
            primary_step = 0;
            ps_dual_configure(0, "", "");
        } else if (role == 1) {
            if (settings.role == 1) return true;
            primary_setup_begin();
        } else if (role == 2) {
            primary_step = 0;
            pair_edit_begin();
        }
        return true;
    }
    if (primary_step) {
        if (primary_step == 1) {
            if (dual_hit(e,dual_setup_choice(0))) {
                char lan[48];
                if (!local_address(lan)) {
                    ps_toast("请先连接同一局域网 Wi-Fi，或改用主机热点");
                    return true;
                }
                primary_net_draft = DUAL_NET_LAN;
                primary_step = 2;
                ps_dirty();
            } else if (dual_hit(e,dual_setup_choice(1))) {
                primary_net_draft = DUAL_NET_HOTSPOT;
                primary_step = 2;
                ps_dirty();
            } else if (dual_hit(e,dual_setup_choice(2))) {
                primary_step = 0;
                ps_dirty();
            }
            return true;
        }
        if (dual_hit(e,dual_setup_choice(0))) {
            primary_keep_draft = 1;
            if (!primary_setup_finish()) ps_toast("双机设置保存失败");
        } else if (dual_hit(e,dual_setup_choice(1))) {
            primary_keep_draft = 0;
            if (!primary_setup_finish()) ps_toast("双机设置保存失败");
        } else if (dual_hit(e,dual_setup_choice(2))) {
            primary_step = 1;
            ps_dirty();
        }
        return true;
    }
    if (pairing && (pair_scanning || pair_found || pair_failed)) {
        if (pair_failed) {
            if (dual_hit(e,dual_pair_choice(0))) { pair_scan_start(); return true; }
            if (dual_hit(e,dual_pair_choice(1))) { pair_stop(); pair_failed = false; secondary_begin(); return true; }
            if (dual_hit(e,dual_pair_choice(2))) { pair_stop(); pair_editing = false; ps_dirty(); return true; }
            return true;
        }
        if (pair_found && dual_hit(e,dual_pair_choice(0))) {
            snprintf(draft_host, sizeof(draft_host), "%s", pair_ip);
            snprintf(draft_ap_ssid, sizeof(draft_ap_ssid), "%s", pair_ap_ssid);
            draft_short = false;
            pair_found = false;
            edit_code("");
            return true;
        }
        if (pair_found && dual_hit(e,dual_pair_choice(1))) { pair_scan_start(); return true; }
        if (pair_found && dual_hit(e,dual_pair_choice(2))) { pair_stop(); pair_editing = false; ps_dirty(); return true; }
        if (!pair_found && dual_hit(e,dual_pair_choice(0))) { pair_stop(); pair_editing = false; ps_dirty(); return true; }
        if (!pair_found && dual_hit(e,dual_pair_choice(1))) { pair_stop(); secondary_begin(); return true; }
        return true;
    }
    if (pairing && dual_hit(e,dual_pair_choice(0))) {
        pair_scan_start();
        return true;
    }
    if (pairing && dual_hit(e,dual_pair_choice(1))) {
        pair_failed = false;
        secondary_begin();
        return true;
    }
    if (pairing && dual_hit(e,dual_pair_choice(2))) {
        ps_navigate(PS_WIFI);
        return true;
    }
    for(int role=1;role<=2;role++){
        EpdRect flip=dual_control_rect(0,role-1);
        if(ps_hit(e->x,e->y,flip.x,flip.y,flip.width,flip.height)){
            bool on=role==1?orientation.primary_flip:orientation.secondary_flip;
            if(!ps_dual_flip(role,!on))ps_toast("请先连接主机再调整方向");
            return true;
        }
    }
    EpdRect guard=dual_control_rect(1,0);
    if(ps_hit(e->x,e->y,guard.x,guard.y,guard.width,guard.height)){
        bool on=ps_touch_guard_enabled();
        if(ps_touch_guard_set(!on))ps_toast(on?"已关闭贴合防误触":"已开启贴合防误触");
        return true;
    }
    EpdRect lock=dual_control_rect(1,1);
    if(ps_hit(e->x,e->y,lock.x,lock.y,lock.width,lock.height)){
        bool on=ps_dual_rotation_locked();
        if(ps_dual_lock_set(!on))ps_toast(on?"已解除方向锁定":"已锁定当前方向");
        return true;
    }
    EpdRect calibrate=dual_control_rect(2,0);
    if(ps_hit(e->x,e->y,calibrate.x,calibrate.y,calibrate.width,calibrate.height)){
        (void)ps_dual_calibrate();
        return true;
    }
    int bottom=dual_actions_y(),bw=(w-16)/3;
    if(ps_hit(e->x,e->y,x,bottom,w,36)){
        if(e->x<x+bw){
            data_finished = false;
            if (dual_session) dl_rescan(dual_session);
            else start();
            ps_dirty();
        }else if(e->x<x+2*(bw+8)){
            if (settings.role == 1)
                primary_setup_begin();
            else
                pair_edit_begin();
        }else{
            ps_navigate(PS_SHELF);
        }
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
    bool radio = !strcmp(line, "RPICO DUAL RADIO");
    if (!primary && !item && !open && !next && !previous && !shelf && !rescan && !rotate && !lock && !guard && !calibrate && !font && !radio && strcmp(line, "RPICO DUAL STATUS")) return false;
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
    if (radio) {
        snprintf(out,capacity,"{\"radio\":true,\"connected\":%s,\"generation\":%u,\"error\":%d,\"rx_dropped\":%u,\"tx_retries\":%u}",
            ble.connected?"true":"false",(unsigned)ble.generation,ble.error,(unsigned)ble.rx_dropped,(unsigned)ble.tx_retries);
        return true;
    }
    app_book_dual_status_t book; app_book_dual_status(&book);
    char book_id[65];
    for (unsigned i=0; i<32; ++i) snprintf(book_id+2*i,3,"%02x",state.reading.book[i]);
    snprintf(
        out, capacity,
        "{\"dual\":true,\"accepted\":%s,\"role\":%u,\"connected\":%s,\"synced\":%s,\"scanning\":%s,\"request_pending\":%s,"
        "\"books\":%u,\"peer_books\":%u,\"spread\":%u,\"data_connected\":%s,"
        "\"ble_active\":%s,\"ble_error\":%d,\"error\":%s,"
        "\"ui_block\":%u,\"tick_age\":%u,\"work_age\":%u,\"busy\":%s,\"received\":%u,\"expected\":%u,\"sent\":%u,\"receiving\":%s,\"sending\":%s,"
        "\"reused\":%u,\"verified_bytes\":%u,\"resumed\":%u,"
        "\"listed\":%s,\"peer_listed\":%s,\"done\":%s,\"peer_done\":%s,\"catalog_valid\":%s,\"peer_catalog_valid\":%s,"
        "\"revision\":%u,\"applied\":%u,\"presented\":%u,\"left_ready\":%s,\"rotation\":%d,\"rotation_locked\":%s,\"dual_locked\":%s,\"guard\":%s,\"face\":%d,\"peer_face\":%d,\"contact\":%s,\"calibrated\":%s,\"base_seq\":%u,\"base_valid\":%s,\"cal_request\":%s,\"peer_reading\":%s,\"orientation\":%u,"
        "\"font_px\":%u,\"book_font_px\":%u,\"reflow_pending\":%s,\"reading_open\":%s,\"target\":%u,\"page\":%u,\"blank\":%s,\"chapter\":%u,\"start\":%u,\"end\":%u,\"book\":\"%s\"}",
        accepted ? "true" : "false", settings.role, state.connected ? "true" : "false",
        state.synced ? "true" : "false", state.scanning ? "true" : "false", state.request_pending ? "true" : "false", state.books,
        state.peer_books, (unsigned)state.reading.spread, state.data_connected ? "true" : "false",
        ble.active ? "true" : "false", ble.error, state.error && !state.synced ? "true" : "false",
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

bool ps_dual_prepare_open(const char* path){return dl_prepare_open(dual_session,path);}
void ps_dual_prepare_open_reset(void){dl_prepare_open_reset(dual_session);}

bool ps_dual_reconcile_at(uint32_t page){return dl_reconcile_at(dual_session,page);}

/* SPDX-License-Identifier: Apache-2.0
 * 中文：直接运行薄宿主UI与语言服务，仅替换绘制、门户和应用装载的外部环境。
 * English: Exercise the actual thin-host UI and locale service; replace only drawing, portal and app-loader surroundings.
 */
#include "pico_system.h"
#include "read_pico_home_link.h"
#include "pico_home_portal.h"
#include "pico_i18n.h"
#include "ui_wifi_qr.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

// ROM解压用zlib适配，执行生产目录解压器与真实i18n，不替换按钮命中逻辑。
// Adapt ROM inflation through zlib, keeping the production catalog decoder, i18n, and button hit logic.
#include "miniz.h"
static unsigned long mz_crc32(unsigned long crc, const unsigned char *data, size_t bytes) {
    return crc32(crc, data, (uInt)bytes);
}
#define MZ_CRC32_INIT 0
static void thin_test_log(const char *tag, const char *format, ...) { assert(tag && format); }
#include "../main/ui/ui_packed_asset.c"

enum { MUTED = 5 };
#ifndef PICO_THIN_UI_SOURCE
#define PICO_THIN_UI_SOURCE "../main/system/pico_home_link_ui_thin.inc"
#endif
#include PICO_THIN_UI_SOURCE

static ps_view_t view = {360, 640, false};
static ps_home_portal_view_t portal;
static unsigned stops, qr_clears, locale_stores, open_count, draws;
static bool package_available, locale_saved;
static uint8_t locale;
static uint8_t entry;

ps_view_t ps_view(void) { return view; }
bool ps_load(const char *key, void *out, size_t bytes) {
    assert(!strcmp(key, "uilocale") && bytes == 1);
    if (!locale_saved) return false;
    *(uint8_t *)out = locale;
    return true;
}
bool ps_store(const char *key, const void *data, size_t bytes) {
    assert(!strcmp(key, "uilocale") && bytes == 1);
    locale = *(const uint8_t *)data;
    locale_saved = true;
    locale_stores++;
    return true;
}
bool ps_hit(int x, int y, int rx, int ry, int w, int h) {
    return x >= rx && y >= ry && x < rx + w && y < ry + h;
}
void ps_toast(const char *text) { assert(text && *text); }
void ps_apps_prepare_home(uint8_t page) { entry = page; }
void ps_apps_expect_home_route(ps_route_t route) { (void)route; }
bool ps_apps_open(const char *id) { assert(!strcmp(id, "home-link")); open_count++; return package_available; }
bool ps_home_portal_start(int owner) {
    assert(owner == PS_HOME_PORTAL_OWNER_HOME);
    portal.active = true;
    snprintf(portal.url, sizeof(portal.url), "http://pico.local/lan#key=test-only");
    return true;
}
void ps_home_portal_stop(void) { stops++; portal.active = false; }
void ps_home_portal_view(ps_home_portal_view_t *out) { *out = portal; }
bool ui_wifi_qr_prepare_url(const char *url) { return url && strstr(url, "/lan#key="); }
void ui_wifi_qr_clear(void) { qr_clears++; }
void ui_wifi_qr_draw(uint8_t *fb, EpdRect area) { (void)fb; assert(area.width > 0 && area.width == area.height); draws++; }
EpdRect ps_rect(int x, int y, int w, int h) { return (EpdRect){x, y, w, h}; }
void ps_fill(uint8_t *fb, int x, int y, int w, int h, int gray) {
    (void)fb;
    assert(x >= 0 && y >= 0 && x + w <= view.w && y + h <= view.h && gray >= 0 && gray <= 15);
    draws++;
}
void ps_heading(uint8_t *fb, const char *title, const char *subtitle) { (void)fb; (void)subtitle; assert(title && *title); draws++; }
void ps_button(uint8_t *fb, int x, int y, int w, int h, const char *label, bool selected) {
    (void)selected;
    assert(label && *label);
    ps_fill(fb, x, y, w, h, 15);
}
void ps_wrap(uint8_t *fb, int x, int y, int w, int size, int lines, const char *text, int gray) {
    (void)size; (void)lines;
    assert(text && *text);
    ps_fill(fb, x, y, w, 1, gray);
}

static bool tap(int x, int y) { return real_ha_event(&(ps_event_t){.type = PS_TAP, .x = x, .y = y}); }

static void portal_contract(void) {
    for (int landscape = 0; landscape < 2; landscape++) {
        view = landscape ? (ps_view_t){640, 360, true} : (ps_view_t){360, 640, false};
        ps_i18n_init();
        thin_portal = false;
        real_ha_render(NULL);
        unsigned before = locale_stores;
        assert(tap(view.w - 40, 40) && locale_stores == before + 1);
        assert(tap(40, view.h - 32) && thin_portal && portal.active);
        real_ha_render(NULL);
        ps_locale_t old = ps_locale();
        before = locale_stores;
        unsigned closed = stops;
        unsigned cleared = qr_clears;
        (void)tap(view.w - 40, 40);
        assert(ps_locale() == old && locale_stores == before);
        assert(thin_portal && portal.active && stops == closed && qr_clears == cleared);
        assert(tap(40, 36));
        assert(!thin_portal && !portal.active && stops == closed + 1 && qr_clears == cleared + 1);
        real_ha_settings_render(NULL);
        assert(thin_open_portal() && thin_portal_back() && !thin_portal);
        assert(!thin_portal_back());
        package_available = false;
        assert(real_ha_event(&(ps_event_t){.type = PS_TAP, .x = 40, .y = view.h - 80}));
        assert(entry == 0xff);
        package_available = true;
        assert(real_ha_settings_event(&(ps_event_t){.type = PS_TAP, .x = 40, .y = view.h - 80}));
        assert(entry == THIN_HOME_SETUP);
        assert(real_ha_event(&(ps_event_t){.type = PS_TAP, .x = 40, .y = view.h - 80}));
        assert(entry == THIN_HOME_MONITOR);
        real_printer_render(NULL, true);
        real_printer_render(NULL, false);
        assert(real_printer_event(&(ps_event_t){.type = PS_TAP, .x = 40, .y = view.h - 80}, true));
        assert(entry == THIN_HOME_PRINTERS);
        assert(real_printer_event(&(ps_event_t){.type = PS_TAP, .x = 40, .y = view.h - 80}, false));
        assert(entry == THIN_HOME_PRINTERS);
        package_available = false;
        assert(real_ha_event(&(ps_event_t){.type = PS_TAP, .x = 40, .y = view.h - 80}));
        assert(entry == 0xff);
        assert(!real_ha_event(NULL) && !real_ha_event(&(ps_event_t){.type = PS_MOVE}));
    }
}

int main(void) {
    portal_contract();
    assert(open_count == 12 && draws);
    puts("thin UI: visible chip toggles, hidden chip is inert, visible close/back release portal, TF setup links passed");
    return 0;
}

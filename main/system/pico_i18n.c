/*
 * SPDX-License-Identifier: Apache-2.0
 * 中文：紧凑静态表。冷路径线性查找，不引入框架，不把键名再复制一份到调用点之外。
 * English: Compact static table. Linear lookup on the cold UI path, with no framework.
 */
#include "pico_i18n.h"
#include "ui_packed_asset.h"
#include <stdlib.h>
#include <string.h>

extern bool ps_load(const char *key, void *out, size_t bytes);
extern bool ps_store(const char *key, const void *data, size_t bytes);
extern void ps_toast(const char *text);
extern bool ps_hit(int x, int y, int rx, int ry, int rw, int rh);
extern void ps_button(uint8_t *fb, int x, int y, int w, int h, const char *label, bool selected);

// 双语包只占一份 Flash，首次使用校验后保留在 PSRAM；不以中文冒充英文。
// Keep both languages in one Flash package, verified into PSRAM on first use.
#include "pico_i18n_packed.inc"
static uint8_t *s_catalog;
static uint16_t catalog_u16(const uint8_t *p) { return (uint16_t)(p[0] | ((uint16_t)p[1] << 8)); }
static bool catalog_load(void) {
    if (s_catalog) return true;
    size_t bytes = 0;
    uint8_t *data = ui_packed_asset_unpack(s_catalog_packed, sizeof(s_catalog_packed), &bytes);
    size_t start = 12u + PS_I18N_CATALOG_COUNT * 6u;
    if (!data) return false;
    if (bytes <= start || memcmp(data, "PICOTR1\0", 8) ||
        catalog_u16(data + 8) != PS_I18N_CATALOG_COUNT || catalog_u16(data + 10)) {
        free(data); return false;
    }
    for (unsigned i = 0; i < PS_I18N_CATALOG_COUNT * 3u; i++) {
        unsigned offset = catalog_u16(data + 12u + i * 2u);
        if (offset < start || offset >= bytes || !data[offset] || !memchr(data + offset, 0, bytes - offset)) {
            free(data); return false;
        }
    }
    s_catalog = data;
    return true;
}
static const char *catalog_field(int index, unsigned field) {
    if (index < 0 || (unsigned)index >= PS_I18N_CATALOG_COUNT || !catalog_load()) return NULL;
    return (const char *)s_catalog + catalog_u16(s_catalog + 12u + (index * 3u + field) * 2u);
}

static uint8_t g_locale = PS_LOCALE_ZH_HANS;
static bool g_ready;

void ps_i18n_init(void) {
    uint8_t value = PS_LOCALE_ZH_HANS;
    if (ps_load("uilocale", &value, 1) && value <= PS_LOCALE_EN) g_locale = value;
    else g_locale = PS_LOCALE_ZH_HANS;
    g_ready = true;
}
static void ensure(void) { if (!g_ready) ps_i18n_init(); }
ps_locale_t ps_locale(void) { ensure(); return (ps_locale_t)g_locale; }
const char *ps_locale_tag(void) { return ps_locale() == PS_LOCALE_EN ? "en" : "zh-Hans"; }
bool ps_locale_set(ps_locale_t locale) {
    if (locale != PS_LOCALE_ZH_HANS && locale != PS_LOCALE_EN) return false;
    if (!catalog_load()) return false;
    uint8_t value = (uint8_t)locale;
    if (!ps_store("uilocale", &value, 1)) return false;
    g_locale = value;
    g_ready = true;
    return true;
}
const char *ps_tr(const char *key) {
    ensure();
    if (!key) return "";
    if (!catalog_load()) return key;
    for (unsigned i = 0; i < PS_I18N_CATALOG_COUNT; i++) {
        if (!strcmp(catalog_field((int)i, 0), key)) return catalog_field((int)i, g_locale == PS_LOCALE_EN ? 2 : 1);
    }
    return key;
}
int ps_i18n_count(void) { return (int)PS_I18N_CATALOG_COUNT; }
const char *ps_i18n_key(int index) { return catalog_field(index, 0); }
const char *ps_i18n_zh(int index) { return catalog_field(index, 1); }
const char *ps_i18n_en(int index) { return catalog_field(index, 2); }

void ps_i18n_chip(uint8_t *fb, int x, int y) {
    ps_button(fb, x, y, PS_I18N_CHIP_W, PS_I18N_CHIP_H, ps_tr("settings.locale.chip"), false);
}
bool ps_i18n_chip_hit(int x, int y, int chip_x, int chip_y) {
    if (!ps_hit(x, y, chip_x, chip_y, PS_I18N_CHIP_W, PS_I18N_CHIP_H)) return false;
    ps_locale_t next = ps_locale() == PS_LOCALE_EN ? PS_LOCALE_ZH_HANS : PS_LOCALE_EN;
    if (!ps_locale_set(next)) ps_toast(ps_tr("settings.locale.save_failed"));
    else ps_toast(ps_tr("settings.locale.saved"));
    return true;
}

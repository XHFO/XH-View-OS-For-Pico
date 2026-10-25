/*
 * SPDX-License-Identifier: Apache-2.0
 * 中文：紧凑静态表。冷路径线性查找，不引入框架，不把键名再复制一份到调用点之外。
 * English: Compact static table. Linear lookup on the cold UI path, with no framework.
 */
#include "pico_i18n.h"
#include <string.h>

extern bool ps_load(const char *key, void *out, size_t bytes);
extern bool ps_store(const char *key, const void *data, size_t bytes);
extern void ps_toast(const char *text);
extern bool ps_hit(int x, int y, int rx, int ry, int rw, int rh);
extern void ps_button(uint8_t *fb, int x, int y, int w, int h, const char *label, bool selected);

#define PS_I18N(key, zh, en) {key, zh, en},
static const struct { const char *key, *zh, *en; } k_table[] = {
#include "pico_i18n_table.inc"
};
#undef PS_I18N

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
    uint8_t value = (uint8_t)locale;
    if (!ps_store("uilocale", &value, 1)) return false;
    g_locale = value;
    g_ready = true;
    return true;
}
const char *ps_tr(const char *key) {
    ensure();
    if (!key) return "";
    for (size_t i = 0; i < sizeof(k_table) / sizeof(k_table[0]); i++) {
        if (!strcmp(k_table[i].key, key)) return g_locale == PS_LOCALE_EN ? k_table[i].en : k_table[i].zh;
    }
    return key;
}
int ps_i18n_count(void) { return (int)(sizeof(k_table) / sizeof(k_table[0])); }
const char *ps_i18n_key(int index) { return index >= 0 && index < ps_i18n_count() ? k_table[index].key : NULL; }
const char *ps_i18n_zh(int index) { return index >= 0 && index < ps_i18n_count() ? k_table[index].zh : NULL; }
const char *ps_i18n_en(int index) { return index >= 0 && index < ps_i18n_count() ? k_table[index].en : NULL; }

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

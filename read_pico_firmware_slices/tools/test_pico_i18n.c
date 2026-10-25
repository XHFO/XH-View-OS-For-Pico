/* SPDX-License-Identifier: Apache-2.0
 * Catalog, locale persistence, and language-chip checks. No device access.
 */
#include <assert.h>
#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include "../main/system/pico_i18n.h"

static uint8_t stored = 0;
static bool has_store, store_ok = true;
static char last_toast[160];
static int toasts, buttons;

bool ps_load(const char *key, void *out, size_t bytes) {
    assert(key && !strcmp(key, "uilocale") && out && bytes == 1);
    if (!has_store) return false;
    *(uint8_t *)out = stored;
    return true;
}
bool ps_store(const char *key, const void *data, size_t bytes) {
    assert(key && !strcmp(key, "uilocale") && data && bytes == 1);
    if (!store_ok) return false;
    stored = *(const uint8_t *)data;
    has_store = true;
    return true;
}
void ps_toast(const char *text) { assert(text && *text); snprintf(last_toast, sizeof(last_toast), "%s", text); toasts++; }
bool ps_hit(int x, int y, int rx, int ry, int rw, int rh) { return x >= rx && y >= ry && x < rx + rw && y < ry + rh; }
void ps_button(uint8_t *fb, int x, int y, int w, int h, const char *label, bool selected) {
    (void)fb; (void)selected;
    assert(x >= 0 && y >= 0 && w == PS_I18N_CHIP_W && h == PS_I18N_CHIP_H);
    assert(label && *label);
    buttons++;
}

#include "../main/system/pico_i18n.c"

static void specs(const char *s, char *out, size_t cap) {
    size_t n = 0;
    while (*s) {
        if (*s != '%') { s++; continue; }
        if (s[1] == '%') { s += 2; continue; }
        s++;
        while (*s && strchr("-+ #0'", *s)) s++;
        while (*s && (isdigit((unsigned char)*s) || *s == '.' || *s == '*')) s++;
        while (*s && strchr("hljztL", *s)) {
            assert(n + 1 < cap);
            out[n++] = *s++;
        }
        assert(*s && n + 1 < cap);
        out[n++] = *s++;
    }
    out[n] = 0;
}

static bool key_ok(const char *key) {
    if (!key || !islower((unsigned char)*key)) return false;
    int dots = 0;
    for (const char *p = key; *p; p++) {
        unsigned char c = (unsigned char)*p;
        if (c == '.') { if (p[1] == '.' || p == key || !p[1]) return false; dots++; }
        else if (!islower(c) && !isdigit(c) && c != '_') return false;
    }
    return dots >= 1;
}

int main(void) {
    ps_i18n_init();
    assert(ps_locale() == PS_LOCALE_ZH_HANS);
    assert(!strcmp(ps_locale_tag(), "zh-Hans"));
    assert(!strcmp(ps_tr("common.cancel"), "取消"));
    assert(ps_locale_set(PS_LOCALE_EN) && has_store && stored == PS_LOCALE_EN);
    assert(ps_locale() == PS_LOCALE_EN && !strcmp(ps_locale_tag(), "en"));
    assert(!strcmp(ps_tr("common.cancel"), "Cancel"));
    ps_i18n_init();
    assert(ps_locale() == PS_LOCALE_EN);
    stored = 9;
    ps_i18n_init();
    assert(ps_locale() == PS_LOCALE_ZH_HANS && !strcmp(ps_locale_tag(), "zh-Hans"));
    store_ok = false;
    assert(!ps_locale_set(PS_LOCALE_EN) && ps_locale() == PS_LOCALE_ZH_HANS);
    store_ok = true;
    assert(!ps_locale_set((ps_locale_t)7));
    assert(!strcmp(ps_tr("missing.key"), "missing.key"));
    assert(!strcmp(ps_tr(NULL), ""));

    int n = ps_i18n_count();
    assert(n > 40);
    for (int i = 0; i < n; i++) {
        const char *key = ps_i18n_key(i), *zh = ps_i18n_zh(i), *en = ps_i18n_en(i);
        assert(key && zh && en && *key && *zh && *en);
        assert(strlen(zh) < 180 && strlen(en) < 180);
        assert(!strchr(key, '\t') && !strchr(zh, '\t') && !strchr(en, '\t'));
        assert(!strchr(key, '\n') && !strchr(zh, '\n') && !strchr(en, '\n'));
        assert(key_ok(key));
        for (int j = 0; j < i; j++) assert(strcmp(key, ps_i18n_key(j)) != 0);
        char a[64], b[64];
        specs(zh, a, sizeof(a));
        specs(en, b, sizeof(b));
        if (strcmp(a, b)) {
            fprintf(stderr, "specifier mismatch %s zh [%s] en [%s]\n", key, a, b);
            return 1;
        }
        assert(ps_locale() == PS_LOCALE_ZH_HANS);
        assert(!strcmp(ps_tr(key), zh));
    }
    const char *needles[] = {
        "已连接，但网络未保存", "扫描启动", "扫描超时", "扫描结果", "扫描完成",
        "连接未成功", "扫描已中断", "Wi-Fi 开启未成功", "扫描请求未提交", NULL
    };
    for (const char **need = needles; *need; need++) {
        bool found = false;
        for (int i = 0; i < n && !found; i++) found = strstr(ps_i18n_zh(i), *need) != NULL;
        if (!found) { fprintf(stderr, "missing zh substring: %s\n", *need); return 1; }
    }

    ps_i18n_chip(NULL, 0, 0);
    assert(buttons == 1 && !strcmp(ps_tr("settings.locale.chip"), "中文"));
    assert(!ps_i18n_chip_hit(80, 0, 0, 0));
    assert(ps_i18n_chip_hit(1, 1, 0, 0));
    assert(ps_locale() == PS_LOCALE_EN && strstr(last_toast, "Language"));
    assert(!strcmp(ps_tr("settings.locale.chip"), "EN"));
    assert(ps_i18n_chip_hit(1, 1, 0, 0) && ps_locale() == PS_LOCALE_ZH_HANS);
    printf("i18n catalog passed: %d keys\n", n);
    return 0;
}

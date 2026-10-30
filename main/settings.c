/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 *
 * NVS 读写。打开失败就用关机默认值，不擦除整个分区。
 *
 * NVS load/store. A failed open keeps the power-off default; the
 * partition is not erased.
 */

#include "settings.h"
#include "ttf_font.h"

#include <string.h>
#include <stdatomic.h>

#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"

#define TAG "settings"
#define NVS_NS "read_pico"
#define NVS_KEY_SLEEP "sleep"
#define NVS_KEY_FONT "font"
#define NVS_KEY_FONT_VERSION "font_ver"
#define NVS_KEY_WAKE "lwake"
#define NVS_KEY_BOOT "lboot"
#define NVS_KEY_PICKUP "pickup"
#define NVS_KEY_TEST_AWAKE "dev_awake"
#define NVS_KEY_BOOK_PX "bk_px"
#define NVS_KEY_BOOK_SHAKE "bk_shake"
#define NVS_KEY_SOUND "sound_v1"
#define FONT_PATH_MAX 160

static app_sleep_mode_t s_sleep = APP_SLEEP_OFF;
static char s_font[FONT_PATH_MAX] = TTF_FONT_DEFAULT_SD;
static uint32_t s_font_revision;
static uint8_t s_last_wake;
static uint8_t s_last_boot;
static bool s_pickup_wake;
static bool s_test_awake = true;
static uint8_t s_book_px = 48;
static bool s_book_shake;
static atomic_uchar s_sound_flags = 15;
static atomic_flag s_sound_save = ATOMIC_FLAG_INIT;

static uint8_t valid_book_px(uint8_t px) {
    return px >= 36 && px <= 72 && (px - 36) % 4 == 0 ? px : 48;
}

void app_settings_init(void) {
    // 测试版无此键时保持唤醒；已保存的关闭值优先。/ This test build stays awake without a key; a saved off value takes precedence.
    s_test_awake = true;
    s_sound_flags = 15;
    esp_err_t err = nvs_flash_init();
    // 升级兼容失败时保留原设置，禁止为演示擦除整个 NVS。
    // Preserve original settings on upgrade errors; never erase NVS for the demo.
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "nvs init %s, use power off", esp_err_to_name(err));
        return;
    }

    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READONLY, &h) != ESP_OK) return;
    uint8_t raw = APP_SLEEP_OFF;
    if (nvs_get_u8(h, NVS_KEY_SLEEP, &raw) == ESP_OK && raw <= APP_SLEEP_OFF) {
        s_sleep = (app_sleep_mode_t)raw;
    }
    char saved_font[FONT_PATH_MAX];
    size_t font_len = sizeof(saved_font);
    bool have_font = nvs_get_str(h, NVS_KEY_FONT, saved_font, &font_len) == ESP_OK;
    uint8_t font_version = 0;
    (void)nvs_get_u8(h, NVS_KEY_FONT_VERSION, &font_version);
    // 旧默认只迁移一次；此后空路径是用户明确选择内建，回退不改首选。
    // Migrate the old default once; an empty path then means explicit built-in selection, never a temporary fallback.
    if (have_font && (font_version || (saved_font[0] && strcmp(saved_font, TTF_FONT_BUILTIN)))) {
        strlcpy(s_font, saved_font, sizeof(s_font));
    }
    uint8_t wake = 0;
    if (nvs_get_u8(h, NVS_KEY_WAKE, &wake) == ESP_OK) s_last_wake = wake;
    uint8_t boot = 0;
    if (nvs_get_u8(h, NVS_KEY_BOOT, &boot) == ESP_OK) s_last_boot = boot;
    uint8_t pickup = 0;
    if (nvs_get_u8(h, NVS_KEY_PICKUP, &pickup) == ESP_OK) s_pickup_wake = pickup != 0;
    uint8_t test_awake = 1;
    if (nvs_get_u8(h, NVS_KEY_TEST_AWAKE, &test_awake) == ESP_OK) s_test_awake = test_awake != 0;
    uint8_t book_px = 48, book_shake = 0;
    if (nvs_get_u8(h, NVS_KEY_BOOK_PX, &book_px) == ESP_OK) s_book_px = valid_book_px(book_px);
    if (nvs_get_u8(h, NVS_KEY_BOOK_SHAKE, &book_shake) == ESP_OK) s_book_shake = book_shake != 0;
    uint8_t sound_flags = 15;
    if (nvs_get_u8(h, NVS_KEY_SOUND, &sound_flags) == ESP_OK && !(sound_flags & ~15u)) s_sound_flags = sound_flags;
    nvs_close(h);
    if (!font_version && nvs_open(NVS_NS, NVS_READWRITE, &h) == ESP_OK) {
        if (nvs_set_str(h, NVS_KEY_FONT, s_font) == ESP_OK &&
            nvs_set_u8(h, NVS_KEY_FONT_VERSION, 1) == ESP_OK) (void)nvs_commit(h);
        nvs_close(h);
    }
    ESP_LOGI(
        TAG, "sleep mode %s, font %s",
        app_sleep_mode_name(s_sleep),
        s_font[0] != '\0' ? s_font : "(builtin)"
    );
}

app_sleep_mode_t app_settings_sleep_mode(void) {
    return s_sleep;
}

const char* app_sleep_mode_name(app_sleep_mode_t mode) {
    switch (mode) {
        case APP_SLEEP_LIGHT: return "light";
        case APP_SLEEP_DEEP: return "deep";
        case APP_SLEEP_OFF: return "off";
        default: return "?";
    }
}

void app_settings_set_sleep_mode(app_sleep_mode_t mode) {
    if (mode > APP_SLEEP_OFF) mode = APP_SLEEP_OFF;
    s_sleep = mode;
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READWRITE, &h) != ESP_OK) return;
    nvs_set_u8(h, NVS_KEY_SLEEP, (uint8_t)mode);
    nvs_commit(h);
    nvs_close(h);
}

const char* app_settings_font_path(void) {
    return s_font;
}

uint32_t app_settings_font_revision(void) {
    return s_font_revision;
}

static void nvs_put_u8(const char* key, uint8_t value) {
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READWRITE, &h) != ESP_OK) return;
    nvs_set_u8(h, key, value);
    nvs_commit(h);
    nvs_close(h);
}

uint8_t app_settings_last_wake(void) {
    return s_last_wake;
}

void app_settings_set_last_wake(uint8_t src) {
    if (src == s_last_wake) return;
    s_last_wake = src;
    nvs_put_u8(NVS_KEY_WAKE, src);
}

uint8_t app_settings_last_boot(void) {
    return s_last_boot;
}

void app_settings_set_last_boot(uint8_t reason) {
    if (reason == 0 || reason == s_last_boot) return;
    s_last_boot = reason;
    nvs_put_u8(NVS_KEY_BOOT, reason);
}

bool app_settings_pickup_wake(void) {
    return s_pickup_wake;
}

void app_settings_set_pickup_wake(bool on) {
    if (s_pickup_wake == on) return;
    s_pickup_wake = on;
    nvs_put_u8(NVS_KEY_PICKUP, on ? 1 : 0);
}

bool app_settings_test_awake(void) {
    return s_test_awake;
}

void app_settings_set_test_awake(bool on) {
    s_test_awake = on;
    nvs_put_u8(NVS_KEY_TEST_AWAKE, on ? 1 : 0);
}

void app_settings_set_font_path(const char* path) {
    if (path == NULL) path = "";
    s_font_revision++;
    strlcpy(s_font, path, sizeof(s_font));
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READWRITE, &h) != ESP_OK) return;
    if (nvs_set_str(h, NVS_KEY_FONT, s_font) == ESP_OK &&
        nvs_set_u8(h, NVS_KEY_FONT_VERSION, 1) == ESP_OK) (void)nvs_commit(h);
    nvs_close(h);
}

uint8_t app_settings_book_px(void) {
    return s_book_px;
}

void app_settings_set_book_px(uint8_t px) {
    px = valid_book_px(px);
    if (s_book_px == px) return;
    s_book_px = px;
    nvs_put_u8(NVS_KEY_BOOK_PX, px);
}

bool app_settings_book_shake(void) {
    return s_book_shake;
}

uint8_t app_settings_sound_flags(void) { return atomic_load(&s_sound_flags); }
bool app_settings_set_sound_flags(uint8_t flags) {
    if (flags & ~15u) return false;
    if (atomic_flag_test_and_set(&s_sound_save)) return false;
    if (flags == atomic_load(&s_sound_flags)) { atomic_flag_clear(&s_sound_save); return true; }
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READWRITE, &h) != ESP_OK) { atomic_flag_clear(&s_sound_save); return false; }
    esp_err_t error = nvs_set_u8(h, NVS_KEY_SOUND, flags);
    if (error == ESP_OK) error = nvs_commit(h);
    nvs_close(h);
    if (error == ESP_OK) atomic_store(&s_sound_flags, flags);
    atomic_flag_clear(&s_sound_save);
    return error == ESP_OK;
}

void app_settings_set_book_shake(bool on) {
    if (s_book_shake == on) return;
    s_book_shake = on;
    nvs_put_u8(NVS_KEY_BOOK_SHAKE, on ? 1 : 0);
}

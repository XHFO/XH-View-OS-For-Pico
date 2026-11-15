/*
 * SPDX-License-Identifier: Apache-2.0
 * 中文：ota_1 小型更新器；从 TF 校验并写入 ota_0，失败时自动返回原系统。
 * English: Tiny ota_1 updater; verify a TF image, write ota_0 and return to the system on failure.
 * 冻结：只接受 ESP32-S3 APP 镜像；写前流式校验 SHA-256，成功后切换启动分区并重启。
 * Frozen: Accept only ESP32-S3 APP images; verify SHA-256 before writing, then switch boot and restart.
 */
#include "read_pico_init.h"
#include "read_pico_sd.h"
#include "read_pico_buzzer.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "esp_system.h"
#include "esp_log.h"
#include "epdiy.h"
#include "epd_highlevel.h"
#include "psa/crypto.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define IMAGE_PATH "/sdcard/readpico/firmware/update.bin"
#define SHA_PATH "/sdcard/readpico/firmware/update.sha256"

static read_pico_handle_t s_hw;
static const char *TAG = "ota_updater";

enum {
    UPDATE_VERIFYING,
    UPDATE_WRITING,
    UPDATE_COMPLETE,
    UPDATE_FAILED
};

typedef struct {
    char ch;
    uint8_t columns[5];
} glyph_t;

static const glyph_t glyphs[] = {
    {' ', {0,0,0,0,0}},
    {'A', {0x7e,0x11,0x11,0x11,0x7e}}, {'B', {0x7f,0x49,0x49,0x49,0x36}},
    {'C', {0x3e,0x41,0x41,0x41,0x22}}, {'D', {0x7f,0x41,0x41,0x22,0x1c}},
    {'E', {0x7f,0x49,0x49,0x49,0x41}}, {'F', {0x7f,0x09,0x09,0x09,0x01}},
    {'G', {0x3e,0x41,0x49,0x49,0x7a}}, {'H', {0x7f,0x08,0x08,0x08,0x7f}},
    {'I', {0x00,0x41,0x7f,0x41,0x00}}, {'J', {0x20,0x40,0x41,0x3f,0x01}},
    {'K', {0x7f,0x08,0x14,0x22,0x41}}, {'L', {0x7f,0x40,0x40,0x40,0x40}},
    {'M', {0x7f,0x02,0x0c,0x02,0x7f}}, {'N', {0x7f,0x04,0x08,0x10,0x7f}},
    {'O', {0x3e,0x41,0x41,0x41,0x3e}}, {'P', {0x7f,0x09,0x09,0x09,0x06}},
    {'Q', {0x3e,0x41,0x51,0x21,0x5e}}, {'R', {0x7f,0x09,0x19,0x29,0x46}},
    {'S', {0x46,0x49,0x49,0x49,0x31}}, {'T', {0x01,0x01,0x7f,0x01,0x01}},
    {'U', {0x3f,0x40,0x40,0x40,0x3f}}, {'V', {0x1f,0x20,0x40,0x20,0x1f}},
    {'W', {0x3f,0x40,0x38,0x40,0x3f}}, {'X', {0x63,0x14,0x08,0x14,0x63}},
    {'Y', {0x07,0x08,0x70,0x08,0x07}}, {'Z', {0x61,0x51,0x49,0x45,0x43}},
    {'0', {0x3e,0x51,0x49,0x45,0x3e}}, {'1', {0x00,0x42,0x7f,0x40,0x00}},
    {'2', {0x42,0x61,0x51,0x49,0x46}}, {'3', {0x21,0x41,0x45,0x4b,0x31}},
    {'4', {0x18,0x14,0x12,0x7f,0x10}}, {'5', {0x27,0x45,0x45,0x45,0x39}},
    {'6', {0x3c,0x4a,0x49,0x49,0x30}}, {'7', {0x01,0x71,0x09,0x05,0x03}},
    {'8', {0x36,0x49,0x49,0x49,0x36}}, {'9', {0x06,0x49,0x49,0x29,0x1e}},
    {'-', {0x08,0x08,0x08,0x08,0x08}}, {'%', {0x23,0x13,0x08,0x64,0x62}}
};

static EpdRect logical_rect(EpdRect rect) {
    const int pw = epd_width(), ph = epd_height();
    const int lx0 = rect.x, ly0 = rect.y;
    const int lx1 = rect.x + rect.width - 1;
    const int ly1 = rect.y + rect.height - 1;
    int ax, ay, bx, by;
    switch (epd_get_rotation()) {
        case EPD_ROT_LANDSCAPE:
            ax = lx0; ay = ly0; bx = lx1; by = ly1; break;
        case EPD_ROT_PORTRAIT:
            ax = pw - ly0 - 1; ay = lx0; bx = pw - ly1 - 1; by = lx1; break;
        case EPD_ROT_INVERTED_LANDSCAPE:
            ax = pw - lx0 - 1; ay = ph - ly0 - 1;
            bx = pw - lx1 - 1; by = ph - ly1 - 1; break;
        default:
            ax = ly0; ay = ph - lx0 - 1; bx = ly1; by = ph - lx1 - 1; break;
    }
    const int x0 = ax < bx ? ax : bx, x1 = ax < bx ? bx : ax;
    const int y0 = ay < by ? ay : by, y1 = ay < by ? by : ay;
    return (EpdRect){x0, y0, x1 - x0 + 1, y1 - y0 + 1};
}

static void logical_fill(EpdRect rect, uint8_t color, uint8_t *fb) {
    if (rect.width > 0 && rect.height > 0)
        epd_fill_rect(logical_rect(rect), color, fb);
}

static const glyph_t *glyph_for(char ch) {
    for (size_t i = 0; i < sizeof(glyphs) / sizeof(glyphs[0]); ++i)
        if (glyphs[i].ch == ch) return &glyphs[i];
    return &glyphs[0];
}

static int text_width(const char *text, int scale) {
    return text && *text ? ((int)strlen(text) * 6 - 1) * scale : 0;
}

static void text_center(uint8_t *fb, int logical_width, int x_center, int y,
                        int scale, const char *text) {
    int x = x_center - text_width(text, scale) / 2;
    for (; text && *text; ++text, x += 6 * scale) {
        const glyph_t *glyph = glyph_for(*text);
        for (int column = 0; column < 5; ++column) {
            uint8_t bits = glyph->columns[column];
            for (int row = 0; row < 7;) {
                if (!(bits & (1u << row))) { ++row; continue; }
                int end = row + 1;
                while (end < 7 && (bits & (1u << end))) ++end;
                logical_fill((EpdRect){x + column * scale, y + row * scale,
                    scale, (end - row) * scale}, 0, fb);
                row = end;
            }
        }
    }
    (void)logical_width;
}

static void status(int filled, int stage) {
    uint8_t *fb = s_hw.framebuffer;
    if (!fb) return;
    epd_hl_set_all_white(&s_hw.hl);
    const int w = epd_rotated_display_width();
    const int card_x = 72, card_w = w - 144;
    text_center(fb, w, w / 2, 170, 4, "XH VIEW OS FOR PICO");
    text_center(fb, w, w / 2, 232, 3, "FIRMWARE UPDATE");
    logical_fill((EpdRect){card_x, 292, card_w, 3}, 0, fb);

    const char *title = stage == UPDATE_VERIFYING ? "CHECKING UPDATE" :
        stage == UPDATE_WRITING ? "INSTALLING SYSTEM" :
        stage == UPDATE_COMPLETE ? "UPDATE COMPLETE" : "UPDATE FAILED";
    const char *detail = stage == UPDATE_VERIFYING ? "PLEASE WAIT" :
        stage == UPDATE_WRITING ? "KEEP POWER CONNECTED" :
        stage == UPDATE_COMPLETE ? "RESTARTING" : "RETURNING TO SYSTEM";
    text_center(fb, w, w / 2, 430, 4, title);
    text_center(fb, w, w / 2, 500, 3, detail);

    const int bar_x = card_x, bar_y = 585, bar_w = card_w, bar_h = 92;
    logical_fill((EpdRect){bar_x, bar_y, bar_w, bar_h}, 0, fb);
    logical_fill((EpdRect){bar_x + 5, bar_y + 5, bar_w - 10, bar_h - 10}, 15, fb);
    if (stage != UPDATE_FAILED && filled > 0)
        logical_fill((EpdRect){bar_x + 5, bar_y + 5,
            (bar_w - 10) * filled / 100, bar_h - 10}, 0, fb);

    char percent[8];
    if(stage == UPDATE_FAILED)snprintf(percent,sizeof(percent),"STOPPED");
    else snprintf(percent, sizeof(percent), "%d%%", filled < 0 ? 0 : filled > 100 ? 100 : filled);
    text_center(fb, w, w / 2, 725, 6, percent);
    logical_fill((EpdRect){card_x, 845, card_w, 2}, 0, fb);
    text_center(fb, w, w / 2, 900, 3,
                stage == UPDATE_FAILED ? "RETRY FROM SETTINGS" : "DO NOT POWER OFF");
    text_center(fb, w, w / 2, 1080, 3, "XHFO.NET");
    epd_poweron();
    epd_hl_update_screen(&s_hw.hl, MODE_GC16, 24);
}

static void return_to_system(void) {
    const esp_partition_t *system = esp_partition_find_first(
        ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_OTA_0, NULL);
    esp_err_t err = system ? esp_ota_set_boot_partition(system) : ESP_ERR_NOT_FOUND;
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "restore ota_0 failed: %s", esp_err_to_name(err));
        err = esp_ota_mark_app_invalid_rollback_and_reboot();
        if (err != ESP_OK) ESP_LOGE(TAG, "rollback failed: %s", esp_err_to_name(err));
        return;
    }
    vTaskDelay(pdMS_TO_TICKS(3000));
    esp_restart();
}

static bool parse_sha(const char *path, uint8_t out[32]) {
    char text[80] = {0};
    FILE *f = fopen(path, "rb");
    if (!f) return false;
    size_t n = fread(text, 1, sizeof(text) - 1, f);
    fclose(f);
    while (n && isspace((unsigned char)text[n - 1])) text[--n] = 0;
    if (n != 64) return false;
    for (int i = 0; i < 32; i++) {
        char pair[3] = {text[2 * i], text[2 * i + 1], 0};
        if (!isxdigit((unsigned char)pair[0]) || !isxdigit((unsigned char)pair[1])) return false;
        out[i] = (uint8_t)strtoul(pair, NULL, 16);
    }
    return true;
}

static bool hash_file(const char *path, uint8_t out[32], size_t *length) {
    uint8_t block[4096];
    psa_hash_operation_t sha = PSA_HASH_OPERATION_INIT;
    bool ok = psa_crypto_init() == PSA_SUCCESS &&
        psa_hash_setup(&sha, PSA_ALG_SHA_256) == PSA_SUCCESS;
    FILE *f = ok ? fopen(path, "rb") : NULL;
    ok = ok && f;
    *length = 0;
    while (ok) {
        size_t n = fread(block, 1, sizeof(block), f);
        if (!n) break;
        ok = psa_hash_update(&sha, block, n) == PSA_SUCCESS;
        *length += n;
    }
    size_t digest_bytes = 0;
    ok = ok && !ferror(f) && psa_hash_finish(&sha, out, 32, &digest_bytes) == PSA_SUCCESS &&
        digest_bytes == 32;
    if (f) fclose(f);
    if (!ok) psa_hash_abort(&sha);
    return ok && *length >= 512;
}

static bool write_image(size_t length, const uint8_t expected[32]) {
    const esp_partition_t *target = esp_partition_find_first(
        ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_OTA_0, NULL);
    if (!target || length > target->size) return false;
    const esp_partition_t *running = esp_ota_get_running_partition();
    if (!running || running->subtype != ESP_PARTITION_SUBTYPE_APP_OTA_1) return false;
    FILE *f = fopen(IMAGE_PATH, "rb");
    if (!f) return false;
    esp_ota_handle_t ota = 0;
    esp_err_t err = esp_ota_begin(target, length, &ota);
    uint8_t block[4096], digest[32];
    psa_hash_operation_t sha = PSA_HASH_OPERATION_INIT;
    if (err == ESP_OK) err = psa_hash_setup(&sha, PSA_ALG_SHA_256) == PSA_SUCCESS ? ESP_OK : ESP_FAIL;
    size_t done = 0;
    while (err == ESP_OK) {
        size_t n = fread(block, 1, sizeof(block), f);
        if (!n) break;
        if (psa_hash_update(&sha, block, n) != PSA_SUCCESS ||
            esp_ota_write(ota, block, n) != ESP_OK) err = ESP_FAIL;
        done += n;
        // 4 MiB 镜像最多刷新 11 次，避免每 16 KiB 触发一次整屏 GC16。
        // Refresh at most 11 times for a 4 MiB image instead of every 16 KiB.
        if ((done * 10 / length) != ((done - n) * 10 / length))
            status((int)(done * 100 / length), UPDATE_WRITING);
    }
    size_t digest_bytes = 0;
    bool ok = err == ESP_OK && !ferror(f) && done == length &&
        psa_hash_finish(&sha, digest, 32, &digest_bytes) == PSA_SUCCESS &&
        digest_bytes == 32 && !memcmp(digest, expected, 32);
    fclose(f);
    if (!ok) psa_hash_abort(&sha);
    if (ok) ok = esp_ota_end(ota) == ESP_OK;
    else if (ota) (void)esp_ota_abort(ota);
    if (!ok) return false;
    return esp_ota_set_boot_partition(target) == ESP_OK;
}

void app_main(void) {
    if (read_pico_init(&s_hw) != ESP_OK) {
        ESP_LOGE(TAG, "board init failed");
        return;
    }
    // 更新器由主系统主动切换启动；先确认自身有效，后续重试不会触发回滚抖动。
    // Acknowledge the updater as soon as it boots so retries do not trigger rollback churn.
    esp_err_t valid = esp_ota_mark_app_valid_cancel_rollback();
    if (valid != ESP_OK) {
        ESP_LOGE(TAG, "updater acknowledgement failed: %s", esp_err_to_name(valid));
        status(100, UPDATE_FAILED);
        return_to_system();
        return;
    }
    status(0, UPDATE_VERIFYING);
    read_pico_sd_info_t sd = {0};
    for (int i = 0; i < 50 && (!sd.mounted || sd.state != READ_PICO_SD_STATE_READY); i++) {
        vTaskDelay(pdMS_TO_TICKS(100));
        read_pico_sd_get_info(&sd);
    }
    uint8_t expected[32], actual[32];
    size_t length = 0;
    bool ok = sd.mounted && sd.state == READ_PICO_SD_STATE_READY &&
        parse_sha(SHA_PATH, expected) && hash_file(IMAGE_PATH, actual, &length) &&
        !memcmp(expected, actual, sizeof(expected));
    if (ok) ok = write_image(length, expected);
    if (ok) {
        status(100, UPDATE_COMPLETE);
        vTaskDelay(pdMS_TO_TICKS(300));
        esp_restart();
    }
    ESP_LOGE(TAG, "update failed length=%u", (unsigned)length);
    status(100, UPDATE_FAILED);
    read_pico_buzzer_init();
    read_pico_buzzer_tone(880, 800);
    // 失败镜像不会成为启动项；保留原 ota_0 系统是唯一可恢复路径。
    // A failed image is never selected; restore the existing ota_0 system after the error state.
    return_to_system();
}

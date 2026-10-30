/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 * 中文：执行生产开机锁图校验及工程锁屏入口，确认解压错误无屏幕副作用且睡前释放PSRAM。
 * English: Execute production boot lock validation and the engineering lock entry, checking failure side effects and PSRAM release before sleep.
 * 冻结：仅替换硬件与ROM接口；不声称验证真实屏幕、ROM或整机睡眠耗电。
 * Frozen: Replace only hardware and ROM interfaces; do not claim physical display, ROM or sleep-power validation.
 */
#include <assert.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "miniz.h"
#include "ui_image.h"

#define MALLOC_CAP_SPIRAM 1u
#define MALLOC_CAP_8BIT 2u
#define MZ_CRC32_INIT 0
typedef struct { int unused; } EpdiyHighlevelState;
typedef void* sc7a20h_handle_t;
typedef enum { APP_SLEEP_LIGHT, APP_SLEEP_OFF, APP_SLEEP_DEEP } app_sleep_mode_t;
#define MODE_GC16 2
#define APP_LOCK_IGNORE_BOOT_MS 2000
static unsigned live, allocations, fail_allocation, update_calls, power_calls, clear_calls, wait_calls;
static unsigned sleep_calls, drain_calls, white_calls;
static int rotation, width = 1216;
static uint8_t* framebuffer;
static const uint8_t* lock_source;
static size_t lock_source_bytes;

static void test_log(const char* tag, const char* format, ...) { assert(tag && format); }
static void* tracked_allocate(size_t bytes, unsigned caps) {
    assert(caps == (MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (++allocations == fail_allocation) return NULL;
    void* pointer = malloc(bytes);
    assert(pointer);
    ++live;
    return pointer;
}
static void tracked_free(void* pointer) { if (pointer) { assert(live); --live; free(pointer); } }
static unsigned long mz_crc32(unsigned long crc, const unsigned char* bytes, size_t count) { return crc32(crc, bytes, (uInt)count); }
static int epd_width(void) { return width; }
static int epd_height(void) { return 684; }
static int epd_get_rotation(void) { return rotation; }
static bool read_pico_buzzer_score_busy(void) { return false; }
static int read_pico_buzzer_score_stop(void) { return 0; }
static void epd_poweron(void) { ++power_calls; }
static void epd_poweroff(void) { assert(!live); }
static void epd_clear(void) { ++clear_calls; }
static void epd_hl_set_all_white(EpdiyHighlevelState* hl) { assert(hl); ++white_calls; }
static uint8_t* epd_hl_get_framebuffer(EpdiyHighlevelState* hl) { assert(hl); return framebuffer; }
static void epd_hl_update_screen_from_white(EpdiyHighlevelState* hl, int mode, int temperature) {
    assert(hl && mode == MODE_GC16 && temperature == 25 && live == 1);
    ++update_calls;
}
static void app_lock_wait_key_idle(int timeout) { assert(timeout == 800 && !live); ++wait_calls; }
static app_sleep_mode_t app_settings_sleep_mode(void) { return APP_SLEEP_LIGHT; }
static const char* app_sleep_mode_name(app_sleep_mode_t mode) { assert(mode == APP_SLEEP_LIGHT); return "light"; }
static int app_enter_host_sleep(app_sleep_mode_t mode) { (void)mode; assert(!"unexpected host shutdown"); return -1; }
static const char* esp_err_to_name(int error) { (void)error; return "stub"; }
static void app_light_sleep_wait(sc7a20h_handle_t acc) { assert(acc && !live); ++sleep_calls; }
static void read_pico_pmu_drain_events(void) { ++drain_calls; }
static int64_t esp_timer_get_time(void) { return 123000; }

#define heap_caps_malloc tracked_allocate
#define free tracked_free
#include "ui_packed_asset.c"
#define ESP_LOGI(tag, ...) test_log(tag, __VA_ARGS__)
#define ESP_LOGW(tag, ...) test_log(tag, __VA_ARGS__)
#define lock_4bpp_p4b_deflate_start lock_source
#define lock_4bpp_p4b_deflate_end (lock_source + lock_source_bytes)
#include "packed_asset_integration_under_test.inc"
#undef free

static uint8_t* read_file(const char* path, size_t* bytes) {
    FILE* file = fopen(path, "rb"); assert(file && !fseek(file, 0, SEEK_END));
    long length = ftell(file); assert(length > 0); rewind(file);
    uint8_t* result = malloc((size_t)length); assert(result);
    assert(fread(result, 1, (size_t)length, file) == (size_t)length); fclose(file);
    *bytes = (size_t)length; return result;
}
static void reset(void) {
    assert(!live); allocations = fail_allocation = 0;
    update_calls = power_calls = clear_calls = wait_calls = sleep_calls = drain_calls = white_calls = 0;
}
static void no_hardware(void) { assert(!live && !update_calls && !power_calls && !clear_calls && !wait_calls && !sleep_calls && !drain_calls && !white_calls); }
static void rejected(EpdiyHighlevelState* hl) {
    assert(!lock_image_matches_panel()); no_hardware();
    int64_t ignore = -1;
    enter_lock_and_sleep(hl, &ignore, (void*)1);
    no_hardware(); assert(ignore == -1);
}
int main(int argc, char** argv) {
    assert(argc == 4);
    size_t packed_bytes, original_bytes, corrupt_bytes;
    uint8_t* packed = read_file(argv[1], &packed_bytes);
    uint8_t* original = read_file(argv[2], &original_bytes);
    uint8_t* corrupt = read_file(argv[3], &corrupt_bytes);
    const size_t raw_bytes = 1216u * 684u / 2u;
    framebuffer = malloc(raw_bytes); uint8_t* expected = malloc(raw_bytes); assert(framebuffer && expected);
    EpdiyHighlevelState hl = {0}; lock_source = packed; lock_source_bytes = packed_bytes;
    for (rotation = 0; rotation < 4; ++rotation) {
        reset(); assert(lock_image_matches_panel()); no_hardware();
        memset(framebuffer, 0xa5, raw_bytes);
        int64_t ignore = -1;
        enter_lock_and_sleep(&hl, &ignore, (void*)1);
        assert(!live && update_calls == 1 && power_calls == 2 && clear_calls == 2 && wait_calls == 1 && sleep_calls == 1 && drain_calls == 2 && white_calls == 2);
        assert(ignore == 2123);
        assert(ui_image_decode(expected, raw_bytes, original, original_bytes, 1216, 684, (ui_image_rotation_t)rotation));
        assert(!memcmp(framebuffer, expected, raw_bytes));
    }
    reset(); lock_source_bytes = packed_bytes - 1; rejected(&hl);
    reset(); lock_source = corrupt; lock_source_bytes = corrupt_bytes; rejected(&hl);
    lock_source = packed; lock_source_bytes = packed_bytes;
    reset(); width = 1200; rejected(&hl); width = 1216;
    for (unsigned failure = 1; failure <= 2; ++failure) {
        reset(); fail_allocation = failure; assert(!lock_image_matches_panel()); no_hardware();
        allocations = 0; int64_t ignore = -1; enter_lock_and_sleep(&hl, &ignore, (void*)1); no_hardware(); assert(ignore == -1);
    }
    reset(); rotation = 4; memset(framebuffer, 0xa5, raw_bytes);
    enter_lock_and_sleep(&hl, NULL, (void*)1);
    assert(!live && !update_calls && !wait_calls && !sleep_calls && power_calls == 1 && clear_calls == 1);
    for (size_t i = 0; i < raw_bytes; ++i) assert(framebuffer[i] == 0xa5);
    puts("boot/lock integration: actual production validators/lock entry, four rotations, free-before-sleep, OOM/truncation/inner CRC/dimension/decode failures PASS");
    free(framebuffer); free(expected); free(packed); free(original); free(corrupt);
    return 0;
}

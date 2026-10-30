/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 *
 * 只做开机装配：拉起板级硬件、读设置、定 VCOM、放开机图、开字体；
 * 未标定则先拦住进设定页。然后把控制权交给 app_loop。
 *
 * Boot wiring only: board init, settings, VCOM, splash, fonts. If VCOM
 * is unset, the factory page blocks first. Then control goes to app_loop.
 *
 * 冻结：启动/锁屏外层共用无损解压；先验锁屏后释放再载启动图，保持解码方向、刷屏及锁屏资源不变。
 * Frozen: Share lossless splash/lock unpacking; validate/free lock before loading splash, preserving rotation, display updates and the lock asset.
 * 用户修订：启动资源统一显示 XH View OS For Pico、系统版本和先行预览测试版；保持静态单次显示，不伪造加载进度。
 * User revision: The splash identifies XH View OS For Pico, its version and preview test status in one static update without fabricated progress.
 */

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "app_loop.h"
#include "app_registry.h"
#include "display.h"
#include "epd_highlevel.h"
#include "epdiy.h"
#include "esp_log.h"
#include "pmu_selftest.h"
#include "read_pico_board.h"
#include "read_pico_init.h"
#include "read_pico_pmu.h"
#include "settings.h"
#include "sleep.h"
#include "ttf_font.h"
#include "ui_kit.h"
#include "ui_image.h"
#include "ui_packed_asset.h"
#include "vcom_setup.h"

static const char* TAG = "read_pico";

extern const uint8_t lock_4bpp_p4b_deflate_start[] asm("_binary_lock_4bpp_p4b_deflate_start");
extern const uint8_t lock_4bpp_p4b_deflate_end[] asm("_binary_lock_4bpp_p4b_deflate_end");
extern const uint8_t loading_4bpp_p4b_deflate_start[] asm("_binary_loading_4bpp_p4b_deflate_start");
extern const uint8_t loading_4bpp_p4b_deflate_end[] asm("_binary_loading_4bpp_p4b_deflate_end");

// 已标定则开机读一次喂给 epdiy。未标定先用板级默认出开机图，随后拦住进标定页。
// Load VCOM once when set. Otherwise keep the board default for the splash,
// then gate into the setup page.
static bool resolve_vcom_at_boot(void) {
    int mv = 0;
    for (int i = 0; i < 3; i++) {
        if (read_pico_pmu_vcom_get(&mv) == ESP_OK) {
            epd_set_vcom((uint16_t)mv);
            ESP_LOGI(TAG, "panel VCOM loaded from PMU");
            return true;
        }
    }
    ESP_LOGW(TAG, "panel VCOM unset, keeping board default until setup");
    return false;
}

// 开机/锁屏图无损压缩，尺寸、整个压缩流及CRC均须与面板匹配。
// Lossless splash/lock assets must match the panel dimensions, complete stream, and CRC.
static bool lock_image_matches_panel(void) {
    size_t image_bytes = 0;
    uint8_t* image = ui_packed_asset_unpack(lock_4bpp_p4b_deflate_start,
        (size_t)(lock_4bpp_p4b_deflate_end - lock_4bpp_p4b_deflate_start), &image_bytes);
    if (!image) return false;
    bool valid = ui_image_validate(image, image_bytes, epd_width(), epd_height());
    free(image);
    if (!valid) ESP_LOGE(TAG, "Packed lock image validation failed");
    return valid;
}

void app_main(void) {
    read_pico_handle_t hw;
    if (read_pico_init(&hw) != ESP_OK) return;
    app_settings_init();
    const bool vcom_ok = resolve_vcom_at_boot();
    pmu_selftest_bind(hw.sensor);
    app_sleep_bind(hw.touch, hw.sensor);
    const bool st_resume = pmu_selftest_boot_resume();

    if (!lock_image_matches_panel()) return;
    size_t loading_size = 0;
    uint8_t* loading = ui_packed_asset_unpack(loading_4bpp_p4b_deflate_start,
        (size_t)(loading_4bpp_p4b_deflate_end - loading_4bpp_p4b_deflate_start), &loading_size);
    if (!loading) return;
    if (!ui_image_validate(loading, loading_size, epd_width(), epd_height())) {
        ESP_LOGE(TAG, "Packed splash image validation failed");
        free(loading);
        return;
    }

    EpdiyHighlevelState hl = hw.hl;
    uint8_t* framebuffer = hw.framebuffer;

    epd_poweron();
    read_pico_i2c_census_take();
    epd_clear();
    epd_hl_set_all_white(&hl);
    if (!ui_image_decode(framebuffer,(size_t)epd_width()*epd_height()/2,
                         loading,loading_size,
                         epd_width(),epd_height(),(ui_image_rotation_t)epd_get_rotation())) {
        ESP_LOGE(TAG,"Packed splash decode failed");
        free(loading);
        return;
    }
    update_display_from_white(&hl);
    free(loading);

    ttf_font_init();

    if (!hw.touch_ready) {
        ESP_LOGE(TAG, "No touch controller, UI cannot run");
        return;
    }

    if (hw.pmu_ready && !vcom_ok) {
        vcom_setup_run(&hl, framebuffer, hw.touch);
    }

    app_loop_run(&(app_loop_config_t){
        .hl = &hl,
        .fb = framebuffer,
        .acc = hw.sensor,
        .tp = hw.touch,
        .sensor_ready = hw.sensor_ready,
        // 自检跑到一半断电的话，重新上电直接回自检页接着跑。
        // Resume the self-test page if a power-cut interrupted it.
        .first_app = st_resume ? app_selftest_page() : app_home_page(),
    });
}

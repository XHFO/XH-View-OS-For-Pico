/* SPDX-License-Identifier: Apache-2.0
 * 中文：实际 app_book.c 的宿主入口；保留生产绘图、手势与4位 framebuffer 存储。
 * English: Host entry to actual app_book.c; retain production paint, gestures and packed 4-bit framebuffer storage.
 */
#include "app.h"
#include "app_book.h"
#include "pico_dual.h"
#include "pico_system.h"
#include "pico_annotations.h"
#include "app_registry.h"
#include "book_layout.h"
#include "book_policy.h"
#include "book_progress.h"
#include "book_source.h"
#include "book_epub.h"
#include "book_store.h"
#include "display.h"
#include "e0470_page_turn.h"
#include "e0470_epaper_waveform.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "read_pico_init.h"
#include "read_pico_sd.h"
#include "read_pico_search.h"
#include "settings.h"
#include "ttf_font.h"
#include "ui_kit.h"
#include "ui_gesture.h"
#include "ui_menu.h"
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

/* 中文：网页没有加速度计；保留页面代码但明确报告此硬件接口不可用。
 * English: The browser has no accelerometer; retain the page code and report its hardware interface unavailable.
 */
typedef struct { int odr, fs; } sc7a20h_sensor_config_t;
typedef struct { int unused; } sc7a20h_aoi_cfg_t;
typedef struct { unsigned aoi2_src; } sc7a20h_events_t;
enum { SC7A20H_ODR_100 = 100, SC7A20H_FS_2G = 2, SC7A20H_AOI2 = 2 };
static const sc7a20h_sensor_config_t *sc7a20h_get_config(sc7a20h_handle_t h) { (void)h; static const sc7a20h_sensor_config_t c={0}; return &c; }
static esp_err_t sc7a20h_apply_config(sc7a20h_handle_t h,const sc7a20h_sensor_config_t *c) { (void)h;(void)c;return ESP_ERR_NOT_SUPPORTED; }
static esp_err_t sc7a20h_activity_config(sc7a20h_handle_t h,int threshold,int duration) { (void)h;(void)threshold;(void)duration;return ESP_ERR_NOT_SUPPORTED; }
static esp_err_t sc7a20h_aoi_config(sc7a20h_handle_t h,int aoi,const sc7a20h_aoi_cfg_t *c) { (void)h;(void)aoi;(void)c;return ESP_ERR_NOT_SUPPORTED; }
static bool sc7a20h_powered(sc7a20h_handle_t h) { (void)h;return false; }
static esp_err_t sc7a20h_read_events(sc7a20h_handle_t h,sc7a20h_events_t *e) { (void)h;(void)e;return ESP_ERR_NOT_SUPPORTED; }
#ifndef pdTRUE
#define pdTRUE 1
#endif
// 未启用宿主预绘任务，二值等待信号不会被使用。/ No host pre-render task runs, so the binary completion signal is unused.
#define xSemaphoreCreateBinary xSemaphoreCreateMutex
#include "native-preview-book-source.inc"
#include "native_preview_book.h"

static void native_preview_book_raw(uint16_t x, uint16_t y, uint16_t *a, uint16_t *b) {
    switch (epd_get_rotation()) {
        case EPD_ROT_LANDSCAPE: *a = UI_LOCK_WIDTH - 1 - y; *b = x; break;
        case EPD_ROT_PORTRAIT: *a = UI_LOCK_WIDTH - 1 - x; *b = UI_LOCK_HEIGHT - 1 - y; break;
        case EPD_ROT_INVERTED_LANDSCAPE: *a = y; *b = UI_LOCK_HEIGHT - 1 - x; break;
        default: *a = x; *b = y; break;
    }
}
app_redraw_t native_preview_book_gesture(app_ctx_t *ctx, const ui_gesture_event_t *event) {
    ui_gesture_event_t raw = *event;
    native_preview_book_raw(event->x0, event->y0, &raw.x0, &raw.y0);
    native_preview_book_raw(event->x, event->y, &raw.x, &raw.y);
    return app_book.on_gesture(ctx, &raw);
}
const char *native_preview_book_path(void) { return s_path; }
const char *native_preview_book_kind(void) { return book_kind()==BOOK_KIND_EPUB ? "epub" : "txt"; }
const char *native_preview_book_view(void) {
    if (s_open_path[0] || s_dual_wait) return "loading";
    if (s_page_jump_open) return "page-jump";
    if (s_image_confirm_open) return "image-confirm";
    if (s_cover_open) return "cover";
    if (s_image_open) return "image";
    static const char *names[]={"shelf","reading","toc","manage","bulk","search"};
    return names[s_view];
}
size_t native_preview_book_page(void) { return s_page; }
size_t native_preview_book_chapter(void) { return s_chapter; }
size_t native_preview_book_pages(void) { return book_layout_page_count(); }
bool native_preview_book_jump_open(void) { return s_page_jump_open; }
bool native_preview_book_tools_open(void) { return s_toolbar; }
bool native_preview_book_selection_active(void) { return s_selection; }


size_t native_preview_book_inline_images(void) {
    size_t count = 0;
    for (size_t i = 0; i < s_block_count; ++i) count += s_blocks[i].image != NULL;
    return count;
}
size_t native_preview_book_image_bytes(void) {
    size_t bytes = 0;
    for (size_t i = 0; i < s_block_count; ++i)
        if (s_blocks[i].image) bytes += (size_t)s_blocks[i].image_width * s_blocks[i].image_height;
    return bytes;
}



uint32_t native_preview_dual_target(void){return s_dual_target;}
unsigned native_preview_book_px(void){return s_px;}
uint32_t native_preview_dual_page(void){return s_dual_displayed;}
bool native_preview_dual_blank(void){return s_dual_blank;}
size_t native_preview_page_offset(bool end){return book_layout_page_start_offset(s_page+(end?1:0));}

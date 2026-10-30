/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 *
 * 原生演示控制器与真实 book_layout 的主机检查，使用模拟字体和 UI。
 * Host checks for the native preview controller and real book_layout, using
 * mock font metrics and UI. Page counts are logic fixtures, not device results.
 *
 * 冻结：不链接板级驱动、不访问串口、不改固件；资源由测试运行器映射。
 * Frozen: no board drivers, serial access or firmware changes. The runner maps
 * the real embedded resources and builds in a temporary directory.
 */

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "app.h"
#include "ttf_font.h"
#include "ui_kit.h"

static int mock_weight = 500;
static size_t painted, menu_handles;
static uint64_t pixel_hash;
static char mock_font_path[160] = "builtin";
static const app_desc_t mock_home = {0};
const app_desc_t app_font_pick = {0};

const app_desc_t* app_home_page(void) { return &mock_home; }
int ttf_get_weight(void) { return mock_weight; }
void ttf_set_weight(int weight) { mock_weight = weight; }
const char* ttf_font_path(void) { return mock_font_path; }
const char* ttf_font_display_name(void) { return "Mock font - not device metrics"; }

// 两种模拟 advance 确保换字体真的改变分页；不是字形渲染。 / Two mock advances force actual repagination without claiming glyph rendering.
int ttf_text_width_px(int px, const char* text) {
    int width = 0;
    for (; *text; ++text) {
        if (((unsigned char)*text & 0xc0) == 0x80) continue;
        int advance = (unsigned char)*text < 128 ? px / 2 : px;
        if (strcmp(mock_font_path, "builtin")) advance = advance * 3 / 4;
        width += advance;
    }
    return width;
}

int ttf_ascender_px(int px) { return px; }
void ttf_draw_text_px(uint8_t* fb, int x, int y, int px, const char* text,
                      enum EpdFontFlags align, uint8_t fg, uint8_t bg) {
    (void)fb; (void)align; (void)fg; (void)bg;
    assert(x >= 0 && x + ttf_text_width_px(px, text) <= 684);
    assert(y >= 0 && y <= 1216);
}

void epd_draw_pixel(int x, int y, uint8_t color, uint8_t* fb) {
    (void)color; (void)fb;
    assert(x >= 0 && x < 684 && y >= 0 && y < 1216);
    ++painted;
    pixel_hash = (pixel_hash ^ (uint32_t)(y * 684 + x)) * UINT64_C(1099511628211);
}
int epd_rotated_display_width(void) { return 684; }
int ui_content_width(void) { return 604; }
void ui_text(uint8_t* fb, int x, int y, int px, const char* text,
             enum EpdFontFlags align, bool inverted) {
    (void)inverted;
    ttf_draw_text_px(fb, x, y, px, text, align, 0, 15);
}
int ui_draw_header(uint8_t* fb, const char* title, const char* subtitle) {
    (void)fb; (void)title; (void)subtitle;
    return UI_CONTENT_TOP;
}
void ui_clear_page(uint8_t* fb) { (void)fb; }
void ui_hairline(uint8_t* fb, int y, int x, int width, uint8_t color) {
    (void)fb; (void)y; (void)x; (void)width; (void)color;
}
EpdRect ui_grid_rect(int column, int columns, int row, int top, int height) {
    int width = (604 - (columns - 1) * 12) / columns;
    return (EpdRect){40 + column * (width + 12), top + row * (height + 12), width, height};
}
bool ui_rect_hit(EpdRect rect, uint16_t x, uint16_t y) {
    return x >= rect.x && y >= rect.y && x < rect.x + rect.width && y < rect.y + rect.height;
}
void ui_draw_button(uint8_t* fb, EpdRect rect, const char* text, bool on) {
    (void)fb; (void)on;
    assert(rect.x >= 0 && rect.x + rect.width <= 684 && rect.y + rect.height <= 1216);
    assert(ttf_text_width_px(34, text) <= rect.width);
}
void ui_draw_menu_handle(uint8_t* fb, bool open) { (void)fb; (void)open; ++menu_handles; }

static size_t host_strlcpy(char* out, const char* source, size_t capacity) {
    size_t length = strlen(source);
    if (capacity) {
        size_t copied = length < capacity - 1 ? length : capacity - 1;
        memcpy(out, source, copied);
        out[copied] = 0;
    }
    return length;
}
#ifdef strlcpy
#undef strlcpy
#endif
#define strlcpy host_strlcpy

// 同一翻译单元检查静态状态；编译的控制器仍是实际固件源。 / Inspect static state in the same translation unit while compiling the actual firmware controller.
#include "../main/apps/app_preview_reading.c"

static void complete_layout(app_ctx_t* ctx) {
    for (int i = 0; i < 6000 && s_layout && (!book_layout_complete() || s_seek_pending); ++i) {
        ctx->now_ms += 100;
        app_preview_reading.on_tick(ctx);
    }
    assert(s_layout && !s_seek_pending && book_layout_complete());
    size_t pages = book_layout_page_count();
    assert(pages > 0);
    for (size_t page = 0; page < pages; ++page) {
        size_t start = book_layout_page_start_offset(page);
        assert(start < book_layout_page_start_offset(page + 1));
        assert((preview_text_start[start] & 0xc0) != 0x80);
    }
    assert(book_layout_page_start_offset(pages) == (size_t)(preview_text_end - preview_text_start));
}

static void checked_render(app_ctx_t* ctx) {
    uint8_t fb[1];
    size_t before = menu_handles;
    app_preview_reading.render(ctx, fb);
    assert(menu_handles == before + 1);
}

int main(void) {
    app_ctx_t ctx = {0};
    assert(app_preview_reading.owns_keys && app_preview_reading.enter_full);
    preview_reading_open(0);
    app_preview_reading.on_enter(&ctx);
    assert(s_section == PREVIEW_TEXT && s_layout && book_layout_page_count() >= 2);
    complete_layout(&ctx);
    size_t total = book_layout_page_count();
    assert(total > 50);
    checked_render(&ctx);

    for (int i = 0; i < 6; ++i) app_preview_reading.on_key(&ctx, UI_KEY_3);
    assert(s_page == 6);
    size_t anchor = book_layout_page_start_offset(s_page);
    change_size(4);
    complete_layout(&ctx);
    assert(s_offset == anchor && s_page == book_layout_page_for_offset(anchor));
    size_t wide_pages = book_layout_page_count();
    checked_render(&ctx);

    strcpy(mock_font_path, "/mock/narrow.ttf");
    ctx.now_ms += 100;
    app_preview_reading.on_tick(&ctx);
    complete_layout(&ctx);
    assert(!strcmp(s_font_path, mock_font_path));
    assert(s_offset == anchor && s_page == book_layout_page_for_offset(anchor));
    assert(book_layout_page_count() < wide_pages);
    checked_render(&ctx);
    strcpy(mock_font_path, "builtin");
    ctx.now_ms += 100;
    app_preview_reading.on_tick(&ctx);
    complete_layout(&ctx);
    assert(s_offset == anchor && book_layout_page_count() == wide_pages);

    app_preview_reading.on_key(&ctx, UI_KEY_2);
    assert(s_tools);
    app_preview_reading.on_key(&ctx, UI_KEY_1);
    assert(s_tool == PREVIEW_TOOLS - 1);
    app_preview_reading.on_key(&ctx, UI_KEY_3);
    assert(s_tool == 0);
    checked_render(&ctx);
    EpdRect button = tool_rect(1);
    ui_gesture_event_t tap = {.type = UI_GESTURE_TAP, .x0 = button.x + 5, .y0 = button.y + 5};
    app_preview_reading.on_gesture(&ctx, &tap);
    assert(s_section == PREVIEW_COMIC && !s_tools && s_comic_valid);

    uint64_t comic_hashes[8];
    for (int i = 0; i < 8; ++i) {
        assert(s_comic_page == i);
        painted = 0;
        pixel_hash = UINT64_C(1469598103934665603);
        checked_render(&ctx);
        assert(painted > 10000);
        comic_hashes[i] = pixel_hash;
        for (int previous = 0; previous < i; ++previous) assert(comic_hashes[previous] != pixel_hash);
        app_preview_reading.on_key(&ctx, UI_KEY_3);
    }
    assert(s_comic_page == 7 && turn_page(1) == APP_REDRAW_NONE);
    ui_gesture_event_t gesture = {.type = UI_GESTURE_SWIPE_R};
    app_preview_reading.on_gesture(&ctx, &gesture);
    assert(s_comic_page == 6);
    gesture.type = UI_GESTURE_SWIPE_L;
    app_preview_reading.on_gesture(&ctx, &gesture);
    assert(s_comic_page == 7);
    gesture.type = UI_GESTURE_CANCEL;
    assert(app_preview_reading.on_gesture(&ctx, &gesture) == APP_REDRAW_NONE);

    execute_tool(&ctx, 2);
    for (int i = 0; i < 3; ++i) {
        assert(s_specimen == i);
        checked_render(&ctx);
        app_preview_reading.on_key(&ctx, UI_KEY_3);
    }
    assert(s_specimen == 0);
    execute_tool(&ctx, 6);
    assert(s_weight == 700 && mock_weight == 700);
    checked_render(&ctx);
    gesture.type = UI_GESTURE_LONG_PRESS;
    app_preview_reading.on_gesture(&ctx, &gesture);
    assert(s_tools);
    checked_render(&ctx);
    execute_tool(&ctx, 3);
    assert(ctx.request_app == &app_font_pick);
    ctx.request_app = NULL;
    execute_tool(&ctx, 8);
    assert(ctx.request_app == &mock_home);
    ctx.request_app = NULL;
    app_preview_reading.on_key_long(&ctx, UI_KEY_2);
    assert(ctx.request_menu);
    app_preview_reading.on_exit(&ctx);
    assert(mock_weight == 500 && book_layout_page_count() == 0);

    preview_reading_open(0);
    assert(s_section == PREVIEW_FONT);
    app_preview_reading.on_enter(&ctx);
    assert(s_section == PREVIEW_TEXT);
    complete_layout(&ctx);
    assert(s_offset == anchor && s_page == book_layout_page_for_offset(anchor));
    app_preview_reading.on_exit(&ctx);
    assert(mock_weight == 500 && book_layout_page_count() == 0);

    printf("Passed native preview host logic: %zu pages with MOCK metrics (not device pagination).\n", total);
    puts("Checked real long-text/source spans; size and differing-font reflow anchors; all 8 bounded RLE pages;");
    puts("specimen/tool bounds, menu handles, keys/swipes/cancel, explicit home/font/menu exits and cleanup.");
    puts("No serial access or firmware writes. Glyph appearance, EPD refresh, touch timing and MCU performance are untested here.");
    return 0;
}

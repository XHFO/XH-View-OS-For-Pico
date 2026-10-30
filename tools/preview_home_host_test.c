/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 *
 * 中文：首页状态、三入口、虚拟返回、菜单、绘制范围和分钟刷新节流的宿主检查。
 * English: Host checks for home state, routes, back/menu, drawing bounds, and clock cadence.
 *
 * 冻结：使用真实 app/UI/PMU 头文件和测试服务；不验证实际字体或任何物理硬件。
 * Frozen: Real app/UI/PMU headers with test services; no real typography or physical validation.
 */

#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "app.h"
#include "read_pico_pmu.h"
#include "ttf_font.h"
#include "ui_gesture.h"
#include "ui_kit.h"
#include "ui_menu.h"

static pmu_snapshot_t s_pmu;
static bool s_pmu_present = true;
static time_t s_system_time;
static int s_time_reads;
static int s_pmu_reads;
static int s_selected = -1;
static unsigned s_geometry_checks;
static unsigned s_menu_draws;
static bool s_drew_disconnected;
static bool s_drew_example_time;
static bool s_drew_utc_time;

const app_desc_t app_preview_reading = {0};

void preview_reading_open(int section) { s_selected = section; }

const pmu_snapshot_t* read_pico_pmu_get(void) {
    return s_pmu_present ? &s_pmu : NULL;
}

esp_err_t read_pico_pmu_refresh(void) {
    ++s_pmu_reads;
    return ESP_OK;
}

static time_t host_time(time_t* output) {
    ++s_time_reads;
    if (output) *output = s_system_time;
    return s_system_time;
}

static void check_rect(EpdRect rect) {
    assert(rect.x >= 0 && rect.y >= 0 && rect.width >= 0 && rect.height >= 0);
    assert(rect.x + rect.width <= UI_LOCK_WIDTH);
    assert(rect.y + rect.height <= UI_LOCK_HEIGHT);
    ++s_geometry_checks;
}

// 仅保守估算文字框，实际字体由独立字形测试覆盖。/ Conservative text-box estimate only; actual fonts are checked separately.
int ttf_text_width_px(int px, const char* text) {
    int width = 0;
    for (size_t at = 0; text[at];) {
        unsigned char first = (unsigned char)text[at];
        size_t bytes = first < 0x80 ? 1 : first < 0xe0 ? 2 : first < 0xf0 ? 3 : 4;
        for (size_t i = 1; i < bytes; ++i) assert(((unsigned char)text[at + i] & 0xc0) == 0x80);
        width += first < 0x80 ? px * 3 / 5 : px;
        at += bytes;
    }
    return width;
}

void ui_text(uint8_t* framebuffer, int x, int y, int px, const char* text,
             enum EpdFontFlags align, bool inverted) {
    (void)framebuffer;
    (void)inverted;
    int width = ttf_text_width_px(px, text);
    if (align == EPD_DRAW_ALIGN_CENTER) x -= width / 2;
    if (align == EPD_DRAW_ALIGN_RIGHT) x -= width;
    check_rect((EpdRect){x, y, width, px});
    if (strcmp(text, "尚未连接") == 0) s_drew_disconnected = true;
    if (strcmp(text, "示例时间 · 尚未同步") == 0) s_drew_example_time = true;
    if (strcmp(text, "设备时间 · UTC") == 0) s_drew_utc_time = true;
}

void ui_draw_round_rect(uint8_t* framebuffer, EpdRect rect, int radius, uint8_t color) {
    (void)framebuffer;
    (void)radius;
    (void)color;
    check_rect(rect);
}

void ui_fill_round_rect(uint8_t* framebuffer, EpdRect rect, int radius, uint8_t color) {
    ui_draw_round_rect(framebuffer, rect, radius, color);
}

void epd_fill_rect(EpdRect rect, uint8_t color, uint8_t* framebuffer) {
    (void)color;
    (void)framebuffer;
    check_rect(rect);
}

void ui_hairline(uint8_t* framebuffer, int y, int x, int width, uint8_t color) {
    epd_fill_rect((EpdRect){x, y, width, 1}, color, framebuffer);
}

void epd_draw_circle(int x, int y, int radius, uint8_t color, uint8_t* framebuffer) {
    epd_fill_rect((EpdRect){x - radius, y - radius, 2 * radius, 2 * radius}, color, framebuffer);
}

void epd_draw_line(int x0, int y0, int x1, int y1, uint8_t color, uint8_t* framebuffer) {
    (void)color;
    (void)framebuffer;
    check_rect((EpdRect){x0, y0, 1, 1});
    check_rect((EpdRect){x1, y1, 1, 1});
}

void ui_draw_button(uint8_t* framebuffer, EpdRect rect, const char* label, bool on) {
    (void)label;
    (void)on;
    ui_draw_round_rect(framebuffer, rect, 0, UI_GRAY_BLACK);
}

void ui_draw_menu_handle(uint8_t* framebuffer, bool open) {
    (void)framebuffer;
    assert(!open);
    ++s_menu_draws;
}

int ui_draw_header(uint8_t* framebuffer, const char* title, const char* subtitle) {
    ui_text(framebuffer, UI_MARGIN, UI_HEADER_TITLE_Y, UI_PX_TITLE, title, EPD_DRAW_ALIGN_LEFT, false);
    ui_text(framebuffer, UI_MARGIN, UI_HEADER_SUB_Y, UI_PX_SUB, subtitle, EPD_DRAW_ALIGN_LEFT, false);
    return UI_CONTENT_TOP;
}

void ui_clear_page(uint8_t* framebuffer) { (void)framebuffer; }

EpdRect ui_bar_rect(int column, int columns) {
    int right = UI_LOCK_WIDTH - UI_BAR_MARGIN - UI_MENU_BTN_SIZE - UI_GAP;
    int width = (right - UI_MARGIN - (columns - 1) * UI_GAP) / columns;
    return (EpdRect){UI_MARGIN + column * (width + UI_GAP), UI_BAR_TOP, width, UI_BAR_H};
}

bool ui_rect_hit(EpdRect rect, uint16_t x, uint16_t y) {
    return x >= rect.x && y >= rect.y && x < rect.x + rect.width && y < rect.y + rect.height;
}

int ui_bar_hit(uint16_t x, uint16_t y, int columns) {
    for (int column = 0; column < columns; ++column)
        if (ui_rect_hit(ui_bar_rect(column, columns), x, y)) return column;
    return -1;
}

// 固定时间使校时、UTC 和跨午夜检查可重复。/ Fix time for reproducible synchronization, UTC, and midnight checks.
#define time host_time
#include "../main/apps/app_preview_home.c"
#undef time

static app_redraw_t tap(app_ctx_t* ctx, int x, int y) {
    const ui_gesture_event_t event = {
        .type = UI_GESTURE_TAP, .x0 = x, .y0 = y, .x = x, .y = y,
    };
    return app_preview_home.on_gesture(ctx, &event);
}

static void render_checked(app_ctx_t* ctx) {
    uint8_t framebuffer = 0;
    const int pmu_reads = s_pmu_reads;
    const int time_reads = s_time_reads;
    const unsigned menu_draws = s_menu_draws;
    const int view = s_view;
    const app_desc_t* request = ctx->request_app;
    app_preview_home.render(ctx, &framebuffer);
    assert(s_pmu_reads == pmu_reads && s_time_reads == time_reads);
    assert(s_view == view && ctx->request_app == request);
    assert(s_menu_draws == menu_draws + 1);
}

static void test_routes_and_returns(app_ctx_t* ctx) {
    const int positions[3][2] = {{300, 300}, {150, 600}, {450, 600}};
    for (int section = 0; section < 3; ++section) {
        s_selected = -1;
        ctx->request_app = NULL;
        tap(ctx, positions[section][0], positions[section][1]);
        assert(s_selected == section && ctx->request_app == &app_preview_reading);
    }
    ctx->request_app = NULL;
    assert(tap(ctx, 300, 780) == APP_REDRAW_FULL);
    assert(s_view == PREVIEW_HA && !ctx->request_app);
    render_checked(ctx);
    assert(s_drew_disconnected);
    assert(tap(ctx, 70, 50) == APP_REDRAW_FULL && s_view == PREVIEW_HOME);
    tap(ctx, 300, 780);
    s_selected = -1;
    tap(ctx, 250, 960);
    assert(s_selected == 0 && ctx->request_app == &app_preview_reading);
    ctx->request_app = NULL;
    assert(app_preview_home.on_key(ctx, UI_KEY_1) == APP_REDRAW_FULL);
    assert(s_view == PREVIEW_HOME);
    app_preview_home.on_key(ctx, UI_KEY_1);
    assert(ctx->request_app == &app_preview_reading);
    ctx->request_app = NULL;
    assert(app_preview_home.on_key(ctx, UI_KEY_2) == APP_REDRAW_FULL);
    assert(app_preview_home.on_key(ctx, UI_KEY_3) == APP_REDRAW_NONE && ctx->request_menu);
    ctx->request_menu = false;
    const ui_gesture_event_t press = {.type = UI_GESTURE_PRESS, .x0 = 150, .y0 = 600};
    assert(app_preview_home.on_gesture(ctx, &press) == APP_REDRAW_NONE && !ctx->request_app);
    const ui_gesture_event_t moved_tap = {
        .type = UI_GESTURE_TAP, .x0 = 150, .y0 = 600, .x = 450, .y = 600,
    };
    assert(app_preview_home.on_gesture(ctx, &moved_tap) == APP_REDRAW_NONE && !ctx->request_app);
}

static void test_clock(app_ctx_t* ctx) {
    assert(tap(ctx, 200, 950) == APP_REDRAW_FULL && s_view == PREVIEW_CLOCK);
    assert(!s_clock_live && s_clock_hour == 12 && s_clock_minute == 34);
    render_checked(ctx);
    assert(s_drew_example_time);
    ctx->now_ms += 120000;
    assert(app_preview_home.on_tick(ctx) == APP_REDRAW_NONE);
    const ui_gesture_event_t swipe = {.type = UI_GESTURE_SWIPE_R};
    assert(app_preview_home.on_gesture(ctx, &swipe) == APP_REDRAW_FULL && s_view == PREVIEW_HOME);

    s_pmu.time_ok = true;
    s_pmu.time_synced = 1;
    s_pmu.unix_sec = 1704153540u;
    tap(ctx, 200, 950);
    assert(s_clock_live && s_clock_hour == 23 && s_clock_minute == 59);
    render_checked(ctx);
    assert(s_drew_utc_time);
    assert(app_preview_home.on_tick(ctx) == APP_REDRAW_NONE);
    ctx->now_ms += 60000;
    ctx->consumed = true;
    assert(app_preview_home.on_tick(ctx) == APP_REDRAW_NONE);
    ctx->consumed = false;
    assert(app_preview_home.on_tick(ctx) == APP_REDRAW_PAGE);
    assert(s_clock_hour == 0 && s_clock_minute == 0);
    assert(strcmp(s_clock_date, "2024 / 01 / 02") == 0);
    assert(app_preview_home.on_tick(ctx) == APP_REDRAW_NONE);
    render_checked(ctx);
    assert(tap(ctx, 120, UI_BAR_TOP + 40) == APP_REDRAW_FULL && s_view == PREVIEW_HOME);

    s_pmu.time_synced = 0;
    s_system_time = 1704200000;
    tap(ctx, 200, 950);
    assert(s_clock_live && s_clock_epoch == s_system_time);
    assert(tap(ctx, 400, UI_BAR_TOP + 40) == APP_REDRAW_FULL);
    app_preview_home.on_key(ctx, UI_KEY_1);
    assert(s_view == PREVIEW_HOME);
}

static void test_battery(app_ctx_t* ctx) {
    app_preview_home.on_enter(ctx);
    assert(strstr(s_battery, "84%") && strstr(s_battery, "充电中"));
    render_checked(ctx);
    s_pmu.qb_flags = 0;
    app_preview_home.on_enter(ctx);
    assert(strstr(s_battery, "4.03 V") && !strstr(s_battery, "%"));
    render_checked(ctx);
    s_pmu_present = false;
    app_preview_home.on_enter(ctx);
    assert(strstr(s_battery, "未读到"));
    render_checked(ctx);
}

int main(void) {
    app_ctx_t ctx = {.now_ms = 1000};
    s_pmu.status_ok = true;
    s_pmu.flags = PMU_STATUS_BATTERY_VALID;
    s_pmu.battery_mv = 4033;
    s_pmu.soc_permille = 840;
    s_pmu.qb_flags = 3;
    s_pmu.charge_state = PMU_CHARGE_CHARGING;
    assert(app_preview_home.enter_full && app_preview_home.owns_keys);
    assert(app_preview_home.blocks_idle_sleep);
    app_preview_home.on_enter(&ctx);
    render_checked(&ctx);
    test_routes_and_returns(&ctx);
    test_clock(&ctx);
    test_battery(&ctx);
    printf("Preview home host checks passed: reading/comic/font routes, HA disconnected, "
           "back/menu/keys, battery fallback, UTC midnight and minute cadence, render purity; "
           "%u draw bounds checks. No physical hardware or real-font acceptance.\n", s_geometry_checks);
    return 0;
}

/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 *
 * 中文：原生系统演示首页，提供阅读、漫画、字体和时钟外观的入口。
 * English: Native system demo home with reading, comic, typography, and clock entries.
 *
 * 冻结：家庭控制尚未接入，必须明确未连接；时钟页不是休眠实现。
 * Frozen: Home Assistant is not integrated and must read disconnected; the clock is not sleep.
 * 冻结：只读电池及时间，不写 PMU、设置、面板参数或网络凭据；render 纯绘制。
 * Frozen: Read battery/time only; no PMU, settings, panel, or credential writes; render only paints.
 * 冻结：诊断演示页不自动空闲关机，保留电源键和用户睡眠设置。
 * Frozen: Diagnostic demo pages stay awake on idle; retain power-key behavior and saved sleep settings.
 */

#include <stdio.h>
#include <time.h>

#include "app.h"
#include "read_pico_pmu.h"
#include "ttf_font.h"
#include "ui_gesture.h"
#include "ui_kit.h"
#include "ui_menu.h"

extern const app_desc_t app_preview_reading;
void preview_reading_open(int section);

enum {
    PREVIEW_HOME,
    PREVIEW_HA,
    PREVIEW_CLOCK,
};

enum {
    HIT_NONE = -1,
    HIT_READING,
    HIT_COMIC,
    HIT_FONT,
    HIT_HA,
    HIT_CLOCK,
    HIT_BACK,
    HIT_REFRESH,
};

static int s_view = PREVIEW_HOME;
static char s_battery[112];
static bool s_clock_live;
static time_t s_clock_epoch;
static int64_t s_clock_base_ms;
static int64_t s_clock_next_ms;
static int s_clock_hour = 12;
static int s_clock_minute = 34;
static char s_clock_date[40];

static const EpdRect s_reading = {40, 186, 604, 302};
static const EpdRect s_comic = {40, 510, 290, 176};
static const EpdRect s_font = {350, 510, 294, 176};
static const EpdRect s_ha = {40, 710, 604, 146};
static const EpdRect s_clock = {40, 880, 604, 148};
static const EpdRect s_back = {40, 26, 138, 64};

// 字体宽度按实际字形收缩，避免换用 TF 字体后把标签画出卡片。/ Fit actual glyph widths so TF fonts cannot spill outside cards.
static void fit_text(uint8_t* fb, int x, int y, int px, int max_width, const char* text) {
    while (px > 22 && ttf_text_width_px(px, text) > max_width) --px;
    ui_text(fb, x, y, px, text, EPD_DRAW_ALIGN_LEFT, false);
}

static void card(uint8_t* fb, EpdRect rect) {
    ui_draw_round_rect(fb, rect, 16, UI_GRAY_BLACK);
}

static void book_icon(uint8_t* fb, int x, int y) {
    ui_draw_round_rect(fb, (EpdRect){x, y, 146, 204}, 8, UI_GRAY_BLACK);
    epd_fill_rect((EpdRect){x + 13, y + 1, 4, 202}, UI_GRAY_BLACK, fb);
    ui_hairline(fb, y + 48, x + 37, 78, UI_GRAY_BLACK);
    ui_hairline(fb, y + 60, x + 37, 78, UI_GRAY_BLACK);
    ui_hairline(fb, y + 72, x + 37, 52, UI_GRAY_BLACK);
    ui_text(fb, x + 37, y + 108, 42, "河", EPD_DRAW_ALIGN_LEFT, false);
}

static void draw_footer(uint8_t* fb) {
    ui_draw_button(fb, ui_bar_rect(0, 2), s_view == PREVIEW_HOME ? "开始阅读" : "返回首页", false);
    ui_draw_button(fb, ui_bar_rect(1, 2), "强刷", false);
    ui_draw_menu_handle(fb, false);
}

static void draw_home(uint8_t* fb) {
    ui_draw_header(fb, "小纸 Pico", "0.15.0-hw2 原生演示 · 阅读优先");
    card(fb, s_reading);
    book_icon(fb, 65, 224);
    fit_text(fb, 240, 222, 27, 376, "长文阅读演示");
    fit_text(fb, 240, 275, 50, 376, "沿河的书店");
    fit_text(fb, 240, 345, 28, 376, "24 章 · 真实文字排版");
    fit_text(fb, 240, 405, 32, 376, "开始阅读  >");

    card(fb, s_comic);
    fit_text(fb, 64, 535, 40, 242, "漫画演示");
    fit_text(fb, 64, 601, 28, 242, "雨夜书店 · 8 页");
    card(fb, s_font);
    fit_text(fb, 374, 535, 40, 246, "字体试读");
    fit_text(fb, 374, 601, 28, 246, "字号 · 中英标点");

    card(fb, s_ha);
    fit_text(fb, 64, 733, 36, 452, "Home Assistant");
    fit_text(fb, 64, 792, 28, 550, "家庭控制 · 尚未连接");
    epd_draw_circle(594, 752, 15, UI_GRAY_BLACK, fb);

    card(fb, s_clock);
    fit_text(fb, 64, 904, 38, 550, "翻页时钟");
    fit_text(fb, 64, 964, 28, 550, "大字外观预览 · 轻点查看");
    fit_text(fb, 40, 1046, 25, 604, s_battery);
}

static void draw_back_header(uint8_t* fb, const char* title, const char* subtitle) {
    ui_text(fb, 40, 34, 34, "< 返回", EPD_DRAW_ALIGN_LEFT, false);
    ui_text(fb, 644, 34, 34, title, EPD_DRAW_ALIGN_RIGHT, false);
    fit_text(fb, 40, 112, 28, 604, subtitle);
    ui_hairline(fb, 160, 40, 604, UI_GRAY_BLACK);
}

static void draw_ha(uint8_t* fb) {
    draw_back_header(fb, "家庭控制", "Home Assistant");
    epd_draw_circle(342, 335, 65, UI_GRAY_BLACK, fb);
    epd_draw_line(308, 335, 376, 335, UI_GRAY_BLACK, fb);
    ui_text(fb, 342, 450, 58, "尚未连接", EPD_DRAW_ALIGN_CENTER, false);
    fit_text(fb, 76, 556, 34, 532, "本次为设备原生界面演示。");
    fit_text(fb, 76, 615, 34, 532, "网络配对与实体控制尚未实现。");
    ui_hairline(fb, 716, 76, 532, UI_GRAY_LIGHT);
    fit_text(fb, 76, 756, 32, 532, "后续可配置本地与外部地址，");
    fit_text(fb, 76, 812, 32, 532, "按家庭房间整理控制面板。");
    ui_draw_button(fb, (EpdRect){76, 926, 532, 92}, "先去阅读", false);
}

// 四位数字由矩形构成，无巨号字形缓存；只在分钟改变时重绘。/ Rectangle digits need no large glyph cache; repaint only on minute changes.
static void clock_digit(uint8_t* fb, int x, int y, int value) {
    static const uint8_t segments[10] = {0x3f, 0x06, 0x5b, 0x4f, 0x66,
                                         0x6d, 0x7d, 0x07, 0x7f, 0x6f};
    const int width = 82;
    const int height = 272;
    const int thick = 15;
    const int half = height / 2;
    const EpdRect lines[7] = {
        {x + thick, y, width - 2 * thick, thick},
        {x + width - thick, y + thick, thick, half - thick},
        {x + width - thick, y + half + thick, thick, half - 2 * thick},
        {x + thick, y + height - thick, width - 2 * thick, thick},
        {x, y + half + thick, thick, half - 2 * thick},
        {x, y + thick, thick, half - thick},
        {x + thick, y + half, width - 2 * thick, thick},
    };
    for (int i = 0; i < 7; ++i) {
        if (segments[value] & (1u << i)) epd_fill_rect(lines[i], UI_GRAY_WHITE, fb);
    }
}

static void draw_clock(uint8_t* fb) {
    draw_back_header(fb, "翻页时钟", "外观预览 · 电源键仍使用原厂锁屏");
    const int digits[4] = {s_clock_hour / 10, s_clock_hour % 10,
                           s_clock_minute / 10, s_clock_minute % 10};
    const int x[4] = {40, 182, 372, 514};
    for (int i = 0; i < 4; ++i) {
        ui_fill_round_rect(fb, (EpdRect){x[i], 346, 130, 412}, 14, UI_GRAY_BLACK);
        clock_digit(fb, x[i] + 24, 410, digits[i]);
        // 中缝保留翻页卡片的层次，不触发高频动画。/ A middle seam suggests flip cards without high-frequency animation.
        ui_hairline(fb, 552, x[i] + 2, 126, 0x50);
    }
    epd_fill_rect((EpdRect){336, 474, 12, 12}, UI_GRAY_BLACK, fb);
    epd_fill_rect((EpdRect){336, 618, 12, 12}, UI_GRAY_BLACK, fb);
    ui_text(fb, 342, 842, 32, s_clock_live ? "设备时间 · UTC" : "示例时间 · 尚未同步",
            EPD_DRAW_ALIGN_CENTER, false);
    if (s_clock_live) ui_text(fb, 342, 898, 32, s_clock_date, EPD_DRAW_ALIGN_CENTER, false);
    fit_text(fb, 76, 992, 28, 532, "当前展示外观，尚未接入休眠与动画。");
}

static void update_clock(app_ctx_t* ctx, bool resample) {
    if (resample) {
        const pmu_snapshot_t* pmu = read_pico_pmu_get();
        time_t epoch = time(NULL);
        if (pmu != NULL && pmu->time_ok && pmu->time_synced && pmu->unix_sec >= 1704067200u)
            epoch = (time_t)pmu->unix_sec;
        s_clock_live = epoch >= (time_t)1704067200;
        s_clock_epoch = epoch;
        s_clock_base_ms = ctx->now_ms;
        s_clock_next_ms = ctx->now_ms + 1000;
    }
    if (!s_clock_live) {
        s_clock_hour = 12;
        s_clock_minute = 34;
        s_clock_date[0] = '\0';
        return;
    }
    int64_t elapsed = ctx->now_ms - s_clock_base_ms;
    if (elapsed < 0) elapsed = 0;
    const time_t now = s_clock_epoch + (time_t)(elapsed / 1000);
    struct tm tm = {0};
    gmtime_r(&now, &tm);
    s_clock_hour = tm.tm_hour;
    s_clock_minute = tm.tm_min;
    snprintf(s_clock_date, sizeof(s_clock_date), "%04d / %02d / %02d",
             tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday);
}

static void render(app_ctx_t* ctx, uint8_t* fb) {
    (void)ctx;
    ui_clear_page(fb);
    if (s_view == PREVIEW_HA) draw_ha(fb);
    else if (s_view == PREVIEW_CLOCK) draw_clock(fb);
    else draw_home(fb);
    draw_footer(fb);
}

static void on_enter(app_ctx_t* ctx) {
    s_view = PREVIEW_HOME;
    ctx->leaf = 0;
    read_pico_pmu_refresh();
    const pmu_snapshot_t* pmu = read_pico_pmu_get();
    if (pmu != NULL && pmu->status_ok && (pmu->flags & PMU_STATUS_BATTERY_VALID)) {
        const char* charge = pmu->charge_state == PMU_CHARGE_CHARGING ? " · 充电中" : "";
        if ((pmu->qb_flags & 2u) && pmu->soc_permille <= 1000) {
            snprintf(s_battery, sizeof(s_battery), "电量 %u%%%s · 设备菜单见右下角",
                     pmu->soc_permille / 10, charge);
        } else {
            snprintf(s_battery, sizeof(s_battery), "电池 %u.%02u V%s",
                     pmu->battery_mv / 1000, pmu->battery_mv % 1000 / 10, charge);
        }
    } else {
        snprintf(s_battery, sizeof(s_battery), "电池状态未读到 · 设备菜单见右下角");
    }
    update_clock(ctx, true);
}

static app_redraw_t open_reading(app_ctx_t* ctx, int section) {
    preview_reading_open(section);
    ctx->request_app = &app_preview_reading;
    return APP_REDRAW_NONE;
}

static int hit(uint16_t x, uint16_t y) {
    const int bar = ui_bar_hit(x, y, 2);
    if (bar == 0) return s_view == PREVIEW_HOME ? HIT_READING : HIT_BACK;
    if (bar == 1) return HIT_REFRESH;
    if (s_view != PREVIEW_HOME) {
        if (ui_rect_hit(s_back, x, y)) return HIT_BACK;
        if (s_view == PREVIEW_HA && ui_rect_hit((EpdRect){76, 926, 532, 92}, x, y))
            return HIT_READING;
        return HIT_NONE;
    }
    if (ui_rect_hit(s_reading, x, y)) return HIT_READING;
    if (ui_rect_hit(s_comic, x, y)) return HIT_COMIC;
    if (ui_rect_hit(s_font, x, y)) return HIT_FONT;
    if (ui_rect_hit(s_ha, x, y)) return HIT_HA;
    if (ui_rect_hit(s_clock, x, y)) return HIT_CLOCK;
    return HIT_NONE;
}

static app_redraw_t on_gesture(app_ctx_t* ctx, const ui_gesture_event_t* event) {
    if (event->type == UI_GESTURE_SWIPE_R && s_view != PREVIEW_HOME) {
        s_view = PREVIEW_HOME;
        return APP_REDRAW_FULL;
    }
    if (event->type != UI_GESTURE_TAP) return APP_REDRAW_NONE;
    const int action = hit(event->x0, event->y0);
    if (action != hit(event->x, event->y)) return APP_REDRAW_NONE;
    switch (action) {
        case HIT_READING: return open_reading(ctx, 0);
        case HIT_COMIC: return open_reading(ctx, 1);
        case HIT_FONT: return open_reading(ctx, 2);
        case HIT_HA: s_view = PREVIEW_HA; return APP_REDRAW_FULL;
        case HIT_CLOCK: s_view = PREVIEW_CLOCK; update_clock(ctx, true); return APP_REDRAW_FULL;
        case HIT_BACK: s_view = PREVIEW_HOME; return APP_REDRAW_FULL;
        case HIT_REFRESH: return APP_REDRAW_FULL;
        default: return APP_REDRAW_NONE;
    }
}

static app_redraw_t on_key(app_ctx_t* ctx, int key) {
    if (key == UI_KEY_1) {
        if (s_view == PREVIEW_HOME) return open_reading(ctx, 0);
        s_view = PREVIEW_HOME;
        return APP_REDRAW_FULL;
    }
    if (key == UI_KEY_2) return APP_REDRAW_FULL;
    if (key == UI_KEY_3) ctx->request_menu = true;
    return APP_REDRAW_NONE;
}

static app_redraw_t on_tick(app_ctx_t* ctx) {
    if (ctx->consumed || s_view != PREVIEW_CLOCK || !s_clock_live) return APP_REDRAW_NONE;
    if (ctx->now_ms < s_clock_next_ms) return APP_REDRAW_NONE;
    s_clock_next_ms = ctx->now_ms + 1000;
    const int previous_minute = s_clock_minute;
    const int previous_hour = s_clock_hour;
    update_clock(ctx, false);
    if (s_clock_minute != previous_minute || s_clock_hour != previous_hour) return APP_REDRAW_PAGE;
    return APP_REDRAW_NONE;
}

const app_desc_t app_preview_home = {
    .title = "系统演示",
    .detail = "阅读 · 漫画 · 字体 · 时钟外观",
    .enter_full = true,
    .owns_keys = true,
    .blocks_idle_sleep = true,
    .on_enter = on_enter,
    .render = render,
    .on_gesture = on_gesture,
    .on_key = on_key,
    .on_tick = on_tick,
};

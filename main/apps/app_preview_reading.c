/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 *
 * 无 TF 的长文、八页漫画与原生字形样张。复用 book_layout 分批分页，原文映射在 flash。
 * Card-free long text, eight comic pages and native font specimens. Incremental
 * book_layout pagination borrows flash-mapped UTF-8 text.
 *
 * 冻结：只演示内置资源，不宣称 CBZ 导入；漫画为离线转换的 1bpp RLE，绘制不分配整图。
 * 所有排版与字体操作只在主循环回调执行，无后台任务；render 只画图，不切页、不改设置。
 * 三键为上页/工具/下页；工具内提供强刷、返回和字体入口；长按中键保留全局菜单。
 * Frozen: built-in resources only, never claim CBZ import. Comics are offline
 * converted 1bpp RLE, drawn without a decoded-image allocation. Pagination and
 * font operations run on the loop task only. render paints without navigation
 * or settings changes. Keys are previous/tools/next; tools keep full refresh,
 * return and font selection; holding the middle key opens the global menu.
 * 冻结：实机演示期间不自动空闲关机，保留手动电源键行为。
 * Frozen: Stay awake during hardware demonstration while preserving manual power-key behavior.
 */

#include "app.h"
#include "app_registry.h"
#include "book_layout.h"
#include "ttf_font.h"
#include "ui_gesture.h"
#include "ui_kit.h"
#include "ui_menu.h"

#include <stdio.h>
#include <string.h>

#include "esp_log.h"

extern const uint8_t preview_text_start[] asm("_binary_preview_riverside_txt_start");
extern const uint8_t preview_text_end[] asm("_binary_preview_riverside_txt_end");
extern const uint8_t preview_comic_start[] asm("_binary_preview_comic_bin_start");
extern const uint8_t preview_comic_end[] asm("_binary_preview_comic_bin_end");
extern const app_desc_t app_font_pick;

enum { PREVIEW_TEXT, PREVIEW_COMIC, PREVIEW_FONT };
#define PREVIEW_TOOLS 10
#define PREVIEW_COMIC_PAGES 8
#define PREVIEW_COMIC_WIDTH 600
#define PREVIEW_COMIC_HEIGHT 848

static const char* TAG = "preview_reading";
static int s_section;
static int s_requested = -1;
static int s_size = 44;
static int s_weight = 400;
static int s_previous_weight = 400;
static int s_comic_page;
static int s_specimen;
static int s_tool;
static bool s_tools;
static bool s_comic_valid;
static bool s_layout;
static bool s_seek_pending;
static size_t s_page;
static size_t s_offset;
static const char* s_error;
static int64_t s_extend_ms;
static char s_font_path[TTF_FONT_PATH_MAX];

// 首页只请求视图，资源与分页在 on_enter 准备。/ The home page requests a view; on_enter prepares resources and layout.
void preview_reading_open(int section) {
    if (section >= PREVIEW_TEXT && section <= PREVIEW_FONT) s_requested = section;
}

static EpdRect body_rect(void) {
    return (EpdRect){UI_MARGIN, UI_CONTENT_TOP, ui_content_width(), UI_CONTENT_BOTTOM - UI_CONTENT_TOP};
}

static uint16_t read16(const uint8_t* p) { return (uint16_t)p[0] | ((uint16_t)p[1] << 8); }
static uint32_t read32(const uint8_t* p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

// 页表、每页展开长度和每个 RLE 对都先校验；运行时不接受外部数据。 / Validate the table, expansion length and every pair before drawing; runtime accepts no external data.
static bool check_comic(void) {
    size_t size = (size_t)(preview_comic_end - preview_comic_start);
    if (size < 12 + PREVIEW_COMIC_PAGES * 8 || memcmp(preview_comic_start, "PVC1", 4) ||
        read16(preview_comic_start + 4) != PREVIEW_COMIC_WIDTH ||
        read16(preview_comic_start + 6) != PREVIEW_COMIC_HEIGHT ||
        read16(preview_comic_start + 8) != PREVIEW_COMIC_PAGES) return false;
    const size_t expected = PREVIEW_COMIC_WIDTH / 8 * PREVIEW_COMIC_HEIGHT;
    size_t previous = 12 + PREVIEW_COMIC_PAGES * 8;
    for (int page = 0; page < PREVIEW_COMIC_PAGES; ++page) {
        const uint8_t* entry = preview_comic_start + 12 + page * 8;
        size_t offset = read32(entry), length = read32(entry + 4), expanded = 0;
        if (offset != previous || offset > size || length > size - offset || length % 2) return false;
        for (size_t at = 0; at < length; at += 2) {
            size_t run = preview_comic_start[offset + at];
            if (!run || run > expected - expanded) return false;
            expanded += run;
        }
        if (expanded != expected) return false;
        previous = offset + length;
    }
    return previous == size;
}

static void remember_position(void) {
    if (s_section == PREVIEW_TEXT && s_layout && !s_seek_pending) {
        size_t start = book_layout_page_start_offset(s_page);
        size_t end = book_layout_page_start_offset(s_page + 1);
        // 改字号后保留页内原锚点，未翻页时不退回新页首。/ Keep the in-page anchor after reflow until a real page turn.
        if (s_offset < start || s_offset >= end) s_offset = start;
    }
}

static bool finish_seek(void) {
    if (!s_seek_pending || !s_layout) return false;
    size_t pages = book_layout_page_count();
    if (pages && (book_layout_complete() || s_offset < book_layout_page_start_offset(pages))) {
        s_page = book_layout_page_for_offset(s_offset);
        s_seek_pending = false;
        return true;
    }
    return false;
}

static void begin_layout(void) {
    s_error = NULL;
    ttf_set_weight(s_weight);
    strlcpy(s_font_path, ttf_font_path(), sizeof(s_font_path));
    s_layout = book_layout_begin_blocks((const char*)preview_text_start,
        (size_t)(preview_text_end - preview_text_start), NULL, 0, body_rect(), s_size);
    if (!s_layout) {
        s_error = "长文排版未完成，请用工具减小字号或返回。";
        s_seek_pending = false;
        return;
    }
    s_seek_pending = true;
    finish_seek();
    s_extend_ms = 0;
    ESP_LOGI(TAG, "embedded text bytes=%u size=%d weight=%d initial_pages=%u",
             (unsigned)(preview_text_end - preview_text_start), s_size, s_weight,
             (unsigned)book_layout_page_count());
}

static void select_section(int section) {
    remember_position();
    book_layout_free();
    s_layout = false;
    s_section = section;
    s_tools = false;
    s_error = NULL;
    if (s_section == PREVIEW_TEXT) begin_layout();
}

static void on_enter(app_ctx_t* ctx) {
    (void)ctx;
    s_previous_weight = ttf_get_weight();
    ttf_set_weight(s_weight);
    if (s_requested >= PREVIEW_TEXT && s_requested <= PREVIEW_FONT) s_section = s_requested;
    s_requested = -1;
    s_tools = false;
    s_comic_valid = check_comic();
    s_error = NULL;
    if (s_section == PREVIEW_TEXT) begin_layout();
}

static void preview_exit(app_ctx_t* ctx) {
    (void)ctx;
    remember_position();
    book_layout_free();
    s_layout = false;
    ttf_set_weight(s_previous_weight);
}

// 完整字形由所选的真实 TTF 决定；这里没有电脑字体或系统回退。 / Actual selected TTF controls coverage; no desktop font or OS fallback is used.
static void clipped_text(uint8_t* fb, int x, int y, int px, int width, const char* text) {
    char line[192];
    strlcpy(line, text, sizeof(line));
    size_t len = strlen(line);
    while (len && ttf_text_width_px(px, line) > width) {
        do { --len; } while (len && (((uint8_t)line[len] & 0xc0) == 0x80));
        line[len] = 0;
    }
    ui_text(fb, x, y, px, line, EPD_DRAW_ALIGN_LEFT, false);
}

static void draw_footer(uint8_t* fb, const char* line) {
    clipped_text(fb, UI_MARGIN, UI_BAR_TOP + 8, 26, 480, line);
    ui_text(fb, UI_MARGIN, UI_BAR_TOP + 49, 24, "左/右键翻页 · 中键工具", EPD_DRAW_ALIGN_LEFT, false);
}

static void draw_text(uint8_t* fb) {
    char subtitle[112], footer[112];
    snprintf(subtitle, sizeof(subtitle), "内置长文 · 24 章 · 56,673 字 · %dpx", s_size);
    ui_draw_header(fb, "沿河的书店", subtitle);
    if (s_error || !s_layout) {
        clipped_text(fb, UI_MARGIN, 240, 32, ui_content_width(), s_error ? s_error : "正文尚未排版，请返回后重试。");
    } else if (s_seek_pending) {
        ui_text(fb, UI_MARGIN, 240, 36, "正在恢复原文位置…", EPD_DRAW_ALIGN_LEFT, false);
        ui_text(fb, UI_MARGIN, 310, 28, "保留文字位置，逐批重新分页。", EPD_DRAW_ALIGN_LEFT, false);
    } else {
        book_layout_draw_page(fb, s_page, body_rect(), s_size);
    }
    if (s_layout) snprintf(footer, sizeof(footer), "第 %u 页 / %u%s · 字重 %d",
        (unsigned)s_page + 1, (unsigned)book_layout_page_count(), book_layout_complete() ? "" : "+", s_weight);
    else snprintf(footer, sizeof(footer), "内置长文 · 不需要 TF 卡");
    draw_footer(fb, footer);
}

// 按字节流式展开，白色字节直接跳过；不保存解压整页。 / Expand bytes directly, skipping white runs, without retaining a decoded page.
static void draw_comic(uint8_t* fb) {
    char subtitle[96], footer[96];
    snprintf(subtitle, sizeof(subtitle), "原创黑白漫画 · 第 %d / 8 页", s_comic_page + 1);
    ui_draw_header(fb, "雨夜书店", subtitle);
    if (!s_comic_valid) {
        ui_text(fb, UI_MARGIN, 240, 34, "内置漫画资源校验失败。", EPD_DRAW_ALIGN_LEFT, false);
    } else {
        const uint8_t* entry = preview_comic_start + 12 + s_comic_page * 8;
        const uint8_t* data = preview_comic_start + read32(entry);
        size_t length = read32(entry + 4), decoded = 0;
        int left = (epd_rotated_display_width() - PREVIEW_COMIC_WIDTH) / 2;
        for (size_t at = 0; at < length; at += 2) {
            unsigned run = data[at];
            uint8_t pixels = data[at + 1];
            if (pixels == 255) { decoded += run; continue; }
            while (run--) {
                int y = UI_CONTENT_TOP + (int)(decoded / (PREVIEW_COMIC_WIDTH / 8));
                int x = left + (int)(decoded % (PREVIEW_COMIC_WIDTH / 8)) * 8;
                for (int bit = 0; bit < 8; ++bit)
                    if (!(pixels & (0x80 >> bit))) epd_draw_pixel(x + bit, y, UI_GRAY_BLACK, fb);
                ++decoded;
            }
        }
        ui_text(fb, UI_MARGIN, UI_CONTENT_BOTTOM - 40, 26, "预转换图片演示 · 尚未支持 CBZ 导入", EPD_DRAW_ALIGN_LEFT, false);
    }
    snprintf(footer, sizeof(footer), "第 %d / 8 页 · 1bpp 本地资源", s_comic_page + 1);
    draw_footer(fb, footer);
}

static size_t utf8_bytes(uint8_t first) {
    return first < 0x80 ? 1 : first < 0xe0 ? 2 : first < 0xf0 ? 3 : 4;
}

static void wrapped_sample(uint8_t* fb, const char* text, int y, int px) {
    size_t offset = 0, length = strlen(text);
    for (int row = 0; row < 2 && offset < length; ++row) {
        char line[160] = {0};
        size_t count = 0;
        while (offset + count < length) {
            size_t step = utf8_bytes((uint8_t)text[offset + count]);
            if (step > length - offset - count || count + step >= sizeof(line)) break;
            memcpy(line + count, text + offset + count, step);
            line[count + step] = 0;
            if (ttf_text_width_px(px, line) > ui_content_width()) { line[count] = 0; break; }
            count += step;
        }
        if (!count) break;
        ui_text(fb, UI_MARGIN, y + row * (px + px / 2), px, line, EPD_DRAW_ALIGN_LEFT, false);
        offset += count;
    }
}

static void draw_font(uint8_t* fb) {
    static const char* const samples[] = {
        "清晨，树影落在书页上。慢慢读，留白。",
        "繁體閱讀與風景。Read Pico: café, 23.6°C / 48%.",
        "“你好！”（阅读）《书店》0123456789。"
    };
    static const int sizes[] = {36, 44, 52};
    char subtitle[96], footer[96];
    snprintf(subtitle, sizeof(subtitle), "真实字形 · 样张 %d / 3 · 字重 %d", s_specimen + 1, s_weight);
    ui_draw_header(fb, "字体显示测试", subtitle);
    clipped_text(fb, UI_MARGIN, 178, 28, ui_content_width(), ttf_font_display_name());
    for (int i = 0; i < 3; ++i) {
        int top = 242 + i * 242;
        char label[32]; snprintf(label, sizeof(label), "%d px", sizes[i]);
        ui_text(fb, UI_MARGIN, top, 28, label, EPD_DRAW_ALIGN_LEFT, false);
        ui_hairline(fb, top + 38, UI_MARGIN, ui_content_width(), UI_GRAY_LIGHT);
        wrapped_sample(fb, samples[s_specimen], top + 60, sizes[i]);
    }
    ui_text(fb, UI_MARGIN, 1000, 26, "内置为单一 OFL 字族，可用工具切换字重。", EPD_DRAW_ALIGN_LEFT, false);
    ui_text(fb, UI_MARGIN, 1040, 26, "多字体比较需在 TF 卡字体页选用真实 TTF。", EPD_DRAW_ALIGN_LEFT, false);
    snprintf(footer, sizeof(footer), "样张 %d / 3 · 无电脑字体回退", s_specimen + 1);
    draw_footer(fb, footer);
}

static EpdRect tool_rect(int index) { return ui_grid_rect(index % 2, 2, index / 2, 194, 96); }

static void draw_tools(uint8_t* fb) {
    static const char* const labels[] = {"长文阅读", "漫画演示", "字体样张", "卡上字体", "字号减小",
        "字号增大", "切换字重", "清除残影", "返回首页", "继续当前页"};
    ui_draw_header(fb, "阅读工具", "左右键选择 · 中键确认 · 触摸直接操作");
    for (int i = 0; i < PREVIEW_TOOLS; ++i) ui_draw_button(fb, tool_rect(i), labels[i], i == s_tool);
    char detail[96]; snprintf(detail, sizeof(detail), "正文字号 %dpx · 字重 %d", s_size, s_weight);
    ui_text(fb, UI_MARGIN, 790, 32, detail, EPD_DRAW_ALIGN_LEFT, false);
    ui_text(fb, UI_MARGIN, 852, 28, "长文与漫画在本机离线运行，不需要 TF 卡。", EPD_DRAW_ALIGN_LEFT, false);
    ui_text(fb, UI_MARGIN, 900, 28, "当前演示不会修改书库文件或原有阅读进度。", EPD_DRAW_ALIGN_LEFT, false);
    ui_text(fb, UI_MARGIN, 948, 28, "长按中键可打开原厂功能菜单。", EPD_DRAW_ALIGN_LEFT, false);
    draw_footer(fb, "选择工具后可继续阅读");
}

static void render(app_ctx_t* ctx, uint8_t* fb) {
    (void)ctx;
    ui_clear_page(fb);
    if (s_tools) draw_tools(fb);
    else if (s_section == PREVIEW_COMIC) draw_comic(fb);
    else if (s_section == PREVIEW_FONT) draw_font(fb);
    else draw_text(fb);
    ui_draw_menu_handle(fb, false);
}

static app_redraw_t change_size(int delta) {
    int next = s_size + delta;
    if (next < 36 || next > 60) return APP_REDRAW_NONE;
    remember_position();
    s_size = next;
    if (s_section == PREVIEW_TEXT) begin_layout();
    s_tools = false;
    return APP_REDRAW_FULL;
}

static app_redraw_t execute_tool(app_ctx_t* ctx, int index) {
    if (index <= 2) { select_section(index); return APP_REDRAW_FULL; }
    if (index == 3) { ctx->request_app = &app_font_pick; return APP_REDRAW_NONE; }
    if (index == 4) return change_size(-4);
    if (index == 5) return change_size(4);
    if (index == 6) {
        remember_position();
        s_weight = s_weight == 400 ? 700 : 400;
        ttf_set_weight(s_weight);
        if (s_section == PREVIEW_TEXT) begin_layout();
    }
    if (index == 8) { ctx->request_app = app_home_page(); return APP_REDRAW_NONE; }
    s_tools = false;
    return APP_REDRAW_FULL;
}

static app_redraw_t turn_page(int delta) {
    if (s_section == PREVIEW_COMIC) {
        int next = s_comic_page + delta;
        if (next < 0 || next >= PREVIEW_COMIC_PAGES) return APP_REDRAW_NONE;
        s_comic_page = next;
        return APP_REDRAW_FULL;
    }
    if (s_section == PREVIEW_FONT) {
        s_specimen = (s_specimen + delta + 3) % 3;
        return APP_REDRAW_FULL;
    }
    if (!s_layout || s_seek_pending || (delta < 0 && !s_page)) return APP_REDRAW_NONE;
    size_t target = delta < 0 ? s_page - 1 : s_page + 1;
    if (target >= book_layout_page_count() && !book_layout_complete()) {
        if (!book_layout_extend(2)) { s_layout = false; s_error = "分页失败，请打开工具或返回。"; return APP_REDRAW_FULL; }
    }
    if (target >= book_layout_page_count()) return APP_REDRAW_NONE;
    s_page = target;
    s_offset = book_layout_page_start_offset(s_page);
    return APP_REDRAW_PAGE;
}

static app_redraw_t on_key(app_ctx_t* ctx, int key) {
    if (s_tools) {
        if (key == UI_KEY_2) return execute_tool(ctx, s_tool);
        s_tool = (s_tool + (key == UI_KEY_1 ? -1 : 1) + PREVIEW_TOOLS) % PREVIEW_TOOLS;
        return APP_REDRAW_PAGE;
    }
    if (key == UI_KEY_2) { s_tools = true; return APP_REDRAW_FULL; }
    return turn_page(key == UI_KEY_1 ? -1 : 1);
}

static app_redraw_t on_key_long(app_ctx_t* ctx, int key) {
    if (key == UI_KEY_2) { s_tools = false; ctx->request_menu = true; }
    return APP_REDRAW_NONE;
}

static app_redraw_t on_gesture(app_ctx_t* ctx, const ui_gesture_event_t* event) {
    if (s_tools) {
        if (event->type != UI_GESTURE_TAP) return APP_REDRAW_NONE;
        for (int i = 0; i < PREVIEW_TOOLS; ++i)
            if (ui_rect_hit(tool_rect(i), event->x0, event->y0)) { s_tool = i; return execute_tool(ctx, i); }
        return APP_REDRAW_NONE;
    }
    if (event->type == UI_GESTURE_SWIPE_L) return turn_page(1);
    if (event->type == UI_GESTURE_SWIPE_R) return turn_page(-1);
    if (event->type == UI_GESTURE_LONG_PRESS) { s_tools = true; return APP_REDRAW_FULL; }
    if (event->type != UI_GESTURE_TAP) return APP_REDRAW_NONE;
    if (event->y0 < UI_CONTENT_TOP || event->y0 > UI_CONTENT_BOTTOM ||
        (event->x0 >= UI_LOCK_WIDTH / 3 && event->x0 <= UI_LOCK_WIDTH * 2 / 3)) {
        s_tools = true;
        return APP_REDRAW_FULL;
    }
    return turn_page(event->x0 < UI_LOCK_WIDTH / 3 ? -1 : 1);
}

static app_redraw_t on_tick(app_ctx_t* ctx) {
    if (s_section != PREVIEW_TEXT || s_tools || !s_layout) return APP_REDRAW_NONE;
    if (strcmp(s_font_path, ttf_font_path())) { remember_position(); begin_layout(); return APP_REDRAW_FULL; }
    if (book_layout_complete() || ctx->now_ms < s_extend_ms) return APP_REDRAW_NONE;
    s_extend_ms = ctx->now_ms + 100;
    if (!book_layout_extend(2)) {
        s_layout = false; s_error = "分页失败，请打开工具或返回。";
        return APP_REDRAW_FULL;
    }
    if (finish_seek() || book_layout_complete()) return APP_REDRAW_PAGE;
    return APP_REDRAW_NONE;
}

const app_desc_t app_preview_reading = {
    .title = "阅读与漫画演示", .detail = "内置长文、八页漫画与字体样张",
    .enter_full = true, .owns_keys = true,
    .blocks_idle_sleep = true,
    .on_enter = on_enter, .on_exit = preview_exit,
    .render = render, .on_key = on_key, .on_key_long = on_key_long,
    .on_gesture = on_gesture, .on_tick = on_tick,
};

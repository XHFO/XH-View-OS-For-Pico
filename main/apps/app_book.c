/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 *
 * 图书书架、阅读、目录与页内进度；文件解析和排版由book模块负责。
 * Book shelf, reader, TOC and progress; book modules own parsing and pagination.
 * 用户授权双机实验分支：配对正文使用竖屏及共享字号、字体，主从显示相邻页，翻页请求交给主机仲裁；普通模式不变。
 * User-authorized dual experiment: paired reading uses portrait geometry and shared typography, consecutive pages and primary-arbitrated turns; normal mode is unchanged.
 *
 * 冻结：Phase4b统一手势入口并接管三键为上页/工具条/下页；工具条保留强刷，长按中键或把手打开演示菜单。屏幕翻页在抬起提交，不画按下态。
 * 晃动实验默认关，只翻下一页；离页关闭AOI2并休眠。render只绘图。
 * 预渲染回调返回前收齐，避免菜单/锁屏绕过页内TTF锁。
 * 普通翻页正文与页脚分区刷新；页脚只驱动变化像素，整屏强刷仍清全屏。
 * 中键按下切工具条，持续按住500ms返回演示菜单（用户新增快捷出口）。
 * 用户授权基础管理：长按书架先看完整详情，清进度与删文件分别确认；失败保留待重试记录，不自动回收其他书进度。
 * 用户修订：单本管理为书架弹窗；管理页用于批量操作。分页和排序保留勾选，筛选/应用搜索及重扫清除勾选。
 * 失败进度仅按变更路径失效；删除后的清理重试保留到本次开机结束，不随切页释放。
 * 卡失效时先保存进度并关闭阅读资源，再由主循环回退字体；禁止自动续读失效挂载。
 * 用户修订：进入书架只询问续读，确认前不打开上次图书，避免大书阻塞书架。
 * 用户修订：JPEG/PNG插图按稳定比例自动排版和载入，像素缓存至多1.3MiB/64幅；render不做IO。独立封面首次无进度自动展示、不计正文页，书首上一页可重看，下一页返回正文，旧锚点不变。
 * 用户修订：轻点正文插图先确认，全屏预览只在新的确认接触抬起后加载；取消保持原页，图上左右滑仍翻页，确认期间实体键和BLE不得翻页。
 * 产品书架可排队真实路径直达阅读；产品会话中键/书架按钮/角落把手返回产品，字体子页返回恢复当前图书。工程入口保留原书架行为。
 * 用户修订：产品开书和字体页返回先推送静态载入页，后续tick才加载首选字体、读取与排版；不显示虚假进度或循环刷新动画。
 * 用户修订：真实书长按命中字起选并保持拖动，松手显示紧凑菜单；圆形首尾手柄直接调整，锚点仍为章节UTF-8字节。全局角落/返回优先，取消不提交。
 * 用户新增：阅读进度独立于底部安全区；拖动只预览章号/百分比，松手一次定位并可回原处，未完整分页也先补排目标。产品阅读保留屏幕方向，页内手势按实际方向转换原始触摸坐标。
 * 用户修订：进度条轻点输入全书准确页码，超过24像素才拖动；全书计数逐章分批使用独立排版任务，取消/失败不改变原阅读页。缓存只适用于当前书、字体、字重、字号与正文几何；目录和返回先处理最内层弹窗。
 * 用户修订：产品阅读将目录、章节与页码进度合并为84像素紧凑顶栏（网页44px等比），栏内章节、完整页码/百分比分两行；长章节名省略，数值不截断。正文延伸到底，只保留两角悬浮触区。轻点进度开数字跳页，长按开阅读工具；真实水平MOVE超过24像素才认领拖动，返回原处、选区和工具优先。
 * 用户真机修订：轻点只开数字页码，真实按住MOVE横向超过24像素且大于纵向位移才拖动；抬手不重新推断目标。数字页码确认后才懒定位，输入和统计采用局部DU反馈，避免后台逐章整屏刷新阻塞触摸。
 * 用户新增：BLE外部翻页器只在无遮盖的已载入正文翻页；弹窗、选文、手指交互或载入期间丢弃输入，不操作工具和设置。
 * 用户修订：轻点弹窗外空白只关闭最内层页码、插图确认、选文或工具浮层，不翻页、不保存批注或改变正文进度；首尾手柄仍可直接拖动。
 * 用户修订：双机阅读保留各自的竖屏正反方向，分页几何不变；页码输入先验证后同步跳至包含该页的双页组合。
 * User revision: Paired reading retains each local portrait inversion without repagination; validate page input before syncing its containing spread.
 * 用户修订：双机字号按主机原文锚点统一重排；目录、进度和批注定位先验证再提交共享页，续读不重置书首。
 * User revision: paired size changes reflow from the primary text anchor; validate contents, progress and annotation seeks before shared navigation; resume retains position.
 * 用户修订：数字跳页只保留取消与跳转，单层按键边框，主操作和按下态用黑白反差强调；定位中禁止重复提交。
 * User revision: numeric entry keeps cancel/jump only, single key outlines and black/white action emphasis; disable repeat submission while locating.
 * Frozen: Phase4b uses the shared gesture entry and owns previous/tools/next keys; the toolbar keeps full refresh and the middle-key hold or handle opens the demo menu. Screen turns commit on release without pressed decoration.
 * Shake is experimental, off by default, forward only; exit disables AOI2 and sleeps it. Render only paints.
 * Join preparation before returning callbacks so menus/lock cannot race the page-local TTF lock.
 * Ordinary turns refresh body and footer separately; the footer drives changed pixels only, while full refresh still clears the whole screen.
 * Middle-key press toggles tools; holding for 500ms opens the demo menu, as requested.
 * User-authorized management shows full details before separate clear/delete confirmations; retain failed saves for retry without pruning other books.
 * User revision: single-book actions use a shelf dialog; full management is for batches. Paging/sorting preserve selection; filtering/applied search and rescanning clear it.
 * Invalidate failed progress only for changed paths; retain deletion cleanup retries across page exits for this boot.
 * Lost media saves progress and closes reader resources before global font fallback; never auto-resume an invalid mount.
 * User revision: offer resume on shelf entry; open the previous book only after confirmation to keep the shelf available.
 * User revision: automatically lay out and load JPEG/PNG illustrations at stable proportions; pixel cache is bounded to 1.3MiB/64 items and render performs no IO. A standalone cover opens for a new book, excluded from text pages; previous at the book start reopens it, next returns to text, preserving old anchors.
 * User revision: tapping an inline image asks before loading a full-screen preview; confirmation requires a fresh released contact. Cancel preserves the page, horizontal image swipes still turn pages, and physical/BLE keys cannot turn behind the dialog.
 * Product shelves queue real paths directly into reading; product sessions return through the middle key, shelf action or corner handle and restore the book after font selection. Engineering entry keeps its original shelf behavior.
 * User revision: product opening and font-page return present a static loading page before a later tick loads the preferred font, reads and paginates; no fictitious progress or repeating refresh animation.
 * User revision: long-press a hit character and drag; release reveals a compact menu and round endpoint handles adjust directly. Anchors remain chapter UTF-8 bytes. Global corners/back take priority and cancellation never commits.
 * User addition: progress stays above the bottom safe area; dragging previews chapter/percentage, release seeks once with a return point and extends incomplete pagination. Product reading retains orientation and maps raw touches into the actual display rotation.
 * User revision: tap progress for an exact whole-book page; movement beyond 24 pixels begins scrubbing. Independent chapter jobs count incrementally; cancellation/failure preserves reading. Counts are scoped to this book/font/weight/size/body geometry; TOC/back respect the innermost dialog.
 * User revision: product reading merges contents, chapter and page progress into an 84-pixel compact bar (44 web pixels scaled to the device); chapter and complete page/percentage values use two rows. Ellipsize long chapters, never numeric values. Text extends to the bottom with only floating corner targets. Tap progress for numeric entry, hold for tools and capture only actual horizontal MOVE beyond 24 pixels; return, selection and tool controls take priority.
 * Hardware-feedback revision: taps only open numeric entry; a held MOVE must travel horizontally beyond 24 pixels and dominate vertical travel before scrubbing. Release never infers a target. Count lazily only after confirmation and use local DU feedback for entry/counting to avoid chapter-triggered full-screen refreshes blocking touch.
 * User revision: tapping outside closes only the innermost numeric/image/selection/tool overlay without turning pages or moving the reading position; endpoint handles remain directly draggable.
 */
#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <time.h>

#include "app.h"
#include "app_book.h"
#include "../system/pico_dual.h"
#include "../system/pico_system.h"
#include "../system/pico_annotations.h"
#include "app_registry.h"
#include "book_layout.h"
#include "book_policy.h"
#include "book_progress.h"
#include "book_source.h"
#include "book_epub.h"
#include "book_store.h"
#include "display.h"
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

#define BOOK_ROWS 7
#define BOOK_TOC_ROWS 10
#define BOOK_PX_MIN 36
#define BOOK_PX_MAX 72
#define BOOK_PX_STEP 4
#define BOOK_SHAKE_THS_MG 192
#define BOOK_SHAKE_DUR 2
#define BOOK_SIZE_SETTLE_MS 400
#define BOOK_TOOL_COUNT 9
#define BOOK_TOOL_ROWS 3
#define BOOK_PAGE_CONTROL 960
#define BOOK_IMAGE_CONFIRM_CONTROL 990
#define BOOK_PAGE_DIGITS 10

typedef enum { SHELF, READING, TOC, MANAGE, BULK, SEARCH } book_view_t;
typedef struct {
    char name[256];
    char path[BOOK_STORE_PATH_MAX];
    uint32_t size;
    bool is_flash;
    bool has_progress;
    uint8_t pct;
    uint32_t recent;
    bool selected, removed, search_match;
} shelf_entry_t;

static const char* TAG = "book";
static book_view_t s_view;
static uint32_t s_dual_present_pending;
static enum EpdRotation s_book_rotation;
static uint32_t s_dual_target,s_dual_displayed;
static bool s_dual_blank,s_dual_applying;
static char s_dual_font[BOOK_STORE_PATH_MAX];
static uint8_t s_dual_px = 48, s_dual_old_px = 48;
static int s_layout_px;
static uint32_t s_dual_reflow;
static bool s_dual_annotation, s_dual_wait;
static app_redraw_t dual_read_failed(void);
static void dual_read_cancel(void);
static struct { bool pending, source; size_t chapter, offset; uint32_t revision; } s_dual_anchor;
static void dual_anchor_start(size_t chapter, size_t offset, bool source, uint32_t revision);
static shelf_entry_t* s_shelf;
static size_t s_shelf_capacity;
static int s_count, s_visible_count, s_filter;
static bool s_recent_sort;
static shelf_entry_t s_managed;
static bool s_delete_confirm, s_file_removed;
static char s_shelf_warning[128], s_manage_message[128];
static char s_query[65], s_search_draft[65], s_batch_message[128];
static book_view_t s_search_parent;
static bool s_batch_confirm, s_batch_delete;
typedef struct pending_progress {
    char path[BOOK_STORE_PATH_MAX];
    book_progress_t value;
    bool dirty, progress_saved;
    book_progress_watch_t* watch;
    struct pending_progress* next;
} pending_progress_t;
static pending_progress_t* s_pending;
typedef struct delete_retry {
    shelf_entry_t entry;
    struct delete_retry* next;
} delete_retry_t;
static delete_retry_t* s_delete_retries;
static char s_latest_path[BOOK_STORE_PATH_MAX];
static bool s_save_failed;
static bool s_pending_invalidated;
static int64_t s_save_retry_ms;
static unsigned s_store_revision;
static bool s_scan_pending, s_resume_pending, s_toolbar, s_clear_confirm;
static bool s_product_session;
static char s_last_product_path[BOOK_STORE_PATH_MAX];
static char s_requested_path[BOOK_STORE_PATH_MAX];
static char s_open_path[BOOK_STORE_PATH_MAX];
static bool s_open_presented;
static bool s_annotation_pending, s_selection;
static uint8_t s_selection_edge = 1;
static size_t s_selection_start, s_selection_end;
static bool s_selection_drag, s_selection_menu, s_drag_previous;
static size_t s_drag_fixed, s_drag_before_start, s_drag_before_end;
static EpdRect s_handle_start, s_handle_end;
static bool s_handle_start_visible, s_handle_end_visible;
static int s_selection_menu_x, s_selection_menu_y;
static bool s_scrubbing, s_jump_back_valid;
static int s_scrub_x = -1;
static uint32_t s_scrub_target;
static size_t s_scrub_chapter, s_jump_chapter, s_jump_offset;
/* ---- 全书页码状态 / Whole-book page state ---- */
typedef struct {
    uint32_t* counts;
    size_t chapters, done, job_chapter;
    uint64_t total;
    uint32_t file_size;
    int px, weight;
    EpdRect body;
    char path[BOOK_STORE_PATH_MAX], font[192];
    book_layout_job_t* job;
    html_text_t loaded;
    bool failed;
} page_index_t;
static page_index_t s_page_index;
static bool s_page_jump_open, s_page_jump_waiting, s_modal_key_cancelled;
static bool s_progress_pending, s_progress_dragged;
static char s_page_jump_input[BOOK_PAGE_DIGITS + 1], s_page_jump_message[128];
static uint32_t s_page_jump_target;
static int64_t s_page_jump_status_ms;
static ps_book_anchor_t s_annotation_target;
static char s_resume_path[BOOK_STORE_PATH_MAX], s_resume_name[256];
static char s_message[128], s_storage[128], s_path[BOOK_STORE_PATH_MAX], s_title[128];
static char s_font_path[192];
static char* s_text;
static blk_t* s_blocks;
static size_t s_block_count;
static size_t s_text_len, s_chapter, s_page;
static bool s_image_open, s_cover_open, s_cover_missing, s_image_confirm_open;
static size_t s_image_confirm_block = SIZE_MAX;
static uint8_t* s_image_pixels;
static uint16_t s_image_width, s_image_height;
static size_t s_image_block = SIZE_MAX;
static const char* s_image_error;
static char s_image_origin[224];
static uint32_t s_file_size;
static int s_px, s_turns, s_unsaved;
static int64_t s_poll_ms, s_last_turn_ms, s_size_settle_ms, s_sensor_ms;
static bool s_shake_enabled, s_sensor_on;
static bool s_sensor_saved;
static sc7a20h_sensor_config_t s_sensor_config;
static book_shake_gate_t s_shake;
static EpdRect s_area, s_layout_rect;
static enum EpdDrawMode s_mode = MODE_GL16;
static bool s_full;
static bool s_reader_split;
static int s_pressed_control = -1;
static int64_t s_du_ms;
static unsigned s_du_count;
static EpdRect s_du_area;
static SemaphoreHandle_t s_draw_lock, s_prep_done;
static TaskHandle_t s_prep_task;
static uint8_t* s_next_fb;
static uint8_t* s_turn_old;
static int s_next_page = -1, s_prep_page = -1;
static bool s_turn_anim_pending;
static int s_turn_anim_dir;

static void render(app_ctx_t* ctx, uint8_t* fb);
static void scan_shelf(app_ctx_t* ctx);
static void free_book(void);
static void save_progress(void);
static void invalidate_prep(void);
static bool select_character(size_t at);
static bool selection_anchor(ps_book_anchor_t* out);
static app_redraw_t annotation_action(app_ctx_t* ctx, int action);
static app_redraw_t paint_reading(app_ctx_t* ctx, enum EpdDrawMode mode);
static bool load_chapter(app_ctx_t* ctx, size_t chapter, size_t offset, bool last_page);
static bool load_chapter_position(app_ctx_t* ctx, size_t chapter, size_t offset, bool last_page, bool source_units);
static app_redraw_t progress_commit(app_ctx_t* ctx, bool back);
static void page_index_pause(void);
static void page_index_reset(void);
static bool page_index_matches(void);
static void page_jump_close(void);
static app_redraw_t page_jump_open(void);
static EpdRect page_jump_rect(void);
static void page_jump_draw(uint8_t* fb);
static app_redraw_t page_jump_feedback(app_ctx_t* ctx, bool status_only);
static int page_jump_control_at(uint16_t x, uint16_t y, EpdRect* rect);
static app_redraw_t page_jump_action(app_ctx_t* ctx, int control);
static app_redraw_t page_jump_tick(app_ctx_t* ctx);
static app_redraw_t book_open_toc(app_ctx_t* ctx);
static void sort_shelf(app_ctx_t* ctx);
static void image_confirm_close(void);
static void image_confirm_draw(uint8_t* fb);
static int image_confirm_control_at(uint16_t x, uint16_t y, EpdRect* rect);
static app_redraw_t image_confirm_request(size_t block_index);
static app_redraw_t image_confirm_action(app_ctx_t* ctx, int control);
static void draw_control(uint8_t* fb, EpdRect rect, const char* label, int id) {
    if (id == 112 && s_query[0]) {
        ui_fill_round_rect(fb, rect, UI_BTN_RADIUS, UI_GRAY_BLACK);
        ui_text_vc(fb, rect.x + rect.width / 2, rect.y + rect.height / 2,
                   UI_PX_BTN, label, EPD_DRAW_ALIGN_CENTER, true);
        return;
    }
    if (s_pressed_control == id) ui_draw_pressed_round_rect(fb, rect, UI_BTN_RADIUS);
    ui_draw_button(fb, rect, label, false);
}
static void lock_draw(void) { if (s_draw_lock) xSemaphoreTake(s_draw_lock, portMAX_DELAY); }
static void unlock_draw(void) { if (s_draw_lock) xSemaphoreGive(s_draw_lock); }
static size_t fb_bytes(void) { return (size_t)epd_width() * epd_height() / 2; }
// 旋转后差分列向32像素对齐，分界必须落在对齐边上才不会驱动进度条。
// Rotated diff columns expand to 32 pixels; align the boundary so body updates cannot drive the track.
static int reader_width(void) { return epd_rotated_display_width(); }
static int reader_height(void) { return epd_rotated_display_height(); }
static int reader_footer_top(void) { return s_product_session ? reader_height() - 16 : ((reader_height() - 224) / 32) * 32; }
static EpdRect reader_area(void) { return s_product_session ? (EpdRect){0, 96, reader_width(), reader_height() - 96} : (EpdRect){0, 0, reader_width(), reader_footer_top()}; }
static EpdRect body_rect(void) {
    int top = s_product_session ? (ttf_font_is_builtin() ? 212 : 96) : (ttf_font_is_builtin() ? 140 : 24);
    return (EpdRect){UI_MARGIN, top, reader_width() - 2 * UI_MARGIN, reader_footer_top() - 8 - top};
}
static EpdRect progress_rect(void) {
    if (s_product_session && s_view == READING && s_text)
        return (EpdRect){84, 0, reader_width() - (s_jump_back_valid ? 184 : 100), 84};
    return (EpdRect){UI_MARGIN, s_product_session ? reader_height() - 180 : reader_footer_top(), reader_width() - 2 * UI_MARGIN, 84};
}
static EpdRect progress_back_rect(void) {
    if (s_product_session) return (EpdRect){reader_width() - 100, 0, 84, 84};
    EpdRect p = progress_rect(); return (EpdRect){p.x, p.y + 42, 160, 40};
}
static int toc_rows(void) {
    int bottom = s_product_session ? reader_height() - 112 : reader_footer_top();
    int rows = (bottom - UI_CONTENT_TOP - UI_GAP) / 72;
    return rows < 1 ? 1 : rows > BOOK_TOC_ROWS ? BOOK_TOC_ROWS : rows;
}
static EpdRect book_nav_rect(int i, int count) {
    if (s_view != TOC) return ui_bar_rect(i, count);
    int width = (reader_width() - 2 * UI_MARGIN - (count - 1) * UI_GAP) / count;
    return (EpdRect){UI_MARGIN + i * (width + UI_GAP), s_product_session ? reader_height() - 112 : reader_footer_top(), width, 84};
}
static EpdRect row_rect(int row, bool toc) {
    int bottom = s_product_session ? reader_height() - 112 : reader_footer_top();
    int h = toc ? (bottom - UI_CONTENT_TOP - UI_GAP) / toc_rows() : UI_BTN_H + UI_GAP;
    return (EpdRect){UI_MARGIN, (toc ? UI_CONTENT_TOP : 308) + row * h,
                     toc ? reader_width() - 2 * UI_MARGIN : ui_content_width(), toc ? h - 6 : UI_BTN_H};
}
static EpdRect tool_rect(int i) {
    int width = (reader_width() - 2 * UI_MARGIN - 2 * UI_GAP) / 3;
    return (EpdRect){UI_MARGIN + (i % 3) * (width + UI_GAP), (s_product_session ? reader_height() - 112 : reader_footer_top()) - BOOK_TOOL_ROWS * (UI_BTN_H + UI_GAP) + (i / 3) * (UI_BTN_H + UI_GAP), width, UI_BTN_H};
}
static EpdRect toolbar_panel(void) {
    return (EpdRect){UI_MARGIN - 8, tool_rect(0).y - 110, reader_width() - 2 * UI_MARGIN + 16,
                     BOOK_TOOL_ROWS * (UI_BTN_H + UI_GAP) + 110};
}
static EpdRect selection_rect(int i) {
    int width = (reader_width() - 2 * UI_MARGIN - 16) / 3;
    if (width > 148) width = 148;
    return (EpdRect){s_selection_menu_x + i * (width + 8), s_selection_menu_y, width, 60};
}
static EpdRect selection_panel(void) {
    EpdRect first = selection_rect(0), last = selection_rect(2);
    return (EpdRect){first.x - 6, first.y - 6, last.x + last.width - first.x + 12, first.height + 12};
}
static int leaves(void) {
    int count = s_view == TOC ? (int)book_chapter_count() : s_visible_count;
    int rows = s_view == TOC ? toc_rows() : BOOK_ROWS;
    return count ? 1 + (count - 1) / rows : 1;
}

// 截断必须停在UTF8字符边界。/ Truncation must stop at a UTF8 character boundary.
static void copy_text(char* dst, size_t cap, const char* src) {
    if (!cap) return;
    size_t n = strlen(src);
    if (n >= cap) {
        n = cap - 1;
        while (n && ((unsigned char)src[n] & 0xc0) == 0x80) --n;
    }
    memcpy(dst, src, n);
    dst[n] = 0;
}
static void fit_text(char* text, int px, int width) {
    while (*text && ttf_text_width_px(px, text) > width) {
        size_t n = strlen(text) - 1;
        while (n && ((unsigned char)text[n] & 0xc0) == 0x80) --n;
        text[n] = 0;
    }
}
static uint32_t chapter_end(void) {
    return s_chapter + 1 < book_chapter_count()
        ? book_chapter_byte_offset(s_chapter + 1) : book_total_bytes();
}
static unsigned percent(size_t page) {
    uint32_t total = book_total_bytes();
    if (!total) return 0;
    if (book_layout_complete() && s_chapter + 1 == book_chapter_count() && page + 1 == book_layout_page_count()) return 100;
    uint32_t off = book_position_bytes(book_chapter_byte_offset(s_chapter), chapter_end(),
                                       book_layout_page_start_offset(page), s_text_len);
    return (unsigned)((uint64_t)off * 100 / total);
}

/* ---- 真实正文批注 / Real-text annotations ---- */
static size_t text_next(size_t at) {
    if (at >= s_text_len) return s_text_len;
    do { ++at; } while (at < s_text_len && ((unsigned char)s_text[at] & 0xc0) == 0x80);
    return at;
}
static size_t text_previous(size_t at) {
    if (!at) return 0;
    do { --at; } while (at && ((unsigned char)s_text[at] & 0xc0) == 0x80);
    return at;
}
static void selection_geometry(void) {
    s_handle_start_visible = s_handle_end_visible = false;
    if (!s_selection) return;
    lock_draw();
    s_handle_start_visible = book_layout_text_bounds(s_page, body_rect(), s_selection_start, &s_handle_start);
    s_handle_end_visible = book_layout_text_bounds(s_page, body_rect(), text_previous(s_selection_end), &s_handle_end);
    unlock_draw();
    EpdRect anchor = s_handle_start_visible ? s_handle_start : s_handle_end_visible ? s_handle_end : body_rect();
    int width = selection_rect(0).width * 3 + 16;
    int left = anchor.x - width / 3;
    if (left < UI_MARGIN) left = UI_MARGIN;
    if (left + width > reader_width() - UI_MARGIN) left = reader_width() - UI_MARGIN - width;
    s_selection_menu_x = left;
    int top = anchor.y - 80;
    if (top < 20) top = anchor.y + anchor.height + 42;
    if (top > reader_footer_top() - 76) top = reader_footer_top() - 76;
    s_selection_menu_y = top < 20 ? 20 : top;
}
static bool select_character(size_t at) {
    if (!s_text || at >= s_text_len || !s_path[0] || ((unsigned char)s_text[at] & 0xc0) == 0x80) return false;
    for (size_t i = 0; i < s_block_count; ++i) if (at >= s_blocks[i].offset && at < s_blocks[i].offset + s_blocks[i].len &&
        (s_blocks[i].image || s_blocks[i].image_src)) return false;
    s_selection_start = at; s_selection_end = text_next(at); s_selection = s_selection_menu = true;
    s_selection_drag = false; s_selection_edge = 1;
    s_toolbar = false; s_view = READING; invalidate_prep(); selection_geometry();
    return true;
}
static int s_drag_dx, s_drag_dy;
static void selection_drag_start(size_t fixed, bool previous) {
    s_drag_before_start = s_selection_start; s_drag_before_end = s_selection_end; s_drag_previous = previous;
    s_drag_fixed = fixed; s_selection_drag = true; s_selection_menu = false;
    s_drag_dx = s_drag_dy = 0;
    invalidate_prep();
}
static bool selection_drag_move(int x, int y) {
    lock_draw(); size_t at = book_layout_text_nearest(s_page, body_rect(), x - s_drag_dx, y - s_drag_dy); unlock_draw();
    if (at == SIZE_MAX) return false;
    size_t start = at < s_drag_fixed ? at : s_drag_fixed;
    size_t end = text_next(at > s_drag_fixed ? at : s_drag_fixed);
    if (start == s_selection_start && end == s_selection_end) return false;
    s_selection_start = start; s_selection_end = end; invalidate_prep(); selection_geometry();
    return true;
}
static void cancel_interaction(void) {
    s_scrubbing = s_progress_pending = s_progress_dragged = false;
    if (s_selection_drag) {
        s_selection = s_drag_previous;
        s_selection_start = s_drag_before_start; s_selection_end = s_drag_before_end;
        s_selection_menu = s_selection;
        s_selection_drag = false; selection_geometry();
    }
    invalidate_prep();
}
static bool handle_hit(EpdRect glyph, bool end, int x, int y) {
    int cx = glyph.x + (end ? glyph.width : 0), cy = glyph.y + glyph.height + 14;
    if (cy > reader_footer_top() - 16) cy = reader_footer_top() - 16;
    int dx = x - cx, dy = y - cy;
    return dx >= -28 && dx <= 28 && dy >= -28 && dy <= 28;
}
static bool selection_anchor(ps_book_anchor_t* out) {
    if (!out || !s_text || !s_path[0] || s_chapter > UINT32_MAX || s_text_len > UINT32_MAX) return false;
    memset(out, 0, sizeof(*out));
    copy_text(out->path, sizeof(out->path), s_path);
    const char* name = strrchr(s_path, '/');
    copy_text(out->title, sizeof(out->title), name ? name + 1 : s_path);
    out->file_bytes = s_file_size; out->chapter = (uint32_t)s_chapter;
    size_t start = s_selection ? s_selection_start : book_layout_page_start_offset(s_page);
    size_t end = s_selection ? s_selection_end : start;
    if (start > s_text_len || end > s_text_len || end < start) return false;
    out->start = (uint32_t)start; out->end = (uint32_t)end;
    size_t bytes = end - start;
    if (bytes >= sizeof(out->quote)) bytes = sizeof(out->quote) - 1;
    while (bytes && start + bytes < end && ((unsigned char)s_text[start + bytes] & 0xc0) == 0x80) --bytes;
    memcpy(out->quote, s_text + start, bytes); out->quote[bytes] = 0;
    return true;
}
static bool restore_annotation(void) {
    uint32_t start = s_annotation_target.start, end = s_annotation_target.end;
    if (!s_text || strcmp(s_path, s_annotation_target.path) || s_annotation_target.file_bytes != s_file_size || s_annotation_target.chapter != s_chapter ||
        start > s_text_len || end > s_text_len || end < start ||
        (start < s_text_len && ((unsigned char)s_text[start] & 0xc0) == 0x80) ||
        (end < s_text_len && ((unsigned char)s_text[end] & 0xc0) == 0x80)) {
        copy_text(s_message, sizeof(s_message), "批注原文已变动，无法安全定位"); return false;
    }
    s_selection_start = start; s_selection_end = end; s_selection = end > start;
    s_selection_edge = 1; s_selection_drag = false; s_selection_menu = s_selection; selection_geometry();
    return true;
}
static void selection_adjust(size_t at) {
    if (!s_selection || at >= s_text_len) return;
    if (s_selection_edge == 0) {
        s_selection_start = at;
        if (s_selection_end <= at) s_selection_end = text_next(at);
    } else {
        s_selection_end = text_next(at);
        if (s_selection_start >= s_selection_end) s_selection_start = at;
    }
    invalidate_prep(); selection_geometry();
}
static app_redraw_t annotation_action(app_ctx_t* ctx, int action) {
    if (action == 5) { s_selection = s_selection_drag = s_selection_menu = false; invalidate_prep(); return APP_REDRAW_PAGE; }
    if (action == 3 || action == 4) { s_selection_edge = (uint8_t)(action - 3); return APP_REDRAW_PAGE; }
    ps_book_anchor_t anchor;
    if (!selection_anchor(&anchor)) return APP_REDRAW_NONE;
    save_progress();
    if (action == 0) {
        if (ps_reader_real_highlight(&anchor)) { s_selection = s_selection_menu = s_selection_drag = false; invalidate_prep(); }
    } else if (action == 1) {
        ps_reader_real_note(&anchor);
        ctx->request_app = app_home_page();
    } else if (action == 2) {
        if (ps_reader_real_bookmark(&anchor)) { s_selection = s_selection_menu = s_selection_drag = false; invalidate_prep(); }
    }
    return APP_REDRAW_PAGE;
}
static int selection_control_at(uint16_t x, uint16_t y, EpdRect* rect) {
    if (!s_selection_menu || s_selection_drag) return -1;
    for (int i = 0; i < 3; ++i) {
        *rect = selection_rect(i);
        if (ui_rect_hit(*rect, x, y)) return 900 + i;
    }
    return -1;
}
static app_redraw_t selection_touch(app_ctx_t* ctx, uint16_t x, uint16_t y) {
    EpdRect rect; int control = selection_control_at(x, y, &rect);
    if (control >= 900) return annotation_action(ctx, control == 902 ? 5 : control - 900);
    return APP_REDRAW_NONE;
}
static pending_progress_t* pending_find(const char* path) {
    for (pending_progress_t* p = s_pending; p; p = p->next) if (!strcmp(p->path, path)) return p;
    return NULL;
}
static int layout_name(uint8_t* fb, const char* name, int y, bool draw);
static EpdRect manage_panel(void) {
    int height = layout_name(NULL, s_managed.name, 0, false) + 420;
    return (EpdRect){UI_MARGIN - 16, 190 + (876 - height) / 2, ui_content_width() + 32, height};
}
static EpdRect manage_rect(int index, int count) {
    EpdRect panel = manage_panel();
    return ui_row_rect(index, count, panel.y + panel.height - 94, 76);
}
static EpdRect batch_rect(int id) {
    if (id < 3) return ui_row_rect(id, 3, 978, 48);
    return ui_row_rect(id - 3, 2, 1038, 48);
}
static EpdRect search_rect(int id) {
    if (id < 40) {
        int width = (ui_content_width() - 54) / 10;
        return (EpdRect){UI_MARGIN + id % 10 * (width + 6), 388 + id / 10 * 100, width, 88};
    }
    if (id < 43) return ui_row_rect(id - 40, 3, 808, 80);
    return ui_bar_rect(id - 43, 2);
}
static size_t selected_count(void) {
    size_t selected = 0;
    for (int i = 0; i < s_count; ++i) if (s_shelf[i].selected) ++selected;
    return selected;
}
static void clear_selection(void) {
    for (int i = 0; i < s_count; ++i) s_shelf[i].selected = false;
}
static void toggle_selection(int index) {
    if (index >= 0 && index < s_visible_count) s_shelf[index].selected = !s_shelf[index].selected;
}
static void select_page(int page) {
    for (int i = page * BOOK_ROWS; i < s_visible_count && i < (page + 1) * BOOK_ROWS; ++i) s_shelf[i].selected = true;
}
static const char* search_keys(void) {
    return "1234567890" "qwertyuiop" "asdfghjkl-" "zxcvbnm._'";
}
static void search_begin(void) {
    s_search_parent = s_view;
    memcpy(s_search_draft, s_query, sizeof(s_query));
    s_view = SEARCH;
}
static void refresh_search_matches(void) {
    for (int i = 0; i < s_count; ++i)
        s_shelf[i].search_match = read_pico_search_match(s_shelf[i].name, s_query);
}
static void search_finish(app_ctx_t* ctx, bool apply) {
    s_view = s_search_parent;
    if (apply) {
        memcpy(s_query, s_search_draft, sizeof(s_query));
        refresh_search_matches();
        clear_selection();
        sort_shelf(ctx);
        s_batch_message[0] = 0;
    }
    memset(s_search_draft, 0, sizeof(s_search_draft));
}
static app_redraw_t search_action(app_ctx_t* ctx, int id) {
    size_t len = strlen(s_search_draft);
    if ((id >= 0 && id < 40) || id == 40) {
        if (len < sizeof(s_search_draft) - 1) {
            s_search_draft[len] = id == 40 ? ' ' : search_keys()[id];
            s_search_draft[len + 1] = 0;
        }
    } else if (id == 41 && len) s_search_draft[len - 1] = 0;
    else if (id == 42) s_search_draft[0] = 0;
    else if (id == 43 || id == 44) { search_finish(ctx, id == 44); return APP_REDRAW_PAGE; }
    return APP_REDRAW_AREA;
}
static bool pending_reserve(const char* path) {
    if (pending_find(path)) return true;
    pending_progress_t* p = heap_caps_malloc(sizeof(*p), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!p) p = malloc(sizeof(*p));
    if (!p) return false;
    memset(p, 0, sizeof(*p));
    copy_text(p->path, sizeof(p->path), path);
    // 阅读页内注册；传书 HTTP 尚未启动，退出后也不注销失败项。
    // Register in reading before transfer HTTP starts; failed records survive page exit.
    p->watch = book_progress_watch_create(path);
    if (!p->watch) { free(p); return false; }
    pending_progress_t** tail = &s_pending;
    while (*tail) tail = &(*tail)->next;
    *tail = p;
    return true;
}
static bool pending_restore(const char* path, uint32_t size, book_progress_t* out) {
    pending_progress_t* pending = pending_find(path);
    if (!pending || !pending->dirty || pending->value.file_size != size) return false;
    *out = pending->value;
    return true;
}
static void pending_discard(const char* path) {
    pending_progress_t** p = &s_pending;
    while (*p) {
        if (!strcmp((*p)->path, path)) {
            pending_progress_t* old = *p;
            *p = old->next;
            book_progress_watch_destroy(old->watch);
            free(old);
            break;
        }
        p = &(*p)->next;
    }
    s_save_failed = false;
    for (pending_progress_t* item = s_pending; item; item = item->next) if (item->dirty) s_save_failed = true;
}
static void pending_mark_latest(const char* path) {
    pending_progress_t** item = &s_pending;
    while (*item && strcmp((*item)->path, path)) item = &(*item)->next;
    if (*item) {
        pending_progress_t* current = *item;
        *item = current->next;
        current->next = NULL;
        pending_progress_t** tail = &s_pending;
        while (*tail) tail = &(*tail)->next;
        *tail = current;
    }
    copy_text(s_latest_path, sizeof(s_latest_path), path);
}
static void pending_drop_invalidated(void) {
    pending_progress_t* p = s_pending;
    while (p) {
        pending_progress_t* next = p->next;
        if (book_progress_watch_invalidated(p->watch)) {
            if (p->dirty) s_pending_invalidated = true;
            pending_discard(p->path);
        }
        p = next;
    }
}
static delete_retry_t* delete_retry_find(const char* path) {
    for (delete_retry_t* p = s_delete_retries; p; p = p->next)
        if (!strcmp(p->entry.path, path)) return p;
    return NULL;
}
static bool pending_flush(pending_progress_t* p) {
    if (!p->dirty) return true;
    if (!p->progress_saved) {
        if (book_progress_save(p->path, &p->value) != ESP_OK) return false;
        p->progress_saved = true;
    }
    if (!strcmp(p->path, s_latest_path) && book_progress_set_last_path(p->path) != ESP_OK) return false;
    p->dirty = false;
    return true;
}
static void retry_progress(void) {
    s_save_failed = false;
    bool had_dirty = false;
    pending_progress_t* p = s_pending;
    while (p) {
        pending_progress_t* next = p->next;
        bool dirty = p->dirty;
        had_dirty |= dirty;
        if (s_text && !strcmp(p->path, s_path)) { p = next; continue; }
        if (!pending_flush(p)) s_save_failed = true;
        else if (strcmp(p->path, s_path)) pending_discard(p->path);
        else if (dirty) s_unsaved = 0;
        p = next;
    }
    if (s_text && (s_unsaved || had_dirty)) save_progress();
    s_save_failed = false;
    for (pending_progress_t* item = s_pending; item; item = item->next) if (item->dirty) s_save_failed = true;
}

/* ---- 管理详情 / Management details ---- */
static int layout_name(uint8_t* fb, const char* name, int y, bool draw) {
    const char* at = name;
    while (*at) {
        char line[256];
        size_t used = 0;
        while (at[used]) {
            unsigned char first = (unsigned char)at[used];
            size_t n = first < 0x80 ? 1 : first < 0xe0 ? 2 : first < 0xf0 ? 3 : 4;
            size_t remain = strlen(at + used);
            if (n > remain) n = 1;
            if (used + n >= sizeof(line)) break;
            memcpy(line + used, at + used, n);
            line[used + n] = 0;
            if (ttf_text_width_px(UI_PX_CAPTION, line) > ui_content_width() && used) break;
            used += n;
        }
        if (!used) break;
        line[used] = 0;
        if (draw) ui_text(fb, UI_MARGIN, y, UI_PX_CAPTION, line, EPD_DRAW_ALIGN_LEFT, false);
        y += 36;
        at += used;
    }
    return y;
}
static int draw_wrapped_name(uint8_t* fb, const char* name, int y) {
    return layout_name(fb, name, y, true);
}
static void draw_manage(uint8_t* fb) {
    EpdRect panel = manage_panel();
    ui_fill_round_rect(fb, panel, UI_BTN_RADIUS, UI_GRAY_WHITE);
    ui_draw_round_rect(fb, panel, UI_BTN_RADIUS, UI_GRAY_BLACK);
    ui_text(fb, UI_MARGIN, panel.y + 18, UI_PX_BODY,
            s_clear_confirm ? (s_delete_confirm ? "确认删除文件？" : "确认清除进度？") : "图书详情", EPD_DRAW_ALIGN_LEFT, false);
    int y = draw_wrapped_name(fb, s_managed.name, panel.y + 74) + 12;
    char info[96];
    snprintf(info, sizeof(info), "%s · %s · %.2f MB", s_managed.is_flash ? "内置存储" : "TF 卡",
             strrchr(s_managed.name, '.') ? strrchr(s_managed.name, '.') + 1 : "", s_managed.size / 1048576.0);
    ui_text(fb, UI_MARGIN, y, UI_PX_CAPTION, info, EPD_DRAW_ALIGN_LEFT, false);
    snprintf(info, sizeof(info), s_managed.has_progress ? "阅读进度 %u%%" : "尚无阅读进度", s_managed.pct);
    ui_text(fb, UI_MARGIN, y + 44, UI_PX_CAPTION, info, EPD_DRAW_ALIGN_LEFT, false);
    ui_text(fb, UI_MARGIN, y + 88, UI_PX_CAPTION, s_managed.is_flash ? "/flash/books" : "/sdcard/books", EPD_DRAW_ALIGN_LEFT, false);
    if (s_manage_message[0]) ui_text(fb, UI_MARGIN, panel.y + panel.height - 198, UI_PX_CAPTION, s_manage_message, EPD_DRAW_ALIGN_LEFT, false);
    if (s_clear_confirm) {
        ui_text(fb, UI_MARGIN, panel.y + panel.height - 142, UI_PX_CAPTION, s_delete_confirm ? "删除后文件无法恢复" : "仅清阅读进度，保留图书文件", EPD_DRAW_ALIGN_LEFT, false);
        draw_control(fb, manage_rect(0, 2), "取消", 300);
        draw_control(fb, manage_rect(1, 2), s_delete_confirm ? "确认删除" : "确认清除", 301);
    } else if (s_file_removed) {
        draw_control(fb, manage_rect(0, 2), "关闭", 400);
        draw_control(fb, manage_rect(1, 2), "重试清理", 403);
    } else {
        draw_control(fb, manage_rect(0, 3), "关闭", 400);
        draw_control(fb, manage_rect(1, 3), "清进度", 401);
        draw_control(fb, manage_rect(2, 3), "删除文件", 402);
    }
}
static void draw_search(uint8_t* fb) {
    ui_clear_page(fb);
    ui_draw_header(fb, "搜索图书", "输入拼音首字母、完整拼音或英文");
    EpdRect field = {UI_MARGIN, 200, ui_content_width(), 88};
    ui_draw_round_rect(fb, field, UI_BTN_RADIUS, UI_GRAY_BLACK);
    const char* tail = s_search_draft;
    while (*tail && ttf_text_width_px(UI_PX_BODY, tail) > field.width - 2 * UI_PAD) ++tail;
    ui_text_vc(fb, field.x + UI_PAD, field.y + field.height / 2, UI_PX_BODY, tail, EPD_DRAW_ALIGN_LEFT, false);
    char count[48];
    snprintf(count, sizeof(count), "%u/64 · 清空后应用可显示全部", (unsigned)strlen(s_search_draft));
    ui_text(fb, UI_MARGIN, 310, UI_PX_CAPTION, count, EPD_DRAW_ALIGN_LEFT, false);
    const char* keys = search_keys();
    for (int i = 0; i < 40; ++i) {
        char label[2] = {keys[i], 0};
        draw_control(fb, search_rect(i), label, 500 + i);
    }
    draw_control(fb, search_rect(40), "空格", 540);
    draw_control(fb, search_rect(41), "退格", 541);
    draw_control(fb, search_rect(42), "清空", 542);
    ui_text(fb, UI_MARGIN, 938, UI_PX_CAPTION, "应用清除勾选 · KEY1 取消", EPD_DRAW_ALIGN_LEFT, false);
    draw_control(fb, search_rect(43), "取消", 543);
    draw_control(fb, search_rect(44), "应用", 544);
    if (!s_product_session) ui_draw_menu_handle(fb, false);
}
static void draw_batch_confirmation(uint8_t* fb) {
    if (!s_batch_confirm) return;
    EpdRect panel = {UI_MARGIN - 12, 410, ui_content_width() + 24, 330};
    ui_fill_round_rect(fb, panel, UI_BTN_RADIUS, UI_GRAY_WHITE);
    ui_draw_round_rect(fb, panel, UI_BTN_RADIUS, UI_GRAY_BLACK);
    char title[96];
    snprintf(title, sizeof(title), "%s %u 本图书？", s_batch_delete ? "删除" : "清除进度：", (unsigned)selected_count());
    ui_text(fb, UI_MARGIN, 438, UI_PX_BODY, title, EPD_DRAW_ALIGN_LEFT, false);
    ui_text(fb, UI_MARGIN, 508, UI_PX_CAPTION, s_batch_delete ? "删除文件不可撤销，失败项可重试" : "仅清阅读进度，所有文件保留", EPD_DRAW_ALIGN_LEFT, false);
    draw_control(fb, ui_row_rect(0, 2, 620, UI_BTN_H), "取消", 600);
    draw_control(fb, ui_row_rect(1, 2, 620, UI_BTN_H), "确认", 601);
}
static void save_progress(void) {
    if (!s_text || !s_path[0] || !book_layout_page_count()) return;
    bool was_failed = s_save_failed;
    pending_progress_t* pending = pending_find(s_path);
    if (!pending) { s_save_failed = true; return; }
    book_progress_t p = {
        .file_size = s_file_size, .chapter = (uint16_t)s_chapter,
        .byte_off = (uint32_t)book_layout_page_start_offset(s_page),
        .px = (uint8_t)s_px, .pct = (uint8_t)percent(s_page),
        .last_open_s = 0,
    };
    if (!pending->dirty || pending->value.file_size != p.file_size || pending->value.chapter != p.chapter ||
        pending->value.byte_off != p.byte_off || pending->value.px != p.px || pending->value.pct != p.pct) {
        pending->value = p;
        pending->progress_saved = false;
    }
    pending->dirty = true;
    if (pending_flush(pending)) {
        s_unsaved = 0;
        s_save_failed = false;
        for (pending_progress_t* item = s_pending; item; item = item->next) if (item->dirty) s_save_failed = true;
    }
    else { if (!s_unsaved) s_unsaved = 1; s_save_failed = true; }
    if (was_failed != s_save_failed) invalidate_prep();
}
static void invalidate_prep(void) { s_next_page = s_prep_page = -1; }
static void image_confirm_close(void) {
    s_image_confirm_open = false; s_image_confirm_block = SIZE_MAX;
    s_pressed_control = -1; invalidate_prep();
}
static void free_image(void) {
    image_confirm_close();
    free(s_image_pixels); s_image_pixels = NULL;
    s_image_width = s_image_height = 0;
    s_image_block = SIZE_MAX; s_image_open = s_cover_open = false;
    s_image_error = NULL; s_image_origin[0] = 0;
}
static app_redraw_t close_image(void) {
    if (s_cover_open) free_image();
    s_image_open = false;
    invalidate_prep();
    return APP_REDRAW_PAGE;
}

// 自动载入仅在事件/章节回调中发生；锁保证预绘只读像素时不会遭遇淘汰。
// Load only from event/chapter callbacks; the drawing lock prevents eviction while preparation reads pixels.
static void prepare_page_images(void) {
    if (!s_text || !s_blocks || s_page >= book_layout_page_count()) return;
    lock_draw();
    if (book_chapter_prepare_images(s_chapter, s_blocks, s_block_count,
                                    book_layout_page_start_offset(s_page), book_layout_page_start_offset(s_page + 1)))
        invalidate_prep();
    unlock_draw();
}

/* ---- 绘制与预渲染 / Drawing and preparation ---- */
static size_t source_chapter(uint32_t target) {
    size_t lo = 0, hi = book_chapter_count();
    while (lo + 1 < hi) { size_t mid = lo + (hi - lo) / 2; if (book_chapter_byte_offset(mid) <= target) lo = mid; else hi = mid; }
    return lo;
}
static bool progress_preview(int x) {
    EpdRect p = progress_rect(); int right = p.x + p.width - 1;
    if (x < p.x) x = p.x;
    if (x > right) x = right;
    if (x == s_scrub_x) return false;
    s_scrub_x = x;
    s_scrub_target = (uint32_t)((uint64_t)(x - p.x) * book_total_bytes() / (p.width - 1));
    s_scrub_chapter = source_chapter(s_scrub_target);
    return true;
}
static void draw_compact_header(uint8_t* fb, size_t page) {
    EpdRect progress = progress_rect();
    ui_clear_rect_fast(fb, (EpdRect){0, 0, reader_width(), 84});
    ui_hairline(fb, 82, 0, reader_width(), UI_GRAY_LIGHT);
    for (int i = 0; i < 3; ++i) epd_fill_rect((EpdRect){27, 23 + i * 11, 30, 3}, UI_GRAY_BLACK, fb);
    char label[160], title[160], percent_label[16];
    unsigned pct = s_scrubbing ? (book_total_bytes() ? (unsigned)((uint64_t)s_scrub_target * 100 / book_total_bytes()) : 0) : percent(page);
    if (pct > 100) pct = 100;
    if (s_scrubbing) snprintf(label, sizeof(label), "松手跳转");
    else if (page_index_matches() && s_chapter <= s_page_index.done) {
        uint64_t current = (uint64_t)page + 1;
        for (size_t i = 0; i < s_chapter; ++i) current += s_page_index.counts[i];
        if (s_page_index.done == s_page_index.chapters) snprintf(label, sizeof(label), "全书 %llu/%llu 页", (unsigned long long)current, (unsigned long long)s_page_index.total);
        else snprintf(label, sizeof(label), "全书 %llu/… 页", (unsigned long long)current);
    } else if (book_layout_complete()) snprintf(label, sizeof(label), "本章 %llu/%llu 页", (unsigned long long)page + 1, (unsigned long long)book_layout_page_count());
    else snprintf(label, sizeof(label), "本章 %llu/… 页", (unsigned long long)page + 1);
    // 标题保留实际章节而非文件名；数值独占第二行，完整保留页码和百分比。
    // Keep the real chapter instead of the filename; reserve a separate row for complete numeric values.
    if (s_scrubbing) snprintf(title, sizeof(title), "第 %u/%u 节", (unsigned)s_scrub_chapter + 1, (unsigned)book_chapter_count());
    else if (s_title[0]) copy_text(title, sizeof(title), s_title);
    else snprintf(title, sizeof(title), "第 %u 节", (unsigned)s_chapter + 1);
    int available = progress.width - 10;
    if (ttf_text_width_px(28, title) > available) {
        fit_text(title, 28, available - ttf_text_width_px(28, "…"));
        strcat(title, "…");
    }
    if(ps_dual_enabled()&&s_dual_displayed)snprintf(label,sizeof(label),"%s · 第 %u 页",ps_dual_role()==1?"主机":"从机",(unsigned)s_dual_displayed);
    snprintf(percent_label, sizeof(percent_label), "%u%%", pct);
    int page_px = 26;
    int page_width = available - ttf_text_width_px(26, percent_label) - 18;
    while (page_px > 16 && ttf_text_width_px(page_px, label) > page_width) --page_px;
    ui_text_vc(fb, progress.x + 4, 20, 28, title, EPD_DRAW_ALIGN_LEFT, false);
    ui_text_vc(fb, progress.x + 4, 51, page_px, label, EPD_DRAW_ALIGN_LEFT, false);
    ui_text_vc(fb, progress.x + progress.width - 6, 51, 26, percent_label, EPD_DRAW_ALIGN_RIGHT, false);
    EpdRect track = {progress.x + 4, 74, progress.width - 10, 4};
    epd_fill_rect(track, UI_GRAY_LIGHT, fb);
    int thumb = track.x + (int)((uint64_t)(track.width - 1) * pct / 100);
    epd_fill_rect((EpdRect){track.x, track.y, thumb - track.x, track.height}, UI_GRAY_BLACK, fb);
    epd_fill_circle(thumb, track.y + 2, 5, UI_GRAY_BLACK, fb);
    if (s_jump_back_valid) {
        EpdRect back = progress_back_rect();
        epd_fill_rect((EpdRect){back.x + 25, 27, 33, 3}, UI_GRAY_BLACK, fb);
        epd_fill_rect((EpdRect){back.x + 23, 27, 3, 22}, UI_GRAY_BLACK, fb);
        for (int i = 0; i < 12; ++i) epd_fill_rect((EpdRect){back.x + 13 + i, 38 - i, 3, 3}, UI_GRAY_BLACK, fb);
        ui_text_vc(fb, back.x + back.width / 2, 67, 18, "原处", EPD_DRAW_ALIGN_CENTER, false);
    }
}
static void draw_progress(uint8_t* fb, size_t page) {
    if (s_product_session) { draw_compact_header(fb, page); return; }
    EpdRect track = progress_rect();
    ui_clear_rect_fast(fb, track);
    uint32_t total = book_total_bytes();
    unsigned pct = s_scrubbing ? (total ? (unsigned)((uint64_t)s_scrub_target * 100 / total) : 0) : percent(page);
    int thumb = s_scrubbing ? s_scrub_x : track.x + (int)((uint64_t)(track.width - 1) * pct / 100);
    epd_fill_rect((EpdRect){track.x, track.y + 16, track.width, 8}, UI_GRAY_LIGHT, fb);
    if (book_chapter_count() <= 40 && total) for (size_t i = 1; i < book_chapter_count(); ++i) {
        int x = track.x + (int)((uint64_t)book_chapter_byte_offset(i) * (track.width - 1) / total);
        epd_fill_rect((EpdRect){x, track.y + 12, 2, 16}, UI_GRAY_BLACK, fb);
    }
    epd_fill_rect((EpdRect){track.x, track.y + 16, thumb - track.x, 8}, UI_GRAY_BLACK, fb);
    epd_fill_circle(thumb, track.y + 20, 11, UI_GRAY_BLACK, fb);
    char line[192];
    if (s_scrubbing) snprintf(line, sizeof(line), "第 %u/%u 节 · %u%% · 松手跳转", (unsigned)s_scrub_chapter + 1, (unsigned)book_chapter_count(), pct);
    else if (page_index_matches() && s_chapter <= s_page_index.done) {
        uint64_t whole_page = page + 1;
        for (size_t i = 0; i < s_chapter; ++i) whole_page += s_page_index.counts[i];
        if (s_page_index.done == s_page_index.chapters)
            snprintf(line, sizeof(line), "全书 %llu/%llu 页 · 第 %u 节 · %u%%", (unsigned long long)whole_page,
                     (unsigned long long)s_page_index.total, (unsigned)s_chapter + 1, pct);
        else snprintf(line, sizeof(line), "全书第 %llu 页 · 总数待统计 · %u%%", (unsigned long long)whole_page, pct);
    } else if (book_layout_complete()) snprintf(line, sizeof(line), "第 %u 节 · 本章 %u/%u · %u%%", (unsigned)s_chapter + 1, (unsigned)page + 1, (unsigned)book_layout_page_count(), pct);
    else snprintf(line, sizeof(line), "第 %u 节 · 本章 %u/… · %u%%", (unsigned)s_chapter + 1, (unsigned)page + 1, pct);
    int left = track.x;
    if (s_jump_back_valid && !s_scrubbing) { draw_control(fb, progress_back_rect(), "返回原处", 950); left += 176; }
    fit_text(line, UI_PX_CAPTION, track.x + track.width - left);
    ui_text(fb, left, track.y + 44, UI_PX_CAPTION, line, EPD_DRAW_ALIGN_LEFT, false);
}
static app_redraw_t paint_progress(app_ctx_t* ctx, enum EpdDrawMode mode) {
    lock_draw(); draw_progress(ctx->fb, s_page); unlock_draw();
    s_area = s_product_session ? (EpdRect){0, 0, reader_width(), 84} : progress_rect(); s_mode = mode; s_reader_split = false;
    return APP_REDRAW_AREA;
}
static void draw_selection_handle(uint8_t* fb, EpdRect glyph, bool end) {
    int x = glyph.x + (end ? glyph.width : 0), y = glyph.y + glyph.height + 14;
    if (y > reader_footer_top() - 16) y = reader_footer_top() - 16;
    if (y >= glyph.y + glyph.height)
        epd_fill_rect((EpdRect){x - 1, glyph.y + glyph.height, 3, y - glyph.y - glyph.height + 1}, UI_GRAY_BLACK, fb);
    epd_fill_circle(x, y, 12, UI_GRAY_BLACK, fb);
    epd_fill_circle(x, y, 7, UI_GRAY_WHITE, fb);
}
static void draw_reader(uint8_t* fb, size_t page) {
    ui_clear_page(fb);
    if(s_dual_blank&&ps_dual_enabled()){
        ui_text(fb,UI_MARGIN,120,UI_PX_BODY,"本书已结束",EPD_DRAW_ALIGN_LEFT,false);
        ui_text(fb,UI_MARGIN,190,UI_PX_CAPTION,"最后一页在主机上",EPD_DRAW_ALIGN_LEFT,false);return;
    }
    book_layout_draw_page(fb, page, body_rect(), s_px);
    ps_book_range_t ranges[256];
    size_t count = ps_reader_real_ranges(s_path, s_file_size, (uint32_t)s_chapter, ranges, 256);
    size_t page_start = book_layout_page_start_offset(page), page_end = book_layout_page_start_offset(page + 1);
    if (count > 256) count = 256;
    for (size_t i = 0; i < count; ++i) if (ranges[i].start < page_end && ranges[i].end > page_start)
        book_layout_draw_range(fb, page, body_rect(), ranges[i].start, ranges[i].end, false);
    if (s_selection) book_layout_draw_range(fb, page, body_rect(), s_selection_start, s_selection_end, true);
    if (ttf_font_is_builtin()) {
        ui_text(fb, UI_MARGIN, s_product_session ? 98 : 16, UI_PX_CAPTION, "内建字体缺字，请选择 TF 卡字体", EPD_DRAW_ALIGN_LEFT, false);
        ui_draw_button(fb, (EpdRect){UI_MARGIN, s_product_session ? 134 : 52, reader_width() - 2 * UI_MARGIN, 72},
                       "打开字体页 / 无卡正文可能缺字", false);
    }
    draw_progress(fb, page);
    if (s_toolbar) {
        EpdRect panel = toolbar_panel();
        ui_clear_rect_fast(fb, (EpdRect){panel.x, panel.y - 24, panel.width, panel.height + 24});
        epd_fill_rect((EpdRect){panel.x, panel.y - 16, panel.width, 8}, UI_GRAY_LIGHT, fb);
        ui_hairline(fb, panel.y - 8, panel.x, panel.width, UI_GRAY_BLACK);
        ui_text(fb, UI_MARGIN, panel.y + 8, UI_PX_CAPTION,
                s_product_session ? "KEY1 上页 / KEY2 返回 / KEY3 下页" : "KEY1 上页 / KEY2 工具 / KEY3 下页", EPD_DRAW_ALIGN_LEFT, false);
        ui_text(fb, UI_MARGIN, panel.y + 40, UI_PX_CAPTION,
                "左右点按/滑动 · 长按文字并拖动", EPD_DRAW_ALIGN_LEFT, false);
        ui_text(fb, UI_MARGIN, panel.y + 72, UI_PX_CAPTION,
                s_product_session ? "两角快捷菜单 · 中键返回书架" : "中键长按：演示菜单", EPD_DRAW_ALIGN_LEFT, false);
        EpdRect size_box = {reader_width() - UI_MARGIN - 180, panel.y + 8, 180, 84};
        ui_draw_round_rect(fb, size_box, UI_CHIP_RADIUS, UI_GRAY_BLACK);
        ui_text(fb, size_box.x + size_box.width - UI_PAD, size_box.y + 8, UI_PX_CAPTION,
                "当前字号", EPD_DRAW_ALIGN_RIGHT, false);
        char size_hint[32];
        snprintf(size_hint, sizeof(size_hint), "%d px", s_px);
        ui_text(fb, size_box.x + size_box.width - UI_PAD, size_box.y + 44, UI_PX_CAPTION,
                size_hint, EPD_DRAW_ALIGN_RIGHT, false);
        const char* labels[] = {"目录", "字号 −", "字号 +", s_shake_enabled ? "晃动 开*" : "晃动 关*",
                                "强刷", "书架", "选择文字", "添加书签", "划线与笔记"};
        for (int i = 0; i < BOOK_TOOL_COUNT; ++i) draw_control(fb, tool_rect(i), labels[i], 200 + i);
    }
    if (s_selection) {
        if (s_handle_start_visible) draw_selection_handle(fb, s_handle_start, false);
        if (s_handle_end_visible) draw_selection_handle(fb, s_handle_end, true);
        if (s_selection_menu && !s_selection_drag) {
            EpdRect panel = selection_panel();
            ui_fill_round_rect(fb, panel, UI_BTN_RADIUS, UI_GRAY_WHITE);
            ui_draw_round_rect(fb, panel, UI_BTN_RADIUS, UI_GRAY_BLACK);
            const char* labels[] = {"划线", "批注", "取消"};
            for (int i = 0; i < 3; ++i) draw_control(fb, selection_rect(i), labels[i], 900 + i);
        }
    }
    if (!s_product_session) ui_draw_menu_handle(fb, false);
}
static void prep_task(void* arg) {
    (void)arg;
    for (;;) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        lock_draw();
        if (s_prep_page >= 0 && s_text && s_next_fb) {
            draw_reader(s_next_fb, (size_t)s_prep_page);
            s_next_page = s_prep_page;
        }
        unlock_draw();
        xSemaphoreGive(s_prep_done);
    }
}
static void ensure_prep(void) {
    if (!s_draw_lock) s_draw_lock = xSemaphoreCreateMutex();
    if (!s_prep_done) s_prep_done = xSemaphoreCreateBinary();
    if (!s_next_fb) s_next_fb = heap_caps_aligned_alloc(16, fb_bytes(), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!s_prep_task && s_draw_lock && s_prep_done && s_next_fb) {
        if (xTaskCreatePinnedToCore(prep_task, "book_prep", 12 * 1024, NULL, 3, &s_prep_task, 1) != pdPASS)
            s_prep_task = NULL;
    }
}
static bool kick_prep(void) {
    if (!s_prep_task || s_view != READING || s_image_open || s_image_confirm_open || s_toolbar || s_selection || s_scrubbing || s_progress_pending || s_page_jump_open || s_clear_confirm ||
        s_page + 1 >= book_layout_page_count() || s_next_page == (int)s_page + 1) return false;
    s_prep_page = (int)s_page + 1;
    xSemaphoreTake(s_prep_done, 0);
    xTaskNotifyGive(s_prep_task);
    return true;
}
static EpdRect image_back_rect(void) {
    return (EpdRect){UI_MARGIN, reader_height() - 124, reader_width() - 2 * UI_MARGIN, 72};
}
static void draw_image(uint8_t* fb) {
    ui_clear_page(fb);
    bool title_image = s_image_block < s_block_count && s_blocks[s_image_block].image_title;
    if (!s_cover_open) ui_draw_header(fb, s_image_error ? "图片未加载" : title_image ? "标题图预览" : "图片预览", "点击图片或返回按钮回到正文");
    int top = s_cover_open ? 12 : UI_CONTENT_TOP;
    if (s_image_origin[0]) {
        char line[sizeof(s_image_origin)]; copy_text(line, sizeof(line), s_image_origin);
        fit_text(line, UI_PX_CAPTION, reader_width() - 2 * UI_MARGIN);
        ui_text(fb, UI_MARGIN, top, UI_PX_CAPTION, line, EPD_DRAW_ALIGN_LEFT, false);
        top += UI_PX_CAPTION + UI_GAP;
    }
    EpdRect area = {UI_MARGIN, top, reader_width() - 2 * UI_MARGIN,
                    (s_cover_open ? reader_height() - 48 : image_back_rect().y - UI_GAP) - top};
    if (s_image_pixels && s_image_width && s_image_height) {
        int width = s_image_width, height = s_image_height;
        if (width > area.width) { height = height * area.width / width; width = area.width; }
        if (height > area.height) { width = width * area.height / height; height = area.height; }
        if (width < 1) width = 1;
        if (height < 1) height = 1;
        int left = area.x + (area.width - width) / 2, y0 = area.y + (area.height - height) / 2;
        for (int y = 0; y < height; ++y) for (int x = 0; x < width; ++x) {
            size_t src = (size_t)((int64_t)y * s_image_height / height) * s_image_width +
                         (size_t)((int64_t)x * s_image_width / width);
            epd_draw_pixel(left + x, y0 + y, s_image_pixels[src], fb);
        }
    } else ui_text(fb, area.x, area.y + area.height / 2, UI_PX_CAPTION,
                   s_image_error ? s_image_error : "图片不可用", EPD_DRAW_ALIGN_LEFT, false);
    if (s_cover_open) ui_text(fb, reader_width() / 2, reader_height() - 38, 24,
                              "封面 · 向左滑动或轻点开始阅读", EPD_DRAW_ALIGN_CENTER, false);
    else ui_draw_button(fb, image_back_rect(), "返回正文", false);
    if (!s_product_session) ui_draw_menu_handle(fb, false);
}
static EpdRect pending_back_rect(void) { return (EpdRect){UI_MARGIN, reader_height()-180, reader_width()-2*UI_MARGIN, 84}; }
static bool draw_pending_open(uint8_t* fb) {
    if (!s_open_path[0] && !s_dual_wait) return false;
    ui_clear_page(fb);
    ui_draw_header(fb, "正在打开图书", "读取字体与排版需要一些时间");
    const char* name = strrchr(s_open_path, '/');
    int y;
    if (reader_height() > reader_width()) y = draw_wrapped_name(fb, name ? name + 1 : s_open_path, 280) + 64;
    else {
        char line[BOOK_STORE_PATH_MAX]; copy_text(line, sizeof(line), name ? name + 1 : s_open_path);
        fit_text(line, UI_PX_CAPTION, reader_width() - 2 * UI_MARGIN);
        ui_text(fb, UI_MARGIN, UI_CONTENT_TOP, UI_PX_CAPTION, line, EPD_DRAW_ALIGN_LEFT, false);
        y = UI_CONTENT_TOP + 72;
    }
    ui_text(fb, UI_MARGIN, y, UI_PX_CAPTION, "正在准备阅读，请稍候…", EPD_DRAW_ALIGN_LEFT, false);
    ui_text(fb, UI_MARGIN, y + 48, UI_PX_CAPTION, "大书或首次加载字体会稍慢", EPD_DRAW_ALIGN_LEFT, false);
    ui_draw_button(fb, pending_back_rect(), "返回书架", false);
    if (!s_product_session) ui_draw_menu_handle(fb, false);
    return true;
}
static void render(app_ctx_t* ctx, uint8_t* fb) {
    if(!s_dual_wait && (s_dual_reflow || s_dual_anchor.pending)) return;
    lock_draw();
    if (draw_pending_open(fb)) { unlock_draw(); return; }
    if (s_product_session && s_view == SHELF && !s_text) {
        ui_clear_page(fb);
        ui_draw_header(fb, "图书 Books", "中键或屏幕下方返回书架");
        ui_text(fb, UI_MARGIN, UI_CONTENT_TOP, UI_PX_CAPTION, s_message, EPD_DRAW_ALIGN_LEFT, false);
        ui_draw_button(fb, progress_rect(), "返回书架", false);
        if (!s_product_session) ui_draw_menu_handle(fb, false);
        unlock_draw();
        return;
    }
    if (s_image_open) { draw_image(fb); unlock_draw(); return; }
    if (s_page_jump_open) { ui_clear_page(fb); page_jump_draw(fb); unlock_draw(); return; }
    if (s_view == SEARCH) { draw_search(fb); unlock_draw(); return; }
    if (s_view == READING && s_text) {
        draw_reader(fb, s_page);
        if (s_image_confirm_open) image_confirm_draw(fb);
    } else {
        ui_clear_page(fb);
        ui_draw_header(fb, s_view == TOC ? "目录 Contents" : "图书 Books", s_view == TOC ? s_title : s_storage);
        if (s_view != TOC) {
            draw_control(fb, ui_row_rect(0, 3, 176, 72), s_filter == 0 ? "全部来源" : s_filter == 1 ? "TF 卡" : "内置", 110);
            draw_control(fb, ui_row_rect(1, 3, 176, 72), s_recent_sort ? "按最近" : "按名称", 111);
            draw_control(fb, ui_row_rect(2, 3, 176, 72), s_query[0] ? "搜索中" : "搜索", 112);
            char shelf_hint[128];
            snprintf(shelf_hint, sizeof(shelf_hint), "已选 %u 本 · 筛选/重扫清勾选", (unsigned)selected_count());
            ui_text(fb, UI_MARGIN, 264, UI_PX_CAPTION, s_view == BULK && s_batch_message[0] ? s_batch_message : s_message[0] ? s_message : s_view == BULK ? shelf_hint : s_shelf_warning, EPD_DRAW_ALIGN_LEFT, false);
            if (s_view != BULK) ui_text(fb, UI_MARGIN, UI_CONTENT_BOTTOM - UI_PX_CAPTION, UI_PX_CAPTION,
                    s_save_failed ? "进度未保存：点此重试" : "点击打开 · 长按查看详情和管理", EPD_DRAW_ALIGN_LEFT, false);
        }
        if (s_message[0] && s_view == TOC) ui_text(fb, UI_MARGIN, UI_CONTENT_TOP, UI_PX_CAPTION, s_message, EPD_DRAW_ALIGN_LEFT, false);
        if (s_view != TOC && !s_visible_count && s_count)
            ui_text(fb, UI_MARGIN, 308, UI_PX_CAPTION, "当前筛选没有图书", EPD_DRAW_ALIGN_LEFT, false);
        if (!s_message[0] || (s_view != TOC && s_visible_count)) {
            int rows = s_view == TOC ? toc_rows() : BOOK_ROWS;
            int count = s_view == TOC ? (int)book_chapter_count() : s_visible_count;
            for (int row = 0; row < rows; ++row) {
                int i = ctx->leaf * rows + row;
                if (i >= count) break;
                EpdRect r = row_rect(row, s_view == TOC);
                char name[128], mark[32] = "";
                if (s_view == TOC) {
                    if (book_chapter_title(i, name, sizeof(name)) != ESP_OK) snprintf(name, sizeof(name), "第 %d 节", i + 1);
                    if (page_index_matches() && (size_t)i <= s_page_index.done) {
                        uint64_t first = 1;
                        for (int chapter = 0; chapter < i; ++chapter) first += s_page_index.counts[chapter];
                        snprintf(mark, sizeof(mark), "第 %llu 页", (unsigned long long)first);
                    }
                } else {
                    copy_text(name, sizeof(name), s_shelf[i].name);
                    if (s_shelf[i].has_progress) snprintf(mark, sizeof(mark), "%s %u%%", s_shelf[i].is_flash ? "内置" : "", s_shelf[i].pct);
                    else if (s_shelf[i].is_flash) copy_text(mark, sizeof(mark), "内置");
                }
                if (s_view == BULK) snprintf(mark, sizeof(mark), "%s", s_shelf[i].selected ? "已选" : "未选");
                if (s_view != TOC && s_shelf[i].removed) snprintf(mark, sizeof(mark), "%s待清理", s_view == BULK && s_shelf[i].selected ? "已选·" : "已删·");
                fit_text(name, UI_PX_BTN, r.width - 2 * UI_PAD - ttf_text_width_px(UI_PX_CAPTION, mark) - UI_GAP);
                if (s_pressed_control == row) ui_draw_pressed_round_rect(fb, r, UI_BTN_RADIUS);
                ui_draw_round_rect(fb, r, UI_BTN_RADIUS, UI_GRAY_BLACK);
                if (s_view == TOC && i == (int)s_chapter) ui_draw_selected_round_rect(fb, r, UI_BTN_RADIUS);
                ui_text_vc(fb, r.x + UI_PAD, r.y + r.height / 2, UI_PX_BTN, name, EPD_DRAW_ALIGN_LEFT, false);
                ui_text_vc(fb, r.x + r.width - UI_PAD, r.y + r.height / 2, UI_PX_CAPTION, mark, EPD_DRAW_ALIGN_RIGHT, false);
            }
        }
        draw_control(fb, book_nav_rect(0, 3), "上一页", 100);
        draw_control(fb, book_nav_rect(1, 3), s_view == TOC ? "返回阅读" : s_view == BULK ? "返回书架" : "管理", 101);
        draw_control(fb, book_nav_rect(2, 3), "下一页", 102);
        if (s_view == BULK) {
            const char* labels[] = {"本页全选", "清除勾选", "重新扫描", "删除所选", "清进度"};
            for (int i = 0; i < 5; ++i) draw_control(fb, batch_rect(i), labels[i], 610 + i);
            draw_batch_confirmation(fb);
        }
        if (!s_product_session) ui_draw_menu_handle(fb, false);
        if (s_view == MANAGE) draw_manage(fb);
        if (s_resume_path[0]) {
            EpdRect panel = {UI_MARGIN - 12, 380, ui_content_width() + 24, 350};
            ui_fill_round_rect(fb, panel, UI_BTN_RADIUS, UI_GRAY_WHITE);
            ui_draw_round_rect(fb, panel, UI_BTN_RADIUS, UI_GRAY_BLACK);
            ui_text(fb, UI_MARGIN, 410, UI_PX_BODY, "继续上次阅读？", EPD_DRAW_ALIGN_LEFT, false);
            char name[256];
            copy_text(name, sizeof(name), s_resume_name);
            fit_text(name, UI_PX_CAPTION, ui_content_width());
            ui_text(fb, UI_MARGIN, 478, UI_PX_CAPTION, name, EPD_DRAW_ALIGN_LEFT, false);
            ui_text(fb, UI_MARGIN, 530, UI_PX_CAPTION, "确认后才加载图书", EPD_DRAW_ALIGN_LEFT, false);
            draw_control(fb, ui_row_rect(0, 2, 620, UI_BTN_H), "留在书架", 700);
            draw_control(fb, ui_row_rect(1, 2, 620, UI_BTN_H), "继续阅读", 701);
        }
    }
    unlock_draw();
}
static bool present(app_ctx_t* ctx, app_redraw_t redraw) {
    if (redraw == APP_REDRAW_NONE || redraw == APP_REDRAW_DONE) return true;
    int64_t start = esp_timer_get_time();
    if (redraw == APP_REDRAW_PAGE || redraw == APP_REDRAW_FULL) render(ctx, ctx->fb);
    ps_global_draw_controls(&app_book, ctx->fb);
    bool prep = kick_prep();
    int64_t drawn = esp_timer_get_time();
    enum EpdDrawError err;
    if (redraw == APP_REDRAW_FULL || s_full) err = update_display_full(ctx->hl);
    else if (redraw == APP_REDRAW_AREA) {
        err = update_display_area_with(ctx->hl, &E0470_WAVEFORM, s_mode, s_area);
        if (s_reader_split) {
            // 页码即时更新；百分比不变时轨道像素不变，DU不会擦掉整条轨道。
            // Page numbers stay current; unchanged percentages leave the track unchanged, so DU never wipes the whole track.
            err = (enum EpdDrawError)(err | update_display_area_with(ctx->hl, &E0470_FOLLOW_WAVEFORM,
                                                                     MODE_DU, s_product_session ? (EpdRect){0, 0, reader_width(), 84} : progress_rect()));
            ESP_LOGI(TAG, "reader regions body_h=%d footer=diff pct=%u", s_area.height, percent(s_page));
        }
    }
    else err = update_display_mode(ctx->hl, APP_PAGE_REFRESH_MODE);
    int64_t displayed = esp_timer_get_time();
    if (prep) xSemaphoreTake(s_prep_done, portMAX_DELAY);
    if (redraw == APP_REDRAW_AREA && s_mode == MODE_DU && !s_full) {
        s_du_area = s_du_count ? ui_rect_union(s_du_area, s_area) : s_area;
        ++s_du_count;
        s_du_ms = esp_timer_get_time() / 1000;
    } else s_du_count = 0;
    ESP_LOGI(TAG, "present draw=%lld display=%lld join=%lld ms", (drawn - start) / 1000,
             (displayed - drawn) / 1000, (esp_timer_get_time() - displayed) / 1000);
    guard_draw_result(ctx->hl, err);
    // 仅实际正文推屏成功后确认左页，失败保留重试；不以已排版代替已显示。
    // Confirm the left page only after successful text presentation; retain failures for retry, never equate layout with display.
    bool presented_text = redraw == APP_REDRAW_PAGE || redraw == APP_REDRAW_FULL || s_reader_split;
    if (err == EPD_DRAW_SUCCESS && presented_text && s_dual_present_pending && !s_dual_target &&
        !s_open_path[0] && s_view == READING && !s_page_jump_open && !s_toolbar &&
        !s_selection && !s_image_open && !s_image_confirm_open && !s_scrubbing && !s_progress_pending) {
        ps_dual_presented(s_dual_present_pending); s_dual_present_pending = 0;
    }
    // 首帧推屏（含欠载恢复）返回后才允许读书和字体IO；render本身不改调度状态。
    // Permit book/font IO only after first-frame presentation (including underrun recovery); render never changes scheduling state.
    if (s_open_path[0] && (redraw == APP_REDRAW_PAGE || redraw == APP_REDRAW_FULL)) s_open_presented = true;
    s_full = false;
    s_reader_split = false;
    s_mode = MODE_GL16;
    return true;
}
// 墨水屏翻页过渡：保留旧页，按斜向页边推进 4 帧，再用正常波形定稿。
// 页边不是硬黑线：用 30px 软阴影和 1px 灰边，避免滑动时出现一条突兀的黑缝。
// E-ink page turn: retain the old page, advance a slanted page edge across four
// frames, then settle. The edge is a soft 30 px shadow plus a 1 px gray line,
// not the previous harsh black seam.
static void turn_anim_free(void) {
    free(s_turn_old);
    s_turn_old = NULL;
}
static void turn_anim_compose(uint8_t *dst, const uint8_t *old, const uint8_t *fresh, int dir, int progress) {
    EpdRect body = reader_area();
    EpdRect pr = ui_rotate_rect_to_fb(body);
    int pw = epd_width(), ph = epd_height();
    enum EpdRotation rotation = epd_get_rotation();
    bool axis_x = rotation == EPD_ROT_LANDSCAPE || rotation == EPD_ROT_INVERTED_LANDSCAPE;
    bool axis_inverted = rotation == EPD_ROT_INVERTED_LANDSCAPE;
    int logical_w = reader_width();
    int seam = dir > 0 ? logical_w * (100 - progress) / 100 : logical_w * progress / 100;
    int shadow = 24;
    int x0 = pr.x < 0 ? 0 : pr.x;
    int y0 = pr.y < 0 ? 0 : pr.y;
    int x1 = pr.x + pr.width > pw ? pw : pr.x + pr.width;
    int y1 = pr.y + pr.height > ph ? ph : pr.y + pr.height;
    int row_bytes = pw / 2;
    for (int y = y0; y < y1; ++y) {
        for (int x = x0; x < x1; ++x) {
            int at = axis_x ? x : y;
            int base = axis_x ? pr.x : pr.y;
            int span = axis_x ? pr.width : pr.height;
            if (span < 1) span = 1;
            int logical_x = axis_inverted && axis_x ? span - 1 - (at - base) : at - base;
            // 斜向页边：两端各偏 15px，像纸张从一角掀起。
            // Slanted edge: +/-15 px at the ends, like a sheet lifting from a corner.
            int edge = seam + (at - base - span / 2) * 30 / span;
            bool take_new = dir > 0 ? logical_x >= edge : logical_x <= edge;
            int dist = logical_x > edge ? logical_x - edge : edge - logical_x;
            size_t idx = (size_t)y * row_bytes + x / 2;
            int shift = (x & 1) * 4;
            uint8_t old_byte = dst[idx];
            int old_gray = (old[idx] >> shift) & 15;
            int new_gray = (fresh[idx] >> shift) & 15;
            int gray = take_new ? new_gray : old_gray;
            // 白色页边 + 旧页一侧的柔和阴影；不再同时压暗两侧形成黑缝。
            // White page edge plus a soft shadow on the old side only; no dark line on both sides.
            if (dist <= 1) gray = 15;
            else if (dist < shadow && !take_new) {
                int amount = (shadow - dist) * 48 / shadow;
                gray = gray * (100 - amount) / 100 + 12 * 17 * amount / 100;
            }
            dst[idx] = (uint8_t)((old_byte & ~(15 << shift)) | ((gray & 15) << shift));
        }
    }
}
static app_redraw_t paint_reading(app_ctx_t* ctx, enum EpdDrawMode mode) {
    prepare_page_images();
    int64_t started = esp_timer_get_time();
    lock_draw();
    bool cached = !s_toolbar && !s_selection && !s_scrubbing && !s_clear_confirm && s_next_fb && s_next_page == (int)s_page;
    bool animate = s_turn_anim_pending && cached && !s_image_open && !s_image_confirm_open && !s_page_jump_open;
    s_turn_anim_pending = false;
    if (animate) {
        if (!s_turn_old)
            s_turn_old = heap_caps_aligned_alloc(16, fb_bytes(), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (s_turn_old) {
            static const int steps[] = {28, 52, 74, 90};
            memcpy(s_turn_old, ctx->fb, fb_bytes());
            s_area = reader_area();
            s_mode = MODE_DU;
            s_reader_split = false;
            for (size_t i = 0; i < sizeof(steps) / sizeof(steps[0]); i++) {
                turn_anim_compose(ctx->fb, s_turn_old, s_next_fb, s_turn_anim_dir, steps[i]);
                unlock_draw();
                guard_draw_result(ctx->hl, update_display_area_with(ctx->hl, &E0470_WAVEFORM, MODE_DU, s_area));
                vTaskDelay(pdMS_TO_TICKS(12));
                lock_draw();
            }
        }
    }
    if (cached)
        memcpy(ctx->fb, s_next_fb, fb_bytes());
    else draw_reader(ctx->fb, s_page);
    unlock_draw();
    ESP_LOGI(TAG, "paint cached=%d ms=%lld", cached, (esp_timer_get_time() - started) / 1000);
    s_area = reader_area();
    s_reader_split = true;
    s_mode = mode;
    return APP_REDRAW_AREA;
}
static void loading_detail(app_ctx_t* ctx, const char* text, const char* detail) {
    lock_draw();
    EpdRect r = progress_rect();
    if (s_page_jump_open) {
        // 数字跳页期间只更新弹窗状态，避免横屏进度条覆盖底部操作键。
        // Numeric jumps update only modal status so the landscape track cannot cover action buttons.
        EpdRect panel = page_jump_rect();
        bool landscape = reader_width() > reader_height();
        r = (EpdRect){panel.x + 24, panel.y + (landscape ? 238 : 202),
                      landscape ? panel.width / 2 - 48 : panel.width - 48, 36};
    }
    ui_clear_rect_fast(ctx->fb, r);
    char line[160]; copy_text(line, sizeof(line), text);
    fit_text(line, UI_PX_CAPTION, r.width);
    ui_text(ctx->fb, r.x, r.y, UI_PX_CAPTION, line, EPD_DRAW_ALIGN_LEFT, false);
    if (detail && !s_page_jump_open) ui_text(ctx->fb, r.x, r.y + 36, UI_PX_CAPTION, detail, EPD_DRAW_ALIGN_LEFT, false);
    unlock_draw();
    guard_draw_result(ctx->hl, update_display_area_with(ctx->hl, &E0470_WAVEFORM, MODE_DU, r));
}
static void loading(app_ctx_t* ctx, const char* text) { loading_detail(ctx, text, NULL); }
// OPF独立封面是书首视图，不插入spine/正文，不移动旧页码和批注锚点。
// A declared cover is an opening view, never inserted into spine/text or old page/annotation anchors.
static bool open_cover(void) {
    if (book_kind() != BOOK_KIND_EPUB || !s_path[0] || s_cover_missing) return false;
    lock_draw();
    invalidate_prep(); free_image();
    free(s_next_fb); s_next_fb = NULL;
    turn_anim_free();
    esp_err_t err = book_epub_load_cover(s_path, &s_image_pixels, &s_image_width, &s_image_height);
    if (err == ESP_OK && s_image_pixels) s_cover_open = s_image_open = true;
    else { free_image(); s_cover_missing = err != ESP_ERR_NO_MEM; }
    unlock_draw(); ensure_prep();
    return s_cover_open;
}
static app_redraw_t open_image(app_ctx_t* ctx, size_t block_index) {
    if (block_index >= s_block_count || !s_blocks[block_index].image_src) return APP_REDRAW_NONE;
    blk_t* block = &s_blocks[block_index];
    if (s_image_block != block_index || !s_image_pixels) {
        free_image();
        invalidate_prep();
        loading_detail(ctx, "正在加载图片…", "加载后可返回原阅读位置");
        lock_draw();
        free(s_next_fb); s_next_fb = NULL;
        turn_anim_free();
        esp_err_t err = book_chapter_load_image(s_chapter, block->image_src, &s_image_pixels, &s_image_width, &s_image_height);
        unlock_draw();
        ensure_prep();
        s_image_block = block_index;
        if (err != ESP_OK) {
            s_image_error = err == ESP_ERR_NO_MEM ? "内存不足，请返回后重试" :
                            err == ESP_ERR_NOT_FOUND ? "图片文件缺失" :
                            err == ESP_ERR_INVALID_SIZE ? "图片超出大小限制或文件损坏" : "图片损坏或格式暂不支持";
        }
        if (block->image_repeated && !block->image_title) {
            char title[160] = {0};
            (void)book_chapter_title(block->image_first_chapter, title, sizeof(title));
            snprintf(s_image_origin, sizeof(s_image_origin), "已读最早：第 %u 节 · %s", (unsigned)block->image_first_chapter + 1, title);
        }
    }
    s_image_open = true;
    s_size_settle_ms = 0;
    invalidate_prep();
    return APP_REDRAW_PAGE;
}
static EpdRect image_confirm_rect(void) {
    int width = reader_width() - 64;
    if (width > 780) width = 780;
    return (EpdRect){(reader_width() - width) / 2, (reader_height() - 300) / 2, width, 300};
}
static EpdRect image_confirm_button_rect(int button) {
    EpdRect panel = image_confirm_rect(); int width = (panel.width - 60) / 2;
    return (EpdRect){panel.x + 24 + button * (width + 12), panel.y + 204, width, 72};
}
static int image_confirm_control_at(uint16_t x, uint16_t y, EpdRect* rect) {
    for (int i = 0; i < 2; ++i) {
        *rect = image_confirm_button_rect(i);
        if (ui_rect_hit(*rect, x, y)) return BOOK_IMAGE_CONFIRM_CONTROL + i;
    }
    return -1;
}
static void image_confirm_draw(uint8_t* fb) {
    EpdRect panel = image_confirm_rect();
    ui_fill_round_rect(fb, panel, UI_BTN_RADIUS, UI_GRAY_WHITE);
    ui_draw_round_rect(fb, panel, UI_BTN_RADIUS, UI_GRAY_BLACK);
    ui_text(fb, panel.x + 24, panel.y + 24, UI_PX_BODY, "全屏预览图片？", EPD_DRAW_ALIGN_LEFT, false);
    ui_text(fb, panel.x + 24, panel.y + 96, UI_PX_CAPTION,
            "确认后加载图片，可随时返回正文", EPD_DRAW_ALIGN_LEFT, false);
    const char* labels[] = {"继续阅读", "全屏预览"};
    for (int i = 0; i < 2; ++i)
        draw_control(fb, image_confirm_button_rect(i), labels[i], BOOK_IMAGE_CONFIRM_CONTROL + i);
}
static app_redraw_t image_confirm_request(size_t block_index) {
    if (block_index >= s_block_count || !s_blocks[block_index].image_src) return APP_REDRAW_NONE;
    cancel_interaction(); page_index_pause();
    s_image_confirm_block = block_index; s_image_confirm_open = true;
    s_toolbar = false; s_size_settle_ms = 0; s_pressed_control = -1;
    invalidate_prep();
    return APP_REDRAW_PAGE;
}
static app_redraw_t image_confirm_action(app_ctx_t* ctx, int control) {
    if (!s_image_confirm_open || (control != BOOK_IMAGE_CONFIRM_CONTROL && control != BOOK_IMAGE_CONFIRM_CONTROL + 1))
        return APP_REDRAW_NONE;
    size_t block = s_image_confirm_block;
    image_confirm_close();
    if (control == BOOK_IMAGE_CONFIRM_CONTROL) return APP_REDRAW_PAGE;
    app_redraw_t redraw = open_image(ctx, block);
    return redraw == APP_REDRAW_NONE ? APP_REDRAW_PAGE : redraw;
}

static const char* book_error_message(esp_err_t err) {
    if (err == ESP_ERR_NO_MEM) return "解析失败：内存不足，请稍后重试";
    if (err == ESP_ERR_INVALID_SIZE) return "超出上限：32768 项或资源过大";
    if (err == ESP_ERR_NOT_SUPPORTED) return "文件格式、压缩或加密暂不支持";
    if (err == ESP_ERR_NOT_FOUND) return "书籍或章节缺失，请检查文件和卡";
    return "解析失败：文件异常，请重新传入";
}

/* ---- 文件与进度 / Files and progress ---- */
static void free_book(void) {
    page_jump_close(); page_index_reset();
    s_selection = s_selection_drag = s_selection_menu = s_scrubbing = s_jump_back_valid = false;
    pending_progress_t* pending = pending_find(s_path);
    if (pending && !pending->dirty) pending_discard(s_path);
    invalidate_prep();
    free_image();
    book_layout_free();
    free(s_text);
    html_blocks_free(s_blocks, s_block_count);
    s_blocks = NULL;
    s_block_count = 0;
    s_text = NULL;
    s_text_len = 0;
    book_close();
    s_path[0] = 0;
    s_cover_missing = false;
}
static bool load_chapter_position(app_ctx_t* ctx, size_t chapter, size_t offset, bool last_page, bool source_units) {
    loading_detail(ctx, "正在加载和排版…", "长章节需要更多时间，请稍候");
    html_text_t loaded = {0};
    esp_err_t err = book_chapter_load_blocks(chapter, &loaded);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "chapter load failed: %s", esp_err_to_name(err));
        copy_text(s_message, sizeof(s_message), book_error_message(err));
        return false;
    }
    if (source_units) {
        uint32_t start = book_chapter_byte_offset(chapter);
        uint32_t end = chapter + 1 < book_chapter_count() ? book_chapter_byte_offset(chapter + 1) : book_total_bytes();
        uint32_t span = end >= start ? end - start : 0;
        if (offset > span) offset = span;
        offset = span ? (size_t)((uint64_t)offset * loaded.len / span) : 0;
        while (offset < loaded.len && offset && ((unsigned char)loaded.utf8[offset] & 0xc0) == 0x80) --offset;
    }
    lock_draw();
    invalidate_prep();
    // 旧布局保持借用，只有目标分页成功才替换；失败不再分配内存恢复原页。
    // Retain the old borrowed layout until target pagination succeeds; failure restores it without allocating.
    bool ok = book_layout_position_blocks(loaded.utf8, loaded.len, loaded.blocks, loaded.count,
                                          body_rect(), s_px, offset, last_page);
    if (!ok) {
        html_text_free(&loaded);
        unlock_draw();
        copy_text(s_message, sizeof(s_message), "排版失败：内存不足或章节过长");
        return false;
    }
    free(s_text);
    html_blocks_free(s_blocks, s_block_count);
    free_image();
    s_text = loaded.utf8;
    s_text_len = loaded.len;
    s_blocks = loaded.blocks;
    s_block_count = loaded.count;
    s_chapter = chapter;
    s_layout_rect = body_rect(); s_layout_px = s_px;
    s_selection = s_selection_menu = s_selection_drag = false;
    s_page = last_page ? book_layout_page_count() - 1 : book_layout_page_for_offset(offset);
    if (book_chapter_title(chapter, s_title, sizeof(s_title)) != ESP_OK)
        snprintf(s_title, sizeof(s_title), "第 %u 节", (unsigned)chapter + 1);
    copy_text(s_font_path, sizeof(s_font_path), ttf_font_path());
    s_message[0] = 0;
    unlock_draw();
    prepare_page_images();
    return true;
}
static bool load_chapter(app_ctx_t* ctx, size_t chapter, size_t offset, bool last_page) {
    return load_chapter_position(ctx, chapter, offset, last_page, false);
}
static app_redraw_t progress_commit(app_ctx_t* ctx, bool back) {
    size_t old_chapter = s_chapter, old_offset = book_layout_page_start_offset(s_page);
    size_t chapter = back ? s_jump_chapter : source_chapter(s_scrub_target);
    size_t offset = back ? s_jump_offset : s_scrub_target - book_chapter_byte_offset(chapter);
    s_scrubbing = false;
    if (ps_dual_enabled()) {
        dual_anchor_start(chapter, offset, !back, 0);
        if (back) s_jump_back_valid = false;
        else { s_jump_chapter = old_chapter; s_jump_offset = old_offset; s_jump_back_valid = true; }
        return APP_REDRAW_PAGE;
    }
    save_progress();
    lock_draw(); invalidate_prep(); unlock_draw();
    if (!load_chapter_position(ctx, chapter, offset, false, !back)) {
        ps_toast("跳转失败，请重试"); return APP_REDRAW_PAGE;
    }
    s_selection = false; s_toolbar = false;
    if (back) s_jump_back_valid = false;
    else { s_jump_chapter = old_chapter; s_jump_offset = old_offset; s_jump_back_valid = true; }
    save_progress();
    return APP_REDRAW_PAGE;
}

/* ---- 全书准确页码与数字弹窗 / Exact whole-book pages and numeric dialog ---- */
static void page_index_pause(void) {
    if (!s_page_index.job && !s_page_index.loaded.utf8 && !s_page_index.loaded.blocks) return;
    lock_draw();
    book_layout_job_free(s_page_index.job);
    s_page_index.job = NULL;
    html_text_free(&s_page_index.loaded);
    memset(&s_page_index.loaded, 0, sizeof(s_page_index.loaded));
    unlock_draw();
}
static void page_index_reset(void) {
    page_index_pause();
    free(s_page_index.counts);
    memset(&s_page_index, 0, sizeof(s_page_index));
}
static bool page_index_matches(void) {
    EpdRect body = body_rect();
    const char* font = ttf_font_path();
    return s_page_index.counts && s_text && !strcmp(s_page_index.path, s_path) &&
        !strcmp(s_page_index.font, font ? font : "") && s_page_index.file_size == s_file_size &&
        s_page_index.px == s_px && s_page_index.weight == ttf_get_weight() &&
        s_page_index.chapters == book_chapter_count() && s_page_index.body.x == body.x &&
        s_page_index.body.y == body.y && s_page_index.body.width == body.width && s_page_index.body.height == body.height;
}
static bool page_index_prepare(void) {
    if (page_index_matches()) return true;
    page_index_reset();
    size_t chapters = book_chapter_count();
    if (!s_text || !chapters || chapters > SIZE_MAX / sizeof(*s_page_index.counts)) return false;
    s_page_index.counts = heap_caps_malloc(chapters * sizeof(*s_page_index.counts), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!s_page_index.counts) return false;
    memset(s_page_index.counts, 0, chapters * sizeof(*s_page_index.counts));
    s_page_index.chapters = chapters; s_page_index.file_size = s_file_size;
    s_page_index.px = s_px; s_page_index.weight = ttf_get_weight(); s_page_index.body = body_rect();
    copy_text(s_page_index.path, sizeof(s_page_index.path), s_path);
    copy_text(s_page_index.font, sizeof(s_page_index.font), ttf_font_path());
    return true;
}
static bool page_index_locate(uint32_t target, size_t* chapter, size_t* page) {
    if (!target) return false;
    uint64_t first = 0;
    for (size_t i = 0; i < s_page_index.done; ++i) {
        uint64_t end = first + s_page_index.counts[i];
        if (target <= end) { *chapter = i; *page = (size_t)(target - first - 1); return true; }
        first = end;
    }
    return false;
}
static bool page_index_step(size_t chapter, bool* changed) {
    *changed = false;
    if (!s_page_index.job || s_page_index.job_chapter != chapter) {
        page_index_pause();
        // 纯分页读取不记录图片已读历史，且不替换当前阅读章节。
        // Pagination-only loading never records visited images or replaces the active reading chapter.
        if (book_chapter_load_blocks_for_pagination(chapter, &s_page_index.loaded) != ESP_OK) goto fail;
        s_page_index.job_chapter = chapter;
        lock_draw();
        s_page_index.job = book_layout_job_create(s_page_index.loaded.utf8, s_page_index.loaded.len,
            s_page_index.loaded.blocks, s_page_index.loaded.count, s_page_index.body, s_page_index.px);
        unlock_draw();
        if (!s_page_index.job) goto fail;
    } else {
        lock_draw(); bool ok = book_layout_job_step(s_page_index.job, 2); unlock_draw();
        if (!ok) goto fail;
    }
    lock_draw();
    bool complete = book_layout_job_complete(s_page_index.job);
    size_t count = book_layout_job_page_count(s_page_index.job);
    unlock_draw();
    if (complete && chapter == s_page_index.done) {
        if (!count || count > UINT32_MAX || s_page_index.total > UINT32_MAX - count) goto fail;
        s_page_index.counts[chapter] = (uint32_t)count;
        s_page_index.total += count; ++s_page_index.done; *changed = true;
    }
    return true;
fail:
    page_index_pause(); s_page_index.failed = true; s_page_jump_waiting = false;
    copy_text(s_page_jump_message, sizeof(s_page_jump_message), "页码统计失败，请重试；原阅读位置已保留");
    return false;
}
static EpdRect page_jump_rect(void) {
    int width = reader_width() > 900 ? 1000 : reader_width() - 64;
    int height = reader_width() > reader_height() ? 500 : 680;
    return (EpdRect){(reader_width() - width) / 2, (reader_height() - 84 - height) / 2, width, height};
}
static EpdRect page_jump_key_rect(int key) {
    EpdRect panel = page_jump_rect();
    bool landscape = reader_width() > reader_height();
    int left = landscape ? panel.x + panel.width / 2 + 12 : panel.x + 24;
    int width = landscape ? panel.width / 2 - 36 : panel.width - 48;
    int top = panel.y + (landscape ? 108 : 240), height = landscape ? 64 : 76;
    int cell = (width - 20) / 3;
    return (EpdRect){left + (key % 3) * (cell + 10), top + (key / 3) * (height + 10), cell, height};
}
static EpdRect page_jump_button_rect(int button) {
    EpdRect panel = page_jump_rect(); int width = (panel.width - 72) / 3;
    return (EpdRect){panel.x + 24 + button * (width + 12), panel.y + panel.height - 76,
                     button ? width * 2 + 12 : width, 56};
}
static int page_jump_control_at(uint16_t x, uint16_t y, EpdRect* rect) {
    for (int i = 0; i < 12; ++i) { *rect = page_jump_key_rect(i); if (ui_rect_hit(*rect, x, y)) return BOOK_PAGE_CONTROL + i; }
    for (int i = 0; i < 2; ++i) { *rect = page_jump_button_rect(i); if (ui_rect_hit(*rect, x, y) && (!i || !s_page_jump_waiting)) return BOOK_PAGE_CONTROL + 12 + i * 2; }
    return -1;
}
static void page_jump_control_draw(uint8_t* fb, EpdRect rect, const char* label, int action) {
    // DU浅灰会变白；黑白反相直接反馈，不叠加多道选中框。/ DU collapses light gray to white; invert ink instead of stacking selection rings.
    bool pressed = s_pressed_control == BOOK_PAGE_CONTROL + action;
    bool inverted = action == 14 ? !pressed : pressed;
    ui_fill_round_rect(fb, rect, UI_BTN_RADIUS, inverted ? UI_GRAY_BLACK : UI_GRAY_WHITE);
    ui_draw_round_rect(fb, rect, UI_BTN_RADIUS, UI_GRAY_BLACK);
    ui_text_vc(fb, rect.x + rect.width / 2, rect.y + rect.height / 2,
               UI_PX_BTN, label, EPD_DRAW_ALIGN_CENTER, inverted);
}
static void page_jump_draw(uint8_t* fb) {
    EpdRect panel = page_jump_rect();
    bool landscape = reader_width() > reader_height();
    ui_fill_round_rect(fb, panel, UI_BTN_RADIUS, UI_GRAY_WHITE);
    ui_draw_round_rect(fb, panel, UI_BTN_RADIUS, UI_GRAY_BLACK);
    int left = panel.x + 24, text_width = landscape ? panel.width / 2 - 48 : panel.width - 48;
    ui_text(fb, left, panel.y + 22, UI_PX_BODY, "跳转到全书页码", EPD_DRAW_ALIGN_LEFT, false);
    char line[160];
    if (s_page_index.counts && s_page_index.done == s_page_index.chapters)
        snprintf(line, sizeof(line), "全书共 %llu 页", (unsigned long long)s_page_index.total);
    else snprintf(line, sizeof(line), "已完成 %u/%u 章 · %s", (unsigned)s_page_index.done,
                  (unsigned)(s_page_index.chapters ? s_page_index.chapters : book_chapter_count()),
                  s_page_jump_waiting ? "正在定位" : "输入页码后定位");
    fit_text(line, UI_PX_CAPTION, text_width);
    ui_text(fb, left, panel.y + 78, UI_PX_CAPTION, line, EPD_DRAW_ALIGN_LEFT, false);
    EpdRect input = {left, panel.y + (landscape ? 140 : 124), text_width, 64};
    ui_draw_round_rect(fb, input, UI_BTN_RADIUS, UI_GRAY_BLACK);
    epd_fill_rect((EpdRect){input.x + 12, input.y + input.height - 4, input.width - 24, 4}, UI_GRAY_BLACK, fb);
    ui_text_vc(fb, input.x + 18, input.y + input.height / 2, UI_PX_BODY,
               s_page_jump_input[0] ? s_page_jump_input : "输入页码", EPD_DRAW_ALIGN_LEFT, false);
    copy_text(line, sizeof(line), s_page_jump_message[0] ? s_page_jump_message :
              s_page_jump_waiting ? "正在定位，点取消可返回阅读" : ps_dual_enabled() ? "两台一起打开该页所在的双页" : "按当前字体、字号与屏幕方向计算");
    fit_text(line, UI_PX_CAPTION, text_width);
    ui_text(fb, left, panel.y + (landscape ? 238 : 202), UI_PX_CAPTION, line, EPD_DRAW_ALIGN_LEFT, false);
    if (landscape) {
        ui_text(fb, left, panel.y + 292, UI_PX_CAPTION, "跳转前不会改变阅读位置", EPD_DRAW_ALIGN_LEFT, false);
        ui_text(fb, left, panel.y + 338, UI_PX_CAPTION, "中键或返回：关闭此窗口", EPD_DRAW_ALIGN_LEFT, false);
    }
    const char* keys[] = {"1", "2", "3", "4", "5", "6", "7", "8", "9", "退格", "0", "清空"};
    for (int i = 0; i < 12; ++i) page_jump_control_draw(fb, page_jump_key_rect(i), keys[i], i);
    page_jump_control_draw(fb, page_jump_button_rect(0), "取消", 12);
    page_jump_control_draw(fb, page_jump_button_rect(1), s_page_jump_waiting ? "定位中" : "跳转", 14);
}
static EpdRect page_jump_status_rect(void) {
    EpdRect panel = page_jump_rect();
    return (EpdRect){panel.x + 24, panel.y + 74,
        reader_width() > reader_height() ? panel.width / 2 - 48 : panel.width - 48, 46};
}
static app_redraw_t page_jump_feedback(app_ctx_t* ctx, bool status_only) {
    lock_draw(); page_jump_draw(ctx->fb); unlock_draw();
    // 计页反馈只推统计行，避开输入框和键盘；输入反馈只驱动弹窗变化像素。
    // Count feedback pushes only the summary line, clear of the input/keypad; entry drives changed modal pixels.
    s_area = status_only ? page_jump_status_rect() : page_jump_rect();
    s_mode = MODE_DU; s_reader_split = false;
    return APP_REDRAW_AREA;
}
static void page_jump_close(void) {
    s_page_jump_open = s_page_jump_waiting = false;
    s_page_jump_message[0] = 0; s_page_jump_target = 0; s_page_jump_status_ms = 0;
    s_progress_pending = s_progress_dragged = s_scrubbing = false;
    page_index_pause(); invalidate_prep();
}
static app_redraw_t page_jump_open(void) {
    s_progress_pending = s_progress_dragged = s_scrubbing = false;
    s_page_jump_open = true; s_page_jump_waiting = false;
    s_page_jump_status_ms = 0;
    s_page_jump_input[0] = s_page_jump_message[0] = 0;
    s_toolbar = s_selection = s_selection_menu = false;
    invalidate_prep();
    if (!page_index_prepare()) copy_text(s_page_jump_message, sizeof(s_page_jump_message), "内存不足，无法统计页码，请重试");
    return APP_REDRAW_PAGE;
}
static app_redraw_t book_open_toc(app_ctx_t* ctx) {
    page_jump_close(); cancel_interaction();
    if (s_image_open) close_image();
    save_progress(); s_selection = s_selection_menu = s_toolbar = false;
    s_message[0] = 0; s_view = TOC; ctx->leaf = (int)(s_chapter / (size_t)toc_rows());
    return APP_REDRAW_PAGE;
}
static app_redraw_t page_jump_commit(app_ctx_t* ctx, size_t chapter, size_t offset) {
    if (ps_dual_enabled()) {
        if (!ps_dual_seek(s_page_jump_target)) {
            s_page_jump_waiting = false;
            copy_text(s_page_jump_message, sizeof(s_page_jump_message), "另一台尚未就绪，请重试");
            return page_jump_feedback(ctx, false);
        }
        page_jump_close(); return APP_REDRAW_PAGE;
    }
    size_t old_chapter = s_chapter, old_offset = book_layout_page_start_offset(s_page);
    page_index_pause(); save_progress();
    if (!load_chapter_position(ctx, chapter, offset, false, false)) {
        s_page_jump_waiting = false;
        copy_text(s_page_jump_message, sizeof(s_page_jump_message), "跳转失败，原阅读位置已保留，请重试");
        return page_jump_feedback(ctx, false);
    }
    s_jump_chapter = old_chapter; s_jump_offset = old_offset; s_jump_back_valid = true;
    s_toolbar = s_selection = false; page_jump_close();
    save_progress(); return APP_REDRAW_PAGE;
}
static app_redraw_t page_jump_action(app_ctx_t* ctx, int control) {
    int action = control - BOOK_PAGE_CONTROL;
    if (action == 12) { page_jump_close(); return APP_REDRAW_PAGE; }
    if (action == 14) {
        if (s_page_jump_waiting) return APP_REDRAW_NONE;
        uint64_t value = 0;
        for (const char* p = s_page_jump_input; *p; ++p) {
            if (*p < '0' || *p > '9' || value > (UINT32_MAX - (uint32_t)(*p - '0')) / 10) {
                copy_text(s_page_jump_message, sizeof(s_page_jump_message), "页码超出可输入范围"); return page_jump_feedback(ctx, false);
            }
            value = value * 10 + (unsigned)(*p - '0');
        }
        if (!value) { copy_text(s_page_jump_message, sizeof(s_page_jump_message), "请输入大于 0 的页码"); return page_jump_feedback(ctx, false); }
        if (!page_index_prepare()) { copy_text(s_page_jump_message, sizeof(s_page_jump_message), "内存不足，无法统计页码，请重试"); return page_jump_feedback(ctx, false); }
        if (s_page_index.done == s_page_index.chapters && value > s_page_index.total) {
            copy_text(s_page_jump_message, sizeof(s_page_jump_message), "页码超出全书范围，请重新输入"); return page_jump_feedback(ctx, false);
        }
        s_page_index.failed = false; s_page_jump_target = (uint32_t)value;
        s_page_jump_waiting = true; s_page_jump_message[0] = 0;
        s_page_jump_status_ms = ctx->now_ms;
        return page_jump_feedback(ctx, false);
    }
    if (action < 0 || action > 11) return APP_REDRAW_NONE;
    s_page_jump_waiting = false; s_page_jump_message[0] = 0;
    page_index_pause();
    size_t length = strlen(s_page_jump_input);
    if (action == 9) { if (length) s_page_jump_input[length - 1] = 0; }
    else if (action == 11) s_page_jump_input[0] = 0;
    else if (length < BOOK_PAGE_DIGITS) {
        s_page_jump_input[length] = action == 10 ? '0' : (char)('1' + action);
        s_page_jump_input[length + 1] = 0;
    } else copy_text(s_page_jump_message, sizeof(s_page_jump_message), "页码过长，请退格修改");
    return page_jump_feedback(ctx, false);
}
static app_redraw_t page_jump_tick(app_ctx_t* ctx) {
    if (!s_page_jump_open || !s_page_jump_waiting || !s_text || (ctx->touch && ctx->touch->touched)) return APP_REDRAW_NONE;
    if (!page_index_prepare()) return APP_REDRAW_NONE;
    if (s_page_index.failed) return APP_REDRAW_NONE;
    size_t chapter = s_page_index.done, page = 0;
    bool located = s_page_jump_waiting && page_index_locate(s_page_jump_target, &chapter, &page);
    if (located && chapter == s_chapter && page < book_layout_page_count())
        return page_jump_commit(ctx, chapter, book_layout_page_start_offset(page));
    if (!located && s_page_index.done == s_page_index.chapters) {
        if (s_page_jump_waiting) {
            s_page_jump_waiting = false;
            copy_text(s_page_jump_message, sizeof(s_page_jump_message), "页码超出全书范围，请重新输入");
            return page_jump_feedback(ctx, false);
        }
        return APP_REDRAW_NONE;
    }
    bool changed = false;
    if (!page_index_step(chapter, &changed)) return page_jump_feedback(ctx, false);
    if (s_page_jump_waiting) {
        size_t target_chapter, target_page;
        bool known = page_index_locate(s_page_jump_target, &target_chapter, &target_page);
        if (!known && s_page_index.job_chapter == s_page_index.done && s_page_jump_target > s_page_index.total) {
            target_chapter = s_page_index.done;
            target_page = (size_t)(s_page_jump_target - s_page_index.total - 1);
            known = true;
        }
        if (known && target_chapter == s_page_index.job_chapter) {
            size_t offset;
            lock_draw(); bool ready = book_layout_job_page_offset(s_page_index.job, target_page, &offset); unlock_draw();
            if (ready) return page_jump_commit(ctx, target_chapter, offset);
        }
    }
    lock_draw(); bool complete = book_layout_job_complete(s_page_index.job); unlock_draw();
    if (complete) page_index_pause();
    if (changed && ctx->now_ms - s_page_jump_status_ms >= 1000) {
        s_page_jump_status_ms = ctx->now_ms;
        return page_jump_feedback(ctx, true);
    }
    return APP_REDRAW_NONE;
}
static EpdRect progress_contact_rect(void) {
    if (s_product_session && s_view == READING && s_text) return progress_rect();
    int top = reader_footer_top() - 8;
    return (EpdRect){0, top, reader_width(), reader_height() - 96 - top};
}
static app_redraw_t progress_contact_move(app_ctx_t* ctx, const ui_gesture_event_t* event) {
    int dx = (int)event->x - event->x0, dy = (int)event->y - event->y0;
    if (!s_progress_dragged && abs(dx) > UI_TOUCH_SLOP_PX && abs(dx) > abs(dy)) {
        s_progress_dragged = s_scrubbing = true; s_scrub_x = -1;
    }
    if (s_progress_dragged) ctx->capture_gesture = true;
    if (s_progress_dragged && progress_preview(event->x)) return paint_progress(ctx, MODE_DU);
    return APP_REDRAW_NONE;
}
static app_redraw_t progress_contact_release(app_ctx_t* ctx, const ui_gesture_event_t* event) {
    bool dragged = s_progress_dragged;
    s_progress_pending = s_progress_dragged = false;
    // 终态坐标不再推断拖动或更新目标；轻点只开输入，真实拖动使用最后一次MOVE预览。
    // Terminal coordinates never infer a drag or update its target; taps enter a page and drags commit the last MOVE preview.
    if (event->type == UI_GESTURE_TAP) { s_scrubbing = false; return page_jump_open(); }
    if (event->type == UI_GESTURE_RELEASE && dragged) return progress_commit(ctx, false);
    s_scrubbing = false;
    return APP_REDRAW_NONE;
}
static bool open_book(app_ctx_t* ctx, const char* path) {
    if (!strncmp(path, "/sdcard/", 8)) {
        read_pico_sd_info_t sd = {0};
        read_pico_sd_get_info(&sd);
        if (!sd.present || !sd.mounted) {
            copy_text(s_message, sizeof(s_message), "TF 卡不可用，请重新挂载后打开");
            return false;
        }
    }
    // 自动续读也不能绕过同路径旧文件的清理重试。
    // Automatic resume must also finish cleanup of the old file at this path.
    if (delete_retry_find(path)) {
        copy_text(s_message, sizeof(s_message), "文件已删除，进度清理失败，请重试");
        return false;
    }
    struct stat st;
    if (stat(path, &st) != 0 || !S_ISREG(st.st_mode) || st.st_size < 0 || (uint64_t)st.st_size > UINT32_MAX) {
        copy_text(s_message, sizeof(s_message), "无法读取文件，请检查存储卡");
        return false;
    }
    bool annotated = s_annotation_pending && !strcmp(path, s_annotation_target.path);
    if (annotated && (uint64_t)st.st_size != s_annotation_target.file_bytes) {
        copy_text(s_message, sizeof(s_message), "图书文件已更换，原批注位置无法定位");
        return false;
    }
    if (strncmp(path, "/flash/", 7) == 0 && st.st_size > BOOK_STORE_FLASH_FILE_MAX) {
        copy_text(s_message, sizeof(s_message), "内置存储单本限 1 MB");
        return false;
    }
    if (!pending_reserve(path)) {
        copy_text(s_message, sizeof(s_message), "内存不足，无法保留待保存进度");
        return false;
    }
    save_progress();
    free_book();
    if (!pending_reserve(path)) {
        copy_text(s_message, sizeof(s_message), "内存不足，无法打开图书");
        return false;
    }
    loading_detail(ctx, "正在解析图书…", "大书需要更多时间，请稍候");
    // 持绘制锁释放下一页缓存，降低解析峰值。/ Release the next-page buffer under the drawing lock to reduce parsing peaks.
    lock_draw(); free(s_next_fb); s_next_fb = NULL; turn_anim_free(); unlock_draw();
    int64_t open_started = esp_timer_get_time();
    esp_err_t err = book_open(path);
    int64_t source_ms = (esp_timer_get_time() - open_started) / 1000;
    ensure_prep();
    if (err != ESP_OK) {
        pending_progress_t* pending = pending_find(path);
        if (pending && !pending->dirty) pending_discard(path);
        copy_text(s_message, sizeof(s_message), book_error_message(err));
        ESP_LOGW(TAG, "open failed: %s", esp_err_to_name(err));
        return false;
    }
    copy_text(s_path, sizeof(s_path), path);
    s_file_size = (uint32_t)st.st_size;
    book_progress_t p = {0};
    bool resume = pending_restore(path, s_file_size, &p) || book_progress_load(path, s_file_size, &p);
    s_px = resume ? p.px : app_settings_book_px();
    if (s_px < BOOK_PX_MIN || s_px > BOOK_PX_MAX) s_px = 48;
    if(ps_dual_enabled()){s_px=s_dual_reflow?s_dual_old_px:s_dual_px;resume=false;s_dual_blank=false;if(!s_dual_target&&!s_dual_annotation)s_dual_target=ps_dual_role()==1?1:2;}
    size_t chapter = resume && p.chapter < book_chapter_count() ? p.chapter : 0;
    if (annotated && s_annotation_target.chapter >= book_chapter_count()) {
        free_book(); copy_text(s_message, sizeof(s_message), "批注所在章节不存在"); return false;
    }
    if (annotated) chapter = s_annotation_target.chapter;
    size_t offset = annotated ? s_annotation_target.start : resume && p.chapter == chapter ? p.byte_off : 0;
    if (!load_chapter(ctx, chapter, offset, false)) {
        free_book();
        return false;
    }
    if (annotated && !restore_annotation()) { free_book(); return false; }
    if (annotated && ps_dual_enabled()) {
        s_selection = s_selection_menu = false;
        dual_anchor_start(s_annotation_target.chapter, s_annotation_target.start, false, 0);
    }
    // 已有进度直接续读；spine本来以图片开篇时不再重复插入独立封面。
    // Resume immediately; an image already leading the spine does not need a duplicate opening cover.
    if (!ps_dual_enabled() && !resume && !annotated && !(s_block_count && s_blocks[0].image_src)) (void)open_cover();
    s_view = READING;
    s_toolbar = s_clear_confirm = s_batch_confirm = false;
    s_turns = s_unsaved = 0;
    s_size_settle_ms = 0;
    pending_mark_latest(s_path);
    if (s_product_session) copy_text(s_last_product_path, sizeof(s_last_product_path), s_path);
    save_progress();
    ESP_LOGI(TAG, "opened kind=%d chapters=%u pages=%u px=%d source=%lld chapter=%lld ms", book_kind(), (unsigned)book_chapter_count(), (unsigned)book_layout_page_count(), s_px,
             (long long)source_ms, (long long)((esp_timer_get_time() - open_started) / 1000 - source_ms));
    return true;
}
static app_redraw_t resume_choice(app_ctx_t* ctx, bool confirmed) {
    if (!s_resume_path[0]) return APP_REDRAW_NONE;
    char path[BOOK_STORE_PATH_MAX];
    copy_text(path, sizeof(path), s_resume_path);
    s_resume_path[0] = 0;
    if (confirmed) open_book(ctx, path);
    return APP_REDRAW_PAGE;
}
/* ---- 书架数据 / Shelf data ---- */
static bool shelf_matches(const shelf_entry_t* item) {
    return item->search_match && (s_filter == 0 || (s_filter == 1 && !item->is_flash) || (s_filter == 2 && item->is_flash));
}
static int compare_books(const void* a, const void* b) {
    const shelf_entry_t* x = a;
    const shelf_entry_t* y = b;
    if (shelf_matches(x) != shelf_matches(y)) return shelf_matches(x) ? -1 : 1;
    if (s_recent_sort && x->recent != y->recent) return x->recent > y->recent ? -1 : 1;
    int name = strcasecmp(x->name, y->name);
    return name ? name : strcmp(x->path, y->path);
}
static void sort_shelf(app_ctx_t* ctx) {
    if (s_count > 1) qsort(s_shelf, s_count, sizeof(*s_shelf), compare_books);
    s_visible_count = 0;
    while (s_visible_count < s_count && shelf_matches(&s_shelf[s_visible_count])) ++s_visible_count;
    ctx->leaf = 0;
}
static bool shelf_reserve(void) {
    if ((size_t)s_count < s_shelf_capacity) return true;
    if (s_count == INT_MAX) return false;
    size_t cap = s_shelf_capacity ? s_shelf_capacity * 2 : 32;
    if (cap > INT_MAX || cap > SIZE_MAX / sizeof(*s_shelf)) return false;
    shelf_entry_t* entries = heap_caps_realloc(s_shelf, cap * sizeof(*entries), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!entries) return false;
    s_shelf = entries;
    s_shelf_capacity = cap;
    return true;
}

// 在删除文件前预留记录，避免删除成功后才遇到内存不足。
// Reserve before unlink so allocation failure never loses a completed deletion's retry.
static delete_retry_t* delete_retry_reserve(const shelf_entry_t* entry) {
    delete_retry_t* existing = delete_retry_find(entry->path);
    if (existing) return existing;
    delete_retry_t* p = heap_caps_malloc(sizeof(*p), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!p) p = malloc(sizeof(*p));
    if (!p) return NULL;
    p->entry = *entry;
    p->next = s_delete_retries;
    s_delete_retries = p;
    return p;
}
static void delete_retry_discard(const char* path) {
    delete_retry_t** p = &s_delete_retries;
    while (*p) {
        if (!strcmp((*p)->entry.path, path)) {
            delete_retry_t* old = *p;
            *p = old->next;
            free(old);
            return;
        }
        p = &(*p)->next;
    }
}
static void scan_shelf(app_ctx_t* ctx) {
    s_count = 0;
    s_visible_count = 0;
    s_message[0] = 0;
    s_shelf_warning[0] = 0;
    if (!book_store_flash_ready()) loading(ctx, "初始化内置存储…");
    book_store_root_t roots[2];
    int n = 0;
    esp_err_t root_err = book_store_roots(roots, &n);
    bool truncated = false, unreadable = root_err != ESP_OK || book_store_roots_degraded(), skipped = false;
    s_storage[0] = 0;
    for (int i = 0; i < n; ++i) {
        char capacity[64];
        snprintf(capacity, sizeof(capacity), "%s%s %.1f MB", i ? " · " : "", roots[i].is_flash ? "内置余" : "TF余", book_store_free_bytes(&roots[i]) / 1048576.0);
        strncat(s_storage, capacity, sizeof(s_storage) - strlen(s_storage) - 1);
        DIR* dir = opendir(roots[i].path);
        if (!dir) { unreadable = true; continue; }
        struct dirent* ent;
        for (;;) {
            errno = 0;
            ent = readdir(dir);
            if (!ent) { if (errno) unreadable = true; break; }
            const char* ext = strrchr(ent->d_name, '.');
            if (!ext || (strcasecmp(ext, ".txt") && strcasecmp(ext, ".epub"))) continue;
            shelf_entry_t candidate = {0};
            shelf_entry_t* item = &candidate;
            int len = snprintf(item->path, sizeof(item->path), "%s/%s", roots[i].path, ent->d_name);
            if (len < 0 || (size_t)len >= sizeof(item->path) || strlen(ent->d_name) >= sizeof(item->name)) { skipped = true; continue; }
            struct stat st;
            if (stat(item->path, &st) != 0) { unreadable = true; continue; }
            if (!S_ISREG(st.st_mode)) continue;
            if (st.st_size < 0 || (uint64_t)st.st_size > UINT32_MAX) { skipped = true; continue; }
            if (!shelf_reserve()) { truncated = true; break; }
            copy_text(item->name, sizeof(item->name), ent->d_name);
            item->search_match = read_pico_search_match(item->name, s_query);
            item->size = st.st_size;
            item->is_flash = roots[i].is_flash;
            book_progress_t p;
            item->has_progress = book_progress_load(item->path, item->size, &p);
            item->pct = item->has_progress ? p.pct : 0;
            item->recent = item->has_progress ? p.last_open_s : 0;
            s_shelf[s_count++] = candidate;
        }
        closedir(dir);
    }
    // 同路径重新出现也先完成旧清理，避免新阅读进度被后续重试擦掉。
    // Finish old cleanup even if the path reappears, before new reading can create progress.
    for (delete_retry_t* p = s_delete_retries; p; p = p->next) {
        int i = 0;
        while (i < s_count && strcmp(s_shelf[i].path, p->entry.path)) ++i;
        if (i == s_count) {
            if (!shelf_reserve()) { truncated = true; continue; }
            ++s_count;
        }
        s_shelf[i] = p->entry;
        s_shelf[i].selected = false;
        s_shelf[i].search_match = read_pico_search_match(p->entry.name, s_query);
    }
    sort_shelf(ctx);
    if (!n) copy_text(s_storage, sizeof(s_storage), "存储不可用，请检查 TF 卡");
    if (truncated) snprintf(s_shelf_warning, sizeof(s_shelf_warning), "内存不足，仅列 %d 本；释放后重扫", s_count);
    else if (unreadable) copy_text(s_shelf_warning, sizeof(s_shelf_warning), "部分目录或文件不可读，请检查后重扫");
    else if (skipped) copy_text(s_shelf_warning, sizeof(s_shelf_warning), "部分文件过大或名称过长，未列入");
    else if (s_pending_invalidated) copy_text(s_shelf_warning, sizeof(s_shelf_warning), "图书已更新，旧待保存进度已作废");
    if (!s_count && !unreadable && !truncated) copy_text(s_message, sizeof(s_message), "暂无图书，请传入 TXT 或 EPUB");
    ESP_LOGI(TAG, "shelf books=%d roots=%d", s_count, n);
}

static void refresh_capacity(void) {
    book_store_root_t roots[2];
    int count = 0;
    s_storage[0] = 0;
    book_store_roots(roots, &count);
    for (int i = 0; i < count; ++i) {
        char line[64];
        snprintf(line, sizeof(line), "%s%s %.1f MB", i ? " · " : "", roots[i].is_flash ? "内置余" : "TF余", book_store_free_bytes(&roots[i]) / 1048576.0);
        strncat(s_storage, line, sizeof(s_storage) - strlen(s_storage) - 1);
    }
}
static void manage_apply(app_ctx_t* ctx) {
    if (!strcmp(s_path, s_managed.path)) free_book();
    esp_err_t err;
    if (s_delete_confirm && !s_file_removed) {
        delete_retry_t* retry = delete_retry_reserve(&s_managed);
        if (!retry) {
            copy_text(s_manage_message, sizeof(s_manage_message), "内存不足，无法保留删除重试记录");
            s_clear_confirm = false;
            return;
        }
        bool removed = false;
        err = book_store_delete(s_managed.path, &removed);
        if (removed) {
            retry->entry.removed = true;
            s_file_removed = true;
            pending_discard(s_managed.path);
            book_store_notify_changed();
            s_store_revision = book_store_revision();
            for (int i = 0; i < s_count; ++i) if (!strcmp(s_shelf[i].path, s_managed.path)) s_shelf[i].removed = true;
            refresh_capacity();
        } else delete_retry_discard(s_managed.path);
    } else {
        pending_discard(s_managed.path);
        err = book_progress_forget(s_managed.path);
    }
    s_clear_confirm = false;
    if (err != ESP_OK) {
        copy_text(s_manage_message, sizeof(s_manage_message), s_file_removed ?
                  "文件已删除，进度清理失败，请重试" : s_delete_confirm ?
                  "文件删除失败，请检查存储后重试" : "进度清理失败，请重新尝试");
        return;
    }
    delete_retry_discard(s_managed.path);
    s_view = SHELF;
    int write = 0, leaf = ctx->leaf;
    for (int i = 0; i < s_count; ++i) {
        if (!strcmp(s_shelf[i].path, s_managed.path)) {
            if (s_file_removed) continue;
            s_shelf[i].has_progress = false; s_shelf[i].pct = 0; s_shelf[i].recent = 0;
        }
        s_shelf[write++] = s_shelf[i];
    }
    s_count = write;
    sort_shelf(ctx);
    ctx->leaf = leaf < leaves() ? leaf : leaves() - 1;
}

/* ---- 输入与生命周期 / Input and lifecycle ---- */
static void sensor_set(app_ctx_t* ctx, bool on) {
    memset(&s_shake, 0, sizeof(s_shake));
    if (!ctx->sensor_ready) return;
    if (on) {
        if (!s_sensor_saved) {
            s_sensor_config = *sc7a20h_get_config(ctx->acc);
            s_sensor_saved = true;
        }
        sc7a20h_sensor_config_t config = s_sensor_config;
        config.odr = SC7A20H_ODR_100;
        config.fs = SC7A20H_FS_2G;
        s_sensor_on = sc7a20h_apply_config(ctx->acc, &config) == ESP_OK &&
            sc7a20h_activity_config(ctx->acc, BOOK_SHAKE_THS_MG, BOOK_SHAKE_DUR) == ESP_OK;
        if (!s_sensor_on) {
            sc7a20h_aoi_cfg_t off = {0};
            sc7a20h_aoi_config(ctx->acc, SC7A20H_AOI2, &off);
            sc7a20h_apply_config(ctx->acc, &s_sensor_config);
            s_sensor_saved = false;
            read_pico_sensor_sleep(ctx->acc);
        }
    } else {
        sc7a20h_aoi_cfg_t off = {0};
        sc7a20h_aoi_config(ctx->acc, SC7A20H_AOI2, &off);
        if (s_sensor_saved) sc7a20h_apply_config(ctx->acc, &s_sensor_config);
        s_sensor_saved = false;
        read_pico_sensor_sleep(ctx->acc);
        s_sensor_on = false;
    }
}
static app_redraw_t turn_page(app_ctx_t* ctx, int dir) {
    if(ps_dual_enabled()){if(!s_dual_anchor.pending&&!s_dual_target&&!s_selection&&!s_toolbar&&!s_page_jump_open&&!s_image_confirm_open)ps_dual_turn(dir);return APP_REDRAW_NONE;}
    if (s_cover_open) return dir > 0 ? close_image() : APP_REDRAW_NONE;
    if (!s_text || s_clear_confirm || s_selection || s_scrubbing || s_progress_pending || s_page_jump_open) return APP_REDRAW_NONE;
    if (dir > 0 && s_page + 1 >= book_layout_page_count() && !book_layout_complete()) {
        lock_draw();
        invalidate_prep();
        bool ok = book_layout_extend(2);
        unlock_draw();
        if (!ok) {
            free_book(); s_view = SHELF; ctx->leaf = 0;
            copy_text(s_message, sizeof(s_message), "排版失败：内存不足或章节过长");
            return APP_REDRAW_PAGE;
        }
    }
    bool changed = true;
    if (dir > 0 && s_page + 1 < book_layout_page_count()) ++s_page;
    else if (dir < 0 && s_page) --s_page;
    else if (dir > 0 && s_chapter + 1 < book_chapter_count()) { save_progress(); changed = load_chapter(ctx, s_chapter + 1, 0, false); }
    else if (dir < 0 && s_chapter) { save_progress(); changed = load_chapter(ctx, s_chapter - 1, 0, true); }
    else if (dir < 0 && !s_chapter && !s_page) return open_cover() ? APP_REDRAW_PAGE : APP_REDRAW_NONE;
    else return APP_REDRAW_NONE;
    if (!changed) { s_view = s_text ? TOC : SHELF; ctx->leaf = s_text ? s_chapter / toc_rows() : 0; return APP_REDRAW_PAGE; }
    s_toolbar = false;
    s_last_turn_ms = ctx->now_ms;
    s_turn_anim_dir = dir;
#if APP_GC16_EVERY > 0
    s_full = (++s_turns % APP_GC16_EVERY) == 0;
#else
    s_full = false;
#endif
    s_turn_anim_pending = !s_full;
    if (s_unsaved < 8) ++s_unsaved;
    if (s_unsaved >= 8 && !s_save_failed) save_progress();
    ESP_LOGI(TAG, "turn chapter=%u page=%u/%u pct=%u", (unsigned)s_chapter, (unsigned)s_page + 1, (unsigned)book_layout_page_count(), percent(s_page));
    return paint_reading(ctx, MODE_GL16);
}
static app_redraw_t resize_text(app_ctx_t* ctx, int dir) {
    if (ps_dual_enabled()) {
        dl_status_t current; ps_dual_status(&current);
        int px = current.reading.font_px + dir * BOOK_PX_STEP;
        if (px < BOOK_PX_MIN || px > BOOK_PX_MAX) return APP_REDRAW_NONE;
        if (s_dual_anchor.pending || !ps_dual_resize((uint8_t)px)) { ps_toast("请等待双机就绪后调整字号"); return APP_REDRAW_NONE; }
        s_toolbar = false; return APP_REDRAW_PAGE;
    }
    int next = s_px + dir * BOOK_PX_STEP;
    if (next < BOOK_PX_MIN || next > BOOK_PX_MAX) return APP_REDRAW_NONE;
    page_jump_close(); page_index_reset();
    size_t off = book_layout_page_start_offset(s_page);
    save_progress();
    lock_draw();
    invalidate_prep();
    int old = s_px;
    if (!book_layout_build_blocks(s_text, s_text_len, s_blocks, s_block_count, body_rect(), next)) {
        bool restored = book_layout_build_blocks(s_text, s_text_len, s_blocks, s_block_count, body_rect(), old);
        unlock_draw();
        if (!restored) {
            free_book();
            s_view = SHELF;
            copy_text(s_message, sizeof(s_message), "排版失败，请重新扫描并打开图书");
            return APP_REDRAW_PAGE;
        }
        return APP_REDRAW_NONE;
    }
    s_px = next;
    s_layout_rect = body_rect();
    s_page = book_layout_page_for_offset(off);
    unlock_draw();
    app_settings_set_book_px(s_px);
    save_progress();
    s_size_settle_ms = ctx->now_ms + BOOK_SIZE_SETTLE_MS;
    return paint_reading(ctx, MODE_DU);
}
static void goto_font(app_ctx_t* ctx) {
    if(ps_dual_enabled()){ps_toast("双机阅读使用主机共享字体");return;}
    page_jump_close(); page_index_reset();
    s_selection = false;
    // 通过注册表查找，不引用另一个页面符号。/ Find via registry without referencing another page symbol.
    for (int i = 0; i < app_count(); ++i) {
        const app_desc_t* app = app_at(i);
        if (strstr(app->title, " Font")) {
            save_progress();
            if (s_product_session && s_path[0]) copy_text(s_requested_path, sizeof(s_requested_path), s_path);
            ctx->request_app = app;
            break;
        }
    }
}
static app_redraw_t product_exit(app_ctx_t* ctx) {
    save_progress();
    s_requested_path[0] = 0;
    s_open_path[0] = 0;
    s_open_presented = false;
    s_annotation_pending = s_selection = false;
    // 一次性回到书库根页：清掉阅读子页/历史，避免 ps_navigate 后
    // 又因旧 history 回到记录页或设置页，也避免连续两次整屏刷新。
    // Return to the library root in one step: clear child history first, so Back/home
    // cannot land on a stale records/settings page and the screen refreshes once.
    ps_switch(0);
    ctx->request_app = app_home_page();
    ps_global_book_return(false);
    return APP_REDRAW_NONE;
}
void app_book_global_prepare(void) {
    cancel_interaction();
    page_index_pause();
    lock_draw(); invalidate_prep(); unlock_draw();
    s_pressed_control = -1;
}
bool app_book_power_prepare(void) { app_book_global_prepare();save_progress();return !s_save_failed; }
bool app_book_global_is_open(void) { return s_product_session && s_text && !s_open_path[0]; }
bool app_book_global_has_selection(void) { return app_book_global_is_open() && s_selection && s_selection_end > s_selection_start; }
bool app_book_resume_last(void) {
    if (ps_dual_enabled()) {
        if (!ps_dual_resume()) return false;
        dl_status_t current; ps_dual_status(&current);
        return app_book_dual_apply(ps_dual_path(), ps_dual_font(),
            current.reading.spread + (ps_dual_role()==2?2:1), current.reading.font_px,
            current.reading.reflow_pending, current.reading.revision);
    }
    return s_last_product_path[0] && app_book_open_path(s_last_product_path);
}
app_redraw_t app_book_global_action(app_ctx_t* ctx, app_book_global_action_t action) {
    // 全局按下已调用prepare；它只取消接触，Back仍须消费尚在的最内层弹窗。
    // Global press already prepared the page; preparation cancels contacts so Back can still close the innermost dialog.
    if (action == BOOK_GLOBAL_BACK && s_image_confirm_open) { image_confirm_close(); return APP_REDRAW_PAGE; }
    if (action == BOOK_GLOBAL_BACK && s_page_jump_open) { page_jump_close(); return APP_REDRAW_PAGE; }
    app_book_global_prepare();
    if (s_image_confirm_open) image_confirm_close();
    if (action == BOOK_GLOBAL_TOC) return s_text ? book_open_toc(ctx) : APP_REDRAW_PAGE;
    page_jump_close();
    if (action == BOOK_GLOBAL_BACK) {
        if (s_selection) { s_selection = false; return APP_REDRAW_PAGE; }
        if (s_image_open) return close_image();
        if (s_view == TOC && s_text) { s_view = READING; s_toolbar = false; return APP_REDRAW_PAGE; }
        if (s_toolbar) { s_toolbar = false; return APP_REDRAW_PAGE; }
        return product_exit(ctx);
    }
    if (s_open_path[0] || !s_text) { ps_toast("图书尚未打开，请稍候或返回书架"); return APP_REDRAW_PAGE; }
    if (action == BOOK_GLOBAL_PAGE) {
        if (s_image_open) close_image();
        s_view = READING; s_toolbar = s_selection = false;
        return page_jump_open();
    }
    if (action == BOOK_GLOBAL_FAVORITE) {
        save_progress();
        if (ps_reader_file_favorite(s_path, true)) ps_toast(ps_reader_file_favorite(s_path, false) ? "已加入收藏" : "已取消收藏");
        return APP_REDRAW_PAGE;
    }
    if (action == BOOK_GLOBAL_HIGHLIGHT || action == BOOK_GLOBAL_NOTE) {
        if (!s_selection) { ps_toast("请先长按并选择文字"); return APP_REDRAW_PAGE; }
        return annotation_action(ctx, action == BOOK_GLOBAL_HIGHLIGHT ? 0 : 1);
    }
    if (action == BOOK_GLOBAL_RECORDS || action == BOOK_GLOBAL_HIGHLIGHTS || action == BOOK_GLOBAL_NOTES || action == BOOK_GLOBAL_BOOKMARKS) {
        save_progress();
        if (action == BOOK_GLOBAL_RECORDS) ps_reader_real_records(s_path, s_file_size);
        else ps_reader_real_records_tab(s_path, s_file_size, action == BOOK_GLOBAL_HIGHLIGHTS ? 1 : action == BOOK_GLOBAL_BOOKMARKS ? 2 : 0);
        ctx->request_app = app_home_page();
        return APP_REDRAW_PAGE;
    }
    if (action == BOOK_GLOBAL_SELECT) {
        if (s_image_open) close_image();
        s_toolbar = false; s_selection = false; ps_toast("长按文字并拖动选择");
        return APP_REDRAW_PAGE;
    }
    if (action == BOOK_GLOBAL_BOOKMARK) return annotation_action(ctx, 2);
    if (action == BOOK_GLOBAL_FONT) { goto_font(ctx); return APP_REDRAW_NONE; }
    if (s_image_open) close_image();
    s_selection = false;
    s_view = READING;
    s_toolbar = action == BOOK_GLOBAL_TOOLS;
    return APP_REDRAW_PAGE;
}
static app_redraw_t manage_action(app_ctx_t* ctx, uint16_t x, uint16_t y) {
    int count = s_clear_confirm || s_file_removed ? 2 : 3;
    for (int i = 0; i < count; ++i) if (ui_rect_hit(manage_rect(i, count), x, y)) {
        if (s_clear_confirm) { if (!i) s_clear_confirm = false; else manage_apply(ctx); }
        else if (!i) s_view = SHELF;
        else if (s_file_removed) manage_apply(ctx);
        else { s_clear_confirm = true; s_delete_confirm = i == 2; s_manage_message[0] = 0; }
        break;
    }
    return APP_REDRAW_PAGE;
}
// 成功项退出选择，已删文件的失败项仅重试元数据。/ Deselect successes; removed-file failures retry metadata only.
static void batch_apply(app_ctx_t* ctx) {
    unsigned done = 0, failed = 0;
    int write = 0;
    for (int i = 0; i < s_count; ++i) {
        shelf_entry_t item = s_shelf[i];
        bool discard = false;
        if (item.selected) {
            if (!strcmp(s_path, item.path)) free_book();
            esp_err_t err;
            if (s_batch_delete && !item.removed) {
                delete_retry_t* retry = delete_retry_reserve(&item);
                if (!retry) { ++failed; s_shelf[write++] = item; continue; }
                bool removed = false;
                err = book_store_delete(item.path, &removed);
                if (removed) {
                    retry->entry.removed = true;
                    item.removed = true; pending_discard(item.path);
                    book_store_notify_changed(); s_store_revision = book_store_revision();
                } else delete_retry_discard(item.path);
            } else { pending_discard(item.path); err = book_progress_forget(item.path); }
            if (err == ESP_OK) {
                delete_retry_discard(item.path);
                ++done; item.selected = false; item.has_progress = false; item.pct = 0; item.recent = 0;
                discard = item.removed;
            } else ++failed;
        }
        if (!discard) s_shelf[write++] = item;
    }
    s_count = write;
    refresh_capacity();
    int leaf = ctx->leaf;
    sort_shelf(ctx);
    ctx->leaf = leaf < leaves() ? leaf : leaves() - 1;
    s_batch_confirm = false;
    snprintf(s_batch_message, sizeof(s_batch_message), "成功 %u 本，失败 %u 本%s", done, failed, failed ? "；所选可重试" : "");
}
static app_redraw_t batch_action(app_ctx_t* ctx, uint16_t x, uint16_t y) {
    if (s_batch_confirm) {
        if (ui_rect_hit(ui_row_rect(0, 2, 620, UI_BTN_H), x, y)) s_batch_confirm = false;
        else if (ui_rect_hit(ui_row_rect(1, 2, 620, UI_BTN_H), x, y)) batch_apply(ctx);
        return APP_REDRAW_PAGE;
    }
    for (int i = 0; i < 5; ++i) if (ui_rect_hit(batch_rect(i), x, y)) {
        if (!i) select_page(ctx->leaf);
        else if (i == 1) clear_selection();
        else if (i == 2) { clear_selection(); s_batch_message[0] = 0; scan_shelf(ctx); }
        else if (selected_count()) { s_batch_delete = i == 3; s_batch_confirm = true; }
        return APP_REDRAW_PAGE;
    }
    return APP_REDRAW_NONE;
}
static app_redraw_t action_at(app_ctx_t* ctx, uint16_t x, uint16_t y) {
    if (s_page_jump_open) { EpdRect rect; return page_jump_action(ctx, page_jump_control_at(x, y, &rect)); }
    if (s_product_session && s_view == SHELF && !s_text)
        return ui_rect_hit(progress_rect(), x, y) ? product_exit(ctx) : APP_REDRAW_NONE;
    if (s_resume_path[0]) {
        for (int i = 0; i < 2; ++i) if (ui_rect_hit(ui_row_rect(i, 2, 620, UI_BTN_H), x, y)) {
            return resume_choice(ctx, i != 0);
        }
        return APP_REDRAW_NONE;
    }
    if (s_view == MANAGE) return manage_action(ctx, x, y);
    if (s_view == SEARCH) {
        for (int i = 0; i < 45; ++i) if (ui_rect_hit(search_rect(i), x, y)) {
            app_redraw_t result = search_action(ctx, i);
            if (result == APP_REDRAW_AREA) { render(ctx, ctx->fb); s_area = (EpdRect){UI_MARGIN, 190, ui_content_width(), 850}; s_mode = MODE_DU; }
            return result;
        }
        return APP_REDRAW_NONE;
    }
    if (s_view == BULK) { app_redraw_t result = batch_action(ctx, x, y); if (result != APP_REDRAW_NONE) return result; }
    if (s_view == READING) {
        if (s_image_open) return close_image();
        if (s_product_session && y < 84) {
            if (x < 84) return book_open_toc(ctx);
        }
        if (s_jump_back_valid && ui_rect_hit(progress_back_rect(), x, y)) return progress_commit(ctx, true);
        if (s_selection) return selection_touch(ctx, x, y);
        if (ttf_font_is_builtin() && y >= (s_product_session ? 84 : 0) && y < (s_product_session ? 212 : 140)) { goto_font(ctx); return APP_REDRAW_NONE; }
        if (s_toolbar) {
            for (int i = 0; i < BOOK_TOOL_COUNT; ++i) if (ui_rect_hit(tool_rect(i), x, y)) {
                invalidate_prep();
                if (i == 0) return book_open_toc(ctx);
                else if (i == 1 || i == 2) return resize_text(ctx, i == 1 ? -1 : 1);
                else if (i == 3) { s_shake_enabled = !s_shake_enabled; app_settings_set_book_shake(s_shake_enabled); sensor_set(ctx, s_shake_enabled); }
                else if (i == 4) return APP_REDRAW_FULL;
                else if (i == 5) {
                    if (s_product_session) return product_exit(ctx);
                    save_progress(); free_book(); s_view = SHELF; scan_shelf(ctx);
                }
                else if (i == 6) { s_toolbar = false; ps_toast("长按文字并拖动选择"); }
                else if (i == 7) return annotation_action(ctx, 2);
                else if (i == 8) return app_book_global_action(ctx, BOOK_GLOBAL_RECORDS);
                return APP_REDRAW_PAGE;
            }
            return APP_REDRAW_NONE;
        }
        if (s_product_session && y < 84) return APP_REDRAW_NONE;
        if (!s_toolbar) {
            lock_draw();
            size_t block = book_layout_image_at(s_page, body_rect(), x, y, NULL);
            size_t at = block == SIZE_MAX ? book_layout_text_at(s_page, body_rect(), x, y) : SIZE_MAX;
            unlock_draw();
            if (block != SIZE_MAX) return image_confirm_request(block);
            if (at != SIZE_MAX && at <= UINT32_MAX && ps_reader_real_open_at(s_path,s_file_size,(uint32_t)s_chapter,(uint32_t)at)) {
                invalidate_prep();return APP_REDRAW_PAGE;
            }
        }
        if (x < reader_width() * 3 / 10) return turn_page(ctx, -1);
        if (x >= reader_width() * 7 / 10) return turn_page(ctx, 1);
        s_toolbar = !s_toolbar;
        invalidate_prep();
        return APP_REDRAW_PAGE;
    }
    if (s_view == SHELF || s_view == BULK) {
        if (ui_rect_hit(ui_row_rect(0, 3, 176, 72), x, y)) {
            s_filter = (s_filter + 1) % 3; clear_selection(); s_batch_message[0] = 0;
            sort_shelf(ctx);
            return APP_REDRAW_PAGE;
        }
        if (ui_rect_hit(ui_row_rect(1, 3, 176, 72), x, y)) {
            s_recent_sort = !s_recent_sort;
            sort_shelf(ctx);
            return APP_REDRAW_PAGE;
        }
        if (ui_rect_hit(ui_row_rect(2, 3, 176, 72), x, y)) { search_begin(); return APP_REDRAW_PAGE; }
        if (s_view == SHELF && s_save_failed && y >= UI_CONTENT_BOTTOM - UI_PX_CAPTION && y < UI_CONTENT_BOTTOM) {
            retry_progress();
            return APP_REDRAW_PAGE;
        }
    }
    int nav = -1;
    for (int i = 0; i < 3; ++i) if (ui_rect_hit(book_nav_rect(i, 3), x, y)) nav = i;
    if (nav == 0 || nav == 2) {
        int next = ctx->leaf + (nav == 0 ? -1 : 1);
        if (next >= 0 && next < leaves()) ctx->leaf = next;
        return APP_REDRAW_PAGE;
    }
    if (nav == 1) {
        s_message[0] = 0;
        if (s_view == TOC && s_text) { s_view = READING; s_toolbar = false; }
        else { s_view = s_view == BULK ? SHELF : BULK; s_batch_confirm = false; }
        return APP_REDRAW_PAGE;
    }
    if (s_message[0] && !((s_view == SHELF || s_view == BULK) && s_visible_count)) return APP_REDRAW_NONE;
    int rows = s_view == TOC ? toc_rows() : BOOK_ROWS;
    for (int row = 0; row < rows; ++row) if (ui_rect_hit(row_rect(row, s_view == TOC), x, y)) {
        int i = ctx->leaf * rows + row;
        if (s_view == BULK && i < s_visible_count) toggle_selection(i);
        else if (s_view == SHELF && i < s_visible_count) {
            if (s_shelf[i].removed) {
                s_managed = s_shelf[i]; s_view = MANAGE; s_file_removed = s_delete_confirm = true; s_clear_confirm = false;
                copy_text(s_manage_message, sizeof(s_manage_message), "文件已删除，进度清理失败，请重试");
            } else open_book(ctx, s_shelf[i].path);
        }
        else if (s_view == TOC && i < book_chapter_count()) {
            if (ps_dual_enabled()) { dual_anchor_start(i, 0, false, 0); return APP_REDRAW_PAGE; }
            save_progress();
            if (load_chapter(ctx, i, 0, false)) { s_view = READING; s_toolbar = false; save_progress(); }
        }
        return APP_REDRAW_PAGE;
    }
    return APP_REDRAW_NONE;
}
static bool request_path_valid(const char* path) {
    if (!path || !path[0] || strlen(path) >= BOOK_STORE_PATH_MAX || path[0] != '/') return false;
    const char* ext = strrchr(path, '.');
    if (!ext || (strcasecmp(ext, ".txt") && strcasecmp(ext, ".epub"))) return false;
    const char* segment = path + 1;
    while (*segment) {
        const char* slash = strchr(segment, '/');
        size_t len = slash ? (size_t)(slash - segment) : strlen(segment);
        if (!len || (len == 1 && segment[0] == '.') || (len == 2 && segment[0] == '.' && segment[1] == '.')) return false;
        if (!slash) break;
        segment = slash + 1;
    }
    if (!strncmp(path, "/sdcard/", 8) && !strchr(path + 8, '/')) return true;
    book_store_root_t roots[2];
    int count = 0;
    book_store_roots(roots, &count);
    for (int i = 0; i < count; ++i) {
        size_t len = strlen(roots[i].path);
        if (len && !strncmp(path, roots[i].path, len) && path[len] == '/' && path[len + 1]) return true;
    }
    return false;
}
bool app_book_open_path(const char* path) {
    if (!request_path_valid(path)) return false;
    if(ps_dual_enabled()&&!s_dual_applying){ps_dual_open(path);return false;}
    copy_text(s_requested_path, sizeof(s_requested_path), path);
    s_annotation_pending = false;
    return true;
}
bool app_book_open_annotation(const char* path, uint64_t file_bytes, uint32_t chapter, uint32_t start, uint32_t end) {
    if (end < start || !file_bytes || file_bytes > UINT32_MAX || !request_path_valid(path)) return false;
    if (ps_dual_enabled()) {
        dl_status_t current; ps_dual_status(&current);
        if (!current.connected || !current.synced || current.reading.reflow_pending) return false;
        s_dual_px = current.reading.font_px; s_dual_reflow = s_dual_target = 0;
        s_dual_annotation = s_dual_wait = true;
        copy_text(s_dual_font, sizeof(s_dual_font), ps_dual_font());
        s_dual_applying = true; bool ok = app_book_open_path(path); s_dual_applying = false;
        if (!ok) { s_dual_annotation = s_dual_wait = false; return false; }
    } else if (!app_book_open_path(path)) return false;
    memset(&s_annotation_target, 0, sizeof(s_annotation_target));
    copy_text(s_annotation_target.path, sizeof(s_annotation_target.path), s_requested_path);
    s_annotation_target.file_bytes = file_bytes; s_annotation_target.chapter = chapter;
    s_annotation_target.start = start; s_annotation_target.end = end;
    s_annotation_pending = true;
    return true;
}
static void on_enter(app_ctx_t* ctx) {
    page_jump_close(); page_index_reset(); s_modal_key_cancelled = false;
    char requested[BOOK_STORE_PATH_MAX];
    copy_text(requested, sizeof(requested), s_requested_path);
    s_requested_path[0] = 0;
    s_product_session = requested[0] != 0;
    // 产品阅读继承系统方向，工程书架仍使用原竖屏布局。
    // Product reading inherits system orientation; the engineering shelf retains portrait geometry.
    if (!s_product_session) epd_set_rotation(EPD_ROT_INVERTED_PORTRAIT);
    ps_book_rotation_enter(ctx);
    s_book_rotation = epd_get_rotation();
    copy_text(s_open_path, sizeof(s_open_path), requested);
    s_open_presented = false;
    s_selection = s_selection_menu = s_selection_drag = s_scrubbing = s_jump_back_valid = false;
    s_resume_path[0] = 0;
    ensure_prep();
    s_view = SHELF;
    ctx->leaf = 0;
    s_count = 0;
    s_px = app_settings_book_px();
    s_scan_pending = !s_product_session;
    s_resume_pending = !s_product_session;
    // 传书页已停止并 join HTTP，安全注销且只丢弃对应路径的旧进度。
    // Transfer has stopped and joined HTTP; safely unregister only invalidated paths.
    pending_drop_invalidated();
    s_store_revision = book_store_revision();
    s_toolbar = s_clear_confirm = s_batch_confirm = false;
    s_pressed_control = -1;
    s_du_count = 0;
    s_full = false;
    s_size_settle_ms = 0;
    s_poll_ms = 0;
    copy_text(s_storage, sizeof(s_storage), "正在检测存储…");
    copy_text(s_message, sizeof(s_message), "正在扫描图书…");
    read_pico_sd_start_probe();
    s_shake_enabled = app_settings_book_shake();
    if (s_shake_enabled) sensor_set(ctx, true);
    if (s_product_session) {
        s_message[0] = 0;
    }
}
static void book_on_exit(app_ctx_t* ctx) {
    if(s_dual_reflow) (void)ps_dual_reflow_failed(s_dual_reflow);
    s_dual_reflow=0; s_dual_anchor.pending=false;
    if(!s_annotation_pending) s_dual_annotation=s_dual_wait=false;
    s_open_path[0] = 0;
    s_open_presented = false;
    s_dual_present_pending = 0;
    s_resume_path[0] = 0;
    s_pressed_control = -1;
    save_progress();
    sensor_set(ctx, false);
    free_book();
    free(s_shelf);
    s_shelf = NULL;
    s_shelf_capacity = 0;
    s_count = s_visible_count = 0;
    if (s_prep_task) { vTaskDelete(s_prep_task); s_prep_task = NULL; }
    free(s_next_fb);
    s_next_fb = NULL;
    turn_anim_free();
    if (s_prep_done) { vSemaphoreDelete(s_prep_done); s_prep_done = NULL; }
    if (s_draw_lock) { vSemaphoreDelete(s_draw_lock); s_draw_lock = NULL; }
}
// 先停止使用旧卡句柄与字体预渲染，主循环随后切换内置字体。
// Stop old-card handles and font preparation before the loop switches to the builtin font.
static void book_on_media_lost(app_ctx_t* ctx) {
    if(ps_dual_enabled()) (void)dual_read_failed();
    page_jump_close(); page_index_reset();
    s_open_path[0] = 0;
    s_open_presented = false;
    s_annotation_pending = s_selection = false;
    s_resume_path[0] = 0;
    lock_draw();
    save_progress();
    free_book();
    unlock_draw();
    s_view = SHELF;
    s_count = s_visible_count = 0;
    ctx->leaf = 0;
    s_toolbar = s_clear_confirm = s_batch_confirm = false;
    s_pressed_control = -1;
    s_scan_pending = !s_product_session;
    s_resume_pending = false;
    s_du_count = 0;
    s_size_settle_ms = 0;
    copy_text(s_storage, sizeof(s_storage), "TF 卡已移除");
    copy_text(s_message, sizeof(s_message), s_product_session ? "TF 卡已移除，请返回书架后重新打开" : "已停止阅读，正在检查内置图书");
    if (s_product_session) ps_media_lost();
}
// 重挂后只刷新目录与系统资源；旧文件句柄已失效，不能自动续读。
// After remount, refresh catalogs and system resources only; invalidated file handles cannot resume reading.
static void book_on_media_restored(app_ctx_t* ctx) {
    (void)ctx;
    if (s_product_session) ps_media_restored();
    else s_scan_pending = true;
    s_storage[0] = 0;
    copy_text(s_message, sizeof(s_message), s_product_session ? "TF 卡已恢复，请返回书架后重新打开" : "正在扫描图书…");
}
// 控件编号仅用于保持按下与抬起命中同一个目标。/ IDs pair a press with release on the same control.
static int control_at(app_ctx_t* ctx, uint16_t x, uint16_t y, EpdRect* rect) {
    if (s_image_confirm_open) return image_confirm_control_at(x, y, rect);
    if (s_page_jump_open) return page_jump_control_at(x, y, rect);
    if (s_product_session && s_view == SHELF && !s_text) {
        *rect = progress_rect();
        return ui_rect_hit(*rect, x, y) ? 800 : -1;
    }
    if (s_resume_path[0]) {
        for (int i = 0; i < 2; ++i) {
            *rect = ui_row_rect(i, 2, 620, UI_BTN_H);
            if (ui_rect_hit(*rect, x, y)) return 700 + i;
        }
        return -1;
    }
    if (s_view == SEARCH) {
        for (int i = 0; i < 45; ++i) { *rect = search_rect(i); if (ui_rect_hit(*rect, x, y)) return 500 + i; }
        return -1;
    }
    if (s_view == BULK && s_batch_confirm) {
        for (int i = 0; i < 2; ++i) { *rect = ui_row_rect(i, 2, 620, UI_BTN_H); if (ui_rect_hit(*rect, x, y)) return 600 + i; }
        return -1;
    }
    if (s_view == BULK) for (int i = 0; i < 5; ++i) { *rect = batch_rect(i); if (ui_rect_hit(*rect, x, y)) return 610 + i; }
    if (s_view == MANAGE) {
        int count = s_clear_confirm || s_file_removed ? 2 : 3;
        for (int i = 0; i < count; ++i) {
            *rect = manage_rect(i, count);
            if (ui_rect_hit(*rect, x, y)) return s_clear_confirm ? 300 + i : s_file_removed && i == 1 ? 403 : 400 + i;
        }
        return -1;
    }
    if (s_clear_confirm) {
        for (int i = 0; i < 2; ++i) {
            *rect = ui_row_rect(i, 2, 610, UI_BTN_H);
            if (ui_rect_hit(*rect, x, y)) return 300 + i;
        }
        return -1;
    }
    if (s_view == READING) {
        if (s_product_session) {
            *rect = (EpdRect){0, 0, 84, 84}; if (ui_rect_hit(*rect, x, y)) return 951;
        }
        if (s_jump_back_valid) { *rect = progress_back_rect(); if (ui_rect_hit(*rect, x, y)) return 950; }
        if (s_selection) return selection_control_at(x, y, rect);
        if (s_toolbar) for (int i = 0; i < BOOK_TOOL_COUNT; ++i) {
            *rect = tool_rect(i);
            if (ui_rect_hit(*rect, x, y)) return 200 + i;
        }
        if (!s_toolbar) {
            lock_draw();
            size_t block = book_layout_image_at(s_page, body_rect(), x, y, rect);
            unlock_draw();
            if (block != SIZE_MAX) return 1000 + (int)block;
        }
        return -1;
    }
    for (int i = 0; i < 3; ++i) {
        *rect = book_nav_rect(i, 3);
        if (ui_rect_hit(*rect, x, y)) return 100 + i;
    }
    if (s_view == SHELF || s_view == BULK) {
        for (int i = 0; i < 3; ++i) {
            *rect = ui_row_rect(i, 3, 176, 72);
            if (ui_rect_hit(*rect, x, y)) return 110 + i;
        }
        *rect = (EpdRect){UI_MARGIN, UI_CONTENT_BOTTOM - UI_PX_CAPTION, ui_content_width(), UI_PX_CAPTION};
        if (s_view == SHELF && s_save_failed && ui_rect_hit(*rect, x, y)) return 113;
    }
    if (s_message[0] && !((s_view == SHELF || s_view == BULK) && s_visible_count)) return -1;
    int rows = s_view == TOC ? toc_rows() : BOOK_ROWS;
    int count = s_view == TOC ? (int)book_chapter_count() : s_visible_count;
    for (int row = 0; row < rows && ctx->leaf * rows + row < count; ++row) {
        *rect = row_rect(row, s_view == TOC);
        if (ui_rect_hit(*rect, x, y)) return row;
    }
    return -1;
}
static app_redraw_t paint_control(app_ctx_t* ctx, EpdRect rect) {
    render(ctx, ctx->fb);
    s_area = rect;
    s_mode = MODE_DU;
    return APP_REDRAW_AREA;
}
// 控制器坐标固定为倒置竖屏；只在页内转换，不改变主循环和实体键的原始坐标契约。
// Controller coordinates stay in inverted portrait; map only inside this page, preserving loop/key raw coordinates.
static void book_touch_point(enum EpdRotation rotation, uint16_t rawx, uint16_t rawy, uint16_t* x, uint16_t* y) {
    int a = rawx < UI_LOCK_WIDTH ? rawx : UI_LOCK_WIDTH - 1;
    int b = rawy < UI_LOCK_HEIGHT ? rawy : UI_LOCK_HEIGHT - 1;
    switch (rotation) {
        case EPD_ROT_LANDSCAPE: *x = b; *y = UI_LOCK_WIDTH - 1 - a; break;
        case EPD_ROT_PORTRAIT: *x = UI_LOCK_WIDTH - 1 - a; *y = UI_LOCK_HEIGHT - 1 - b; break;
        case EPD_ROT_INVERTED_LANDSCAPE: *x = UI_LOCK_HEIGHT - 1 - b; *y = a; break;
        default: *x = a; *y = b; break;
    }
}
static ui_gesture_event_t book_touch_event(const ui_gesture_event_t* raw) {
    ui_gesture_event_t mapped = *raw;
    enum EpdRotation rotation = epd_get_rotation();
    book_touch_point(rotation, raw->x0, raw->y0, &mapped.x0, &mapped.y0);
    book_touch_point(rotation, raw->x, raw->y, &mapped.x, &mapped.y);
    if (mapped.type == UI_GESTURE_SWIPE_L || mapped.type == UI_GESTURE_SWIPE_R ||
        mapped.type == UI_GESTURE_SWIPE_U || mapped.type == UI_GESTURE_SWIPE_D) {
        int dx = (int)mapped.x - mapped.x0, dy = (int)mapped.y - mapped.y0;
        if (abs(dx) > abs(dy)) mapped.type = dx < 0 ? UI_GESTURE_SWIPE_L : UI_GESTURE_SWIPE_R;
        else mapped.type = dy < 0 ? UI_GESTURE_SWIPE_U : UI_GESTURE_SWIPE_D;
    }
    return mapped;
}
// 浮层从按下起隔离外部接触；抬起只关闭，不把同一次轻点交给正文。
// Isolate outside contacts from press onward; a terminal tap dismisses without reaching prose.
static int book_modal_rect(EpdRect* rect) {
    if (s_page_jump_open) { *rect = page_jump_rect(); return 1; }
    if (s_image_confirm_open) { *rect = image_confirm_rect(); return 2; }
    if (s_view == READING && s_selection && s_selection_menu && !s_selection_drag) { *rect = selection_panel(); return 3; }
    if (s_view == READING && s_toolbar) { *rect = toolbar_panel(); return 4; }
    if (s_resume_path[0]) { *rect = (EpdRect){UI_MARGIN - 12, 380, ui_content_width() + 24, 350}; return 5; }
    if (s_view == MANAGE) { *rect = manage_panel(); return 6; }
    if (s_view == BULK && s_batch_confirm) { *rect = (EpdRect){UI_MARGIN - 12, 410, ui_content_width() + 24, 330}; return 7; }
    return 0;
}
static app_redraw_t gesture_event(app_ctx_t* ctx, const ui_gesture_event_t* raw) {
    ui_gesture_event_t mapped = book_touch_event(raw);
    const ui_gesture_event_t* ev = &mapped;
    if(s_dual_wait || s_dual_anchor.pending) {
        if(s_dual_wait && ev->type==UI_GESTURE_TAP && ui_rect_hit(pending_back_rect(),ev->x0,ev->y0) && ui_rect_hit(pending_back_rect(),ev->x,ev->y)) {
            dual_read_cancel(); return product_exit(ctx);
        }
        return APP_REDRAW_NONE;
    }
    if (ev->type == UI_GESTURE_CANCEL) {
        bool active = s_scrubbing || s_selection_drag;
        bool progress = s_scrubbing;
        cancel_interaction();
        bool decorated = s_pressed_control >= 0; s_pressed_control = -1;
        if (active) return progress ? paint_progress(ctx, MODE_GL16) : APP_REDRAW_PAGE;
        return decorated ? (s_page_jump_open ? page_jump_feedback(ctx, false) : APP_REDRAW_PAGE) : APP_REDRAW_NONE;
    }
    if (ev->type == UI_GESTURE_PRESS && !s_product_session && ui_menu_handle_hit_test(ev->x0, ev->y0) &&
        !(s_product_session && s_view == READING && s_text && ui_rect_hit(progress_contact_rect(), ev->x0, ev->y0))) {
        cancel_interaction(); s_pressed_control = -1;
        if (s_product_session) return product_exit(ctx);
        ctx->request_menu = true;
        return APP_REDRAW_NONE;
    }
    EpdRect modal;
    int modal_kind = book_modal_rect(&modal);
    bool handle = modal_kind == 3 &&
        ((s_handle_start_visible && handle_hit(s_handle_start, false, ev->x0, ev->y0)) ||
         (s_handle_end_visible && handle_hit(s_handle_end, true, ev->x0, ev->y0)));
    if (modal_kind && !handle && !ui_rect_hit(modal, ev->x0, ev->y0)) {
        s_pressed_control = -1;
        if (ev->type != UI_GESTURE_TAP || ui_rect_hit(modal, ev->x, ev->y)) return APP_REDRAW_NONE;
        if (modal_kind == 1) page_jump_close();
        else if (modal_kind == 2) image_confirm_close();
        else if (modal_kind == 3) s_selection = s_selection_menu = false;
        else if (modal_kind == 4) s_toolbar = false;
        else if (modal_kind == 5) s_resume_path[0] = 0;
        else if (modal_kind == 6) { if (s_clear_confirm) s_clear_confirm = false; else s_view = SHELF; }
        else s_batch_confirm = false;
        invalidate_prep(); return APP_REDRAW_PAGE;
    }
    if (s_page_jump_open) {
        EpdRect start_rect = {0}, end_rect = {0};
        int start = page_jump_control_at(ev->x0, ev->y0, &start_rect);
        int end = page_jump_control_at(ev->x, ev->y, &end_rect);
        if (ev->type == UI_GESTURE_PRESS) {
            s_pressed_control = start;
            return start >= 0 ? paint_control(ctx, start_rect) : APP_REDRAW_NONE;
        }
        if (ev->type == UI_GESTURE_MOVE) return APP_REDRAW_NONE;
        bool decorated = s_pressed_control >= 0; s_pressed_control = -1;
        if (ev->type == UI_GESTURE_TAP && start >= 0 && start == end) return page_jump_action(ctx, start);
        return decorated ? page_jump_feedback(ctx, false) : APP_REDRAW_NONE;
    }
    if (s_image_confirm_open) {
        EpdRect start_rect = {0}, end_rect = {0};
        int start = image_confirm_control_at(ev->x0, ev->y0, &start_rect);
        int end = image_confirm_control_at(ev->x, ev->y, &end_rect);
        if (ev->type == UI_GESTURE_PRESS) {
            s_pressed_control = start;
            return start >= 0 ? paint_control(ctx, start_rect) : APP_REDRAW_NONE;
        }
        if (ev->type == UI_GESTURE_MOVE) return APP_REDRAW_NONE;
        // 打开弹窗的旧接触不能确认；只有在同一按钮开始的新接触可提交。
        // The opening contact cannot confirm; only a fresh press starting on the same button may commit.
        int pressed = s_pressed_control; s_pressed_control = -1;
        if (ev->type == UI_GESTURE_TAP && start >= 0 && start == end && pressed == start)
            return image_confirm_action(ctx, start);
        return pressed >= 0 ? paint_control(ctx, start_rect) : APP_REDRAW_NONE;
    }
    if (s_image_open) {
        s_pressed_control = -1;
        return ev->type == UI_GESTURE_TAP || (s_cover_open && ev->type == UI_GESTURE_SWIPE_L) ? close_image() : APP_REDRAW_NONE;
    }
    if (s_progress_pending) {
        if (ev->type == UI_GESTURE_MOVE) return progress_contact_move(ctx, ev);
        if (ev->type == UI_GESTURE_LONG_PRESS && !s_progress_dragged) {
            // 识别器在长按时认领；此处清待定接触，后续抬手不能打开数字弹窗。
            // The recognizer captures a hold; clear the pending contact so its release cannot open numeric entry.
            s_progress_pending = s_scrubbing = false;
            s_selection = s_selection_menu = false; s_toolbar = true;
            invalidate_prep(); return APP_REDRAW_PAGE;
        }
        if (ev->type == UI_GESTURE_TAP || ev->type == UI_GESTURE_RELEASE ||
            ev->type == UI_GESTURE_SWIPE_L || ev->type == UI_GESTURE_SWIPE_R ||
            ev->type == UI_GESTURE_SWIPE_U || ev->type == UI_GESTURE_SWIPE_D)
            return progress_contact_release(ctx, ev);
        return APP_REDRAW_NONE;
    }
    if (s_selection_drag) {
        if (ev->type == UI_GESTURE_MOVE)
            return selection_drag_move(ev->x, ev->y) ? paint_reading(ctx, MODE_DU) : APP_REDRAW_NONE;
        if (ev->type == UI_GESTURE_RELEASE) {
            selection_drag_move(ev->x, ev->y);
            s_selection_drag = false; s_selection_menu = true; selection_geometry();
            return APP_REDRAW_PAGE;
        }
        return APP_REDRAW_NONE;
    }
    if (ev->type == UI_GESTURE_PRESS && s_view == READING && s_text && !s_clear_confirm) {
        bool left = s_selection && s_handle_start_visible && handle_hit(s_handle_start, false, ev->x, ev->y);
        bool right = s_selection && s_handle_end_visible && handle_hit(s_handle_end, true, ev->x, ev->y);
        if (left || right) {
            // 单字两端命中区可能重叠，选择离指尖更近的圆柄。
            // Single-character handle hit areas can overlap; choose the closer round handle.
            if (left && right) {
                int a = abs((int)ev->x - s_handle_start.x);
                int b = abs((int)ev->x - s_handle_end.x - s_handle_end.width);
                right = b < a;
            }
            EpdRect glyph = right ? s_handle_end : s_handle_start;
            selection_drag_start(right ? s_selection_start : text_previous(s_selection_end), true);
            s_selection_edge = right ? 1 : 0;
            s_drag_dx = (int)ev->x - glyph.x - glyph.width / 2;
            s_drag_dy = (int)ev->y - glyph.y - glyph.height / 2;
            ctx->capture_gesture = true; s_pressed_control = -1;
            return paint_reading(ctx, MODE_DU);
        }
        EpdRect existing_rect;
        if (ui_rect_hit(progress_contact_rect(), ev->x, ev->y) && control_at(ctx, ev->x, ev->y, &existing_rect) < 0) {
            s_progress_pending = true; s_progress_dragged = s_scrubbing = false; s_pressed_control = -1;
            // 先等轻点或长按；拖动认领延迟到超过容差，静止时才能收到真实LONG_PRESS。
            // Wait for tap or hold; defer capture until drag slop so a stationary contact can emit real LONG_PRESS.
            invalidate_prep();
            return APP_REDRAW_NONE;
        }
    }
    // 未认领的MOVE不清按下态，也不抢走普通左右滑翻页。
    // Uncaptured MOVE preserves press decoration and ordinary horizontal swipe paging.
    if (ev->type == UI_GESTURE_MOVE || ev->type == UI_GESTURE_RELEASE) return APP_REDRAW_NONE;
    EpdRect start_rect = {0}, end_rect = {0};
    int start = control_at(ctx, ev->x0, ev->y0, &start_rect);
    int end = control_at(ctx, ev->x, ev->y, &end_rect);
    if (ev->type == UI_GESTURE_PRESS) {
        if (start >= 1000) { s_pressed_control = -1; return APP_REDRAW_NONE; }
        s_pressed_control = start;
        return start >= 0 ? paint_control(ctx, start_rect) : APP_REDRAW_NONE;
    }
    bool decorated = s_pressed_control >= 0;
    s_pressed_control = -1;
    if (ev->type == UI_GESTURE_LONG_PRESS && start == end) {
        if (s_view == SHELF && start >= 0 && start < BOOK_ROWS && !s_clear_confirm) {
            s_managed = s_shelf[ctx->leaf * BOOK_ROWS + start]; s_view = MANAGE;
            s_clear_confirm = false; s_delete_confirm = s_file_removed = s_managed.removed;
            copy_text(s_manage_message, sizeof(s_manage_message), s_file_removed ? "文件已删除，进度清理失败，请重试" : "");
            return APP_REDRAW_PAGE;
        }
        if (s_view == READING && !s_toolbar && !s_clear_confirm && start < 0 && ui_rect_hit(body_rect(), ev->x0, ev->y0)) {
            lock_draw(); size_t at = book_layout_text_at(s_page, body_rect(), ev->x0, ev->y0); unlock_draw();
            bool previous = s_selection; size_t before_start = s_selection_start, before_end = s_selection_end;
            if (at != SIZE_MAX && select_character(at)) {
                selection_drag_start(at, previous);
                s_drag_before_start = before_start; s_drag_before_end = before_end;
                ctx->capture_gesture = true;
                return paint_reading(ctx, MODE_DU);
            }
        }
    }
    if (ev->type == UI_GESTURE_TAP && !s_scan_pending) {
        if (start >= 0 && start == end) {
            app_redraw_t result = action_at(ctx, ev->x0, ev->y0);
            return result != APP_REDRAW_NONE ? result : paint_control(ctx, start_rect);
        }
        if (start < 0 && end < 0 && s_view == READING && !s_clear_confirm) return action_at(ctx, ev->x0, ev->y0);
    }
    if (!s_clear_confirm && !s_scan_pending && !s_resume_path[0]) {
        if (s_view == READING && !s_toolbar && !s_selection && ui_rect_hit(body_rect(), ev->x0, ev->y0)) {
            if (ev->type == UI_GESTURE_SWIPE_L) return turn_page(ctx, 1);
            if (ev->type == UI_GESTURE_SWIPE_R) return turn_page(ctx, -1);
        } else if ((s_view == SHELF || s_view == TOC || (s_view == BULK && !s_batch_confirm)) &&
                   (ev->type == UI_GESTURE_SWIPE_U || ev->type == UI_GESTURE_SWIPE_D)) {
            int next = ctx->leaf + (ev->type == UI_GESTURE_SWIPE_U ? 1 : -1);
            if (next >= 0 && next < leaves()) { ctx->leaf = next; return APP_REDRAW_PAGE; }
        }
    }
    return decorated ? paint_control(ctx, start_rect) : APP_REDRAW_NONE;
}
app_redraw_t app_book_external_turn(app_ctx_t* ctx, int direction) {
    if (!ctx || !s_product_session || s_view != READING || !s_text ||
        s_toolbar || s_selection || s_image_confirm_open || (s_image_open && !s_cover_open) || s_scrubbing || s_progress_pending ||
        s_page_jump_open || s_clear_confirm || s_batch_confirm || s_scan_pending ||
        s_open_path[0] || s_resume_path[0] || (direction != -1 && direction != 1)) return APP_REDRAW_NONE;
    if (ps_dual_enabled()) {
        if (s_dual_anchor.pending || s_dual_target || !ps_dual_turn(direction)) return APP_REDRAW_NONE;
        // 已接收异步翻页，推屏由双机服务完成。/ Accepted asynchronous turn; the paired service owns presentation.
        return APP_REDRAW_DONE;
    }
    return turn_page(ctx, direction);
}
static app_redraw_t on_key(app_ctx_t* ctx, int key) {
    if(s_dual_anchor.pending) { if(key==UI_KEY_2){dual_read_cancel();return product_exit(ctx);} return APP_REDRAW_NONE; }
    s_modal_key_cancelled = false;
    s_pressed_control = -1;
    cancel_interaction();
    if (s_image_confirm_open) {
        if (key == UI_KEY_2) { image_confirm_close(); s_modal_key_cancelled = true; return APP_REDRAW_PAGE; }
        return APP_REDRAW_NONE;
    }
    if (s_page_jump_open) {
        if (key == UI_KEY_2) { page_jump_close(); s_modal_key_cancelled = true; return APP_REDRAW_PAGE; }
        return APP_REDRAW_NONE;
    }
    if (s_selection) {
        if (key == UI_KEY_2) { s_selection = s_selection_menu = false; invalidate_prep(); return APP_REDRAW_PAGE; }
        size_t at = s_selection_edge ? text_previous(s_selection_end) : s_selection_start;
        if (key == UI_KEY_1) at = text_previous(at);
        else if (key == UI_KEY_3) at = text_next(at);
        else return APP_REDRAW_NONE;
        selection_adjust(at); return APP_REDRAW_PAGE;
    }
    if (key == UI_KEY_2 && s_view == TOC && s_text) { s_view = READING; s_toolbar = false; s_modal_key_cancelled = true; return APP_REDRAW_PAGE; }
    if (s_cover_open) return key == UI_KEY_3 || key == UI_KEY_2 ? close_image() : APP_REDRAW_NONE;
    if (s_image_open && (key == UI_KEY_1 || key == UI_KEY_2 || key == UI_KEY_3)) return close_image();
    if (s_product_session && key == UI_KEY_2) return product_exit(ctx);
    if (s_open_path[0]) return APP_REDRAW_NONE;
    if (s_resume_path[0]) {
        if (key == UI_KEY_1 || key == UI_KEY_3) {
            return resume_choice(ctx, key == UI_KEY_3);
        }
        if (key == UI_KEY_2) { s_resume_path[0] = 0; ctx->request_menu = true; }
        return APP_REDRAW_PAGE;
    }
    if (s_scan_pending || s_clear_confirm) return APP_REDRAW_NONE;
    if (s_view == MANAGE) {
        if (key == UI_KEY_1) { s_view = SHELF; return APP_REDRAW_PAGE; }
        return APP_REDRAW_NONE;
    }
    if (s_view == SEARCH) { if (key == UI_KEY_1) { search_finish(ctx, false); return APP_REDRAW_PAGE; } return APP_REDRAW_NONE; }
    if (s_batch_confirm) { if (key == UI_KEY_1) { s_batch_confirm = false; return APP_REDRAW_PAGE; } return APP_REDRAW_NONE; }
    if (key == UI_KEY_2) {
        if (s_view == BULK) s_view = SHELF;
        else if (s_view == READING) {
            s_toolbar = !s_toolbar;
            invalidate_prep();
        } else if (s_view == TOC && s_text) {
            s_view = READING;
            s_toolbar = false;
        } else {
            ctx->request_menu = true;
            return APP_REDRAW_NONE;
        }
        return APP_REDRAW_PAGE;
    }
    if (key != UI_KEY_1 && key != UI_KEY_3) return APP_REDRAW_NONE;
    int dir = key == UI_KEY_1 ? -1 : 1;
    if (s_view == READING) return turn_page(ctx, dir);
    int next = ctx->leaf + dir;
    if (next < 0 || next >= leaves()) return APP_REDRAW_NONE;
    ctx->leaf = next;
    return APP_REDRAW_PAGE;
}
static app_redraw_t on_key_long(app_ctx_t* ctx, int key) {
    if (key != UI_KEY_2) return APP_REDRAW_NONE;
    if (s_modal_key_cancelled) { s_modal_key_cancelled = false; return APP_REDRAW_NONE; }
    if (s_image_confirm_open) { image_confirm_close(); return APP_REDRAW_PAGE; }
    if (s_page_jump_open) { page_jump_close(); return APP_REDRAW_PAGE; }
    if (s_product_session) return product_exit(ctx);
    save_progress();
    ctx->request_menu = true;
    return APP_REDRAW_NONE;
}
static app_redraw_t open_pending_book(app_ctx_t* ctx) {
    if (!s_open_path[0] || !s_open_presented || ctx->consumed || ctx->request_app || ctx->request_menu ||
        ctx->request_return || ctx->request_developer_menu) return APP_REDRAW_NONE;
    // 恢复系统首选但不写设置；缺卡或失败时保留字体引擎的临时回退。
    // Restore the saved preference without writing settings; missing media/failure keeps the temporary font fallback.
    const char* preferred = ps_dual_enabled()&&s_dual_font[0]?s_dual_font:app_settings_font_path();
    if (!preferred || !preferred[0]) preferred = TTF_FONT_BUILTIN;
    if (!ttf_font_ready() || strcmp(ttf_font_path(), preferred)) {
        if (!ttf_font_path_is_builtin(preferred)) loading_detail(ctx, "正在加载字体…", "首次加载需要一些时间，请稍候");
        ttf_font_open(preferred);
    }
    if(ps_dual_enabled())ttf_set_weight(400);
    bool opened = open_book(ctx, s_open_path);
    s_annotation_pending = false;
    s_open_path[0] = 0;
    s_open_presented = false;
    if(!opened && ps_dual_enabled()) return dual_read_failed();
    return APP_REDRAW_PAGE;
}
bool app_book_dual_apply(const char *path,const char *font,uint32_t target,uint8_t px,bool reflow,uint32_t revision) {
    if(!ps_dual_enabled()||!target||!path||!font||strlen(font)>=sizeof(s_dual_font))return false;
    if(s_dual_anchor.pending||s_dual_annotation||s_selection||s_toolbar||(s_view==TOC&&s_text)||s_image_open||s_image_confirm_open||s_page_jump_open||s_scrubbing)return false;
    s_dual_present_pending=0;
    dl_status_t current; ps_dual_status(&current);
    s_dual_px=px; s_dual_old_px=current.reading.reflow_from_px;
    s_dual_reflow=reflow?revision:0;
    s_dual_target=target;s_dual_blank=false;copy_text(s_dual_font,sizeof(s_dual_font),font);
    if((s_text&&!strcmp(s_path,path))||!strcmp(s_open_path,path)||!strcmp(s_requested_path,path))return true;
    s_dual_applying=true;bool ok=app_book_open_path(path);s_dual_applying=false;return ok;
}
bool app_book_dual_engaged(void) {
    return s_product_session && s_text && !s_open_path[0] &&
           (s_view == READING || s_view == TOC);
}
void app_book_dual_pause(void) {
    s_dual_target = 0;
    s_dual_present_pending = 0;
    s_dual_anchor.pending = false;
    s_dual_wait = false;
}
void app_book_dual_status(app_book_dual_status_t* out) {
    *out = (app_book_dual_status_t){.open=s_text && s_view==READING, .blank=s_dual_blank,
        .target=s_dual_target, .page=s_dual_displayed, .chapter=s_chapter, .font_px=s_px,
        .start=s_text ? book_layout_page_start_offset(s_page) : 0,
        .end=s_text ? book_layout_page_start_offset(s_page+1) : 0};
}
static void dual_anchor_start(size_t chapter, size_t offset, bool source, uint32_t revision) {
    page_jump_close(); cancel_interaction();
    s_dual_anchor.pending=true; s_dual_anchor.source=source;
    s_dual_anchor.chapter=chapter; s_dual_anchor.offset=offset; s_dual_anchor.revision=revision;
    s_toolbar=s_selection=s_selection_menu=false; s_view=READING;
}
static app_redraw_t dual_anchor_tick(app_ctx_t* ctx) {
    if (!s_text || !page_index_prepare() || s_dual_anchor.chapter>=book_chapter_count()) goto fail;
    size_t chapter=s_page_index.done<s_dual_anchor.chapter?s_page_index.done:s_dual_anchor.chapter;
    bool changed=false;
    if (!page_index_step(chapter,&changed)) goto fail;
    if (chapter!=s_dual_anchor.chapter) return APP_REDRAW_NONE;
    size_t offset=s_dual_anchor.offset;
    if (s_dual_anchor.source) {
        uint32_t first=book_chapter_byte_offset(chapter);
        uint32_t end=chapter+1<book_chapter_count()?book_chapter_byte_offset(chapter+1):book_total_bytes();
        uint32_t span=end>=first?end-first:0;
        offset=span?(size_t)((uint64_t)(offset>span?span:offset)*s_page_index.loaded.len/span):0;
    }
    if(offset>s_page_index.loaded.len) goto fail;
    size_t count=book_layout_job_page_count(s_page_index.job), local=0, start=0, next=0;
    if(!count) return APP_REDRAW_NONE;
    while(local+1<count && book_layout_job_page_offset(s_page_index.job,local+1,&next) && next<=offset) ++local;
    if(local+1==count&&!book_layout_job_complete(s_page_index.job)) return APP_REDRAW_NONE;
    if(!book_layout_job_page_offset(s_page_index.job,local,&start)) goto fail;
    uint64_t page=local+1;
    for(size_t i=0;i<chapter;++i) page+=s_page_index.counts[i];
    if(page>UINT32_MAX) goto fail;
    bool ok=s_dual_anchor.revision ? ps_dual_reflow_resolved(s_dual_anchor.revision,((uint32_t)page-1)&~1u) :
        s_dual_annotation ? ps_dual_open_at(s_path,(uint32_t)page) : ps_dual_seek((uint32_t)page);
    if(!ok) goto fail;
    s_dual_anchor.pending=s_dual_annotation=false; s_dual_reflow=0;
    page_index_pause(); return APP_REDRAW_NONE;
fail:
    return dual_read_failed();
}
static void dual_read_cancel(void) {
    bool annotation=s_dual_annotation;
    if(s_dual_reflow) (void)ps_dual_reflow_failed(s_dual_reflow);
    s_dual_reflow=s_dual_target=s_dual_present_pending=0;
    s_dual_anchor.pending=s_dual_annotation=s_dual_wait=false;
    if(s_text) s_px=s_layout_px;
    page_index_pause();
    if(annotation) { free_book(); s_view=SHELF; }
}
static app_redraw_t dual_read_failed(void) {
    bool restore=s_dual_annotation;
    dual_read_cancel();
    if(restore) (void)ps_dual_resume();
    ps_toast("定位或重排失败，请重试"); return APP_REDRAW_PAGE;
}
static app_redraw_t dual_page_ready(app_ctx_t* ctx) {
    if(s_dual_reflow) {
        size_t chapter=s_chapter, offset=book_layout_page_start_offset(s_page);
        s_dual_target=0; s_px=s_dual_px; page_index_reset();
        dual_anchor_start(chapter,offset,false,s_dual_reflow);
        return APP_REDRAW_NONE;
    }
    uint32_t previous_page = s_dual_displayed;
    s_dual_displayed=s_dual_target; s_dual_present_pending=s_dual_target;
    // 主机是同步基准，先推屏确认再让从机跟进；主机不做翻页动画，避免把
    // left_ready 推迟四帧，让从机再等一轮。从机保留动画掩盖网络/刷新间隔。
    // The primary is the sync reference: skip its animation so left_ready is not
    // delayed by four frames; the secondary keeps the animation to mask the gap.
    s_turn_anim_pending = previous_page && s_dual_displayed != previous_page && ps_dual_role() != 1;
    s_turn_anim_dir = s_dual_displayed > previous_page ? 1 : -1;
    s_dual_target=0; s_dual_blank=s_dual_wait=false; s_view=READING;
    prepare_page_images(); save_progress(); return paint_reading(ctx,MODE_GL16);
}
static app_redraw_t dual_page_tick(app_ctx_t *ctx) {
    if(!s_dual_target||!s_text||!ps_dual_enabled())return APP_REDRAW_NONE;
    if(strcmp(ttf_font_path(),s_dual_font)||s_px!=(s_dual_reflow?s_dual_old_px:s_dual_px)||s_layout_px!=s_px){
        page_index_reset(); lock_draw(); invalidate_prep();
        bool ok=!strcmp(ttf_font_path(),s_dual_font)||ttf_font_open(s_dual_font)==ESP_OK;
        ttf_set_weight(400); unlock_draw();
        if(!ok) return dual_read_failed();
        s_px=s_dual_reflow?s_dual_old_px:s_dual_px;
        if(!load_chapter(ctx,0,0,false)) return dual_read_failed();
    }
    if(!page_index_prepare()) return dual_read_failed();
    size_t chapter=0,page=0,offset=0;
    bool located=page_index_locate(s_dual_target,&chapter,&page);
    if(located&&chapter==s_chapter&&page<book_layout_page_count()){
        s_page=page;return dual_page_ready(ctx);
    }
    if(!located&&s_page_index.done==s_page_index.chapters){
        if(s_dual_reflow) return dual_read_failed();
        ps_dual_total((uint32_t)s_page_index.total);s_dual_blank=ps_dual_role()==2;s_dual_wait=false;
        s_dual_present_pending=s_dual_blank?s_dual_target:0;s_dual_target=0;s_view=READING;return APP_REDRAW_PAGE;
    }
    size_t job_chapter=located?chapter:s_page_index.done;bool changed=false;
    if(!page_index_step(job_chapter,&changed)) return dual_read_failed();
    size_t target_page=located?page:0;
    // 完成一章后total已更新；重新定位，避免无符号下溢。/ Completion advances total; locate again before subtracting.
    if(page_index_locate(s_dual_target,&chapter,&page)){job_chapter=chapter;target_page=page;}
    else {job_chapter=s_page_index.done;target_page=s_dual_target>s_page_index.total?(size_t)(s_dual_target-s_page_index.total-1):0;}
    bool ready=false;
    if(s_page_index.job&&s_page_index.job_chapter==job_chapter){lock_draw();ready=book_layout_job_page_offset(s_page_index.job,target_page,&offset);unlock_draw();}
    if(ready){
        if(!load_chapter(ctx,job_chapter,offset,false)) return dual_read_failed();
        return dual_page_ready(ctx);
    }
    if(s_page_index.job){lock_draw();bool complete=book_layout_job_complete(s_page_index.job);unlock_draw();if(complete)page_index_pause();}
    return APP_REDRAW_NONE;
}
static app_redraw_t on_tick(app_ctx_t* ctx) {
    if (ps_book_rotation_tick(ctx, s_view == READING && s_text && !s_image_open && !s_cover_open && !s_open_path[0] && !s_dual_anchor.pending && !s_dual_target && !s_selection &&
        !s_toolbar && !s_page_jump_open && !s_image_confirm_open && !s_scrubbing && !s_progress_pending) ||
        s_book_rotation != epd_get_rotation()) {
        s_book_rotation = epd_get_rotation();
        lock_draw(); invalidate_prep(); unlock_draw();
        s_du_count = 0;
        return APP_REDRAW_FULL;
    }
    if (ctx->consumed) return APP_REDRAW_NONE;
    if (s_requested_path[0]&&ps_dual_enabled()){
        char next[BOOK_STORE_PATH_MAX];copy_text(next,sizeof(next),s_requested_path);s_requested_path[0]=0;copy_text(s_open_path,sizeof(s_open_path),next);s_open_presented=true;
    }
    if (s_open_path[0]) return open_pending_book(ctx);
    if(s_dual_anchor.pending&&ps_dual_enabled())return dual_anchor_tick(ctx);
    if(s_dual_target&&ps_dual_enabled())return dual_page_tick(ctx);
    if(s_dual_present_pending && s_view == READING && !s_page_jump_open && !s_toolbar && !s_selection &&
        !s_image_open && !s_image_confirm_open && !s_scrubbing && !s_progress_pending &&
        !(ctx->touch && ctx->touch->touched)) return APP_REDRAW_FULL;
    EpdRect body = body_rect();
    bool geometry_changed = s_text && (body.width != s_layout_rect.width || body.height != s_layout_rect.height);
    bool font_changed = s_text && (strcmp(s_font_path, ttf_font_path()) ||
        (s_page_index.counts && s_page_index.weight != ttf_get_weight()));
    if (geometry_changed || font_changed || (s_page_index.counts && !page_index_matches())) {
        cancel_interaction(); page_jump_close(); page_index_reset();
        if (s_image_confirm_open) image_confirm_close();
    }
    if (s_progress_pending || s_scrubbing || s_selection_drag) return APP_REDRAW_NONE;
    // 弹窗未确认时完全静止；定位时也不让普通DU定稿或保存重试触发整页刷新。
    // An unconfirmed dialog stays idle; locating also bypasses ordinary DU settling and save-retry page refreshes.
    if (s_image_confirm_open) return APP_REDRAW_NONE;
    if (s_page_jump_open) return page_jump_tick(ctx);
    if (s_save_failed && ctx->now_ms - s_save_retry_ms >= 15000) {
        s_save_retry_ms = ctx->now_ms;
        bool failed = s_save_failed;
        retry_progress();
        if (failed != s_save_failed) { invalidate_prep(); return APP_REDRAW_PAGE; }
    }
    if (s_scan_pending && ctx->now_ms - s_poll_ms >= 500) {
        s_poll_ms = ctx->now_ms;
        read_pico_sd_info_t info = {0};
        if (read_pico_sd_get_info(&info) == ESP_ERR_NOT_FINISHED) return APP_REDRAW_NONE;
        s_scan_pending = false;
        scan_shelf(ctx);
        if (s_resume_pending) {
            s_resume_pending = false;
            char path[BOOK_STORE_PATH_MAX];
            if (book_progress_last_path(path, sizeof(path))) {
                for (int i = 0; i < s_count; ++i) if (!s_shelf[i].removed && !strcmp(s_shelf[i].path, path)) {
                    copy_text(s_resume_path, sizeof(s_resume_path), path);
                    copy_text(s_resume_name, sizeof(s_resume_name), s_shelf[i].name);
                    break;
                }
            }
        }
        return APP_REDRAW_PAGE;
    }
    if (!geometry_changed && s_du_count && (s_du_count >= UI_SETTLE_DU_MAX ||
        ctx->now_ms - s_du_ms >= UI_SETTLE_IDLE_MS)) {
        s_area = s_du_area;
        s_mode = MODE_GL16;
        ESP_LOGI(TAG, "settle du=%u", s_du_count);
        return APP_REDRAW_AREA;
    }
    if (s_text && (geometry_changed || font_changed)) {
        size_t off = book_layout_page_start_offset(s_page);
        save_progress();
        lock_draw();
        invalidate_prep();
        bool ok = book_layout_build_blocks(s_text, s_text_len, s_blocks, s_block_count, body_rect(), s_px);
        if (ok) { s_page = book_layout_page_for_offset(off); s_layout_rect = body_rect(); }
        copy_text(s_font_path, sizeof(s_font_path), ttf_font_path());
        unlock_draw();
        if (!ok) { free_book(); s_view = SHELF; copy_text(s_message, sizeof(s_message), "字体重排失败，请重新打开图书"); }
        else { selection_geometry(); prepare_page_images(); }
        return APP_REDRAW_PAGE;
    }
    if (s_size_settle_ms && ctx->now_ms >= s_size_settle_ms) {
        s_size_settle_ms = 0;
        if (s_view == READING && !s_image_open) return paint_reading(ctx, MODE_GL16);
    }
    if (s_view == READING && !s_image_open && !s_image_confirm_open && s_text && !book_layout_complete() &&
        !s_toolbar && !s_selection && !(ctx->touch && ctx->touch->touched)) {
        lock_draw();
        invalidate_prep();
        bool ok = book_layout_extend(2);
        unlock_draw();
        if (!ok) {
            free_book(); s_view = SHELF; ctx->leaf = 0;
            copy_text(s_message, sizeof(s_message), "排版失败：内存不足或章节过长");
            return APP_REDRAW_PAGE;
        }
        // 页数完成后随下次交互更新，避免阅读中重复刷屏。
        // Display the final page count on the next interaction, avoiding unsolicited refreshes.
    }
    if (s_shake_enabled && s_sensor_on && ctx->now_ms - s_sensor_ms >= 40) {
        s_sensor_ms = ctx->now_ms;
        if (!sc7a20h_powered(ctx->acc)) sensor_set(ctx, true);
        sc7a20h_events_t ev;
        if (sc7a20h_read_events(ctx->acc, &ev) == ESP_OK) {
            bool suppressed = s_view != READING || s_image_open || s_image_confirm_open || s_toolbar || s_selection || s_scrubbing || s_progress_pending || s_page_jump_open || s_clear_confirm ||
                              (ctx->touch && ctx->touch->touched) || ctx->now_ms - s_last_turn_ms < 800;
            if (book_shake_feed(&s_shake, ev.aoi2_src & 0x40, suppressed, ctx->now_ms)) return turn_page(ctx, 1);
        }
    }
    return APP_REDRAW_NONE;
}
static EpdRect area_hint(app_ctx_t* ctx) { (void)ctx; return s_area; }

const app_desc_t app_book = {
    .title = "图书 Books", .detail = "TF 卡 txt / epub 阅读", .enter_full = true, .owns_keys = true,
    .custom_menu_handle = true, .continuous_gestures = true,
    .render = render, .present = present, .on_enter = on_enter, .on_exit = book_on_exit,
    .on_media_lost = book_on_media_lost,
    .on_media_restored = book_on_media_restored,
    .on_gesture = gesture_event, .on_key = on_key, .on_key_long = on_key_long,
    .on_tick = on_tick, .area_hint = area_hint,
};

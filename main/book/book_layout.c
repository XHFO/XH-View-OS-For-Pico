/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 *
 * 中文：借鉴阅读演示的逐码点折行，建立 PSRAM 页表并绘制章节。
 * English: Adapt the reading demo's codepoint wrapping into PSRAM chapter pagination.
 *
 * 冻结：原文由调用方持有；字体测量和绘制必须由调用方串行化。
 * 为尽早进入阅读，支持分批分页；图片按预探测宽高等比整块排版，可见页在绘制前载入；失败占位不改变分页。绘图不读取资源或解码。
 * 用户新增：批注命中与下划线复用真实折行和字宽，保留章节UTF-8字节锚点，不修改原文或分页。
 * 用户修订：拖拽吸附最近可见文字，目标分页失败时保留原页表；全书精确计页使用独立可分步上下文，不改变活跃阅读布局。
 * Frozen: Caller owns source text and serializes all font measurement and drawing.
 * Incremental pagination enables early reading; probed image dimensions define proportional whole-image rows, loaded before visible-page drawing. Failure placeholders preserve pagination; never read or decode resources while drawing.
 * User addition: annotation hit testing and underlines reuse actual wrapping and glyph advances, preserving chapter UTF-8 byte anchors without changing source or pagination.
 * User revision: drag snapping exposes glyph bounds; transactional seeks retain the old layout on failure, and independent bounded pagination jobs preserve active reading.
 */
#include "book_layout.h"
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "esp_heap_caps.h"
#include "ttf_font.h"

#define PAGE_MAX 4096u
#define PSRAM_CAPS (MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)

// 活跃阅读和独立计页各自持有状态；调用方仍以字体锁串行执行。
// Active reading and pagination jobs own separate states, serialized by the caller's font lock.
typedef struct {
    const char* text;
    size_t len, *pages, count, capacity;
    char* line;
    EpdRect rect;
    int px;
    size_t scan;
    int64_t used;
    bool complete, count_only;
    const blk_t* blocks;
    size_t block_count;
} layout_state_t;
struct book_layout_job { layout_state_t state; bool failed; };
static layout_state_t s_active;
static layout_state_t* s_current = &s_active;
#define s_text (s_current->text)
#define s_len (s_current->len)
#define s_pages (s_current->pages)
#define s_count (s_current->count)
#define s_capacity (s_current->capacity)
#define s_line (s_current->line)
#define s_rect (s_current->rect)
#define s_px (s_current->px)
#define s_scan (s_current->scan)
#define s_used (s_current->used)
#define s_complete (s_current->complete)
#define s_blocks (s_current->blocks)
#define s_block_count (s_current->block_count)

// 块表是有序字节区间，二分查找当前行样式。/ Blocks are ordered byte ranges; binary-search the line style.
static const blk_t* block_at(size_t off) {
    if (!s_block_count) return NULL;
    size_t lo = 0, hi = s_block_count;
    while (lo + 1 < hi) {
        size_t mid = lo + (hi - lo) / 2;
        if (s_blocks[mid].offset <= off) lo = mid;
        else hi = mid;
    }
    return &s_blocks[lo];
}
// 插图等比适配正文区域，不放大；整块换页，不切断图片。
// Fit illustrations proportionally without upscaling; move whole images across page boundaries.
static void image_size(const blk_t* block, int* width, int* height) {
    *width = block->image_width; *height = block->image_height;
    if (*width > s_rect.width) { *height = (int)((int64_t)*height * s_rect.width / *width); *width = s_rect.width; }
    if (*height > s_rect.height) { *width = (int)((int64_t)*width * s_rect.height / *height); *height = s_rect.height; }
    if (*width < 1) *width = 1;
    if (*height < 1) *height = 1;
}
static int row_height(const blk_t* block, int px) {
    if (block && block->image_width && block->image_height && (block->image || block->image_src)) { int width, height; image_size(block, &width, &height); return height; }
    if (block && block->image_src) return 2 * s_px < s_rect.height ? 2 * s_px : s_rect.height;
    return px + px / 2;
}
static EpdRect placeholder_rect(EpdRect body, int top, int height) {
    int pad = body.width > 24 && height > 16 ? 8 : 0;
    return (EpdRect){body.x + pad, body.y + top + pad / 2, body.width - 2 * pad, height - pad};
}
static void draw_placeholder(uint8_t* fb, const blk_t* block, EpdRect rect) {
    for (int x = rect.x; x < rect.x + rect.width; ++x) {
        epd_draw_pixel(x, rect.y, 0, fb); epd_draw_pixel(x, rect.y + rect.height - 1, 0, fb);
    }
    for (int y = rect.y; y < rect.y + rect.height; ++y) {
        epd_draw_pixel(rect.x, y, 0, fb); epd_draw_pixel(rect.x + rect.width - 1, y, 0, fb);
    }
    int px = s_px < 32 ? s_px : 32;
    bool repeat = block->image_repeated && !block->image_title;
    const char* label = block->image_failed ? "图片暂不可用 · 点击重试" : "图片加载中";
    if (ttf_text_width_px(px, label) > rect.width - 16) return;
    int lines = repeat ? 2 : 1;
    if (rect.height < lines * (px + 4) || rect.width < 1) return;
    int top = rect.y + (rect.height - lines * (px + 4)) / 2;
    ttf_draw_text_px(fb, rect.x + rect.width / 2, top + ttf_ascender_px(px), px,
                     label, EPD_DRAW_ALIGN_CENTER, 0, 15);
    if (repeat) {
        char origin[64]; snprintf(origin, sizeof(origin), "已读最早：第 %u 节", (unsigned)block->image_first_chapter + 1);
        int small = px < 24 ? px : 24;
        if (ttf_text_width_px(small, origin) <= rect.width - 16)
            ttf_draw_text_px(fb, rect.x + rect.width / 2, top + px + 4 + ttf_ascender_px(small), small,
                             origin, EPD_DRAW_ALIGN_CENTER, 0, 15);
    }
}
// 拒绝截断、过长编码、代理项和嵌入零字节。/ Reject truncation, overlong encodings, surrogates and embedded NUL.
static size_t codepoint_size(const char* text, size_t remaining) {
    if (!remaining) return 0;
    const unsigned char* p = (const unsigned char*)text;
    if (p[0] > 0 && p[0] < 0x80) return 1;
    size_t n = p[0] >= 0xc2 && p[0] <= 0xdf ? 2 :
               p[0] >= 0xe0 && p[0] <= 0xef ? 3 :
               p[0] >= 0xf0 && p[0] <= 0xf4 ? 4 : 0;
    if (!n || n > remaining) return 0;
    for (size_t i = 1; i < n; i++) if ((p[i] & 0xc0) != 0x80) return 0;
    if ((p[0] == 0xe0 && p[1] < 0xa0) || (p[0] == 0xed && p[1] >= 0xa0) ||
        (p[0] == 0xf0 && p[1] < 0x90) || (p[0] == 0xf4 && p[1] >= 0x90)) return 0;
    return n;
}

void book_layout_free(void) {
    free(s_pages);
    free(s_line);
    s_pages = NULL;
    s_line = NULL;
    s_text = NULL;
    s_count = s_capacity = s_len = 0;
    s_px = 0;
    s_scan = 0;
    s_used = 0;
    s_complete = false;
    s_blocks = NULL;
    s_block_count = 0;
}

// 原页表直到目标准备完才释放，内存或分页失败可无分配回滚。
// Keep the original page table until the target is ready, permitting allocation-free rollback.
bool book_layout_position_blocks(const char* text, size_t len, const blk_t* blocks, size_t count,
                                 EpdRect rect, int px, size_t offset, bool last_page) {
    const char* old_text = s_text; size_t old_len = s_len;
    size_t* old_pages = s_pages; size_t old_count = s_count, old_capacity = s_capacity;
    char* old_line = s_line; EpdRect old_rect = s_rect; int old_px = s_px;
    size_t old_scan = s_scan; int64_t old_used = s_used; bool old_complete = s_complete;
    const blk_t* old_blocks = s_blocks; size_t old_block_count = s_block_count;
    s_pages = NULL; s_line = NULL;
    bool ok = book_layout_begin_blocks(text, len, blocks, count, rect, px);
    while (ok && !book_layout_complete() &&
           (last_page || book_layout_page_start_offset(book_layout_page_count()) <= offset))
        ok = book_layout_extend(2);
    if (ok) { free(old_pages); free(old_line); return true; }
    book_layout_free();
    s_text = old_text; s_len = old_len; s_pages = old_pages; s_count = old_count; s_capacity = old_capacity;
    s_line = old_line; s_rect = old_rect; s_px = old_px; s_scan = old_scan; s_used = old_used;
    s_complete = old_complete; s_blocks = old_blocks; s_block_count = old_block_count;
    return false;
}

static bool append_page(size_t off) {
    if (s_count == PAGE_MAX) return false;
    if (s_count == s_capacity) {
        size_t cap = s_capacity ? s_capacity * 2 : 16;
        size_t* pages = heap_caps_realloc(s_pages, cap * sizeof(*pages), PSRAM_CAPS);
        if (!pages) return false;
        s_pages = pages;
        s_capacity = cap;
    }
    s_pages[s_count++] = off;
    return true;
}

// 折行时保留原文字节位置；CRLF 算一个段落边界。/ Preserve source offsets while wrapping; CRLF is one paragraph boundary.
static bool take_line(size_t off, size_t* next, bool* paragraph_end, int* px, bool* heading) {
    const blk_t* block = block_at(off);
    *heading = block && block->heading;
    *px = s_px + (*heading ? 8 : 0);
    size_t limit = block ? block->offset + block->len : s_len;
    size_t end = off;
    int64_t width = 0;
    if (s_line) s_line[0] = 0;
    *paragraph_end = false;
    if (block && (block->image || block->image_src)) {
        *heading = false;
        *next = limit + (limit < s_len && s_text[limit] == '\n');
        return *next > off;
    }
    while (end < limit && s_text[end] != '\r' && s_text[end] != '\n') {
        size_t n = codepoint_size(s_text + end, s_len - end);
        if (!n) return false;
        // 字体逐字取整后累加 advance；单字测量避免反复扫描整行前缀。
        // Font advances are rounded per glyph and summed; measure each glyph once instead of every prefix.
        char glyph[5];
        memcpy(glyph, s_text + end, n);
        glyph[n] = 0;
        int64_t candidate = width + ttf_text_width_px(*px, glyph);
        if (candidate < 0) return false;
        if (candidate > s_rect.width) {
            if (end == off) return false;
            break;
        }
        width = candidate;
        if (s_line) { memcpy(s_line + end - off, glyph, n); s_line[end - off + n] = 0; }
        end += n;
    }
    *next = end;
    if (end < s_len && (s_text[end] == '\r' || s_text[end] == '\n')) {
        *next = end + 1;
        if (s_text[end] == '\r' && *next < s_len && s_text[*next] == '\n') (*next)++;
        *paragraph_end = true;
    }
    return *next > off;
}

bool book_layout_build(const char* utf8, size_t len, EpdRect rect, int px) {
    return book_layout_build_blocks(utf8, len, NULL, 0, rect, px);
}

bool book_layout_build_blocks(const char* utf8, size_t len, const blk_t* blocks, size_t count, EpdRect rect, int px) {
    return book_layout_begin_blocks(utf8, len, blocks, count, rect, px) && book_layout_extend(PAGE_MAX);
}

bool book_layout_begin_blocks(const char* utf8, size_t len, const blk_t* blocks, size_t count, EpdRect rect, int px) {
    book_layout_free();
    if ((!utf8 && len) || len == SIZE_MAX || px <= 0 || px > INT_MAX / 3 ||
        rect.width <= 0 || rect.height <= 0 || rect.x < 0 || rect.y < 0 ||
        rect.x > INT_MAX - rect.width || rect.y > INT_MAX - rect.height) return false;
    int line_height = px + px / 2;
    if (line_height > rect.height) return false;
    for (size_t off = 0; off < len;) {
        size_t n = codepoint_size(utf8 + off, len - off);
        if (!n) return false;
        off += n;
    }
    if (count) {
        if (!blocks || count > HTML_TEXT_MAX_BLOCKS) return false;
        size_t expected = 0;
        for (size_t i = 0; i < count; ++i) {
            const blk_t* b = &blocks[i];
            if (b->image && (!b->image_width || !b->image_height)) return false;
            if (b->offset != expected || b->offset >= len || !b->len || b->len > len - b->offset ||
                ((unsigned char)utf8[b->offset] & 0xc0) == 0x80) return false;
            size_t end = b->offset + b->len;
            if (i + 1 < count) {
                if (end >= len || utf8[end] != '\n') return false;
                expected = end + 1;
            } else if (end != len) return false;
        }
    }
    s_blocks = blocks;
    s_block_count = count;
    s_text = utf8;
    s_len = len;
    s_px = px;
    s_rect = rect;
    // 纯计页不复制当前行，避免为2MB章节再分配同等大小的scratch。
    // Counting never copies a drawn line, avoiding another chapter-sized scratch allocation.
    if (!s_current->count_only) s_line = heap_caps_malloc(len + 1, PSRAM_CAPS);
    if ((!s_current->count_only && !s_line) || !append_page(0)) goto fail;
    return book_layout_extend(2);
fail:
    book_layout_free();
    return false;
}

bool book_layout_extend(size_t pages) {
    if (!s_count) return false;
    if (!pages || s_complete) return true;
    size_t target = pages > PAGE_MAX - book_layout_page_count() ? PAGE_MAX : book_layout_page_count() + pages;
    while (s_scan < s_len) {
        size_t next;
        bool paragraph_end, heading;
        int line_px;
        if (!take_line(s_scan, &next, &paragraph_end, &line_px, &heading)) goto fail;
        const blk_t* block = block_at(s_scan);
        int line_height = row_height(block, line_px);
        if (line_height > s_rect.height) goto fail;
        if (s_used + line_height > s_rect.height) {
            if (!append_page(s_scan)) goto fail;
            s_used = 0;
            if (book_layout_page_count() >= target) return true;
        }
        s_used += line_height;
        if (paragraph_end) s_used += line_height / (heading ? 2 : 3);
        s_scan = next;
    }
    s_complete = true;
    return true;
fail:
    book_layout_free();
    return false;
}

size_t book_layout_page_count(void) { return s_count ? s_count - !s_complete : 0; }
bool book_layout_complete(void) { return s_complete; }

size_t book_layout_page_start_offset(size_t page) {
    return page < s_count ? s_pages[page] : s_len;
}

size_t book_layout_page_for_offset(size_t off) {
    size_t lo = 0, hi = book_layout_page_count();
    while (lo + 1 < hi) {
        size_t mid = lo + (hi - lo) / 2;
        if (s_pages[mid] <= off) lo = mid;
        else hi = mid;
    }
    return lo;
}

void book_layout_draw_page(uint8_t* fb, size_t page, EpdRect rect, int px) {
    if (!fb || page >= book_layout_page_count() || px != s_px || rect.width != s_rect.width ||
        rect.height != s_rect.height || rect.x < 0 || rect.y < 0 ||
        rect.x > INT_MAX - rect.width || rect.y > INT_MAX - rect.height) return;
    size_t off = s_pages[page];
    size_t end = page + 1 < s_count ? s_pages[page + 1] : s_len;
    int64_t used = 0;
    while (off < end) {
        size_t next;
        bool paragraph_end, heading;
        int line_px;
        if (!take_line(off, &next, &paragraph_end, &line_px, &heading)) return;
        const blk_t* block = block_at(off);
        int line_height = row_height(block, line_px);
        int image_width = 0;
        if (block && block->image) image_size(block, &image_width, &line_height);
        if (used + line_height > rect.height) return;
        if (image_width) {
            int left = rect.x + (rect.width - image_width) / 2;
            for (int y = 0; y < line_height; ++y) for (int x = 0; x < image_width; ++x) {
                size_t src = (size_t)((int64_t)y * block->image_height / line_height) * block->image_width +
                             (size_t)((int64_t)x * block->image_width / image_width);
                epd_draw_pixel(left + x, rect.y + (int)used + y, block->image[src], fb);
            }
        } else if (block && block->image_src) {
            draw_placeholder(fb, block, placeholder_rect(rect, (int)used, line_height));
        } else if (s_line[0]) {
            ttf_draw_text_px(fb, rect.x, rect.y + (int)used + ttf_ascender_px(line_px), line_px,
                             s_line, EPD_DRAW_ALIGN_LEFT, 0, 15);
        }
        used += line_height;
        if (paragraph_end) used += line_height / (heading ? 2 : 3);
        off = next;
    }
}
size_t book_layout_image_at(size_t page, EpdRect rect, int x, int y, EpdRect* hit) {
    if (page >= book_layout_page_count() || rect.width != s_rect.width || rect.height != s_rect.height ||
        rect.x < 0 || rect.y < 0 || rect.x > INT_MAX - rect.width || rect.y > INT_MAX - rect.height ||
        x < rect.x || y < rect.y || x >= rect.x + rect.width || y >= rect.y + rect.height) return SIZE_MAX;
    size_t off = s_pages[page], end = page + 1 < s_count ? s_pages[page + 1] : s_len;
    int64_t used = 0;
    while (off < end) {
        size_t next; bool paragraph_end, heading; int px;
        if (!take_line(off, &next, &paragraph_end, &px, &heading)) return SIZE_MAX;
        const blk_t* block = block_at(off);
        int height = row_height(block, px);
        if (used + height > rect.height) return SIZE_MAX;
        if (block && block->image_src) {
            EpdRect area;
            if (block->image) {
                int width; image_size(block, &width, &height);
                area = (EpdRect){rect.x + (rect.width - width) / 2, rect.y + (int)used, width, height};
            } else area = placeholder_rect(rect, (int)used, height);
            if (x >= area.x && x < area.x + area.width && y >= area.y && y < area.y + area.height) {
                if (hit) *hit = area;
                return (size_t)(block - s_blocks);
            }
        }
        used += height;
        if (paragraph_end) used += height / (heading ? 2 : 3);
        off = next;
    }
    return SIZE_MAX;
}

// 同一折行路径用于命中与标记，标题/图片的高度不会退回按行数估算。
// Reuse wrapping for hits and marks so heading/image heights are never estimated from line counts.
static bool annotation_rect(size_t page, EpdRect rect) {
    return page < book_layout_page_count() && rect.width == s_rect.width && rect.height == s_rect.height &&
        rect.x >= 0 && rect.y >= 0 && rect.x <= INT_MAX - rect.width && rect.y <= INT_MAX - rect.height;
}
size_t book_layout_text_at(size_t page, EpdRect rect, int x, int y) {
    if (!annotation_rect(page, rect) || x < rect.x || x >= rect.x + rect.width ||
        y < rect.y || y >= rect.y + rect.height) return SIZE_MAX;
    size_t off = s_pages[page], end = page + 1 < s_count ? s_pages[page + 1] : s_len;
    int64_t used = 0;
    while (off < end) {
        size_t next; bool paragraph_end, heading; int px;
        if (!take_line(off, &next, &paragraph_end, &px, &heading)) return SIZE_MAX;
        const blk_t* block = block_at(off);
        int height = row_height(block, px);
        if (used + height > rect.height) return SIZE_MAX;
        if ((!block || (!block->image && !block->image_src)) && y >= rect.y + used && y < rect.y + used + height) {
            size_t limit = off + strlen(s_line);
            int left = rect.x;
            for (size_t at = off; at < limit;) {
                size_t n = codepoint_size(s_text + at, limit - at);
                if (!n) return SIZE_MAX;
                char glyph[5]; memcpy(glyph, s_text + at, n); glyph[n] = 0;
                int width = ttf_text_width_px(px, glyph);
                if (x >= left && x < left + width) return at;
                left += width; at += n;
            }
            return SIZE_MAX;
        }
        used += height;
        if (paragraph_end) used += height / (heading ? 2 : 3);
        off = next;
    }
    return SIZE_MAX;
}
void book_layout_draw_range(uint8_t* fb, size_t page, EpdRect rect, size_t start, size_t end, bool selected) {
    if (!fb || start >= end || !annotation_rect(page, rect) || end > s_len ||
        (start < s_len && ((unsigned char)s_text[start] & 0xc0) == 0x80) ||
        (end < s_len && ((unsigned char)s_text[end] & 0xc0) == 0x80)) return;
    size_t off = s_pages[page], limit = page + 1 < s_count ? s_pages[page + 1] : s_len;
    int64_t used = 0;
    while (off < limit && off < end) {
        size_t next; bool paragraph_end, heading; int px;
        if (!take_line(off, &next, &paragraph_end, &px, &heading)) return;
        const blk_t* block = block_at(off);
        int height = row_height(block, px);
        if (used + height > rect.height) return;
        if ((!block || (!block->image && !block->image_src)) && next > start) {
            size_t text_end = off + strlen(s_line);
            int left = rect.x, a = -1, b = -1;
            for (size_t at = off; at < text_end;) {
                size_t n = codepoint_size(s_text + at, text_end - at);
                if (!n) return;
                char glyph[5]; memcpy(glyph, s_text + at, n); glyph[n] = 0;
                int width = ttf_text_width_px(px, glyph);
                if (at >= start && at < end) { if (a < 0) a = left; b = left + width; }
                left += width; at += n;
            }
            int top = rect.y + (int)used + px + 1;
            int rows = selected ? 4 : 2;
            if (top + rows > rect.y + used + height) rows = rect.y + (int)used + height - top;
            if (a >= 0 && b > a) for (int yy = 0; yy < rows; ++yy)
                for (int xx = a; xx < b; ++xx) epd_draw_pixel(xx, top + yy, 0, fb);
        }
        used += height;
        if (paragraph_end) used += height / (heading ? 2 : 3);
        off = next;
    }
}

// 拖拽命中与手柄共享逐行字形遍历，不在交互中排版或访问文件。
// Drag hits and handles share glyph traversal without pagination or file IO during interaction.
static size_t text_geometry(size_t page, EpdRect rect, int x, int y, size_t wanted, EpdRect* out) {
    if (!annotation_rect(page, rect)) return SIZE_MAX;
    if (x < rect.x) x = rect.x;
    if (x >= rect.x + rect.width) x = rect.x + rect.width - 1;
    if (y < rect.y) y = rect.y;
    if (y >= rect.y + rect.height) y = rect.y + rect.height - 1;
    size_t off = s_pages[page], end = page + 1 < s_count ? s_pages[page + 1] : s_len;
    size_t best = SIZE_MAX; int64_t best_distance = INT64_MAX, used = 0;
    while (off < end) {
        size_t next; bool paragraph_end, heading; int px;
        if (!take_line(off, &next, &paragraph_end, &px, &heading)) return SIZE_MAX;
        const blk_t* block = block_at(off); int height = row_height(block, px);
        if (used + height > rect.height) return best;
        if ((!block || (!block->image && !block->image_src)) && s_line[0]) {
            size_t text_end = off + strlen(s_line); int left = rect.x;
            int64_t top = rect.y + used, vertical = y < top ? top - y : y >= top + height ? y - top - height + 1 : 0;
            for (size_t at = off; at < text_end;) {
                size_t n = codepoint_size(s_text + at, text_end - at); if (!n) return SIZE_MAX;
                char glyph[5]; memcpy(glyph, s_text + at, n); glyph[n] = 0;
                int width = ttf_text_width_px(px, glyph);
                EpdRect bounds = {left, (int)top, width, px};
                if (wanted != SIZE_MAX && at == wanted) { if (out) *out = bounds; return at; }
                int64_t horizontal = x < left ? (int64_t)left - x : x >= left + width ? (int64_t)x - left - width + 1 : 0;
                int64_t distance = vertical * ((int64_t)rect.width + 1) + horizontal;
                if (wanted == SIZE_MAX && distance < best_distance) { best_distance = distance; best = at; if (out) *out = bounds; }
                left += width; at += n;
            }
        }
        used += height;
        if (paragraph_end) used += height / (heading ? 2 : 3);
        off = next;
    }
    return wanted == SIZE_MAX ? best : SIZE_MAX;
}
size_t book_layout_text_nearest(size_t page, EpdRect rect, int x, int y) {
    return text_geometry(page, rect, x, y, SIZE_MAX, NULL);
}
bool book_layout_text_bounds(size_t page, EpdRect rect, size_t offset, EpdRect* out) {
    return out && offset < s_len && text_geometry(page, rect, 0, 0, offset, out) != SIZE_MAX;
}

// 方法仅在字体锁内短暂选择独立上下文，返回前始终恢复活跃上下文。
// Methods select a private context only while the font lock is held and always restore the active context before returning.
book_layout_job_t* book_layout_job_create(const char* text, size_t len, const blk_t* blocks, size_t count, EpdRect rect, int px) {
    book_layout_job_t* job = heap_caps_malloc(sizeof(*job), PSRAM_CAPS);
    if (!job) return NULL;
    memset(job, 0, sizeof(*job)); job->state.count_only = true;
    layout_state_t* saved = s_current; s_current = &job->state;
    bool ok = book_layout_begin_blocks(text, len, blocks, count, rect, px);
    s_current = saved;
    if (!ok) { free(job); return NULL; }
    return job;
}
bool book_layout_job_step(book_layout_job_t* job, size_t pages) {
    if (!job || job->failed) return false;
    layout_state_t* saved = s_current; s_current = &job->state;
    bool ok = book_layout_extend(pages);
    s_current = saved;
    if (!ok) job->failed = true;
    return ok;
}
bool book_layout_job_complete(const book_layout_job_t* job) {
    return job && !job->failed && job->state.complete;
}
size_t book_layout_job_page_count(const book_layout_job_t* job) {
    return job && !job->failed && job->state.count ? job->state.count - !job->state.complete : 0;
}
bool book_layout_job_page_offset(const book_layout_job_t* job, size_t page, size_t* out) {
    if (!out || page >= book_layout_job_page_count(job)) return false;
    *out = job->state.pages[page]; return true;
}
void book_layout_job_free(book_layout_job_t* job) {
    if (!job) return;
    layout_state_t* saved = s_current; s_current = &job->state;
    book_layout_free(); s_current = saved; free(job);
}

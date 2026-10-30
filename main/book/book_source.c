/* SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 * 中文：TXT 与 EPUB 单实例书源分派，不依赖界面。
 * English: Singleton TXT and EPUB source dispatch without UI dependencies.
 * 冻结：TXT 保留源字节偏移，EPUB 采用累计 spine HTML 字节；兼容纯文本加载，不写文件。
 * Frozen: TXT retains source offsets; EPUB uses cumulative spine HTML bytes; retain plain-text loading and never write files.
 */
#include "book_source_internal.h"
#include "book_epub.h"
#include <stdlib.h>
#include <string.h>
#include <strings.h>
static book_txt_t s_book;
static book_epub_t *s_epub;
esp_err_t book_open(const char *path) {
    book_close();
    if (!path || !*path) return ESP_ERR_INVALID_ARG;
    const char *ext = strrchr(path, '.');
    if (ext && !strcasecmp(ext, ".epub")) return book_epub_open(path, &s_epub);
    if (!ext || strcasecmp(ext, ".txt")) return ESP_ERR_NOT_SUPPORTED;
    esp_err_t err = book_txt_open(&s_book, path);
    if (err != ESP_OK) book_close();
    return err;
}
void book_close(void) {
    book_epub_close(s_epub); s_epub = NULL;
    if (s_book.file) fclose(s_book.file);
    free(s_book.entries); memset(&s_book, 0, sizeof(s_book));
}
size_t book_chapter_count(void) { return s_epub ? book_epub_chapter_count(s_epub) : s_book.count; }
esp_err_t book_chapter_title(size_t i, char *buf, size_t cap) {
    if (s_epub) return book_epub_chapter_title(s_epub, i, buf, cap);
    if (!buf || !cap || i >= s_book.count) return ESP_ERR_INVALID_ARG;
    size_t n = strlen(s_book.entries[i].title);
    if (n >= cap) { buf[0] = 0; return ESP_ERR_INVALID_SIZE; }
    memcpy(buf, s_book.entries[i].title, n + 1); return ESP_OK;
}
esp_err_t book_chapter_load(size_t i, char **utf8, size_t *len) {
    if (!utf8 || !len) return ESP_ERR_INVALID_ARG;
    *utf8 = NULL; *len = 0;
    if (s_epub) {
        html_text_t text = {0}; esp_err_t err = book_epub_load(s_epub, i, &text);
        if (err != ESP_OK) return err;
        *utf8 = text.utf8; *len = text.len; text.utf8 = NULL;
        html_text_free(&text); return ESP_OK;
    }
    if (!s_book.file || i >= s_book.count) return ESP_ERR_INVALID_ARG;
    return book_txt_load(&s_book, i, utf8, len);
}
esp_err_t book_chapter_load_blocks(size_t i, html_text_t *out) {
    if (!out) return ESP_ERR_INVALID_ARG;
    memset(out, 0, sizeof(*out));
    if (s_epub) return book_epub_load(s_epub, i, out);
    return book_chapter_load(i, &out->utf8, &out->len);
}
esp_err_t book_chapter_load_blocks_for_pagination(size_t i, html_text_t *out) {
    if (!out) return ESP_ERR_INVALID_ARG;
    memset(out, 0, sizeof(*out));
    if (s_epub) return book_epub_load_for_pagination(s_epub, i, out);
    return book_chapter_load(i, &out->utf8, &out->len);
}
esp_err_t book_chapter_load_image(size_t i, const char *reference, uint8_t **pixels, uint16_t *width, uint16_t *height) {
    if (!pixels || !width || !height) return ESP_ERR_INVALID_ARG;
    *pixels = NULL; *width = *height = 0;
    if (!s_epub) return ESP_ERR_NOT_SUPPORTED;
    return book_epub_load_image(s_epub, i, reference, pixels, width, height);
}
// 只在输入处理/载入回调中调用；render和预绘工作任务不访问存储。
// Call only from input/loading callbacks; neither render nor the pre-render worker touches storage.
bool book_chapter_prepare_images(size_t chapter, blk_t* blocks, size_t count, size_t first, size_t end) {
    if (!blocks || count > HTML_TEXT_MAX_BLOCKS || first >= end) return false;
    size_t bytes = 0, items = 0;
    bool changed = false;
    for (size_t i = 0; i < count; ++i) if (blocks[i].image) {
        bytes += (size_t)blocks[i].image_width * blocks[i].image_height; ++items;
    }
    for (size_t i = 0; i < count; ++i) {
        blk_t* block = blocks + i;
        if (block->offset < first || block->offset >= end || !block->image_src || block->image || block->image_failed) continue;
        size_t expected = (size_t)block->image_width * block->image_height;
        if (!expected || expected > BOOK_INLINE_IMAGE_CACHE_BYTES) continue;
        // 优先释放页外缓存，当前可见图不会因装入同页其他图片而消失。
        // Evict off-page entries first so loading another illustration never removes an already visible image.
        for (size_t j = 0; j < count && (bytes > BOOK_INLINE_IMAGE_CACHE_BYTES - expected || items >= BOOK_INLINE_IMAGE_CACHE_ITEMS); ++j) {
            blk_t* victim = blocks + j;
            if (!victim->image || (victim->offset >= first && victim->offset < end)) continue;
            bytes -= (size_t)victim->image_width * victim->image_height; --items;
            free(victim->image); victim->image = NULL; changed = true;
        }
        if (bytes > BOOK_INLINE_IMAGE_CACHE_BYTES - expected || items >= BOOK_INLINE_IMAGE_CACHE_ITEMS) continue;
        uint8_t* pixels = NULL; uint16_t width = 0, height = 0;
        esp_err_t err = book_chapter_load_image(chapter, block->image_src, &pixels, &width, &height);
        if (err == ESP_OK && pixels && width == block->image_width && height == block->image_height) {
            block->image = pixels; bytes += expected; ++items; changed = true;
        } else {
            free(pixels); block->image_failed = true; changed = true;
        }
    }
    return changed;
}
uint32_t book_total_bytes(void) { return s_epub ? book_epub_total_bytes(s_epub) : s_book.total; }
uint32_t book_chapter_byte_offset(size_t i) {
    return s_epub ? book_epub_chapter_byte_offset(s_epub, i) : i < s_book.count ? s_book.entries[i].offset : 0;
}
book_kind_t book_kind(void) { return s_epub ? BOOK_KIND_EPUB : BOOK_KIND_TXT; }

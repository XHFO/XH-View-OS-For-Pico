/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 *
 * 中文：借用章节 UTF-8 文本，生成分页并绘制正文。
 * English: Paginate borrowed chapter UTF-8 text and draw its body.
 *
 * 冻结：不释放原文、不刷新屏幕；调用方持有字体绘制互斥锁。
 * Frozen: Never free source text or present the display; caller holds the font draw lock.
 */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "epdiy.h"
#include "html_text.h"

/// 重排借用文本；空文一页，失败清空布局，超过 4096 页返回 false。/ Borrow and paginate; empty text has one page, failure clears layout, over 4096 pages fails.
bool book_layout_build(const char* utf8, size_t len, EpdRect rect, int px);
/// 借用块表；标题字号加8，块间单换行；原文与块表须存活至free。/ Borrow blocks; headings add 8 px, with one newline between blocks; text and blocks must outlive layout.
bool book_layout_build_blocks(const char* utf8, size_t len, const blk_t* blocks, size_t count, EpdRect rect, int px);
/// 初始化并只排前两页；后续由调用方分批推进，原文继续借用。/ Initialize the first two pages; caller advances later batches while retaining source ownership.
bool book_layout_begin_blocks(const char* utf8, size_t len, const blk_t* blocks, size_t count, EpdRect rect, int px);
/// 事务式替换并排到目标字节（或末页）；失败完整保留原布局，不需分配回滚内存。
/// Replace and paginate through a target byte (or final page) transactionally; failure retains the complete old layout without rollback allocation.
bool book_layout_position_blocks(const char* utf8, size_t len, const blk_t* blocks, size_t count,
                                 EpdRect rect, int px, size_t offset, bool last_page);
/// 最多增加指定页数；失败清空布局，调用方必须停止阅读。/ Add at most the requested pages; failure clears layout and requires ending reading.
bool book_layout_extend(size_t pages);
/// 是否完成整章分页。/ Whether the whole chapter has been paginated.
bool book_layout_complete(void);
/// 释放页表，不释放原文。/ Free layout storage, never the borrowed text.
void book_layout_free(void);
/// 返回已完成页数；未建立布局时为零，分批排版时不是整章总页数。/ Return completed pages, zero without a layout; incremental counts are not the chapter total.
size_t book_layout_page_count(void);
/// 使用建立布局时的宽高与字号绘图；不匹配或越界时不绘制。/ Draw with the built dimensions and size; mismatches or invalid pages do nothing.
void book_layout_draw_page(uint8_t* fb, size_t page, EpdRect rect, int px);
/// 命中本页未加载图片占位，返回块索引及可选矩形；未命中为 SIZE_MAX；与绘制使用相同锁。
/// Hit an unloaded image placeholder, returning its block index and optional bounds; SIZE_MAX if absent; use the drawing lock.
size_t book_layout_image_at(size_t page, EpdRect rect, int x, int y, EpdRect* hit);
/// 命中真实排版文字的UTF-8字节起点；图片、段间空白和越界返回SIZE_MAX；调用方持绘制锁。
/// Hit the UTF-8 byte start of laid-out text; images, paragraph gaps and outside points return SIZE_MAX; caller holds the draw lock.
size_t book_layout_text_at(size_t page, EpdRect rect, int x, int y);
/// 拖拽时吸附最近的可见文字，允许坐标位于行尾/段间；图片页返回SIZE_MAX。
/// Snap a drag to the nearest visible glyph, including line ends/gaps; image-only pages return SIZE_MAX.
size_t book_layout_text_nearest(size_t page, EpdRect rect, int x, int y);
/// 返回指定UTF-8字节起点在当前页的字形推进矩形；图片和不可见锚点返回false。
/// Return the glyph-advance rectangle of a UTF-8 byte start on this page; images/off-page anchors return false.
bool book_layout_text_bounds(size_t page, EpdRect rect, size_t offset, EpdRect* out);
/// 对章节原文字节区间逐行画下划线；选区加粗，禁止读取文件；调用方持绘制锁。
/// Underline a chapter source-byte range line by line, thicker for selection, without file IO; caller holds the draw lock.
void book_layout_draw_range(uint8_t* fb, size_t page, EpdRect rect, size_t start, size_t end, bool selected);
/// 查找字节偏移所属页，越界偏移夹到末页。/ Find page containing a byte offset; excessive offsets clamp to the last page.
size_t book_layout_page_for_offset(size_t off);
/// 返回页首偏移；已完成页数索引是待排页起点（完成后为文本末尾），更大索引返回文本长度。
/// Return a page start; index equal to completed count marks the pending page (EOF when complete), larger indexes return text length.
size_t book_layout_page_start_offset(size_t page);

/// 独立计页上下文，借用原文/块表至free；所有方法仍要求调用方持同一字体锁。
/// Independent pagination borrows text/blocks until free; all methods still require the same caller-held font lock.
/// 字体/字重/正文几何改变后须丢弃重建，不跨线程或在render中推进。
/// Recreate after font/weight/body geometry changes; never advance across threads or inside render.
typedef struct book_layout_job book_layout_job_t;
/// 校验并排首两页；失败NULL且活跃阅读布局不变。纯计页不分配章节大小的行缓冲。
/// Validate and paginate the first two pages; NULL on failure, preserving active reading. Counting avoids a chapter-sized line buffer.
book_layout_job_t* book_layout_job_create(const char* utf8, size_t len, const blk_t* blocks, size_t count, EpdRect rect, int px);
/// 最多完成pages页，0不推进；false后job失效但仍可free，其他上下文不变。
/// Complete at most pages additional pages, zero is a no-op; false invalidates this job but still permits free, preserving other contexts.
bool book_layout_job_step(book_layout_job_t* job, size_t pages);
bool book_layout_job_complete(const book_layout_job_t* job);
/// 仅返回真实已完成页数；未完成章节不能当成总页数，NULL/失败job为0。
/// Return real completed pages only; incomplete counts are not chapter totals, NULL/failed jobs return zero.
size_t book_layout_job_page_count(const book_layout_job_t* job);
/// 只接受已完成页的零基编号；成功返回UTF-8起点，失败不写out。
/// Accept only a completed zero-based page; return its UTF-8 start on success, leaving out untouched on failure.
bool book_layout_job_page_offset(const book_layout_job_t* job, size_t page, size_t* out);
void book_layout_job_free(book_layout_job_t* job);

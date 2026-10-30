/* SPDX-License-Identifier: Apache-2.0
 * 真实图书批注的稳定位置；章节内偏移指解析后的UTF-8正文。 / Stable real-book annotation anchors use parsed chapter UTF-8 byte offsets.
 */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "book_store.h"

typedef struct {
    char path[BOOK_STORE_PATH_MAX];
    char title[96];
    uint64_t file_bytes;
    uint32_t chapter,start,end;
    char quote[512];
} ps_book_anchor_t;
typedef struct { uint32_t start,end; } ps_book_range_t;

bool ps_reader_real_highlight(const ps_book_anchor_t *anchor);
bool ps_reader_real_bookmark(const ps_book_anchor_t *anchor);
void ps_reader_real_note(const ps_book_anchor_t *anchor);
void ps_reader_real_records(const char *path,uint64_t file_bytes);
/// 只读RAM；正文渲染期间不访问文件。 / RAM-only query: rendering never reads files.
size_t ps_reader_real_ranges(const char *path,uint64_t file_bytes,uint32_t chapter,
                            ps_book_range_t *ranges,size_t capacity);

/// 点按已划线正文打开对应记录；只命中相同文件与章节，不修改记录。/ Open a tapped annotation for the exact file/chapter, without mutating records.
bool ps_reader_real_open_at(const char *path,uint64_t file_bytes,uint32_t chapter,uint32_t offset);

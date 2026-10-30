/*
 * SPDX-FileCopyrightText: 2026 XH Lab
 * SPDX-License-Identifier: Apache-2.0
 * 中文：实际书籍封面的有界内存及 TF 缩略图缓存。
 * English: Bounded memory and TF thumbnail caching for actual book covers.
 * 冻结：render 查询不做 IO；仅显式 prepare 读取本地书，在书目录自有子目录写缓存；失败不影响阅读。
 * Frozen: Render queries never perform I/O; explicit preparation reads local books and writes only an owned cache directory; failure never blocks reading.
 */
#pragma once
#include <stdbool.h>
#include <stdint.h>

typedef struct {
    const uint8_t *pixels; ///< 借用八位灰度，clear/prepare 后可失效。/ Borrowed 8-bit gray; clear/prepare may invalidate it.
    uint16_t width, height; ///< 保持源比例，最多 128×192。/ Source aspect ratio, at most 128x192.
} book_cover_view_t;

/// 仅内存查询，失败清空输出。/ Memory-only lookup; clear output on failure.
bool book_cover_get(const char *path, book_cover_view_t *out);
/// 内存已记录成功或无可用封面；不访问文件。/ Success or unavailable already known in memory; no file access.
bool book_cover_known(const char *path);
/// 主循环逐本调用；优先磁盘缓存，否则读取 EPUB；返回是否增加了一条内存记录。
/// Prepare one book from the main loop; prefer disk cache, otherwise inspect EPUB; return whether a memory record was added.
bool book_cover_prepare(const char *path);
/// 重扫、媒体切换或独占传输前释放所有内存；不删除用户文件。/ Release memory for rescan/media/transfer changes; never delete user files.
void book_cover_clear(void);

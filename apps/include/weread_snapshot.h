/*
 * SPDX-FileCopyrightText: 2026 XH Lab
 * SPDX-License-Identifier: Apache-2.0
 * 中文：微信读书离线书架与笔记 WRS1 字节协议；不依赖结构体填充。
 * English: Byte-addressed WRS1 offline WeRead shelf and notes, independent of struct padding.
 * 冻结：仅导入用户可合法读取的书架和笔记，不包含正文下载、登录或密钥。
 * Frozen: Import legitimately available shelf and notes only; no body download, login or secrets.
 */
#pragma once

#define WRS_APP_KIND 129u
#define WRS_APP_VERSION 10200u
#define WRS_SCHEMA 1u
#define WRS_BOOK_MAX 64u
#define WRS_CHUNK_MAX 256u
#define WRS_HEADER_BYTES 64u
#define WRS_BOOK_BYTES 256u
#define WRS_INDEX_BYTES (WRS_HEADER_BYTES + WRS_BOOK_MAX * WRS_BOOK_BYTES)
#define WRS_CHUNK_BYTES 32768u
#define WRS_TEXT_BYTES (WRS_CHUNK_BYTES - WRS_HEADER_BYTES)
#define WRS_POSITION_BYTES 128u
#define WRS_INDEX_MAGIC "PWRIDX01"
#define WRS_CHUNK_MAGIC "PWRCHK01"
#define WRS_POSITION_MAGIC "PWRPOS01"

/// 索引头：所有整数小端；36开始的flags和保留区必须为零。
/// Index header: little-endian integers; flags and reserved bytes from 36 must be zero.
enum { WRS_I_SCHEMA=8, WRS_I_BYTES=12, WRS_I_GENERATION=16, WRS_I_BOOKS=20,
       WRS_I_NOTES=24, WRS_I_CREATED=28, WRS_I_RESERVED=36 };
/// 书记录：定长UTF-8字符串NUL结尾，ID仅ASCII字母数字下划线和连字符。
/// Book record: NUL-terminated padded UTF-8; IDs allow ASCII alphanumerics, underscore and hyphen.
enum { WRS_B_ID=0, WRS_B_TITLE=64, WRS_B_AUTHOR=192, WRS_B_NOTES=240,
       WRS_B_CHUNKS=244, WRS_B_RESERVED=248 };
/// 内容块：text_bytes不含终止符；恰好填满32704字节时无需NUL。
/// Content chunk: text_bytes excludes a terminator; all 32704 bytes may be used without a NUL.
enum { WRS_C_SCHEMA=8, WRS_C_BYTES=12, WRS_C_GENERATION=16, WRS_C_BOOK=20,
       WRS_C_INDEX=24, WRS_C_COUNT=28, WRS_C_TEXT=32, WRS_C_RESERVED=36 };
/// 模块独占阅读位置；桌面同步不读写或删除此键。
/// Module-owned reading position; desktop sync must never write or delete this key.
enum { WRS_P_SCHEMA=8, WRS_P_BYTES=12, WRS_P_GENERATION=16, WRS_P_BOOK=20,
       WRS_P_CHUNK=24, WRS_P_OFFSET=28, WRS_P_MODE=32, WRS_P_RESERVED=36,
       WRS_P_ID=40, WRS_P_TAIL=104 };

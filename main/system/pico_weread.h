/*
 * SPDX-FileCopyrightText: 2026 XH Lab
 * SPDX-License-Identifier: Apache-2.0
 * 中文：设备本机微信读书授权与个人笔记同步；密钥不离开设备的NVS。
 * English: On-device WeRead authorization and personal-note sync, keeping credentials in device NVS.
 * 冻结：仅固定官方HTTPS接口，证书验证不可关闭；不下载正文，不覆盖阅读位置或旧代笔记。
 * Frozen: Fixed official HTTPS only with certificate verification; no body download or overwrite of reading
 * positions/old generations.
 */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define PS_WEREAD_KEY_MAX 512u
#define PS_WEREAD_BOOK_MAX 64u

typedef struct {
    char id[64];
    char title[128];
    char author[48];
} ps_weread_book_t;

typedef struct {
    bool configured, verified, wifi, busy;
    char state[12];
    char error[24];
    uint32_t revision, books_count, synced_books, synced_notes, progress, total;
    ps_weread_book_t books[PS_WEREAD_BOOK_MAX];
} ps_weread_status_t;

/// 固定只读查询种类；不开放正文、写回或任意接口。/ Fixed read-only queries; no book bodies, writes or arbitrary APIs.
enum {
    PS_WEREAD_SEARCH = 1, PS_WEREAD_INFO, PS_WEREAD_CHAPTERS, PS_WEREAD_PROGRESS,
    PS_WEREAD_RECOMMEND, PS_WEREAD_SIMILAR, PS_WEREAD_STATS, PS_WEREAD_HIGHLIGHTS, PS_WEREAD_REVIEWS
};
enum { PS_WEREAD_WEEKLY, PS_WEREAD_MONTHLY, PS_WEREAD_ANNUALLY, PS_WEREAD_OVERALL };
typedef struct {
    uint32_t bytes, kind, scope, count, mode, review_type;
    int64_t cursor, synckey, chapter_uid, base_time;
    char book_id[64], keyword[160], session_id[128];
} ps_weread_query_t;
typedef struct {
    uint32_t bytes, request_id, revision, kind, total_bytes;
    bool busy, ready;
    char error[24];
} ps_weread_query_status_t;

/// 空请求仅读状态，非空请求异步提交；失败以out安全错误码说明。/ Read status with NULL, otherwise submit asynchronously; safe failures are in out.
bool ps_weread_query(const ps_weread_query_t *request, ps_weread_query_status_t *out);
/// 仅读匹配ready请求的原始JSON；busy可表示仍在取可选封面，revision变化后可重绘。
/// Read matching ready raw JSON, even while an optional cover keeps busy true; redraw after revision changes.
/// UTF-8解码由流式调用方处理。/ Streaming callers decode UTF-8.
bool ps_weread_query_read(uint32_t request_id, uint32_t offset, void *out, size_t capacity, size_t *read);

/// 初始化本机配置，无网络访问。/ Initialize local configuration without network access.
void ps_weread_init(void);
/// 后台任务状态；不执行网络或文件访问。/ Query background work without network or file I/O.
bool ps_weread_busy(void);
/// 请求取消，工作器会在安全边界停止并保留上次索引。/ Request cancellation at safe boundaries, preserving the previous
/// index.
void ps_weread_cancel(void);
/// 与工作准入共用互斥锁；忙或计数满时失败。每次成功取得一个暂停租约，必须由一次resume释放。
/// Share the job-admission lock; fail while busy or saturated. Pair every successful counted pause lease with one
/// resume. 嵌套租约全部释放后才允许新任务；多余resume不下溢。 Admit new jobs only after every nested lease is released;
/// unmatched resumes never underflow.
bool ps_weread_pause(void);
void ps_weread_resume(void);
/// 获取无密钥状态快照。/ Copy a credential-free status snapshot.
void ps_weread_get_status(ps_weread_status_t *out);
/// 重新同步上次成功选择的书籍，无选择时返回false。/ Resync the last successfully selected books, returning false if
/// none were selected.
bool ps_weread_sync_saved(void);
/// 管理层已鉴权的本机API扩展；不处理的路径返回0。/ Management-authorized API extension; return zero for unhandled
/// routes.
int ps_weread_handle(const char *method, const char *path, const char *type, const void *body, size_t bytes, char *out,
                     size_t capacity);

/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 *
 * 产品书架进入现有真实图书引擎的入口。/ Product shelf entry into the existing real book engine.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "app.h"

/// 仅在 UI 线程排队并复制 TXT/EPUB 路径，随后通过 request_app 进入 app_book。
/// Queue and copy a TXT/EPUB path on the UI thread, then enter app_book through request_app.
/// 接受可用书根内的文件和 /sdcard 根目录直接文件；返回值只表示排队成功，实际解析错误在阅读页显示。
/// Accept files within available book roots and direct files in /sdcard; success means queued, with parsing errors shown in the reader.
bool app_book_open_path(const char* path);
/// Open locally without joining or notifying the paired reader. / 本机独立打开，不联动对端。
bool app_book_open_path_single(const char* path);
/// Path retained for an explicit single-device reading session. / 显式单机阅读保留的路径。
bool app_book_local_only_path(char* out,size_t capacity);
typedef enum { BOOK_GLOBAL_TOOLS, BOOK_GLOBAL_READING, BOOK_GLOBAL_FONT,
    BOOK_GLOBAL_BACK, BOOK_GLOBAL_RECORDS, BOOK_GLOBAL_SELECT, BOOK_GLOBAL_BOOKMARK,
    BOOK_GLOBAL_TOC, BOOK_GLOBAL_HIGHLIGHTS, BOOK_GLOBAL_NOTES, BOOK_GLOBAL_FAVORITE,
    BOOK_GLOBAL_HIGHLIGHT, BOOK_GLOBAL_NOTE, BOOK_GLOBAL_PAGE, BOOK_GLOBAL_BOOKMARKS } app_book_global_action_t;
/// 全局菜单操作真实书；恢复只排队，不提前读文件。/ Global actions target the real book; resume only queues its path.
app_redraw_t app_book_global_action(app_ctx_t* ctx, app_book_global_action_t action);
bool app_book_resume_last(void);
/// 当前真实图书与选区的只读状态，不触发文件访问。/ Read-only real-book/selection state without file access.
bool app_book_global_is_open(void);
bool app_book_global_has_selection(void);
void app_book_global_prepare(void);
bool app_book_power_prepare(void);
/// 外部翻页器仅操作无弹窗、选区或载入中的正文，不改变其他页面。/ External pagers only turn unobstructed, loaded reading pages.
/// 双机接受异步翻页返回DONE；被浮层或连接状态阻止返回NONE。/ Paired asynchronous admission returns DONE; blocked input returns NONE.
app_redraw_t app_book_external_turn(app_ctx_t* ctx, int direction);
/// 只排队稳定批注锚点；首帧载入页推送后才读取，校验源文件大小/章节/UTF-8区间。
/// Queue a stable annotation anchor; read only after presenting loading, validating file size/chapter/UTF-8 range.
bool app_book_open_annotation(const char* path, uint64_t file_bytes, uint32_t chapter, uint32_t start, uint32_t end);

/// 双机主机仲裁后的精确全书页码；不重复发起网络开书。/ Apply an authoritative whole-book page without echoing an open request.
bool app_book_dual_apply(const char *path,const char *font,uint32_t page,uint8_t px,bool reflow,uint32_t revision);
typedef struct {
    bool open, blank, solo, animation_lead;
    uint32_t target, page;
    uint32_t painted_page;
    int8_t turn_dir;
    uint8_t font_px;
    size_t chapter, start, end;
} app_book_dual_status_t;
/// 只读已显示分页，不加载书籍或读取文件。/ Inspect displayed pagination without loading books or reading files.
void app_book_dual_status(app_book_dual_status_t* out);
/// 双机阅读仍在正文/目录页时保持联动；离开到书架或其他应用即暂停对端翻页。
/// Keep pairing active while on the reading page or contents; leaving for shelf/apps pauses the peer.
bool app_book_dual_engaged(void);
bool app_book_dual_annotation(uint8_t type, uint32_t chapter, uint32_t start, uint32_t end, const char* text);
void app_book_dual_pause(void);
extern const app_desc_t app_book;

/// 重连后按最远正文锚点重新定位全书页组。/ Resolve the furthest text anchor to a whole-book spread after reconnect.
bool app_book_dual_reconcile(const char* path);

struct book_progress;
/// 只读尚未批量落盘的正文锚点。/ Read the live text anchor before a batched checkpoint.
bool app_book_progress_snapshot(const char*,uint32_t,struct book_progress*);

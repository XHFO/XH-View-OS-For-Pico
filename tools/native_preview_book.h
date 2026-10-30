/* SPDX-License-Identifier: Apache-2.0
 * 中文：本地预览连接真实图书页面，仅适配宿主平台。
 * English: Connect the actual book page to the local preview with host-only adaptation.
 */
#pragma once
#include "app.h"
#include "ui_gesture.h"

/// 接收规范画布坐标并转换回真实触控器坐标，再运行实际页面回调。
/// Map canonical canvas coordinates to controller coordinates before the real callback.
app_redraw_t native_preview_book_gesture(app_ctx_t *ctx, const ui_gesture_event_t *event);
/// 向预览测试提供只读页内状态，不绕过真实事件处理。
/// Expose read-only state to preview checks without bypassing actual events.
const char *native_preview_book_path(void);
const char *native_preview_book_kind(void);
const char *native_preview_book_view(void);
size_t native_preview_book_page(void);
size_t native_preview_book_chapter(void);
size_t native_preview_book_pages(void);
bool native_preview_book_jump_open(void);
bool native_preview_book_tools_open(void);
bool native_preview_book_selection_active(void);


size_t native_preview_book_inline_images(void);
size_t native_preview_book_image_bytes(void);

uint32_t native_preview_dual_target(void);
unsigned native_preview_book_px(void);
uint32_t native_preview_dual_page(void);
bool native_preview_dual_blank(void);
size_t native_preview_page_offset(bool end);

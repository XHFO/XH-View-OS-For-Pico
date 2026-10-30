/* SPDX-License-Identifier: Apache-2.0
 * 中文：编辑器触摸边沿队列；同步刷屏期间保留短按，切页/转向/电源交接时清空。
 * English: Editor touch-edge buffering across synchronous display work; flush at page, rotation and power boundaries.
 */
#pragma once
#include "app.h"

/// 只在文字编辑器启用；停用等待正在进行的读取结束。/ Enable only in the text editor; disabling waits for an in-flight read.
bool app_touch_buffer_enable(cst836u_handle_t tp, bool enabled, int rotation);
/// 按原始顺序返回采样；空队列复用最后状态。/ Consume samples in capture order; an empty queue retains the last state.
esp_err_t app_touch_buffer_read(cst836u_touch_t *touch, int64_t *sample_ms, bool *pending);

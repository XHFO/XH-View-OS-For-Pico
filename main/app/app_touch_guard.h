/* SPDX-License-Identifier: Apache-2.0
 * 双机屏幕贴合的电容误触抑制。/ Capacitive ghost-touch suppression for face-to-face dual screens.
 */
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "cst836u.h"

#ifdef __cplusplus
extern "C" {
#endif

/// 开始新的会话或关闭防误触时清空统计。/ Clear statistics when a session starts or guard is disabled.
void app_touch_guard_reset(void);

/// 送入一帧原始触摸；返回 true 时本帧必须丢弃（已改写为空触点）。
/// Feed one raw sample; when true this frame is suppressed (the touch is rewritten to empty).
bool app_touch_guard_filter(cst836u_touch_t *touch, int64_t now_ms, bool dual, bool enabled);

/// 当前是否处于贴合误触屏蔽窗口。/ Whether the contact-guard window is currently blocking input.
bool app_touch_guard_blocked(void);

/// 取一次状态变化事件：1=开始屏蔽，2=恢复触摸，0=无。用于一次性提示。
/// Take a one-shot transition: 1=blocked, 2=released, 0=none. Used for a single toast.
int app_touch_guard_take_event(void);

#ifdef __cplusplus
}
#endif

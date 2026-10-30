/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 *
 * 中文：由主循环喂入触摸快照的纯状态识别器，不解释页面动作。
 * English: Pure state recognizer fed by loop touch snapshots; no page actions.
 *
 * 冻结：翻页不做按下反馈；阈值来自 ui_kit.h；短拖取消以免误翻。
 * 用户需要长按拖选与直接拖动进度；可选连续模式增加移动/释放，认领后禁止生成翻页或轻点。
 * Frozen: No press decoration for page turns; thresholds live in ui_kit.h; short drags cancel to avoid accidental turns.
 * User-requested text selection and direct progress dragging opt into move/release events; captured contacts never turn pages or tap.
 */
#pragma once
#include "app.h"

typedef enum {
    UI_GESTURE_PRESS, ///< 按下 / Press
    UI_GESTURE_LONG_PRESS, ///< 长按一次 / Single long press
    UI_GESTURE_TAP, ///< 原点轻点 / Tap at origin
    UI_GESTURE_SWIPE_L, ///< 左滑 / Swipe left
    UI_GESTURE_SWIPE_R, ///< 右滑 / Swipe right
    UI_GESTURE_SWIPE_U, ///< 上滑 / Swipe up
    UI_GESTURE_SWIPE_D, ///< 下滑 / Swipe down
    UI_GESTURE_CANCEL, ///< 取消 / Cancel
    UI_GESTURE_MOVE, ///< 连续模式坐标变化 / Coordinate change in continuous mode
    UI_GESTURE_RELEASE, ///< 连续模式已认领接触的抬手 / Captured contact released in continuous mode
} ui_gesture_type_t;

typedef struct ui_gesture_event_s {
    ui_gesture_type_t type; ///< 事件类型 / Event type
    uint16_t x0, y0; ///< 按下原点，也是 TAP 的逻辑坐标 / Press origin, also the logical TAP position
    uint16_t x, y; ///< 当前或释放位置，供控件落外取消 / Current or release position for outside-control cancellation
    int64_t hold_ms; ///< 非负持续时间 / Nonnegative duration
} ui_gesture_event_t;

typedef struct {
    bool active; ///< 正在跟踪 / Tracking a contact
    uint16_t x0, y0; ///< 起点 / Origin
    uint16_t x, y; ///< 最近有效坐标 / Last valid coordinates
    int64_t t0_ms; ///< 起始时刻 / Start time
    int64_t last_ms; ///< 最近时刻，倒退时取消 / Last time, cancel on clock regression
    bool moved; ///< 曾超出容差 / Has exceeded movement slop
    bool long_fired; ///< 已发出长按 / Long press emitted
    bool continuous; ///< 当前序列启用连续事件 / Continuous events enabled for this contact
    bool captured; ///< 页面已认领，抬手不再识别轻点或滑动 / Captured; release cannot become a tap or swipe
} ui_gesture_t;

/// 清除状态；进出页面时调用。/ Clear state when entering or leaving a page.
void ui_gesture_reset(ui_gesture_t *gesture);
/// 每次最多一个事件；consumed、多点或缺失快照取消活动序列。
/// Emit at most one event; consumed, multitouch or a missing snapshot cancels an active sequence.
bool ui_gesture_feed(ui_gesture_t *gesture, const app_ctx_t *ctx, ui_gesture_event_t *out);
/// 连续模式在坐标变化时产生 MOVE；长按自动认领，未认领的轻点和滑动规则不变。
/// Continuous mode emits MOVE on coordinate changes; long press captures automatically, and uncaptured taps/swipes retain their rules.
bool ui_gesture_feed_continuous(ui_gesture_t *gesture, const app_ctx_t *ctx, bool continuous, ui_gesture_event_t *out);
/// 页面回调后立即认领活动连续序列，保证最终仅发 RELEASE。/ Capture an active continuous contact immediately after its page callback; release then emits only RELEASE.
void ui_gesture_capture(ui_gesture_t *gesture);

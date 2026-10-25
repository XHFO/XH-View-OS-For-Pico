/*
 * SPDX-License-Identifier: Apache-2.0
 * 列表滑动在松手合成手势之前就前进一步。
 * Advance a list one step before the release-synthesized swipe.
 *
 * 根因：ui_gesture 只在抬手后发 SWIPE，且行程要达到 UI_SWIPE_MIN_PX（64）。
 * 设置列表、应用集和家庭工具包只处理这个合成事件，所以手指已经移动时画面仍不动。
 * 短拖还会被取消，既不翻页也不点击。贪吃蛇已在 MOVE 上用约 34px 提前响应。
 * Root cause: ui_gesture emits SWIPE only after lift, and only after UI_SWIPE_MIN_PX (64).
 * Settings lists, the app grid and the home-link package wait for that event, so the
 * screen stays still while the finger is already moving. Short drags are cancelled.
 * Snake already commits on MOVE around 34px.
 *
 * 这里在主轴明确且超过 PS_SCROLL_ARM_PX 时提交一次既有翻页量，并吃掉随后的合成
 * SWIPE/TAP，避免同一次接触翻两页。无按下的裸 SWIPE（方向键）仍只翻一页。
 * Commit the existing page quantum once the axis is clear and past PS_SCROLL_ARM_PX,
 * then swallow the synthesized SWIPE/TAP so one contact cannot turn twice.
 * A bare SWIPE with no press (direction keys) still turns exactly once.
 */
#pragma once
#include <stdbool.h>

#define PS_SCROLL_ARM_PX 36
enum { PS_SCROLL_AXIS_X = 1, PS_SCROLL_AXIS_Y = 2, PS_SCROLL_AXIS_BOTH = 3 };

typedef struct {
    bool tracking;
    bool armed;
    int x0, y0;
} ps_scroll_track_t;

static inline void ps_scroll_disarm(ps_scroll_track_t *track) {
    if (track) track->armed = false;
}

static inline int ps_scroll_abs(int value) { return value < 0 ? -value : value; }

/// 返回 +1 下一页（左/上）、-1 上一页（右/下），或 0。consume 为真时调用方应吃掉该事件。
/// Return +1 next (left/up), -1 previous (right/down), or 0. When consume is set, the caller should swallow the event.
static inline int ps_scroll_step(ps_scroll_track_t *track, int type, int x0, int y0, int x, int y, int axis, bool *consume) {
    if (consume) *consume = false;
    if (!track) return 0;
    /* PRESS 0, MOVE 1, RELEASE 2, TAP 3, LONG 4, SWIPE_L 5 .. SWIPE_D 8, CANCEL 9. Matches PGA_/PS_. */
    if (type == 0) {
        track->tracking = true;
        track->armed = false;
        track->x0 = x;
        track->y0 = y;
        return 0;
    }
    if (type == 9) {
        *track = (ps_scroll_track_t){0};
        return 0;
    }
    if (type == 1 && !track->armed) {
        int ox = track->tracking ? track->x0 : x0;
        int oy = track->tracking ? track->y0 : y0;
        int dx = x - ox, dy = y - oy;
        int ax = ps_scroll_abs(dx), ay = ps_scroll_abs(dy);
        bool horizontal = axis == PS_SCROLL_AXIS_X || (axis == PS_SCROLL_AXIS_BOTH && ax >= ay);
        bool vertical = axis == PS_SCROLL_AXIS_Y || (axis == PS_SCROLL_AXIS_BOTH && ay > ax);
        int along = horizontal ? ax : vertical ? ay : 0;
        int cross = horizontal ? ay : ax;
        if (along < PS_SCROLL_ARM_PX || along * 4 < cross * 5) return 0;
        track->armed = true;
        track->tracking = true;
        track->x0 = ox;
        track->y0 = oy;
        if (consume) *consume = true;
        if (horizontal) return dx < 0 ? 1 : -1;
        return dy < 0 ? 1 : -1;
    }
    if (type == 2) {
        track->tracking = false;
        return 0;
    }
    if (track->armed && (type == 3 || type == 4 || (type >= 5 && type <= 8))) {
        int travel = ps_scroll_abs(x - x0) + ps_scroll_abs(y - y0);
        bool same = x0 == track->x0 && y0 == track->y0;
        track->armed = false;
        /* 只吃掉同一次接触的合成事件。方向键裸滑动原点不同（或位移为 0），仍然翻页。
         * Swallow only the synthesized event from this contact. A direction-key swipe has a different origin (or zero travel) and still turns. */
        if (same && travel >= PS_SCROLL_ARM_PX) {
            if (consume) *consume = true;
            return 0;
        }
    }
    if (type == 5 && axis != PS_SCROLL_AXIS_Y) return 1;
    if (type == 6 && axis != PS_SCROLL_AXIS_Y) return -1;
    if (type == 7 && axis != PS_SCROLL_AXIS_X) return 1;
    if (type == 8 && axis != PS_SCROLL_AXIS_X) return -1;
    return 0;
}

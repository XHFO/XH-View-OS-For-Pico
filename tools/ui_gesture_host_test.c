/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 * 中文：真实识别器的时序与边界测试。
 * English: Timing and boundary tests for the actual recognizer.
 * 冻结：仅用于主机测试。/ Frozen: Host tests only.
 */
#include "ui_gesture.h"
#include "ui_kit.h"
#include <assert.h>
#include <limits.h>
#include <stdio.h>
static ui_gesture_t g;
static ui_gesture_event_t event;
static cst836u_touch_t touch;
static app_ctx_t ctx;
static bool continuous;
static bool feed(int x, int y, int64_t time, bool down, bool press, bool release, bool consumed) {
    touch = (cst836u_touch_t){ .x = x, .y = y, .touched = down, .count = down ? 1 : 0 };
    ctx = (app_ctx_t){ .touch = &touch, .now_ms = time, .pressed = press, .released = release, .consumed = consumed };
    return continuous ? ui_gesture_feed_continuous(&g, &ctx, true, &event)
                      : ui_gesture_feed(&g, &ctx, &event);
}
static void press(int x, int y, int64_t time) {
    ui_gesture_reset(&g);
    assert(feed(x, y, time, true, true, false, false));
    assert(event.type == UI_GESTURE_PRESS && event.hold_ms == 0);
}
int main(void) {
    _Static_assert(UI_GESTURE_CANCEL == 7 && UI_GESTURE_MOVE == 8 && UI_GESTURE_RELEASE == 9, "Keep existing event numbers stable");
    press(200, 300, 100);
    assert(!feed(200, 300, 100, true, true, false, false));
    assert(feed(210, 310, 200, false, false, true, false));
    assert(event.type == UI_GESTURE_TAP && event.x0 == 200 && event.y0 == 300);
    assert(event.x == 210 && event.y == 310 && event.hold_ms == 100);
    assert(!feed(210, 310, 200, false, false, true, false));
    press(100, 100, 0);
    assert(!feed(124, 100, 499, true, false, false, false));
    assert(feed(124, 100, 500, true, false, false, false));
    assert(event.type == UI_GESTURE_LONG_PRESS);
    assert(!feed(124, 100, 900, true, false, false, false));
    assert(feed(124, 100, 901, false, false, true, false));
    assert(event.type == UI_GESTURE_CANCEL);
    const int dx[] = {-64, 64, 0, 0}, dy[] = {0, 0, -64, 64};
    const ui_gesture_type_t kinds[] = {UI_GESTURE_SWIPE_L, UI_GESTURE_SWIPE_R, UI_GESTURE_SWIPE_U, UI_GESTURE_SWIPE_D};
    for (int i = 0; i < 4; ++i) {
        press(200, 300, 100);
        assert(!feed(200 + dx[i], 300 + dy[i], 150, true, false, false, false));
        assert(feed(200 + dx[i], 300 + dy[i], 200, false, false, true, false));
        assert(event.type == kinds[i]);
    }
    press(100, 100, 0);
    assert(!feed(125, 100, 500, true, false, false, false));
    assert(feed(100, 100, 600, false, false, true, false));
    assert(event.type == UI_GESTURE_CANCEL);
    press(100, 100, 0);
    assert(feed(220, 220, 100, false, false, true, false)); assert(event.type == UI_GESTURE_CANCEL);
    press(100, 100, 0);
    assert(feed(163, 100, 100, false, false, true, false)); assert(event.type == UI_GESTURE_CANCEL);
    press(100, 100, 0);
    assert(feed(100, 163, 100, false, false, true, false)); assert(event.type == UI_GESTURE_CANCEL);
    press(200, 300, 0);
    assert(!feed(136, 300, 500, true, false, false, false));
    assert(feed(136, 300, 501, false, false, true, false)); assert(event.type == UI_GESTURE_SWIPE_L);
    press(100, 100, 0);
    assert(!feed(117, 117, 500, true, false, false, false));
    assert(feed(117, 117, 600, false, false, true, false)); assert(event.type == UI_GESTURE_CANCEL);
    press(100, 100, 0);
    assert(feed(100, 100, 10, true, false, false, true)); assert(event.type == UI_GESTURE_CANCEL);
    assert(!feed(100, 100, 20, false, false, true, false));
    press(0, 0, INT64_MAX - 500);
    assert(feed(0, 0, INT64_MAX, true, false, false, false)); assert(event.type == UI_GESTURE_LONG_PRESS);
    press(65535, 65535, 100);
    assert(feed(0, 65535, 200, false, false, true, false)); assert(event.type == UI_GESTURE_SWIPE_L);
    press(10, 10, 100);
    assert(feed(10, 10, 99, true, false, false, false)); assert(event.type == UI_GESTURE_CANCEL);
    press(10, 10, 100); ctx.touch = NULL;
    assert(ui_gesture_feed(&g, &ctx, &event)); assert(event.type == UI_GESTURE_CANCEL);
    press(10, 10, 100); touch.count = 2; ctx.pressed = false;
    assert(ui_gesture_feed(&g, &ctx, &event)); assert(event.type == UI_GESTURE_CANCEL);
    ui_gesture_reset(&g);
    assert(!feed(10, 10, 100, true, true, false, true));
    assert(feed(10, 10, 100, true, true, false, false));
    assert(feed(10, 10, INT64_MIN, true, false, false, false));
    assert(event.type == UI_GESTURE_CANCEL && event.hold_ms == 0);
    assert(!ui_gesture_feed(NULL, &ctx, &event));
    // 长按后移动持续保留起点；抬手只有一次 RELEASE，即使释放点足够构成翻页。
    // A long-press drag retains the origin and releases exactly once, even beyond swipe distance.
    continuous = true;press(200, 300, 0);
    assert(!feed(200, 300, 499, true, false, false, false));
    assert(feed(200, 300, 500, true, false, false, false) && event.type == UI_GESTURE_LONG_PRESS && g.captured);
    assert(feed(300, 350, 501, true, false, false, false) && event.type == UI_GESTURE_MOVE);
    assert(event.x0 == 200 && event.y0 == 300 && event.x == 300 && event.y == 350);
    assert(!feed(300, 350, 502, true, false, false, false));
    assert(feed(400, 360, 503, false, false, true, false) && event.type == UI_GESTURE_RELEASE && event.x == 400);
    assert(!g.active && !feed(400, 360, 504, false, false, true, false));
    // 未认领的连续事件保留普通左右滑与轻点，页面可以忽略长按前的 MOVE。
    // Uncaptured contacts retain normal swipes/taps; pages may ignore moves before long press.
    for (int i = 0; i < 4; ++i) {
        press(200, 300, 0);
        assert(feed(200 + dx[i], 300 + dy[i], 100, true, false, false, false) && event.type == UI_GESTURE_MOVE);
        assert(feed(200 + dx[i], 300 + dy[i], 200, false, false, true, false) && event.type == kinds[i]);
    }
    press(100, 100, 0);
    assert(feed(105, 100, 20, true, false, false, false) && event.type == UI_GESTURE_MOVE);
    assert(feed(105, 100, 30, false, false, true, false) && event.type == UI_GESTURE_TAP);
    // 轨道按下即可认领；保持不生成长按，短拖同样以 RELEASE 提交。
    // A progress track can capture on press; holding produces no long press and short drags release normally.
    press(100, 100, 0);ui_gesture_capture(&g);assert(g.captured);
    assert(!feed(100, 100, 600, true, false, false, false));
    assert(feed(110, 100, 610, true, false, false, false) && event.type == UI_GESTURE_MOVE);
    assert(feed(110, 100, 620, false, false, true, false) && event.type == UI_GESTURE_RELEASE);
    // 认领不能让错误、多点、时钟倒退或没有释放沿的触摸丢失变成提交。
    // Capture never turns errors, multitouch, clock regression or a lost unannounced contact into a commit.
    for (int failure = 0; failure < 6; ++failure) {
        press(100, 100, 100);ui_gesture_capture(&g);
        ctx.pressed = false;ctx.now_ms = 200;
        if (failure == 0) ctx.consumed = true;
        if (failure == 1) ctx.touch = NULL;
        if (failure == 2) touch.count = 2;
        if (failure == 3) ctx.now_ms = 99;
        if (failure == 4) touch.touched = false;
        bool changed = failure == 5 ? ui_gesture_feed(&g, &ctx, &event)
                                    : ui_gesture_feed_continuous(&g, &ctx, true, &event);
        assert(changed && event.type == UI_GESTURE_CANCEL && !g.active);
    }
    // 移动时才认领也必须生效；旧模式认领不改变原有滑动语义。
    // Capture on move is immediate; legacy capture requests cannot alter swipe semantics.
    press(100, 100, 0);assert(feed(180, 100, 100, true, false, false, false));
    ui_gesture_capture(&g);assert(feed(180, 100, 200, false, false, true, false) && event.type == UI_GESTURE_RELEASE);
    continuous = false;press(100, 100, 0);ui_gesture_capture(&g);assert(!g.captured);
    assert(feed(180, 100, 200, false, false, true, false) && event.type == UI_GESTURE_SWIPE_R);
    ui_gesture_capture(NULL);
    puts("gesture host tests passed");
}

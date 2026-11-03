/* Host check: paired ghost touches are suppressed, deliberate taps pass. */
#include "app_touch_guard.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static cst836u_touch_t sample(bool touched, int x, int y, int second_x, int second_y) {
    cst836u_touch_t t = {.touched = touched, .count = touched ? (second_x >= 0 ? 2 : 1) : 0,
                         .x = (uint16_t)x, .y = (uint16_t)y};
    if (touched) {
        t.points[0] = (cst836u_point_t){.active = true, .x = (uint16_t)x, .y = (uint16_t)y};
        if (second_x >= 0)
            t.points[1] = (cst836u_point_t){.active = true, .x = (uint16_t)second_x, .y = (uint16_t)second_y};
    }
    return t;
}

int main(void) {
    app_touch_guard_reset();
    int64_t now = 1000;
    // Normal single-finger taps while unfolded never trip the guard.
    for (int i = 0; i < 8; i++) {
        cst836u_touch_t t = sample(true, 340, 600, -1, -1);
        assert(!app_touch_guard_filter(&t, now, true, true, false));
        now += 90;
        t = sample(false, 0, 0, -1, -1);
        assert(!app_touch_guard_filter(&t, now, true, true, false));
        now += 120;
    }
    // In face-to-face contact every screen touch is suppressed and stays latched.
    app_touch_guard_reset();
    cst836u_touch_t tap = sample(true, 340, 600, -1, -1);
    assert(app_touch_guard_filter(&tap, now, true, true, true));
    assert(app_touch_guard_blocked() && !tap.touched);
    assert(app_touch_guard_take_event() == 1);
    // The bottom keys still navigate but must not clear a contact latch.
    cst836u_touch_t key = sample(true, 240, 1300, -1, -1);
    assert(!app_touch_guard_filter(&key, now + 20, true, true, true));
    assert(app_touch_guard_blocked());
    // A persistent touch, quiet gap, and a central hold cannot release contact mode.
    for (int i = 0; i < 10; i++) {
        cst836u_touch_t t = sample(true, 30, 1200, -1, -1);
        assert(app_touch_guard_filter(&t, now, true, true, true));
        now += 30;
    }
    cst836u_touch_t empty = sample(false, 0, 0, -1, -1);
    assert(app_touch_guard_filter(&empty, now, true, true, true));
    now += 600;
    empty = sample(false, 0, 0, -1, -1);
    assert(app_touch_guard_filter(&empty, now, true, true, true));
    assert(app_touch_guard_blocked());
    for (int i = 0; i < 30; i++) {
        cst836u_touch_t hold = sample(true, 342, 608, -1, -1);
        assert(app_touch_guard_filter(&hold, now, true, true, true));
        now += 35;
    }
    assert(app_touch_guard_blocked());
    // Only a stable unfolded contact=false ends the latch.
    empty = sample(false, 0, 0, -1, -1);
    assert(!app_touch_guard_filter(&empty, now, true, true, false));
    assert(!app_touch_guard_blocked());
    assert(app_touch_guard_take_event() == 2);
    // Unfolded pose must never filter normal sliding, even with noisy multi-point samples.
    app_touch_guard_reset();
    for (int i = 0; i < 20; i++) {
        cst836u_touch_t slide = sample(true, 80 + i * 20, 600, 640, 1100);
        assert(!app_touch_guard_filter(&slide, now, true, true, false));
        now += 20;
    }
    assert(!app_touch_guard_blocked());
    // Disabled or non-dual sessions bypass the filter.
    app_touch_guard_reset();
    cst836u_touch_t t = sample(true, 40, 100, 640, 1100);
    assert(!app_touch_guard_filter(&t, now, false, true, true));
    assert(!app_touch_guard_filter(&t, now, true, false, true));
    puts("PASS: paired ghost-touch guard latches through contact motion/quiet and releases only when unfolded");
    return 0;
}

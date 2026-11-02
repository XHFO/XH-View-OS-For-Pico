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
    // Normal single-finger taps in the middle never trip the guard.
    for (int i = 0; i < 8; i++) {
        cst836u_touch_t t = sample(true, 340, 600, -1, -1);
        assert(!app_touch_guard_filter(&t, now, true, true));
        now += 90;
        t = sample(false, 0, 0, -1, -1);
        assert(!app_touch_guard_filter(&t, now, true, true));
        now += 120;
    }
    // Opposed two-point contacts with large jumps are the face-to-face signature.
    bool blocked = false;
    for (int i = 0; i < 8 && !blocked; i++) {
        cst836u_touch_t t = sample(true, 40, 100, 640, 1100);
        blocked = app_touch_guard_filter(&t, now, true, true);
        if (!blocked) {
            assert(t.touched);
            now += 30;
        } else {
            assert(!t.touched && t.count == 0);
            assert(app_touch_guard_blocked());
            assert(app_touch_guard_take_event() == 1);
        }
    }
    assert(blocked);
    // A persistent contact is ignored until it leaves and stays quiet for 500 ms.
    for (int i = 0; i < 10; i++) {
        cst836u_touch_t t = sample(true, 30, 1200, -1, -1);
        assert(app_touch_guard_filter(&t, now, true, true));
        now += 30;
    }
    cst836u_touch_t empty = sample(false, 0, 0, -1, -1);
    assert(app_touch_guard_filter(&empty, now, true, true));
    now += 520;
    empty = sample(false, 0, 0, -1, -1);
    assert(!app_touch_guard_filter(&empty, now, true, true));
    assert(!app_touch_guard_blocked());
    assert(app_touch_guard_take_event() == 2);
    // The three bottom keys are physical, always work, and clear the guard.
    app_touch_guard_reset();
    now += 1000;
    for (int i = 0; i < 8 && !app_touch_guard_blocked(); i++) {
        cst836u_touch_t ghost = sample(true, 40, 100, 640, 1100);
        (void)app_touch_guard_filter(&ghost, now, true, true);
        now += 30;
    }
    assert(app_touch_guard_blocked());
    cst836u_touch_t key = sample(true, 240, 1300, -1, -1);
    assert(!app_touch_guard_filter(&key, now, true, true));
    assert(!app_touch_guard_blocked());
    now += 40;
    // A deliberate central hold recovers and then waits for lift; the recovery touch must not tap.
    app_touch_guard_reset();
    now += 1000;
    for (int i = 0; i < 8 && !app_touch_guard_blocked(); i++) {
        cst836u_touch_t ghost = sample(true, 40, 100, 640, 1100);
        (void)app_touch_guard_filter(&ghost, now, true, true);
        now += 30;
    }
    assert(app_touch_guard_blocked());
    (void)app_touch_guard_take_event();
    bool recovered = false;
    for (int i = 0; i < 30 && !recovered; i++) {
        cst836u_touch_t hold = sample(true, 342, 608, -1, -1);
        (void)app_touch_guard_filter(&hold, now, true, true);
        recovered = !app_touch_guard_blocked();
        now += 35;
    }
    assert(recovered && !app_touch_guard_blocked());
    // While the recovery finger is still down, input stays suppressed; lifting arms the pass-through.
    cst836u_touch_t hold = sample(true, 342, 608, -1, -1);
    (void)app_touch_guard_filter(&hold, now, true, true);
    now += 40;
    cst836u_touch_t lift = sample(false, 0, 0, -1, -1);
    assert(!app_touch_guard_filter(&lift, now, true, true));
    cst836u_touch_t normal = sample(true, 340, 600, -1, -1);
    assert(!app_touch_guard_filter(&normal, now + 50, true, true));
    // Disabled or non-dual sessions bypass the filter.
    app_touch_guard_reset();
    cst836u_touch_t t = sample(true, 40, 100, 640, 1100);
    assert(!app_touch_guard_filter(&t, now, false, true));
    assert(!app_touch_guard_filter(&t, now, true, false));
    puts("PASS: paired ghost-touch guard blocks opposed contacts, releases after quiet, bypasses normal taps");
    return 0;
}

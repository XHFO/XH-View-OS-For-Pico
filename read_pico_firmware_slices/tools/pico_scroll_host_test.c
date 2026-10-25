/* 滑动提前提交：未到阈值不翻页，过阈值只翻一次，合成滑动不重复，方向键裸滑动仍有效。
 * Early scroll commit: no step before the arm distance, one step after it, no duplicate synthesized swipe, bare key swipes still work.
 */
#include <assert.h>
#include <stdio.h>
#include "../main/system/pico_scroll.h"

enum { PRESS, MOVE, RELEASE, TAP, LONG, SWIPE_L, SWIPE_R, SWIPE_U, SWIPE_D, CANCEL };

static int step(ps_scroll_track_t *track, int type, int x0, int y0, int x, int y, int axis, bool *consume) {
    return ps_scroll_step(track, type, x0, y0, x, y, axis, consume);
}

int main(void) {
    ps_scroll_track_t track = {0};
    bool consume = false;
    assert(step(&track, PRESS, 0, 0, 100, 80, PS_SCROLL_AXIS_X, &consume) == 0 && !consume);
    assert(step(&track, MOVE, 100, 80, 80, 80, PS_SCROLL_AXIS_X, &consume) == 0 && !consume);
    assert(step(&track, MOVE, 100, 80, 60, 88, PS_SCROLL_AXIS_X, &consume) == 1 && consume);
    consume = false;
    assert(step(&track, MOVE, 100, 80, 10, 80, PS_SCROLL_AXIS_X, &consume) == 0 && !consume);
    assert(step(&track, RELEASE, 100, 80, 10, 80, PS_SCROLL_AXIS_X, &consume) == 0 && !consume);
    assert(step(&track, SWIPE_L, 100, 80, 10, 80, PS_SCROLL_AXIS_X, &consume) == 0 && consume);
    assert(step(&track, TAP, 100, 80, 10, 80, PS_SCROLL_AXIS_X, &consume) == 0 && !consume);

    track = (ps_scroll_track_t){0};
    assert(step(&track, SWIPE_R, 0, 0, 0, 0, PS_SCROLL_AXIS_X, &consume) == -1 && !consume);
    assert(step(&track, SWIPE_L, 0, 0, 0, 0, PS_SCROLL_AXIS_Y, &consume) == 0);
    assert(step(&track, SWIPE_U, 0, 0, 0, 0, PS_SCROLL_AXIS_Y, &consume) == 1);
    assert(step(&track, SWIPE_D, 0, 0, 0, 0, PS_SCROLL_AXIS_X, &consume) == 0);

    track = (ps_scroll_track_t){0};
    assert(step(&track, MOVE, 40, 40, 70, 70, PS_SCROLL_AXIS_BOTH, &consume) == 0);
    assert(step(&track, MOVE, 40, 80, 40, 40, PS_SCROLL_AXIS_BOTH, &consume) == 1);
    ps_scroll_disarm(&track);
    assert(!track.armed);
    assert(step(&track, SWIPE_U, 40, 40, 40, 80, PS_SCROLL_AXIS_BOTH, &consume) == 1 && !consume);

    track = (ps_scroll_track_t){0};
    assert(step(&track, MOVE, 10, 10, 50, 12, PS_SCROLL_AXIS_X, &consume) == -1);
    assert(step(&track, SWIPE_L, 0, 0, 0, 0, PS_SCROLL_AXIS_X, &consume) == 1 && !consume);
    assert(step(&track, CANCEL, 10, 10, 50, 12, PS_SCROLL_AXIS_X, &consume) == 0 && !track.armed && !track.tracking);
    assert(step(&track, SWIPE_L, 10, 10, 50, 12, PS_SCROLL_AXIS_X, &consume) == 1);

    puts("scroll host: early arm, one step per contact, swallowed synthesis, bare keys, axis filter PASS");
    return 0;
}

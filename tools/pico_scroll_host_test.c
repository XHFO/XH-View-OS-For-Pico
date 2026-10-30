/* 滑动提前提交：未到阈值不翻页，过阈值只翻一次，合成滑动不重复，方向键裸滑动仍有效。
 * Early scroll commit: no step before the arm distance, one step after it, no duplicate synthesized swipe, bare key swipes still work.
 */
#include <assert.h>
#include <stdio.h>
#ifndef PICO_SCROLL_HEADER
#define PICO_SCROLL_HEADER "../main/system/pico_scroll.h"
#endif
#include PICO_SCROLL_HEADER

enum { PRESS, MOVE, RELEASE, TAP, LONG, SWIPE_L, SWIPE_R, SWIPE_U, SWIPE_D, CANCEL };

static int step(ps_scroll_track_t *track, int type, int x0, int y0, int x, int y, int axis, bool *consume) {
    return ps_scroll_step(track, type, x0, y0, x, y, axis, consume);
}

static void imported_cases(void) {
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

    puts("PASS imported: early arm, one step per contact, swallowed synthesis, bare keys, axis filter");
}

// 模拟分页后仍接收点击的列表，检查往返拖动不会打开新页面中的条目。
// Model a paged list that accepts taps, ensuring an out-and-back drag never opens a newly shown item.
typedef struct {
    ps_scroll_track_t track;
    int pages, taps;
} list_t;

static void feed(list_t *list, int type, int x0, int y0, int x, int y, int axis) {
    bool consume = false;
    int delta = step(&list->track, type, x0, y0, x, y, axis, &consume);
    if (delta) list->pages += delta;
    else if (!consume && type == TAP) list->taps++;
}

static void returned_tap(void) {
    const int axes[] = {PS_SCROLL_AXIS_X, PS_SCROLL_AXIS_Y, PS_SCROLL_AXIS_BOTH};
    for (unsigned i = 0; i < sizeof(axes) / sizeof(axes[0]); i++) {
        int axis = axes[i];
        for (int jitter = 0; jitter <= 8; jitter += 8) {
            list_t list = {0};
            int ex = axis == PS_SCROLL_AXIS_Y ? 100 : 50;
            int ey = axis == PS_SCROLL_AXIS_Y ? 50 : 100;
            feed(&list, PRESS, 100, 100, 100, 100, axis);
            feed(&list, MOVE, 100, 100, ex, ey, axis);
            assert(list.pages == 1 && list.taps == 0);
            feed(&list, MOVE, 100, 100, 100 - jitter, 100, axis);
            feed(&list, RELEASE, 100, 100, 100 - jitter, 100, axis);
            feed(&list, TAP, 100, 100, 100 - jitter, 100, axis);
            assert(list.pages == 1 && list.taps == 0);
            feed(&list, PRESS, 160, 160, 160, 160, axis);
            feed(&list, RELEASE, 160, 160, 160, 160, axis);
            feed(&list, TAP, 160, 160, 160, 160, axis);
            assert(list.pages == 1 && list.taps == 1);
        }
    }
    puts("PASS regression: return-to-origin and near-origin drags do not tap new content; next real tap works");
}

static void returned_swipe(void) {
    // 产品壳终端滑动阈值34，小于提前提交阈值36；同一次触摸仍只能翻一页。
    // The product shell's 34px terminal swipe is shorter than the 36px early commit; one contact must still turn once.
    const int endings[] = {66, 65, 150};
    for (unsigned i = 0; i < sizeof(endings) / sizeof(endings[0]); i++) {
        int end = endings[i];
        list_t list = {0};
        feed(&list, PRESS, 100, 100, 100, 100, PS_SCROLL_AXIS_X);
        feed(&list, MOVE, 100, 100, 50, 100, PS_SCROLL_AXIS_X);
        feed(&list, MOVE, 100, 100, end, 100, PS_SCROLL_AXIS_X);
        feed(&list, RELEASE, 100, 100, end, 100, PS_SCROLL_AXIS_X);
        feed(&list, end < 100 ? SWIPE_L : SWIPE_R, 100, 100, end, 100, PS_SCROLL_AXIS_X);
        assert(list.pages == 1 && list.taps == 0);
    }
    puts("PASS regression: 34/35px final swipes and reversed swipes cannot commit twice");
}

static void long_and_keys(void) {
    list_t list = {0};
    bool consume = false;
    feed(&list, PRESS, 0, 0, 0, 0, PS_SCROLL_AXIS_X);
    feed(&list, MOVE, 0, 0, 40, 0, PS_SCROLL_AXIS_X);
    assert(list.pages == -1);
    assert(step(&list.track, SWIPE_L, 0, 0, 0, 0, PS_SCROLL_AXIS_X, &consume) == 1 && !consume);
    assert(list.track.armed);
    assert(step(&list.track, LONG, 0, 0, 0, 0, PS_SCROLL_AXIS_X, &consume) == 0 && consume);
    assert(list.track.armed);
    feed(&list, MOVE, 0, 0, 50, 0, PS_SCROLL_AXIS_X);
    assert(list.pages == -1);
    feed(&list, RELEASE, 0, 0, 0, 0, PS_SCROLL_AXIS_X);
    assert(step(&list.track, SWIPE_R, 0, 0, 0, 0, PS_SCROLL_AXIS_X, &consume) == -1 && !consume);
    assert(!list.track.armed);

    list = (list_t){0};
    feed(&list, PRESS, 0, 0, 0, 0, PS_SCROLL_AXIS_X);
    feed(&list, MOVE, 0, 0, 40, 0, PS_SCROLL_AXIS_X);
    feed(&list, RELEASE, 0, 0, 0, 0, PS_SCROLL_AXIS_X);
    feed(&list, TAP, 0, 0, 0, 0, PS_SCROLL_AXIS_X);
    assert(list.pages == -1 && list.taps == 0);
    assert(step(NULL, MOVE, 0, 0, 40, 0, PS_SCROLL_AXIS_X, NULL) == 0);
    puts("PASS regression: pre-lift LONG keeps the commit; zero-origin taps are blocked and bare keys remain independent");
}

int main(void) {
    imported_cases();
    returned_tap();
    returned_swipe();
    long_and_keys();
    return 0;
}

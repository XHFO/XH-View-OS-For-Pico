/* SPDX-License-Identifier: Apache-2.0
 * 双机贴合误触判断：不靠固定禁用整块触摸，而是识别电容耦合的典型鬼触序列。
 * 典型特征：两指长时间对置、坐标大幅跳变、按下/抬起高频翻转、边缘反复触发。
 * 命中后短暂屏蔽，等所有触点离开 500ms 再恢复，避免误触继续落到菜单或翻页。
 *
 * Ghost-touch detection for two capacitive panels pressed together. It recognizes
 * opposed two-point contacts, large coordinate jumps, rapid down/up chatter and
 * repeated edge hits. A hit blocks input briefly and releases after 500 ms quiet.
 */
#include "app_touch_guard.h"

#include <string.h>

#define GUARD_BLOCK_MS 1400
#define GUARD_QUIET_MS 500
#define GUARD_DECAY_MS 220
#define GUARD_EDGE_PX 30

typedef struct {
    bool initialized;
    bool blocked;
    bool prev_active;
    uint8_t prev_count;
    uint16_t prev_x, prev_y;
    int64_t block_until;
    int64_t quiet_since;
    int64_t last_decay;
    int32_t score;
    uint16_t transitions;
    uint16_t jumps;
    uint16_t two_point;
    uint16_t opposite;
    uint16_t edge_hits;
    int transition_event;
} guard_state_t;

static guard_state_t g;

static void clear_stats(void) {
    g.transitions = g.jumps = g.two_point = g.opposite = g.edge_hits = 0;
    g.score = 0;
    g.prev_active = false;
    g.prev_count = 0;
    g.prev_x = g.prev_y = 0;
    g.last_decay = 0;
}

void app_touch_guard_reset(void) {
    memset(&g, 0, sizeof(g));
    g.initialized = true;
}

static void begin_block(int64_t now) {
    g.blocked = true;
    g.block_until = now + GUARD_BLOCK_MS;
    g.quiet_since = 0;
    g.transition_event = 1;
    clear_stats();
}

static void end_block(void) {
    g.blocked = false;
    g.block_until = 0;
    g.quiet_since = 0;
    g.transition_event = 2;
    clear_stats();
}

static int point_distance(uint16_t ax, uint16_t ay, uint16_t bx, uint16_t by) {
    int dx = (int)ax - (int)bx, dy = (int)ay - (int)by;
    if (dx < 0) dx = -dx;
    if (dy < 0) dy = -dy;
    return dx + dy;
}

bool app_touch_guard_filter(cst836u_touch_t *touch, int64_t now_ms, bool dual, bool enabled) {
    if (!g.initialized) app_touch_guard_reset();
    if (!touch) return false;
    if (!dual || !enabled) {
        if (g.blocked) end_block();
        else clear_stats();
        return false;
    }

    if (g.blocked) {
        // 只要还有触点就继续屏蔽；全部离开并安静 500ms 才恢复。
        // Remain blocked while any contact persists; release only after 500 ms quiet.
        if (touch->touched) {
            g.quiet_since = 0;
            g.block_until = now_ms + GUARD_BLOCK_MS;
        } else if (!g.quiet_since) {
            g.quiet_since = now_ms;
        } else if (now_ms - g.quiet_since >= GUARD_QUIET_MS) {
            end_block();
            return false;
        }
        memset(touch, 0, sizeof(*touch));
        return true;
    }

    if (now_ms - g.last_decay >= GUARD_DECAY_MS) {
        g.last_decay = now_ms;
        if (g.score > 0) --g.score;
        if (!touch->touched && !g.prev_active) {
            // 安静时让累计计数缓慢归零，只有真正持续的鬼触序列才会命中。
            // Decay counters while quiet so only a sustained ghost sequence trips the guard.
            if (g.transitions) --g.transitions;
            if (g.jumps) --g.jumps;
            if (g.two_point) --g.two_point;
            if (g.opposite) --g.opposite;
            if (g.edge_hits) --g.edge_hits;
        }
    }

    const int w = 684, h = 1216;
    if (touch->touched) {
        if (!g.prev_active) {
            ++g.transitions;
            g.score += 2;
        } else {
            int jump = point_distance(touch->x, touch->y, g.prev_x, g.prev_y);
            if (jump > 140) {
                ++g.jumps;
                g.score += 3;
            }
        }
        if (touch->count > 1) {
            ++g.two_point;
            g.score += 1;
            int dx = (int)touch->points[0].x - (int)touch->points[1].x;
            int dy = (int)touch->points[0].y - (int)touch->points[1].y;
            if (dx < 0) dx = -dx;
            if (dy < 0) dy = -dy;
            if (dx > 320 || dy > 520) {
                ++g.opposite;
                g.score += 4;
            }
        }
        if (touch->x < GUARD_EDGE_PX || touch->x >= w - GUARD_EDGE_PX ||
            touch->y < GUARD_EDGE_PX || touch->y >= h - GUARD_EDGE_PX) {
            ++g.edge_hits;
            g.score += 1;
        }
        g.prev_x = touch->x;
        g.prev_y = touch->y;
    }
    g.prev_active = touch->touched;
    g.prev_count = touch->count;

    if (g.opposite >= 3 || g.jumps >= 8 || g.transitions >= 12 ||
        (g.edge_hits >= 10 && g.two_point >= 3) || g.score >= 24) {
        begin_block(now_ms);
        memset(touch, 0, sizeof(*touch));
        return true;
    }
    return false;
}

bool app_touch_guard_blocked(void) { return g.blocked; }

int app_touch_guard_take_event(void) {
    int event = g.transition_event;
    g.transition_event = 0;
    return event;
}

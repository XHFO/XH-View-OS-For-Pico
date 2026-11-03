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
#define GUARD_RECOVER_HOLD_MS 900
#define GUARD_SUSPEND_MS 2500
#define GUARD_RELEASE_WAIT_MS 1500

typedef struct {
    bool initialized;
    bool blocked;
    bool wait_release;
    bool prev_active;
    uint8_t prev_count;
    uint16_t prev_x, prev_y;
    int64_t block_until;
    int64_t quiet_since;
    int64_t block_start;
    int64_t calm_since;
    int64_t wait_since;
    int64_t suspended_until;
    int64_t edge_since;
    uint16_t calm_x, calm_y, edge_x, edge_y;
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
    g.block_start = 0;
    g.calm_since = 0;
    g.wait_since = 0;
    g.edge_since = 0;
}

void app_touch_guard_reset(void) {
    memset(&g, 0, sizeof(g));
    g.initialized = true;
}

static void begin_block(int64_t now) {
    g.blocked = true;
    g.block_until = now + GUARD_BLOCK_MS;
    g.quiet_since = 0;
    g.block_start = now;
    g.calm_since = 0;
    g.wait_release = false;
    g.suspended_until = 0;
    g.edge_since = 0;
    g.transition_event = 1;
    clear_stats();
    g.block_start = now;
}

static void end_block(void) {
    g.blocked = false;
    g.block_until = 0;
    g.quiet_since = 0;
    g.transition_event = 2;
    g.wait_release = false;
    clear_stats();
}

bool app_touch_guard_filter(cst836u_touch_t *touch, int64_t now_ms, bool dual, bool enabled, bool contact) {
    if (!g.initialized) app_touch_guard_reset();
    if (!touch) return false;
    // 底部三键是独立物理触摸区（y>=1300），不属于屏幕鬼触范围；
    // 按键必须始终可用，并顺带解除屏蔽，避免只能按电源键。
    // The three bottom keys are a separate physical touch area (y>=1300),
    // never ghost contacts. They must always work and also clear the guard.
    if (touch->y >= 1300) {
        // 贴合屏蔽期间按键仍可导航，但不能清除屏蔽；展开后按键才恢复“顺带解除”。
        // Keys still navigate during a contact latch, but cannot clear it; once unfolded they clear again.
        if (g.blocked && !contact) {
            end_block();
            g.suspended_until = now_ms + 2000;
        }
        return false;
    }
    if (!dual || !enabled || !contact) {
        // 展开使用时两屏同向朝上，不做鬼触判断；正常滑动不会被误判。
        // When unfolded both screens face up; skip ghost detection so normal slides pass.
        if (g.blocked) end_block();
        else clear_stats();
        return false;
    }

    // 长按恢复后的短暂放行窗口；窗口内不再识别鬼触，给用户移开屏幕/关开关的时间。
    // Short pass-through after the recovery hold, so the user can separate the screens or toggle the guard.
    if (g.suspended_until) {
        if (now_ms < g.suspended_until) return false;
        g.suspended_until = 0;
        clear_stats();
    }
    // 恢复长按必须先抬手，不能让同一根手指在恢复后误触页面。
    // The recovery hold must lift before normal input resumes.
    if (g.wait_release) {
        if (!touch->touched) {
            g.wait_release = false;
            g.suspended_until = now_ms + GUARD_SUSPEND_MS;
            clear_stats();
            return false;
        }
        if (now_ms - g.wait_since >= GUARD_RELEASE_WAIT_MS) {
            g.wait_release = false;
            g.suspended_until = now_ms + GUARD_SUSPEND_MS;
            clear_stats();
            return false;
        }
        memset(touch, 0, sizeof(*touch));
        return true;
    }

    // 两屏相对贴合时，任何屏幕触摸都可能是耦合鬼触；一律先屏蔽，
    // 只保留物理三键和中央长按恢复。展开后 contact=false，正常滑动完全不经过这里。
    // Face-to-face contact: suppress every screen touch; only the physical keys and the
    // central recovery hold remain. Unfolded contact=false bypasses this entirely.
    if (!g.blocked) {
        if (!touch->touched) return false;
        begin_block(now_ms);
        memset(touch, 0, sizeof(*touch));
        return true;
    }

    if (g.blocked) {
        // 贴合状态下屏蔽保持锁存：触点离开、设备一起移动或姿态暂时不明都不解除；
        // 只有展开且稳定同向 800ms 后由上层 contact=false 结束。
        // Keep the shield latched through quiet, shared motion and ambiguous pose;
        // only the upper layer's stable unfolded contact=false ends it.
        if (touch->touched) g.quiet_since = 0;
        else if (!g.quiet_since) g.quiet_since = now_ms;
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

/* SPDX-License-Identifier: Apache-2.0
 * 中文：认证BLE内的锁屏状态仲裁；独立于阅读版本、翻页和正文锚点。
 * English: Lock arbitration inside authenticated BLE, separate from reading revisions, turns and anchors.
 */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
typedef struct {
    uint32_t clock, peer_ack;
    uint8_t origin;
    bool locked, dirty;
    int64_t sent;
} ps_dual_power_t;
static inline void pdp_set(ps_dual_power_t* s, unsigned role, bool locked) {
    if (++s->clock == 0) ++s->clock;
    s->origin = role;
    s->locked = locked;
    s->dirty = true;
    s->sent = 0;
}
static inline void pdp_put(uint8_t* p, uint32_t n) {
    for (unsigned i = 0; i < 4; i++) p[i] = (uint8_t)(n >> (i * 8));
}
static inline uint32_t pdp_get(const uint8_t* p) {
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}
static inline size_t pdp_take(ps_dual_power_t* s, unsigned role, int64_t now, uint8_t* p) {
    if (!s->dirty && (!s->clock || s->peer_ack == s->clock || now - s->sent < 400)) return 0;
    p[0] = 242;
    p[1] = s->origin == role ? 1 : 2;
    p[2] = role;
    p[3] = s->locked;
    pdp_put(p + 4, s->clock);
    p[8] = s->origin;
    s->dirty = false;
    s->sent = now;
    return 9;
}
// 返回1为新状态、0为确认/重复、-1为其他协议。/ 1 new state, 0 acknowledgement/duplicate, -1 other protocol.
static inline int pdp_receive(ps_dual_power_t* s, unsigned role, const uint8_t* p, size_t n) {
    if (n != 9 || p[0] != 242 || (p[1] != 1 && p[1] != 2) || p[2] != 3 - role || p[3] > 1 ||
        p[8] < 1 || p[8] > 2 || (p[1] == 1 && p[8] != p[2]) ||
        (p[1] == 2 && p[8] != role))
        return -1;
    uint32_t c = pdp_get(p + 4);
    if (!c) return -1;
    if (c == s->clock && p[8] == s->origin && p[3] == s->locked) {
        s->peer_ack = c;
        if (p[1] == 1) s->dirty = true;
        return 0;
    }
    if (c > s->clock || (c == s->clock && p[8] < s->origin)) {
        s->clock = c;
        s->origin = p[8];
        s->locked = p[3];
        s->peer_ack = c;
        s->dirty = p[1] == 1;
        return 1;
    }
    s->dirty = true;
    return 0;
}

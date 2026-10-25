/*
 * SPDX-FileCopyrightText: 2026 XH Lab
 * SPDX-License-Identifier: Apache-2.0
 * 中文：家庭连接 TF 工具包骨架（kind 130）；v1 显示空状态并提示后续完整监看。
 * English: Home-link TF tools package skeleton (kind 130); v1 shows empty state and next-step guidance.
 */
#include "pico_app_sdk.h"

enum { HOME_LINK_KIND = 130, HOME_LINK_VERSION = 10000 };

static const pico_app_host_t *host;

__attribute__((visibility("hidden"))) void *memcpy(void *dst, const void *src, size_t n) {
    return host->copy(dst, src, n);
}
__attribute__((visibility("hidden"))) void *memset(void *dst, int value, size_t n) {
    return host->set(dst, value, n);
}

static bool start(const pico_app_host_t *h) {
    if (!h || h->abi != PICO_APP_ABI || h->bytes < PICO_APP_HOST_V1_BYTES || !h->view || !h->text || !h->wrap ||
        !h->heading || !h->button || !h->fill || !h->toast || !h->navigate || !h->copy || !h->set)
        return false;
    host = h;
    return true;
}

static void render(uint8_t *fb) {
    pico_app_view_t v = host->view();
    host->fill(fb, 0, 0, v.w, v.h, 15);
    host->heading(fb, "家庭连接", "TF 工具包");
    host->wrap(fb, 24, 120, v.w - 48, 14, 6,
               "这是 home-link 工具包骨架。完整 Home Assistant 与打印机监看将在后续版本迁入本模块；"
               "当前固件在未安装或未配置时保持空状态，阅读与书架不受影响。",
               5);
    host->button(fb, 16, v.h - 56, v.w - 32, 40, "返回", false);
}

static bool event(const pico_app_event_t *e) {
    if (!e) return false;
    if (e->type == PGA_TAP) {
        pico_app_view_t v = host->view();
        if (e->x >= 16 && e->x < v.w - 16 && e->y >= v.h - 56 && e->y < v.h - 16) {
            host->navigate(PGA_PAGE_LAUNCHER);
            return true;
        }
        host->toast("完整监看逻辑尚未迁入此包");
        return true;
    }
    if (e->type == PGA_SWIPE_R) {
        host->navigate(PGA_PAGE_LAUNCHER);
        return true;
    }
    return false;
}

static bool key(int k) {
    if (k == -1) {
        host->navigate(PGA_PAGE_LAUNCHER);
        return true;
    }
    return false;
}

static bool tick(void) { return false; }
static void action(int a) { (void)a; }
static uint32_t status(void) { return 0; }
static bool save(void) { return true; }
static void stop(void) {}

static const pico_app_exports_t exports = {
    .abi = PICO_APP_ABI,
    .bytes = PICO_APP_EXPORTS_V1_BYTES,
    .version = HOME_LINK_VERSION,
    .kind = HOME_LINK_KIND,
    .id = "home-link",
    .start = start,
    .render = render,
    .event = event,
    .key = key,
    .tick = tick,
    .action = action,
    .status = status,
    .save = save,
    .stop = stop,
};

__attribute__((visibility("default"))) const pico_app_exports_t *pico_app_query(void) { return &exports; }

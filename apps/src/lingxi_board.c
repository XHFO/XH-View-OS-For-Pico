/* SPDX-License-Identifier: Apache-2.0
 * 中文：TF独立灵犀/网站流量绘制引擎，宿主仅保留认证、持久化和已校验模型。
 * English: Independent TF Lingxi/traffic renderer; host retains authentication, storage and validated models.
 */
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include "pico_app_sdk.h"
static const pico_app_host_t* H;
#undef memcpy
__attribute__((visibility("hidden"))) void* memcpy(void* d, const void* s, size_t n) {
    return H->copy(d, s, n);
}
#define PICO_BOARD_MODULE 1
#define PICO_BOARD_PORTABLE 1
#undef memcpy
#undef snprintf
#define memcpy H->copy
#define snprintf H->format
static const char* ps_board_kind_name(unsigned kind) {
    static const char* const names[] = {
        "clock",          "date",      "cpu",         "memory",  "network",
        "now_playing",    "pomodoro",  "uptime",      "text",    "image",
        "qwen_quota",     "codex",     "oracle_text", "sspai",   "disk",
        "home_assistant", "bambu_lab", "formlabs",    "weather", "reading_excerpt",
        "cloudflare",     "grok_bot"
    };
    return kind < sizeof(names) / sizeof(names[0]) ? names[kind] : "";
}
#include "../../main/system/pico_board_render.c"
#ifdef BOARD_TRAFFIC_FULL
#include "../include/traffic_app.h"
#else
static bool start(const pico_app_host_t* host) {
    if (!host || host->abi != PICO_APP_ABI ||
        host->bytes < offsetof(pico_app_host_t, board_model_abi) + sizeof(host->board_model_abi) ||
        !host->board_time || host->board_model_abi != PICO_BOARD_MODEL_ABI)
        return false;
    H = host;
    return true;
}
static void render(uint8_t* fb) {
    (void)fb;
}
static bool event(const pico_app_event_t* e) {
    (void)e;
    return false;
}
static bool key(int k) {
    (void)k;
    return false;
}
static bool tick(void) {
    return false;
}
static void action(int a) {
    (void)a;
}
static uint32_t status(void) {
    return 0;
}
static bool save(void) {
    return true;
}
static void stop(void) {
    H = NULL;
}
static void paint(const void* model, const void* painter) {
    if (H) ps_board_model_paint(model, painter);
}
#ifndef BOARD_TRAFFIC
#define BOARD_TRAFFIC 0
#endif
static const pico_app_exports_t app = { .abi = 1,
                                        .bytes = sizeof(app),
                                        .version = 10001,
                                        .kind =
                                            BOARD_TRAFFIC ? PGA_WEBSITE_TRAFFIC : PGA_LINGXI_BOARD,
                                        .id = BOARD_TRAFFIC ? "website-traffic" : "lingxi-board",
                                        .start = start,
                                        .render = render,
                                        .event = event,
                                        .key = key,
                                        .tick = tick,
                                        .action = action,
                                        .status = status,
                                        .save = save,
                                        .stop = stop,
                                        .board_paint = paint };
__attribute__((visibility("default"))) const pico_app_exports_t* pico_app_query(void) {
    return &app;
}

#endif

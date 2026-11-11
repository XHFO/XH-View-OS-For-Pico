/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 *
 * 产品页面清单；原始工程演示不进入产品菜单或链接映像。
 *
 * Product page table; original engineering demos stay out of the product menu and linked image.
 */

#include "app_registry.h"

extern const app_desc_t app_pico_system;
extern const app_desc_t app_font_pick;
#ifndef PICO_DUAL_READER
extern const app_desc_t app_selftest;
#endif
extern const app_desc_t app_book;
extern const app_desc_t app_transfer;

// 自检只保留启动恢复入口，不加入日常产品菜单。/ Factory self-test retains its boot-resume entry outside the daily product menu.
static const app_desc_t* const s_apps[] = {
    &app_pico_system,
    &app_book,
    &app_font_pick,
    &app_transfer,
};

#define APP_COUNT ((int)(sizeof(s_apps) / sizeof(s_apps[0])))

int app_count(void) {
    return APP_COUNT;
}

const app_desc_t* app_at(int index) {
    if (index < 0 || index >= APP_COUNT) return NULL;
    return s_apps[index];
}

int app_index_of(const app_desc_t* app) {
    for (int i = 0; i < APP_COUNT; i++) {
        if (s_apps[i] == app) return i;
    }
    return -1;
}

const app_desc_t* app_home_page(void) {
    return &app_pico_system;
}

const app_desc_t* app_selftest_page(void) {
#ifdef PICO_DUAL_READER
    // 双机发布版把固定APP槽留给设备端应用下载；自检仍由工厂工具维护。
    // The dual-reader release reserves the fixed APP slot for on-device app downloads; factory tools retain self-test.
    return &app_pico_system;
#else
    return &app_selftest;
#endif
}

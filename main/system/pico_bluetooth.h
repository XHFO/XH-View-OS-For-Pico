/* SPDX-License-Identifier: Apache-2.0
 * 蓝牙翻页器设置与UI线程事件桥。/ Bluetooth page-turner settings and UI-thread event bridge.
 * 冻结：只翻正文，网页仅模拟，蓝牙回调不得画屏。/ Frozen: Reading only; simulated on web; no painting in Bluetooth callbacks.
 */
#pragma once
#include "pico_system.h"
void ps_bluetooth_init(void);
void ps_bluetooth_enter(void);
void ps_bluetooth_render(uint8_t *fb);
bool ps_bluetooth_event(const ps_event_t *event);
bool ps_bluetooth_tick(void);
bool ps_bluetooth_input(app_ctx_t *ctx, const app_desc_t *app, bool blocked, app_redraw_t *redraw);

/*
 * SPDX-FileCopyrightText: 2026 XH Lab
 * SPDX-License-Identifier: Apache-2.0
 * 设备上的短时局域网填写门户：仅在已连接的 Wi-Fi 上监听，屏幕只显示打开表单所需的地址。
 * Short-lived on-device LAN portal. Listens only while Wi-Fi is connected; the screen URL opens the form.
 */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum { PS_HOME_PORTAL_OWNER_NONE, PS_HOME_PORTAL_OWNER_HOME, PS_HOME_PORTAL_OWNER_GAMES };

typedef struct {
    bool active;
    char phase[16];
    char url[160];
    char error[64];
    uint32_t seconds_left;
    uint32_t revision;
} ps_home_portal_view_t;

void ps_home_portal_register(void);
bool ps_home_portal_start(int owner);
/// 同一短时门户按入口分别显示家庭或打印机表单。/ Scope the temporary portal form to Home or Printers.
void ps_home_portal_set_printers(bool printers);
void ps_home_portal_stop(void);
void ps_home_portal_stop_if_home(void);
bool ps_home_portal_active(void);
bool ps_home_portal_tick(void);
void ps_home_portal_view(ps_home_portal_view_t *out);
void ps_home_portal_status_suffix(char *out, size_t capacity);
int ps_home_portal_mac_handle(const char *method, char *out, size_t capacity);

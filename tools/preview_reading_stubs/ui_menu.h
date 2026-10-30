/* SPDX-License-Identifier: Apache-2.0
 * 主机按键与菜单把手契约。/ Host key and menu-handle contract.
 */
#pragma once
#include <stdbool.h>
#include <stdint.h>
#define UI_KEY_1 0
#define UI_KEY_2 1
#define UI_KEY_3 2
void ui_draw_menu_handle(uint8_t* fb, bool open);

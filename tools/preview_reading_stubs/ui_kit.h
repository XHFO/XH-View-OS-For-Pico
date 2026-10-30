/* SPDX-License-Identifier: Apache-2.0
 * 主机界面边界检查子集。/ UI subset for host bounds checking.
 */
#pragma once
#include "app.h"
#define UI_MARGIN 40
#define UI_CONTENT_TOP 176
#define UI_CONTENT_BOTTOM 1076
#define UI_BAR_TOP 1096
#define UI_LOCK_WIDTH 684
#define UI_GRAY_LIGHT 176
#define UI_GRAY_BLACK 0
int ui_content_width(void);
void ui_text(uint8_t* fb, int x, int y, int px, const char* text, enum EpdFontFlags align, bool inverted);
int ui_draw_header(uint8_t* fb, const char* title, const char* subtitle);
void ui_clear_page(uint8_t* fb);
void ui_hairline(uint8_t* fb, int y, int x, int width, uint8_t color);
EpdRect ui_grid_rect(int column, int columns, int row, int top, int height);
bool ui_rect_hit(EpdRect rect, uint16_t x, uint16_t y);
void ui_draw_button(uint8_t* fb, EpdRect rect, const char* label, bool on);

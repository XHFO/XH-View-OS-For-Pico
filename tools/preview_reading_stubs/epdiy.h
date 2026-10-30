/* SPDX-License-Identifier: Apache-2.0
 * 主机几何与绘图检查接口。/ Host geometry and drawing-check interface.
 */
#pragma once
#include <stdint.h>
typedef struct { int x, y, width, height; } EpdRect;
enum EpdFontFlags { EPD_DRAW_ALIGN_LEFT = 0, EPD_DRAW_ALIGN_CENTER = 1 };
void epd_draw_pixel(int x, int y, uint8_t color, uint8_t* fb);
int epd_rotated_display_width(void);

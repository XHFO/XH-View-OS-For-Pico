/* SPDX-License-Identifier: Apache-2.0
 * 中文：首页宿主检查的屏幕类型，不连接硬件。
 * English: Display types for home host checks; no hardware connection.
 */
#pragma once
#include <stdint.h>

typedef struct { int x, y, width, height; } EpdRect;
enum EpdFontFlags { EPD_DRAW_ALIGN_LEFT, EPD_DRAW_ALIGN_CENTER, EPD_DRAW_ALIGN_RIGHT };
#define MODE_DU 1
#define MODE_GL16 2
#define MODE_GC16 3

void epd_fill_rect(EpdRect rect, uint8_t color, uint8_t* framebuffer);
void epd_draw_circle(int x, int y, int radius, uint8_t color, uint8_t* framebuffer);
void epd_draw_line(int x0, int y0, int x1, int y1, uint8_t color, uint8_t* framebuffer);

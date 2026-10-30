/* SPDX-License-Identifier: Apache-2.0
 * 模拟字体契约；页数不代表实机。/ Mock font contract; page counts are not device results.
 */
#pragma once
#include "epdiy.h"
#define TTF_FONT_PATH_MAX 160
int ttf_get_weight(void);
void ttf_set_weight(int weight);
const char* ttf_font_path(void);
const char* ttf_font_display_name(void);
int ttf_text_width_px(int px, const char* text);
int ttf_ascender_px(int px);
void ttf_draw_text_px(uint8_t* fb, int x, int y, int px, const char* text,
                      enum EpdFontFlags align, uint8_t fg, uint8_t bg);

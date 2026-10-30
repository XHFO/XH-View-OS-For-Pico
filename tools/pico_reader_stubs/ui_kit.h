#pragma once
#include "epdiy.h"
void ui_fill_round_rect(uint8_t*,EpdRect,int,uint8_t);
void ui_draw_round_rect(uint8_t*,EpdRect,int,uint8_t);

EpdRect ui_rotate_rect_to_fb(EpdRect);
void ui_fill_rect_fast(uint8_t*,EpdRect,uint8_t);

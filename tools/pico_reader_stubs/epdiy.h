#pragma once
#include <stdint.h>
typedef struct { int x,y,width,height; } EpdRect;
enum EpdFontFlags { EPD_DRAW_ALIGN_LEFT=0, EPD_DRAW_ALIGN_CENTER=1, EPD_DRAW_ALIGN_RIGHT=2 };
void epd_draw_pixel(int,int,uint8_t,uint8_t*);
void epd_fill_rect(EpdRect,uint8_t,uint8_t*);
void epd_draw_hline(int,int,int,uint8_t,uint8_t*);
void epd_draw_vline(int,int,int,uint8_t,uint8_t*);
uint8_t epd_get_pixel(int,int,int,int,const uint8_t*);
void epd_draw_line(int,int,int,int,uint8_t,uint8_t*);
void epd_draw_circle(int,int,int,uint8_t,uint8_t*);
void epd_fill_circle_helper(int,int,int,int,int,uint8_t,uint8_t*);

#pragma once
#include <stdbool.h>
#include "epdiy.h"
#define TTF_FONT_PATH_MAX 160
#define TTF_FONT_BUILTIN "builtin"
#define TTF_FONT_BUILTIN_SERIF "builtin-serif"
#define TTF_FONT_BUILTIN_KAI "builtin-kai"
#define TTF_FONT_DEFAULT_SD "/sdcard/fonts/MiSansVF.ttf"
const char *ttf_font_path(void);
const char *ttf_font_display_name(void);
int ttf_text_width_px(int px,const char *s);
int ttf_font_open_builtin(void);
int ttf_font_open(const char*);
bool ttf_font_path_is_builtin(const char*);
bool ttf_font_is_builtin(void);
bool ttf_font_sd_suspended(void);
void ttf_set_weight(int weight);
int ttf_get_weight(void);
int ttf_ascender_px(int px);
void ttf_draw_text_px(uint8_t*,int,int,int,const char*,enum EpdFontFlags,uint8_t,uint8_t);

void ttf_font_cache_clear(void);

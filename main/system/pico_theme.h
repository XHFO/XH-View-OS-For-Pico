/*
 * 16 级灰阶的界面语义色；不改变布局、命中区域或屏幕刷新策略。
 * Semantic 16-gray UI tokens; no layout, hit testing or refresh policy.
 */
#pragma once
#include <stdbool.h>
#include <stdint.h>

enum {
    PS_THEME_PAPER = 15,
    PS_THEME_CANVAS = 14,
    PS_THEME_SURFACE = 15,
    PS_THEME_INSET = 13,
    PS_THEME_BORDER = 12,
    PS_THEME_INK = 1,
    PS_THEME_SECONDARY = 5,
    PS_THEME_DISABLED = 7,
    PS_THEME_SELECTED = 2,
    PS_THEME_ON_SELECTED = 15,
    PS_THEME_RADIUS = 12,
    PS_THEME_BUTTON_RADIUS = 10,
    PS_THEME_STROKE_PX = 3,
};

/* 使用当前画布直接混合边缘，无额外帧缓冲。/ Blend on the current initialized canvas. */
void ps_surface(uint8_t *fb, int x, int y, int w, int h, bool inset);

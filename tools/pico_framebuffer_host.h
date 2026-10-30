/* SPDX-License-Identifier: Apache-2.0
 * 原生绘图的主机灰阶画布，不代表屏幕波形、刷新或物理触摸。 / Host grayscale canvas; not physical display refresh or touch acceptance.
 */
#pragma once
#include <stdbool.h>
#include <stdint.h>
bool pico_fb_init(const char *font_path);
uint8_t *pico_fb_new(void);
bool pico_fb_save(const char *path,const uint8_t *fb);
void pico_fb_close(void);

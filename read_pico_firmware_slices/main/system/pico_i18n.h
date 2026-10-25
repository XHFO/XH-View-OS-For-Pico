/*
 * SPDX-License-Identifier: Apache-2.0
 * 中文：宿主界面文案。简体中文为默认，英文成对存放；键名与 Mac 多屏灵犀对齐。
 * English: Host UI copy. Simplified Chinese is the default; English is paired. Keys match Mac 多屏灵犀.
 * 没有更早的语言设置。偏好存在 uilocale（0=zh-Hans，1=en）。
 * There is no earlier language setting. The preference is uilocale (0=zh-Hans, 1=en).
 */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum { PS_LOCALE_ZH_HANS = 0, PS_LOCALE_EN = 1 } ps_locale_t;

/// 从 uilocale 读取；缺失或非法时保持简体中文。/ Load uilocale; missing or invalid values stay Simplified Chinese.
void ps_i18n_init(void);
ps_locale_t ps_locale(void);
/// "zh-Hans" 或 "en"。/ "zh-Hans" or "en".
const char *ps_locale_tag(void);
/// 写入 uilocale。失败不改变当前语言。/ Persist uilocale. Failure leaves the current language.
bool ps_locale_set(ps_locale_t locale);
/// 未知键返回键名本身。/ Unknown keys return the key.
const char *ps_tr(const char *key);

int ps_i18n_count(void);
const char *ps_i18n_key(int index);
const char *ps_i18n_zh(int index);
const char *ps_i18n_en(int index);

enum { PS_I18N_CHIP_W = 64, PS_I18N_CHIP_H = 30 };
/// 右上角语言芯片，显示当前语言。/ Top-right chip showing the active language.
void ps_i18n_chip(uint8_t *fb, int x, int y);
/// 命中则切换并提示；未命中返回 false。/ Toggle and toast on hit; return false otherwise.
bool ps_i18n_chip_hit(int x, int y, int chip_x, int chip_y);

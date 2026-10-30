/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 *
 * NVS 里的用户设置：睡眠档、字体路径、上次唤醒/开机原因、拿起唤醒开关。
 *
 * User settings in NVS: sleep mode, font path, last wake/boot reason,
 * pickup-wake switch.
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

/// 默认关机。浅睡：按键回原页；拿起唤醒默认关。软睡：SOFT_SLEEP 拉低 EN，再短按开机。关机：EN=0，长按开机。
/// Default is off. Light: key returns to the page; pickup-wake defaults off. Soft sleep: SOFT_SLEEP drops EN, then a short press boots. Off: EN=0, long-press to boot.
typedef enum {
    APP_SLEEP_LIGHT = 0,
    APP_SLEEP_DEEP = 1,
    APP_SLEEP_OFF = 2,
} app_sleep_mode_t;

void app_settings_init(void);
app_sleep_mode_t app_settings_sleep_mode(void);
void app_settings_set_sleep_mode(app_sleep_mode_t mode);
const char* app_sleep_mode_name(app_sleep_mode_t mode);
/// 默认首选SD完整字体；显式空路径选择内建，临时回退不改变此值。/ Prefer the complete SD font by default; explicit empty selects built-in, and temporary fallback never changes this value.
const char* app_settings_font_path(void);
void app_settings_set_font_path(const char* path);
/// 本次开机用户选择修订号，区分字体选择与断卡临时回退。/ Session preference revision distinguishes explicit selection from temporary media fallback.
uint32_t app_settings_font_revision(void);
/// 上次浅睡唤醒源（app_wake_source_t），掉电也保留。/ Last light-sleep wake source (app_wake_source_t); kept across power loss.
uint8_t app_settings_last_wake(void);
void app_settings_set_last_wake(uint8_t src);
/// 最近一次非 0 的 PMU wake_reason。STATUS 报 0 时用这个回显。/ Last non-zero PMU wake_reason. Used when STATUS reports 0.
uint8_t app_settings_last_boot(void);
void app_settings_set_last_boot(uint8_t reason);
/// 浅睡拿起唤醒。默认关；有加速度计也不会自动开。/ Light-sleep pickup wake. Defaults off; an accelerometer does not turn it on.
bool app_settings_pickup_wake(void);
void app_settings_set_pickup_wake(bool on);
/// 开发测试保持唤醒；本测试版默认开，仅阻止空闲自动休眠，不影响手动电源键。/ Development keep-awake defaults on in this test build; blocks idle sleep only, never the manual power key.
bool app_settings_test_awake(void);
/// 保存开发测试保持唤醒开关，重启仍保留。/ Persist the development keep-awake switch across restarts.
void app_settings_set_test_awake(bool on);
/// 阅读默认字号，36..72、步长 4，默认 48。/ Default reading size, 36..72 in steps of 4, initially 48.
uint8_t app_settings_book_px(void);
/// 无效字号恢复 48。/ Invalid sizes fall back to 48.
void app_settings_set_book_px(uint8_t px);
/// 实验性晃动翻页，默认关闭。/ Experimental shake page turn, off by default.
bool app_settings_book_shake(void);
/// 保存实验性晃动翻页开关。/ Persist the experimental shake page-turn switch.
void app_settings_set_book_shake(bool on);
/// 提示音独立位掩码：总开关/触摸/充电/错误，默认全部开；不改变已有设置结构。/ Separate sound mask: master/touch/charge/error, all on by default; existing settings layouts stay unchanged.
uint8_t app_settings_sound_flags(void);
/// 检查NVS写入和提交；失败保留当前值。/ Check NVS write and commit; failure retains the current value.
bool app_settings_set_sound_flags(uint8_t flags);

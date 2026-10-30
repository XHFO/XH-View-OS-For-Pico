/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 *
 * 锁屏、浅睡等待、软睡/关机下电。
 *
 * Lock, light-sleep wait, and soft-sleep / power-off rail drop.
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "cst836u.h"
#include "epd_highlevel.h"
#include "esp_err.h"
#include "sc7a20h.h"
#include "settings.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    APP_WAKE_NONE = 0,
    APP_WAKE_KEY,
    APP_WAKE_PICKUP,
    APP_WAKE_TOUCH_DOUBLE,
    APP_WAKE_ERROR,
    APP_WAKE_KEY_LONG,
} app_wake_source_t;

/// 登记常开 3V3 上的触摸与加速度计，睡前由本模块送进最低功耗态。
/// Register the always-on touch and accelerometer so sleep paths can park them.
void app_sleep_bind(cst836u_handle_t tp, sc7a20h_handle_t acc);
/// 当前锁屏设置是否走软睡/关机。
/// Whether the stored lock choice powers the host down (soft sleep or off).
bool app_lock_powers_down(void);

void enter_lock_and_sleep(
    EpdiyHighlevelState* hl, int64_t* ignore_until_ms, sc7a20h_handle_t acc
);

/// 等电源键松开，避免进睡瞬间被同一下按住立刻唤醒。
/// Wait for the power key to release so the same press does not wake immediately.
void app_lock_wait_key_idle(int timeout_ms);
/// ESP 浅睡，按键或拿起唤醒。acc 为空则只等按键。
/// ESP light sleep; wake on key or pickup. Key only when acc is NULL.
app_wake_source_t app_light_sleep_wait(sc7a20h_handle_t acc);
app_wake_source_t app_last_wake_source(void);
/// 锁屏关闭屏轨和IMU；可选双击保留触摸NORMAL+INT，其余送DEEPSLEEP；允许浅睡前须先停止无线和文件任务。
/// Lock parks panel/IMU; optional double tap keeps touch NORMAL+INT, otherwise DEEPSLEEP; stop radio/file workers before allowing light sleep.
/// 禁止浅睡时仅CPU等待，同样接受电源短按；外电画板可主动保留网络，其余须提示低功耗受限。
/// With light sleep disallowed, wait on CPU with the same power-short rule; external-power board mode may intentionally retain networking.
esp_err_t app_product_lock_begin(bool light_sleep_allowed, bool doubletap_allowed);
/// 浅睡等完整电源短按/长按、可选双短触或超时；无触摸时不持续读取触控I2C。
/// Light sleep until a completed power click, long event, optional double tap or timeout; no idle touch I2C polling.
/// ERROR表示硬件浅睡失败，调用方须退出或明确退回CPU等待，不能静默重试。
/// ERROR means hardware sleep failed; exit or explicitly fall back to CPU waiting instead of silently retrying.
app_wake_source_t app_product_lock_wait_step(uint32_t timeout_ms);
/// 撤销产品锁屏唤醒源并恢复外设；不改当前帧、页面、字体或存储。
/// Remove product-lock wake sources and restore peripherals without changing the frame, page, fonts or storage.
void app_product_lock_end(void);
/// 软睡或关机：停外设、落盘后让 PMU 拉掉 EN。成功不返回；返回即失败，外设已恢复。
/// Soft sleep or power-off: park peripherals, sync, let the PMU drop EN. Does not return on success; a return means failure with peripherals restored.
esp_err_t app_enter_host_sleep(app_sleep_mode_t mode);

#ifdef __cplusplus
}
#endif

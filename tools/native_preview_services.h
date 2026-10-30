/* SPDX-License-Identifier: Apache-2.0
 * 网页原生预览的可控硬件替身，不访问电脑 Wi-Fi、USB 或存储设备；家庭网络服务另行手动配置。
 * Controllable service doubles for the native web preview; no host Wi-Fi, USB or block-device access.
 */
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

void native_preview_services_init(void);
void native_preview_services_tick(int64_t now_ms);
/// 仅修改预览快照；名称见实现，未知名称返回 false。/ Mutate preview snapshots only; unknown scenario names return false.
bool native_preview_service_scenario(const char *name);
/// 输出硬件模拟范围、家庭联网授权与非敏感状态，不含密码。/ Emit hardware simulation scope, Home network opt-in and nonsensitive state, never passwords.
void native_preview_service_status_json(FILE *out);

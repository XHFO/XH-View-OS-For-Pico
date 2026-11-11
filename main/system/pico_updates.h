/* SPDX-License-Identifier: Apache-2.0
 * 中文：网站版本检查、PGA应用包下载和设备端Wi-Fi固件下载。
 * English: Website release checks, PGA package downloads and on-device Wi-Fi firmware downloads.
 */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "pico_system.h"

#define PS_UPDATES_APPS_MAX 12
enum {
    PS_UPDATE_CHANNEL_STABLE = 0,
    PS_UPDATE_CHANNEL_TEST = 1
};
typedef struct {
    char id[32];
    bool tool;
    bool installed;
} ps_updates_app_t;

/// 手动重新检查 xhfo.net；离线或已检查时返回 false。/ Recheck xhfo.net; false when offline or already busy.
bool ps_updates_check(void);
/// 下载网站目录中缺失或版本较旧的应用包；安装回到UI线程完成。/ Download missing or older catalog apps; installation completes on the UI thread.
bool ps_updates_download_apps(void);
/// 读取最新应用目录但不下载。/ Refresh the app catalog without downloading.
bool ps_updates_apps_catalog_refresh(void);
/// 读取或切换系统更新频道；切换会清除另一频道的缓存镜像。
/// Read or switch the system-update channel; switching removes cached firmware from the other channel.
uint8_t ps_updates_channel(void);
const char *ps_updates_channel_name(uint8_t channel);
bool ps_updates_channel_set(uint8_t channel);
/// 复制当前应用目录快照；渲染线程只读内存。/ Copy the current app snapshot; rendering stays memory-only.
unsigned ps_updates_apps_catalog(ps_updates_app_t *out, unsigned capacity);
/// 仅下载指定的应用 ID，安装仍回到 UI 线程。/ Download only selected app IDs; installation stays on the UI thread.
bool ps_updates_download_apps_selected(const char *const *ids, unsigned count);
/// 下载网站最新固件到TF的update.bin与update.sha256；不改写运行分区。/ Download the latest firmware to TF without touching the running partition.
bool ps_updates_download_firmware(void);
/// TF中已有固件镜像和64字节SHA文件。/ A firmware image and 64-byte digest exist on TF.
bool ps_updates_firmware_ready(void);
/// 确认入口调用；切换到ota_1更新器并重启。/ Called after confirmation; switch to the ota_1 updater and restart.
bool ps_updates_arm_firmware(void);
/// 复制最新版本、应用数量和最近状态供“关于本机”绘制。/ Copy latest version, app count and status for About.
bool ps_updates_status(char *latest, size_t latest_bytes, bool *newer, unsigned *apps,
                       char *message, size_t message_bytes);
/// 固件下载百分比仅在后台下载期间有效。/ Firmware download percentage is valid only while downloading.
bool ps_updates_firmware_progress(unsigned *percent);
/// 应用集专用状态；与系统固件提示分开。/ App-collection status, separate from system firmware messages.
bool ps_updates_apps_status(unsigned *catalog, char *message, size_t message_bytes);
/// 后台完成或状态改变时返回 true。/ Return true when background state changes.
bool ps_updates_tick(void);

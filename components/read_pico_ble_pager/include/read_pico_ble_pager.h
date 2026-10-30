/*
 * SPDX-FileCopyrightText: 2026 XH Lab
 * SPDX-License-Identifier: Apache-2.0
 *
 * 蓝牙低功耗 HID 翻页器；UI 只读快照及消费事件，不在蓝牙回调绘图。
 * Bluetooth LE HID page turners; UI reads snapshots and consumes queued events.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define READ_PICO_BLE_PAGER_MAX_DEVICES 8
#define READ_PICO_BLE_PAGER_NAME_SIZE 48

typedef struct {
    char name[READ_PICO_BLE_PAGER_NAME_SIZE]; ///< 广播名称；无名称时用 BLE HID / Advertised name or BLE HID fallback.
    uint8_t address[6]; ///< BLE 地址，供状态展示 / BLE address for display.
    uint8_t address_type; ///< BLE 地址类型 / BLE address type.
    int8_t rssi; ///< 信号强度 / Received signal strength.
} read_pico_ble_pager_device_t;

typedef struct {
    bool enabled; ///< 用户开关，关闭时不扫描 / User switch; disabled means no scanning.
    bool ready; ///< 蓝牙协议栈已就绪 / Bluetooth stack synchronized.
    bool scanning; ///< 正在扫描兼容 HID 设备 / Scanning compatible HID devices.
    bool connecting; ///< 连接、配对或发现服务中 / Connecting, pairing or discovering services.
    bool connected; ///< 可接收翻页输入 / Ready to receive page-turn input.
    bool remembered; ///< 已记住上次成功连接设备 / A successful peer is remembered.
    bool reversed; ///< 交换上一页和下一页 / Swap previous and next page.
    bool simulated; ///< 网页模拟，未访问真实蓝牙 / Web simulation; no real Bluetooth access.
    uint8_t battery_percent; ///< 0–100；255 表示未知 / 0–100; 255 means unknown.
    uint8_t device_count; ///< 当前扫描结果数 / Number of scan results.
    uint32_t revision; ///< 状态变化序号 / Status-change revision.
    int32_t error; ///< 0 成功；其余为 ESP 或 NimBLE 错误 / Zero or ESP/NimBLE error.
    char name[READ_PICO_BLE_PAGER_NAME_SIZE]; ///< 当前或记住的设备名 / Connected or remembered peer name.
} read_pico_ble_pager_status_t;

/// 初始化有界队列并异步恢复已保存的开关；默认关闭时不创建任务或蓝牙栈。/ Initialize bounded queues and restore the saved switch; no task or Bluetooth stack while initially disabled.
esp_err_t read_pico_ble_pager_init(void);
/// 异步启用或关闭；首次启用才初始化蓝牙。/ Asynchronously enable/disable; initialize Bluetooth lazily.
esp_err_t read_pico_ble_pager_set_enabled(bool enabled);
/// 异步扫描 8 秒；只显示广播 HID over GATT 服务的设备。/ Scan for eight seconds; only advertised HID-over-GATT peers.
esp_err_t read_pico_ble_pager_scan(void);
/// 连接当前列表中的指定设备并配对；成功后自动记忆。/ Connect/pair a scan result; remember only on success.
esp_err_t read_pico_ble_pager_connect(size_t scan_index);
/// 断开且本次不自动重连，直到重新开启或手动选择。/ Disconnect and suspend reconnect until re-enabled or selected.
esp_err_t read_pico_ble_pager_disconnect(void);
/// 断开、删除保存的设备和配对信息。/ Disconnect and delete remembered peer and bond.
esp_err_t read_pico_ble_pager_forget(void);
/// 交换按键方向并保存。/ Swap page-turn direction and persist.
esp_err_t read_pico_ble_pager_set_reversed(bool reversed);
/// 复制线程安全状态快照。/ Copy a thread-safe state snapshot.
void read_pico_ble_pager_get_status(read_pico_ble_pager_status_t *status);
/// 复制扫描结果，返回实际复制数。/ Copy scan results and return the copied count.
size_t read_pico_ble_pager_get_devices(read_pico_ble_pager_device_t *devices, size_t capacity);
/// 非阻塞消费一次翻页：-1 上一页，+1 下一页，0 无事件。/ Nonblocking page turn: -1 previous, +1 next, zero none.
int read_pico_ble_pager_poll_event(void);
/// 完成协议栈与控制器停用且无在途任务后才允许显式浅睡；不能只看用户开关。/ Explicit light sleep is safe only after host/controller teardown and all in-flight tasks complete; the switch alone is insufficient.
bool read_pico_ble_pager_light_sleep_safe(void);

/// 暂借控制器给双机 BLE；调用释放前必须完整停止双机 host，不改持久 HID 设置。
/// Lease the controller; fully stop the dual host before release, preserving HID settings.
esp_err_t read_pico_ble_pager_reserve(bool reserve);
bool read_pico_ble_pager_reservation_ready(void);

#ifdef __cplusplus
}
#endif

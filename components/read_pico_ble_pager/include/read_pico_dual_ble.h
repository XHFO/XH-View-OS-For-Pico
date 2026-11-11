/* SPDX-License-Identifier: Apache-2.0
 * 双机 BLE 控制通道；不绘图，不读写书籍。/ Dual-reader BLE control; no drawing or book IO.
 */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
#define PICO_DUAL_BLE_PACKET_MAX 96
// 只有加密、认证、MTU及订阅均就绪时 connected 才为真。
// Connected means encryption, authentication, MTU and subscription are all ready.
typedef struct {
    bool active, connected, simulated;
    uint32_t generation;
    int error;
} pico_dual_ble_status_t;
typedef struct {
    bool found;
    char ip[48];
    char ap_ssid[33]; ///< 直连热点名称；非直连为空。/ Direct AP name; empty on existing LAN.
    bool direct; ///< 发现项来自主机直连热点。/ Discovered through the primary direct AP.
} pico_dual_ble_peer_info_t;
esp_err_t pico_dual_ble_configure(int role, const char* code);
/// 强制按相同角色/配对码重建 BLE 会话；用于网络模式切换后的连接恢复。
/// Force a BLE session rebuild with the same role/code after a network-mode switch.
void pico_dual_ble_request_restart(void);
/// 主机广播可达 IP 与可选直连 SSID；认证码永不出现在广播中。
/// Primary advertises a reachable IP and optional direct-AP SSID; authentication codes never appear in advertisements.
void pico_dual_ble_set_pairing_info(const char* ip, const char* ap_ssid);
esp_err_t pico_dual_ble_scan_start(void);
void pico_dual_ble_scan_stop(void);
void pico_dual_ble_peer_info(pico_dual_ble_peer_info_t* out);
void pico_dual_ble_stop(void);
void pico_dual_ble_status(pico_dual_ble_status_t* out);
bool pico_dual_ble_can_send(void);
bool pico_dual_ble_send(const uint8_t* data, size_t size);
size_t pico_dual_ble_receive(uint8_t* data, size_t capacity);

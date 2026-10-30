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
esp_err_t pico_dual_ble_configure(int role, const char* code);
void pico_dual_ble_stop(void);
void pico_dual_ble_status(pico_dual_ble_status_t* out);
bool pico_dual_ble_can_send(void);
bool pico_dual_ble_send(const uint8_t* data, size_t size);
size_t pico_dual_ble_receive(uint8_t* data, size_t capacity);

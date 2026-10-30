/* SPDX-License-Identifier: Apache-2.0
 * 真实TF卡USB大容量传输；root先关闭所有本地文件和网络传书。
 * Real TF-card USB mass storage; the root first drains local files and wireless transfer.
 */
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

typedef enum {
    READ_PICO_USB_IDLE,
    READ_PICO_USB_STARTING,
    READ_PICO_USB_EXPORTED,
    READ_PICO_USB_STOPPING,
    READ_PICO_USB_ERROR,
} read_pico_usb_state_t;

typedef struct {
    read_pico_usb_state_t state;
    bool active;           ///< Any MSC/raw-card ownership remains; do not reopen local files.
    bool connected;       ///< The USB host configured this device.
    bool host_ejected;    ///< The host sent a successful eject command.
    bool writing;         ///< A block write callback is in progress.
    bool card_exported;
    bool debug_restored;  ///< Internal PHY has been returned to Serial/JTAG.
    uint32_t read_blocks;
    uint32_t written_blocks;
    esp_err_t last_error;
} read_pico_usb_status_t;

/// Call once before use. No USB enumeration or card access. / 首次准备，不切USB、不访问TF。
esp_err_t read_pico_usb_init(void);
/// Blocking lifecycle API; call on the root's maintenance worker, never during rendering.
/// Synchronously acquire the TF raw lease and enumerate MSC. Never exposes internal flash.
esp_err_t read_pico_usb_start(void);
/// Join MSC callbacks first, restore Serial/JTAG, then release the raw lease and verify normal remount.
/// Failures preserve remaining ownership for a retry; ESP_OK alone permits local file access.
esp_err_t read_pico_usb_stop(void);
/// Copy a lock-protected snapshot; no storage/USB operations. / 只复制快照。
void read_pico_usb_get_status(read_pico_usb_status_t *out);

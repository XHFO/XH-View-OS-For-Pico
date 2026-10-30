/* SPDX-License-Identifier: Apache-2.0
 * 受控TF块设备的USB MSC生命周期；无内部flash、PMU或eFuse写入。
 * USB MSC lifetime over a controlled TF block lease; no internal-flash, PMU or eFuse writes.
 */
#include "read_pico_usb.h"
#include "read_pico_sd.h"
#ifdef READ_PICO_USB_HOST_TEST
#include "pico_usb_host_env.h"
#else
#include "esp_heap_caps.h"
#include "esp_mac.h"
#include "esp_private/usb_phy.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "sdmmc_cmd.h"
#include "tusb.h"
#endif
#include <limits.h>
#include <stdio.h>
#include <string.h>

/* Only the official TinyUSB core is linked. The esp_tinyusb filesystem helper
 * automatically mounts on disconnect/eject and acknowledges deferred writes
 * before the SD write finishes. This adapter keeps the BSP's raw lease exclusive
 * until its own USB task has stopped, and completes writes synchronously.
 */
enum { USB_PORT = 0, USB_SECTOR = 512, USB_IO_SECTORS = 8,
       USB_IO_BYTES = USB_SECTOR * USB_IO_SECTORS, USB_TASK_STACK = 4096, USB_JOIN_MS = 15000 };
static portMUX_TYPE u_lock = portMUX_INITIALIZER_UNLOCKED;
static SemaphoreHandle_t u_operation, u_started, u_joined;
static sdmmc_card_t *u_card;
static uint32_t u_media_generation;
static uint8_t *u_sector;
static usb_phy_handle_t u_otg_phy, u_debug_phy;
static bool u_raw_owned, u_task_owned, u_task_running, u_stack_ready, u_stopping, u_accepting;
static esp_err_t u_start_error;
static read_pico_usb_status_t u_status = { .debug_restored = true };
static char u_serial[16];

static const tusb_desc_device_t u_device = {
    .bLength = sizeof(tusb_desc_device_t), .bDescriptorType = TUSB_DESC_DEVICE,
    .bcdUSB = 0x0200, .bDeviceClass = 0, .bDeviceSubClass = 0, .bDeviceProtocol = 0,
    .bMaxPacketSize0 = 64, .idVendor = 0x303a, .idProduct = 0x4002,
    .bcdDevice = 0x0100, .iManufacturer = 1, .iProduct = 2, .iSerialNumber = 3,
    .bNumConfigurations = 1,
};
static const uint8_t u_configuration[] = {
    TUD_CONFIG_DESCRIPTOR(1, 1, 0, TUD_CONFIG_DESC_LEN + TUD_MSC_DESC_LEN, 0, 100),
    TUD_MSC_DESCRIPTOR(0, 4, 0x01, 0x81, 64),
};

uint8_t const *tud_descriptor_device_cb(void) { return (uint8_t const *)&u_device; }
uint8_t const *tud_descriptor_configuration_cb(uint8_t index) { return index == 0 ? u_configuration : NULL; }
uint16_t const *tud_descriptor_string_cb(uint8_t index, uint16_t langid) {
    (void)langid;
    static uint16_t text[33];
    if (index == 0) { text[0] = (TUSB_DESC_STRING << 8) | 4; text[1] = 0x0409; return text; }
    const char *value = index == 1 ? "MindReset" : index == 2 ? "Read Pico TF" : index == 3 ? u_serial : index == 4 ? "TF card" : NULL;
    if (!value) return NULL;
    size_t n = strlen(value); if (n > 32) n = 32;
    for (size_t i = 0; i < n; ++i) text[i + 1] = (uint8_t)value[i];
    text[0] = (uint16_t)((TUSB_DESC_STRING << 8) | (2 * n + 2));
    return text;
}

static void usb_error(esp_err_t error) {
    portENTER_CRITICAL(&u_lock);
    u_status.last_error = error;
    if (error != ESP_OK) u_status.state = READ_PICO_USB_ERROR;
    portEXIT_CRITICAL(&u_lock);
}

void read_pico_usb_get_status(read_pico_usb_status_t *out) {
    if (!out) return;
    portENTER_CRITICAL(&u_lock); *out = u_status; portEXIT_CRITICAL(&u_lock);
}

/* True only while the card lease and the callback task both belong to MSC.
 * Never infer readiness from FAT mounted=false: that is expected during export.
 */
static bool usb_media_ready(uint8_t lun) {
    portENTER_CRITICAL(&u_lock);
    bool ready = lun == 0 && u_accepting && u_card && !u_status.host_ejected;
    portEXIT_CRITICAL(&u_lock);
    if (ready) {
        read_pico_sd_info_t info = {0};
        esp_err_t error = read_pico_sd_get_info(&info);
        ready = error == ESP_OK && info.present && info.exported &&
            info.state == READ_PICO_SD_STATE_EXPORTED && info.media_generation == u_media_generation;
        if (!ready) {
            /* An observed removal/reinsertion invalidates this raw handle for
             * the remainder of the lease, even if CD is high again. */
            portENTER_CRITICAL(&u_lock); u_accepting = false; portEXIT_CRITICAL(&u_lock);
            usb_error(error == ESP_OK ? ESP_ERR_INVALID_STATE : error);
        }
    }
    if (!ready) tud_msc_set_sense(lun, SCSI_SENSE_NOT_READY, 0x3a, 0);
    return ready;
}

void tud_mount_cb(void) {
    portENTER_CRITICAL(&u_lock); u_status.connected = true; portEXIT_CRITICAL(&u_lock);
}
void tud_umount_cb(void) {
    portENTER_CRITICAL(&u_lock); u_status.connected = false; portEXIT_CRITICAL(&u_lock);
    /* Do not remount FAT from a USB callback. The stop worker owns that handoff. */
}
uint8_t tud_msc_get_maxlun_cb(void) { return 1; }
void tud_msc_inquiry_cb(uint8_t lun, uint8_t vendor[8], uint8_t product[16], uint8_t revision[4]) {
    (void)lun;
    memcpy(vendor, "MindRset", 8); memcpy(product, "Read Pico TF    ", 16); memcpy(revision, "0100", 4);
}
bool tud_msc_test_unit_ready_cb(uint8_t lun) { return usb_media_ready(lun); }
bool tud_msc_is_writable_cb(uint8_t lun) { return usb_media_ready(lun); }
void tud_msc_capacity_cb(uint8_t lun, uint32_t *blocks, uint16_t *size) {
    *blocks = 0; *size = USB_SECTOR;
    if (usb_media_ready(lun)) *blocks = (uint32_t)u_card->csd.capacity;
}
bool tud_msc_start_stop_cb(uint8_t lun, uint8_t power_condition, bool start, bool load_eject) {
    (void)power_condition;
    if (lun != 0) return false;
    portENTER_CRITICAL(&u_lock);
    bool accepted = u_accepting && u_card;
    if (accepted && load_eject) u_status.host_ejected = !start;
    portEXIT_CRITICAL(&u_lock);
    return accepted;
}

static int32_t usb_blocks(uint8_t lun, uint32_t lba, uint32_t offset, void *buffer, uint32_t length, bool write) {
    if (!usb_media_ready(lun)) return -1;
    const uint64_t address = (uint64_t)lba * USB_SECTOR + offset;
    const uint64_t capacity = (uint64_t)u_card->csd.capacity * USB_SECTOR;
    if (!buffer || !length || length > INT32_MAX || address >= capacity || length > capacity - address) {
        tud_msc_set_sense(lun, SCSI_SENSE_ILLEGAL_REQUEST, 0x21, 0); return -1;
    }
    portENTER_CRITICAL(&u_lock); u_status.writing = write; portEXIT_CRITICAL(&u_lock);
    uint8_t *bytes = buffer; uint32_t remaining = length;
    uint64_t at = address; esp_err_t error = ESP_OK; bool media_lost = false;
    while (remaining) {
        /* The admission check already verified the first batch. Recheck every
         * later batch so removal/generation changes still revoke the lease.
         * A 4 KiB MSC request needs one CD/I2C check, not two checks per sector.
         */
        if (remaining != length && !usb_media_ready(lun)) {
            error = ESP_ERR_INVALID_STATE; media_lost = true; break;
        }
        size_t sector = (size_t)(at / USB_SECTOR), within = (size_t)(at % USB_SECTOR);
        size_t sectors = 1, count = USB_SECTOR - within;
        if (!within && remaining >= USB_SECTOR) {
            sectors = remaining / USB_SECTOR;
            if (sectors > USB_IO_SECTORS) sectors = USB_IO_SECTORS;
            count = sectors * USB_SECTOR;
        } else if (count > remaining) count = remaining;
        if (!write || within || count < USB_SECTOR) {
            error = sdmmc_read_sectors(u_card, u_sector, sector, sectors);
            if (error != ESP_OK) break;
        }
        if (write) {
            memcpy(u_sector + within, bytes, count);
            error = sdmmc_write_sectors(u_card, u_sector, sector, sectors);
            if (error != ESP_OK) break;
        } else memcpy(bytes, u_sector + within, count);
        portENTER_CRITICAL(&u_lock);
        if (write) u_status.written_blocks += sectors; else u_status.read_blocks += sectors;
        portEXIT_CRITICAL(&u_lock);
        bytes += count; at += count; remaining -= (uint32_t)count;
    }
    portENTER_CRITICAL(&u_lock); u_status.writing = false; portEXIT_CRITICAL(&u_lock);
    if (error != ESP_OK) {
        usb_error(error);
        if (!media_lost) tud_msc_set_sense(lun, SCSI_SENSE_MEDIUM_ERROR, write ? 0x0c : 0x11, 0);
        return -1;
    }
    return (int32_t)length;
}
int32_t tud_msc_read10_cb(uint8_t lun, uint32_t lba, uint32_t offset, void *buffer, uint32_t length) {
    return usb_blocks(lun, lba, offset, buffer, length, false);
}
int32_t tud_msc_write10_cb(uint8_t lun, uint32_t lba, uint32_t offset, uint8_t *buffer, uint32_t length) {
    return usb_blocks(lun, lba, offset, buffer, length, true);
}
int32_t tud_msc_scsi_cb(uint8_t lun, uint8_t const command[16], void *buffer, uint16_t length) {
    (void)buffer; (void)length;
    if (!usb_media_ready(lun)) return -1;
    /* Every write is already synchronous; there is no deferred device cache.
     * PREVENT/ALLOW is accepted, while user-controlled stop remains explicit.
     */
    if (command[0] == 0x35 || command[0] == 0x1e) return 0;
    tud_msc_set_sense(lun, SCSI_SENSE_ILLEGAL_REQUEST, 0x20, 0); return -1;
}

static void usb_task(void *arg) {
    (void)arg;
    const tusb_rhport_init_t config = { .role = TUSB_ROLE_DEVICE, .speed = TUSB_SPEED_FULL };
    bool initialized = tusb_rhport_init(USB_PORT, &config);
    portENTER_CRITICAL(&u_lock);
    u_stack_ready = tud_inited(); u_task_running = true;
    u_start_error = initialized ? ESP_OK : ESP_FAIL;
    portEXIT_CRITICAL(&u_lock);
    xSemaphoreGive(u_started);
    if (initialized) {
        for (;;) {
            portENTER_CRITICAL(&u_lock); bool stop = u_stopping; portEXIT_CRITICAL(&u_lock);
            if (stop) break;
            tud_task_ext(50, false);
        }
    }
    if (tud_inited()) {
        tud_disconnect();
        bool stopped = tud_deinit(USB_PORT);
        portENTER_CRITICAL(&u_lock); u_stack_ready = !stopped; portEXIT_CRITICAL(&u_lock);
        if (!stopped) usb_error(ESP_FAIL);
    }
    portENTER_CRITICAL(&u_lock); u_task_running = false; portEXIT_CRITICAL(&u_lock);
    /* No card, stack or shared-state access follows this join signal. */
    xSemaphoreGive(u_joined);
    vTaskDelete(NULL);
}

esp_err_t read_pico_usb_init(void) {
    if (u_operation) return ESP_OK;
    SemaphoreHandle_t operation = xSemaphoreCreateMutex();
    SemaphoreHandle_t started = xSemaphoreCreateBinary();
    SemaphoreHandle_t joined = xSemaphoreCreateBinary();
    if (!operation || !started || !joined) {
        if (operation) vSemaphoreDelete(operation);
        if (started) vSemaphoreDelete(started);
        if (joined) vSemaphoreDelete(joined);
        return ESP_ERR_NO_MEM;
    }
    u_started = started; u_joined = joined; u_operation = operation;
    return ESP_OK;
}

static esp_err_t usb_debug_restore(void) {
    if (u_debug_phy) return ESP_OK;
    const usb_phy_config_t config = { .controller = USB_PHY_CTRL_SERIAL_JTAG, .target = USB_PHY_TARGET_INT };
    esp_err_t error = usb_new_phy(&config, &u_debug_phy);
    if (error == ESP_OK) {
        portENTER_CRITICAL(&u_lock); u_status.debug_restored = true; portEXIT_CRITICAL(&u_lock);
    }
    return error;
}

static esp_err_t usb_stop_locked(void) {
    portENTER_CRITICAL(&u_lock);
    u_accepting = false; u_stopping = true; u_status.state = READ_PICO_USB_STOPPING;
    portEXIT_CRITICAL(&u_lock);
    if (u_task_owned) {
        if (xSemaphoreTake(u_joined, pdMS_TO_TICKS(USB_JOIN_MS)) != pdTRUE) return ESP_ERR_TIMEOUT;
        u_task_owned = false;
    }
    if (u_stack_ready) {
        if (!tud_deinit(USB_PORT)) return ESP_FAIL;
        u_stack_ready = false;
    }
    if (u_otg_phy) {
        esp_err_t error = usb_del_phy(u_otg_phy); if (error != ESP_OK) return error;
        u_otg_phy = NULL;
    }
    esp_err_t error = usb_debug_restore(); if (error != ESP_OK) return error;
    if (u_raw_owned) {
        /* BSP may deinitialize/free the raw handle even when remount fails.
         * A retry invokes raw_release again; never reuse the old pointer.
         */
        u_card = NULL;
        error = read_pico_sd_raw_release(); if (error != ESP_OK) return error;
        u_raw_owned = false;
    }
    heap_caps_free(u_sector); u_sector = NULL;
    portENTER_CRITICAL(&u_lock);
    u_status.state = READ_PICO_USB_IDLE; u_status.active = u_status.card_exported = false;
    u_status.connected = u_status.writing = false; u_status.last_error = ESP_OK;
    portEXIT_CRITICAL(&u_lock);
    return ESP_OK;
}

esp_err_t read_pico_usb_start(void) {
    if (!u_operation) return ESP_ERR_INVALID_STATE;
    if (xSemaphoreTake(u_operation, 0) != pdTRUE) return ESP_ERR_INVALID_STATE;
    esp_err_t error = ESP_OK;
    portENTER_CRITICAL(&u_lock); bool busy = u_status.active || u_task_running; portEXIT_CRITICAL(&u_lock);
    if (busy || tud_inited()) { error = ESP_ERR_INVALID_STATE; goto done; }
    portENTER_CRITICAL(&u_lock);
    u_status = (read_pico_usb_status_t){ .state = READ_PICO_USB_STARTING, .debug_restored = true };
    portEXIT_CRITICAL(&u_lock);
    u_sector = heap_caps_malloc(USB_IO_BYTES, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    if (!u_sector) { error = ESP_ERR_NO_MEM; goto done; }
    error = read_pico_sd_raw_acquire(&u_card);
    if (error != ESP_OK) { heap_caps_free(u_sector); u_sector = NULL; goto done; }
    u_raw_owned = true;
    portENTER_CRITICAL(&u_lock); u_status.active = u_status.card_exported = true; portEXIT_CRITICAL(&u_lock);
    if (!u_card || u_card->csd.sector_size != USB_SECTOR || !u_card->csd.capacity || (uint64_t)u_card->csd.capacity > UINT32_MAX) {
        error = ESP_ERR_NOT_SUPPORTED; goto unwind;
    }
    read_pico_sd_info_t media = {0};
    error = read_pico_sd_get_info(&media);
    if (error != ESP_OK || !media.present || !media.exported || media.state != READ_PICO_SD_STATE_EXPORTED) {
        if (error == ESP_OK) error = ESP_ERR_INVALID_STATE;
        goto unwind;
    }
    u_media_generation = media.media_generation;
    uint8_t mac[6];
    error = esp_read_mac(mac, ESP_MAC_WIFI_STA); if (error != ESP_OK) goto unwind;
    snprintf(u_serial, sizeof(u_serial), "%02X%02X%02X%02X%02X%02X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    if (u_debug_phy) {
        error = usb_del_phy(u_debug_phy); if (error != ESP_OK) goto unwind;
        u_debug_phy = NULL;
    }
    const usb_phy_config_t config = { .controller = USB_PHY_CTRL_OTG, .target = USB_PHY_TARGET_INT,
        .otg_mode = USB_OTG_MODE_DEVICE, .otg_speed = USB_PHY_SPEED_FULL };
    portENTER_CRITICAL(&u_lock); u_status.debug_restored = false; portEXIT_CRITICAL(&u_lock);
    error = usb_new_phy(&config, &u_otg_phy); if (error != ESP_OK) goto unwind;
    xSemaphoreTake(u_started, 0); xSemaphoreTake(u_joined, 0);
    portENTER_CRITICAL(&u_lock); u_stopping = false; u_accepting = true; portEXIT_CRITICAL(&u_lock);
    u_task_owned = true;
    if (xTaskCreatePinnedToCore(usb_task, "pico-usb-msc", USB_TASK_STACK, NULL, 5, NULL, 0) != pdPASS) {
        u_task_owned = false; error = ESP_ERR_NO_MEM; goto unwind;
    }
    if (xSemaphoreTake(u_started, pdMS_TO_TICKS(5000)) != pdTRUE) { error = ESP_ERR_TIMEOUT; goto unwind; }
    error = u_start_error; if (error != ESP_OK) goto unwind;
    portENTER_CRITICAL(&u_lock); u_status.state = READ_PICO_USB_EXPORTED; portEXIT_CRITICAL(&u_lock);
    goto done;
unwind:
    { esp_err_t cleanup = usb_stop_locked(); if (cleanup != ESP_OK) error = cleanup; }
done:
    if (error != ESP_OK) usb_error(error);
    xSemaphoreGive(u_operation);
    return error;
}

esp_err_t read_pico_usb_stop(void) {
    if (!u_operation) return ESP_ERR_INVALID_STATE;
    if (xSemaphoreTake(u_operation, 0) != pdTRUE) return ESP_ERR_INVALID_STATE;
    esp_err_t error = u_status.active ? usb_stop_locked() : ESP_OK;
    if (error != ESP_OK) usb_error(error);
    xSemaphoreGive(u_operation);
    return error;
}

#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <pthread.h>
#include "esp_err.h"
#include "sdmmc_cmd.h"
typedef pthread_mutex_t portMUX_TYPE;
#define portMUX_INITIALIZER_UNLOCKED PTHREAD_MUTEX_INITIALIZER
#define portENTER_CRITICAL(lock) ((void)pthread_mutex_lock(lock))
#define portEXIT_CRITICAL(lock) ((void)pthread_mutex_unlock(lock))
typedef struct mock_semaphore *SemaphoreHandle_t;
#define pdTRUE 1
#define pdPASS 1
#define pdMS_TO_TICKS(ms) ((unsigned)(ms) / 50u + ((ms) != 0))
SemaphoreHandle_t xSemaphoreCreateMutex(void);
SemaphoreHandle_t xSemaphoreCreateBinary(void);
void vSemaphoreDelete(SemaphoreHandle_t);
int xSemaphoreTake(SemaphoreHandle_t, unsigned);
int xSemaphoreGive(SemaphoreHandle_t);
int xTaskCreatePinnedToCore(void (*)(void *), const char *, unsigned, void *, unsigned, void *, int);
void vTaskDelete(void *);
#define MALLOC_CAP_DMA 1
#define MALLOC_CAP_INTERNAL 2
void *heap_caps_malloc(size_t, unsigned);
void heap_caps_free(void *);
#define ESP_MAC_WIFI_STA 0
esp_err_t esp_read_mac(uint8_t *, int);
typedef struct mock_phy *usb_phy_handle_t;
enum { USB_PHY_CTRL_OTG = 1, USB_PHY_CTRL_SERIAL_JTAG, USB_PHY_TARGET_INT,
       USB_OTG_MODE_DEVICE, USB_PHY_SPEED_FULL };
typedef struct { int controller, target, otg_mode, otg_speed; } usb_phy_config_t;
esp_err_t usb_new_phy(const usb_phy_config_t *, usb_phy_handle_t *);
esp_err_t usb_del_phy(usb_phy_handle_t);
enum { TUSB_DESC_DEVICE = 1, TUSB_DESC_STRING = 3, TUSB_ROLE_DEVICE = 1, TUSB_SPEED_FULL = 1,
       SCSI_SENSE_NOT_READY = 2, SCSI_SENSE_MEDIUM_ERROR = 3, SCSI_SENSE_ILLEGAL_REQUEST = 5 };
typedef struct __attribute__((packed)) {
    uint8_t bLength, bDescriptorType; uint16_t bcdUSB;
    uint8_t bDeviceClass, bDeviceSubClass, bDeviceProtocol, bMaxPacketSize0;
    uint16_t idVendor, idProduct, bcdDevice;
    uint8_t iManufacturer, iProduct, iSerialNumber, bNumConfigurations;
} tusb_desc_device_t;
#define TUD_CONFIG_DESC_LEN 9
#define TUD_MSC_DESC_LEN 23
/* SDK descriptor encoding is covered by the final target build; these standard
 * descriptor bytes let the production callback boundary be checked on a host. */
#define TUD_CONFIG_DESCRIPTOR(num, count, str, len, attr, power) \
    9, 2, (len) & 255, ((len) >> 8) & 255, count, num, str, 0x80 | (attr), (power) / 2
#define TUD_MSC_DESCRIPTOR(itf, str, epout, epin, size) \
    9, 4, itf, 0, 2, 8, 6, 0x50, str, 7, 5, epout, 2, size, 0, 0, 7, 5, epin, 2, size, 0, 0
typedef struct { int role, speed; } tusb_rhport_init_t;
bool tusb_rhport_init(uint8_t, const tusb_rhport_init_t *);
bool tud_inited(void);
bool tud_deinit(uint8_t);
void tud_disconnect(void);
void tud_task_ext(uint32_t, bool);
bool tud_msc_set_sense(uint8_t, uint8_t, uint8_t, uint8_t);

uint8_t const *tud_descriptor_device_cb(void);
uint8_t const *tud_descriptor_configuration_cb(uint8_t);
uint16_t const *tud_descriptor_string_cb(uint8_t, uint16_t);
void tud_mount_cb(void);
void tud_umount_cb(void);
uint8_t tud_msc_get_maxlun_cb(void);
void tud_msc_inquiry_cb(uint8_t, uint8_t[8], uint8_t[16], uint8_t[4]);
bool tud_msc_test_unit_ready_cb(uint8_t);
bool tud_msc_is_writable_cb(uint8_t);
void tud_msc_capacity_cb(uint8_t, uint32_t *, uint16_t *);
bool tud_msc_start_stop_cb(uint8_t, uint8_t, bool, bool);
int32_t tud_msc_read10_cb(uint8_t, uint32_t, uint32_t, void *, uint32_t);
int32_t tud_msc_write10_cb(uint8_t, uint32_t, uint32_t, uint8_t *, uint32_t);
int32_t tud_msc_scsi_cb(uint8_t, uint8_t const[16], void *, uint16_t);

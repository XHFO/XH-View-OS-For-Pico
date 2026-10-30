/* SPDX-License-Identifier: Apache-2.0
 * 主机侧SDK边界声明，不替换实际引擎。/ Host SDK boundaries without replacing the engine.
 */
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include "esp_err.h"
typedef int portMUX_TYPE;
#define portMUX_INITIALIZER_UNLOCKED 0
#define portENTER_CRITICAL(p) ((void)(p))
#define portEXIT_CRITICAL(p) ((void)(p))
#define pdMS_TO_TICKS(ms) (ms)
#define pdTRUE 1
#define pdPASS 1
typedef struct host_queue *QueueHandle_t;
QueueHandle_t xQueueCreate(unsigned, size_t);
int xQueueSend(QueueHandle_t, const void *, unsigned);
int xQueueReceive(QueueHandle_t, void *, unsigned);
void vQueueDelete(QueueHandle_t);
int xTaskCreate(void (*)(void *), const char *, unsigned, void *, unsigned, void *);
void vTaskDelay(unsigned);
int64_t esp_timer_get_time(void);
typedef const char *esp_event_base_t;
typedef void *esp_event_handler_instance_t;
typedef struct { int id; } esp_netif_t;
typedef struct { unsigned char bytes[4]; } host_ip_t;
typedef struct { esp_netif_t *esp_netif; struct {host_ip_t ip;} ip_info; } ip_event_got_ip_t;
#define IPSTR "%u.%u.%u.%u"
#define IP2STR(p) (p)->bytes[0],(p)->bytes[1],(p)->bytes[2],(p)->bytes[3]
extern const char connectivity_host_wifi_event[], connectivity_host_ip_event[];
#define WIFI_EVENT connectivity_host_wifi_event
#define IP_EVENT connectivity_host_ip_event
enum { ESP_EVENT_ANY_ID=-1, WIFI_EVENT_SCAN_DONE=1, WIFI_EVENT_STA_DISCONNECTED=2, IP_EVENT_STA_LOST_IP=3, IP_EVENT_STA_GOT_IP=4, WIFI_EVENT_STA_START=5 };
enum { WIFI_STORAGE_RAM, WIFI_MODE_STA, WIFI_IF_STA, WIFI_SCAN_TYPE_ACTIVE, WPA3_SAE_PWE_BOTH };
enum { WIFI_AUTH_OPEN=0, WIFI_AUTH_WPA2_PSK=3, WIFI_AUTH_WPA_WPA2_PSK=4, WIFI_AUTH_WPA3_PSK=6, WIFI_AUTH_WPA2_WPA3_PSK=7, WIFI_AUTH_OWE=9 };
typedef struct {
    int static_rx_buf_num,dynamic_rx_buf_num,tx_buf_type,static_tx_buf_num,dynamic_tx_buf_num;
    int rx_mgmt_buf_type,rx_mgmt_buf_num,cache_tx_buf_num,rx_ba_win,ampdu_rx_enable,ampdu_tx_enable;
    int mgmt_sbuf_num,magic;
    uint64_t feature_caps;
} wifi_init_config_t;
#define WIFI_INIT_CONFIG_DEFAULT() ((wifi_init_config_t){.static_rx_buf_num=16,.dynamic_rx_buf_num=32,.tx_buf_type=0,.static_tx_buf_num=16,.cache_tx_buf_num=32,.rx_ba_win=16,.rx_mgmt_buf_num=5,.mgmt_sbuf_num=32,.ampdu_rx_enable=1,.ampdu_tx_enable=1,.feature_caps=0x1234,.magic=0x5a5a})
typedef struct { struct { unsigned char ssid[32],password[64]; struct {int authmode;} threshold; struct {bool capable;} pmf_cfg; int sae_pwe_h2e; } sta; } wifi_config_t;
typedef struct { bool show_hidden; int scan_type; struct { struct {int min,max;} active; } scan_time; } wifi_scan_config_t;
typedef struct { uint32_t status; uint8_t number,scan_id; } wifi_event_sta_scan_done_t;
enum { ESP_ERR_WIFI_NOT_STARTED=0x3002, ESP_ERR_WIFI_STATE=0x3006 };
typedef struct { unsigned char ssid[33]; int8_t rssi; int authmode; } wifi_ap_record_t;
esp_err_t esp_netif_init(void);
esp_err_t esp_event_loop_create_default(void);
esp_err_t esp_event_loop_delete_default(void);
esp_netif_t *esp_netif_create_default_wifi_sta(void);
void esp_netif_destroy_default_wifi(esp_netif_t *);
esp_err_t esp_event_handler_instance_register(esp_event_base_t, int, void (*)(void *,esp_event_base_t,int32_t,void *), void *, esp_event_handler_instance_t *);
esp_err_t esp_event_handler_instance_unregister(esp_event_base_t,int,esp_event_handler_instance_t);
esp_err_t esp_wifi_init(const wifi_init_config_t *);
esp_err_t esp_wifi_deinit(void);
esp_err_t esp_wifi_set_storage(int);
esp_err_t esp_wifi_set_mode(int);
esp_err_t esp_wifi_start(void);
esp_err_t esp_wifi_stop(void);
esp_err_t esp_wifi_set_config(int,const wifi_config_t *);
esp_err_t esp_wifi_connect(void);
esp_err_t esp_wifi_disconnect(void);
esp_err_t esp_wifi_scan_start(const wifi_scan_config_t *,bool);
esp_err_t esp_wifi_scan_stop(void);
esp_err_t esp_wifi_clear_ap_list(void);
esp_err_t esp_wifi_scan_get_ap_num(uint16_t *);
esp_err_t esp_wifi_scan_get_ap_record(wifi_ap_record_t *);
esp_err_t esp_wifi_sta_get_ap_info(wifi_ap_record_t *);

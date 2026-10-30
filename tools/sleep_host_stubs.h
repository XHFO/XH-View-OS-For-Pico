/* 中文：真实sleep.c的确定性硬件替身。/ English: Deterministic hardware shims for the actual sleep.c. */
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include "../main/settings.h"
#include "../main/power_touch.h"
#include "../main/ui/ui_image.h"
#include "../main/ui/ui_packed_asset.h"
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_FAIL 1
#define ESP_ERR_INVALID_STATE 2
#define ESP_ERR_NOT_SUPPORTED 3
#define ESP_ERR_TIMEOUT 4
typedef int gpio_num_t;
#define GPIO_NUM_NC (-1)
#define READ_PICO_IOE_INT_GPIO 41
#define GPIO_MODE_INPUT 1
#define GPIO_PULLUP_ENABLE 1
#define GPIO_PULLDOWN_DISABLE 0
#define GPIO_INTR_DISABLE 0
#define GPIO_INTR_LOW_LEVEL 1
#define ESP_SLEEP_WAKEUP_TIMER 1
#define ESP_SLEEP_WAKEUP_GPIO 2
#define APP_LOCK_IGNORE_BOOT_MS 2000
#define MODE_GC16 3
#define pdMS_TO_TICKS(x) (x)
#define ESP_LOGI(tag,...) ((void)(tag))
#define ESP_LOGW(tag,...) ((void)(tag))
#define ESP_LOGE(tag,...) ((void)(tag))
typedef struct { uint64_t pin_bit_mask; int mode,pull_up_en,pull_down_en,intr_type; } gpio_config_t;
typedef void* cst836u_handle_t;
typedef void* sc7a20h_handle_t;
typedef enum { CST836U_MODE_NORMAL, CST836U_MODE_DEEPSLEEP } cst836u_mode_t;
typedef struct { bool touched; uint8_t count; uint16_t x,y; } cst836u_touch_t;
typedef struct { int unused; } EpdiyHighlevelState;
typedef struct { uint8_t aoi1_src; } sc7a20h_events_t;
typedef struct { bool ia; } sc7a20h_aoi_src_t;
typedef struct { int ths_mg,duration; } sc7a20h_motion_cfg_t;
#define SC7A20H_MOTION_DEFAULT() ((sc7a20h_motion_cfg_t){0})
typedef struct { uint8_t key_state; } test_pmu_status_t;
const char* esp_err_to_name(esp_err_t err);
esp_err_t sleep_cpu_configure(bool enabled);
int64_t esp_timer_get_time(void);
void vTaskDelay(int ms);
esp_err_t gpio_config(const gpio_config_t* config);
esp_err_t gpio_wakeup_enable(gpio_num_t pin,int mode);
esp_err_t gpio_wakeup_disable(gpio_num_t pin);
int gpio_get_level(gpio_num_t pin);
esp_err_t esp_sleep_enable_gpio_wakeup(void);
esp_err_t esp_sleep_enable_timer_wakeup(uint64_t us);
esp_err_t esp_sleep_disable_wakeup_source(int source);
esp_err_t esp_light_sleep_start(void);
int esp_sleep_get_wakeup_cause(void);
esp_err_t cst836u_read(cst836u_handle_t tp,cst836u_touch_t* touch);
esp_err_t cst836u_set_mode(cst836u_handle_t tp,cst836u_mode_t mode);
cst836u_mode_t cst836u_get_mode(cst836u_handle_t tp);
esp_err_t cst836u_wake(cst836u_handle_t tp);
gpio_num_t cst836u_int_gpio(cst836u_handle_t tp);
bool cst836u_int_asserted(cst836u_handle_t tp);
bool sc7a20h_powered(sc7a20h_handle_t acc);
esp_err_t sc7a20h_power_down(sc7a20h_handle_t acc);
esp_err_t sc7a20h_power_up(sc7a20h_handle_t acc);
esp_err_t sc7a20h_read_events(sc7a20h_handle_t acc,sc7a20h_events_t* events);
esp_err_t sc7a20h_ack_int(sc7a20h_handle_t acc);
void sc7a20h_aoi_decode(uint8_t src,sc7a20h_aoi_src_t* aoi);
int sc7a20h_int1_level(sc7a20h_handle_t acc);
gpio_num_t sc7a20h_int1_gpio(sc7a20h_handle_t acc);
esp_err_t sc7a20h_arm_pickup_wake(sc7a20h_handle_t acc,const sc7a20h_motion_cfg_t* cfg);
esp_err_t sc7a20h_config_light_sleep_wakeup(sc7a20h_handle_t acc);
esp_err_t sc7a20h_int1_begin(sc7a20h_handle_t acc);
bool read_pico_buzzer_score_busy(void);
esp_err_t read_pico_buzzer_score_stop(void);
esp_err_t read_pico_ioe_sleep(bool host_off);
esp_err_t read_pico_ioe_wake(void);
void read_pico_clear_ioe_int(void);
bool read_pico_rails_on(void);
void read_pico_pmu_drain_events(void);
esp_err_t read_pico_pmu_poll(void);
const test_pmu_status_t* read_pico_pmu_get(void);
typedef enum {PMU_PRODUCT_KEY_NONE,PMU_PRODUCT_KEY_SHORT,PMU_PRODUCT_KEY_LONG} pmu_product_key_action_t;
pmu_product_key_action_t read_pico_pmu_take_product_key(void);
bool read_pico_pmu_take_key_wakeup(void);
esp_err_t read_pico_pmu_power_off(void);
esp_err_t read_pico_pmu_report_sleep(void);
void pmu_selftest_prepare_powerdown(void);
void epd_poweroff(void);
void epd_poweron(void);
void epd_clear(void);
int epd_width(void);
int epd_height(void);
int epd_get_rotation(void);
void epd_hl_set_all_white(EpdiyHighlevelState* hl);
void epd_hl_update_screen_from_white(EpdiyHighlevelState* hl,int mode,int temperature);
uint8_t* epd_hl_get_framebuffer(EpdiyHighlevelState* hl);

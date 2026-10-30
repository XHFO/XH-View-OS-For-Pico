/* SPDX-License-Identifier: Apache-2.0
 * 中文：文字输入期间独立采样CST，I2C事务沿用IDF主总线锁；暂停后电源/旧页面才接管CST。
 * English: Sample CST independently during text entry, using the IDF master-bus transaction lock; pause before power/legacy ownership.
 */
#include "app_touch_buffer.h"
#include "app_touch_queue.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

static app_touch_queue_t queue;
static SemaphoreHandle_t gate;
static cst836u_handle_t device;
static TaskHandle_t worker;
static bool active;
static bool unavailable;
static int orientation;

static void sample_task(void *arg){
    (void)arg;
    for(;;){
        bool sampled=false;
        if(xSemaphoreTake(gate,portMAX_DELAY)==pdTRUE){
            if(active){
                sampled=true;
                app_touch_sample_t sample={0};
                sample.error=cst836u_read(device,&sample.touch);
                sample.ms=esp_timer_get_time()/1000;
                app_touch_queue_push(&queue,&sample);
            }
            xSemaphoreGive(gate);
        }
        if(sampled)vTaskDelay(pdMS_TO_TICKS(5));
        else ulTaskNotifyTake(pdTRUE,portMAX_DELAY);
    }
}
bool app_touch_buffer_enable(cst836u_handle_t tp,bool enabled,int rotation){
    if(!gate){
        if(!enabled)return false;
        if(unavailable)return false;
        gate=xSemaphoreCreateMutex();
        if(!gate){unavailable=true;ESP_LOGW("editor_touch","touch buffer allocation unavailable; direct sampling fallback");return false;}
        if(xTaskCreate(sample_task,"editor_touch",4096,NULL,5,&worker)!=pdPASS){vSemaphoreDelete(gate);gate=NULL;unavailable=true;ESP_LOGW("editor_touch","touch worker unavailable; direct sampling fallback");return false;}
    }
    xSemaphoreTake(gate,portMAX_DELAY);
    bool wake=!active&&enabled;
    if(active!=enabled||device!=tp||orientation!=rotation){app_touch_queue_reset(&queue);device=tp;orientation=rotation;}
    active=enabled&&tp!=NULL;
    xSemaphoreGive(gate);
    if(wake)xTaskNotifyGive(worker);
    return active;
}
esp_err_t app_touch_buffer_read(cst836u_touch_t *touch,int64_t *sample_ms,bool *pending){
    xSemaphoreTake(gate,portMAX_DELAY);
    app_touch_sample_t sample=app_touch_queue_pop(&queue,esp_timer_get_time()/1000);
    *pending=queue.count!=0||queue.overflow;*touch=sample.touch;*sample_ms=sample.ms;
    xSemaphoreGive(gate);
    if(sample.error==ESP_ERR_NO_MEM)ESP_LOGW("editor_touch","touch queue overflow; cancel contact until release");
    return sample.error;
}

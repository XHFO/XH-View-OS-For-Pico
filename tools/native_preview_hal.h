/* SPDX-License-Identifier: Apache-2.0
 * 中文：网页原生运行器的平台边界；只使用私有本地数据，不接触硬件。
 * English: Host platform boundary for the native browser runtime; private local data, no hardware access.
 */
#pragma once
#ifndef __ASSEMBLER__
#define _POSIX_C_SOURCE 200809L
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include "esp_err.h"
#define MALLOC_CAP_SPIRAM (1u<<10)
#define MALLOC_CAP_8BIT (1u<<2)
#define MALLOC_CAP_INTERNAL (1u<<11)
#define MALLOC_CAP_DEFAULT 0
#define pdPASS 1
#define pdFAIL 0
#define portMAX_DELAY 0xffffffffu
#define pdMS_TO_TICKS(ms) (ms)
typedef void *TaskHandle_t;
typedef void *QueueHandle_t;
typedef void *SemaphoreHandle_t;
typedef unsigned TickType_t;
typedef int BaseType_t;
typedef unsigned UBaseType_t;
void native_preview_log(const char *tag,const char *format,...);
#define ESP_LOGI native_preview_log
#define ESP_LOGW native_preview_log
#define ESP_LOGE native_preview_log
#define ESP_LOGD(...) ((void)0)
#define ESP_ERROR_CHECK(x) ((void)(x))
void *heap_caps_malloc(size_t size,unsigned caps);
void *heap_caps_calloc(size_t count,size_t size,unsigned caps);
void *heap_caps_realloc(void *ptr,size_t size,unsigned caps);
void *heap_caps_aligned_alloc(size_t align,size_t size,unsigned caps);
void heap_caps_free(void *ptr);
size_t heap_caps_get_largest_free_block(unsigned caps);
size_t heap_caps_get_free_size(unsigned caps);
uint32_t esp_random(void);
int64_t esp_timer_get_time(void);
int xTaskCreateWithCaps(void (*fn)(void *),const char *name,unsigned stack,void *arg,int priority,TaskHandle_t *handle,unsigned caps);
int xTaskCreate(void (*fn)(void *),const char *name,unsigned stack,void *arg,int priority,TaskHandle_t *handle);
int xTaskCreatePinnedToCore(void (*fn)(void *),const char *name,unsigned stack,void *arg,int priority,TaskHandle_t *handle,int core);
void vTaskDeleteWithCaps(TaskHandle_t task);
void vTaskDelete(TaskHandle_t task);
void vTaskSuspend(TaskHandle_t task);
void vTaskDelay(unsigned ticks);
unsigned uxTaskGetStackHighWaterMark(TaskHandle_t task);
const char *native_preview_root(void);
const char *native_preview_font(void);
void native_preview_services_init(void);
void native_preview_services_tick(int64_t ms);
bool native_preview_service_scenario(const char *name);
void native_preview_service_status_json(FILE *out);

SemaphoreHandle_t xSemaphoreCreateMutex(void);int xSemaphoreTake(SemaphoreHandle_t,unsigned);int xSemaphoreGive(SemaphoreHandle_t);void vSemaphoreDelete(SemaphoreHandle_t);unsigned ulTaskNotifyTake(int,unsigned);void xTaskNotifyGive(TaskHandle_t);

size_t strlcpy(char*,const char*,size_t);
const char *esp_err_to_name(esp_err_t);
#include "native_preview_book_vfs.h"
#endif

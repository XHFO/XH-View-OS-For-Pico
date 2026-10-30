/* 中文：仅模拟任务交接，不模拟真实硬件。/ English: Simulates task handoff, never physical hardware. */
#pragma once
#define pdPASS 1
#define MALLOC_CAP_8BIT (1u<<2)
#define MALLOC_CAP_SPIRAM (1u<<10)
#define MALLOC_CAP_INTERNAL (1u<<11)
#define ESP_LOGI pg_test_log
typedef void *TaskHandle_t;
uint32_t esp_random(void);
int64_t esp_timer_get_time(void);
int xTaskCreateWithCaps(void (*fn)(void *),const char *name,unsigned stack,void *arg,int priority,TaskHandle_t *handle,unsigned caps);
void vTaskDeleteWithCaps(TaskHandle_t task);
void vTaskSuspend(TaskHandle_t task);
unsigned uxTaskGetStackHighWaterMark(TaskHandle_t task);
void vTaskDelay(unsigned ticks);
size_t heap_caps_get_largest_free_block(unsigned caps);
size_t heap_caps_get_free_size(unsigned caps);
void pg_test_log(const char *tag,const char *format,...);

/* 私有解析器固定使用PSRAM，不改变其它组件的全局hooks；cJSON仍来自原MIT许可依赖。
 * Private parser allocates in PSRAM without changing other components hooks; cJSON retains its dependency MIT license. */
#include <stdlib.h>
#ifdef ESP_PLATFORM
#include "esp_heap_caps.h"
static void *home_json_allocate(size_t n){return heap_caps_malloc(n,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);}
#else
static void *home_json_allocate(size_t n){return malloc(n);}
#endif
#include "home_link_json.h"
#define malloc home_json_allocate
#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
#endif
#include "cJSON.c"
#ifdef __clang__
#pragma clang diagnostic pop
#endif

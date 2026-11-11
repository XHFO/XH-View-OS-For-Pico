#pragma once
typedef struct { int unused; } esp_partition_t;
#define ESP_PARTITION_TYPE_APP 0
#define ESP_PARTITION_SUBTYPE_APP_OTA_1 17
const esp_partition_t *esp_partition_find_first(int,int,const char *);

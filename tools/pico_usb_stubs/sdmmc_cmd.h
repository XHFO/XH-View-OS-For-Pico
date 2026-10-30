#pragma once
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
typedef struct { struct { uint64_t capacity; uint16_t sector_size; } csd; } sdmmc_card_t;
esp_err_t sdmmc_read_sectors(sdmmc_card_t *, void *, size_t, size_t);
esp_err_t sdmmc_write_sectors(sdmmc_card_t *, const void *, size_t, size_t);

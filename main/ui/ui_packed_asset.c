/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 * 中文：启动和锁屏原始P4B外层压缩流的共同ROM解码器，内层像素验证仍归ui_image。
 * English: Shared ROM decoder for outer splash/lock P4B streams; ui_image retains inner pixel validation.
 * 冻结：云端登录需要APP空间，改为共享无损封装；固定64KiB上限、临时PSRAM，失败不返回部分数据。
 * Frozen: Cloud login needs APP room, so share a lossless wrapper; cap output at 64KiB in transient PSRAM and never return partial data.
 */
#include "ui_packed_asset.h"
#include <stdlib.h>
#include <string.h>
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "miniz.h"

#define ASSET_HEADER_BYTES 16u
#define ASSET_MAX_PACKED_BYTES (UI_PACKED_ASSET_MAX_BYTES + 1024u)
#define ASSET_PSRAM (MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)
static const char* TAG = "packed_asset";

uint8_t* ui_packed_asset_unpack(const uint8_t* packed, size_t packed_bytes, size_t* image_bytes) {
    static const uint8_t magic[8] = {'P', 'I', 'C', 'O', '4', 'D', 'F', '1'};
    if (!image_bytes) return NULL;
    *image_bytes = 0;
    if (!packed || packed_bytes < ASSET_HEADER_BYTES || packed_bytes > ASSET_MAX_PACKED_BYTES ||
        memcmp(packed, magic, sizeof(magic))) {
        ESP_LOGE(TAG, "Compressed asset header invalid");
        return NULL;
    }
    uint32_t expected = (uint32_t)packed[8] | ((uint32_t)packed[9] << 8) |
                        ((uint32_t)packed[10] << 16) | ((uint32_t)packed[11] << 24);
    uint32_t crc = (uint32_t)packed[12] | ((uint32_t)packed[13] << 8) |
                   ((uint32_t)packed[14] << 16) | ((uint32_t)packed[15] << 24);
    if (expected < 24u || expected > UI_PACKED_ASSET_MAX_BYTES) {
        ESP_LOGE(TAG, "Compressed asset size invalid: %u", (unsigned)expected);
        return NULL;
    }
    uint8_t* image = heap_caps_malloc(expected, ASSET_PSRAM);
    tinfl_decompressor* state = heap_caps_malloc(sizeof(*state), ASSET_PSRAM);
    if (!image || !state) {
        ESP_LOGE(TAG, "Compressed asset PSRAM allocation failed");
        free(image);
        free(state);
        return NULL;
    }
    tinfl_init(state);
    size_t input_bytes = packed_bytes - ASSET_HEADER_BYTES;
    size_t output_bytes = expected;
    tinfl_status status = tinfl_decompress(state, packed + ASSET_HEADER_BYTES, &input_bytes,
        image, image, &output_bytes, TINFL_FLAG_USING_NON_WRAPPING_OUTPUT_BUF);
    free(state);
    if (status != TINFL_STATUS_DONE || input_bytes != packed_bytes - ASSET_HEADER_BYTES ||
        output_bytes != expected || (uint32_t)mz_crc32(MZ_CRC32_INIT, image, expected) != crc) {
        ESP_LOGE(TAG, "Compressed asset stream/size/CRC invalid");
        free(image);
        return NULL;
    }
    *image_bytes = expected;
    return image;
}

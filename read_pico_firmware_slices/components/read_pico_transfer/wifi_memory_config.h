/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 * 小内存墨水屏产品共用的WiFi缓冲上限，先保留SDK默认结构，再限制数据缓冲。
 * Shared WiFi data-buffer caps for the memory-constrained reader, applied after SDK defaults.
 * 冻结：静态TX、管理帧、AMPDU与特性位沿用SDK；不修改BSP或Flash/PSRAM时序。
 * Frozen: retain SDK TX type, management buffers, AMPDU and feature bits; never alter BSP or flash/PSRAM timing.
 */
#pragma once

// esp_wifi.h或主机SDK边界须先定义配置结构。/ Include esp_wifi.h or the host SDK boundary before this header.
static inline wifi_init_config_t read_pico_wifi_init_config(void) {
    wifi_init_config_t config = WIFI_INIT_CONFIG_DEFAULT();
    // 数据静态池由16RX+16TX减至最多4+4，约少驻留38.4KB内部DMA内存。
    // Cap the default 16 RX + 16 TX data pools at 4 + 4, saving about 38.4 KB of resident internal DMA memory.
    if (config.static_rx_buf_num > 4) config.static_rx_buf_num = 4;
    if (config.tx_buf_type == 0 && config.static_tx_buf_num > 4) config.static_tx_buf_num = 4;
    if (config.dynamic_rx_buf_num == 0 || config.dynamic_rx_buf_num > 16) config.dynamic_rx_buf_num = 16;
    if (config.cache_tx_buf_num > 8) config.cache_tx_buf_num = 8;
    // 接收聚合窗口不大于静态接收池，也不放大较小的上游配置。
    // Keep the RX aggregation window within the static RX pool without enlarging smaller upstream settings.
    if (config.rx_ba_win > config.static_rx_buf_num) config.rx_ba_win = config.static_rx_buf_num;
    return config;
}

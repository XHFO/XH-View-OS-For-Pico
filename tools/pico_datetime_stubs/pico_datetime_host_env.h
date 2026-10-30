/* SPDX-License-Identifier: Apache-2.0
 * 中文：时间页的宿主边界；仅替换PMU、存储和系统钟，不访问电脑时钟。
 * English: Datetime test boundary; PMU/storage/clock doubles never touch the host clock.
 */
#pragma once
#include "pico_datetime.h"
#include "read_pico_pmu_protocol.h"
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_FAIL -1
typedef struct {uint32_t unix_sec;bool time_ok;uint8_t time_synced;uint16_t last_op_status;} pmu_snapshot_t;
bool read_pico_pmu_ready(void);
esp_err_t read_pico_pmu_cmd(uint16_t command,const uint8_t *data,uint8_t bytes);
const pmu_snapshot_t *read_pico_pmu_get(void);
bool datetime_test_set_clock(uint32_t utc);
int64_t datetime_test_get_clock(void);
bool datetime_test_network_online(void);
bool datetime_test_ntp_start(const char *server);
void datetime_test_ntp_stop(void);

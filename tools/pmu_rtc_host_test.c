/* SPDX-License-Identifier: Apache-2.0
 * 中文：真实PMU命令与刷新路径，覆盖旧RTC、传输失败及短响应。
 * English: Real PMU command/refresh paths under stale RTC, transport faults and short replies.
 */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "pmu_api_under_test.h"

#define ESP_FAIL (-1)
#define ESP_ERR_INVALID_STATE 0x103
#define ESP_ERR_INVALID_RESPONSE 0x108
#define ESP_ERR_TIMEOUT 0x107
#define ESP_LOGW(...) ((void)0)
#define TAG "fixture"
#define pdMS_TO_TICKS(ms) (ms)
static void vTaskDelay(unsigned ms) {(void)ms;}

static void *s_dev;
static pmu_snapshot_t s_snap;
static uint8_t s_last_payload[PMU_FRAME_PAYLOAD_SIZE], s_last_plen;
static uint16_t active_code;
static unsigned write_calls, time_reads, checks;
static bool write_failure, read_failure;
static uint8_t time_length, time_status;
static uint32_t next_utc;

static uint32_t rd32(const uint8_t *p) {
    return (uint32_t)p[0] | (uint32_t)p[1]<<8 | (uint32_t)p[2]<<16 | (uint32_t)p[3]<<24;
}
static void fill_req(pmu_frame_t *frame, uint16_t code, const uint8_t *payload, uint8_t plen) {
    (void)payload; (void)plen;
    memset(frame, 0, sizeof(*frame)); frame->code=code; frame->sequence=1;
}
static void recover_seq(void) {}
static esp_err_t pmu_write_reg(uint8_t reg, const uint8_t *data, size_t bytes) {
    assert(reg==PMU_REG_COMMAND && bytes==sizeof(pmu_frame_t));
    const pmu_frame_t *frame=(const pmu_frame_t *)data;
    assert(frame->code==PMU_CMD_TIME_GET || frame->code==PMU_CMD_CONFIG_GET || frame->code==PMU_CMD_ALARM_GET);
    active_code=frame->code; write_calls++;
    return write_failure?ESP_FAIL:ESP_OK;
}
static esp_err_t read_response_for(uint16_t seq, pmu_frame_t *frame) {
    (void)seq;
    memset(frame, 0, sizeof(*frame)); frame->status=PMU_STATUS_OK;
    if (active_code==PMU_CMD_TIME_GET) {
        time_reads++;
        if (read_failure) return ESP_ERR_TIMEOUT;
        frame->status=time_status; frame->payload_length=time_length;
        for (unsigned i=0;i<4;i++) frame->payload[i]=(uint8_t)(next_utc>>(8*i));
        frame->payload[6]=1;
    }
    return ESP_OK;
}
static void parse_config(const uint8_t *p) {(void)p;}
static void parse_diag(const uint8_t *p) {(void)p;}
static void parse_quick(const uint8_t *p) {(void)p;}
static void parse_event(const uint8_t *p) {(void)p;}
static bool parse_identity(const uint8_t *p) {(void)p; return true;}
static bool parse_status(const uint8_t *p) {(void)p; return true;}
static esp_err_t pmu_read_reg(uint8_t reg, uint8_t *data, size_t bytes) {
    (void)reg; memset(data, 0, bytes); return ESP_OK;
}

#include "pmu_rtc_under_test.c"

static void fresh(void) {
    s_dev=(void*)1;
    s_snap=(pmu_snapshot_t){.time_ok=true,.time_synced=1,.unix_sec=1700000000};
    write_failure=read_failure=false;
    time_length=8; time_status=PMU_STATUS_OK; next_utc=1792425600;
    write_calls=time_reads=0;
}
static void stale(void) {
    assert(!s_snap.time_ok && s_snap.unix_sec==1700000000 && s_snap.time_synced==1); checks++;
}
int main(void) {
    fresh(); s_dev=NULL;
    assert(read_pico_pmu_cmd(PMU_CMD_TIME_GET,NULL,0)==ESP_ERR_INVALID_STATE && !write_calls); stale();
    fresh(); write_failure=true;
    assert(read_pico_pmu_cmd(PMU_CMD_TIME_GET,NULL,0)==ESP_FAIL && !time_reads); stale();
    fresh(); read_failure=true;
    assert(read_pico_pmu_cmd(PMU_CMD_TIME_GET,NULL,0)==ESP_ERR_TIMEOUT); stale();
    for (unsigned length=0;length<8;length++) {
        fresh(); time_length=(uint8_t)length;
        assert(read_pico_pmu_cmd(PMU_CMD_TIME_GET,NULL,0)==ESP_ERR_INVALID_RESPONSE);
        assert(s_snap.last_err==ESP_ERR_INVALID_RESPONSE); stale();
    }
    fresh(); time_status=PMU_STATUS_INVALID_STATE;
    assert(read_pico_pmu_cmd(PMU_CMD_TIME_GET,NULL,0)==ESP_ERR_INVALID_RESPONSE); stale();
    fresh(); time_status=PMU_STATUS_ACCEPTED;
    assert(read_pico_pmu_cmd(PMU_CMD_TIME_GET,NULL,0)==ESP_OK); stale();
    for (unsigned length=8;length<=9;length++) {
        fresh(); time_length=(uint8_t)length;
        assert(read_pico_pmu_cmd(PMU_CMD_TIME_GET,NULL,0)==ESP_OK);
        assert(s_snap.time_ok && s_snap.unix_sec==next_utc && s_snap.time_synced==1); checks++;
    }
    fresh(); read_failure=true;
    for (unsigned i=0;i<4;i++) {
        assert(read_pico_pmu_refresh()==ESP_OK && s_snap.last_op_code==PMU_CMD_ALARM_GET && s_snap.last_op_status==PMU_STATUS_OK);
        stale();
    }
    read_failure=false; time_length=6;
    assert(read_pico_pmu_refresh()==ESP_OK); stale();
    time_length=8;
    assert(read_pico_pmu_refresh()==ESP_OK && s_snap.time_ok && s_snap.unix_sec==next_utc); checks++;
    unsigned previous=time_reads;
    assert(read_pico_pmu_poll()==ESP_OK && s_snap.time_ok && time_reads==previous); checks++;
    assert(time_reads==6 && write_calls==18); checks++;
    printf("PMU RTC: %u transport, short-response, full-refresh freshness and recovery checks passed; no hardware writes\n",checks);
    return 0;
}

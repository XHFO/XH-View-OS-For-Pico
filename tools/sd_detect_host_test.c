/* SPDX-License-Identifier: Apache-2.0
 * 执行板级真实卡检测函数，区分通信失败与确实拔卡。
 * Execute the actual board detector, separating communication failure from confirmed removal.
 */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stddef.h>
typedef int esp_err_t;
enum { ESP_OK, ESP_ERR_INVALID_ARG, ESP_ERR_INVALID_STATE, ESP_ERR_TIMEOUT };
#define IOE_SD_CD (1U << 6)
static void* s_ioe;
static int result;
static uint8_t input;
static int fca9555_read_reg(void* handle, int reg, uint8_t* output) {
    assert(handle == s_ioe && reg == 0);
    if (!result) *output = input;
    return result;
}
#include "board_sd_detect.inc"
int main(void) {
    bool present = true;
    assert(read_pico_sd_detect(NULL) == ESP_ERR_INVALID_ARG);
    assert(read_pico_sd_detect(&present) == ESP_ERR_INVALID_STATE && present);
    assert(!read_pico_sd_present());
    s_ioe = &input;
    input = 0;
    assert(read_pico_sd_detect(&present) == ESP_OK && present);
    result = ESP_ERR_TIMEOUT;
    assert(read_pico_sd_detect(&present) == ESP_ERR_TIMEOUT && present);
    assert(!read_pico_sd_present());
    present = false;
    assert(read_pico_sd_detect(&present) == ESP_ERR_TIMEOUT && !present);
    result = ESP_OK;
    input = IOE_SD_CD;
    assert(read_pico_sd_detect(&present) == ESP_OK && !present);
    input = 0;
    assert(read_pico_sd_present());
    puts("PASS board detector: I2C error preserves caller state, live active-low CD, legacy fail-closed boolean");
}

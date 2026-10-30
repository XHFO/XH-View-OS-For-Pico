/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 *
 * CST836U 主机：I2C 读点、动态上报 / 深睡、RST 唤醒。
 *
 * CST836U host: I2C point read, dynamic report / deep sleep, RST wake.
 *
 * 临时诊断：只读记录原始触摸帧，最多 5 Hz；不改协议、解析或触摸控制命令。
 * Temporary diagnostics: observe raw touch frames at up to 5 Hz; no protocol,
 * parsing, or touch-control command changes.
 */

#include "cst836u.h"

#include <stdlib.h>
#include <string.h>

#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define CST836U_REG_TOUCH_DATA 0x00
#define CST836U_REG_INFO 0xA6

#define CST836U_CMD_NORMAL 0xFE00
#define CST836U_CMD_DEEPSLEEP 0xA503

#define I2C_TIMEOUT_MS 100
#define RST_HOLD_MS 10
#define RST_BOOT_MS 50

// 实机触摸排障的临时开关；诊断结束后可编译为 0，不作为产品设置。
// Temporary hardware-investigation switch; build as 0 after diagnosis, not a product setting.
#ifndef CST836U_TEMP_TOUCH_DIAGNOSTICS
#define CST836U_TEMP_TOUCH_DIAGNOSTICS 0
#endif

#if CST836U_TEMP_TOUCH_DIAGNOSTICS
#define TOUCH_DIAG_MIN_US 200000
#define TOUCH_DIAG_IDLE_US 5000000
#endif

static const char* TAG = "cst836u";

struct cst836u_dev_t {
    i2c_master_bus_handle_t bus;
    i2c_master_dev_handle_t dev;
    gpio_num_t int_gpio;
    gpio_num_t rst_gpio;
    esp_err_t (*reset_fn)(void);
    cst836u_info_t info;
    cst836u_mode_t mode;
    bool info_ready;
#if CST836U_TEMP_TOUCH_DIAGNOSTICS
    uint8_t diag_last_raw[CST836U_RAW_LEN];
    esp_err_t diag_last_result;
    bool diag_seen;
    bool diag_emitted;
    bool diag_pending;
    int64_t diag_last_emit_us;
    uint32_t diag_reads;
    uint32_t diag_changes;
    uint32_t diag_errors;
    bool diag_parsed_seen;
    bool diag_last_touched;
    uint32_t diag_down_edges;
    uint32_t diag_up_edges;
    uint8_t diag_error_raw[CST836U_RAW_LEN];
    esp_err_t diag_error_result;
    bool diag_error_pending;
    bool diag_error_complete;
    int diag_error_attempts;
#endif
};

#if CST836U_TEMP_TOUCH_DIAGNOSTICS
// 把变化合并到最近样本，始终限速；空闲每 5 秒摘要一次以确认读取循环仍在运行。
// Coalesce changes into the latest sample, always rate-limited; idle summaries every 5 s prove polling continues.
static void diagnose_touch(cst836u_handle_t h, const uint8_t* raw, bool frame_complete,
                           esp_err_t result, const cst836u_touch_t* parsed, int attempts) {
    const int64_t now = esp_timer_get_time();
    ++h->diag_reads;
    const char* edge = "unparsed";
    if (parsed) {
        edge = !h->diag_parsed_seen ? "initial" : parsed->touched == h->diag_last_touched
            ? "hold" : parsed->touched ? "down" : "up";
        if (h->diag_parsed_seen && parsed->touched != h->diag_last_touched) {
            if (parsed->touched) ++h->diag_down_edges;
            else ++h->diag_up_edges;
        }
        h->diag_parsed_seen = true;
        h->diag_last_touched = parsed->touched;
    }
    if (result != ESP_OK) {
        ++h->diag_errors;
        memcpy(h->diag_error_raw, raw, CST836U_RAW_LEN);
        h->diag_error_result = result;
        h->diag_error_complete = frame_complete;
        h->diag_error_attempts = attempts;
        h->diag_error_pending = true;
    }
    if (!h->diag_seen || result != h->diag_last_result ||
        memcmp(h->diag_last_raw, raw, CST836U_RAW_LEN) != 0) {
        memcpy(h->diag_last_raw, raw, CST836U_RAW_LEN);
        h->diag_last_result = result;
        h->diag_seen = true;
        h->diag_pending = true;
        ++h->diag_changes;
    }
    const int64_t elapsed = now - h->diag_last_emit_us;
    if (h->diag_emitted && elapsed < TOUCH_DIAG_MIN_US) return;
    if (h->diag_emitted && !h->diag_pending && elapsed < TOUCH_DIAG_IDLE_US) return;

    // 瞬态失败保留至下一次允许输出，避免成功帧在限速窗口内覆盖错误原文。
    // Preserve transient failures until the next permitted log so successful frames cannot hide the failed bytes.
    const esp_err_t latest_result = result;
    const bool logging_error = h->diag_error_pending;
    if (logging_error) {
        raw = h->diag_error_raw;
        result = h->diag_error_result;
        frame_complete = h->diag_error_complete;
        attempts = h->diag_error_attempts;
        parsed = NULL;
        edge = "unparsed";
    }

    // 失败帧也输出原始槽字段，但 parsed=0/touched=-1 明确没有被驱动接受。
    // Failed frames retain raw slot fields, while parsed=0/touched=-1 explicitly means the driver rejected them.
    const uint8_t* slot0 = raw + 3;
    const uint8_t* slot1 = raw + 9;
    const unsigned x0 = ((unsigned)(slot0[0] & 0x0f) << 8) | slot0[1];
    const unsigned y0 = ((unsigned)(slot0[2] & 0x0f) << 8) | slot0[3];
    const unsigned x1 = ((unsigned)(slot1[0] & 0x0f) << 8) | slot1[1];
    const unsigned y1 = ((unsigned)(slot1[2] & 0x0f) << 8) | slot1[3];
    ESP_LOGI(TAG,
        "touch-diag %s result=%s latest=%s complete=%d parsed=%d mode=%s INT=%d nraw=0x%02X count=%u "
        "touched=%d xy=%u,%u edge=%s s0=e%u/id%u/a%d@%u,%u s1=e%u/id%u/a%d@%u,%u "
        "reads=%lu changes=%lu errors=%lu down/up=%lu/%lu tries=%d "
        "raw=%02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X",
        logging_error ? "error" : h->diag_pending ? "change" : "idle",
        esp_err_to_name(result), esp_err_to_name(latest_result), frame_complete, parsed != NULL,
        cst836u_mode_name(h->mode), cst836u_int_level(h), (unsigned)raw[2], (unsigned)(raw[2] & 0x0f),
        parsed ? (int)parsed->touched : -1, parsed ? (unsigned)parsed->x : 0,
        parsed ? (unsigned)parsed->y : 0, edge, (unsigned)(slot0[0] >> 6), (unsigned)(slot0[2] >> 4),
        parsed ? (int)parsed->points[0].active : -1, x0, y0,
        (unsigned)(slot1[0] >> 6), (unsigned)(slot1[2] >> 4),
        parsed ? (int)parsed->points[1].active : -1, x1, y1,
        (unsigned long)h->diag_reads, (unsigned long)h->diag_changes,
        (unsigned long)h->diag_errors, (unsigned long)h->diag_down_edges,
        (unsigned long)h->diag_up_edges, attempts,
        (unsigned)raw[0], (unsigned)raw[1], (unsigned)raw[2], (unsigned)raw[3],
        (unsigned)raw[4], (unsigned)raw[5], (unsigned)raw[6], (unsigned)raw[7],
        (unsigned)raw[8], (unsigned)raw[9], (unsigned)raw[10], (unsigned)raw[11],
        (unsigned)raw[12], (unsigned)raw[13], (unsigned)raw[14]);
    h->diag_last_emit_us = now;
    h->diag_emitted = true;
    h->diag_pending = logging_error && latest_result == ESP_OK;
    h->diag_error_pending = false;
    h->diag_reads = h->diag_changes = h->diag_errors = 0;
    h->diag_down_edges = h->diag_up_edges = 0;
}
#else
#define diagnose_touch(h, raw, complete, result, parsed, attempts) \
    ((void)(h), (void)(raw), (void)(complete), (void)(result), (void)(parsed), (void)(attempts))
#endif

static bool gpio_ok(gpio_num_t gpio) {
    return gpio != GPIO_NUM_NC && GPIO_IS_VALID_GPIO(gpio);
}

static esp_err_t read_regs(cst836u_handle_t h, uint8_t reg, uint8_t* data, size_t length) {
    esp_err_t err = i2c_master_transmit(h->dev, &reg, 1, I2C_TIMEOUT_MS);
    if (err != ESP_OK) return err;
    esp_rom_delay_us(5);
    return i2c_master_receive(h->dev, data, length, I2C_TIMEOUT_MS);
}

static esp_err_t write_cmd(cst836u_handle_t h, uint16_t cmd) {
    uint8_t buf[2] = { (uint8_t)(cmd >> 8), (uint8_t)(cmd & 0xFF) };
    return i2c_master_transmit(h->dev, buf, sizeof(buf), I2C_TIMEOUT_MS);
}

static esp_err_t int_as_input(cst836u_handle_t h) {
    if (!gpio_ok(h->int_gpio)) return ESP_ERR_INVALID_ARG;
    gpio_config_t io = {
        .pin_bit_mask = 1ULL << h->int_gpio,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    return gpio_config(&io);
}

static esp_err_t rst_as_output(cst836u_handle_t h) {
    if (!gpio_ok(h->rst_gpio)) return ESP_ERR_INVALID_ARG;
    gpio_config_t io = {
        .pin_bit_mask = 1ULL << h->rst_gpio,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    esp_err_t err = gpio_config(&io);
    if (err != ESP_OK) return err;
    return gpio_set_level(h->rst_gpio, 1);
}

static esp_err_t load_info(cst836u_handle_t h) {
    uint8_t info[6] = { 0 };
    esp_err_t err = read_regs(h, CST836U_REG_INFO, info, sizeof(info));
    if (err != ESP_OK) return err;
    h->info.fw = info[0];
    h->info.id = info[1];
    h->info.module = info[2];
    h->info.project = info[3];
    h->info.type = (uint16_t)info[5] << 8 | info[4];
    h->info_ready = true;
    return ESP_OK;
}

esp_err_t cst836u_init(i2c_master_bus_handle_t bus_handle, const cst836u_config_t* config,
                       cst836u_handle_t* handle) {
    if (bus_handle == NULL || config == NULL || handle == NULL) return ESP_ERR_INVALID_ARG;

    cst836u_handle_t h = calloc(1, sizeof(*h));
    if (h == NULL) return ESP_ERR_NO_MEM;

    h->bus = bus_handle;
    h->int_gpio = config->int_gpio;
    h->rst_gpio = config->rst_gpio;
    h->reset_fn = config->reset_fn;
    h->mode = CST836U_MODE_NORMAL;

    uint8_t addr = config->i2c_addr ? config->i2c_addr : CST836U_ADDR_DEFAULT;
    uint32_t hz = config->scl_speed_hz ? config->scl_speed_hz : CST836U_I2C_HZ_DEFAULT;
    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = addr,
        .scl_speed_hz = hz,
    };
    esp_err_t err = i2c_master_bus_add_device(bus_handle, &dev_cfg, &h->dev);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "I2C device add failed: %s", esp_err_to_name(err));
        free(h);
        return err;
    }

    if (gpio_ok(h->rst_gpio)) {
        err = rst_as_output(h);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "RST GPIO%d failed: %s", (int)h->rst_gpio, esp_err_to_name(err));
        }
    }

    err = load_info(h);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Chip information read failed: %s", esp_err_to_name(err));
        i2c_master_bus_rm_device(h->dev);
        free(h);
        return err;
    }

    if (gpio_ok(h->int_gpio)) {
        err = int_as_input(h);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "INT GPIO%d input failed: %s", (int)h->int_gpio, esp_err_to_name(err));
        }
    }

    ESP_LOGI(
        TAG,
        "Ready: FW=0x%02X, ID=0x%02X, module=0x%02X, project=0x%02X, type=0x%04X",
        h->info.fw, h->info.id, h->info.module, h->info.project, h->info.type
    );
    *handle = h;
    return ESP_OK;
}

esp_err_t cst836u_deinit(cst836u_handle_t h) {
    if (h == NULL) return ESP_OK;
    if (h->dev) {
        i2c_master_bus_rm_device(h->dev);
    }
    free(h);
    return ESP_OK;
}

esp_err_t cst836u_get_info(cst836u_handle_t h, cst836u_info_t* info) {
    if (h == NULL || info == NULL) return ESP_ERR_INVALID_ARG;
    if (!h->info_ready) return ESP_ERR_INVALID_STATE;
    *info = h->info;
    return ESP_OK;
}

esp_err_t cst836u_read(cst836u_handle_t h, cst836u_touch_t* touch) {
    if (h == NULL || touch == NULL) return ESP_ERR_INVALID_ARG;
    if (h->dev == NULL) return ESP_ERR_INVALID_STATE;
    // 深睡后不再应答 I2C。这里直接回空点，主循环才能继续走 on_tick 做超时复位。
    // / Deep sleep ignores I2C. Return empty points so the loop can still
    // run on_tick and time out a reset.
    if (h->mode == CST836U_MODE_DEEPSLEEP) {
        memset(touch, 0, sizeof(*touch));
        diagnose_touch(h, touch->raw, false, ESP_OK, touch, 0);
        return ESP_OK;
    }

    uint8_t data[CST836U_RAW_LEN] = { 0 };
    esp_err_t err = ESP_FAIL;
    int attempts = 0;
    for (int retry = 0; retry < 3; retry++) {
        attempts = retry + 1;
        err = read_regs(h, CST836U_REG_TOUCH_DATA, data, sizeof(data));
        if (err != ESP_OK) {
            diagnose_touch(h, data, false, err, NULL, retry + 1);
            return err;
        }
        if (data[2] < 3) {
            err = ESP_OK;
            break;
        }
        err = ESP_ERR_INVALID_RESPONSE;
    }
    if (err != ESP_OK) {
        diagnose_touch(h, data, true, err, NULL, 3);
        return err;
    }

    memset(touch, 0, sizeof(*touch));
    memcpy(touch->raw, data, sizeof(data));
    touch->count = data[2] & 0x0F;

    uint8_t active_n = 0;
    bool have_primary = false;
    for (int i = 0; i < CST836U_MAX_POINTS; i++) {
        const uint8_t* src = &data[3 + i * 6];
        cst836u_point_t* pt = &touch->points[i];
        uint8_t event = src[0] >> 6;
        uint8_t id = src[2] >> 4;
        pt->event = event;
        pt->id = id;
        pt->x = (uint16_t)(src[0] & 0x0F) << 8 | src[1];
        pt->y = (uint16_t)(src[2] & 0x0F) << 8 | src[3];
        if (id > 1 || event == 3) {
            pt->active = false;
            continue;
        }
        pt->active = event != 1;
        if (!have_primary) {
            touch->x = pt->x;
            touch->y = pt->y;
            have_primary = true;
        }
        if (pt->active) active_n++;
    }
    touch->touched = active_n != 0;
    diagnose_touch(h, data, true, ESP_OK, touch, attempts);
    return ESP_OK;
}

esp_err_t cst836u_set_mode(cst836u_handle_t h, cst836u_mode_t mode) {
    if (h == NULL || h->dev == NULL) return ESP_ERR_INVALID_STATE;
    uint16_t cmd;
    switch (mode) {
        case CST836U_MODE_NORMAL: cmd = CST836U_CMD_NORMAL; break;
        case CST836U_MODE_DEEPSLEEP: cmd = CST836U_CMD_DEEPSLEEP; break;
        default: return ESP_ERR_INVALID_ARG;
    }

    esp_err_t err = write_cmd(h, cmd);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Mode %d command failed: %s", mode, esp_err_to_name(err));
        return err;
    }
    h->mode = mode;
    ESP_LOGI(TAG, "Work mode %s (command 0x%04X)", cst836u_mode_name(mode), cmd);
    return ESP_OK;
}

cst836u_mode_t cst836u_get_mode(cst836u_handle_t h) {
    return h == NULL ? CST836U_MODE_NORMAL : h->mode;
}

esp_err_t cst836u_reset(cst836u_handle_t h) {
    if (h == NULL) return ESP_ERR_INVALID_ARG;
    if (gpio_ok(h->rst_gpio)) {
        gpio_set_level(h->rst_gpio, 0);
        vTaskDelay(pdMS_TO_TICKS(RST_HOLD_MS));
        gpio_set_level(h->rst_gpio, 1);
        return ESP_OK;
    }
    if (h->reset_fn != NULL) return h->reset_fn();
    return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t cst836u_wake(cst836u_handle_t h) {
    if (h == NULL || h->dev == NULL) return ESP_ERR_INVALID_STATE;
    esp_err_t err = cst836u_reset(h);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Touch reset failed: %s", esp_err_to_name(err));
        return err;
    }
    vTaskDelay(pdMS_TO_TICKS(RST_BOOT_MS));
    h->mode = CST836U_MODE_NORMAL;
    return cst836u_set_mode(h, CST836U_MODE_NORMAL);
}

bool cst836u_int_asserted(cst836u_handle_t h) {
    return h != NULL && gpio_ok(h->int_gpio) && gpio_get_level(h->int_gpio) == 0;
}

int cst836u_int_level(cst836u_handle_t h) {
    if (h == NULL || !gpio_ok(h->int_gpio)) return 1;
    return gpio_get_level(h->int_gpio);
}

gpio_num_t cst836u_int_gpio(cst836u_handle_t h) {
    return h == NULL ? GPIO_NUM_NC : h->int_gpio;
}

const char* cst836u_mode_name(cst836u_mode_t mode) {
    switch (mode) {
        case CST836U_MODE_NORMAL: return "normal";
        case CST836U_MODE_DEEPSLEEP: return "deepsleep";
        default: return "unknown";
    }
}

const char* cst836u_event_name(uint8_t event) {
    switch (event) {
        case 0: return "down";
        case 1: return "up";
        case 2: return "move";
        default: return "---";
    }
}

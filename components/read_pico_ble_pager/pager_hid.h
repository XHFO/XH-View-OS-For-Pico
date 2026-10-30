/*
 * SPDX-FileCopyrightText: 2026 XH Lab
 * SPDX-License-Identifier: Apache-2.0
 * 有界 HID 输入描述解析与按下边沿过滤。/ Bounded HID input parsing and press-edge filtering.
 */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define PAGER_HID_MAX_FIELDS 48
#define PAGER_HID_MAX_REPORTS 16

typedef struct {
    uint16_t bit_offset;
    uint16_t usage;
    uint16_t usage_page;
    uint8_t bit_size;
    uint8_t count;
    uint8_t report_id;
    bool array;
} pager_hid_field_t;

typedef struct {
    pager_hid_field_t fields[PAGER_HID_MAX_FIELDS];
    uint8_t report_ids[PAGER_HID_MAX_REPORTS];
    uint16_t report_bits[PAGER_HID_MAX_REPORTS];
    uint8_t held[PAGER_HID_MAX_REPORTS];
    uint8_t field_count;
    uint8_t report_count;
} pager_hid_t;

/// 失败时清空输出，拒绝截断或超预算描述。/ Clear output on failure; reject malformed or over-budget maps.
bool pager_hid_parse(pager_hid_t *hid, const uint8_t *map, size_t length);
/// HID over GATT 报告不包含 ID 前缀；仅新按下返回方向。/ HOGP data excludes report-ID prefix; return only a new press.
int pager_hid_input(pager_hid_t *hid, uint8_t report_id, const uint8_t *data, size_t length);
/// 标准 8 字节 Boot Keyboard 报告。/ Standard eight-byte Boot Keyboard input.
int pager_hid_boot_input(uint8_t *held, const uint8_t *data, size_t length);

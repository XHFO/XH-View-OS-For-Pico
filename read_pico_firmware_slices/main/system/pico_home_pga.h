/*
 * SPDX-FileCopyrightText: 2026 XH Lab
 * SPDX-License-Identifier: Apache-2.0
 * 中文：薄宿主上的 Home Link PGA 桥。home/printers 在桥注册后写入 pico_home_pga；bambu 仍 503。
 * English: Home Link PGA bridge on the thin host. After the store bridge is registered, home/printers persist pico_home_pga; bambu stays 503.
 */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "pico_app_sdk.h"
#include "read_pico_home_link.h"

void ps_home_pga_init(void);
bool ps_home_pga_active(void);
bool ps_home_pga_command(uint32_t command, pico_app_home_status_t *status);
bool ps_home_pga_config(uint32_t command, pico_app_home_config_t *config);
bool ps_home_pga_entity(uint32_t index, pico_app_home_entity_t *out);
bool ps_home_pga_printer(uint32_t slot, pico_app_home_printer_t *out);
bool ps_home_pga_control(const char *entity_id, const char *action, float value);
bool ps_home_pga_input(uint32_t command, char *value, size_t capacity);
void ps_home_pga_input_done(const char *text);
bool ps_home_pga_suspend(uint32_t timeout_ms);
void ps_home_pga_resume(void);
bool ps_home_pga_portal_load(rp_home_config_t *config, rp_home_snapshot_t *snapshot);
bool ps_home_pga_portal_commit(const rp_home_config_t *config, bool *queued);
void ps_home_pga_portal_set_owner(bool portal_owns_writes);

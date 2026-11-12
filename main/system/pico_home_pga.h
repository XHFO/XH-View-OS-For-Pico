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
#include "home_link_discovery.h"

void ps_home_pga_init(void);
bool ps_home_pga_active(void);
/// 仅回读已初始化快照，不分配/启任务/访问TF。/ Read initialized state without allocation, tasks, or TF access.
bool ps_home_pga_peek_status(pico_app_home_status_t *status);
bool ps_home_pga_command(uint32_t command, pico_app_home_status_t *status);
bool ps_home_pga_config(uint32_t command, pico_app_home_config_t *config);
bool ps_home_pga_entity(uint32_t index, pico_app_home_entity_t *out);
bool ps_home_pga_printer(uint32_t slot, pico_app_home_printer_t *out);
bool ps_home_pga_printer_config(uint32_t command, pico_app_printer_config_t *config);
bool ps_home_pga_rooms(uint32_t command,pico_app_home_rooms_t *rooms);
bool ps_home_pga_printer_candidate(uint32_t index,pico_app_printer_config_t *out);
enum { PS_HOME_PRINTER_SYNC=4 };
bool ps_home_pga_control(const char *entity_id, const char *action, float value);
bool ps_home_pga_input(uint32_t command, char *value, size_t capacity);
void ps_home_pga_input_done(const char *text);
bool ps_home_pga_suspend(uint32_t timeout_ms);
void ps_home_pga_resume(void);
bool ps_home_pga_portal_load(rp_home_config_t *config, rp_home_snapshot_t *snapshot);
bool ps_home_pga_portal_commit(const rp_home_config_t *config, bool *queued);
void ps_home_pga_portal_set_owner(bool portal_owns_writes);

bool ps_home_pga_discover(bool start, rp_home_discovery_t *out, bool *busy, bool *failed);

bool ps_home_pga_catalog(uint32_t command,pico_app_home_catalog_t *out);
bool ps_home_pga_selection(uint32_t command,pico_app_home_selection_t *out);

/// 认证BLE通道上的低优先级家庭共享；回调只缓存，持久化由工作器提交。/ Low-priority home sharing on authenticated BLE; callbacks buffer, the worker commits.
void ps_home_pga_share_tick(int role,bool connected,const uint8_t owner[8],int64_t now,bool reading);
bool ps_home_pga_share_receive(const uint8_t *packet,size_t bytes);
size_t ps_home_pga_share_take(uint8_t *packet,size_t capacity);

void ps_home_pga_share_context(int role,bool reading);

/*
 * SPDX-FileCopyrightText: 2026 XH Lab
 * SPDX-License-Identifier: Apache-2.0
 * 中文：未注册 PGA 存储桥时，manage home/printer/bambu 仍 503 home_module_missing。
 * English: Without a registered PGA store bridge, manage home/printer/bambu stay 503 home_module_missing.
 * 中文：桥注册后，home/printers 读写 pico_home_pga；门户活动时 PUT/DELETE 为 409。bambu 始终 503。
 * English: With the bridge, home/printers use pico_home_pga; PUT/DELETE is 409 while the portal is active. Bambu stays 503.
 * 模型解析由 home_link_model.c 提供给 PGA 宿主桥；本文件不实现 rp_home_parse/fetch。
 * Model parse/fetch live in home_link_model.c for the PGA host bridge; this file does not implement them.
 */
#include "read_pico_home_link.h"
#include "home_link_web.h"
#include "home_link_portal.h"
#include <string.h>

static uint32_t s_revision = 1;

void read_pico_home_link_init(void) {}

void read_pico_home_link_get_config(rp_home_config_t *out) {
    if (!out) return;
    memset(out, 0, sizeof(*out));
    out->version = RP_HOME_CONFIG_VERSION;
}

bool read_pico_home_link_configure(const rp_home_config_t *config) {
    (void)config;
    return false;
}

bool read_pico_home_link_request(bool network_online, bool at_home) {
    (void)network_online; (void)at_home;
    return false;
}

uint32_t read_pico_home_link_revision(void) { return s_revision; }

void read_pico_home_link_get_snapshot(rp_home_snapshot_t *out) {
    if (!out) return;
    memset(out, 0, sizeof(*out));
    out->result = RP_HOME_EMPTY;
}

bool read_pico_home_link_control(const rp_home_entity_t *entity, const char *action, float value, bool at_home) {
    (void)entity; (void)action; (void)value; (void)at_home;
    return false;
}

bool read_pico_home_link_suspend(uint32_t timeout_ms) {
    (void)timeout_ms;
    return true;
}

void read_pico_home_link_resume(void) {}

bool read_pico_bambu_auth_submit(const rp_home_bambu_input_t *input, bool online) {
    (void)input; (void)online;
    return false;
}

void read_pico_bambu_auth_get(rp_home_bambu_state_t *out) {
    if (!out) return;
    memset(out, 0, sizeof(*out));
    out->phase = RP_BAMBU_IDLE;
}

bool read_pico_bambu_auth_cancel_session(uint32_t session) {
    (void)session;
    return true;
}

void read_pico_bambu_auth_cancel(void) {}

bool read_pico_bambu_auth_prepare_import(uint32_t session, unsigned index, rp_home_printer_config_t *out) {
    (void)session; (void)index;
    if (out) memset(out, 0, sizeof(*out));
    return false;
}

bool rp_home_web_handle(const rp_home_web_request_t *request, rp_home_web_response_t *response,
                        char *output, size_t capacity) {
    if (!request || !response || !output || capacity < 64 || !request->path) return false;
    const char *path = request->path;
    if (rp_home_store_bridge_ready() &&
        (!strcmp(path, "/api/manage/home") || !strcmp(path, "/api/manage/home/discover") || !strcmp(path, "/api/manage/printers")))
        return rp_portal_serve(request, response, output, capacity, true);
    if (strncmp(path, "/api/manage/home", 16) && strncmp(path, "/api/manage/printer", 19) &&
        strncmp(path, "/api/manage/bambu", 17))
        return false;
    static const char body[] =
        "{\"ok\":false,\"error\":\"home_module_missing\",\"message\":\"Install TF tools/home-link.pga\"}";
    size_t n = sizeof(body) - 1;
    if (n >= capacity) return false;
    memcpy(output, body, n + 1);
    response->http_status = 503;
    response->body_bytes = n;
    return true;
}

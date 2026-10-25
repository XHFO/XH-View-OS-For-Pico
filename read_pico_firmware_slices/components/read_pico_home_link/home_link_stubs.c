/*
 * SPDX-FileCopyrightText: 2026 XH Lab
 * SPDX-License-Identifier: Apache-2.0
 * 中文：家庭服务薄宿主桩；完整 HA/打印机逻辑迁至 TF tools/home-link.pga。
 * English: Thin-host stubs for home services; full HA/printer logic moves to TF tools/home-link.pga.
 * 冻结：不发明凭据或假连接；未加载模块时返回未配置空状态；电源挂起始终可完成；不常驻大块配置/快照。
 * Frozen: Never invent credentials or fake links; report unconfigured empty state without a module; power suspend always completes; do not retain large config/snapshot BSS.
 */
#include "read_pico_home_link.h"
#include "home_link_web.h"
#include <string.h>

static uint32_t s_revision = 1;

bool rp_home_config_valid(const rp_home_config_t *config) {
    return config && config->version == RP_HOME_CONFIG_VERSION;
}
bool rp_home_config_upgrade(rp_home_config_t *config) {
    if (!config) return false;
    if (!config->version) config->version = RP_HOME_CONFIG_VERSION;
    return config->version == RP_HOME_CONFIG_VERSION;
}
bool rp_home_config_unpack_version(const void *data, size_t bytes, rp_home_config_t *out, uint32_t *stored_version) {
    (void)data; (void)bytes;
    if (out) { memset(out, 0, sizeof(*out)); out->version = RP_HOME_CONFIG_VERSION; }
    if (stored_version) *stored_version = RP_HOME_CONFIG_VERSION;
    return false;
}
size_t rp_home_config_pack(const rp_home_config_t *config, void *out, size_t capacity) {
    (void)config; (void)out; (void)capacity;
    return 0;
}
bool rp_home_config_unpack(const void *data, size_t bytes, rp_home_config_t *out) {
    (void)data; (void)bytes;
    if (out) { memset(out, 0, sizeof(*out)); out->version = RP_HOME_CONFIG_VERSION; }
    return false;
}
rp_home_result_t rp_home_parse_ha(const char *json, size_t bytes, const rp_home_config_t *config,
                                  rp_home_snapshot_t *out, int64_t now_ms) {
    (void)json; (void)bytes; (void)config; (void)now_ms;
    if (out) { memset(out, 0, sizeof(*out)); out->result = RP_HOME_UNSUPPORTED; }
    return RP_HOME_UNSUPPORTED;
}
rp_home_result_t rp_home_parse_formlabs(const char *json, size_t bytes, const char *serial,
                                        rp_home_printer_t *out, int64_t now_ms) {
    (void)json; (void)bytes; (void)serial; (void)now_ms;
    if (out) { memset(out, 0, sizeof(*out)); out->result = RP_HOME_UNSUPPORTED; }
    return RP_HOME_UNSUPPORTED;
}
rp_home_result_t rp_home_parse_bambu(const char *json, size_t bytes, rp_home_printer_t *out, int64_t now_ms) {
    (void)json; (void)bytes; (void)now_ms;
    if (out) { memset(out, 0, sizeof(*out)); out->result = RP_HOME_UNSUPPORTED; }
    return RP_HOME_UNSUPPORTED;
}
bool rp_home_form_body(const char *id, const char *secret, char *body, size_t capacity) {
    (void)id; (void)secret; (void)body; (void)capacity;
    return false;
}
rp_home_result_t rp_home_fetch(const rp_home_config_t *config, rp_home_snapshot_t *out,
                               rp_home_token_t tokens[RP_HOME_PRINTERS], const rp_home_transport_t *transport,
                               bool network_online, bool at_home, int64_t now_ms) {
    (void)config; (void)tokens; (void)transport; (void)network_online; (void)at_home; (void)now_ms;
    if (out) { memset(out, 0, sizeof(*out)); out->result = RP_HOME_UNSUPPORTED; }
    return RP_HOME_UNSUPPORTED;
}
rp_home_result_t rp_home_control(const rp_home_config_t *config, const rp_home_entity_t *entity,
                                 const char *action, float value, const rp_home_transport_t *transport,
                                 bool at_home, int *http_status) {
    (void)config; (void)entity; (void)action; (void)value; (void)transport; (void)at_home;
    if (http_status) *http_status = 0;
    return RP_HOME_UNSUPPORTED;
}
const char *rp_home_result_text(rp_home_result_t result) {
    switch (result) {
    case RP_HOME_EMPTY: return "未配置";
    case RP_HOME_BUSY: return "同步中";
    case RP_HOME_OK: return "已连接";
    case RP_HOME_OFFLINE: return "离线";
    case RP_HOME_AUTH: return "认证失败";
    case RP_HOME_UNSUPPORTED: return "需要安装家庭工具包";
    default: return "不可用";
    }
}

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

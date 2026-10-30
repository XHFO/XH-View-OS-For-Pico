/*
 * SPDX-FileCopyrightText: 2026 XH Lab
 * SPDX-License-Identifier: Apache-2.0
 * 局域网填写门户与 Mac 回读共用的字段写入。不监听套接字，不保存密钥。
 * Shared field writer for the LAN fill-in portal and Mac readback. No sockets and no key storage.
 */
#pragma once
#include "read_pico_home_link.h"
#include "home_link_web.h"
#include "home_link_discovery.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define RP_PORTAL_BODY_LIMIT 4096u

enum {
    RP_PORTAL_IDLE = 0,
    RP_PORTAL_OPEN = 1,
    RP_PORTAL_SAVED = 2,
    RP_PORTAL_OFFLINE = 3,
    RP_PORTAL_ERROR = 4
};

typedef struct {
    bool active;
    uint8_t phase;
    uint32_t revision;
    uint32_t seconds_left;
} rp_portal_public_t;

typedef struct {
    bool (*load)(rp_home_config_t *config, rp_home_snapshot_t *snapshot);
    bool (*commit)(const rp_home_config_t *config, bool *queued);
    bool (*portal_active)(void);
    void (*get_public)(rp_portal_public_t *out);
    void (*set_public)(const rp_portal_public_t *in);
} rp_home_store_bridge_t;

void rp_home_set_store_bridge(const rp_home_store_bridge_t *bridge);
bool rp_home_store_bridge_ready(void);

/// 处理 /api/lan 或 /api/manage 的 home 与 printers。manage 在门户活动时拒绝写入。
/// Serve home and printers for /api/lan or /api/manage. Manage writes are refused while the portal is active.
bool rp_portal_serve(const rp_home_web_request_t *request, rp_home_web_response_t *response, char *output,
                     size_t capacity, bool manage_route);

/// UTF-8 HTML 的 gzip 二进制；长度是编码后字节数，不以零结尾。HTTP 必须发送 Content-Encoding: gzip。
/// Gzip-encoded UTF-8 HTML bytes, not a NUL-terminated string. Send Content-Encoding: gzip and the encoded length.
const unsigned char *rp_portal_html_gzip(size_t *encoded_bytes);
const char *rp_portal_phase_name(uint8_t phase);

/// Mac 只读汇总，字段名与 GET /api/manage/home 及 printers 对齐，并带 portal。
/// Mac read-only summary. Field names match GET /api/manage/home and printers, plus portal.
bool rp_portal_write_mac(const rp_home_web_request_t *request, const rp_home_config_t *config,
                         const rp_home_snapshot_t *snapshot, const rp_portal_public_t *portal, const char *mode,
                         char *output, size_t capacity, size_t *used);

/// 仅排队/回读发现，不修改连接配置。/ Queue/read discovery without changing connection settings.
typedef bool (*rp_home_discovery_bridge_t)(bool start, rp_home_discovery_t *out, bool *busy, bool *failed);
void rp_home_set_discovery_bridge(rp_home_discovery_bridge_t callback);

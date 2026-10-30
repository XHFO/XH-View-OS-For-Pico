/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 *
 * 有界设备管理 API；HTTP 层负责逐请求认证、Host/Origin、读取限额与输入清理。
 * Bounded device-management API; the HTTP owner authenticates every request,
 * verifies Host/Origin, limits reads and wipes its input after dispatch.
 *
 * 冻结：凭据只写；处理器不得阻塞访问网络或承担 HTTP 认证。
 * Frozen: credentials are write-only; no blocking network I/O or HTTP authentication here.
 */
#pragma once
#include "read_pico_home_link.h"

#define RP_HOME_WEB_BODY_LIMIT 12288u
#define RP_HOME_WEB_QUERY_LIMIT 768u
#define RP_HOME_WEB_OUTPUT_LIMIT 32768u
#define RP_HOME_WEB_WIFI_LIMIT 16u

typedef struct {
    /// 调用方持有的请求字符串；query 可空。/ Caller-owned request strings; query may be NULL.
    const char *method, *path, *query, *body;
    /// 正文有效字节数，不含终止符。/ Body bytes excluding the terminator.
    size_t body_bytes;
    /// 当前联网与预先计算的家庭网络状态。/ Current online and precomputed home-network flags.
    bool network_online, at_home;
    /// 可选当前 SSID；有值时按新保存的 home_wifi 重新判断家庭网络。
    /// Optional connected SSID; classify the saved/new home_wifi instead of the old at_home flag.
    const char *current_wifi;
    /// HTTP 层持有的 SSID 列表，禁止包含密码；NULL 表示不可用，空数组表示无记忆网络。
    /// Adapter-owned SSIDs only, never passwords; NULL means unavailable, an empty array means none saved.
    const char *const *saved_wifi;
    /// saved_wifi 的条目数。/ Number of saved_wifi entries.
    size_t saved_wifi_count;
} rp_home_web_request_t;
typedef struct {
    /// 已知路由的 HTTP 结果码。/ HTTP result code for a known route.
    int http_status;
    /// 输出 JSON 字节数，不含终止符。/ Output JSON bytes excluding the terminator.
    size_t body_bytes;
} rp_home_web_response_t;

/// 未知路由或调用参数无效时返回 false；已知路由失败时返回固定错误码 JSON。
/// Returns false for an unknown route or invalid caller arguments; known-route failures return fixed JSON errors.
/// 缓冲区归调用方；HTTP 层须设置 application/json 与 Cache-Control: no-store。
/// Buffers are caller-owned; the adapter sets application/json and Cache-Control: no-store.
/// 配置仅事务保存到本机；刷新与控制只排队既有 worker，不进行阻塞网络 I/O。
/// Configuration commits locally; refresh/control queue existing workers without blocking network I/O.
/// 凭据只写；缺失/空值保留原值，clear:[field] 显式清除；换来源先清旧字段，非法保存不提交。
/// Credentials are write-only; missing/empty preserves, clear:[field] clears; source changes clear old fields, invalid saves never commit.
bool rp_home_web_handle(const rp_home_web_request_t *, rp_home_web_response_t *, char *output, size_t capacity);

/*
 * SPDX-FileCopyrightText: 2026 XH Lab
 * SPDX-License-Identifier: Apache-2.0
 * 有界只读 Home Assistant 服务发现；不访问实体、不发送凭据、不保存配置。
 * Bounded read-only Home Assistant discovery; no entities, credentials or configuration writes.
 */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define RP_HOME_DISCOVERY_MAX 4u

typedef struct {
    char name[96]; ///< 服务名称 / Service name
    char url[192]; ///< 与配置容量一致的可选地址 / Selectable address matching configuration capacity
} rp_home_discovered_t;

typedef struct {
    unsigned count; ///< 已完整解析的服务数 / Number of resolved services
    bool truncated; ///< 发现超过容量的服务 / More services were advertised than fit
    rp_home_discovered_t items[RP_HOME_DISCOVERY_MAX];
} rp_home_discovery_t;

/// 在工作线程调用；500–4000ms 超时，约每100ms检查取消。成功且count=0表示本次没有发现。
/// Call from a worker; timeout clamps to 500–4000ms, cancellation checked about every 100ms.
/// True with count=0 means no service found; false means socket/allocation failure or cancellation.
bool rp_home_discover(rp_home_discovery_t *out, uint32_t timeout_ms,
                      bool (*cancel)(void *context), void *context);

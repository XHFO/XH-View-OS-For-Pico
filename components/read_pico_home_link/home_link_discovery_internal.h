/* SPDX-License-Identifier: Apache-2.0
 * DNS 解析状态，仅用于发现实现与主机测试。/ DNS state private to discovery and host tests.
 */
#pragma once
#include "home_link_discovery.h"

#define RP_HOME_DNS_NAME 256u
#define RP_HOME_DNS_PACKET 2048u

typedef struct {
    char instance[RP_HOME_DNS_NAME], host[RP_HOME_DNS_NAME];
    char internal_url[224], base_url[224], name[96];
    uint16_t port;
} rp_home_dns_service_t;

typedef struct {
    char host[RP_HOME_DNS_NAME];
    uint8_t ip[4];
} rp_home_dns_address_t;

typedef struct {
    rp_home_dns_service_t services[RP_HOME_DISCOVERY_MAX];
    rp_home_dns_address_t addresses[RP_HOME_DISCOVERY_MAX * 2];
    bool truncated;
} rp_home_dns_t;

bool rp_home_dns_feed(rp_home_dns_t *state, const uint8_t *packet, size_t size);
void rp_home_dns_results(const rp_home_dns_t *state, rp_home_discovery_t *out);
size_t rp_home_dns_query(uint8_t *packet, size_t capacity, const char *name, uint16_t type);

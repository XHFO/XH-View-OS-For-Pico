/* SPDX-License-Identifier: Apache-2.0
 * 官方HA广告字段与坏DNS包回归。/ Official HA advertisement fields and malformed DNS regressions.
 */
#include "home_link_discovery_internal.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned checks;
#define CHECK(x) do { ++checks; if (!(x)) { fprintf(stderr, "check %u line %d: %s\n", checks, __LINE__, #x); abort(); } } while (0)
static const char *service = "_home-assistant._tcp.local";
typedef struct { uint8_t b[RP_HOME_DNS_PACKET]; size_t n, rd; unsigned records; } packet_t;
static void word(packet_t *p, unsigned value) { p->b[p->n++] = value >> 8; p->b[p->n++] = value; }
static void name(packet_t *p, const char *s) {
    while (*s) { size_t n = strcspn(s, "."); p->b[p->n++] = n; memcpy(p->b + p->n, s, n); p->n += n; s += n; if (*s) ++s; }
    p->b[p->n++] = 0;
}
static void start(packet_t *p) { memset(p, 0, sizeof(*p)); p->n = 12; p->b[2] = 0x84; }
static void rr(packet_t *p, const char *owner, unsigned type, bool live) {
    name(p, owner); word(p, type); word(p, 0x8001); word(p, 0); word(p, live ? 120 : 0); p->rd = p->n; word(p, 0); ++p->records;
}
static void end(packet_t *p) { size_t n = p->n - p->rd - 2; p->b[p->rd] = n >> 8; p->b[p->rd + 1] = n; p->b[7] = p->records; }
static void srv(packet_t *p, const char *instance, const char *host, unsigned port) {
    rr(p, instance, 33, true); word(p, 0); word(p, 0); word(p, port); name(p, host); end(p);
}
static void a(packet_t *p, const char *host) { rr(p, host, 1, true); p->b[p->n++] = 192; p->b[p->n++] = 168; p->b[p->n++] = 8; p->b[p->n++] = 42; end(p); }
static void ptr(packet_t *p, const char *instance, bool live) { rr(p, service, 12, live); name(p, instance); end(p); }
static void txt(packet_t *p, const char *instance, const char *value) { rr(p, instance, 16, true); size_t n = strlen(value); p->b[p->n++] = n; memcpy(p->b + p->n, value, n); p->n += n; end(p); }
static void add_text(packet_t *p, const char *value) { size_t n = strlen(value); p->b[p->n++] = n; memcpy(p->b + p->n, value, n); p->n += n; end(p); }
static void result(rp_home_dns_t *s, rp_home_discovery_t *r, unsigned count, const char *url) {
    rp_home_dns_results(s, r); CHECK(r->count == count); if (count && url) CHECK(!strcmp(r->items[0].url, url));
}

int main(void) {
    rp_home_dns_t s = {0}; rp_home_discovery_t r; packet_t p;
    const char *instance = "My Home._home-assistant._tcp.local", *host = "a1b2c3d4e5.local";
    start(&p); ptr(&p, instance, true); srv(&p, instance, host, 8123); a(&p, host);
    CHECK(rp_home_dns_feed(&s, p.b, p.n)); result(&s, &r, 1, "http://192.168.8.42:8123"); CHECK(!strcmp(r.items[0].name, "My Home"));
    // 不同包、相反顺序与服务的真实端口。/ Split packets, reverse order and the advertised port.
    memset(&s, 0, sizeof(s)); start(&p); a(&p, host); CHECK(rp_home_dns_feed(&s, p.b, p.n)); result(&s, &r, 0, NULL);
    start(&p); srv(&p, instance, host, 8124); CHECK(rp_home_dns_feed(&s, p.b, p.n)); result(&s, &r, 1, "http://192.168.8.42:8124");
    start(&p); txt(&p, instance, "internal_url=https://ha.example.net:8443/prefix"); add_text(&p, "location_name=楼上家庭");
    CHECK(rp_home_dns_feed(&s, p.b, p.n)); result(&s, &r, 1, "https://ha.example.net:8443/prefix"); CHECK(!strcmp(r.items[0].name, "楼上家庭"));
    // HTTPS保留证书主机；不把外部base_url误作本地。/ Preserve TLS hostname; do not substitute an external base_url.
    start(&p); txt(&p, instance, "base_url=https://external.example.net"); CHECK(rp_home_dns_feed(&s, p.b, p.n)); result(&s, &r, 1, "http://192.168.8.42:8124");
    start(&p); txt(&p, instance, "base_url=https://a1b2c3d4e5.local:8124"); CHECK(rp_home_dns_feed(&s, p.b, p.n)); result(&s, &r, 1, "https://a1b2c3d4e5.local:8124");
    const char *bad_urls[] = {"internal_url=http://localhost:8123", "internal_url=http://127.0.0.1:8123", "internal_url=http://0.0.0.0:8123", "internal_url=http://user:password@ha.local", "internal_url=javascript:evil", "internal_url=http://ha.local:999999999999999999999999", "internal_url=http://ha.local:0", "internal_url=http://ha.local/a#secret", "internal_url=http://ha.local/a?token=secret", "internal_url=http://ha.local/path\nheader"};
    for (unsigned i = 0; i < sizeof(bad_urls) / sizeof(*bad_urls); ++i) {
        start(&p); txt(&p, instance, bad_urls[i]); CHECK(rp_home_dns_feed(&s, p.b, p.n)); result(&s, &r, 1, "http://192.168.8.42:8124");
    }
    char long_url[220] = "internal_url=http://ha.local/";
    memset(long_url + strlen(long_url), 'x', sizeof(long_url) - strlen(long_url) - 1); long_url[sizeof(long_url) - 1] = 0;
    start(&p); txt(&p, instance, long_url); CHECK(rp_home_dns_feed(&s, p.b, p.n)); result(&s, &r, 0, NULL);
    start(&p); ptr(&p, instance, false); CHECK(rp_home_dns_feed(&s, p.b, p.n)); result(&s, &r, 0, NULL);
    // DNS压缩指向首条服务名；缓存刷新位不应阻止IN解析。/ Compression points to the first owner; accept the IN cache-flush bit.
    start(&p); srv(&p, instance, host, 8123); a(&p, host);
    p.b[p.n++] = 0xc0; p.b[p.n++] = 12; word(&p, 16); word(&p, 1); word(&p, 0); word(&p, 120); p.rd = p.n; word(&p, 0); ++p.records;
    add_text(&p, "location_name=Compressed"); CHECK(rp_home_dns_feed(&s, p.b, p.n)); result(&s, &r, 1, "http://192.168.8.42:8123"); CHECK(!strcmp(r.items[0].name, "Compressed"));
    rp_home_dns_t before = s;
    for (size_t n = 0; n < p.n; ++n) { CHECK(!rp_home_dns_feed(&s, p.b, n)); CHECK(!memcmp(&s, &before, sizeof(s))); }
    p.b[p.n++] = 0; CHECK(!rp_home_dns_feed(&s, p.b, p.n)); CHECK(!memcmp(&s, &before, sizeof(s)));
    uint8_t loop[] = {0,0,0x84,0,0,0,0,1,0,0,0,0,0xc0,12,0,1,0,1,0,0,0,1,0,4,1,2,3,4};
    CHECK(!rp_home_dns_feed(&s, loop, sizeof(loop))); loop[13] = 255; CHECK(!rp_home_dns_feed(&s, loop, sizeof(loop)));
    // 超容量不越界；完全相同地址不产生重复可选项。/ Bound capacity and deduplicate identical endpoints.
    memset(&s, 0, sizeof(s));
    for (unsigned i = 0; i < 8; ++i) {
        char inst[80]; snprintf(inst, sizeof(inst), "Home%u.%s", i, service);
        start(&p); srv(&p, inst, host, 8123 + i); a(&p, host); CHECK(rp_home_dns_feed(&s, p.b, p.n));
    }
    result(&s, &r, RP_HOME_DISCOVERY_MAX, NULL); CHECK(r.truncated);
    memset(&s, 0, sizeof(s)); start(&p); srv(&p, instance, host, 8123); srv(&p, "Second._home-assistant._tcp.local", host, 8123); a(&p, host);
    CHECK(rp_home_dns_feed(&s, p.b, p.n)); result(&s, &r, 1, "http://192.168.8.42:8123");
    uint8_t query[300]; size_t qn = rp_home_dns_query(query, sizeof(query), service, 12);
    CHECK(qn == strlen(service) + 18); CHECK(query[5] == 1 && query[qn - 2] == 0x80 && query[qn - 1] == 1); CHECK(!rp_home_dns_query(query, 16, service, 12));
    // 确定性随机坏包和已知包变异，ASan/UBSan覆盖解析边界。/ Deterministic malformed and mutated packets under ASan/UBSan.
    uint32_t random = 0x35ac88e1;
    for (unsigned i = 0; i < 20000; ++i) {
        uint8_t fuzz[2048]; size_t n = i % sizeof(fuzz);
        for (size_t j = 0; j < n; ++j) { random = random * 1664525u + 1013904223u; fuzz[j] = random >> 24; }
        if (i % 2) { n = p.n; memcpy(fuzz, p.b, n); fuzz[i % n] ^= (i >> 8) | 1; }
        rp_home_dns_t state = {0}; rp_home_dns_feed(&state, fuzz, n); rp_home_dns_results(&state, &r); CHECK(r.count <= RP_HOME_DISCOVERY_MAX);
    }
    printf("home discovery: %u checks passed\n", checks);
    return 0;
}

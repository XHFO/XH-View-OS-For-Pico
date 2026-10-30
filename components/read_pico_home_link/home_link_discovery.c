/*
 * SPDX-FileCopyrightText: 2026 XH Lab
 * SPDX-License-Identifier: Apache-2.0
 * 一次性 mDNS 查询与有界 DNS-SD 解析；不扫描网段、不访问账户。
 * One-shot mDNS queries and bounded DNS-SD parsing; no subnet scan or account access.
 */
#include "home_link_discovery_internal.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

static const char service_type[] = "_home-assistant._tcp.local";

static uint16_t u16(const uint8_t *p) { return (uint16_t)((p[0] << 8) | p[1]); }

// 限制指针跳转与域名总长，拒绝环和截断。/ Bound pointer hops and name length; reject cycles and truncation.
static bool dns_name(const uint8_t *p, size_t n, size_t *offset, char out[RP_HOME_DNS_NAME])
{
    size_t at = *offset, used = 0, next = 0;
    unsigned hops = 0;
    while (at < n && ++hops < 128) {
        unsigned len = p[at++];
        if ((len & 0xc0) == 0xc0) {
            if (at >= n) return false;
            size_t target = ((len & 0x3f) << 8) | p[at++];
            if (target >= n) return false;
            if (!next) next = at;
            at = target;
        } else {
            if (len & 0xc0) return false;
            if (!len) {
                out[used] = 0;
                *offset = next ? next : at;
                return true;
            }
            if (at + len > n || used + len + (used != 0) >= RP_HOME_DNS_NAME) return false;
            if (used) out[used++] = '.';
            for (unsigned i = 0; i < len; ++i) {
                if (!p[at + i] || p[at + i] == '.') return false;
                out[used++] = (char)p[at + i];
            }
            at += len;
        }
    }
    return false;
}

static bool instance_name(const char *name)
{
    size_t n = strlen(name), suffix = sizeof(service_type) - 1;
    return n > suffix + 1 && name[n - suffix - 1] == '.' && !strcasecmp(name + n - suffix, service_type);
}

static rp_home_dns_service_t *get_service(rp_home_dns_t *s, const char *name, bool create)
{
    rp_home_dns_service_t *empty = NULL;
    for (unsigned i = 0; i < RP_HOME_DISCOVERY_MAX; ++i) {
        if (!strcasecmp(s->services[i].instance, name)) return &s->services[i];
        if (!empty && !s->services[i].instance[0]) empty = &s->services[i];
    }
    if (!create) return NULL;
    if (!empty) { s->truncated = true; return NULL; }
    strcpy(empty->instance, name);
    return empty;
}

static void txt_value(char *out, size_t capacity, const uint8_t *p, size_t n)
{
    if (!n || n >= capacity || memchr(p, 0, n)) return;
    memcpy(out, p, n);
    out[n] = 0;
}

static bool records(rp_home_dns_t *s, const uint8_t *p, size_t n)
{
    if (n < 12 || n > RP_HOME_DNS_PACKET || !(p[2] & 0x80) || (p[2] & 0x7a) || (p[3] & 15)) return false;
    unsigned q = u16(p + 4), count = u16(p + 6) + u16(p + 8) + u16(p + 10);
    if (q > 16 || count > 128) return false;
    size_t at = 12;
    char name[RP_HOME_DNS_NAME], target[RP_HOME_DNS_NAME];
    for (unsigned i = 0; i < q; ++i) {
        if (!dns_name(p, n, &at, name) || at + 4 > n) return false;
        at += 4;
    }
    for (unsigned i = 0; i < count; ++i) {
        if (!dns_name(p, n, &at, name) || at + 10 > n) return false;
        unsigned type = u16(p + at), klass = u16(p + at + 2) & 0x7fff, length = u16(p + at + 8);
        bool live = p[at + 4] || p[at + 5] || p[at + 6] || p[at + 7];
        at += 10;
        size_t end = at + length, value = at;
        if (end > n) return false;
        if (klass != 1) { at = end; continue; }
        if (type == 12 || type == 33) {
            if (type == 33) value += 6;
            if (value >= end || !dns_name(p, n, &value, target) || value != end) return false;
        }
        if (type == 16) {
            while (value < end) {
                unsigned len = p[value++];
                if (value + len > end) return false;
                value += len;
            }
        }
        if (s && type == 12 && !strcasecmp(name, service_type) && instance_name(target)) {
            rp_home_dns_service_t *entry = get_service(s, target, live);
            if (entry && !live) memset(entry, 0, sizeof(*entry));
        } else if (s && (type == 33 || type == 16) && instance_name(name)) {
            rp_home_dns_service_t *entry = get_service(s, name, live);
            if (entry) {
                if (type == 33) {
                    entry->port = live ? u16(p + at + 4) : 0;
                    strcpy(entry->host, target);
                } else {
                    entry->internal_url[0] = entry->base_url[0] = entry->name[0] = 0;
                    for (value = at; live && value < end;) {
                        unsigned len = p[value++];
                        const uint8_t *v = p + value;
                        if (len > 13 && !memcmp(v, "internal_url=", 13)) txt_value(entry->internal_url, sizeof(entry->internal_url), v + 13, len - 13);
                        else if (len > 9 && !memcmp(v, "base_url=", 9)) txt_value(entry->base_url, sizeof(entry->base_url), v + 9, len - 9);
                        else if (len > 14 && !memcmp(v, "location_name=", 14)) txt_value(entry->name, sizeof(entry->name), v + 14, len - 14);
                        value += len;
                    }
                }
            }
        } else if (s && type == 1 && length == 4) {
            rp_home_dns_address_t *entry = NULL, *empty = NULL;
            for (unsigned j = 0; j < RP_HOME_DISCOVERY_MAX * 2; ++j) {
                if (!strcasecmp(s->addresses[j].host, name)) entry = &s->addresses[j];
                if (!s->addresses[j].host[0]) empty = &s->addresses[j];
            }
            if (!entry && live) entry = empty;
            if (entry) {
                if (!live) memset(entry, 0, sizeof(*entry));
                else { strcpy(entry->host, name); memcpy(entry->ip, p + at, 4); }
            }
        }
        at = end;
    }
    return at == n;
}

bool rp_home_dns_feed(rp_home_dns_t *s, const uint8_t *p, size_t n)
{
    // 先完整验证，坏包不能留下半份可选地址。/ Validate first so malformed packets cannot partially publish a result.
    return s && p && records(NULL, p, n) && records(s, p, n);
}

static bool url_host(const char *url, const char **host, size_t *length)
{
    const char *start = !strncmp(url, "https://", 8) ? url + 8 : !strncmp(url, "http://", 7) ? url + 7 : NULL;
    if (!start || !*start) return false;
    for (const char *p = start; *p; ++p) {
        if ((unsigned char)*p <= 32 || (unsigned char)*p >= 127 || strchr("@?#\\", *p)) return false;
    }
    size_t n = strcspn(start, ":/");
    if (!n || n >= RP_HOME_DNS_NAME) return false;
    if ((n == 9 && !strncasecmp(start, "localhost", n)) ||
        (n >= 4 && !strncmp(start, "127.", 4)) ||
        (n == 7 && !strncmp(start, "0.0.0.0", n))) return false;
    for (size_t i = 0; i < n; ++i) {
        char c = start[i];
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '.')) return false;
    }
    if (start[n] == ':') {
        unsigned port = 0;
        const char *p = start + n + 1;
        if (*p < '0' || *p > '9') return false;
        for (; *p >= '0' && *p <= '9'; ++p) {
            port = port * 10 + (*p - '0');
            if (port > 65535) return false;
        }
        if (!port || (*p && *p != '/')) return false;
    }
    *host = start; *length = n;
    return true;
}

void rp_home_dns_results(const rp_home_dns_t *s, rp_home_discovery_t *out)
{
    memset(out, 0, sizeof(*out));
    out->truncated = s->truncated;
    for (unsigned i = 0; i < RP_HOME_DISCOVERY_MAX; ++i) {
        const rp_home_dns_service_t *e = &s->services[i];
        if (!e->instance[0] || !e->port || !e->host[0]) continue;
        char ip[16] = "";
        for (unsigned j = 0; j < RP_HOME_DISCOVERY_MAX * 2; ++j) {
            const rp_home_dns_address_t *a = &s->addresses[j];
            if (!strcasecmp(a->host, e->host) && a->ip[0] && a->ip[0] != 127 && a->ip[0] < 224)
                snprintf(ip, sizeof(ip), "%u.%u.%u.%u", a->ip[0], a->ip[1], a->ip[2], a->ip[3]);
        }
        const char *selected = NULL, *host = NULL;
        size_t length = 0;
        if (url_host(e->internal_url, &host, &length)) selected = e->internal_url;
        else if (url_host(e->base_url, &host, &length) &&
                 ((strlen(e->host) == length && !strncasecmp(host, e->host, length)) ||
                  (strlen(ip) == length && !strncmp(host, ip, length)))) selected = e->base_url;
        rp_home_discovered_t *r = &out->items[out->count];
        if (selected && strlen(selected) >= sizeof(r->url)) continue;
        if (selected) strcpy(r->url, selected);
        else if (ip[0]) snprintf(r->url, sizeof(r->url), "http://%s:%u", ip, e->port);
        else continue;
        if (e->name[0]) strcpy(r->name, e->name);
        else {
            size_t n = strlen(e->instance) - sizeof(service_type);
            if (n >= sizeof(r->name)) n = sizeof(r->name) - 1;
            memcpy(r->name, e->instance, n); r->name[n] = 0;
        }
        bool duplicate = false;
        for (unsigned j = 0; j < out->count; ++j) if (!strcmp(r->url, out->items[j].url)) duplicate = true;
        if (!duplicate) ++out->count;
        else memset(r, 0, sizeof(*r));
    }
}

size_t rp_home_dns_query(uint8_t *p, size_t capacity, const char *name, uint16_t type)
{
    if (!p || !name || !*name || capacity < 18) return 0;
    memset(p, 0, 12); p[5] = 1;
    size_t at = 12;
    while (*name) {
        size_t len = strcspn(name, ".");
        if (!len || len > 63 || at + len + 6 > capacity) return 0;
        p[at++] = (uint8_t)len; memcpy(p + at, name, len); at += len;
        name += len;
        if (*name) ++name;
    }
    p[at++] = 0; p[at++] = (uint8_t)(type >> 8); p[at++] = (uint8_t)type;
    // QU与临时源端口请求单播响应，无需启动完整mDNS响应器。/ QU and an ephemeral source port request unicast replies without a responder.
    p[at++] = 0x80; p[at++] = 1;
    return at;
}

#ifndef RP_HOME_DISCOVERY_PARSER_ONLY
#ifdef ESP_PLATFORM
#include "lwip/sockets.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
static int64_t now_ms(void) { return esp_timer_get_time() / 1000; }
#else
#include <sys/socket.h>
#include <sys/select.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <time.h>
static int64_t now_ms(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return (int64_t)t.tv_sec * 1000 + t.tv_nsec / 1000000; }
#endif

static bool query(int fd, const struct sockaddr_in *to, uint8_t *packet, const char *name, uint16_t type)
{
    size_t n = rp_home_dns_query(packet, RP_HOME_DNS_PACKET, name, type);
    return n && sendto(fd, packet, n, 0, (const struct sockaddr *)to, sizeof(*to)) == (int)n;
}

bool rp_home_discover(rp_home_discovery_t *out, uint32_t timeout_ms, bool (*cancel)(void *), void *context)
{
    if (!out) return false;
    memset(out, 0, sizeof(*out));
    if (cancel && cancel(context)) return false;
#if defined(PICO_NATIVE_PREVIEW) && !defined(ESP_PLATFORM)
    const char *offline = getenv("RPICO_HOME_LINK_OFFLINE");
    if (offline && !strcmp(offline, "1")) return true;
#endif
    if (timeout_ms < 500) timeout_ms = 500;
    if (timeout_ms > 4000) timeout_ms = 4000;
    size_t bytes = sizeof(rp_home_dns_t) + RP_HOME_DNS_PACKET;
#ifdef ESP_PLATFORM
    rp_home_dns_t *state = heap_caps_calloc(1, bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
#else
    rp_home_dns_t *state = calloc(1, bytes);
#endif
    if (!state) return false;
    uint8_t *packet = (uint8_t *)(state + 1);
    int fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    bool okay = false;
    if (fd < 0) goto done;
    struct sockaddr_in local = { .sin_family = AF_INET, .sin_port = 0, .sin_addr.s_addr = INADDR_ANY };
    struct sockaddr_in to = { .sin_family = AF_INET, .sin_port = htons(5353) };
    to.sin_addr.s_addr = inet_addr("224.0.0.251");
    unsigned char ttl = 255;
    if (bind(fd, (struct sockaddr *)&local, sizeof(local)) || setsockopt(fd, IPPROTO_IP, IP_MULTICAST_TTL, &ttl, sizeof(ttl))) goto done;
    int64_t deadline = now_ms() + timeout_ms, next_query = 0;
    unsigned received = 0;
    okay = true;
    while (now_ms() < deadline) {
        if (cancel && cancel(context)) { okay = false; break; }
        if (now_ms() >= next_query) {
            if (!query(fd, &to, packet, service_type, 12)) { okay = false; break; }
            for (unsigned i = 0; i < RP_HOME_DISCOVERY_MAX; ++i) {
                const rp_home_dns_service_t *e = &state->services[i];
                if (!e->instance[0]) continue;
                if (!e->port) query(fd, &to, packet, e->instance, 33);
                if (!e->internal_url[0]) query(fd, &to, packet, e->instance, 16);
                if (e->host[0]) query(fd, &to, packet, e->host, 1);
            }
            next_query = now_ms() + 1000;
        }
        struct timeval wait = { .tv_sec = 0, .tv_usec = 100000 };
        fd_set set; FD_ZERO(&set); FD_SET(fd, &set);
        int ready = select(fd + 1, &set, NULL, NULL, &wait);
        if (ready < 0) { okay = false; break; }
        if (ready > 0) {
            struct sockaddr_in from; socklen_t length = sizeof(from);
            int n = recvfrom(fd, packet, RP_HOME_DNS_PACKET, 0, (struct sockaddr *)&from, &length);
            if (n > 0 && from.sin_port == htons(5353)) {
                rp_home_dns_feed(state, packet, (size_t)n);
                if (++received >= 128) break;
            }
        }
    }
    if (okay) rp_home_dns_results(state, out);
 done:
    if (fd >= 0) close(fd);
    free(state);
    return okay;
}
#endif

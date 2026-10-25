/*
 * SPDX-FileCopyrightText: 2026 XH Lab
 * SPDX-License-Identifier: Apache-2.0
 * 局域网填写服务。与电脑管理页互斥占用 80 端口，配置写入 PGA 的 pico_home_pga。
 * LAN fill-in service. It exclusively shares port 80 with the computer manage page and writes PGA pico_home_pga.
 */
#ifndef PICO_HOME_LINK_FULL
#define PICO_HOME_LINK_FULL 0
#endif
#include "pico_home_portal.h"
#include "pico_home_pga.h"
#include "home_link_portal.h"
#include "read_pico_connectivity.h"
#include "read_pico_management.h"
#include "esp_heap_caps.h"
#include "esp_http_server.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <stdio.h>
#include <string.h>

#define PORTAL_LIFETIME_MS (10 * 60 * 1000)

static SemaphoreHandle_t s_lock;
static httpd_handle_t s_http;
static int s_owner;
static bool s_active;
static char s_token[33];
static char s_ip[48];
static char s_url[160];
static char s_error[64];
static int64_t s_expires;
static int64_t s_next_check;
static rp_portal_public_t s_public;

static void lock(void) {
    if (s_lock) xSemaphoreTake(s_lock, portMAX_DELAY);
}
static void unlock(void) { if (s_lock) xSemaphoreGive(s_lock); }
static void wipe(void *p, size_t n) {
    volatile unsigned char *v = p;
    while (n--) *v++ = 0;
}
static bool ipv4(const char *s) {
    int parts = 0;
    if (!s || !*s) return false;
    while (*s) {
        if (parts == 4) return false;
        int n = 0, digits = 0;
        while (*s >= '0' && *s <= '9') {
            n = n * 10 + *s - '0';
            if (n > 255 || ++digits > 3) return false;
            s++;
        }
        if (!digits) return false;
        parts++;
        if (*s == '.') s++;
        else if (*s) return false;
    }
    return parts == 4;
}
static void public_get(rp_portal_public_t *out) {
    lock();
    *out = s_public;
    int64_t left = s_expires - esp_timer_get_time() / 1000;
    out->active = s_active && left > 0;
    out->seconds_left = out->active ? (uint32_t)((left + 999) / 1000) : 0;
    unlock();
}
static void public_set(const rp_portal_public_t *in) {
    if (!in) return;
    lock();
    s_public.phase = in->phase;
    s_public.revision = in->revision;
    unlock();
}
static bool portal_active_cb(void) {
    lock();
    bool active = s_active && esp_timer_get_time() / 1000 < s_expires;
    unlock();
    return active;
}

static void *alloc(size_t n) { return heap_caps_calloc(1, n, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT); }

static bool load_cb(rp_home_config_t *config, rp_home_snapshot_t *snapshot) {
    return ps_home_pga_portal_load(config, snapshot);
}
static bool commit_cb(const rp_home_config_t *config, bool *queued) {
    return ps_home_pga_portal_commit(config, queued);
}

void ps_home_portal_register(void) {
#if !PICO_HOME_LINK_FULL
    rp_home_store_bridge_t next = {load_cb, commit_cb, portal_active_cb, public_get, public_set};
    rp_home_set_store_bridge(&next);
#else
    rp_home_set_store_bridge(NULL);
#endif
}

static void stop_http(void) {
    httpd_handle_t http = NULL;
    lock();
    http = s_http;
    s_http = NULL;
    s_active = false;
    if (s_public.phase == RP_PORTAL_OPEN) s_public.phase = RP_PORTAL_IDLE;
    s_url[0] = 0;
    s_ip[0] = 0;
    s_expires = 0;
    wipe(s_token, sizeof(s_token));
    unlock();
    if (http) httpd_stop(http);
    ps_home_pga_portal_set_owner(false);
    s_owner = PS_HOME_PORTAL_OWNER_NONE;
}

void ps_home_portal_stop(void) { stop_http(); }
void ps_home_portal_stop_if_home(void) {
    if (s_owner == PS_HOME_PORTAL_OWNER_HOME) stop_http();
}
bool ps_home_portal_active(void) { return portal_active_cb(); }

void ps_home_portal_view(ps_home_portal_view_t *out) {
    if (!out) return;
    memset(out, 0, sizeof(*out));
    lock();
    int64_t left = s_expires - esp_timer_get_time() / 1000;
    out->active = s_active && left > 0;
    out->seconds_left = out->active ? (uint32_t)((left + 999) / 1000) : 0;
    out->revision = s_public.revision;
    snprintf(out->phase, sizeof(out->phase), "%s", rp_portal_phase_name(s_public.phase));
    if (out->active) snprintf(out->url, sizeof(out->url), "%s", s_url);
    snprintf(out->error, sizeof(out->error), "%s", s_error);
    unlock();
}

void ps_home_portal_status_suffix(char *out, size_t capacity) {
    if (!out || !capacity) return;
    out[0] = 0;
#if PICO_HOME_LINK_FULL
    snprintf(out, capacity,
             ",\"home_portal\":{\"active\":false,\"phase\":\"idle\",\"revision\":0,\"seconds_left\":0,\"writer\":\"manage\"}");
#else
    rp_portal_public_t pub;
    public_get(&pub);
    snprintf(out, capacity,
             ",\"home_portal\":{\"active\":%s,\"phase\":\"%s\",\"revision\":%u,\"seconds_left\":%u,\"writer\":\"device\"}",
             pub.active ? "true" : "false", rp_portal_phase_name(pub.phase), (unsigned)pub.revision,
             (unsigned)pub.seconds_left);
#endif
}

int ps_home_portal_mac_handle(const char *method, char *out, size_t capacity) {
    if (!out || capacity < 64) return 500;
#if PICO_HOME_LINK_FULL
    snprintf(out, capacity, "{\"ok\":false,\"error\":\"not_found\"}");
    return 404;
#else
    if (!method || strcmp(method, "GET")) {
        snprintf(out, capacity, "{\"error\":\"method\"}");
        return 405;
    }
    rp_home_config_t *cfg = alloc(sizeof(*cfg));
    rp_home_snapshot_t *snap = alloc(sizeof(*snap));
    read_pico_connectivity_status_t *net = alloc(sizeof(*net));
    if (!cfg || !snap || !net || !ps_home_pga_portal_load(cfg, snap)) {
        free(cfg);
        free(snap);
        free(net);
        snprintf(out, capacity, "{\"error\":\"memory\"}");
        return 503;
    }
    read_pico_connectivity_get_status(net);
    const char *saved[READ_PICO_CONNECTIVITY_SAVED_MAX];
    for (size_t i = 0; i < net->saved_count && i < READ_PICO_CONNECTIVITY_SAVED_MAX; i++) saved[i] = net->saved[i].ssid;
    rp_portal_public_t pub;
    public_get(&pub);
    rp_home_web_request_t request = {.method = "GET",
                                     .path = "/api/manage/pico/home-link",
                                     .network_online = net->connected && !net->suspended && !net->transfer_active,
                                     .current_wifi = net->ssid,
                                     .saved_wifi = saved,
                                     .saved_wifi_count = net->saved_count < READ_PICO_CONNECTIVITY_SAVED_MAX
                                                            ? net->saved_count
                                                            : READ_PICO_CONNECTIVITY_SAVED_MAX};
    size_t used = 0;
    bool ok = rp_portal_write_mac(&request, cfg, snap, &pub, "pga", out, capacity, &used);
    wipe(cfg, sizeof(*cfg));
    free(cfg);
    free(snap);
    free(net);
    if (!ok) {
        snprintf(out, capacity, "{\"error\":\"output_limit\"}");
        return 507;
    }
    return 200;
#endif
}

bool ps_home_portal_tick(void) {
    if (!portal_active_cb()) return false;
    int64_t now = esp_timer_get_time() / 1000;
    lock();
    bool due = now >= s_next_check || now >= s_expires;
    if (due) s_next_check = now + 1000;
    char ip[48];
    snprintf(ip, sizeof(ip), "%s", s_ip);
    unlock();
    if (!due) return false;
    if (now >= s_expires || read_pico_management_active() || !read_pico_connectivity_online()) {
        lock();
        if (now >= s_expires) snprintf(s_error, sizeof(s_error), "%s", "门户已到期");
        else if (!read_pico_connectivity_online()) {
            snprintf(s_error, sizeof(s_error), "%s", "Wi-Fi 已断开");
            if (s_public.phase != RP_PORTAL_SAVED) s_public.phase = RP_PORTAL_OFFLINE;
        } else snprintf(s_error, sizeof(s_error), "%s", "管理网页已开启");
        unlock();
        stop_http();
        return true;
    }
    read_pico_connectivity_status_t *net = alloc(sizeof(*net));
    if (!net) {
        lock();
        snprintf(s_error, sizeof(s_error), "%s", "内存不足");
        s_public.phase = RP_PORTAL_ERROR;
        unlock();
        stop_http();
        return true;
    }
    read_pico_connectivity_get_status(net);
    bool stop = !net->connected || net->suspended || net->transfer_active || strcmp(net->ip, ip);
    free(net);
    if (!stop) return false;
    lock();
    snprintf(s_error, sizeof(s_error), "%s", "Wi-Fi 已断开");
    if (s_public.phase != RP_PORTAL_SAVED) s_public.phase = RP_PORTAL_OFFLINE;
    unlock();
    stop_http();
    return true;
}

static bool token_ok(httpd_req_t *r) {
    char key[40] = {0};
    if (httpd_req_get_hdr_value_len(r, "X-Pico-Portal-Key") != 32 ||
        httpd_req_get_hdr_value_str(r, "X-Pico-Portal-Key", key, sizeof(key)) != ESP_OK)
        return false;
    unsigned diff = 0;
    lock();
    bool live = s_active && esp_timer_get_time() / 1000 < s_expires;
    for (int i = 0; i < 32; i++) diff |= (unsigned char)key[i] ^ (unsigned char)s_token[i];
    unlock();
    wipe(key, sizeof(key));
    return live && diff == 0;
}
static bool host_ok(httpd_req_t *r) {
    char host[64] = {0}, origin[80] = {0}, ip[48];
    lock();
    snprintf(ip, sizeof(ip), "%s", s_ip);
    unlock();
    size_t len = httpd_req_get_hdr_value_len(r, "Host");
    if (!ipv4(ip) || !len || len >= sizeof(host) || httpd_req_get_hdr_value_str(r, "Host", host, sizeof(host)) != ESP_OK)
        return false;
    char port80[72];
    snprintf(port80, sizeof(port80), "%s:80", ip);
    if (strcmp(host, ip) && strcmp(host, port80)) return false;
    len = httpd_req_get_hdr_value_len(r, "Origin");
    if (len) {
        if (len >= sizeof(origin) || httpd_req_get_hdr_value_str(r, "Origin", origin, sizeof(origin)) != ESP_OK)
            return false;
        char expected[80];
        snprintf(expected, sizeof(expected), "http://%s", host);
        if (strcmp(origin, expected)) return false;
    }
    return true;
}
static esp_err_t send_json(httpd_req_t *r, int status, const char *body, size_t n) {
    const char *text = status == 200 ? "200 OK" : status == 400 ? "400 Bad Request"
                                                  : status == 401 ? "401 Unauthorized"
                                                  : status == 405 ? "405 Method Not Allowed"
                                                  : status == 409 ? "409 Conflict"
                                                  : status == 413 ? "413 Payload Too Large"
                                                  : status == 503 ? "503 Service Unavailable"
                                                                   : "507 Insufficient Storage";
    httpd_resp_set_hdr(r, "Cache-Control", "no-store");
    httpd_resp_set_hdr(r, "X-Content-Type-Options", "nosniff");
    httpd_resp_set_hdr(r, "Referrer-Policy", "no-referrer");
    httpd_resp_set_status(r, text);
    httpd_resp_set_type(r, "application/json; charset=utf-8");
    return httpd_resp_send(r, body ? body : "{}", (ssize_t)(body ? n : 2));
}
static esp_err_t send_error(httpd_req_t *r, int status, const char *error) {
    char body[64];
    snprintf(body, sizeof(body), "{\"error\":\"%s\"}", error);
    return send_json(r, status, body, strlen(body));
}
static esp_err_t page(httpd_req_t *r) {
    if (!host_ok(r) || !portal_active_cb()) return send_error(r, 401, "unauthorized");
    size_t n = 0;
    const char *html = rp_portal_html(&n);
    httpd_resp_set_hdr(r, "Cache-Control", "no-store");
    httpd_resp_set_hdr(r, "X-Content-Type-Options", "nosniff");
    httpd_resp_set_hdr(r, "Referrer-Policy", "no-referrer");
    httpd_resp_set_hdr(r, "Content-Security-Policy",
                       "default-src 'none'; script-src 'unsafe-inline'; style-src 'unsafe-inline'; "
                       "connect-src 'self'; base-uri 'none'; frame-ancestors 'none'; form-action 'self'");
    httpd_resp_set_type(r, "text/html; charset=utf-8");
    return httpd_resp_send(r, html, (ssize_t)n);
}
static esp_err_t api(httpd_req_t *r) {
    if (!host_ok(r)) return send_error(r, 403, "request_rejected");
    if (!token_ok(r)) return send_error(r, 401, "unauthorized");
    if (r->content_len > RP_PORTAL_BODY_LIMIT) return send_error(r, 413, "too_large");
    if (r->content_len) {
        char type[80] = {0};
        if (httpd_req_get_hdr_value_str(r, "Content-Type", type, sizeof(type)) != ESP_OK ||
            (strcmp(type, "application/json") && strcmp(type, "application/json; charset=utf-8")))
            return send_error(r, 415, "unsupported_type");
    }
    char *body = alloc(r->content_len + 1);
    char *out = alloc(8192);
    read_pico_connectivity_status_t *net = alloc(sizeof(*net));
    if ((!body && r->content_len) || !out || !net) {
        free(body);
        free(out);
        free(net);
        return send_error(r, 503, "memory");
    }
    size_t got = 0;
    while (got < r->content_len) {
        int n = httpd_req_recv(r, body + got, r->content_len - got);
        if (n <= 0) {
            wipe(body, r->content_len + 1);
            free(body);
            free(out);
            free(net);
            return send_error(r, 400, "bad_json");
        }
        got += (size_t)n;
    }
    if (!token_ok(r)) {
        wipe(body, r->content_len + 1);
        free(body);
        free(out);
        free(net);
        return send_error(r, 401, "unauthorized");
    }
    char path[64], query[RP_HOME_WEB_QUERY_LIMIT + 1] = {0};
    size_t path_bytes = strcspn(r->uri, "?");
    if (path_bytes >= sizeof(path) || httpd_req_get_url_query_len(r) >= sizeof(query)) {
        wipe(body, r->content_len + 1);
        free(body);
        free(out);
        free(net);
        return send_error(r, 400, "invalid_query");
    }
    memcpy(path, r->uri, path_bytes);
    path[path_bytes] = 0;
    if (httpd_req_get_url_query_len(r)) httpd_req_get_url_query_str(r, query, sizeof(query));
    read_pico_connectivity_get_status(net);
    const char *saved[READ_PICO_CONNECTIVITY_SAVED_MAX];
    for (size_t i = 0; i < net->saved_count && i < READ_PICO_CONNECTIVITY_SAVED_MAX; i++) saved[i] = net->saved[i].ssid;
    const char *method = r->method == HTTP_GET ? "GET" : r->method == HTTP_PUT ? "PUT" : r->method == HTTP_DELETE ? "DELETE" : "POST";
    rp_home_web_request_t request = {.method = method,
                                     .path = path,
                                     .query = query,
                                     .body = body,
                                     .body_bytes = got,
                                     .network_online = net->connected && !net->suspended && !net->transfer_active,
                                     .current_wifi = net->ssid,
                                     .saved_wifi = saved,
                                     .saved_wifi_count = net->saved_count < READ_PICO_CONNECTIVITY_SAVED_MAX
                                                            ? net->saved_count
                                                            : READ_PICO_CONNECTIVITY_SAVED_MAX};
    rp_home_web_response_t response = {0};
    esp_err_t result;
    if (!rp_portal_serve(&request, &response, out, 8192, false)) result = send_error(r, 404, "not_found");
    else result = send_json(r, response.http_status, out, response.body_bytes);
    wipe(body, r->content_len + 1);
    free(body);
    free(out);
    free(net);
    return result;
}

bool ps_home_portal_start(int owner) {
    ps_home_portal_register();
#if PICO_HOME_LINK_FULL
    (void)owner;
    (void)page;
    (void)api;
    lock();
    snprintf(s_error, sizeof(s_error), "%s", "请使用管理网页");
    unlock();
    return false;
#else
    if (!s_lock) s_lock = xSemaphoreCreateMutex();
    if (!s_lock) return false;
    lock();
    bool already = s_active && esp_timer_get_time() / 1000 < s_expires;
    if (already) s_owner = owner;
    unlock();
    if (already) return true;
    stop_http();
    ps_home_pga_init();
    if (!ps_home_pga_active()) {
        lock();
        snprintf(s_error, sizeof(s_error), "%s", "内存不足");
        s_public.phase = RP_PORTAL_ERROR;
        unlock();
        return false;
    }
    if (read_pico_management_active()) {
        lock();
        snprintf(s_error, sizeof(s_error), "%s", "请先关闭管理网页");
        s_public.phase = RP_PORTAL_ERROR;
        unlock();
        return false;
    }
    read_pico_connectivity_status_t *net = alloc(sizeof(*net));
    if (!net) {
        lock();
        snprintf(s_error, sizeof(s_error), "%s", "内存不足");
        s_public.phase = RP_PORTAL_ERROR;
        unlock();
        return false;
    }
    read_pico_connectivity_get_status(net);
    if (!net->connected || net->suspended || net->busy || net->transfer_active || !ipv4(net->ip)) {
        lock();
        snprintf(s_error, sizeof(s_error), "%s", "请先连接 Wi-Fi");
        s_public.phase = RP_PORTAL_OFFLINE;
        unlock();
        free(net);
        return false;
    }
    uint8_t random[16];
    char token[33];
    esp_fill_random(random, sizeof(random));
    for (size_t i = 0; i < sizeof(random); i++) snprintf(token + i * 2, 3, "%02x", random[i]);
    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.server_port = 80;
    cfg.ctrl_port = 32769;
    cfg.stack_size = 8192;
    cfg.max_uri_handlers = 8;
    cfg.lru_purge_enable = true;
    cfg.recv_wait_timeout = 5;
    cfg.send_wait_timeout = 5;
    httpd_handle_t http = NULL;
    if (httpd_start(&http, &cfg) != ESP_OK) {
        lock();
        snprintf(s_error, sizeof(s_error), "%s", "局域网页面启动失败");
        s_public.phase = RP_PORTAL_ERROR;
        unlock();
        wipe(token, sizeof(token));
        wipe(random, sizeof(random));
        free(net);
        return false;
    }
    const httpd_uri_t routes[] = {
        {.uri = "/h", .method = HTTP_GET, .handler = page},
        {.uri = "/api/lan/home", .method = HTTP_GET, .handler = api},
        {.uri = "/api/lan/home", .method = HTTP_PUT, .handler = api},
        {.uri = "/api/lan/printers", .method = HTTP_GET, .handler = api},
        {.uri = "/api/lan/printers", .method = HTTP_PUT, .handler = api},
    };
    for (size_t i = 0; i < sizeof(routes) / sizeof(routes[0]); i++) {
        if (httpd_register_uri_handler(http, &routes[i]) != ESP_OK) {
            httpd_stop(http);
            lock();
            snprintf(s_error, sizeof(s_error), "%s", "局域网页面启动失败");
            s_public.phase = RP_PORTAL_ERROR;
            unlock();
            wipe(token, sizeof(token));
            wipe(random, sizeof(random));
            free(net);
            return false;
        }
    }
    lock();
    snprintf(s_token, sizeof(s_token), "%s", token);
    snprintf(s_ip, sizeof(s_ip), "%s", net->ip);
    snprintf(s_url, sizeof(s_url), "http://%s/h#t=%s", net->ip, token);
    s_expires = esp_timer_get_time() / 1000 + PORTAL_LIFETIME_MS;
    s_next_check = 0;
    s_error[0] = 0;
    s_public.phase = RP_PORTAL_OPEN;
    s_http = http;
    s_active = true;
    s_owner = owner;
    unlock();
    ps_home_pga_portal_set_owner(true);
    wipe(token, sizeof(token));
    wipe(random, sizeof(random));
    free(net);
    return true;
#endif
}

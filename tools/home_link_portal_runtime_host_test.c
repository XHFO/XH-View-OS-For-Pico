/* SPDX-License-Identifier: Apache-2.0
 * 真实门户 HTTP 包装器的生命周期与鉴权回归；不使用套接字或真实凭据。
 * Lifecycle/authentication regression for the real HTTP wrapper; no sockets or real credentials.
 */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../main/system/pico_home_portal.c"

static int64_t fake_us;
static read_pico_connectivity_status_t network;
static bool manage_on, bridge_ready = true, owner_on, fail_start, fail_mutex;
static bool fail_load;
static int fail_allocation = -1, fail_register = -1;
static int starts, stops, init_calls, serve_calls, allocations;
static httpd_uri_t registered[8];
static size_t route_count;
static rp_home_store_bridge_t registered_bridge;
static int serve_status = 200;

int64_t esp_timer_get_time(void) { return fake_us; }
void esp_fill_random(void *out, size_t n) { memset(out, 0x5a, n); }
SemaphoreHandle_t xSemaphoreCreateMutex(void) { return fail_mutex ? NULL : (void *)&registered_bridge; }
int xSemaphoreTake(SemaphoreHandle_t sem, unsigned delay) { assert(sem && delay); return 1; }
int xSemaphoreGive(SemaphoreHandle_t sem) { assert(sem); return 1; }
void *heap_caps_calloc(size_t count, size_t bytes, unsigned caps) {
    assert(caps == (MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    allocations++;
    if (fail_allocation == 0) { fail_allocation = -1; return NULL; }
    if (fail_allocation > 0) fail_allocation--;
    return calloc(count, bytes);
}
void read_pico_connectivity_get_status(read_pico_connectivity_status_t *out) { *out = network; }
bool read_pico_connectivity_online(void) { return network.connected && !network.suspended && !network.transfer_active; }
bool read_pico_management_active(void) { return manage_on; }
void ps_home_pga_init(void) { init_calls++; }
bool ps_home_pga_active(void) { return bridge_ready; }
bool ps_home_pga_portal_load(rp_home_config_t *config, rp_home_snapshot_t *snapshot) {
    if (fail_load) return false;
    memset(config, 0, sizeof(*config));
    memset(snapshot, 0, sizeof(*snapshot));
    return true;
}
bool ps_home_pga_portal_commit(const rp_home_config_t *config, bool *queued) {
    assert(config);
    if (queued) *queued = true;
    return true;
}
void ps_home_pga_portal_set_owner(bool owns) { owner_on = owns; }
bool ps_home_pga_discover(bool start, rp_home_discovery_t *out, bool *busy, bool *failed) {
    (void)start; memset(out, 0, sizeof(*out)); *busy = *failed = false; return true;
}
void rp_home_set_discovery_bridge(rp_home_discovery_bridge_t callback) { assert(callback); }
void rp_home_set_store_bridge(const rp_home_store_bridge_t *bridge) {
    if (bridge) registered_bridge = *bridge;
    else memset(&registered_bridge, 0, sizeof(registered_bridge));
}
const char *rp_portal_phase_name(uint8_t phase) {
    static const char *names[] = {"idle", "open", "saved", "offline", "error"};
    return phase < sizeof(names) / sizeof(names[0]) ? names[phase] : "idle";
}
/* Same generated asset and API implementation used by the host model and ESP firmware. */
#include "../components/read_pico_home_link/home_link_portal_html_gzip.inc"
bool rp_portal_write_mac(const rp_home_web_request_t *request, const rp_home_config_t *config,
                         const rp_home_snapshot_t *snapshot, const rp_portal_public_t *portal, const char *mode,
                         char *output, size_t capacity, size_t *used) {
    assert(request && config && snapshot && portal && mode);
    int n = snprintf(output, capacity, "{\"ok\":true,\"home_link_mode\":\"%s\"}", mode);
    if (used) *used = n > 0 && (size_t)n < capacity ? (size_t)n : 0;
    return n > 0 && (size_t)n < capacity;
}
bool rp_portal_serve(const rp_home_web_request_t *request, rp_home_web_response_t *response, char *output,
                     size_t capacity, bool manage) {
    assert(!manage && request && capacity >= 32);
    serve_calls++;
    if (strcmp(request->path, "/api/lan/home") && strcmp(request->path, "/api/lan/printers")) return false;
    snprintf(output, capacity, "{\"ok\":true}");
    response->body_bytes = strlen(output);
    response->http_status = serve_status;
    return true;
}
esp_err_t httpd_start(httpd_handle_t *out, const httpd_config_t *cfg) {
    assert(cfg->server_port == 80 && cfg->ctrl_port == 32769 && cfg->max_uri_handlers >= 6);
    assert(cfg->recv_wait_timeout == 5 && cfg->send_wait_timeout == 5);
    if (fail_start) return ESP_FAIL;
    starts++;
    route_count = 0;
    *out = (void *)&registered;
    return ESP_OK;
}
esp_err_t httpd_stop(httpd_handle_t handle) { assert(handle == (void *)&registered); stops++; return ESP_OK; }
esp_err_t httpd_register_uri_handler(httpd_handle_t handle, const httpd_uri_t *route) {
    assert(handle && route && route_count < 8);
    if ((int)route_count == fail_register) return ESP_FAIL;
    registered[route_count++] = *route;
    return ESP_OK;
}
static const char *header(httpd_req_t *request, const char *name) {
    if (!strcmp(name, "Host")) return request->host;
    if (!strcmp(name, "Origin")) return request->origin;
    if (!strcmp(name, "X-Pico-Portal-Key")) return request->key;
    if (!strcmp(name, "Content-Type")) return request->content_type;
    return NULL;
}
size_t httpd_req_get_hdr_value_len(httpd_req_t *request, const char *name) {
    const char *value = header(request, name);
    return value ? strlen(value) : 0;
}
esp_err_t httpd_req_get_hdr_value_str(httpd_req_t *request, const char *name, char *out, size_t capacity) {
    const char *value = header(request, name);
    if (!value || strlen(value) >= capacity) return ESP_FAIL;
    snprintf(out, capacity, "%s", value);
    return ESP_OK;
}
size_t httpd_req_get_url_query_len(httpd_req_t *request) {
    const char *query = strchr(request->uri, '?');
    return query ? strlen(query + 1) : 0;
}
esp_err_t httpd_req_get_url_query_str(httpd_req_t *request, char *out, size_t capacity) {
    const char *query = strchr(request->uri, '?');
    if (!query || strlen(query + 1) >= capacity) return ESP_FAIL;
    snprintf(out, capacity, "%s", query + 1);
    return ESP_OK;
}
int httpd_req_recv(httpd_req_t *request, char *out, size_t capacity) {
    if (request->fail_receive) return -1;
    size_t n = request->content_len - request->received;
    if (n > capacity) n = capacity;
    memcpy(out, request->body + request->received, n);
    request->received += n;
    if (request->expire_during_receive) fake_us += (int64_t)PORTAL_LIFETIME_MS * 1000;
    return (int)n;
}
esp_err_t httpd_resp_set_hdr(httpd_req_t *request, const char *name, const char *value) {
    if (!strcmp(name, "Cache-Control")) request->no_store = !strcmp(value, "no-store");
    if (!strcmp(name, "X-Content-Type-Options")) request->no_sniff = !strcmp(value, "nosniff");
    if (!strcmp(name, "Referrer-Policy")) request->no_referrer = !strcmp(value, "no-referrer");
    if (!strcmp(name, "Content-Encoding")) { assert(!strcmp(value, "gzip")); request->gzip = true; }
    if (!strcmp(name, "Content-Security-Policy")) snprintf(request->csp, sizeof(request->csp), "%s", value);
    return ESP_OK;
}
esp_err_t httpd_resp_set_status(httpd_req_t *request, const char *status) {
    snprintf(request->response_status, sizeof(request->response_status), "%s", status);
    return ESP_OK;
}
esp_err_t httpd_resp_set_type(httpd_req_t *request, const char *type) {
    snprintf(request->response_type, sizeof(request->response_type), "%s", type);
    return ESP_OK;
}
esp_err_t httpd_resp_send(httpd_req_t *request, const char *body, ssize_t n) {
    assert(n >= 0 && (size_t)n < sizeof(request->response_body));
    memcpy(request->response_body, body, (size_t)n);
    request->response_body[n] = 0;
    request->response_bytes = (size_t)n;
    return ESP_OK;
}

static void reset(void) {
    ps_home_portal_stop();
    fake_us = 0;
    memset(&network, 0, sizeof(network));
    memset(&s_public, 0, sizeof(s_public));
    s_error[0] = 0;
    network.connected = true;
    snprintf(network.ip, sizeof(network.ip), "192.168.4.20");
    snprintf(network.ssid, sizeof(network.ssid), "Home");
    manage_on = fail_start = fail_load = fail_mutex = false;
    bridge_ready = true;
    fail_allocation = fail_register = -1;
    starts = stops = serve_calls = allocations = 0;
    serve_status = 200;
}
static httpd_req_t request(int method, const char *path) {
    httpd_req_t r = {0};
    r.method = method;
    r.uri = path;
    r.host = "192.168.4.20";
    r.origin = "http://192.168.4.20";
    r.key = "5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a";
    return r;
}
static void expect_status(httpd_req_t *r, int status) {
    assert(api(r) == ESP_OK);
    assert(atoi(r->response_status) == status);
    assert(r->no_store && r->no_sniff && r->no_referrer);
    assert(!r->gzip);
}
static void check_lifecycle(void) {
    reset();
    assert(!ps_home_portal_tick());
    assert(ps_home_portal_start(PS_HOME_PORTAL_OWNER_HOME));
    assert(owner_on && starts == 1 && ps_home_portal_active() && registered_bridge.portal_active());
    assert(route_count == 8);
    bool has_delete = false;
    for (size_t i = 0; i < route_count; i++)
        if (registered[i].method == HTTP_DELETE && !strcmp(registered[i].uri, "/api/lan/printers")) has_delete = true;
    assert(has_delete);
    assert(ps_home_portal_start(PS_HOME_PORTAL_OWNER_GAMES) && starts == 1);
    ps_home_portal_stop_if_home();
    assert(stops == 0 && owner_on);
    fake_us = ((int64_t)PORTAL_LIFETIME_MS - 1) * 1000;
    assert(!ps_home_portal_tick() && owner_on);
    fake_us++;
    fake_us += 999;
    assert(!ps_home_portal_active());
    assert(ps_home_portal_tick());
    assert(stops == 1 && !owner_on && !s_http && !s_token[0] && !s_url[0]);
    ps_home_portal_view_t v;
    ps_home_portal_view(&v);
    assert(!v.active && !v.url[0] && !v.seconds_left && strstr(v.error, "到期"));
    assert(!ps_home_portal_tick() && stops == 1);
    assert(ps_home_portal_start(PS_HOME_PORTAL_OWNER_HOME) && starts == 2 && owner_on);
    ps_home_portal_stop_if_home();
    assert(!owner_on && stops == 2);

    for (int reason = 0; reason < 5; reason++) {
        reset();
        assert(ps_home_portal_start(PS_HOME_PORTAL_OWNER_HOME));
        if (reason == 0) manage_on = true;
        if (reason == 1) network.connected = false;
        if (reason == 2) network.suspended = true;
        if (reason == 3) network.transfer_active = true;
        if (reason == 4) snprintf(network.ip, sizeof(network.ip), "192.168.4.21");
        assert(ps_home_portal_tick() && !owner_on && stops == 1 && !s_http);
    }
    reset();
    assert(ps_home_portal_start(PS_HOME_PORTAL_OWNER_HOME));
    s_public.phase = RP_PORTAL_SAVED;
    fail_allocation = 0;
    assert(ps_home_portal_tick() && !owner_on && stops == 1);

    reset();
    network.busy = true; /* A scan/profile save does not remove the acquired STA IP. */
    network.saved_count = 0; /* Saved profiles do not determine portal availability. */
    assert(ps_home_portal_start(PS_HOME_PORTAL_OWNER_GAMES));
    assert(owner_on && starts == 1 && ps_home_portal_active());
    assert(!ps_home_portal_tick());
    ps_home_portal_view_t view;
    ps_home_portal_view(&view);
    assert(strstr(view.url, "http://192.168.4.20/h#t="));
    network.connected = false;
    fake_us += 1000000;
    assert(ps_home_portal_tick() && !owner_on && stops == 1);
    network.connected = true;
    assert(ps_home_portal_start(PS_HOME_PORTAL_OWNER_GAMES));
    puts("PASS: connected STA can open/reopen phone QR during scans with zero saved profiles; real link loss still closes it");
}
static void check_http(void) {
    reset();
    assert(ps_home_portal_start(PS_HOME_PORTAL_OWNER_GAMES));
    httpd_req_t r = request(HTTP_GET, "/api/lan/home");
    expect_status(&r, 200);
    assert(serve_calls == 1);
    r = request(HTTP_GET, "/api/lan/home"); r.key = "00000000000000000000000000000000";
    expect_status(&r, 401);
    r = request(HTTP_GET, "/api/lan/home"); r.host = "attacker.example";
    expect_status(&r, 403);
    r = request(HTTP_GET, "/api/lan/home"); r.origin = "http://attacker.example";
    expect_status(&r, 403);
    r = request(HTTP_GET, "/api/lan/home"); r.origin = NULL; r.host = "192.168.4.20:80";
    expect_status(&r, 200);
    r = request(HTTP_PUT, "/api/lan/home"); r.body = "{}"; r.content_len = 2; r.content_type = "text/plain";
    expect_status(&r, 415);
    r = request(HTTP_PUT, "/api/lan/home"); r.content_len = RP_PORTAL_BODY_LIMIT + 1;
    expect_status(&r, 413);
    r = request(HTTP_PUT, "/api/lan/home"); r.body = "{}"; r.content_len = 2;
    r.content_type = "application/json; charset=utf-8";
    expect_status(&r, 200);
    r = request(HTTP_DELETE, "/api/lan/printers?slot=0");
    expect_status(&r, 200);
    r = request(HTTP_GET, "/api/lan/missing");
    expect_status(&r, 404);
    r = request(HTTP_PUT, "/api/lan/home"); r.body = "{}"; r.content_len = 2;
    r.content_type = "application/json"; r.fail_receive = true;
    expect_status(&r, 400);
    fail_allocation = 0;
    r = request(HTTP_GET, "/api/lan/home");
    expect_status(&r, 503);
    for (size_t i = 0; i < 4; i++) {
        const int statuses[] = {405, 409, 507, 500};
        serve_status = statuses[i];
        r = request(HTTP_GET, "/api/lan/home");
        expect_status(&r, statuses[i]);
    }
    r = request(HTTP_GET, "/h");
    assert(page(&r) == ESP_OK && r.gzip && r.response_bytes == sizeof(portal_html_gzip));
    assert(!memcmp(r.response_body, portal_html_gzip, r.response_bytes));
    assert(!strcmp(r.response_type, "text/html; charset=utf-8"));
    assert(strstr(r.csp, "frame-ancestors 'none'") && r.no_referrer && r.no_store);
    int before = serve_calls;
    r = request(HTTP_PUT, "/api/lan/home"); r.body = "{}"; r.content_len = 2;
    r.content_type = "application/json"; r.expire_during_receive = true;
    expect_status(&r, 401);
    assert(serve_calls == before);
    r = request(HTTP_GET, "/h");
    assert(page(&r) == ESP_OK && atoi(r.response_status) == 401);
    assert(ps_home_portal_tick() && !owner_on);
}
static void check_start_failure(void) {
    for (int reason = 0; reason < 8; reason++) {
        reset();
        if (reason == 0) network.connected = false;
        if (reason == 1) manage_on = true;
        if (reason == 2) bridge_ready = false;
        if (reason == 3) fail_start = true;
        if (reason == 4) fail_register = 3;
        if (reason == 5) fail_allocation = 0;
        if (reason == 6) snprintf(network.ip, sizeof(network.ip), "999.1.1.1");
        if (reason == 7) snprintf(network.ip, sizeof(network.ip), "192.168.4.20.");
        assert(!ps_home_portal_start(PS_HOME_PORTAL_OWNER_HOME));
        assert(!owner_on && !ps_home_portal_active() && !s_http && !s_token[0]);
        assert(stops == (reason == 4 ? 1 : 0));
    }
    reset();
    s_lock = NULL;
    fail_mutex = true;
    assert(!ps_home_portal_start(PS_HOME_PORTAL_OWNER_HOME) && !owner_on);
    fail_mutex = false;
    assert(ps_home_portal_start(PS_HOME_PORTAL_OWNER_HOME));
    char out[8192];
    assert(ps_home_portal_mac_handle("POST", out, sizeof(out)) == 405);
    assert(ps_home_portal_mac_handle("GET", out, sizeof(out)) == 200 && strstr(out, "home_link_mode"));
    fail_load = true;
    assert(ps_home_portal_mac_handle("GET", out, sizeof(out)) == 503);
    ps_home_portal_stop();
}
int main(int argc, char **argv) {
    if (argc == 2 && !strcmp(argv[1], "--html-gzip")) {
        reset();
        assert(ps_home_portal_start(PS_HOME_PORTAL_OWNER_GAMES));
        httpd_req_t r = request(HTTP_GET, "/h");
        assert(page(&r) == ESP_OK && r.gzip);
        assert(fwrite(r.response_body, 1, r.response_bytes, stdout) == r.response_bytes);
        ps_home_portal_stop();
        return 0;
    }
    assert(argc == 1);
    check_lifecycle();
    check_http();
    check_start_failure();
    puts("PASS: real portal expiry cleanup/reopen, network ownership, HTTP status/auth, DELETE and PSRAM failures");
    return 0;
}

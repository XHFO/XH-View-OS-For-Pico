/* 生产HTTP适配器的无socket契约测试。/ Socket-free contracts for the production HTTP adapter. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "management_host_env.h"

static void *test_calloc(size_t, size_t);
static void test_free(void *);
#define calloc test_calloc
#define free test_free
#include "../components/read_pico_transfer/read_pico_management.c"
#undef calloc
#undef free

// 同一节嵌入构建已校验的真实归档，按生产起止标记发送。/ Embed the real checked-in archive under production blob labels.
__asm__(".globl _binary_manage_html_gz_start\n"
        "_binary_manage_html_gz_start:\n.incbin \"components/read_pico_transfer/assets/manage.html.gz\"\n"
        ".globl _binary_manage_html_gz_end\n_binary_manage_html_gz_end:\n");

static read_pico_connectivity_status_t test_net;
static rp_home_config_t test_config;
static int64_t test_us = 1000000;
static int locks, starts, stops, registrations, dispatches, response_code = 200, random_epoch, network_reads;
static int extension_calls;
static unsigned content_calls,content_cancels;
static bool expire_on_stream,revoke_on_stream,fail_on_send;
static const void *extension_body;
static size_t extension_bytes;
static bool allocation_fail, mutex_fail, start_fail, unknown_path, expire_on_recv;
static int registration_fail = -1;
static httpd_uri_t registered[6];
static size_t expected_body_size;
static struct { void *pointer; size_t bytes; bool scrub; } allocations[32];
static size_t live_allocations, cleared_bytes;
static char method_seen[12], path_seen[100], query_seen[769], body_seen[12289];
static bool online_seen, home_seen;
static size_t saved_seen;

static bool zeroed(const void *data, size_t bytes) {
    const unsigned char *p = data;
    for (size_t i = 0; i < bytes; i++) if (p[i]) return false;
    return true;
}
static void *test_calloc(size_t count, size_t size) {
    if (allocation_fail) return NULL;
    void *p = calloc(count, size); assert(p);
    assert(live_allocations < sizeof(allocations) / sizeof(allocations[0]));
    size_t bytes = count * size;
    allocations[live_allocations++] = (typeof(allocations[0])){p, bytes,
        bytes == sizeof(rp_home_config_t) || (expected_body_size && bytes == expected_body_size)};
    return p;
}
static void test_free(void *p) {
    if (!p) return;
    size_t i = 0;
    while (i < live_allocations && allocations[i].pointer != p) i++;
    assert(i < live_allocations);
    if (allocations[i].scrub) { assert(zeroed(p, allocations[i].bytes)); cleared_bytes += allocations[i].bytes; }
    allocations[i] = allocations[--live_allocations];
    free(p);
}
void *heap_caps_calloc(size_t n, size_t size, unsigned flags) {
    assert(flags == (MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)); return test_calloc(n, size);
}
int64_t esp_timer_get_time(void) { return test_us; }
void esp_fill_random(void *data, size_t bytes) {
    random_epoch++; for (size_t i = 0; i < bytes; i++) ((unsigned char *)data)[i] = (unsigned char)(random_epoch + i);
}
SemaphoreHandle_t xSemaphoreCreateMutex(void) { static int mutex; return mutex_fail ? NULL : &mutex; }
int xSemaphoreTake(SemaphoreHandle_t semaphore, uint32_t wait) { assert(semaphore && wait == portMAX_DELAY && !locks); locks++; return 1; }
int xSemaphoreGive(SemaphoreHandle_t semaphore) { assert(semaphore && locks == 1); locks--; return 1; }
void read_pico_connectivity_get_status(read_pico_connectivity_status_t *out) { network_reads++; *out = test_net; }
void read_pico_home_link_get_config(rp_home_config_t *out) { *out = test_config; }
void read_pico_bambu_auth_cancel(void) {}
bool rp_home_web_handle(const rp_home_web_request_t *request, rp_home_web_response_t *response, char *out, size_t capacity) {
    dispatches++; assert(capacity == SETUP_OUTPUT_MAX);
    snprintf(method_seen, sizeof(method_seen), "%s", request->method);
    snprintf(path_seen, sizeof(path_seen), "%s", request->path);
    snprintf(query_seen, sizeof(query_seen), "%s", request->query);
    assert(request->body_bytes <= SETUP_BODY_MAX && request->body[request->body_bytes] == 0);
    memcpy(body_seen, request->body, request->body_bytes + 1);
    online_seen = request->network_online; home_seen = request->at_home; saved_seen = request->saved_wifi_count;
    assert(!strcmp(request->current_wifi, test_net.ssid));
    for (size_t i = 0; i < request->saved_wifi_count; i++) assert(!strcmp(request->saved_wifi[i], test_net.saved[i].ssid));
    const char *payload = "{\"ok\":true}";
    strcpy(out, payload); response->body_bytes = strlen(payload); response->http_status = response_code;
    return !unknown_path;
}
bool httpd_uri_match_wildcard(const char *pattern, const char *uri, size_t length) {
    (void)length; size_t size = strlen(pattern);
    return size && pattern[size - 1] == '*' ? !strncmp(pattern, uri, size - 1) : !strcmp(pattern, uri);
}
esp_err_t httpd_start(httpd_handle_t *handle, const httpd_config_t *config) {
    starts++; assert(locks == 1);
    assert(config->stack_size == 8192 && config->max_uri_handlers == 6 && config->lru_purge_enable);
    assert(config->recv_wait_timeout == 5 && config->send_wait_timeout == 5 && config->uri_match_fn == httpd_uri_match_wildcard);
    registrations = 0; if (start_fail) return ESP_FAIL;
    *handle = (void *)(uintptr_t)1; return ESP_OK;
}
esp_err_t httpd_stop(httpd_handle_t handle) {
    assert(handle && locks == 1 && !m_status.active && !*m_status.url && !*m_status.host && !m_expires);
    assert(zeroed(m_key, sizeof(m_key))); stops++; return ESP_OK;
}
esp_err_t httpd_register_uri_handler(httpd_handle_t handle, const httpd_uri_t *route) {
    assert(handle && registrations < 6);
    if (registration_fail == registrations) return ESP_FAIL;
    registered[registrations++] = *route; return ESP_OK;
}
static const char *header(httpd_req_t *r, const char *name) {
    if(r->headers_purged)return NULL;
    if (!strcmp(name, "Host")) return r->host;
    if (!strcmp(name, "Origin")) return r->origin;
    if (!strcmp(name, "X-Pico-Setup-Key")) return r->key;
    if (!strcmp(name, "Content-Type")) return r->content_type;
    return NULL;
}
int read_pico_content_handle(const read_pico_content_request_t *r,char *out,size_t capacity){
    content_calls++;assert(r->authorized(r->context));assert(capacity==SETUP_OUTPUT_MAX);
    assert(!strcmp(r->endpoint,"/api/manage/pico/files")||!strcmp(r->endpoint,"/api/manage/pico/file"));
    if(strstr(r->query,"download=1")){
        assert(r->download(r->context,"fixture",7));
        if(expire_on_stream)test_us=m_expires*1000;
        if(revoke_on_stream)++m_session;
        if(!r->authorized(r->context))return 401;
        assert(r->download(r->context,"-tail",5)&&r->download(r->context,NULL,0));return 200;
    }
    if(r->bytes){char block[8];assert(r->receive(r->context,block,sizeof(block))>0);if(!r->authorized(r->context)){strcpy(out,"{\"ok\":false}");return 401;}}
    strcpy(out,"{\"ok\":true}");return 200;
}
void read_pico_content_cancel(void){content_cancels++;}
size_t httpd_req_get_hdr_value_len(httpd_req_t *r, const char *name) { const char *value = header(r, name); return value ? strlen(value) : 0; }
esp_err_t httpd_req_get_hdr_value_str(httpd_req_t *r, const char *name, char *out, size_t capacity) {
    const char *value = header(r, name);
    if (!value || strlen(value) >= capacity) return ESP_FAIL;
    strcpy(out, value); return ESP_OK;
}
size_t httpd_req_get_url_query_len(httpd_req_t *r) { const char *q = strchr(r->uri, '?'); return q ? strlen(q + 1) : 0; }
esp_err_t httpd_req_get_url_query_str(httpd_req_t *r, char *out, size_t capacity) {
    const char *q = strchr(r->uri, '?');
    if (!q || strlen(q + 1) >= capacity) return ESP_FAIL;
    strcpy(out, q + 1); return ESP_OK;
}
int httpd_req_recv(httpd_req_t *r, char *out, size_t capacity) {
    if (expire_on_recv) test_us = m_expires * 1000;
    size_t input_bytes=r->input_bytes?r->input_bytes:r->input?strlen(r->input):0;
    if (r->recv_fail || !r->input || r->received >= input_bytes) return -1;
    size_t left = input_bytes - r->received, bytes = capacity < left ? capacity : left;
    if (r->chunk && bytes > r->chunk) bytes = r->chunk;
    memcpy(out, r->input + r->received, bytes); r->received += bytes; return (int)bytes;
}
esp_err_t httpd_resp_set_hdr(httpd_req_t *r, const char *name, const char *value) {
    if (!strcmp(name, "Cache-Control")) { assert(!strcmp(value, "no-store")); r->security_headers |= 1; }
    else if (!strcmp(name, "X-Content-Type-Options")) { assert(!strcmp(value, "nosniff")); r->security_headers |= 2; }
    else if (!strcmp(name, "Referrer-Policy")) { assert(!strcmp(value, "no-referrer")); r->security_headers |= 4; }
    else if (!strcmp(name, "Content-Security-Policy")) { assert(strstr(value, "connect-src 'self'") && strstr(value, "frame-ancestors 'none'")); r->security_headers |= 8; }
    else if (!strcmp(name, "Content-Encoding")) { assert(!strcmp(value, "gzip")); r->gzip = true; }
    else assert(!"unexpected header");
    return ESP_OK;
}
esp_err_t httpd_resp_set_status(httpd_req_t *r, const char *value) { snprintf(r->status, sizeof(r->status), "%s", value); return ESP_OK; }
esp_err_t httpd_resp_set_type(httpd_req_t *r, const char *value) { snprintf(r->type, sizeof(r->type), "%s", value); return ESP_OK; }
esp_err_t httpd_resp_send(httpd_req_t *r, const char *body, ssize_t bytes) {
    assert(bytes >= 0 && (size_t)bytes < sizeof(r->output)); r->sends++;
    memcpy(r->output, body, (size_t)bytes); r->output[bytes] = 0; r->output_bytes = (size_t)bytes;
    assert(r->security_headers == 15);r->headers_purged=true;return fail_on_send?ESP_FAIL:ESP_OK;
}
esp_err_t httpd_resp_send_chunk(httpd_req_t *r,const char *body,ssize_t bytes){
    assert(bytes>=0&&r->output_bytes+(size_t)bytes<sizeof(r->output));if(bytes)memcpy(r->output+r->output_bytes,body,(size_t)bytes);r->output_bytes+=(size_t)bytes;r->output[r->output_bytes]=0;r->sends++;r->headers_purged=true;return ESP_OK;
}
// 避免每次按值返回420KB输出缓冲区累积未优化栈临时值。/ Avoid repeated 420KB return-value temporaries in unoptimized test stacks.
static const httpd_req_t *valid_request(void) {
    static httpd_req_t request;
    memset(&request,0,sizeof(request));
    request.uri="/api/manage/config?brand=bambu";request.host="192.0.2.4";request.origin="http://192.0.2.4";
    request.key=m_key;request.method=HTTP_GET;request.content_type="application/json";request.input="";
    return &request;
}
static void api_expect(httpd_req_t *r, int status, int dispatched) {
    int before = dispatches; expected_body_size = r->content_len + 1;
    assert(m_api(r) == ESP_OK); expected_body_size = 0;
    assert(atoi(r->status) == status && r->sends == 1 && r->security_headers == 15);
    assert(!strcmp(r->type, "application/json; charset=utf-8"));
    assert(dispatches == before + dispatched && !live_allocations);
    assert(!strstr(r->output, "private-fixture-ha-token"));
}
static void begin(void) {
    test_net = (read_pico_connectivity_status_t){.connected=true,.ready=true,.wifi_enabled=true,.ip="192.0.2.4",.ssid="Home",.saved_count=2};
    strcpy(test_net.saved[0].ssid, "Home"); strcpy(test_net.saved[1].ssid, "Office");
    test_config = (rp_home_config_t){.version=RP_HOME_CONFIG_VERSION,.home_wifi="Home",.ha_token="private-fixture-ha-token"};
    assert(read_pico_management_start()); assert(!locks && !live_allocations && registrations == 6);
    read_pico_management_status_t status; read_pico_management_get_status(&status);
    assert(status.active && status.seconds_left == 900 && !strcmp(status.host, "192.0.2.4"));
    assert(strstr(status.url, "/manage#key=") && strlen(m_key) == 32 && !strstr(status.url, test_config.ha_token));
}
static void lifecycle_contract(void) {
    mutex_fail = true; assert(!read_pico_management_start() && !m_lifecycle); mutex_fail = false;
    begin(); char old_key[33]; strcpy(old_key, m_key);
    read_pico_management_stop(); assert(!m_status.active && !*m_key && !m_http && !*m_status.url);
    begin(); assert(strcmp(old_key, m_key));
    httpd_req_t r = *valid_request(); r.key = old_key; api_expect(&r, 401, 0);
    for (int failure = 0; failure < 5; failure++) {
        begin();
        if (failure == 0) test_net.connected = false;
        if (failure == 1) test_net.suspended = true;
        if (failure == 2) test_net.busy = true;
        if (failure == 3) test_net.transfer_active = true;
        if (failure == 4) *test_net.ip = 0;
        int count = starts; assert(!read_pico_management_start() && starts == count && !m_http && !*m_key && *m_status.error);
    }
    begin(); allocation_fail = true; assert(!read_pico_management_start()); allocation_fail = false;
    assert(!m_http && !*m_key && *m_status.error);
    begin(); start_fail = true; assert(!read_pico_management_start()); start_fail = false;
    assert(!m_http && !*m_key && *m_status.error);
    begin(); registration_fail = 3; assert(!read_pico_management_start()); registration_fail = -1;
    assert(!m_http && !*m_key && *m_status.error);
    for (int condition = 0; condition < 6; condition++) {
        begin();
        if (condition == 0) test_net.connected = false;
        if (condition == 1) test_net.suspended = true;
        if (condition == 2) test_net.transfer_active = true;
        if (condition == 3) strcpy(test_net.ip, "192.0.2.5");
        if (condition == 4) test_us = m_expires * 1000;
        if (condition == 5) allocation_fail = true;
        read_pico_management_tick(); allocation_fail = false;
        assert(!m_status.active && !m_http && !*m_key);
    }
    begin(); int sampled = network_reads;
    for (int i = 0; i < 200; i++) read_pico_management_tick();
    assert(m_status.active && network_reads == sampled + 1);
    test_us += 1000000; read_pico_management_tick();
    assert(m_status.active && network_reads == sampled + 2);
    test_us = m_expires * 1000; read_pico_management_tick();
    assert(!m_status.active && !m_http && !*m_key);
}
static void authentication_contract(void) {
    begin(); httpd_req_t r = *valid_request(); api_expect(&r, 200, 1);
    assert(online_seen && home_seen && saved_seen == 2 && !strcmp(path_seen, "/api/manage/config") && !strcmp(query_seen, "brand=bambu"));
    const char *bad_hosts[] = {NULL,"","attacker.invalid","192.0.2.4.attacker.invalid","192.0.2.4:8080","user@192.0.2.4"};
    for (size_t i = 0; i < sizeof(bad_hosts)/sizeof(*bad_hosts); i++) { r = *valid_request(); r.host = bad_hosts[i]; api_expect(&r, 403, 0); }
    const char *bad_origins[] = {"null","https://192.0.2.4","http://attacker.invalid","http://192.0.2.4.attacker.invalid","http://192.0.2.4/"};
    for (size_t i = 0; i < sizeof(bad_origins)/sizeof(*bad_origins); i++) { r = *valid_request(); r.origin = bad_origins[i]; api_expect(&r, 403, 0); }
    char overlong[200]; memset(overlong, 'x', sizeof(overlong)-1); overlong[sizeof(overlong)-1] = 0;
    r = *valid_request(); r.host = overlong; api_expect(&r, 403, 0);
    r = *valid_request(); r.origin = overlong; api_expect(&r, 403, 0);
    r = *valid_request(); r.host = "192.0.2.4:80"; r.origin = "http://192.0.2.4:80"; api_expect(&r, 200, 1);
    r = *valid_request(); r.origin = NULL; api_expect(&r, 200, 1);
    const char *bad_keys[] = {NULL,"","short","00000000000000000000000000000000","000000000000000000000000000000000"};
    for (size_t i = 0; i < sizeof(bad_keys)/sizeof(*bad_keys); i++) { r = *valid_request(); r.key = bad_keys[i]; api_expect(&r, 401, 0); }
    int64_t previous_expiry=m_expires;test_us+=1000000;
    r=*valid_request();r.key="00000000000000000000000000000000";api_expect(&r,401,0);assert(m_expires==previous_expiry);
    r=*valid_request();response_code=503;api_expect(&r,503,1);response_code=200;assert(m_expires==previous_expiry);
    r=*valid_request();r.uri="/manage";assert(m_page(&r)==ESP_OK&&m_expires==previous_expiry);
    test_us = m_expires * 1000 - 1; r = *valid_request(); api_expect(&r, 200, 1);
    assert(m_expires>previous_expiry);
    test_us=m_expires*1000; r = *valid_request(); api_expect(&r, 401, 0);
    read_pico_management_tick(); assert(!*m_key);
    begin(); r = *valid_request(); r.method = HTTP_PUT; r.content_len = 2; r.input = "{}"; r.chunk = 1;
    expire_on_recv = true; api_expect(&r, 401, 0); expire_on_recv = false;
    begin(); r = *valid_request(); r.uri = "/manage"; assert(m_page(&r) == ESP_OK);
    assert(r.gzip && r.output_bytes == (size_t)(management_html_end - management_html_start) &&
           !memcmp(r.output, management_html_start, r.output_bytes) && !strcmp(r.type, "text/html; charset=utf-8"));
    r = *valid_request(); r.host = "attacker.invalid"; assert(m_page(&r) == ESP_OK && atoi(r.status) == 403);
}
static void response_lifecycle_contract(void){
    begin();test_us+=1000000;int64_t expires=m_expires;httpd_req_t r=*valid_request();
    fail_on_send=true;assert(m_api(&r)==ESP_FAIL&&m_expires==expires&&r.headers_purged);fail_on_send=false;
    r=*valid_request();r.uri="/api/manage/pico/file?download=1";
    assert(m_api(&r)==ESP_OK&&m_expires>expires&&r.headers_purged&&!strcmp(r.output,"fixture-tail")&&r.sends==3);
    for(unsigned condition=0;condition<2;condition++){
        begin();test_us+=1000000;expires=m_expires;r=*valid_request();r.uri="/api/manage/pico/file?download=1";
        expire_on_stream=condition==0;revoke_on_stream=condition==1;
        assert(m_api(&r)==ESP_FAIL&&m_expires==expires&&!strcmp(r.output,"fixture")&&r.sends==1);
        expire_on_stream=revoke_on_stream=false;
    }
    begin();uint64_t previous=m_authorization_ticket(&(httpd_req_t){.key=m_key});assert(previous&&m_ticket_live(previous,false));
    read_pico_management_stop();begin();expires=m_expires;test_us+=1000000;assert(!m_ticket_live(previous,true)&&m_expires==expires);
}
static void body_and_dispatch_contract(void) {
    begin(); httpd_req_t r;
    for (int method = HTTP_GET; method <= HTTP_DELETE; method++) {
        r = *valid_request(); r.method = (httpd_method_t)method; r.content_len = 19; r.input = "{\"token\":\"fixture\"}"; r.chunk = 3;
        api_expect(&r, 200, 1);
        const char *expected[] = {"GET","PUT","POST","DELETE"};
        assert(!strcmp(method_seen, expected[method]) && !strcmp(body_seen, r.input));
    }
    bool registered_method[HTTP_OPTIONS+1] = {0};
    for (int i = 0; i < registrations; i++) if (!strcmp(registered[i].uri,"/api/manage/*")) registered_method[registered[i].method] = true;
    for (int i = HTTP_GET; i <= HTTP_DELETE; i++) assert(registered_method[i]);
    assert(!registered_method[HTTP_PATCH] && !registered_method[HTTP_HEAD] && !registered_method[HTTP_OPTIONS]);
    r = *valid_request(); r.content_len = SETUP_BODY_MAX + 1; api_expect(&r, 413, 0);
    const char *types[] = {NULL,"text/plain","application/x-www-form-urlencoded","application/json; charset=latin1"};
    for (size_t i = 0; i < sizeof(types)/sizeof(*types); i++) { r = *valid_request(); r.content_len = 2; r.input = "{}"; r.content_type = types[i]; api_expect(&r, 415, 0); }
    r = *valid_request(); r.content_len = 2; r.input = "{}"; r.content_type = "application/json; charset=utf-8"; api_expect(&r, 200, 1);
    r = *valid_request(); r.content_len = 2; r.input = "{"; api_expect(&r, 400, 0);
    r = *valid_request(); r.content_len = 2; r.input = "{}"; r.recv_fail = true; api_expect(&r, 400, 0);
    char max_body[SETUP_BODY_MAX + 1]; memset(max_body, 'x', SETUP_BODY_MAX); max_body[SETUP_BODY_MAX] = 0;
    r = *valid_request(); r.content_len = SETUP_BODY_MAX; r.input = max_body; r.chunk = 997; api_expect(&r, 200, 1);
    assert(strlen(body_seen) == SETUP_BODY_MAX);
    char uri[900]; strcpy(uri, "/api/manage/config?"); size_t prefix = strlen(uri);
    memset(uri + prefix, 'q', RP_HOME_WEB_QUERY_LIMIT); uri[prefix + RP_HOME_WEB_QUERY_LIMIT] = 0;
    r = *valid_request(); r.uri = uri; api_expect(&r, 200, 1); assert(strlen(query_seen) == RP_HOME_WEB_QUERY_LIMIT);
    uri[prefix + RP_HOME_WEB_QUERY_LIMIT] = 'q'; uri[prefix + RP_HOME_WEB_QUERY_LIMIT + 1] = 0;
    r = *valid_request(); r.uri = uri; api_expect(&r, 400, 0);
    memset(uri, 'a', 96); uri[0] = '/'; uri[96] = 0;
    r = *valid_request(); r.uri = uri; api_expect(&r, 400, 0);
    r = *valid_request(); unknown_path = true; api_expect(&r, 404, 1); unknown_path = false;
    r = *valid_request(); allocation_fail = true; api_expect(&r, 503, 0); allocation_fail = false;
    const int codes[] = {200,202,400,401,403,404,405,409,413,415,422,500,503,507};
    for (size_t i = 0; i < sizeof(codes)/sizeof(*codes); i++) { response_code = codes[i]; r = *valid_request(); api_expect(&r, codes[i], 1); }
    response_code = 200;
    strcpy(test_net.ssid, "Office"); test_net.saved_count = READ_PICO_CONNECTIVITY_SAVED_MAX + 5;
    r = *valid_request(); api_expect(&r, 200, 1); assert(!home_seen && saved_seen == READ_PICO_CONNECTIVITY_SAVED_MAX);
}
static int desktop_extension(const char *method,const char *path,const char *type,const void *body,size_t bytes,char *out,size_t capacity){
    extension_calls++;assert(bytes==extension_bytes&&(!bytes||!memcmp(body,extension_body,bytes)));assert(capacity==SETUP_OUTPUT_MAX);
    if(!strcmp(path,"/api/manage/pico/canvas/model")||!strcmp(path,"/api/manage/pico/canvas/model/show")||!strcmp(path,"/api/manage/pico/apps/cloudflare/model")||!strcmp(path,"/api/manage/pico/apps/cloudflare/model/show")){assert(!strcmp(method,"POST")&&!strcmp(type,"application/vnd.readpico.board-model+json"));strcpy(out,"{\"ok\":true,\"accepted\":true}");return 202;}
    if(!strcmp(path,"/api/manage/pico/canvas/pair")||!strcmp(path,"/api/manage/pico/canvas/pair/show")||!strcmp(path,"/api/manage/pico/apps/cloudflare/pair")||!strcmp(path,"/api/manage/pico/apps/cloudflare/pair/show")){assert(!strcmp(method,"POST")&&!strcmp(type,"application/vnd.readpico.canvas-pair"));strcpy(out,"{\"ok\":true,\"accepted\":true}");return 202;}
    if(!strcmp(path,"/api/manage/pico/canvas")||!strcmp(path,"/api/manage/pico/canvas/show")||!strcmp(path,"/api/manage/pico/lock-image")){assert(!strcmp(method,"POST")&&!strcmp(type,"image/bmp"));strcpy(out,"{\"ok\":true,\"accepted\":true}");return 202;}
    assert(!strcmp(path,"/api/manage/pico/status")||!strcmp(path,"/api/manage/pico/config"));strcpy(out,"{\"protocol\":1}");return 200;
}
static void desktop_extension_contract(void){
    read_pico_management_set_extension(desktop_extension);begin();httpd_req_t r=*valid_request();r.uri="/api/manage/pico/status";extension_bytes=0;int before=extension_calls;api_expect(&r,200,0);assert(extension_calls==before+1);
    unsigned char *binary=malloc(840032);assert(binary);for(size_t i=0;i<840032;i++)binary[i]=(unsigned char)i;
    const size_t sizes[]={1,62,107070,110000,415990,418422,420000};
    for(size_t i=0;i<sizeof(sizes)/sizeof(*sizes);i++){
        r=*valid_request();r.uri="/api/manage/pico/canvas";r.method=HTTP_POST;r.content_type="image/bmp";r.input=(const char*)binary;r.input_bytes=r.content_len=sizes[i];r.chunk=997;
        extension_body=binary;extension_bytes=sizes[i];before=extension_calls;api_expect(&r,202,0);assert(extension_calls==before+1&&strstr(r.output,"\"accepted\":true")&&!strstr(r.output,"displayed"));
    }
    r=*valid_request();r.uri="/api/manage/pico/canvas/show";r.method=HTTP_POST;r.content_type="image/bmp";r.input=(const char*)binary;r.input_bytes=r.content_len=418422;r.chunk=997;
    extension_body=binary;extension_bytes=418422;before=extension_calls;api_expect(&r,202,0);assert(extension_calls==before+1&&strstr(r.output,"\"accepted\":true")&&!strstr(r.output,"displayed"));
    r=*valid_request();r.uri="/api/manage/pico/canvas";r.method=HTTP_POST;r.content_type="image/bmp";r.input=(const char*)binary;r.input_bytes=r.content_len=62;r.chunk=7;
    before=extension_calls;r.key="00000000000000000000000000000000";api_expect(&r,401,0);assert(extension_calls==before);
    r=*valid_request();r.uri="/api/manage/pico/canvas";r.method=HTTP_POST;r.content_type="image/bmp";r.input=(const char*)binary;r.input_bytes=r.content_len=62;r.chunk=7;
    test_us=m_expires*1000-1;expire_on_recv=true;api_expect(&r,401,0);expire_on_recv=false;assert(extension_calls==before);
    begin();r=*valid_request();r.uri="/api/manage/pico/canvas";r.method=HTTP_POST;r.content_type="image/bmp";r.input=(const char*)binary;r.input_bytes=r.content_len=420001;api_expect(&r,413,0);assert(extension_calls==before);
    r=*valid_request();r.uri="/api/manage/pico/canvas";r.method=HTTP_POST;r.content_type="text/plain";r.content_len=1;r.input="x";api_expect(&r,415,0);assert(extension_calls==before);
    r=*valid_request();r.uri="/api/manage/pico/canvas?ignored=1";r.method=HTTP_POST;r.content_type="image/bmp";r.content_len=1;r.input="x";api_expect(&r,400,0);assert(extension_calls==before);
    r=*valid_request();r.uri="/api/manage/pico/config";r.method=HTTP_PUT;r.content_type="text/plain";r.content_len=2;r.input="{}";api_expect(&r,415,0);assert(extension_calls==before);
    for(size_t i=0;i<sizeof(sizes)/sizeof(*sizes);i++){
        r=*valid_request();r.uri="/api/manage/pico/lock-image";r.method=HTTP_POST;r.content_type="image/bmp";r.input=(const char*)binary;r.input_bytes=r.content_len=sizes[i];r.chunk=997;
        extension_body=binary;extension_bytes=sizes[i];before=extension_calls;api_expect(&r,202,0);assert(extension_calls==before+1);
    }
    r=*valid_request();r.uri="/api/manage/pico/lock-image";r.method=HTTP_POST;r.content_type="text/plain";r.content_len=1;r.input="x";before=extension_calls;api_expect(&r,415,0);assert(extension_calls==before);
    r=*valid_request();r.uri="/api/manage/pico/lock-image";r.method=HTTP_POST;r.content_type="image/bmp";r.input=(const char*)binary;r.input_bytes=r.content_len=420001;before=extension_calls;api_expect(&r,413,0);assert(extension_calls==before);
    // 大幅16灰画布也须在逐块接收和提交前重新鉴权，不能借新上限跨越到期。
    // Large 16-gray uploads still reauthorize during chunks and before commit; the larger bound cannot bypass expiry.
    r=*valid_request();r.uri="/api/manage/pico/canvas";r.method=HTTP_POST;r.content_type="image/bmp";r.input=(const char*)binary;r.input_bytes=r.content_len=418422;r.chunk=997;before=extension_calls;test_us=m_expires*1000-1;expire_on_recv=true;api_expect(&r,401,0);expire_on_recv=false;assert(extension_calls==before);
    begin();r=*valid_request();r.uri="/api/manage/pico/lock-image";r.method=HTTP_POST;r.content_type="image/bmp";r.input=(const char*)binary;r.input_bytes=r.content_len=418422;r.chunk=997;before=extension_calls;
    r.key="00000000000000000000000000000000";api_expect(&r,401,0);assert(extension_calls==before);
    r=*valid_request();r.uri="/api/manage/pico/lock-image";r.method=HTTP_POST;r.content_type="image/bmp";r.input=(const char*)binary;r.input_bytes=r.content_len=418422;r.chunk=997;before=extension_calls;test_us=m_expires*1000-1;expire_on_recv=true;api_expect(&r,401,0);expire_on_recv=false;assert(extension_calls==before);
    begin();
    for(unsigned show=0;show<4;show++){
        const char *paths[]={"/api/manage/pico/canvas/pair","/api/manage/pico/canvas/pair/show","/api/manage/pico/apps/cloudflare/pair","/api/manage/pico/apps/cloudflare/pair/show"};
        r=*valid_request();r.uri=paths[show];r.method=HTTP_POST;r.content_type="application/vnd.readpico.canvas-pair";r.input=(const char*)binary;r.input_bytes=r.content_len=840032;r.chunk=997;
        extension_body=binary;extension_bytes=840032;before=extension_calls;api_expect(&r,202,0);assert(extension_calls==before+1);
    }
    r=*valid_request();r.uri="/api/manage/pico/canvas/pair";r.method=HTTP_POST;r.content_type="application/vnd.readpico.canvas-pair";r.content_len=840033;before=extension_calls;api_expect(&r,413,0);assert(extension_calls==before);
    r=*valid_request();r.uri="/api/manage/pico/canvas/pair";r.method=HTTP_POST;r.content_type="image/bmp";r.content_len=1;r.input="x";api_expect(&r,415,0);assert(extension_calls==before);
    r=*valid_request();r.uri="/api/manage/pico/canvas/pair";r.method=HTTP_POST;r.content_type="application/vnd.readpico.canvas-pair";r.input=(const char*)binary;r.input_bytes=r.content_len=840032;r.chunk=997;test_us=m_expires*1000-1;expire_on_recv=true;api_expect(&r,401,0);expire_on_recv=false;assert(extension_calls==before);
    begin();
    for(unsigned mode=0;mode<4;mode++){
        const char *paths[]={"/api/manage/pico/canvas/model","/api/manage/pico/canvas/model/show","/api/manage/pico/apps/cloudflare/model","/api/manage/pico/apps/cloudflare/model/show"};
        r=*valid_request();r.uri=paths[mode];r.method=HTTP_POST;r.content_type="application/vnd.readpico.board-model+json";r.input=(const char*)binary;r.input_bytes=r.content_len=65536;r.chunk=997;
        extension_body=binary;extension_bytes=65536;before=extension_calls;api_expect(&r,202,0);assert(extension_calls==before+1);
    }
    before=extension_calls;
    r=*valid_request();r.uri="/api/manage/pico/canvas/model";r.method=HTTP_POST;r.content_type="application/vnd.readpico.board-model+json";r.content_len=65537;api_expect(&r,413,0);assert(extension_calls==before);
    r=*valid_request();r.uri="/api/manage/pico/canvas/model";r.method=HTTP_POST;r.content_type="application/json";r.content_len=2;r.input="{}";api_expect(&r,415,0);assert(extension_calls==before);
    r=*valid_request();r.uri="/api/manage/pico/canvas/model";r.method=HTTP_POST;r.content_type="application/vnd.readpico.board-model+json";r.input=(const char*)binary;r.input_bytes=r.content_len=65536;r.chunk=997;test_us=m_expires*1000-1;expire_on_recv=true;api_expect(&r,401,0);expire_on_recv=false;assert(extension_calls==before);
    begin();r=*valid_request();r.uri="/api/manage/pico/canvas/model";r.method=HTTP_POST;r.content_type="application/vnd.readpico.board-model+json";r.input=(const char*)binary;r.input_bytes=r.content_len=65536;r.key="00000000000000000000000000000000";api_expect(&r,401,0);assert(extension_calls==before);
    free(binary);read_pico_management_set_extension(NULL);
}
static int image_reads,image_code=200;static size_t image_size=110000;static bool expire_on_image;
static int image_extension(const char *path,uint8_t **data,size_t *bytes){
    image_reads++;assert(!strcmp(path,"/api/manage/pico/lock-image"));*data=NULL;*bytes=0;
    if(image_code==200){*bytes=image_size;*data=test_calloc(1,*bytes);memset(*data,0xab,*bytes);(*data)[0]='B';(*data)[1]='M';}
    if(expire_on_image)test_us=m_expires*1000;return image_code;
}
static void image_extension_contract(void){
    read_pico_management_set_extension(desktop_extension);read_pico_management_set_image_extension(image_extension);begin();
    httpd_req_t r=*valid_request();r.uri="/api/manage/pico/lock-image";int before=image_reads;
    assert(m_api(&r)==ESP_OK&&image_reads==before+1&&atoi(r.status)==200&&!strcmp(r.type,"image/bmp")&&r.output_bytes==110000&&!memcmp(r.output,"BM",2)&&!live_allocations);
    image_size=418422;r=*valid_request();r.uri="/api/manage/pico/lock-image";before=image_reads;
    assert(m_api(&r)==ESP_OK&&image_reads==before+1&&atoi(r.status)==200&&!strcmp(r.type,"image/bmp")&&r.output_bytes==image_size&&!memcmp(r.output,"BM",2)&&!live_allocations);
    r=*valid_request();r.uri="/api/manage/pico/lock-image";r.key="00000000000000000000000000000000";before=image_reads;api_expect(&r,401,0);assert(image_reads==before);
    r=*valid_request();r.uri="/api/manage/pico/lock-image?ignored=1";api_expect(&r,400,0);assert(image_reads==before);
    r=*valid_request();r.uri="/api/manage/pico/lock-image";r.content_len=1;r.input="x";api_expect(&r,400,0);assert(image_reads==before);
    const int errors[]={404,409,503};for(unsigned i=0;i<sizeof(errors)/sizeof(*errors);i++){image_code=errors[i];r=*valid_request();r.uri="/api/manage/pico/lock-image";api_expect(&r,image_code,0);}
    image_code=200;expire_on_image=true;r=*valid_request();r.uri="/api/manage/pico/lock-image";api_expect(&r,401,0);expire_on_image=false;
    read_pico_management_set_image_extension(NULL);read_pico_management_set_extension(NULL);
}
static int dump_page_gzip(void) {
    begin();
    static httpd_req_t r;
    r = *valid_request();
    r.uri = "/manage";
    assert(m_page(&r) == ESP_OK && r.gzip && !strcmp(r.type, "text/html; charset=utf-8"));
    assert(fwrite(r.output, 1, r.output_bytes, stdout) == r.output_bytes);
    read_pico_management_stop();
    return 0;
}
int main(int argc, char **argv) {
    if (argc == 2 && !strcmp(argv[1], "--page-gzip")) return dump_page_gzip();
    assert(argc == 1);
    lifecycle_contract(); authentication_contract();response_lifecycle_contract();body_and_dispatch_contract();desktop_extension_contract();image_extension_contract();
    read_pico_management_stop(); read_pico_management_stop();
    assert(!live_allocations && !locks && !m_http && !m_status.active && !*m_key);
    printf("management HTTP: Host/Origin/key/expiry/slow-body reauth, body/query bounds, method routes/status, private-buffer wipe, offline/ownership failures, revoke-before-stop passed; %d controller dispatches, %d starts, %d stops, %zu scrubbed bytes; no sockets/storage/transfer calls\n", dispatches, starts, stops, cleared_bytes);
    return 0;
}

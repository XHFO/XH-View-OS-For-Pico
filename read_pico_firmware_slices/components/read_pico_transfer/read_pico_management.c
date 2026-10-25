/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 * 手机同源管理HTTP适配器；与设备和宿主共用JSON控制器。
 * Same-origin phone-management HTTP adapter over the shared device/host JSON controller.
 * 冻结：仅借用已连接STA；不挂载存储、不改网络所有权、不挂起家庭服务；配对电脑扩展只写其自有TF文件。
 * Frozen: Borrow connected STA only; never mount storage, own networking or suspend home services; paired desktop extension writes owned TF files only.
 */
#include "read_pico_management.h"
#include "read_pico_content.h"
#include "read_pico_connectivity.h"
#include "read_pico_home_link.h"
#include "home_link_web.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "esp_http_server.h"
#include "esp_heap_caps.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#define SETUP_LIFETIME_MS (15 * 60 * 1000)
#define SETUP_BODY_MAX (12 * 1024)
#define SETUP_OUTPUT_MAX (32 * 1024)
static httpd_handle_t m_http;
static SemaphoreHandle_t m_lifecycle;
static portMUX_TYPE m_lock = portMUX_INITIALIZER_UNLOCKED;
static read_pico_management_status_t m_status;
static char m_key[33];
static uint64_t m_session;
static read_pico_management_extension_t m_extension;
static read_pico_management_image_extension_t m_image_extension;
void read_pico_management_set_image_extension(read_pico_management_image_extension_t handler){m_image_extension=handler;}
void read_pico_management_set_extension(read_pico_management_extension_t handler){m_extension=handler;}
static int64_t m_expires, m_next_check;
static uint64_t m_authorization_ticket(httpd_req_t *r);
static bool m_ticket_live(uint64_t ticket,bool renew);
extern const uint8_t management_html_start[] asm("_binary_manage_html_gz_start");
extern const uint8_t management_html_end[] asm("_binary_manage_html_gz_end");

static void *m_alloc(size_t size) {
    void *p = heap_caps_calloc(1, size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    return p ? p : calloc(1, size);
}
static void m_clear(void *data, size_t size) {
    volatile unsigned char *p = data;
    while (size--) *p++ = 0;
}
static void m_headers(httpd_req_t *r) {
    httpd_resp_set_hdr(r, "Cache-Control", "no-store");
    httpd_resp_set_hdr(r, "X-Content-Type-Options", "nosniff");
    httpd_resp_set_hdr(r, "Referrer-Policy", "no-referrer");
    httpd_resp_set_hdr(r, "Content-Security-Policy", "default-src 'none'; script-src 'unsafe-inline'; style-src 'unsafe-inline'; connect-src 'self'; img-src 'self' data:; base-uri 'none'; frame-ancestors 'none'; form-action 'self'");
}
static const char *m_status_text(int status) {
    switch (status) {
        case 200: return "200 OK"; case 202: return "202 Accepted";
        case 400: return "400 Bad Request"; case 401: return "401 Unauthorized";
        case 403: return "403 Forbidden"; case 404: return "404 Not Found";
        case 405: return "405 Method Not Allowed"; case 507: return "507 Insufficient Storage";
        case 409: return "409 Conflict"; case 413: return "413 Payload Too Large";
        case 415: return "415 Unsupported Media Type"; case 422: return "422 Unprocessable Entity";
        case 503: return "503 Service Unavailable"; default: return "500 Internal Server Error";
    }
}
static esp_err_t m_reply(httpd_req_t *r, int status, const char *body, size_t bytes) {
    uint64_t ticket=status>=200&&status<300?m_authorization_ticket(r):0;
    m_headers(r); httpd_resp_set_status(r, m_status_text(status));
    httpd_resp_set_type(r, "application/json; charset=utf-8");
    esp_err_t result=httpd_resp_send(r, body, (ssize_t)bytes);
    if(result==ESP_OK)(void)m_ticket_live(ticket,true);
    return result;
}
static esp_err_t m_error(httpd_req_t *r, int code) {
    const char *body = code == 401 ? "{\"ok\":false,\"error\":\"setup_expired\"}" : "{\"ok\":false,\"error\":\"request_rejected\"}";
    return m_reply(r, code, body, strlen(body));
}
static bool m_origin(httpd_req_t *r) {
    char host[64] = {0}, origin[80] = {0}, expected[64], origin_expected[80];
    portENTER_CRITICAL(&m_lock); snprintf(expected, sizeof(expected), "%s", m_status.host); portEXIT_CRITICAL(&m_lock);
    size_t len = httpd_req_get_hdr_value_len(r, "Host");
    if (!*expected || !len || len >= sizeof(host) || httpd_req_get_hdr_value_str(r, "Host", host, sizeof(host)) != ESP_OK) return false;
    char port80[72]; snprintf(port80, sizeof(port80), "%s:80", expected);
    if (strcmp(host, expected) && strcmp(host, port80)) return false;
    len = httpd_req_get_hdr_value_len(r, "Origin");
    if (len) {
        if (len >= sizeof(origin) || httpd_req_get_hdr_value_str(r, "Origin", origin, sizeof(origin)) != ESP_OK) return false;
        snprintf(origin_expected, sizeof(origin_expected), "http://%s", host);
        if (strcmp(origin, origin_expected)) return false;
    }
    return true;
}
static uint64_t m_authorization_ticket(httpd_req_t *r) {
    char key[33] = {0};
    if (httpd_req_get_hdr_value_len(r, "X-Pico-Setup-Key") != 32 ||
        httpd_req_get_hdr_value_str(r, "X-Pico-Setup-Key", key, sizeof(key)) != ESP_OK) return false;
    unsigned diff = 0;
    portENTER_CRITICAL(&m_lock);
    bool live = m_status.active && esp_timer_get_time() / 1000 < m_expires;
    for (size_t i = 0; i < 32; ++i) diff |= (unsigned char)key[i] ^ (unsigned char)m_key[i];
    uint64_t ticket=live&&!diff?m_session:0;
    portEXIT_CRITICAL(&m_lock);
    m_clear(key, sizeof(key)); return ticket;
}
// ESP HTTP响应会清除请求头；只保留不含凭据的会话代号，撤销或过期后立即失效。
// ESP HTTP responses purge request headers; retain only a credential-free ticket invalidated by revocation or expiry.
static bool m_ticket_live(uint64_t ticket,bool renew){
    portENTER_CRITICAL(&m_lock);
    bool live=ticket&&ticket==m_session&&m_status.active&&esp_timer_get_time()/1000<m_expires;
    if(live&&renew)m_expires=esp_timer_get_time()/1000+SETUP_LIFETIME_MS;
    portEXIT_CRITICAL(&m_lock);return live;
}
static bool m_authorized(httpd_req_t *r){return m_authorization_ticket(r)!=0;}
static esp_err_t m_page(httpd_req_t *r) {
    if (!m_origin(r)) return m_error(r, 403);
    m_headers(r); httpd_resp_set_type(r, "text/html; charset=utf-8");
    httpd_resp_set_hdr(r, "Content-Encoding", "gzip");
    return httpd_resp_send(r, (const char *)management_html_start, management_html_end - management_html_start);
}
typedef struct { httpd_req_t *request; uint64_t ticket; bool streaming; size_t received; } m_content_context_t;
static bool m_content_authorized(void *p){return m_ticket_live(((m_content_context_t*)p)->ticket,false);}
static int m_content_receive(void *p,void *data,size_t n){m_content_context_t *c=p;int got=httpd_req_recv(c->request,data,n);if(got>0)c->received+=(size_t)got;return got;}
static esp_err_t m_content_error(httpd_req_t *r,int code){esp_err_t result=m_error(r,code);return r->content_len?ESP_FAIL:result;}
static bool m_content_download(void *p,const void *data,size_t n){
    m_content_context_t *context=p;httpd_req_t *r=context->request;
    if(!context->streaming){m_headers(r);httpd_resp_set_status(r,"200 OK");httpd_resp_set_type(r,"application/octet-stream");context->streaming=true;}
    return httpd_resp_send_chunk(r,data,(ssize_t)n)==ESP_OK;
}
static esp_err_t m_content(httpd_req_t *r,const char *path){
    char query[769]={0},sha[65]={0},*out=m_alloc(SETUP_OUTPUT_MAX);
    if(!out)return m_content_error(r,503);
    if(httpd_req_get_url_query_len(r)>=sizeof(query)||(httpd_req_get_url_query_len(r)&&httpd_req_get_url_query_str(r,query,sizeof(query))!=ESP_OK)){free(out);return m_content_error(r,400);}
    if(r->method==HTTP_PUT){
        char type[80]={0};
        if(httpd_req_get_hdr_value_str(r,"Content-Type",type,sizeof(type))!=ESP_OK||strcmp(type,"application/octet-stream")){free(out);return m_content_error(r,415);}
        if(httpd_req_get_hdr_value_len(r,"X-Pico-Content-SHA256")!=64||httpd_req_get_hdr_value_str(r,"X-Pico-Content-SHA256",sha,sizeof(sha))!=ESP_OK){free(out);return m_content_error(r,400);}
    }
    m_content_context_t context={.request=r,.ticket=m_authorization_ticket(r)};
    read_pico_content_request_t request={.method=r->method==HTTP_GET?"GET":r->method==HTTP_PUT?"PUT":r->method==HTTP_DELETE?"DELETE":"POST",.endpoint=path,.query=query,.sha256=sha,.bytes=r->content_len,.context=&context,.authorized=m_content_authorized,.receive=m_content_receive,.download=m_content_download};
    int status=read_pico_content_handle(&request,out,SETUP_OUTPUT_MAX);
    esp_err_t result=context.streaming?(status==200?ESP_OK:ESP_FAIL):m_reply(r,status,out,strlen(out));
    if(context.streaming&&result==ESP_OK)(void)m_ticket_live(context.ticket,true);
    free(out);return context.received<r->content_len?ESP_FAIL:result;
}
static esp_err_t m_extended(httpd_req_t *r){
    char path[96],type[80]={0};size_t n=strcspn(r->uri,"?");
    if(n>=sizeof(path))return m_error(r,400);
    memcpy(path,r->uri,n);path[n]=0;
    if(!strcmp(path,"/api/manage/pico/files")||!strcmp(path,"/api/manage/pico/file"))return m_content(r,path);
    if(strchr(r->uri,'?'))return m_error(r,400);
    if(!m_extension)return m_error(r,404);
    if(!strcmp(path,"/api/manage/pico/lock-image")&&r->method==HTTP_GET){
        if(r->content_len)return m_error(r,400);
        if(!m_image_extension)return m_error(r,404);
        uint8_t *image=NULL;size_t bytes=0;int status=m_image_extension(path,&image,&bytes);
        if(!m_authorized(r)){free(image);return m_error(r,401);}
        if(status!=200){free(image);return m_error(r,status);}
        uint64_t ticket=m_authorization_ticket(r);
        m_headers(r);httpd_resp_set_status(r,m_status_text(status));httpd_resp_set_type(r,"image/bmp");
        esp_err_t result=httpd_resp_send(r,(const char*)image,(ssize_t)bytes);if(result==ESP_OK)(void)m_ticket_live(ticket,true);free(image);return result;
    }
    bool pair=(!strcmp(path,"/api/manage/pico/canvas/pair")||!strcmp(path,"/api/manage/pico/canvas/pair/show")||!strcmp(path,"/api/manage/pico/apps/cloudflare/pair")||!strcmp(path,"/api/manage/pico/apps/cloudflare/pair/show"))&&r->method==HTTP_POST;
    bool canvas=(pair||!strcmp(path,"/api/manage/pico/canvas")||!strcmp(path,"/api/manage/pico/canvas/show")||!strcmp(path,"/api/manage/pico/lock-image"))&&r->method==HTTP_POST;
    size_t limit=pair?840032u:canvas?420000u:SETUP_BODY_MAX;
    if(r->content_len>limit)return m_error(r,413);
    if(r->content_len&&httpd_req_get_hdr_value_str(r,"Content-Type",type,sizeof(type))!=ESP_OK)return m_error(r,415);
    if(canvas&&strcmp(type,pair?"application/vnd.readpico.canvas-pair":"image/bmp"))return m_error(r,415);
    if(!canvas&&r->content_len&&strcmp(type,"application/json")&&strcmp(type,"application/json; charset=utf-8"))return m_error(r,415);
    char *body=m_alloc(r->content_len+1),*out=m_alloc(SETUP_OUTPUT_MAX);
    if(!body||!out){free(body);free(out);return m_error(r,503);}
    size_t received=0;esp_err_t result;
    while(received<r->content_len){
        if(!m_authorized(r)){result=m_error(r,401);goto done;}
        int count=httpd_req_recv(r,body+received,r->content_len-received);
        if(count<=0){result=m_error(r,400);goto done;}received+=(size_t)count;
    }
    if(!m_authorized(r)){result=m_error(r,401);goto done;}
    const char *method=r->method==HTTP_GET?"GET":r->method==HTTP_POST?"POST":r->method==HTTP_PUT?"PUT":"DELETE";
    int status=m_extension(method,path,type,body,received,out,SETUP_OUTPUT_MAX);
    result=m_reply(r,status,out,strlen(out));
done:
    m_clear(body,r->content_len+1);free(body);free(out);return result;
}
static esp_err_t m_api(httpd_req_t *r) {
    if (!m_origin(r)) return m_error(r, 403);
    if (!m_authorized(r)) return m_error(r, 401);
    if(!strncmp(r->uri,"/api/manage/pico/",17))return m_extended(r);
    if (r->content_len > SETUP_BODY_MAX) return m_error(r, 413);
    if (r->content_len) {
        char type[80] = {0};
        if (httpd_req_get_hdr_value_str(r, "Content-Type", type, sizeof(type)) != ESP_OK ||
            (strcmp(type, "application/json") && strcmp(type, "application/json; charset=utf-8"))) return m_error(r, 415);
    }
    char *body = m_alloc(r->content_len + 1), *out = m_alloc(SETUP_OUTPUT_MAX);
    read_pico_connectivity_status_t *net = m_alloc(sizeof(*net));
    rp_home_config_t *config = m_alloc(sizeof(*config));
    if (!body || !out || !net || !config) { free(body); free(out); free(net); free(config); return m_error(r, 503); }
    size_t received = 0; esp_err_t result = ESP_OK;
    while (received < r->content_len) {
        if (!m_authorized(r)) { result = m_error(r, 401); goto done; }
        int n = httpd_req_recv(r, body + received, r->content_len - received);
        if (n <= 0) { result = m_error(r, 400); goto done; }
        received += (size_t)n;
    }
    // 慢请求读完后再次鉴权，退出或到期立即阻止提交。/ Recheck after slow reads so exit/expiry prevents commits.
    if (!m_authorized(r)) { result = m_error(r, 401); goto done; }
    char path[96], query[RP_HOME_WEB_QUERY_LIMIT + 1] = {0};
    size_t path_bytes = strcspn(r->uri, "?");
    if (path_bytes >= sizeof(path) || httpd_req_get_url_query_len(r) >= sizeof(query)) { result = m_error(r, 400); goto done; }
    memcpy(path, r->uri, path_bytes); path[path_bytes] = 0;
    if (httpd_req_get_url_query_len(r)) httpd_req_get_url_query_str(r, query, sizeof(query));
    read_pico_connectivity_get_status(net); read_pico_home_link_get_config(config);
    const char *saved[READ_PICO_CONNECTIVITY_SAVED_MAX];
    for (size_t i = 0; i < net->saved_count && i < READ_PICO_CONNECTIVITY_SAVED_MAX; ++i) saved[i] = net->saved[i].ssid;
    const char *method = r->method == HTTP_GET ? "GET" : r->method == HTTP_PUT ? "PUT" : r->method == HTTP_DELETE ? "DELETE" : "POST";
    rp_home_web_request_t request = {.method=method, .path=path, .query=query, .body=body, .body_bytes=received,
        .network_online=net->connected && !net->suspended && !net->transfer_active,
        .at_home=net->connected && *config->home_wifi && !strcmp(config->home_wifi, net->ssid),
        .current_wifi=net->ssid, .saved_wifi=saved, .saved_wifi_count=net->saved_count < READ_PICO_CONNECTIVITY_SAVED_MAX ? net->saved_count : READ_PICO_CONNECTIVITY_SAVED_MAX};
    rp_home_web_response_t response = {0};
    if (!rp_home_web_handle(&request, &response, out, SETUP_OUTPUT_MAX)) result = m_error(r, 404);
    else result = m_reply(r, response.http_status, out, response.body_bytes);
 done:
    m_clear(body, r->content_len + 1); m_clear(config, sizeof(*config));
    free(body); free(out); free(net); free(config); return result;
}
void read_pico_management_get_status(read_pico_management_status_t *out) {
    if (!out) return;
    portENTER_CRITICAL(&m_lock); *out = m_status; int64_t expires = m_expires; portEXIT_CRITICAL(&m_lock);
    int64_t left = expires - esp_timer_get_time() / 1000;
    out->seconds_left = out->active && left > 0 ? (uint32_t)((left + 999) / 1000) : 0;
}
bool read_pico_management_active(void) {
    portENTER_CRITICAL(&m_lock); bool active = m_status.active && esp_timer_get_time() / 1000 < m_expires; portEXIT_CRITICAL(&m_lock); return active;
}
static void m_stop_locked(void) {
    portENTER_CRITICAL(&m_lock);
    m_status.active = false; m_status.url[0] = m_status.host[0] = 0; m_status.seconds_left = 0;
    if(!++m_session)++m_session;
    m_clear(m_key, sizeof(m_key)); m_expires = m_next_check = 0;
    portEXIT_CRITICAL(&m_lock);
    read_pico_bambu_auth_cancel();
    read_pico_content_cancel();
    if (m_http) { httpd_stop(m_http); m_http = NULL; }
    // 等待已授权请求退出后再次撤销，覆盖停止前最后一次排队的登录。
    // Revoke again after authorized requests finish to cover a last login queued during shutdown.
    read_pico_bambu_auth_cancel();
}
void read_pico_management_stop(void) {
    if (!m_lifecycle) return;
    xSemaphoreTake(m_lifecycle, portMAX_DELAY); m_stop_locked(); xSemaphoreGive(m_lifecycle);
}
bool read_pico_management_start(void) {
    // 仅UI任务首次创建；此后生命周期互斥，HTTP线程只读取短快照。
    // First creation belongs to the UI task; later lifecycle is serialized, while HTTP only reads short snapshots.
    if (!m_lifecycle) m_lifecycle = xSemaphoreCreateMutex();
    if (!m_lifecycle) return false;
    xSemaphoreTake(m_lifecycle, portMAX_DELAY); m_stop_locked();
    read_pico_connectivity_status_t *net = m_alloc(sizeof(*net));
    bool ok = false; const char *error = "请先连接 Wi-Fi";
    if (!net) { error = "内存不足，请稍后重试"; goto done; }
    read_pico_connectivity_get_status(net);
    if (!net->connected || net->suspended || net->busy || net->transfer_active || !*net->ip) goto done;
    uint8_t random[16]; char key[33]; esp_fill_random(random, sizeof(random));
    for (size_t i = 0; i < sizeof(random); ++i) snprintf(key + i * 2, 3, "%02x", random[i]);
    portENTER_CRITICAL(&m_lock);
    snprintf(m_key, sizeof(m_key), "%s", key); snprintf(m_status.host, sizeof(m_status.host), "%s", net->ip);
    snprintf(m_status.url, sizeof(m_status.url), "http://%s/manage#key=%s", net->ip, key);
    m_expires = esp_timer_get_time() / 1000 + SETUP_LIFETIME_MS; m_status.error[0] = 0;
    portEXIT_CRITICAL(&m_lock); m_clear(key, sizeof(key)); m_clear(random, sizeof(random));
    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG(); cfg.stack_size = 8192; cfg.max_uri_handlers = 6;
    cfg.uri_match_fn = httpd_uri_match_wildcard; cfg.lru_purge_enable = true; cfg.recv_wait_timeout = cfg.send_wait_timeout = 5;
    if (httpd_start(&m_http, &cfg) != ESP_OK) { error = "管理网页启动失败"; goto done; }
    const httpd_uri_t routes[] = {
        {.uri="/manage", .method=HTTP_GET, .handler=m_page},
        {.uri="/", .method=HTTP_GET, .handler=m_page},
        {.uri="/api/manage/*", .method=HTTP_GET, .handler=m_api},
        {.uri="/api/manage/*", .method=HTTP_PUT, .handler=m_api},
        {.uri="/api/manage/*", .method=HTTP_POST, .handler=m_api},
        {.uri="/api/manage/*", .method=HTTP_DELETE, .handler=m_api},
    };
    for (size_t i = 0; i < sizeof(routes) / sizeof(routes[0]); ++i) if (httpd_register_uri_handler(m_http, &routes[i]) != ESP_OK) { error = "管理网页启动失败"; goto done; }
    portENTER_CRITICAL(&m_lock); m_status.active = true; portEXIT_CRITICAL(&m_lock); ok = true;
 done:
    free(net);
    if (!ok) { m_stop_locked(); portENTER_CRITICAL(&m_lock); snprintf(m_status.error, sizeof(m_status.error), "%s", error); portEXIT_CRITICAL(&m_lock); }
    xSemaphoreGive(m_lifecycle); return ok;
}
void read_pico_management_tick(void) {
    read_pico_management_status_t status; read_pico_management_get_status(&status);
    if (!status.active) return;
    // UI高频tick只每秒取网络快照，到期仍立即撤销。/ Sample the network once per second; expire immediately.
    int64_t now = esp_timer_get_time() / 1000;
    portENTER_CRITICAL(&m_lock);
    bool check = !status.seconds_left || now >= m_next_check;
    if (check) m_next_check = now + 1000;
    portEXIT_CRITICAL(&m_lock);
    if (!check) return;
    read_pico_connectivity_status_t *net = m_alloc(sizeof(*net));
    if (!net) { read_pico_management_stop(); return; }
    read_pico_connectivity_get_status(net);
    bool stop = !status.seconds_left || !net->connected || net->suspended || net->transfer_active || strcmp(net->ip, status.host);
    free(net); if (stop) read_pico_management_stop();
}

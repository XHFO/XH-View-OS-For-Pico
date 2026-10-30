/* SPDX-License-Identifier: Apache-2.0
 * Host HTTP boundaries only. Production PGA, portal handlers and JSON rules remain unchanged.
 */
#pragma once
#include "native_preview_hal.h"
#include <sys/types.h>

typedef enum { HTTP_METHOD_GET, HTTP_METHOD_POST } esp_http_client_method_t;
typedef struct {
    const char *url;
    int timeout_ms, buffer_size;
    esp_err_t (*crt_bundle_attach)(void *);
    bool disable_auto_redirect;
    esp_http_client_method_t method;
} esp_http_client_config_t;
typedef struct native_http_client *esp_http_client_handle_t;
esp_err_t esp_crt_bundle_attach(void *);
esp_http_client_handle_t esp_http_client_init(const esp_http_client_config_t *);
esp_err_t esp_http_client_set_header(esp_http_client_handle_t,const char *,const char *);
esp_err_t esp_http_client_open(esp_http_client_handle_t,int);
int esp_http_client_write(esp_http_client_handle_t,const char *,int);
int64_t esp_http_client_fetch_headers(esp_http_client_handle_t);
int esp_http_client_get_status_code(esp_http_client_handle_t);
int esp_http_client_read(esp_http_client_handle_t,char *,int);
bool esp_http_client_is_complete_data_received(esp_http_client_handle_t);
esp_err_t esp_http_client_close(esp_http_client_handle_t);
esp_err_t esp_http_client_cleanup(esp_http_client_handle_t);

typedef enum { HTTP_GET, HTTP_POST, HTTP_PUT, HTTP_DELETE } httpd_method_t;
typedef struct httpd_req {
    const char *uri;
    httpd_method_t method;
    size_t content_len;
    void *user_ctx;
    void *native_context;
} httpd_req_t;
typedef struct {
    const char *uri;
    httpd_method_t method;
    esp_err_t (*handler)(httpd_req_t *);
    void *user_ctx;
} httpd_uri_t;
typedef struct {
    unsigned server_port, ctrl_port, stack_size, max_uri_handlers;
    bool lru_purge_enable;
    unsigned recv_wait_timeout, send_wait_timeout;
} httpd_config_t;
typedef struct native_http_server *httpd_handle_t;
#define HTTPD_DEFAULT_CONFIG() ((httpd_config_t){.server_port=80,.max_uri_handlers=8})
esp_err_t httpd_start(httpd_handle_t *,const httpd_config_t *);
esp_err_t httpd_stop(httpd_handle_t);
esp_err_t httpd_register_uri_handler(httpd_handle_t,const httpd_uri_t *);
size_t httpd_req_get_hdr_value_len(httpd_req_t *,const char *);
esp_err_t httpd_req_get_hdr_value_str(httpd_req_t *,const char *,char *,size_t);
size_t httpd_req_get_url_query_len(httpd_req_t *);
esp_err_t httpd_req_get_url_query_str(httpd_req_t *,char *,size_t);
int httpd_req_recv(httpd_req_t *,char *,size_t);
esp_err_t httpd_resp_set_hdr(httpd_req_t *,const char *,const char *);
esp_err_t httpd_resp_set_status(httpd_req_t *,const char *);
esp_err_t httpd_resp_set_type(httpd_req_t *,const char *);
esp_err_t httpd_resp_send(httpd_req_t *,const char *,ssize_t);
void esp_fill_random(void *,size_t);

/* Hex-encoded JSON envelope; response preserves production status, headers and body. */
void native_preview_home_portal_rpc(const char *,FILE *);
bool native_preview_home_network_enabled(void);

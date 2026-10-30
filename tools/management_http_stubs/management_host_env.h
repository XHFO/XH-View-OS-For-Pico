/* 无 socket 的管理HTTP测试替身。/ Socket-free management HTTP test doubles. */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>
#include "esp_err.h"

typedef enum { HTTP_GET, HTTP_PUT, HTTP_POST, HTTP_DELETE, HTTP_PATCH, HTTP_HEAD, HTTP_OPTIONS } httpd_method_t;
typedef struct {
    const char *uri, *host, *origin, *key, *content_type, *input;
    size_t content_len, received, chunk, input_bytes;
    httpd_method_t method;
    bool recv_fail,headers_purged;
    char status[64], type[64], output[420001];
    size_t output_bytes;
    unsigned security_headers, sends;
    bool gzip;
} httpd_req_t;
typedef void *httpd_handle_t;
typedef struct {
    const char *uri;
    httpd_method_t method;
    esp_err_t (*handler)(httpd_req_t *);
} httpd_uri_t;
typedef struct {
    int stack_size, max_uri_handlers, recv_wait_timeout, send_wait_timeout;
    bool lru_purge_enable;
    bool (*uri_match_fn)(const char *, const char *, size_t);
} httpd_config_t;
#define HTTPD_DEFAULT_CONFIG() ((httpd_config_t){0})
bool httpd_uri_match_wildcard(const char *, const char *, size_t);
esp_err_t httpd_start(httpd_handle_t *, const httpd_config_t *);
esp_err_t httpd_stop(httpd_handle_t);
esp_err_t httpd_register_uri_handler(httpd_handle_t, const httpd_uri_t *);
size_t httpd_req_get_hdr_value_len(httpd_req_t *, const char *);
esp_err_t httpd_req_get_hdr_value_str(httpd_req_t *, const char *, char *, size_t);
size_t httpd_req_get_url_query_len(httpd_req_t *);
esp_err_t httpd_req_get_url_query_str(httpd_req_t *, char *, size_t);
int httpd_req_recv(httpd_req_t *, char *, size_t);
esp_err_t httpd_resp_set_hdr(httpd_req_t *, const char *, const char *);
esp_err_t httpd_resp_set_status(httpd_req_t *, const char *);
esp_err_t httpd_resp_set_type(httpd_req_t *, const char *);
esp_err_t httpd_resp_send(httpd_req_t *, const char *, ssize_t);
esp_err_t httpd_resp_send_chunk(httpd_req_t *, const char *, ssize_t);

enum { MALLOC_CAP_SPIRAM = 1, MALLOC_CAP_8BIT = 2 };
void *heap_caps_calloc(size_t, size_t, unsigned);
void esp_fill_random(void *, size_t);
int64_t esp_timer_get_time(void);
typedef void *SemaphoreHandle_t;
typedef int portMUX_TYPE;
#define portMUX_INITIALIZER_UNLOCKED 0
#define portMAX_DELAY UINT32_MAX
#define portENTER_CRITICAL(mux) ((void)(mux))
#define portEXIT_CRITICAL(mux) ((void)(mux))
SemaphoreHandle_t xSemaphoreCreateMutex(void);
int xSemaphoreTake(SemaphoreHandle_t, uint32_t);
int xSemaphoreGive(SemaphoreHandle_t);

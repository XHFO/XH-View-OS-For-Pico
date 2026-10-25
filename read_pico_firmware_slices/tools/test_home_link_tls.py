#!/usr/bin/env python3
"""实际ESP HTTPS/MQTT分支的离线故障注入，不连接云端或更改电脑时间。
Offline fault injection into the actual ESP HTTPS/MQTT branches; no cloud or host-clock changes.
"""
from pathlib import Path
import os
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
source = (ROOT / 'components/read_pico_home_link/read_pico_home_link.c').read_text()
start = source.index('static bool tls_clock_ready(')
helpers = source[start:source.index('\n#endif', start)]
start = source.index('static rp_home_result_t http_request(')
request = source[start:source.index('\n#else\n    if (host_transport_offline())', start)]
request = request.replace('#ifdef ESP_PLATFORM\n', '', 1) + '\n}\n'
start = source.index('static bool mqtt_open(')
mqtt = source[start:source.index('\n#else\n    if (host_transport_offline())', start)]
mqtt = mqtt.replace('#ifdef ESP_PLATFORM\n', '', 1) + '\n}\n'

prelude = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include "read_pico_home_link.h"
#include "esp_tls_errors.h"
#define MBEDTLS_ERR_SSL_ALLOC_FAILED (-141)
#define MBEDTLS_ERR_SSL_TIMEOUT (-0x6800)
#define ESP_ERR_NO_MEM 0x101
#define HTTP_METHOD_POST 1
#define HTTP_METHOD_GET 0
#define ESP_LOGW(...) ((void)0)
typedef void *esp_http_client_handle_t;
typedef struct {const char *url;int timeout_ms,buffer_size;void *crt_bundle_attach;bool disable_auto_redirect;int method;} esp_http_client_config_t;
static void *esp_crt_bundle_attach=(void*)1;
static time_t clock_now;
static time_t fixture_time(time_t *out){if(out)*out=clock_now;return clock_now;}
#define time fixture_time
static int opened,transport,code,flags,init_count,clean_count,open_count,headers;
static bool oom;
static esp_http_client_handle_t esp_http_client_init(const esp_http_client_config_t *cfg){assert(cfg->crt_bundle_attach && cfg->disable_auto_redirect);init_count++;return oom?NULL:(void*)1;}
static int esp_http_client_set_header(void *h,const char *key,const char *v){assert(h && key && v);headers++;return 0;}
static int esp_http_client_open(void *h,int n){assert(h && n>=0);open_count++;return opened;}
static int esp_http_client_write(void *h,const char *p,int n){assert(h && p);return n;}
static int64_t esp_http_client_fetch_headers(void *h){assert(h);return 0;}
static int esp_http_client_get_status_code(void *h){assert(h);return 200;}
static int esp_http_client_read(void *h,char *p,int n){assert(h && p && n);return 0;}
static bool esp_http_client_is_complete_data_received(void *h){assert(h);return true;}
static int esp_http_client_close(void *h){assert(h);return 0;}
static int esp_http_client_cleanup(void *h){assert(h);clean_count++;return 0;}
static int esp_http_client_get_and_clear_last_tls_error(void *h,int *c,int *f){assert(h);*c=code;*f=flags;return transport;}
typedef struct {int marker;} esp_tls_t;
typedef struct {int timeout_ms;void *crt_bundle_attach;const unsigned char *cacert_buf;unsigned cacert_bytes;} esp_tls_cfg_t;
typedef struct {esp_tls_t *tls;int64_t deadline;rp_home_result_t failure;} mqtt_io_t;
static esp_tls_t tls_object;
static int64_t now_ms(void){return 0;}
static esp_tls_t *esp_tls_init(void){return oom?NULL:&tls_object;}
static int esp_tls_conn_new_sync(const char *host,int length,int port,const esp_tls_cfg_t *cfg,esp_tls_t *tls){assert(host && length>0 && port>0 && tls);assert(cfg->crt_bundle_attach || (cfg->cacert_buf && cfg->cacert_bytes));return opened?0:1;}
static int esp_tls_get_error_handle(esp_tls_t *tls,esp_tls_error_handle_t *error){assert(tls);*error=(void*)1;return 0;}
static int esp_tls_get_and_clear_last_error(void *h,int *c,int *f){assert(h);*c=code;*f=flags;return transport;}
static void esp_tls_conn_destroy(esp_tls_t *tls){assert(tls);}
'''
tests = r'''
int main(void){
 rp_home_http_request_t req={.url="https://api.bambulab.cn/v1/user-service/user/login",.method="POST",.body="{}",.content_type="application/json"};
 char body[128];size_t used=99;int status=99;
 clock_now=100;assert(http_request(NULL,&req,body,sizeof(body),&used,&status)==RP_HOME_TIME);assert(!init_count && !open_count && used==0 && status==0);
 clock_now=4102444800LL;assert(http_request(NULL,&req,body,sizeof(body),&used,&status)==RP_HOME_TIME);assert(!init_count);
 clock_now=1792425600;opened=-1;
 const int network_errors[]={ESP_ERR_ESP_TLS_CANNOT_RESOLVE_HOSTNAME,ESP_ERR_ESP_TLS_CANNOT_CREATE_SOCKET,ESP_ERR_ESP_TLS_FAILED_CONNECT_TO_HOST,ESP_ERR_ESP_TLS_CONNECTION_TIMEOUT,ESP_ERR_ESP_TLS_TCP_CLOSED_FIN,ESP_ERR_ESP_TLS_SERVER_HANDSHAKE_TIMEOUT,ESP_ERR_ESP_TLS_SOCKET_SETOPT_FAILED};
 for(size_t i=0;i<sizeof(network_errors)/sizeof(*network_errors);i++){transport=network_errors[i];code=flags=0;assert(http_request(NULL,&req,body,sizeof(body),&used,&status)==RP_HOME_OFFLINE);}
 transport=ESP_ERR_MBEDTLS_SSL_HANDSHAKE_FAILED;code=MBEDTLS_ERR_SSL_TIMEOUT;assert(http_request(NULL,&req,body,sizeof(body),&used,&status)==RP_HOME_OFFLINE);
 code=-0x2700;flags=8;assert(http_request(NULL,&req,body,sizeof(body),&used,&status)==RP_HOME_TLS);
 flags=0;assert(http_request(NULL,&req,body,sizeof(body),&used,&status)==RP_HOME_TLS);
 code=0;transport=ESP_ERR_MBEDTLS_X509_CRT_PARSE_FAILED;assert(http_request(NULL,&req,body,sizeof(body),&used,&status)==RP_HOME_TLS);
 transport=0;code=MBEDTLS_ERR_SSL_ALLOC_FAILED;assert(http_request(NULL,&req,body,sizeof(body),&used,&status)==RP_HOME_NOMEM);
 code=0;opened=ESP_ERR_NO_MEM;assert(http_request(NULL,&req,body,sizeof(body),&used,&status)==RP_HOME_NOMEM);
 opened=0;assert(http_request(NULL,&req,body,sizeof(body),&used,&status)==RP_HOME_OK && status==200);
 assert(init_count==clean_count && headers>=init_count);
 oom=true;assert(http_request(NULL,&req,body,sizeof(body),&used,&status)==RP_HOME_NOMEM);
 mqtt_io_t io={0};rp_home_printer_config_t printer={.source=RP_HOME_BAMBU_CLOUD};oom=false;opened=-1;
 clock_now=0;assert(!mqtt_open(&io,&printer) && io.failure==RP_HOME_TIME);
 clock_now=1792425600;transport=ESP_ERR_ESP_TLS_CANNOT_RESOLVE_HOSTNAME;code=flags=0;assert(!mqtt_open(&io,&printer) && io.failure==RP_HOME_OFFLINE && !io.tls);
 transport=ESP_ERR_MBEDTLS_SSL_HANDSHAKE_FAILED;code=-0x2700;flags=8;assert(!mqtt_open(&io,&printer) && io.failure==RP_HOME_TLS && !io.tls);
 code=MBEDTLS_ERR_SSL_TIMEOUT;flags=0;assert(!mqtt_open(&io,&printer) && io.failure==RP_HOME_OFFLINE);
 oom=true;assert(!mqtt_open(&io,&printer) && io.failure==RP_HOME_NOMEM);oom=false;
 opened=0;code=transport=0;assert(mqtt_open(&io,&printer));esp_tls_conn_destroy(io.tls);
 printer.source=RP_HOME_BAMBU_LAN;strcpy(printer.host,"printer.local");strcpy(printer.certificate,"trusted-CA-fixture");assert(mqtt_open(&io,&printer));esp_tls_conn_destroy(io.tls);
 printf("ESP HTTPS/MQTT fault injection passed: clock gate before credentials, DNS/TCP/timeouts, certificate/handshake, OOM and success; no certificate bypass\n");
}
'''

with tempfile.TemporaryDirectory(prefix='pico-tls-') as directory:
    private = Path(directory)
    (private / 'esp_err.h').write_text('#pragma once\ntypedef int esp_err_t;\n#define ESP_OK 0\n')
    test = private / 'test.c'
    test.write_text(prelude + helpers + request + mqtt + tests)
    binary = private / 'checks'
    subprocess.run([os.environ.get('CC', 'cc'), '-std=c11', '-g', '-Wall', '-Wextra', '-Werror',
                    '-fsanitize=address,undefined', '-fno-omit-frame-pointer',
                    '-I'+str(private), '-I'+str(ROOT/'components/read_pico_home_link/include'),
                    '-I'+str(ROOT.parent/'esp-idf-v6.1/components/esp-tls'), str(test), '-o', str(binary)], check=True)
    subprocess.run([str(binary)], check=True)

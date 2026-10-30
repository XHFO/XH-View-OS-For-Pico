#!/usr/bin/env python3
"""在SDK边界注入初始化内存故障，执行真实AP/STA传书与旧扫描生命周期。
Exercise exact transfer/legacy-scan lifecycles with SDK-boundary allocation faults, without devices.
"""
from pathlib import Path
import argparse
import hashlib
import json
import os
import re
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
COMPONENT = ROOT / "components/read_pico_transfer"


def extract(source, name):
    """保留原函数体，词法跳过注释和字符串。/ Preserve exact functions while skipping comment/string braces."""
    found = re.search(r"^(?:static\s+)?(?:[\w]+\s+)+" + re.escape(name) + r"\s*\([^;]*?\)\s*\{", source, re.M)
    if not found:
        raise ValueError("Function missing: " + name)
    at = source.index("{", found.start())
    depth = 0
    state = "code"
    while at < len(source):
        char = source[at]
        pair = source[at:at + 2]
        if state == "line":
            if char == "\n":
                state = "code"
        elif state == "block":
            if pair == "*/":
                state = "code"
                at += 1
        elif state in ('"', "'"):
            if char == "\\":
                at += 1
            elif char == state:
                state = "code"
        elif pair == "//":
            state = "line"
            at += 1
        elif pair == "/*":
            state = "block"
            at += 1
        elif char in ('"', "'"):
            state = char
        elif char == "{":
            depth += 1
        elif char == "}":
            depth -= 1
            if depth == 0:
                return source[found.start():at + 1]
        at += 1
    raise ValueError("Unterminated function: " + name)


WIFI_STUB = r'''
#pragma once
#include <stdint.h>
typedef struct {
    int static_rx_buf_num, static_tx_buf_num, dynamic_rx_buf_num, cache_tx_buf_num, rx_ba_win;
    int tx_buf_type, dynamic_tx_buf_num, rx_mgmt_buf_type, rx_mgmt_buf_num, mgmt_sbuf_num;
    int ampdu_rx_enable, ampdu_tx_enable, amsdu_tx_enable, nvs_enable, wifi_task_core_id, magic;
    uint64_t feature_caps;
} wifi_init_config_t;
static wifi_init_config_t host_wifi_default(void);
#define WIFI_INIT_CONFIG_DEFAULT() host_wifi_default()
'''


HOST = r'''
/* 测试只替换SDK边界，网络/文件生命周期来自生产函数。/ Only SDK boundaries are replaced; production owns networking/file lifecycles. */
#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <dirent.h>
#include <sys/stat.h>
#include "read_pico_transfer.h"
#include "transfer_policy.h"
#include "esp_wifi.h"
#include "wifi_memory_config.h"

static unsigned checks;
#define CHECK(expr) do { checks++; if (!(expr)) { fprintf(stderr,"transfer memory line %d: %s\n",__LINE__,#expr); abort(); } } while (0)
typedef int portMUX_TYPE;
#define portENTER_CRITICAL(p) ((void)(p))
#define portEXIT_CRITICAL(p) ((void)(p))
static portMUX_TYPE s_lock;
static read_pico_transfer_status_t s_status;
static read_pico_transfer_cfg_t s_cfg;
static char s_root[160], s_font_root[160];
typedef void *httpd_handle_t;
typedef struct { int kind; } esp_netif_t;
typedef const char *esp_event_base_t;
typedef void *esp_event_handler_instance_t;
static httpd_handle_t s_http;
static esp_netif_t *s_netif;
static esp_event_handler_instance_t s_events,s_ip_events;
static bool s_wifi,s_started,s_loop_owned,s_stopping,s_upload_active;
static char *s_buffer;
static transfer_connection_t s_connection;

enum { WIFI_STORAGE_RAM=1,WIFI_MODE_STA=2,WIFI_MODE_AP=3,WIFI_IF_STA=4,WIFI_IF_AP=5,
       WIFI_SCAN_TYPE_ACTIVE=6,WPA3_SAE_PWE_BOTH=7,ESP_MAC_WIFI_SOFTAP=8,
       ESP_EVENT_ANY_ID=-1,HTTP_GET=10,HTTP_POST=11,HTTP_PUT=12,HTTP_DELETE=13,
       MALLOC_CAP_SPIRAM=1,MALLOC_CAP_8BIT=2 };
enum { WIFI_AUTH_OPEN=0,WIFI_AUTH_WPA2_PSK=3,WIFI_AUTH_WPA_WPA2_PSK=4,
       WIFI_AUTH_WPA3_PSK=6,WIFI_AUTH_WPA2_WPA3_PSK=7,WIFI_AUTH_OWE=9 };
#define WIFI_EVENT "wifi"
#define IP_EVENT "ip"
typedef struct {
    struct { unsigned char ssid[32],password[64]; struct {int authmode;} threshold; struct {bool capable;} pmf_cfg; int sae_pwe_h2e; } sta;
    struct { unsigned char ssid[32],password[64]; unsigned ssid_len,channel,max_connection; int authmode; } ap;
} wifi_config_t;
typedef struct {bool show_hidden;int scan_type;struct {struct {int min,max;} active;} scan_time;} wifi_scan_config_t;
typedef struct {unsigned char ssid[33];int8_t rssi;int authmode;} wifi_ap_record_t;
typedef struct {int stack_size,max_uri_handlers,recv_wait_timeout;bool lru_purge_enable;} httpd_config_t;
#define HTTPD_DEFAULT_CONFIG() ((httpd_config_t){0})
typedef struct {int unused;} httpd_req_t;
typedef struct {const char *uri;int method;esp_err_t (*handler)(httpd_req_t *);void *user_ctx;} httpd_uri_t;
static esp_err_t handler(httpd_req_t *req){(void)req;return ESP_OK;}
#define index_handler handler
#define info_handler handler
#define upload_handler handler
#define font_info_handler handler
#define books_handler handler
#define wifi_handler handler
static void host_log(const char *tag,const char *format,...){(void)tag;(void)format;}
#define ESP_LOGI(...) host_log(__VA_ARGS__)
#define ESP_LOGW(...) host_log(__VA_ARGS__)
static const char *esp_err_to_name(esp_err_t err){(void)err;return "test error";}

static bool loop_exists,driver,radio,fail_init,fail_buffer,fail_http;
static int netifs,handlers,default_calls,init_calls,start_calls,stop_calls,deinit_calls;
static int http_calls,http_stops,route_calls,connect_calls,loop_creates,loop_deletes;
static int set_storage_calls,set_mode_calls,set_config_calls,scan_calls,scan_index;
static wifi_init_config_t captured;
static wifi_config_t captured_network;
static int captured_mode,captured_interface;
static wifi_init_config_t host_wifi_default(void) {
    default_calls++;
    return (wifi_init_config_t){.static_rx_buf_num=16,.static_tx_buf_num=16,.dynamic_rx_buf_num=32,
        .cache_tx_buf_num=32,.rx_ba_win=16,.tx_buf_type=0,.dynamic_tx_buf_num=29,
        .rx_mgmt_buf_type=1,.rx_mgmt_buf_num=7,.mgmt_sbuf_num=31,.ampdu_rx_enable=1,
        .ampdu_tx_enable=1,.amsdu_tx_enable=1,.nvs_enable=1,.wifi_task_core_id=1,
        .magic=0x1f2f3f4f,.feature_caps=UINT64_C(0x87654321fedcba98)};
}
static void check_policy(const wifi_init_config_t *cfg) {
    CHECK(cfg->static_rx_buf_num==4&&cfg->static_tx_buf_num==4);
    CHECK(cfg->dynamic_rx_buf_num==16&&cfg->cache_tx_buf_num==8&&cfg->rx_ba_win==4);
    CHECK(cfg->tx_buf_type==0&&cfg->dynamic_tx_buf_num==29);
    CHECK(cfg->rx_mgmt_buf_type==1&&cfg->rx_mgmt_buf_num==7&&cfg->mgmt_sbuf_num==31);
    CHECK(cfg->ampdu_rx_enable==1&&cfg->ampdu_tx_enable==1&&cfg->amsdu_tx_enable==1);
    CHECK(cfg->nvs_enable==1&&cfg->wifi_task_core_id==1&&cfg->magic==0x1f2f3f4f);
    CHECK(cfg->feature_caps==UINT64_C(0x87654321fedcba98));
}
static esp_err_t esp_netif_init(void){return ESP_OK;}
static esp_err_t esp_event_loop_create_default(void){loop_creates++;if(loop_exists)return ESP_ERR_INVALID_STATE;loop_exists=true;return ESP_OK;}
static esp_err_t esp_event_loop_delete_default(void){CHECK(loop_exists);loop_deletes++;loop_exists=false;return ESP_OK;}
static esp_netif_t *create_netif(int kind){esp_netif_t *out=malloc(sizeof(*out));CHECK(out);out->kind=kind;netifs++;return out;}
static esp_netif_t *esp_netif_create_default_wifi_ap(void){return create_netif(WIFI_MODE_AP);}
static esp_netif_t *esp_netif_create_default_wifi_sta(void){return create_netif(WIFI_MODE_STA);}
static void esp_netif_destroy_default_wifi(esp_netif_t *netif){CHECK(netif&&netifs==1);netifs--;free(netif);}
static esp_err_t esp_wifi_init(const wifi_init_config_t *cfg){init_calls++;check_policy(cfg);captured=*cfg;CHECK(!driver);if(fail_init)return ESP_ERR_NO_MEM;driver=true;return ESP_OK;}
static esp_err_t esp_wifi_deinit(void){CHECK(driver&&!radio);driver=false;deinit_calls++;return ESP_OK;}
static esp_err_t esp_wifi_set_storage(int mode){CHECK(driver&&mode==WIFI_STORAGE_RAM);set_storage_calls++;return ESP_OK;}
static esp_err_t esp_wifi_set_mode(int mode){CHECK(driver);captured_mode=mode;set_mode_calls++;return ESP_OK;}
static esp_err_t esp_wifi_set_config(int iface,const wifi_config_t *cfg){CHECK(driver);captured_interface=iface;captured_network=*cfg;set_config_calls++;return ESP_OK;}
static esp_err_t esp_wifi_start(void){CHECK(driver&&!radio);radio=true;start_calls++;return ESP_OK;}
static esp_err_t esp_wifi_stop(void){CHECK(driver&&radio);radio=false;stop_calls++;return ESP_OK;}
static esp_err_t esp_wifi_connect(void){CHECK(driver&&radio);connect_calls++;return ESP_OK;}
static esp_err_t esp_wifi_disconnect(void){CHECK(driver);return ESP_OK;}
static esp_err_t esp_wifi_scan_start(const wifi_scan_config_t *cfg,bool block){CHECK(driver&&radio&&block&&!cfg->show_hidden&&cfg->scan_type==WIFI_SCAN_TYPE_ACTIVE);scan_calls++;scan_index=0;return ESP_OK;}
static esp_err_t esp_wifi_scan_stop(void){CHECK(driver&&radio);return ESP_OK;}
static esp_err_t esp_wifi_clear_ap_list(void){CHECK(driver&&radio);return ESP_OK;}
static esp_err_t esp_wifi_scan_get_ap_num(uint16_t *count){*count=2;return ESP_OK;}
static esp_err_t esp_wifi_scan_get_ap_record(wifi_ap_record_t *record){CHECK(scan_index<2);memset(record,0,sizeof(*record));snprintf((char *)record->ssid,sizeof(record->ssid),"Test-%d",scan_index);record->rssi=(int8_t)(-30-scan_index++);record->authmode=WIFI_AUTH_WPA2_PSK;return ESP_OK;}
static esp_err_t esp_read_mac(uint8_t mac[6],int type){CHECK(type==ESP_MAC_WIFI_SOFTAP);memcpy(mac,(uint8_t[]){1,2,3,4,5,6},6);return ESP_OK;}
static esp_err_t esp_event_handler_instance_register(esp_event_base_t base,int id,void (*cb)(void *,esp_event_base_t,int32_t,void *),void *arg,esp_event_handler_instance_t *out){(void)base;(void)arg;CHECK(driver&&id==ESP_EVENT_ANY_ID&&cb);*out=malloc(1);CHECK(*out);handlers++;return ESP_OK;}
static esp_err_t esp_event_handler_instance_unregister(esp_event_base_t base,int id,esp_event_handler_instance_t instance){(void)base;CHECK(id==ESP_EVENT_ANY_ID&&instance&&handlers>0);free(instance);handlers--;return ESP_OK;}
static void wifi_event(void *a,esp_event_base_t b,int32_t c,void *d){(void)a;(void)b;(void)c;(void)d;}
static void ip_event(void *a,esp_event_base_t b,int32_t c,void *d){(void)a;(void)b;(void)c;(void)d;}
static void *heap_caps_malloc(size_t count,int caps){CHECK(count==16384&&caps==(MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));return fail_buffer?NULL:malloc(count);}
static esp_err_t httpd_start(httpd_handle_t *out,const httpd_config_t *cfg){CHECK(driver&&radio);CHECK(cfg->stack_size==12288&&cfg->max_uri_handlers==10&&cfg->lru_purge_enable);http_calls++;if(fail_http)return ESP_ERR_NO_MEM;*out=malloc(1);CHECK(*out);return ESP_OK;}
static esp_err_t httpd_stop(httpd_handle_t http){CHECK(http&&radio);http_stops++;free(http);return ESP_OK;}
static esp_err_t httpd_register_uri_handler(httpd_handle_t http,const httpd_uri_t *route){CHECK(http&&route&&route->handler);route_calls++;return ESP_OK;}
static int64_t esp_timer_get_time(void){return 1000000;}

/* 真实凭据读函数通过假NVS读取自编凭据，不读取用户网络。/ Actual credential reads use self-authored mock NVS data, never user networks. */
#define READ_PICO_TRANSFER_NVS_TEST
typedef unsigned nvs_handle_t;
enum {NVS_READONLY,NVS_READWRITE};
static esp_err_t nvs_open(const char *name,int mode,nvs_handle_t *out){CHECK(!strcmp(name,"rp_wifi")&&mode==NVS_READONLY);*out=1;return ESP_OK;}
static esp_err_t nvs_get_blob(nvs_handle_t handle,const char *key,void *out,size_t *size){CHECK(handle==1&&!strcmp(key,"credentials")&&*size==sizeof(transfer_credentials_t));transfer_credentials_t value={.version=1,.ssid="Test LAN",.password="samplepass"};memcpy(out,&value,sizeof(value));*size=sizeof(value);return ESP_OK;}
static void nvs_close(nvs_handle_t handle){CHECK(handle==1);}
static esp_err_t nvs_set_blob(nvs_handle_t h,const char *k,const void *p,size_t s){(void)h;(void)k;(void)p;(void)s;return ESP_OK;}
static esp_err_t nvs_erase_key(nvs_handle_t h,const char *k){(void)h;(void)k;return ESP_OK;}
static esp_err_t nvs_commit(nvs_handle_t h){(void)h;return ESP_OK;}
#include "transfer_credentials_store.h"
#include "transfer-memory-functions.inc"

static void clean_resources(bool borrowed) {
    CHECK(!driver&&!radio&&!netifs&&!handlers&&!s_wifi&&!s_started&&!s_http&&!s_netif&&!s_events&&!s_ip_events&&!s_buffer&&!s_loop_owned);
    CHECK(loop_exists==borrowed);
    if(borrowed)CHECK(loop_deletes==0);
}
static void reset(bool borrowed) {
    clean_resources(loop_exists);
    memset(&s_status,0,sizeof(s_status));s_stopping=s_upload_active=false;
    memset(&s_connection,0,sizeof(s_connection));
    loop_exists=borrowed;fail_init=fail_buffer=fail_http=false;
    default_calls=init_calls=start_calls=stop_calls=deinit_calls=http_calls=http_stops=route_calls=connect_calls=0;
    loop_creates=loop_deletes=set_storage_calls=set_mode_calls=set_config_calls=scan_calls=scan_index=0;
}
static uint64_t free_bytes(void *ctx){(void)ctx;return UINT64_C(10485760);}
static void test_scan(bool borrowed) {
    reset(borrowed);read_pico_transfer_network_t result[READ_PICO_TRANSFER_SCAN_MAX];size_t count=99;
    memset(result,0xa5,sizeof(result));fail_init=true;
    CHECK(read_pico_transfer_scan_wifi(result,&count)==ESP_ERR_NO_MEM);
    CHECK(default_calls==1&&init_calls==1&&start_calls==0&&deinit_calls==0&&http_calls==0&&scan_calls==0);
    CHECK(!set_storage_calls&&!set_mode_calls&&!set_config_calls&&!handlers&&!netifs);
    CHECK(count==0);for(size_t i=0;i<sizeof(result);i++)CHECK(((unsigned char *)result)[i]==0);
    clean_resources(borrowed);fail_init=false;
    CHECK(read_pico_transfer_scan_wifi(result,&count)==ESP_OK);
    CHECK(default_calls==2&&init_calls==2&&start_calls==1&&stop_calls==1&&deinit_calls==1&&scan_calls==1);
    CHECK(count==2&&!strcmp(result[0].ssid,"Test-0")&&result[0].supported);
    CHECK(!http_calls&&!route_calls&&!connect_calls&&!set_config_calls);clean_resources(borrowed);
}
static void test_start(const char *directory,read_pico_transfer_mode_t mode,bool borrowed,int fault) {
    reset(borrowed);
    read_pico_transfer_cfg_t cfg={.mode=mode,.root_dir=directory,.free_bytes_cb=free_bytes};
    fail_init=fault==0;fail_buffer=fault==1;fail_http=fault==2;
    CHECK(read_pico_transfer_start(&cfg)==ESP_ERR_NO_MEM);
    CHECK(default_calls==1&&init_calls==1&&s_status.state==READ_PICO_TRANSFER_ERROR&&s_status.last_error==ESP_ERR_NO_MEM);
    if(fault==0)CHECK(!start_calls&&!http_calls&&!route_calls&&!connect_calls&&!set_storage_calls&&!set_mode_calls&&!set_config_calls&&!deinit_calls);
    if(fault==1)CHECK(!start_calls&&!http_calls&&!route_calls&&deinit_calls==1);
    if(fault==2)CHECK(start_calls==1&&stop_calls==1&&http_calls==1&&!route_calls&&deinit_calls==1);
    clean_resources(borrowed);fail_init=fail_buffer=fail_http=false;
    CHECK(read_pico_transfer_start(&cfg)==ESP_OK);CHECK(default_calls==2&&init_calls==2);
    CHECK(driver&&radio&&netifs==1&&s_wifi&&s_started&&s_http&&s_buffer&&route_calls==10);
    CHECK(captured_mode==(mode==READ_PICO_TRANSFER_MODE_AP?WIFI_MODE_AP:WIFI_MODE_STA));
    CHECK(captured_interface==(mode==READ_PICO_TRANSFER_MODE_AP?WIFI_IF_AP:WIFI_IF_STA));
    if(mode==READ_PICO_TRANSFER_MODE_AP){CHECK(!strcmp((char *)captured_network.ap.ssid,"ReadPico-0506"));CHECK(captured_network.ap.max_connection==2&&s_status.network_ready&&s_status.state==READ_PICO_TRANSFER_READY&&!connect_calls&&handlers==1);}
    else {CHECK(!strcmp((char *)captured_network.sta.ssid,"Test LAN"));CHECK(s_status.state==READ_PICO_TRANSFER_STARTING&&connect_calls==1&&handlers==2);}
    CHECK(read_pico_transfer_start(&cfg)==ESP_ERR_INVALID_STATE&&init_calls==2);
    CHECK(read_pico_transfer_stop_checked()==ESP_OK);clean_resources(borrowed);
    CHECK(s_status.state==READ_PICO_TRANSFER_STOPPED);CHECK(read_pico_transfer_stop_checked()==ESP_OK);clean_resources(borrowed);
}
int main(int argc,char **argv) {
    CHECK(argc==2);
    for(int borrowed=0;borrowed<2;borrowed++){
        test_scan(borrowed);
        for(int mode=0;mode<2;mode++)for(int fault=0;fault<3;fault++)test_start(argv[1],(read_pico_transfer_mode_t)mode,borrowed,fault);
    }
    printf("transfer memory: %u checks, AP/STA + legacy scan, owned/borrowed loop, init/buffer/http OOM recovery passed\n",checks);
    return 0;
}
'''


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--sanitize", action="store_true")
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    source = (COMPONENT / "read_pico_transfer.c").read_text()
    memory_header = COMPONENT / "wifi_memory_config.h"
    header_snapshot = memory_header.read_text()
    names = ("decode_name_type", "decode_name_limit", "decode_name", "decode_font_name", "hex_value",
             "temporary_basename", "cleanup_interrupted", "read_pico_transfer_get_status", "publish_credentials",
             "set_error", "read_pico_transfer_service_poll", "read_pico_transfer_scan_wifi",
             "read_pico_transfer_stop_checked", "read_pico_transfer_stop", "read_pico_transfer_start")
    # 先声明所有函数以保留生产函数顺序，不改函数内部。/ Declare exact signatures first, without rewriting any function body.
    functions = [extract(source, name) for name in names]
    declarations = "\n".join(function[:function.index("{")].rstrip() + ";" for function in functions)
    for name in ("read_pico_transfer_scan_wifi", "read_pico_transfer_start"):
        assert "read_pico_wifi_init_config()" in extract(source, name), name + " bypasses shared memory policy"
    with tempfile.TemporaryDirectory(prefix="pico-transfer-memory-") as directory:
        build = Path(directory)
        books = build / "books"
        books.mkdir()
        (build / "esp_wifi.h").write_text(WIFI_STUB)
        (build / "transfer-memory-functions.inc").write_text(declarations + "\n\n" + "\n\n".join(functions))
        (build / "host.c").write_text(HOST)
        executable = build / "host"
        command = [os.environ.get("CC", "cc"), "-std=gnu11", "-Wall", "-Wextra", "-Werror", "-Wno-unused-function", "-O1", "-g"]
        if args.sanitize:
            command += ["-fsanitize=address,undefined", "-fno-omit-frame-pointer"]
        for include in (build, ROOT / "tools/connectivity_stubs", COMPONENT, COMPONENT / "include", ROOT / "managed_components/espressif__cjson/cJSON"):
            command += ["-I", str(include)]
        subprocess.run(command + [str(build / "host.c"), "-o", str(executable)], check=True)
        environment = os.environ.copy()
        environment["UBSAN_OPTIONS"] = environment.get("UBSAN_OPTIONS", "") + ":halt_on_error=1"
        result = subprocess.run([str(executable), str(books)], env=environment, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        print(result.stdout, end="")
        result.check_returncode()
    assert (COMPONENT / "read_pico_transfer.c").read_text() == source, "Transfer source changed during test; rerun after freeze"
    assert memory_header.read_text() == header_snapshot, "Memory policy changed during test; rerun after freeze"
    if args.output:
        args.output.mkdir(parents=True, exist_ok=True)
        (args.output / "validation.log").write_text(result.stdout)
        (args.output / "source-evidence.json").write_text(json.dumps({
            "physical_device_acceptance": False,
            "transfer_sha256": hashlib.sha256(source.encode()).hexdigest(),
            "memory_policy_sha256": hashlib.sha256(header_snapshot.encode()).hexdigest(),
            "sdk_boundary_stubs": True,
            "functions": list(names),
        }, indent=2) + "\n")


if __name__ == "__main__":
    main()

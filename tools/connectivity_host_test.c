/* SPDX-License-Identifier: Apache-2.0
 * 用SDK边界替身执行实际异步命令/worker状态机，不访问设备。
 * Run the actual asynchronous command/worker engine against SDK boundary doubles, without hardware.
 */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "connectivity_host_env.h"
#define READ_PICO_CONNECTIVITY_HOST_TEST
#include "../components/read_pico_transfer/read_pico_connectivity.c"
const char connectivity_host_wifi_event[]="wifi",connectivity_host_ip_event[]="ip";

static bool worker_context, driver_live, radio_live, transfer_live, loop_live;
static int64_t mock_now;
static unsigned connects, radio_starts, transfer_starts, transfer_stops, transfer_polls;
static int fail_commit, fail_join, fail_radio_stop, fail_radio_deinit,fail_wifi_init;
static unsigned wifi_init_calls;
static wifi_init_config_t last_init;
static size_t wifi_static_budget=16000;
static unsigned commit_calls, fail_commit_on;
static bool late_quiesce;
static char driver_ssid[33], transfer_root[160], transfer_fonts[160];
static wifi_ap_record_t scan_results[24];
static unsigned scan_count, scan_index, scan_starts, scan_start_failures;
static bool delay_sta_start;
static esp_err_t scan_start_error,scan_num_error,scan_record_error;
static read_pico_transfer_status_t transfer_status;
static void owner(void) { assert(worker_context); }
static void step(void) { bool before=worker_context;worker_context=true;connectivity_worker_step();worker_context=before; }
static void poll_at(int64_t ms) { mock_now=ms;worker_context=true;connectivity_poll();worker_context=false; }
static read_pico_connectivity_status_t status(void) { read_pico_connectivity_status_t out;read_pico_connectivity_get_status(&out);return out; }

struct host_queue { unsigned capacity,count; size_t size; void *items[4]; };
QueueHandle_t xQueueCreate(unsigned capacity,size_t size) { assert(capacity==4&&size==sizeof(void *));struct host_queue *q=calloc(1,sizeof(*q));q->capacity=capacity;q->size=size;return q; }
int xQueueSend(QueueHandle_t q,const void *item,unsigned wait) {
    assert(wait==0);
    if(late_quiesce){late_quiesce=false;assert(read_pico_connectivity_quiesce(0)==ESP_ERR_TIMEOUT);step();assert(!status().suspended&&status().busy);}
    if(q->count==q->capacity)return 0;
    memcpy(&q->items[q->count++],item,q->size);return pdTRUE;
}
int xQueueReceive(QueueHandle_t q,void *out,unsigned wait) {
    owner();mock_now+=wait;
    if(!q->count)return 0;
    memcpy(out,&q->items[0],q->size);for(unsigned i=1;i<q->count;i++)q->items[i-1]=q->items[i];q->items[--q->count]=NULL;return pdTRUE;
}
void vQueueDelete(QueueHandle_t q) { free(q); }
int xTaskCreate(void (*fn)(void *),const char *name,unsigned stack,void *arg,unsigned priority,void *handle) { assert(fn&&name&&stack>=6144&&priority==3);(void)arg;(void)handle;return pdPASS; }
void vTaskDelay(unsigned ms) { mock_now+=ms;if(!worker_context)step(); }
int64_t esp_timer_get_time(void) { return mock_now*1000; }

typedef struct { bool present;size_t size;unsigned char bytes[2048]; } blob_t;
static blob_t blobs[2],staged[2];
static bool handles[2];
esp_err_t nvs_open(const char *name,int mode,nvs_handle_t *handle) { owner();(void)mode;int at=!strcmp(name,"rp_connect")?1:0;assert(at||!strcmp(name,"rp_wifi"));assert(!handles[at]);handles[at]=true;staged[at]=blobs[at];*handle=(unsigned)at;return ESP_OK; }
esp_err_t nvs_get_blob(nvs_handle_t handle,const char *key,void *out,size_t *length) { owner();assert(handle<2&&handles[handle]);assert(!strcmp(key,handle?"profiles":"credentials"));blob_t *b=&blobs[handle];if(!b->present)return ESP_ERR_NVS_NOT_FOUND;if(*length<b->size)return ESP_ERR_NVS_INVALID_LENGTH;*length=b->size;memcpy(out,b->bytes,b->size);return ESP_OK; }
esp_err_t nvs_set_blob(nvs_handle_t handle,const char *key,const void *data,size_t size) { owner();(void)key;assert(handles[handle]&&size<=sizeof(staged[handle].bytes));staged[handle].present=true;staged[handle].size=size;memcpy(staged[handle].bytes,data,size);return ESP_OK; }
esp_err_t nvs_erase_key(nvs_handle_t handle,const char *key) { owner();(void)key;staged[handle].present=false;return ESP_OK; }
esp_err_t nvs_commit(nvs_handle_t handle) { owner();++commit_calls;if(fail_commit||(fail_commit_on&&commit_calls==fail_commit_on))return ESP_FAIL;blobs[handle]=staged[handle];return ESP_OK; }
void nvs_close(nvs_handle_t handle) { owner();assert(handles[handle]);handles[handle]=false;memset(&staged[handle],0,sizeof(staged[handle])); }

esp_err_t esp_netif_init(void) { owner();return ESP_OK; }
esp_err_t esp_event_loop_create_default(void) { owner();if(loop_live)return ESP_ERR_INVALID_STATE;loop_live=true;return ESP_OK; }
esp_err_t esp_event_loop_delete_default(void) { owner();assert(loop_live&&!radio_live&&!driver_live);loop_live=false;return ESP_OK; }
static esp_netif_t mock_netif;
esp_netif_t *esp_netif_create_default_wifi_sta(void) { owner();assert(!transfer_live);return &mock_netif; }
void esp_netif_destroy_default_wifi(esp_netif_t *netif) { owner();assert(netif==&mock_netif&&!driver_live); }
esp_err_t esp_event_handler_instance_register(esp_event_base_t base,int id,void (*fn)(void *,esp_event_base_t,int32_t,void *),void *arg,esp_event_handler_instance_t *out) { owner();(void)id;(void)arg;assert(fn);*out=(void *)base;return ESP_OK; }
esp_err_t esp_event_handler_instance_unregister(esp_event_base_t base,int id,esp_event_handler_instance_t handle) { owner();(void)base;(void)id;assert(handle);return ESP_OK; }
esp_err_t esp_wifi_init(const wifi_init_config_t *cfg) {
    owner();assert(cfg&&!driver_live&&!transfer_live);last_init=*cfg;++wifi_init_calls;
    assert(cfg->magic==0x5a5a&&cfg->feature_caps==0x1234&&cfg->tx_buf_type==0);
    assert(cfg->rx_ba_win<=cfg->static_rx_buf_num&&cfg->dynamic_rx_buf_num>=cfg->static_rx_buf_num);
    assert(cfg->rx_mgmt_buf_num==5&&cfg->mgmt_sbuf_num==32&&cfg->ampdu_rx_enable&&cfg->ampdu_tx_enable);
    if(fail_wifi_init || (size_t)(cfg->static_rx_buf_num+cfg->static_tx_buf_num)*1600>wifi_static_budget)return ESP_ERR_NO_MEM;
    driver_live=true;return ESP_OK;
}
esp_err_t esp_wifi_deinit(void) { owner();assert(driver_live&&!radio_live);if(fail_radio_deinit)return ESP_FAIL;driver_live=false;return ESP_OK; }
esp_err_t esp_wifi_set_storage(int storage) { owner();assert(storage==WIFI_STORAGE_RAM&&driver_live);return ESP_OK; }
esp_err_t esp_wifi_set_mode(int mode) { owner();assert(mode==WIFI_MODE_STA&&driver_live);return ESP_OK; }
esp_err_t esp_wifi_start(void) { owner();assert(driver_live&&!radio_live);radio_live=true;radio_starts++;if(!delay_sta_start)connectivity_event(NULL,WIFI_EVENT,WIFI_EVENT_STA_START,NULL);return ESP_OK; }
esp_err_t esp_wifi_stop(void) { owner();assert(radio_live);if(fail_radio_stop)return ESP_FAIL;radio_live=false;return ESP_OK; }
esp_err_t esp_wifi_set_config(int interface,const wifi_config_t *wifi) { owner();assert(interface==WIFI_IF_STA&&driver_live);memcpy(driver_ssid,wifi->sta.ssid,32);driver_ssid[32]=0;return ESP_OK; }
esp_err_t esp_wifi_connect(void) { owner();assert(radio_live);connects++;return ESP_OK; }
esp_err_t esp_wifi_disconnect(void) { owner();assert(radio_live);return ESP_OK; }
esp_err_t esp_wifi_scan_start(const wifi_scan_config_t *scan,bool blocking) { owner();assert(scan&&!blocking&&radio_live);scan_starts++;if(scan_start_failures){scan_start_failures--;return scan_start_error;}scan_index=0;return ESP_OK; }
esp_err_t esp_wifi_scan_stop(void) { owner();return ESP_OK; }
esp_err_t esp_wifi_clear_ap_list(void) { owner();return ESP_OK; }
esp_err_t esp_wifi_scan_get_ap_num(uint16_t *count) { owner();if(scan_num_error)return scan_num_error;*count=(uint16_t)scan_count;return ESP_OK; }
esp_err_t esp_wifi_scan_get_ap_record(wifi_ap_record_t *out) { owner();if(scan_record_error)return scan_record_error;assert(scan_index<scan_count);*out=scan_results[scan_index++];return ESP_OK; }
esp_err_t esp_wifi_sta_get_ap_info(wifi_ap_record_t *out) { owner();out->rssi=-42;return ESP_OK; }

void read_pico_transfer_get_status(read_pico_transfer_status_t *out) { *out=transfer_status; }
void read_pico_management_stop(void) {}
esp_err_t read_pico_transfer_save_wifi(const char *ssid,const char *password) { owner();assert(!driver_live&&!transfer_live);transfer_credentials_t c;assert(credentials_from_text(ssid,password,&c));esp_err_t err=store_credentials(&c);clear_secret(&c,sizeof(c));return err; }
esp_err_t read_pico_transfer_start(const read_pico_transfer_cfg_t *cfg) { owner();assert(!driver_live&&!transfer_live);transfer_live=true;transfer_starts++;snprintf(transfer_root,sizeof(transfer_root),"%s",cfg->root_dir);snprintf(transfer_fonts,sizeof(transfer_fonts),"%s",cfg->font_dir?cfg->font_dir:"");memset(&transfer_status,0,sizeof(transfer_status));transfer_status.mode=cfg->mode;transfer_status.state=READ_PICO_TRANSFER_READY;transfer_status.network_ready=true;strcpy(transfer_status.ssid,"ReadPico-test");strcpy(transfer_status.url,"http://192.168.4.1");transfer_credentials_t c;assert(load_credentials(&c)==ESP_OK);strcpy(transfer_status.wifi_ssid,c.ssid);clear_secret(&c,sizeof(c));return ESP_OK; }
esp_err_t read_pico_transfer_stop_checked(void) { owner();transfer_stops++;if(fail_join)return ESP_FAIL;transfer_live=false;transfer_status.state=READ_PICO_TRANSFER_STOPPED;transfer_status.network_ready=false;return ESP_OK; }
void read_pico_transfer_service_poll(void) { owner();assert(transfer_live);transfer_polls++; }

static void got_ip(void) { ip_event_got_ip_t event={.esp_netif=c_netif,.ip_info.ip.bytes={192,168,1,12}};connectivity_event(NULL,IP_EVENT,IP_EVENT_STA_GOT_IP,&event);poll_at(mock_now+500); }
static uint64_t free_bytes(void *ctx) { (void)ctx;assert(!"Engine must not invoke caller storage callbacks outside the transfer implementation");return 0; }
static void complete(void) { while(c_queue->count)step(); }

static void test_normal_and_scan(void) {
    assert(read_pico_connectivity_init()==ESP_OK);worker_context=true;connectivity_worker_initialize();worker_context=false;
    assert(status().ready&&!status().wifi_enabled&&status().saved_count==0&&transfer_starts==0);
    assert(!read_pico_connectivity_online());
    // 无凭据也能启用STA与扫描，关闭后仍保留真正的零记忆状态。
    // With no credentials, enabling starts STA and permits scanning; disabling keeps the profile list empty.
    wifi_init_config_t defaults=WIFI_INIT_CONFIG_DEFAULT();
    assert((size_t)(defaults.static_rx_buf_num+defaults.static_tx_buf_num)*1600>wifi_static_budget);
    fail_wifi_init=1;unsigned starts_before=radio_starts;
    assert(read_pico_connectivity_set_enabled(true)==ESP_OK);step();
    assert(status().last_error==ESP_ERR_NO_MEM&&status().last_operation==READ_PICO_CONNECTIVITY_OP_WIFI);
    assert(!status().busy&&!status().wifi_enabled&&!status().connecting&&!driver_live&&!loop_live&&!c_netif);
    assert(radio_starts==starts_before&&transfer_starts==0&&last_init.static_rx_buf_num==4&&last_init.static_tx_buf_num==4);
    unsigned init_after_failure=wifi_init_calls;poll_at(mock_now+1000);
    assert(wifi_init_calls==init_after_failure&&!status().busy&&!status().wifi_enabled);
    fail_wifi_init=0;
    assert(read_pico_connectivity_set_enabled(true)==ESP_OK);step();
    assert((size_t)(last_init.static_rx_buf_num+last_init.static_tx_buf_num)*1600==12800);
    assert(last_init.dynamic_rx_buf_num==16&&last_init.cache_tx_buf_num==8&&last_init.rx_ba_win==4);
    assert(status().wifi_enabled&&!status().connected&&!status().connecting&&status().saved_count==0&&driver_live&&radio_live);
    assert(status().state==READ_PICO_CONNECTIVITY_IDLE&&status().last_error==ESP_OK);
    scan_count=1;scan_results[0]=(wifi_ap_record_t){.ssid="First Home",.rssi=-45,.authmode=WIFI_AUTH_WPA2_PSK};
    assert(read_pico_connectivity_scan()==ESP_OK);step();assert(status().scanning&&status().saved_count==0);
    connectivity_event(NULL,WIFI_EVENT,WIFI_EVENT_SCAN_DONE,NULL);poll_at(mock_now+500);
    assert(!status().scanning&&status().network_count==1&&!strcmp(status().networks[0].ssid,"First Home")&&!status().connected);
    assert(read_pico_connectivity_set_enabled(false)==ESP_OK);step();
    assert(!status().wifi_enabled&&!driver_live&&!radio_live&&status().saved_count==0);
    // 真正异步启动：命令完成但STA_START尚未到达，扫描保持等待而不失败。
    // True asynchronous startup: the enable command completes before STA_START; scan waits without failing.
    delay_sta_start=true;
    assert(read_pico_connectivity_set_enabled(true)==ESP_OK);
    assert(read_pico_connectivity_scan()==ESP_OK);step();
    assert(status().wifi_enabled&&!c_sta_ready&&status().busy);step();
    assert(status().scanning&&c_scan_pending&&!c_scan&&!status().busy);
    unsigned scans_before=scan_starts;uint32_t done_before=status().scan_completed_seq;
    poll_at(mock_now+500);assert(scan_starts==scans_before&&status().scanning);
    connectivity_event(NULL,WIFI_EVENT,WIFI_EVENT_STA_START,NULL);
    scan_start_error=ESP_ERR_WIFI_STATE;scan_start_failures=2;
    poll_at(mock_now+500);assert(scan_starts==scans_before+1&&status().scanning&&!c_scan&&status().scan_completed_seq==done_before);
    poll_at(mock_now+100);assert(scan_starts==scans_before+1);
    poll_at(mock_now+400);assert(scan_starts==scans_before+2&&!c_scan);
    poll_at(mock_now+500);assert(scan_starts==scans_before+3&&c_scan);
    scan_count=0;connectivity_event(NULL,WIFI_EVENT,WIFI_EVENT_SCAN_DONE,NULL);poll_at(mock_now+500);
    assert(!status().scanning&&!status().network_count&&status().scan_completed_seq==done_before+1&&status().scan_error==ESP_OK);
    // 扫描事件本身失败不能读取旧结果；读取失败、启动失败、超时各自结束并允许重扫。
    // Failed scan events cannot publish stale records; read/start/timeout failures each finish and permit retry.
    assert(read_pico_connectivity_scan()==ESP_OK);step();
    wifi_event_sta_scan_done_t failed_scan={.status=1};scan_count=1;
    connectivity_event(NULL,WIFI_EVENT,WIFI_EVENT_SCAN_DONE,&failed_scan);poll_at(mock_now+500);
    assert(!status().scanning&&!status().network_count&&status().scan_error==ESP_FAIL&&status().scan_failure==READ_PICO_CONNECTIVITY_SCAN_RESULT);
    assert(status().last_operation==READ_PICO_CONNECTIVITY_OP_SCAN);
    scan_num_error=ESP_FAIL;assert(read_pico_connectivity_scan()==ESP_OK);step();
    connectivity_event(NULL,WIFI_EVENT,WIFI_EVENT_SCAN_DONE,NULL);poll_at(mock_now+500);
    assert(status().scan_failure==READ_PICO_CONNECTIVITY_SCAN_RESULT&&!status().scanning);scan_num_error=ESP_OK;
    scan_record_error=ESP_FAIL;assert(read_pico_connectivity_scan()==ESP_OK);step();
    connectivity_event(NULL,WIFI_EVENT,WIFI_EVENT_SCAN_DONE,NULL);poll_at(mock_now+500);
    assert(status().scan_failure==READ_PICO_CONNECTIVITY_SCAN_RESULT&&!status().scanning);scan_record_error=ESP_OK;
    scan_start_error=ESP_ERR_NO_MEM;scan_start_failures=1;assert(read_pico_connectivity_scan()==ESP_OK);step();
    assert(!status().scanning&&status().scan_error==ESP_ERR_NO_MEM&&status().scan_failure==READ_PICO_CONNECTIVITY_SCAN_START);
    scan_start_error=ESP_ERR_WIFI_NOT_STARTED;scan_start_failures=100;assert(read_pico_connectivity_scan()==ESP_OK);step();
    assert(status().scanning);poll_at(mock_now+21000);
    assert(!status().scanning&&status().scan_error==ESP_ERR_TIMEOUT&&status().scan_failure==READ_PICO_CONNECTIVITY_SCAN_TIMEOUT);
    scan_start_failures=0;assert(read_pico_connectivity_scan()==ESP_OK);step();
    scan_count=1;connectivity_event(NULL,WIFI_EVENT,WIFI_EVENT_SCAN_DONE,NULL);poll_at(mock_now+500);
    assert(status().scan_error==ESP_OK&&status().network_count==1);
    assert(read_pico_connectivity_scan()==ESP_OK);step();assert(status().scanning);
    assert(read_pico_connectivity_set_enabled(false)==ESP_OK);step();
    assert(!status().scanning&&!status().wifi_enabled&&status().scan_failure==READ_PICO_CONNECTIVITY_SCAN_CANCELLED);
    // STA_START丢失时有界结束，关闭/重开后的首次扫描仍然可用。
    // Missing STA_START finishes within a bound; the first scan after a fresh restart remains usable.
    assert(read_pico_connectivity_set_enabled(true)==ESP_OK);step();assert(!c_sta_ready);
    assert(read_pico_connectivity_scan()==ESP_OK);step();poll_at(mock_now+21000);
    assert(!status().scanning&&status().scan_failure==READ_PICO_CONNECTIVITY_SCAN_TIMEOUT);
    assert(read_pico_connectivity_set_enabled(false)==ESP_OK);step();delay_sta_start=false;
    char ssid[33]="home",password[65]="12345678";
    assert(read_pico_connectivity_connect(ssid,password)==ESP_OK);strcpy(ssid,"mutated");memset(password,'x',8);
    assert(status().busy&&!driver_live);step();assert(driver_live&&!strcmp(driver_ssid,"home")&&status().connecting);
    poll_at(mock_now+500);assert(connects==1&&status().connecting);
    assert(read_pico_connectivity_scan()==ESP_OK);step();assert(status().scanning&&c_scan_pending&&!c_scan&&status().last_error==ESP_OK&&status().connecting);
    got_ip();assert(status().connected&&!status().connecting&&!strcmp(status().ip,"192.168.1.12")&&status().saved_count==1);
    assert(!strcmp(status().saved[0].ssid,"home")&&transfer_starts==0);
    assert(c_scan&&!c_scan_pending);connectivity_event(NULL,WIFI_EVENT,WIFI_EVENT_SCAN_DONE,NULL);poll_at(mock_now+500);
    unsigned starts=radio_starts;assert(read_pico_connectivity_resume()==ESP_OK);step();assert(radio_starts==starts);
    scan_count=3;scan_results[0]=(wifi_ap_record_t){.ssid="same",.rssi=-80,.authmode=WIFI_AUTH_WPA2_PSK};scan_results[1]=(wifi_ap_record_t){.ssid="open",.rssi=-50,.authmode=WIFI_AUTH_OPEN};scan_results[2]=(wifi_ap_record_t){.ssid="same",.rssi=-30,.authmode=WIFI_AUTH_WPA2_PSK};
    assert(read_pico_connectivity_scan()==ESP_OK);step();assert(status().scanning);
    connectivity_event(NULL,WIFI_EVENT,WIFI_EVENT_SCAN_DONE,NULL);poll_at(mock_now+500);
    assert(!status().scanning&&status().connected&&status().network_count==2&&!strcmp(status().networks[0].ssid,"same")&&status().networks[0].rssi==-30);
    assert(read_pico_connectivity_scan()==ESP_OK);step();poll_at(mock_now+10500);assert(!status().scanning&&status().last_error==ESP_ERR_TIMEOUT);
    assert(read_pico_connectivity_set_auto_join("home",false)==ESP_OK);step();assert(!status().saved[0].auto_join);
    fail_radio_stop=1;assert(read_pico_connectivity_forget("home")==ESP_OK);step();assert(status().last_error==ESP_FAIL&&status().saved_count==1&&status().connected&&driver_live);fail_radio_stop=0;
    got_ip();assert(status().saved_count==1&&status().connected);
    fail_commit=1;assert(read_pico_connectivity_forget("home")==ESP_OK);step();assert(status().last_error==ESP_FAIL&&status().saved_count==1&&!status().connected);fail_commit=0;
}

static void test_transfer_quiesce(void) {
    char root[]="/mounted/books",fonts[]="/mounted/fonts";
    read_pico_transfer_cfg_t cfg={.mode=READ_PICO_TRANSFER_MODE_STA,.root_dir=root,.font_dir=fonts,.free_bytes_cb=free_bytes};
    assert(read_pico_connectivity_start_transfer(&cfg)==ESP_OK);root[1]='X';fonts[1]='X';step();
    assert(transfer_live&&!driver_live&&status().transfer_active&&!strcmp(transfer_root,"/mounted/books")&&!strcmp(transfer_fonts,"/mounted/fonts"));
    poll_at(mock_now+500);assert(transfer_polls==1);
    fail_join=1;assert(read_pico_connectivity_stop_transfer()==ESP_OK);step();assert(status().transfer_active&&status().last_error==ESP_FAIL&&!driver_live);
    assert(read_pico_connectivity_quiesce(100)==ESP_ERR_TIMEOUT);assert(status().busy&&!status().suspended&&transfer_live);
    assert(read_pico_connectivity_scan()==ESP_ERR_INVALID_STATE);
    fail_join=0;step();assert(status().suspended&&!status().transfer_active&&!status().busy&&!driver_live&&!transfer_live);
    assert(read_pico_connectivity_quiesce(100)==ESP_OK);
    // legacy演示页修改凭据后，resume同步变更而不复活已删除列表。
    // Resume imports changes made by the legacy demo without resurrecting unchanged deleted profiles.
    worker_context=true;transfer_credentials_t legacy;assert(credentials_from_text("legacy","abcdefgh",&legacy));assert(store_credentials(&legacy)==ESP_OK);worker_context=false;
    assert(read_pico_connectivity_resume()==ESP_OK);step();assert(!status().suspended&&status().saved_count==2&&!strcmp(status().saved[0].ssid,"legacy"));
    got_ip();assert(!strcmp(status().ssid,"legacy"));
    cfg.mode=READ_PICO_TRANSFER_MODE_AP;cfg.root_dir="/mounted/books";cfg.font_dir=NULL;
    assert(read_pico_connectivity_start_transfer(&cfg)==ESP_OK);step();assert(status().transfer_active);
    worker_context=true;assert(store_credentials(NULL)==ESP_OK);worker_context=false;
    assert(read_pico_connectivity_stop_transfer()==ESP_OK);step();assert(!status().transfer_active&&status().saved_count==1&&strcmp(status().saved[0].ssid,"legacy"));
}

static void test_failure_and_queue(void) {
    assert(read_pico_connectivity_connect_saved("home")==ESP_OK);step();got_ip();
    fail_radio_stop=1;assert(read_pico_connectivity_set_enabled(false)==ESP_OK);step();assert(driver_live&&status().last_error==ESP_FAIL);fail_radio_stop=0;
    fail_radio_deinit=1;assert(read_pico_connectivity_quiesce(100)==ESP_ERR_TIMEOUT);assert(driver_live&&!radio_live&&!status().suspended);fail_radio_deinit=0;step();assert(status().suspended&&!driver_live);
    assert(read_pico_connectivity_resume()==ESP_OK);step();assert(!status().suspended&&!status().wifi_enabled);
    assert(read_pico_connectivity_connect_saved("home")==ESP_OK);step();assert(status().wifi_enabled&&status().connecting);got_ip();
    assert(read_pico_connectivity_connect("unreachable","12345678")==ESP_OK);step();poll_at(mock_now+16000);assert(!status().connected&&!status().connecting&&status().last_error==ESP_ERR_TIMEOUT);
    got_ip();assert(!status().connected&&!status().connecting&&c_connection.failed);
    connectivity_event(NULL,WIFI_EVENT,WIFI_EVENT_STA_DISCONNECTED,NULL);poll_at(mock_now+500);assert(!status().connected&&!status().connecting);
    assert(read_pico_connectivity_connect_saved("home")==ESP_OK);step();got_ip();
    for(int i=0;i<4;i++)assert(read_pico_connectivity_set_auto_join("home",i%2)==ESP_OK);
    assert(read_pico_connectivity_scan()==ESP_ERR_NO_MEM);assert(status().busy);complete();assert(!status().busy);
    late_quiesce=true;assert(read_pico_connectivity_connect("must-not-start","12345678")==ESP_OK);step();assert(status().suspended&&!driver_live&&!transfer_live);
    assert(read_pico_connectivity_resume()==ESP_OK);step();
    assert(read_pico_connectivity_quiesce(500)==ESP_OK);assert(!driver_live&&!transfer_live);
}

static void test_forget_legacy(void) {
    worker_context=true;transfer_credentials_t legacy;assert(credentials_from_text("home","12345678",&legacy));assert(store_credentials(&legacy)==ESP_OK);worker_context=false;
    fail_commit=1;assert(read_pico_connectivity_forget("home")==ESP_OK);step();assert(status().saved_count==1&&blobs[0].present&&status().last_error==ESP_FAIL);fail_commit=0;
    fail_commit_on=commit_calls+2;assert(read_pico_connectivity_forget("home")==ESP_OK);step();assert(status().saved_count==1&&!blobs[0].present&&status().last_error==ESP_FAIL);fail_commit_on=0;
    assert(read_pico_connectivity_forget("home")==ESP_OK);step();assert(status().saved_count==0&&!blobs[0].present);
    assert(read_pico_connectivity_connect("home","12345678")==ESP_OK);step();got_ip();assert(status().saved_count==1);
}

static void test_bounded_auto(void) {
    assert(status().suspended);
    worker_context=true;memset(&c_profiles,0,sizeof(c_profiles));c_profiles.version=1;c_profiles.enabled=1;
    for(int i=0;i<8;i++){
        char name[33];snprintf(name,sizeof(name),"saved-%d",i);transfer_credentials_t credentials;
        assert(credentials_from_text(name,"12345678",&credentials));assert(profiles_remember(&c_profiles,&credentials)==ESP_OK);
    }
    worker_context=false;
    assert(read_pico_connectivity_resume()==ESP_OK);step();
    unsigned before=connects;
    for(int i=7;i>=0;i--){
        char expected[33];snprintf(expected,sizeof(expected),"saved-%d",i);assert(!strcmp(c_target.ssid,expected));
        poll_at(mock_now+16000);
    }
    assert(c_connection.failed&&!c_auto_cycle&&!status().connecting&&!status().connected);
    unsigned starts=radio_starts;poll_at(mock_now+100000);assert(starts==radio_starts&&connects-before<=24);
    got_ip();assert(!status().connected);
    read_pico_transfer_cfg_t cfg={.mode=READ_PICO_TRANSFER_MODE_AP,.root_dir="/mounted/books",.free_bytes_cb=free_bytes};
    assert(read_pico_connectivity_start_transfer(&cfg)==ESP_OK);step();
    worker_context=true;transfer_credentials_t ninth;assert(credentials_from_text("ninth","12345678",&ninth));assert(store_credentials(&ninth)==ESP_OK);worker_context=false;
    assert(read_pico_connectivity_stop_transfer()==ESP_OK);step();assert(status().last_error==ESP_ERR_NO_MEM&&c_import_pending&&status().saved_count==8);
    assert(read_pico_connectivity_forget("saved-4")==ESP_OK);step();assert(status().saved_count==7);
    assert(read_pico_connectivity_connect_saved("ninth")==ESP_OK);step();assert(!c_import_pending&&status().saved_count==8&&!strcmp(status().ssid,"ninth"));
    assert(read_pico_connectivity_quiesce(500)==ESP_OK);
    transfer_live=true;transfer_status.mode=READ_PICO_TRANSFER_MODE_AP;transfer_status.state=READ_PICO_TRANSFER_READY;
    fail_join=1;assert(read_pico_connectivity_resume()==ESP_OK);step();assert(status().suspended&&status().transfer_active&&status().last_error==ESP_FAIL);
    fail_join=0;assert(read_pico_connectivity_stop_transfer()==ESP_OK);step();assert(status().suspended&&!status().transfer_active&&!driver_live);
    assert(read_pico_connectivity_resume()==ESP_OK);step();assert(!status().suspended&&!status().transfer_active&&!transfer_live);
    assert(read_pico_connectivity_quiesce(500)==ESP_OK);
    transfer_live=true;transfer_status.mode=READ_PICO_TRANSFER_MODE_AP;transfer_status.state=READ_PICO_TRANSFER_READY;
    fail_join=1;assert(read_pico_connectivity_resume()==ESP_OK);step();assert(status().suspended&&status().transfer_active&&status().last_error==ESP_FAIL);
    fail_join=0;assert(read_pico_connectivity_resume()==ESP_OK);step();assert(!status().suspended&&!status().transfer_active&&!transfer_live);
    assert(read_pico_connectivity_quiesce(500)==ESP_OK);
}

static void test_resume_retry(void) {
    assert(status().suspended);
    assert(read_pico_connectivity_resume()==ESP_OK);step();
    assert(read_pico_connectivity_set_enabled(true)==ESP_OK);step();
    assert(read_pico_connectivity_quiesce(500)==ESP_OK);
    fail_wifi_init=1;assert(read_pico_connectivity_resume()==ESP_OK);step();
    assert(!status().suspended&&status().last_error==ESP_ERR_NO_MEM&&c_resume_retry&&!driver_live);
    read_pico_connectivity_resume_status_t small;read_pico_connectivity_get_resume_status(&small);
    assert(!small.suspended&&!small.busy&&small.last_error==ESP_ERR_NO_MEM&&small.completed_seq==small.submitted_seq);
    fail_wifi_init=0;assert(read_pico_connectivity_resume()==ESP_OK);step();
    assert(!status().suspended&&status().last_error==ESP_OK&&!c_resume_retry&&driver_live&&radio_live);
    assert(read_pico_connectivity_quiesce(500)==ESP_OK);
}

int main(void) {
    test_normal_and_scan();test_transfer_quiesce();test_forget_legacy();test_failure_and_queue();test_bounded_auto();test_resume_retry();
    assert(!handles[0]&&!handles[1]&&!c_queue->count);
    for(int connected=0;connected<2;connected++)for(int suspended=0;suspended<2;suspended++){
        c_status.connected=connected;c_status.suspended=suspended;
        assert(read_pico_connectivity_online()==(connected&&!suspended));
        c_status.saved_count=0;c_status.busy=true;
        snprintf(c_status.ssid,sizeof(c_status.ssid),"Current LAN");
        read_pico_connectivity_link_t link;
        memset(&link,0xa5,sizeof(link));read_pico_connectivity_get_link(&link);
        assert(link.online==(connected&&!suspended)&&!strcmp(link.ssid,"Current LAN"));
    }
    read_pico_connectivity_get_link(NULL);
    vQueueDelete(c_queue);c_queue=NULL;clear_secret(&c_profiles,sizeof(c_profiles));
    puts("PASS: actual async WiFi worker, copied commands, DHCP/scan/timeout, profiles, transfer ownership/join failure, quiesce and stale enqueue cancellation; SDK/NVS doubles, no hardware acceptance.");return 0;
}

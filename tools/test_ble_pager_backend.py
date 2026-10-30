#!/usr/bin/env python3
"""中文：执行真实 BLE 回调/队列分支；仅替换 NimBLE、NVS 与 FreeRTOS 边界。
English: Exercise actual BLE callbacks/queues with NimBLE, NVS and FreeRTOS boundary doubles.
"""
from pathlib import Path
import argparse
import re
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
SOURCE = (ROOT / "components/read_pico_ble_pager/read_pico_ble_pager.c").read_text()
HEADER = (ROOT / "components/read_pico_ble_pager/include/read_pico_ble_pager.h").read_text()


def function(name):
    match = re.search(r"(?:^|\n)((?:static\s+)?[\w\s*]+?\b" + re.escape(name) + r"\([^;]*?\)\s*\{)", SOURCE)
    if not match:
        raise RuntimeError(name)
    start, at, depth, quote = match.start(1), match.end() - 1, 0, None
    while at < len(SOURCE):
        c = SOURCE[at]
        if quote:
            if c == "\\":
                at += 2
                continue
            if c == quote:
                quote = None
        elif SOURCE.startswith("//", at):
            at = SOURCE.index("\n", at)
            continue
        elif SOURCE.startswith("/*", at):
            at = SOURCE.index("*/", at) + 2
            continue
        elif c in "\"'":
            quote = c
        elif c == "{":
            depth += 1
        elif c == "}":
            depth -= 1
            if not depth:
                return SOURCE[start:at + 1]
        at += 1
    raise RuntimeError("unbalanced " + name)


prelude = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdatomic.h>
#include "pager_hid.h"
typedef int esp_err_t;
enum {ESP_OK=0,ESP_FAIL=-1,ESP_ERR_INVALID_STATE=100,ESP_ERR_NOT_SUPPORTED=101,
      ESP_ERR_INVALID_SIZE=102,ESP_ERR_NO_MEM=103,ESP_ERR_TIMEOUT=104,
      BLE_HS_CONN_HANDLE_NONE=0xffff,BLE_HS_EDONE=200,BLE_HS_EAPP=201,
      BLE_HS_EALREADY=202,BLE_HS_ENOTSYNCED=203,BLE_ERR_REM_USER_CONN_TERM=0x13,
      BLE_ATT_ERR_INVALID_OFFSET=0x07,
      BLE_GATT_CHR_PROP_NOTIFY=0x10,BLE_GAP_REPEAT_PAIRING_IGNORE=2,
      BLE_GAP_EVENT_DISC=1,BLE_GAP_EVENT_DISC_COMPLETE,BLE_GAP_EVENT_CONNECT,
      BLE_GAP_EVENT_ENC_CHANGE,BLE_GAP_EVENT_DISCONNECT,BLE_GAP_EVENT_NOTIFY_RX,
      BLE_GAP_EVENT_REPEAT_PAIRING,BLE_GAP_EVENT_PASSKEY_ACTION,NVS_READWRITE=1,
      pdTRUE=1,pdFALSE=0};
typedef int portMUX_TYPE;
#define portMUX_INITIALIZER_UNLOCKED 0
#define portENTER_CRITICAL(x) ((void)(x))
#define portEXIT_CRITICAL(x) ((void)(x))
#define ESP_LOGI(...) ((void)0)
#define ESP_LOGW(...) ((void)0)
#define BLE_HS_ATT_ERR(code) (0x100+(code))
typedef void *QueueHandle_t;
typedef int nvs_handle_t;
typedef struct {uint8_t type,val[6];} ble_addr_t;
struct ble_npl_event {int unused;};
struct ble_npl_callout {int unused;};
struct os_mbuf {uint8_t *data;size_t length;};
#define OS_MBUF_PKTLEN(om) ((om)->length)
struct ble_gap_disc_desc {int unused;};
struct ble_gap_conn_desc {ble_addr_t peer_id_addr;uint16_t conn_handle;};
struct ble_gap_disc_params {int itvl,window,filter_duplicates,passive;};
struct ble_gap_event {
 int type;
 struct ble_gap_disc_desc disc;
 struct {int status;uint16_t conn_handle;} connect,enc_change;
 struct {struct ble_gap_conn_desc conn;} disconnect;
 struct {uint16_t conn_handle,attr_handle;struct os_mbuf *om;} notify_rx;
};
struct ble_gatt_error {int status;};
struct ble_gatt_attr {uint16_t offset;struct os_mbuf *om;};
struct ble_gatt_chr {uint16_t def_handle,val_handle;uint8_t properties;struct {struct {uint16_t value;} u;}uuid;};
'''

stubs = r'''
static unsigned checks,terminations,securities,discoveries,next_reports,read_longs,writes,
                bond_clears,peer_saves,marker_erases,marker_sets,conn_attempts,cancels,timer_resets;
static int64_t fake_now=10000000;
static int read_rc,write_rc,clear_rc,connect_rc,security_rc,ensure_rc,infer_rc;
static uint16_t terminated_handle,last_protocol;
static bool marker;
static void *last_generation;
static uint8_t last_protocol_value;
static struct {page_event_t events[PAGER_EVENT_COUNT];unsigned count;} queue;
#define CHECK(x) do {checks++;assert(x);}while(0)
static int64_t esp_timer_get_time(void){return fake_now;}
static int xQueueReceive(QueueHandle_t h,void *out,unsigned ticks){
 (void)ticks;CHECK(h==&queue);if(!queue.count)return pdFALSE;
 memcpy(out,&queue.events[0],sizeof(page_event_t));
 memmove(queue.events,queue.events+1,--queue.count*sizeof(page_event_t));return pdTRUE;
}
static int xQueueSend(QueueHandle_t h,const void *in,unsigned ticks){
 (void)ticks;CHECK(h==&queue);if(queue.count==PAGER_EVENT_COUNT)return pdFALSE;
 memcpy(&queue.events[queue.count++],in,sizeof(page_event_t));return pdTRUE;
}
static int nvs_open(const char *name,int mode,nvs_handle_t *nvs){CHECK(!strcmp(name,"pico_ble"));CHECK(mode==NVS_READWRITE);*nvs=1;return ESP_OK;}
static int nvs_set_blob(nvs_handle_t n,const char *key,const void *data,size_t size){(void)n;(void)data;CHECK(!strcmp(key,"peer"));CHECK(size==sizeof(s_remembered));peer_saves++;return 0;}
static int nvs_set_u8(nvs_handle_t n,const char *key,uint8_t value){(void)n;if(!strcmp(key,"forget_bond")){marker=value;marker_sets++;}return 0;}
static int nvs_erase_key(nvs_handle_t n,const char *key){(void)n;if(!strcmp(key,"forget_bond")){marker=false;marker_erases++;}return 0;}
static int nvs_commit(nvs_handle_t n){(void)n;return 0;}
static void nvs_close(nvs_handle_t n){(void)n;}
static int ble_gap_terminate(uint16_t conn,int reason){CHECK(reason==BLE_ERR_REM_USER_CONN_TERM);terminations++;terminated_handle=conn;return 0;}
static int ble_gap_disc_cancel(void){return 0;}
static int ble_gap_conn_cancel(void){cancels++;return 0;}
static int ble_gap_security_initiate(uint16_t conn){CHECK(conn==s_connection);securities++;return security_rc;}
static int ble_gap_conn_find(uint16_t conn,struct ble_gap_conn_desc *desc){CHECK(conn==s_connection);memset(desc,0,sizeof(*desc));desc->peer_id_addr.val[0]=42;return 0;}
static int ble_store_clear(void){bond_clears++;return clear_rc;}
static int ble_hs_util_ensure_addr(int arg){CHECK(!arg);return ensure_rc;}
static int ble_hs_id_infer_auto(int arg,uint8_t *type){CHECK(!arg);*type=0;return infer_rc;}
static int ble_gap_connect(uint8_t type,const ble_addr_t *addr,int timeout,void *params,int (*cb)(struct ble_gap_event*,void*),void *arg){
 (void)type;(void)addr;(void)params;CHECK(timeout==10000);CHECK(cb==gap_event);last_generation=arg;conn_attempts++;return connect_rc;
}
static int ble_gap_disc(uint8_t type,int duration,const struct ble_gap_disc_params *params,int(*cb)(struct ble_gap_event*,void*),void *arg){(void)type;(void)params;(void)cb;(void)arg;CHECK(duration==8000);return 0;}
static esp_err_t start_stack(void){s_stack_started=true;return 0;}
static void stop_stack(void){disconnect_now();}
static int ble_npl_time_ms_to_ticks32(int n){return n;}
static void ble_npl_callout_reset(struct ble_npl_callout *timer,int ticks){CHECK(timer==&s_poll_timer);CHECK(ticks>0&&ticks<=1000);timer_resets++;}
static int os_mbuf_copydata(struct os_mbuf *om,size_t offset,size_t size,void *dst){if(offset+size>om->length)return -1;memcpy(dst,om->data+offset,size);return 0;}
static uint16_t ble_uuid_u16(const void *uuid){return *(const uint16_t*)uuid;}
static void begin_discovery(void){discoveries++;}
static void next_report(void){next_reports++;}
static void scan_result(const struct ble_gap_disc_desc *disc){(void)disc;}
static int ble_gattc_write_no_rsp_flat(uint16_t conn,uint16_t handle,const void *data,size_t size){
 CHECK(conn==s_connection);CHECK(size==1);writes++;last_protocol=handle;last_protocol_value=*(const uint8_t*)data;return write_rc;
}
static int ble_gattc_read_long(uint16_t conn,uint16_t handle,int offset,int(*cb)(uint16_t,const struct ble_gatt_error*,struct ble_gatt_attr*,void*),void *arg){
 (void)arg;CHECK(conn==s_connection);CHECK(handle==s_map_handle);CHECK(!offset);CHECK(cb==map_cb);read_longs++;return read_rc;
}
'''

tests = r'''
static void reset(void){
 memset(&s_status,0,sizeof(s_status));memset(&s_peer,0,sizeof(s_peer));memset(&s_remembered,0,sizeof(s_remembered));
 memset(&s_hid,0,sizeof(s_hid));memset(s_reports,0,sizeof(s_reports));memset(&queue,0,sizeof(queue));
 s_status.enabled=true;s_status.ready=true;s_stack_started=true;s_want_connection=true;s_auto_reconnect=false;
 s_connection=BLE_HS_CONN_HANDLE_NONE;s_connect_generation=7;s_events=&queue;s_forget_bond=false;
 s_map_length=0;s_protocol_handle=20;s_map_handle=0;s_service_end=100;s_report_count=s_report_cursor=s_boot_held=s_reconnect_attempts=0;
 fake_now=10000000;read_rc=write_rc=clear_rc=connect_rc=security_rc=ensure_rc=infer_rc=0;
 terminations=securities=discoveries=next_reports=read_longs=writes=bond_clears=peer_saves=marker_erases=marker_sets=conn_attempts=cancels=timer_resets=0;
 marker=false;
}
static void stale_connect_test(void){
 for(int forget=0;forget<2;forget++){
  reset();s_status.connecting=true;s_status.remembered=true;
  command_t cmd={.kind=forget?CMD_FORGET:CMD_DISCONNECT};handle_command(&cmd);
  CHECK(!s_want_connection&&!s_auto_reconnect&&!s_status.connected);
  struct ble_gap_event e={.type=BLE_GAP_EVENT_CONNECT,.connect={.status=0,.conn_handle=3}};
  gap_event(&e,(void*)s_connect_generation);
  CHECK(terminations==1&&terminated_handle==3&&!securities&&s_connection==BLE_HS_CONN_HANDLE_NONE);
  s_connection=3;s_report_count=1;s_reports[0].subscribed=true;connection_ready();
  CHECK(!s_status.connected&&!peer_saves);
  e=(struct ble_gap_event){.type=BLE_GAP_EVENT_ENC_CHANGE,.enc_change={.status=0,.conn_handle=3}};
  gap_event(&e,(void*)s_connect_generation);CHECK(!discoveries);
 }
 reset();struct ble_gap_event e={.type=BLE_GAP_EVENT_CONNECT,.connect={.status=0,.conn_handle=4}};
 gap_event(&e,(void*)(s_connect_generation-1));CHECK(terminations==1&&!securities);
 gap_event(&e,(void*)s_connect_generation);CHECK(s_connection==4&&securities==1);
 s_report_count=1;s_reports[0].subscribed=true;connection_ready();CHECK(s_status.connected&&peer_saves==1&&s_status.remembered);
}
static void deferred_forget_test(void){
 reset();s_status.ready=false;s_status.remembered=true;
 command_t cmd={.kind=CMD_FORGET};handle_command(&cmd);
 CHECK(marker&&s_forget_bond&&!s_status.remembered&&!bond_clears);
 clear_rc=17;host_synced();CHECK(bond_clears==1&&marker&&s_forget_bond&&s_status.error==17);
 clear_rc=0;host_synced();CHECK(bond_clears==2&&!marker&&!s_forget_bond&&marker_erases==1);
 reset();s_forget_bond=true;marker=true;ensure_rc=19;host_synced();CHECK(!bond_clears&&marker&&!s_status.ready);
}
static void boot_fallback_test(void){
 reset();s_connection=1;s_report_count=1;s_reports[0]=(input_report_t){.value=10,.boot=true};
 struct ble_gatt_error done={BLE_HS_EDONE};
 characteristic_cb(1,&done,NULL,NULL);CHECK(writes==1&&last_protocol==20&&!last_protocol_value&&next_reports==1);
 reset();s_connection=1;s_report_count=1;s_map_handle=12;read_rc=23;
 characteristic_cb(1,&done,NULL,NULL);CHECK(read_longs==1&&writes==1&&next_reports==1);
 reset();s_connection=1;s_report_count=1;map_cb(1,&done,NULL,NULL);CHECK(writes==1&&next_reports==1);
 reset();s_connection=1;s_report_count=1;s_protocol_handle=0;begin_reports();CHECK(!next_reports&&!s_want_connection&&s_status.error==ESP_ERR_NOT_SUPPORTED);
 reset();s_connection=1;s_hid.field_count=1;begin_reports();CHECK(!writes&&next_reports==1);
 reset();s_connection=1;write_rc=24;begin_reports();CHECK(writes==1&&!next_reports&&!s_want_connection&&s_status.error==24);
}
static void queue_test(void){
 reset();s_connection=1;s_status.connected=true;s_report_count=1;
 s_reports[0]=(input_report_t){.value=5,.boot=true,.subscribed=true};
 uint8_t key[8]={0,0,0x4e};struct os_mbuf om={key,8};
 struct ble_gap_event e={.type=BLE_GAP_EVENT_NOTIFY_RX,.notify_rx={1,5,&om}};
 gap_event(&e,(void*)s_connect_generation);CHECK(queue.count==1);CHECK(read_pico_ble_pager_poll_event()==1);
 gap_event(&e,(void*)s_connect_generation);CHECK(!queue.count);
 key[2]=0;gap_event(&e,(void*)s_connect_generation);key[2]=0x4b;
 gap_event(&e,(void*)s_connect_generation);fake_now+=2000001;CHECK(read_pico_ble_pager_poll_event()==0&&!queue.count);
 page_event_t future={1,fake_now+1},old={-1,fake_now-3000000},fresh={1,fake_now-2000000};
 xQueueSend(s_events,&future,0);xQueueSend(s_events,&old,0);xQueueSend(s_events,&fresh,0);
 CHECK(read_pico_ble_pager_poll_event()==1&&!queue.count);
 s_boot_held=0;s_status.reversed=true;key[2]=0x4e;gap_event(&e,(void*)s_connect_generation);
 CHECK(read_pico_ble_pager_poll_event()==-1);
 s_boot_held=0;gap_event(&e,(void*)(s_connect_generation-1));CHECK(!queue.count);
 for(int i=0;i<PAGER_EVENT_COUNT;i++)CHECK(xQueueSend(s_events,&fresh,0)==pdTRUE);
 CHECK(xQueueSend(s_events,&fresh,0)==pdFALSE);clear_events();CHECK(!queue.count);
}
static void gatt_bounds_test(void){
 reset();s_connection=1;
 struct ble_gatt_error ok={0},done={BLE_HS_EDONE},failed={30};
 struct ble_gatt_chr chr={.def_handle=10,.val_handle=11,.properties=BLE_GATT_CHR_PROP_NOTIFY,.uuid={.u={0x2a4d}}};
 for(int i=0;i<PAGER_MAX_REPORTS;i++){chr.def_handle=(uint16_t)(10+i*4);chr.val_handle=chr.def_handle+1;CHECK(characteristic_cb(1,&ok,&chr,NULL)==0);}
 CHECK(s_report_count==PAGER_MAX_REPORTS);
 CHECK(characteristic_cb(1,&ok,&chr,NULL)==BLE_HS_EAPP&&s_status.error==ESP_ERR_NO_MEM&&!s_want_connection);
 reset();s_connection=1;characteristic_cb(1,&failed,NULL,NULL);CHECK(!s_want_connection&&s_status.error==30);
 begin_reports();CHECK(!next_reports);
 reset();s_connection=1;
 uint8_t map[]={0x05,0x0c,0x09,0x01,0xa1,0x01,0x85,0x03,0x09,0xe9,0x75,0x01,0x95,0x01,0x81,0x02,0xc0};
 struct os_mbuf first={map,8},second={map+8,sizeof(map)-8};
 struct ble_gatt_attr attr={.offset=0,.om=&first};
 CHECK(map_cb(1,&ok,&attr,NULL)==0&&s_map_length==8);
 attr.offset=8;attr.om=&second;
 CHECK(map_cb(1,&ok,&attr,NULL)==0&&s_map_length==sizeof(map));
 map_cb(1,&done,NULL,NULL);CHECK(s_hid.field_count==1&&!writes&&next_reports==1);
 reset();s_connection=1;struct os_mbuf full={map,sizeof(map)};attr.offset=0;attr.om=&full;
 map_cb(1,&ok,&attr,NULL);struct ble_gatt_error eof={BLE_HS_ATT_ERR(BLE_ATT_ERR_INVALID_OFFSET)};
 map_cb(1,&eof,NULL,NULL);CHECK(s_hid.field_count==1&&!writes&&next_reports==1);
 reset();s_connection=1;attr.offset=1;attr.om=&first;
 CHECK(map_cb(1,&ok,&attr,NULL)==BLE_HS_EAPP&&!s_want_connection&&s_status.error==ESP_ERR_INVALID_SIZE);
 reset();s_connection=1;s_map_length=PAGER_MAP_SIZE-1;attr.offset=PAGER_MAP_SIZE-1;attr.om=&first;
 CHECK(map_cb(1,&ok,&attr,NULL)==BLE_HS_EAPP&&s_map_length==PAGER_MAP_SIZE-1&&!s_want_connection);
}
static void reconnect_test(void){
 reset();s_want_connection=false;s_auto_reconnect=true;s_status.remembered=true;s_retry_after=0;
 for(int attempt=0;attempt<3;attempt++){
  poll_event(NULL);CHECK(conn_attempts==(unsigned)attempt+1&&s_want_connection&&s_status.connecting);
  CHECK((uintptr_t)last_generation==s_connect_generation);
  connection_failed(40);fake_now+=16000000;
 }
 poll_event(NULL);CHECK(conn_attempts==3);
 reset();s_connection=1;s_status.connecting=true;s_setup_deadline=fake_now-1;poll_event(NULL);
 CHECK(cancels==1&&terminations==1&&!s_want_connection&&s_status.error==ESP_ERR_TIMEOUT);
}
int main(void){
 stale_connect_test();deferred_forget_test();boot_fallback_test();queue_test();gatt_bounds_test();reconnect_test();
 printf("BLE backend callbacks: %u checks PASS; late connections, deferred forget, boot fallback, event ages and bounded reconnect\n",checks);
 return 0;
}
'''

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--sanitize", action="store_true")
args = parser.parse_args()
header = re.sub(r'^#include "esp_err.h"\s*$|^#pragma once\s*$', '', HEADER, flags=re.M)
declarations = SOURCE[SOURCE.index("#define PAGER_UUID_HID"):SOURCE.index("void ble_store_config_init(void);")]
names = ["bump_locked", "set_error", "clear_events", "remember_save", "connection_failed", "current_connection", "connection_ready", "begin_reports", "map_cb", "characteristic_cb", "gap_event", "host_synced", "disconnect_now", "connect_peer", "handle_command", "poll_event", "read_pico_ble_pager_poll_event"]
prototypes = '\n'.join(function(name).split('{', 1)[0] + ';' for name in names)
source = prelude + '\n' + header + '\n' + declarations + '\n' + prototypes + '\n' + stubs + '\n' + '\n'.join(function(name) for name in names) + '\n' + tests
with tempfile.TemporaryDirectory(prefix="ble-pager-backend-") as temp:
    path = Path(temp)
    code, binary = path / "test.c", path / "test"
    code.write_text(source)
    flags = ["-std=c11", "-Wall", "-Wextra", "-Werror", "-Wno-unused-function", "-Wno-unused-variable", "-g", "-O1"]
    if args.sanitize:
        flags += ["-fsanitize=address,undefined", "-fno-omit-frame-pointer"]
    subprocess.run(["cc", *flags, "-I", str(ROOT / "components/read_pico_ble_pager"), str(code), str(ROOT / "components/read_pico_ble_pager/pager_hid.c"), "-o", str(binary)], check=True)
    subprocess.run([str(binary)], check=True)

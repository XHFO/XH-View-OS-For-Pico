#!/usr/bin/env python3
"""Execute production BLE security, discovery and frame callbacks with host boundary doubles.
This is not a radio/pairing acceptance test.
"""
from pathlib import Path
import argparse, re, subprocess, tempfile
ROOT=Path(__file__).resolve().parents[1]
SOURCE=(ROOT/'components/read_pico_ble_pager/read_pico_dual_ble.c').read_text()
advertisement=SOURCE[SOURCE.index('if(adv_ip[0])'):SOURCE.index('struct ble_gap_adv_params')]
assert 'adv_code' not in SOURCE and 'strtoul(adv_code' not in advertisement
def function(name):
    m=re.search(r'(?:^|\n)((?:static\s+)?[\w\s*]+?\b'+re.escape(name)+r'\([^;]*?\)\s*\{)',SOURCE)
    if not m:raise RuntimeError(name)
    start,at,depth,quote=m.start(1),m.end()-1,0,None
    while at<len(SOURCE):
        c=SOURCE[at]
        if quote:
            if c=='\\':at+=2;continue
            if c==quote:quote=None
        elif SOURCE.startswith('//',at):at=SOURCE.index('\n',at);continue
        elif SOURCE.startswith('/*',at):at=SOURCE.index('*/',at)+2;continue
        elif c in '\"\'':quote=c
        elif c=='{':depth+=1
        elif c=='}':
            depth-=1
            if not depth:return SOURCE[start:at+1]
        at+=1
    raise RuntimeError(name)
prelude=r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#define PICO_DUAL_BLE_PACKET_MAX 96
#define BLE_HS_CONN_HANDLE_NONE 65535
#define NONE BLE_HS_CONN_HANDLE_NONE
#define MTU_MIN 99
#define portENTER_CRITICAL(x) ((void)(x))
#define portEXIT_CRITICAL(x) ((void)(x))
#define OS_MBUF_PKTLEN(p) ((p)->n)
enum {pdTRUE=1,BLE_ATT_ERR_INSUFFICIENT_AUTHEN=5,BLE_GATT_ACCESS_OP_WRITE_CHR=1,
 BLE_GATT_ACCESS_OP_READ_CHR=2,BLE_ATT_ERR_INSUFFICIENT_RES=17,BLE_ATT_ERR_UNLIKELY=14,
 BLE_HS_EDONE=200,BLE_HS_ENOENT=201,BLE_HS_EMSGSIZE=202};
typedef struct {bool active,connected,simulated;uint32_t generation;int error;} pico_dual_ble_status_t;
typedef struct {uint32_t generation;uint8_t size,bytes[96];} packet_t;
typedef struct {int role;char code[7];} config_t;
struct os_mbuf {uint8_t bytes[128];unsigned n;};
struct ble_gap_conn_desc {struct {bool encrypted,authenticated;unsigned key_size;}sec_state;};
struct ble_gatt_access_ctxt {int op;struct os_mbuf*om;};
struct ble_gatt_error {int status;};
struct ble_gatt_attr {int unused;};
struct uuid {int u;};
struct ble_gatt_dsc {struct uuid uuid;uint16_t handle;};
struct ble_gatt_chr {struct uuid uuid;uint16_t val_handle;};
struct ble_gatt_svc {uint16_t start_handle,end_handle;};
static struct uuid rx_uuid={1},tx_uuid={2},service_uuid={3};
static struct {packet_t p[8];unsigned n;} iq,oq;
static void*incoming=&iq,*outgoing=&oq;
static config_t current;
static pico_dual_ble_status_t snapshot;
static int guard,copy_error,conn_error,rejected,write_rc,disc_rc;
static uint16_t connection=NONE,remote_rx,remote_tx,service_start,service_end;
static bool subscribed,connecting,discovering,writing;
static uint32_t epoch;
static int64_t retry_at;
static unsigned mtu=128,checks,writes,disc_start,disc_end;
static struct ble_gap_conn_desc security;
#define CHECK(x) do{++checks;assert(x);}while(0)
static int64_t esp_timer_get_time(void){return 1000000;}
static int ble_gap_conn_find(uint16_t c,struct ble_gap_conn_desc*d){CHECK(c==connection);*d=security;return conn_error;}
static unsigned ble_att_mtu(uint16_t c){CHECK(c==connection);return mtu;}
static void xQueueReset(void*q){if(q==incoming)iq.n=0;else oq.n=0;}
static int xQueueSend(void*q,const void*p,int ticks){CHECK(q==incoming&&!ticks);if(iq.n==8)return 0;iq.p[iq.n++]=*(packet_t*)p;return pdTRUE;}
static int xQueueReceive(void*q,void*p,int ticks){CHECK(!ticks);if(q==incoming){if(!iq.n)return 0;*(packet_t*)p=iq.p[0];memmove(iq.p,iq.p+1,--iq.n*sizeof(packet_t));}else{if(!oq.n)return 0;*(packet_t*)p=oq.p[0];memmove(oq.p,oq.p+1,--oq.n*sizeof(packet_t));}return pdTRUE;}
static int os_mbuf_copydata(const struct os_mbuf*om,int off,unsigned n,void*dst){CHECK(!off&&n<=om->n);memcpy(dst,om->bytes,n);return copy_error;}
static void reject(int error){rejected=error;}
static unsigned ble_uuid_u16(const int*p){return *p;}
static int ble_uuid_cmp(const int*a,const int*b){return *a-*b;}
static int ble_gattc_write_flat(uint16_t c,uint16_t h,const void*p,int n,int(*cb)(uint16_t,const struct ble_gatt_error*,struct ble_gatt_attr*,void*),void*arg){(void)cb;CHECK(c==connection&&h==11&&n==2&&((uint8_t*)p)[0]==1&&((uint8_t*)p)[1]==0&&(uintptr_t)arg==epoch);++writes;return write_rc;}
static int ble_gattc_disc_all_dscs(uint16_t c,uint16_t from,uint16_t to,int(*cb)(uint16_t,const struct ble_gatt_error*,uint16_t,const struct ble_gatt_dsc*,void*),void*arg){(void)cb;CHECK(c==connection&&(uintptr_t)arg==epoch);disc_start=from;disc_end=to;return disc_rc;}
static int ble_gattc_disc_all_chrs(uint16_t c,uint16_t from,uint16_t to,int(*cb)(uint16_t,const struct ble_gatt_error*,const struct ble_gatt_chr*,void*),void*arg){(void)cb;CHECK(c==connection&&(uintptr_t)arg==epoch);disc_start=from;disc_end=to;return disc_rc;}
static int ble_gattc_disc_svc_by_uuid(uint16_t c,const int*uuid,int(*cb)(uint16_t,const struct ble_gatt_error*,const struct ble_gatt_svc*,void*),void*arg){(void)cb;CHECK(c==connection&&*uuid==3&&(uintptr_t)arg==epoch);disc_start=1;return disc_rc;}
'''
tests=r'''
static void reset(void){
 connection=7;epoch=4;security.sec_state.encrypted=security.sec_state.authenticated=true;security.sec_state.key_size=16;
 conn_error=copy_error=rejected=write_rc=disc_rc=0;mtu=128;subscribed=connecting=discovering=writing=false;
 snapshot=(pico_dual_ble_status_t){.active=true,.generation=epoch};iq.n=oq.n=0;writes=disc_start=disc_end=0;
 current.role=2;remote_rx=5;remote_tx=10;service_start=1;service_end=15;
}
static void gates(void){
 reset();CHECK(secure(7)&&!secure(8)&&!secure(NONE));
 struct os_mbuf om={.n=81};struct ble_gatt_access_ctxt ctx={.op=BLE_GATT_ACCESS_OP_WRITE_CHR,.om=&om};
 for(unsigned condition=0;condition<4;condition++){
  reset();if(condition==0)security.sec_state.encrypted=false;if(condition==1)security.sec_state.authenticated=false;
  if(condition==2)security.sec_state.key_size=7;if(condition==3)conn_error=1;
  subscribed=true;ready_update();CHECK(!snapshot.connected&&!receive_packet(7,&om)&&!iq.n);
  CHECK(access_gatt(7,1,&ctx,(void*)1)==BLE_ATT_ERR_INSUFFICIENT_AUTHEN);
 }
 reset();connecting=true;ready_update();CHECK(!snapshot.connected&&connecting);
 subscribed=true;mtu=98;ready_update();CHECK(!snapshot.connected&&connecting);
 mtu=99;ready_update();CHECK(snapshot.connected&&!connecting);
 CHECK(access_gatt(7,1,&ctx,(void*)1)==0&&iq.n==1&&iq.p[0].generation==epoch);
 om.n=97;CHECK(!receive_packet(7,&om));om.n=0;CHECK(!receive_packet(7,&om));om.n=10;
 for(int i=1;i<8;i++)CHECK(receive_packet(7,&om));CHECK(!receive_packet(7,&om));
 uint8_t data[96];CHECK(pico_dual_ble_receive(data,sizeof(data))==81);
 ++snapshot.generation;CHECK(!pico_dual_ble_receive(data,sizeof(data))&&!iq.n);
 reset();copy_error=1;CHECK(!receive_packet(7,&om)&&!iq.n);
 reset();om.n=10;CHECK(receive_packet(7,&om));snapshot.connected=false;CHECK(!pico_dual_ble_receive(data,sizeof(data))&&!iq.n);
}
static void discovery(void){
 reset();void*arg=(void*)(uintptr_t)epoch;struct ble_gatt_error ok={0},done={BLE_HS_EDONE},bad={42};
 CHECK(!callback_current(7,(void*)3)&&!callback_current(8,arg)&&callback_current(7,arg));
 characteristic_cb(7,&done,NULL,(void*)3);CHECK(!disc_start);
 characteristic_cb(7,&done,NULL,arg);CHECK(disc_start==remote_tx&&disc_end==service_end);
 struct ble_gatt_dsc dsc={.uuid.u=0x2902,.handle=11};descriptor_cb(7,&ok,10,&dsc,arg);CHECK(writes==1);
 subscribed_cb(7,&bad,NULL,(void*)3);CHECK(!rejected);
 connecting=true;discovering=true;mtu=98;subscribed_cb(7,&ok,NULL,arg);CHECK(subscribed&&connecting&&!snapshot.connected);
 mtu=99;ready_update();CHECK(snapshot.connected&&!connecting);
 write_rc=37;descriptor_cb(7,&ok,10,&dsc,arg);CHECK(rejected==37);
 reset();mtu_cb(7,&ok,98,arg);CHECK(rejected==BLE_HS_EMSGSIZE&&!disc_start);
 reset();mtu_cb(7,&ok,128,arg);CHECK(disc_start==1&&!rejected);
 reset();service_start=0;service_cb(7,&done,NULL,arg);CHECK(rejected==BLE_HS_EDONE);
 reset();oq.n=1;writing=true;write_cb(7,&ok,NULL,(void*)3);CHECK(writing&&oq.n==1);
 write_cb(7,&ok,NULL,arg);CHECK(!writing&&!oq.n);
 reset();iq.n=oq.n=1;subscribed=connecting=discovering=writing=true;clear_link(55);
 CHECK(connection==NONE&&snapshot.generation==5&&!snapshot.connected&&snapshot.error==55&&!iq.n&&!oq.n);
}
int main(void){gates();discovery();printf("Dual BLE callbacks: %u checks PASS; authentication, MTU, discovery handles, stale callbacks, bounded queues and generations (no physical radio)\n",checks);}
'''
names=['error_set','secure','ready_update','clear_link','receive_packet','access_gatt','callback_current','subscribed_cb','descriptor_cb','characteristic_cb','service_cb','mtu_cb','write_cb','pico_dual_ble_status','pico_dual_ble_receive']
prototypes='\n'.join(function(n).split('{',1)[0]+';' for n in names)
source=prelude+'\n'+prototypes+'\n'+'\n'.join(function(n) for n in names)+'\n'+tests
p=argparse.ArgumentParser();p.add_argument('--sanitize',action='store_true');a=p.parse_args()
with tempfile.TemporaryDirectory(prefix='dual-ble-backend-') as temp:
    path=Path(temp);(path/'test.c').write_text(source)
    flags=['-std=c11','-Wall','-Wextra','-Werror','-Wno-unused-function','-g','-O1']
    if a.sanitize:flags+=['-fsanitize=address,undefined','-fno-omit-frame-pointer']
    subprocess.run(['cc',*flags,str(path/'test.c'),'-o',str(path/'test')],check=True)
    subprocess.run([str(path/'test')],check=True)

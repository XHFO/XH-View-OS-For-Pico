#!/usr/bin/env python3
"""Execute production turn/display callbacks with the real protocol and a bounded BLE boundary."""
from pathlib import Path
import argparse
import re
import subprocess
import tempfile

root=Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser();p.add_argument('--source',type=Path,default=root/'main/system/pico_dual.c');a=p.parse_args()

def function(source,name):
    m=re.search(r'(?:^|\n)((?:static\s+)?[\w\s*]+?\b'+name+r'\([^;]*?\)\s*\{)',source)
    start,at,depth,quote=m.start(1),m.end()-1,0,None
    while at<len(source):
        c=source[at]
        if quote:
            if c=='\\':at+=2;continue
            if c==quote:quote=None
        elif source.startswith('//',at):at=source.index('\n',at);continue
        elif source.startswith('/*',at):at=source.index('*/',at)+2;continue
        elif c in '\"\'':quote=c
        elif c=='{':depth+=1
        elif c=='}':
            depth-=1
            if not depth:return source[start:at+1]
        at+=1
    raise RuntimeError(name)

prelude=r'''
#include "LINK_SOURCE"
#include <assert.h>
static dual_link_t* dual_session;
static struct {int role;}settings;
static uint32_t applied,presented;
static dl_status_t status;
static bool enabled=true,space=true;
static unsigned sends,toasts;
static uint8_t queued[96];static size_t queued_n;
static bool ps_dual_enabled(void){return enabled;}
static void ps_toast(const char* s){(void)s;++toasts;}
static bool pico_dual_ble_can_send(void){return space;}
static size_t ps_home_pga_share_take(uint8_t* p,size_t cap){(void)p;(void)cap;return 0;}
static bool pico_dual_ble_send(const uint8_t* p,size_t n){assert(space&&n<=96);memcpy(queued,p,n);queued_n=n;++sends;return true;}
#define ESP_OK 0
typedef struct {uint32_t file_size,byte_off;uint16_t chapter;uint8_t px,pct;} book_progress_t;
static book_progress_t durable;
static bool durable_known,store_fail;
static unsigned store_calls;
static bool book_progress_load(const char* path,uint32_t bytes,book_progress_t* out){
 (void)path;if(!durable_known||bytes!=durable.file_size)return false;*out=durable;return true;
}
static bool book_progress_ahead(const book_progress_t* a,const book_progress_t* b){
 return a->chapter>b->chapter||(a->chapter==b->chapter&&a->byte_off>b->byte_off);
}
static int book_progress_save_mode(const char* path,const book_progress_t* p,bool paired){
 (void)path;assert(paired);++store_calls;if(store_fail)return -1;durable=*p;durable_known=true;return ESP_OK;
}
'''.replace('LINK_SOURCE',str(root/'main/system/dual_link.c'))
tests=r'''
static void fresh(int role){
 free(dual_session);dual_session=calloc(1,sizeof(*dual_session));assert(dual_session);
 dual_link_t* d=dual_session;d->cfg.role=settings.role=role;
 d->control_connected=d->control_compatible=d->catalog_valid=d->peer_catalog_valid=true;
 d->reading_active=d->peer_reading=d->left_ready=true;d->display_flags=6;
 d->count=1;d->local[0].seen=true;d->local[0].id[0]=d->reading.book[0]=1;
 d->reading.revision=1;d->reading.font_px=48;d->now=d->control_sent=1000;
 sends=toasts=applied=presented=0;space=enabled=true;
}
int main(void){
 dl_book_progress_t progress={.started=true,.chapter=2,.byte_off=100,.px=48,.pct=30};
 store_fail=true;assert(!dual_progress_set("book",1234,&progress,NULL)&&store_calls==1&&!durable_known);
 store_fail=false;assert(dual_progress_set("book",1234,&progress,NULL)&&store_calls==2&&durable_known);
 assert(dual_progress_set("book",1234,&progress,NULL)&&store_calls==2);
 progress.byte_off=90;assert(dual_progress_set("book",1234,&progress,NULL)&&store_calls==2&&durable.byte_off==100);
 progress.byte_off=110;store_fail=true;
 assert(!dual_progress_set("book",1234,&progress,NULL)&&store_calls==3&&durable.byte_off==100);
 fresh(1);assert(ps_dual_turn(1)&&sends==1&&queued_n==89&&queued[0]==2&&get32(queued+37)==2&&!queued[81]);
 applied=dual_session->reading.revision;
 // 确认回调返回前，最终状态必须已交给独立BLE线程，而非等动画结束。
 // The ready snapshot must reach the BLE worker before returning to the blocking animation.
 assert(ack_queue(dual_session,15,1));ps_dual_presented(3);
 assert(sends==2&&queued_n==89&&queued[0]==2&&queued[81]&&presented==applied);
 ps_dual_presented(4);assert(sends==2);
 fresh(1);space=false;assert(ps_dual_turn(1)&&!sends&&dual_session->state_dirty);
 space=true;control_send();assert(sends==1&&queued[0]==2);
 fresh(2);assert(ps_dual_turn(1)&&sends==1&&queued_n==10&&queued[0]==4);
 // Relay enqueues immediately for either lead role, without marking actual completion.
 fresh(1);dual_session->display_flags=0;applied=1;dl_status(dual_session,&status);
 ps_dual_relay_ready(1);assert(sends==1&&queued_n==89&&queued[81]==9&&presented==0);
 ps_dual_relay_ready(2);assert(sends==1); // wrong page
 applied=0;ps_dual_relay_ready(1);assert(sends==1); // stale revision
 fresh(2);dual_session->display_flags=0;applied=1;dl_status(dual_session,&status);
 ps_dual_relay_ready(2);assert(sends==1&&queued_n==5&&queued[0]==36&&get32(queued+1)==1&&presented==0);
 fresh(2);dual_session->display_flags=0;applied=1;dl_status(dual_session,&status);space=false;
 ps_dual_relay_ready(2);assert(!sends&&dual_session->display_pending);
 space=true;control_send();assert(sends==1&&queued[0]==36);
 fresh(1);dual_session->control_connected=false;assert(!ps_dual_turn(1)&&!sends&&toasts==1);
 free(dual_session);puts("PASS: primary/secondary input, relay and completion callbacks enqueue before painting; full queues retain state; stale pages and offline input rejected");
}
'''
source=a.source.read_text();current=(root/'main/system/pico_dual.c').read_text()
functions=[function(current,'control_send'),function(source,'dual_progress_set'),function(source,'ps_dual_turn'),function(source,'ps_dual_presented'),function(source,'ps_dual_relay_ready')]
with tempfile.TemporaryDirectory(prefix='dual-control-flush-') as folder:
    path=Path(folder);(path/'test.c').write_text(prelude+'\n'+'\n'.join(functions)+'\n'+tests)
    subprocess.run(['cc','-std=gnu11','-g','-O1','-fsanitize=address,undefined','-fno-omit-frame-pointer',str(path/'test.c'),'-o',str(path/'test')],check=True)
    subprocess.run([str(path/'test')],check=True)

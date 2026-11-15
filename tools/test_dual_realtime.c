/* SPDX-License-Identifier: Apache-2.0
 * 控制优先级与丢包/写卡失败回归，不替代射频验收。
 * Control priority and packet-loss/storage-failure regression, not radio acceptance.
 */
#include "../main/system/dual_link.c"
#include <assert.h>
static dl_book_progress_t saved;
static dl_book_progress_t live;
static unsigned reads, writes;
static bool fail_save;
static bool get_progress(const char* path,uint32_t bytes,dl_book_progress_t* out,void* ctx){
    (void)path;(void)bytes;(void)ctx;++reads;*out=progress_ahead(&live,&saved)?live:saved;return out->started;
}
static bool set_progress(const char* path,uint32_t bytes,const dl_book_progress_t* in,void* ctx){
    (void)path;(void)bytes;(void)ctx;if(!progress_ahead(in,&saved))return true;if(fail_save)return false;++writes;saved=*in;return true;
}
static dual_link_t* fresh(int role){
    dual_link_t* d=calloc(1,sizeof(*d));assert(d);
    d->cfg.role=role;d->cfg.progress_get=get_progress;d->cfg.progress_set=set_progress;
    d->control_connected=d->control_compatible=d->catalog_valid=d->peer_catalog_valid=true;
    d->count=1;d->local[0].seen=true;d->local[0].id[0]=1;d->local[0].bytes=1234;
    strcpy(d->local[0].path,"/fixture/book.txt");d->reading.book[0]=1;d->reading.revision=42;d->reading.font_px=48;
    d->reading_active=d->peer_reading=true;d->now=d->control_sent=1000;d->progress_sync_at=1000;
    return d;
}
static void realtime_priority(void){
    dual_link_t* d=fresh(1);uint8_t p[DL_CONTROL_MAX];
    d->state_dirty=d->orientation_dirty=true;d->orientation_revision=1;
    d->annotation_count=1;d->annotations[0].seq=1;strcpy(d->annotations[0].text,"pending note");
    assert(dl_control_take(d,p,sizeof(p))==89&&p[0]==2);
    free(d);d=fresh(2);d->request_pending=true;d->request_sequence=1;d->request_direction=1;
    d->orientation_dirty=true;d->orientation_request=1;d->orientation_sequence=1;
    assert(dl_control_take(d,p,sizeof(p))==10&&p[0]==4);
    free(d);puts("PASS: reading snapshots and requests precede background work");
}
static void rendered_progress(void){
    dual_link_t* d=fresh(1);uint8_t p[DL_CONTROL_MAX];
    saved=(dl_book_progress_t){.started=true,.px=48,.chapter=2,.byte_off=900,.pct=32};
    d->current_progress_dirty=false;
    assert(dl_presented(d,42)&&d->current_progress_dirty);
    assert(dl_control_take(d,p,sizeof(p))==89&&p[0]==2&&p[81]);
    assert(dl_control_take(d,p,sizeof(p))==50&&p[0]==PROGRESS&&get32(p+41)==900&&get32(p+46));
    uint8_t original[50];memcpy(original,p,50);
    // 丢失数据或确认时重试同一序号，不能丢掉当前书的记录。
    // Lost data or ACK retries the same sequence, retaining the current book record.
    d->now+=250;assert(!dl_control_take(d,p,sizeof(p)));
    d->now+=251;assert(dl_control_take(d,p,sizeof(p))==50&&!memcmp(original,p,50));
    uint8_t ack[6]={ACK,PROGRESS};put32(ack+2,get32(original+46)+1);
    assert(dl_control_receive(d,ack,6));d->now+=501;
    assert(dl_control_take(d,p,sizeof(p))==50&&!memcmp(original,p,50));
    put32(ack+2,get32(original+46));assert(dl_control_receive(d,ack,6));
    assert(!dl_control_take(d,p,sizeof(p)));
    free(d);puts("PASS: rendered anchor sent immediately, lost ACK/data retried, stale ACK ignored");
}
static void quiet_catalog(void){
    dual_link_t* d=fresh(1);uint8_t p[DL_CONTROL_MAX];d->progress_dirty=true;reads=0;
    assert(!dl_control_take(d,p,sizeof(p))&&reads==0);
    d->reading_active=false;assert(!dl_control_take(d,p,sizeof(p))&&reads==0);
    d->peer_reading=false;assert(dl_control_take(d,p,sizeof(p))==50&&reads==1);
    free(d);puts("PASS: full progress catalog pauses while either reader is active");
}
static void durable_ack(void){
    dual_link_t* d=fresh(2);uint8_t p[50]={PROGRESS},ack[DL_CONTROL_MAX];
    p[1]=1;p[33]=1;p[34]=50;put16(p+39,3);put32(p+41,1200);p[45]=48;put32(p+46,7);
    saved=(dl_book_progress_t){0};live=(dl_book_progress_t){.started=true,.chapter=3,.byte_off=1200,.px=48};writes=0;fail_save=true;
    assert(!dl_control_receive(d,p,50));assert(!dl_control_take(d,ack,sizeof(ack))&&!writes);
    fail_save=false;assert(dl_control_receive(d,p,50)&&writes==1);
    assert(dl_control_take(d,ack,sizeof(ack))==6&&ack[0]==ACK&&ack[1]==PROGRESS&&get32(ack+2)==7);
    assert(dl_control_receive(d,p,50)&&writes==1);
    assert(dl_control_take(d,ack,sizeof(ack))==6&&get32(ack+2)==7);
    assert(!dl_control_receive(d,p,46));
    live=(dl_book_progress_t){0};
    free(d);puts("PASS: progress ACK follows durable save, failed saves retry, duplicate packets do not rewrite");
}
static void version_guard(void){
    dual_link_t* d=fresh(1);uint8_t p[43]={1,1};p[34]=7;
    assert(!dl_control_receive(d,p,43)&&d->error&&!control_ready(d));
    p[34]=CONTROL_VERSION;assert(dl_control_receive(d,p,43)&&!d->error&&control_ready(d));
    free(d);puts("PASS: mixed protocol versions report an error, compatible rejoin clears it");
}
int main(void){realtime_priority();rendered_progress();quiet_catalog();durable_ack();version_guard();return 0;}

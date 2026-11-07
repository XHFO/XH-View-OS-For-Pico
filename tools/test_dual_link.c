/* SPDX-License-Identifier: Apache-2.0
 * 真实回环TCP和文件系统；不替代设备验收。/ Real loopback TCP and filesystem, not hardware acceptance.
 */
#include "../main/system/dual_link.c"
#include <assert.h>
static int64_t clock_ms;
static bool radio = true, wifi = true, auto_present = true;
static unsigned tick_step=2, reading_frames, orientation_frames;
static void pump(dual_link_t *a,dual_link_t *b,int count){
 for(int i=0;i<count;i++){
  clock_ms+=tick_step;dl_tick(a,clock_ms,wifi);dl_tick(b,clock_ms,wifi);
  bool paired=radio&&!strcmp(a->cfg.code,b->cfg.code);
  dl_control_connected(a,paired);dl_control_connected(b,paired);
  if(auto_present&&nonzero(a->reading.book))assert(dl_presented(a,a->reading.revision));
  uint8_t p[DL_CONTROL_MAX];size_t n=dl_control_take(a,p,sizeof(p));
  if(n){reading_frames+=p[0]==2;orientation_frames+=p[0]==7;assert(dl_control_receive(b,p,n));}
  n=dl_control_take(b,p,sizeof(p));if(n)assert(dl_control_receive(a,p,n));
  usleep(100);
 }
}
static void write_book(const char *p,unsigned seed,unsigned bytes){FILE *f=fopen(p,"wb");assert(f);for(unsigned i=0;i<bytes;i++)fputc('a'+(i+seed)%26,f);assert(!fclose(f));}
static void synchronized(dual_link_t *a,dual_link_t *b){
    dl_status_t x,y;for(int i=0;i<60000;i++){pump(a,b,1);dl_status(a,&x);dl_status(b,&y);if(x.synced&&y.synced)return;}
    fprintf(stderr,"sync failed A=%s B=%s scan=%d/%d auth=%d/%d listed=%d/%d local=%u/%u remote=%u/%u req=%d/%d\n",a->error?a->error:"",b->error?b->error:"",a->scan,b->scan,a->authenticated,b->authenticated,a->peer_listed,b->peer_listed,a->count,b->count,a->remote_count,b->remote_count,a->request_index,b->request_index);abort();
}
static void scan_complete(dual_link_t *d){
    for(unsigned i=0;d->scan&&i<60000;i++)dl_tick(d,++clock_ms,false);
    assert(!d->scan&&d->retry_at!=INT64_MAX&&d->catalog_valid);
}
static void cache_test(const char *root){
    dl_config_t cfg={.role=1,.port=23772,.code="123456",.root_count=1};
    snprintf(cfg.destination,DL_PATH,"%s/cache",root);assert(!mkdir(cfg.destination,0700));strcpy(cfg.roots[0],cfg.destination);
    char x[DL_PATH+32],y[DL_PATH+32],z[DL_PATH+32];
    snprintf(x,sizeof(x),"%s/x.txt",cfg.destination);snprintf(y,sizeof(y),"%s/y.txt",cfg.destination);snprintf(z,sizeof(z),"%s/z.txt",cfg.destination);
    write_book(x,1,17001);write_book(y,2,23003);
    dual_link_t *d=dl_create(&cfg);assert(d);scan_complete(d);assert(d->count==2&&d->verified_bytes==40004);
    uint8_t before[32];memcpy(before,d->catalog,32);
    dl_rescan(d);assert(!dl_entry(d,0,NULL,NULL));scan_complete(d);assert(!d->verified_bytes&&d->reused==2&&same(before,d->catalog,32));
    // chmod 只改 ctime：使缓存失效，但不打断完整扫描或发送；保存扫描结束时的元数据。
    // chmod changes only ctime: invalidate cache without rejecting a full scan or send; retain the final metadata.
    assert(!chmod(x,0600));dl_rescan(d);
    for(int i=0;i<100&&!d->hash_file;i++)dl_tick(d,++clock_ms,false);
    assert(d->hash_file&&!strcmp(d->hash_path,x));dl_tick(d,++clock_ms,false);assert(!chmod(x,0644));
    scan_complete(d);assert(d->verified_bytes==17001&&d->reused==1&&same(before,d->catalog,32));
    unsigned xi=0;while(xi<d->count&&strcmp(d->local[xi].path,x))xi++;assert(xi<d->count);
    struct stat meta;assert(!stat(x,&meta)&&unchanged(&d->local[xi],&meta));
    assert(!chmod(x,0600)&&!stat(x,&meta)&&!unchanged(&d->local[xi],&meta)&&content_unchanged(&d->local[xi].stamp,&meta));
    uint8_t get[36]={0};memcpy(get,d->local[xi].id,32);d->authenticated=true;
    assert(message(d,GET,get,sizeof(get)));close_file(&d->sending);d->send_begin=false;
    dl_rescan(d);scan_complete(d);assert(d->verified_bytes==17001&&d->reused==1);
    // 同长度改写也失效；改名及增删清单都重新核对。/ Same-size edits invalidate cache; renames, additions and removals refresh the manifest.
    write_book(x,7,17001);struct stat st;assert(!stat(x,&st));
    struct timespec changed[2]={{.tv_sec=st.st_atime},{.tv_sec=st.st_mtime+4}};assert(!utimensat(AT_FDCWD,x,changed,0));
    d->authenticated=true;assert(!message(d,GET,get,sizeof(get))&&!d->sending);
    assert(!rename(y,z));dl_rescan(d);scan_complete(d);assert(d->verified_bytes==40004&&!same(before,d->catalog,32));
    assert(!unlink(z));write_book(y,3,11);dl_rescan(d);scan_complete(d);assert(d->count==2&&d->verified_bytes==11&&d->reused==1);
    write_book(z,3,11);dl_rescan(d);scan_complete(d);assert(d->count==2&&d->verified_bytes==11);
    // 会话销毁后重建必须重新全量校验，等同 USB/换卡边界。/ A new session fully verifies again, as at USB/media boundaries.
    dl_destroy(d);d=dl_create(&cfg);assert(d);scan_complete(d);assert(d->count==2&&d->verified_bytes==17023&&!d->reused);
    // 扫描期间的同长度变化不得发布混合摘要。/ A same-size change during scanning must not publish a mixed digest.
    write_book(x,9,17001);dl_rescan(d);
    for(int i=0;i<100&&!d->hash_file;i++)dl_tick(d,++clock_ms,false);
    assert(d->hash_file);dl_tick(d,++clock_ms,false);write_book(x,10,17001);
    assert(!stat(x,&st));changed[1].tv_sec=st.st_mtime+8;assert(!utimensat(AT_FDCWD,x,changed,0));
    for(int i=0;i<100&&d->scan;i++)dl_tick(d,++clock_ms,false);
    assert(!d->catalog_valid&&d->retry_at==INT64_MAX);dl_rescan(d);scan_complete(d);dl_destroy(d);
    puts("PASS: session-only cache, ctime-only metadata changes, real mtime changes rejected, rename/add/remove, duplicate content and scan mutation");
}
static void wait_partial(dual_link_t *a,dual_link_t *b,uint32_t minimum){
    for(int i=0;i<60000&&(!b->incoming_ready||b->received<minimum);i++)pump(a,b,1);
    assert(b->incoming_ready&&b->received>=minimum&&b->received<b->expected);
}
static void wait_data_done(dual_link_t *a,dual_link_t *b){
    for(int i=0;i<12000&&(!a->authenticated||!b->authenticated||!a->done||!b->done);i++)pump(a,b,1);
    assert(a->authenticated&&b->authenticated&&a->done&&b->done);
}
static const char* different_book(dual_link_t *d){
    for(unsigned i=0;i<d->count;i++)if(!same(d->local[i].id,d->reading.book,32))return d->local[i].path;
    abort();
}
static void orientation_test(dual_link_t *a,dual_link_t *b){
    dl_reading_t ar=a->reading,br=b->reading;
    dl_status_t status;dl_status(a,&status);
    assert(status.orientation.inverted&&!status.orientation.primary_flip&&!status.orientation.secondary_flip);
    assert(!dl_orient(NULL,true)&&!dl_flip(NULL,1,true)&&!dl_flip(a,0,true)&&!dl_flip(b,3,true));
    assert(!dl_orient(b,false));dl_orientation_restore(NULL,NULL);dl_orientation_restore(a,NULL);
    radio=false;pump(a,b,1);
    assert(dl_orient(a,false)&&dl_flip(a,1,true)&&dl_flip(a,2,true));
    uint32_t revision=a->orientation_revision;
    assert(dl_orient(a,false)&&dl_flip(a,1,true)&&a->orientation_revision==revision);
    assert(!dl_flip(b,1,true)&&!dl_flip(b,2,false));
    radio=true;pump(a,b,1);assert(!dl_flip(b,1,true));pump(a,b,8);
    dl_status(b,&status);assert(!status.orientation.inverted&&status.orientation.primary_flip&&status.orientation.secondary_flip);
    // 方向不依赖打开图书；旧控制协议和首个主机状态之前都不能假报副机设置成功。
    // Orientation works without an open book; old protocols or missing initial authority cannot acknowledge secondary changes.
    assert(!nonzero(a->reading.book)&&!nonzero(b->reading.book));
    dl_control_connected(b,false);dl_control_connected(b,true);
    uint8_t catalog[35]={1,1},old[DL_CONTROL_MAX],packet[DL_CONTROL_MAX],bad[DL_CONTROL_MAX];
    memcpy(catalog+2,a->catalog,32);catalog[34]=3;
    assert(!dl_control_receive(b,catalog,35)&&!dl_flip(b,1,true));catalog[34]=CONTROL_VERSION;
    assert(dl_control_receive(b,catalog,35)&&!dl_flip(b,1,true));
    a->orientation_dirty=true;a->catalog_dirty=false;a->control_sent=a->now;
    assert(dl_control_take(a,old,sizeof(old))==10&&old[0]==7&&dl_control_receive(b,old,10));
    assert(dl_flip(b,1,true)&&!b->orientation_request);
    assert(dl_flip(b,1,false)&&b->orientation.primary_flip);
    uint32_t sequence=b->orientation_sequence;
    assert(dl_flip(b,1,false)&&b->orientation_sequence==sequence&&!dl_flip(b,2,false));
    b->catalog_dirty=false;b->control_sent=b->now;
    assert(dl_control_take(b,packet,sizeof(packet))==6&&packet[0]==8);
    uint8_t truncated[1]={7};assert(!dl_control_receive(b,truncated,1));truncated[0]=8;assert(!dl_control_receive(a,truncated,1));
    assert(!dl_control_receive(a,packet,5)&&!dl_control_receive(b,packet,6));
    memcpy(bad,packet,6);put32(bad+1,0);assert(!dl_control_receive(a,bad,6));
    for(unsigned i=0;i<8;i++)if(i!=1&&i!=2&&i!=5&&i!=6){memcpy(bad,packet,6);bad[5]=i;assert(!dl_control_receive(a,bad,6));}
    memcpy(bad,old,10);bad[9]=8;assert(!dl_control_receive(b,bad,10));put32(bad+1,0);bad[9]=0;assert(!dl_control_receive(b,bad,10));
    assert(dl_control_receive(a,packet,6)&&!a->orientation.primary_flip);
    revision=a->orientation_revision;assert(dl_control_receive(a,packet,6)&&a->orientation_revision==revision);
    // 丢失确认后按心跳重试；旧方向状态不能回滚，重试不重复翻转。
    // Heartbeats recover a lost acknowledgement; stale state cannot roll back and retries never toggle twice.
    assert(dl_control_take(a,bad,sizeof(bad))==10&&bad[0]==7);pump(a,b,20);assert(b->orientation.primary_flip);
    clock_ms+=1501;pump(a,b,8);assert(!b->orientation.primary_flip&&!b->orientation_request);
    assert(dl_control_receive(b,old,10)&&!b->orientation.primary_flip);
    assert(dl_flip(b,2,false));pump(a,b,8);assert(!a->orientation.secondary_flip&&!b->orientation.secondary_flip);
    assert(dl_orient(a,true));pump(a,b,8);assert(b->orientation.inverted);
    assert(dl_flip(a,1,true));pump(a,b,8);assert(b->orientation.primary_flip&&!b->orientation.secondary_flip);
    // 校验/传书期间仍传递主机方向及副机开关请求。/ Scanning and transfer readiness never block orientation or switch requests.
    dl_rescan(a);assert(!control_ready(a));
    assert(dl_control_take(a,packet,sizeof(packet))==35&&dl_control_receive(b,packet,35));
    assert(!control_ready(b)&&dl_orient(a,false));
    assert(dl_control_take(a,packet,sizeof(packet))==10&&dl_control_receive(b,packet,10));
    assert(!b->orientation.inverted&&dl_flip(b,2,true));b->catalog_dirty=false;b->control_sent=b->now;
    assert(dl_control_take(b,packet,sizeof(packet))==6&&dl_control_receive(a,packet,6));
    assert(dl_control_take(a,packet,sizeof(packet))==10&&dl_control_receive(b,packet,10)&&b->orientation.secondary_flip);
    synchronized(a,b);pump(a,b,8);
    assert(dl_flip(b,1,false));radio=false;pump(a,b,1);assert(!b->orientation_request);
    assert(dl_orient(a,true)&&dl_flip(a,2,false)&&!dl_flip(b,1,false));
    radio=true;synchronized(a,b);pump(a,b,8);
    assert(b->orientation.inverted&&b->orientation.primary_flip&&!b->orientation.secondary_flip);
    dl_orientation_t defaults={.inverted=true};dl_orientation_restore(a,&defaults);pump(a,b,8);
    revision=a->orientation_revision;dl_orientation_restore(a,&defaults);assert(a->orientation_revision==revision);
    assert(b->orientation.inverted&&!b->orientation.primary_flip&&!b->orientation.secondary_flip);
    assert(!memcmp(&a->reading,&ar,sizeof(ar))&&!memcmp(&b->reading,&br,sizeof(br)));
    puts("PASS: independent orientation/defaults, both-role flips, offline primary, confirmed secondary, malformed/old packets, heartbeat retry and reconnect");
}
static void game_link_test(dual_link_t *a,dual_link_t *b){
    // 从机发起打开，主机应用后 ACK；双方共享同一 session。
    // Secondary initiates; primary applies and ACKs; both retain one session.
    assert(dl_game_open(b,1));
    pump(a,b,8);
    uint8_t kind=0;uint32_t session=0,request=0;
    assert(dl_game_open_take(a,&kind,&session,&request));
    assert(kind==1&&session&&request&&!a->game_active);
    dl_game_open_finish(a,request,session,true);
    pump(a,b,8);
    bool accepted=false;uint32_t result_request=0;
    assert(dl_game_open_result_take(b,&result_request,&accepted));
    assert(accepted&&result_request==request&&a->game_active&&b->game_active&&a->game_session==b->game_session);
    assert(dl_game_input(b,2));
    pump(a,b,8);
    uint8_t input=0;assert(dl_game_take(a,&input)&&input==2);
    uint8_t state[4]={1,2,3,4};assert(dl_game_packet(a,state,sizeof(state)));
    pump(a,b,8);
    uint8_t received[4]={0};assert(dl_game_packet_take(b,received,sizeof(received))==sizeof(received)&&!memcmp(state,received,sizeof(state)));
    // 断线重连重新声明现有会话，不生成第二局。
    // Reconnect reasserts the existing session instead of creating another game.
    radio=false;pump(a,b,1);radio=true;synchronized(a,b);pump(a,b,16);
    assert(a->game_active&&b->game_active&&a->game_session==b->game_session);
    assert(dl_game_close(b));
    pump(a,b,8);
    assert(dl_game_close_take(a,&session,&request));
    dl_game_close_finish(a,request,true);
    pump(a,b,8);
    assert(!a->game_active&&!b->game_active&&!b->game_close_pending);
    puts("PASS: either-role game open, session ACK, input/state session filtering, reconnect reassert and close ACK");
}
static void slow_orientation_test(dual_link_t *a,dual_link_t *b){
    pump(a,b,8);assert(a->left_ready&&b->left_ready);
    tick_step=1000;
    assert(dl_turn(a,1));uint32_t revision=a->reading.revision;pump(a,b,6);
    assert(b->reading.revision==revision&&b->left_ready);
    // 每秒一次发送时，两种持续更新都必须有发送机会，不能让心跳反复插队。
    // At one send per second, continuous reading and orientation updates must both progress without heartbeat starvation.
    reading_frames=orientation_frames=0;dl_reading_t stable=a->reading;
    for(int i=0;i<8;i++){
        assert(dl_orient(a,!a->orientation.inverted));a->state_dirty=true;pump(a,b,1);
    }
    assert(reading_frames>=3&&orientation_frames>=3&&!memcmp(&stable,&a->reading,sizeof(stable)));
    pump(a,b,6);assert(b->orientation.inverted==a->orientation.inverted);
    uint32_t spread=a->reading.spread;bool flipped=!b->orientation.secondary_flip;
    assert(dl_turn(b,1)&&dl_flip(b,2,flipped));pump(a,b,8);
    assert(a->reading.spread==spread+2&&b->reading.spread==spread+2);
    assert(a->orientation.secondary_flip==flipped&&b->orientation.secondary_flip==flipped&&!b->orientation_request);
    tick_step=2;
    puts("PASS: 1000 ms ticks deliver reading and orientation fairly, including concurrent secondary requests without reflow");
}
static void display_order_test(dual_link_t *a,dual_link_t *b,const char *path){
    auto_present=false;
    assert(dl_open_path(a,path));pump(a,b,8);
    dl_status_t x,y;dl_status(a,&x);dl_status(b,&y);
    assert(!x.left_ready&&!y.left_ready&&x.busy&&y.busy&&x.reading.revision==y.reading.revision);
    assert(!dl_turn(a,1)&&!dl_turn(b,1)&&!dl_seek(a,2)&&!dl_seek(b,2));
    assert(!dl_presented(a,a->reading.revision-1)&&!dl_presented(b,b->reading.revision));
    uint8_t prepare[DL_CONTROL_MAX],ready[DL_CONTROL_MAX],packet[DL_CONTROL_MAX];
    a->state_dirty=true;a->catalog_dirty=false;a->control_sent=a->now;
    assert(dl_control_take(a,prepare,sizeof(prepare))==87&&!prepare[81]&&prepare[85]);
    assert(!dl_control_receive(b,prepare,81));memcpy(packet,prepare,87);packet[81]=2;
    assert(!dl_control_receive(b,packet,87));
    // 老 BLE 协议和错误版本不能达成书库同步。/ Old BLE framing and incompatible versions cannot synchronize catalogs.
    uint8_t catalog[35]={1,1};memcpy(catalog+2,a->catalog,32);catalog[34]=CONTROL_VERSION;
    assert(!dl_control_receive(b,catalog,34));catalog[34]=1;assert(!dl_control_receive(b,catalog,35));
    assert(dl_presented(a,a->reading.revision));
    assert(dl_control_take(a,ready,sizeof(ready))==87&&ready[81]);
    // 首个完成通知丢失时保持右屏，心跳重发后才放行；迟到准备帧不回退。
    // Drop the first completion: hold the right screen until heartbeat retry; a late prepare cannot revoke it.
    pump(a,b,30);assert(!b->left_ready);
    clock_ms+=1501;pump(a,b,8);assert(b->left_ready);
    assert(dl_control_receive(b,prepare,87)&&b->left_ready);
    assert(b->peer_reading);
    dl_reading_active(a,false);
    a->reading_dirty=false;a->state_dirty=true;
    assert(dl_control_take(a,prepare,sizeof(prepare))==87&&!prepare[85]);
    assert(dl_control_receive(b,prepare,87)&&!b->peer_reading);
    dl_reading_active(a,true);pump(a,b,2);assert(b->peer_reading);
    // 从机跳页仅提交请求；没有主机推屏确认，等待超过一分钟也不得自动放行。
    // A secondary seek only requests a spread; even a minute cannot release it without primary presentation.
    uint32_t previous=b->reading.spread;assert(dl_seek(b,4)&&b->reading.spread==previous);
    pump(a,b,8);assert(a->reading.spread==2&&b->reading.spread==2&&!a->left_ready&&!b->left_ready);
    clock_ms+=61000;pump(a,b,8);assert(!a->left_ready&&!b->left_ready);
    assert(!dl_turn(b,1)&&!dl_seek(b,2));
    assert(dl_presented(a,a->reading.revision));pump(a,b,8);assert(b->left_ready);
    assert(dl_seek(a,7)&&a->reading.spread==6&&!a->left_ready);
    uint32_t before_total=a->reading.revision;dl_total(a,7);
    assert(a->reading.revision!=before_total&&!dl_presented(a,before_total));
    pump(a,b,8);assert(b->reading.spread==6&&!b->left_ready&&b->reading.total==7);
    assert(dl_presented(a,a->reading.revision));pump(a,b,8);assert(b->left_ready);
    assert(!dl_seek(a,0)&&!dl_seek(b,0)&&!dl_seek(a,8)&&!dl_seek(b,8));
    assert(!dl_seek(a,UINT32_MAX)&&!dl_seek(b,UINT32_MAX));
    uint8_t seek[13]={5};put32(seek+1,a->reading.revision);put32(seek+5,a->peer_sequence+1);
    assert(!dl_control_receive(a,seek,13));put32(seek+9,UINT32_MAX);assert(!dl_control_receive(a,seek,13));
    put32(seek+9,8);assert(!dl_control_receive(a,seek,13));put32(seek+9,2);assert(!dl_control_receive(a,seek,12));
    assert(dl_seek(b,2)&&b->reading.spread==6);b->catalog_dirty=false;b->control_sent=b->now;
    assert(dl_control_take(b,packet,sizeof(packet))==13&&packet[0]==5&&get32(packet+9)==2);
    assert(dl_control_receive(a,packet,13)&&!a->reading.spread&&!a->left_ready);
    uint32_t revision=a->reading.revision;
    assert(dl_control_receive(a,packet,13)&&a->reading.revision==revision);pump(a,b,8);
    // 重连会清除双方放行状态，同版本也必须重新完成左屏；恢复状态不继承确认。
    // Reconnect clears release on both sides even for the same revision; restored state never inherits confirmation.
    radio=false;pump(a,b,1);assert(!dl_seek(a,2)&&!dl_seek(b,2));
    radio=true;synchronized(a,b);pump(a,b,8);
    assert(!a->left_ready&&!b->left_ready&&!dl_turn(a,1)&&!dl_turn(b,1));
    assert(dl_presented(a,a->reading.revision));pump(a,b,8);assert(a->left_ready&&b->left_ready);
    dual_link_t *restored=dl_create(&a->cfg);assert(restored);dl_restore(restored,&a->reading);
    assert(restored->reading.revision==a->reading.revision&&!restored->left_ready);dl_destroy(restored);
    dual_link_t *secondary_restored=dl_create(&b->cfg);assert(secondary_restored);
    dl_restore(secondary_restored,&b->reading);
    assert(secondary_restored->reading.revision==b->reading.revision&&!secondary_restored->left_ready);
    dl_destroy(secondary_restored);
    auto_present=true;wait_data_done(a,b);
    puts("PASS: primary presentation gates secondary; lost completion and delayed prepare; both-side seeks, bounds, stale ACK, reconnect and restore");
}
static void resize_test(dual_link_t *a,dual_link_t *b,const char *path){
    auto_present=false;
    assert(a->reading.font_px==48&&b->reading.font_px==48);
    assert(dl_seek(a,5));pump(a,b,8);assert(dl_presented(a,a->reading.revision));pump(a,b,8);
    uint8_t request[DL_CONTROL_MAX],prepare[DL_CONTROL_MAX],bad[DL_CONTROL_MAX];
    assert(!dl_resize(a,35)&&!dl_resize(b,76)&&!dl_resize(a,37)&&!dl_resize(b,48));
    assert(dl_resize(b,52)&&b->reading.font_px==48&&b->reading.spread==4);
    b->catalog_dirty=false;b->control_sent=b->now;
    assert(dl_control_take(b,request,sizeof(request))==10&&request[0]==6&&request[9]==52);
    assert(!dl_control_receive(a,request,9)&&dl_control_receive(a,request,10));
    uint32_t revision=a->reading.revision;
    assert(a->reading.font_px==52&&a->reading.reflow_from_px==48&&a->reading.reflow_pending&&a->reading.spread==4);
    assert(dl_control_receive(a,request,10)&&a->reading.revision==revision);
    pump(a,b,8);assert(b->reading.reflow_pending&&b->reading.reflow_from_px==48&&!b->left_ready);
    assert(!dl_presented(a,revision)&&!dl_turn(a,1)&&!dl_seek(b,1)&&!dl_resize(a,56));
    assert(!dl_open_path(a,dl_reading_path(a))&&!dl_open_path(b,dl_reading_path(b)));
    dl_total(a,3);assert(!a->reading.total&&a->reading.revision==revision&&a->reading.spread==4);
    a->state_dirty=true;a->catalog_dirty=false;a->control_sent=a->now;
    assert(dl_control_take(a,prepare,sizeof(prepare))==87&&prepare[83]);
    assert(!dl_control_receive(b,prepare,82));memcpy(bad,prepare,87);bad[81]=1;assert(!dl_control_receive(b,bad,87));
    memcpy(bad,prepare,87);bad[82]=37;assert(!dl_control_receive(b,bad,87));
    memcpy(bad,prepare,87);bad[84]=0;assert(!dl_control_receive(b,bad,87));
    memcpy(bad,prepare,87);bad[85]=2;assert(!dl_control_receive(b,bad,87));
    memcpy(bad,prepare,87);bad[86]=2;assert(!dl_control_receive(b,bad,87));
    assert(!dl_reflow_resolved(b,revision,6)&&!dl_reflow_resolved(a,revision-1,6));
    assert(!dl_reflow_resolved(a,revision,5)&&!dl_reflow_resolved(a,revision,UINT32_MAX-1));
    // 中途断线保留旧页和旧字号锚点信息；重排完成后独立版本拒绝迟到准备帧。
    // Disconnect retains the old-page/size anchor; a new resolved revision rejects delayed reflow prepares.
    radio=false;pump(a,b,1);radio=true;synchronized(a,b);pump(a,b,8);
    assert(a->reading.reflow_pending&&b->reading.reflow_pending&&!a->left_ready&&!b->left_ready);
    assert(dl_reflow_resolved(a,revision,6)&&a->reading.revision!=revision&&!a->reading.reflow_pending&&!a->left_ready);
    pump(a,b,8);assert(b->reading.spread==6&&b->reading.font_px==52&&!b->reading.reflow_pending&&!b->left_ready);
    assert(dl_control_receive(b,prepare,87)&&b->reading.spread==6&&!b->reading.reflow_pending);
    assert(!dl_presented(a,revision)&&dl_presented(a,a->reading.revision));pump(a,b,8);assert(b->left_ready);
    // 主机发起也进入同一流程；新开书取消旧重排并沿用共享字号。
    // Primary resizing follows the same flow; opening another book cancels old reflow while retaining shared size.
    dl_reading_t durable=a->reading;
    assert(dl_resize(a,44));revision=a->reading.revision;pump(a,b,8);
    assert(a->reading.reflow_from_px==52&&b->reading.reflow_pending);
    dual_link_t *restarted=dl_create(&a->cfg);assert(restarted);dl_restore(restarted,&durable);
    assert(restarted->reading.font_px==52&&restarted->reading.spread==6&&!restarted->left_ready);
    dl_restore(restarted,&a->reading);assert(restarted->reading.revision==durable.revision&&!restarted->reading.reflow_pending);
    dl_destroy(restarted);
    assert(!dl_reflow_failed(b,revision)&&!dl_reflow_failed(a,revision-1));
    assert(dl_reflow_failed(a,revision)&&a->reading.font_px==52&&a->reading.spread==6&&!a->reading.reflow_pending);
    pump(a,b,8);assert(!b->left_ready&&b->reading.font_px==52&&!b->reading.reflow_pending);
    assert(dl_presented(a,a->reading.revision));pump(a,b,8);assert(b->left_ready);
    assert(dl_resize(a,44));revision=a->reading.revision;
    assert(dl_open_at(a,different_book(a),9)&&!a->reading.reflow_pending&&a->reading.font_px==44&&a->reading.spread==8);
    assert(!dl_reflow_resolved(a,revision,8));pump(a,b,8);
    assert(dl_presented(a,a->reading.revision));pump(a,b,8);assert(b->reading.font_px==44&&b->left_ready);
    // 双12记录的固定前缀迁移保留书/页码；未完成重排不能作为可恢复页码。
    // The dual12 prefix preserves book/spread during migration; pending reflow cannot restore a resolved page.
    assert(offsetof(dl_reading_t,font_px)==76&&sizeof(dl_reading_t)==80);
    dl_reading_t legacy={0};memcpy(&legacy,&a->reading,76);legacy.spread=8;
    dual_link_t *restored=dl_create(&a->cfg);assert(restored);dl_restore(restored,&legacy);
    assert(same(restored->reading.book,legacy.book,32)&&restored->reading.spread==8&&restored->reading.font_px==48);
    dl_restore(restored,&a->reading);assert(restored->reading.font_px==44);
    legacy.reflow_pending=true;dl_restore(restored,&legacy);assert(restored->reading.font_px==44);dl_destroy(restored);
    // 不同书的指定页与开书状态同帧发布；断线排队的开书也保留目标页。
    // A different book and its requested spread publish together, including an open queued during disconnect.
    assert(!dl_open_at(b,different_book(b),0)&&!dl_open_at(a,path,UINT32_MAX));
    assert(dl_open_at(b,different_book(b),12));b->catalog_dirty=false;b->control_sent=b->now;
    assert(dl_control_take(b,request,sizeof(request))==45&&request[0]==3&&get32(request+41)==12);
    assert(!dl_control_receive(a,request,41));memcpy(bad,request,45);put32(bad+41,0);assert(!dl_control_receive(a,bad,45));
    assert(dl_control_receive(a,request,45)&&a->reading.spread==10&&!a->left_ready);pump(a,b,8);
    assert(a->remote_open);
    dl_remote_open_clear(a);assert(!a->remote_open);
    assert(b->reading.spread==10&&!b->left_ready&&b->reading.font_px==44);
    assert(dl_presented(a,a->reading.revision));pump(a,b,8);
    assert(dl_open_at(a,dl_reading_path(a),14)&&a->reading.spread==12);pump(a,b,8);
    assert(dl_presented(a,a->reading.revision));pump(a,b,8);
    revision=a->reading.revision;
    assert(dl_open_at(b,dl_reading_path(b),b->reading.spread+1));pump(a,b,8);
    assert(a->reading.revision!=revision&&a->reading.spread==12&&b->reading.spread==12&&!b->left_ready);
    assert(dl_presented(a,a->reading.revision));pump(a,b,8);
    // 书架续读同书保留共享位置；不同书可使用本机实际页作为提示。
    // Shelf resume retains the shared spread for the same book and may use a local page hint for another book.
    revision=a->reading.revision;
    assert(dl_open_resume_at(b,dl_reading_path(b),5));
    pump(a,b,8);assert(a->reading.revision!=revision&&a->reading.spread==12&&b->reading.spread==12);
    assert(dl_presented(a,a->reading.revision));pump(a,b,8);
    assert(dl_open_resume_at(b,different_book(b),15));
    pump(a,b,8);assert(a->reading.spread==14&&b->reading.spread==14&&!b->left_ready);
    assert(dl_presented(a,a->reading.revision));pump(a,b,8);
    radio=false;pump(a,b,1);assert(dl_open_at(a,different_book(a),16)&&a->reading.spread==14);
    radio=true;synchronized(a,b);pump(a,b,8);assert(a->reading.spread==14&&b->reading.spread==14&&!b->left_ready);
    revision=a->reading.revision;
    assert(dl_open_path(a,dl_reading_path(a))&&a->reading.revision!=revision&&a->reading.spread==14);pump(a,b,8);
    revision=a->reading.revision;
    assert(dl_open_path(b,dl_reading_path(b)));pump(a,b,8);
    assert(a->reading.revision!=revision&&a->reading.spread==14&&!b->left_ready);
    assert(dl_presented(a,a->reading.revision));pump(a,b,8);
    auto_present=true;wait_data_done(a,b);
    puts("PASS: shared font bounds, both-side resize, anchor-size retention, reflow rollback, atomic targeted/shelf-resume open, reconnect, stale frames and dual12 migration");
}
static void resume_test(const char *root){
    dl_config_t ac={.role=1,.port=23773,.code="123456",.root_count=1},bc={.role=2,.port=23773,.code="123456",.host="127.0.0.1",.root_count=1};
    snprintf(ac.destination,DL_PATH,"%s/large-a",root);snprintf(bc.destination,DL_PATH,"%s/large-b",root);
    assert(!mkdir(ac.destination,0700)&&!mkdir(bc.destination,0700));strcpy(ac.roots[0],ac.destination);strcpy(bc.roots[0],bc.destination);
    char source[DL_PATH+32];snprintf(source,sizeof(source),"%s/shared.ttf",ac.destination);write_book(source,4,20u*1024*1024+17);
    snprintf(ac.font,DL_PATH,"%s/shared.ttf",ac.destination);
    dual_link_t *a=dl_create(&ac),*b=dl_create(&bc);assert(a&&b);wait_partial(a,b,8u*1024*1024);
    char temporary[DL_PATH],final[DL_PATH];strcpy(temporary,b->temp);strcpy(final,b->final);
    // 真正关闭 TCP 后保存落盘偏移，半文件不进入清单。/ Close real TCP; retain the disk offset without listing the partial.
    assert(!shutdown(a->fd,SHUT_RDWR));for(int i=0;i<1000&&b->receiving;i++)pump(a,b,1);
    uint32_t offset=b->received;struct stat st;
    assert(!b->receiving&&offset>=8u*1024*1024&&!stat(temporary,&st)&&(uint32_t)st.st_size==offset&&stat(final,&st));
    assert(!b->count&&!control_ready(b));
    int64_t resume_started=0;
    for(int i=0;i<60000;i++){
        // 以慢 TF 节拍推进逻辑时间，前缀校验超过 TCP 的 30 秒超时。/ Simulate slow TF reads past the TCP 30-second timeout.
        if(b->resume_hashing){
            if(!resume_started)resume_started=clock_ms;
            clock_ms+=20;assert(a->authenticated&&b->authenticated);
        }
        pump(a,b,1);
        if(a->tx_len&&a->tx[3]==FILE_BEGIN&&get32(a->tx+44)==offset)break;
    }
    assert(resume_started&&clock_ms-resume_started>30000);
    int64_t prefix_ms=clock_ms-resume_started;
    assert(a->tx_len&&a->tx[3]==FILE_BEGIN&&get32(a->tx+44)==offset&&b->resumed==offset);
    // 接收中手动重扫也保留进度。/ A manual rescan while receiving must retain progress too.
    wait_partial(a,b,offset+16384);uint32_t second=b->received;dl_rescan(b);
    synchronized(a,b);wait_data_done(a,b);assert(b->resumed==second&&b->count==1&&b->imports==1);
    assert(!stat(final,&st)&&st.st_size==20u*1024*1024+17&&stat(temporary,&st)&&same(a->local[0].id,b->local[0].id,32));
    dl_rescan(a);dl_rescan(b);synchronized(a,b);wait_data_done(a,b);assert(!a->verified_bytes&&!b->verified_bytes&&b->imports==1);
    printf("PASS: 20 MiB font resumed twice at %u and %u bytes; slow prefix %lld ms; unchanged scans hash zero bytes\n",offset,second,(long long)prefix_ms);
    // 修改已保存前缀，完整 SHA 必须拒绝，再从零恢复。/ Corrupt the saved prefix; full SHA must reject it and retry from zero.
    snprintf(source,sizeof(source),"%s/corrupt-prefix.epub",ac.destination);write_book(source,5,262147);dl_rescan(a);dl_rescan(b);wait_partial(a,b,8192);
    strcpy(temporary,b->temp);strcpy(final,b->final);wifi=false;pump(a,b,2);offset=b->received;
    FILE *f=fopen(temporary,"r+b");assert(f);assert(fputc('!',f)!=EOF&&!fclose(f));wifi=true;
    for(int i=0;i<20000;i++){pump(a,b,1);if(!b->temp[0]&&b->error)break;}
    assert(b->resumed==offset&&!b->temp[0]&&b->error&&stat(final,&st)&&stat(temporary,&st)&&b->imports==1);
    synchronized(a,b);wait_data_done(a,b);assert(b->imports==2&&!stat(final,&st)&&st.st_size==262147);
    // 来源摘要变化必须丢掉旧半文件，以新摘要从零请求。/ A changed source digest discards the old partial and requests the new file at zero.
    snprintf(source,sizeof(source),"%s/source-changed.txt",ac.destination);write_book(source,6,262149);dl_rescan(a);dl_rescan(b);wait_partial(a,b,8192);
    strcpy(temporary,b->temp);wifi=false;pump(a,b,2);write_book(source,8,262149);dl_rescan(a);dl_rescan(b);b->resumed=0;wifi=true;
    synchronized(a,b);wait_data_done(a,b);assert(!b->resumed&&stat(temporary,&st)&&b->imports==3);
    // 越界偏移和旧协议帧不得被接受。/ Reject out-of-range offsets and old wire versions.
    uint8_t get[36];memcpy(get,a->local[0].id,32);put32(get+32,a->local[0].bytes+1);assert(!message(a,GET,get,sizeof(get)));
    uint8_t old[8]={'D','P',2,PING,0,0,0,0};assert(send(a->fd,old,sizeof(old),0)==sizeof(old));
    for(int i=0;i<10&&b->authenticated;i++)pump(a,b,1);
    assert(!b->authenticated&&b->error&&!strcmp(b->error,"invalid protocol"));
    dl_destroy(a);dl_destroy(b);
    puts("PASS: corrupt resume prefix rejected before publish; changed source restarts; bad offsets and protocol v2 rejected");
}
int main(void){
    char root[]="/private/tmp/pico-dual-test-XXXXXX";assert(mkdtemp(root));
    dl_config_t ac={.role=1,.port=23771,.code="123456",.root_count=1},bc={.role=2,.port=23771,.code="123456",.host="127.0.0.1",.root_count=1};
    snprintf(ac.destination,DL_PATH,"%s/a",root);snprintf(bc.destination,DL_PATH,"%s/b",root);assert(!mkdir(ac.destination,0700));assert(!mkdir(bc.destination,0700));
    strcpy(ac.roots[0],ac.destination);strcpy(bc.roots[0],bc.destination);
    char ap[DL_PATH+32],bp[DL_PATH+32];snprintf(ap,sizeof(ap),"%s/same.txt",ac.destination);snprintf(bp,sizeof(bp),"%s/same.txt",bc.destination);
    write_book(ap,1,40001);write_book(bp,3,18003);
    dual_link_t *a=dl_create(&ac),*b=dl_create(&bc);assert(a&&b);synchronized(a,b);
    assert(a->count==2&&b->count==2&&a->imports==1&&b->imports==1);
    orientation_test(a,b);
    game_link_test(a,b);
    assert(dl_open_path(b,bp));pump(a,b,80);assert(a->reading.revision==b->reading.revision&&same(a->reading.book,b->reading.book,32));assert(dl_reading_path(a)&&dl_reading_path(b));
    assert(dl_turn(b,1));pump(a,b,80);assert(a->reading.spread==2&&b->reading.spread==2);
    assert(dl_turn(a,-1));pump(a,b,80);assert(a->reading.spread==0&&b->reading.spread==0);
    dl_total(a,3);pump(a,b,80);assert(dl_turn(b,1));pump(a,b,80);assert(b->reading.spread==2);assert(dl_turn(b,1));pump(a,b,80);assert(b->reading.spread==2);
    radio=false;pump(a,b,1);assert(!dl_turn(b,1));radio=true;synchronized(a,b);assert(a->reading.spread==b->reading.spread&&a->count==2&&b->count==2);
    assert(dl_open_path(a,ap));pump(a,b,80);assert(!a->reading.spread&&!b->reading.spread);
    // Wi-Fi 断开，BLE 继续翻页；TCP 上的控制消息必须被拒绝。
    // BLE keeps turning without Wi-Fi; TCP must reject all reading commands.
    wifi=false;pump(a,b,10);assert(!a->authenticated&&!b->authenticated);
    assert(dl_turn(b,1));pump(a,b,80);assert(a->reading.spread==2&&b->reading.spread==2);
    assert(dl_turn(a,-1));pump(a,b,80);assert(!a->reading.spread&&!b->reading.spread);
    a->authenticated=true;uint8_t reject[76]={0};assert(!message(a,TURN,reject,5)&&!message(a,OPEN,reject,32)&&!message(a,STATE,reject,76));a->authenticated=false;
    // 丢失一次状态通知后由心跳恢复；旧通知不能回滚，重试也不能无限延长请求。
    // Heartbeats recover a lost state; old notifications cannot roll back or extend a request forever.
    assert(dl_turn(a,1));a->catalog_dirty=false;a->control_sent=a->now;
    uint8_t lost_state[DL_CONTROL_MAX],retry[DL_CONTROL_MAX];
    assert(dl_presented(a,a->reading.revision));
    assert(dl_control_take(a,lost_state,sizeof(lost_state))==87);
    assert(a->reading.revision!=b->reading.revision);
    clock_ms+=1501;dl_tick(a,clock_ms,false);dl_tick(b,clock_ms,false);
    assert(dl_control_take(a,retry,sizeof(retry))==35&&dl_control_receive(b,retry,35));
    assert(dl_control_take(a,retry,sizeof(retry))==10&&retry[0]==7&&dl_control_receive(b,retry,10));
    assert(dl_control_take(a,retry,sizeof(retry))==87&&dl_control_receive(b,retry,87));
    assert(a->reading.revision==b->reading.revision&&b->reading.spread==2);
    assert(dl_turn(a,-1));pump(a,b,80);uint32_t latest=b->reading.revision;
    assert(dl_control_receive(b,lost_state,87)&&b->reading.revision==latest&&!b->reading.spread);
    assert(dl_turn(b,1));int64_t started=b->request_started;
    for(int i=0;i<21;i++){clock_ms+=500;dl_tick(b,clock_ms,false);(void)dl_control_take(b,retry,sizeof(retry));}
    assert(!b->request_pending&&b->request_started==started&&!a->reading.spread);
    wifi=true;synchronized(a,b);
    // 重扫不重复下载，原文件保留。/ Rescanning never duplicates imports or alters originals.
    dl_rescan(a);dl_rescan(b);synchronized(a,b);assert(a->count==2&&b->count==2&&a->imports==1&&b->imports==1);
    assert(a->reused==2&&b->reused==2&&!a->verified_bytes&&!b->verified_bytes);
    // BLE书库就绪不等于TCP握手完成，文件故障注入须等待数据通道。
    // BLE catalog readiness can precede TCP auth; file fault injection needs the data channel.
    for(int i=0;i<12000&&(!a->authenticated||!b->authenticated||!a->done||!b->done);i++)pump(a,b,1);
    assert(a->authenticated&&b->authenticated&&a->done&&b->done);
    display_order_test(a,b,ap);
    resize_test(a,b,ap);
    slow_orientation_test(a,b);
    struct stat st;assert(!stat(ap,&st)&&st.st_size==40001);assert(!stat(bp,&st)&&st.st_size==18003);
    // 畸形、路径注入、未认证以及摘要失败均不发布。/ Malformed, injected, unauthenticated and corrupt input never publishes files.
    uint8_t malicious[64]={0};assert(!message(a,255,NULL,0));assert(!message(a,GET,malicious,31));
    assert(!book_name("../a.txt")&&!book_name("a/b.txt")&&!book_name("a\\b.epub")&&!book_name(".a.txt"));
    // 临时文件的长度正确但摘要错误，不能成为可见图书。/ Correct length with a wrong digest must never publish a book.
    remote_t corrupt={.bytes=3,.name="corrupt.txt"};hash_t h;assert(hash_begin(&h)&&hash_add(&h,"abc",3)&&hash_end(&h,corrupt.id));
    b->remote[0]=corrupt;b->remote_count=1;b->request_index=0;
    uint8_t begin[40]={0};memcpy(begin,corrupt.id,32);put32(begin+32,3);
    assert(message(b,FILE_BEGIN,begin,40));char temporary[DL_PATH],final[DL_PATH];strcpy(temporary,b->temp);strcpy(final,b->final);
    assert(message(b,DATA,(const uint8_t*)"bad",3));assert(!message(b,FILE_END,NULL,0));drop(b,"test corruption");assert(stat(final,&st)&&stat(temporary,&st));
    // 半本书断线保留自有临时文件，销毁会话才清理。/ Retain the owned partial on disconnect, clean it on destruction.
    b->authenticated=true;b->remote[0]=corrupt;b->request_index=0;assert(message(b,FILE_BEGIN,begin,40));strcpy(temporary,b->temp);
    assert(message(b,DATA,(const uint8_t*)"a",1));drop(b,"test disconnect");assert(!stat(temporary,&st)&&st.st_size==1);assert(!stat(bp,&st)&&st.st_size==18003);
    // 同时翻页中的旧请求不能把新位置再推进一次。/ A stale concurrent turn cannot advance a newer spread twice.
    uint8_t turn[10]={4};put32(turn+1,a->reading.revision);put32(turn+5,a->peer_sequence+1);turn[9]=1;
    assert(dl_turn(a,1));uint32_t spread=a->reading.spread;
    assert(dl_control_receive(a,turn,10));assert(a->reading.spread==spread);
    put32(turn+1,a->reading.revision);assert(dl_control_receive(a,turn,10));assert(a->reading.spread==spread);
    b->authenticated=false;assert(!message(b,ITEM,malicious,64));
    dl_destroy(b);dl_destroy(a);assert(stat(temporary,&st));
    strcpy(bc.code,"654321");a=dl_create(&ac);b=dl_create(&bc);pump(a,b,200);assert(!a->authenticated&&!b->authenticated&&!a->imports&&!b->imports);dl_destroy(a);dl_destroy(b);
    cache_test(root);resume_test(root);
    printf("PASS: duplex books, collision preservation, open either device, spread arbitration, odd end, reconnect, deduplication, authentication and protocol rejection (%s)\n",root);
    return 0;
}

/*
 * SPDX-FileCopyrightText: 2026 XH Lab
 * SPDX-License-Identifier: Apache-2.0
 * 中文：真实微信读书TF模块的有界快照、完整分页、保存失败与渲染纯度回归。
 * English: Test actual WeRead TF snapshots, complete pagination, failed saves and rendering purity.
 */
#include "pico_app_loader.h"
#include "pico_app_package.h"
#include "../apps/include/weread_snapshot.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

static const pico_app_exports_t *app;
static const char *fixture_root;
static const char *scenario;
static uint8_t framebuffer[640*640],progress[WRS_POSITION_BYTES];
static char screen[131072],body[131072],collected[262144];
static unsigned checks,reads,writes,navigations,toasts,screen_bytes,body_bytes;
static bool landscape,has_progress,fail_store;
static int64_t now_us;
static pico_app_weread_status_t service_status;
static unsigned service_calls,setup_calls,sync_calls;
static bool sync_accepted;
static unsigned explore_service_calls,explore_read_calls;
static bool explore_capture_text;
static char explore_text_capture[100000];
static unsigned explore_text_captured;
#define CHECK(condition) do{checks++;if(!(condition)){fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#condition);abort();}}while(0)

static uint32_t le32(const uint8_t *p){return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static pico_app_view_t view(void){return (pico_app_view_t){landscape?640:360,landscape?360:640,landscape};}
static int32_t page(void){return PGA_PAGE_APP;}
static void navigate(int32_t destination){CHECK(destination==PGA_PAGE_LAUNCHER);navigations++;}
static void noarg(void){}
static int64_t time_us(void){return now_us;}
static void toast(const char *message){CHECK(message&&*message);toasts++;}
static bool state_read(const char *key,const char *suffix,void *out,size_t bytes){
    char path[4096];int count=snprintf(path,sizeof(path),"%s/%s%s%s.%s",fixture_root,scenario,*scenario?"/":"",key,suffix);CHECK(count>0&&(unsigned)count<sizeof(path));
    FILE *file=fopen(path,"rb");if(!file)return false;
    uint8_t header[12];bool ok=fread(header,1,sizeof(header),file)==sizeof(header)&&le32(header)==0x31535058u&&le32(header+4)==bytes;
    if(ok)ok=fread(out,1,bytes,file)==bytes&&fgetc(file)==EOF&&pico_app_crc32(out,bytes)==le32(header+8);
    CHECK(fclose(file)==0);return ok;
}
static bool load(const char *key,void *out,size_t bytes){
    CHECK(bytes&&bytes<=PICO_APP_MAX_STATE);CHECK(key&&strlen(key)<32&&!strchr(key,'/')&&!strchr(key,'.'));reads++;
    if(has_progress&&!strcmp(key,"position")){CHECK(bytes==sizeof(progress));memcpy(out,progress,bytes);return true;}
    return state_read(key,"dat",out,bytes)||state_read(key,"bak",out,bytes);
}
static bool store(const char *key,const void *data,size_t bytes){
    CHECK(!strcmp(key,"position")&&bytes==sizeof(progress));writes++;
    if(fail_store)return false;
    memcpy(progress,data,bytes);has_progress=true;return true;
}
static void fill(uint8_t *fb,int x,int y,int w,int h,int gray){
    pico_app_view_t v=view();CHECK(x>=0&&y>=0&&w>=0&&h>=0&&x+w<=v.w&&y+h<=v.h&&gray>=0&&gray<=15);
    for(int row=y;row<y+h;row++)memset(fb+row*640+x,gray,(size_t)w);
}
static void line(uint8_t *fb,int x,int y,int xx,int yy,int gray){
    CHECK(gray>=0&&gray<=15);if(x==xx)fill(fb,x,y,1,yy-y+1,gray);else if(y==yy)fill(fb,x,y,xx-x+1,1,gray);else CHECK(false);
}
static void box(uint8_t *fb,int x,int y,int w,int h,int radius,int gray,bool solid){(void)radius;if(solid)fill(fb,x,y,w,h,gray);else{line(fb,x,y,x+w-1,y,gray);line(fb,x,y,x,y+h-1,gray);line(fb,x+w-1,y,x+w-1,y+h-1,gray);line(fb,x,y+h-1,x+w-1,y+h-1,gray);}}
static void append(char *buffer,unsigned *used,unsigned capacity,const char *text){size_t bytes=strlen(text);CHECK(bytes+*used+2<capacity);memcpy(buffer+*used,text,bytes);*used+=(unsigned)bytes;buffer[(*used)++]='\n';buffer[*used]=0;}
static void text(uint8_t *fb,int x,int y,int size,const char *value,int gray){
    (void)fb;pico_app_view_t v=view();CHECK(x>=0&&x<v.w&&y>=0&&y+size<=v.h&&gray>=0&&gray<=15);CHECK(strlen(value)<512);
    append(screen,&screen_bytes,sizeof(screen),value);
    if(explore_capture_text&&size==18&&y>=192&&y<v.h-70){size_t bytes=strlen(value);CHECK(explore_text_captured+bytes<sizeof(explore_text_capture));memcpy(explore_text_capture+explore_text_captured,value,bytes+1);explore_text_captured+=(unsigned)bytes;}
    if(x==20&&y>=108&&y<v.h-70)append(body,&body_bytes,sizeof(body),value);
}
static void wrap(uint8_t *fb,int x,int y,int width,int size,int rows,const char *value,int gray){(void)width;(void)rows;text(fb,x,y,size,value,gray);}
static void button(uint8_t *fb,int x,int y,int w,int h,const char *label,bool selected){CHECK(label&&*label);box(fb,x,y,w,h,0,selected?0:15,true);append(screen,&screen_bytes,sizeof(screen),label);}
static bool weread_service(uint32_t command,pico_app_weread_status_t *out){
    CHECK(out&&out->bytes==sizeof(*out));service_calls++;
    bool ok=true;
    if(command==PGA_WEREAD_OPEN_SETUP)setup_calls++;
    else if(command==PGA_WEREAD_SYNC){sync_calls++;ok=sync_accepted;if(ok){service_status.flags|=PGA_WEREAD_BUSY;service_status.state=PGA_WEREAD_SYNCING;}}
    else CHECK(command==PGA_WEREAD_STATUS);
    *out=service_status;return ok;
}
static pico_app_host_t host={.abi=PICO_APP_ABI,.bytes=sizeof(host),.view=view,.page=page,.navigate=navigate,.dirty=noarg,.toast=toast,
    .load=load,.store=store,.time_us=time_us,.fill=fill,.line=line,.box=box,.text=text,.center=text,.wrap=wrap,.button=button,
    .format=snprintf,.copy=memcpy,.move=memmove,.set=memset,.compare=memcmp};
static void render(void){unsigned io=reads+writes,services=service_calls,queries=explore_service_calls,copies=explore_read_calls;screen_bytes=body_bytes=0;screen[0]=body[0]=0;app->render(framebuffer);CHECK(io==reads+writes&&services==service_calls&&queries==explore_service_calls&&copies==explore_read_calls);}
static void start(const char *name,bool clear_progress){
    scenario=name;reads=writes=navigations=toasts=0;fail_store=false;now_us=0;host.bytes=sizeof(host);host.weread=NULL;if(clear_progress)has_progress=false;
    CHECK(app->start(&host));render();CHECK(!app->tick());
}
static bool event(int type,int x,int y){pico_app_event_t e={.type=type,.x=x,.y=y,.ms=now_us/1000};return app->event(&e);}
static void tap(int x,int y){(void)event(PGA_PRESS,x,y);(void)event(PGA_RELEASE,x,y);CHECK(event(PGA_TAP,x,y));render();}
static void stop(void){app->stop();}
static void compact(char *out,const char *in){while(*in){if(*in!='\n'&&*in!='\t'&&*in!=' ') *out++=*in;in++;}*out=0;}
static void collect_book(void){
    collected[0]=0;unsigned used=0,turns=0;
    do{render();CHECK(!strstr(screen,"暂时无法读取"));CHECK(strlen(body)+used+1<sizeof(collected));strcpy(collected+used,body);used+=(unsigned)strlen(body);CHECK(++turns<4096);}while(app->key(2));
}
static void check_expected(void){
    char path[4096];snprintf(path,sizeof(path),"%s/expected.txt",fixture_root);FILE *file=fopen(path,"rb");CHECK(file);
    char expected[131072],a[262144],b[131072];size_t n=fread(expected,1,sizeof(expected)-1,file);expected[n]=0;CHECK(!ferror(file)&&fclose(file)==0);
    compact(a,collected);compact(b,expected);CHECK(!strcmp(a,b));
}
static void external_fixture(void){
    landscape=false;start("",true);CHECK(strstr(screen,"三体"));CHECK(!strstr(screen,"不可读取"));tap(50,130);
    CHECK(strstr(screen,"划线与想法 · 3 条"));CHECK(app->status()&PGA_STATUS_UNSAVED);collect_book();CHECK(*collected);CHECK(app->save());CHECK(writes==1);CHECK(!app->key(2));
    printf("Swift WRS1 fixture: %u checks PASS\n",checks);
}
static void connection_fixture(void){
    for(unsigned orientation=0;orientation<2;orientation++){
        landscape=orientation;start("valid",true);stop();
        host.bytes=PICO_APP_HOST_V1_BYTES;host.weread=weread_service;unsigned services=service_calls;
        pico_app_host_t *legacy=malloc(PICO_APP_HOST_V1_BYTES);CHECK(legacy);memcpy(legacy,&host,PICO_APP_HOST_V1_BYTES);
        CHECK(app->start(legacy));render();tap(view().w-140,88);CHECK(strstr(screen,"需要支持设备同步的新版固件"));
        tap(50,landscape?260:284);CHECK(service_calls==services&&toasts==1);CHECK(app->key(-1));stop();free(legacy);
        start("valid",true);stop();memset(&service_status,0,sizeof(service_status));service_status.bytes=sizeof(service_status);service_status.revision=1;
        host.weread=weread_service;service_calls=setup_calls=sync_calls=0;sync_accepted=true;
        CHECK(app->start(&host));render();CHECK(strstr(screen,"连接与同步"));tap(view().w-140,88);
        CHECK(strstr(screen,"手机配置，Pico 独立同步")&&strstr(screen,"官方 API Key")&&!strstr(screen,"多屏灵犀"));
        unsigned io=reads;render();render();CHECK(io==reads);tap(50,landscape?260:284);CHECK(setup_calls==1&&sync_calls==0);
        tap(view().w-50,landscape?260:284);CHECK(setup_calls==2&&sync_calls==0);
        CHECK(app->key(-1));service_status.flags=PGA_WEREAD_CONFIGURED|PGA_WEREAD_WIFI;tap(view().w-140,88);
        tap(view().w-50,landscape?260:284);CHECK(sync_calls==1&&strstr(screen,"正在同步"));
        io=reads;unsigned before_calls=service_calls;now_us=100000;CHECK(!app->tick()&&service_calls==before_calls&&reads==io);
        now_us=1000000;CHECK(!app->tick()&&reads==io);tap(view().w-50,landscape?260:284);CHECK(sync_calls==1);
        service_status.state=PGA_WEREAD_ERROR;service_status.flags&=~PGA_WEREAD_BUSY;strcpy(service_status.error,"auth");service_status.revision++;
        now_us=2000000;CHECK(app->tick()&&reads==io);render();CHECK(strstr(screen,"API Key 已失效"));
        service_status.state=PGA_WEREAD_DONE;service_status.error[0]=0;service_status.revision++;service_status.books=9;service_status.notes=270;
        now_us=3000000;CHECK(app->tick()&&reads>io);render();CHECK(strstr(screen,"同步完成 · 9 本书 · 270 条笔记"));
        io=reads;now_us=4000000;CHECK(!app->tick()&&reads==io);CHECK(app->key(-1));render();CHECK(strstr(screen,"三体"));
        tap(50,130);CHECK(strstr(screen,"划线与想法"));service_status.revision++;now_us=5000000;CHECK(!app->tick()&&reads==io+1);
        CHECK(app->key(-1));now_us=6000000;CHECK(app->tick());render();CHECK(strstr(screen,"三体"));stop();
    }
}

#include "weread_explore_module_test.h"

#ifdef WRS_DIRECT
extern const pico_app_exports_t *pico_app_query(void);
#endif
int main(int argc,char **argv){
    CHECK(argc==3||argc==4);fixture_root=argv[2];void *code=NULL;uint8_t *payload=NULL;
    pico_app_package_t package;char error[160];CHECK(pico_app_package_read(argv[1],&package,true,error,sizeof(error)));CHECK(!strcmp(package.id,"weread")&&package.kind==WRS_APP_KIND&&package.version==WRS_APP_VERSION);
#ifdef WRS_DIRECT
    app=pico_app_query();
#else
    const pico_app_payload_t *part=pico_app_package_payload(&package);CHECK(part);payload=malloc(part->bytes);CHECK(payload);FILE *file=fopen(argv[1],"rb");CHECK(file&&fseek(file,(long)part->offset,SEEK_SET)==0&&fread(payload,1,part->bytes,file)==part->bytes&&fclose(file)==0);
    CHECK(pico_app_code_open(payload,part->bytes,&app,&code,error,sizeof(error)));
#endif
    CHECK(app->abi==PICO_APP_ABI&&app->kind==WRS_APP_KIND&&!strcmp(app->id,"weread"));CHECK(!app->start(NULL));pico_app_host_t bad=host;bad.abi++;CHECK(!app->start(&bad));bad=host;bad.compare=NULL;CHECK(!app->start(&bad));bad=host;bad.bytes=PICO_APP_HOST_V1_BYTES-1;CHECK(!app->start(&bad));
    if(argc==4){CHECK(!strcmp(argv[3],"--external"));external_fixture();goto done;}
    for(unsigned orientation=0;orientation<2;orientation++){
        landscape=orientation;
        start("missing",true);CHECK(strstr(screen,"还没有同步书架")&&writes==0);CHECK(!app->key(2));CHECK(app->key(-1)&&navigations==1);stop();
        start("empty",true);CHECK(strstr(screen,"还没有同步书架")&&writes==0);stop();
        const char *bad_cases[]={"bad-index","bad-index-crc","bad-total","duplicate-id","bad-id","bad-title","bad-unused","bad-count","bad-chunk-count","bad-note-count","bad-index-padding","bad-index-size"};
        for(unsigned i=0;i<sizeof(bad_cases)/sizeof(*bad_cases);i++){start(bad_cases[i],true);CHECK(strstr(screen,"同步内容不可读取")||strstr(screen,"还没有同步书架"));CHECK(writes==0);CHECK(!event(PGA_TAP,50,130));stop();}
        start("valid",true);CHECK(strstr(screen,"三体")&&strstr(screen,"270 条笔记"));CHECK(app->key(2));render();CHECK(strstr(screen,"第")&&app->key(0));render();
        tap(50,130);CHECK(strstr(screen,"划线与想法 · 270 条"));CHECK(app->status()&PGA_STATUS_UNSAVED);unsigned io=reads;render();render();CHECK(io==reads&&writes==0);
        collect_book();check_expected();CHECK(!app->key(2));CHECK(app->save()&&writes==1);CHECK(!memcmp(progress,"PWRPOS01",8));CHECK(le32(progress+WRS_P_CHUNK)==1);CHECK(app->key(-1));render();CHECK(strstr(screen,"书架与笔记"));CHECK(app->key(-1)&&navigations==1);stop();
        start("valid",true);tap(50,130);CHECK(app->key(2));render();char saved_body[131072];strcpy(saved_body,body);CHECK(app->save());unsigned offset=le32(progress+WRS_P_OFFSET);CHECK(offset>0);stop();
        start("valid",false);tap(50,130);CHECK(!strcmp(saved_body,body));CHECK(le32(progress+WRS_P_OFFSET)==offset);CHECK(app->key(0));render();CHECK(strstr(body,"划线0000"));
        CHECK(app->key(2));fail_store=true;CHECK(!app->save());CHECK(app->status()&PGA_STATUS_UNSAVED);render();CHECK(!strcmp(saved_body,body));fail_store=false;CHECK(app->save());CHECK(!(app->status()&PGA_STATUS_UNSAVED));
        CHECK(app->key(2));unsigned before=writes;now_us=3000000;CHECK(!event(PGA_PRESS,40,130));CHECK(!app->tick()&&writes==before);CHECK(!event(PGA_RELEASE,40,130));now_us=5000000;CHECK(!app->tick()&&writes==before+1);stop();
        start("valid",true);tap(50,130);CHECK(event(PGA_SWIPE_L,100,140));CHECK(event(PGA_SWIPE_R,100,140));render();CHECK(strstr(body,"划线0000"));CHECK(app->key(-1));render();tap(50,202);CHECK(strstr(screen,"这本书还没有笔记"));CHECK(!app->key(2)&&!app->key(0));CHECK(app->save());stop();
        const char *bad_chunks[]={"missing-chunk","bad-chunk-crc","bad-utf8","bad-surrogate","bad-control","bad-chunk-generation","bad-chunk-slot","bad-chunk-index","bad-chunk-total","bad-chunk-padding","bad-text-count","bad-text-zero","bad-chunk-size"};
        for(unsigned i=0;i<sizeof(bad_chunks)/sizeof(*bad_chunks);i++){start(bad_chunks[i],true);tap(50,130);CHECK(strstr(screen,"这段笔记暂时无法读取"));CHECK(writes==0);tap(30,view().h-100);CHECK(strstr(screen,"暂时无法读取"));CHECK(writes==0);CHECK(app->key(-1));render();CHECK(strstr(screen,"书架与笔记"));stop();}
        start("fallback",true);CHECK(strstr(screen,"三体"));tap(50,130);CHECK(!strstr(screen,"暂时无法读取"));CHECK(app->save());stop();
        start("full-block",true);tap(50,130);collect_book();char compacted[262144];compact(compacted,collected);CHECK(strlen(compacted)==WRS_TEXT_BYTES);for(unsigned i=0;i<WRS_TEXT_BYTES;i++)CHECK(compacted[i]=='A');CHECK(app->save());stop();
        start("valid",true);tap(50,130);for(unsigned i=0;i<4;i++)CHECK(app->key(2));unsigned read_count=reads;for(unsigned i=0;i<12;i++){landscape=!landscape;render();CHECK(reads==read_count);}CHECK(app->save());stop();landscape=orientation;
    }
    // 包含最大合法书槽和块号，校验键不会越界或以用户数据生成路径。
    // Exercise the largest valid book/chunk indices without deriving paths from user text.
    landscape=false;start("maximum",true);for(unsigned i=1;i<11;i++)CHECK(app->key(2));render();tap(50,112+3*72+10);CHECK(strstr(screen,"划线与想法 · 256 条"));for(unsigned i=1;i<256;i++)CHECK(app->key(2));CHECK(!app->key(2));CHECK(app->save());CHECK(le32(progress+WRS_P_BOOK)==63&&le32(progress+WRS_P_CHUNK)==255);stop();
    connection_fixture();explore_fixture();printf("WeRead TF module: %u checks PASS\n",checks);
done:
    app->stop();if(code)pico_app_code_close(code);free(payload);return 0;
}

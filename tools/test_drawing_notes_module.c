/* SPDX-License-Identifier: Apache-2.0
 * 中文：通过真实PGA解析器与动态加载器验证画板原稿、增量绘图和保存故障。
 * English: Exercise drawing originals, incremental ink and storage failures through actual PGA parsing and loading.
 */
#include "pico_app_loader.h"
#include "pico_app_package.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

enum { WIDTH=640,HEIGHT=640,PIXELS=WIDTH*HEIGHT };
static uint8_t frame[PIXELS],reference[PIXELS],snapshot[PIXELS];
static struct {char key[32];size_t bytes;uint8_t data[PICO_APP_MAX_STATE];} states[16];
static unsigned state_count,reads,writes,fail_write,checks;
static int64_t now_us;
static bool landscape;
static const pico_app_exports_t *app;
static void *code;
static unsigned char *payload;
static size_t payload_bytes;
#define CHECK(x) do{checks++;assert(x);}while(0)
static pico_app_view_t view(void){return (pico_app_view_t){landscape?640:360,landscape?360:640,landscape};}
static int32_t page(void){return PGA_PAGE_APP;}
static void navigate(int32_t p){CHECK(p==PGA_PAGE_LAUNCHER||p==PGA_PAGE_APP);}
static void noarg(void){}
static void textonly(const char *s){CHECK(s&&*s);}
static bool load(const char *key,void *out,size_t bytes){reads++;for(unsigned i=0;i<state_count;i++)if(!strcmp(states[i].key,key)&&states[i].bytes==bytes){memcpy(out,states[i].data,bytes);return true;}return false;}
static bool store(const char *key,const void *data,size_t bytes){
    writes++;if(fail_write&&writes==fail_write)return false;
    CHECK(bytes<=PICO_APP_MAX_STATE);unsigned slot=0;while(slot<state_count&&strcmp(states[slot].key,key))slot++;
    if(slot==state_count){CHECK(state_count<16);state_count++;snprintf(states[slot].key,32,"%s",key);}
    states[slot].bytes=bytes;memcpy(states[slot].data,data,bytes);return true;
}
static int64_t time_us(void){return now_us;}
static void pixel(uint8_t *fb,int x,int y,int gray){pico_app_view_t v=view();if(x>=0&&y>=0&&x<v.w&&y<v.h)fb[y*WIDTH+x]=(uint8_t)gray;}
static void fill(uint8_t *fb,int x,int y,int w,int h,int gray){for(int yy=y;yy<y+h;yy++)for(int xx=x;xx<x+w;xx++)pixel(fb,xx,yy,gray);}
static void line(uint8_t *fb,int x,int y,int xx,int yy,int gray){int dx=abs(xx-x),sx=x<xx?1:-1,dy=-abs(yy-y),sy=y<yy?1:-1,err=dx+dy;for(;;){pixel(fb,x,y,gray);if(x==xx&&y==yy)break;int e=2*err;if(e>=dy){err+=dy;x+=sx;}if(e<=dx){err+=dx;y+=sy;}}}
static void box(uint8_t *fb,int x,int y,int w,int h,int radius,int gray,bool solid){(void)radius;if(solid)fill(fb,x,y,w,h,gray);else{line(fb,x,y,x+w-1,y,gray);line(fb,x,y,x,y+h-1,gray);line(fb,x+w-1,y,x+w-1,y+h-1,gray);line(fb,x,y+h-1,x+w-1,y+h-1,gray);}}
static void text(uint8_t *fb,int x,int y,int height,const char *s,int gray){(void)fb;(void)x;(void)y;(void)height;(void)s;(void)gray;}
static void wrap(uint8_t *fb,int x,int y,int w,int size,int rows,const char *s,int gray){(void)w;(void)rows;text(fb,x,y,size,s,gray);}
static void button(uint8_t *fb,int x,int y,int w,int h,const char *s,bool selected){(void)s;box(fb,x,y,w,h,0,selected?0:15,true);}
static void heading(uint8_t *fb,const char *s,const char *subtitle){(void)fb;(void)s;(void)subtitle;}
static const pico_app_host_t host={.abi=1,.bytes=sizeof(host),.view=view,.page=page,.navigate=navigate,.dirty=noarg,.toast=textonly,
    .load=load,.store=store,.time_us=time_us,.fill=fill,.line=line,.box=box,.text=text,.center=text,.wrap=wrap,.button=button,.heading=heading,
    .format=snprintf,.copy=memcpy,.move=memmove,.set=memset,.compare=memcmp};
static void start(void){char error[160];CHECK(pico_app_code_open(payload,payload_bytes,&app,&code,error,sizeof(error)));CHECK(app->kind==PGA_DRAWING&&app->bytes>=sizeof(*app)&&app->ink);CHECK(app->start(&host));memset(frame,0,sizeof(frame));app->render(frame);}
static void close_app(void){app->stop();pico_app_code_close(code);app=NULL;code=NULL;}
static bool event(int type,int x,int y){pico_app_event_t e={.type=type,.x=x,.y=y,.ms=now_us/1000};return app->event(&e);}
static void tap(int x,int y){(void)event(PGA_PRESS,x,y);(void)event(PGA_RELEASE,x,y);CHECK(event(PGA_TAP,x,y));}
static void stroke(int y){CHECK(event(PGA_PRESS,landscape?200:105,y));CHECK(event(PGA_MOVE,landscape?300:160,y+20));CHECK(event(PGA_MOVE,landscape?400:225,y+40));CHECK(event(PGA_RELEASE,landscape?500:270,y+50));}
static void tool_tap(int index){int w=(view().w-40)/5;tap(landscape?74:12+index*(w+4)+w/2,landscape?106+index*44:90);}
static unsigned saved_points(void){for(unsigned i=0;i<state_count;i++)if(!strcmp(states[i].key,"board01"))return states[i].data[10]|(unsigned)states[i].data[11]<<8;return 0;}
static unsigned state_slot(const char *key){unsigned i=0;while(i<state_count&&strcmp(states[i].key,key))i++;CHECK(i<state_count);return i;}
static void incremental_equal(void){int32_t regions[4][4];unsigned io=reads+writes;CHECK(app->ink(frame,regions)==1);CHECK(reads+writes==io);CHECK(regions[0][0]>=0&&regions[0][1]>=0&&regions[0][2]>0&&regions[0][3]>0);CHECK(regions[0][0]+regions[0][2]<=view().w&&regions[0][1]+regions[0][3]<=view().h);memset(reference,0,sizeof(reference));app->render(reference);CHECK(!memcmp(frame,reference,PIXELS));CHECK(app->ink(frame,regions)==0);}
int main(int argc,char **argv){
    CHECK(argc==2||argc==3);pico_app_package_t package;char error[160];CHECK(pico_app_package_read(argv[1],&package,true,error,sizeof(error)));const pico_app_payload_t *part=pico_app_package_payload(&package);CHECK(part);payload_bytes=part->bytes;payload=malloc(payload_bytes);CHECK(payload);FILE *f=fopen(argv[1],"rb");CHECK(f&&fseek(f,(long)part->offset,SEEK_SET)==0&&fread(payload,1,payload_bytes,f)==payload_bytes&&fclose(f)==0);
    if(argc==3){pico_app_package_t old;CHECK(pico_app_package_read(argv[2],&old,true,error,sizeof(error)));const pico_app_payload_t *p=pico_app_package_payload(&old);CHECK(p);uint8_t *bytes=malloc(p->bytes);f=fopen(argv[2],"rb");CHECK(f&&fseek(f,p->offset,SEEK_SET)==0&&fread(bytes,1,p->bytes,f)==p->bytes&&fclose(f)==0);const pico_app_exports_t *legacy;void *handle;CHECK(pico_app_code_open(bytes,p->bytes,&legacy,&handle,error,sizeof(error)));CHECK(legacy->bytes==PICO_APP_EXPORTS_V1_BYTES&&legacy->kind==PGA_CHESS);pico_app_code_close(handle);free(bytes);}
    for(int orientation=0;orientation<2;orientation++){
        landscape=orientation;state_count=reads=writes=fail_write=0;now_us=0;start();tap(55,145);CHECK(app->status()&PGA_STATUS_INK);app->render(frame);memcpy(snapshot,frame,PIXELS);unsigned before_io=reads+writes;stroke(160);CHECK(reads+writes==before_io);incremental_equal();CHECK(memcmp(snapshot,frame,PIXELS));CHECK(app->status()&PGA_STATUS_UNSAVED);CHECK(app->save());unsigned first=saved_points();CHECK(first==4);CHECK(!(app->status()&PGA_STATUS_UNSAVED));memcpy(snapshot,frame,PIXELS);close_app();
        start();tap(55,145);app->render(frame);CHECK(!memcmp(snapshot,frame,PIXELS));stroke(180);incremental_equal();CHECK(app->save()&&saved_points()==first+4);
        tool_tap(2);int32_t regions[4][4];CHECK(app->ink(frame,regions)==-1);app->render(frame);CHECK(app->save()&&saved_points()==first);CHECK(!memcmp(snapshot,frame,PIXELS));
        tool_tap(3);CHECK(!(app->status()&PGA_STATUS_INK));tap(2,25);CHECK(app->status()&PGA_STATUS_INK);app->render(frame);CHECK(!memcmp(snapshot,frame,PIXELS));
        stroke(190);incremental_equal();memcpy(snapshot,frame,PIXELS);fail_write=writes+1;CHECK(!app->save());CHECK(app->status()&PGA_STATUS_UNSAVED);app->stop();app->render(frame);CHECK(!memcmp(snapshot,frame,PIXELS));fail_write=writes+1;CHECK(app->key(-1));CHECK(app->status()&PGA_STATUS_INK);
        // key()仍可接管失败；后续一次写入允许重试，不能依赖失败画板被清空。
        // A later write may retry; never rely on clearing the failed drawing.
        fail_write=0;CHECK(app->save());CHECK(saved_points()==first+4);CHECK(app->key(-1));CHECK(!(app->status()&PGA_STATUS_INK));close_app();
        start();tap(55,145);app->render(frame);stroke(200);incremental_equal();unsigned before=writes;now_us=1000000;CHECK(!app->tick()&&writes==before);now_us=3000000;CHECK(!app->tick()&&writes==before+2);CHECK(!(app->status()&PGA_STATUS_UNSAVED));close_app();

        // 目录写失败后，原稿已提交；重启时即使入口显示“新建”，仍优先读取孤立原稿。
        // A failed catalog write leaves the committed original recoverable through every fixed board slot.
        state_count=reads=writes=fail_write=0;now_us=0;start();tap(55,145);app->render(frame);stroke(170);incremental_equal();memcpy(snapshot,frame,PIXELS);
        fail_write=writes+2;CHECK(!app->save());CHECK(app->status()&PGA_STATUS_UNSAVED);CHECK(saved_points()==4);CHECK(state_count==1&&!strcmp(states[0].key,"board01"));close_app();
        fail_write=0;start();tap(55,145);app->render(frame);CHECK(!memcmp(snapshot,frame,PIXELS));CHECK(!(app->status()&PGA_STATUS_UNSAVED));stroke(210);incremental_equal();CHECK(app->save());CHECK(saved_points()==8&&state_count==2);close_app();

        // 有目录记录的原稿读取失败或损坏时，不得打开空画板并覆盖用户内容。
        // Cataloged originals must not be replaced with blank drawings after a read/validation failure.
        unsigned slot=state_slot("board01"),saved_bytes=(unsigned)states[slot].bytes;before=writes;
        states[slot].bytes--;start();CHECK(!event(PGA_TAP,55,145));CHECK(!(app->status()&PGA_STATUS_INK));CHECK(writes==before);close_app();states[slot].bytes=saved_bytes;
        uint8_t magic=states[slot].data[0];states[slot].data[0]=0;start();CHECK(!event(PGA_TAP,55,145));CHECK(!(app->status()&PGA_STATUS_INK));CHECK(writes==before);close_app();states[slot].data[0]=magic;
        start();tap(55,145);CHECK(app->status()&PGA_STATUS_INK);CHECK(saved_points()==8);CHECK(writes==before);close_app();
    }
    // 新纸横竖可写区互为转置，点阵与笔迹随完整纸面旋转；边缘输入不裁切原稿。
    // Transposed new-paper areas rotate the whole dotted page; edge input does not crop the original.
    landscape=false;state_count=reads=writes=fail_write=0;now_us=0;start();tap(55,145);app->render(frame);memcpy(snapshot,frame,PIXELS);
    unsigned dots=0;for(int y=120;y<600;y++)for(int x=12;x<348;x++)if(frame[y*WIDTH+x]==7)dots++;CHECK(dots>1000);
    unsigned before=reads+writes;landscape=true;memset(frame,0,sizeof(frame));app->render(frame);CHECK(reads+writes==before);
    for(int y=0;y<480;y++)for(int x=0;x<336;x++)CHECK(snapshot[(120+y)*WIDTH+12+x]==frame[(24+x)*WIDTH+148+479-y]);
    CHECK(event(PGA_PRESS,154,90));CHECK(event(PGA_MOVE,158,100));CHECK(event(PGA_RELEASE,165,110));
    CHECK(event(PGA_PRESS,620,160));CHECK(event(PGA_MOVE,612,170));CHECK(event(PGA_RELEASE,605,180));incremental_equal();CHECK(app->save());CHECK(saved_points()==6);
    uint8_t saved_original[49168];memcpy(saved_original,states[state_slot("board01")].data,sizeof(saved_original));memcpy(snapshot,frame,PIXELS);before=reads+writes;
    CHECK((saved_original[6]|(unsigned)saved_original[7]<<8)==1400);CHECK((saved_original[8]|(unsigned)saved_original[9]<<8)==2000);
    for(unsigned turn=0;turn<12;turn++){landscape=!landscape;memset(frame,0,sizeof(frame));app->render(frame);CHECK(app->save());CHECK(reads+writes==before);if(landscape)CHECK(!memcmp(snapshot,frame,PIXELS));}
    close_app();start();tap(55,145);app->render(frame);CHECK(!memcmp(snapshot,frame,PIXELS));CHECK(!memcmp(saved_original,states[state_slot("board01")].data,sizeof(saved_original)));close_app();

    // 旧矩形与正方形不迁移、不丢点；旧平移保留字节不再裁切纸面。
    // Neither legacy shape is migrated or truncated; old pan bytes remain stored but never crop the page.
    for(int square=0;square<2;square++){
        landscape=false;state_count=1;reads=writes=fail_write=0;memset(states,0,sizeof(states));snprintf(states[0].key,32,"board01");states[0].bytes=49168;
        uint8_t *data=states[0].data;const uint8_t header[]={0x50,0x44,0x4e,0x31,1,0,0xe8,3,0x78,5,2,0,0x20,3,0x20,3};memcpy(data,header,sizeof(header));
        if(square){data[6]=data[8]=0x40;data[7]=data[9]=6;}
        const uint8_t points[]={100,0,100,0,8,1,0x2c,1,0xc8,0,8,0};memcpy(data+16,points,sizeof(points));memcpy(saved_original,data,sizeof(saved_original));
        start();tap(55,145);app->render(frame);memcpy(snapshot,frame,PIXELS);before=reads+writes;
        for(unsigned turn=0;turn<12;turn++){landscape=!landscape;memset(frame,0,sizeof(frame));app->render(frame);CHECK(app->save());CHECK(reads+writes==before);if(!landscape)CHECK(!memcmp(snapshot,frame,PIXELS));}
        stroke(200);incremental_equal();CHECK(app->save());data=states[state_slot("board01")].data;
        CHECK(!memcmp(saved_original,data,10));CHECK(!memcmp(saved_original+12,data+12,16));CHECK(saved_points()==6);
        memcpy(snapshot,frame,PIXELS);close_app();start();tap(55,145);app->render(frame);CHECK(!memcmp(snapshot,frame,PIXELS));close_app();
    }
    // 擦过空白点阵应逐字节恢复；再覆写、擦除、撤销的增量结果必须等同完整重绘。
    // Erasing blank dots restores every byte; ink/erase/undo incremental results match complete rendering.
    for(int orientation=0;orientation<2;orientation++){
        landscape=orientation;state_count=reads=writes=fail_write=0;start();tap(55,145);app->render(frame);memcpy(snapshot,frame,PIXELS);
        tool_tap(0);app->render(frame);stroke(170);incremental_equal();
        int x0=landscape?148:12,y0=landscape?24:120,w=landscape?480:336,h=landscape?336:480;
        for(int y=y0;y<y0+h;y++)for(int x=x0;x<x0+w;x++)CHECK(frame[y*WIDTH+x]==snapshot[y*WIDTH+x]);
        tool_tap(0);app->render(frame);stroke(180);incremental_equal();tool_tap(0);app->render(frame);stroke(180);incremental_equal();
        CHECK(app->key(0));app->render(frame);CHECK(app->key(2));CHECK(!(app->status()&PGA_STATUS_UNSAVED));close_app();
    }
    free(payload);printf("Drawing TF module: %u checks PASS\n",checks);return 0;
}

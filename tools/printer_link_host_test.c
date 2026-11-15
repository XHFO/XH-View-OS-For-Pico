/* SPDX-License-Identifier: Apache-2.0
 * 中文：执行独立打印机应用的真实页面与草稿流程；仅模拟宿主边界。
 * English: Exercise the actual independent printer UI and drafts with mocked host boundaries only.
 */
#include "pico_app_sdk.h"
#include <assert.h>
#include <dlfcn.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
static pico_app_view_t viewport;
static pico_app_printer_config_t configs[3];
static unsigned revision=1,reads,writes,syncs,opens;
static bool fail_save,input_ready,online=true;
static char typed[256],heading[96],paint[2048];
static int navigated;
static pico_app_view_t view(void){return viewport;}
static void dirty(void){}
static void navigate(int32_t p){navigated=p;}
static void toast(const char *s){(void)s;}
static int format(char *out,size_t n,const char *fmt,...){va_list a;va_start(a,fmt);int r=vsnprintf(out,n,fmt,a);va_end(a);return r;}
static void record(const char *s){if(strlen(paint)+strlen(s)+2<sizeof(paint)){strcat(paint,s);strcat(paint,"\n");}}
static void fill(uint8_t *f,int x,int y,int w,int h,int c){(void)f;(void)c;assert(x>=0&&y>=0&&x+w<=viewport.w&&y+h<=viewport.h);}
static void box(uint8_t *f,int x,int y,int w,int h,int r,int c,bool b){(void)r;(void)b;fill(f,x,y,w,h,c);}
static void wrap(uint8_t *f,int x,int y,int w,int z,int n,const char *s,int c){(void)f;(void)z;(void)n;(void)c;assert(x>=0&&y>=0&&x+w<=viewport.w);record(s);}
static void button(uint8_t *f,int x,int y,int w,int h,const char *s,bool selected){(void)selected;fill(f,x,y,w,h,0);record(s);}
static void title(uint8_t *f,const char *s,const char *sub){(void)f;snprintf(heading,sizeof(heading),"%s",s);record(sub);}
static bool service(uint32_t cmd,pico_app_home_status_t *s){
    reads++;memset(s,0,sizeof(*s));s->bytes=sizeof(*s);s->revision=revision;s->flags=PGA_HOME_CFG|(online?PGA_HOME_WIFI:0);
    if(cmd==PGA_HOME_SYNC)syncs++;
    if(cmd==PGA_HOME_OPEN_SETUP)opens++;
    return true;
}
static bool item(uint32_t i,pico_app_home_printer_t *p){assert(i<3);reads++;memset(p,0,sizeof(*p));p->bytes=sizeof(*p);p->percent=-1;return true;}
static bool config(uint32_t cmd,pico_app_printer_config_t *e){
    assert(e->slot<3);reads++;unsigned i=e->slot;
    if(cmd==PGA_PRINTER_GET){*e=configs[i];e->bytes=sizeof(*e);e->slot=i;e->revision=revision;return true;}
    writes++;
    if(fail_save){strcpy(e->error,"保存失败，草稿保留");return false;}
    assert(e->revision==revision);
    if(cmd==PGA_PRINTER_REMOVE)memset(&configs[i],0,sizeof(configs[i]));else configs[i]=*e;
    revision++;return true;
}
static bool input(uint32_t cmd,char *value,size_t n){
    if(cmd==PGA_HOME_INPUT_OPEN)return true;
    if(cmd==PGA_HOME_INPUT_TAKE&&input_ready){snprintf(value,n,"%s",typed);input_ready=false;return true;}
    return false;
}
static bool candidate(uint32_t index,pico_app_printer_config_t *p){
    reads++;if(index>=6)return false;
    memset(p,0,sizeof(*p));p->bytes=sizeof(*p);p->source=1;
    snprintf(p->name,sizeof(p->name),"Discovered %u",index);snprintf(p->serial,sizeof(p->serial),"sensor.printer_%u",index);return true;
}
static pico_app_host_t host={.abi=PICO_APP_ABI,.bytes=sizeof(host),.view=view,.dirty=dirty,.navigate=navigate,.toast=toast,
    .fill=fill,.box=box,.wrap=wrap,.button=button,.heading=title,.format=format,.copy=memcpy,.set=memset,
    .printer_link=service,.printer_item=item,.printer_config=config,.printer_input=input};
static void render(const pico_app_exports_t *a,const char *expected){unsigned before=reads;paint[0]=0;a->render(NULL);assert(reads==before&&!strcmp(heading,expected));}
static void tap(const pico_app_exports_t *a,int x,int y){assert(a->event(&(pico_app_event_t){.type=PGA_TAP,.x=x,.y=y}));}
static void bottom(const pico_app_exports_t *a,unsigned i){tap(a,20+(int)i*((viewport.w-48)/3+8),viewport.h-36);}
static void enter(const pico_app_exports_t *a,unsigned f,const char *text){int y=viewport.landscape?116:164;tap(a,40,y+20+(int)f*52);snprintf(typed,sizeof(typed),"%s",text);input_ready=true;a->tick();}
int main(int argc,char **argv){
    assert(argc==2);void *lib=dlopen(argv[1],RTLD_NOW|RTLD_LOCAL);assert(lib);
    const pico_app_exports_t *(*query)(void)=(const pico_app_exports_t *(*)(void))dlsym(lib,"pico_app_query");assert(query);
    const pico_app_exports_t *a=query();assert(a->kind==PGA_PRINTER_LINK&&!strcmp(a->id,"printer-link"));
    for(int landscape=0;landscape<2;landscape++){
        viewport=landscape?(pico_app_view_t){640,360,true}:(pico_app_view_t){360,640,false};
        memset(configs,0,sizeof(configs));writes=0;assert(a->start(&host));render(a,"3D 打印机");
        bottom(a,2);render(a,"添加打印机");enter(a,0,"Draft");bottom(a,0);assert(!writes);render(a,"3D 打印机");
        bottom(a,2);enter(a,0,"Printer One");enter(a,1,"sensor.p1s");fail_save=true;bottom(a,2);render(a,"添加打印机");
        assert(strstr(paint,"Printer One")&&strstr(paint,"sensor.p1s")&&strstr(paint,"草稿保留"));
        fail_save=false;bottom(a,2);render(a,"3D 打印机");assert(configs[0].source==1&&!strcmp(configs[0].serial,"sensor.p1s"));
        tap(a,40,landscape?130:170);render(a,"管理打印机");bottom(a,1);render(a,"移除打印机连接");
        bottom(a,0);render(a,"管理打印机");assert(configs[0].source==1);bottom(a,1);bottom(a,2);render(a,"3D 打印机");assert(!configs[0].source);
        bottom(a,2);unsigned prior=opens;bottom(a,1);assert(opens==prior+1);a->key(-1);
        configs[0].source=2;strcpy(configs[0].name,"Legacy Bambu");strcpy(configs[0].serial,"retained");revision++;a->tick();
        render(a,"3D 打印机");assert(strstr(paint,"直连暂不可用"));tap(a,40,landscape?130:170);enter(a,0,"Renamed");bottom(a,2);
        assert(configs[0].source==2&&!strcmp(configs[0].serial,"retained")&&!strcmp(configs[0].name,"Renamed"));
        for(unsigned i=0;i<3;i++)configs[i].source=1;revision++;a->tick();bottom(a,2);render(a,"3D 打印机");assert(strstr(paint,"最多添加"));
        online=false;prior=syncs;a->tick();bottom(a,1);assert(syncs==prior);render(a,"3D 打印机");assert(strstr(paint,"连接 Wi-Fi"));online=true;
        bottom(a,0);assert(navigated==PGA_PAGE_LAUNCHER);a->stop();
    }
    host.printer_candidate=candidate;
    for(int landscape=0;landscape<2;landscape++){
        viewport=landscape?(pico_app_view_t){640,360,true}:(pico_app_view_t){360,640,false};
        memset(configs,0,sizeof(configs));writes=0;online=true;assert(a->start(&host));
        bottom(a,2);render(a,"选择打印机来源");tap(a,40,landscape?136:184);render(a,"选择打印机");
        assert(strstr(paint,"Discovered 0"));
        assert(a->event(&(pico_app_event_t){.type=PGA_SWIPE_L}));render(a,"选择打印机");assert(strstr(paint,"Discovered 4"));
        tap(a,40,landscape?138:184);render(a,"添加打印机");assert(strstr(paint,"sensor.printer_4"));
        assert(!writes);bottom(a,2);assert(writes==1&&!strcmp(configs[0].serial,"sensor.printer_4"));
        bottom(a,2);bottom(a,2);render(a,"添加打印机");bottom(a,0);assert(writes==1);a->stop();
    }
    host.bytes=PICO_APP_HOST_V1_BYTES;assert(a->start(&host));render(a,"3D 打印机");assert(strstr(paint,"请更新系统"));a->stop();
    dlclose(lib);puts("PASS: independent printer add/edit/remove/cancel, retained failed drafts and legacy configs, portal entry, full slots, offline, old host, pure bounded portrait/landscape rendering");return 0;
}

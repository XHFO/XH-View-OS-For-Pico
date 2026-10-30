/* SPDX-License-Identifier: Apache-2.0
 * 真实网络页面、TTF与QR编码截图；worker与存储仍为替身，不访问设备。
 * Actual network-page, TTF and QR captures; worker/storage remain doubles, without device access.
 */
#include "pico_system.h"
#include "pico_framebuffer_host.h"
#include "ttf_font.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {int x,y,w,h;} bounds_t;
typedef struct {bounds_t ink;int control;char text[768];} run_t;
static run_t runs[96];
static bounds_t controls[32],icons[16];
static int run_count,control_count,icon_count,active=-1;
static unsigned verify_count,failures,scenes;
static const char *scene;
static FILE *report;
static int pixel(int a){return (a*19+5)/10;}
static bool intersects(bounds_t a,bounds_t b){return a.w&&a.h&&b.w&&b.h&&a.x<b.x+b.w&&a.x+a.w>b.x&&a.y<b.y+b.h&&a.y+a.h>b.y;}
static bool contains(bounds_t a,bounds_t b){return b.x>=a.x&&b.y>=a.y&&b.x+b.w<=a.x+a.w&&b.y+b.h<=a.y+a.h;}
static void verify(bool good,const char *reason,const char *text){verify_count++;if(good)return;failures++;fprintf(stderr,"FAIL %s %s: %s (%s)\n",scene,ps_view().landscape?"landscape":"portrait",reason,text);}
void connectivity_native_text(uint8_t*,int,int,int,const char*,enum EpdFontFlags,uint8_t,uint8_t);
bool connectivity_font_has_codepoint(uint32_t);
static void glyphs(const char *text){
    const unsigned char *s=(const unsigned char*)text;
    while(*s){uint32_t cp;int bytes;if(*s<128){cp=*s;bytes=1;}else if(*s<224){cp=*s&31;bytes=2;}else if(*s<240){cp=*s&15;bytes=3;}else{cp=*s&7;bytes=4;}
        for(int i=1;i<bytes;i++)cp=(cp<<6)|(s[i]&63);
        char description[48];snprintf(description,sizeof(description),"U+%04X %.*s",(unsigned)cp,bytes,s);
        verify(connectivity_font_has_codepoint(cp),"missing glyph",description);s+=bytes;
    }
}
void ttf_draw_text_px(uint8_t *fb,int x,int y,int size,const char *text,enum EpdFontFlags align,uint8_t fg,uint8_t bg){
    ps_view_t v=ps_view();int width=v.landscape?1216:684,height=v.landscape?684:1216;
    size_t length=(size_t)width*height;uint8_t *before=malloc(length);assert(before&&run_count<96);memcpy(before,fb,length);
    connectivity_native_text(fb,x,y,size,text,align,fg,bg);
    int minx=width,miny=height,maxx=-1,maxy=-1;
    for(int row=0;row<height;row++)for(int col=0;col<width;col++)if(fb[(size_t)row*width+col]!=before[(size_t)row*width+col]){
        if(col<minx)minx=col;if(col>maxx)maxx=col;if(row<miny)miny=row;if(row>maxy)maxy=row;
    }
    free(before);run_t *r=&runs[run_count++];r->ink=maxx>=minx?(bounds_t){minx,miny,maxx-minx+1,maxy-miny+1}:(bounds_t){0};r->control=active;snprintf(r->text,sizeof(r->text),"%s",text);
    int left=x,advance=ttf_text_width_px(size,text);if(align&EPD_DRAW_ALIGN_CENTER)left-=advance/2;else if(align&EPD_DRAW_ALIGN_RIGHT)left-=advance;
    verify(left>=0&&left+advance<=width&&y-ttf_ascender_px(size)>=0&&y-ttf_ascender_px(size)+size<=height,"text metrics outside screen",text);
    verify(r->ink.w&&r->ink.h,"no visible text ink",text);glyphs(text);
}
static void traced_button(uint8_t *fb,int x,int y,int w,int h,const char *label,bool selected){
    assert(control_count<32);active=control_count;controls[control_count++]=(bounds_t){pixel(x),pixel(y),pixel(w),pixel(h)};ps_button(fb,x,y,w,h,label,selected);active=-1;
}
static void traced_icon(uint8_t *fb,int x,int y,int size,ps_icon_t icon,int gray){assert(icon_count<16);icons[icon_count++]=(bounds_t){pixel(x),pixel(y),pixel(size),pixel(size)};ps_icon(fb,x,y,size,icon,gray);}

static void traced_wrap(uint8_t *fb,int x,int y,int width,int size,int lines,const char *text,int gray){
    int first=run_count;ps_wrap(fb,x,y,width,size,lines,text,gray);
    char expected[1536],actual[1536];size_t at=0;
    for(const char *p=text;*p&&at+1<sizeof(expected);p++)if(*p!='\n')expected[at++]=*p;
    expected[at]=0;at=0;
    for(int i=first;i<run_count;i++){size_t n=strlen(runs[i].text);assert(at+n<sizeof(actual));memcpy(actual+at,runs[i].text,n);at+=n;}actual[at]=0;
    verify(!strcmp(expected,actual),"wrapped text truncated",text);
}
#define CONNECTIVITY_REAL_RENDER
#define CONNECTIVITY_REAL_QR
#define main connectivity_state_test_main
#define ps_button traced_button
#define ps_icon traced_icon
#define ps_wrap traced_wrap
#include "pico_connectivity_host_test.c"
#undef ps_wrap
#undef ps_icon
#undef ps_button
#undef main

static void save_scene(const char *dir,const char *name){
    scene=name;run_count=control_count=icon_count=0;active=-1;uint8_t *fb=pico_fb_new();assert(fb);
    int before_card=card_reads;
    int before_reads=reads,before_commands=commands,before_capacity=capacity_reads,before_prepare=prepares;
    unsigned before=failures;ps_connectivity_render(fb);
    verify(card_reads==before_card&&reads==before_reads&&commands==before_commands&&capacity_reads==before_capacity&&prepares==before_prepare,"render has side effects","");
    bounds_t screen={0,0,pixel(view.w),pixel(view.h)};
    for(int i=0;i<run_count;i++){
        verify(contains(screen,runs[i].ink),"ink outside screen",runs[i].text);
        for(int j=i+1;j<run_count;j++){char pair[1536];snprintf(pair,sizeof(pair),"%s / %s",runs[i].text,runs[j].text);verify(!intersects(runs[i].ink,runs[j].ink),"text overlap",pair);}
        for(int j=0;j<control_count;j++)verify(runs[i].control==j?contains(controls[j],runs[i].ink):!intersects(controls[j],runs[i].ink),runs[i].control==j?"button label outside control":"text overlaps control",runs[i].text);
        for(int j=0;j<icon_count;j++)verify(!intersects(icons[j],runs[i].ink),"text overlaps icon",runs[i].text);
    }
    char path[768];snprintf(path,sizeof(path),"%s/%s-%s.pgm",dir,name,view.landscape?"landscape":"portrait");assert(pico_fb_save(path,fb));free(fb);
    fprintf(report,"%s\t%s\t%d\t%d\t%u\n",name,view.landscape?"landscape":"portrait",run_count,control_count,failures-before);scenes++;
}
static void capture(const char *dir){
    fixture();save_scene(dir,"wifi-nearby");cc_scroll=8;save_scene(dir,"wifi-long-ssid");cc_tab=1;cc_scroll=0;save_scene(dir,"wifi-saved");
    detail(0);save_scene(dir,"wifi-detail");action(CC_FORGET);save_scene(dir,"wifi-forget");action(CC_CONFIRM_CANCEL);ps_connectivity_back();
    engine.connected=true;strcpy(engine.ssid,engine.saved[0].ssid);strcpy(engine.ip,"192.168.50.123");cc_status=engine;save_scene(dir,"wifi-connected");detail(0);save_scene(dir,"wifi-connected-detail");
    fixture();engine.network_count=engine.saved_count=0;engine.wifi_enabled=false;cc_status=engine;save_scene(dir,"wifi-disabled");
    engine.wifi_enabled=true;engine.scanning=engine.busy=true;cc_status=engine;save_scene(dir,"wifi-scanning");
    fixture();engine.network_count=engine.saved_count=0;engine.wifi_enabled=false;engine.state=READ_PICO_CONNECTIVITY_DISABLED;cc_status=engine;
    action(CC_WIFI_TOGGLE);action(CC_SCAN);save_scene(dir,"wifi-enable-pending");
    complete_job();tick();save_scene(dir,"wifi-first-scan");
    scan_failure(READ_PICO_CONNECTIVITY_SCAN_START);tick();save_scene(dir,"wifi-scan-start-failed");
    action(CC_SCAN);scan_failure(READ_PICO_CONNECTIVITY_SCAN_RESULT);tick();save_scene(dir,"wifi-scan-result-failed");
    action(CC_SCAN);scan_failure(READ_PICO_CONNECTIVITY_SCAN_TIMEOUT);tick();save_scene(dir,"wifi-scan-timeout");
    action(CC_SCAN);complete_job();tick();save_scene(dir,"wifi-scan-empty");
    fixture();engine.network_count=engine.saved_count=0;engine.wifi_enabled=false;cc_status=engine;
    action(CC_ADD);name_done("家庭 Wi-Fi");save_scene(dir,"wifi-manual-password");
    action(CC_SECURITY);save_scene(dir,"wifi-manual-open");
    fixture();ps_navigate(PS_TRANSFER);save_scene(dir,"transfer-method");
    sd_card.present=sd_card.mounted=true;sd_card.capacity_bytes=32000000000ULL;sd_card.free_bytes=20000000000ULL;tick();save_scene(dir,"transfer-hub-mounted");
    sd_card.read_verified=sd_card.write_verified=true;tick();save_scene(dir,"transfer-hub-verified");
    sd_card.exported=true;tick();save_scene(dir,"transfer-hub-exported");sd_card.exported=false;fail_prepare=1;action(CC_TRANSFER_AP);save_scene(dir,"transfer-storage-error");
    fixture();ps_navigate(PS_TRANSFER);action(CC_TRANSFER_AP);complete_job();tick();save_scene(dir,"transfer-ap-join");action(CC_QR_URL);save_scene(dir,"transfer-ap-url");
    engine.transfer.state=READ_PICO_TRANSFER_UPLOADING;engine.transfer.cur_bytes=501000;engine.transfer.cur_total=1000000;engine.transfer.done_count=9;strcpy(engine.transfer.cur_name,"ABCDEFGHIJKLMNOPQRSTUVWXYZ-很长的图书文件名.epub");tick();save_scene(dir,"transfer-upload");
    ps_connectivity_before_leave(PS_HA);save_scene(dir,"transfer-confirm");action(CC_CONFIRM_YES);save_scene(dir,"transfer-stopping");fail_stop=1;complete_job();tick();save_scene(dir,"transfer-stop-failed");
    fixture();engine.connected=true;strcpy(engine.ssid,"ABCDEFGHIJKLMNOPQRSTUVWXYZ123456");cc_status=engine;ps_navigate(PS_TRANSFER);action(CC_TRANSFER_STA);save_scene(dir,"transfer-preparing");complete_job();strcpy(engine.transfer.url,"http://192.168.50.123");tick();save_scene(dir,"transfer-lan");
    fixture();ps_navigate(PS_TRANSFER);action(CC_TRANSFER_AP);engine.busy=false;engine.last_error=ESP_FAIL;engine.transfer.state=READ_PICO_TRANSFER_ERROR;engine.transfer_active=false;job=0;tick();save_scene(dir,"transfer-start-failed");
    fixture();engine.connected=false;engine.wifi_enabled=false;engine.saved_count=engine.network_count=0;ps_navigate(PS_WIRELESS_MANAGE);save_scene(dir,"manage-first-use");
    fixture();engine.connected=true;strcpy(engine.ip,"192.168.50.123");ps_navigate(PS_WIRELESS_MANAGE);save_scene(dir,"manage-active");
    action(CC_MANAGE_STOP);save_scene(dir,"manage-stopped");
}
int main(int argc,char **argv){
    assert(argc==3&&pico_fb_init(argv[1]));char path[768];snprintf(path,sizeof(path),"%s/geometry.tsv",argv[2]);report=fopen(path,"w");assert(report);fputs("scene\torientation\ttext_runs\tbuttons\tfailures\n",report);
    for(int land=0;land<2;land++){view=land?(ps_view_t){640,360,true}:(ps_view_t){360,640,false};capture(argv[2]);}
    fclose(report);pico_fb_close();printf("Connectivity actual-source/font/QR render: %u scenes, %u bounds/ink/glyph checks, %u failures. Network/storage doubles; no serial access.\n",scenes,verify_count,failures);return failures?1:0;
}

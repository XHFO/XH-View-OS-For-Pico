/* SPDX-License-Identifier: Apache-2.0
 * 真实 USB 页面和字体截图；USB与存储为替身，不访问设备。
 * Actual USB-page/font captures with USB/storage doubles, without device access.
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
void usb_ui_native_text(uint8_t*,int,int,int,const char*,enum EpdFontFlags,uint8_t,uint8_t);
bool usb_ui_font_has_codepoint(uint32_t);
static void glyphs(const char *text){
    const unsigned char *s=(const unsigned char*)text;
    while(*s){uint32_t cp;int bytes;if(*s<128){cp=*s;bytes=1;}else if(*s<224){cp=*s&31;bytes=2;}else if(*s<240){cp=*s&15;bytes=3;}else{cp=*s&7;bytes=4;}
        for(int i=1;i<bytes;i++)cp=(cp<<6)|(s[i]&63);
        char description[48];snprintf(description,sizeof(description),"U+%04X %.*s",(unsigned)cp,bytes,s);
        verify(usb_ui_font_has_codepoint(cp),"missing glyph",description);s+=bytes;
    }
}
void ttf_draw_text_px(uint8_t *fb,int x,int y,int size,const char *text,enum EpdFontFlags align,uint8_t fg,uint8_t bg){
    ps_view_t v=ps_view();int width=v.landscape?1216:684,height=v.landscape?684:1216;
    size_t length=(size_t)width*height;uint8_t *before=malloc(length);assert(before&&run_count<96);memcpy(before,fb,length);
    usb_ui_native_text(fb,x,y,size,text,align,fg,bg);
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
#define USB_UI_REAL_RENDER
#define main usb_ui_state_test_main
#define ps_button traced_button
#define ps_icon traced_icon
#define ps_wrap traced_wrap
#include "pico_usb_ui_host_test.c"
#undef ps_wrap
#undef ps_icon
#undef ps_button
#undef main
static void save_scene(const char *dir,const char *name){
    scene=name;run_count=control_count=icon_count=0;active=-1;uint8_t *fb=pico_fb_new();assert(fb);
    int before_reads=reads,before_starts=starts,before_stops=stops,before_prepare=prepares,before_finished=finishes,before_tasks=tasks;
    unsigned before=failures;if(ps_usb_background())ps_usb_background_render(fb);else ps_usb_render(fb);
    verify(reads==before_reads&&starts==before_starts&&stops==before_stops&&prepares==before_prepare&&finishes==before_finished&&tasks==before_tasks,"render has side effects","");
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
    fixture();route=PS_CANVAS;assert(ps_usb_request_background());save_scene(dir,"usb-background-starting");tick();complete();tick();save_scene(dir,"usb-background-waiting");
    engine.connected=true;tick();save_scene(dir,"usb-background-connected");background_action(PU_END);save_scene(dir,"usb-background-confirm");background_action(PU_END);save_scene(dir,"usb-background-stopping");tick();fail_stop=true;complete();tick();save_scene(dir,"usb-background-failed");
    fixture();save_scene(dir,"usb-ready");fail_prepare=true;action(PU_START);save_scene(dir,"usb-prepare-failed");
    fixture();action(PU_START);save_scene(dir,"usb-starting");tick();complete();tick();save_scene(dir,"usb-waiting");
    engine.connected=true;tick();save_scene(dir,"usb-connected");engine.writing=true;engine.written_blocks=UINT32_MAX;tick();save_scene(dir,"usb-writing");
    action(PU_END);save_scene(dir,"usb-confirm");action(PU_END);save_scene(dir,"usb-stopping");tick();fail_stop=true;complete();tick();save_scene(dir,"usb-stop-failed");
    action(PU_RETRY);save_scene(dir,"usb-retry");tick();fail_stop=false;complete();tick();save_scene(dir,"usb-finished");
    fixture();begin();engine.host_ejected=engine.writing=true;tick();save_scene(dir,"usb-host-eject-writing");engine.writing=false;tick();save_scene(dir,"usb-host-eject-stopping");
    tick();complete();fail_restore=true;tick();save_scene(dir,"usb-restore-failed");
    fixture();fail_start=true;action(PU_START);tick();complete();tick();save_scene(dir,"usb-start-failed");
    fixture();fail_start=partial_start=true;action(PU_START);tick();complete();tick();save_scene(dir,"usb-partial-start-failed");
    fixture();fail_task=true;action(PU_START);tick();tick();save_scene(dir,"usb-task-failed");
}
int main(int argc,char **argv){
    assert(argc==3&&pico_fb_init(argv[1]));char path[768];snprintf(path,sizeof(path),"%s/geometry.tsv",argv[2]);report=fopen(path,"w");assert(report);fputs("scene\torientation\ttext_runs\tbuttons\tfailures\n",report);
    for(int land=0;land<2;land++){view=land?(ps_view_t){640,360,true}:(ps_view_t){360,640,false};capture(argv[2]);}
    fclose(report);pico_fb_close();printf("USB actual-source/font render: %u scenes, %u bounds/ink/glyph/wrap checks, %u failures. USB/storage doubles; no device access.\n",scenes,verify_count,failures);return failures?1:0;
}

/* SPDX-License-Identifier: Apache-2.0
 * 中文：与设备同格式的4位物理画布，导出正向PGM；没有屏幕或USB操作。
 * English: Device-format packed 4-bit physical canvas, exported as canonical PGM; no display or USB access.
 */
#include "pico_system.h"
#include "ttf_font.h"
#include "ui_kit.h"
#include "display.h"
#include "continuous_du.h"
#include <string.h>
static enum EpdRotation host_rotation=EPD_ROT_INVERTED_PORTRAIT;
static uint8_t *host_frame;
static unsigned host_presents,host_follow_presents,host_default_presents,host_gc16_presents;
static bool host_refresh_follow,host_refresh_continuous;
static unsigned host_continuous_presents;
static int host_ink_phases;
static uint8_t host_previous[1216u*684u/2u];
static EpdRect host_refresh_area;
static enum EpdDrawMode host_refresh_mode;
static bool host_refresh_partial;
const EpdWaveform E0470_FOLLOW_WAVEFORM={0},E0470_WAVEFORM={0};
int epd_width(void){return 1216;}
int epd_height(void){return 684;}
enum EpdRotation epd_get_rotation(void){return host_rotation;}
void epd_set_rotation(enum EpdRotation rotation){host_rotation=rotation;}
int epd_rotated_display_width(void){return host_rotation==EPD_ROT_PORTRAIT||host_rotation==EPD_ROT_INVERTED_PORTRAIT?684:1216;}
int epd_rotated_display_height(void){return host_rotation==EPD_ROT_PORTRAIT||host_rotation==EPD_ROT_INVERTED_PORTRAIT?1216:684;}
EpdRect epd_full_screen(void){return (EpdRect){0,0,1216,684};}
static void rotate_point(int *x,int *y){
    int a=*x,b=*y;
    switch(host_rotation){case EPD_ROT_LANDSCAPE:break;case EPD_ROT_PORTRAIT:a=1215-*y;b=*x;break;case EPD_ROT_INVERTED_LANDSCAPE:a=1215-*x;b=683-*y;break;default:a=*y;b=683-*x;break;}
    *x=a;*y=b;
}
void epd_draw_pixel(int x,int y,uint8_t color,uint8_t *fb){
    rotate_point(&x,&y);if(x<0||x>=1216||y<0||y>=684)return;
    uint8_t *p=fb+(size_t)y*608+x/2;
    *p=(x&1)?(*p&15)|(color&240):(*p&240)|(color>>4);
}
uint8_t epd_get_pixel(int x,int y,int w,int h,const uint8_t *fb){
    if(x<0||x>=w||y<0||y>=h)return 0;uint8_t p=fb[(size_t)y*((w+1)/2)+x/2];return (uint8_t)(((x&1)?p>>4:p&15)<<4);
}
void epd_draw_hline(int x,int y,int n,uint8_t c,uint8_t *fb){for(int i=0;i<n;i++)epd_draw_pixel(x+i,y,c,fb);}
void epd_draw_vline(int x,int y,int n,uint8_t c,uint8_t *fb){for(int i=0;i<n;i++)epd_draw_pixel(x,y+i,c,fb);}
void epd_fill_rect(EpdRect r,uint8_t c,uint8_t *fb){for(int y=r.y;y<r.y+r.height;y++)epd_draw_hline(r.x,y,r.width,c,fb);}
void epd_draw_rect(EpdRect r,uint8_t c,uint8_t *fb){epd_draw_hline(r.x,r.y,r.width,c,fb);epd_draw_hline(r.x,r.y+r.height-1,r.width,c,fb);epd_draw_vline(r.x,r.y,r.height,c,fb);epd_draw_vline(r.x+r.width-1,r.y,r.height,c,fb);}
void epd_hl_set_all_white(EpdiyHighlevelState *hl){(void)hl;if(host_frame)memset(host_frame,255,1216u*684u/2u);}
void native_preview_set_frame(uint8_t *fb){host_frame=fb;}
unsigned native_preview_present_count(void){return host_presents;}
void native_preview_last_refresh(EpdRect *area,int *mode,bool *partial){if(area)*area=host_refresh_area;if(mode)*mode=host_refresh_mode;if(partial)*partial=host_refresh_partial;}
// 统计请求到达的推屏边界，不模拟波形或物理耗时；未变化的生产区域判定不会调用这里。
// Count display-boundary requests, not simulated waveform/timing; unchanged production diffs never reach this function.
const char *native_preview_refresh_waveform(void){return host_refresh_continuous?"CONTINUOUS":host_refresh_follow?"FOLLOW":"DEFAULT";}
void native_preview_refresh_counts(unsigned *follow,unsigned *standard,unsigned *gc16){if(follow)*follow=host_follow_presents;if(standard)*standard=host_default_presents;if(gc16)*gc16=host_gc16_presents;}
unsigned native_preview_continuous_count(void){return host_continuous_presents;}
void native_preview_display_event(int result);
static bool host_fail_body;
bool native_preview_body_will_fail(void){return host_fail_body;}
void native_preview_fail_body(bool enabled){host_fail_body=enabled;}
static enum EpdDrawError record_present(EpdiyHighlevelState *hl,enum EpdDrawMode mode,bool partial,EpdRect area,const EpdWaveform *waveform){
    host_refresh_continuous=false;
    host_refresh_follow=waveform==&E0470_FOLLOW_WAVEFORM;
    if(host_refresh_follow)host_follow_presents++;else host_default_presents++;
    if((mode&15)==MODE_GC16)host_gc16_presents++;
    host_presents++;host_refresh_mode=mode;host_refresh_area=area;host_refresh_partial=partial;
    if(host_fail_body && (!partial || area.height>84)) { native_preview_display_event(1); return (enum EpdDrawError)1; }
    if(hl){hl->front_fb=host_frame;hl->back_fb=host_previous;}
    if(!host_frame)return EPD_DRAW_SUCCESS;
    EpdRect physical=partial?ui_rotate_rect_to_fb(area):(EpdRect){0,0,1216,684};
    if(area.x==0&&area.y==0&&area.width==1216&&area.height==684)physical=(EpdRect){0,0,1216,684};
    // 中文：仅回写实际推送的区域，保留其余参考像素，匹配设备的部分更新契约。
    // English: Copy only the presented region, preserving reference pixels outside it like the device.
    int x0=physical.x&~31,y0=physical.y<0?0:physical.y;
    int x1=(physical.x+physical.width+31)&~31,y1=physical.y+physical.height;
    if(x0<0)x0=0;
    if(x1>1216)x1=1216;
    if(y1>684)y1=684;
    for(int y=y0;y<y1&&x1>x0;y++)
        memcpy(host_previous+(size_t)y*608+x0/2,host_frame+(size_t)y*608+x0/2,(size_t)(x1-x0)/2);
    if(!partial || area.height>84)native_preview_display_event(0);
    return EPD_DRAW_SUCCESS;
}
static enum EpdDrawError record_full(EpdiyHighlevelState *h,enum EpdDrawMode m,const EpdWaveform *w){return record_present(h,m,false,(EpdRect){0,0,epd_rotated_display_width(),epd_rotated_display_height()},w);}
enum EpdDrawError update_display_mode(EpdiyHighlevelState *h,enum EpdDrawMode m){return record_full(h,m,&E0470_WAVEFORM);}
enum EpdDrawError update_display_from_white(EpdiyHighlevelState *h){return record_full(h,MODE_GC16,&E0470_WAVEFORM);}
enum EpdDrawError update_display_from_white_with(EpdiyHighlevelState*h,const EpdWaveform*w,enum EpdDrawMode m){return record_full(h,m,w);}
enum EpdDrawError update_display_white(EpdiyHighlevelState *h){epd_hl_set_all_white(h);return record_full(h,MODE_GC16,&E0470_WAVEFORM);}
enum EpdDrawError update_display_full(EpdiyHighlevelState*h){return record_full(h,MODE_GC16,&E0470_WAVEFORM);}
enum EpdDrawError update_display_with(EpdiyHighlevelState*h,const EpdWaveform*w,enum EpdDrawMode m){return record_full(h,m,w);}
enum EpdDrawError update_display_area_with(EpdiyHighlevelState*h,const EpdWaveform*w,enum EpdDrawMode m,EpdRect r){return record_present(h,m,true,r,w);}

void guard_draw_result(EpdiyHighlevelState*h,enum EpdDrawError e){(void)h;(void)e;}
void epd_hl_last_timing(int*a,int*b,int*c){*a=*b=*c=0;}
void display_set_bulk_io(bool b){(void)b;}
static bool host_white_exit;
void display_hold_white_exit(bool b){host_white_exit=b;}
bool display_take_white_exit(void){bool b=host_white_exit;host_white_exit=false;return b;}
void rails_keepalive(void){}
void rails_idle_check(int64_t now){(void)now;}
int display_pclk_mhz(void){return 18;}
bool native_preview_save_frame(const char *path,const uint8_t *fb){
    int w=epd_rotated_display_width(),h=epd_rotated_display_height();
    FILE *f=fopen(path,"wb");if(!f)return false;fprintf(f,"P5\n%d %d\n255\n",w,h);
    uint8_t row[1216];for(int y=0;y<h;y++){for(int x=0;x<w;x++){int px=x,py=y;rotate_point(&px,&py);uint8_t value=epd_get_pixel(px,py,1216,684,fb);row[x]=(value>>4)*17;}if(fwrite(row,1,(size_t)w,f)!=(size_t)w){fclose(f);return false;}}
    return fclose(f)==0;
}

void epd_draw_rotated_image(EpdRect r,const uint8_t *image,uint8_t*fb){for(int y=0;y<r.height;y++)for(int x=0;x<r.width;x++)epd_draw_pixel(r.x+x,r.y+y,epd_get_pixel(x,y,r.width,r.height,image),fb);}

void epd_fill_circle(int x,int y,int r,uint8_t c,uint8_t*fb){epd_draw_vline(x,y-r,2*r+1,c,fb);epd_fill_circle_helper(x,y,r,3,0,c,fb);}

// 网页只计连续DU调用边界及排空调度，不模拟液晶波形、黑度或物理延迟。
// Count continuous-DU calls and drain scheduling only; no waveform, darkness or physical latency simulation.
bool continuous_du_init(void){return true;}
void continuous_du_reset(void){host_ink_phases=0;}
bool continuous_du_busy(void){return host_ink_phases>0;}
int continuous_du_dark_phases(void){return CONTINUOUS_DARK_PHASES;}
void continuous_du_mark_diff(const uint8_t *to,const uint8_t *from,EpdRect r,int phases,bool invert){
    (void)invert;
    for(int y=r.y;y<r.y+r.height;y++)for(int x=r.x;x<r.x+r.width;x++){
        int shift=(x&1)*4;size_t at=(size_t)y*608+x/2;
        if(((to[at]>>shift)&15)!=((from[at]>>shift)&15)){host_ink_phases=phases;return;}
    }
}
enum EpdDrawError continuous_du_scan(EpdiyHighlevelState *hl,EpdRect r){
    (void)hl;if(!host_ink_phases)return EPD_DRAW_SUCCESS;
    host_ink_phases--;host_presents++;host_continuous_presents++;host_refresh_continuous=true;host_refresh_follow=false;
    host_refresh_mode=MODE_DU;host_refresh_partial=true;host_refresh_area=r;return EPD_DRAW_SUCCESS;
}
void epd_poweron(void){}

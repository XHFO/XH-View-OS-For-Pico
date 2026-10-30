/* SPDX-License-Identifier: Apache-2.0
 * 与设备相同的stb字形度量/灰阶混合，连接真实pico_draw与同源ui_kit几何。
 * Host stb metrics/coverage matching the device, linked to actual pico_draw and source-extracted ui_kit geometry.
 * 冻结：当前渲染默认字体轮廓，不模拟可变字重，也不证明实机波形与性能。
 * Frozen: Render the default font outline, not variable-weight changes or physical waveforms/performance.
 */
#include "pico_system.h"
#include "pico_navigation.h"
#include "pico_framebuffer_host.h"
#include "ttf_font.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"
static stbtt_fontinfo host_font;
static unsigned char *host_font_data;
static char host_font_path[256];
static char host_font_id[160]="builtin";
static uint8_t coverage[256];
// 单页绘图检查只需要默认导航；导航专用或系统渲染器可用强定义覆盖这些桩。
// Isolated page renderers need only default navigation; real navigation/system harnesses override these weak doubles.
static const ps_navigation_entry_t host_navigation[] = {
    {PS_NAV_BOOKS,PS_SHELF,PI_BOOK,"书架",NULL},
    {PS_NAV_HOME,PS_HA,PI_HOME,"家庭",NULL},
    {PS_NAV_GAMES,PS_GAMES,PI_GAME,"小游戏",NULL}
};
__attribute__((weak)) int ps_navigation_count(void){return 3;}
__attribute__((weak)) const ps_navigation_entry_t *ps_navigation_item(int index){return index>=0&&index<3?&host_navigation[index]:NULL;}
__attribute__((weak)) int ps_navigation_index(ps_route_t route){for(int i=0;i<3;i++)if(host_navigation[i].route==route)return i;return -1;}
static int native_width(void){return ps_view().landscape?1216:684;}
static int native_height(void){return ps_view().landscape?684:1216;}
bool pico_fb_init(const char *path){
    FILE *f=fopen(path,"rb");if(!f)return false;
    if(fseek(f,0,SEEK_END)){fclose(f);return false;}
    long size=ftell(f);if(size<=0||fseek(f,0,SEEK_SET)){fclose(f);return false;}
    host_font_data=malloc((size_t)size);if(!host_font_data){fclose(f);return false;}
    bool ok=fread(host_font_data,1,(size_t)size,f)==(size_t)size;fclose(f);
    if(!ok||!stbtt_InitFont(&host_font,host_font_data,stbtt_GetFontOffsetForIndex(host_font_data,0))){free(host_font_data);host_font_data=NULL;return false;}
    snprintf(host_font_path,sizeof(host_font_path),"%s",path);
    for(int i=0;i<256;i++)coverage[i]=(uint8_t)(255.f*powf((float)i/255.f,0.6f)+0.5f);
    return true;
}
void pico_fb_close(void){free(host_font_data);host_font_data=NULL;}
uint8_t *pico_fb_new(void){size_t size=(size_t)native_width()*native_height();uint8_t *fb=malloc(size);if(fb)memset(fb,255,size);return fb;}
bool pico_fb_save(const char *path,const uint8_t *fb){
    FILE *f=fopen(path,"wb");if(!f)return false;
    bool ok=fprintf(f,"P5\n%d %d\n255\n",native_width(),native_height())>0;
    size_t size=(size_t)native_width()*native_height();if(fwrite(fb,1,size,f)!=size)ok=false;
    if(fclose(f))ok=false;return ok;
}
void epd_draw_pixel(int x,int y,uint8_t color,uint8_t *fb){if(x>=0&&y>=0&&x<native_width()&&y<native_height())fb[(size_t)y*native_width()+x]=(uint8_t)((color>>4)*17);}
// 主机图片保留阅读方向，背景取样使用同一规范画布。/ Host previews keep canonical reading orientation for background sampling.
EpdRect ui_rotate_rect_to_fb(EpdRect rect){
    return ps_view().landscape ? rect : (EpdRect){1216-rect.y-rect.height,rect.x,rect.height,rect.width};
}
uint8_t epd_get_pixel(int x,int y,int w,int h,const uint8_t *fb){
    (void)w;(void)h;
    if(!ps_view().landscape){int old=x;x=y;y=1215-old;}
    return x>=0&&y>=0&&x<native_width()&&y<native_height()?fb[(size_t)y*native_width()+x]:255;
}
void epd_fill_rect(EpdRect r,uint8_t color,uint8_t *fb){for(int y=r.y;y<r.y+r.height;y++)for(int x=r.x;x<r.x+r.width;x++)epd_draw_pixel(x,y,color,fb);}
void ui_fill_rect_fast(uint8_t *fb,EpdRect r,uint8_t color){epd_fill_rect(r,color,fb);}
void epd_draw_hline(int x,int y,int len,uint8_t c,uint8_t *fb){for(int i=0;i<len;i++)epd_draw_pixel(x+i,y,c,fb);}
void epd_draw_vline(int x,int y,int len,uint8_t c,uint8_t *fb){for(int i=0;i<len;i++)epd_draw_pixel(x,y+i,c,fb);}
static uint32_t decode(const char **text){
    const unsigned char *s=(const unsigned char*)*text;uint32_t cp;int n;
    if(s[0]<0x80){cp=s[0];n=1;}else if(s[0]<0xe0){cp=s[0]&31;n=2;}else if(s[0]<0xf0){cp=s[0]&15;n=3;}else{cp=s[0]&7;n=4;}
    for(int i=1;i<n;i++)cp=(cp<<6)|(s[i]&63);*text+=n;return cp;
}
static int clamp_font(int px){return px<12?12:px>120?120:px;}
static int advance(int px,uint32_t cp){int width,lsb;stbtt_GetCodepointHMetrics(&host_font,(int)cp,&width,&lsb);return (int)lroundf(width*stbtt_ScaleForPixelHeight(&host_font,(float)px));}
const char *ttf_font_path(void){return host_font_id;}
const char *ttf_font_display_name(void){return "内置字体（真实TTF）";}
bool ttf_font_is_builtin(void){return true;}
// 此主机替身每次直接栅格化，没有可释放的字形缓存。 / This host rasterizer has no retained glyph cache to release.
void ttf_font_cache_clear(void){}
int ttf_font_open_builtin(void){return ttf_font_open("builtin");}
bool ttf_font_path_is_builtin(const char *s){return !strcmp(s,"builtin")||!strcmp(s,"builtin-serif")||!strcmp(s,"builtin-kai");}
int ttf_font_open(const char *s){
    if(!strcmp(s,host_font_id))return 0;
    char path[512];
    if(ttf_font_path_is_builtin(s)){
        const char *slash=strrchr(host_font_path,'/');
        int len=slash?(int)(slash-host_font_path+1):0;
        snprintf(path,sizeof(path),"%.*s%s.ttf",len,host_font_path,!strcmp(s,"builtin-serif")?"serif":!strcmp(s,"builtin-kai")?"kai":"builtin");
    }else snprintf(path,sizeof(path),"%s",s);
    FILE *check=fopen(path,"rb");if(!check)return -1;fclose(check);
    pico_fb_close();if(!pico_fb_init(path))return -1;
    snprintf(host_font_id,sizeof(host_font_id),"%s",s);return 0;
}
void ttf_set_weight(int w){(void)w;}
int ttf_get_weight(void){return 400;}
int ttf_text_width_px(int px,const char *text){int width=0;px=clamp_font(px);while(*text)width+=advance(px,decode(&text));return width;}
int ttf_ascender_px(int px){int ascent,descent,gap;stbtt_GetFontVMetrics(&host_font,&ascent,&descent,&gap);return (int)lroundf(ascent*stbtt_ScaleForPixelHeight(&host_font,(float)clamp_font(px)));}
void ttf_draw_text_px(uint8_t *fb,int x,int y,int px,const char *text,enum EpdFontFlags align,uint8_t fg,uint8_t bg){
    px=clamp_font(px);if(align&EPD_DRAW_ALIGN_CENTER)x-=ttf_text_width_px(px,text)/2;else if(align&EPD_DRAW_ALIGN_RIGHT)x-=ttf_text_width_px(px,text);
    float scale=stbtt_ScaleForPixelHeight(&host_font,(float)px);
    while(*text){uint32_t cp=decode(&text);int w,h,dx,dy;unsigned char *bits=stbtt_GetCodepointBitmap(&host_font,scale,scale,(int)cp,&w,&h,&dx,&dy);
        for(int row=0;row<h;row++)for(int col=0;col<w;col++){uint8_t alpha=bits[row*w+col];if(alpha){int gray=bg+coverage[alpha]*((int)fg-(int)bg)/255;epd_draw_pixel(x+dx+col,y+dy+row,(uint8_t)(gray<<4),fb);}}
        stbtt_FreeBitmap(bits,NULL);x+=advance(px,cp);
    }
}

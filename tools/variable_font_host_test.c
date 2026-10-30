/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 * 原始可变字体的真实生产引擎与临时FontTools静态参考逐像素对照。
 * Compare the real production engine with private FontTools reference instances.
 */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ttf_font.h"
#define MALLOC_CAP_SPIRAM 1
#define MALLOC_CAP_8BIT 2
#define ESP_LOGI(...) ((void)0)
#define ESP_LOGW(...) ((void)0)
#define ESP_LOGE(...) ((void)0)
static bool fail_next_allocation;
static void *heap_caps_malloc(size_t n,int c){(void)c;if(fail_next_allocation){fail_next_allocation=false;return NULL;}return malloc(n);}
static void *heap_caps_calloc(size_t n,size_t s,int c){(void)c;return calloc(n,s);}
static void heap_caps_free(void *p){free(p);}
static size_t heap_caps_get_largest_free_block(int c){(void)c;return 16*1024*1024;}
static int64_t esp_timer_get_time(void){return 0;}
static const char *app_settings_font_path(void){return "builtin";}
void epd_draw_pixel(int x,int y,uint8_t g,uint8_t *fb){(void)x;(void)y;(void)g;(void)fb;}
#include "ttf_font-under-test.c"

static uint8_t *read_file(const char *path,size_t *n){
    FILE *f=fopen(path,"rb");assert(f);assert(!fseek(f,0,SEEK_END));
    long size=ftell(f);assert(size>0);*n=(size_t)size;rewind(f);
    uint8_t *p=malloc(*n);assert(p&&fread(p,1,*n,f)==*n);fclose(f);return p;
}
int main(int argc,char **argv){
    assert(argc==6);
    size_t raw_n,ref_n;uint8_t *raw=read_file(argv[1],&raw_n),*ref=read_file(argv[4],&ref_n);
    (void)ref_n;
    builtin_ttf_start=serif_ttf_start=kai_ttf_start=raw;
    builtin_ttf_end=serif_ttf_end=kai_ttf_end=raw+raw_n;
    bool file_mode=atoi(argv[5])!=0;
    assert((file_mode?ttf_font_open(argv[1]):ttf_font_open_builtin())==ESP_OK);
    assert(font_ready&&gvar_ready);
    if(file_mode){assert(font_fd>=0&&!font_mem&&!glyf_ram&&!gvar_ram);}
    stbtt_fontinfo expected;assert(stbtt_InitFont(&expected,ref,0));
    int weight=atoi(argv[3]);ttf_set_weight(weight);
    unsigned checks=0,failures=0;uint32_t cp;int px;
    FILE *points=fopen(argv[2],"r");assert(points);
    while(fscanf(points,"%x %d",&cp,&px)==2){
        int gid=stbtt_FindGlyphIndex(&expected,(int)cp);assert(gid>0);
        const glyph_entry_t *actual=get_glyph(cp,px);assert(actual);
        float scale=stbtt_ScaleForPixelHeight(&expected,(float)px);
        int x0,y0,x1,y1;stbtt_GetGlyphBitmapBox(&expected,gid,scale,scale,&x0,&y0,&x1,&y1);
        int w=x1-x0,h=y1-y0;bool same=actual->width==w&&actual->height==h&&actual->left==x0&&actual->top==-y0;
        uint8_t *pixels=NULL;
        if(w&&h){pixels=malloc((size_t)w*h);assert(pixels);stbtt_MakeGlyphBitmap(&expected,pixels,w,h,w,scale,scale,gid);}
        unsigned diff=0,total=0;
        if(same&&pixels)for(int i=0;i<w*h;i++){unsigned d=(unsigned)abs((int)actual->bitmap[i]-(int)pixels[i]);diff+=d;total+=pixels[i];}
        if(!same||diff){
            if(failures<12)fprintf(stderr,"mismatch U+%X px=%d weight=%d actual=%dx%d@%d,%d ref=%dx%d@%d,%d diff=%u/%u gid=%d\n",cp,px,weight,actual->width,actual->height,actual->left,actual->top,w,h,x0,-y0,diff,total,gid);
            if(failures<3){
                assert(pack_glyph_tree(stbtt_FindGlyphIndex(&font_info,(int)cp)));
                stbtt_vertex *av=NULL,*ev=NULL;int an=stbtt_GetGlyphShape(&font_info,stbtt_FindGlyphIndex(&font_info,(int)cp),&av),en=stbtt_GetGlyphShape(&expected,gid,&ev),shown=0;
                for(int k=0;k<an&&k<en&&shown<6;k++)if(memcmp(&av[k],&ev[k],sizeof(av[k]))){fprintf(stderr," vertex%d actual=(%d,%d;%d,%d) expected=(%d,%d;%d,%d)\n",k,av[k].x,av[k].y,av[k].cx,av[k].cy,ev[k].x,ev[k].y,ev[k].cx,ev[k].cy);shown++;}
                stbtt_FreeShape(&font_info,av);stbtt_FreeShape(&expected,ev);
            }
            failures++;
        }
        free(pixels);checks++;
    }
    fclose(points);
    if(weight!=wght_def){
        /* 复合bbox的低内存失败必须作废暂存区，旧字形随后可恢复。/ A composite-bounds OOM invalidates the arena and the previous glyph can be restored. */
        int previous=stbtt_FindGlyphIndex(&font_info,0x9605),composite=stbtt_FindGlyphIndex(&font_info,0x252);
        assert(previous&&composite&&pack_glyph_tree(previous));
        fail_next_allocation=true;assert(!pack_glyph_tree(composite));
        assert(!fail_next_allocation&&packed_root==-1&&packed_weight==-1);
        const glyph_entry_t *restored=get_glyph(0x9605,47);assert(restored);
        int gid=stbtt_FindGlyphIndex(&expected,0x9605),x0,y0,x1,y1;
        float s=stbtt_ScaleForPixelHeight(&expected,47);
        stbtt_GetGlyphBitmapBox(&expected,gid,s,s,&x0,&y0,&x1,&y1);
        int w=x1-x0,h=y1-y0;assert(restored->width==w&&restored->height==h);
        uint8_t *pixels=malloc((size_t)w*h);assert(pixels);
        stbtt_MakeGlyphBitmap(&expected,pixels,w,h,w,s,s,gid);
        assert(!memcmp(restored->bitmap,pixels,(size_t)w*h));free(pixels);
    }
    /* 失败请求不冒充成功；恢复后的旧字体必须仍能栅格化。/ Failed requests remain failures while the previous file remains usable. */
    if(file_mode){
        assert(ttf_font_open("/nonexistent/misans-regression-missing.ttf")==ESP_ERR_NOT_FOUND);
        assert(!strcmp(ttf_font_path(),argv[1])&&ttf_font_ready()&&get_glyph(0x9605,43));
        assert(!ttf_font_sd_suspended());assert(ttf_font_suspend_sd(true)==ESP_OK);
        assert(ttf_font_sd_suspended()&&ttf_font_is_builtin());
        assert(ttf_font_open(argv[1])==ESP_ERR_INVALID_STATE&&ttf_font_ready());
        assert(ttf_font_suspend_sd(false)==ESP_OK&&!ttf_font_sd_suspended());
        assert(ttf_font_open(argv[1])==ESP_OK&&get_glyph(0x9605,43));
    }
    ttf_font_unload();free(raw);free(ref);
    heap_caps_free(work);heap_caps_free(io_data);heap_caps_free(io_run_buf);
    printf("variable font %s weight=%d: %u cases, %u mismatches\n",file_mode?"stream":"memory",weight,checks,failures);
    return failures?1:0;
}

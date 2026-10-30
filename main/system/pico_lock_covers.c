/*
 * SPDX-FileCopyrightText: 2026 XH Lab
 * SPDX-License-Identifier: Apache-2.0
 * 中文：独立休眠封面布局与带源指纹/CRC的 lock-v1 缓存。
 * English: Independent sleep-cover layouts and a source-fingerprinted, CRC-checked lock-v1 cache.
 * 冻结：所有 I/O 仅在 prepare；render 纯绘图；不借用书架像素、不改书本或进度。
 * Frozen: All I/O belongs to prepare; render paints only; never borrow shelf pixels or change books/progress.
 */
#include "pico_lock_covers.h"
#include "pico_system.h"
#include "book_cover.h"
#include "book_epub.h"
#include "book_progress.h"
#include "esp_heap_caps.h"
#include <errno.h>
#include <limits.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>

#define LOCK_COUNT 6
#define LOCK_PATH 288
#define LOCK_CACHE_PATH (LOCK_PATH + 96)
#define LOCK_HEADER 72
#define LOCK_THUMB_W 128
#define LOCK_THUMB_H 192
#define LOCK_HIGH_W 648
#define LOCK_HIGH_H 1000

typedef struct {
    char path[LOCK_PATH], title[LOCK_PATH];
    uint8_t *pixels;
    uint16_t width, height;
} lock_cover_t;
typedef struct { uint64_t size, modified, sample; } lock_source_t;
static lock_cover_t covers[LOCK_COUNT];
static int cover_count, cover_layout;

static uint64_t hash64(uint64_t h,const void *data,size_t n) {
    const uint8_t *p=data;
    while(n--)h=(h^*p++)*UINT64_C(1099511628211);
    return h;
}
static uint32_t crc32_bytes(const void *data,size_t n) {
    const uint8_t *p=data;uint32_t crc=UINT32_MAX;
    while(n--){crc^=*p++;for(int bit=0;bit<8;bit++)crc=(crc>>1)^(UINT32_C(0xedb88320)&(uint32_t)-(int32_t)(crc&1));}
    return ~crc;
}
static void put16(uint8_t *p,uint16_t n){p[0]=(uint8_t)n;p[1]=(uint8_t)(n>>8);}
static void put32(uint8_t *p,uint32_t n){for(int i=0;i<4;i++)p[i]=(uint8_t)(n>>(i*8));}
static void put64(uint8_t *p,uint64_t n){for(int i=0;i<8;i++)p[i]=(uint8_t)(n>>(i*8));}
static uint16_t get16(const uint8_t *p){return (uint16_t)((uint16_t)p[0]|(uint16_t)p[1]<<8);}
static uint32_t get32(const uint8_t *p){uint32_t n=0;for(int i=0;i<4;i++)n|=(uint32_t)p[i]<<(i*8);return n;}
static uint64_t get64(const uint8_t *p){uint64_t n=0;for(int i=0;i<8;i++)n|=(uint64_t)p[i]<<(i*8);return n;}
static bool valid_path(const char *path) {
    return path&&path[0]=='/'&&strnlen(path,LOCK_PATH)<LOCK_PATH;
}

// 只采样首尾各 2 KiB；mtime/大小也进入显式小端指纹，避免全书读取。
// Sample at most 2 KiB at each end; size/mtime join an explicit little-endian fingerprint, without reading the whole book.
static bool source_read(const char *path,lock_source_t *out) {
    struct stat st;if(stat(path,&st)||!S_ISREG(st.st_mode)||st.st_size<0||(uint64_t)st.st_size>LONG_MAX)return false;
    FILE *file=fopen(path,"rb");if(!file)return false;
    lock_source_t source={.size=(uint64_t)st.st_size,.modified=(uint64_t)st.st_mtime};
    uint8_t metadata[16],bytes[512];put64(metadata,source.size);put64(metadata+8,source.modified);
    source.sample=hash64(UINT64_C(14695981039346656037),metadata,sizeof(metadata));bool ok=true;
    for(int pass=0;pass<2&&ok;pass++){
        if(pass&&source.size<=2048)break;
        uint64_t offset=pass?source.size-2048:0;
        if(fseek(file,(long)offset,SEEK_SET)){ok=false;break;}
        size_t left=source.size-offset>2048?2048:(size_t)(source.size-offset);
        while(left){size_t n=left<sizeof(bytes)?left:sizeof(bytes);if(fread(bytes,1,n,file)!=n){ok=false;break;}
            source.sample=hash64(source.sample,bytes,n);left-=n;}
    }
    if(fclose(file))ok=false;
    // 读取时换书或断卡不得给已变更源写入错误的缓存指纹。
    // Do not cache under a stale fingerprint if the source changes or disappears while sampled.
    struct stat after;
    if(ok&&(stat(path,&after)||after.st_size!=st.st_size||after.st_mtime!=st.st_mtime))ok=false;
    if(ok)*out=source;
    return ok;
}
static bool same_source(const lock_source_t *a,const lock_source_t *b) {
    return a->size==b->size&&a->modified==b->modified&&a->sample==b->sample;
}
static bool cache_paths(const char *book,uint64_t key,bool high,char *parent,char *directory,char *path) {
    const char *slash=strrchr(book,'/');if(!slash||slash==book)return false;
    int n=snprintf(parent,LOCK_CACHE_PATH,"%.*s/.pico-covers",(int)(slash-book),book);
    if(n<0||n>=LOCK_CACHE_PATH)return false;
    n=snprintf(directory,LOCK_CACHE_PATH,"%s/lock-v1",parent);if(n<0||n>=LOCK_CACHE_PATH)return false;
    n=snprintf(path,LOCK_CACHE_PATH,"%s/%016llx-%c.plc",directory,(unsigned long long)key,high?'h':'t');
    return n>0&&n<LOCK_CACHE_PATH;
}
static bool cache_read(const char *path,uint64_t key,const lock_source_t *source,bool high,lock_cover_t *out) {
    FILE *file=fopen(path,"rb");if(!file)return false;
    uint8_t h[LOCK_HEADER];bool ok=fread(h,1,sizeof(h),file)==sizeof(h);
    uint16_t w=0,height=0;uint32_t bytes=0;
    if(ok){w=get16(h+20);height=get16(h+22);bytes=get32(h+24);
        ok=!memcmp(h,"PLCV0001",8)&&get32(h+8)==1&&get32(h+12)==LOCK_HEADER&&get32(h+16)==(high?1u:0u)&&
            w&&height&&w<=(high?LOCK_HIGH_W:LOCK_THUMB_W)&&height<=(high?LOCK_HIGH_H:LOCK_THUMB_H)&&bytes==(uint32_t)w*height&&
            get64(h+32)==key&&get64(h+40)==source->size&&get64(h+48)==source->modified&&get64(h+56)==source->sample&&
            get32(h+64)==crc32_bytes(h,64)&&get32(h+68)==0;
    }
    uint8_t *pixels=NULL;
    if(ok){pixels=heap_caps_malloc(bytes,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
        ok=pixels&&fread(pixels,1,bytes,file)==bytes&&get32(h+28)==crc32_bytes(pixels,bytes);}
    if(ok)ok=fgetc(file)==EOF&&!ferror(file);
    if(fclose(file))ok=false;
    if(!ok){free(pixels);return false;}
    out->pixels=pixels;out->width=w;out->height=height;return true;
}
static bool ensure_directory(const char *path) {
    struct stat st;if(!mkdir(path,0700))return true;
    return errno==EEXIST&&!stat(path,&st)&&S_ISDIR(st.st_mode);
}
static void cache_write(const char *parent,const char *directory,const char *path,uint64_t key,
                        const lock_source_t *source,bool high,const lock_cover_t *cover) {
    if(!ensure_directory(parent)||!ensure_directory(directory))return;
    char temporary[LOCK_CACHE_PATH+8];int n=snprintf(temporary,sizeof(temporary),"%s.tmp",path);
    if(n<0||(size_t)n>=sizeof(temporary))return;
    FILE *file=fopen(temporary,"wb");if(!file)return;
    uint8_t h[LOCK_HEADER]={0};uint32_t bytes=(uint32_t)cover->width*cover->height;
    memcpy(h,"PLCV0001",8);put32(h+8,1);put32(h+12,LOCK_HEADER);put32(h+16,high?1:0);
    put16(h+20,cover->width);put16(h+22,cover->height);put32(h+24,bytes);put32(h+28,crc32_bytes(cover->pixels,bytes));
    put64(h+32,key);put64(h+40,source->size);put64(h+48,source->modified);put64(h+56,source->sample);put32(h+64,crc32_bytes(h,64));
    bool ok=fwrite(h,1,sizeof(h),file)==sizeof(h)&&fwrite(cover->pixels,1,bytes,file)==bytes;
    if(ok)ok=fflush(file)==0;
    if(ok)ok=fsync(fileno(file))==0;
    if(fclose(file))ok=false;
    // FatFs 不支持覆盖重命名；完整临时缓存准备好后才替换可丢弃旧缓存。
    // FatFs cannot rename over a destination; replace disposable old cache only after the new temporary cache is complete.
    if(ok&&unlink(path)&&errno!=ENOENT)ok=false;
    if(ok)ok=rename(temporary,path)==0;
    if(!ok)(void)unlink(temporary);
}
static bool copy_thumb(lock_cover_t *out,const uint8_t *pixels,unsigned w,unsigned h) {
    if(!pixels||!w||!h)return false;
    unsigned width=w,height=h;
    if(width>LOCK_THUMB_W){height=(height*LOCK_THUMB_W+width/2)/width;width=LOCK_THUMB_W;}
    if(height>LOCK_THUMB_H){width=(width*LOCK_THUMB_H+height/2)/height;height=LOCK_THUMB_H;}
    if(!width)width=1;
    if(!height)height=1;
    uint8_t *copy=heap_caps_malloc(width*height,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);if(!copy)return false;
    for(unsigned y=0;y<height;y++)for(unsigned x=0;x<width;x++)copy[y*width+x]=pixels[((size_t)y*h/height)*w+(size_t)x*w/width];
    out->pixels=copy;out->width=(uint16_t)width;out->height=(uint16_t)height;return true;
}
static bool decode_cover(lock_cover_t *cover,bool high) {
    const char *extension=strrchr(cover->path,'.');if(!extension||strcasecmp(extension,".epub"))return false;
    // 新准备的书架缓存可直接复制；旧RAM记录未暴露源指纹，不能证明仍属于当前文件。
    // Copy a freshly prepared shelf cache; an older RAM entry has no exposed source fingerprint and cannot prove source freshness.
    if(!high&&!book_cover_known(cover->path)){
        (void)book_cover_prepare(cover->path);book_cover_view_t view={0};
        return book_cover_get(cover->path,&view)&&copy_thumb(cover,view.pixels,view.width,view.height);
    }
    uint8_t *pixels=NULL;uint16_t w=0,h=0;
    esp_err_t result=book_epub_load_cover(cover->path,&pixels,&w,&h);
    if(result!=ESP_OK||!pixels||!w||!h||w>LOCK_HIGH_W||h>LOCK_HIGH_H){free(pixels);return false;}
    if(high){cover->pixels=pixels;cover->width=w;cover->height=h;return true;}
    bool ok=copy_thumb(cover,pixels,w,h);free(pixels);return ok;
}
static void prepare_cover(lock_cover_t *cover,bool high) {
    lock_source_t source;if(!source_read(cover->path,&source))return;
    uint64_t key=hash64(UINT64_C(14695981039346656037),cover->path,strlen(cover->path));
    char parent[LOCK_CACHE_PATH],directory[LOCK_CACHE_PATH],path[LOCK_CACHE_PATH];
    bool disk=cache_paths(cover->path,key,high,parent,directory,path);
    if(disk&&cache_read(path,key,&source,high,cover))return;
    if(!decode_cover(cover,high))return;
    lock_source_t after;
    if(!source_read(cover->path,&after)||!same_source(&source,&after)){
        free(cover->pixels);cover->pixels=NULL;cover->width=cover->height=0;return;
    }
    if(disk)cache_write(parent,directory,path,key,&source,high,cover);
}
static void add_source(const char *path) {
    if(cover_count>=LOCK_COUNT||!valid_path(path))return;
    for(int i=0;i<cover_count;i++)if(!strcmp(covers[i].path,path))return;
    lock_cover_t *cover=&covers[cover_count++];snprintf(cover->path,sizeof(cover->path),"%s",path);
    const char *name=strrchr(path,'/');name=name?name+1:path;snprintf(cover->title,sizeof(cover->title),"%s",*name?name:"未命名图书");
    char *dot=strrchr(cover->title,'.');if(dot&&dot!=cover->title)*dot=0;
}
void ps_lock_covers_release(void) {
    for(int i=0;i<LOCK_COUNT;i++){free(covers[i].pixels);memset(&covers[i],0,sizeof(covers[i]));}
    cover_count=0;cover_layout=0;
}
void ps_lock_covers_prepare(int layout) {
    ps_lock_covers_release();cover_layout=layout>=0&&layout<=4?layout:0;
    char current[LOCK_PATH]={0},paths[LOCK_COUNT][LOCK_PATH]={{0}};
    if(book_progress_last_path(current,sizeof(current)))add_source(current);
    int count=ps_reader_cover_sources(paths,LOCK_COUNT);if(count<0)count=0;if(count>LOCK_COUNT)count=LOCK_COUNT;
    for(int i=0;i<count;i++)add_source(paths[i]);
    int visible=(cover_layout==0||cover_layout==3)?1:cover_layout==1?2:LOCK_COUNT;
    if(cover_count>visible)cover_count=visible;
    for(int i=0;i<cover_count;i++)prepare_cover(&covers[i],cover_layout==0||cover_layout==3);
}

static void paint_cover(uint8_t *fb,const lock_cover_t *cover,int x,int y,int w,int h) {
    if(cover&&cover->pixels&&cover->width&&cover->height){
        EpdRect target=ps_rect(x,y,w,h);int dw=target.width,dh=(int)((uint32_t)cover->height*dw/cover->width);
        if(dh>target.height){dh=target.height;dw=(int)((uint32_t)cover->width*dh/cover->height);}
        if(dw<1)dw=1;
        if(dh<1)dh=1;
        int dx=target.x+(target.width-dw)/2,dy=target.y+(target.height-dh)/2;
        for(int row=0;row<dh;row++)for(int col=0;col<dw;col++){
            size_t sy=(size_t)row*cover->height/dh,sx=(size_t)col*cover->width/dw;
            epd_draw_pixel(dx+col,dy+row,cover->pixels[sy*cover->width+sx],fb);
        }
        return;
    }
    ps_box(fb,x,y,w,h,0,14,true);ps_box(fb,x,y,w,h,0,6,false);
    int margin=w>160?20:12,size=w>160?25:18,lines=h/(size+5)-2;if(lines>6)lines=6;if(lines<1)lines=1;
    int text_y=y+(h-lines*(size+5))/2;
    ps_wrap(fb,x+margin,text_y,w-2*margin,size,lines,cover?cover->title:"暂无图书",1);
    if(h>150)ps_text_fixed(fb,x+margin,y+h-34,10,"小纸 Pico",6);
}
// 书墙仅裁切显示窗口，不拉伸原图；其余旧样式继续完整等比适配。
// Crop the book wall's display window without stretching art; existing styles retain full aspect fitting.
static void paint_cover_crop(uint8_t *fb,const lock_cover_t *cover,int x,int y,int w,int h){
    if(!cover||!cover->pixels||!cover->width||!cover->height){paint_cover(fb,cover,x,y,w,h);return;}
    EpdRect r=ps_rect(x,y,w,h);unsigned sw=cover->width,sh=cover->height;
    if((uint64_t)sw*r.height>(uint64_t)sh*r.width)sw=(unsigned)((uint64_t)sh*r.width/r.height);
    else sh=(unsigned)((uint64_t)sw*r.height/r.width);
    if(!sw)sw=1;
    if(!sh)sh=1;
    unsigned ox=(cover->width-sw)/2,oy=(cover->height-sh)/2;
    for(int row=0;row<r.height;row++)for(int col=0;col<r.width;col++){
        unsigned sy=oy+(unsigned)((uint64_t)row*sh/r.height),sx=ox+(unsigned)((uint64_t)col*sw/r.width);
        epd_draw_pixel(r.x+col,r.y+row,cover->pixels[(size_t)sy*cover->width+sx],fb);
    }
}
void ps_lock_covers_render(uint8_t *fb) {
    if(!fb)return;
    ps_view_t v=ps_view();ps_fill(fb,0,0,v.w,v.h,15);
    if(cover_layout==0){paint_cover(fb,cover_count?&covers[0]:NULL,0,0,v.w,v.h);return;}
    if(cover_layout==3){
        int margin=v.landscape?36:30,title_h=v.landscape?112:128,art_h=v.h-title_h-2*margin;
        paint_cover(fb,cover_count?&covers[0]:NULL,margin,margin,v.w-2*margin,art_h);
        ps_wrap(fb,margin+8,margin+art_h+20,v.w-2*margin-16,v.landscape?18:22,2,cover_count?covers[0].title:"我的书库",1);
        ps_text_fixed(fb,margin+8,v.h-84,9,"小纸 Pico · 书库封面",6);return;
    }
    if(cover_layout==4){
        int columns=v.landscape?3:2,rows=v.landscape?2:3;
        for(int i=0;i<columns*rows;i++){
            int col=i%columns,row=i/columns,x=col*v.w/columns,y=row*v.h/rows;
            int right=(col+1)*v.w/columns,bottom=(row+1)*v.h/rows;
            paint_cover_crop(fb,i<cover_count?&covers[i]:NULL,x,y,right-x-(col+1<columns?2:0),bottom-y-(row+1<rows?2:0));
        }
        return;
    }
    int columns=cover_layout==1?(v.landscape?2:1):(v.landscape?3:2);
    int rows=cover_layout==1?(v.landscape?1:2):(v.landscape?2:3);
    int gap=10,margin=12,width=(v.w-2*margin-(columns-1)*gap)/columns,height=(v.h-2*margin-(rows-1)*gap)/rows;
    for(int i=0;i<columns*rows;i++)paint_cover(fb,i<cover_count?&covers[i]:NULL,
        margin+(i%columns)*(width+gap),margin+(i/columns)*(height+gap),width,height);
}

/* SPDX-License-Identifier: Apache-2.0
 * 中文：真实书源/图片/排版自动加载、有界缓存和失败几何回归；不接触设备。
 * English: Real source/image/layout auto-load, bounded cache and stable failure geometry; no device access.
 */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "book_source.h"
#include "book_epub.h"
#include "book_layout.h"
#include "ttf_font.h"
static size_t checks, painted;
static EpdRect body;
#define CHECK(x) do { assert(x); ++checks; } while (0)
void epd_draw_pixel(int x,int y,uint8_t c,uint8_t* fb) {
    (void)c;(void)fb; CHECK(x>=body.x&&x<body.x+body.width&&y>=body.y&&y<body.y+body.height); ++painted;
}
int ttf_text_width_px(int px,const char* text) {int width=0;for(;*text;++text)if(((unsigned char)*text&0xc0)!=0x80)width+=px/2;return width;}
int ttf_ascender_px(int px){return px;}
void ttf_draw_text_px(uint8_t* fb,int x,int y,int px,const char* text,enum EpdFontFlags align,uint8_t fg,uint8_t bg) {
    (void)fb;(void)x;(void)y;(void)px;(void)text;(void)align;(void)fg;(void)bg;
}
static size_t cache_bytes(blk_t* blocks,size_t count) {
    size_t bytes=0,items=0;
    for(size_t i=0;i<count;++i)if(blocks[i].image){bytes+=(size_t)blocks[i].image_width*blocks[i].image_height;++items;}
    CHECK(bytes<=BOOK_INLINE_IMAGE_CACHE_BYTES&&items<=BOOK_INLINE_IMAGE_CACHE_ITEMS);return bytes;
}
static void chapter(size_t index,EpdRect rect) {
    html_text_t normal={0},counted={0};CHECK(book_chapter_load_blocks(index,&normal)==ESP_OK);
    CHECK(book_chapter_load_blocks_for_pagination(index,&counted)==ESP_OK);
    CHECK(normal.count==counted.count&&normal.len==counted.len&&!strcmp(normal.utf8,counted.utf8));
    for(size_t i=0;i<normal.count;++i){CHECK(!normal.blocks[i].image&&!counted.blocks[i].image);CHECK(normal.blocks[i].image_width==counted.blocks[i].image_width&&normal.blocks[i].image_height==counted.blocks[i].image_height);}
    body=rect;CHECK(book_layout_build_blocks(normal.utf8,normal.len,normal.blocks,normal.count,body,24));
    size_t pages=book_layout_page_count(),offsets[4096],automatic=0,failures=0;
    CHECK(pages>0&&pages<4096);for(size_t p=0;p<=pages;++p)offsets[p]=book_layout_page_start_offset(p);
    for(size_t p=0;p<pages;++p){
        book_chapter_prepare_images(index,normal.blocks,normal.count,offsets[p],offsets[p+1]);cache_bytes(normal.blocks,normal.count);
        for(size_t i=0;i<normal.count;++i)if(normal.blocks[i].offset>=offsets[p]&&normal.blocks[i].offset<offsets[p+1]&&normal.blocks[i].image_src){
            blk_t* b=normal.blocks+i;
            if(b->image){++automatic;CHECK(b->image_width&&b->image_height);}
            else {++failures;CHECK(b->image_failed);}
        }
        uint8_t fb=0;painted=0;book_layout_draw_page(&fb,p,body,24);
        // 纯绘图不增减缓存；解码状态改变后页首和文本锚点保持不变。
        // Pure drawing does not mutate the cache; decoded states preserve page starts and text anchors.
        size_t before=cache_bytes(normal.blocks,normal.count);book_layout_draw_page(&fb,p,body,24);CHECK(cache_bytes(normal.blocks,normal.count)==before);
    }
    if(index==0)CHECK(automatic==3&&failures==0);
    if(index==1)CHECK(automatic==72&&failures==0);
    if(index==2)CHECK(automatic==2&&failures==2);
    CHECK(book_layout_build_blocks(normal.utf8,normal.len,normal.blocks,normal.count,body,24));
    CHECK(book_layout_page_count()==pages);for(size_t p=0;p<=pages;++p)CHECK(book_layout_page_start_offset(p)==offsets[p]);
    // 分步全书计页与已经加载像素的实际页表相同。
    // Incremental counting matches the displayed table after pixels have been loaded.
    book_layout_job_t* job=book_layout_job_create(counted.utf8,counted.len,counted.blocks,counted.count,body,24);CHECK(job);
    while(!book_layout_job_complete(job))CHECK(book_layout_job_step(job,2));CHECK(book_layout_job_page_count(job)==pages);
    for(size_t p=0;p<pages;++p){size_t at=SIZE_MAX;CHECK(book_layout_job_page_offset(job,p,&at)&&at==offsets[p]);}
    book_layout_job_free(job);book_layout_free();html_text_free(&counted);html_text_free(&normal);
}
static void bounds(void) {
    blk_t blocks[80]={0};
    for(size_t i=0;i<80;++i){blocks[i].offset=2*i;blocks[i].len=1;blocks[i].image_src="images/baseline.jpg";blocks[i].image_width=648;blocks[i].image_height=863;}
    for(size_t i=0;i<80;++i){CHECK(book_chapter_prepare_images(0,blocks,80,2*i,2*i+1));CHECK(blocks[i].image);cache_bytes(blocks,80);}
    CHECK(!blocks[0].image&&blocks[79].image);CHECK(book_chapter_prepare_images(0,blocks,80,0,1)&&blocks[0].image);cache_bytes(blocks,80);
    for(size_t i=0;i<80;++i)free(blocks[i].image);
    memset(blocks,0,sizeof(blocks));
    for(size_t i=0;i<80;++i){blocks[i].offset=2*i;blocks[i].len=1;blocks[i].image_src="images/small.png";blocks[i].image_width=96;blocks[i].image_height=64;}
    for(size_t i=0;i<80;++i){CHECK(book_chapter_prepare_images(0,blocks,80,2*i,2*i+1));CHECK(blocks[i].image);cache_bytes(blocks,80);}
    CHECK(!blocks[0].image&&blocks[79].image);for(size_t i=0;i<80;++i)free(blocks[i].image);
    CHECK(!book_chapter_prepare_images(0,NULL,0,0,1));CHECK(!book_chapter_prepare_images(0,blocks,80,1,0));
}
int main(int argc,char** argv){
    CHECK(argc==2);CHECK(book_open(argv[1])==ESP_OK);CHECK(book_chapter_count()==3);
    for(int orientation=0;orientation<2;++orientation)for(size_t i=0;i<3;++i)chapter(i,orientation?(EpdRect){12,18,1216,600}:(EpdRect){12,18,648,1000});
    bounds();book_close();uint8_t* cover=NULL;uint16_t width=0,height=0;
    CHECK(book_epub_load_cover(argv[1],&cover,&width,&height)==ESP_OK&&cover&&width==432&&height==648);free(cover);
    printf("inline image source/layout: %zu checks PASS; automatic visible pages, count parity, failure geometry, byte/item eviction and declared cover\n",checks);
}

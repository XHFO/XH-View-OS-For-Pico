/* SPDX-License-Identifier: Apache-2.0
 * Independent read-only review of production automatic-image pagination.
 * Source pixels/font/radio are test doubles; actual layout, jobs, hits and marks are production code.
 */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "book_layout.h"
#include "ttf_font.h"

static EpdRect body;
static unsigned checks, scenes;
static size_t painted_pixels, text_draws;
static uint8_t framebuffer;
#define CHECK(v) do { ++checks; if (!(v)) { fprintf(stderr,"automatic-image review line %d: %s\n",__LINE__,#v); abort(); } } while (0)

static bool inside(EpdRect outer, EpdRect inner) {
    return inner.width>0 && inner.height>0 && inner.x>=outer.x && inner.y>=outer.y &&
           inner.x+inner.width<=outer.x+outer.width && inner.y+inner.height<=outer.y+outer.height;
}
void epd_draw_pixel(int x,int y,uint8_t color,uint8_t* fb) {
    (void)color; (void)fb;
    CHECK(inside(body,(EpdRect){x,y,1,1})); ++painted_pixels;
}
int ttf_text_width_px(int px,const char* text) {
    int width=0;
    for (; *text; ++text) if (((unsigned char)*text&0xc0)!=0x80) width+=(unsigned char)*text<0x80?px/2:px;
    return width;
}
int ttf_ascender_px(int px) { return px; }
void ttf_draw_text_px(uint8_t* fb,int x,int y,int px,const char* text,enum EpdFontFlags align,uint8_t fg,uint8_t bg) {
    (void)fb; (void)fg; (void)bg;
    int width=ttf_text_width_px(px,text),left=x-(align==EPD_DRAW_ALIGN_CENTER?width/2:0);
    CHECK(inside(body,(EpdRect){left,y-px,width,px})); ++text_draws;
}

typedef struct { size_t pages,offsets[256]; EpdRect anchors[256]; bool visible[256]; } snapshot_t;
static size_t next_utf8(const char* text,size_t offset) {
    unsigned char ch=(unsigned char)text[offset]; return offset+(ch<0x80?1:ch<0xe0?2:ch<0xf0?3:4);
}
static snapshot_t snapshot(const char* text,size_t len,blk_t* blocks,size_t count,int px) {
    snapshot_t out={0};
    CHECK(book_layout_build_blocks(text,len,blocks,count,body,px));
    out.pages=book_layout_page_count(); CHECK(out.pages>0 && out.pages<256 && len<256);
    for (size_t page=0;page<=out.pages;++page) out.offsets[page]=book_layout_page_start_offset(page);
    for (size_t offset=0;offset<len;offset=next_utf8(text,offset)) {
        size_t page=book_layout_page_for_offset(offset);
        out.visible[offset]=book_layout_text_bounds(page,body,offset,&out.anchors[offset]);
        if (out.visible[offset]) {
            EpdRect at=out.anchors[offset]; CHECK(inside(body,at));
            CHECK(book_layout_text_at(page,body,at.x+at.width/2,at.y+at.height/2)==offset);
            CHECK(book_layout_text_nearest(page,body,at.x+at.width/2,at.y+at.height/2)==offset);
        }
    }
    for (size_t page=0;page<out.pages;++page) {
        book_layout_draw_page(&framebuffer,page,body,px);
        book_layout_draw_page(&framebuffer,page,body,px);
        for (size_t i=0;i<count;++i) if (!blocks[i].image_src)
            book_layout_draw_range(&framebuffer,page,body,blocks[i].offset,blocks[i].offset+blocks[i].len,false);
    }
    CHECK(book_layout_page_count()==out.pages);
    for (size_t page=0;page<=out.pages;++page) CHECK(book_layout_page_start_offset(page)==out.offsets[page]);
    book_layout_job_t* job=book_layout_job_create(text,len,blocks,count,body,px); CHECK(job);
    while (!book_layout_job_complete(job)) CHECK(book_layout_job_step(job,1));
    CHECK(book_layout_job_page_count(job)==out.pages);
    for (size_t page=0;page<out.pages;++page) {
        size_t offset=SIZE_MAX; CHECK(book_layout_job_page_offset(job,page,&offset)); CHECK(offset==out.offsets[page]);
    }
    book_layout_job_free(job);
    return out;
}
static void same_snapshot(snapshot_t a,snapshot_t b,size_t len) {
    CHECK(a.pages==b.pages);
    CHECK(!memcmp(a.offsets,b.offsets,(a.pages+1)*sizeof(a.offsets[0])));
    for (size_t i=0;i<len;++i) {
        CHECK(a.visible[i]==b.visible[i]);
        if (a.visible[i]) CHECK(!memcmp(&a.anchors[i],&b.anchors[i],sizeof(EpdRect)));
    }
}

static void test_scene(int orientation,int px) {
    body=orientation?(EpdRect){40,96,1136,564}:(EpdRect){40,96,604,1096};
    const char* strings[]={"甲乙丙丁 Before","wide","After wide 甲乙","tall","small","square","tail 甲乙丙丁","missing","end"};
    blk_t blocks[9]={0}; char text[256]={0}; size_t len=0;
    for (size_t i=0;i<9;++i) {
        size_t bytes=strlen(strings[i]); blocks[i].offset=len; blocks[i].len=bytes;
        memcpy(text+len,strings[i],bytes); len+=bytes;
        if (i+1<9) text[len++]='\n';
    }
    text[len]=0;
    size_t images[]={1,3,4,5,7}; uint16_t widths[]={648,1,24,648,0},heights[]={1,1000,24,1000,0};
    for (size_t i=0;i<5;++i) {
        blocks[images[i]].image_src="fixture.png";
        blocks[images[i]].image_width=widths[i]; blocks[images[i]].image_height=heights[i];
    }
    snapshot_t original=snapshot(text,len,blocks,9,px);
    CHECK(text_draws>0 && painted_pixels>0);
    for (size_t loaded=0;loaded<4;++loaded) {
        blk_t* block=&blocks[images[loaded]]; size_t bytes=(size_t)block->image_width*block->image_height;
        block->image=malloc(bytes); CHECK(block->image); memset(block->image,64,bytes);
        same_snapshot(original,snapshot(text,len,blocks,9,px),len);
    }
    for (size_t evicted=0;evicted<4;++evicted) {
        blk_t* block=&blocks[images[evicted]]; free(block->image); block->image=NULL;
        same_snapshot(original,snapshot(text,len,blocks,9,px),len);
    }
    book_layout_free(); ++scenes;
}
int main(void) {
    for (int orientation=0;orientation<2;++orientation) for (int px=36;px<=72;px+=12) test_scene(orientation,px);
    printf("independent automatic-image review: %u checks, %u portrait/landscape/font-size scenes; fixed page offsets, count jobs, UTF-8 anchors and bounded drawing PASS\n",checks,scenes);
    return 0;
}

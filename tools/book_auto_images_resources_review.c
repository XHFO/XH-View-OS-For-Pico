/* SPDX-License-Identifier: Apache-2.0
 * Independent tests of the exact production image-cache and opening-cover helpers.
 * Decoding/EPD are controlled doubles; production functions are extracted unchanged.
 */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "book_source.h"

static unsigned checks,decode_calls,cover_calls,paint_calls,save_calls;
#define CHECK(v) do { ++checks; if (!(v)) { fprintf(stderr,"image resource review line %d: %s\n",__LINE__,#v); abort(); } } while (0)
static blk_t* test_blocks;
static size_t test_count;
static int decode_error,cover_error;
static bool wrong_dimensions;
esp_err_t book_chapter_load_image(size_t chapter,const char* reference,uint8_t** pixels,uint16_t* width,uint16_t* height) {
    CHECK(chapter==2); ++decode_calls;
    size_t at=(size_t)strtoul(reference,NULL,10); CHECK(at<test_count);
    *width=test_blocks[at].image_width; *height=test_blocks[at].image_height;
    *pixels=malloc((size_t)*width**height); CHECK(*pixels);
    if (wrong_dimensions) ++*width;
    return decode_error;
}
#include "book-source-images-review.inc"

static void cache_bound(void) {
    size_t bytes=0,items=0;
    for (size_t i=0;i<test_count;++i) if (test_blocks[i].image) {
        bytes+=(size_t)test_blocks[i].image_width*test_blocks[i].image_height; ++items;
    }
    CHECK(bytes<=BOOK_INLINE_IMAGE_CACHE_BYTES && items<=BOOK_INLINE_IMAGE_CACHE_ITEMS);
}
static void clear_cache(void) {
    for (size_t i=0;i<test_count;++i) {free(test_blocks[i].image);free(test_blocks[i].image_src);}
    free(test_blocks);test_blocks=NULL;test_count=0;
}
static void fixture(size_t count,uint16_t width,uint16_t height) {
    clear_cache();test_blocks=calloc(count,sizeof(*test_blocks));CHECK(test_blocks);test_count=count;
    for (size_t i=0;i<count;++i) {
        char ref[32];snprintf(ref,sizeof(ref),"%zu",i);test_blocks[i].image_src=strdup(ref);CHECK(test_blocks[i].image_src);
        test_blocks[i].offset=i*10;test_blocks[i].len=9;test_blocks[i].image_width=width;test_blocks[i].image_height=height;
    }
}
static void test_cache(void) {
    CHECK(!book_chapter_prepare_images(2,NULL,0,0,10));
    fixture(5,648,1000);
    for (size_t i=0;i<5;++i) {
        CHECK(book_chapter_prepare_images(2,test_blocks,test_count,i*10,i*10+10));cache_bound();CHECK(test_blocks[i].image);
        for (size_t j=0;j<test_count;++j) CHECK(test_blocks[j].image_width==648 && test_blocks[j].image_height==1000);
    }
    CHECK(!test_blocks[0].image && !test_blocks[1].image && !test_blocks[2].image);
    unsigned before=decode_calls;CHECK(!book_chapter_prepare_images(2,test_blocks,test_count,40,50));CHECK(decode_calls==before);
    fixture(3,648,1000);CHECK(book_chapter_prepare_images(2,test_blocks,test_count,0,30));cache_bound();
    CHECK(test_blocks[0].image && test_blocks[1].image && !test_blocks[2].image && !test_blocks[2].image_failed);
    fixture(75,1,1);CHECK(book_chapter_prepare_images(2,test_blocks,test_count,0,750));cache_bound();
    for (size_t i=0;i<75;++i) CHECK((test_blocks[i].image!=NULL)==(i<BOOK_INLINE_IMAGE_CACHE_ITEMS));
    before=decode_calls;CHECK(!book_chapter_prepare_images(2,test_blocks,test_count,0,750));CHECK(decode_calls==before);
    CHECK(book_chapter_prepare_images(2,test_blocks,test_count,640,750));cache_bound();
    for (size_t i=64;i<75;++i) CHECK(test_blocks[i].image);
    CHECK(!book_chapter_prepare_images(2,test_blocks,test_count,750,750));
    CHECK(!book_chapter_prepare_images(2,test_blocks,HTML_TEXT_MAX_BLOCKS+1,0,750));
    for (int error=0;error<2;++error) {
        fixture(1,16,8);decode_error=error?ESP_ERR_NO_MEM:ESP_OK;wrong_dimensions=!error;
        CHECK(book_chapter_prepare_images(2,test_blocks,1,0,10));CHECK(!test_blocks[0].image && test_blocks[0].image_failed);cache_bound();
        before=decode_calls;CHECK(!book_chapter_prepare_images(2,test_blocks,1,0,10));CHECK(decode_calls==before);
    }
    decode_error=ESP_OK;wrong_dimensions=false;clear_cache();
}

typedef enum {APP_REDRAW_NONE,APP_REDRAW_PAGE,APP_REDRAW_AREA} app_redraw_t;
typedef struct {int leaf;int64_t now_ms;} app_ctx_t;
enum {MODE_GL16=1,SHELF=0,READING=1,TOC=2};
#define APP_GC16_EVERY 0
#define ESP_LOGI(...) ((void)0)
static bool s_image_open,s_cover_open,s_cover_missing,s_clear_confirm,s_selection,s_scrubbing,s_progress_pending,s_page_jump_open,s_toolbar,s_full,s_save_failed;
static uint8_t* s_image_pixels,*s_next_fb;
static uint16_t s_image_width,s_image_height;
static size_t s_image_block=SIZE_MAX,s_page,s_chapter;
static const char* s_image_error;
static const char* s_text="text body";
static char s_image_origin[224],s_path[256]="/sdcard/books/original-fixture.epub",s_message[128];
static int s_view=READING,s_unsaved;
static int64_t s_last_turn_ms;
static book_kind_t test_kind=BOOK_KIND_EPUB;
book_kind_t book_kind(void){return test_kind;}
size_t book_chapter_count(void){return 3;}
static size_t book_layout_page_count(void){return 5;}
static bool book_layout_complete(void){return true;}
static bool book_layout_extend(size_t count){(void)count;return true;}
static void free_book(void){CHECK(false);}
static void lock_draw(void){}
static void unlock_draw(void){}
static void invalidate_prep(void){}
static void ensure_prep(void){if(!s_next_fb){s_next_fb=malloc(16);CHECK(s_next_fb);}}
static void save_progress(void){++save_calls;}
static void copy_text(char* out,size_t cap,const char* text){snprintf(out,cap,"%s",text);}
static int toc_rows(void){return 3;}
static bool load_chapter(app_ctx_t* ctx,size_t chapter,size_t offset,bool last){(void)ctx;(void)chapter;(void)offset;(void)last;CHECK(false);return false;}
static app_redraw_t paint_reading(app_ctx_t* ctx,int mode){(void)ctx;CHECK(mode==MODE_GL16);++paint_calls;return APP_REDRAW_AREA;}
static esp_err_t book_epub_load_cover(const char* path,uint8_t** pixels,uint16_t* width,uint16_t* height){
    CHECK(!strcmp(path,s_path));++cover_calls;*pixels=malloc(100);CHECK(*pixels);*width=*height=10;return cover_error;
}
#include "book-cover-lifecycle-review.inc"
static void test_cover(void) {
    app_ctx_t ctx={.now_ms=10000};s_page=s_chapter=0;
    CHECK(open_cover() && s_cover_open && s_image_open && s_image_pixels);
    CHECK(s_image_width==10 && s_image_height==10 && s_image_block==SIZE_MAX);
    CHECK(s_page==0 && s_chapter==0 && !save_calls && !paint_calls);
    CHECK(turn_page(&ctx,-1)==APP_REDRAW_NONE && s_cover_open);
    CHECK(turn_page(&ctx,1)==APP_REDRAW_PAGE && !s_cover_open && !s_image_open && !s_image_pixels);
    CHECK(s_page==0 && s_chapter==0 && !save_calls && !paint_calls);
    CHECK(turn_page(&ctx,-1)==APP_REDRAW_PAGE && s_cover_open);
    CHECK(close_image()==APP_REDRAW_PAGE && !s_image_pixels && s_page==0 && s_chapter==0);
    CHECK(turn_page(&ctx,1)==APP_REDRAW_AREA && s_page==1 && !s_cover_open && paint_calls==1);
    CHECK(turn_page(&ctx,-1)==APP_REDRAW_AREA && s_page==0 && !s_cover_open && paint_calls==2);
    unsigned before=cover_calls;test_kind=BOOK_KIND_TXT;CHECK(!open_cover() && before==cover_calls);test_kind=BOOK_KIND_EPUB;
    cover_error=ESP_ERR_NO_MEM;CHECK(!open_cover() && !s_image_pixels && !s_cover_missing);
    cover_error=ESP_OK;CHECK(open_cover());CHECK(close_image()==APP_REDRAW_PAGE);
    cover_error=ESP_ERR_NOT_FOUND;CHECK(!open_cover() && !s_image_pixels && s_cover_missing);
    before=cover_calls;CHECK(!open_cover() && before==cover_calls);
    CHECK(!s_image_open && s_page==0 && s_chapter==0 && !save_calls);
    free(s_next_fb);s_next_fb=NULL;free_image();
}
int main(void) {
    test_cache();test_cover();
    printf("independent image resources/cover review: %u checks; byte/item bounds, eviction, failed decode, first cover, previous/next and preserved text position PASS\n",checks);
    return 0;
}

/* SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 * 中文：独立计页、真实活动布局隔离、精确UTF-8页首和EPUB只读历史测试。
 * English: Independent pagination, real active-layout isolation, exact UTF-8 starts and EPUB history tests.
 */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "book_layout.h"
#include "book_source.h"
#include "book_epub.h"
#include "ttf_font.h"
int job_test_fail_after=-1;
size_t job_test_largest_request;
static size_t checks;
#define CHECK(x) do { assert(x); ++checks; } while(0)
static char drawn[12000], active[1201];
static EpdRect active_rect={40,24,80,60};
static size_t active_count,active_scan;
void epd_draw_pixel(int x,int y,uint8_t c,uint8_t* fb){(void)x;(void)y;(void)c;(void)fb;}
int ttf_text_width_px(int px,const char* t){int w=0;for(;*t;++t)if(((unsigned char)*t&0xc0)!=0x80)w+=*t=='^'?0:*t=='i'?px/2:*t=='W'?px+px/2:px;return w;}
int ttf_ascender_px(int px){return px;}
void ttf_draw_text_px(uint8_t* fb,int x,int y,int px,const char* t,enum EpdFontFlags a,uint8_t f,uint8_t b){(void)fb;(void)x;(void)y;(void)px;(void)a;(void)f;(void)b;CHECK(strlen(drawn)+strlen(t)<sizeof(drawn));strcat(drawn,t);}
static void restore_active(void){
    CHECK(book_layout_begin_blocks(active,1200,NULL,0,active_rect,10));
    active_count=book_layout_page_count();active_scan=book_layout_page_start_offset(active_count);
    CHECK(!book_layout_complete());
}
static void active_unchanged(void){
    CHECK(book_layout_page_count()==active_count&&!book_layout_complete());
    CHECK(book_layout_page_start_offset(active_count)==active_scan);
    uint8_t fb=0;drawn[0]=0;book_layout_draw_page(&fb,0,active_rect,10);CHECK(strlen(drawn)==32);
    CHECK(book_layout_text_at(0,active_rect,40,24)==0);
}
static void parity(const char* t,size_t n,const blk_t* b,size_t bn,EpdRect r,int px){
    CHECK(book_layout_build_blocks(t,n,b,bn,r,px));
    size_t expected=book_layout_page_count(),offsets[4096];CHECK(expected<=4096);
    for(size_t p=0;p<expected;++p)offsets[p]=book_layout_page_start_offset(p);
    restore_active();
    book_layout_job_t* j=book_layout_job_create(t,n,b,bn,r,px);CHECK(j);active_unchanged();
    size_t marker=999999;
    CHECK(!book_layout_job_page_offset(j,book_layout_job_page_count(j),&marker)&&marker==999999);
    CHECK(book_layout_job_step(j,0));
    size_t iterations=0;
    while(!book_layout_job_complete(j)){
        size_t before=book_layout_job_page_count(j);CHECK(book_layout_job_step(j,1));
        CHECK(book_layout_job_page_count(j)<=before+1);active_unchanged();CHECK(++iterations<=4096);
    }
    CHECK(book_layout_job_page_count(j)==expected);
    for(size_t p=0;p<expected;++p){size_t off=SIZE_MAX;CHECK(book_layout_job_page_offset(j,p,&off)&&off==offsets[p]);CHECK(off==n||((unsigned char)t[off]&0xc0)!=0x80);}
    book_layout_job_free(j);active_unchanged();
}
static void test_jobs(void){
    memset(active,'x',1200);active[1200]=0;restore_active();
    parity("",0,NULL,0,(EpdRect){0,0,40,60},10);
    parity("甲乙丙丁戊己庚辛壬癸\r\nWi中文。\n\nnext",strlen("甲乙丙丁戊己庚辛壬癸\r\nWi中文。\n\nnext"),NULL,0,(EpdRect){0,0,40,45},10);
    for(int width=40;width<=160;width+=40)for(int px=10;px<=18;px+=4)parity(active,1200,NULL,0,(EpdRect){0,0,width,120},px);
    const char mixed[]="Heading\n甲乙丙丁\nimg\nend";
    blk_t blocks[]={{.offset=0,.len=7,.heading=true},{.offset=8,.len=12},{.offset=21,.len=3,.image_src="image.jpg"},{.offset=25,.len=3}};
    parity(mixed,strlen(mixed),blocks,4,(EpdRect){40,24,100,100},10);
    uint8_t pixels[2000]={0};blocks[2].image_src=NULL;blocks[2].image=pixels;blocks[2].image_width=40;blocks[2].image_height=50;
    parity(mixed,strlen(mixed),blocks,4,(EpdRect){40,24,100,100},10);
    restore_active();
    book_layout_job_t* a=book_layout_job_create(active,1200,NULL,0,(EpdRect){0,0,40,30},10);
    book_layout_job_t* b=book_layout_job_create(active,1200,NULL,0,(EpdRect){0,0,80,60},10);CHECK(a&&b);
    for(int i=0;i<9;++i){CHECK(book_layout_job_step(a,1)&&book_layout_job_step(b,2));active_unchanged();}
    CHECK(book_layout_job_page_count(a)==11&&book_layout_job_page_count(b)==20);book_layout_job_free(a);book_layout_job_free(b);
    for(int fail=0;fail<2;++fail){job_test_fail_after=fail;CHECK(!book_layout_job_create(active,1200,NULL,0,active_rect,10));job_test_fail_after=-1;active_unchanged();}
    a=book_layout_job_create(active,1200,NULL,0,(EpdRect){0,0,40,30},10);CHECK(a);job_test_fail_after=0;
    CHECK(!book_layout_job_step(a,100));job_test_fail_after=-1;CHECK(!book_layout_job_complete(a)&&!book_layout_job_page_count(a)&&!book_layout_job_step(a,1));book_layout_job_free(a);active_unchanged();
    CHECK(!book_layout_job_create("\xe7\x94",2,NULL,0,active_rect,10));active_unchanged();
    CHECK(!book_layout_job_create("x",1,NULL,0,(EpdRect){0,0,1,30},10));active_unchanged();
    CHECK(!book_layout_job_step(NULL,1)&&!book_layout_job_complete(NULL)&&!book_layout_job_page_count(NULL));book_layout_job_free(NULL);
    char* long_zero=malloc(2*1024*1024+1);CHECK(long_zero);memset(long_zero,'^',2*1024*1024);long_zero[2*1024*1024]=0;
    job_test_largest_request=0;a=book_layout_job_create(long_zero,2*1024*1024,NULL,0,active_rect,10);CHECK(a&&book_layout_job_complete(a)&&book_layout_job_page_count(a)==1);CHECK(job_test_largest_request<1024);book_layout_job_free(a);free(long_zero);active_unchanged();
    CHECK(book_layout_extend(2)&&book_layout_page_count()==active_count+2);restore_active();
}
static void same_geometry(const html_text_t* a,const html_text_t* b){
    CHECK(a->len==b->len&&a->count==b->count&&!strcmp(a->utf8,b->utf8));
    for(size_t i=0;i<a->count;++i){const blk_t* x=a->blocks+i;const blk_t* y=b->blocks+i;CHECK(x->offset==y->offset&&x->len==y->len&&x->heading==y->heading&&x->image_width==y->image_width&&x->image_height==y->image_height);CHECK((!x->image_src&&!y->image_src)||(x->image_src&&y->image_src&&!strcmp(x->image_src,y->image_src)));}
}
static void test_source(const char* path){
    CHECK(book_open(path)==ESP_OK);html_text_t preview={0},normal={0};
    for(size_t i=0;i<2;++i){CHECK(book_chapter_load_blocks_for_pagination(i,&preview)==ESP_OK);parity(preview.utf8,preview.len,preview.blocks,preview.count,(EpdRect){0,0,100,100},10);html_text_free(&preview);}
    CHECK(book_chapter_load_blocks(1,&normal)==ESP_OK);size_t images=0;
    for(size_t i=0;i<normal.count;++i)if(normal.blocks[i].image_src){++images;CHECK(!normal.blocks[i].image_title&&!normal.blocks[i].image_repeated&&normal.blocks[i].image_first_chapter==1&&!normal.blocks[i].image);}
    CHECK(images==2);CHECK(book_chapter_load_blocks_for_pagination(1,&preview)==ESP_OK);same_geometry(&preview,&normal);html_text_free(&preview);html_text_free(&normal);book_close();
    CHECK(book_open(path)==ESP_OK);CHECK(book_chapter_load_blocks(0,&normal)==ESP_OK);html_text_free(&normal);CHECK(book_chapter_load_blocks(1,&normal)==ESP_OK);
    images=0;size_t titles=0;for(size_t i=0;i<normal.count;++i)if(normal.blocks[i].image_src){++images;titles+=normal.blocks[i].image_title;CHECK(normal.blocks[i].image_repeated&&normal.blocks[i].image_first_chapter==0);}
    CHECK(images==2&&titles==1);html_text_free(&normal);book_close();
    CHECK(book_chapter_load_blocks_for_pagination(0,&preview)!=ESP_OK&&!preview.utf8);
    CHECK(book_chapter_load_blocks_for_pagination(0,NULL)!=ESP_OK);
}
static void test_txt(const char* path){
    CHECK(book_open(path)==ESP_OK);html_text_t a={0},b={0};
    CHECK(book_chapter_load_blocks_for_pagination(0,&a)==ESP_OK&&book_chapter_load_blocks(0,&b)==ESP_OK);
    same_geometry(&a,&b);CHECK(!a.blocks&&!b.blocks);parity(a.utf8,a.len,NULL,0,(EpdRect){0,0,80,60},10);
    html_text_free(&a);html_text_free(&b);book_close();active_unchanged();
}
int main(int argc,char** argv){CHECK(argc==3);test_jobs();test_source(argv[1]);test_txt(argv[2]);book_layout_free();printf("book_pagination_host_test: %zu checks PASS (actual layout/source/EPUB, font metric and platform doubles)\n",checks);}

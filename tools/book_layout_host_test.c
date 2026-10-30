/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 * 中文：用伪字体验证布局完整性、偏移和测宽复杂度。
 * English: Verify layout completeness, offsets and measurement complexity using a fake font.
 * 冻结：只用于宿主测试。/ Frozen: Host testing only.
 */
#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "book_layout.h"
#include "ttf_font.h"
#ifdef BOOK_LAYOUT_TEST_ALLOC
int book_layout_fail_after = -1;
#endif
static char drawn[20000];
static size_t measured_codepoints;
static size_t measure_calls;
static int first_draw_px, last_draw_px;
static size_t image_pixels;
static int ink_min_x,ink_min_y,ink_max_x,ink_max_y;
void epd_draw_pixel(int x, int y, uint8_t color, uint8_t* fb) {
    (void)color; (void)fb;
    assert(x >= 0 && y >= 0);
    if (!image_pixels || x < ink_min_x) ink_min_x=x;
    if (!image_pixels || y < ink_min_y) ink_min_y=y;
    if (!image_pixels || x > ink_max_x) ink_max_x=x;
    if (!image_pixels || y > ink_max_y) ink_max_y=y;
    ++image_pixels;
}
int ttf_text_width_px(int px, const char* text) {
    int n = 0, width = 0;
    for (; *text; text++) if (((unsigned char)*text & 0xc0) != 0x80) {
        n++;
        width += *text == 'i' ? px / 2 : *text == 'W' ? px + px / 2 : px;
    }
    measured_codepoints += (size_t)n;
    measure_calls++;
    return width;
}
int ttf_ascender_px(int px) { return px; }
void ttf_draw_text_px(uint8_t* fb, int x, int y, int px, const char* text,
                      enum EpdFontFlags align, uint8_t fg, uint8_t bg) {
    (void)fb; (void)x; (void)y; (void)px; (void)align;
    assert(fg <= 15 && bg <= 15);
    if (!drawn[0]) first_draw_px = px;
    last_draw_px = px;
    assert(strlen(drawn) + strlen(text) < sizeof(drawn));
    strcat(drawn, text);
}
int main(void) {
    EpdRect r = {0, 0, 20, 30};
    const char text[] = "甲乙丙丁戊己庚辛壬癸";
    assert(book_layout_build(text, strlen(text), r, 10));
    assert(book_layout_page_count() == 3);
    assert(book_layout_page_start_offset(1) == 12);
    assert(book_layout_page_for_offset(11) == 0);
    assert(book_layout_page_for_offset(12) == 1);
    assert(book_layout_page_for_offset(999) == 2);
    uint8_t fb = 0;
    for (size_t i = 0; i < book_layout_page_count(); i++) {
        assert(book_layout_page_start_offset(i) % 3 == 0);
        book_layout_draw_page(&fb, i, r, 10);
    }
    assert(strcmp(drawn, text) == 0);
    const char mixed[] = "Wi甲iiW";
    assert(book_layout_build(mixed, strlen(mixed), r, 10));
    assert(book_layout_page_count() == 2);
    assert(book_layout_page_start_offset(1) == 7);
    drawn[0] = 0;
    book_layout_draw_page(&fb, 0, r, 10);
    book_layout_draw_page(&fb, 1, r, 10);
    assert(strcmp(drawn, mixed) == 0);
    // 标记与命中复用真实变宽字形，跨行仍指向同一UTF-8锚点，且不改分页或原文。
    // Hits and marks use actual variable advances, retaining UTF-8 anchors across lines without mutating text/pages.
    EpdRect annotation={20,30,40,60};
    assert(book_layout_build(mixed,strlen(mixed),annotation,10));
    assert(book_layout_text_at(0,annotation,20,30)==0);
    assert(book_layout_text_at(0,annotation,34,35)==0);
    assert(book_layout_text_at(0,annotation,35,35)==1);
    assert(book_layout_text_at(0,annotation,40,35)==2);
    assert(book_layout_text_at(0,annotation,49,35)==2);
    assert(book_layout_text_at(0,annotation,50,35)==5);
    assert(book_layout_text_at(0,annotation,59,35)==6);
    assert(book_layout_text_at(0,annotation,20,45)==7);
    assert(book_layout_text_at(0,annotation,35,45)==SIZE_MAX);
    assert(book_layout_text_at(0,annotation,19,35)==SIZE_MAX);
    assert(book_layout_text_at(1,annotation,20,30)==SIZE_MAX);
    EpdRect bounds;
    assert(book_layout_text_bounds(0,annotation,2,&bounds)&&bounds.x==40&&bounds.y==30&&bounds.width==10&&bounds.height==10);
    assert(book_layout_text_bounds(0,annotation,7,&bounds)&&bounds.x==20&&bounds.y==45&&bounds.width==15);
    assert(!book_layout_text_bounds(0,annotation,3,&bounds));
    assert(!book_layout_text_bounds(0,annotation,strlen(mixed),&bounds));
    assert(!book_layout_text_bounds(1,annotation,0,&bounds));
    assert(!book_layout_text_bounds(0,annotation,0,NULL));
    assert(book_layout_text_nearest(0,annotation,-100,-100)==0);
    assert(book_layout_text_nearest(0,annotation,1000,35)==6);
    assert(book_layout_text_nearest(0,annotation,1000,1000)==7);
    assert(book_layout_text_nearest(0,annotation,INT_MIN,INT_MIN)==0);
    assert(book_layout_text_nearest(0,annotation,INT_MAX,INT_MAX)==7);
    assert(book_layout_text_nearest(1,annotation,20,30)==SIZE_MAX);
    assert(book_layout_text_nearest(0,(EpdRect){20,30,41,60},20,30)==SIZE_MAX);
    image_pixels=0;book_layout_draw_range(&fb,0,annotation,2,7,false);
    assert(image_pixels==40&&ink_min_x==40&&ink_max_x==59&&ink_min_y==41&&ink_max_y==42);
    image_pixels=0;book_layout_draw_range(&fb,0,annotation,2,7,true);
    assert(image_pixels==80&&ink_min_y==41&&ink_max_y==44);
    image_pixels=0;book_layout_draw_range(&fb,0,annotation,5,8,false);
    assert(image_pixels==50&&ink_min_x==20&&ink_max_x==59&&ink_min_y==41&&ink_max_y==57);
    image_pixels=0;
    book_layout_draw_range(&fb,0,annotation,3,7,false);
    book_layout_draw_range(&fb,0,annotation,2,4,false);
    book_layout_draw_range(&fb,0,annotation,2,99,false);
    book_layout_draw_range(&fb,1,annotation,2,7,false);
    book_layout_draw_range(&fb,0,annotation,7,2,false);
    assert(!image_pixels&&book_layout_page_count()==1&&book_layout_page_start_offset(0)==0&&!strcmp(mixed,"Wi甲iiW"));
    assert(book_layout_build("", 0, r, 10));
    assert(book_layout_page_count() == 1);
    assert(book_layout_build(NULL, 0, r, 10));
    assert(!book_layout_build(NULL, 1, r, 10));
    assert(book_layout_page_count() == 0);
    assert(!book_layout_build("\xe7\x94", 2, r, 10));
    assert(!book_layout_build("x\0y", 3, r, 10));
    assert(!book_layout_build("x", 1, (EpdRect){0,0,5,30}, 10));
    assert(!book_layout_build("x", 1, (EpdRect){0,0,20,5}, 10));
    char* many = malloc(4097);
    memset(many, 'x', 4097);
    r = (EpdRect){0,0,10,15};
    assert(book_layout_build(many, 4096, r, 10));
    assert(book_layout_page_count() == 4096);
    assert(!book_layout_build(many, 4097, r, 10));
    assert(book_layout_page_count() == 0);
    free(many);
    assert(book_layout_build("a\r\n\r\nb", 6, (EpdRect){0,0,20,100}, 10));
    drawn[0] = 0;
    book_layout_draw_page(&fb, 0, (EpdRect){0,0,20,100}, 10);
    assert(strcmp(drawn, "ab") == 0);
    char long_line[1024];
    memset(long_line, 'z', sizeof(long_line));
    r = (EpdRect){0,0,10000,30};
    measured_codepoints = measure_calls = 0;
    assert(book_layout_build(long_line, sizeof(long_line), r, 10));
    printf("long-line build: %zu calls, %zu measured codepoints\n", measure_calls, measured_codepoints);
    fflush(stdout);
    assert(measured_codepoints <= sizeof(long_line) * 2);
    assert(book_layout_page_count() == 1);
    drawn[0] = 0;
    book_layout_draw_page(&fb, 0, r, 11);
    assert(drawn[0] == 0);
    book_layout_draw_page(&fb, 99, r, 10);
    assert(drawn[0] == 0);
    measured_codepoints = measure_calls = 0;
    book_layout_draw_page(&fb, 0, r, 10);
    assert(measured_codepoints <= sizeof(long_line) * 2);
    assert(strlen(drawn) == sizeof(long_line));
    assert(memcmp(drawn, long_line, sizeof(long_line)) == 0);
    assert(book_layout_page_start_offset(99) == sizeof(long_line));
    assert(!book_layout_build("\xed\xa0\x80", 3, r, 10));
    assert(!book_layout_build("\xf4\x90\x80\x80", 4, r, 10));
    assert(!book_layout_build("\xc0\xaf", 2, r, 10));
    book_layout_free();
    assert(book_layout_page_count() == 0);
    const char styled[] = "Title\nbody";
    blk_t blocks[] = {{.offset=0, .len=5, .heading=true}, {.offset=6, .len=4}};
    r = (EpdRect){0, 0, 100, 45};
    assert(book_layout_build_blocks(styled, strlen(styled), blocks, 2, r, 10));
    assert(book_layout_page_count() == 2);
    assert(book_layout_page_start_offset(1) == 6);
    drawn[0] = 0;
    book_layout_draw_page(&fb, 0, r, 10);
    book_layout_draw_page(&fb, 1, r, 10);
    assert(!strcmp(drawn, "Titlebody") && first_draw_px == 18 && last_draw_px == 10);
    r.width = 20;
    assert(book_layout_build_blocks(styled, strlen(styled), blocks, 2, r, 10));
    assert(book_layout_page_count() == 6);
    assert(book_layout_page_start_offset(5) == 6);
    drawn[0] = 0;
    for (size_t i = 0; i < book_layout_page_count(); ++i) book_layout_draw_page(&fb, i, r, 10);
    assert(!strcmp(drawn, "Titlebody"));
    assert(!book_layout_build_blocks(styled, strlen(styled), blocks, 2, (EpdRect){0,0,100,20}, 10));
    blocks[1].offset = 5;
    assert(!book_layout_build_blocks(styled, strlen(styled), blocks, 2, r, 10));
    assert(book_layout_page_count() == 0);
    blocks[1].offset = 6;
    blocks[1].len = SIZE_MAX;
    assert(!book_layout_build_blocks(styled, strlen(styled), blocks, 2, r, 10));
    assert(!book_layout_build_blocks(styled, strlen(styled), NULL, 2, r, 10));
    blk_t split_utf8[] = {{.offset=0, .len=1, .heading=true}, {.offset=2, .len=2}};
    assert(!book_layout_build_blocks("甲\nx", 5, split_utf8, 2, r, 10));
    assert(book_layout_build("body", 4, r, 10));
    drawn[0] = 0;
    book_layout_draw_page(&fb, 0, r, 10);
    assert(first_draw_px == 10);
    // 分批与一次分页的边界相同，第一页不必测量整章。
    // Incremental and eager pagination agree; the first page never measures the whole chapter.
    char chapter[1201]; memset(chapter, 'x', 1200); chapter[1200] = 0;
    r = (EpdRect){0,0,40,30};
    assert(book_layout_build(chapter, 1200, r, 10));
    size_t pages = book_layout_page_count(), offsets[256];
    assert(pages < 256);
    for (size_t i=0; i<pages; ++i) offsets[i] = book_layout_page_start_offset(i);
    measured_codepoints = 0;
    assert(book_layout_begin_blocks(chapter, 1200, NULL, 0, r, 10));
    assert(!book_layout_complete() && book_layout_page_count() == 2 && measured_codepoints < 40);
    drawn[0] = 0;
    book_layout_draw_page(&fb, 0, r, 10);
    assert(strlen(drawn) == 8);
    while (!book_layout_complete()) {
        size_t before = book_layout_page_count();
        assert(book_layout_extend(2));
        assert(book_layout_page_count() > before && book_layout_page_count() <= before + 2);
    }
    assert(book_layout_page_count() == pages);
    for (size_t i=0; i<pages; ++i) assert(book_layout_page_start_offset(i) == offsets[i]);
    assert(book_layout_page_for_offset(501) == 62);
    // 图片单块换页，文字不丢失；适配后仍在正文边界内。
    // Images cross pages atomically without losing text and fit inside the body.
    uint8_t pixels[2000]; memset(pixels, 128, sizeof(pixels));
    blk_t illustrated[] = {{.offset=0,.len=1}, {.offset=2,.len=3,.image=pixels,.image_width=40,.image_height=50}, {.offset=6,.len=1}};
    r = (EpdRect){0,0,20,30};
    assert(book_layout_build_blocks("a\nimg\nb", 7, illustrated, 3, r, 10));
    assert(book_layout_page_count() == 3);
    assert(book_layout_page_start_offset(1) == 2 && book_layout_page_start_offset(2) == 6);
    drawn[0] = 0; image_pixels = 0;
    for (size_t i=0; i<3; ++i) book_layout_draw_page(&fb,i,r,10);
    assert(!strcmp(drawn,"ab") && image_pixels == 500);
    // 占位命中与绘图边界一致，不读取像素；重复提示不改变文本偏移。
    // Placeholder hits match drawing bounds without loading pixels; repeat hints preserve text offsets.
    illustrated[1].image = NULL; illustrated[1].image_src = "cover.jpg";
    r = (EpdRect){40,24,604,1000};
    assert(book_layout_build_blocks("a\nimg\nb", 7, illustrated, 3, r, 48));
    size_t before_pages = book_layout_page_count();
    size_t before_offset = book_layout_page_start_offset(0);
    EpdRect hit;
    assert(book_layout_image_at(0,r,100,150,&hit)==1);
    assert(hit.x==48 && hit.y>=r.y && hit.y+hit.height<=r.y+r.height);
    assert(book_layout_image_at(0,r,hit.x,hit.y,NULL)==1);
    assert(book_layout_image_at(0,r,hit.x+hit.width-1,hit.y+hit.height-1,NULL)==1);
    assert(book_layout_image_at(0,r,hit.x-1,hit.y,NULL)==SIZE_MAX);
    assert(book_layout_image_at(0,r,hit.x+hit.width,hit.y,NULL)==SIZE_MAX);
    assert(book_layout_image_at(0,r,hit.x,hit.y+hit.height,NULL)==SIZE_MAX);
    assert(book_layout_image_at(1,r,100,150,NULL)==SIZE_MAX);
    assert(book_layout_image_at(0,(EpdRect){40,24,600,1000},100,150,NULL)==SIZE_MAX);
    drawn[0]=0;book_layout_draw_page(&fb,0,r,48);
    assert(strstr(drawn,"图片加载中"));
    illustrated[1].image_repeated=true; illustrated[1].image_first_chapter=7;
    drawn[0]=0;book_layout_draw_page(&fb,0,r,48);
    assert(!strstr(drawn,"重复图片")); // 矮图不绘制溢出边界的两行提示。/ Short rows omit overflowing two-line hints.
    illustrated[1].image_title=true;
    drawn[0]=0;book_layout_draw_page(&fb,0,r,48);
    assert(!strstr(drawn,"重复") && !strstr(drawn,"第 8 节"));
    illustrated[1].image_failed=true;
    drawn[0]=0;book_layout_draw_page(&fb,0,r,48);
    assert(strstr(drawn,"点击重试"));
    illustrated[1].image=pixels;
    assert(book_layout_image_at(0,r,r.x+r.width/2,150,&hit)==1&&hit.width==40&&hit.height==50);
    illustrated[1].image=NULL;
    assert(book_layout_page_count()==before_pages && book_layout_page_start_offset(0)==before_offset);
    assert(book_layout_text_at(0,r,40,24)==0);
    assert(book_layout_text_at(0,r,40,97)==SIZE_MAX);
    assert(book_layout_text_at(0,r,100,150)==SIZE_MAX);
    image_pixels=0;book_layout_draw_range(&fb,0,r,2,5,true);assert(!image_pixels);
    assert(!book_layout_text_bounds(0,r,2,&bounds));
    assert(book_layout_text_bounds(0,r,6,&bounds));
    assert(book_layout_text_nearest(0,r,100,150)!=2);
    // 图片独占页不会被当成文字；标题字形与段间吸附使用真实折行。
    // Image-only pages contain no text hit; heading and gap snapping follow actual wrapping.
    r=(EpdRect){10,20,40,60};
    blk_t image_only={.offset=0,.len=3,.image=pixels,.image_width=40,.image_height=50};
    assert(book_layout_build_blocks("img",3,&image_only,1,r,10));
    assert(book_layout_text_nearest(0,r,10,20)==SIZE_MAX&&!book_layout_text_bounds(0,r,0,&bounds));
    const char heading_body[]="Hi\n甲乙";
    blk_t heading_blocks[]={{.offset=0,.len=2,.heading=true},{.offset=3,.len=6}};
    r=(EpdRect){10,20,100,100};
    assert(book_layout_build_blocks(heading_body,strlen(heading_body),heading_blocks,2,r,10));
    assert(book_layout_text_bounds(0,r,0,&bounds)&&bounds.height==18&&bounds.y==20);
    assert(book_layout_text_bounds(0,r,3,&bounds)&&bounds.y==60&&bounds.height==10);
    assert(book_layout_text_at(0,r,10,50)==SIZE_MAX);
    assert(book_layout_text_nearest(0,r,10,50)==0);
    // 不完整旧页表在目标失败后保持原扫描状态，成功则先补排目标而非夹到已排末页。
    // An incomplete old layout retains its scan state on failure; success paginates through the target.
    r=(EpdRect){0,0,40,30};
    assert(book_layout_begin_blocks(chapter,1200,NULL,0,r,10));
    size_t retained_count=book_layout_page_count(),retained_scan=book_layout_page_start_offset(retained_count);
    assert(!book_layout_position_blocks("\xe7\x94",2,NULL,0,r,10,0,false));
    assert(book_layout_page_count()==retained_count&&book_layout_page_start_offset(retained_count)==retained_scan&&!book_layout_complete());
#ifdef BOOK_LAYOUT_TEST_ALLOC
    for(int failure=0;failure<5;++failure){
        book_layout_fail_after=failure;
        assert(!book_layout_position_blocks(chapter,1200,NULL,0,r,10,1199,false));
        book_layout_fail_after=-1;
        assert(book_layout_page_count()==retained_count&&book_layout_page_start_offset(retained_count)==retained_scan&&!book_layout_complete());
        drawn[0]=0;book_layout_draw_page(&fb,0,r,10);assert(strlen(drawn)==8);
    }
#endif
    assert(book_layout_extend(2)&&book_layout_page_count()==retained_count+2);
    assert(book_layout_position_blocks(chapter,1200,NULL,0,r,10,501,false));
    assert(book_layout_page_for_offset(501)==62&&book_layout_page_count()>=63&&!book_layout_complete());
    assert(book_layout_position_blocks(chapter,1200,NULL,0,r,10,1200,false));
    assert(book_layout_complete()&&book_layout_page_for_offset(1200)==book_layout_page_count()-1);
    assert(book_layout_position_blocks(mixed,strlen(mixed),NULL,0,annotation,10,0,true)&&book_layout_complete());
    book_layout_free();
    puts("book_layout_host_test: PASS");
}

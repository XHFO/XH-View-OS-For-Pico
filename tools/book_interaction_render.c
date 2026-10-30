/* SPDX-License-Identifier: Apache-2.0
 * 中文：链接真实生产字体/分页函数，检查UTF-8命中、跨行拖选、手柄与数字跳页弹窗绘图。
 * English: Link production font/layout for UTF-8 hits, drag snapping, handles and numeric-page modal drawing.
 * 冻结：仅宿主测试；不复制阅读器状态机、不访问设备，不代表实屏触摸或刷新验收。
 * Frozen: Host only; no reader state-machine copy or device access, and no claim of physical touch/refresh acceptance.
 */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ttf_font.h"
#include "book_layout.h"
#define MALLOC_CAP_SPIRAM 1
#define MALLOC_CAP_8BIT 2
static void host_log(const char* tag,const char* format,...){(void)tag;(void)format;}
#define ESP_LOGI(...) host_log(__VA_ARGS__)
#define ESP_LOGW(...) host_log(__VA_ARGS__)
#define ESP_LOGE(...) host_log(__VA_ARGS__)
static void* heap_caps_malloc(size_t n,int c){(void)c;return malloc(n);}
static void* heap_caps_calloc(size_t n,size_t s,int c){(void)c;return calloc(n,s);}
static void heap_caps_free(void* p){free(p);}
static size_t heap_caps_get_largest_free_block(int c){(void)c;return 16*1024*1024;}
static int64_t esp_timer_get_time(void){return 0;}
static const char* app_settings_font_path(void){return "builtin";}
static int screen_width=684,screen_height=1216;
int epd_rotated_display_width(void){return screen_width;}
int epd_rotated_display_height(void){return screen_height;}
void epd_draw_pixel(int x,int y,uint8_t gray,uint8_t* fb){
    if(fb&&x>=0&&x<screen_width&&y>=0&&y<screen_height)fb[(size_t)y*screen_width+x]=(uint8_t)((gray>>4)*17);
}
void epd_fill_rect(EpdRect r,uint8_t gray,uint8_t* fb){
    for(int y=r.y;y<r.y+r.height;y++)for(int x=r.x;x<r.x+r.width;x++)epd_draw_pixel(x,y,gray,fb);
}
static int circle_x,circle_y,circle_radius;
void epd_fill_circle(int cx,int cy,int radius,uint8_t gray,uint8_t* fb){
    if(radius==12){circle_x=cx;circle_y=cy;circle_radius=radius;}
    for(int y=-radius;y<=radius;y++)for(int x=-radius;x<=radius;x++)if(x*x+y*y<=radius*radius)epd_draw_pixel(cx+x,cy+y,gray,fb);
}
static void epd_draw_vline(int x,int y,int height,uint8_t gray,uint8_t* fb){
    for(int row=0;row<height;row++)epd_draw_pixel(x,y+row,gray,fb);
}
#include "ttf_font-under-test.c"
static bool s_product_session=true,s_jump_back_valid;
static bool test_dual_enabled;
static uint32_t s_dual_displayed;
static bool ps_dual_enabled(void){return test_dual_enabled;}
static int ps_dual_role(void){return 1;}
enum {READING};static int s_view=READING;static const char* s_text="fixture";
#include "book-interaction-geometry.inc"
#include "book-text-primitives.inc"

// 只记录生产文字原语的真实度量，不代写布局或控制逻辑。
// Record real metrics around production text primitives without replacing layout or controller logic.
typedef struct {EpdRect ink;int px,x,y;bool centered;char text[192];} text_record_t;
static text_record_t text_records[64];
static size_t text_record_count;
static bool record_text_enabled;
static void record_text(int x,int y,int px,const char* text,enum EpdFontFlags align,bool centered){
    if(!record_text_enabled)return;
    assert(text_record_count<sizeof(text_records)/sizeof(text_records[0]));
    int above=0,below=0;ttf_measure_line_px(px,text,&above,&below);
    int width=ttf_text_width_px(px,text);
    int left=x-(align==EPD_DRAW_ALIGN_CENTER?width/2:align==EPD_DRAW_ALIGN_RIGHT?width:0);
    int baseline=centered?y+(above-below)/2:y+ttf_ascender_px(px);
    text_record_t* item=&text_records[text_record_count++];
    *item=(text_record_t){.ink={left,baseline-above,width,above+below},.px=px,.x=x,.y=y,.centered=centered};
    assert(strlen(text)<sizeof(item->text));memcpy(item->text,text,strlen(text)+1);
}
static void ui_text(uint8_t* fb,int x,int y,int px,const char* text,enum EpdFontFlags align,bool inverted){
    record_text(x,y,px,text,align,false);production_ui_text(fb,x,y,px,text,align,inverted);
}
static void ui_text_vc(uint8_t* fb,int x,int y,int px,const char* text,enum EpdFontFlags align,bool inverted){
    record_text(x,y,px,text,align,true);production_ui_text_vc(fb,x,y,px,text,align,inverted);
}
#include "book-ui-primitives.inc"
static int s_pressed_control=-1;
static size_t book_chapter_count(void){return 28;}
// 仅补齐生产反馈绘图函数的宿主依赖；实际推屏和线程锁仍由设备验证。
// Supply only drawing-feedback host dependencies; device presentation and locking remain outside this test.
typedef struct {uint8_t* fb;} app_ctx_t;
typedef enum {APP_REDRAW_AREA=1} app_redraw_t;
enum {MODE_DU=1};
static EpdRect s_area;
static int s_mode;
static bool s_reader_split;
static void lock_draw(void){}
static void unlock_draw(void){}
#include "book-page-modal.inc"
static char s_title[128]="沿河的书店：一段安静而漫长的旅程";
static bool s_scrubbing;
static uint32_t s_scrub_target;
static size_t s_scrub_chapter;
static size_t s_chapter=4;
static uint32_t book_total_bytes(void){return 100000;}
static unsigned percent(size_t page){return (unsigned)(page*7)%100;}
static bool page_index_matches(void){return s_page_index.counts!=NULL;}
static void ui_clear_rect_fast(uint8_t* fb,EpdRect r){epd_fill_rect(r,UI_GRAY_WHITE,fb);}
static void ui_hairline(uint8_t* fb,int y,int x,int width,int gray){epd_fill_rect((EpdRect){x,y,width,1},gray,fb);}
#include "book-reading-header.inc"

typedef struct {size_t offset;EpdRect rect;} visible_glyph_t;
static unsigned checks;
#define CHECK(value) do{checks++;if(!(value)){fprintf(stderr,"book interaction failed line %d: %s\n",__LINE__,#value);abort();}}while(0)
static size_t next_utf8(const char* text,size_t at){
    unsigned char c=(unsigned char)text[at];return at+(c<0x80?1:c<0xe0?2:c<0xf0?3:4);
}
static bool boundary(const char* text,size_t length,size_t at){return at<=length&&(at==length||((unsigned char)text[at]&0xc0)!=0x80);}
static void save_pgm(const char* directory,const char* name,const uint8_t* fb){
    char path[1024];CHECK(snprintf(path,sizeof(path),"%s/%s.pgm",directory,name)<(int)sizeof(path));
    FILE* file=fopen(path,"wb");CHECK(file);CHECK(fprintf(file,"P5\n%d %d\n255\n",screen_width,screen_height)>0);
    size_t bytes=(size_t)screen_width*screen_height;CHECK(fwrite(fb,1,bytes,file)==bytes);CHECK(!fclose(file));
}
static void test_page(const char* text,size_t length,size_t page,int px,int weight,const char* directory,int orientation){
    EpdRect body=body_rect();size_t first=book_layout_page_start_offset(page),limit=book_layout_page_start_offset(page+1);
    visible_glyph_t glyphs[2048];size_t count=0;
    for(size_t at=first;at<limit;at=next_utf8(text,at)){
        if(text[at]=='\n'||text[at]=='\r'){EpdRect blank;CHECK(!book_layout_text_bounds(page,body,at,&blank));continue;}
        EpdRect rect;if(!book_layout_text_bounds(page,body,at,&rect))continue;
        CHECK(count<2048&&boundary(text,length,at));
        CHECK(rect.x>=body.x&&rect.y>=body.y&&rect.x+rect.width<=body.x+body.width&&rect.y+rect.height<=body.y+body.height);
        char glyph[5]={0};size_t end=next_utf8(text,at);memcpy(glyph,text+at,end-at);
        CHECK(rect.width==ttf_text_width_px(px,glyph)&&rect.height==px);
        if(rect.width>0){
            int x=rect.x+rect.width/2,y=rect.y+rect.height/2;
            CHECK(book_layout_text_at(page,body,x,y)==at);
            CHECK(book_layout_text_nearest(page,body,x,y)==at);
            glyphs[count++]=(visible_glyph_t){at,rect};
        }
        for(size_t tail=at+1;tail<end;tail++)CHECK(!book_layout_text_bounds(page,body,tail,&rect));
    }
    CHECK(count>8);
    EpdRect invalid;CHECK(!book_layout_text_bounds(page,body,length,&invalid));
    if(page)CHECK(!book_layout_text_bounds(page,body,0,&invalid));
    if(limit<length)CHECK(!book_layout_text_bounds(page,body,limit,&invalid));
    CHECK(book_layout_text_at(page,body,body.x-1,body.y)==SIZE_MAX);
    // 行内两端与正文外侧的拖动都吸附本行可见文字，不落入UTF-8续字节。
    // Dragging across each row and outside the body snaps to visible glyphs, never UTF-8 continuations.
    for(size_t line=0;line<count;){
        size_t last=line;while(last+1<count&&glyphs[last+1].rect.y==glyphs[line].rect.y)last++;
        int y=glyphs[line].rect.y+px/2;
        CHECK(book_layout_text_nearest(page,body,-200,y)==glyphs[line].offset);
        CHECK(book_layout_text_nearest(page,body,screen_width+200,y)==glyphs[last].offset);
        line=last+1;
    }
    // 固定中间字，沿两条相反方向的跨行路径拖动；公共API必须始终返回当前页的完整字锚点。
    // Fix a middle glyph and drag both ways across lines; public APIs must retain whole on-page character anchors.
    size_t fixed=count/2;
    for(int reverse=0;reverse<2;reverse++)for(int step=0;step<=40;step++){
        size_t index=reverse?(count-1)*(40-step)/40:(count-1)*step/40;
        EpdRect at=glyphs[index].rect;size_t hit=book_layout_text_nearest(page,body,at.x+at.width/2,at.y+at.height/2);
        CHECK(hit==glyphs[index].offset&&boundary(text,length,hit));
        size_t a=hit<glyphs[fixed].offset?hit:glyphs[fixed].offset;
        size_t b=next_utf8(text,hit>glyphs[fixed].offset?hit:glyphs[fixed].offset);
        CHECK(a<b&&boundary(text,length,a)&&boundary(text,length,b));
        CHECK(book_layout_text_bounds(page,body,a,&invalid));
    }
    // 连续斜拖经过行尾与段间空白，往返路径都必须吸附当前页、且回到字形中心后命中同一字。
    // Dense diagonal drags cross line ends/paragraph gaps; both directions stay on-page and round-trip to the same glyph center.
    for(int reverse=0;reverse<2;reverse++)for(int step=0;step<=80;step++){
        int part=reverse?80-step:step;
        int x=body.x-80+(body.width+160)*part/80;
        int y=glyphs[0].rect.y+(glyphs[count-1].rect.y-glyphs[0].rect.y)*part/80+px/2;
        size_t hit=book_layout_text_nearest(page,body,x,y);EpdRect rect;
        CHECK(hit!=SIZE_MAX&&boundary(text,length,hit)&&book_layout_text_bounds(page,body,hit,&rect));
        CHECK(book_layout_text_at(page,body,rect.x+rect.width/2,rect.y+rect.height/2)==hit);
    }
    uint8_t* fb=malloc((size_t)screen_width*screen_height);CHECK(fb);memset(fb,255,(size_t)screen_width*screen_height);
    book_layout_draw_page(fb,page,body,px);
    size_t low=count/5,high=count*3/4;
    book_layout_draw_range(fb,page,body,glyphs[low].offset,next_utf8(text,glyphs[high].offset),true);
    // 手柄使用实际app_book绘图/命中函数；记录圆心后检查安全区、命中半径与左右端点。
    // Use exact app_book handle drawing/hits; record circle centers to check safe bounds and endpoint alignment.
    for(int end=0;end<2;end++){
        EpdRect glyph=glyphs[end?high:low].rect;circle_radius=0;draw_selection_handle(fb,glyph,end);
        CHECK(circle_radius==12&&circle_x==glyph.x+(end?glyph.width:0));
        CHECK(circle_x-circle_radius>=0&&circle_x+circle_radius<screen_width&&circle_y-circle_radius>=0&&circle_y+circle_radius<reader_footer_top());
        CHECK(handle_hit(glyph,end,circle_x,circle_y)&&handle_hit(glyph,end,circle_x+28,circle_y));
        CHECK(!handle_hit(glyph,end,circle_x+29,circle_y));
    }
    // 首末可见字覆盖最后一行圆心夹紧；离页锚点不能留下旧手柄。
    // First/last visible glyphs cover final-row handle clamping; off-page anchors cannot leave stale handles.
    for(int edge=0;edge<2;edge++)for(int end=0;end<2;end++){
        EpdRect glyph=glyphs[edge?count-1:0].rect;draw_selection_handle(NULL,glyph,end);
        int cy=glyph.y+glyph.height+14;if(cy>reader_footer_top()-16)cy=reader_footer_top()-16;
        CHECK(circle_x==glyph.x+(end?glyph.width:0)&&circle_y==cy);
        CHECK(circle_x-12>=0&&circle_x+12<screen_width&&circle_y+12<reader_footer_top());
        CHECK(handle_hit(glyph,end,circle_x,circle_y));
    }
    unsigned ink=0;for(int y=body.y;y<body.y+body.height;y++)for(int x=body.x;x<body.x+body.width;x++)ink+=fb[(size_t)y*screen_width+x]<255;
    CHECK(ink>500);
    if(page==0){char name[128];snprintf(name,sizeof(name),"layout-selection-%s-px%d-w%d",orientation?"landscape":"portrait",px,weight);save_pgm(directory,name,fb);}
    draw_compact_header(fb,page);
    if(page==0){char name[128];snprintf(name,sizeof(name),"reading-compact-%s-px%d-w%d",orientation?"landscape":"portrait",px,weight);save_pgm(directory,name,fb);}
    free(fb);
}
static bool contains(EpdRect outer,EpdRect inner){
    return inner.x>=outer.x&&inner.y>=outer.y&&inner.x+inner.width<=outer.x+outer.width&&inner.y+inner.height<=outer.y+outer.height;
}
static bool overlaps(EpdRect a,EpdRect b){
    return a.x<b.x+b.width&&b.x<a.x+a.width&&a.y<b.y+b.height&&b.y<a.y+a.height;
}
static void test_page_modal(const char* directory,int orientation,int weight,int scene,bool dual){
    screen_width=orientation?1216:684;screen_height=orientation?684:1216;ttf_set_weight(weight);
    test_dual_enabled=dual;s_pressed_control=-1;
    memset(&s_page_index,0,sizeof(s_page_index));
    uint32_t counted=1;s_page_index.counts=&counted;s_page_index.chapters=28;s_page_index.done=3;
    s_page_index.total=96;s_page_jump_waiting=false;s_page_jump_input[0]=s_page_jump_message[0]=0;
    const char* name="idle";
    if(scene==1){name="input";strcpy(s_page_jump_input,"4294967295");}
    if(scene==2){name="locating";strcpy(s_page_jump_input,"88");s_page_jump_waiting=true;}
    if(scene==3){name="out-of-range";strcpy(s_page_jump_input,"99999");strcpy(s_page_jump_message,"页码超出全书范围，请重新输入");}
    if(scene==4){name="failed";strcpy(s_page_jump_input,"88");strcpy(s_page_jump_message,"跳转失败，原阅读位置已保留，请重试");}
    if(scene==5){name="total-ready";s_page_index.done=28;s_page_index.total=882;strcpy(s_page_jump_input,"882");}
    EpdRect panel=page_jump_rect();CHECK(contains((EpdRect){0,0,screen_width,screen_height-84},panel));
    EpdRect controls[14];
    for(int i=0;i<14;i++){
        controls[i]=i<12?page_jump_key_rect(i):page_jump_button_rect(i-12);
        CHECK(contains(panel,controls[i])&&controls[i].width>=100&&controls[i].height>=56);
        for(int j=0;j<i;j++)CHECK(!overlaps(controls[i],controls[j]));
        EpdRect hit;int action=i==13?14:i;
        CHECK(page_jump_control_at(controls[i].x+controls[i].width/2,controls[i].y+controls[i].height/2,&hit)==
              (s_page_jump_waiting&&action==14?-1:BOOK_PAGE_CONTROL+action));
    }
    uint8_t* fb=malloc((size_t)screen_width*screen_height);CHECK(fb);memset(fb,255,(size_t)screen_width*screen_height);
    text_record_count=0;record_text_enabled=true;page_jump_draw(fb);record_text_enabled=false;
    CHECK(text_record_count==(size_t)(orientation?20:18));
    int left=panel.x+24,width=orientation?panel.width/2-48:panel.width-48;
    EpdRect input={left,panel.y+(orientation?140:124),width,64};
    EpdRect status=page_jump_status_rect();
    CHECK(contains(panel,status)&&contains(status,text_records[1].ink)&&!overlaps(status,input));
    for(int i=0;i<14;i++)CHECK(!overlaps(status,controls[i]));
    unsigned control_texts=0;
    for(size_t i=0;i<text_record_count;i++){
        text_record_t* text=&text_records[i];CHECK(text->ink.width>0&&text->ink.height>0&&contains(panel,text->ink));
        EpdRect allowed={left,panel.y+16,width,(orientation?400:224)};
        if(text->centered&&text->px==UI_PX_BTN){
            bool found=false;
            for(int c=0;c<14;c++)if(text->x==controls[c].x+controls[c].width/2&&text->y==controls[c].y+controls[c].height/2){
                allowed=controls[c];allowed.x+=8;allowed.width-=16;found=true;control_texts++;break;
            }
            CHECK(found);
        }else if(text->px==UI_PX_BODY&&text->x==input.x+18){allowed=input;allowed.x+=12;allowed.width-=24;}
        CHECK(contains(allowed,text->ink));
        for(size_t j=0;j<i;j++)CHECK(!overlaps(text->ink,text_records[j].ink));
    }
    CHECK(control_texts==14);
    CHECK(!strcmp(text_records[text_record_count-2].text,"取消"));
    CHECK(!strcmp(text_records[text_record_count-1].text,s_page_jump_waiting?"定位中":"跳转"));
    CHECK(!strcmp(text_records[0].text,"跳转到全书页码"));
    CHECK(strstr(text_records[1].text,scene==5?"全书共 882 页":scene==2?"正在定位":"输入页码后定位"));
    CHECK(!strcmp(text_records[2].text,s_page_jump_input[0]?s_page_jump_input:"输入页码"));
    unsigned ink=0;for(int y=panel.y;y<panel.y+panel.height;y++)for(int x=panel.x;x<panel.x+panel.width;x++)ink+=fb[(size_t)y*screen_width+x]<255;
    CHECK(ink>1500);
    char filename[128];snprintf(filename,sizeof(filename),"page-jump-%s-w%d-%s%s",orientation?"landscape":"portrait",weight,name,dual?"-dual":"");save_pgm(directory,filename,fb);
    // 提取真实反馈函数选择更新区，统计变化的像素必须都落在该区，不能遗留输入框或按键变化。
    // Use the exact feedback function's area; every changed count pixel must fit, with input/keys unchanged.
    size_t bytes=(size_t)screen_width*screen_height;
    uint8_t* before=malloc(bytes);CHECK(before);memcpy(before,fb,bytes);
    if(s_page_index.done<s_page_index.chapters)s_page_index.done++;else s_page_index.total++;
    app_ctx_t ctx={.fb=fb};s_mode=0;s_reader_split=true;
    CHECK(page_jump_feedback(&ctx,true)==APP_REDRAW_AREA&&s_mode==MODE_DU&&!s_reader_split);
    CHECK(contains(s_area,status)&&contains(status,s_area));
    unsigned changed=0;bool escaped=false;
    for(int y=0;y<screen_height;y++)for(int x=0;x<screen_width;x++)if(fb[(size_t)y*screen_width+x]!=before[(size_t)y*screen_width+x]){
        changed++;if(x<status.x||x>=status.x+status.width||y<status.y||y>=status.y+status.height)escaped=true;
    }
    CHECK(changed>0&&!escaped);
    CHECK(page_jump_feedback(&ctx,false)==APP_REDRAW_AREA&&contains(s_area,panel)&&contains(panel,s_area));
    // 检查黑白主次层级、实际按下绘图的改动范围与松手恢复；不模拟物理波形。
    // Check binary action hierarchy, actual pressed drawing bounds and release restoration, not physical waveforms.
    memcpy(before,fb,bytes);
    for(int i=0;i<14;i++){
        EpdRect r=controls[i];int action=i==13?14:i;
        size_t sample=(size_t)(r.y+10)*screen_width+r.x+20;
        CHECK(fb[sample]==(action==14?0:255));
        if(s_page_jump_waiting&&action==14)continue;
        s_pressed_control=BOOK_PAGE_CONTROL+action;page_jump_draw(fb);
        CHECK(fb[sample]==(action==14?255:0));
        changed=0;escaped=false;
        for(int y=0;y<screen_height;y++)for(int x=0;x<screen_width;x++)if(fb[(size_t)y*screen_width+x]!=before[(size_t)y*screen_width+x]){
            changed++;if(x<r.x||x>=r.x+r.width||y<r.y||y>=r.y+r.height)escaped=true;
        }
        CHECK(changed>0&&!escaped);
        s_pressed_control=-1;page_jump_draw(fb);CHECK(!memcmp(fb,before,bytes));
    }
    free(before);
    free(fb);
    test_dual_enabled=false;
}
static void test_compact_header(const char* directory,int orientation,bool returned,int weight,int scene){
    screen_width=orientation?1216:684;screen_height=orientation?684:1216;
    ttf_set_weight(weight);book_layout_free();
    s_jump_back_valid=returned;s_scrubbing=scene==4;s_scrub_target=73000;s_scrub_chapter=17;s_chapter=4;
    memset(&s_page_index,0,sizeof(s_page_index));
    uint32_t counts[28]={1111,2222,3333,4444};
    size_t page=3;const char* expected="本章 4/… 页";
    copy_text(s_title,sizeof(s_title),"第五节 河岸上的图书馆：读者与藏书共同留下的记忆以及日常生活中安静的细节");
    if(scene==1){
        CHECK(book_layout_build("A chapter with one page.",24,body_rect(),48));
        page=0;expected="本章 1/1 页";
    }else if(scene==2||scene==3){
        page=UINT32_C(1234567890)-1;s_page_index.counts=counts;s_page_index.chapters=28;
        s_page_index.done=scene==2?28:7;s_page_index.total=UINT32_MAX;
        expected=scene==2?"全书 1234579000/4294967295 页":"全书 1234579000/… 页";
    }else if(scene==4)expected="松手跳转";
    else if(scene==5){
        copy_text(s_title,sizeof(s_title),"Chapter Twenty Eight: A Very Long English Chapter Title About Reading And The Quiet Life Beside The River");
        page=UINT32_MAX-1;expected="本章 4294967295/… 页";
    }else if(scene==6){s_title[0]=0;expected="本章 4/… 页";}
    uint8_t* fb=malloc((size_t)screen_width*screen_height);CHECK(fb);memset(fb,255,(size_t)screen_width*screen_height);
    text_record_count=0;record_text_enabled=true;draw_compact_header(fb,page);record_text_enabled=false;
    EpdRect header={0,0,screen_width,84};
    CHECK(body_rect().y==96&&body_rect().y+body_rect().height==screen_height-24);
    EpdRect progress=progress_rect();CHECK(progress.x==84&&progress.y==0&&progress.height==84&&progress.width>=500);
    CHECK(text_record_count==(size_t)(returned?4:3));
    CHECK(!strcmp(text_records[1].text,expected));
    char expected_percent[16];snprintf(expected_percent,sizeof(expected_percent),"%u%%",s_scrubbing?73:percent(page));
    CHECK(!strcmp(text_records[2].text,expected_percent));
    CHECK(text_records[0].px==28&&text_records[0].y==20);
    CHECK(text_records[1].y==51&&text_records[2].y==51&&text_records[2].px==26);
    CHECK(text_records[1].ink.x+text_records[1].ink.width+18<=text_records[2].ink.x);
    if(scene==4)CHECK(!strcmp(text_records[0].text,"第 18/28 节"));
    else if(scene==6)CHECK(!strcmp(text_records[0].text,"第 5 节"));
    else{
        size_t length=strlen(text_records[0].text);CHECK(length>3);
        CHECK(!strncmp(text_records[0].text,s_title,length-3));
        if(strcmp(text_records[0].text,s_title))CHECK(!strcmp(text_records[0].text+length-3,"…"));
    }
    for(size_t i=0;i<text_record_count;i++){
        CHECK(text_records[i].ink.width>0&&text_records[i].ink.height>0&&contains(header,text_records[i].ink));
        if(i<3)CHECK(contains(progress,text_records[i].ink)&&text_records[i].ink.y+text_records[i].ink.height<74);
        for(size_t j=0;j<i;j++)CHECK(!overlaps(text_records[i].ink,text_records[j].ink));
    }
    if(returned){CHECK(!overlaps(progress,progress_back_rect()));CHECK(!strcmp(text_records[3].text,"原处"));}
    char name[96];snprintf(name,sizeof(name),"header-%s-return%d-w%d-scene%d",orientation?"landscape":"portrait",returned,weight,scene);save_pgm(directory,name,fb);
    free(fb);book_layout_free();s_page_index.counts=NULL;s_jump_back_valid=s_scrubbing=false;
}
int main(int argc,char** argv){
    assert(argc==3);CHECK(ttf_font_open(argv[1])==ESP_OK&&font_ready&&gvar_ready&&font_fd>=0&&!font_mem);
    const char* paragraph="清晨，读者推开窗，看见河面泛起细碎的光。手指停在一个字上，再慢慢向下一行移动，文字之间的距离依然清晰。\n"
        "WiFi 和 Home Assistant 留在日常生活里；iiii 与 WWWW 的宽度不同，标点、数字 2026、é 和中文都应落在完整字符的边界上。\n"
        "阅读时可以向左拖，也可以向右拖；选择起点和终点跨过彼此，选区仍然连续。短暂放下书，下一次依旧从记住的位置开始。\n\n";
    size_t pbytes=strlen(paragraph),length=pbytes*12;char* text=malloc(length+1);CHECK(text);
    for(int i=0;i<12;i++)memcpy(text+i*pbytes,paragraph,pbytes);text[length]=0;
    int sizes[]={43,55,67},weights[]={330,600};unsigned pages=0;
    for(int orientation=0;orientation<2;orientation++)for(int size=0;size<3;size++)for(int w=0;w<2;w++){
        screen_width=orientation?1216:684;screen_height=orientation?684:1216;int px=sizes[size];ttf_set_weight(weights[w]);
        CHECK(ttf_text_width_px(px,"WWWW")>ttf_text_width_px(px,"iiii")*2);
        CHECK(book_layout_build(text,length,body_rect(),px)&&book_layout_complete());
        size_t count=book_layout_page_count();CHECK(count>1);
        for(size_t page=0;page<count&&page<3;page++){test_page(text,length,page,px,weights[w],argv[2],orientation);pages++;}
        book_layout_free();
    }
    printf("real MiSans production font/layout: %u checks, %u pages, 12 orientation/size/weight scenes passed\n",checks,pages);
    unsigned before_modal=checks,modal_scenes=0;
    for(int orientation=0;orientation<2;orientation++)for(int w=0;w<2;w++)for(int scene=0;scene<6;scene++)for(int dual=0;dual<2;dual++){
        test_page_modal(argv[2],orientation,weights[w],scene,dual);modal_scenes++;
    }
    printf("page-jump modal: %u checks, %u real-font scenes passed\n",checks-before_modal,modal_scenes);
    unsigned before_header=checks,header_scenes=0;
    for(int orientation=0;orientation<2;orientation++)for(int back=0;back<2;back++)for(int w=0;w<2;w++)for(int scene=0;scene<7;scene++){
        test_compact_header(argv[2],orientation,back,weights[w],scene);header_scenes++;
    }
    printf("compact reading header: %u checks, %u real-font scenes passed\n",checks-before_header,header_scenes);
    free(text);ttf_font_unload();heap_caps_free(work);heap_caps_free(io_data);heap_caps_free(io_run_buf);
    return 0;
}

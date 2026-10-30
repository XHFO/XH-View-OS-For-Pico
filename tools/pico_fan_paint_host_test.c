/* SPDX-License-Identifier: Apache-2.0
 * 真实绘图与4bpp推屏范围回归；字体使用确定性图案，所有设备出口由内存面板替代。
 * Actual drawing and 4bpp update-region regression; deterministic glyphs and a memory panel replace devices.
 */
#include "pico_system.h"
#include "pico_navigation.h"
#include "ttf_font.h"
#include "ui_kit.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { WIDTH=1216, HEIGHT=684, BYTES=WIDTH*HEIGHT/2, GUARD=32 };
typedef struct { int x_mg,y_mg,z_mg; } sc7a20h_sample_t;
static unsigned checks,failures,full_updates,area_updates,fallback_updates,font_draws;
static unsigned allocations,frees,allocation_failures,display_failures;
static unsigned frame_full,frame_area,frame_fallback;
static unsigned max_local_pixels;
static unsigned long changed_pixels,covered_pixels;
static uint8_t physical[BYTES],before[BYTES],expected[BYTES],coverage[WIDTH*HEIGHT];
static struct { uint8_t head[GUARD],fb[BYTES],tail[GUARD]; } packed;
static enum EpdRotation rotation;
static EpdiyHighlevelState primary,secondary;
static app_ctx_t context={.fb=packed.fb,.hl=&primary};
static const app_desc_t app_book={0},child={0};
// 此检查只测试扇形绘制，固定导航避免接入无关存储控制器。/ This fan-painter harness keeps unrelated navigation/storage fixed.
static const ps_navigation_entry_t navigation_fixture[]={
    {PS_NAV_BOOKS,PS_SHELF,PI_BOOK,"书架",NULL},
    {PS_NAV_HOME,PS_HA,PI_HOME,"家庭",NULL},
    {PS_NAV_GAMES,PS_GAMES,PI_GAME,"小游戏",NULL}
};
int ps_navigation_count(void){return 3;}
const ps_navigation_entry_t *ps_navigation_item(int index){return index>=0&&index<3?&navigation_fixture[index]:NULL;}
int ps_navigation_index(ps_route_t route){for(int i=0;i<3;i++)if(navigation_fixture[i].route==route)return i;return -1;}
static const EpdWaveform E0470_WAVEFORM={0};
#define CHECK(e,m) do { checks++; if(!(e)&&failures++<24) fprintf(stderr,"FAIL %s:%d %s\n",__FILE__,__LINE__,m); } while(0)

int epd_width(void){return WIDTH;}
int epd_height(void){return HEIGHT;}
enum EpdRotation epd_get_rotation(void){return rotation;}
int epd_rotated_display_width(void){return rotation==EPD_ROT_PORTRAIT||rotation==EPD_ROT_INVERTED_PORTRAIT?HEIGHT:WIDTH;}
int epd_rotated_display_height(void){return rotation==EPD_ROT_PORTRAIT||rotation==EPD_ROT_INVERTED_PORTRAIT?WIDTH:HEIGHT;}
static void *paint_allocate(size_t bytes,unsigned capabilities){
    CHECK(bytes==BYTES&&capabilities==3,"one exact 4bpp PSRAM allocation");allocations++;
    if(allocation_failures){allocation_failures--;return NULL;}
    return malloc(bytes);
}
static void paint_free(void *memory){if(memory)frees++;free(memory);}
static bool app_book_global_is_open(void){return true;}
static bool app_book_global_has_selection(void){return false;}
static bool local_hold(void){return false;}
bool ps_reader_is_open(void){return true;}
bool ps_games_is_arcade(void){return false;}
bool ps_games_is_snake(void){return false;}
static enum EpdDrawError update_display_with(EpdiyHighlevelState*,const EpdWaveform*,enum EpdDrawMode);
static enum EpdDrawError update_display_area_with(EpdiyHighlevelState*,const EpdWaveform*,enum EpdDrawMode,EpdRect);
static enum EpdDrawError update_display_full(EpdiyHighlevelState*);
#define heap_caps_malloc paint_allocate
#define MALLOC_CAP_SPIRAM 1u
#define MALLOC_CAP_8BIT 2u
#define free paint_free
#define ESP_LOGI(...) ((void)0)
#include "native-paint-functions.inc"
#undef free

// 屏幕模型按像素复制局部范围，保留同字节另一半，避免整字节复制掩盖范围错误。
// The panel model copies individual pixels, preserving the sibling nibble rather than hiding area mistakes.
static unsigned nibble(const uint8_t *fb,int x,int y){return (fb[(size_t)y*WIDTH/2+x/2]>>((x&1)*4))&15u;}
static void store_nibble(uint8_t *fb,int x,int y,unsigned value){size_t at=(size_t)y*WIDTH/2+x/2;unsigned shift=(x&1)*4;fb[at]=(fb[at]&~(15u<<shift))|(value<<shift);}
static void physical_point(int x,int y,int *px,int *py){
    switch(rotation){
    case EPD_ROT_LANDSCAPE:*px=x;*py=y;break;
    case EPD_ROT_PORTRAIT:*px=WIDTH-y-1;*py=x;break;
    case EPD_ROT_INVERTED_LANDSCAPE:*px=WIDTH-x-1;*py=HEIGHT-y-1;break;
    default:*px=y;*py=HEIGHT-x-1;break;
    }
}
static bool fail_display(void){if(!display_failures)return false;display_failures--;return true;}
static enum EpdDrawError update_display_with(EpdiyHighlevelState *hl,const EpdWaveform *wave,enum EpdDrawMode mode){
    CHECK(hl==context.hl&&wave==&E0470_WAVEFORM&&mode==MODE_GL16,"full fan preserves 16 gray levels with exact GL16 waveform");
    full_updates++;frame_full++;
    if(fail_display())return EPD_DRAW_EMPTY_LINE_QUEUE;
    memset(coverage,1,sizeof(coverage));memcpy(physical,context.fb,BYTES);return EPD_DRAW_SUCCESS;
}
static enum EpdDrawError update_display_area_with(EpdiyHighlevelState *hl,const EpdWaveform *wave,enum EpdDrawMode mode,EpdRect area){
    CHECK(hl==context.hl&&wave==&E0470_WAVEFORM&&mode==MODE_GL16,"local fan preserves 16 gray levels with exact GL16 waveform");
    CHECK(area.x>=0&&area.y>=0&&area.width>0&&area.height>0&&area.x+area.width<=epd_rotated_display_width()&&area.y+area.height<=epd_rotated_display_height(),"logical update bounded in current rotation");
    area_updates++;frame_area++;
    if(fail_display())return EPD_DRAW_EMPTY_LINE_QUEUE;
    for(int y=area.y;y<area.y+area.height;y++)for(int x=area.x;x<area.x+area.width;x++){
        int px,py;physical_point(x,y,&px,&py);
        CHECK(px>=0&&px<WIDTH&&py>=0&&py<HEIGHT,"independent rotated update mapping bounded");
        if(px<0||px>=WIDTH||py<0||py>=HEIGHT)continue;
        coverage[(size_t)py*WIDTH+px]=1;store_nibble(physical,px,py,nibble(context.fb,px,py));
    }
    return EPD_DRAW_SUCCESS;
}
static enum EpdDrawError update_display_full(EpdiyHighlevelState *hl){
    CHECK(hl==context.hl,"fallback retains highlevel owner");fallback_updates++;frame_fallback++;
    if(fail_display())return EPD_DRAW_EMPTY_LINE_QUEUE;
    memset(coverage,1,sizeof(coverage));memcpy(physical,context.fb,BYTES);return EPD_DRAW_SUCCESS;
}

// 字形图案随Unicode内容变化，能够发现旧提示残留；不声称验证字体外观。
// Glyph patterns vary with Unicode content to detect stale prompts, without claiming typography acceptance.
static uint32_t decode(const char **text){
    const unsigned char *p=(const unsigned char*)*text;uint32_t cp;int n;
    if(*p<0x80){cp=*p;n=1;}else if(*p<0xe0){cp=*p&31;n=2;}else if(*p<0xf0){cp=*p&15;n=3;}else{cp=*p&7;n=4;}
    for(int i=1;i<n;i++)cp=(cp<<6)|(p[i]&63);*text+=n;return cp;
}
static int text_advance(int height){return height*3/5;}
int ttf_get_weight(void){return 600;}
void ttf_set_weight(int weight){(void)weight;}
int ttf_ascender_px(int height){return height*4/5;}
int ttf_text_width_px(int height,const char *text){int width=0;while(*text){decode(&text);width+=text_advance(height);}return width;}
void ttf_draw_text_px(uint8_t *fb,int x,int y,int height,const char *text,enum EpdFontFlags align,uint8_t fg,uint8_t bg){
    (void)bg;font_draws++;
    if(align&EPD_DRAW_ALIGN_CENTER)x-=ttf_text_width_px(height,text)/2;
    else if(align&EPD_DRAW_ALIGN_RIGHT)x-=ttf_text_width_px(height,text);
    y-=ttf_ascender_px(height);
    while(*text){uint32_t cp=decode(&text);int advance=text_advance(height);
        for(int row=1;row<height-1;row++)for(int col=1;col<advance-1;col++)
            if(((cp>>(col*5/advance))^(unsigned)(row*7/height))&1u)epd_draw_pixel(x+col,y+row,fg*17,fb);
        x+=advance;
    }
}

static void observe_begin(void){frame_full=frame_area=frame_fallback=0;memset(coverage,0,sizeof(coverage));memcpy(before,physical,BYTES);}
static void reference_frame(void){
    // 从零合成完整参考帧；灰阶混合必须遵循实际基底、边角提示、选中光圈的叠放顺序。
    // Independently composite a complete frame with the production base/corners/cursor layer order.
    memset(expected,0x87,sizeof(expected));render_fan_base(expected);ps_corner_controls(expected);render_fan_cursor(expected);
}
static void check_frame(bool success){
    for(int i=0;i<GUARD;i++)CHECK(packed.head[i]==0xa5&&packed.tail[i]==0x5a,"packed framebuffer allocation guards");
    reference_frame();CHECK(!memcmp(expected,packed.fb,BYTES),"partial result equals independently rendered complete reference");
    if(s_fan_paint.base){
        fan_item_t buttons[QUICK_COUNT];int count=fan_layout(buttons);
        for(int button=0;button<count;button++){
            if(buttons[button].id<0&&!fan_editor_active())continue;
            EpdRect disk=ps_rect(buttons[button].x,buttons[button].y,buttons[button].r,buttons[button].r);
            int radius=disk.width-4;
            for(int dy=-radius;dy<=radius;dy++)for(int dx=-radius;dx<=radius;dx++)if(dx*dx+dy*dy<=radius*radius){
                int x,y;physical_point(disk.x+dx,disk.y+dy,&x,&y);
                CHECK(nibble(packed.fb,x,y)==nibble(s_fan_paint.base,x,y),"moving outline never changes stable button pixels");
            }
        }
    }
    if(!success)return;
    CHECK(!memcmp(physical,packed.fb,BYTES),"no front/back divergence outside submitted regions");
    for(int y=0;y<HEIGHT;y++)for(int x=0;x<WIDTH;x++){
        bool changed=nibble(before,x,y)!=nibble(packed.fb,x,y);size_t at=(size_t)y*WIDTH+x;
        if(changed){changed_pixels++;CHECK(coverage[at],"every changed pixel lies in submitted union");}
        if(coverage[at])covered_pixels++;
    }
}
static void setup(int r,int side,bool global){
    fan_present_reset();rotation=(enum EpdRotation)r;
    memset(packed.head,0xa5,GUARD);memset(packed.tail,0x5a,GUARD);
    memset(packed.fb,0x87,BYTES);memset(physical,0x87,BYTES);
    s_config.landscape=r==EPD_ROT_LANDSCAPE||r==EPD_ROT_INVERTED_LANDSCAPE;
    s_global_app=global?&child:NULL;if(global)s_config.landscape=!s_config.landscape;
    memset(&s_quick_edit,0,sizeof(s_quick_edit));s_quick_edit.selected=s_quick_edit.source=s_quick_edit.target=-1;
    s_fan_side=side;s_fan_selected=-1;s_fan=true;s_route=PS_SHELF;
    s_fast=true;quick_defaults();context.hl=&primary;
}
static void check_all_rotations(void){
    for(int global=0;global<2;global++)for(int r=0;r<4;r++)for(int side=0;side<2;side++){
        setup(r,side,global);observe_begin();CHECK(fan_present(&context),"first menu handled");
        CHECK(frame_full==1&&!frame_area&&!frame_fallback,"first menu is one full GL16 update");
        CHECK(s_fan_paint.base&&s_fan_paint.valid&&!s_fast,"first update saves usable static base");check_frame(true);
        fan_item_t icons[QUICK_COUNT];int count=fan_layout(icons);unsigned draws=font_draws;
        for(int same=0;same<5;same++){
            observe_begin();CHECK(fan_present(&context),"idle menu handled");
            CHECK(!frame_full&&!frame_area&&!frame_fallback&&font_draws==draws,"unchanged choice draws and presents nothing");
        }
        int order[QUICK_COUNT+2],n=0;
        for(int i=0;i<count;i+=2)order[n++]=i;
        for(int i=1;i<count;i+=2)order[n++]=i;
        order[n++]=-1;
        for(int i=0;i<n;i++){
            s_fan_selected=order[i];observe_begin();fan_present(&context);
            CHECK(!frame_full&&frame_area==1&&!frame_fallback,"each changed selection submits one GL16 union");
            unsigned area_count=0;for(size_t p=0;p<sizeof(coverage);p++)area_count+=coverage[p]!=0;
            if(area_count>max_local_pixels)max_local_pixels=area_count;
            CHECK(area_count<WIDTH*HEIGHT,"local cursor and prompt update remains strictly partial");
            check_frame(true);draws=font_draws;observe_begin();fan_present(&context);
            CHECK(!frame_area&&!frame_full&&!frame_fallback&&draws==font_draws,"same selected icon performs zero paint and display work");
        }
        unsigned freed=frees;fan_present_reset();CHECK(frees==freed+1&&!s_fan_paint.base&&!s_fan_paint.valid,"closing releases static buffer and validity");
        freed=frees;fan_present_reset();CHECK(frees==freed,"second reset is safe and idempotent");
    }
}
static void check_nibble_regions(void){
    static const EpdRect regions[]={{1,1,1,1},{8,5,7,3},{17,21,9,11},{42,33,3,17}};
    for(int r=0;r<4;r++){
        setup(r,0,true);
        for(unsigned fixture=0;fixture<sizeof(regions)/sizeof(regions[0]);fixture++){
            EpdRect logical=ps_rect(regions[fixture].x,regions[fixture].y,regions[fixture].width,regions[fixture].height);
            for(size_t i=0;i<BYTES;i++){before[i]=(uint8_t)(i*13+fixture);expected[i]=(uint8_t)(i*7+fixture*5);}
            memcpy(packed.fb,before,BYTES);memset(coverage,0,sizeof(coverage));
            for(int y=logical.y;y<logical.y+logical.height;y++)for(int x=logical.x;x<logical.x+logical.width;x++){
                int px,py;physical_point(x,y,&px,&py);coverage[(size_t)py*WIDTH+px]=1;
            }
            fan_area_pixels(packed.fb,expected,regions[fixture],false);
            for(int y=0;y<HEIGHT;y++)for(int x=0;x<WIDTH;x++)
                CHECK(nibble(packed.fb,x,y)==nibble(coverage[(size_t)y*WIDTH+x]?expected:before,x,y),"restoring local area preserves all sibling and outside nibbles");
            fan_area_pixels(packed.fb,NULL,regions[fixture],true);
            for(int y=0;y<HEIGHT;y++)for(int x=0;x<WIDTH;x++){
                bool inside=coverage[(size_t)y*WIDTH+x];unsigned wanted=nibble(inside?expected:before,x,y);
                if(inside)wanted=wanted>=8?15:0;
                CHECK(nibble(packed.fb,x,y)==wanted,"threshold touches only exact rotated requested pixels");
            }
        }
    }
}
static void check_invalidation(void){
    setup(EPD_ROT_INVERTED_PORTRAIT,0,true);observe_begin();fan_present(&context);check_frame(true);
    s_fan_selected=3;observe_begin();fan_present(&context);CHECK(frame_area,"selection primes local cursor");check_frame(true);
    s_fan_side=1;observe_begin();fan_present(&context);CHECK(frame_full==1&&!frame_area,"mirroring invalidates base");check_frame(true);
    uint8_t first=s_quick_slots[0];s_quick_slots[0]=s_quick_slots[13];s_quick_slots[13]=first;
    observe_begin();fan_present(&context);CHECK(frame_full==1&&!frame_area,"confirmed action layout invalidates base");check_frame(true);
    s_route=PS_READ;observe_begin();fan_present(&context);CHECK(frame_full==1&&!frame_area,"route-dependent action availability invalidates base");check_frame(true);
    context.hl=&secondary;observe_begin();fan_present(&context);CHECK(frame_full==1&&!frame_area,"highlevel owner change invalidates base");check_frame(true);
    rotation=EPD_ROT_PORTRAIT;observe_begin();fan_present(&context);CHECK(frame_full==1&&!frame_area,"180 degree canvas change invalidates base");check_frame(true);
    rotation=EPD_ROT_LANDSCAPE;observe_begin();fan_present(&context);CHECK(frame_full==1&&!frame_area,"portrait to landscape invalidates base");check_frame(true);
    fan_present_reset();
}
static void check_editor(void){
    for(int r=0;r<4;r++)for(int side=0;side<2;side++){
        setup(r,side,true);s_fan=false;s_route=PS_QUICK_SETTINGS;quick_edit_begin();s_quick_edit.side=side;
        CHECK(fan_editor_active()&&fan_frame_active(),"in-place draft uses shared fan painter");
        uint8_t saved[FAN_SLOTS];memcpy(saved,s_quick_slots,FAN_SLOTS);
        observe_begin();fan_present(&context);CHECK(frame_full==1&&!frame_area,"editor opens with one full GL16");check_frame(true);
        s_quick_edit.selected=s_quick_edit.source=1;s_quick_edit.dragging=true;
        for(int target=0;target<FAN_SLOTS;target++){
            s_quick_edit.target=target;observe_begin();fan_present(&context);
            CHECK(frame_area==1&&!frame_full&&!frame_fallback,"editor target motion submits one GL16 union from static base");
            CHECK(!memcmp(saved,s_quick_slots,FAN_SLOTS)&&!memcmp(saved,s_quick_edit.slots,FAN_SLOTS),"drag preview never mutates live or draft assignments");check_frame(true);
            unsigned draws=font_draws;observe_begin();fan_present(&context);
            CHECK(!frame_area&&!frame_full&&!frame_fallback&&font_draws==draws,"stationary draft target performs no redraw");
        }
        s_quick_edit.dragging=false;s_quick_edit.selected=1;
        quick_edit_assign(saved[7]);
        CHECK(s_quick_edit.slots[1]==saved[7]&&s_quick_edit.slots[7]==saved[1],"actual editor assignment swaps existing action");
        CHECK(!memcmp(saved,s_quick_slots,FAN_SLOTS),"swapping draft never persists live layout");
        observe_begin();fan_present(&context);CHECK(frame_full==1&&!frame_area,"draft swap rebuilds base exactly once");check_frame(true);
        unsigned draws=font_draws;observe_begin();fan_present(&context);CHECK(!frame_full&&!frame_area&&font_draws==draws,"swapped draft becomes idle without extra output");
        s_quick_edit.selected=4;quick_edit_assign(-1);
        observe_begin();fan_present(&context);CHECK(frame_full==1&&!frame_area,"removing draft action rebuilds one empty placeholder");check_frame(true);
        fan_item_t items[QUICK_COUNT];fan_layout(items);
        CHECK(items[4].id==-1&&fan_pick(items,FAN_SLOTS,items[4].x,items[4].y,-1)==4,"editor empty position remains selectable");
        s_quick_edit.dragging=true;s_quick_edit.target=4;
        observe_begin();fan_present(&context);CHECK(!frame_full&&!frame_area,"unchanged selected empty position does not update");
        s_quick_edit.target=5;observe_begin();fan_present(&context);CHECK(frame_area&&!frame_full,"moving away from empty editor slot stays local");check_frame(true);
        s_quick_edit.save_failed=true;observe_begin();fan_present(&context);CHECK(frame_full==1&&!frame_area,"save-failure message invalidates editor base");check_frame(true);
        s_quick_edit.side=!side;observe_begin();fan_present(&context);CHECK(frame_full==1&&!frame_area,"editor mirroring invalidates base");check_frame(true);
        s_quick_edit.active=false;s_fan=true;s_fan_selected=-1;
        s_quick_slots[4]=QUICK_EMPTY;observe_begin();fan_present(&context);
        CHECK(frame_full==1&&!frame_area,"leaving editor rebuilds the runtime base");check_frame(true);fan_layout(items);
        CHECK(items[4].id==-1&&fan_pick(items,FAN_SLOTS,items[4].x,items[4].y,-1)==-1,"runtime empty position cannot be selected");
        fan_present_reset();
    }
}
static void check_allocation_failure(void){
    setup(EPD_ROT_INVERTED_PORTRAIT,1,true);allocation_failures=1;unsigned prior=allocations;
    observe_begin();fan_present(&context);CHECK(!s_fan_paint.base&&s_fan_paint.valid,"allocation failure still presents valid complete menu");check_frame(true);
    for(int i=0;i<5;i++){
        s_fan_selected=i;observe_begin();fan_present(&context);
        CHECK(frame_full==1&&!frame_area&&allocations==prior+1,"failed allocation uses safe full GL16 without retry storm");check_frame(true);
        unsigned draws=font_draws;observe_begin();fan_present(&context);
        CHECK(!frame_full&&!frame_area&&draws==font_draws,"same choice still suppresses work after allocation failure");
    }
    fan_present_reset();observe_begin();fan_present(&context);
    CHECK(s_fan_paint.base&&allocations==prior+2,"new menu may retry allocation after reset");check_frame(true);fan_present_reset();
}
static void check_display_failure(void){
    setup(EPD_ROT_LANDSCAPE,0,false);observe_begin();fan_present(&context);check_frame(true);
    s_fan_selected=2;display_failures=1;observe_begin();fan_present(&context);
    CHECK(frame_area==1&&frame_fallback==1&&s_fan_paint.valid,"failed partial output falls back to full and retains valid state");check_frame(true);
    s_fan_selected=5;display_failures=2;observe_begin();fan_present(&context);
    CHECK(frame_area==1&&frame_fallback==1&&!s_fan_paint.valid,"failure of both outputs invalidates cached screen state");check_frame(false);
    observe_begin();fan_present(&context);CHECK(frame_full==1&&!frame_area&&s_fan_paint.valid,"invalid screen state retries complete menu");check_frame(true);fan_present_reset();
}
static unsigned icon_mask_visible(int x,int y,int size,ps_icon_t icon,int gray){
    memset(expected,255,BYTES);ps_icon(expected,x,y,size,icon,gray);
    unsigned pixels=0,grays=0;
    for(int py=0;py<HEIGHT;py++)for(int px=0;px<WIDTH;px++)if(nibble(expected,px,py)!=15){
        unsigned wanted=nibble(expected,px,py);pixels++;grays+=wanted>0&&wanted<15;
        CHECK(nibble(packed.fb,px,py)==wanted,"each original icon stroke and its exact gray level survive the fan base");
    }
    CHECK(pixels>0&&grays>0,"original icon contains visible intermediate gray strokes");return pixels;
}
static void check_gray_icon_visibility(void){
    // 原生矢量图标保留实际灰阶；禁用态和空位独立检查每个笔画像素，避免缺图参考帧掩盖问题。
    // Preserve exact vector gray levels; check every stroke of unavailable actions and empty slots independently of the full-frame reference.
    unsigned unavailable=0,empty=0,all_icons=0;
    for(int r=0;r<4;r++)for(int gray=2;gray<=6;gray+=4)for(int icon=0;icon<PI_COUNT;icon++){
        setup(r,0,false);memset(packed.fb,255,BYTES);ps_icon(packed.fb,100,100,21,(ps_icon_t)icon,gray);
        icon_mask_visible(100,100,21,(ps_icon_t)icon,gray);all_icons++;
    }
    for(int r=0;r<4;r++)for(int side=0;side<2;side++){
        setup(r,side,false);
        for(int id=0;id<QUICK_COUNT;id++){
            s_quick_slots[0]=(uint8_t)id;render_fan_base(packed.fb);
            fan_item_t items[QUICK_COUNT];fan_layout(items);float scale=fan_scale();int size=(int)lroundf(21.f*scale);
            bool disabled=quick_unavailable(id)!=NULL;if(disabled)unavailable++;
            icon_mask_visible(items[0].x-size/2,items[0].y-items[0].r+(int)lroundf(3.f*scale),size,s_quick[id].icon,disabled?6:2);
        }
        s_fan=false;s_route=PS_QUICK_SETTINGS;quick_edit_begin();s_quick_edit.side=side;s_quick_edit.slots[0]=QUICK_EMPTY;
        render_fan_base(packed.fb);fan_item_t items[QUICK_COUNT];fan_layout(items);float scale=fan_scale();int size=(int)lroundf(21.f*scale);
        icon_mask_visible(items[0].x-size/2,items[0].y-items[0].r+(int)lroundf(3.f*scale),size,PI_PLUS,6);empty++;
        fan_present_reset();
    }
    CHECK(all_icons==4u*2u*PI_COUNT&&unavailable>0&&empty==8,"all icons, unavailable no-book actions and empty editor positions exercised");
    printf("16-gray icon visibility: %u vector cases, %u unavailable actions, %u empty positions.\n",all_icons,unavailable,empty);
}

int main(void){
    check_all_rotations();check_invalidation();check_allocation_failure();check_display_failure();check_nibble_regions();check_editor();check_gray_icon_visibility();fan_present_reset();
    CHECK(allocations-frees==1,"all successful cache allocations released; one intentional allocation failure");
    printf("fan paint: %u checks; %u failures; full=%u local=%u fallback=%u; changed=%lu covered=%lu max-local=%u pixels; four rotations, shell/global, allocation and display failures\n",checks,failures,full_updates,area_updates,fallback_updates,changed_pixels,covered_pixels,max_local_pixels);
    return failures?1:0;
}

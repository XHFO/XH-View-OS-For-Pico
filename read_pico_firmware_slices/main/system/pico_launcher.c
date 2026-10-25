/*
 * SPDX-License-Identifier: Apache-2.0
 * 中文：应用集提供系统与TF模块的统一直达网格；首页入口优先沿用用户顺序。
 * English: A single launch grid for system and TF apps, with Home entries in the user's order first.
 * 冻结：不复制应用实现，不在绘制时访问文件，不直接调用模块代码或改变已保存导航。
 * Frozen: Never duplicate app implementations, access files while painting, invoke module code directly or alter saved navigation.
 */
#include "pico_launcher.h"
#include "pico_navigation.h"
#include "pico_theme.h"
#include <stdio.h>
#include <string.h>

static unsigned page;
// 首页已显示入口按用户顺序，其余入口仍可在应用集中发现。/ Use Home order first and keep hidden destinations discoverable in Apps.
static const ps_navigation_entry_t *destination(unsigned index) {
    for(int i=0;i<ps_navigation_count();i++) {
        const ps_navigation_entry_t *item=ps_navigation_item(i);
        if(item&&item->route!=PS_APPS&&index--==0)return item;
    }
    for(int i=0;i<ps_navigation_catalog_count();i++) {
        const ps_navigation_entry_t *item=ps_navigation_catalog_item(i);bool visible=false;
        if(!item||item->route==PS_APPS)continue;
        for(int j=0;j<ps_navigation_count();j++)if(ps_navigation_item(j)->id==item->id)visible=true;
        if(!visible&&index--==0)return item;
    }
    return NULL;
}
static unsigned destinations(void) {
    unsigned n=0;while(destination(n))n++;return n;
}
static unsigned count(void) {return destinations()+ps_games_count()+1;}
static unsigned pages(void) {return (count()+5)/6;}
static unsigned effective_page(void) {unsigned n=pages();return page<n?page:n-1;}
static EpdRect card(unsigned slot) {
    ps_view_t v=ps_view();int cols=v.landscape?3:2,rows=v.landscape?2:3;
    int top=v.landscape?121:148,gap=10,w=(v.w-32-(cols-1)*gap)/cols;
    int h=(v.h-top-77-(rows-1)*gap)/rows;
    return (EpdRect){16+(int)(slot%cols)*(w+gap),top+(int)(slot/cols)*(h+gap),w,h};
}
void ps_launcher_enter(void) {ps_games_rescan();page=effective_page();}
void ps_launcher_render(uint8_t *fb) {
    ps_view_t v=ps_view();unsigned n=destinations(),shown=effective_page();char text[56];
    ps_fill(fb,0,76,v.w,v.h-76,14);
    if(ps_navigation_index(PS_APPS)>=0) {
        ps_tabs(fb,0);ps_text(fb,16,85,25,"应用集",1);
    } else ps_heading(fb,"应用集",NULL);
    ps_button(fb,v.w-80,85,64,29,"刷新",false);
    if(!v.landscape){snprintf(text,sizeof(text),"系统与工具 %u · TF 游戏 %u",n,ps_games_count());ps_text(fb,18,122,11,text,5);}
    for(unsigned slot=0;slot<6&&shown*6+slot<count();slot++) {
        unsigned index=shown*6+slot;EpdRect r=card(slot);
        const ps_navigation_entry_t *item=index<n?destination(index):NULL;
        const char *id=NULL,*title="系统设置",*source="设置";ps_icon_t icon=PI_SETTINGS;
        if(item){title=item->label;icon=item->icon;source=item->route==PS_APPLICATION?"TF 应用":"系统应用";}
        else if(index<n+ps_games_count()) {
            if(!ps_games_item(index-n,&id,&title))continue;
            (void)id;icon=PI_GAME;source="TF 游戏";
        }
        ps_surface(fb,r.x,r.y,r.width,r.height,false);
        if(v.landscape) {
            ps_icon(fb,r.x+12,r.y+15,29,icon,2);
            ps_wrap(fb,r.x+53,r.y+13,r.width-65,14,2,title,1);
            ps_text(fb,r.x+53,r.y+r.height-20,10,source,5);
        } else {
            ps_box(fb,r.x+r.width/2-25,r.y+10,50,50,12,13,true);
            ps_icon(fb,r.x+r.width/2-17,r.y+18,34,icon,2);
            ps_wrap(fb,r.x+12,r.y+68,r.width-24,14,2,title,1);
            ps_text(fb,r.x+12,r.y+r.height-19,10,source,5);
        }
    }
    int y=v.h-64;
    ps_button(fb,16,y,68,29,"上一页",false);
    snprintf(text,sizeof(text),"%u / %u",shown+1,pages());ps_center(fb,v.w/2,y+8,11,text,5);
    ps_button(fb,v.w-84,y,68,29,"下一页",false);
}
static bool turn(int delta) {
    unsigned last=pages();if(last<=1)return false;
    page=(effective_page()+last+delta)%last;return true;
}
bool ps_launcher_key(int key) {return key==0||key==2?turn(key==0?-1:1):false;}
bool ps_launcher_event(const ps_event_t *e) {
    if(e->type==PS_SWIPE_U||e->type==PS_SWIPE_D)return turn(e->type==PS_SWIPE_U?1:-1);
    if(e->type!=PS_TAP)return false;
    ps_view_t v=ps_view();
    if(ps_hit(e->x,e->y,v.w-80,85,64,29)){ps_launcher_enter();return true;}
    int y=v.h-64;
    if(ps_hit(e->x,e->y,16,y,68,29))return turn(-1);
    if(ps_hit(e->x,e->y,v.w-84,y,68,29))return turn(1);
    unsigned n=destinations(),shown=effective_page();
    for(unsigned slot=0;slot<6&&shown*6+slot<count();slot++) {
        EpdRect r=card(slot);if(!ps_hit(e->x,e->y,r.x,r.y,r.width,r.height))continue;
        unsigned index=shown*6+slot;
        if(index<n) {
            const ps_navigation_entry_t *item=destination(index);
            if(item->launch)item->launch(item->id);else ps_navigate(item->route);
        } else if(index<n+ps_games_count()) {
            const char *id,*title;char stable[32];
            if(ps_games_item(index-n,&id,&title)) {
                snprintf(stable,sizeof(stable),"%s",id);
                if(!ps_games_open_id(stable))ps_toast(ps_games_last_error());
            }
        } else ps_navigate(PS_DEVICE);
        return true;
    }
    return false;
}

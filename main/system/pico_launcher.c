/*
 * SPDX-License-Identifier: Apache-2.0
 * 中文：应用集提供系统与TF模块的统一直达网格；首页入口优先沿用用户顺序。
 * English: A single launch grid for system and TF apps, with Home entries in the user's order first.
 * 冻结：不复制应用实现，不在绘制时访问文件，不直接调用模块代码或改变已保存导航；应用包下载入口只属于应用集。
 * Frozen: Never duplicate app implementations, access files while painting, invoke module code directly or alter saved navigation; app downloads live only in Apps.
 */
#include "pico_launcher.h"
#include "pico_scroll.h"
#include "pico_navigation.h"
#include "pico_i18n.h"
#include "pico_theme.h"
#include "pico_updates.h"
#include <stdio.h>
#include <string.h>

static unsigned page;
static int remove_item=-1;
static ps_scroll_track_t launcher_scroll;
static bool download_picker;
static unsigned download_picker_page;
static bool download_selected[PS_UPDATES_APPS_MAX];
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
static EpdRect download_rect(void) {
    ps_view_t v=ps_view();
    return v.landscape?(EpdRect){16,118,v.w-32,40}:
        (EpdRect){16,148,v.w-32,58};
}
static EpdRect download_picker_rect(void) {
    ps_view_t v=ps_view();
    int w=v.w-32;if(w>600)w=600;
    int h=v.landscape?280:440;
    return (EpdRect){(v.w-w)/2,(v.h-h)/2,w,h};
}
static unsigned download_per_page(void) {
    return ps_view().landscape?9:7;
}
static unsigned download_picker_pages(unsigned count) {
    unsigned per=download_per_page();return count?(count+per-1)/per:1;
}
static EpdRect download_item_rect(unsigned index) {
    ps_view_t v=ps_view();EpdRect picker=download_picker_rect();
    if(v.landscape) {
        int gap=8,w=(picker.width-32-gap*2)/3;
        return (EpdRect){picker.x+16+(int)(index%3)*(w+gap),
            picker.y+62+(int)(index/3)*52,w,44};
    }
    return (EpdRect){picker.x+16,picker.y+62+(int)index*40,picker.width-32,36};
}
static const char *download_title(const char *id) {
    static const struct {const char *id,*key;} names[]={
        {"gomoku","launcher.app.gomoku"},{"go","launcher.app.go"},
        {"xiangqi","launcher.app.xiangqi"},{"chess","launcher.app.chess"},
        {"2048","launcher.app.2048"},{"snake","launcher.app.snake"},
        {"drawing-notes","launcher.app.drawing"},{"weread","launcher.app.weread"},
        {"home-link","launcher.app.home"}
    };
    for(size_t i=0;i<sizeof(names)/sizeof(names[0]);i++)
        if(!strcmp(id,names[i].id))return ps_tr(names[i].key);
    return id;
}
static unsigned download_selected_count(const ps_updates_app_t *apps,unsigned count) {
    unsigned selected=0;
    for(unsigned i=0;i<count;i++)if(!apps[i].installed&&download_selected[i])selected++;
    return selected;
}
static unsigned download_missing_count(const ps_updates_app_t *apps,unsigned count) {
    unsigned missing=0;
    for(unsigned i=0;i<count;i++)if(!apps[i].installed)missing++;
    return missing;
}
static void download_toggle_all(const ps_updates_app_t *apps,unsigned count) {
    unsigned selected=download_selected_count(apps,count);
    unsigned missing=download_missing_count(apps,count);
    for(unsigned i=0;i<count;i++)download_selected[i]=!apps[i].installed&&selected!=missing;
}
static EpdRect card(unsigned slot) {
    ps_view_t v=ps_view();int cols=v.landscape?3:2,rows=v.landscape?2:3;
    int top=v.landscape?168:216,gap=10,w=(v.w-32-(cols-1)*gap)/cols;
    int h=(v.h-top-77-(rows-1)*gap)/rows;
    return (EpdRect){16+(int)(slot%cols)*(w+gap),top+(int)(slot/cols)*(h+gap),w,h};
}
static void download_picker_render(uint8_t *fb) {
    ps_view_t v=ps_view();EpdRect r=download_picker_rect();
    ps_box(fb,r.x,r.y,r.width,r.height,12,15,true);
    ps_box(fb,r.x,r.y,r.width,r.height,12,0,false);
    ps_text(fb,r.x+16,r.y+12,20,ps_tr("launcher.download.picker"),0);
    ps_updates_app_t apps[PS_UPDATES_APPS_MAX];
    unsigned count=ps_updates_apps_catalog(apps,PS_UPDATES_APPS_MAX);
    unsigned total=download_picker_pages(count);char text[80];
    if(download_picker_page>=total)download_picker_page=total-1;
    if(total>1) {
        ps_button(fb,r.x+r.width-76,r.y+10,30,26,"<",false);
        ps_button(fb,r.x+r.width-42,r.y+10,30,26,">",false);
        snprintf(text,sizeof(text),"%u / %u",download_picker_page+1,total);
        ps_center(fb,r.x+r.width-59,r.y+44,9,text,5);
    }
    unsigned per=download_per_page(),first=download_picker_page*per,last=first+per;
    if(last>count)last=count;
    if(!count) {
        unsigned catalog=0;char message[96];
        (void)ps_updates_apps_status(&catalog,message,sizeof(message));
        ps_wrap(fb,r.x+20,r.y+84,r.width-40,12,3,message,5);
    }
    for(unsigned index=first;index<last;index++) {
        EpdRect item=download_item_rect(index-first);
        ps_box(fb,item.x,item.y,item.width,item.height,7,
               download_selected[index]?13:15,true);
        ps_box(fb,item.x+8,item.y+(item.height-18)/2,18,18,4,
               download_selected[index]?2:11,download_selected[index]);
        if(download_selected[index])ps_icon(fb,item.x+11,item.y+(item.height-12)/2,12,PI_CHECK,15);
        ps_wrap(fb,item.x+32,item.y+(v.landscape?5:7),item.width-96,11,1,
                download_title(apps[index].id),1);
        const char *state=apps[index].installed?ps_tr("launcher.download.installed"):
            download_selected[index]?ps_tr("launcher.download.chosen"):
            ps_tr("launcher.download.available");
        if(v.landscape)ps_text(fb,item.x+32,item.y+24,9,state,5);
        else ps_text(fb,item.x+item.width-62,item.y+10,9,state,5);
    }
    int by=r.y+r.height-48;
    unsigned selected=download_selected_count(apps,count);
    ps_button(fb,r.x+16,by,58,34,ps_tr("launcher.download.all"),false);
    ps_button(fb,r.x+82,by,74,34,ps_tr("common.cancel"),false);
    snprintf(text,sizeof(text),ps_tr("launcher.download.selected"),selected);
    ps_button(fb,r.x+164,by,r.width-180,34,text,selected!=0);
}
void ps_launcher_enter(void) {
    ps_games_rescan();remove_item=-1;page=effective_page();
    download_picker=false;download_picker_page=0;
    memset(download_selected,0,sizeof(download_selected));
}
void ps_launcher_render(uint8_t *fb) {
    ps_view_t v=ps_view();unsigned n=destinations(),shown=effective_page();char text[56];
    ps_fill(fb,0,76,v.w,v.h-76,14);
    if(ps_navigation_index(PS_APPS)>=0) {
        ps_tabs(fb,0);ps_text(fb,16,85,25,ps_tr("launcher.title"),1);
    } else ps_heading(fb,ps_tr("launcher.title"),NULL);
    ps_button(fb,v.w-80,85,64,29,ps_tr("launcher.refresh"),false);
    if(!v.landscape){snprintf(text,sizeof(text),ps_tr("launcher.summary"),n,ps_games_count());ps_text(fb,18,122,11,text,5);}
    EpdRect download=download_rect();
    ps_surface(fb,download.x,download.y,download.width,download.height,false);
    int icon_y=download.y+(download.height-30)/2;
    ps_box(fb,download.x+10,icon_y,30,30,8,13,true);
    ps_icon(fb,download.x+15,icon_y+5,20,PI_PLUS,2);
    ps_text(fb,download.x+50,download.y+(v.landscape?5:8),13,ps_tr("launcher.download.title"),1);
    unsigned catalog=0;char message[96];
    (void)ps_updates_apps_status(&catalog,message,sizeof(message));
    ps_wrap(fb,download.x+50,download.y+(v.landscape?23:32),download.width-122,9,1,message,5);
    ps_button(fb,download.x+download.width-64,download.y+(download.height-28)/2,52,26,
              ps_tr("launcher.download.action"),false);
    for(unsigned slot=0;slot<6&&shown*6+slot<count();slot++) {
        unsigned index=shown*6+slot;EpdRect r=card(slot);
        const ps_navigation_entry_t *item=index<n?destination(index):NULL;
        const char *id=NULL,*title=ps_tr("launcher.settings"),*source=ps_tr("launcher.source.settings");ps_icon_t icon=PI_SETTINGS;
        if(item){title=ps_navigation_label(item);icon=item->icon;source=ps_tr(item->route==PS_APPLICATION?"launcher.source.tf_app":"launcher.source.system");}
        else if(index<n+ps_games_count()) {
            if(!ps_games_item(index-n,&id,&title))continue;
            (void)id;icon=PI_GAME;source=ps_tr("launcher.source.tf_game");
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
    ps_button(fb,16,y,68,29,ps_tr("common.prev"),false);
    snprintf(text,sizeof(text),"%u / %u",shown+1,pages());ps_center(fb,v.w/2,y+8,11,text,5);
    ps_button(fb,v.w-84,y,68,29,ps_tr("common.next"),false);
    if(remove_item>=0&&(unsigned)remove_item<ps_games_count()) {
        int w=v.w-40;if(w>500)w=500;int x=(v.w-w)/2,top=(v.h-220)/2,bw=(w-52)/2;
        ps_box(fb,x,top,w,220,8,15,true);ps_box(fb,x,top,w,220,8,0,false);
        ps_text(fb,x+20,top+20,20,"移除这个应用？",0);
        ps_wrap(fb,x+20,top+65,w-40,13,2,"移除应用包，存档会保留",4);
        ps_button(fb,x+20,top+150,bw,42,"取消",false);
        ps_button(fb,x+32+bw,top+150,bw,42,"移除",true);
    }
    if(download_picker)download_picker_render(fb);
}
static bool turn(int delta) {
    unsigned last=pages();if(last<=1)return false;
    page=(effective_page()+last+delta)%last;return true;
}
bool ps_launcher_key(int key) {
    if(download_picker) {
        if(key!=0&&key!=2)return false;
        ps_updates_app_t apps[PS_UPDATES_APPS_MAX];
        unsigned count=ps_updates_apps_catalog(apps,PS_UPDATES_APPS_MAX);
        unsigned total=download_picker_pages(count);
        if(total<=1)return true;
        download_picker_page=(download_picker_page+total+(key==0?-1:1))%total;
        return true;
    }
    return key==0||key==2?turn(key==0?-1:1):false;
}
bool ps_launcher_event(const ps_event_t *e) {
    if(remove_item>=0) {
        if(e->type!=PS_TAP)return true;
        ps_view_t v=ps_view();int w=v.w-40;if(w>500)w=500;int x=(v.w-w)/2,top=(v.h-220)/2,bw=(w-52)/2;
        if(!ps_hit(e->x,e->y,x,top,w,220)||ps_hit(e->x,e->y,x+20,top+150,bw,42)) {
            remove_item=-1;
        } else if(ps_hit(e->x,e->y,x+32+bw,top+150,bw,42)) {
            const char *id=NULL,*title=NULL;
            if(ps_games_item((unsigned)remove_item,&id,&title)&&ps_games_remove(id))remove_item=-1;
            else {ps_toast(ps_games_last_error());remove_item=-1;}
        }
        ps_dirty();return true;
    }
    if(download_picker) {
        if(e->type!=PS_TAP)return true;
        EpdRect r=download_picker_rect();
        if(!ps_hit(e->x,e->y,r.x,r.y,r.width,r.height)) {
            download_picker=false;ps_dirty();return true;
        }
        ps_updates_app_t apps[PS_UPDATES_APPS_MAX];
        unsigned count=ps_updates_apps_catalog(apps,PS_UPDATES_APPS_MAX);
        unsigned total=download_picker_pages(count),per=download_per_page();
        if(total>1) {
            if(ps_hit(e->x,e->y,r.x+r.width-76,r.y+10,30,26)) {
                download_picker_page=(download_picker_page+total-1)%total;
                return true;
            }
            if(ps_hit(e->x,e->y,r.x+r.width-42,r.y+10,30,26)) {
                download_picker_page=(download_picker_page+1)%total;
                return true;
            }
        }
        unsigned first=download_picker_page*per,last=first+per;if(last>count)last=count;
        for(unsigned index=first;index<last;index++) {
            EpdRect item=download_item_rect(index-first);
            if(ps_hit(e->x,e->y,item.x,item.y,item.width,item.height)) {
                if(apps[index].installed) {
                    ps_toast(ps_tr("launcher.download.installed"));
                } else {
                    download_selected[index]=!download_selected[index];
                    ps_dirty();
                }
                return true;
            }
        }
        int by=r.y+r.height-48;
        if(ps_hit(e->x,e->y,r.x+16,by,58,34)) {
            download_toggle_all(apps,count);ps_dirty();return true;
        }
        if(ps_hit(e->x,e->y,r.x+82,by,74,34)) {
            download_picker=false;ps_dirty();return true;
        }
        if(ps_hit(e->x,e->y,r.x+164,by,r.width-180,34)) {
            const char *ids[PS_UPDATES_APPS_MAX];unsigned selected=0;
            for(unsigned i=0;i<count;i++)
                if(!apps[i].installed&&download_selected[i])ids[selected++]=apps[i].id;
            if(!selected) {
                ps_toast(ps_tr("launcher.download.need_selection"));
            } else if(!ps_updates_download_apps_selected(ids,selected)) {
                unsigned catalog=0;char message[96];
                (void)ps_updates_apps_status(&catalog,message,sizeof(message));
                ps_toast(message);
            } else {
                download_picker=false;
                ps_toast(ps_tr("launcher.download.started"));
            }
            ps_dirty();return true;
        }
        return true;
    }
    // 上下翻页跟手提交；左右滑动仍留给首页导航。/ Vertical paging commits with the finger; horizontal swipes stay with Home navigation.
    bool consume=false;
    int d=ps_scroll_step(&launcher_scroll,e->type,e->x0,e->y0,e->x,e->y,PS_SCROLL_AXIS_Y,&consume);
    if(d){
        if(turn(d))return true;
        ps_scroll_disarm(&launcher_scroll);
        return false;
    }
    if(consume)return true;
    if(e->type!=PS_TAP&&e->type!=PS_LONG)return false;
    ps_view_t v=ps_view();
    if(ps_hit(e->x,e->y,v.w-80,85,64,29)){ps_launcher_enter();return true;}
    EpdRect download=download_rect();
    if(e->type==PS_TAP&&ps_hit(e->x,e->y,download.x,download.y,download.width,download.height)){
        download_picker=true;download_picker_page=0;remove_item=-1;
        memset(download_selected,0,sizeof(download_selected));
        (void)ps_updates_apps_catalog_refresh();
        ps_dirty();
        return true;
    }
    int y=v.h-64;
    if(ps_hit(e->x,e->y,16,y,68,29))return turn(-1);
    if(ps_hit(e->x,e->y,v.w-84,y,68,29))return turn(1);
    unsigned n=destinations(),shown=effective_page();
    for(unsigned slot=0;slot<6&&shown*6+slot<count();slot++) {
        EpdRect r=card(slot);if(!ps_hit(e->x,e->y,r.x,r.y,r.width,r.height))continue;
        unsigned index=shown*6+slot;
        if(e->type==PS_LONG&&index>=n&&index<n+ps_games_count()) {
            remove_item=(int)(index-n);ps_dirty();return true;
        }
        if(e->type==PS_LONG)return false;
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

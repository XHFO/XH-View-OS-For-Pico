/*
 * SPDX-FileCopyrightText: 2026 XH Lab
 * SPDX-License-Identifier: Apache-2.0
 *
 * 中文：首页入口使用稳定ID保存，编排草稿与正在使用的导航隔离。
 * English: Persist home destinations by stable ID and isolate the editor draft from live navigation.
 * 用户修订：新配置默认书架/家庭/小游戏/应用集；已保存首页顺序不变，应用集也可从设置进入。
 * User revision: New configurations default to Books/Home/Games/Apps; retain existing saved order and keep Apps available in Settings.
 * 冻结：用户可添加、替换、排序、隐藏；设置独立且永不隐藏，至少保留一个可用入口。
 * Frozen: Support add/replace/order/hide; Settings stays separate and one usable destination remains.
 * 冻结：保存成功后才替换运行配置；失败保留草稿；绘制不读写文件或改变状态。
 * Frozen: Replace live configuration only after successful persistence; preserve failed drafts; rendering is pure.
 */
#include "pico_navigation.h"
#include "pico_i18n.h"
#include <stdio.h>
#include <string.h>

#define NAV_MAGIC 0x4e415631u
typedef struct {
    uint32_t magic;
    uint8_t count, reserved[3];
    uint16_t ids[PS_NAV_MAX_VISIBLE];
} navigation_saved_t;
static const ps_navigation_entry_t builtin[] = {
    {PS_NAV_BOOKS, PS_SHELF, PI_BOOK, "书架", NULL},
    {PS_NAV_HOME, PS_HA, PI_HOME, "家庭", NULL},
    {PS_NAV_GAMES, PS_GAMES, PI_GAME, "小游戏", NULL},
    {PS_NAV_PRINTERS, PS_PRINTERS, PI_PRINTER, "打印机", NULL},
    {PS_NAV_APPS, PS_APPS, PI_GRID, "应用集", NULL}
};
static ps_navigation_entry_t registry[PS_NAV_MAX_ENTRIES];
static int registry_count;
static navigation_saved_t current;
static struct {
    navigation_saved_t draft;
    int selected, picker_page;
    bool active, picking, adding;
} editor;

static const ps_navigation_entry_t *find_entry(uint16_t id) {
    for (int i=0;i<registry_count;i++) if (registry[i].id==id) return &registry[i];
    return NULL;
}
static void defaults(navigation_saved_t *out) {
    memset(out,0,sizeof(*out));out->magic=NAV_MAGIC;out->count=4;
    out->ids[0]=PS_NAV_BOOKS;out->ids[1]=PS_NAV_HOME;out->ids[2]=PS_NAV_GAMES;
    out->ids[3]=PS_NAV_APPS;
}
static bool valid(const navigation_saved_t *saved) {
    if(saved->magic!=NAV_MAGIC||!saved->count||saved->count>PS_NAV_MAX_VISIBLE)return false;
    for(int i=0;i<saved->count;i++) {
        if(!saved->ids[i])return false;
        for(int j=0;j<i;j++)if(saved->ids[j]==saved->ids[i])return false;
    }
    return true;
}
static int usable(const navigation_saved_t *saved) {
    int n=0;for(int i=0;i<saved->count;i++)if(find_entry(saved->ids[i]))n++;return n;
}
void ps_navigation_init(void) {
    ps_i18n_init();
    memcpy(registry,builtin,sizeof(builtin));registry_count=(int)(sizeof(builtin)/sizeof(*builtin));
    defaults(&current);navigation_saved_t loaded;
    if(ps_load("navigation1",&loaded,sizeof(loaded))&&valid(&loaded))current=loaded;
    memset(&editor,0,sizeof(editor));
}
bool ps_navigation_register(const ps_navigation_entry_t *entry) {
    if(!entry||entry->id<PS_NAV_CUSTOM_FIRST||!entry->label||!*entry->label||
       entry->icon<0||entry->icon>=PI_COUNT||(!entry->launch&&(entry->route<0||entry->route>=PS_ROUTE_COUNT))||
       find_entry(entry->id)||registry_count>=PS_NAV_MAX_ENTRIES)return false;
    if(!entry->launch)for(int i=0;i<registry_count;i++)if(registry[i].route==entry->route)return false;
    registry[registry_count++]=*entry;return true;
}
void ps_navigation_unregister(uint16_t id) {
    if(id<PS_NAV_CUSTOM_FIRST)return;
    for(int i=0;i<registry_count;i++)if(registry[i].id==id){
        memmove(registry+i,registry+i+1,(registry_count-i-1)*sizeof(*registry));registry_count--;return;
    }
}
int ps_navigation_count(void) {int n=usable(&current);return n?n:1;}
int ps_navigation_catalog_count(void) {return registry_count;}
const ps_navigation_entry_t *ps_navigation_catalog_item(int index) {return index>=0&&index<registry_count?&registry[index]:NULL;}
const ps_navigation_entry_t *ps_navigation_item(int index) {
    if(index<0)return NULL;
    for(int i=0;i<current.count;i++) {
        const ps_navigation_entry_t *entry=find_entry(current.ids[i]);
        if(entry&&index--==0)return entry;
    }
    return index==0&&!usable(&current)?&builtin[0]:NULL;
}
const char *ps_navigation_label(const ps_navigation_entry_t *entry) {
    if(!entry)return "";
    const char *key=NULL;
    switch(entry->id){
        case PS_NAV_BOOKS:key="nav.books";break;
        case PS_NAV_HOME:key="nav.home";break;
        case PS_NAV_GAMES:key="nav.games";break;
        case PS_NAV_PRINTERS:key="nav.printers";break;
        case PS_NAV_APPS:key="nav.apps";break;
        default:break;
    }
    return key?ps_tr(key):entry->label?entry->label:"";
}
int ps_navigation_index(ps_route_t route) {
    for(int i=0;i<ps_navigation_count();i++) {
        const ps_navigation_entry_t *entry=ps_navigation_item(i);
        if(entry&&entry->route==route)return i;
    }
    return -1;
}
bool ps_navigation_storage_save(void) {return ps_store("navigation1",&current,sizeof(current));}
void ps_navigation_enter(void) {
    memset(&editor,0,sizeof(editor));editor.active=true;editor.draft=current;
    if(!usable(&editor.draft))defaults(&editor.draft);
}
static int row_top(void) {return ps_view().landscape?114:152;}
static int row_height(void) {return ps_view().landscape?28:57;}
static int action_y(void) {return ps_view().landscape?264:454;}
static int more_y(void) {return ps_view().landscape?300:503;}
static int footer_y(void) {return ps_view().h-(ps_view().landscape?43:67);}
static int picker_rows(void) {return ps_view().landscape?3:6;}
static EpdRect picker_box(void) {
    ps_view_t v=ps_view();int top=v.landscape?82:143;
    return (EpdRect){24,top,v.w-48,v.h-top-(v.landscape?50:80)};
}
static int picker_row_y(void) {return picker_box().y+45;}
static int picker_row_height(void) {return ps_view().landscape?41:43;}
static int draft_index(uint16_t id) {
    for(int i=0;i<editor.draft.count;i++)if(editor.draft.ids[i]==id)return i;
    return -1;
}
static void picker_render(uint8_t *fb) {
    ps_view_t v=ps_view();EpdRect r=picker_box();
    ps_fill(fb,0,76,v.w,v.h-76,13);ps_box(fb,r.x,r.y,r.width,r.height,12,15,true);
    ps_box(fb,r.x,r.y,r.width,r.height,12,9,false);
    ps_text(fb,r.x+16,r.y+13,15,ps_tr(editor.adding?"settings.nav.add_title":"settings.nav.replace_title"),1);
    ps_icon(fb,r.x+r.width-36,r.y+11,22,PI_CLOSE,3);
    int rows=picker_rows(),rh=picker_row_height(),start=editor.picker_page*rows;
    for(int j=0;j<rows&&start+j<registry_count;j++) {
        const ps_navigation_entry_t *entry=&registry[start+j];int y=picker_row_y()+j*rh;
        int used=draft_index(entry->id);
        ps_icon(fb,r.x+14,y+8,22,entry->icon,2);
        ps_text_fixed(fb,r.x+46,y+9,14,ps_navigation_label(entry),2);
        if(used>=0)ps_text_fixed(fb,r.x+r.width-76,y+13,10,ps_tr("settings.nav.shown"),5);
        ps_line(fb,r.x+14,y+rh-1,r.x+r.width-15,y+rh-1,13);
    }
    int y=r.y+r.height-36;
    ps_button(fb,r.x+12,y,76,28,ps_tr("common.prev"),false);
    char page[40];snprintf(page,sizeof(page),"%d / %d",editor.picker_page+1,(registry_count+rows-1)/rows);
    ps_center(fb,r.x+r.width/2,y+7,11,page,5);
    ps_button(fb,r.x+r.width-88,y,76,28,ps_tr("common.next"),false);
}
void ps_navigation_render(uint8_t *fb) {
    ps_view_t v=ps_view();ps_heading(fb,ps_tr("settings.nav.title"),NULL);
    ps_i18n_chip(fb,v.w-16-PS_I18N_CHIP_W,32);
    ps_text_fixed(fb,18,84,12,ps_tr("settings.nav.hint"),5);
    if(!v.landscape)ps_text_fixed(fb,18,111,11,ps_tr("settings.nav.hint_settings"),5);
    for(int i=0;i<editor.draft.count;i++) {
        int y=row_top()+i*row_height(),h=row_height()-4;bool selected=editor.selected==i;
        const ps_navigation_entry_t *entry=find_entry(editor.draft.ids[i]);
        ps_box(fb,16,y,v.w-32,h,6,selected?11:14,true);
        if(selected)ps_box(fb,16,y,v.w-32,h,6,7,false);
        ps_icon(fb,28,y+(h-22)/2,22,entry?entry->icon:PI_GRID,2);
        ps_text_fixed(fb,63,y+(h-14)/2,14,entry?ps_navigation_label(entry):ps_tr("settings.nav.missing"),2);
        char number[12];snprintf(number,sizeof(number),"%d",i+1);
        ps_text(fb,v.w-44,y+(h-12)/2,12,number,5);
    }
    int cw=(v.w-50)/4,y=action_y(),h=v.landscape?29:37;
    const char *actions[]={ps_tr("settings.nav.up"),ps_tr("settings.nav.down"),ps_tr("settings.nav.replace"),ps_tr("settings.nav.hide")};
    for(int i=0;i<4;i++)ps_button(fb,16+i*(cw+6),y,cw,h,actions[i],false);
    y=more_y();
    if(v.landscape) {
        ps_button(fb,16,y,102,29,ps_tr("settings.nav.add"),false);
        ps_button(fb,126,y,102,29,ps_tr("settings.nav.reset"),false);
    } else {
        ps_button(fb,16,y,(v.w-42)/2,34,ps_tr("settings.nav.add"),false);
        ps_button(fb,26+(v.w-42)/2,y,(v.w-42)/2,34,ps_tr("settings.nav.reset"),false);
    }
    y=footer_y();
    if(v.landscape) {
        ps_button(fb,v.w-220,y,94,31,ps_tr("common.cancel"),false);
        ps_button(fb,v.w-114,y,98,31,ps_tr("common.done"),true);
    } else {
        ps_button(fb,16,y,100,35,ps_tr("common.cancel"),false);
        ps_button(fb,v.w-116,y,100,35,ps_tr("common.done"),true);
    }
    if(editor.picking)picker_render(fb);
}
static void move_selected(int delta) {
    int next=editor.selected+delta;
    if(next<0||next>=editor.draft.count)return;
    uint16_t id=editor.draft.ids[next];editor.draft.ids[next]=editor.draft.ids[editor.selected];
    editor.draft.ids[editor.selected]=id;editor.selected=next;
}
static void hide_selected(void) {
    int pos=editor.selected;
    if(editor.draft.count<=1||(find_entry(editor.draft.ids[pos])&&usable(&editor.draft)<=1)) {
        ps_toast(ps_tr("settings.nav.keep_one"));return;
    }
    memmove(editor.draft.ids+pos,editor.draft.ids+pos+1,(editor.draft.count-pos-1)*sizeof(*editor.draft.ids));
    editor.draft.ids[--editor.draft.count]=0;
    if(editor.selected>=editor.draft.count)editor.selected=editor.draft.count-1;
}
static void choose_entry(uint16_t id) {
    int used=draft_index(id);
    if(editor.adding) {
        if(used>=0){ps_toast(ps_tr("settings.nav.already"));return;}
        editor.selected=editor.draft.count;editor.draft.ids[editor.draft.count++]=id;
    } else {
        if(used>=0)editor.draft.ids[used]=editor.draft.ids[editor.selected];
        editor.draft.ids[editor.selected]=id;
    }
    editor.picking=false;
}
static bool picker_event(const ps_event_t *e) {
    if(e->type!=PS_TAP)return true;
    EpdRect r=picker_box();int rows=picker_rows(),rh=picker_row_height();
    if(!ps_hit(e->x,e->y,r.x,r.y,r.width,r.height)||ps_hit(e->x,e->y,r.x+r.width-48,r.y,48,43)) {
        editor.picking=false;return true;
    }
    for(int i=0;i<rows&&editor.picker_page*rows+i<registry_count;i++)
        if(ps_hit(e->x,e->y,r.x+10,picker_row_y()+i*rh,r.width-20,rh)) {
            choose_entry(registry[editor.picker_page*rows+i].id);return true;
        }
    int y=r.y+r.height-36;
    if(ps_hit(e->x,e->y,r.x+12,y,76,28)&&editor.picker_page>0)editor.picker_page--;
    if(ps_hit(e->x,e->y,r.x+r.width-88,y,76,28)&&(editor.picker_page+1)*rows<registry_count)editor.picker_page++;
    return true;
}
bool ps_navigation_event(const ps_event_t *e) {
    if(!editor.active)return false;
    if(e->type==PS_TAP){
        ps_view_t v=ps_view();
        if(ps_i18n_chip_hit(e->x,e->y,v.w-16-PS_I18N_CHIP_W,32))return true;
    }
    if(editor.picking)return picker_event(e);
    if(e->type!=PS_TAP)return false;
    ps_view_t v=ps_view();
    for(int i=0;i<editor.draft.count;i++)if(ps_hit(e->x,e->y,16,row_top()+i*row_height(),v.w-32,row_height()-4)) {
        editor.selected=i;return true;
    }
    int cw=(v.w-50)/4,y=action_y(),h=v.landscape?29:37;
    for(int i=0;i<4;i++)if(ps_hit(e->x,e->y,16+i*(cw+6),y,cw,h)) {
        if(i==0)move_selected(-1);else if(i==1)move_selected(1);else if(i==3)hide_selected();
        else {editor.adding=false;editor.picker_page=0;editor.picking=true;}return true;
    }
    y=more_y();int add_w=v.landscape?102:(v.w-42)/2,other_x=v.landscape?126:26+add_w;
    if(ps_hit(e->x,e->y,16,y,add_w,v.landscape?29:34)) {
        if(editor.draft.count>=PS_NAV_MAX_VISIBLE)ps_toast(ps_tr("settings.nav.full"));
        else {
            bool available=false;for(int i=0;i<registry_count;i++)if(draft_index(registry[i].id)<0)available=true;
            if(!available)ps_toast(ps_tr("settings.nav.none_left"));
            else {editor.adding=true;editor.picker_page=0;editor.picking=true;}
        }return true;
    }
    if(ps_hit(e->x,e->y,other_x,y,add_w,v.landscape?29:34)) {defaults(&editor.draft);editor.selected=0;return true;}
    y=footer_y();int cancel_x=v.landscape?v.w-220:16,done_x=v.landscape?v.w-114:v.w-116;
    if(ps_hit(e->x,e->y,cancel_x,y,v.landscape?94:100,v.landscape?31:35)) {editor.active=false;ps_back();return true;}
    if(ps_hit(e->x,e->y,done_x,y,v.landscape?98:100,v.landscape?31:35)) {
        if(!usable(&editor.draft)){ps_toast(ps_tr("settings.nav.keep_usable"));return true;}
        if(!ps_store("navigation1",&editor.draft,sizeof(editor.draft))) {ps_toast(ps_tr("settings.nav.save_failed"));return true;}
        current=editor.draft;editor.active=false;ps_back();return true;
    }
    return false;
}
bool ps_navigation_back(void) {
    if(editor.picking){editor.picking=false;return true;}
    editor.active=false;return false;
}

/*
 * SPDX-FileCopyrightText: 2026 XH Lab
 * SPDX-License-Identifier: Apache-2.0
 *
 * 家庭、打印监看与阅读待机界面，使用16灰阶表面层次突出设备状态与阅读内容。
 * Home, printer monitoring, and reader standby use 16-gray surface hierarchy for device state and reading content.
 * 冻结：家庭/打印使用真实共享服务；未配置显示空状态，render纯绘制，不访问硬件或写设置。
 * 用户新增：翻页时钟可同步随机划线摘录，沿用九条缓存、轮换与低电量暂停；默认关闭且单独保存。
 * User addition: optional clock excerpts reuse the nine-item queue, rotation interval and low-power pause; disabled by default and stored separately.
 * Frozen: Home/printers use real shared services and unconfigured empty states; render never accesses hardware or writes settings.
 * 冻结：新版视觉不改变实体排序、筛选、命中区域、时钟帧时序或摘录缓存；紧凑横屏保留12项。
 * Frozen: Visual refinement preserves sorting, filters, hit regions, clock frame timing and excerpt caching; compact landscape retains 12 items.
 * 冻结：为缩短锁屏，当前存储会话成功提交且未变化的四项配置不重复访问文件；任何存储边界使快照失效。
 * Frozen: To shorten locking, four unchanged configurations skip file access only after a successful same-session commit; storage boundaries invalidate snapshots.
 * 冻结：再次锁屏保留有效摘录，空队列按需补充，仅实际间隔到期推进；低电量暂停不消费队列。
 * Frozen: Re-entering lock retains a valid excerpt, fills an empty queue on demand, and advances only when due; low-battery pause never consumes it.
 */
#include "pico_system.h"
#include "pico_i18n.h"
#include "pico_clock_digits_sparse.h"
#include "pico_power.h"
#include "pico_desktop.h"
#include "pico_theme.h"
#include "read_pico_home_link.h"
#include "read_pico_management.h"
#include "ui_wifi_qr.h"
#include "ttf_font.h"

#ifdef ESP_PLATFORM
#include "esp_attr.h"
#define PICO_UI_CACHE_ATTR EXT_RAM_BSS_ATTR
#else
#define PICO_UI_CACHE_ATTR
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define HOME_MAGIC 0x50484132u
#define PRINT_MAGIC 0x50505232u
#define QUOTE_MAGIC 0x50515132u
#define ENTITY_COUNT 30
#define ROOM_COUNT 5
#define TYPE_COUNT 6
#define QUOTE_CACHE 9
#define QUOTE_BYTES 768
#define SOURCE_BYTES 160
#define PRINT_COUNT 3
#define BLACK 0
#define WHITE 15
#define MUTED PS_THEME_SECONDARY

typedef struct {
    uint32_t magic, hidden, on;
    uint8_t types, rooms, unavailable, manual, name_sort, compact;
    uint8_t order[ENTITY_COUNT];
    int8_t climate_temp[ROOM_COUNT];
    uint8_t climate_mode[ROOM_COUNT];
    uint8_t external_enabled, external_control;
    char local[128], external[128], home_wifi[48];
} home_config_t;
typedef struct {
    uint8_t active, source, broker, tls;
    uint16_t port;
    char name[40], host[96], serial[40], topic[128], username[32];
} printer_t;
typedef struct {
    uint32_t magic;
    uint8_t selected, image, refresh;
    printer_t devices[PRINT_COUNT];
} print_config_t;
typedef struct { uint32_t magic, current, pending[QUOTE_CACHE - 1]; } quote_config_t;
typedef struct { uint32_t id; char text[QUOTE_BYTES], source[SOURCE_BYTES]; } cached_quote_t;
typedef struct { int x,y,w,h; } rect_t;
typedef enum { PV_LIST, PV_SOURCE, PV_IMPORT, PV_MQTT, PV_INFO, PV_REMOVE } printer_view_t;

static const char *const rooms[] = {"客厅", "卧室", "书房", "厨房", "玄关"};
static const char *const type_names[] = {"灯光", "开关", "空调", "风扇", "温度", "湿度"};
static const char *const entity_names[] = {"主灯", "插座", "空调", "风扇", "温度", "湿度"};
static const char *const domains[] = {"light", "switch", "climate", "fan", "sensor", "sensor"};
static const ps_icon_t entity_icons[] = {PI_LIGHT, PI_SWITCH, PI_CLIMATE, PI_FAN, PI_THERMOMETER, PI_DROPLET};
static const int intervals[] = {5,15,30,60,360,720};
static const char *const interval_names[] = {"5 分钟", "15 分钟", "30 分钟", "1 小时", "6 小时", "12 小时"};
static const char *const modes[] = {"制冷", "制热", "自动", "送风"};
static home_config_t hc;
static print_config_t pc;
static quote_config_t qc;
// 普通UI缓存不占用DMA内部堆；没有ISR或外设持有其地址。 / Ordinary UI caches spare DMA RAM; no ISR or peripheral retains these addresses.
static PICO_UI_CACHE_ATTR cached_quote_t qcache[QUOTE_CACHE];
static PICO_UI_CACHE_ATTR cached_quote_t quote_candidates[QUOTE_CACHE];
static uint8_t available[ENTITY_COUNT];
static int section, dashboard_page, ha_tab, filter_page, layout_page, climate_entity = -1;
static bool room_picker;
static bool wifi_picker;
static int wifi_choice_page;
static printer_view_t printer_view, printer_return;
static printer_t printer_draft;
static uint8_t import_mask;
static int info_printer, mqtt_page;
static int64_t print_updated[PRINT_COUNT];
static bool print_was_live[PRINT_COUNT];
static uint32_t rng_state;
static int quote_segment;
static bool quote_paused;
static uint8_t clock_quotes;
static int64_t quote_due, wake_until;
static int clock_values[2], clock_old[2], clock_frame;
static bool clock_valid;
static int64_t clock_flip_start;
static ps_route_t entered_route = PS_ROUTE_COUNT;
static PICO_UI_CACHE_ATTR rp_home_config_t link_config;
static PICO_UI_CACHE_ATTR rp_home_snapshot_t link_snapshot;
static int64_t link_due;
static uint32_t link_revision;

static void copy_text(char *out,size_t n,const char *value) {
    if (!n) return;
    snprintf(out,n,"%s",value ? value : "");
}
static int clampi(int n,int lo,int hi) { return n < lo ? lo : n > hi ? hi : n; }
static bool in_rect(const ps_event_t *e,rect_t r) { return ps_hit(e->x,e->y,r.x,r.y,r.w,r.h); }
static uint8_t saved_home[sizeof(hc)+sizeof(pc)+sizeof(qc)+sizeof(clock_quotes)], saved_home_valid;
static const struct { const char *key; const void *value; uint16_t bytes, offset; } home_values[] = {
    {"home2",&hc,sizeof(hc),0}, {"print2",&pc,sizeof(pc),sizeof(hc)},
    {"quotes2",&qc,sizeof(qc),sizeof(hc)+sizeof(pc)},
    {"clockquote",&clock_quotes,sizeof(clock_quotes),sizeof(hc)+sizeof(pc)+sizeof(qc)}
};
void ps_home_storage_invalidate(void) { saved_home_valid=0; }
static bool save_home_value(int index) {
    if(!ps_storage_save_allowed())return false;
    const void *value=home_values[index].value;size_t bytes=home_values[index].bytes;
    uint8_t *saved=saved_home+home_values[index].offset,bit=(uint8_t)(1u<<index);
    if((saved_home_valid&bit)&&!memcmp(saved,value,bytes))return true;
    if(!ps_store(home_values[index].key,value,bytes)){saved_home_valid&=(uint8_t)~bit;return false;}
    memcpy(saved,value,bytes);saved_home_valid|=bit;return true;
}
static void save_home(void) { if (!save_home_value(0)) ps_toast("设置保存失败，请稍后重试"); }
static void save_print(void) { if (!save_home_value(1)) ps_toast("打印设置保存失败"); }
static void save_queue(void) { if (!save_home_value(2)) ps_toast("摘录缓存保存失败"); }
static void mark_dirty(void) { ps_dirty(); }
static uint32_t random_u32(void) {
    rng_state ^= rng_state << 13; rng_state ^= rng_state >> 17; rng_state ^= rng_state << 5;
    return rng_state;
}
// 表面与轻阴影只占现有卡片内部及2像素间隙，不扩展触摸判定。
// Surfaces and soft shadows use only the existing card and two pixels of its gap, without enlarging hit regions.
static void home_surface(uint8_t *fb,int x,int y,int w,int h,bool inset) {
    ps_view_t v=ps_view();
    if(y+h+2<=v.h)ps_box(fb,x,y+2,w,h,PS_THEME_RADIUS,PS_THEME_BORDER,true);
    ps_box(fb,x,y,w,h,PS_THEME_RADIUS,inset?PS_THEME_INSET:PS_THEME_SURFACE,true);
    ps_box(fb,x,y,w,h,PS_THEME_RADIUS,PS_THEME_BORDER,false);
}
static void label_row(uint8_t *fb,int y,const char *label,const char *value) {
    ps_view_t v=ps_view();
    ps_text(fb,18,y+3,12,label,MUTED);
    home_surface(fb,16,y+21,v.w-32,38,false);
    ps_wrap(fb,26,y+30,v.w-52,12,1,value,BLACK);
}
static bool row_hit(const ps_event_t *e,int y) { ps_view_t v=ps_view(); return ps_hit(e->x,e->y,16,y+21,v.w-32,38); }
static void checkbox(uint8_t *fb,int x,int y,const char *label,bool value) {
    ps_box(fb,x,y,18,18,5,value?PS_THEME_SELECTED:PS_THEME_SURFACE,true);
    if(!value)ps_box(fb,x,y,18,18,5,PS_THEME_BORDER,false);
    if (value) ps_icon(fb,x+2,y+2,14,PI_CHECK,WHITE);
    ps_text(fb,x+27,y+1,12,label,BLACK);
}
static void page_bar(uint8_t *fb,int page,int total) {
    ps_view_t v=ps_view(); int y=v.h-88; char b[32];
    ps_button(fb,28,y,76,34,"上一页",false);
    ps_box(fb,v.w/2-30,y+1,60,32,8,PS_THEME_INSET,true);
    snprintf(b,sizeof(b),"%d / %d",page+1,total);
    ps_center(fb,v.w/2,y+10,12,b,BLACK);
    ps_button(fb,v.w-104,y,76,34,"下一页",false);
}
static int page_action(const ps_event_t *e,int page,int total) {
    ps_view_t v=ps_view(); int d=0;
    if (e->type==PS_SWIPE_L) d=1;
    else if (e->type==PS_SWIPE_R) d=-1;
    else if (e->type==PS_TAP && ps_hit(e->x,e->y,28,v.h-88,76,34)) d=-1;
    else if (e->type==PS_TAP && ps_hit(e->x,e->y,v.w-104,v.h-88,76,34)) d=1;
    return clampi(page+d,0,total-1);
}
static bool home_reachable(void) {
    int i=ps_wifi_connected_index();
    if(i<0||!ps_demo_home_online())return false;
    if(!*hc.home_wifi||!strcmp(hc.home_wifi,ps_wifi_name(i)))return true;
    return hc.external_enabled&&*hc.external;
}
static bool home_control_available(void) {
    int i=ps_wifi_connected_index();
    return home_reachable()&&(!*hc.home_wifi||!strcmp(hc.home_wifi,ps_wifi_name(i))||hc.external_control);
}

/* ---- 实体面板 / Entity dashboard ---- */
static bool entity_allowed(int id) {
    int room=id/TYPE_COUNT,type=id%TYPE_COUNT;
    return id>=0 && id<ENTITY_COUNT && !(hc.hidden&(1u<<id)) &&
        (hc.types==0 || (hc.types&(1u<<type))) && (hc.rooms==0 || (hc.rooms&(1u<<room))) &&
        (!hc.unavailable || available[id]);
}
static int entity_list(int *out) {
    int n=0;
    for (int p=0;p<ENTITY_COUNT;p++) {
        int id=(section==0 && hc.manual)?hc.order[p]:p;
        if (entity_allowed(id) && (section==0 || id/TYPE_COUNT==section-1)) out[n++]=id;
    }
    if (!(section==0 && hc.manual)) {
        for (int i=1;i<n;i++) {
            int id=out[i],j=i;
            while (j>0) {
                int prior=out[j-1],a,b;
                if (hc.name_sort) { static const int rank[]={5,0,2,1,4,3};a=rank[id%TYPE_COUNT];b=rank[prior%TYPE_COUNT]; }
                else if(section){static const int rank[]={1,0,2,3,4,5};a=rank[id%TYPE_COUNT];b=rank[prior%TYPE_COUNT];}
                else { a=id; b=prior; }
                if (a>=b) break;
                out[j]=prior; j--;
            }
            out[j]=id;
        }
    }
    return n;
}
static int entities_per_page(void) { return ps_view().landscape ? (hc.compact?12:6) : 6; }
static rect_t entity_rect(int index) {
    ps_view_t v=ps_view(); int cols=v.landscape?(hc.compact?4:3):2;
    int rows=v.landscape?(hc.compact?3:2):3, gap=8, top=v.landscape?110:129;
    int cell_w=(v.w-32-gap*(cols-1))/cols;
    int cell_h=(v.h-(v.landscape?94:103)-top-gap*(rows-1))/rows;
    return (rect_t){16+(index%cols)*(cell_w+gap),top+(index/cols)*(cell_h+gap),cell_w,cell_h};
}
static rect_t room_rect(int i) {
    ps_view_t v=ps_view(); int gap=4,w=(v.w-32-5*gap)/6;
    return (rect_t){16+i*(w+gap),v.landscape?70:84,w,v.landscape?30:32};
}
static void entity_value(int id,char *out,size_t bytes) {
    int r=id/6,t=id%6; bool on=(hc.on&(1u<<id))!=0;
    if (!available[id]) copy_text(out,bytes,"不可用");
    else if (t==4) snprintf(out,bytes,"%.1f°C",23.6+r*.1);
    else if (t==5) snprintf(out,bytes,"%d%%",48+r);
    else if (t==2 && on) snprintf(out,bytes,"%d°C · %s",hc.climate_temp[r],modes[hc.climate_mode[r]]);
    else copy_text(out,bytes,on?"已开启":"已关闭");
}
static void render_climate(uint8_t *fb) {
    ps_view_t v=ps_view(); int r=climate_entity/6; char b[80];
    ps_heading(fb,"空调控制",NULL);
    snprintf(b,sizeof(b),"%s · 空调",rooms[r]); ps_center(fb,v.w/2,92,18,b,BLACK);
    home_surface(fb,16,122,v.w-32,128,false);
    snprintf(b,sizeof(b),"%d°C",hc.climate_temp[r]); ps_center(fb,v.w/2,133,48,b,BLACK);
    ps_button(fb,v.w/2-110,204,64,42,"−",false);
    ps_button(fb,v.w/2+46,204,64,42,"＋",false);
    ps_button(fb,v.w/2-42,204,84,42,(hc.on&(1u<<climate_entity))?"关闭":"开启",(hc.on&(1u<<climate_entity))!=0);
    int y=v.landscape?256:280;
    for (int i=0;i<4;i++) ps_button(fb,16+i*(v.w-32)/4,y,(v.w-32)/4-4,32,modes[i],hc.climate_mode[r]==i);
    if (!v.landscape) ps_wrap(fb,24,346,v.w-48,12,3,"长按空调卡片打开温度和运行模式；轻触卡片快速开关。",MUTED);
}
static void render_ha(uint8_t *fb) {
    if (climate_entity>=0) { render_climate(fb); return; }
    ps_view_t v=ps_view(); int list[ENTITY_COUNT],n=entity_list(list),per=entities_per_page();
    if(room_picker){
        ps_heading(fb,"家庭房间",NULL);
        ps_text(fb,18,88,12,"选择房间，查看该房间的标准设备",MUTED);
        for(int i=0;i<ROOM_COUNT;i++){
            int cols=v.landscape?3:2,w=(v.w-32-(cols-1)*8)/cols,h=v.landscape?64:92;
            int x=16+(i%cols)*(w+8),y=128+(i/cols)*(h+10),count=0;char b[40];
            for(int id=i*TYPE_COUNT;id<(i+1)*TYPE_COUNT;id++)if(entity_allowed(id))count++;
            home_surface(fb,x,y,w,h,false);
            ps_box(fb,x+12,y+12,32,32,10,PS_THEME_INSET,true);
            ps_icon(fb,x+18,y+18,20,PI_HOME,PS_THEME_INK);
            ps_text(fb,x+(v.landscape?54:14),y+(v.landscape?12:50),14,rooms[i],PS_THEME_INK);
            snprintf(b,sizeof(b),"%d 个实体",count);
            ps_text(fb,x+(v.landscape?54:14),y+(v.landscape?37:73),8,b,MUTED);
        }
        return;
    }
    ps_text(fb,18,38,21,section?rooms[section-1]:"自定义面板",BLACK);
    ps_text(fb,v.w-112,45,10,home_control_available()?"示例家庭控制":"控制连接不可用",MUTED);
    for (int i=0;i<6;i++) { rect_t r=room_rect(i); ps_button(fb,r.x,r.y,r.w,r.h,i?rooms[i-1]:"自定义",section==i); }
    for (int i=0;i<per && dashboard_page*per+i<n;i++) {
        int id=list[dashboard_page*per+i],t=id%6; rect_t r=entity_rect(i); char b[80];
        bool on=(hc.on&(1u<<id))!=0 && t<4;
        home_surface(fb,r.x,r.y,r.w,r.h,on);
        if(v.landscape){
            bool compact=hc.compact!=0;int badge=compact?24:30,bx=r.x+r.w-badge-8,by=r.y+(compact?10:13);
            ps_box(fb,bx,by,badge,badge,badge/2,on?PS_THEME_SELECTED:PS_THEME_INSET,true);
            ps_icon(fb,bx+4,by+4,badge-8,entity_icons[t],on?PS_THEME_ON_SELECTED:PS_THEME_SECONDARY);
            snprintf(b,sizeof(b),"%s · %s",rooms[id/6],entity_names[t]);
            ps_wrap(fb,r.x+10,r.y+(compact?5:9),r.w-badge-24,compact?8:10,1,b,MUTED);
            entity_value(id,b,sizeof(b));
            ps_wrap(fb,r.x+10,r.y+(compact?23:39),r.w-badge-24,t>=4?(compact?13:17):(compact?10:13),1,b,PS_THEME_INK);
        }else{
            ps_text(fb,r.x+12,r.y+12,10,section?type_names[t]:rooms[id/6],MUTED);
            ps_box(fb,r.x+r.w-45,r.y+14,32,32,16,on?PS_THEME_SELECTED:PS_THEME_INSET,true);
            ps_icon(fb,r.x+r.w-39,r.y+20,20,entity_icons[t],on?PS_THEME_ON_SELECTED:PS_THEME_SECONDARY);
            ps_text(fb,r.x+12,r.y+49,14,entity_names[t],PS_THEME_INK);
            entity_value(id,b,sizeof(b));
            ps_wrap(fb,r.x+12,r.y+83,r.w-24,t>=4?23:16,1,b,PS_THEME_INK);
        }
    }
    if (!n) ps_wrap(fb,32,160,v.w-64,15,3,"暂无设备，请在 Home Assistant 设置中选择实体。",MUTED);
    page_bar(fb,dashboard_page,n?(n+per-1)/per:1);
}
static bool ha_event(const ps_event_t *e) {
    ps_view_t v=ps_view();
    if(room_picker){
        if(e->type!=PS_TAP)return false;
        if(e->x<44&&e->y>=24&&e->y<76){room_picker=false;return true;}
        for(int i=0;i<ROOM_COUNT;i++){int cols=v.landscape?3:2,w=(v.w-32-(cols-1)*8)/cols,h=v.landscape?64:92,x=16+(i%cols)*(w+8),y=128+(i/cols)*(h+10);if(ps_hit(e->x,e->y,x,y,w,h)){section=i+1;dashboard_page=0;room_picker=false;return true;}}
        return false;
    }
    if (climate_entity>=0) {
        int r=climate_entity/6,y=v.landscape?256:280;
        if (e->type==PS_TAP && e->x<48 && e->y<80) { climate_entity=-1; return true; }
        if (e->type!=PS_TAP) return false;
        if(!home_control_available()){ps_toast("连接不可用或未允许外部控制");return true;}
        if (ps_hit(e->x,e->y,v.w/2-110,204,64,42)) hc.climate_temp[r]=clampi(hc.climate_temp[r]-1,16,30);
        else if (ps_hit(e->x,e->y,v.w/2+46,204,64,42)) hc.climate_temp[r]=clampi(hc.climate_temp[r]+1,16,30);
        else if (ps_hit(e->x,e->y,v.w/2-42,204,84,42)) hc.on^=1u<<climate_entity;
        else if (ps_hit(e->x,e->y,16,y,v.w-32,32)) hc.climate_mode[r]=(uint8_t)clampi((e->x-16)/((v.w-32)/4),0,3);
        else return false;
        save_home(); return true;
    }
    int list[ENTITY_COUNT],n=entity_list(list),per=entities_per_page(),total=n?(n+per-1)/per:1;
    if (e->type==PS_TAP) for (int i=0;i<6;i++) if (in_rect(e,room_rect(i))) { section=i; dashboard_page=0; return true; }
    int next=page_action(e,dashboard_page,total);
    if (next!=dashboard_page) { dashboard_page=next; return true; }
    if (e->type!=PS_TAP && e->type!=PS_LONG) return false;
    for (int i=0;i<per && dashboard_page*per+i<n;i++) if (in_rect(e,entity_rect(i))) {
        int id=list[dashboard_page*per+i],t=id%6;
        if (t>=4) { ps_toast("温湿度实体为只读传感器"); return true; }
        if (!home_control_available() || !available[id]) { ps_toast("连接不可用或未允许外部控制"); return true; }
        if (e->type==PS_LONG && t==2) climate_entity=id;
        else { hc.on^=1u<<id; save_home(); ps_toast("示例操作已更新"); }
        return true;
    }
    return false;
}

/* ---- 家庭设置 / Home settings ---- */
static void set_local(const char *s) {
    if (strncmp(s,"http://",7) && strncmp(s,"https://",8)) { ps_toast("地址须以 http:// 或 https:// 开头"); return; }
    copy_text(hc.local,sizeof(hc.local),s); save_home();
}
static void set_external(const char *s) {
    if (*s && strncmp(s,"https://",8)) { ps_toast("外部地址请使用 HTTPS"); return; }
    copy_text(hc.external,sizeof(hc.external),s); save_home();
}
static void settings_tabs(uint8_t *fb) {
    ps_view_t v=ps_view(); const char *labels[]={"连接", "实体筛选", "面板排布"};
    ps_box(fb,16,82,v.w-32,37,9,PS_THEME_INSET,true);
    for (int i=0;i<3;i++) ps_button(fb,16+i*(v.w-32)/3,83,(v.w-32)/3-5,34,labels[i],ha_tab==i);
}
static int filter_rows(void) { return ps_view().landscape?1:5; }
static int layout_rows(void) { return ps_view().landscape?1:5; }
static rect_t filter_entity_rect(int i) { return (rect_t){16,302+i*43,ps_view().w-32,41}; }
static void render_ha_connection(uint8_t *fb) {
    ps_view_t v=ps_view();
    if (v.landscape) {
        ps_wrap(fb,18,130,275,12,2,"连接流程为示例；不收集访问令牌。",MUTED);
        ps_button(fb,16,170,286,36,"编辑内网地址",false);
        ps_wrap(fb,24,215,270,11,2,hc.local,BLACK);
        ps_button(fb,16,254,286,36,"编辑外部 HTTPS 地址",false);
        ps_wrap(fb,24,302,270,10,1,*hc.external?hc.external:"尚未配置外部地址",MUTED);
        ps_button(fb,326,130,298,34,"选择家庭 Wi-Fi",false);
        ps_wrap(fb,335,171,280,11,1,*hc.home_wifi?hc.home_wifi:"未指定本地网络",BLACK);
        checkbox(fb,326,200,"失败时尝试外部地址",hc.external_enabled);
        checkbox(fb,326,229,"允许外部手动控制",hc.external_control);
        ps_button(fb,326,265,140,35,"示例自动发现",false);
        ps_button(fb,476,265,148,35,"模拟连接测试",false);
        return;
    }
    label_row(fb,128,"本地连接 Wi-Fi",*hc.home_wifi?hc.home_wifi:"从已记忆的 Wi-Fi 中选择");
    label_row(fb,196,"内网地址",hc.local);
    ps_button(fb,16,260,v.w-32,32,"自动发现本地服务器 · 示例",false);
    label_row(fb,302,"外部访问 HTTPS 地址",*hc.external?hc.external:"可选：https://你的 HA 域名");
    checkbox(fb,18,376,"内网失败时尝试外部地址",hc.external_enabled);
    checkbox(fb,18,412,"允许在外面手动使用家庭控制",hc.external_control);
    ps_button(fb,16,455,v.w-32,38,"模拟连接测试",false);
    ps_wrap(fb,18,507,v.w-36,11,4,"连接与发现均使用示例数据。仅已选家庭 Wi-Fi 且 HA 示例可用时启用家庭自动模式；外部可达不会被判断为人在家。预览不保存访问令牌。",MUTED);
}
static void render_filter(uint8_t *fb) {
    ps_view_t v=ps_view();
    ps_wrap(fb,18,128,v.w-36,11,2,"仅纳入开关、灯、空调、风扇和温湿度。单项排除优先，仅影响本终端。",MUTED);
    for (int i=0;i<6;i++) ps_button(fb,16+i*(v.w-32)/6,166,(v.w-32)/6-3,32,type_names[i],!hc.types||(hc.types&(1u<<i)));
    for (int i=0;i<5;i++) ps_button(fb,16+i*(v.w-32)/5,207,(v.w-32)/5-4,32,rooms[i],!hc.rooms||(hc.rooms&(1u<<i)));
    checkbox(fb,18,254,"自动排除不可用实体",hc.unavailable);
    ps_text(fb,v.w-144,258,10,"空选范围表示全部",MUTED);
    int per=filter_rows();
    for (int i=0;i<per && filter_page*per+i<ENTITY_COUNT;i++) {
        int id=filter_page*per+i; rect_t r=filter_entity_rect(i); char b[100];
        if (v.landscape) r.y=280;
        home_surface(fb,r.x,r.y,r.w,r.h,false);
        checkbox(fb,r.x+4,r.y+7,"",!(hc.hidden&(1u<<id)));
        snprintf(b,sizeof(b),"%s · %s",rooms[id/6],entity_names[id%6]); ps_text(fb,r.x+34,r.y+4,12,b,BLACK);
        snprintf(b,sizeof(b),"%s.demo_%d_%d%s",domains[id%6],id/6,id%6,entity_allowed(id)?"":" · 已排除");
        ps_wrap(fb,r.x+34,r.y+22,v.landscape?270:r.w-44,9,1,b,MUTED);
    }
    if (!v.landscape) page_bar(fb,filter_page,(ENTITY_COUNT+per-1)/per);
    else { char b[80]; snprintf(b,sizeof(b),"%d / 30 · 左右滑切换实体",filter_page+1); ps_text(fb,350,298,11,b,MUTED); }
}
static void render_layout(uint8_t *fb) {
    ps_view_t v=ps_view(); int half=(v.w-36)/2;
    ps_button(fb,16,132,half,34,hc.manual?"手动 Dashboard":"自动按房间分类",hc.manual);
    ps_button(fb,20+half,132,half,34,hc.name_sort?"按名称排序":"按房间 / 类型排序",hc.name_sort);
    ps_button(fb,16,176,v.w-32,32,hc.compact?"横屏紧凑：每页 12 项":"横屏舒适：每页 6 项",hc.compact);
    ps_wrap(fb,18,221,v.w-36,11,2,"移动即切换手动排布。自定义面板可跨房间组合；房间页始终自动分类。",MUTED);
    int per=layout_rows();
    for (int i=0;i<per && layout_page*per+i<ENTITY_COUNT;i++) {
        int p=layout_page*per+i,id=hc.order[p],y=272+i*48; char b[80];
        home_surface(fb,16,y,v.w-32,41,false);
        snprintf(b,sizeof(b),"%s · %s",rooms[id/6],entity_names[id%6]); ps_text(fb,26,y+12,12,b,BLACK);
        ps_button(fb,v.w-148,y,40,36,"↑",false); ps_button(fb,v.w-103,y,40,36,"↓",false);
        ps_button(fb,v.w-58,y,42,36,(hc.hidden&(1u<<id))?"显":"隐",false);
    }
    if (!v.landscape) page_bar(fb,layout_page,(ENTITY_COUNT+per-1)/per);
    else ps_text(fb,18,309,9,"左右滑动选择其他实体",MUTED);
}
static void render_ha_settings(uint8_t *fb) {
    ps_heading(fb,"Home Assistant",NULL);
    settings_tabs(fb);
    if(wifi_picker){
        ps_view_t v=ps_view();int per=v.landscape?4:8,n=ps_wifi_count();
        ps_text(fb,18,130,12,"选择本地连接使用的已记忆 Wi-Fi",MUTED);
        if(!n)ps_wrap(fb,18,177,v.w-36,14,3,"尚无已记忆网络，请先在设备的 Wi-Fi 设置中添加。",MUTED);
        for(int i=0;i<per&&wifi_choice_page*per+i<n;i++){int id=wifi_choice_page*per+i,y=163+i*(v.landscape?25:43);ps_button(fb,16,y,v.w-32,v.landscape?23:36,ps_wifi_name(id),!strcmp(hc.home_wifi,ps_wifi_name(id)));}
        if(n>per)page_bar(fb,wifi_choice_page,(n+per-1)/per);
        else ps_button(fb,16,v.h-88,v.w-32,34,"取消选择",false);
        return;
    }
    if (ha_tab==0) render_ha_connection(fb); else if (ha_tab==1) render_filter(fb); else render_layout(fb);
}
static void choose_home_wifi(void) {
    int n=ps_wifi_count();
    if (n<=0) { ps_toast("请先在 Wi-Fi 设置中记忆网络"); return; }
    wifi_picker=true;wifi_choice_page=0;
}
bool ps_home_local_network(void) {
    int i=ps_wifi_connected_index();
    return i>=0 && *link_config.home_wifi && !strcmp(link_config.home_wifi,ps_wifi_name(i)) && link_config.enabled && *link_config.ha_token && link_snapshot.result==RP_HOME_OK && !link_snapshot.external;
}
const char *ps_home_connection_label(void) {
    if(ps_wifi_connected_index()<0)return "离线阅读";
    if(link_config.enabled&&*link_config.ha_token){
        if(link_snapshot.busy)return "家庭连接中";
        if(link_snapshot.result==RP_HOME_OK)return link_snapshot.external?"外部家庭已连接":"本地家庭已连接";
    }
    return "Wi-Fi 已连接";
}
bool ps_home_service_configured(void) {
    if(!link_config.enabled)return false;
    if(*link_config.ha_token)return true;
    for(int i=0;i<RP_HOME_PRINTERS;i++)if(link_config.printers[i].source!=RP_HOME_NONE)return true;
    return false;
}
static bool connection_event(const ps_event_t *e) {
    ps_view_t v=ps_view(); if (e->type!=PS_TAP) return false;
    bool wifi=false,local=false,external=false,discover=false,test=false,ext_flag=false,outside=false;
    if (v.landscape) {
        wifi=ps_hit(e->x,e->y,326,130,298,62); local=ps_hit(e->x,e->y,16,170,286,77);
        external=ps_hit(e->x,e->y,16,254,286,36); discover=ps_hit(e->x,e->y,326,265,140,35);
        test=ps_hit(e->x,e->y,476,265,148,35); ext_flag=ps_hit(e->x,e->y,326,194,298,28); outside=ps_hit(e->x,e->y,326,225,298,28);
    } else {
        wifi=row_hit(e,128); local=row_hit(e,196); external=row_hit(e,302);
        discover=ps_hit(e->x,e->y,16,260,v.w-32,32); test=ps_hit(e->x,e->y,16,455,v.w-32,38);
        ext_flag=ps_hit(e->x,e->y,16,370,v.w-32,30); outside=ps_hit(e->x,e->y,16,406,v.w-32,30);
    }
    if (wifi) choose_home_wifi();
    else if (local) ps_edit("HA 内网地址",hc.local,sizeof(hc.local)-1,set_local);
    else if (external) ps_edit("HA 外部 HTTPS 地址",hc.external,sizeof(hc.external)-1,set_external);
    else if (ext_flag) { hc.external_enabled=!hc.external_enabled; save_home(); }
    else if (outside) { hc.external_control=!hc.external_control; save_home(); }
    else if (discover) { copy_text(hc.local,sizeof(hc.local),"http://homeassistant.local:8123"); save_home(); ps_toast("示例发现：homeassistant.local，未执行网络扫描"); }
    else if (test) {
        bool connected=ps_wifi_connected_index()>=0;
        ps_demo_connection(connected,connected);
        ps_toast(connected?"示例认证与状态同步成功；未实际连接 HA":"未连接 Wi-Fi，请先配置网络");
    } else return false;
    return true;
}
static bool ha_settings_event(const ps_event_t *e) {
    ps_view_t v=ps_view();
    if(wifi_picker){
        int per=v.landscape?4:8,n=ps_wifi_count(),total=n?(n+per-1)/per:1;
        int next=page_action(e,wifi_choice_page,total);if(next!=wifi_choice_page){wifi_choice_page=next;return true;}
        if(e->type!=PS_TAP)return false;
        if(n<=per&&ps_hit(e->x,e->y,16,v.h-88,v.w-32,34)){wifi_picker=false;return true;}
        for(int i=0;i<per&&wifi_choice_page*per+i<n;i++){int id=wifi_choice_page*per+i,y=163+i*(v.landscape?25:43);if(ps_hit(e->x,e->y,16,y,v.w-32,v.landscape?23:36)){copy_text(hc.home_wifi,sizeof(hc.home_wifi),ps_wifi_name(id));wifi_picker=false;save_home();return true;}}
        return false;
    }
    if (e->type==PS_TAP && ps_hit(e->x,e->y,16,83,v.w-32,34)) {
        ha_tab=clampi((e->x-16)/((v.w-32)/3),0,2); return true;
    }
    if (ha_tab==0) return connection_event(e);
    int per=ha_tab==1?filter_rows():layout_rows();
    int *page=ha_tab==1?&filter_page:&layout_page;
    int next=(v.landscape && e->type==PS_TAP)?*page:page_action(e,*page,(ENTITY_COUNT+per-1)/per);
    if (next!=*page) { *page=next; return true; }
    if (e->type!=PS_TAP) return false;
    if (ha_tab==1) {
        if (ps_hit(e->x,e->y,16,166,v.w-32,32)) hc.types^=(uint8_t)(1u<<clampi((e->x-16)/((v.w-32)/6),0,5));
        else if (ps_hit(e->x,e->y,16,207,v.w-32,32)) hc.rooms^=(uint8_t)(1u<<clampi((e->x-16)/((v.w-32)/5),0,4));
        else if (ps_hit(e->x,e->y,16,248,v.w-32,34)) hc.unavailable=!hc.unavailable;
        else {
            bool found=false;
            for (int i=0;i<per && *page*per+i<ENTITY_COUNT;i++) {
                rect_t r=filter_entity_rect(i); if(v.landscape) r.y=280;
                if (in_rect(e,r)) { hc.hidden^=1u<<(*page*per+i); found=true; break; }
            }
            if (!found) return false;
        }
    } else {
        int half=(v.w-36)/2;
        if (ps_hit(e->x,e->y,16,132,half,34)) hc.manual=!hc.manual;
        else if (ps_hit(e->x,e->y,20+half,132,half,34)) hc.name_sort=!hc.name_sort;
        else if (ps_hit(e->x,e->y,16,176,v.w-32,32)) hc.compact=!hc.compact;
        else {
            bool found=false;
            for (int i=0;i<per && *page*per+i<ENTITY_COUNT;i++) {
                int p=*page*per+i,y=272+i*48;
                if (ps_hit(e->x,e->y,v.w-148,y,40,36)) { if(p>0) {uint8_t t=hc.order[p];hc.order[p]=hc.order[p-1];hc.order[p-1]=t;hc.manual=1;} found=true; }
                else if (ps_hit(e->x,e->y,v.w-103,y,40,36)) { if(p<ENTITY_COUNT-1) {uint8_t t=hc.order[p];hc.order[p]=hc.order[p+1];hc.order[p+1]=t;hc.manual=1;} found=true; }
                else if (ps_hit(e->x,e->y,v.w-58,y,42,36)) {hc.hidden^=1u<<hc.order[p];found=true;}
                if (found) break;
            }
            if (!found) return false;
        }
    }
    dashboard_page=0; save_home(); return true;
}

/* ---- 打印监看 / Printer monitor ---- */
static bool printer_live(int id) {
    return pc.devices[id].active && (pc.devices[id].source==0?home_reachable():ps_wifi_connected_index()>=0) &&
        print_updated[id]>=0 && ps_now()-print_updated[id] <= (int64_t)pc.refresh*2000;
}
static int printer_selected(void) {
    if (pc.selected<PRINT_COUNT && pc.devices[pc.selected].active) return pc.selected;
    for (int i=0;i<PRINT_COUNT;i++) if (pc.devices[i].active) return i;
    return -1;
}
static int printer_free(void) { for(int i=0;i<PRINT_COUNT;i++) if(!pc.devices[i].active) return i; return -1; }
static bool printer_add(const printer_t *p) {
    for(int i=0;i<PRINT_COUNT;i++) if(pc.devices[i].active && pc.devices[i].source==p->source &&
        (p->source==0?!strcmp(pc.devices[i].serial,p->serial):(!strcmp(pc.devices[i].host,p->host)&&!strcmp(pc.devices[i].topic,p->topic)))) {
        pc.selected=(uint8_t)i; ps_toast("这台打印机已经添加"); return false;
    }
    int i=printer_free(); if(i<0) {ps_toast("示例最多监看 3 台，请先移除一台");return false;}
    pc.devices[i]=*p;pc.devices[i].active=1;pc.selected=(uint8_t)i;print_updated[i]=ps_now();save_print();return true;
}
static void begin_mqtt(void) {
    memset(&printer_draft,0,sizeof(printer_draft));printer_draft.source=1;printer_draft.tls=1;printer_draft.port=8883;
    copy_text(printer_draft.name,sizeof(printer_draft.name),"MQTT 打印机");
    copy_text(printer_draft.host,sizeof(printer_draft.host),"192.168.1.88");
    copy_text(printer_draft.serial,sizeof(printer_draft.serial),"PREVIEWP1S01");
    copy_text(printer_draft.topic,sizeof(printer_draft.topic),"device/PREVIEWP1S01/report");
    copy_text(printer_draft.username,sizeof(printer_draft.username),"bblp");
    printer_view=PV_MQTT;mqtt_page=0;
}
static void mqtt_name(const char *s) {copy_text(printer_draft.name,sizeof(printer_draft.name),s);}
static void mqtt_host(const char *s) {copy_text(printer_draft.host,sizeof(printer_draft.host),s);}
static void mqtt_serial(const char *s) {copy_text(printer_draft.serial,sizeof(printer_draft.serial),s);snprintf(printer_draft.topic,sizeof(printer_draft.topic),"device/%s/report",printer_draft.serial);}
static void mqtt_topic(const char *s) {copy_text(printer_draft.topic,sizeof(printer_draft.topic),s);}
static void mqtt_user(const char *s) {copy_text(printer_draft.username,sizeof(printer_draft.username),s);}
static void mqtt_port(const char *s) {char *end;long n=strtol(s,&end,10);if(*s && !*end && n>0 && n<=65535)printer_draft.port=(uint16_t)n;else ps_toast("端口范围为 1–65535");}
static bool mqtt_valid(const printer_t *p) {
    return *p->host && *p->serial && *p->topic && p->port && !strpbrk(p->host," \t\r\n/#") &&
        !strpbrk(p->topic,"+# \t\r\n") && (!p->broker?p->tls:true);
}
static void printer_subhead(uint8_t *fb,const char *title) {
    ps_view_t v=ps_view();ps_heading(fb,title,NULL);
    ps_button(fb,16,83,v.w-32,32,"返回打印机列表",false);
}
static void render_source(uint8_t *fb) {
    ps_view_t v=ps_view();printer_subhead(fb,"添加 3D 打印机");
    ps_button(fb,16,135,v.w-32,56,"从 Home Assistant 导入 · 推荐",true);
    ps_wrap(fb,24,204,v.w-48,12,3,"共用 HA 已接入的 Bambu Lab 打印机，终端无需额外占用打印机 MQTT 连接。",MUTED);
    ps_button(fb,16,v.landscape?262:300,v.w-32,46,"独立 MQTT 连接",false);
    if(!v.landscape)ps_wrap(fb,24,362,v.w-48,12,4,"可连接打印机局域网 MQTT，或独立服务器转发的状态报文。此原生演示只保存示例信息，不保存访问码或密码。",MUTED);
}
static void render_import(uint8_t *fb) {
    ps_view_t v=ps_view();printer_subhead(fb,"从 Home Assistant 导入");
    ps_wrap(fb,18,131,v.w-36,12,2,home_reachable()?"发现 2 台示例 Bambu Lab 打印机。仅打印监看使用，不改变家庭实体白名单。":"HA 示例连接不可用，请先检查 HA 设置。",MUTED);
    for(int i=0;i<2;i++) {int y=v.landscape?170+i*56:185+i*60;ps_box(fb,16,y,v.w-32,50,6,10,false);checkbox(fb,28,y+15,i?"书房 A1 mini · 已暂停":"工作室 P1S · 打印中",(import_mask&(1u<<i))!=0);}
    ps_button(fb,16,v.landscape?283:330,v.w-32,36,"导入所选示例打印机",true);
    if(!v.landscape)ps_button(fb,16,386,v.w-32,36,"打开 HA 连接设置",false);
}
static void render_mqtt(uint8_t *fb) {
    ps_view_t v=ps_view();printer_subhead(fb,"独立 MQTT 连接");char port[12];snprintf(port,sizeof(port),"%u",printer_draft.port);
    if(v.landscape) {
        ps_wrap(fb,18,130,v.w-36,11,2,"请竖屏编辑连接字段，或左右滑动查看分组。示例不保存密码。",MUTED);
        label_row(fb,169,mqtt_page?"设备序列号":"服务器地址",mqtt_page?printer_draft.serial:printer_draft.host);
        ps_button(fb,16,260,290,38,mqtt_page?"返回地址分组":"序列号与主题分组",false);
        ps_button(fb,326,260,298,38,"模拟连接并添加",true);return;
    }
    if(!mqtt_page) {
        ps_button(fb,16,130,v.w-32,34,printer_draft.broker?"独立 MQTT 服务器":"打印机局域网 MQTT",printer_draft.broker);
        label_row(fb,174,"设备名称",printer_draft.name);label_row(fb,243,"服务器地址",printer_draft.host);label_row(fb,312,"端口",port);
        checkbox(fb,18,390,"TLS 加密",printer_draft.tls);
        ps_wrap(fb,18,427,v.w-36,11,3,"打印机直连使用 TLS。正式连接需访问码或密码及证书校验；演示不保存真实凭据。",MUTED);
        ps_button(fb,16,505,v.w-32,38,"下一步：设备与订阅主题",false);
    } else {
        label_row(fb,128,"设备序列号",printer_draft.serial);label_row(fb,197,"用户名（可选）",printer_draft.username);label_row(fb,266,"订阅主题",printer_draft.topic);
        ps_wrap(fb,18,347,v.w-36,11,3,"只接受具体主题，不接受 + / # 通配。MQTT 仅提供状态数据，不含摄像头画面。",MUTED);
        ps_button(fb,16,427,v.w-32,36,"上一步：服务器地址",false);ps_button(fb,16,482,v.w-32,42,"模拟连接并添加",true);
    }
}
static void render_printer_info(uint8_t *fb,bool remove) {
    ps_view_t v=ps_view();printer_t *p=&pc.devices[info_printer];printer_subhead(fb,remove?"移除这台打印机？":"打印机连接详情");
    ps_text(fb,18,134,20,p->name,BLACK);
    if(remove) {
        ps_wrap(fb,18,179,v.w-36,14,3,"仅移除此终端的监看配置，打印任务继续运行。",MUTED);
        ps_button(fb,16,260,v.w-32,40,"移除监看",true);return;
    }
    ps_wrap(fb,18,178,v.w-36,12,2,p->source?"独立 MQTT · 示例服务器":"通过 Home Assistant 读取；阅读器不直连打印机。",MUTED);
    if(p->source) {char b[260];snprintf(b,sizeof(b),"%s:%u · %s",p->host,p->port,p->tls?"TLS":"局域网明文");ps_wrap(fb,18,225,v.w-36,12,2,b,BLACK);ps_wrap(fb,18,v.landscape?267:288,v.w-36,11,2,p->topic,MUTED);}
    else {char b[200];snprintf(b,sizeof(b),"sensor.bambu_%s_print_status",p->serial);ps_wrap(fb,18,225,v.w-36,12,2,b,BLACK);if(!v.landscape)ps_wrap(fb,18,287,v.w-36,12,4,"状态、进度、任务、剩余分钟、层数、喷嘴与热床温度按同一示例设备关联。任务图片独立于家庭实体筛选。",MUTED);}
}
static void printer_cover(uint8_t *fb,rect_t r) {
    bool wide=ps_view().landscape;int cx=wide?r.x+r.w/2:r.x+39,cy=wide?r.y+40:r.y+r.h/2,s=wide?30:23;
    ps_box(fb,r.x+2,r.y+2,r.w-4,r.h-4,8,PS_THEME_INSET,true);
    ps_line(fb,cx-s,cy-s/2,cx,cy-s,4);ps_line(fb,cx,cy-s,cx+s,cy-s/2,4);
    ps_line(fb,cx+s,cy-s/2,cx,cy,4);ps_line(fb,cx,cy,cx-s,cy-s/2,4);
    ps_line(fb,cx-s,cy-s/2,cx-s,cy+s/2,4);ps_line(fb,cx+s,cy-s/2,cx+s,cy+s/2,4);ps_line(fb,cx,cy,cx,cy+s,4);
    ps_line(fb,cx-s,cy+s/2,cx,cy+s,4);ps_line(fb,cx,cy+s,cx+s,cy+s/2,4);
    if(wide)ps_center(fb,r.x+r.w/2,r.y+r.h-22,10,"任务封面示意 · 示例模型",MUTED);
    else ps_text(fb,r.x+76,r.y+r.h/2-6,10,"任务封面示意 · 示例模型",MUTED);
}
static void render_printers(uint8_t *fb) {
    if(printer_view==PV_SOURCE){render_source(fb);return;}if(printer_view==PV_IMPORT){render_import(fb);return;}if(printer_view==PV_MQTT){render_mqtt(fb);return;}
    if(printer_view==PV_INFO||printer_view==PV_REMOVE){render_printer_info(fb,printer_view==PV_REMOVE);return;}
    ps_view_t v=ps_view();int id=printer_selected();
    ps_text(fb,18,38,21,"3D 打印监看",BLACK);ps_button(fb,v.w-78,34,60,34,"设置",false);
    if(id<0) {ps_center(fb,v.w/2,145,19,"把打印进度，放在手边。",BLACK);ps_button(fb,20,210,v.w-40,44,"从 Home Assistant 导入 · 推荐",true);ps_button(fb,20,270,v.w-40,40,"独立 MQTT 连接",false);if(!v.landscape)ps_wrap(fb,26,335,v.w-52,12,4,"支持多台 Bambu Lab 打印机。所有连接与打印数据均为示例。",MUTED);return;}
    printer_t *p=&pc.devices[id];bool live=printer_live(id);bool paused=p->source==0&&!strcmp(p->serial,"a1_mini");int progress=p->source?36:paused?72:46;char b[140];
    ps_button(fb,16,84,v.w-76,34,p->name,false);ps_button(fb,v.w-52,84,36,34,"＋",false);
    ps_text(fb,18,130,8,p->source?"MQTT · 示例数据":"Home Assistant · 示例数据",MUTED);
    ps_text(fb,v.w-90,130,8,live?"数据有效":"连接中断",MUTED);
    int main_w=v.landscape?350:v.w-32,x=30,y=v.landscape?155:164,inner_w=main_w-28;
    home_surface(fb,16,151,main_w,v.landscape?132:263,false);
    snprintf(b,sizeof(b),"%d%%",progress);ps_text(fb,x,y,v.landscape?40:56,b,PS_THEME_INK);
    ps_box(fb,x+147,y+19,82,28,8,PS_THEME_INSET,true);
    ps_center(fb,x+188,y+25,12,paused?"已暂停":"打印中",PS_THEME_INK);
    int py=v.landscape?202:238;
    ps_box(fb,x,py,inner_w,8,4,PS_THEME_INSET,true);
    ps_box(fb,x,py,inner_w*progress/100,8,4,PS_THEME_SELECTED,true);
    ps_text(fb,x,py+(v.landscape?16:23),v.landscape?12:17,p->source?"示例齿轮":paused?"阅读器支架":"桌面收纳盒",PS_THEME_INK);
    const char *labels[]={"剩余时间","打印层数","喷嘴温度","热床温度"};const char *values[]={"48 分钟","114 / 245","220°C","60°C"};
    for(int i=0;i<4;i++) {
        int cols=v.landscape?4:2,gap=v.landscape?6:8,mw=(inner_w-(cols-1)*gap)/cols;
        int mx=x+(i%cols)*(mw+gap),my=v.landscape?243:294+(i/cols)*54,mh=v.landscape?38:47;
        ps_box(fb,mx,my,mw,mh,7,PS_THEME_INSET,true);
        ps_text(fb,mx+7,my+4,8,labels[i],MUTED);
        ps_text(fb,mx+7,my+(v.landscape?19:23),v.landscape?10:14,values[i],PS_THEME_INK);
    }
    rect_t cover=v.landscape?(rect_t){386,158,236,110}:(rect_t){18,437,v.w-36,73};
    home_surface(fb,cover.x,cover.y,cover.w,cover.h,false);
    if(pc.image==0&&!p->source)printer_cover(fb,cover);
    else ps_wrap(fb,cover.x+12,cover.y+17,cover.w-24,11,3,pc.image==2?"图像已关闭":p->source?"MQTT 提供状态；图像可通过 HA 单独接入":"摄像头静态帧 · 演示未接入摄像头",MUTED);
    int foot=v.h-(v.landscape?74:102);
    ps_text(fb,18,foot+8,8,live?"示例监看 · 非实际打印数据":"上次数据 · 请检查连接",MUTED);
    ps_button(fb,v.w-106,foot,88,v.landscape?28:34,"刷新示例",false);
    if(paused&&!v.landscape)ps_text(fb,18,foot-20,11,"耗材已用尽，请检查料盘 · 示例",BLACK);
}
static void render_printer_settings(uint8_t *fb) {
    if(printer_view!=PV_LIST){render_printers(fb);return;}
    ps_view_t v=ps_view();ps_heading(fb,"3D 打印机设置",NULL);
    ps_button(fb,16,84,v.w-32,37,"添加打印机",true);
    int slot=0;
    for(int i=0;i<PRINT_COUNT;i++)if(pc.devices[i].active) {
        int y=139+slot*(v.landscape?56:76);printer_t *p=&pc.devices[i];
        home_surface(fb,16,y-8,v.w-32,v.landscape?49:64,false);
        ps_wrap(fb,26,y,v.w-192,12,1,p->name,PS_THEME_INK);
        ps_wrap(fb,26,y+23,v.w-192,8,1,p->source?"MQTT · Bambu Lab":"Home Assistant · Bambu Lab",MUTED);
        ps_button(fb,v.w-153,y,80,34,"连接详情",false);ps_button(fb,v.w-66,y,50,34,"移除",false);slot++;
    }
    if(!slot)ps_text(fb,18,150,13,"尚未添加打印机",MUTED);
    if(!v.landscape){const char *image[]={"任务封面","摄像头静态帧","关闭图像"};label_row(fb,380,"显示图像",image[pc.image]);char b[40];snprintf(b,sizeof(b),"%u 秒",pc.refresh);label_row(fb,449,"状态刷新间隔",b);ps_wrap(fb,18,525,v.w-36,11,3,"演示使用手动「刷新示例」，不进行真实网络请求。正式固件按变化刷新状态。",MUTED);}
    else ps_text(fb,18,309,9,"竖屏可调整图像与刷新间隔",MUTED);
}
static void import_printers(void) {
    if(!home_reachable()){ps_toast("请先连接 Home Assistant 示例");return;}
    for(int i=0;i<2;i++)if(import_mask&(1u<<i)){printer_t p={0};p.active=1;copy_text(p.name,sizeof(p.name),i?"书房 A1 mini":"工作室 P1S");copy_text(p.serial,sizeof(p.serial),i?"a1_mini":"p1s_lab");printer_add(&p);}
    printer_view=PV_LIST;ps_navigate(PS_PRINTERS);
}
static bool printer_event(const ps_event_t *e,bool settings) {
    ps_view_t v=ps_view();
    if(printer_view!=PV_LIST&&e->type==PS_TAP&&e->x<44&&e->y>=24&&e->y<76)return ps_home_key(-1);
    if(printer_view!=PV_LIST && e->type==PS_TAP && ps_hit(e->x,e->y,16,83,v.w-32,32)){printer_view=PV_LIST;return true;}
    if(printer_view==PV_SOURCE) {
        if(e->type!=PS_TAP)return false;
        printer_return=PV_SOURCE;
        if(ps_hit(e->x,e->y,16,135,v.w-32,56)){printer_view=PV_IMPORT;import_mask=3;return true;}
        if(ps_hit(e->x,e->y,16,v.landscape?262:300,v.w-32,46)){begin_mqtt();return true;}return false;
    }
    if(printer_view==PV_IMPORT) {
        if(e->type!=PS_TAP)return false;
        for(int i=0;i<2;i++)if(ps_hit(e->x,e->y,16,v.landscape?170+i*56:185+i*60,v.w-32,50)){import_mask^=(uint8_t)(1u<<i);return true;}
        if(ps_hit(e->x,e->y,16,v.landscape?283:330,v.w-32,36)){import_printers();return true;}
        if(!v.landscape && ps_hit(e->x,e->y,16,386,v.w-32,36)){ps_navigate(PS_HA_SETTINGS);return true;}return false;
    }
    if(printer_view==PV_MQTT) {
        if(e->type==PS_SWIPE_L||e->type==PS_SWIPE_R){mqtt_page=!mqtt_page;return true;}if(e->type!=PS_TAP)return false;
        if(v.landscape) {
            if(row_hit(e,169)){if(mqtt_page)ps_edit("打印机序列号",printer_draft.serial,39,mqtt_serial);else ps_edit("MQTT 服务器",printer_draft.host,95,mqtt_host);return true;}
            if(ps_hit(e->x,e->y,16,260,290,38)){mqtt_page=!mqtt_page;return true;}
        }else if(!mqtt_page) {
            if(ps_hit(e->x,e->y,16,130,v.w-32,34)){printer_draft.broker=!printer_draft.broker;copy_text(printer_draft.host,sizeof(printer_draft.host),printer_draft.broker?"mqtt.example.local":"192.168.1.88");copy_text(printer_draft.username,sizeof(printer_draft.username),printer_draft.broker?"":"bblp");if(!printer_draft.broker){printer_draft.tls=1;printer_draft.port=8883;}return true;}
            if(row_hit(e,174)){ps_edit("设备名称",printer_draft.name,39,mqtt_name);return true;}
            if(row_hit(e,243)){ps_edit("MQTT 服务器",printer_draft.host,95,mqtt_host);return true;}
            if(row_hit(e,312)){char b[12];snprintf(b,sizeof(b),"%u",printer_draft.port);ps_edit("MQTT 端口",b,5,mqtt_port);return true;}
            if(ps_hit(e->x,e->y,16,384,v.w-32,30)){if(printer_draft.broker){printer_draft.tls=!printer_draft.tls;printer_draft.port=printer_draft.tls?8883:1883;}else ps_toast("打印机局域网直连使用 TLS");return true;}
            if(ps_hit(e->x,e->y,16,505,v.w-32,38)){mqtt_page=1;return true;}
        }else {
            if(row_hit(e,128)){ps_edit("打印机序列号",printer_draft.serial,39,mqtt_serial);return true;}
            if(row_hit(e,197)){ps_edit("MQTT 用户名",printer_draft.username,31,mqtt_user);return true;}
            if(row_hit(e,266)){ps_edit("具体订阅主题",printer_draft.topic,127,mqtt_topic);return true;}
            if(ps_hit(e->x,e->y,16,427,v.w-32,36)){mqtt_page=0;return true;}
        }
        if(ps_hit(e->x,e->y,v.landscape?326:16,v.landscape?260:482,v.landscape?298:v.w-32,v.landscape?38:42)) {
            if(!mqtt_valid(&printer_draft))ps_toast("请检查服务器、端口、序列号和具体主题");
            else if(ps_wifi_connected_index()<0)ps_toast("请先连接 Wi-Fi");
            else if(printer_add(&printer_draft)){printer_view=PV_LIST;ps_navigate(PS_PRINTERS);ps_toast("已添加 MQTT 示例，未实际连接服务器");}
            return true;
        }return false;
    }
    if(printer_view==PV_REMOVE) {if(e->type==PS_TAP&&ps_hit(e->x,e->y,16,260,v.w-32,40)){memset(&pc.devices[info_printer],0,sizeof(printer_t));printer_view=PV_LIST;save_print();return true;}return false;}
    if(printer_view==PV_INFO)return false;
    if(e->type!=PS_TAP)return false;
    if(settings) {
        if(ps_hit(e->x,e->y,16,84,v.w-32,37)){printer_view=PV_SOURCE;return true;}
        int slot=0;for(int i=0;i<PRINT_COUNT;i++)if(pc.devices[i].active){int y=139+slot++*(v.landscape?56:76);
            if(ps_hit(e->x,e->y,v.w-153,y,80,34)){info_printer=i;printer_view=PV_INFO;return true;}
            if(ps_hit(e->x,e->y,v.w-66,y,50,34)){info_printer=i;printer_view=PV_REMOVE;return true;}}
        if(!v.landscape&&row_hit(e,380)){pc.image=(pc.image+1)%3;save_print();return true;}
        if(!v.landscape&&row_hit(e,449)){pc.refresh=pc.refresh==15?30:pc.refresh==30?60:15;save_print();return true;}return false;
    }
    if(ps_hit(e->x,e->y,v.w-78,34,60,34)){ps_navigate(PS_PRINTER_SETTINGS);return true;}
    int id=printer_selected();
    if(id<0){printer_return=PV_LIST;if(ps_hit(e->x,e->y,20,210,v.w-40,44)){printer_view=PV_IMPORT;import_mask=3;return true;}if(ps_hit(e->x,e->y,20,270,v.w-40,40)){begin_mqtt();return true;}return false;}
    if(ps_hit(e->x,e->y,v.w-52,84,36,34)){printer_view=PV_SOURCE;return true;}
    if(ps_hit(e->x,e->y,16,84,v.w-76,34)){for(int i=1;i<=PRINT_COUNT;i++)if(pc.devices[(id+i)%PRINT_COUNT].active){pc.selected=(uint8_t)((id+i)%PRINT_COUNT);save_print();break;}return true;}
    if(ps_hit(e->x,e->y,v.w-106,v.h-(v.landscape?74:102),88,v.landscape?28:34)){if(pc.devices[id].source==0?!home_reachable():ps_wifi_connected_index()<0)ps_toast("连接不可用，保留上次数据");else{print_updated[id]=ps_now();ps_toast("已刷新示例数据");}return true;}return false;
}

/* ---- 摘录缓存 / Excerpt cache ---- */
static uint32_t quote_hash(const char *text,const char *source) {
    uint32_t h=2166136261u;const unsigned char *p=(const unsigned char*)text;
    for(;*p;p++){h^=*p;h*=16777619u;}h^=0xff;h*=16777619u;
    for(p=(const unsigned char*)source;*p;p++){h^=*p;h*=16777619u;}return h?h:1;
}
static bool quote_read(int index,cached_quote_t *out) {
    if(!ps_reader_excerpt(index,out->text,sizeof(out->text),out->source,sizeof(out->source))||!*out->text)return false;
    out->text[sizeof(out->text)-1]=0;out->source[sizeof(out->source)-1]=0;out->id=quote_hash(out->text,out->source);return true;
}
static bool quote_find(uint32_t id,cached_quote_t *out) {
    if(!id)return false;
    int n=ps_reader_excerpt_count();
    for(int i=0;i<n;i++){if(quote_read(i,out)&&out->id==id)return true;}
    return false;
}
static bool queue_contains(uint32_t id) {if(qc.current==id)return true;for(int i=0;i<8;i++)if(qc.pending[i]==id)return true;return false;}
static void resolve_quote_cache(void) {
    if(!quote_find(qc.current,&qcache[0])){qc.current=0;memset(&qcache[0],0,sizeof(qcache[0]));}
    uint32_t keep[8]={0};int n=0;
    for(int i=0;i<8;i++)if(qc.pending[i]&&qc.pending[i]!=qc.current){cached_quote_t q={0};if(quote_find(qc.pending[i],&q)){bool duplicate=false;for(int j=0;j<n;j++)if(keep[j]==q.id)duplicate=true;if(!duplicate){keep[n]=q.id;qcache[n+1]=q;n++;}}}
    memcpy(qc.pending,keep,sizeof(keep));for(int i=n+1;i<9;i++)memset(&qcache[i],0,sizeof(qcache[i]));
}
static void refill_quotes(void) {
    cached_quote_t *candidates=quote_candidates;int used=0,seen=0,n=ps_reader_excerpt_count();
    int pending=0;while(pending<8&&qc.pending[pending])pending++;
    int need=8-pending;if(!qc.current)need++;
    for(int i=0;i<n;i++){cached_quote_t q={0};if(!quote_read(i,&q)||queue_contains(q.id))continue;bool duplicate=false;for(int j=0;j<used;j++)if(candidates[j].id==q.id)duplicate=true;if(duplicate)continue;
        seen++;if(used<need)candidates[used++]=q;else{uint32_t j=random_u32()%(uint32_t)seen;if(j<(uint32_t)need)candidates[j]=q;}}
    for(int i=used-1;i>0;i--){int j=(int)(random_u32()%(uint32_t)(i+1));cached_quote_t t=candidates[i];candidates[i]=candidates[j];candidates[j]=t;}
    for(int i=0;i<used;i++){if(!qc.current){qc.current=candidates[i].id;qcache[0]=candidates[i];}else if(pending<8){qc.pending[pending]=candidates[i].id;qcache[pending+1]=candidates[i];pending++;}}
}
static void advance_quote(bool refill) {
    resolve_quote_cache();uint32_t old=qc.current;
    if(qc.pending[0]){qc.current=qc.pending[0];qcache[0]=qcache[1];for(int i=0;i<7;i++){qc.pending[i]=qc.pending[i+1];qcache[i+1]=qcache[i+2];}qc.pending[7]=0;memset(&qcache[8],0,sizeof(qcache[8]));}
    else if(refill){qc.current=0;memset(&qcache[0],0,sizeof(qcache[0]));refill_quotes();if(qc.current==old&&qc.pending[0]){uint32_t id=qc.current;cached_quote_t q=qcache[0];qc.current=qc.pending[0];qcache[0]=qcache[1];qc.pending[0]=id;qcache[1]=q;}}
    if(refill&&!qc.pending[0])refill_quotes();
    quote_segment=0;save_queue();
}
static bool power_paused(bool previous) {
    int p=ps_battery_percent();if(ps_charging()||p>=15)return false;if(p>=0&&p<=10)return true;return previous;
}
static size_t utf8_next(const char *s,size_t p) {if(!s[p])return p;unsigned char c=(unsigned char)s[p];size_t n=c<0x80?1:c<0xe0?2:c<0xf0?3:4;for(size_t i=1;i<n;i++)if(!s[p+i]||((unsigned char)s[p+i]&0xc0)!=0x80)return p+1;return p+n;}
static int quote_chars(const char *s) {int n=0;for(size_t p=0;s[p];p=utf8_next(s,p))n++;return n;}
static int quote_chars_per_segment(void) {return ps_view().landscape?100:120;}
static int quote_segments(void) {int n=quote_chars(qcache[0].text),per=quote_chars_per_segment();return n?(n+per-1)/per:1;}
static void quote_slice(char *out,size_t bytes) {
    const char *s=qcache[0].text;int per=quote_chars_per_segment(),start=quote_segment*per;size_t a=0,b;
    for(int i=0;i<start&&s[a];i++){a=utf8_next(s,a);}
    b=a;
    for(int i=0;i<per&&s[b];i++)b=utf8_next(s,b);
    size_t len=b-a;if(len>=bytes)len=bytes-1;memcpy(out,s+a,len);out[len]=0;
}
static size_t quote_line(const char *text,char *line,size_t bytes,int width,int size) {
    size_t at=0,previous=0;int pixels=(width*19+5)/10,px=(size*19+5)/10;
    while(text[at]){
        size_t next=utf8_next(text,at);if(next>=bytes)break;
        if(text[at]=='\n'){line[at]=0;return next;}
        memcpy(line+at,text+at,next-at);line[next]=0;
        if(at&&ttf_text_width_px(px,line)>pixels){
            char ch[5]={0};size_t len=next-at;if(len<sizeof(ch))memcpy(ch,text+at,len);
            if(previous&&strstr("，。！？；：、）》」』】〉…",ch)){line[previous]=0;return previous;}
            line[at]=0;return at;
        }
        previous=at;at=next;
    }
    line[at]=0;return at;
}
static int quote_rows(const char *text,int width,int size) {
    char line[QUOTE_BYTES];int rows=0;
    while(*text){size_t count=quote_line(text,line,sizeof(line),width,size);if(!count)break;text+=count;rows++;}
    return rows?rows:1;
}
static void quote_source(uint8_t *fb,int margin,int y) {
    ps_view_t v=ps_view();int width=v.w-2*margin,size=12;const char *text=qcache[0].source;char line[SOURCE_BYTES];
    while(size>9&&quote_rows(text,width,size)>2)size--;
    ps_line(fb,margin,y-14,v.w-margin,y-14,PS_THEME_BORDER);
    for(int row=0;*text&&row<2;row++){
        size_t count=quote_line(text,line,sizeof(line),width,size);if(!count)break;
        int pixels=ttf_text_width_px((size*19+5)/10,line),logical=(pixels*10+18)/19;
        ps_body_text(fb,v.w-margin-logical,y+row*(size+7),size,line,PS_THEME_SECONDARY);text+=count;
    }
}
static void wake_icons(uint8_t *fb,bool quotes) {
    ps_view_t v=ps_view();int count=quotes&&quote_segments()>1?6:4,step=46,w=count*step+12,x=(v.w-w)/2,y=v.h-((quotes||clock_quotes)&&quote_paused?115:72);
    ps_box(fb,x,y,w,46,8,WHITE,true);ps_box(fb,x,y,w,46,8,8,false);
    for(int i=0;i<count;i++){ps_icon_t icon=i==0?PI_BOOK:i==1?PI_HOME:i==count-2?PI_SETTINGS:i==count-1?PI_CLOCK:i==2?PI_MINUS:PI_PLUS;ps_icon(fb,x+13+i*step,y+11,24,icon,BLACK);}
}
static void render_quotes(uint8_t *fb) {
    ps_view_t v=ps_view();ps_fill(fb,0,0,v.w,v.h,WHITE);
    if(!qc.current){ps_center(fb,v.w/2,v.h/2-38,20,"让读过的文字，陪你片刻",BLACK);ps_wrap(fb,40,v.h/2+12,v.w-80,14,3,"在阅读中高亮喜欢的段落，下次休眠便会自动随机展示。",MUTED);}
    else {char text[QUOTE_BYTES],b[60];quote_slice(text,sizeof(text));int chars=quote_chars(text),size=chars<30?32:chars<65?26:22;
        int margin=v.w/11,body_top=v.landscape?78:135,source_y=v.landscape?277:544,body_h=source_y-40-body_top,width=v.w-margin*2;
        int rows=quote_rows(text,width,size);while(size>16&&rows*(size+7)-7>body_h){size--;rows=quote_rows(text,width,size);}
        int body_y=body_top+(body_h-(rows*(size+7)-7))/2;
        ps_text(fb,v.w/11,v.landscape?24:42,11,"划线摘录",MUTED);ps_text(fb,v.w/11,v.landscape?45:84,52,"“",PS_THEME_BORDER);
        const char *remaining=text;char line[QUOTE_BYTES];
        for(int row=0;*remaining&&row<rows;row++){size_t count=quote_line(remaining,line,sizeof(line),width,size);if(!count)break;ps_body_text(fb,margin,body_y+row*(size+7),size,line,PS_THEME_INK);remaining+=count;}
        quote_source(fb,margin,source_y);
        if(quote_segments()>1){snprintf(b,sizeof(b),"第 %d / %d 段",quote_segment+1,quote_segments());ps_text(fb,v.w-130, v.landscape?25:43,10,b,MUTED);}}
    if(quote_paused){char b[100];int p=ps_battery_percent();if(p<0)copy_text(b,sizeof(b),"低电量 · 已暂停自动刷新，请充电");else snprintf(b,sizeof(b),"电量不足（%d%%）· 已暂停自动刷新，请充电",p);int y=v.h-39;ps_fill(fb,0,y,v.w,39,WHITE);ps_box(fb,12,y+2,v.w-24,31,4,6,false);ps_center(fb,v.w/2,y+12,11,b,BLACK);}
    if(ps_now()<wake_until)wake_icons(fb,true);
}

/* ---- 翻页时钟 / Flip clock ---- */
static void clock_sample(bool animate) {
    struct tm tm={0};bool valid=ps_wall_time(&tm);int values[2]={valid?tm.tm_hour:12,valid?tm.tm_min:34};
    clock_valid=valid;
    if(values[0]!=clock_values[0]||values[1]!=clock_values[1]){memcpy(clock_old,clock_values,sizeof(clock_old));memcpy(clock_values,values,sizeof(clock_values));clock_flip_start=ps_now();clock_frame=animate?0:6;}
}
static void clipped_fill(uint8_t *fb,rect_t clip,int x,int y,int w,int h,int gray) {
    int left=x>clip.x?x:clip.x,top=y>clip.y?y:clip.y;
    int right=x+w<clip.x+clip.w?x+w:clip.x+clip.w,bottom=y+h<clip.y+clip.h?y+h:clip.y+clip.h;
    if(right>left&&bottom>top)ps_fill(fb,left,top,right-left,bottom-top,gray);
}
// 在目标尺寸混合四邻采样，保留灰度边缘，避免放大mask时出现粗方块。
// Blend four neighbours at the target size to keep grayscale edges instead of enlarged mask blocks.
static int clock_alpha(int value,int x,int y) {
    x=clampi(x,0,PS_CLOCK_MASK_W-1);y=clampi(y,0,PS_CLOCK_MASK_H-1);
    unsigned row=(unsigned)value*PS_CLOCK_MASK_H+(unsigned)y;
    unsigned start=ps_clock_row_offset[row],length=ps_clock_row_offset[row+1]-start;
    int at=x/2-ps_clock_row_left[row];
    return at<0||(unsigned)at>=length?0:(ps_clock_row_data[start+(unsigned)at]>>((x%2)*4))&15u;
}
static void digit(uint8_t *fb,int value,rect_t r,rect_t clip,int gray) {
    if(r.w<=0||r.h<=0)return;
    int mask=clampi(value,0,9);
    int top=r.y>clip.y?r.y:clip.y,bottom=r.y+r.h<clip.y+clip.h?r.y+r.h:clip.y+clip.h;
    for(int y=top;y<bottom;y++){
        int fy=((2*(y-r.y)+1)*PS_CLOCK_MASK_H*128)/r.h-128,sy=fy>=0?fy/256:-1,wy=fy-sy*256;
        int run=0,last=-1;
        for(int x=0;x<=r.w;x++){
            int level=-1;
            if(x<r.w){int fx=((2*x+1)*PS_CLOCK_MASK_W*128)/r.w-128,sx=fx>=0?fx/256:-1,wx=fx-sx*256;
                int upper=clock_alpha(mask,sx,sy)*(256-wx)+clock_alpha(mask,sx+1,sy)*wx;
                int lower=clock_alpha(mask,sx,sy+1)*(256-wx)+clock_alpha(mask,sx+1,sy+1)*wx;
                level=((upper*(256-wy)+lower*wy)*gray+491520)/983040;}
            if(level!=last){if(last>0)clipped_fill(fb,clip,r.x+run,y,x-run,1,last);run=x;last=level;}
        }
    }
}

static void clock_glyph(uint8_t *fb,int value,rect_t r,rect_t clip) {
    int gap=r.w/28,pad=r.w/10,dw=(r.w-pad*2-gap)/2;int dh=r.h*2/3,dy=r.y+(r.h-dh)/2;
    digit(fb,value/10,(rect_t){r.x+pad,dy,dw,dh},clip,WHITE);digit(fb,value%10,(rect_t){r.x+pad+dw+gap,dy,dw,dh},clip,WHITE);
}
static void clock_tile(uint8_t *fb,int index,rect_t r) {
    bool flipping=clock_frame<6&&clock_values[index]!=clock_old[index];
    ps_box(fb,r.x,r.y,r.w,r.h,10,BLACK,true);
    if(flipping){clock_glyph(fb,clock_values[index],r,(rect_t){r.x,r.y,r.w,r.h/2});clock_glyph(fb,clock_old[index],r,(rect_t){r.x,r.y+r.h/2,r.w,r.h-r.h/2});}
    else clock_glyph(fb,clock_values[index],r,r);
    if(clock_frame<6&&clock_values[index]!=clock_old[index]){
        int half=r.h/2;rect_t leaf;
        if(clock_frame<3){int height=half*(3-clock_frame)/3;leaf=(rect_t){r.x,r.y+half-height,r.w,height};ps_fill(fb,leaf.x,leaf.y,leaf.w,leaf.h,clock_frame?2:BLACK);clock_glyph(fb,clock_old[index],r,leaf);}
        else {int height=half*(clock_frame-2)/3;leaf=(rect_t){r.x,r.y+half,r.w,height};ps_fill(fb,leaf.x,leaf.y,leaf.w,leaf.h,2);clock_glyph(fb,clock_values[index],r,leaf);}
    }
    ps_fill(fb,r.x,r.y+r.h/2-1,r.w,3,WHITE);ps_fill(fb,r.x,r.y+r.h/2-5,3,10,WHITE);ps_fill(fb,r.x+r.w-3,r.y+r.h/2-5,3,10,WHITE);
}
// 时钟摘录仅绘制缓存预览；长段落以省略号提示，完整内容在摘录样式查看。
// Clock excerpts paint a cached preview only; ellipses mark long text, available fully in the excerpt style.
static void render_clock_quote(uint8_t *fb) {
    ps_view_t v=ps_view();int margin=v.w/16,top=v.h*(quote_paused?61:70)/100;
    int source_y=v.h-(quote_paused?76:38),width=v.w-2*margin,size=v.landscape?14:18;
    int rows=(source_y-top-20)/(size+7);if(rows<1)rows=1;
    if(!qc.current){ps_wrap(fb,margin,top,width,12,2,"在阅读中添加划线，休眠时自动展示",MUTED);return;}
    char text[QUOTE_BYTES],line[QUOTE_BYTES];quote_slice(text,sizeof(text));const char *remaining=text;
    for(int row=0;*remaining&&row<rows;row++){
        size_t count=quote_line(remaining,line,sizeof(line)-8,width-2*size,size);if(!count)break;
        if(row+1==rows&&(remaining[count]||quote_segments()>1))strcat(line,"…");
        ps_body_text(fb,margin,top+row*(size+7),size,line,PS_THEME_INK);remaining+=count;
    }
    (void)quote_line(qcache[0].source,line,sizeof(line)-8,width-24,10);
    if(strlen(line)<strlen(qcache[0].source))strcat(line,"…");
    ps_line(fb,margin,source_y-7,v.w-margin,source_y-7,PS_THEME_BORDER);
    ps_body_text(fb,margin,source_y,10,line,MUTED);
}
static void render_clock_low_power(uint8_t *fb) {
    if(!clock_quotes||!quote_paused)return;
    ps_view_t v=ps_view();int y=v.h-37;
    ps_fill(fb,0,y,v.w,37,WHITE);ps_box(fb,12,y+2,v.w-24,31,4,6,false);
    ps_center(fb,v.w/2,y+11,11,"低电量 · 已暂停自动刷新，请充电",BLACK);
}
static void render_clock(uint8_t *fb) {
    ps_view_t v=ps_view();ps_fill(fb,0,0,v.w,v.h,WHITE);int x=v.w*4/100,y=v.h*5/100,w=v.w*92/100,h=v.h*90/100,gap=v.landscape?v.w*3/100:v.h*3/100;
    if(clock_quotes){y=v.h*3/100;h=v.h*(quote_paused?55:65)/100;}
    for(int i=0;i<2;i++){rect_t r=v.landscape?(rect_t){x+i*(w+gap)/2,y,(w-gap)/2,h}:(rect_t){x,y+i*(h+gap)/2,w,(h-gap)/2};clock_tile(fb,i,r);}
    if(clock_quotes)render_clock_quote(fb);
    render_clock_low_power(fb);
    if(!clock_valid&&!(clock_quotes&&quote_paused)){ps_fill(fb,v.w/2-90,v.h-22,180,19,WHITE);ps_center(fb,v.w/2,v.h-18,10,"示例时间 · 时间尚未同步",MUTED);}
    if(ps_now()<wake_until)wake_icons(fb,false);
}
static bool standby_event(const ps_event_t *e,bool quotes) {
    if(e->type!=PS_TAP&&e->type!=PS_SWIPE_L&&e->type!=PS_SWIPE_R)return false;
    int count=quotes&&quote_segments()>1?6:4;ps_view_t v=ps_view();int step=46,w=count*step+12,x=(v.w-w)/2,y=v.h-((quotes||clock_quotes)&&quote_paused?115:72);
    if(ps_now()>=wake_until){wake_until=ps_now()+8000;return true;}
    if(quotes&&(e->type==PS_SWIPE_L||e->type==PS_SWIPE_R)){int n=quote_segments();quote_segment=(quote_segment+(e->type==PS_SWIPE_L?1:n-1))%n;wake_until=ps_now()+8000;return true;}
    if(e->type==PS_TAP&&ps_hit(e->x,e->y,x,y,w,46)){int i=clampi((e->x-x-6)/step,0,count-1);
        if(i==0){ps_wake();ps_switch(0);}else if(i==1){ps_wake();ps_switch(1);}else if(i==count-2){ps_wake();ps_navigate(PS_SLEEP_SETTINGS);}else if(i==count-1)wake_until=0;
        else {int n=quote_segments();quote_segment=(quote_segment+(i==2?n-1:1))%n;wake_until=ps_now()+8000;}return true;}
    wake_until=ps_now()+8000;return true;
}
static int sleep_detail_y(void){return ps_view().landscape?180:256;}
static void render_sleep_settings(uint8_t *fb) {
    ps_view_t v=ps_view();ps_heading(fb,"锁屏外观",NULL);
    const char *styles[]={"翻页时钟","划线摘录","藏书封面","保持当前页面","自定义图片","灵犀画板"};
    int cols=v.landscape?3:2,half=(v.w-32-(cols-1)*8)/cols,h=v.landscape?38:44,style=ps_sleep_style();
    for(int i=0;i<6;i++)ps_button(fb,16+(i%cols)*(half+8),84+(i/cols)*(h+8),half,h,styles[i],style==i);
    int y=sleep_detail_y();
    if(style==2){
        const char *layouts[]={"全屏单封面","双封面","书库拼贴","留白海报","全屏书墙"};int cols=v.landscape?3:2,cw=(v.w-32-(cols-1)*6)/cols;
        ps_text(fb,18,y,13,"封面排布",MUTED);
        for(int i=0;i<5;i++)ps_button(fb,16+(i%cols)*(cw+6),y+28+(i/cols)*44,cw,40,layouts[i],ps_power_cover_layout()==i);
    }else if(style==4){
        ps_text(fb,18,y,14,ps_lock_image_available()?"自定义图片已保存":"尚未添加自定义图片",BLACK);
        ps_wrap(fb,18,y+28,v.w-36,12,v.landscape?2:4,"在多屏灵犀的设备侧栏上传锁屏图片，或将标准BMP放入 /readpico/lockscreen/custom.bmp。图片保留在TF卡，下次锁屏生效。",MUTED);
    }else if(style==5){
        ps_wrap(fb,18,y,v.w-36,13,v.landscape?3:5,"外接电源时显示灵犀画板，并保留网络接收更新。阅读和画画时仅保存，不切换页面。拔掉电源后停止联网，保留画面进入低功耗。未接电或尚无画板时保留当前页面。",MUTED);
    }else if(style==3){
        ps_wrap(fb,18,y,v.w-36,14,v.landscape?3:4,"保留锁屏前的内容，叠加已锁屏提示。锁屏期间停止页面交互，唤醒后恢复原页面。",MUTED);
    }else{
        if(!style){checkbox(fb,18,y,"时钟同时显示划线摘录",clock_quotes);y+=v.landscape?36:44;}
        if(style==1||clock_quotes){
            ps_text(fb,18,y,13,"摘录自动轮换间隔",MUTED);y+=v.landscape?22:28;
            for(int i=0;i<6;i++){int cols=v.landscape?6:3,cw=(v.w-32-(cols-1)*6)/cols;ps_button(fb,16+(i%cols)*(cw+6),y+(i/cols)*44,cw,v.landscape?32:38,interval_names[i],ps_excerpt_interval_minutes()==intervals[i]);}
        }
    }
    if(!v.landscape)ps_wrap(fb,18,488,v.w-36,12,3,"锁屏沿用当前屏幕方向。自动锁屏时间、唤醒方式与方向锁定在上一级设置。",MUTED);
    ps_button(fb,16,v.h-82,v.w-32,40,"预览当前锁屏",false);
}
static bool sleep_settings_event(const ps_event_t *e) {
    if(e->type!=PS_TAP)return false;
    ps_view_t v=ps_view();int cols=v.landscape?3:2,half=(v.w-32-(cols-1)*8)/cols,h=v.landscape?38:44;
    for(int i=0;i<6;i++)if(ps_hit(e->x,e->y,16+(i%cols)*(half+8),84+(i/cols)*(h+8),half,h)){if(i==4&&!ps_lock_image_prepare()){ps_toast("请先从多屏灵犀上传锁屏图片，或检查TF卡");return true;}ps_set_sleep_style(i);return true;}
    if(ps_hit(e->x,e->y,16,v.h-82,v.w-32,40)){ps_power_preview(ps_sleep_style());return true;}
    int y=sleep_detail_y(),style=ps_sleep_style();
    if(style==2){int cols=v.landscape?3:2,cw=(v.w-32-(cols-1)*6)/cols;for(int i=0;i<5;i++)if(ps_hit(e->x,e->y,16+(i%cols)*(cw+6),y+28+(i/cols)*44,cw,40)){ps_power_set_cover_layout(i);return true;}}
    else if(style<=1){
        if(!style){if(ps_hit(e->x,e->y,18,y,v.w-36,30)){uint8_t next=!clock_quotes;uint8_t previous=clock_quotes;clock_quotes=next;if(!save_home_value(3))clock_quotes=previous;return true;}y+=v.landscape?36:44;}
        y+=v.landscape?22:28;
        if(style==1||clock_quotes)for(int i=0;i<6;i++){int cols=v.landscape?6:3,cw=(v.w-32-(cols-1)*6)/cols;if(ps_hit(e->x,e->y,16+(i%cols)*(cw+6),y+(i/cols)*44,cw,v.landscape?32:38)){ps_set_excerpt_interval(intervals[i]);return true;}}
    }
    return false;
}

/* ---- 页面生命周期 / Page lifecycle ---- */
bool ps_home_storage_save(void) {
    bool ok=true;
    for(int i=0;i<4;i++)if(!save_home_value(i))ok=false;
    return ok;
}
#if defined(PICO_HOME_LINK_FULL) && PICO_HOME_LINK_FULL
#include "pico_home_link_ui.inc"
#else
#include "pico_home_link_ui_thin.inc"
#endif

void ps_home_init(void) {
    ps_i18n_init();
    ps_home_storage_invalidate();
    real_manage_close();real_printer_cancel();
    read_pico_home_link_init();read_pico_home_link_get_config(&link_config);
    memset(&hc,0,sizeof(hc));hc.magic=HOME_MAGIC;hc.compact=1;hc.unavailable=1;hc.on=1;hc.types=63;hc.rooms=31;
    copy_text(hc.local,sizeof(hc.local),"http://homeassistant.local:8123");hc.external_enabled=1;
    for(int i=0;i<ENTITY_COUNT;i++){hc.order[i]=(uint8_t)i;available[i]=1;}
    for(int i=0;i<ROOM_COUNT;i++)hc.climate_temp[i]=26;
    home_config_t loaded;if(ps_load("home2",&loaded,sizeof(loaded))&&loaded.magic==HOME_MAGIC){bool valid=true;uint32_t seen=0;for(int i=0;i<ENTITY_COUNT;i++){int id=loaded.order[i];if(id>=ENTITY_COUNT||(seen&(1u<<id))){valid=false;break;}seen|=1u<<id;}if(valid)hc=loaded;}
    hc.local[sizeof(hc.local)-1]=0;hc.external[sizeof(hc.external)-1]=0;hc.home_wifi[sizeof(hc.home_wifi)-1]=0;hc.types&=63;hc.rooms&=31;
    for(int i=0;i<ROOM_COUNT;i++){hc.climate_temp[i]=(int8_t)clampi(hc.climate_temp[i],16,30);hc.climate_mode[i]%=4;}
    memset(&pc,0,sizeof(pc));pc.magic=PRINT_MAGIC;pc.refresh=30;print_config_t lp;if(ps_load("print2",&lp,sizeof(lp))&&lp.magic==PRINT_MAGIC)pc=lp;
    pc.selected%=PRINT_COUNT;pc.image%=3;if(pc.refresh!=15&&pc.refresh!=30&&pc.refresh!=60)pc.refresh=30;
    for(int i=0;i<PRINT_COUNT;i++){printer_t *p=&pc.devices[i];p->name[39]=0;p->host[95]=0;p->serial[39]=0;p->topic[127]=0;p->username[31]=0;p->source=!!p->source;p->broker=!!p->broker;p->tls=!!p->tls;print_updated[i]=-1;}
    memset(&qc,0,sizeof(qc));qc.magic=QUOTE_MAGIC;quote_config_t lq;if(ps_load("quotes2",&lq,sizeof(lq))&&lq.magic==QUOTE_MAGIC)qc=lq;
    clock_quotes=0;uint8_t cq=0;if(ps_load("clockquote",&cq,sizeof(cq))&&cq<=1)clock_quotes=cq;
    rng_state=(uint32_t)ps_now()^0x9e3779b9u;if(!rng_state)rng_state=1;
    clock_values[0]=-1;clock_values[1]=-1;clock_frame=6;entered_route=PS_ROUTE_COUNT;
    memset(&link_ui,0,sizeof(link_ui));link_ui.magic=0x484c5531u;
    if(!ps_load("home-real-ui",&link_ui,sizeof(link_ui))||link_ui.magic!=0x484c5531u){memset(&link_ui,0,sizeof(link_ui));link_ui.magic=0x484c5531u;}
    for(int i=0;i<RP_HOME_ENTITIES;i++)if(link_ui.order[i]>=RP_HOME_ENTITIES)link_ui.order[i]=(uint8_t)i;
}
void ps_home_enter(ps_route_t route) {
    ps_route_t previous=entered_route;bool changed=route!=previous;entered_route=route;
    if(changed&&real_manage)real_manage_close();
    if(route==PS_SLEEP_SETTINGS&&changed){(void)ps_lock_image_prepare();ps_lock_image_release();}
    if(route==PS_HA){int list[ENTITY_COUNT],n=entity_list(list),per=entities_per_page();dashboard_page=clampi(dashboard_page,0,n?(n-1)/per:0);if(changed)climate_entity=-1;}
    if((route==PS_PRINTERS||route==PS_PRINTER_SETTINGS)&&changed&&previous!=PS_EDITOR){printer_view=PV_LIST;printer_return=PV_LIST;real_printer_cancel();}
    if(route==PS_CLOCK&&changed){wake_until=0;clock_sample(false);if(!clock_valid){clock_old[0]=clock_values[0];clock_old[1]=(clock_values[1]+59)%60;clock_frame=0;clock_flip_start=ps_now();}}
    if((route==PS_QUOTES||(route==PS_CLOCK&&clock_quotes))&&changed){wake_until=0;quote_paused=power_paused(false);advance_quote(!quote_paused);quote_due=ps_now()+(int64_t)ps_excerpt_interval_minutes()*60000;}
    if(route==PS_CLOCK&&clock_quotes&&quote_paused)clock_frame=6;
    if(route==PS_QUOTES)quote_segment=clampi(quote_segment,0,quote_segments()-1);
}
bool ps_home_launcher_visible(void) {
    return ps_route()==PS_HA?(!real_room_picker&&climate_entity<0):
           ps_route()==PS_PRINTERS?(!real_manage&&!real_import):false;
}
void ps_home_render(uint8_t *fb) {
    ps_view_t v=ps_view();
    if(ps_route()!=PS_CLOCK&&ps_route()!=PS_QUOTES)ps_fill(fb,0,0,v.w,v.h,PS_THEME_CANVAS);
    switch(ps_route()) {
#ifdef PICO_HOME_FIXTURE_CONTRACT
    case PS_HA:render_ha(fb);break;case PS_HA_SETTINGS:render_ha_settings(fb);break;case PS_PRINTERS:render_printers(fb);break;case PS_PRINTER_SETTINGS:render_printer_settings(fb);break;
#else
    case PS_HA:real_ha_render(fb);break;case PS_HA_SETTINGS:real_ha_settings_render(fb);break;case PS_PRINTERS:real_printer_render(fb,false);break;case PS_PRINTER_SETTINGS:real_printer_render(fb,true);break;
#endif
    case PS_SLEEP_SETTINGS:render_sleep_settings(fb);break;case PS_CLOCK:render_clock(fb);break;case PS_QUOTES:render_quotes(fb);break;default:break;}
}
bool ps_home_animating(void) { return ps_route()==PS_CLOCK&&clock_frame<6; }
void ps_home_action(int action) {
    if(real_manage)real_manage_close();
    if(action==2){ps_navigate(PS_HA_SETTINGS);ha_tab=1;filter_page=0;wifi_picker=false;}
    else {ps_navigate(PS_HA);room_picker=action==1;climate_entity=-1;if(action==0){section=0;dashboard_page=0;}}
    mark_dirty();
    if(action==1){real_room_picker=true;real_rooms_page=0;}
    else if(action==0){real_room=real_page=0;real_room_picker=false;}
}
bool ps_home_event(const ps_event_t *e) {
    bool redraw=false;switch(ps_route()){
#ifdef PICO_HOME_FIXTURE_CONTRACT
    case PS_HA:redraw=ha_event(e);break;case PS_HA_SETTINGS:redraw=ha_settings_event(e);break;case PS_PRINTERS:redraw=printer_event(e,false);break;case PS_PRINTER_SETTINGS:redraw=printer_event(e,true);break;
#else
    case PS_HA:redraw=real_ha_event(e);break;case PS_HA_SETTINGS:redraw=real_ha_settings_event(e);break;case PS_PRINTERS:redraw=real_printer_event(e,false);break;case PS_PRINTER_SETTINGS:redraw=real_printer_event(e,true);break;
#endif
    case PS_SLEEP_SETTINGS:redraw=sleep_settings_event(e);break;case PS_CLOCK:redraw=standby_event(e,false);break;case PS_QUOTES:redraw=standby_event(e,true);break;default:break;}
    if(redraw)mark_dirty();
    return redraw;
}
bool ps_home_key(int key) {
    ps_route_t route=ps_route();if(route==PS_CLOCK||route==PS_QUOTES){wake_until=ps_now()+8000;mark_dirty();return true;}
    if(key==-1){
        if(route==PS_HA&&real_room_picker){real_room_picker=false;return true;}
        if((route==PS_PRINTERS||route==PS_PRINTER_SETTINGS)&&real_import){real_import=false;return true;}
        if(real_manage){real_manage_close();return true;}
        if(route==PS_HA&&climate_entity>=0){climate_entity=-1;return true;}
        if(route==PS_HA&&room_picker){room_picker=false;return true;}
        if(route==PS_HA_SETTINGS&&wifi_picker){wifi_picker=false;return true;}
        if((route==PS_PRINTERS||route==PS_PRINTER_SETTINGS)&&printer_view!=PV_LIST){
            printer_view=(printer_view==PV_IMPORT||printer_view==PV_MQTT)?printer_return:PV_LIST;printer_return=PV_LIST;return true;
        }
        return false;
    }
    if(key!=0&&key!=2)return false;
    ps_event_t e={.type=key==0?PS_SWIPE_R:PS_SWIPE_L};return ps_home_event(&e);
}
bool ps_home_tick(void) {
    ps_route_t route=ps_route();bool redraw=false;int64_t now=ps_now();
    if(real_manage){read_pico_management_tick();redraw=real_manage_sync();}
    if(route==PS_HA||route==PS_HA_SETTINGS||route==PS_PRINTERS||route==PS_PRINTER_SETTINGS){
        int wifi=ps_wifi_connected_index();
        bool at_home=wifi>=0&&*link_config.home_wifi&&!strcmp(link_config.home_wifi,ps_wifi_name(wifi));
        if(now>=link_due){read_pico_home_link_request(wifi>=0,at_home);link_due=now+30000;}
        uint32_t revision=read_pico_home_link_revision();
        if(revision!=link_revision){read_pico_home_link_get_config(&link_config);read_pico_home_link_get_snapshot(&link_snapshot);link_revision=revision;link_sync_ui();int list[RP_HOME_ENTITIES],n=link_entities(list,false);real_page=clampi(real_page,0,n?(n-1)/entities_per_page():0);redraw=true;}
    }
    if(route!=PS_CLOCK&&route!=PS_QUOTES&&entered_route!=route){if(real_manage)real_manage_close();entered_route=route;}
    bool excerpts=route==PS_QUOTES||(route==PS_CLOCK&&clock_quotes);
    if(excerpts){bool paused=power_paused(quote_paused);if(paused!=quote_paused){quote_paused=paused;if(paused)clock_frame=6;else quote_due=now+(int64_t)ps_excerpt_interval_minutes()*60000;redraw=true;}if(!quote_paused&&quote_due>0&&now>=quote_due){advance_quote(true);quote_due=now+(int64_t)ps_excerpt_interval_minutes()*60000;redraw=true;}}
    if(route==PS_CLOCK&&!(clock_quotes&&quote_paused)){int previous[2]={clock_values[0],clock_values[1]};bool prior_valid=clock_valid;clock_sample(true);if(previous[0]!=clock_values[0]||previous[1]!=clock_values[1]||prior_valid!=clock_valid)redraw=true;int f=clampi((int)((now-clock_flip_start)/140),0,6);if(clock_frame<6&&f!=clock_frame){clock_frame=f;redraw=true;}}

    if((route==PS_CLOCK||route==PS_QUOTES)&&wake_until>0&&now>=wake_until){wake_until=0;redraw=true;}
    if(route==PS_PRINTERS){for(int i=0;i<PRINT_COUNT;i++){bool live=printer_live(i);if(live!=print_was_live[i]){print_was_live[i]=live;redraw=true;}}}
    if(redraw)mark_dirty();
    return redraw;
}

// 同一组锁屏绘制与轮换供产品控制器使用，不切换页面。/ Share standby drawing without switching the active page.
void ps_home_standby_begin(int style) {
    wake_until=0;
    if(style==0)clock_sample(false);
    if(style==1||(style==0&&clock_quotes)){
        quote_paused=power_paused(false);resolve_quote_cache();bool empty=!qc.current;
        if(empty&&!quote_paused)advance_quote(true);
        int64_t now=ps_now(),interval=(int64_t)ps_excerpt_interval_minutes()*60000;
        if(empty||quote_due<=0)quote_due=now+interval;
        else if(!quote_paused&&now>=quote_due){advance_quote(true);quote_due=now+interval;}
        save_queue();
    }
}
void ps_home_standby_render(uint8_t *fb,int style) { if(style==1)render_quotes(fb);else render_clock(fb); }
bool ps_home_standby_tick(int style) {
    int64_t now=ps_now();bool changed=false;
    if(style==1||(style==0&&clock_quotes)){
        bool paused=power_paused(quote_paused);
        if(paused!=quote_paused){quote_paused=paused;changed=true;}
        if(!quote_paused&&now>=quote_due){advance_quote(true);quote_due=now+(int64_t)ps_excerpt_interval_minutes()*60000;changed=true;}
    }
    if(style==0){int a=clock_values[0],b=clock_values[1],f=clock_frame;bool valid=clock_valid;clock_sample(true);clock_frame=clampi((int)((now-clock_flip_start)/140),0,6);changed|=a!=clock_values[0]||b!=clock_values[1]||f!=clock_frame||valid!=clock_valid;}
    return changed;
}

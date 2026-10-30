/* SPDX-License-Identifier: Apache-2.0
 * 中文：执行生产薄宿主生命周期，验证旧家庭状态彻底隔离且时钟/摘录仍可持久化。
 * English: Exercise the production thin host; isolate legacy home state while retaining clock/excerpt persistence.
 */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#define PICO_HOME_LINK_FULL 0
#include "../main/system/pico_home.c"

static ps_route_t route=PS_SHELF;
static ps_view_t view={360,640,false};
static int64_t now=10000;
static int wifi=-1, loads, stores, bridge_peeks, legacy_calls, opens, portal_calls, draws, excerpt_reads;
static bool bridge_ready, allow_store=true;
static const char *failed_key;
static pico_app_home_status_t bridge;
static uint8_t entry;
static int sleep_style;
static int cover_layout;
static struct {bool set;size_t size;unsigned char data[64];} persisted[2];

static int record(const char *key) {
    if(!strcmp(key,"quotes2"))return 0;
    if(!strcmp(key,"clockquote"))return 1;
    // 旧键的读取与保存均禁止；保留卡内既有文件，不把默认值回写它们。
    // Legacy keys must never be read or written; existing card files remain untouched.
    fprintf(stderr,"unexpected legacy record: %s\n",key);assert(0);return -1;
}
bool ps_load(const char *key,void *out,size_t size){loads++;int i=record(key);if(!persisted[i].set)return false;assert(size==persisted[i].size);memcpy(out,persisted[i].data,size);return true;}
bool ps_store(const char *key,const void *data,size_t size){stores++;int i=record(key);if(failed_key&&!strcmp(key,failed_key))return false;assert(size<=sizeof(persisted[i].data));memcpy(persisted[i].data,data,size);persisted[i].size=size;persisted[i].set=true;return true;}
bool ps_storage_save_allowed(void){return allow_store;}
void ps_i18n_init(void){}
const char *ps_tr(const char *key){return key;}
void ps_i18n_chip(uint8_t *fb,int x,int y){(void)fb;(void)x;(void)y;}
bool ps_i18n_chip_hit(int x,int y,int rx,int ry){(void)x;(void)y;(void)rx;(void)ry;return false;}
bool ps_home_pga_peek_status(pico_app_home_status_t *out){bridge_peeks++;*out=bridge;return bridge_ready;}
void read_pico_home_link_init(void){legacy_calls++;}
void read_pico_home_link_get_config(rp_home_config_t *out){legacy_calls++;memset(out,0,sizeof(*out));}
void read_pico_home_link_get_snapshot(rp_home_snapshot_t *out){legacy_calls++;memset(out,0,sizeof(*out));}
uint32_t read_pico_home_link_revision(void){legacy_calls++;return 1;}
bool read_pico_home_link_request(bool online,bool home){(void)online;(void)home;legacy_calls++;return true;}
ps_view_t ps_view(void){return view;}
ps_route_t ps_route(void){return route;}
int64_t ps_now(void){return now;}
void ps_navigate(ps_route_t next){route=next;ps_home_enter(next);}
void ps_switch(int section){ps_navigate(section==1?PS_HA:PS_SHELF);}
void ps_wake(void){}
void ps_dirty(void){}
void ps_toast(const char *text){assert(text&&*text);}
int ps_wifi_connected_index(void){return wifi;}
const char *ps_wifi_name(int index){(void)index;return "fixture-wifi";}
int ps_sleep_style(void){return sleep_style;}
void ps_set_sleep_style(int value){sleep_style=value;}
int ps_power_cover_layout(void){return cover_layout;}
void ps_power_set_cover_layout(int value){cover_layout=value;}
void ps_power_preview(int style){(void)style;}
bool ps_lock_image_prepare(void){return true;}
bool ps_lock_image_available(void){return true;}
void ps_lock_image_release(void){}
int ps_excerpt_interval_minutes(void){return 5;}
void ps_set_excerpt_interval(int minutes){(void)minutes;}
int ps_battery_percent(void){return 90;}
bool ps_charging(void){return false;}
bool ps_wall_time(struct tm *out){*out=(struct tm){.tm_hour=12,.tm_min=34};return true;}
int ps_reader_excerpt_count(void){return 2;}
bool ps_reader_excerpt(int index,char *text,size_t size,char *source,size_t source_size){excerpt_reads++;snprintf(text,size,"excerpt %d",index);snprintf(source,source_size,"book %d",index);return true;}
void ps_apps_prepare_home(uint8_t page){entry=page;}
void ps_apps_expect_home_route(ps_route_t next){assert(next==route);}
bool ps_apps_open(const char *id){assert(!strcmp(id,"home-link"));opens++;return true;}
bool ps_home_portal_start(int owner){(void)owner;portal_calls++;return false;}
void ps_home_portal_stop(void){portal_calls++;}
void ps_home_portal_stop_if_home(void){portal_calls++;}
bool ps_home_portal_tick(void){return false;}
void ps_home_portal_view(ps_home_portal_view_t *out){memset(out,0,sizeof(*out));}
bool ui_wifi_qr_prepare_url(const char *url){(void)url;portal_calls++;return true;}
void ui_wifi_qr_clear(void){portal_calls++;}
void ui_wifi_qr_draw(uint8_t *fb,EpdRect area){(void)fb;(void)area;draws++;}
EpdRect ps_rect(int x,int y,int w,int h){return (EpdRect){x,y,w,h};}
bool ps_hit(int x,int y,int rx,int ry,int w,int h){return x>=rx&&y>=ry&&x<rx+w&&y<ry+h;}
static void bounds(int x,int y,int w,int h){assert(x>=0&&y>=0&&w>0&&h>0&&x+w<=view.w&&y+h<=view.h);draws++;}
void ps_fill(uint8_t *fb,int x,int y,int w,int h,int gray){(void)fb;assert(gray>=0&&gray<=15);bounds(x,y,w,h);}
void ps_box(uint8_t *fb,int x,int y,int w,int h,int radius,int gray,bool fill){(void)radius;(void)fill;ps_fill(fb,x,y,w,h,gray);}
void ps_line(uint8_t *fb,int x0,int y0,int x1,int y1,int gray){(void)fb;(void)gray;assert(x0>=0&&x0<view.w&&x1>=0&&x1<view.w&&y0>=0&&y0<view.h&&y1>=0&&y1<view.h);draws++;}
void ps_text(uint8_t *fb,int x,int y,int size,const char *text,int gray){(void)fb;(void)gray;assert(text);bounds(x,y,1,size);}
void ps_center(uint8_t *fb,int x,int y,int size,const char *text,int gray){ps_text(fb,x,y,size,text,gray);}
void ps_body_text(uint8_t *fb,int x,int y,int size,const char *text,int gray){ps_text(fb,x,y,size,text,gray);}
void ps_wrap(uint8_t *fb,int x,int y,int w,int size,int rows,const char *text,int gray){(void)rows;assert(w>0&&x+w<=view.w);ps_text(fb,x,y,size,text,gray);}
void ps_icon(uint8_t *fb,int x,int y,int size,ps_icon_t icon,int gray){(void)icon;ps_fill(fb,x,y,size,size,gray);}
void ps_button(uint8_t *fb,int x,int y,int w,int h,const char *text,bool selected){(void)selected;assert(text);ps_fill(fb,x,y,w,h,0);}
void ps_heading(uint8_t *fb,const char *title,const char *subtitle){(void)subtitle;ps_text(fb,18,34,20,title,0);}
int ttf_text_width_px(int px,const char *text){return (int)strlen(text)*px/2;}

static void storage_contract(void){
    ps_home_init();assert(loads==2&&stores==0&&legacy_calls==0&&bridge_peeks==0);
    assert(HOME_SAVE_COUNT==2&&sizeof(saved_home)==sizeof(qc)+sizeof(clock_quotes));
    assert(ps_home_storage_save()&&stores==2);
    assert(ps_home_storage_save()&&stores==2);
    allow_store=false;assert(!ps_home_storage_save()&&stores==2);allow_store=true;
    ps_home_storage_invalidate();failed_key="clockquote";
    assert(!ps_home_storage_save()&&stores==4);
    failed_key=NULL;assert(ps_home_storage_save()&&stores==5);
    clock_quotes=1;qc.current=42;assert(ps_home_storage_save()&&stores==7);
    ps_home_init();assert(clock_quotes==1&&qc.current==42&&loads==4&&legacy_calls==0);
}
static void status_contract(void){
    bridge_ready=false;wifi=-1;
    assert(!ps_home_local_network()&&!ps_home_service_configured());
    assert(!strcmp(ps_home_connection_label(),"离线阅读"));
    wifi=0;assert(!strcmp(ps_home_connection_label(),"Wi-Fi 已连接"));
    bridge_ready=true;bridge=(pico_app_home_status_t){.enabled=1,.has_token=1,.at_home=1,.result=RP_HOME_OK,.flags=PGA_HOME_WIFI};
    assert(ps_home_local_network()&&ps_home_service_configured());
    assert(!strcmp(ps_home_connection_label(),"本地家庭已连接"));
    bridge.external=1;assert(!ps_home_local_network());assert(!strcmp(ps_home_connection_label(),"外部家庭已连接"));
    bridge.busy=1;assert(!strcmp(ps_home_connection_label(),"家庭连接中"));
    bridge=(pico_app_home_status_t){.enabled=1,.printer_count=1};assert(ps_home_service_configured());
    assert(!legacy_calls);
}
static void routes_contract(void){
    const ps_route_t routes[]={PS_HA,PS_HA_SETTINGS,PS_PRINTERS,PS_PRINTER_SETTINGS};
    const uint8_t entries[]={THIN_HOME_MONITOR,THIN_HOME_SETUP,THIN_HOME_PRINTERS,THIN_HOME_PRINTERS};
    for(int landscape=0;landscape<2;landscape++){
        view=landscape?(ps_view_t){640,360,true}:(ps_view_t){360,640,false};
        for(unsigned i=0;i<4;i++){
            ps_navigate(PS_SHELF);int opened=opens;ps_navigate(routes[i]);assert(opens==opened+1&&entry==entries[i]);
            int read=loads,written=stores,portal=portal_calls,loaded=opens;
            ps_home_render(NULL);
            for(int ticks=0;ticks<3;ticks++){now+=31000;(void)ps_home_tick();}
            assert(read==loads&&written==stores&&portal==portal_calls&&loaded==opens&&legacy_calls==0);
            assert(!ps_home_key(-1));
        }
        ps_navigate(PS_SLEEP_SETTINGS);sleep_style=0;
        int written=stores,read=loads,excerpts=excerpt_reads;
        ps_home_render(NULL);assert(written==stores&&read==loads&&excerpt_reads==excerpts);
        ps_navigate(PS_CLOCK);written=stores;read=loads;excerpts=excerpt_reads;
        ps_home_render(NULL);assert(written==stores&&read==loads&&excerpt_reads==excerpts);
    }
}
int main(void){storage_contract();status_contract();routes_contract();assert(draws>0);puts("thin home: no legacy storage/services, two owned records, checkpoint retries, cached status, four routes and portrait/landscape pure rendering PASS");return 0;}

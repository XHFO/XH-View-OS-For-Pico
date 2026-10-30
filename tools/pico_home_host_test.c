/* 家庭界面契约检查，全部硬件/网络由桩替代。/ Home contract checks with hardware/network doubles. */
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#define PICO_HOME_FIXTURE_CONTRACT 1
#include "../main/system/pico_home.c"

// 完整宿主契约中的门户/语言服务不启动外部任务。/ Full-host tests leave portal/locale services inert.
void ps_i18n_init(void) {}
void ps_home_portal_stop(void) {}
void ps_home_portal_stop_if_home(void) {}
bool ps_home_portal_tick(void) { return false; }

static ps_route_t test_route;
static ps_view_t test_view={360,640,false};
static int64_t test_now=10000;
static bool test_online=true,test_charging,test_time_valid=true,test_locked;
static int test_battery=86,test_style,test_interval=5,test_hour=12,test_minute=34;
static int stores,toasts,draws,excerpt_reads;
static int test_cover_layout,test_preview_style=-1;
static int test_wifi=0,test_excerpt_count=13,deleted_excerpt=-1;
static char last_toast[200];
static void (*edit_done)(const char*);
typedef struct {char key[16];size_t bytes;unsigned char data[2048];} store_t;
static store_t saved[8];
static int saved_count;
static bool test_storage_allowed=true;
static const char *test_failed_store;
static rp_home_config_t test_saved_link;
static bool test_link_fail;
static int test_link_stores;
static uint32_t test_link_revision;
static read_pico_management_status_t test_management;
static int management_starts, management_stops, management_reads, management_ticks, qr_prepares, qr_clears, qr_draws;
bool read_pico_management_start(void) {
    management_starts++;
    memset(&test_management, 0, sizeof(test_management));
    if (test_wifi < 0) { snprintf(test_management.error, sizeof(test_management.error), "请先连接 Wi-Fi"); return false; }
    test_management.active = true; test_management.seconds_left = 900;
    snprintf(test_management.host, sizeof(test_management.host), "http://192.0.2.4");
    snprintf(test_management.url, sizeof(test_management.url), "http://192.0.2.4/manage#key=%032d", management_starts);
    return true;
}
void read_pico_management_stop(void) { management_stops++; memset(&test_management, 0, sizeof(test_management)); }
void read_pico_management_get_status(read_pico_management_status_t *out) { management_reads++; *out = test_management; }
void read_pico_management_tick(void) {
    management_ticks++;
    if (test_management.active && (!test_management.seconds_left || test_wifi < 0))
        read_pico_management_stop();
}
#ifndef PICO_HOME_REAL_QR
bool ui_wifi_qr_prepare_url(const char *url) { assert(strstr(url, "/manage#key=")); qr_prepares++; return true; }
bool ui_wifi_qr_prepare(const char *ssid, const char *password) { (void)ssid; (void)password; return false; }
void ui_wifi_qr_clear(void) { qr_clears++; }
void ui_wifi_qr_draw(uint8_t *fb, EpdRect area) { (void)fb; assert(area.x >= 0 && area.y >= 0 && area.width == area.height); qr_draws++; }
#endif

void read_pico_home_link_init(void){}
void read_pico_home_link_get_config(rp_home_config_t *out){*out=test_saved_link;if(!out->version)out->version=RP_HOME_CONFIG_VERSION;}
bool read_pico_home_link_request(bool online,bool home){(void)online;(void)home;return true;}
uint32_t read_pico_home_link_revision(void){return test_link_revision;}
void read_pico_home_link_get_snapshot(rp_home_snapshot_t *out){memset(out,0,sizeof(*out));}
bool read_pico_home_link_configure(const rp_home_config_t *config){if(test_link_fail)return false;test_saved_link=*config;test_link_stores++;return true;}
bool read_pico_home_link_control(const rp_home_entity_t *entity,const char *action,float value,bool home){(void)entity;(void)action;(void)value;(void)home;return true;}
const char *rp_home_result_text(rp_home_result_t result){(void)result;return "测试连接状态";}

ps_view_t ps_view(void){return test_view;}
ps_route_t ps_route(void){return test_route;}
int64_t ps_now(void){return test_now;}
void ps_navigate(ps_route_t r){test_route=r;ps_home_enter(r);}
void ps_switch(int section_id){ps_navigate(section_id==1?PS_HA:PS_SHELF);}
void ps_back(void){ps_navigate(PS_DEVICE);}
void ps_sleep(void){ps_navigate(test_style?PS_QUOTES:PS_CLOCK);}
void ps_wake(void){ps_navigate(PS_SHELF);}
void ps_rotate(void){test_view=(ps_view_t){test_view.h,test_view.w,!test_view.landscape};}
void ps_rotation_lock(bool locked){test_locked=locked;}
bool ps_rotation_locked(void){return test_locked;}
void ps_toast(const char *s){toasts++;snprintf(last_toast,sizeof(last_toast),"%s",s);}
void ps_dirty(void){}
bool ps_load(const char *key,void *out,size_t bytes){for(int i=0;i<saved_count;i++)if(!strcmp(key,saved[i].key)&&saved[i].bytes==bytes){memcpy(out,saved[i].data,bytes);return true;}return false;}
bool ps_storage_save_allowed(void){return test_storage_allowed;}
bool ps_store(const char *key,const void *data,size_t bytes){stores++;if(test_failed_store&&!strcmp(key,test_failed_store))return false;assert(bytes<=sizeof(saved[0].data));int i;for(i=0;i<saved_count;i++)if(!strcmp(key,saved[i].key))break;if(i==saved_count)saved_count++;assert(saved_count<=8);snprintf(saved[i].key,16,"%s",key);saved[i].bytes=bytes;memcpy(saved[i].data,data,bytes);return true;}
void ps_edit(const char *title,const char *initial,size_t max_bytes,void(*done)(const char*)){assert(title&&initial&&max_bytes);edit_done=done;}
void ps_edit_password(const char *title,size_t max_bytes,void(*done)(const char*)){ps_edit(title,"",max_bytes,done);}
void ps_edit_secret(const char *title,size_t max_bytes,void(*done)(const char*)){ps_edit(title,"",max_bytes,done);}
const char *ps_network_label(void){return test_wifi>=0?"示例家庭":"离线";}
bool ps_demo_home_online(void){return test_online;}
void ps_demo_connection(bool wifi,bool ha){test_online=wifi&&ha;}
int ps_power_cover_layout(void){return test_cover_layout;}
void ps_power_set_cover_layout(int layout){assert(layout>=0&&layout<=4);test_cover_layout=layout;}
bool ps_lock_image_prepare(void){return true;}
bool ps_lock_image_available(void){return true;}
void ps_lock_image_release(void){}
void ps_power_preview(int style){assert(style>=0&&style<=5);test_preview_style=style;}
int ps_sleep_style(void){return test_style;}
int ps_excerpt_interval_minutes(void){return test_interval;}
void ps_set_sleep_style(int s){test_style=s;}
void ps_set_excerpt_interval(int minutes){test_interval=minutes;}
bool ps_wall_time(struct tm *out){*out=(struct tm){.tm_hour=test_hour,.tm_min=test_minute};return test_time_valid;}
int ps_battery_percent(void){return test_battery;}
bool ps_charging(void){return test_charging;}
int ps_wifi_count(void){return 2;}
const char *ps_wifi_name(int index){return index==0?"Home":"Office";}
int ps_wifi_connected_index(void){return test_wifi;}
int ps_reader_excerpt_count(void){return test_excerpt_count;}
bool ps_reader_excerpt(int index,char *quote,size_t bytes,char *source,size_t source_bytes){excerpt_reads++;if(index==deleted_excerpt)return false;snprintf(quote,bytes,"第 %d 条划线：在安静的午后，把喜欢的文字留在手边。",index);snprintf(source,source_bytes,"《河岸来信》 · 第 %d 章",index+1);return true;}

#ifndef PICO_HOME_REAL_RENDER
int ttf_text_width_px(int px,const char *text){int n=0;for(;*text;text++)if(((unsigned char)*text&0xc0)!=0x80)n+=(unsigned char)*text<128?px/2:px;return n;}
static void bounds(int x,int y,int w,int h){assert(x>=0&&y>=0&&w>0&&h>0);if(x+w>test_view.w||y+h>test_view.h){fprintf(stderr,"bounds route=%d view=%dx%d rect=%d,%d %dx%d\n",test_route,test_view.w,test_view.h,x,y,w,h);assert(0);}draws++;}
EpdRect ps_rect(int x,int y,int w,int h){return (EpdRect){x,y,w,h};}
bool ps_hit(int x,int y,int rx,int ry,int rw,int rh){return x>=rx&&y>=ry&&x<rx+rw&&y<ry+rh;}
void ps_fill(uint8_t *fb,int x,int y,int w,int h,int gray){(void)fb;assert(gray>=0&&gray<=15);bounds(x,y,w,h);}
void ps_line(uint8_t *fb,int x0,int y0,int x1,int y1,int gray){(void)fb;(void)gray;assert(x0>=0&&x0<test_view.w&&x1>=0&&x1<test_view.w&&y0>=0&&y0<test_view.h&&y1>=0&&y1<test_view.h);draws++;}
void ps_box(uint8_t *fb,int x,int y,int w,int h,int radius,int gray,bool filled){(void)fb;(void)radius;(void)gray;(void)filled;bounds(x,y,w,h);}
void ps_text(uint8_t *fb,int x,int y,int size,const char *s,int gray){(void)fb;(void)gray;assert(s);bounds(x,y,1,size);}
void ps_body_text(uint8_t *fb,int x,int y,int size,const char *s,int gray){ps_text(fb,x,y,size,s,gray);}
void ps_center(uint8_t *fb,int x,int y,int size,const char *s,int gray){ps_text(fb,x,y,size,s,gray);}
void ps_wrap(uint8_t *fb,int x,int y,int w,int size,int lines,const char *s,int gray){(void)lines;ps_text(fb,x,y,size,s,gray);assert(w>0&&x+w<=test_view.w);}
void ps_icon(uint8_t *fb,int x,int y,int size,ps_icon_t icon,int gray){(void)fb;(void)icon;(void)gray;bounds(x,y,size,size);}
void ps_button(uint8_t *fb,int x,int y,int w,int h,const char *label,bool selected){(void)fb;(void)label;(void)selected;bounds(x,y,w,h);}
void ps_heading(uint8_t *fb,const char *title,const char *subtitle){ps_text(fb,18,34,20,title,0);if(subtitle)ps_text(fb,18,81,12,subtitle,6);}
void ps_tabs(uint8_t *fb,int selected){(void)fb;(void)selected;assert(0&&"home content must not draw persistent workspace tabs");}
#endif

static bool event(ps_event_type_t type,int x,int y){ps_event_t e={.type=type,.x=x,.y=y};return ps_home_event(&e);}
static bool tap(int x,int y){return event(PS_TAP,x,y);}
static void enter(ps_route_t r){test_route=r;ps_home_enter(r);}
static void render_pure(void){home_config_t a=hc;print_config_t b=pc;quote_config_t c=qc;cached_quote_t cache[QUOTE_CACHE];memcpy(cache,qcache,sizeof(cache));int n=stores,reads=excerpt_reads;ps_home_render(NULL);assert(!memcmp(&a,&hc,sizeof(a))&&!memcmp(&b,&pc,sizeof(b))&&!memcmp(&c,&qc,sizeof(c))&&!memcmp(cache,qcache,sizeof(cache)));assert(stores==n&&excerpt_reads==reads);}
static void check_queue(void){uint32_t ids[9]={qc.current};memcpy(ids+1,qc.pending,sizeof(qc.pending));for(int i=0;i<9;i++)if(ids[i])for(int j=0;j<i;j++)assert(ids[i]!=ids[j]);}
static void sleep_appearance_contract(void){
    for(int orientation=0;orientation<2;orientation++){
        test_view=orientation?(ps_view_t){640,360,true}:(ps_view_t){360,640,false};
        enter(PS_SLEEP_SETTINGS);bool locked=test_locked;
        int cols=orientation?3:2,half=(test_view.w-32-(cols-1)*8)/cols,h=orientation?38:44,y=orientation?180:256;
        for(int style=0;style<5;style++){
            assert(tap(16+(style%cols)*(half+8)+half/2,84+(style/cols)*(h+8)+h/2)&&test_style==style);
            render_pure();test_preview_style=-1;assert(tap(40,test_view.h-62)&&test_preview_style==style);
            if(style==2){int cols=orientation?3:2,cw=(test_view.w-32-(cols-1)*6)/cols;for(int layout=0;layout<5;layout++){assert(tap(16+(layout%cols)*(cw+6)+cw/2,y+28+(layout/cols)*44+20));assert(test_cover_layout==layout);render_pure();}}
            if(style==0){bool before=clock_quotes;assert(tap(40,y+12)&&clock_quotes!=before);assert(tap(40,y+12)&&clock_quotes==before);}
            if(style==1){int cols=orientation?6:3,cw=(test_view.w-32-(cols-1)*6)/cols,base=y+(orientation?22:28);for(int i=0;i<6;i++){assert(tap(16+(i%cols)*(cw+6)+cw/2,base+(i/cols)*44+16)&&test_interval==intervals[i]);}}
            assert(test_locked==locked);
        }
    }
    test_style=0;test_view=(ps_view_t){360,640,false};
}

static void real_ui_contract(void) {
    memset(&link_snapshot, 0, sizeof(link_snapshot)); climate_entity = -1; real_room_picker = false;
    link_snapshot.entity_count = 6;
    for (int i = 0; i < 6; i++) {
        rp_home_entity_t *x = &link_snapshot.entities[i];
        snprintf(x->id, sizeof(x->id), "light.real_%d", i);
        snprintf(x->name, sizeof(x->name), "真实实体 %d", i);
        snprintf(x->room, sizeof(x->room), "房间 %d", i / 2);
        x->kind = (uint8_t)i; x->available = 1; x->number = 24;
    }
    link_sync_ui();
    for (int orientation = 0; orientation < 2; orientation++) {
        test_view = orientation ? (ps_view_t){640,360,true} : (ps_view_t){360,640,false};
        enter(PS_HA); real_ha_render(NULL);
        assert(real_ha_event(&(ps_event_t){.type=PS_TAP,.x=30,.y=90}));
        enter(PS_HA_SETTINGS); wifi_picker = false;
        for (ha_tab = 0; ha_tab < 3; ha_tab++) real_ha_settings_render(NULL);
        enter(PS_PRINTER_SETTINGS); real_printer_render(NULL, true);
        assert(real_printer_event(&(ps_event_t){.type=PS_TAP,.x=30,.y=110}, true));
        assert(pc.selected == 0 && !real_import && !real_manage);
        enter(PS_PRINTERS); real_printer_render(NULL, false);
    }
}
static void real_management_contract(void) {
    for (int orientation = 0; orientation < 2; orientation++) {
        test_view = orientation ? (ps_view_t){640,360,true} : (ps_view_t){360,640,false};
        enter(PS_HA_SETTINGS); ha_tab = 0; test_wifi = -1;
        int y = orientation ? 212 : 286;
        assert(real_ha_settings_event(&(ps_event_t){.type=PS_TAP,.x=30,.y=y+10}));
        assert(real_manage && !real_management.active && !real_qr_ready && *real_management.error);
        int calls = management_starts + management_stops + management_reads + qr_prepares + qr_clears;
        real_ha_settings_render(NULL);
        assert(calls == management_starts + management_stops + management_reads + qr_prepares + qr_clears);
        assert(real_ha_settings_event(&(ps_event_t){.type=PS_TAP,.x=30,.y=test_view.h-80}));
        assert(test_route == PS_WIFI && !real_manage && !real_qr_ready && !test_management.active);
        test_wifi = 0; enter(PS_HA_SETTINGS); ha_tab = 0;
        assert(real_ha_settings_event(&(ps_event_t){.type=PS_TAP,.x=30,.y=y+10}));
        assert(real_manage && real_management.active && real_qr_ready && test_management.active);
        char first_url[192]; copy_text(first_url, sizeof(first_url), real_qr_url);
        int prepared = qr_prepares; calls = management_starts + management_stops + management_reads + qr_prepares + qr_clears;
        real_ha_settings_render(NULL);
        assert(calls == management_starts + management_stops + management_reads + qr_prepares + qr_clears);
        assert(!real_manage_sync() && qr_prepares == prepared);
        test_management.seconds_left--;
        assert(!real_manage_sync() && qr_prepares == prepared);
        test_management.seconds_left = 539;
        assert(real_manage_sync() && qr_prepares == prepared);
        test_management.seconds_left = 30;
        assert(real_manage_sync() && real_manage_expiry(&real_management) == -1);
        int ticks = management_ticks;
        test_management.seconds_left = 0;
        assert(ps_home_tick());
        assert(management_ticks == ticks + 1 && !real_management.active && !real_qr_ready && !*real_qr_url);
        real_manage_begin();
        prepared = qr_prepares;
        assert(real_ha_settings_event(&(ps_event_t){.type=PS_TAP,.x=30,.y=test_view.h-80}));
        assert(strcmp(first_url, real_qr_url) && qr_prepares == prepared + 1 && real_management.seconds_left == 900);
        int stopped = management_stops;
        assert(ps_home_key(-1) && !real_manage && !real_qr_ready && !*real_qr_url);
        assert(!test_management.active && management_stops == stopped + 1);
        real_manage_begin(); stopped = management_stops;
        enter(PS_SHELF);
        assert(!real_manage && !test_management.active && management_stops == stopped + 1);
        enter(PS_PRINTER_SETTINGS); real_manage_begin();
        test_management.active = false; test_management.seconds_left = 0;
        assert(real_manage_sync() && !real_qr_ready && !*real_qr_url);
        real_printer_render(NULL, true);
        assert(real_printer_event(&(ps_event_t){.type=PS_TAP,.x=30,.y=test_view.h-80}, true));
        assert(real_management.active && real_qr_ready);
        assert(real_printer_event(&(ps_event_t){.type=PS_TAP,.x=test_view.w-40,.y=test_view.h-80}, true));
        assert(!real_manage && !test_management.active);
        // 手机端保存配置后，下次服务修订更新本机快照。 / A phone save refreshes the device snapshot on revision.
        enter(PS_HA_SETTINGS); copy_text(test_saved_link.home_wifi, sizeof(test_saved_link.home_wifi), "Phone-saved WiFi");
        test_link_revision++; assert(ps_home_tick());
        assert(!strcmp(link_config.home_wifi, "Phone-saved WiFi"));
    }
    printf("management UI: offline route, real status, pure rendering, stable QR, refresh/expiry, back/leave invalidation passed; %d QR draws\n", qr_draws);
}
static void real_printer_contract(void) {
    for (int orientation = 0; orientation < 2; orientation++) {
        test_view = orientation ? (ps_view_t){640,360,true} : (ps_view_t){360,640,false};
        link_config = (rp_home_config_t){.version=RP_HOME_CONFIG_VERSION,.enabled=1,.ha_token="fixture-ha"};
        link_config.printers[0] = (rp_home_printer_config_t){.source=RP_HOME_FORMLABS,.name="Saved Formlabs",.serial="FORM1",.client_id="fixture-id",.client_secret="private-fixture-oauth"};
        link_config.printers[1] = (rp_home_printer_config_t){.source=RP_HOME_BAMBU_CLOUD,.name="Saved Bambu",.serial="BAMBU2",.username="u_fixture",.secret="private-fixture-token"};
        test_saved_link = link_config; rp_home_config_t before = link_config;
        enter(PS_PRINTER_SETTINGS); pc.selected = 0; int y = orientation ? 264 : 350;
        assert(real_printer_event(&(ps_event_t){.type=PS_TAP,.x=test_view.w-40,.y=y+10}, true));
        assert(!real_import && !memcmp(&link_config, &before, sizeof(before)));
        assert(real_printer_event(&(ps_event_t){.type=PS_TAP,.x=30,.y=110+2*(orientation?48:72)}, true));
        assert(pc.selected == 2);
        assert(real_printer_event(&(ps_event_t){.type=PS_TAP,.x=test_view.w-40,.y=y+10}, true));
        assert(real_import && real_printer == 2);
        assert(ps_home_key(-1) && !real_import);
        link_snapshot.import_count = 1;
        link_snapshot.imports[0] = (rp_home_printer_t){.name="Imported Bambu",.serial="sensor.studio_bambu"};
        assert(real_printer_event(&(ps_event_t){.type=PS_TAP,.x=test_view.w-40,.y=y+10}, true));
        real_printer_render(NULL, true);
        int count = test_link_stores; test_link_fail = true;
        assert(real_printer_event(&(ps_event_t){.type=PS_TAP,.x=30,.y=130}, true));
        assert(real_import && test_link_stores == count && !memcmp(&link_config, &before, sizeof(before)));
        test_link_fail = false;
        assert(real_printer_event(&(ps_event_t){.type=PS_TAP,.x=30,.y=130}, true));
        assert(!real_import && test_link_stores == count + 1 && test_saved_link.printers[2].source == RP_HOME_HA);
        assert(!strcmp(test_saved_link.printers[2].serial, "sensor.studio_bambu"));
        assert(!memcmp(test_saved_link.printers, before.printers, 2 * sizeof(rp_home_printer_config_t)));
        // 手机同时配置空位时不得由旧导入列表覆盖。 / Do not overwrite a slot configured concurrently on the phone.
        real_import = true; real_printer = 2; count = test_link_stores;
        assert(real_printer_event(&(ps_event_t){.type=PS_TAP,.x=30,.y=130}, true));
        assert(!real_import && test_link_stores == count);
        real_import = true; enter(PS_HA_SETTINGS); enter(PS_PRINTER_SETTINGS); assert(!real_import);
        real_printer_render(NULL, true);
        enter(PS_PRINTERS); real_printer_render(NULL, false);
    }
    real_printer_cancel();
    puts("printer UI: both orientations, slot selection, protected occupied slots, HA import rollback/save, credentials preserved passed");
}

// 成功快照只缩减同一存储会话的重复提交，不掩盖失败或加载备份后的首次提交。
// Successful snapshots suppress same-session commits only, preserving failed and first-after-load submissions.
static void storage_checkpoint_contract(void){
    int before=stores;ps_home_storage_invalidate();assert(ps_home_storage_save()&&stores==before+4);
    before=stores;assert(ps_home_storage_save()&&stores==before);
    hc.compact^=1;assert(ps_home_storage_save()&&stores==before+1);before=stores;
    pc.selected=(uint8_t)((pc.selected+1)%PRINT_COUNT);save_print();assert(stores==before+1);before=stores;
    assert(ps_home_storage_save()&&stores==before);
    qc.current^=1;assert(ps_home_storage_save()&&stores==before+1);before=stores;
    clock_quotes^=1;assert(ps_home_storage_save()&&stores==before+1);before=stores;
    test_storage_allowed=false;assert(!ps_home_storage_save()&&stores==before);
    hc.rooms^=1;save_home();assert(stores==before);
    test_storage_allowed=true;assert(ps_home_storage_save()&&stores==before+1);
    before=stores;ps_home_storage_invalidate();test_failed_store="home2";
    assert(!ps_home_storage_save()&&stores==before+4);before=stores;
    test_failed_store=NULL;assert(ps_home_storage_save()&&stores==before+1);before=stores;
    assert(ps_home_storage_save()&&stores==before);
    // 主档或备份加载都不是已成功提交的证明，必须重新走四项提交验证。
    // Neither primary nor backup loading proves a successful commit; all four are checked again.
    ps_home_init();assert(stores==before);assert(ps_home_storage_save()&&stores==before+4);before=stores;
    assert(ps_home_storage_save()&&stores==before);
    // USB、换卡、格式化、重新绑定共用显式失效，丢失的文件必须重新提交。
    // USB, media changes, formatting and rebind share explicit invalidation, forcing absent files to commit.
    for(int boundary=0;boundary<4;boundary++){
        before=stores;saved_count=0;ps_home_storage_invalidate();
        assert(ps_home_storage_save()&&stores==before+4&&saved_count==4);
        before=stores;assert(ps_home_storage_save()&&stores==before);
    }
    test_style=0;test_route=PS_SLEEP_SETTINGS;test_view=(ps_view_t){360,640,false};
    before=stores;uint8_t old=clock_quotes;test_failed_store="clockquote";
    assert(tap(40,267)&&clock_quotes==old&&stores==before+1);
    // 失败提交可能已轮换主备，RAM回滚后也必须重新验证，不能信任旧成功快照。
    // A failed commit may have rotated primary/backup; rollback must still revalidate the old value.
    test_failed_store=NULL;assert(ps_home_storage_save()&&clock_quotes==old&&stores==before+2);
    assert(tap(40,267)&&clock_quotes!=old&&stores==before+3);
    before=stores;assert(ps_home_storage_save()&&stores==before);
}

// 锁屏不消费当前摘录或重置尚未到期的轮换；空队列和真实到期仍持久保存。
// Lock entry retains the excerpt/deadline; an empty queue or due rotation still persists real changes.
static void standby_checkpoint_contract(void){
    test_excerpt_count=13;deleted_excerpt=-1;test_battery=86;test_charging=false;test_interval=5;
    memset(&qc,0,sizeof(qc));qc.magic=QUOTE_MAGIC;memset(qcache,0,sizeof(qcache));quote_due=0;
    ps_home_storage_invalidate();assert(ps_home_storage_save());int before=stores;
    ps_home_standby_begin(1);assert(qc.current&&qc.pending[7]&&stores==before+1);check_queue();
    uint32_t current=qc.current;int64_t due=quote_due;before=stores;
    test_now+=60000;ps_home_standby_begin(1);assert(qc.current==current&&quote_due==due&&stores==before);
    test_now+=60000;clock_quotes=1;save_home_value(3);before=stores;
    ps_home_standby_begin(0);assert(qc.current==current&&quote_due==due&&stores==before);
    test_now=due-1;assert(!ps_home_standby_tick(1)&&qc.current==current&&stores==before);
    uint32_t next=qc.pending[0];test_now=due;assert(ps_home_standby_tick(1));
    assert(qc.current==next&&stores==before+1);current=qc.current;due=quote_due;before=stores;
    ps_home_standby_begin(1);assert(qc.current==current&&quote_due==due&&stores==before);
    // 清空解码缓存模拟进程重开；已存ID应补齐文字而不提前换下一条。
    // Empty decoded text simulates restart: restore the saved ID without advancing it.
    ps_home_init();memset(qcache,0,sizeof(qcache));assert(ps_home_storage_save());before=stores;
    current=qc.current;ps_home_standby_begin(1);assert(qc.current==current&&qcache[0].id==current&&qcache[0].text[0]&&stores==before);
    due=quote_due;test_now=due+1;test_battery=8;before=stores;
    ps_home_standby_begin(1);assert(quote_paused&&qc.current==current&&quote_due==due&&stores==before);
    assert(!ps_home_standby_tick(1)&&qc.current==current&&stores==before);
    test_battery=86;assert(ps_home_standby_tick(1)&&qc.current!=current&&stores==before+1);
    // 删除当前摘录必须重新选择，不允许只凭非空缓存继续显示已移除的内容。
    // A deleted current excerpt must be resolved again despite an otherwise nonempty decoded cache.
    current=qc.current;cached_quote_t q={0};
    for(int i=0;i<test_excerpt_count;i++)if(quote_read(i,&q)&&q.id==current)deleted_excerpt=i;
    assert(deleted_excerpt>=0);before=stores;ps_home_standby_begin(1);
    assert(qc.current&&qc.current!=current&&!queue_contains(current)&&stores==before+1);check_queue();
    // 没有额外候选时，删除当前项后也必须从剩余八条提升一条，不能显示空白。
    // With no spare candidate, deleting the current item must promote one of the eight pending entries.
    test_excerpt_count=9;deleted_excerpt=-1;memset(&qc,0,sizeof(qc));qc.magic=QUOTE_MAGIC;
    memset(qcache,0,sizeof(qcache));quote_due=0;assert(ps_home_storage_save());
    ps_home_standby_begin(1);assert(qc.current&&qc.pending[7]);current=qc.current;next=qc.pending[0];
    for(int i=0;i<test_excerpt_count;i++)if(quote_read(i,&q)&&q.id==current)deleted_excerpt=i;
    assert(deleted_excerpt>=0);before=stores;ps_home_standby_begin(1);
    assert(qc.current==next&&!queue_contains(current)&&stores==before+1);check_queue();
    test_excerpt_count=0;deleted_excerpt=-1;memset(&qc,0,sizeof(qc));qc.magic=QUOTE_MAGIC;
    memset(qcache,0,sizeof(qcache));quote_due=0;assert(ps_home_storage_save());before=stores;
    ps_home_standby_begin(1);ps_home_standby_begin(1);assert(!qc.current&&stores==before);
    puts("home checkpoint: success-only snapshots, storage gates/invalidation/failure/reload; retained excerpt, interval, deleted/empty content and low-power pause PASS");
}

int main(void){
    ps_home_init();assert(stores==0);assert(sizeof(hc)+sizeof(pc)+sizeof(qc)<=2048);
    enter(PS_HA);int list[ENTITY_COUNT];assert(entity_list(list)==30);render_pure();
    rect_t r=entity_rect(0);uint32_t on=hc.on;assert(tap(r.x+15,r.y+40));assert(hc.on==(on^1));
    r=entity_rect(4);on=hc.on;assert(tap(r.x+15,r.y+40));assert(hc.on==on);
    test_online=false;r=entity_rect(1);assert(tap(r.x+15,r.y+40));assert(hc.on==on);test_online=true;
    r=entity_rect(2);assert(event(PS_LONG,r.x+15,r.y+40));assert(climate_entity==2);assert(tap(240,220));assert(hc.climate_temp[0]==27);render_pure();climate_entity=-1;
    enter(PS_HA_SETTINGS);assert(tap(150,94));assert(ha_tab==1);assert(tap(30,310));assert(hc.hidden&1);assert(entity_list(list)==29);render_pure();
    assert(tap(280,94));assert(ha_tab==2);assert(tap(270,282));assert(hc.manual);assert(hc.order[0]==1&&hc.order[1]==0);render_pure();
    ha_tab=0;assert(tap(40,160));assert(wifi_picker);assert(tap(40,180));assert(!strcmp(hc.home_wifi,"Home"));link_config.enabled=1;snprintf(link_config.ha_token,sizeof(link_config.ha_token),"fixture");snprintf(link_config.home_wifi,sizeof(link_config.home_wifi),"Home");link_snapshot.result=RP_HOME_OK;assert(ps_home_local_network());test_wifi=1;assert(!ps_home_local_network());test_wifi=0;
    assert(tap(40,336));assert(edit_done);edit_done("http://unsafe.example");assert(!*hc.external);edit_done("https://ha.example");assert(!strcmp(hc.external,"https://ha.example"));
    enter(PS_PRINTERS);assert(printer_selected()<0);render_pure();assert(tap(100,230));assert(printer_view==PV_IMPORT);render_pure();assert(tap(100,345));assert(printer_selected()>=0);assert(pc.devices[0].active&&pc.devices[1].active);render_pure();
    int id=printer_selected();int64_t stamp=print_updated[id];ps_home_tick();test_now+=61000;assert(ps_home_tick());assert(!printer_live(id));assert(print_updated[id]==stamp);assert(tap(300,550));assert(printer_live(id));test_online=false;stamp=print_updated[id];assert(tap(300,550));assert(print_updated[id]==stamp);test_online=true;
    enter(PS_PRINTER_SETTINGS);render_pure();info_printer=0;printer_view=PV_REMOVE;render_pure();assert(tap(100,275));assert(!pc.devices[0].active&&pc.devices[1].active);begin_mqtt();assert(mqtt_valid(&printer_draft));copy_text(printer_draft.topic,sizeof(printer_draft.topic),"device/+/report");assert(!mqtt_valid(&printer_draft));begin_mqtt();mqtt_page=1;render_pure();assert(tap(100,500));assert(pc.devices[0].active&&pc.devices[0].source==1);
    enter(PS_QUOTES);assert(qc.current&&qc.pending[7]);check_queue();render_pure();uint32_t expected=qc.pending[0],current=qc.current;int n=stores;assert(tap(100,100));assert(qc.current==current&&stores==n);render_pure();
    enter(PS_READ);ps_home_tick();enter(PS_QUOTES);assert(qc.current==expected);check_queue();for(int i=0;i<25;i++){current=qc.current;test_now+=300000;ps_home_tick();assert(qc.current!=current);check_queue();}
    test_battery=8;current=qc.current;assert(ps_home_tick());assert(quote_paused);test_now+=600000;ps_home_tick();assert(qc.current==current);render_pure();test_battery=12;ps_home_tick();assert(quote_paused);test_battery=15;assert(ps_home_tick());assert(!quote_paused);assert(qc.current==current);
    test_battery=8;test_charging=true;ps_home_tick();assert(!quote_paused);test_charging=false;test_battery=86;
    enter(PS_CLOCK);render_pure();test_minute=35;assert(ps_home_tick());assert(clock_frame==0);render_pure();for(int i=1;i<=6;i++){test_now+=140;assert(ps_home_tick());render_pure();}assert(clock_frame==6);
    ps_home_action(1);assert(room_picker);render_pure();assert(tap(50,150));assert(!room_picker&&section==1);ps_home_action(2);assert(test_route==PS_HA_SETTINGS&&ha_tab==1);ps_home_action(0);assert(section==0&&!room_picker);
    assert(dashboard_page==0);assert(ps_home_key(2));assert(dashboard_page==1);assert(!ps_home_key(1));assert(dashboard_page==1);assert(ps_home_key(0));assert(dashboard_page==0);
    enter(PS_PRINTER_SETTINGS);begin_mqtt();mqtt_page=1;enter(PS_EDITOR);enter(PS_PRINTER_SETTINGS);assert(printer_view==PV_MQTT&&mqtt_page==1);assert(ps_home_key(-1));assert(printer_view==PV_LIST);
    test_style=1;enter(PS_SLEEP_SETTINGS);render_pure();assert(tap(280,347));assert(test_interval==720);test_style=0;
    test_view=(ps_view_t){640,360,true};ha_tab=0;printer_view=PV_LIST;climate_entity=-1;
    ps_route_t routes[]={PS_HA,PS_HA_SETTINGS,PS_PRINTERS,PS_PRINTER_SETTINGS,PS_CLOCK,PS_QUOTES,PS_SLEEP_SETTINGS};
    for(size_t i=0;i<sizeof(routes)/sizeof(routes[0]);i++){enter(routes[i]);render_pure();}
    enter(PS_HA_SETTINGS);for(int i=0;i<3;i++){ha_tab=i;render_pure();}
    enter(PS_PRINTER_SETTINGS);printer_view_t views[]={PV_SOURCE,PV_IMPORT,PV_MQTT,PV_INFO,PV_REMOVE};for(size_t i=0;i<sizeof(views)/sizeof(views[0]);i++){printer_view=views[i];render_pure();}
    printer_view=PV_LIST;enter(PS_HA);assert(entities_per_page()==12);r=entity_rect(11);assert(r.y+r.h<test_view.h-88);render_pure();
    test_view=(ps_view_t){360,640,false};ha_tab=0;enter(PS_HA_SETTINGS);choose_home_wifi();render_pure();assert(ps_home_key(-1));assert(!wifi_picker);
    test_wifi=1;hc.external_enabled=1;hc.external_control=0;assert(home_reachable());assert(!home_control_available());hc.external_control=1;assert(home_control_available());test_wifi=0;
    available[2]=0;assert(!entity_allowed(2));hc.unavailable=0;assert(entity_allowed(2));available[2]=1;hc.unavailable=1;
    test_time_valid=false;enter(PS_READ);enter(PS_CLOCK);assert(!clock_valid&&clock_frame==0&&clock_old[0]==clock_values[0]);render_pure();
    enter(PS_QUOTES);expected=qc.pending[0];assert(expected);int before_init=stores;ps_home_init();assert(stores==before_init);enter(PS_QUOTES);assert(qc.current==expected);check_queue();
    uint32_t removed=qc.pending[0];cached_quote_t q={0};for(int i=0;i<test_excerpt_count;i++)if(quote_read(i,&q)&&q.id==removed)deleted_excerpt=i;assert(deleted_excerpt>=0);resolve_quote_cache();assert(!queue_contains(removed));check_queue();
    char line[64];assert(quote_line("甲乙。丙",line,sizeof(line),40,20)==3&&!strcmp(line,"甲"));
    for(int i=0;i<180;i++)memcpy(qcache[0].text+i*3,"读",3);qcache[0].text[540]=0;qc.current=1;quote_segment=0;assert(quote_segments()==2);render_pure();quote_segment=1;render_pure();
    // 时钟可选摘录复用缓存、轮换、暂停及持久化，绘图保持无副作用。
    // Optional clock excerpts reuse queue/rotation/power/persistence while drawing stays pure.
    test_view=(ps_view_t){360,640,false};enter(PS_SLEEP_SETTINGS);assert(!clock_quotes);
    assert(tap(40,267)&&clock_quotes);uint32_t prior=qc.pending[0];assert(prior);
    enter(PS_CLOCK);assert(qc.current==prior);check_queue();render_pure();
    current=qc.current;test_now+=(int64_t)test_interval*60000;assert(ps_home_tick()&&qc.current!=current);check_queue();render_pure();
    test_battery=8;current=qc.current;assert(ps_home_tick()&&quote_paused);test_minute++;test_now+=1000000;
    int minute=clock_values[1];ps_home_tick();assert(qc.current==current&&clock_values[1]==minute);render_pure();
    test_battery=15;assert(ps_home_tick()&&!quote_paused);render_pure();
    for(int orientation=0;orientation<2;orientation++){
        test_view=orientation?(ps_view_t){640,360,true}:(ps_view_t){360,640,false};
        enter(PS_SLEEP_SETTINGS);bool lock_before=test_locked;
        assert(tap(40,orientation?190:267)&&!clock_quotes&&test_locked==lock_before);
        assert(tap(40,orientation?190:267)&&clock_quotes&&test_locked==lock_before);
        render_pure();enter(PS_CLOCK);render_pure();
        test_battery=8;ps_home_tick();render_pure();test_battery=86;ps_home_tick();
    }
    assert(ps_home_storage_save());ps_home_init();assert(clock_quotes);enter(PS_CLOCK);render_pure();
    test_excerpt_count=0;memset(&qc,0,sizeof(qc));memset(qcache,0,sizeof(qcache));enter(PS_READ);enter(PS_CLOCK);render_pure();
    real_ui_contract();
    real_printer_contract();
    real_management_contract();
    sleep_appearance_contract();
    storage_checkpoint_contract();
    standby_checkpoint_contract();
    printf("pico_home: legacy filtering/control/layout, real-service config routes, 9-item queue, power hysteresis, flip frames and render purity passed; %d draw bounds; configs %zu bytes\n",draws,sizeof(hc)+sizeof(pc)+sizeof(qc));
    return 0;
}

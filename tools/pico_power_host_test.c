/* SPDX-License-Identifier: Apache-2.0
 * 真正电源控制器的故障边界；平台只作确定性替身，无设备I/O。
 * Deterministic platform doubles around the actual power controller; no device I/O.
 */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include "../main/power_touch.h"
#include "../main/system/pico_i18n.h"
typedef int esp_err_t;
enum {ESP_OK=0,ESP_FAIL=-1,MODE_GL16=5,APP_SLEEP_OFF=2,MALLOC_CAP_SPIRAM=1,MALLOC_CAP_8BIT=2,APP_AUTO_SLEEP_MS=180000};
typedef struct {bool touched;uint8_t count;uint16_t x,y;} cst836u_touch_t;
typedef struct {uint8_t *back_fb;} EpdiyHighlevelState;
typedef struct app_ctx {uint8_t *fb;EpdiyHighlevelState *hl;int64_t now_ms;const cst836u_touch_t *touch;bool pressed,released,consumed;void *request_app;bool request_return,request_menu,request_developer_menu;} app_ctx_t;
typedef struct app_desc {void (*render)(app_ctx_t*,uint8_t*);bool holds_pmu,blocks_idle_sleep,product_shell;} app_desc_t;
typedef enum {APP_REDRAW_NONE,APP_REDRAW_AREA,APP_REDRAW_PAGE,APP_REDRAW_FULL,APP_REDRAW_DONE} app_redraw_t;
typedef struct {int w,h;bool landscape;} ps_view_t;
typedef struct {int x,y,width,height;} EpdRect;
typedef struct {int type,x,y;} ps_event_t;
typedef int ps_route_t;
enum {PS_TAP,PS_SLEEP_SETTINGS,PS_POWER,PS_CANVAS=29,PS_CLOUDFLARE=32};
typedef enum {APP_WAKE_NONE,APP_WAKE_KEY,APP_WAKE_PICKUP,APP_WAKE_TOUCH_DOUBLE,APP_WAKE_ERROR,APP_WAKE_KEY_LONG} app_wake_source_t;
typedef struct {bool suspended,busy;uint32_t submitted_seq,completed_seq;esp_err_t last_error;} read_pico_connectivity_resume_status_t;
#include "power_api_under_test.h"
static unsigned checks;
#define CHECK(x) do {checks++;if(!(x)){fprintf(stderr,"FAIL %s line %d\n",#x,__LINE__);abort();}}while(0)
static uint8_t frame[1216u*684u/2u],panel_back[1216u*684u/2u];
static EpdiyHighlevelState panel={panel_back};
static app_ctx_t ctx;
static bool storage_busy,usb_busy,usb_held,content_held,transfer_busy,keep_awake,input_free,dock_ready,cache_ready;
static bool fail_malloc,fail_reader,fail_home,fail_games,fail_network,fail_book,fail_home_suspend,radio_safe;
static bool game_worker,game_paused;
static bool weread_busy,weread_paused;
bool ps_weread_busy(void){return weread_busy;}
bool ps_weread_pause(void){if(weread_busy)return false;weread_paused=true;return true;}
void ps_weread_resume(void){weread_paused=false;}
static int render_calls,reader_calls,home_calls,game_prepares,game_resumes,network_stops,network_resumes,lock_begins,lock_ends,full_presents,draw_limited;
static int lock_begin_error,host_off_error;
static int home_suspends,home_resumes;
static bool sleep_allowed,taps_allowed,fail_store,has_options,off_hint_drawn,fail_refresh;
static uint8_t saved_options[2];
static int back_calls,off_calls,draw_errors;
static int area_presents,feedback_requests,book_prepares,network_starts,home_starts,timing_logs;
static bool wait_probe,partial_allowed,fail_area,drive_latency_probe,render_latency_probe;
static int64_t home_ready_ms,network_ready_ms;
static char timing_log[384];
static char checkpoint_order[8];
static size_t checkpoint_count;
static const int E0470_WAVEFORM=1;
static size_t allocations;
static app_wake_source_t wake_source;
static read_pico_connectivity_resume_status_t net;
static char toast[160];
static ps_view_t test_view={360,640,false};
static int style_x,style_y,style_w,style_h,navigated,test_style=3;
static bool external_power,desktop_ready=true,dock_network_wait;static unsigned desktop_version,desktop_ticks,desktop_presents,mode_presents;
static bool lock_image_ready=true;static int lock_image_prepares,lock_image_renders,lock_image_releases;
static void render(app_ctx_t *c,uint8_t *fb){render_calls++;if(render_latency_probe)c->now_ms+=327;memset(fb,cache_ready?0x55:0x11,sizeof(frame));}
const app_desc_t app_book={.render=render};
static const app_desc_t shell={.render=render,.product_shell=true,.blocks_idle_sleep=true};
static const app_desc_t child={.render=render};
bool ps_lock_image_prepare(void){lock_image_prepares++;return lock_image_ready;}
void ps_lock_image_render(uint8_t *fb){lock_image_renders++;memset(fb,0x33,sizeof(frame));}
void ps_lock_image_release(void){lock_image_releases++;}
bool ps_power_partial_safe(void){return partial_allowed;}
bool ps_storage_busy(void){return storage_busy;}
bool ps_usb_busy(void){return usb_busy;}
bool ps_usb_held(void){return usb_held;}
bool ps_content_held(void){return content_held;}
bool ps_transfer_prepared(void){return transfer_busy;}
bool ps_background_io_allowed(void){return input_free;}
bool ps_dual_enabled(void){return false;}
bool ps_dual_holds_awake(void){return false;}
int ps_dual_role(void){return 0;}
void ps_dual_suspend(void){}
bool app_book_global_is_open(void){return false;}
app_redraw_t app_book_external_turn(app_ctx_t *c,int direction){(void)c;(void)direction;assert(false);return APP_REDRAW_NONE;}
bool ps_home_pga_suspend(uint32_t timeout){(void)timeout;return true;}
void ps_home_pga_resume(void){}
void ps_home_portal_stop(void){}
bool app_settings_test_awake(void){return keep_awake;}
bool ps_power_dock_ready(void){return dock_ready;}
int64_t ps_now(void){return ctx.now_ms;}
ps_route_t ps_route(void){return 0;}
void ps_datetime_service_tick(bool allowed){(void)allowed;}
int64_t esp_timer_get_time(void){return ctx.now_ms*1000;}
void *heap_caps_malloc(size_t n,unsigned caps){CHECK(caps==(MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT)&&n==sizeof(frame));if(fail_malloc)return NULL;allocations++;return malloc(n);}
static void tracked_free(void *p){if(p){CHECK(allocations==1);allocations--;}free(p);}
#define free tracked_free
static void checkpoint(char tag){if(wait_probe){CHECK(network_starts==1&&home_starts==1&&!feedback_requests&&!area_presents&&!full_presents&&!lock_begins&&!ps_power_active());CHECK(checkpoint_count<sizeof(checkpoint_order)-1);checkpoint_order[checkpoint_count++]=tag;if(tag!='N'&&tag!='W')ctx.now_ms+=1000;}}
static void capture_power_log(const char *tag,const char *format,...){CHECK(!strcmp(tag,"pico_power"));timing_logs++;va_list args;va_start(args,format);vsnprintf(timing_log,sizeof(timing_log),format,args);va_end(args);}
#define ESP_LOGI(...) capture_power_log(__VA_ARGS__)
esp_err_t read_pico_connectivity_quiesce(uint32_t timeout){CHECK(timeout==0);if(dock_network_wait){network_starts++;return net.suspended&&!net.busy?ESP_OK:ESP_FAIL;}CHECK(home_starts==network_starts+1);network_starts++;if(wait_probe)network_ready_ms=ctx.now_ms+5000;return network_ready_ms>ctx.now_ms?ESP_FAIL:ESP_OK;}
void app_book_global_prepare(void){book_prepares++;}
bool app_book_power_prepare(void){checkpoint('B');return !fail_book;}
bool ps_reader_power_save(void){checkpoint('R');reader_calls++;cache_ready=false;return !fail_reader;}
static bool management_active;
bool read_pico_management_active(void){return management_active;}
bool ps_home_storage_save(void){checkpoint('H');home_calls++;return !fail_home;}
bool ps_games_power_prepare(void){checkpoint('G');game_prepares++;game_paused=true;game_worker=false;return !fail_games;}
void ps_games_power_resume(void){game_resumes++;game_paused=false;}
bool ps_connectivity_shutdown(void){checkpoint('W');network_stops++;if(fail_network){net.busy=true;return false;}if(wait_probe&&ctx.now_ms<network_ready_ms)ctx.now_ms=network_ready_ms;net.busy=false;net.suspended=true;return true;}
esp_err_t read_pico_connectivity_resume(void){network_resumes++;if(net.busy)return ESP_FAIL;net.busy=true;net.submitted_seq++;return ESP_OK;}
void read_pico_connectivity_get_resume_status(read_pico_connectivity_resume_status_t *out){*out=net;}
bool read_pico_home_link_suspend(uint32_t timeout){if(timeout==0){CHECK(home_starts==network_starts);home_starts++;if(wait_probe)home_ready_ms=ctx.now_ms+2000;return !fail_home_suspend;}checkpoint('N');CHECK(timeout==2500&&game_paused&&reader_calls&&home_calls);home_suspends++;if(wait_probe&&ctx.now_ms<home_ready_ms)ctx.now_ms=home_ready_ms;return !fail_home_suspend;}
void read_pico_home_link_resume(void){home_resumes++;}
int read_pico_ble_pager_poll_event(void){return 0;}
bool read_pico_ble_pager_light_sleep_safe(void){return radio_safe;}
void ps_global_cancel_input(void){}
void ps_global_draw_controls(const app_desc_t*a,uint8_t*f){(void)a;(void)f;}
void ps_dirty(void){}
void ps_toast(const char *s){snprintf(toast,sizeof(toast),"%s",s);}
bool ps_load(const char *k,void *d,size_t n){if(!strcmp(k,"powercfg")&&n==2&&has_options){memcpy(d,saved_options,2);return true;}return false;}
bool ps_store(const char *k,const void *d,size_t n){if(fail_store)return false;if(!strcmp(k,"powercfg")&&n==2){memcpy(saved_options,d,2);has_options=true;}return true;}
ps_view_t ps_view(void){return test_view;}
int ps_battery_percent(void){return 50;}
bool ps_charging(void){return false;}
bool ps_external_power(void){return external_power;}
bool ps_desktop_prepare(void){return desktop_ready;}
bool ps_desktop_prepare_app(bool app){(void)app;return desktop_ready;}
bool ps_desktop_tick(void){desktop_ticks++;return false;}
unsigned ps_desktop_revision(void){return desktop_version;}
void ps_desktop_render(uint8_t *fb){memset(fb,0x77,sizeof(frame));}
void ps_desktop_presented(bool success){CHECK(success);desktop_presents++;}
void read_pico_management_tick(void){}
int ps_sleep_style(void){return test_style;}
void ps_power_poll_hardware(app_ctx_t *c){(void)c;}
void ps_lock_covers_release(void){}
void ps_lock_covers_prepare(int n){(void)n;}
void ps_lock_covers_render(uint8_t *fb){memset(fb,0x88,sizeof(frame));}
void ps_home_standby_begin(int n){(void)n;}
bool ps_home_standby_tick(int n){(void)n;return false;}
void ps_home_standby_render(uint8_t *fb,int n){(void)n;memset(fb,0xaa,sizeof(frame));}
void guard_draw_result(void *hl,int result){(void)hl;if(result)draw_errors++;}
int update_display_full(void *hl){(void)hl;full_presents++;if(drive_latency_probe)ctx.now_ms+=800;if(fail_refresh&&full_presents==1)return ESP_FAIL;memcpy(panel_back,frame,sizeof(frame));return 0;}
int update_display_mode(void *hl,int m){(void)hl;(void)m;mode_presents++;return 0;}
EpdRect ps_rect(int x,int y,int w,int h){return (EpdRect){x,y,w,h};}
int update_display_area_with(void *hl,const int *wave,int mode,EpdRect area){(void)hl;CHECK(wave==&E0470_WAVEFORM&&mode==MODE_GL16);CHECK(area.x==0&&area.y==test_view.h-64&&area.width==test_view.w&&area.height==64);area_presents++;if(drive_latency_probe)ctx.now_ms+=100;if(fail_area)return ESP_FAIL;memcpy(panel_back,frame,sizeof(frame));return 0;}
void app_product_lock_end(void){lock_ends++;}
esp_err_t app_product_lock_begin(bool allowed,bool taps){lock_begins++;sleep_allowed=allowed;taps_allowed=taps;return lock_begin_error;}
app_wake_source_t app_product_lock_wait_step(uint32_t timeout){CHECK(timeout==1000||timeout==140);return wake_source;}
esp_err_t app_enter_host_sleep(int mode){CHECK(mode==APP_SLEEP_OFF&&off_hint_drawn&&full_presents>0&&!draw_errors);off_calls++;return host_off_error;}
void esp_restart(void){}
void ps_box(uint8_t*f,int x,int y,int w,int h,int r,int g,bool b){(void)f;(void)x;(void)y;(void)w;(void)h;(void)r;(void)g;(void)b;}
void ps_fill(uint8_t*f,int x,int y,int w,int h,int g){(void)x;(void)w;if(y==test_view.h-64&&h==64&&g==15)f[sizeof(frame)-1]=0xee;}
void ps_center(uint8_t*f,int x,int y,int n,const char*s,int g){(void)f;(void)x;(void)y;(void)n;(void)g;if(strstr(s,"受限"))draw_limited++;if(strstr(s,"已关机"))off_hint_drawn=true;if(!strcmp(s,"正在锁屏…")){feedback_requests++;}}
void ps_text(uint8_t*f,int x,int y,int n,const char*s,int g){(void)f;(void)x;(void)y;(void)n;(void)s;(void)g;}
void ps_wrap(uint8_t*f,int x,int y,int w,int n,int l,const char*s,int g){(void)f;(void)x;(void)y;(void)w;(void)n;(void)l;(void)s;(void)g;}
void ps_button(uint8_t*f,int x,int y,int w,int h,const char*s,bool b){(void)f;(void)b;if(!strncmp(s,"锁屏外观",12)){style_x=x;style_y=y;style_w=w;style_h=h;}}
void ps_heading(uint8_t*f,const char*s,const char*t){(void)f;(void)s;(void)t;}
static bool rotation_lock;
bool ps_rotation_locked(void){return rotation_lock;}
void ps_rotation_lock(bool locked){rotation_lock=locked;}
void ps_navigate(int r){navigated=r;if(r==PS_POWER)ps_power_enter();}
void ps_back(void){back_calls++;}
bool ps_hit(int x,int y,int a,int b,int w,int h){return x>=a&&y>=b&&x<a+w&&y<b+h;}
static bool sound_ready=true,sound_paused;
static unsigned sound_pauses,sound_resumes;
void ps_sound_key(void){assert(false);}
void ps_sound_pause(void){sound_paused=true;sound_pauses++;}
bool ps_sound_quiet(void){return sound_ready;}
void ps_sound_resume(void){sound_paused=false;sound_resumes++;}
#include "power_under_test.c"
#undef free
static void fixture(void){
    if(resume_frame){tracked_free(resume_frame);resume_frame=NULL;}
    CHECK(!allocations);requested=-1;power_state=style=0;style_override=-1;initialized=false;
    sound_ready=true;sound_paused=false;sound_pauses=sound_resumes=0;
    external_power=dock_network_wait=board_lock=board_stopping=false;desktop_ready=true;desktop_version=desktop_ticks=desktop_presents=mode_presents=board_revision=0;
    activity_valid=network_resume_pending=network_resume_waiting=false;
    power_options_loaded=quick_menu=management_active=radio_wait=false;auto_minutes=3;doubletap_enabled=true;has_options=fail_store=fail_refresh=off_hint_drawn=false;back_calls=off_calls=draw_errors=0;network_retry_at=last_activity_ms=0;
    storage_busy=usb_busy=usb_held=content_held=transfer_busy=keep_awake=dock_ready=weread_busy=weread_paused=false;input_free=cache_ready=radio_safe=true;
    fail_malloc=fail_reader=fail_home=fail_games=fail_network=fail_book=fail_home_suspend=false;game_worker=true;game_paused=false;
    render_calls=reader_calls=home_calls=game_prepares=game_resumes=network_stops=network_resumes=lock_begins=lock_ends=full_presents=draw_limited=0;
    lock_begin_error=0;host_off_error=ESP_FAIL;wake_source=APP_WAKE_NONE;memset(&net,0,sizeof(net));memset(frame,0x55,sizeof(frame));
    home_suspends=home_resumes=0;test_style=3;lock_image_ready=true;lock_image_prepares=lock_image_renders=lock_image_releases=0;
    area_presents=feedback_requests=book_prepares=network_starts=home_starts=timing_logs=0;home_ready_ms=network_ready_ms=0;timing_log[0]=0;wait_probe=partial_allowed=fail_area=drive_latency_probe=render_latency_probe=false;checkpoint_count=0;memset(checkpoint_order,0,sizeof(checkpoint_order));
    panel.back_fb=panel_back;memcpy(panel_back,frame,sizeof(frame));ctx=(app_ctx_t){.fb=frame,.hl=&panel,.now_ms=1000};toast[0]=0;ps_power_activity(ctx.now_ms);
}
static void complete_resume(bool success){net.busy=false;net.completed_seq=net.submitted_seq;net.last_error=success?ESP_OK:ESP_FAIL;if(success)net.suspended=false;}
static void unchanged_frame(void){for(size_t i=0;i<sizeof(frame);i++)CHECK(frame[i]==0x55);}
static void parallel_lock_prepare(void){
    for(int landscape=0;landscape<2;landscape++){
        test_view=(ps_view_t){landscape?640:360,landscape?360:640,landscape};
        for(int action=PS_POWER_LOCK;action<=PS_POWER_SLEEP;action++)for(int book=0;book<2;book++){
            fixture();wait_probe=true;int64_t started=ctx.now_ms;
            const app_desc_t *current=book?&app_book:&shell;
            ps_power_request(action);CHECK(ps_power_run(&ctx,current));
            CHECK(!feedback_requests&&!area_presents&&network_starts==1&&home_starts==1);
            CHECK(!strcmp(checkpoint_order,book?"BGRHNW":"GRHNW"));
            CHECK(ctx.now_ms==started+5000); // Workers run during saves; never add two serial timeouts.
            CHECK(power_state==(action==PS_POWER_LOCK?1:2)&&full_presents==1&&lock_begins==1);
            CHECK(timing_logs==1&&strstr(timing_log,"total_ms=5000")&&strstr(timing_log,book?"save_ms=4000":"save_ms=3000")&&strstr(timing_log,book?"network_ms=1000":"network_ms=2000"));
            CHECK(book_prepares==book&&render_calls==1&&resume_frame&&allocations==1);
            for(size_t i=0;i<sizeof(frame);i++)CHECK(resume_frame[i]==0x55);
            wait_probe=false;wake_source=APP_WAKE_KEY;CHECK(ps_power_run(&ctx,current));
            CHECK(!ps_power_pending()&&!allocations&&full_presents==2&&render_calls==1);unchanged_frame();
        }
        for(int failure=0;failure<6;failure++){
            fixture();wait_probe=true;
            switch(failure){case 0:fail_book=true;break;case 1:fail_games=true;break;case 2:fail_reader=true;break;case 3:fail_home=true;break;case 4:fail_home_suspend=true;break;default:fail_network=true;break;}
            const char *order[]={"B","BG","BGR","BGRH","BGRHN","BGRHNW"};
            ps_power_request(PS_POWER_LOCK);CHECK(!ps_power_run(&ctx,&app_book));
            CHECK(!feedback_requests&&!area_presents&&network_starts==1&&home_starts==1);
            CHECK(!strcmp(checkpoint_order,order[failure]));
            CHECK(!full_presents&&!lock_begins&&!ps_power_pending()&&!allocations&&!game_paused&&!timing_logs);
            CHECK(network_resume_pending&&home_resumes==1&&game_resumes==1);unchanged_frame();
        }
        for(int action=PS_POWER_RESTART;action<=PS_POWER_OFF;action++){
            fixture();ps_power_request(action);(void)ps_power_run(&ctx,&shell);
            CHECK(!feedback_requests&&!area_presents);
        }
    }
    test_view=(ps_view_t){360,640,false};
}
static void retained_page_fast_lock(void){
    for(int landscape=0;landscape<2;landscape++)for(int action=PS_POWER_LOCK;action<=PS_POWER_SLEEP;action++){
        test_view=(ps_view_t){landscape?640:360,landscape?360:640,landscape};
        fixture();partial_allowed=drive_latency_probe=true;int64_t started=ctx.now_ms;
        ps_power_request(action);CHECK(ps_power_run(&ctx,&shell));
        CHECK(area_presents==1&&!full_presents&&lock_begins==1&&sleep_allowed&&!feedback_requests);
        CHECK(ctx.now_ms==started+100&&timing_logs==1&&strstr(timing_log,"paint_ms=100")&&strstr(timing_log,"total_ms=100"));
        ps_power_wake();ps_power_run(&ctx,&shell);CHECK(full_presents==1&&!allocations);unchanged_frame();
        for(int gate=0;gate<3;gate++){
            fixture();partial_allowed=true;if(gate==0)partial_allowed=false;else if(gate==1)panel_back[0]^=1;else radio_safe=false;
            ps_power_request(action);CHECK(ps_power_run(&ctx,&shell));
            CHECK(full_presents==1&&!area_presents&&lock_begins==1);ps_power_wake();ps_power_run(&ctx,&shell);unchanged_frame();
        }
        fixture();partial_allowed=fail_area=true;ps_power_request(action);CHECK(!ps_power_run(&ctx,&shell));
        CHECK(area_presents==1&&full_presents==1&&!lock_begins&&!allocations&&!ps_power_active()&&network_resume_pending&&draw_errors==1);unchanged_frame();
        fixture();fail_refresh=true;ps_power_request(action);CHECK(!ps_power_run(&ctx,&shell));
        CHECK(full_presents==2&&!area_presents&&!lock_begins&&!allocations&&!ps_power_active()&&network_resume_pending&&draw_errors==1);unchanged_frame();
        for(int visual=0;visual<5;visual++)if(visual!=3){
            fixture();partial_allowed=true;test_style=visual;ps_power_request(action);CHECK(ps_power_run(&ctx,&shell));
            CHECK(full_presents==1&&!area_presents&&lock_begins==1);ps_power_wake();ps_power_run(&ctx,&shell);unchanged_frame();
        }
    }
    test_view=(ps_view_t){360,640,false};
}
static void submitted_capture(void){
    uint8_t expected[sizeof(frame)];
    for(int orientation=0;orientation<4;orientation++)for(int visual=0;visual<5;visual++)
        for(int action=PS_POWER_LOCK;action<=PS_POWER_SLEEP;action++)for(int book=0;book<2;book++){
        test_view=(ps_view_t){orientation&1?640:360,orientation&1?360:640,(orientation&1)!=0};
        fixture();partial_allowed=render_latency_probe=true;cache_ready=false;test_style=visual;
        for(size_t i=0;i<sizeof(expected);i++)expected[i]=(uint8_t)(i*29u+orientation*17u+visual*13u+book*23u);
        memcpy(frame,expected,sizeof(frame));memcpy(panel_back,expected,sizeof(frame));
        const app_desc_t *current=book?&app_book:&shell;
        ps_power_request(action);CHECK(ps_power_run(&ctx,current));
        CHECK(!render_calls&&reader_calls==1&&home_calls==1&&game_prepares==1&&book_prepares==book);
        CHECK(network_stops==1&&home_suspends==1&&lock_begins==1&&resume_frame);
        CHECK(!memcmp(resume_frame,expected,sizeof(frame)));
        CHECK(timing_logs==1&&strstr(timing_log,"capture_ms=0")&&strstr(timing_log,"total_ms=0"));
        ps_power_wake();CHECK(ps_power_run(&ctx,current));
        CHECK(!render_calls&&!allocations&&!memcmp(frame,expected,sizeof(frame)));
    }
    for(int gate=0;gate<3;gate++){
        fixture();partial_allowed=render_latency_probe=true;test_style=0;
        if(gate==0)partial_allowed=false; // 取消输入/未清浮层债务。/ Cancelled input or outstanding overlay debt.
        else if(gate==1)frame[0]^=1; // 已绘制但尚未提交。/ Painted but not submitted.
        else panel.back_fb=NULL;
        ps_power_request(PS_POWER_LOCK);CHECK(ps_power_run(&ctx,&shell));
        CHECK(render_calls==1&&timing_logs==1&&strstr(timing_log,"capture_ms=327"));
        memset(expected,0x55,sizeof(expected));CHECK(resume_frame&&!memcmp(resume_frame,expected,sizeof(frame)));
        ps_power_wake();ps_power_run(&ctx,&shell);unchanged_frame();
    }
    for(int failure=0;failure<3;failure++){
        fixture();partial_allowed=render_latency_probe=true;test_style=0;
        if(failure==0)fail_reader=true;else if(failure==1)fail_refresh=true;else lock_begin_error=ESP_FAIL;
        ps_power_request(PS_POWER_LOCK);CHECK(!ps_power_run(&ctx,&shell));
        CHECK(!render_calls&&!allocations&&!ps_power_active()&&network_resume_pending);unchanged_frame();
        if(failure<2)CHECK(!lock_begins);
    }
    test_view=(ps_view_t){360,640,false};
}
static void lock_and_restore(void){
    for(int visual=0;visual<5;visual++){
        fixture();style_override=visual;ps_power_request(PS_POWER_LOCK);CHECK(ps_power_run(&ctx,&shell));
        CHECK(power_state==1&&resume_frame&&allocations==1&&!cache_ready&&!game_worker&&game_paused);
        CHECK(render_calls==1&&reader_calls==1&&sleep_allowed&&network_stops==1);
        for(size_t i=0;i<sizeof(frame);i++)CHECK(resume_frame[i]==0x55);
        ps_power_wake();CHECK(!ps_power_active()&&ps_power_pending());
        ctx.pressed=ctx.released=ctx.request_return=ctx.request_menu=ctx.request_developer_menu=true;ctx.request_app=(void*)1;
        CHECK(ps_power_run(&ctx,&shell)&&!ps_power_pending()&&!allocations&&!game_paused);
        CHECK(ctx.consumed&&!ctx.pressed&&!ctx.released&&!ctx.request_app&&!ctx.request_menu&&!ctx.request_return&&!ctx.request_developer_menu);
        unchanged_frame();CHECK(render_calls==1&&network_resumes==1);
        complete_resume(true);ps_power_run(&ctx,&shell);CHECK(!network_resume_pending);
    }
}
static void failures(void){
    fixture();sound_ready=false;ps_power_request(PS_POWER_LOCK);CHECK(ps_power_run(&ctx,&shell)&&requested==PS_POWER_LOCK&&sound_paused&&!render_calls&&!reader_calls&&!lock_begins&&!allocations);
    CHECK(ps_power_run(&ctx,&shell)&&sound_pauses==2&&!lock_begins);sound_ready=true;
    CHECK(ps_power_run(&ctx,&shell)&&power_state==1&&lock_begins==1&&sound_paused);ps_power_wake();CHECK(!sound_paused&&sound_resumes==1);ps_power_run(&ctx,&shell);
    fixture();fail_malloc=true;ps_power_request(0);CHECK(!ps_power_run(&ctx,&shell)&&!reader_calls&&!network_stops&&!allocations&&strstr(toast,"内存"));
    fixture();fail_book=true;ps_power_request(0);CHECK(!ps_power_run(&ctx,&app_book)&&render_calls==1&&!game_prepares&&!allocations&&!area_presents&&network_resume_pending);unchanged_frame();
    fixture();fail_reader=true;ps_power_request(0);CHECK(!ps_power_run(&ctx,&shell)&&game_resumes==1&&!game_paused&&!network_stops&&!allocations);
    fixture();fail_games=true;ps_power_request(0);CHECK(!ps_power_run(&ctx,&shell)&&!reader_calls&&!game_paused&&!allocations);
    fixture();fail_home_suspend=true;ps_power_request(0);CHECK(!ps_power_run(&ctx,&shell)&&home_suspends==1&&home_resumes==1&&!network_stops&&!game_paused&&!allocations);
    fixture();usb_held=true;int previous_route=navigated;CHECK(ps_power_toggle(&ctx,&shell)&&!ps_power_pending());CHECK(ps_power_key_long(&ctx,&shell)&&navigated==previous_route);ps_power_request(0);CHECK(!ps_power_run(&ctx,&shell)&&!render_calls&&!reader_calls&&!network_stops);
    fixture();content_held=true;int content_route=navigated;CHECK(ps_power_toggle(&ctx,&shell)&&!ps_power_pending());CHECK(ps_power_key_long(&ctx,&shell)&&navigated==content_route);ps_power_request(0);CHECK(!ps_power_run(&ctx,&shell)&&!render_calls&&!reader_calls&&!network_stops);
    fixture();transfer_busy=true;ps_power_request(0);CHECK(!ps_power_run(&ctx,&shell)&&!render_calls&&!reader_calls&&!network_stops);
    fixture();lock_begin_error=ESP_FAIL;ps_power_request(0);CHECK(!ps_power_run(&ctx,&shell)&&!ps_power_pending()&&!allocations&&!game_paused&&network_resume_pending);unchanged_frame();
    fixture();radio_safe=false;ps_power_request(0);CHECK(ps_power_run(&ctx,&shell)&&!sleep_allowed&&draw_limited);ps_power_wake();ps_power_run(&ctx,&shell);
    fixture();radio_safe=false;ps_power_request(0);CHECK(ps_power_run(&ctx,&shell)&&radio_wait&&!sleep_allowed&&low_power_limited);
    int locked=lock_begins,ended=lock_ends;radio_safe=true;CHECK(ps_power_run(&ctx,&shell)&&!radio_wait&&sleep_allowed&&!low_power_limited&&lock_begins==locked+1&&lock_ends==ended+1);
    CHECK(power_state==1&&render_calls==1&&game_paused&&resume_frame);wake_source=APP_WAKE_TOUCH_DOUBLE;CHECK(ps_power_run(&ctx,&shell)&&!power_state&&!allocations);unchanged_frame();
    fixture();radio_safe=false;ps_power_request(0);CHECK(ps_power_run(&ctx,&shell));radio_safe=true;wake_source=APP_WAKE_ERROR;CHECK(ps_power_run(&ctx,&shell)&&!radio_wait&&low_power_limited);locked=lock_begins;wake_source=APP_WAKE_NONE;CHECK(ps_power_run(&ctx,&shell)&&lock_begins==locked);
    fixture();radio_safe=false;ps_power_request(0);CHECK(ps_power_run(&ctx,&shell));radio_safe=true;lock_begin_error=ESP_FAIL;CHECK(ps_power_run(&ctx,&shell)&&!power_state&&!allocations&&!game_paused);unchanged_frame();
    fixture();ps_power_request(3);CHECK(!ps_power_run(&ctx,&shell)&&!ps_power_active()&&!game_paused&&network_resume_pending&&strstr(toast,"关机失败"));
}
static void network_recovery(void){
    fixture();fail_network=true;ps_power_request(0);CHECK(!ps_power_run(&ctx,&shell)&&network_resume_pending&&!allocations&&!game_paused);
    CHECK(network_stops==1);ps_power_run(&ctx,&shell);CHECK(network_resumes==1&&!network_resume_waiting);
    ctx.now_ms+=100;ps_power_run(&ctx,&shell);CHECK(network_resumes==1);
    net.busy=false;net.suspended=true;ctx.now_ms+=500;ps_power_run(&ctx,&shell);CHECK(network_resumes==2&&network_resume_waiting);
    ctx.now_ms+=10;ps_power_run(&ctx,&shell);CHECK(network_resume_pending&&network_resumes==2);
    complete_resume(false);ctx.now_ms+=10;ps_power_run(&ctx,&shell);CHECK(network_resume_pending&&!network_resume_waiting);
    ctx.now_ms+=500;ps_power_run(&ctx,&shell);CHECK(network_resumes==3&&network_resume_waiting);
    complete_resume(true);ps_power_run(&ctx,&shell);CHECK(!network_resume_pending);
}
static void automatic(void){
    fixture();management_active=true;ctx.now_ms+=3600000;ps_power_poll(&ctx,&shell);CHECK(!ps_power_pending());management_active=false;ctx.now_ms+=APP_AUTO_SLEEP_MS-1;ps_power_poll(&ctx,&shell);CHECK(!ps_power_pending());ctx.now_ms++;ps_power_poll(&ctx,&shell);CHECK(ps_power_pending());
    fixture();management_active=true;ps_power_request(PS_POWER_LOCK);CHECK(ps_power_run(&ctx,&shell)&&ps_power_active());
    fixture();ctx.now_ms+=APP_AUTO_SLEEP_MS-1;ps_power_poll(&ctx,&shell);CHECK(!ps_power_pending());ctx.now_ms++;ps_power_poll(&ctx,&shell);CHECK(ps_power_pending());
    fixture();ctx.now_ms+=APP_AUTO_SLEEP_MS;keep_awake=true;ps_power_poll(&ctx,&shell);CHECK(!ps_power_pending());keep_awake=false;ctx.now_ms+=APP_AUTO_SLEEP_MS-1;ps_power_poll(&ctx,&child);CHECK(!ps_power_pending());ctx.now_ms++;ps_power_poll(&ctx,&child);CHECK(ps_power_pending());
    fixture();dock_ready=true;ctx.now_ms+=9999;ps_power_poll(&ctx,&shell);CHECK(!ps_power_pending());ctx.now_ms++;ps_power_poll(&ctx,&shell);CHECK(ps_power_pending());
    fixture();input_free=false;ctx.now_ms+=APP_AUTO_SLEEP_MS;ps_power_poll(&ctx,&shell);CHECK(!ps_power_pending());input_free=true;ctx.now_ms+=100;ps_power_poll(&ctx,&shell);CHECK(!ps_power_pending());
    fixture();transfer_busy=true;ctx.now_ms+=APP_AUTO_SLEEP_MS;ps_power_poll(&ctx,&shell);CHECK(!ps_power_pending());
    fixture();ctx.now_ms+=APP_AUTO_SLEEP_MS-1;ps_power_activity(ctx.now_ms);ctx.now_ms+=1;ps_power_poll(&ctx,&shell);CHECK(!ps_power_pending());
    fixture();ps_power_request(PS_POWER_SLEEP);CHECK(ps_power_run(&ctx,&shell)&&power_state==2);wake_source=APP_WAKE_TOUCH_DOUBLE;CHECK(ps_power_run(&ctx,&shell)&&power_state==2);wake_source=APP_WAKE_KEY;CHECK(ps_power_run(&ctx,&shell)&&!power_state&&!allocations);unchanged_frame();
}
static void power_button_only(void){
    for(int action=PS_POWER_LOCK;action<=PS_POWER_SLEEP;action++){
        fixture();CHECK(ps_power_set_doubletap(false));ps_power_request(action);CHECK(ps_power_run(&ctx,&shell)&&ps_power_active());
        int expected=action==PS_POWER_LOCK?1:2;
        cst836u_touch_t contact={.touched=true,.count=1,.x=300,.y=500};ctx.touch=&contact;
        for(int event=APP_WAKE_PICKUP;event<=APP_WAKE_TOUCH_DOUBLE;event++){
            wake_source=(app_wake_source_t)event;ctx.now_ms+=60;
            CHECK(ps_power_run(&ctx,&shell)&&power_state==expected&&allocations==1);
        }
        contact.touched=false;ctx.released=true;wake_source=APP_WAKE_KEY;ctx.now_ms+=60;
        CHECK(ps_power_run(&ctx,&shell)&&!ps_power_pending()&&!allocations&&ctx.consumed&&!ctx.released);
        CHECK(home_suspends==1&&home_resumes==1&&lock_begins==1&&lock_ends==1);unchanged_frame();
    }
    for(int idle=0;idle<2;idle++){
        fixture();radio_safe=!idle;int64_t start=ctx.now_ms;
        for(int cycle=0;cycle<8;cycle++){
            cache_ready=true;wake_source=APP_WAKE_NONE;ctx.now_ms+=40;
            CHECK(ps_power_toggle(&ctx,&shell)&&ps_power_pending());
            CHECK(ps_power_run(&ctx,&shell)&&power_state==1&&sleep_allowed==!idle);
            wake_source=APP_WAKE_KEY;ctx.now_ms+=40;
            CHECK(ps_power_run(&ctx,&shell)&&!ps_power_pending()&&!allocations&&!game_paused);
            CHECK(ctx.consumed&&lock_begins==cycle+1&&lock_ends==cycle+1);unchanged_frame();
            complete_resume(true);wake_source=APP_WAKE_NONE;ps_power_run(&ctx,&shell);
        }
        CHECK(ctx.now_ms-start<2000&&home_suspends==8&&home_resumes==8&&network_stops==8);
    }
}
static void style_entry(void){
    fixture();confirmation=-1;
    for(int landscape=0;landscape<2;landscape++){
        test_view=(ps_view_t){landscape?640:360,landscape?360:640,landscape};
        style_w=style_h=0;navigated=-1;ps_power_render(frame);
        CHECK(style_w==(landscape?296:328)&&style_h==40&&style_y==(landscape?188:438));
        ps_event_t event={PS_TAP,style_x+style_w/2,style_y+style_h/2};
        CHECK(ps_power_event(&event)&&navigated==PS_SLEEP_SETTINGS);
    }
    test_view=(ps_view_t){360,640,false};
}
static void confirmation_back(void){
    for(int landscape=0;landscape<2;landscape++)for(int action=2;action<=3;action++){
        fixture();test_view=(ps_view_t){landscape?640:360,landscape?360:640,landscape};ps_power_enter();
        int col=settings_column(test_view),cw=(col-8)/2,rh=landscape?36:44;
        ps_event_t event={PS_TAP,16+(action%2)*(cw+8)+cw/2,76+(action/2)*(rh+6)+rh/2};
        CHECK(ps_power_event(&event)&&confirmation==action&&!ps_power_pending());
        CHECK(ps_power_back()&&confirmation==-1&&!ps_power_pending()&&!back_calls&&!off_calls);
        CHECK(!ps_power_back()&&!ps_power_pending());
    }
    fixture();ps_power_enter();quick_menu=true;CHECK(!ps_power_back()&&quick_menu&&!ps_power_pending());
    test_view=(ps_view_t){360,640,false};
}
static void new_power_options(void){
    fixture();CHECK(ps_power_auto_minutes()==3&&ps_power_doubletap());
    CHECK(ps_power_set_auto_minutes(10)&&ps_power_set_doubletap(false));
    power_options_loaded=false;auto_minutes=3;doubletap_enabled=true;
    CHECK(ps_power_auto_minutes()==10&&!ps_power_doubletap());
    CHECK(!ps_power_set_auto_minutes(2));
    fail_store=true;CHECK(!ps_power_set_auto_minutes(1)&&ps_power_auto_minutes()==10&&strstr(toast,"保存失败"));fail_store=false;
    CHECK(ps_power_set_auto_minutes(0));dock_ready=true;ctx.now_ms+=3600000;ps_power_poll(&ctx,&shell);CHECK(!ps_power_pending());
    for(unsigned i=1;i<sizeof(auto_choices);i++){
        fixture();CHECK(ps_power_set_auto_minutes(auto_choices[i]));ctx.now_ms+=(int64_t)auto_choices[i]*60000-1;ps_power_poll(&ctx,&child);CHECK(!ps_power_pending());ctx.now_ms++;ps_power_poll(&ctx,&child);CHECK(ps_power_pending());
    }
    fixture();CHECK(ps_power_set_auto_minutes(1));keep_awake=true;ctx.now_ms+=3600000;ps_power_poll(&ctx,&shell);CHECK(!ps_power_pending()&&keep_awake);
    ps_power_request(PS_POWER_LOCK);CHECK(ps_power_run(&ctx,&shell)&&power_state==1&&taps_allowed);wake_source=APP_WAKE_TOUCH_DOUBLE;CHECK(ps_power_run(&ctx,&shell)&&!ps_power_pending());unchanged_frame();
    fixture();CHECK(ps_power_key_long(&ctx,&shell)&&quick_menu&&navigated==PS_POWER&&!ps_power_pending());
    ps_event_t cancel={PS_TAP,180,320};CHECK(ps_power_event(&cancel)&&!quick_menu&&back_calls==1&&!ps_power_pending());
    fixture();ps_power_request(PS_POWER_LOCK);CHECK(ps_power_run(&ctx,&shell));wake_source=APP_WAKE_KEY_LONG;
    CHECK(ps_power_run(&ctx,&shell)&&!ps_power_pending()&&quick_menu&&navigated==PS_POWER&&!allocations);unchanged_frame();
    fixture();power_state=4;CHECK(ps_power_toggle(&ctx,&shell)&&power_state==4);ps_power_wake();CHECK(power_state==4);CHECK(ps_power_key_long(&ctx,&shell)&&power_state==4&&!quick_menu);power_state=0;
    fixture();ps_power_request(PS_POWER_OFF);CHECK(!ps_power_run(&ctx,&shell)&&off_hint_drawn&&off_calls==1&&full_presents==2&&!allocations&&strstr(toast,"关机失败"));unchanged_frame();
    fixture();fail_refresh=true;ps_power_request(PS_POWER_OFF);CHECK(!ps_power_run(&ctx,&shell)&&!off_calls&&draw_errors==1&&full_presents==2&&!allocations&&strstr(toast,"刷新失败"));unchanged_frame();
}
static void image_lock_policy(void){
    fixture();test_style=4;ps_power_request(PS_POWER_LOCK);CHECK(ps_power_run(&ctx,&shell)&&power_state==1&&style==4&&lock_image_prepares==1&&lock_image_renders==1);
    for(size_t i=0;i<sizeof(frame);i++)CHECK(frame[i]==0x33);
    wake_source=APP_WAKE_KEY;CHECK(ps_power_run(&ctx,&shell)&&!power_state&&lock_image_releases==1);unchanged_frame();
    fixture();test_style=4;lock_image_ready=false;ps_power_request(PS_POWER_LOCK);CHECK(ps_power_run(&ctx,&shell)&&power_state==1&&style==3&&test_style==4&&!lock_image_renders);unchanged_frame();
    ps_power_wake();ps_power_run(&ctx,&shell);
    fixture();test_style=4;ps_power_request(PS_POWER_OFF);CHECK(!ps_power_run(&ctx,&shell)&&off_hint_drawn&&lock_image_renders==1&&!allocations);unchanged_frame();
}
static void board_lock_policy(void){
    fixture();test_style=5;external_power=true;management_active=true;
    CHECK(ps_power_set_auto_minutes(1));ctx.now_ms+=60001;ps_power_poll(&ctx,&shell);
    CHECK(requested==PS_POWER_LOCK);CHECK(ps_power_run(&ctx,&shell));
    CHECK(board_lock&&style==5&&power_state==1&&!sleep_allowed&&taps_allowed);
    CHECK(!network_starts&&!network_stops&&home_suspends==1&&desktop_presents==1&&!low_power_limited);
    unsigned modes=mode_presents,received=desktop_presents;
    for(unsigned i=0;i<4;i++)CHECK(ps_power_run(&ctx,&shell));
    CHECK(mode_presents==modes&&desktop_presents==received);
    desktop_version++;CHECK(ps_power_run(&ctx,&shell));
    CHECK(mode_presents==modes+1&&desktop_presents==received+1&&board_revision==desktop_version);
    external_power=false;dock_network_wait=true;net.busy=true;
    CHECK(ps_power_run(&ctx,&shell)&&board_lock&&board_stopping&&!sleep_allowed);
    net.busy=false;net.suspended=true;
    CHECK(ps_power_run(&ctx,&shell)&&!board_lock&&!board_stopping&&sleep_allowed&&lock_begins==2);
    unsigned ticks=desktop_ticks;desktop_version++;
    CHECK(ps_power_run(&ctx,&shell)&&desktop_ticks==ticks);
    wake_source=APP_WAKE_TOUCH_DOUBLE;CHECK(ps_power_run(&ctx,&shell)&&!ps_power_pending());unchanged_frame();
    for(int unavailable=0;unavailable<2;unavailable++){
        fixture();test_style=5;external_power=unavailable!=0;desktop_ready=unavailable==0;
        ps_power_request(PS_POWER_LOCK);CHECK(ps_power_run(&ctx,&shell));
        CHECK(style==3&&!board_lock&&sleep_allowed&&network_stops==1&&!desktop_presents&&test_style==5);
        wake_source=APP_WAKE_KEY;CHECK(ps_power_run(&ctx,&shell)&&!ps_power_pending());unchanged_frame();
    }
    fixture();test_style=5;external_power=true;ps_power_request(PS_POWER_SLEEP);
    CHECK(ps_power_run(&ctx,&shell)&&power_state==2&&style==3&&!board_lock&&sleep_allowed&&network_stops==1);
    wake_source=APP_WAKE_KEY;CHECK(ps_power_run(&ctx,&shell)&&!ps_power_pending());unchanged_frame();
}
// 同步任务与电源状态共享准入屏障，唤醒和失败均归还。/ Sync and power share admission; wake and failure release it.
static void weread_ownership(void){
    fixture();weread_busy=true;ps_power_request(0);
    CHECK(!ps_power_run(&ctx,&shell)&&!weread_paused&&!render_calls&&!allocations);
    weread_busy=false;ps_power_request(0);CHECK(ps_power_run(&ctx,&shell)&&weread_paused);
    ps_power_wake();CHECK(!weread_paused);restore(&ctx);
    fixture();fail_malloc=true;ps_power_request(0);CHECK(!ps_power_run(&ctx,&shell)&&!weread_paused);
    fixture();weread_busy=true;ctx.now_ms+=APP_AUTO_SLEEP_MS;ps_power_poll(&ctx,&shell);CHECK(!ps_power_pending());
}
int main(void){weread_ownership();board_lock_policy();parallel_lock_prepare();retained_page_fast_lock();submitted_capture();image_lock_policy();new_power_options();lock_and_restore();failures();network_recovery();automatic();power_button_only();style_entry();confirmation_back();fixture();printf("power lifecycle: %u checks PASS; stable submitted capture without rerender and dirty/debt fallback, parallel real network shutdown with ordered data saves, no intermediate lock feedback, exact retained frame and failure restoration, optional IRQ double-tap, persisted idle/wake settings, long-key menu, off-face-before-PMU/failure recovery, repeated power lock/wake, joined games, delayed/failed async resume, actual BLE lifecycle gate and unified idle\n",checks);return 0;}

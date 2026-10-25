/* SPDX-License-Identifier: Apache-2.0
 * 中文：锁屏/休眠/关机/重启的唯一产品入口；保留页面实例与恢复画面。
 * English: One product entry for lock/sleep/off/restart, retaining the page instance and resume frame.
 * 冻结：没有经过真实外设休眠的预览不声称功耗；失败不丢弃未保存内容。
 * Frozen: A host preview never claims measured power savings; failures retain unsaved content.
 * 用户新增：电源与锁屏是唯一设置入口，锁屏外观子页集中管理样式及各样式专属选项。
 * User addition: Power and Lock is the sole settings entry; its Appearance child owns styles and style-specific options.
 * 用户修订：返回优先关闭关机或重启确认框，保留电源设置页面且不提交操作。
 * User revision: Back dismisses shutdown/restart confirmation before leaving power settings, without submitting an action.
 * 冻结：全局轮询承接限频网络授时，PMU独占、USB/存储忙或交互期间延后RTC提交。
 * Frozen: Global polling hosts rate-limited network time service, deferring RTC commits while PMU-owned, USB/storage-busy or interacting.
 * 冻结：已授予WiFi内容租约时和USB独占一样禁止手动锁屏、关机与重启，不提前恢复文件消费者。
 * Frozen: An active WiFi content lease blocks manual lock, shutdown and restart like USB ownership, without prematurely restoring file consumers.
 * 用户修订：可选灵犀画板锁屏仅在PMU确认外电时保留网络；普通阅读/画稿不被推送切换，断电后收束网络再浅睡。
 * User revision: Optional Lingxi Board lock retains networking only with PMU-confirmed external power; pushes never switch reading/drawing, and unplugging joins networking before light sleep.
 */
#include "pico_power.h"
#include "pico_datetime.h"
#include "pico_sound.h"
#include "../power_touch.h"
#include "pico_lock_covers.h"
#include "pico_desktop.h"
#include "pico_weread.h"
#include "pico_connectivity.h"
#include "../apps/app_book.h"
#include "read_pico_ble_pager.h"
#include "read_pico_connectivity.h"
#include "read_pico_home_link.h"
#include "pico_home_pga.h"
#include "read_pico_management.h"
#include "display.h"
#include "e0470_epaper_waveform.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "settings.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#ifndef PICO_NATIVE_PREVIEW
#include "sleep.h"
#include "esp_system.h"
#include "esp_timer.h"
#else
#include "native_preview_hal.h"
#endif

static int requested=-1,power_state,style,cover_layout,confirmation=-1,style_override=-1;
static bool initialized,low_power_limited,radio_wait,power_options_loaded,quick_menu;
static bool board_lock,board_stopping;
static unsigned board_revision;
static uint8_t auto_minutes=3;
static bool doubletap_enabled=true;
static const uint8_t auto_choices[]={0,1,3,5,10,30};
#ifdef PICO_NATIVE_PREVIEW
static power_touch_t preview_touch;
#endif
static uint8_t *resume_frame;
static bool activity_valid,network_resume_pending,network_resume_waiting;
static int64_t last_activity_ms,network_retry_at;
static uint32_t network_resume_seq;
static const size_t FRAME_BYTES=1216u*684u/2u;

static void load_options(void) {
    if(power_options_loaded)return;
    uint8_t value[2];
    if(ps_load("powercfg",value,sizeof(value))&&value[1]<=1){
        for(unsigned i=0;i<sizeof(auto_choices);i++)if(value[0]==auto_choices[i]){auto_minutes=value[0];doubletap_enabled=value[1]!=0;break;}
    }
    power_options_loaded=true;
}
unsigned ps_power_auto_minutes(void){load_options();return auto_minutes;}
bool ps_power_doubletap(void){load_options();return doubletap_enabled;}
static bool save_options(uint8_t minutes,bool taps){
    uint8_t value[2]={minutes,taps?1:0};
    if(!ps_store("powercfg",value,sizeof(value))){ps_toast("电源设置保存失败");return false;}
    auto_minutes=minutes;doubletap_enabled=taps;power_options_loaded=true;ps_power_activity(ps_now());ps_dirty();return true;
}
bool ps_power_set_auto_minutes(unsigned minutes){
    load_options();for(unsigned i=0;i<sizeof(auto_choices);i++)if(minutes==auto_choices[i])return save_options((uint8_t)minutes,doubletap_enabled);
    return false;
}
bool ps_power_set_doubletap(bool enabled){load_options();return save_options(auto_minutes,enabled);}
bool ps_power_configure(unsigned minutes,bool doubletap){
    load_options();for(unsigned i=0;i<sizeof(auto_choices);i++)if(minutes==auto_choices[i])return save_options((uint8_t)minutes,doubletap);
    return false;
}
static void drain_pager(void) {
    for(int i=0;i<32&&read_pico_ble_pager_poll_event()!=0;i++) {}
}
void ps_power_preview(int preview_style) { if(preview_style>=0&&preview_style<=5&&!power_state){style_override=preview_style;ps_power_request(PS_POWER_LOCK);} }
bool ps_power_active(void) { return power_state!=0; }
bool ps_power_board_visible(void) { return board_lock&&style==5&&power_state==1; }
int ps_power_state(void) { return power_state; }
bool ps_power_pending(void) { return requested>=0||power_state!=0||resume_frame!=NULL; }
void ps_power_request(int action) { if(action>=0&&action<=3&&!power_state)requested=action; }
void ps_power_activity(int64_t now_ms) { last_activity_ms=now_ms;activity_valid=true; }
static void network_recover_begin(void) {
    network_resume_pending=true;network_resume_waiting=false;network_retry_at=0;
}
static void network_recover_tick(int64_t now_ms) {
    if(!network_resume_pending||power_state||now_ms<network_retry_at)return;
    read_pico_connectivity_resume_status_t state;
    read_pico_connectivity_get_resume_status(&state);
    if(network_resume_waiting) {
        if(state.busy||state.completed_seq<network_resume_seq)return;
        network_resume_waiting=false;
        if(!state.suspended&&state.last_error==ESP_OK){network_resume_pending=false;return;}
        network_retry_at=now_ms+500;return;
    }
    // quiesce超时仍可能随后完成；入队成功也须等待完成序号，不能当成恢复完成。
    // A timed-out quiesce may finish later; accepted resume commands still require completion.
    if(read_pico_connectivity_resume()==ESP_OK) {
        read_pico_connectivity_get_resume_status(&state);
        network_resume_seq=state.submitted_seq;network_resume_waiting=true;
    } else network_retry_at=now_ms+500;
}
static void automatic_tick(app_ctx_t *ctx,const app_desc_t *current) {
    if(!activity_valid||ctx->now_ms<last_activity_ms)ps_power_activity(ctx->now_ms);
    if(power_state||requested>=0||resume_frame)return;
    bool board_candidate=ps_sleep_style()==5&&ps_external_power();
    bool blocked=(read_pico_management_active()&&!board_candidate)||ps_weread_busy()||app_settings_test_awake()||ps_storage_busy()||ps_usb_busy()||ps_transfer_prepared()||
        !ps_background_io_allowed()||current->holds_pmu||
        (!current->product_shell&&current->blocks_idle_sleep)||
        (ctx->touch&&ctx->touch->touched);
    if(blocked){ps_power_activity(ctx->now_ms);return;}
    int64_t elapsed=ctx->now_ms-last_activity_ms;
    unsigned minutes=ps_power_auto_minutes();
    if(minutes&&(elapsed>=(int64_t)minutes*60000||(elapsed>=10000&&ps_power_dock_ready())))ps_power_request(PS_POWER_LOCK);
}
void ps_power_poll(app_ctx_t *ctx,const app_desc_t *current) {
    ps_datetime_service_tick(!current->holds_pmu&&!ps_usb_busy()&&!ps_storage_busy()&&ps_background_io_allowed());
    network_recover_tick(ctx->now_ms);automatic_tick(ctx,current);
}
bool ps_power_toggle(app_ctx_t *ctx,const app_desc_t *current) {
    (void)ctx;(void)current;
    if(ps_storage_busy()||ps_usb_busy()||ps_usb_held()||ps_content_held()||power_state>=3)return true;
    if(power_state)ps_power_wake();else ps_power_request(PS_POWER_LOCK);
    return true;
}
int ps_power_cover_layout(void) {
    if(!initialized){uint8_t saved=0;if(ps_load("coverlayout",&saved,1)&&saved<=4)cover_layout=saved;initialized=true;}
    return cover_layout;
}
void ps_power_set_cover_layout(int layout) {
    if(layout<0||layout>4)return;
    uint8_t saved=(uint8_t)layout;
    if(ps_store("coverlayout",&saved,1))cover_layout=layout;
    else ps_toast("封面布局保存失败");
    initialized=true;ps_dirty();
}
static void lock_hint(uint8_t *fb) {
    ps_view_t v=ps_view();int width=v.w>400?320:v.w-36,y=v.h-((style<=1&&ps_battery_percent()>=0&&ps_battery_percent()<=10&&!ps_charging())?88:53),x=(v.w-width)/2;
    ps_box(fb,x,y,width,36,6,15,true);ps_box(fb,x,y,width,36,6,5,false);
    const char *label=power_state==4?"已关机 · 长按电源键开机":power_state==2?"已休眠 · 按电源键恢复":ps_power_doubletap()?"已锁屏 · 双击屏幕唤醒":"已锁屏 · 点按电源键唤醒";
    ps_center(fb,v.w/2,y+11,12,label,0);
    if(low_power_limited){ps_fill(fb,0,0,v.w,24,15);ps_center(fb,v.w/2,7,10,"低功耗待机受限 · 电源键可唤醒",4);}
}
static bool paint_lock(app_ctx_t *ctx,bool full) {
    // 原页与已提交基准完全一致、且没有余相或浮层债务时，只驱动新增锁屏标识。
    // Drive only the added lock badge when the retained page exactly matches the submitted baseline and no phase/overlay debt remains.
    bool badge_only=full&&style==3&&power_state<=2&&!low_power_limited&&resume_frame&&ps_power_partial_safe()&&
        ctx->hl->back_fb&&!memcmp(resume_frame,ctx->hl->back_fb,FRAME_BYTES);
    if(style==5)ps_desktop_render(ctx->fb);
    else if(style==4)ps_lock_image_render(ctx->fb);
    else if(style==3&&resume_frame)memcpy(ctx->fb,resume_frame,FRAME_BYTES);
    else {memset(ctx->fb,255,FRAME_BYTES);if(style==2)ps_lock_covers_render(ctx->fb);else ps_home_standby_render(ctx->fb,style);}
    lock_hint(ctx->fb);
    ps_view_t v=ps_view();
    int result=badge_only?update_display_area_with(ctx->hl,&E0470_WAVEFORM,MODE_GL16,ps_rect(0,v.h-64,v.w,64)):
        full?update_display_full(ctx->hl):update_display_mode(ctx->hl,MODE_GL16);
    guard_draw_result(ctx->hl,result);return result==0;
}
void ps_power_wake(void) {
    if(!power_state||power_state==4)return;
#ifndef PICO_NATIVE_PREVIEW
    app_product_lock_end();
#endif
    power_state=0;requested=-1;radio_wait=board_lock=board_stopping=false;ps_sound_resume();
    ps_weread_resume();
    drain_pager();ps_global_cancel_input();
    ps_lock_covers_release();ps_lock_image_release();
    ps_power_activity(ps_now());read_pico_home_link_resume();ps_home_pga_resume();network_recover_begin();
    ps_dirty();
}
static void restore(app_ctx_t *ctx) {
    if(resume_frame){memcpy(ctx->fb,resume_frame,FRAME_BYTES);guard_draw_result(ctx->hl,update_display_full(ctx->hl));free(resume_frame);resume_frame=NULL;}
    // 先恢复屏幕，再恢复整屏应用缓存，避免下一次强刷早于后台tick时出现空态。
    // Restore the screen first, then its app cache before an immediate redraw can precede the background tick.
    ps_route_t route=ps_route();
    if(route==PS_CANVAS||route==PS_CLOUDFLARE)(void)ps_desktop_prepare_app(route==PS_CLOUDFLARE);
    ctx->pressed=ctx->released=false;ctx->consumed=true;ctx->request_app=NULL;ctx->request_return=false;ctx->request_menu=false;
    ctx->request_developer_menu=false;
    // 恢复画面后才允许AI重新调度；贪吃蛇保留stop()产生的暂停状态。
    // Resume AI scheduling only after restoring the image; Snake retains stop()'s paused state.
    ps_games_power_resume();ps_power_activity(ctx->now_ms);
}
bool ps_power_key_long(app_ctx_t *ctx,const app_desc_t *current) {
    (void)current;
    if(ps_storage_busy()||ps_usb_busy()||ps_usb_held()||ps_content_held()){ps_toast("请先结束存储操作");return true;}
    if(power_state==4){
#ifdef PICO_NATIVE_PREVIEW
        // 只模拟长按开机后的首页；不声称重置了硬件，不重置用户持久数据。
        // Simulate the post-boot home only; do not claim a hardware reset or reset persistent user data.
        power_state=1;ps_power_wake();restore(ctx);ps_navigate(PS_SHELF);ps_dirty();
#endif
        return true;
    }
    if(power_state>2)return true;
    if(power_state){ps_power_wake();restore(ctx);}
    requested=-1;ps_global_cancel_input();ps_power_activity(ctx->now_ms);
    ps_navigate(PS_POWER);quick_menu=true;ps_dirty();return true;
}
static bool abort_begin(app_ctx_t *ctx,const char *message,bool resume_network) {
    ps_weread_resume();
    board_lock=board_stopping=false;
    ps_sound_resume();
    ps_lock_image_release();
    if(resume_frame){free(resume_frame);resume_frame=NULL;}
    ps_games_power_resume();read_pico_home_link_resume();ps_home_pga_resume();ps_power_activity(ctx->now_ms);
    if(resume_network)network_recover_begin();
    ps_toast(message);return false;
}
static bool begin(app_ctx_t *ctx,const app_desc_t *current,int action) {
    if(ps_storage_busy()||ps_usb_busy()||ps_usb_held()||ps_content_held()||ps_transfer_prepared()){ps_toast("请先结束存储操作或文件传输");return false;}
    if(!ps_weread_pause()){ps_toast("请先结束微信读书同步");return false;}
    const int64_t started=esp_timer_get_time();
    int chosen=style_override>=0?style_override:ps_sleep_style();
    board_lock=action==PS_POWER_LOCK&&chosen==5&&ps_external_power();board_stopping=false;
    if(current==&app_book)app_book_global_prepare();
    ps_global_cancel_input();drain_pager();
    if(board_lock){(void)ps_desktop_tick();board_lock=ps_desktop_prepare();}
    if(action<=PS_POWER_SLEEP||action==PS_POWER_OFF) {
        resume_frame=heap_caps_malloc(FRAME_BYTES,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
        if(!resume_frame)return abort_begin(ctx,"锁屏内存不足，请稍后重试",false);
    }
    // 先禁止新的联网工作并请求现有后台收束，与必须落盘及绘制并行；最终仍检查真实完成。
    // Block new network work and request existing workers to stop in parallel with required saves/rendering; completion remains mandatory.
    (void)read_pico_home_link_suspend(0);(void)ps_home_pga_suspend(0);
    if(!board_lock)(void)read_pico_connectivity_quiesce(0);
    if(resume_frame){
        // 稳定时复用已提交画面；取消输入或未提交绘制仍按普通绘制恢复，必须在保存前捕获。
        // Reuse a stable submitted frame; cancelled input or unsubmitted paint still requires the normal render before saving.
        const uint8_t *source=ctx->hl&&ctx->hl->back_fb&&ps_power_partial_safe()&&
            !memcmp(ctx->fb,ctx->hl->back_fb,FRAME_BYTES)?ctx->hl->back_fb:NULL;
        if(!source){
            if(current->render)current->render(ctx,ctx->fb);
            ps_global_draw_controls(current,ctx->fb);source=ctx->fb;
        }
        memcpy(resume_frame,source,FRAME_BYTES);
    }
    const int64_t captured=esp_timer_get_time();
    if(current==&app_book&&!app_book_power_prepare())return abort_begin(ctx,"阅读进度未能保存，请检查TF卡",true);
    if(!ps_games_power_prepare()||!ps_reader_power_save()||!ps_home_storage_save())
        return abort_begin(ctx,"内容未保存，暂不进入电源状态",true);
    const int64_t saved=esp_timer_get_time();
    if(!read_pico_home_link_suspend(2500)||!ps_home_pga_suspend(2500))return abort_begin(ctx,"家庭网络任务尚未结束，请稍后重试",true);
    if(!board_lock&&!ps_connectivity_shutdown())return abort_begin(ctx,"网络传输尚未结束，请稍后重试",true);
    network_resume_pending=network_resume_waiting=false;
    const int64_t networked=esp_timer_get_time();
    if(action==PS_POWER_RESTART){
#ifndef PICO_NATIVE_PREVIEW
        esp_restart();
#else
        power_state=3;memset(ctx->fb,255,FRAME_BYTES);ps_center(ctx->fb,ps_view().w/2,ps_view().h/2,24,"正在重启",0);guard_draw_result(ctx->hl,update_display_full(ctx->hl));return true;
#endif
    }
    if(action==PS_POWER_OFF){
        style=ps_sleep_style();if(style==5||(style==4&&!ps_lock_image_prepare()))style=3;power_state=4;low_power_limited=radio_wait=false;
        if(style==2)ps_lock_covers_prepare(ps_power_cover_layout());else if(style<=1)ps_home_standby_begin(style);
        bool shown=paint_lock(ctx,true);
#ifdef PICO_NATIVE_PREVIEW
        if(shown)return true;
#else
        if(shown)(void)app_enter_host_sleep(APP_SLEEP_OFF);
#endif
        power_state=0;ps_lock_covers_release();
        if(resume_frame){memcpy(ctx->fb,resume_frame,FRAME_BYTES);guard_draw_result(ctx->hl,update_display_full(ctx->hl));}
        return abort_begin(ctx,shown?"关机失败，请重试":"关机画面刷新失败，已取消关机",true);
    }
    style=chosen;style_override=-1;
    if(style==5&&!board_lock)style=3;
    if(style==4&&!ps_lock_image_prepare())style=3;
    power_state=action==PS_POWER_SLEEP?2:1;
    if(style==2)ps_lock_covers_prepare(ps_power_cover_layout());
    else if(style<=1)ps_home_standby_begin(style);
    low_power_limited=!board_lock&&!read_pico_ble_pager_light_sleep_safe();radio_wait=low_power_limited;
    const int64_t prepared=esp_timer_get_time();
    if(!paint_lock(ctx,true)){ps_power_wake();restore(ctx);ps_toast("无法进入锁屏，请重试");return false;}
    if(board_lock){board_revision=ps_desktop_revision();ps_desktop_presented(true);}
    const int64_t painted=esp_timer_get_time();
#ifdef PICO_NATIVE_PREVIEW
    memset(&preview_touch,0,sizeof(preview_touch));
    preview_touch.blocked=ctx->touch&&ctx->touch->touched;
    preview_touch.down=preview_touch.blocked;
#else
    if(app_product_lock_begin(!board_lock&&!low_power_limited,power_state==1&&ps_power_doubletap())!=ESP_OK){ps_power_wake();restore(ctx);ps_toast("无法进入锁屏，请重试");return false;}
#endif
    const int64_t armed=esp_timer_get_time();
    ESP_LOGI("pico_power","lock action=%d capture_ms=%lld save_ms=%lld network_ms=%lld prepare_ms=%lld paint_ms=%lld arm_ms=%lld total_ms=%lld",action,
        (long long)((captured-started)/1000),(long long)((saved-captured)/1000),(long long)((networked-saved)/1000),
        (long long)((prepared-networked)/1000),(long long)((painted-prepared)/1000),(long long)((armed-painted)/1000),(long long)((armed-started)/1000));
    return true;
}
static void board_lock_tick(app_ctx_t *ctx) {
    if(!board_lock)return;
    if(!ps_external_power())board_stopping=true;
    if(board_stopping){
        // 断电后不再采纳新画面，先等网络汇合再进入原有低功耗路径。
        // Stop adopting frames after unplugging, joining networking before the existing low-power path.
        esp_err_t stopped=read_pico_connectivity_quiesce(0);
        read_pico_connectivity_resume_status_t net;read_pico_connectivity_get_resume_status(&net);
        if(stopped!=ESP_OK&&(!net.suspended||net.busy))return;
        board_lock=board_stopping=false;low_power_limited=!read_pico_ble_pager_light_sleep_safe();radio_wait=low_power_limited;
#ifndef PICO_NATIVE_PREVIEW
        app_product_lock_end();
        if(app_product_lock_begin(!low_power_limited,power_state==1&&ps_power_doubletap())!=ESP_OK)low_power_limited=true;
#endif
        return;
    }
    read_pico_management_tick();
    (void)ps_desktop_tick();
    unsigned next=ps_desktop_revision();
    if(next!=board_revision){
        bool shown=paint_lock(ctx,false);ps_desktop_presented(shown);
        if(shown)board_revision=next;
    }
}
bool ps_power_run(app_ctx_t *ctx,const app_desc_t *current) {
    ps_power_poll(ctx,current);
    if(requested>=0){
        // 不在UI线程等短音；停收后下一轮检查空闲，进入低功耗前不留任务延时。
        // Never wait for a tone on the UI thread; stop admission and recheck next tick, leaving no audio deadline across sleep.
        ps_sound_pause();if(!ps_sound_quiet())return true;
        int action=requested;requested=-1;if(!begin(ctx,current,action)){ps_sound_resume();return false;}
    }
    if(!power_state){if(resume_frame){restore(ctx);return true;}return false;}
    ps_power_poll_hardware(ctx);
    board_lock_tick(ctx);
#ifdef PICO_NATIVE_PREVIEW
    if(power_state==3){ps_power_wake();restore(ctx);return true;}
    if(power_state==1&&ps_power_doubletap()&&ctx->touch&&
       power_touch_feed(&preview_touch,true,ctx->touch->touched,ctx->touch->count,ctx->touch->x,ctx->touch->y,ctx->now_ms)){
        ps_power_wake();restore(ctx);return true;
    }
#else
    app_wake_source_t source=APP_WAKE_NONE;
    // 无线异步停妥后重试浅睡一次；硬件浅睡错误不进入重复重试。/ Rearm once after asynchronous radio shutdown; hardware sleep errors never enter a retry loop.
    if(radio_wait&&read_pico_ble_pager_light_sleep_safe()){
        radio_wait=false;app_product_lock_end();
        if(app_product_lock_begin(true,power_state==1&&ps_power_doubletap())!=ESP_OK)source=APP_WAKE_ERROR;
        else{low_power_limited=false;paint_lock(ctx,false);}
    }
    if(source!=APP_WAKE_ERROR)source=app_product_lock_wait_step(style==0||board_lock?140:1000);
    ctx->now_ms=esp_timer_get_time()/1000;
    if(source==APP_WAKE_ERROR){radio_wait=false;app_product_lock_end();if(app_product_lock_begin(false,power_state==1&&ps_power_doubletap())!=ESP_OK){ps_power_wake();restore(ctx);return true;}low_power_limited=true;paint_lock(ctx,false);}
    if(source==APP_WAKE_KEY_LONG){ps_power_key_long(ctx,current);return true;}
    if(source==APP_WAKE_KEY||(source==APP_WAKE_TOUCH_DOUBLE&&power_state==1&&ps_power_doubletap())){ps_power_wake();restore(ctx);return true;}
#endif
    if(style<=1&&power_state<=2&&ps_home_standby_tick(style))paint_lock(ctx,false);
    ctx->consumed=true;ctx->pressed=ctx->released=false;return true;
}
void ps_power_enter(void) { quick_menu=false;confirmation=-1;(void)ps_power_cover_layout(); }
bool ps_power_back(void) {
    if(confirmation<0)return false;
    confirmation=-1;return true;
}
static int settings_column(ps_view_t v){return v.landscape?(v.w-48)/2:v.w-32;}
static void quick_geometry(ps_view_t v,int i,int *x,int *y,int *w,int *h){
    *w=v.landscape?(v.w-48)/2:v.w-64;*h=v.landscape?68:56;
    *x=v.landscape?16+(i%2)*(*w+16):32;*y=88+(v.landscape?i/2:i)*(*h+12);
}
static int appearance_y(ps_view_t v){return v.landscape?188:438;}
void ps_power_render(uint8_t *fb) {
    ps_view_t v=ps_view();ps_heading(fb,quick_menu?"电源菜单":"电源与锁屏",NULL);
    if(quick_menu){const char *items[]={"关机","重启","锁屏","取消"};for(int i=0;i<4;i++){int x,y,w,h;quick_geometry(v,i,&x,&y,&w,&h);ps_button(fb,x,y,w,h,items[i],false);}return;}
    int col=settings_column(v),right=v.landscape?32+col:16,rh=v.landscape?36:44,cw=(col-8)/2;
    if(confirmation>=0){
        ps_box(fb,12,94,v.w-24,154,8,15,true);ps_box(fb,12,94,v.w-24,154,8,6,false);
        ps_center(fb,v.w/2,112,21,confirmation==2?"确认重启设备？":"确认关机？",0);
        ps_button(fb,18,180,(v.w-44)/2,44,"取消",false);ps_button(fb,26+(v.w-44)/2,180,(v.w-44)/2,44,"确认",true);return;
    }
    const char *actions[]={"立即锁屏","深度休眠","重启","关机"};
    for(int i=0;i<4;i++)ps_button(fb,16+(i%2)*(cw+8),76+(i/2)*(rh+6),cw,rh,actions[i],false);
    int ay=v.landscape?176:200,aw=(col-12)/3;
    ps_text(fb,16,ay,13,"自动锁屏时间",4);
    const char *labels[]={"关闭","1 分钟","3 分钟","5 分钟","10 分钟","30 分钟"};
    for(int i=0;i<6;i++)ps_button(fb,16+(i%3)*(aw+6),ay+22+(i/3)*36,aw,32,labels[i],ps_power_auto_minutes()==auto_choices[i]);
    if(app_settings_test_awake())ps_wrap(fb,16,ay+98,col,11,2,"保持唤醒已开启，自动锁屏暂停",5);
    int ty=v.landscape?76:332;
    ps_button(fb,right,ty,col,v.landscape?36:44,ps_power_doubletap()?"双击屏幕唤醒 · 开":"双击屏幕唤醒 · 关",ps_power_doubletap());
    ps_wrap(fb,right,ty+(v.landscape?46:54),col,11,3,"电源键单击锁屏或唤醒。深度休眠仅用电源键恢复。",5);
    int y=appearance_y(v);const char *names[]={"翻页时钟","划线摘录","藏书封面","保持当前页面","自定义图片","灵犀画板"};
    char label[80];snprintf(label,sizeof(label),"锁屏外观 · %s",names[ps_sleep_style()]);
    ps_button(fb,right,y,col,40,label,false);
    ps_button(fb,right,y+52,col,40,ps_rotation_locked()?"方向锁定 · 开":"方向锁定 · 关",ps_rotation_locked());
    if(!v.landscape)ps_wrap(fb,18,554,v.w-36,11,3,"关机前保留锁屏画面，并标注已关机。长按电源键打开电源菜单。",5);
}
bool ps_power_event(const ps_event_t *e) {
    if(e->type!=PS_TAP)return false;
    ps_view_t v=ps_view();
    if(quick_menu){
        for(int i=0;i<4;i++){int x,y,w,h;quick_geometry(v,i,&x,&y,&w,&h);if(ps_hit(e->x,e->y,x,y,w,h)){quick_menu=false;ps_back();if(i<3)ps_power_request(i==0?PS_POWER_OFF:i==1?PS_POWER_RESTART:PS_POWER_LOCK);return true;}}
        quick_menu=false;ps_back();return true;
    }
    int col=settings_column(v),right=v.landscape?32+col:16,rh=v.landscape?36:44,cw=(col-8)/2;
    if(confirmation>=0){
        if(ps_hit(e->x,e->y,18,180,(v.w-44)/2,44)||!ps_hit(e->x,e->y,12,94,v.w-24,154))confirmation=-1;
        else if(ps_hit(e->x,e->y,26+(v.w-44)/2,180,(v.w-44)/2,44)){ps_power_request(confirmation);confirmation=-1;}
        return true;
    }
    for(int i=0;i<4;i++)if(ps_hit(e->x,e->y,16+(i%2)*(cw+8),76+(i/2)*(rh+6),cw,rh)){if(i>=2)confirmation=i;else ps_power_request(i);return true;}
    int ay=v.landscape?176:200,aw=(col-12)/3;
    for(int i=0;i<6;i++)if(ps_hit(e->x,e->y,16+(i%3)*(aw+6),ay+22+(i/3)*36,aw,32)){ps_power_set_auto_minutes(auto_choices[i]);return true;}
    if(ps_hit(e->x,e->y,right,v.landscape?76:332,col,v.landscape?36:44)){ps_power_set_doubletap(!ps_power_doubletap());return true;}
    int y=appearance_y(v);
    if(ps_hit(e->x,e->y,right,y,col,40)){ps_navigate(PS_SLEEP_SETTINGS);return true;}
    if(ps_hit(e->x,e->y,right,y+52,col,40)){ps_rotation_lock(!ps_rotation_locked());return true;}
    return false;
}

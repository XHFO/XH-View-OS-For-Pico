/* SPDX-License-Identifier: Apache-2.0
 * 中文：运行真实主循环，用确定性触摸和任务延时退出验证调度。
 * English: Run the real loop with deterministic touch and task-delay exit.
 */
#include "app_loop.h"
#include "app_touch_buffer.h"
#include "ui_gesture.h"
#include "ui_menu.h"
#include "../main/system/pico_system.h"
#include <assert.h>
#include <setjmp.h>
#include <stdio.h>
#include <string.h>

static jmp_buf done;
typedef struct { int x, y, count, error; } sample_t;
static sample_t samples[32];
static int sample_count, step, ticks, enters, exits, touch_calls, keys[3], events[10];
static enum EpdRotation rotations[32];
static int menus, highlights, restores, fulls, mode, tick_consumed[32];
static int long_keys, shell_locks;
static app_redraw_t long_key(app_ctx_t* c, int k) { assert(k==UI_KEY_2); ++long_keys; c->request_menu=true; return APP_REDRAW_NONE; }
static app_redraw_t response;
static app_redraw_t continuous_response[10];
static ui_gesture_event_t continuous_log[32];
static int continuous_count, continuous_request_at, continuous_request_kind;
static bool capture_press, capture_move;
static bool request_on_touch, request_on_tick, menu_on_tick, menu_on_touch, cancel_clobber;
static bool home_on_touch, home_on_tick, home_on_key, start_second, developer_on_touch;
static int home_enters, home_renders, last_menu_leaf, rendered_leaf;
static const app_desc_t* menu_background;
static app_desc_t first, second, third;
static bool full_tick, lock_due, font_due;
static bool idle_pmu, test_awake;
static int sleep_calls;
static bool power_queued;
static int power_runs, power_toggles, power_request_at;
static int power_longs;
static pmu_product_key_action_t power_keys[32];
static const app_desc_t* power_page;
static int64_t time_offset, time_step;
static int du_areas, gl_areas;
static bool media_test, sd_font, saved_sd_font;
static bool mounted_steps[32];
static int media_lost, media_restored, builtin_opens, font_opens, probes, loss_step;
static bool font_suspended;
static bool font_case, font_fail, font_wrong_success, suspended_steps[32];
static read_pico_sd_info_t font_info_steps[32];
static char saved_font_path[TTF_FONT_PATH_MAX], opened_font_path[TTF_FONT_PATH_MAX];
static unsigned run_number;
static bool global_enabled, global_claim[32], global_back[32], bind_before_enter;
static bool global_overlay, order_check;
static const app_desc_t *global_page, *global_open[32];
static int global_inputs, global_claims, global_controls, global_presents;
static int global_tick_effects, global_tick_claimed, render_order;
static int global_cancels,global_cancel_steps[32];
static bool global_pending;
void ps_global_cancel_input(void) {
    assert(step>=0&&step<32);global_cancels++;global_cancel_steps[step]++;global_pending=false;
}
void ps_global_enable(bool enabled) {global_enabled=enabled;}
void ps_global_page(app_ctx_t*c,const app_desc_t*a) {(void)c;global_page=a;}
bool ps_global_input(app_ctx_t*c,const app_desc_t*a,app_redraw_t*r) {
    assert(global_page==a);global_inputs++;
    if(!global_enabled||a->product_shell||!global_claim[step])return false;
    global_claims++;
    if(global_back[step])c->request_return=true;
    if(global_open[step])c->request_app=global_open[step];
    *r=APP_REDRAW_PAGE;
    return true;
}
bool ps_global_present(app_ctx_t*c,const app_desc_t*a,app_redraw_t r) {
    (void)c;(void)r;assert(global_page==a);global_presents++;
    return global_overlay;
}
void ps_global_draw_controls(const app_desc_t*a,uint8_t*f) {
    (void)f;assert(global_page==a);global_controls++;
    if(order_check){assert(render_order==1);render_order=2;}
}
int E0470_WAVEFORM;
enum EpdRotation epd_get_rotation(void) {return rotations[step];}
int cst836u_read(void* h, cst836u_touch_t* t) {
    (void)h;
    sample_t s = samples[step];
    *t = (cst836u_touch_t){ .touched=s.count>0, .count=s.count, .x=s.x, .y=s.y };
    t->points[0].active=s.count>0;
    return s.error;
}
int cst836u_get_info(void* h,cst836u_info_t* i) {(void)h;(void)i;return 0;}
bool app_touch_buffer_enable(cst836u_handle_t tp,bool enabled,int rotation){(void)tp;(void)enabled;(void)rotation;return false;}
esp_err_t app_touch_buffer_read(cst836u_touch_t *touch,int64_t *ms,bool *pending){(void)touch;(void)ms;(void)pending;assert(false);return ESP_ERR_INVALID_STATE;}
bool ps_keyboard_capture_active(void){return false;}
bool ps_dual_enabled(void){return false;}
bool ps_touch_guard_enabled(void){return true;}
void ps_toast(const char *text){(void)text;}
void ps_sound_contact(bool valid,bool down,unsigned count,int64_t now){(void)valid;(void)down;(void)count;(void)now;}
const char* esp_err_to_name(int e) {(void)e;return "test";}
int64_t esp_timer_get_time(void) {return time_offset+(int64_t)(step+1)*time_step;}
void vTaskDelay(int ms) {(void)ms;if (++step>=sample_count) longjmp(done,1);}
bool continuous_du_init(void) {return true;}
void guard_draw_result(EpdiyHighlevelState* h,enum EpdDrawError e) {(void)h;assert(e==0);}
enum EpdDrawError update_display_area_with(EpdiyHighlevelState*h,const void*w,int m,EpdRect a) {(void)h;(void)w;(void)a;if(m==MODE_DU)du_areas++;else if(m==MODE_GL16)gl_areas++;return 0;}
enum EpdDrawError update_display_full(EpdiyHighlevelState*h) {(void)h;if(order_check){assert(render_order==2);render_order=3;}fulls++;return 0;}
enum EpdDrawError update_display_mode(EpdiyHighlevelState*h,int m) {(void)h;(void)m;if(order_check){assert(render_order==2);render_order=3;}mode++;return 0;}
enum EpdDrawError update_display_white(EpdiyHighlevelState*h) {(void)h;return 0;}
bool display_take_white_exit(void) {return false;}
void rails_idle_check(int64_t n) {(void)n;}
bool read_pico_pmu_ready(void) {return lock_due||idle_pmu||power_keys[step]!=PMU_PRODUCT_KEY_NONE;}
bool read_pico_pmu_take_key_short(void) {return lock_due;}
pmu_product_key_action_t read_pico_pmu_take_product_key(void) {
    pmu_product_key_action_t action=power_keys[step];
    power_keys[step]=PMU_PRODUCT_KEY_NONE;
    return action? action:lock_due?PMU_PRODUCT_KEY_SHORT:PMU_PRODUCT_KEY_NONE;
}
void enter_lock_and_sleep(EpdiyHighlevelState*h,int64_t*t,void*a) {(void)h;(void)t;(void)a;sleep_calls++;lock_due=false;time_offset+=1000000;}
bool app_lock_powers_down(void) {return false;}
bool ps_power_pending(void) {return power_queued||step==power_request_at;}
void ps_power_poll(app_ctx_t*c,const app_desc_t*a) {(void)c;(void)a;}
bool ps_power_toggle(app_ctx_t*c,const app_desc_t*a) {(void)c;(void)a;power_toggles++;power_queued=true;lock_due=false;return true;}
bool ps_power_key_long(app_ctx_t*c,const app_desc_t*a) {
    assert(a==&first||a==&second);
    assert(c->consumed&&!c->pressed&&!c->released&&!c->request_app&&!c->request_menu&&!c->request_return);
    power_longs++;power_page=a;c->request_app=&second;return true;
}
bool ps_power_run(app_ctx_t*c,const app_desc_t*a) {
    if(!ps_power_pending())return false;
    assert(c->consumed&&!c->pressed&&!c->released&&!c->request_app&&!c->request_menu&&!c->request_return);
    power_runs++;power_page=a;power_queued=false;time_offset+=1000000;return true;
}
const char* app_settings_font_path(void) {return font_due||saved_sd_font?saved_font_path:"builtin";}
bool app_settings_test_awake(void) {return test_awake;}
bool ttf_font_path_is_builtin(const char*p) {return !strncmp(p,"builtin",7);}
bool ttf_font_ready(void) {return true;}
bool ttf_font_sd_suspended(void) {return font_suspended||(font_case&&suspended_steps[step]);}
bool ttf_font_is_builtin(void) {return !font_due&&!sd_font;}
const char* ttf_font_path(void) {return opened_font_path;}
int ttf_font_open(const char*p) {font_opens++;if(font_fail)return ESP_ERR_INVALID_STATE;sd_font=true;if(!font_wrong_success)snprintf(opened_font_path,sizeof(opened_font_path),"%s",p);return 0;}
int ttf_font_open_builtin(void) {assert(media_lost>0);builtin_opens++;sd_font=font_due=false;return 0;}
int read_pico_sd_get_info(read_pico_sd_info_t*i) {if(font_case){*i=font_info_steps[step];return 0;}*i=(read_pico_sd_info_t){.mounted=media_test?mounted_steps[step]:font_due,.present=true};return media_test&&!i->mounted?ESP_ERR_INVALID_STATE:0;}
void read_pico_sd_start_probe(void) {probes++;}
static void lost(app_ctx_t*c) {(void)c;media_lost++;loss_step=step;}
static void restored(app_ctx_t*c) {(void)c;media_restored++;}
EpdRect ui_content_refresh_area(void) {return (EpdRect){0,0,684,1000};}
const app_desc_t* app_home_page(void) {return &first;}
const app_desc_t* app_at(int i) {return i==0?&first:i==1?&second:i==2?&third:NULL;}
int ui_key_hit_test(uint16_t x,uint16_t y) {return y>=1300 && x<480?x/160:-1;}
bool ui_menu_handle_hit_test(uint16_t x,uint16_t y) {return x>600 && y>1100 && y<1300;}
int ui_menu_leaf_for_app(const app_desc_t*a) {return a==&second?1:0;}
int ui_menu_leaf_count(void) {return 2;}
void ui_draw_menu_page(uint8_t*f,const app_desc_t*a,int l) {(void)f;menu_background=a;last_menu_leaf=l;menus++;}
int ui_menu_hit_test(uint16_t x,uint16_t y,int l) {(void)l;if(x==550&&y==500)return UI_MENU_HIT_NEXT;return x<500&&y>=100&&y<300?(y-100)/100:-1;}
bool ui_menu_row_rect(int l,int r,EpdRect*out) {(void)l;*out=(EpdRect){0,r*100+100,500,100};return r>=0&&r<2;}
void ui_draw_menu_row_pressed(uint8_t*f,const app_desc_t*a,int l,int r,bool p) {(void)f;(void)a;(void)l;(void)r;if(p)highlights++;else restores++;}
static void enter(app_ctx_t*c) {(void)c;enters++;}
static void leave(app_ctx_t*c) {(void)c;exits++;}
static void render(app_ctx_t*c,uint8_t*f) {(void)c;(void)f;}
static void home_enter(app_ctx_t*c) {c->leaf=0;enters++;home_enters++;}
static void home_render(app_ctx_t*c,uint8_t*f) {(void)f;rendered_leaf=c->leaf;home_renders++;}
static app_redraw_t touch(app_ctx_t*c,const cst836u_touch_t*t) {
    (void)t;touch_calls++;assert(c->pressed&&c->consumed);
    if(request_on_touch)c->request_app=&second;
    if(menu_on_touch)c->request_menu=true;
    if(home_on_touch)c->request_return=true;
    return response;
}
static app_redraw_t gesture(app_ctx_t*c,const ui_gesture_event_t*e) {
    events[e->type]++;
    if(e->type==UI_GESTURE_CANCEL&&cancel_clobber) {c->request_app=NULL;c->request_menu=true;c->request_return=false;c->request_developer_menu=!c->request_developer_menu;}
    if(e->type==UI_GESTURE_PRESS&&request_on_touch)c->request_app=&second;
    if(e->type==UI_GESTURE_PRESS&&home_on_touch)c->request_return=true;
    if(e->type==UI_GESTURE_PRESS&&developer_on_touch)c->request_developer_menu=true;
    return response;
}
static app_redraw_t continuous_gesture(app_ctx_t*c,const ui_gesture_event_t*e) {
    assert(!c->capture_gesture && continuous_count<32);
    continuous_log[continuous_count++]=*e;events[e->type]++;
    if((e->type==UI_GESTURE_PRESS&&capture_press)||(e->type==UI_GESTURE_MOVE&&capture_move))c->capture_gesture=true;
    if(step==continuous_request_at&&e->type==UI_GESTURE_MOVE) {
        if(continuous_request_kind==1)c->request_app=&second;
        if(continuous_request_kind==2)c->request_menu=true;
        if(continuous_request_kind==3)c->request_return=true;
        if(continuous_request_kind==4)c->request_developer_menu=true;
    }
    return continuous_response[e->type];
}
static app_redraw_t key(app_ctx_t*c,int k) {keys[k]++;if(home_on_key)c->request_return=true;return APP_REDRAW_NONE;}
static app_redraw_t tick(app_ctx_t*c) {
    ticks++;tick_consumed[step]=c->consumed;
    if(request_on_tick)c->request_app=&second;
    if(menu_on_tick)c->request_menu=true;
    if(home_on_tick){c->request_return=true;home_on_tick=false;}
    return full_tick ? APP_REDRAW_FULL : APP_REDRAW_NONE;
}
static void reset(void) {
    font_suspended=font_case=font_fail=font_wrong_success=false;
    memset(suspended_steps,0,sizeof(suspended_steps));memset(font_info_steps,0,sizeof(font_info_steps));
    snprintf(saved_font_path,sizeof(saved_font_path),"/sdcard/fonts/test-%u.ttf",++run_number);
    snprintf(opened_font_path,sizeof(opened_font_path),"other");
    global_enabled=global_overlay=bind_before_enter=order_check=false;
    global_page=NULL;global_inputs=global_claims=global_controls=global_presents=0;
    global_tick_effects=global_tick_claimed=render_order=global_cancels=0;global_pending=false;
    memset(global_cancel_steps,0,sizeof(global_cancel_steps));
    memset(global_claim,0,sizeof(global_claim));memset(global_back,0,sizeof(global_back));memset(global_open,0,sizeof(global_open));
    media_test=sd_font=saved_sd_font=false;media_lost=media_restored=builtin_opens=font_opens=probes=0;loss_step=-1;
    memset(mounted_steps,0,sizeof(mounted_steps));
    long_keys=shell_locks=0;
    home_on_touch=home_on_tick=home_on_key=start_second=developer_on_touch=false;
    home_enters=home_renders=0;rendered_leaf=-1;last_menu_leaf=-1;menu_background=NULL;
    full_tick=lock_due=font_due=false;time_offset=0;time_step=10000;du_areas=gl_areas=0;
    idle_pmu=false;test_awake=true;sleep_calls=0;
    power_queued=false;power_runs=power_toggles=0;power_request_at=-1;power_page=NULL;
    power_longs=0;memset(power_keys,0,sizeof(power_keys));
    memset(samples,0,sizeof(samples));memset(keys,0,sizeof(keys));memset(events,0,sizeof(events));memset(tick_consumed,0,sizeof(tick_consumed));
    memset(rotations,0,sizeof(rotations));
    sample_count=step=ticks=enters=exits=touch_calls=menus=highlights=restores=fulls=mode=0;
    request_on_touch=request_on_tick=menu_on_tick=menu_on_touch=cancel_clobber=false;response=APP_REDRAW_NONE;
    memset(continuous_response,0,sizeof(continuous_response));memset(continuous_log,0,sizeof(continuous_log));
    continuous_count=continuous_request_kind=0;continuous_request_at=-1;capture_press=capture_move=false;
    first=(app_desc_t){.title="first",.render=render,.on_enter=enter,.on_exit=leave,.on_touch=touch,.on_tick=tick,.on_key=key};
    second=(app_desc_t){.title="second",.render=render,.on_enter=enter};
    third=(app_desc_t){0};
}
static void add(int x,int y,int count,int error) {samples[sample_count++]=(sample_t){x,y,count,error};}
static void run(void) {app_loop_config_t cfg={.first_app=start_second?&second:&first};if(!setjmp(done))app_loop_run(&cfg);}
static app_redraw_t consumed_return_tick(app_ctx_t*c) { if(step==0){c->leaf=7;c->request_app=&second;}else c->request_return=true;return APP_REDRAW_NONE; }
static app_redraw_t return_tick(app_ctx_t*c) { c->request_return=true; return APP_REDRAW_NONE; }
static app_redraw_t origin_tick(app_ctx_t*c) { c->leaf=7; if(step==0 && request_on_tick)c->request_app=&second; return APP_REDRAW_NONE; }
static void home_case(void) {
    reset();start_second=true;
    second=first;second.title="transfer";
    first.on_enter=home_enter;first.render=home_render;first.on_tick=NULL;first.on_touch=NULL;
}
static app_redraw_t shell_lock(app_ctx_t*c) {(void)c;shell_locks++;lock_due=false;return APP_REDRAW_PAGE;}

static void keep_awake_cases(void) {
    // 真主循环：开关只改变无操作超时，通用页与产品锁屏均保留手动电源短按。
    // Actual loop: only idle timeout changes; generic and shell pages retain manual power locking.
    for(int shell=0;shell<2;shell++) {
        for(int enabled=0;enabled<2;enabled++) {
            reset();test_awake=enabled;idle_pmu=true;time_step=60000000;
            if(shell)first.on_lock=shell_lock;
            for(int i=0;i<3;i++)add(0,0,0,0);
            run();
            assert(shell_locks==(shell&&!enabled));
            assert(sleep_calls==(!shell&&!enabled));
            reset();test_awake=enabled;lock_due=true;time_step=1000000;
            if(shell)first.on_lock=shell_lock;
            for(int i=0;i<4;i++)add(0,0,0,0);
            run();
            assert(shell_locks==shell);
            assert(sleep_calls==!shell);
        }
    }
}

static void product_power_cases(void) {
    // 真主循环保留阅读实例，取消锁前拖动，并吞掉恢复时仍按住的触摸和释放。
    // The real loop keeps the reader instance, cancels the pre-lock drag, and consumes the held wake contact/release.
    reset();first.product_shell=true;first.continuous_gestures=true;first.on_gesture=continuous_gesture;
    first.on_key_long=long_key;first.owns_keys=true;capture_press=true;power_request_at=2;
    add(100,400,1,0);add(120,400,1,0);add(0,0,0,0);
    add(240,1500,1,0);add(240,1500,1,0);add(0,0,0,0);
    add(100,400,1,0);add(0,0,0,0);run();
    assert(power_runs==1&&power_page==&first&&!exits&&enters==1&&!sleep_calls);
    assert(events[UI_GESTURE_CANCEL]==1&&events[UI_GESTURE_PRESS]==2&&events[UI_GESTURE_RELEASE]==1&&!events[UI_GESTURE_TAP]&&!long_keys&&!keys[UI_KEY_2]);
    assert(ticks==4);

    // 产品子页没有on_lock也由相同控制器处理；手动锁屏不受保持唤醒影响。
    // Product children without on_lock use the same controller; keep-awake never blocks manual locking.
    for(int enabled=0;enabled<2;enabled++) {
        reset();first.product_shell=true;first.on_tick=origin_tick;request_on_tick=true;
        second.on_tick=tick;test_awake=enabled;lock_due=true;time_step=1000000;
        for(int i=0;i<8;i++)add(0,0,0,0);
        run();
        assert(power_toggles==1&&power_runs==1&&!sleep_calls);
        assert(power_page==&second&&exits==1&&enters==2);
    }
    for(int enabled=0;enabled<2;enabled++) {
        reset();first.product_shell=true;test_awake=enabled;idle_pmu=true;time_step=60000000;
        for(int i=0;i<5;i++)add(0,0,0,0);
        run();
        // The product controller owns idle policy; this scheduler double intentionally has no idle clock.
        assert(!power_toggles&&!power_runs&&!sleep_calls);
    }

    // 完整LONG先取消拖动再进入操作菜单，不触发SHORT锁屏；后续抬手也不穿透。
    // A complete LONG cancels the drag before the action menu, never toggles SHORT lock, and consumes the held release.
    reset();first.product_shell=true;first.continuous_gestures=true;first.on_gesture=continuous_gesture;
    capture_press=true;time_step=1000000;power_keys[2]=PMU_PRODUCT_KEY_LONG;
    add(100,400,1,0);add(120,400,1,0);add(130,400,1,0);
    add(130,400,1,0);add(0,0,0,0);add(0,0,0,0);run();
    assert(power_longs==1&&power_page==&first&&!power_toggles&&!power_runs&&!sleep_calls);
    assert(exits==1&&enters==2&&events[UI_GESTURE_CANCEL]==1&&!events[UI_GESTURE_TAP]);

    // 工程区不调用产品动作入口，即使设备有LONG也保持原短按消费语义。
    // Engineering never calls the product action consumer and retains its short-only behavior.
    reset();time_step=1000000;power_keys[2]=PMU_PRODUCT_KEY_LONG;
    for(int i=0;i<5;i++)add(0,0,0,0);run();
    assert(!power_longs&&!power_toggles&&!power_runs&&!sleep_calls);

    // 开机抑制只覆盖启动残留LONG，不重新引入恢复后的短按空窗。
    // Boot suppression covers a leftover LONG without restoring a post-wake short-press blackout.
    reset();first.product_shell=true;time_step=500000;power_keys[0]=PMU_PRODUCT_KEY_LONG;
    for(int i=0;i<4;i++)add(0,0,0,0);run();
    assert(!power_longs&&!power_toggles&&!power_runs);
}

// 产品返回走真实调度器，脚本仅代替页面回调发出请求。/ Product returns use the real scheduler; scripts only replace page requests.
typedef enum { NAV_NONE, NAV_BOOK, NAV_FONT, NAV_SHELL, NAV_MENU, NAV_RETURN, NAV_DEVELOPER } nav_request_t;
static nav_request_t nav_requests[32];
static int nav_leaf[32], nav_active, nav_enters[3], nav_exits[3], nav_keys[3][3], nav_rendered_leaf[3];
static void nav_enter(app_ctx_t* c,int page) {if(bind_before_enter)assert(global_page==(page==0?&first:page==1?&second:&third));nav_active=page;nav_enters[page]++;c->leaf=0;}
static void nav_enter_shell(app_ctx_t* c) {nav_enter(c,0);}
static void nav_enter_book(app_ctx_t* c) {nav_enter(c,1);}
static void nav_enter_font(app_ctx_t* c) {nav_enter(c,2);}
static void nav_exit_shell(app_ctx_t* c) {(void)c;nav_exits[0]++;}
static void nav_exit_book(app_ctx_t* c) {(void)c;nav_exits[1]++;}
static void nav_exit_font(app_ctx_t* c) {(void)c;nav_exits[2]++;}
static void nav_render(app_ctx_t* c,uint8_t* f) {(void)f;nav_rendered_leaf[nav_active]=c->leaf;}
static app_redraw_t nav_key(app_ctx_t* c,int key_id) {(void)c;nav_keys[nav_active][key_id]++;return APP_REDRAW_NONE;}
static app_redraw_t nav_tick(app_ctx_t* c) {
    if(nav_leaf[step]>=0)c->leaf=nav_leaf[step];
    switch(nav_requests[step]) {
        case NAV_BOOK:c->request_app=&second;break;
        case NAV_FONT:c->request_app=&third;break;
        case NAV_SHELL:c->request_app=&first;break;
        case NAV_MENU:c->request_menu=true;break;
        case NAV_RETURN:c->request_return=true;break;
        case NAV_DEVELOPER:c->request_developer_menu=true;break;
        case NAV_NONE:break;
    }
    return APP_REDRAW_NONE;
}
static void nav_reset(void) {
    reset();
    memset(nav_requests,0,sizeof(nav_requests));
    for(size_t i=0;i<sizeof(nav_leaf)/sizeof(nav_leaf[0]);i++)nav_leaf[i]=-1;
    memset(nav_enters,0,sizeof(nav_enters));memset(nav_exits,0,sizeof(nav_exits));
    memset(nav_keys,0,sizeof(nav_keys));
    for(int i=0;i<3;i++)nav_rendered_leaf[i]=-1;
    nav_active=-1;
    first=(app_desc_t){.title="product",.product_shell=true,.owns_keys=true,.custom_menu_handle=true,
        .on_enter=nav_enter_shell,.on_exit=nav_exit_shell,.render=nav_render,.on_tick=nav_tick,.on_key=nav_key};
    second=(app_desc_t){.title="book",.owns_keys=true,.on_enter=nav_enter_book,.on_exit=nav_exit_book,
        .render=nav_render,.on_tick=nav_tick,.on_key=nav_key};
    third=(app_desc_t){.title="font",.on_enter=nav_enter_font,.on_exit=nav_exit_font,
        .render=nav_render,.on_tick=nav_tick,.on_key=nav_key};
}
static void nav_idle(int count) {for(int i=0;i<count;i++)add(0,0,0,0);}
static void product_navigation_cases(void) {
    // 嵌套页逐层返回，恢复各自页码；普通菜单请求不能落入工程菜单。
    // Nested pages return one level with their leaf restored; ordinary menu requests never enter demos.
    nav_reset();nav_requests[0]=NAV_BOOK;nav_leaf[0]=4;nav_requests[1]=NAV_FONT;nav_leaf[1]=7;
    nav_requests[2]=NAV_MENU;nav_requests[3]=NAV_MENU;nav_idle(4);run();
    assert(nav_active==0&&!menus&&nav_enters[0]==2&&nav_enters[1]==2&&nav_enters[2]==1);
    assert(nav_exits[0]==1&&nav_exits[1]==2&&nav_exits[2]==1);
    assert(nav_rendered_leaf[0]==4&&nav_rendered_leaf[1]==7);

    // 显式返回与普通菜单请求共享产品栈。/ Explicit returns and ordinary menu requests share product history.
    nav_reset();nav_requests[0]=NAV_BOOK;nav_requests[1]=NAV_FONT;
    nav_requests[2]=NAV_RETURN;nav_requests[3]=NAV_RETURN;nav_idle(4);run();
    assert(nav_active==0&&!menus&&nav_enters[0]==2&&nav_enters[1]==2&&nav_enters[2]==1);

    // 超过八层时丢掉较早的中间页，保留产品根及最近页；返回不能越界或打开工程菜单。
    // Beyond eight origins, retain the product root and recent pages without overrunning or entering demos.
    nav_reset();
    for(int i=0;i<10;i++)nav_requests[i]=i%2?NAV_FONT:NAV_BOOK;
    for(int i=10;i<18;i++)nav_requests[i]=NAV_MENU;
    nav_idle(18);run();
    assert(nav_active==0&&!menus&&nav_enters[0]==2&&nav_exits[0]==1);

    // 已无子页历史时普通菜单请求仍留在产品根，只能由明确入口打开工程菜单。
    // With no child history, ordinary menu requests stay at the product root until an explicit developer entry.
    nav_reset();nav_requests[0]=NAV_MENU;nav_requests[1]=NAV_RETURN;nav_idle(2);run();
    assert(nav_active==0&&!menus&&nav_enters[0]==1&&!nav_exits[0]);

    // 不接管三键的工程页：KEY3先回阅读器，再由普通菜单回产品。
    // A page without key ownership returns to reading on KEY3, then back to the product on menu.
    nav_reset();nav_requests[0]=NAV_BOOK;nav_requests[1]=NAV_FONT;nav_requests[4]=NAV_MENU;
    nav_idle(2);add(400,1500,1,0);nav_idle(2);run();
    assert(nav_active==0&&!menus&&nav_enters[1]==2&&!nav_keys[2][UI_KEY_3]);

    // 全局把手在接管三键的阅读器上也返回产品。/ The global handle returns from a key-owning reader too.
    nav_reset();nav_requests[0]=NAV_BOOK;nav_idle(1);add(650,1150,1,0);nav_idle(1);run();
    assert(nav_active==0&&!menus&&nav_enters[0]==2);

    // 阅读器自己的KEY3保持翻页，不能被产品返回策略抢走。/ Reader-owned KEY3 remains a page action.
    nav_reset();nav_requests[0]=NAV_BOOK;nav_requests[3]=NAV_MENU;
    nav_idle(1);add(400,1500,1,0);nav_idle(2);run();
    assert(nav_keys[1][UI_KEY_3]==1&&nav_active==0&&!menus&&nav_enters[0]==2);

    // 显式回到产品清空旧栈；再进阅读器时不恢复上次遗留的字体页。
    // Explicit product navigation clears old history; a later reader visit cannot resurrect its old font page.
    nav_reset();nav_requests[0]=NAV_BOOK;nav_requests[1]=NAV_FONT;nav_requests[2]=NAV_SHELL;
    nav_requests[3]=NAV_BOOK;nav_requests[4]=NAV_MENU;nav_idle(5);run();
    assert(nav_active==0&&!menus&&nav_enters[0]==3&&nav_enters[1]==2&&nav_enters[2]==1);

    // 明确打开开发菜单后保留传统菜单行为，不再强制回产品来源。
    // An explicit developer entry restores legacy menu behavior instead of forcing a product return.
    nav_reset();nav_requests[0]=NAV_BOOK;nav_requests[1]=NAV_DEVELOPER;nav_requests[4]=NAV_MENU;
    nav_idle(2);add(400,1500,1,0);nav_idle(2);run();
    assert(nav_active==1&&nav_enters[0]==1&&menus==2&&menu_background==&second);

    // 从产品明确进入开发菜单并选择工程页，页面自己的菜单出口仍回工程菜单。
    // Selecting a developer page from the product's explicit menu retains its developer-menu exit.
    nav_reset();nav_requests[0]=NAV_DEVELOPER;nav_requests[3]=NAV_MENU;
    nav_idle(1);add(100,220,1,0);nav_idle(2);run();
    assert(nav_active==1&&nav_enters[0]==1&&menus==2&&menu_background==&second);

    // CANCEL不能擦掉明确开发请求，也不能改变其清除产品来源的效果。
    // CANCEL cannot erase an explicit developer request or its release of product ownership.
    nav_reset();nav_requests[0]=NAV_BOOK;second.on_gesture=gesture;
    developer_on_touch=cancel_clobber=true;nav_requests[5]=NAV_MENU;
    nav_idle(1);add(100,400,1,0);nav_idle(1);add(400,1500,1,0);nav_idle(2);run();
    assert(events[UI_GESTURE_CANCEL]==1&&nav_active==1&&nav_enters[0]==1&&menus==2&&menu_background==&second);

    // 普通重绘取消手势时，CANCEL也不能凭空创建开发菜单请求。
    // A redraw cancellation cannot invent a developer-menu request that was absent before CANCEL.
    nav_reset();first.on_gesture=gesture;first.on_tick=tick;full_tick=cancel_clobber=true;
    add(100,400,1,0);nav_idle(1);run();
    assert(events[UI_GESTURE_CANCEL]==1&&nav_active==0&&nav_enters[0]==1&&!menus);
}
static app_redraw_t global_guarded_tick(app_ctx_t*c) {
    if(global_claim[step])global_tick_claimed++;
    if(!c->consumed)global_tick_effects++;
    return APP_REDRAW_NONE;
}
// 全局延迟动作在真实调度器的读错和拔卡出口取消，不依赖页面自己的取消回调。
// Pending global actions are canceled by the real scheduler's read-error/media exits, independently of page callbacks.
static void global_cancel_cases(void) {
    for(int book=0;book<2;book++)for(int media=0;media<2;media++) {
        nav_reset();if(book)nav_requests[0]=NAV_BOOK;
        global_pending=true;
        if(media) {
            media_test=true;mounted_steps[0]=true;time_step=600000;
            first.on_media_lost=second.on_media_lost=lost;
        }
        nav_idle(1);add(240,1500,1,media?0:9);
        add(0,0,0,media?0:9);nav_idle(1);run();
        assert(global_enabled&&global_page==(book?&second:&first));
        assert(!global_pending&&!nav_keys[0][UI_KEY_2]&&!nav_keys[1][UI_KEY_2]&&!menus);
        if(media)assert(media_lost==1&&global_cancels==1&&global_cancel_steps[1]==1);
        else assert(!media_lost&&global_cancels==2&&global_cancel_steps[1]==1&&global_cancel_steps[2]==1);
    }
}
static void ordered_render(app_ctx_t*c,uint8_t*f) {(void)c;(void)f;assert(render_order==0);render_order=1;}
static void global_bridge_cases(void) {
    // 全局接管完整输入序列时，页面点击/翻页/轮询均不得动作。
    // A globally claimed sequence must not reach page taps, turns or polling.
    nav_reset();nav_requests[0]=NAV_BOOK;second.on_gesture=gesture;second.on_tick=global_guarded_tick;
    global_claim[1]=global_claim[2]=global_claim[3]=true;
    nav_idle(1);add(25,1180,1,0);add(240,930,1,0);add(0,0,0,0);run();
    assert(global_claims==3&&!touch_calls&&!global_tick_claimed&&!global_tick_effects);
    for(int i=0;i<8;i++)assert(!events[i]);

    // 抢占开始前已按下的阅读手势，只发送一次取消，抬手不能变成翻页。
    // A previously started page gesture receives one cancel, never a release turn.
    nav_reset();nav_requests[0]=NAV_BOOK;second.on_gesture=gesture;second.on_tick=global_guarded_tick;
    global_claim[2]=global_claim[3]=true;
    nav_idle(1);add(520,400,1,0);add(150,400,1,0);add(0,0,0,0);run();
    assert(events[UI_GESTURE_PRESS]==1&&events[UI_GESTURE_CANCEL]==1);
    assert(!events[UI_GESTURE_TAP]&&!events[UI_GESTURE_SWIPE_L]&&!global_tick_claimed&&global_claims==2);

    // 按键前的全局接管优先于页面按键派发，不能触发遗留菜单。
    // A claimed press precedes page keys and the legacy handle.
    nav_reset();nav_requests[0]=NAV_BOOK;global_claim[1]=true;
    nav_idle(1);add(240,1500,1,0);run();assert(!nav_keys[1][UI_KEY_2]&&!menus&&global_claims==1);

    // 普通左右阅读滑动仍由真实手势状态机识别。
    // Ordinary horizontal reading swipes still use the real recognizer.
    nav_reset();nav_requests[0]=NAV_BOOK;second.on_gesture=gesture;
    nav_idle(1);
    add(550,400,1,0);add(120,410,1,0);add(0,0,0,0);
    add(120,400,1,0);add(550,410,1,0);add(0,0,0,0);run();
    assert(events[UI_GESTURE_SWIPE_L]==1&&events[UI_GESTURE_SWIPE_R]==1&&!global_claims&&!menus);

    // 两次全局返回依次恢复真实书与产品根，绑定发生在每次进入前。
    // Global returns restore book and product origins, binding before each enter.
    nav_reset();bind_before_enter=true;nav_requests[0]=NAV_BOOK;nav_leaf[0]=4;
    nav_requests[1]=NAV_FONT;nav_leaf[1]=7;
    global_claim[2]=global_claim[3]=true;global_back[2]=global_back[3]=true;nav_idle(4);run();
    assert(nav_active==0&&nav_rendered_leaf[0]==4&&nav_rendered_leaf[1]==7);
    assert(global_claims==2&&!menus&&nav_enters[0]==2&&nav_enters[1]==2&&nav_enters[2]==1);

    // 全局菜单直接请求字体子页时，真实书仍是返回目标。
    // A global font request keeps the real book as its return target.
    nav_reset();bind_before_enter=true;nav_requests[0]=NAV_BOOK;nav_leaf[0]=4;
    global_claim[1]=true;global_open[1]=&third;nav_leaf[1]=8;
    nav_requests[2]=NAV_RETURN;nav_requests[3]=NAV_RETURN;nav_idle(4);run();
    assert(nav_active==0&&global_claims==1&&!menus&&nav_enters[1]==2&&nav_enters[2]==1);

    // 取消旧页手势不能清掉同轮全局菜单已发出的字体跳转。
    // Cancelling the old page gesture cannot erase a global font request from the same tick.
    nav_reset();nav_requests[0]=NAV_BOOK;second.on_gesture=gesture;cancel_clobber=true;
    global_claim[2]=true;global_open[2]=&third;
    nav_idle(1);add(520,400,1,0);add(150,400,1,0);run();
    assert(nav_active==2&&nav_enters[2]==1&&events[UI_GESTURE_CANCEL]==1&&!menus);

    // 明确进入工程菜单后关闭产品手势桥，工程页不受产品拦截。
    // Explicit developer navigation disables product interception.
    nav_reset();nav_requests[0]=NAV_DEVELOPER;global_claim[2]=global_claim[3]=true;
    nav_idle(1);add(100,220,1,0);nav_idle(2);run();
    assert(nav_active==1&&!global_enabled&&!global_claims&&menus==1);

    // 默认整页刷新先画页面，再画全局入口，最后推屏。
    // Default full-page presentation paints page, global controls, then the display.
    reset();app_ctx_t c={0};first.render=ordered_render;order_check=true;
    app_present(&c,&first,APP_REDRAW_PAGE);assert(render_order==3&&global_controls==1);
    render_order=0;app_present(&c,&first,APP_REDRAW_FULL);assert(render_order==3&&global_controls==2);
    order_check=false;global_overlay=true;int before_mode=mode,before_fulls=fulls;
    app_present(&c,&first,APP_REDRAW_PAGE);assert(global_controls==2&&mode==before_mode&&fulls==before_fulls);
    global_overlay=false;app_present(&c,&first,APP_REDRAW_NONE);assert(global_controls==2);

    // 字体子页KEY2强刷仍保留全局可见入口。
    // A child-page KEY2 full refresh retains the global controls.
    nav_reset();nav_requests[0]=NAV_BOOK;nav_requests[1]=NAV_FONT;
    nav_idle(2);add(240,1500,1,0);run();
    assert(global_controls==4&&fulls==2&&nav_active==2);
}
static void font_retry_reset(void) {
    reset();font_case=font_due=saved_sd_font=true;time_step=4000000;first.on_media_lost=lost;
    for(int i=0;i<32;i++)font_info_steps[i]=(read_pico_sd_info_t){.present=true,.mounted=true,.media_generation=1};
}
static void font_retry_cases(void) {
    // 缺失字体最多尝试三次；成功但仍保留旧字体的错误返回也不能被当成成功。
    // Missing fonts stop after three attempts; stale-path success is not accepted.
    font_retry_reset();font_fail=true;nav_idle(10);run();assert(font_opens==3&&fulls==1);
    font_retry_reset();font_wrong_success=true;nav_idle(10);run();assert(font_opens==3&&fulls==1);

    // 暂停、USB导出、缺卡和未挂载都不能尝试打开卡上字体。
    // Suspension, USB export, absence and unmounted cards cannot open SD fonts.
    font_retry_reset();font_suspended=true;nav_idle(5);run();assert(!font_opens);
    font_retry_reset();for(int i=0;i<5;i++)font_info_steps[i].exported=true;
    nav_idle(5);run();assert(!font_opens);
    font_retry_reset();for(int i=0;i<5;i++)font_info_steps[i].present=false;
    nav_idle(5);run();assert(!font_opens);
    font_retry_reset();for(int i=0;i<5;i++)font_info_steps[i].mounted=false;
    nav_idle(5);run();assert(!font_opens);

    // 同路径媒体代次变化仍须重开；成功后不每轮重开。
    // A generation change refreshes the same path once, without per-tick reopens.
    font_retry_reset();for(int i=4;i<10;i++)font_info_steps[i].media_generation=2;
    nav_idle(10);run();assert(font_opens==2&&fulls==3);

    // 三次失败后，暂停再恢复或真正重新挂载均允许新的有限重试。
    // Suspension/resume and an observed remount each permit another bounded retry set.
    font_retry_reset();font_fail=true;suspended_steps[4]=true;nav_idle(10);run();assert(font_opens==6);
    font_retry_reset();font_fail=true;font_info_steps[4].mounted=false;
    nav_idle(10);run();assert(font_opens==6&&media_lost==1);
}
static void continuous_reset(void) {
    reset();first.on_gesture=continuous_gesture;first.continuous_gestures=true;time_step=200000;
}
static void long_contact(void) {
    for(int i=0;i<4;i++)add(200,400,1,0);
}
static void no_release_action(void) {
    assert(events[UI_GESTURE_CANCEL]==1&&!events[UI_GESTURE_RELEASE]);
    assert(!events[UI_GESTURE_TAP]&&!events[UI_GESTURE_SWIPE_L]&&!events[UI_GESTURE_SWIPE_R]);
}
static void continuous_cases(void) {
    // 同源循环：长按刷新不取消，移动可局刷，松手提交一次且不误翻。
    // Real-loop long press survives page feedback, moves may area-refresh, and release commits once without turning.
    continuous_reset();continuous_response[UI_GESTURE_LONG_PRESS]=APP_REDRAW_PAGE;
    continuous_response[UI_GESTURE_MOVE]=APP_REDRAW_AREA;continuous_response[UI_GESTURE_RELEASE]=APP_REDRAW_PAGE;
    long_contact();add(300,450,1,0);add(450,500,1,0);add(0,0,0,0);nav_idle(1);run();
    assert(continuous_count==5&&events[UI_GESTURE_PRESS]==1&&events[UI_GESTURE_LONG_PRESS]==1&&events[UI_GESTURE_MOVE]==2&&events[UI_GESTURE_RELEASE]==1);
    assert(!events[UI_GESTURE_CANCEL]&&!events[UI_GESTURE_TAP]&&!events[UI_GESTURE_SWIPE_R]);
    assert(continuous_log[1].type==UI_GESTURE_LONG_PRESS&&continuous_log[2].type==UI_GESTURE_MOVE&&continuous_log[4].type==UI_GESTURE_RELEASE);
    assert(continuous_log[4].x0==200&&continuous_log[4].y0==400&&continuous_log[4].x==450&&continuous_log[4].y==500);
    assert(tick_consumed[3]&&tick_consumed[4]&&!tick_consumed[7]&&du_areas==2);
    // 进度条按下或第一次移动即可认领；不足长按时间的拖动不能变成轻点/翻页。
    // Progress may capture on press or first move; short drags never turn into taps/swipes.
    for(int on_move=0;on_move<2;on_move++) {
        continuous_reset();capture_move=on_move;capture_press=!on_move;
        continuous_response[UI_GESTURE_PRESS]=APP_REDRAW_PAGE;continuous_response[UI_GESTURE_MOVE]=APP_REDRAW_PAGE;
        add(200,400,1,0);add(500,400,1,0);add(0,0,0,0);run();
        assert(events[UI_GESTURE_MOVE]==1&&events[UI_GESTURE_RELEASE]==1&&!events[UI_GESTURE_LONG_PRESS]&&!events[UI_GESTURE_CANCEL]);
        assert(!events[UI_GESTURE_TAP]&&!events[UI_GESTURE_SWIPE_R]);
    }
    // 未认领连续页仍有正常左右滑/轻点；旧页 PAGE 保持原有取消行为。
    // Uncaptured continuous pages retain taps/swipes; legacy PAGE keeps its previous cancellation behavior.
    continuous_reset();add(500,400,1,0);add(100,400,1,0);add(0,0,0,0);
    add(100,400,1,0);add(500,400,1,0);add(0,0,0,0);add(200,400,1,0);add(0,0,0,0);run();
    assert(events[UI_GESTURE_SWIPE_L]==1&&events[UI_GESTURE_SWIPE_R]==1&&events[UI_GESTURE_TAP]==1&&!events[UI_GESTURE_RELEASE]);
    reset();first.on_gesture=gesture;response=APP_REDRAW_PAGE;add(200,400,1,0);add(400,400,1,0);add(0,0,0,0);run();no_release_action();
    // 强刷、多点、读错、方向变化、字体重载与拔卡均取消已认领接触，不提交或误翻。
    // Full refresh, multitouch, read errors, rotation, font reload and card loss cancel captured contacts without commit/turn.
    for(int failure=0;failure<6;failure++) {
        continuous_reset();capture_press=true;
        if(failure==0)continuous_response[UI_GESTURE_MOVE]=APP_REDRAW_FULL;
        if(failure==3)rotations[1]=rotations[2]=EPD_ROT_PORTRAIT;
        if(failure==4){font_due=true;time_offset=4000000;}
        if(failure==2||failure==5)global_pending=true;
        if(failure==5){media_test=sd_font=true;mounted_steps[0]=true;time_step=600000;first.on_media_lost=lost;}
        add(200,400,1,0);add(400,400,failure==1?2:1,failure==2?9:0);add(0,0,0,0);run();no_release_action();
        if(failure==2||failure==5)assert(global_cancels==1&&global_cancel_steps[1]==1&&!global_pending);
    }
    // 角落扇形菜单、全局返回、字体跳转先于正文接管，同轮旧接触只收到 CANCEL。
    // Global fan, back and font navigation preempt the page; the old contact receives only CANCEL.
    for(int action=0;action<3;action++) {
        nav_reset();nav_requests[0]=NAV_BOOK;second.on_gesture=continuous_gesture;second.continuous_gestures=true;capture_press=true;
        global_claim[2]=global_claim[3]=true;
        if(action==1)global_back[2]=true;
        if(action==2)global_open[2]=&third;
        nav_idle(1);add(200,400,1,0);add(400,400,1,0);add(0,0,0,0);run();no_release_action();
    }
    // 菜单、切页、显式返回、开发菜单及电源锁屏可靠取消拖动。
    // Menus, page switches, explicit back, developer menu and power locking reliably cancel dragging.
    for(int action=1;action<=4;action++) {
        continuous_reset();capture_press=true;continuous_request_at=1;continuous_request_kind=action;
        add(200,400,1,0);add(400,400,1,0);add(0,0,0,0);run();no_release_action();
    }
    continuous_reset();first.on_lock=shell_lock;lock_due=true;time_step=1000000;capture_press=true;
    add(200,400,1,0);add(300,400,1,0);add(400,400,1,0);add(0,0,0,0);run();no_release_action();assert(shell_locks==1);
}
int main(void) {
    reset();add(100,400,1,0);add(110,400,1,0);add(0,0,0,0);run();
    assert(touch_calls==1&&ticks==3&&!tick_consumed[0]);
    reset();response=APP_REDRAW_AREA;add(100,400,1,0);run();assert(touch_calls==1&&tick_consumed[0]);
    reset();first.on_gesture=gesture;response=APP_REDRAW_AREA;add(100,400,1,0);add(0,0,0,0);run();
    assert(!touch_calls&&events[UI_GESTURE_PRESS]==1&&events[UI_GESTURE_TAP]==1&&tick_consumed[0]);
    reset();first.on_gesture=gesture;add(100,400,1,0);add(0,0,0,9);add(100,400,1,0);add(0,0,0,0);run();
    assert(events[UI_GESTURE_PRESS]==1&&events[UI_GESTURE_CANCEL]==1&&!events[UI_GESTURE_TAP]);
    reset();request_on_touch=true;menu_on_touch=true;add(100,400,1,0);run();assert(enters==2&&exits==1&&ticks==0&&!menus);
    reset();request_on_tick=true;menu_on_tick=true;add(0,0,0,0);run();assert(enters==2&&exits==1&&ticks==1&&!menus);
    reset();first.on_gesture=gesture;request_on_touch=true;cancel_clobber=true;add(100,400,1,0);run();
    assert(enters==2&&events[UI_GESTURE_CANCEL]==1&&!menus);
    reset();menu_on_tick=true;add(0,0,0,0);add(0,0,0,0);run();assert(menus==1&&ticks==1);
    reset();first.owns_keys=true;for(int k=0;k<3;k++){add(k*160+80,1500,1,0);add(0,0,0,0);}run();
    assert(keys[0]==1&&keys[1]==1&&keys[2]==1&&!menus&&fulls==1);
    reset();add(240,1500,1,0);add(0,0,0,0);add(400,1500,1,0);run();assert(!keys[1]&&!keys[2]&&menus==1&&fulls==2);
    reset();add(650,1150,1,0);add(0,0,0,0);add(100,220,1,0);add(0,0,0,0);run();
    assert(highlights==1&&restores==1&&enters==2&&exits==1);
    reset();add(650,1150,1,0);add(0,0,0,0);add(100,220,1,0);add(550,220,1,0);add(100,220,1,0);add(0,0,0,0);run();
    assert(highlights==1&&restores==1&&enters==1&&!exits);
    reset();add(650,1150,1,0);add(0,0,0,0);add(100,220,1,0);add(0,0,0,9);run();assert(restores==1&&enters==1);
    reset();first.on_gesture=gesture;full_tick=true;add(100,400,1,0);add(0,0,0,0);run();
    assert(events[UI_GESTURE_CANCEL]==1&&!events[UI_GESTURE_TAP]);
    reset();first.on_gesture=gesture;font_due=true;time_offset=4000000;add(100,400,1,0);add(0,0,0,0);run();
    assert(events[UI_GESTURE_CANCEL]==1&&!events[UI_GESTURE_TAP]&&tick_consumed[0]);
    reset();first.owns_keys=true;add(650,1150,1,0);add(0,0,0,0);add(240,1500,1,0);add(0,0,0,0);add(400,1500,1,0);run();
    assert(!keys[1]&&!keys[2]&&fulls==2&&menus==2);
    reset();add(650,1150,1,0);add(0,0,0,0);
    for(int i=0;i<3;i++){add(100,220,1,0);add(550,220,1,0);add(0,0,0,0);}run();
    assert(du_areas==6&&gl_areas==1);
    reset();time_step=1000000;add(650,1150,1,0);add(0,0,0,0);add(100,220,1,0);add(550,220,1,0);add(0,0,0,0);add(0,0,0,0);run();
    assert(du_areas==2&&gl_areas==1);
    reset();first.on_gesture=gesture;time_step=1000000;lock_due=true;
    add(100,400,1,0);add(100,400,1,0);add(100,400,1,0);add(0,0,0,0);run();
    assert(!lock_due&&events[UI_GESTURE_CANCEL]==1&&!events[UI_GESTURE_TAP]&&tick_consumed[2]);
    reset();first.owns_keys=true;first.on_key_long=long_key;time_step=200000;
    add(240,1500,1,0);add(240,1500,1,0);add(240,1500,1,0);add(240,1500,1,0);add(240,1500,1,0);add(0,0,0,0);run();
    assert(keys[1]==1&&long_keys==1&&menus==1);
    reset();first.owns_keys=true;first.on_key_long=long_key;time_step=200000;
    add(240,1500,1,0);add(0,0,0,0);run();assert(keys[1]==1&&!long_keys&&!menus);
    reset();first.owns_keys=true;first.on_key_long=long_key;time_step=300000;
    add(240,1500,1,0);add(80,1500,1,0);add(240,1500,1,0);add(240,1500,1,0);run();assert(!long_keys);
    reset();first.owns_keys=true;first.on_key_long=long_key;time_step=300000;
    add(240,1500,1,0);add(0,0,0,9);add(240,1500,1,0);add(240,1500,1,0);run();assert(!long_keys);
    reset();first.owns_keys=true;first.on_key_long=long_key;time_step=1000000;lock_due=true;
    add(0,0,0,0);add(240,1500,1,0);add(240,1500,1,0);add(240,1500,1,0);add(0,0,0,0);run();assert(!lock_due&&!long_keys);
    home_case();home_on_tick=true;add(0,0,0,0);add(0,0,0,0);run();
    assert(exits==0&&home_enters==0&&menus==1&&last_menu_leaf==1&&menu_background==&second);
    assert(home_renders==0&&ticks==1&&fulls==1&&mode==1);
    home_case();home_on_tick=true;add(0,0,0,0);add(400,1500,1,0);add(0,0,0,0);run();
    assert(exits==0&&home_enters==0&&menus==1&&home_renders==0);
    home_case();home_on_touch=true;request_on_touch=true;menu_on_touch=true;add(100,400,1,0);run();
    assert(exits==0&&home_enters==0&&menus==1&&home_renders==0&&ticks==0);
    home_case();second.on_gesture=gesture;home_on_touch=true;cancel_clobber=true;response=APP_REDRAW_PAGE;add(100,400,1,0);run();
    assert(exits==0&&home_enters==0&&menus==1&&events[UI_GESTURE_CANCEL]==1&&home_renders==0);
    home_case();second.owns_keys=true;second.on_key_long=long_key;home_on_key=true;time_step=300000;
    add(240,1500,1,0);add(240,1500,1,0);add(240,1500,1,0);add(240,1500,1,0);run();
    assert(exits==0&&home_enters==0&&menus==1&&!long_keys&&ticks==0);
    reset();first.render=home_render;first.on_tick=origin_tick;second.on_tick=return_tick;second.on_exit=leave;
    add(0,0,0,0);add(650,1150,1,0);add(0,0,0,0);add(550,500,1,0);add(0,0,0,0);
    add(100,220,1,0);add(0,0,0,0);add(0,0,0,0);run();
    assert(exits==2&&enters==3&&last_menu_leaf==1&&menu_background==&first&&home_renders==1);
    reset();first.render=home_render;first.on_tick=origin_tick;second.on_tick=return_tick;second.on_exit=leave;
    add(0,0,0,0);add(650,1150,1,0);add(0,0,0,0);add(550,500,1,0);add(0,0,0,0);
    add(100,220,1,0);add(0,0,0,0);add(0,0,0,0);add(400,1500,1,0);run();
    assert(exits==2&&enters==3&&rendered_leaf==7&&home_renders==2);
    reset();request_on_tick=true;first.render=home_render;first.on_tick=origin_tick;second.on_tick=return_tick;second.on_exit=leave;
    add(0,0,0,0);add(0,0,0,0);run();
    assert(exits==2&&enters==3&&!menus&&home_renders==2&&rendered_leaf==7);
    reset();first.render=home_render;first.on_enter=home_enter;first.on_tick=origin_tick;second.on_tick=return_tick;second.on_exit=leave;
    add(0,0,0,0);add(650,1150,1,0);add(0,0,0,0);add(550,500,1,0);add(0,0,0,0);
    add(100,220,1,0);add(0,0,0,0);add(0,0,0,0);
    add(100,220,1,0);add(0,0,0,0);add(0,0,0,0);add(400,1500,1,0);run();
    assert(exits==4&&enters==5&&last_menu_leaf==1&&menu_background==&first&&home_renders==2&&rendered_leaf==7);
    reset();start_second=true;second.on_tick=origin_tick;second.on_exit=leave;second.on_enter=home_enter;second.render=home_render;first.on_tick=return_tick;
    add(0,0,0,0);add(650,1150,1,0);add(0,0,0,0);add(100,120,1,0);add(0,0,0,0);add(0,0,0,0);add(400,1500,1,0);run();
    assert(exits==2&&enters==3&&last_menu_leaf==1&&menu_background==&second&&home_renders==2&&rendered_leaf==7);
    reset();first.on_tick=consumed_return_tick;second.on_tick=return_tick;second.on_exit=leave;
    add(0,0,0,0);add(0,0,0,0);add(0,0,0,0);run();
    assert(exits==2&&enters==3&&menus==1&&last_menu_leaf==0&&menu_background==&first);
    // 拔卡先关资源再回退字体，取消本轮按键，持续无卡不重复通知。
    // Removal closes consumers before fallback, drops the current key and notifies only once.
    reset();media_test=sd_font=true;mounted_steps[0]=true;time_step=600000;first.on_media_lost=lost;
    add(0,0,0,0);add(80,1500,1,0);add(0,0,0,0);run();
    assert(media_lost==1&&loss_step==1&&builtin_opens==1&&!keys[0]&&fulls==2);
    assert(global_cancels==1&&global_cancel_steps[1]==1);
    // 后台页即使被菜单遮盖也释放资源，仍保留菜单视图。
    // A menu-covered page still releases resources and retains the menu view.
    reset();media_test=sd_font=true;mounted_steps[0]=true;time_step=600000;first.on_media_lost=lost;
    add(650,1150,1,0);add(0,0,0,0);add(0,0,0,0);run();
    assert(media_lost==1&&builtin_opens==1&&menus==2&&ticks==0);
    assert(global_cancels==1&&global_cancel_steps[1]==1);
    // 延迟挂载后拔卡同样被捕获，使用内置字体无需再打开一次。
    // Removal after a late mount is detected without reopening an already built-in font.
    reset();media_test=true;mounted_steps[1]=true;time_step=600000;first.on_media_lost=lost;
    add(0,0,0,0);add(0,0,0,0);add(0,0,0,0);run();
    assert(media_lost==1&&loss_step==2&&!builtin_opens);
    // 手势和长按在拔卡边界取消，不能把旧按住状态变成新的动作。
    // Removal cancels gestures and holds without turning an old contact into another action.
    reset();media_test=true;mounted_steps[0]=true;time_step=600000;first.on_media_lost=lost;first.on_gesture=gesture;
    add(100,400,1,0);add(100,400,1,0);add(0,0,0,0);run();
    assert(media_lost==1&&events[UI_GESTURE_CANCEL]==1&&!events[UI_GESTURE_TAP]);
    reset();media_test=true;mounted_steps[0]=true;time_step=600000;first.on_media_lost=lost;first.owns_keys=true;first.on_key_long=long_key;
    add(240,1500,1,0);add(240,1500,1,0);add(240,1500,1,0);run();
    assert(media_lost==1&&keys[1]==1&&!long_keys);
    // 失效后不自动探测；显式恢复的挂载可重新加载保存字体并再次检测拔卡。
    // No automatic probing after invalidation; an explicit remount restores saved-font loading and loss detection.
    reset();media_test=sd_font=saved_sd_font=true;mounted_steps[0]=true;time_step=4000000;first.on_media_lost=lost;
    add(0,0,0,0);add(0,0,0,0);add(0,0,0,0);run();
    assert(media_lost==1&&builtin_opens==1&&!probes&&font_opens==1);
    reset();media_test=sd_font=saved_sd_font=true;mounted_steps[0]=mounted_steps[2]=true;time_step=4000000;first.on_media_lost=lost;
    add(0,0,0,0);add(0,0,0,0);add(0,0,0,0);add(0,0,0,0);run();
    assert(media_lost==2&&builtin_opens==2&&!probes&&font_opens==2);
    assert(global_cancels==2&&global_cancel_steps[1]==1&&global_cancel_steps[3]==1);
    // 安全归还后仅通知一次；未知读取或 USB 租约不能产生恢复事件。
    // Notify once after safe return; an unknown sample or USB lease cannot produce restoration.
    reset();font_case=true;time_step=600000;first.on_media_lost=lost;first.on_media_restored=restored;
    for(int i=0;i<6;i++)font_info_steps[i]=(read_pico_sd_info_t){.present=true,.mounted=true,.media_generation=1};
    font_info_steps[1].mounted=false;
    font_info_steps[2].exported=true;
    nav_idle(6);run();assert(media_lost==1&&media_restored==1);
    // 自有角落入口不会被旧菜单把手截获，默认页仍由以上42项覆盖。/ Owned corners bypass only the legacy hotspot.
    reset();first.custom_menu_handle=true;add(650,1150,1,0);add(0,0,0,0);run();assert(!menus&&touch_calls==1);
    // 电源锁屏扩展不退出应用或调用全局睡眠；中断当前触摸。/ A shell lock stays in-app and cancels the touch.
    reset();first.on_gesture=gesture;first.on_lock=shell_lock;lock_due=true;time_step=1000000;
    add(100,400,1,0);add(100,400,1,0);add(100,400,1,0);add(0,0,0,0);run();
    assert(shell_locks==1&&!exits&&events[UI_GESTURE_CANCEL]==1&&!events[UI_GESTURE_TAP]);
    // 锁屏接管关闭旧菜单，后续触摸与时钟计时继续送达页面。
    // Callback locking closes the old menu so subsequent input and clock ticks reach the page.
    reset();first.on_lock=shell_lock;lock_due=true;time_step=3000000;
    add(650,1150,1,0);add(0,0,0,0);add(100,400,1,0);run();
    assert(shell_locks==1&&menus==1&&ticks==2&&touch_calls==1&&!exits);
    // 自管字体页不被旧延迟重试覆盖，媒体失效监测仍运行。/ Font owners skip retries while retaining media monitoring.
    reset();first.manages_fonts=true;font_due=true;time_offset=4000000;add(0,0,0,0);add(0,0,0,0);run();assert(!font_opens);
    product_navigation_cases();
    global_bridge_cases();
    global_cancel_cases();
    font_retry_cases();
    continuous_cases();
    keep_awake_cases();
    product_power_cases();
    puts("app_loop: 58 scheduler + 10 global bridge + 9 font retry + 19 continuous gesture + 4 global cancellation + 8 keep-awake + 8 unified product-power scenarios passed");
    return 0;
}

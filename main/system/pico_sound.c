/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 *
 * 中文：GPIO2无源蜂鸣器的触摸、充电、错误短音；合并待播，不积压触摸。
 * English: Touch, charge and error cues on the GPIO2 passive buzzer; coalesce pending cues instead of queuing touches.
 * 冻结：官方tone是阻塞API，只能在本工作器调用；预览永不接触硬件。
 * Frozen: The official tone API blocks and runs only in this worker; previews never access hardware.
 */
#include "pico_sound.h"
#include "settings.h"
#include <stdatomic.h>
#include <stdio.h>
#if defined(ESP_PLATFORM) && !defined(PICO_NATIVE_PREVIEW)
#include "read_pico_buzzer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#define PS_SOUND_HARDWARE 1
static TaskHandle_t sound_worker;
#else
#define PS_SOUND_HARDWARE 0
#endif

typedef struct { uint16_t hz,ms,gap; } sound_note_t;
static const sound_note_t touch_notes[]={{2400,22,0}};
static const sound_note_t charge_notes[]={{1450,38,35},{2200,38,0}};
static const sound_note_t error_notes[]={{900,35,30},{700,55,0}};
static atomic_uint pending,last_event,played;
static atomic_bool paused,active,available;
static bool initialized,contact_down,charge_known,external_power;
static bool touch_clock,error_clock;
static int64_t touch_at,error_at;

static bool permitted(unsigned event) {
    unsigned value=app_settings_sound_flags(),bit=event==PS_SOUND_TAP?PS_SOUND_TOUCH:
        event==PS_SOUND_CHARGE?PS_SOUND_CHARGING:event==PS_SOUND_FAILURE?PS_SOUND_ERROR:0;
    return !atomic_load(&paused)&&(value&PS_SOUND_ENABLED)&&(value&bit);
}
#if PS_SOUND_HARDWARE
static const sound_note_t *notes_for(unsigned event,unsigned *count) {
    if(event==PS_SOUND_TAP){*count=sizeof(touch_notes)/sizeof(*touch_notes);return touch_notes;}
    if(event==PS_SOUND_CHARGE){*count=sizeof(charge_notes)/sizeof(*charge_notes);return charge_notes;}
    *count=sizeof(error_notes)/sizeof(*error_notes);return error_notes;
}
static void sound_task(void *arg) {
    (void)arg;
    for(;;) {
        (void)ulTaskNotifyTake(pdTRUE,portMAX_DELAY);
        for(;;) {
            // 领取待播前先公布busy，避免领取后被挂起而跨越一次睡眠/唤醒后补播旧音。
            // Publish busy before taking pending work so preemption cannot carry a claimed cue across sleep/wake.
            atomic_store(&active,true);
            unsigned event=atomic_exchange(&pending,PS_SOUND_NONE);
            if(event==PS_SOUND_NONE){atomic_store(&active,false);break;}
            if(permitted(event)) {
                unsigned count;const sound_note_t *notes=notes_for(event,&count);
                bool sounded=false;
                for(unsigned i=0;i<count&&permitted(event);i++) {
                    if(read_pico_buzzer_tone(notes[i].hz,notes[i].ms)!=ESP_OK)break;
                    sounded=true;
                    if(notes[i].gap)(void)ulTaskNotifyTake(pdTRUE,pdMS_TO_TICKS(notes[i].gap));
                }
                if(sounded){atomic_store(&last_event,event);atomic_fetch_add(&played,1);}
            }
            atomic_store(&active,false);
        }
    }
}
#endif
void ps_sound_init(void) {
    if(initialized)return;
    initialized=true;
#if PS_SOUND_HARDWARE
    if(read_pico_buzzer_init()==ESP_OK&&xTaskCreatePinnedToCore(sound_task,"pico_sound",2560,NULL,3,&sound_worker,1)==pdPASS)
        atomic_store(&available,true);
#else
    atomic_store(&available,true);
#endif
}
static void notify_worker(void) {
#if PS_SOUND_HARDWARE
    if(sound_worker)xTaskNotifyGive(sound_worker);
#endif
}
static void submit(unsigned event) {
    if(!initialized||!atomic_load(&available)||!permitted(event))return;
#if PS_SOUND_HARDWARE
    // 一个待播槽按错误、充电、触摸优先级合并；不会在手指停下后补播一串旧按键。
    // One pending slot coalesces error, charge and touch priorities; no stale key sequence plays after input stops.
    unsigned previous=atomic_load(&pending);
    while(previous<event&&!atomic_compare_exchange_weak(&pending,&previous,event)){}
    notify_worker();
#else
    atomic_store(&last_event,event);atomic_fetch_add(&played,1);
#endif
}
uint8_t ps_sound_options(void) { return app_settings_sound_flags(); }
bool ps_sound_configure(bool enabled,bool touch,bool charging,bool error) {
    uint8_t value=(enabled?PS_SOUND_ENABLED:0)|(touch?PS_SOUND_TOUCH:0)|
        (charging?PS_SOUND_CHARGING:0)|(error?PS_SOUND_ERROR:0);
    if(!app_settings_set_sound_flags(value))return false;
    atomic_store(&pending,PS_SOUND_NONE);notify_worker();
    return true;
}
void ps_sound_contact(bool valid,bool down,unsigned count,int64_t now_ms) {
    if(!valid)return;
    bool rising=down&&!contact_down;contact_down=down;
    if(!rising||count!=1)return;
    if(touch_clock&&now_ms>=touch_at&&now_ms-touch_at<55)return;
    touch_clock=true;touch_at=now_ms;submit(PS_SOUND_TAP);
}
void ps_sound_key(void) { submit(PS_SOUND_TAP); }
void ps_sound_charge_sample(bool valid,unsigned state,int64_t now_ms) {
    (void)now_ms;
    if(!valid||state<1||state>3)return;
    bool present=state==2||state==3;
    if(!charge_known){charge_known=true;external_power=present;return;}
    bool connected=present&&!external_power;external_power=present;
    if(connected)submit(PS_SOUND_CHARGE);
}
void ps_sound_error(void) {
    int64_t now_ms=ps_now();
    if(error_clock&&now_ms>=error_at&&now_ms-error_at<600)return;
    error_clock=true;error_at=now_ms;submit(PS_SOUND_FAILURE);
}
void ps_sound_pause(void) {
    atomic_store(&paused,true);atomic_store(&pending,PS_SOUND_NONE);notify_worker();
}
bool ps_sound_quiet(void) { return !atomic_load(&active); }
void ps_sound_resume(void) { contact_down=false;atomic_store(&paused,false); }
void ps_sound_snapshot(ps_sound_snapshot_t *out) {
    if(out)*out=(ps_sound_snapshot_t){.options=ps_sound_options(),.last_event=(uint8_t)atomic_load(&last_event),
        .available=atomic_load(&available),.simulated=!PS_SOUND_HARDWARE,.suspended=atomic_load(&paused),
        .busy=atomic_load(&active),.played=atomic_load(&played)};
}
void ps_sound_render(uint8_t *fb) {
    ps_view_t v=ps_view();uint8_t flags=ps_sound_options();bool enabled=flags&PS_SOUND_ENABLED;
    ps_heading(fb,"提示音","操作短音 · 充电升调 · 错误低双音");
    int top=v.landscape?88:112,rh=v.landscape?44:56,gap=v.landscape?7:12;
    const char *names[]={"提示音","按键与触摸","充电接入","错误提醒"};
    for(unsigned i=0;i<4;i++) {
        char label[64];snprintf(label,sizeof(label),"%s · %s",names[i],flags&(1u<<i)?"开":"关");
        ps_button(fb,20,top+i*(rh+gap),v.w-40,rh,label,(flags&(1u<<i))&&(i==0||enabled));
    }
    ps_wrap(fb,24,top+4*(rh+gap)+4,v.w-48,11,2,"关闭总开关即静音。锁屏和休眠期间不发提示音。",5);
}
bool ps_sound_event(const ps_event_t *event) {
    if(!event||event->type!=PS_TAP)return false;
    ps_view_t v=ps_view();int top=v.landscape?88:112,rh=v.landscape?44:56,gap=v.landscape?7:12;
    uint8_t flags=ps_sound_options();
    for(unsigned i=0;i<4;i++)if(ps_hit(event->x,event->y,20,top+i*(rh+gap),v.w-40,rh)) {
        if(i&&!(flags&PS_SOUND_ENABLED)){ps_toast("请先开启提示音");return true;}
        flags^=1u<<i;
        if(!ps_sound_configure(flags&PS_SOUND_ENABLED,flags&PS_SOUND_TOUCH,flags&PS_SOUND_CHARGING,flags&PS_SOUND_ERROR)) {
            ps_toast("提示音设置保存失败");ps_sound_error();
        }
        ps_dirty();return true;
    }
    return false;
}

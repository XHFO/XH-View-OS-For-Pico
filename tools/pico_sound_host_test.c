/* SPDX-License-Identifier: Apache-2.0
 * 中文：真实提示音工作器与策略；pthread只替换RTOS，tone只记录频率和任务边界。
 * English: Actual feedback worker/policy; pthread replaces RTOS and tone records pitch/task boundaries only.
 */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdatomic.h>
#include <pthread.h>
#include <time.h>
#include <unistd.h>
#define ESP_PLATFORM 1
#define ESP_OK 0
#define pdPASS 1
#define pdTRUE 1
#define portMAX_DELAY UINT32_MAX
#define pdMS_TO_TICKS(x) (x)
typedef struct {int w,h;bool landscape;} ps_view_t;
typedef struct {int type,x,y;} ps_event_t;
enum {PS_TAP};
typedef struct task {pthread_t thread;pthread_mutex_t lock;pthread_cond_t condition;unsigned notifications;bool stop;void (*fn)(void*);void *arg;} task_t;
typedef task_t *TaskHandle_t;
static task_t task;
static pthread_t main_thread;
static pthread_mutex_t audio_lock=PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t audio_changed=PTHREAD_COND_INITIALIZER;
static unsigned note_count,note_hz[1024],note_ms[1024],dirty_count,toast_count;
static bool hold_note,note_entered,fail_save,fail_tone;
static bool hold_claim,claim_entered;
static atomic_uchar saved_flags=15;
static int64_t fake_now=1000;
static atomic_uint checks;
#define CHECK(x) do{checks++;if(!(x)){fprintf(stderr,"FAIL %s:%d: %s\n",__FILE__,__LINE__,#x);abort();}}while(0)
static struct timespec deadline(unsigned ms) {
    struct timespec t;clock_gettime(CLOCK_REALTIME,&t);t.tv_sec+=ms/1000;t.tv_nsec+=(ms%1000)*1000000L;
    if(t.tv_nsec>=1000000000){t.tv_sec++;t.tv_nsec-=1000000000;}return t;
}
static void *task_main(void *arg){task_t *t=arg;t->fn(t->arg);return NULL;}
static int xTaskCreatePinnedToCore(void (*fn)(void*),const char *name,unsigned stack,void *arg,int priority,TaskHandle_t *out,int core) {
    CHECK(stack<=3072&&priority==3&&core==1&&!strcmp(name,"pico_sound"));
    task.fn=fn;task.arg=arg;pthread_mutex_init(&task.lock,NULL);pthread_cond_init(&task.condition,NULL);
    *out=&task;CHECK(!pthread_create(&task.thread,NULL,task_main,&task));return pdPASS;
}
static void xTaskNotifyGive(TaskHandle_t t){pthread_mutex_lock(&t->lock);t->notifications++;pthread_cond_signal(&t->condition);pthread_mutex_unlock(&t->lock);}
static unsigned ulTaskNotifyTake(int clear,unsigned timeout) {
    CHECK(clear==pdTRUE&&!pthread_equal(pthread_self(),main_thread));
    pthread_mutex_lock(&task.lock);struct timespec until=deadline(timeout==portMAX_DELAY?1000:timeout);
    while(!task.notifications&&!task.stop) {
        if(timeout==portMAX_DELAY)pthread_cond_wait(&task.condition,&task.lock);
        else if(pthread_cond_timedwait(&task.condition,&task.lock,&until))break;
    }
    if(task.stop){pthread_mutex_unlock(&task.lock);pthread_exit(NULL);}
    unsigned value=task.notifications;task.notifications=0;pthread_mutex_unlock(&task.lock);return value;
}
static int read_pico_buzzer_tone(uint32_t hz,uint32_t ms) {
    CHECK(!pthread_equal(pthread_self(),main_thread)&&hz>=70&&hz<=6000&&ms>0&&ms<=55);
    pthread_mutex_lock(&audio_lock);CHECK(note_count<1024);note_hz[note_count]=hz;note_ms[note_count++]=ms;
    note_entered=true;pthread_cond_broadcast(&audio_changed);
    while(hold_note)pthread_cond_wait(&audio_changed,&audio_lock);
    bool failed=fail_tone;pthread_mutex_unlock(&audio_lock);return failed?-1:ESP_OK;
}
static int read_pico_buzzer_init(void){CHECK(pthread_equal(pthread_self(),main_thread));return ESP_OK;}
static uint8_t app_settings_sound_flags(void){return saved_flags;}
static bool app_settings_set_sound_flags(uint8_t value){if(fail_save)return false;saved_flags=value;return true;}
static int64_t ps_now(void){return fake_now;}
static void ps_toast(const char *text){CHECK(text&&*text);toast_count++;}
static void ps_dirty(void){dirty_count++;}
static ps_view_t ps_view(void){return (ps_view_t){360,640,false};}
static void ps_heading(uint8_t *fb,const char *a,const char *b){(void)fb;CHECK(a&&b);}
static void ps_button(uint8_t *fb,int x,int y,int w,int h,const char *s,bool selected){(void)fb;(void)x;(void)y;(void)w;(void)h;(void)s;(void)selected;}
static void ps_wrap(uint8_t *fb,int x,int y,int w,int px,int lines,const char *s,int gray){(void)fb;(void)x;(void)y;(void)w;(void)px;(void)lines;(void)s;(void)gray;}
static bool ps_hit(int x,int y,int a,int b,int w,int h){return x>=a&&y>=b&&x<a+w&&y<b+h;}
static void sound_claim_probe(void) {
    pthread_mutex_lock(&audio_lock);
    if(hold_claim){claim_entered=true;pthread_cond_broadcast(&audio_changed);while(hold_claim)pthread_cond_wait(&audio_changed,&audio_lock);}
    pthread_mutex_unlock(&audio_lock);
}
#include "sound_api_under_test.h"
#include "sound_under_test.c"
static void wait_notes(unsigned count){pthread_mutex_lock(&audio_lock);struct timespec until=deadline(2000);while(note_count<count)CHECK(!pthread_cond_timedwait(&audio_changed,&audio_lock,&until));pthread_mutex_unlock(&audio_lock);}
static void wait_quiet(void){for(int i=0;i<2000&&(atomic_load(&active)||atomic_load(&pending));i++)usleep(1000);CHECK(ps_sound_quiet()&&!atomic_load(&pending));}
static unsigned notes(void){pthread_mutex_lock(&audio_lock);unsigned n=note_count;pthread_mutex_unlock(&audio_lock);return n;}
static void contact(int64_t when){ps_sound_contact(true,false,0,when-1);ps_sound_contact(true,true,1,when);}
int main(void) {
    main_thread=pthread_self();ps_sound_init();ps_sound_init();
    ps_sound_snapshot_t status;ps_sound_snapshot(&status);CHECK(status.available&&!status.simulated&&status.options==15);
    contact(1000);wait_notes(1);wait_quiet();CHECK(note_hz[0]==2400&&note_ms[0]==22);
    for(int i=0;i<200;i++)ps_sound_contact(true,true,1,1001+i);wait_quiet();CHECK(notes()==1);
    ps_sound_contact(false,false,0,1300);ps_sound_contact(true,true,1,1400);wait_quiet();CHECK(notes()==1);
    ps_sound_contact(true,false,0,1401);ps_sound_contact(true,true,2,1402);ps_sound_contact(true,true,1,1450);wait_quiet();CHECK(notes()==1);
    contact(1455);wait_notes(2);wait_quiet();contact(1470);wait_quiet();CHECK(notes()==2);
    ps_sound_charge_sample(true,2,2000);ps_sound_charge_sample(true,3,3000);ps_sound_charge_sample(false,1,4000);
    ps_sound_charge_sample(true,2,5000);wait_quiet();CHECK(notes()==2);
    ps_sound_charge_sample(true,1,6000);ps_sound_charge_sample(true,2,7000);wait_notes(4);wait_quiet();CHECK(note_hz[2]==1450&&note_hz[3]==2200);
    ps_sound_charge_sample(true,3,8000);ps_sound_charge_sample(true,0,9000);ps_sound_charge_sample(true,4,10000);ps_sound_charge_sample(true,2,11000);wait_quiet();CHECK(notes()==4);
    fake_now=12000;ps_sound_error();wait_notes(6);wait_quiet();CHECK(note_hz[4]==900&&note_hz[5]==700);
    fake_now=12001;ps_sound_error();wait_quiet();CHECK(notes()==6&&!dirty_count&&!toast_count);
    CHECK(ps_sound_configure(true,false,true,true));contact(13000);wait_quiet();CHECK(notes()==6);
    CHECK(ps_sound_configure(false,true,true,true));contact(14000);fake_now=14000;ps_sound_error();ps_sound_charge_sample(true,1,14000);ps_sound_charge_sample(true,2,15000);wait_quiet();CHECK(notes()==6);
    fail_save=true;CHECK(!ps_sound_configure(true,true,true,true)&&ps_sound_options()==14);fail_save=false;
    CHECK(ps_sound_configure(true,true,true,true));
    pthread_mutex_lock(&audio_lock);hold_note=true;note_entered=false;pthread_mutex_unlock(&audio_lock);
    contact(16000);wait_notes(7);CHECK(!ps_sound_quiet());
    for(int i=0;i<200;i++)contact(16100+i*60);CHECK(atomic_load(&pending)==PS_SOUND_TAP);
    fake_now=30000;ps_sound_error();ps_sound_charge_sample(true,1,31000);ps_sound_charge_sample(true,2,32000);CHECK(atomic_load(&pending)==PS_SOUND_FAILURE);
    ps_sound_pause();CHECK(!ps_sound_quiet()&&!atomic_load(&pending));contact(33000);fake_now=33000;ps_sound_error();
    ps_sound_charge_sample(true,1,34000);ps_sound_charge_sample(true,2,35000);CHECK(!atomic_load(&pending));
    pthread_mutex_lock(&audio_lock);hold_note=false;pthread_cond_broadcast(&audio_changed);pthread_mutex_unlock(&audio_lock);wait_quiet();CHECK(notes()==7);
    ps_sound_resume();ps_sound_charge_sample(true,2,36000);wait_quiet();CHECK(notes()==7);
    contact(37000);wait_notes(8);wait_quiet();CHECK(notes()==8);
    fail_tone=true;fake_now=38000;ps_sound_error();wait_notes(9);wait_quiet();CHECK(ps_sound_quiet());fail_tone=false;
    ps_sound_snapshot(&status);CHECK(status.last_event==PS_SOUND_TAP&&status.played==6&&!status.busy&&!status.suspended);
    pthread_mutex_lock(&audio_lock);hold_claim=true;claim_entered=false;pthread_mutex_unlock(&audio_lock);
    contact(39000);pthread_mutex_lock(&audio_lock);struct timespec until=deadline(2000);while(!claim_entered)CHECK(!pthread_cond_timedwait(&audio_changed,&audio_lock,&until));pthread_mutex_unlock(&audio_lock);
    ps_sound_pause();CHECK(!ps_sound_quiet()&&!atomic_load(&pending));
    pthread_mutex_lock(&audio_lock);hold_claim=false;pthread_cond_broadcast(&audio_changed);pthread_mutex_unlock(&audio_lock);
    wait_quiet();ps_sound_resume();wait_quiet();CHECK(notes()==9);
    // 按键不改变接触边沿，静音和睡眠同样生效。/ Keys preserve touch edges and share mute/sleep policy.
    unsigned before=notes();bool down=contact_down;int64_t at=touch_at;
    ps_sound_key();wait_notes(before+1);wait_quiet();CHECK(note_hz[before]==2400&&note_ms[before]==22&&contact_down==down&&touch_at==at);
    CHECK(ps_sound_configure(true,false,true,true));ps_sound_key();wait_quiet();CHECK(notes()==before+1);
    CHECK(ps_sound_configure(false,true,true,true));ps_sound_key();wait_quiet();CHECK(notes()==before+1);
    CHECK(ps_sound_configure(true,true,true,true));ps_sound_pause();ps_sound_key();wait_quiet();CHECK(notes()==before+1);
    ps_sound_resume();ps_sound_key();wait_notes(before+2);wait_quiet();CHECK(notes()==before+2);
    ps_sound_pause();pthread_mutex_lock(&task.lock);task.stop=true;pthread_cond_signal(&task.condition);pthread_mutex_unlock(&task.lock);CHECK(!pthread_join(task.thread,NULL));
    printf("pico sound: %u checks passed; real worker/policy, recorded pitch, contact throttling, PMU edges, mute, queue cancellation and power-idle gating\n",checks);
    return 0;
}

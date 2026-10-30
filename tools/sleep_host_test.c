/* 中文：执行真实睡眠路径；IRQ、时间和外设由可重复样本驱动。
 * English: Execute the real sleep path with reproducible IRQ, time and peripheral samples. */
#include <assert.h>
#include <stdio.h>
#include "sleep_host_stubs.h"
#include "sleep_api_under_test.h"
bool read_pico_pmu_take_key_short(void);
#include "sleep_under_test.c"

enum { KEY_NONE, KEY_DOWN, KEY_SHORT, KEY_UP, KEY_LONG };
typedef struct { int64_t at; int count,x,y,error; int key; } sample_t;
static sample_t samples[40];
static int sample_count,sleep_calls,delay_calls,acc_downs,acc_ups,touch_sleep_calls,touch_read_calls,touch_wake_calls,ioe_parks;
static int64_t now_ms,key_consumed_at;
static uint64_t timer_us;
static bool gpio_enabled[64],acc_on,rails_on,ioe_stuck,tp_stuck,tp_pulse,tp_hide_once;
static int wake_cause;
static int sleep_error,timer_error,ioe_error,gpio_error;
static uint8_t last_wake;
static cst836u_mode_t touch_mode;
static test_pmu_status_t pmu;

static const sample_t* current(void) {
    static const sample_t empty={0};
    const sample_t* result=&empty;
    for(int i=0;i<sample_count;i++)if(samples[i].at<=now_ms)result=&samples[i];
    return result;
}
static void add(int offset,int count,int x,int y,int error,bool key) {
    assert(sample_count<(int)(sizeof(samples)/sizeof(samples[0])));
    samples[sample_count++]=(sample_t){now_ms+offset,count,x,y,error,key?KEY_SHORT:KEY_NONE};
}
static void add_key(int offset,int event) {
    add(offset,0,0,0,ESP_OK,false);samples[sample_count-1].key=event;
}
static void reset(void) {
    app_product_lock_end();
    memset(samples,0,sizeof(samples));memset(gpio_enabled,0,sizeof(gpio_enabled));
    sample_count=sleep_calls=delay_calls=acc_downs=acc_ups=touch_sleep_calls=touch_read_calls=touch_wake_calls=ioe_parks=0;
    sleep_error=timer_error=ioe_error=gpio_error=0;now_ms=1;key_consumed_at=-1;timer_us=0;
    acc_on=rails_on=true;ioe_stuck=tp_stuck=tp_pulse=tp_hide_once=false;wake_cause=ESP_SLEEP_WAKEUP_TIMER;last_wake=0;touch_mode=CST836U_MODE_NORMAL;memset(&pmu,0,sizeof(pmu));
    app_sleep_bind((void*)1,(void*)2);
}
static void begin(bool low_power) {
    int previous_sleeps=touch_sleep_calls;
    assert(app_product_lock_begin(low_power,false)==ESP_OK);
    assert(!acc_on&&!rails_on&&touch_mode==CST836U_MODE_DEEPSLEEP&&touch_sleep_calls==previous_sleeps+1);
    assert(!gpio_enabled[43]&&!touch_read_calls);
}
const char* esp_err_to_name(int err) {(void)err;return "stub";}
esp_err_t sleep_cpu_configure(bool enabled) {(void)enabled;return ESP_OK;}
int64_t esp_timer_get_time(void) {return now_ms*1000;}
void vTaskDelay(int ms) {assert(ms>0);delay_calls++;now_ms+=ms;}
esp_err_t gpio_config(const gpio_config_t* config) {(void)config;return gpio_error;}
esp_err_t gpio_wakeup_enable(gpio_num_t pin,int mode) {(void)mode;assert(pin>=0&&pin<64);gpio_enabled[pin]=true;return gpio_error;}
esp_err_t gpio_wakeup_disable(gpio_num_t pin) {assert(pin>=0&&pin<64);gpio_enabled[pin]=false;return ESP_OK;}
int gpio_get_level(gpio_num_t pin) {if(pin==43&&tp_hide_once){tp_hide_once=false;return 1;}return pin==41?(ioe_stuck?0:1):((current()->count&&!tp_pulse)||tp_stuck?0:1);}
esp_err_t esp_sleep_enable_gpio_wakeup(void) {return gpio_error;}
esp_err_t esp_sleep_enable_timer_wakeup(uint64_t us) {assert(us>0);timer_us=us;return timer_error;}
esp_err_t esp_sleep_disable_wakeup_source(int source) {(void)source;timer_us=0;return ESP_OK;}
esp_err_t esp_light_sleep_start(void) {
    sleep_calls++;assert(sleep_calls<20000);if(sleep_error)return sleep_error;
    int64_t until=now_ms+(int64_t)(timer_us/1000);assert(until>now_ms);bool interrupted=false;
    for(int i=0;i<sample_count;i++) {
        if(samples[i].at>now_ms&&samples[i].at<=until&&
           ((samples[i].key&&gpio_enabled[41])||(!samples[i].key&&gpio_enabled[43]))){until=samples[i].at;interrupted=true;}
    }
    wake_cause=interrupted?ESP_SLEEP_WAKEUP_GPIO:ESP_SLEEP_WAKEUP_TIMER;
    now_ms=until+(tp_pulse&&interrupted?2:0);return ESP_OK;
}
int esp_sleep_get_wakeup_cause(void){return wake_cause;}
esp_err_t cst836u_read(void* tp,cst836u_touch_t* touch) {
    (void)tp;touch_read_calls++;const sample_t* s=current();*touch=(cst836u_touch_t){s->count>0,(uint8_t)s->count,(uint16_t)s->x,(uint16_t)s->y};return s->error;
}
esp_err_t cst836u_set_mode(void* tp,cst836u_mode_t mode) {(void)tp;touch_mode=mode;if(mode==CST836U_MODE_DEEPSLEEP)touch_sleep_calls++;return ESP_OK;}
cst836u_mode_t cst836u_get_mode(void* tp) {(void)tp;return touch_mode;}
esp_err_t cst836u_wake(void* tp) {(void)tp;touch_wake_calls++;touch_mode=CST836U_MODE_NORMAL;return ESP_OK;}
gpio_num_t cst836u_int_gpio(void* tp) {return tp?43:GPIO_NUM_NC;}
bool cst836u_int_asserted(void* tp) {(void)tp;return gpio_get_level(43)==0;}
bool sc7a20h_powered(void* acc) {return acc&&acc_on;}
esp_err_t sc7a20h_power_down(void* acc) {(void)acc;acc_on=false;acc_downs++;return ESP_OK;}
esp_err_t sc7a20h_power_up(void* acc) {(void)acc;acc_on=true;acc_ups++;return ESP_OK;}
esp_err_t sc7a20h_read_events(void* acc,sc7a20h_events_t* ev) {(void)acc;memset(ev,0,sizeof(*ev));return ESP_OK;}
esp_err_t sc7a20h_ack_int(void* acc) {(void)acc;return ESP_OK;}
void sc7a20h_aoi_decode(uint8_t src,sc7a20h_aoi_src_t* aoi) {(void)src;aoi->ia=false;}
int sc7a20h_int1_level(void* acc) {(void)acc;return 0;}
gpio_num_t sc7a20h_int1_gpio(void* acc) {(void)acc;return 40;}
esp_err_t sc7a20h_arm_pickup_wake(void* acc,const sc7a20h_motion_cfg_t* cfg) {(void)acc;(void)cfg;return ESP_OK;}
esp_err_t sc7a20h_config_light_sleep_wakeup(void* acc) {(void)acc;return ESP_OK;}
esp_err_t sc7a20h_int1_begin(void* acc) {(void)acc;return ESP_OK;}
bool read_pico_buzzer_score_busy(void) {return false;}
esp_err_t read_pico_buzzer_score_stop(void) {return ESP_OK;}
esp_err_t read_pico_ioe_sleep(bool off) {assert(!off);ioe_parks++;return ioe_error;}
esp_err_t read_pico_ioe_wake(void) {return ESP_OK;}
void read_pico_clear_ioe_int(void) {}
bool read_pico_rails_on(void) {return rails_on;}
void read_pico_pmu_drain_events(void) {key_consumed_at=now_ms;}
esp_err_t read_pico_pmu_poll(void) {return ESP_OK;}
const test_pmu_status_t* read_pico_pmu_get(void) {return &pmu;}
bool read_pico_pmu_take_key_wakeup(void) {
    bool wake=false;
    for(int i=0;i<sample_count;i++)if(samples[i].at<=now_ms&&samples[i].at>key_consumed_at)
        wake|=samples[i].key==KEY_DOWN||samples[i].key==KEY_SHORT;
    key_consumed_at=now_ms;return wake;
}
bool read_pico_pmu_take_key_short(void) {
    bool wake=false;
    for(int i=0;i<sample_count;i++)if(samples[i].at<=now_ms&&samples[i].at>key_consumed_at)
        wake|=samples[i].key==KEY_SHORT;
    key_consumed_at=now_ms;return wake;
}
pmu_product_key_action_t read_pico_pmu_take_product_key(void) {
    pmu_product_key_action_t result=PMU_PRODUCT_KEY_NONE;
    for(int i=0;i<sample_count;i++)if(samples[i].at<=now_ms&&samples[i].at>key_consumed_at){
        if(samples[i].key==KEY_LONG)result=PMU_PRODUCT_KEY_LONG;
        else if(samples[i].key==KEY_SHORT&&result!=PMU_PRODUCT_KEY_LONG)result=PMU_PRODUCT_KEY_SHORT;
    }
    key_consumed_at=now_ms;return result;
}
esp_err_t read_pico_pmu_power_off(void) {return ESP_FAIL;}
esp_err_t read_pico_pmu_report_sleep(void) {return ESP_FAIL;}
void pmu_selftest_prepare_powerdown(void) {}
void epd_poweroff(void) {rails_on=false;}
void epd_poweron(void) {rails_on=true;}
void epd_clear(void) {}
int epd_width(void) {return 960;}
int epd_height(void) {return 540;}
int epd_get_rotation(void) {return 0;}
void epd_hl_set_all_white(EpdiyHighlevelState* hl) {(void)hl;}
void epd_hl_update_screen_from_white(EpdiyHighlevelState* hl,int mode,int temp) {(void)hl;(void)mode;(void)temp;}
uint8_t* epd_hl_get_framebuffer(EpdiyHighlevelState* hl) {(void)hl;return NULL;}
app_sleep_mode_t app_settings_sleep_mode(void) {return APP_SLEEP_OFF;}
const char* app_sleep_mode_name(app_sleep_mode_t mode) {(void)mode;return "off";}
bool app_settings_pickup_wake(void) {return false;}
uint8_t app_settings_last_wake(void) {return last_wake;}
void app_settings_set_last_wake(uint8_t wake) {last_wake=wake;}
bool ui_image_validate(const uint8_t* p,size_t bytes,int w,int h) {(void)p;(void)bytes;(void)w;(void)h;return false;}
bool ui_image_decode(uint8_t* dst,size_t len,const uint8_t* p,size_t bytes,int w,int h,ui_image_rotation_t rot) {(void)dst;(void)len;(void)p;(void)bytes;(void)w;(void)h;(void)rot;return false;}
uint8_t* ui_packed_asset_unpack(const uint8_t* p,size_t bytes,size_t* image_bytes) {(void)p;(void)bytes;*image_bytes=0;return NULL;}
const uint8_t lock_4bpp_p4b_deflate_start[] asm("_binary_lock_4bpp_p4b_deflate_start")={0};
const uint8_t lock_4bpp_p4b_deflate_end[] asm("_binary_lock_4bpp_p4b_deflate_end")={0};

static void taps(int offset) {
    add(offset,1,200,200,ESP_OK,false);add(offset+60,0,200,200,ESP_OK,false);
    add(offset+160,1,210,205,ESP_OK,false);add(offset+220,0,210,205,ESP_OK,false);
}
static void begin_touch(bool light) {
    assert(app_product_lock_begin(light,true)==ESP_OK);
    assert(!rails_on&&!acc_on&&touch_mode==CST836U_MODE_NORMAL&&!touch_sleep_calls);
    assert(gpio_enabled[43]==light);
}
static void touch_wake_cases(void) {
    reset();begin_touch(true);int reads=touch_read_calls;
    assert(app_product_lock_wait_step(5000)==APP_WAKE_NONE);
    assert(touch_read_calls==reads&&sleep_calls==5);
    // 两击间也只等IRQ，只有按住/等待松手时短暂读点。/ Between taps wait on IRQ; sample only an active contact/release.
    for(int idle=0;idle<2;idle++){
        reset();begin_touch(!idle);taps(100);
        assert(app_product_lock_wait_step(1500)==APP_WAKE_TOUCH_DOUBLE&&!current()->count);
        app_product_lock_end();assert(!gpio_enabled[41]&&!gpio_enabled[43]&&acc_on);
    }
    // 首查与arm间的触点不被禁用唤醒后的长睡吞掉。/ A contact between the first check and wake arming cannot be lost to long sleep.
    reset();begin_touch(true);taps(0);tp_hide_once=true;
    assert(app_product_lock_wait_step(1500)==APP_WAKE_TOUCH_DOUBLE);
    // IRQ 可在CPU恢复前回高，唤醒原因仍须使第一触点被读取。/ IRQ may return high before CPU resumes; its wake cause still requires a first-contact read.
    reset();begin_touch(true);tp_pulse=true;taps(100);
    assert(app_product_lock_wait_step(1500)==APP_WAKE_TOUCH_DOUBLE&&!current()->count);
    // 跨调用期限的GPIO事件不能丢失。/ A GPIO event at a wait deadline survives into the next step.
    reset();begin_touch(true);tp_pulse=true;taps(100);
    assert(app_product_lock_wait_step(100)==APP_WAKE_NONE);
    assert(app_product_lock_wait_step(1400)==APP_WAKE_TOUCH_DOUBLE);
    reset();add(0,1,100,100,ESP_OK,false);begin_touch(true);
    add(100,0,100,100,ESP_OK,false);add(200,1,100,100,ESP_OK,false);add(260,0,100,100,ESP_OK,false);
    assert(app_product_lock_wait_step(800)==APP_WAKE_NONE);
    taps(100);assert(app_product_lock_wait_step(800)==APP_WAKE_TOUCH_DOUBLE);
    for(int invalid=0;invalid<6;invalid++){
        reset();begin_touch(true);add(100,1,100,100,ESP_OK,false);
        if(invalid==1)add(600,0,100,100,ESP_OK,false);
        else if(invalid==2){add(120,1,300,300,ESP_OK,false);add(160,0,300,300,ESP_OK,false);}
        else if(invalid==3){add(120,2,100,100,ESP_OK,false);add(160,0,100,100,ESP_OK,false);}
        else add(160,0,100,100,ESP_OK,false);
        if(invalid>0){int second=invalid==5?900:invalid==1?700:260,x=invalid==4?600:100;add(second,1,x,100,ESP_OK,false);add(second+60,0,x,100,ESP_OK,false);}
        assert(app_product_lock_wait_step(1600)==APP_WAKE_NONE);
    }
    reset();begin_touch(true);add(100,1,100,100,ESP_FAIL,false);add(160,0,100,100,ESP_OK,false);
    add(260,1,100,100,ESP_OK,false);add(320,0,100,100,ESP_OK,false);
    assert(app_product_lock_wait_step(800)==APP_WAKE_NONE);
    reset();add(0,0,0,0,ESP_FAIL,false);begin_touch(true);reads=touch_read_calls;
    assert(app_product_lock_wait_step(5000)==APP_WAKE_NONE&&touch_read_calls==reads);
    reset();begin_touch(true);tp_stuck=true;reads=touch_read_calls;
    assert(app_product_lock_wait_step(5000)==APP_WAKE_NONE);
    assert(touch_read_calls<=reads+1&&sleep_calls<=51&&!gpio_enabled[43]);
    reset();begin_touch(true);add_key(50,KEY_LONG);
    assert(app_product_lock_wait_step(1000)==APP_WAKE_KEY_LONG);
    reset();begin(true);add_key(50,KEY_LONG);
    assert(app_product_lock_wait_step(1000)==APP_WAKE_KEY_LONG);
}
int main(void) {
    touch_wake_cases();
    reset();begin(true);int64_t started=now_ms;
    assert(app_product_lock_wait_step(2500)==APP_WAKE_NONE&&now_ms==started+2500&&sleep_calls>0);
    assert(touch_sleep_calls==1&&!touch_read_calls&&!acc_ups);app_product_lock_end();
    assert(acc_on&&acc_downs==1&&acc_ups==1&&touch_mode==CST836U_MODE_NORMAL&&touch_wake_calls==1&&!gpio_enabled[41]&&!gpio_enabled[43]&&!timer_us);

    // 真实浅睡与蓝牙限制时的CPU待机都只认完成的电源短按；屏幕接触完全不轮询。
    // Both light sleep and CPU-idle fallback accept completed power clicks only, without polling touch.
    for(int idle=0;idle<2;idle++) {
        reset();begin(!idle);sleep_calls=delay_calls=0;taps(100);
        assert(app_product_lock_wait_step(1500)==APP_WAKE_NONE);
        assert(!last_wake&&!touch_read_calls&&!gpio_enabled[43]&&touch_sleep_calls==1);
        assert((sleep_calls>0)==!idle);
        if(idle)assert(delay_calls>0);
        // A raw key down must not wake and later generate a second action on release.
        add_key(50,KEY_DOWN);assert(app_product_lock_wait_step(200)==APP_WAKE_NONE);
        add_key(50,KEY_UP);add_key(50,KEY_SHORT);
        assert(app_product_lock_wait_step(1000)==APP_WAKE_KEY&&last_wake==APP_WAKE_KEY);
        assert(!touch_read_calls&&touch_mode==CST836U_MODE_DEEPSLEEP);
        app_product_lock_end();assert(acc_on&&touch_mode==CST836U_MODE_NORMAL&&!timer_us);
    }
    // 首次休眠画面绘制时排入的新SHORT不得被入口drain吞掉；同一事件只消费一次。
    // A new SHORT queued while presenting the lock image survives entry; no event wakes twice.
    for(int idle=0;idle<2;idle++) {
        reset();add_key(0,KEY_SHORT);begin(!idle);
        assert(app_product_lock_wait_step(100)==APP_WAKE_KEY);
        app_product_lock_end();begin(!idle);
        assert(app_product_lock_wait_step(100)==APP_WAKE_NONE);
        add_key(20,KEY_SHORT);assert(app_product_lock_wait_step(100)==APP_WAKE_KEY);
        app_product_lock_end();assert(touch_mode==CST836U_MODE_NORMAL);
    }
    // 连续八次休眠/唤醒不引入固定两秒屏蔽，也不留下触摸唤醒源或停机的IMU。
    // Eight rapid lock/wake cycles introduce no two-second blackout and restore peripherals each time.
    for(int idle=0;idle<2;idle++) {
        reset();
        for(int cycle=0;cycle<8;cycle++) {
            begin(!idle);add_key(20,KEY_SHORT);
            assert(app_product_lock_wait_step(100)==APP_WAKE_KEY);
            assert(!gpio_enabled[43]&&!touch_read_calls);
            app_product_lock_end();assert(acc_on&&touch_mode==CST836U_MODE_NORMAL&&!timer_us);
        }
        assert(now_ms<2000&&touch_sleep_calls==8&&touch_wake_calls==8&&acc_downs==8&&acc_ups==8);
    }
    reset();begin(true);ioe_stuck=tp_stuck=true;
    assert(app_product_lock_wait_step(200)==APP_WAKE_NONE&&sleep_calls==10);
    assert(!gpio_enabled[41]&&!gpio_enabled[43]);
    reset();begin(true);sleep_error=ESP_FAIL;
    assert(app_product_lock_wait_step(500)==APP_WAKE_ERROR&&sleep_calls==1&&!timer_us);
    reset();begin(true);timer_error=ESP_FAIL;
    assert(app_product_lock_wait_step(500)==APP_WAKE_ERROR&&!sleep_calls&&!timer_us);
    reset();begin(true);gpio_error=ESP_FAIL;
    assert(app_product_lock_wait_step(500)==APP_WAKE_ERROR&&!sleep_calls);
    reset();gpio_error=ESP_FAIL;
    assert(app_product_lock_begin(true,false)==ESP_FAIL&&acc_on&&!gpio_enabled[41]&&!gpio_enabled[43]);
    assert(app_product_lock_wait_step(100)==APP_WAKE_ERROR);
    reset();begin(true);int parks=ioe_parks;rails_on=true;
    assert(app_product_lock_wait_step(100)==APP_WAKE_NONE&&!rails_on&&ioe_parks==parks+1&&acc_downs==1&&!acc_ups);
    app_product_lock_end();assert(acc_on&&touch_mode==CST836U_MODE_NORMAL);
    puts("sleep: IRQ-only idle touch, optional complete double taps, rejected drag/hold/multitouch, stuck IRQ backoff, power short/long, repeated clicks, peripheral recovery and failure boundaries passed");
    return 0;
}

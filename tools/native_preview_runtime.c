/* SPDX-License-Identifier: Apache-2.0
 * 中文：运行当前固件页面、导航和手势的本地进程；标准输入为单行命令，标准输出仅JSON。
 * English: Local process for current firmware pages/navigation/gestures; line commands in, JSON only out.
 */
#include "native_preview_hal.h"
#include "pico_system.h"
#include "pico_dual.h"
#include "native_preview_book.h"
#include "ui_gesture.h"
#include "ui_menu.h"
#include "ttf_font.h"
#include "read_pico_pmu.h"
#include "read_pico_ble_pager.h"
#include "settings.h"
#include "nvs.h"
#include "native_home_http_stub.h"
#include <pthread.h>
#include <stdatomic.h>
#include <stdarg.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>
#include "../main/system/pico_system.c"

static char runtime_root[1024],runtime_font[1024],runtime_frame[1024];
static int64_t runtime_now=10000;
static atomic_uint runtime_rng=0x8e314a2bu;
static time_t runtime_epoch;
static int64_t runtime_epoch_ms=10000;
static const app_desc_t *runtime_app;
static const app_desc_t *runtime_origins[10];static int runtime_origin_count;
static app_ctx_t runtime_ctx;
static cst836u_touch_t runtime_touch;
static ui_gesture_t runtime_gesture;
static int runtime_held_key=-1;
static int64_t runtime_held_since_ms;
static uint8_t runtime_fb[1216u*684u/2u];
static EpdiyHighlevelState runtime_hl;
static uint64_t runtime_revision,runtime_digest;
static bool runtime_force=true;
static bool runtime_media_mounted=true;
extern void native_preview_set_frame(uint8_t*);
extern bool native_preview_save_frame(const char*,const uint8_t*);
extern unsigned native_preview_present_count(void);
extern void native_preview_last_refresh(EpdRect*,int*,bool*);
extern const char *native_preview_refresh_waveform(void);
extern unsigned native_preview_continuous_count(void);
extern void native_preview_refresh_counts(unsigned*,unsigned*,unsigned*);
extern void native_preview_ble_tick(int64_t);
extern bool native_preview_ble_press(int);
extern const char *native_preview_book_path(void);
extern const char *native_preview_book_kind(void),*native_preview_book_view(void);
extern size_t native_preview_book_page(void),native_preview_book_chapter(void),native_preview_book_pages(void);
extern size_t native_preview_book_inline_images(void),native_preview_book_image_bytes(void);
extern bool native_preview_book_jump_open(void),native_preview_book_tools_open(void),native_preview_book_selection_active(void);
extern const app_desc_t app_font_pick,app_transfer;
const char *esp_err_to_name(esp_err_t e){(void)e;return "host service error";}
const char *native_preview_root(void){return runtime_root;}
const char *native_preview_font(void){return runtime_font;}
void native_preview_log(const char *tag,const char *format,...){va_list ap;va_start(ap,format);fprintf(stderr,"[%s] ",tag);vfprintf(stderr,format,ap);fputc('\n',stderr);va_end(ap);}
void pg_test_log(const char *tag,const char *format,...){va_list ap;va_start(ap,format);fprintf(stderr,"[%s] ",tag);vfprintf(stderr,format,ap);fputc('\n',stderr);va_end(ap);}
void *heap_caps_malloc(size_t n,unsigned c){(void)c;return malloc(n);}
void *heap_caps_calloc(size_t n,size_t s,unsigned c){(void)c;return calloc(n,s);}
void *heap_caps_realloc(void*p,size_t n,unsigned c){(void)c;return realloc(p,n);}
void *heap_caps_aligned_alloc(size_t a,size_t n,unsigned c){(void)c;void*p=NULL;return posix_memalign(&p,a,n)==0?p:NULL;}
void heap_caps_free(void*p){free(p);}
size_t heap_caps_get_largest_free_block(unsigned c){(void)c;return 64u*1024u*1024u;}
size_t heap_caps_get_free_size(unsigned c){(void)c;return 128u*1024u*1024u;}
uint32_t esp_random(void){uint32_t old=atomic_load(&runtime_rng),next;do{next=old;next^=next<<13;next^=next>>17;next^=next<<5;}while(!atomic_compare_exchange_weak(&runtime_rng,&old,next));return next;}
int64_t esp_timer_get_time(void){struct timespec ts;clock_gettime(CLOCK_MONOTONIC,&ts);return (int64_t)ts.tv_sec*1000000+ts.tv_nsec/1000;}
typedef struct host_task {pthread_t thread;pthread_mutex_t lock;pthread_cond_t condition;bool removed,detached;unsigned notifications;void(*fn)(void*);void*arg;} host_task_t;
static _Thread_local host_task_t *runtime_task;
static void host_task_cleanup(void*p){host_task_t*t=p;if(t->detached){pthread_mutex_destroy(&t->lock);pthread_cond_destroy(&t->condition);free(t);}}
static void *host_task_run(void *p){host_task_t*t=p;runtime_task=t;pthread_cleanup_push(host_task_cleanup,t);t->fn(t->arg);pthread_cleanup_pop(1);return NULL;}
int xTaskCreateWithCaps(void(*fn)(void*),const char*name,unsigned stack,void*arg,int priority,TaskHandle_t*out,unsigned caps){(void)name;(void)stack;(void)priority;(void)caps;host_task_t*t=calloc(1,sizeof(*t));if(!t)return pdFAIL;t->fn=fn;t->arg=arg;t->detached=out==NULL;pthread_mutex_init(&t->lock,NULL);pthread_cond_init(&t->condition,NULL);pthread_attr_t attr;pthread_attr_init(&attr);if(!out)pthread_attr_setdetachstate(&attr,PTHREAD_CREATE_DETACHED);pthread_t thread;int result=pthread_create(&thread,&attr,host_task_run,t);pthread_attr_destroy(&attr);if(result){pthread_mutex_destroy(&t->lock);pthread_cond_destroy(&t->condition);free(t);return pdFAIL;}if(out){t->thread=thread;*out=t;}return pdPASS;}
int xTaskCreate(void(*fn)(void*),const char*n,unsigned s,void*a,int p,TaskHandle_t*h){return xTaskCreateWithCaps(fn,n,s,a,p,h,0);}
int xTaskCreatePinnedToCore(void(*fn)(void*),const char*n,unsigned s,void*a,int p,TaskHandle_t*h,int c){(void)fn;(void)n;(void)s;(void)a;(void)p;(void)h;(void)c;return pdFAIL;}
void vTaskDeleteWithCaps(TaskHandle_t h){host_task_t*t=h;if(!t){pthread_exit(NULL);return;}pthread_mutex_lock(&t->lock);t->removed=true;pthread_cond_broadcast(&t->condition);pthread_mutex_unlock(&t->lock);pthread_join(t->thread,NULL);pthread_mutex_destroy(&t->lock);pthread_cond_destroy(&t->condition);free(t);}
void vTaskDelete(TaskHandle_t h){if(!h)pthread_exit(NULL);else vTaskDeleteWithCaps(h);}
void vTaskSuspend(TaskHandle_t h){host_task_t*t=h?h:runtime_task;if(!t)return;pthread_mutex_lock(&t->lock);while(!t->removed)pthread_cond_wait(&t->condition,&t->lock);pthread_mutex_unlock(&t->lock);pthread_exit(NULL);}
void vTaskDelay(unsigned ticks){struct timespec ts={ticks/1000,(long)(ticks%1000)*1000000};nanosleep(&ts,NULL);}
unsigned uxTaskGetStackHighWaterMark(TaskHandle_t h){(void)h;return 16384;}
SemaphoreHandle_t xSemaphoreCreateMutex(void){pthread_mutex_t*m=malloc(sizeof(*m));if(m)pthread_mutex_init(m,NULL);return m;}
int xSemaphoreTake(SemaphoreHandle_t s,unsigned ticks){(void)ticks;return s&&pthread_mutex_lock(s)==0;}
int xSemaphoreGive(SemaphoreHandle_t s){return s&&pthread_mutex_unlock(s)==0;}
void vSemaphoreDelete(SemaphoreHandle_t s){if(s){pthread_mutex_destroy(s);free(s);}}
unsigned ulTaskNotifyTake(int clear,unsigned ticks){
    host_task_t *t=runtime_task;if(!t){if(ticks!=portMAX_DELAY)vTaskDelay(ticks);return 0;}
    struct timespec deadline;clock_gettime(CLOCK_REALTIME,&deadline);deadline.tv_sec+=ticks/1000;deadline.tv_nsec+=(long)(ticks%1000)*1000000;if(deadline.tv_nsec>=1000000000){deadline.tv_sec++;deadline.tv_nsec-=1000000000;}
    pthread_mutex_lock(&t->lock);
    while(!t->notifications&&!t->removed&&ticks){int e=ticks==portMAX_DELAY?pthread_cond_wait(&t->condition,&t->lock):pthread_cond_timedwait(&t->condition,&t->lock,&deadline);if(e)break;}
    unsigned count=t->notifications;if(count){if(clear)t->notifications=0;else t->notifications--;}
    pthread_mutex_unlock(&t->lock);return count;
}
void xTaskNotifyGive(TaskHandle_t h){host_task_t *t=h;if(!t)return;pthread_mutex_lock(&t->lock);if(t->notifications!=0xffffffffu)t->notifications++;pthread_cond_signal(&t->condition);pthread_mutex_unlock(&t->lock);}
void app_lock_ignore_for(int64_t ms){(void)ms;}
static char runtime_setting_font[TTF_FONT_PATH_MAX];static uint32_t runtime_font_revision;
const char *app_settings_font_path(void){return runtime_setting_font;}
void app_settings_set_font_path(const char*p){
    snprintf(runtime_setting_font,sizeof(runtime_setting_font),"%s",p?p:"");runtime_font_revision++;
    nvs_handle_t h;if(nvs_open("read_pico",NVS_READWRITE,&h)==ESP_OK){uint8_t version=1;if(nvs_set_str(h,"font",runtime_setting_font)==ESP_OK&&nvs_set_blob(h,"font_ver",&version,sizeof(version))==ESP_OK)nvs_commit(h);nvs_close(h);}
}
uint32_t app_settings_font_revision(void){return runtime_font_revision;}
static bool stage_font(void){
    /* 中文：默认字体也位于私有TF中，真实字体选择页可发现它；不修改外部字库。 */
    mkdir("tf/fonts",0700);const char *target="tf/fonts/MiSansVF.ttf";struct stat input,staged;
    if(stat(runtime_font,&input))return false;
    if(!stat(target,&staged)&&S_ISREG(staged.st_mode)&&staged.st_size==input.st_size)return true;
    FILE *source=fopen(runtime_font,"rb"),*dest=source?fopen("tf/fonts/MiSansVF.ttf.tmp","wb"):NULL;
    if(!source||!dest){if(source)fclose(source);return false;}
    unsigned char block[65536];size_t bytes;bool ok=true;
    while((bytes=fread(block,1,sizeof(block),source))>0)if(fwrite(block,1,bytes,dest)!=bytes){ok=false;break;}
    if(ferror(source))ok=false;if(fclose(source))ok=false;if(fclose(dest))ok=false;
    if(ok&&rename("tf/fonts/MiSansVF.ttf.tmp",target)==0)return true;
    unlink("tf/fonts/MiSansVF.ttf.tmp");return false;
}
static void load_font_setting(void){
    snprintf(runtime_setting_font,sizeof(runtime_setting_font),"/sdcard/fonts/MiSansVF.ttf");
    nvs_handle_t h;if(nvs_open("read_pico",NVS_READONLY,&h)==ESP_OK){char saved[TTF_FONT_PATH_MAX];size_t size=sizeof(saved);if(nvs_get_str(h,"font",saved,&size)==ESP_OK)snprintf(runtime_setting_font,sizeof(runtime_setting_font),"%s",saved);nvs_close(h);}
}
int esp_log_level_get(const char*t){(void)t;return 2;}
void esp_log_level_set(const char*t,int l){(void)t;(void)l;}
int app_count(void){return 4;}
const app_desc_t *app_at(int n){const app_desc_t *list[]={&app_pico_system,&app_book,&app_font_pick,&app_transfer};return n>=0&&n<4?list[n]:NULL;}
int app_index_of(const app_desc_t*a){for(int i=0;i<4;i++)if(app_at(i)==a)return i;return -1;}
const app_desc_t *app_home_page(void){return &app_pico_system;}
const app_desc_t *app_selftest_page(void){return &app_pico_system;}
void ps_maintenance_tick(void){}

esp_err_t read_pico_sensor_wake(sc7a20h_handle_t h){(void)h;return ESP_OK;}
esp_err_t read_pico_sensor_sleep(sc7a20h_handle_t h){(void)h;return ESP_OK;}
// 显式测试样本，不读取电脑传感器。/ Explicit test samples, never host sensor readings.
static bool runtime_accel_valid;
static sc7a20h_sample_t runtime_accel;
esp_err_t read_pico_accel_read(sc7a20h_handle_t h,sc7a20h_sample_t*s){(void)h;if(!runtime_accel_valid)return ESP_ERR_NOT_SUPPORTED;*s=runtime_accel;return ESP_OK;}
static pmu_snapshot_t runtime_pmu={.present=true,.status_ok=true,.flags=PMU_STATUS_BATTERY_VALID,.charge_state=PMU_CHARGE_NOT_CHARGING,.soc_permille=860,.time_ok=true,.time_synced=true};
int read_pico_pmu_refresh(void){return ESP_OK;}
const pmu_snapshot_t *read_pico_pmu_get(void){runtime_pmu.unix_sec=(uint32_t)(runtime_epoch+(runtime_now-runtime_epoch_ms)/1000);return &runtime_pmu;}
// 中文：仅改变私有预览PMU快照，不探测电脑供电或设备硬件。
// English: Change only the private preview PMU snapshot, never host power or device hardware.
bool native_preview_power_scenario(const char *name){
    unsigned charge;
    if(!strcmp(name,"power-charging"))charge=PMU_CHARGE_CHARGING;
    else if(!strcmp(name,"power-full"))charge=PMU_CHARGE_FULL_INFERRED;
    else if(!strcmp(name,"power-battery"))charge=PMU_CHARGE_NOT_CHARGING;
    else if(!strcmp(name,"power-unknown"))charge=PMU_CHARGE_UNKNOWN;
    else if(!strcmp(name,"power-fault"))charge=PMU_CHARGE_FAULT;
    else return false;
    runtime_pmu.charge_state=charge;return true;
}
bool read_pico_pmu_ready(void){return true;}
int read_pico_pmu_cmd(uint16_t cmd,const uint8_t *data,size_t bytes){
    if(cmd==PMU_CMD_TIME_GET)return ESP_OK;
    if(cmd!=PMU_CMD_TIME_SYNC||!data||bytes!=4)return ESP_ERR_INVALID_ARG;
    runtime_epoch=(uint32_t)data[0]|(uint32_t)data[1]<<8|(uint32_t)data[2]<<16|(uint32_t)data[3]<<24;runtime_epoch_ms=runtime_now;return ESP_OK;
}
static const char *route_names[]={"书架","阅读","书签与笔记","家庭","打印监看","系统设置","Wi-Fi","家庭设置","打印设置","阅读设置","输入设置","休眠设置","快捷编排","小游戏","棋类对局","首次引导","翻页时钟","划线摘录","导入图书","输入","TF 卡存储","Wi-Fi 传书","USB 传输","休闲游戏","蓝牙翻页器","日期与时间","电源与锁屏","首页导航","应用","电脑画布","无线管理","提示音","网站流量","应用集","双机阅读","翻页动画"};
_Static_assert(ARRAY_COUNT(route_names)==PS_ROUTE_COUNT,"Every native route needs a readable name");
static uint64_t frame_hash(void){uint64_t h=UINT64_C(1469598103934665603);for(size_t i=0;i<sizeof(runtime_fb);i++)h=(h^runtime_fb[i])*UINT64_C(1099511628211);return h;}
void app_present(app_ctx_t *ctx,const app_desc_t *app,app_redraw_t redraw){
    if(redraw==APP_REDRAW_NONE||redraw==APP_REDRAW_DONE)return;
    if(ps_global_present(ctx,app,redraw))return;
    if(app->present&&app->present(ctx,redraw))return;
    if(app->render)app->render(ctx,ctx->fb);
    ps_global_draw_controls(app,ctx->fb);
    if(redraw==APP_REDRAW_FULL)update_display_full(ctx->hl);else update_display_mode(ctx->hl,MODE_GL16);
}
static void runtime_cancel_gesture(void){
    if(runtime_gesture.active&&runtime_app->on_gesture){
        ui_gesture_event_t event={.type=UI_GESTURE_CANCEL,.x0=runtime_gesture.x0,.y0=runtime_gesture.y0,.x=runtime_gesture.x,.y=runtime_gesture.y};
        const app_desc_t *requested=runtime_ctx.request_app;
        bool menu=runtime_ctx.request_menu,developer=runtime_ctx.request_developer_menu,back=runtime_ctx.request_return;
        app_redraw_t redraw=runtime_app->on_gesture(&runtime_ctx,&event);
        runtime_ctx.request_app=requested;runtime_ctx.request_menu=menu;runtime_ctx.request_developer_menu=developer;runtime_ctx.request_return=back;
        ui_gesture_reset(&runtime_gesture);app_present(&runtime_ctx,runtime_app,redraw);
    }
    ui_gesture_reset(&runtime_gesture);runtime_ctx.capture_gesture=false;
}
static void runtime_present_page(app_redraw_t redraw){
    if(redraw==APP_REDRAW_FULL||(redraw==APP_REDRAW_PAGE&&!runtime_app->continuous_gestures))runtime_cancel_gesture();
    app_present(&runtime_ctx,runtime_app,redraw);
}
static bool runtime_request_pending(void){return runtime_ctx.request_app||runtime_ctx.request_menu||runtime_ctx.request_developer_menu||runtime_ctx.request_return;}
static void follow_request(void){
    for(int attempt=0;attempt<6;attempt++){
        const app_desc_t *next=runtime_ctx.request_app;
        bool returning=runtime_ctx.request_return;
        if(returning){runtime_ctx.request_return=false;next=runtime_origin_count?runtime_origins[--runtime_origin_count]:&app_pico_system;}
        if(runtime_ctx.request_menu){runtime_ctx.request_menu=false;next=&app_pico_system;}
        if(!next)break;runtime_ctx.request_app=NULL;
        if(next==runtime_app)continue;
        if(!returning&&next!=&app_pico_system&&runtime_origin_count<10)runtime_origins[runtime_origin_count++]=runtime_app;
        if(!returning&&next==&app_pico_system)runtime_origin_count=0;
        if(runtime_app&&runtime_app->on_exit)runtime_app->on_exit(&runtime_ctx);
        runtime_app=next;runtime_held_key=-1;ui_gesture_reset(&runtime_gesture);ps_global_page(&runtime_ctx,runtime_app);
        if(runtime_app->on_enter)runtime_app->on_enter(&runtime_ctx);
        app_present(&runtime_ctx,runtime_app,APP_REDRAW_FULL);
    }
}
static void step(void){
    runtime_ctx.now_ms=runtime_now;runtime_ctx.consumed=false;runtime_ctx.capture_gesture=false;
    if(ps_power_run(&runtime_ctx,runtime_app)){ui_gesture_reset(&runtime_gesture);runtime_ctx.pressed=runtime_ctx.released=false;return;}
    ps_sound_contact(true,runtime_touch.touched,runtime_touch.count,runtime_now);
    native_preview_services_tick(runtime_now);
    native_preview_ble_tick(runtime_now);
    // 中文：模拟卡状态跨越挂载边界时复用页面媒体回调，先停消费者再切换字体。
    // English: Mirror page media callbacks across simulated mount changes, stopping consumers before font fallback.
    read_pico_sd_info_t media={0};esp_err_t media_result=read_pico_sd_get_info(&media);
    if(media_result!=ESP_ERR_NOT_FINISHED){
        bool mounted=media_result==ESP_OK&&media.present&&media.mounted&&!media.exported;
        if(runtime_media_mounted&&!mounted){
            if(runtime_app->on_media_lost)runtime_app->on_media_lost(&runtime_ctx);
            if(ttf_font_ready()&&!ttf_font_is_builtin())ttf_font_open_builtin();
            ui_gesture_reset(&runtime_gesture);app_present(&runtime_ctx,runtime_app,APP_REDRAW_FULL);
        }else if(!runtime_media_mounted&&mounted){
            if(runtime_app->on_media_restored)runtime_app->on_media_restored(&runtime_ctx);
            if(!runtime_app->manages_fonts)(void)ttf_font_open(app_settings_font_path());
            app_present(&runtime_ctx,runtime_app,APP_REDRAW_FULL);
        }
        runtime_media_mounted=mounted;
    }
    app_redraw_t global=APP_REDRAW_NONE;bool claimed=ps_global_input(&runtime_ctx,runtime_app,&global);
    // 与实机循环一致：全局存储/USB认领输入后取消页面手势，跳过页面定时回调。
    // 中文：沿用 app_loop 派发顺序；全局存储/USB 认领时取消页面手势，并跳过页面 tick。
    // English: Mirror app_loop; global storage/USB input claims cancel page gestures and skip page ticks.
    if(claimed){runtime_held_key=-1;runtime_cancel_gesture();runtime_ctx.consumed=true;app_present(&runtime_ctx,runtime_app,global);}
    if(!claimed&&runtime_ctx.pressed){
        int key=ui_key_hit_test(runtime_touch.x,runtime_touch.y);runtime_held_key=-1;runtime_ctx.consumed=true;
        if(key>=0)runtime_cancel_gesture();
        if(key>=0&&runtime_app->owns_keys){
            runtime_present_page(runtime_app->on_key?runtime_app->on_key(&runtime_ctx,key):APP_REDRAW_NONE);
            if(runtime_app->on_key_long&&runtime_touch.count==1){runtime_held_key=key;runtime_held_since_ms=runtime_now;}
        }else if(key==UI_KEY_2){app_present(&runtime_ctx,runtime_app,APP_REDRAW_FULL);}
        else if(key==UI_KEY_3){runtime_ctx.request_return=true;}
        else if(key>=0){runtime_present_page(runtime_app->on_key?runtime_app->on_key(&runtime_ctx,key):APP_REDRAW_NONE);}
        else if(runtime_app->on_gesture){runtime_ctx.consumed=false;}
        else{
            app_redraw_t redraw=runtime_app->on_touch?runtime_app->on_touch(&runtime_ctx,&runtime_touch):APP_REDRAW_NONE;
            app_present(&runtime_ctx,runtime_app,redraw);if(redraw==APP_REDRAW_NONE)runtime_ctx.consumed=false;
        }
    }
    if(runtime_app->on_gesture){
        ui_gesture_event_t event;
        if(ui_gesture_feed_continuous(&runtime_gesture,&runtime_ctx,runtime_app->continuous_gestures,&event)){
            app_redraw_t redraw=runtime_app->on_gesture(&runtime_ctx,&event);
            if(runtime_app->continuous_gestures&&runtime_ctx.capture_gesture)ui_gesture_capture(&runtime_gesture);
            runtime_ctx.capture_gesture=false;runtime_present_page(redraw);if(redraw!=APP_REDRAW_NONE)runtime_ctx.consumed=true;
        }
    }
    if(runtime_held_key>=0){
        if(runtime_request_pending()||!runtime_touch.touched||runtime_touch.count!=1||ui_key_hit_test(runtime_touch.x,runtime_touch.y)!=runtime_held_key||runtime_now<runtime_held_since_ms)runtime_held_key=-1;
        else if(!runtime_ctx.pressed&&runtime_now-runtime_held_since_ms>=UI_LONG_PRESS_MS){int key=runtime_held_key;runtime_held_key=-1;runtime_ctx.consumed=true;runtime_present_page(runtime_app->on_key_long(&runtime_ctx,key));}
    }
    if(!claimed&&!runtime_request_pending()&&runtime_app->on_tick){
        app_redraw_t redraw=runtime_app->on_tick(&runtime_ctx);if(redraw==APP_REDRAW_PAGE||redraw==APP_REDRAW_FULL)runtime_held_key=-1;runtime_present_page(redraw);
    }
    follow_request();runtime_ctx.pressed=runtime_ctx.released=false;
}
void native_preview_content_pump(void){runtime_now+=1;step();}
static void set_touch(const char *action,int x,int y,int64_t ms){
    if(!strcmp(action,"cancel")){ps_global_cancel_input();ui_gesture_reset(&runtime_gesture);if(runtime_app->on_gesture){ui_gesture_event_t e={.type=UI_GESTURE_CANCEL};app_present(&runtime_ctx,runtime_app,runtime_app->on_gesture(&runtime_ctx,&e));}runtime_touch.touched=false;runtime_touch.count=0;runtime_ctx.pressed=runtime_ctx.released=false;runtime_now=ms>runtime_now?ms:runtime_now+1;step();app_present(&runtime_ctx,runtime_app,APP_REDRAW_PAGE);return;}
    ps_view_t v=ps_view();int rx,ry;
    if(v.landscape){rx=y*684/360;ry=1215-x*1216/640;}else{rx=x*684/360;ry=y*1216/640;}
    enum EpdRotation r=epd_get_rotation();if(r==EPD_ROT_PORTRAIT||r==EPD_ROT_LANDSCAPE){rx=683-rx;ry=1215-ry;}
    runtime_touch.x=(uint16_t)rx;runtime_touch.y=(uint16_t)ry;
    bool up=!strcmp(action,"up")||!strcmp(action,"cancel");runtime_touch.touched=!up;runtime_touch.count=up?0:1;
    runtime_ctx.pressed=!strcmp(action,"down");runtime_ctx.released=up;runtime_now=ms>runtime_now?ms:runtime_now+1;step();
}
static void key_sample(int key,bool down,bool pressed,bool released,int delta){
    runtime_touch=(cst836u_touch_t){.x=(uint16_t)(80+key*160),.y=1500,.touched=down,.count=down?1:0};runtime_ctx.pressed=pressed;runtime_ctx.released=released;runtime_now+=delta;runtime_ctx.now_ms=runtime_now;ui_gesture_reset(&runtime_gesture);
    step();
}
static bool save_state(void){uint64_t h=frame_hash();bool changed=runtime_force||h!=runtime_digest;if(changed){runtime_digest=h;runtime_revision++;native_preview_save_frame(runtime_frame,runtime_fb);}runtime_force=false;return changed;}
static void runtime_json_string(const char *text){
    putchar('"');for(const unsigned char *p=(const unsigned char*)(text?text:"");*p;p++){
        if(*p=='"'||*p=='\\'){putchar('\\');putchar(*p);}else if(*p<32)printf("\\u%04x",*p);else putchar(*p);
    }putchar('"');
}
static bool runtime_stage_name(const char *name){
    size_t n=strlen(name);if(n<5||n>120||name[0]=='.'||strstr(name,"..")||strcmp(name+n-4,".tmp"))return false;
    for(size_t i=0;i<n;i++)if(!((name[i]>='a'&&name[i]<='z')||(name[i]>='A'&&name[i]<='Z')||(name[i]>='0'&&name[i]<='9')||name[i]=='-'||name[i]=='_'||name[i]=='.'))return false;
    return true;
}
static bool runtime_seed_ime(void){
#ifdef PICO_NATIVE_IME_PACKAGE
    // 只补充缺少的默认词库，不覆盖用户已替换的词库。/ Seed a missing default only; preserve user replacements.
    struct stat st;if(stat("/sdcard/readpico/ime/pinyin-v1.bin",&st)==0)return true;
    if(errno!=ENOENT)return false;
    if((mkdir("/sdcard/readpico",0700)&&errno!=EEXIST)||(mkdir("/sdcard/readpico/ime",0700)&&errno!=EEXIST))return false;
    FILE *input=fopen(PICO_NATIVE_IME_PACKAGE,"rb"),*output=input?fopen("/sdcard/readpico/ime/pinyin-v1.tmp","wb"):NULL;
    bool ok=input&&output;char buffer[4096];size_t n;
    if(ok){while((n=fread(buffer,1,sizeof(buffer),input)))if(fwrite(buffer,1,n,output)!=n){ok=false;break;}if(ferror(input))ok=false;}
    if(input&&fclose(input))ok=false;if(output&&fclose(output))ok=false;
    if(ok&&rename("/sdcard/readpico/ime/pinyin-v1.tmp","/sdcard/readpico/ime/pinyin-v1.bin")==0)return true;
    remove("/sdcard/readpico/ime/pinyin-v1.tmp");return false;
#else
    return true;
#endif
}
static bool runtime_seed_games(bool new_tf){
#ifdef PICO_NATIVE_GAME_PACKAGES
    // 中文：只对新的私有TF安装明确的开发样本；重启不得恢复用户已移除的游戏。
    // English: Seed explicit development packages only on a new private TF; restart never reinstalls removed games.
    if(!new_tf)return true;
    if(mkdir("/sdcard/apps",0700)&&errno!=EEXIST)return false;
    if(mkdir("/sdcard/apps/.staging",0700)&&errno!=EEXIST)return false;
    FILE *marker=fopen("/sdcard/apps/.defaults-initialized","wb");if(!marker)return false;
    bool marked=fputs("native-preview-development-packages-v1\n",marker)>=0;if(fclose(marker))marked=false;if(!marked)return false;
    const char *ids[]={"gomoku","go","xiangqi","chess","2048","snake"};
    for(unsigned i=0;i<sizeof(ids)/sizeof(ids[0]);i++){
        char from[2048],staged[256];snprintf(from,sizeof(from),"%s/%s.pga",PICO_NATIVE_GAME_PACKAGES,ids[i]);
        snprintf(staged,sizeof(staged),"/sdcard/apps/.staging/default-%s.tmp",ids[i]);
        FILE *input=fopen(from,"rb"),*output=input?fopen(staged,"wb"):NULL;bool copied=input&&output;char buffer[4096];size_t n;
        if(copied){while((n=fread(buffer,1,sizeof(buffer),input))!=0)if(fwrite(buffer,1,n,output)!=n){copied=false;break;}if(ferror(input))copied=false;}
        if(input&&fclose(input))copied=false;if(output&&fclose(output))copied=false;
        bool installed=copied&&ps_games_install(staged,false);remove(staged);
        if(!installed){fprintf(stderr,"Development game seed failed: %s: %s\n",ids[i],ps_games_last_error());return false;}
    }
#else
    (void)new_tf;
#endif
    return true;
}
static bool runtime_seed_tools(void){
#ifdef PICO_NATIVE_TOOL_PACKAGES
    // 版本标记只安装一次，保留升级前会话和用户之后的移除。/ Seed once per version, preserving old sessions and later removals.
    struct stat st;if(stat("/sdcard/apps/.tools-defaults-v2",&st)==0)return true;
    if(errno!=ENOENT)return false;
    if((mkdir("/sdcard/apps",0700)&&errno!=EEXIST)||(mkdir("/sdcard/apps/tools",0700)&&errno!=EEXIST))return false;
    bool old_defaults=stat("/sdcard/apps/.tools-defaults-v1",&st)==0;
    const char *ids[]={"drawing-notes","weread","home-link"};
    for(size_t i=0;i<sizeof(ids)/sizeof(ids[0]);i++){
    if(i==0&&old_defaults)continue; /* Preserve a drawing app deliberately removed from a v1 session. */
    char target[160],staged[160],source[2048];snprintf(target,sizeof(target),"/sdcard/apps/tools/%s.pga",ids[i]);snprintf(staged,sizeof(staged),"/sdcard/apps/tools/%s.tmp",ids[i]);snprintf(source,sizeof(source),"%s/%s.pga",PICO_NATIVE_TOOL_PACKAGES,ids[i]);
    if(stat(target,&st)!=0){
        if(errno!=ENOENT)return false;
        FILE *input=fopen(source,"rb"),*output=input?fopen(staged,"wb"):NULL;
        bool ok=input&&output;char buffer[4096];size_t n;
        if(ok){while((n=fread(buffer,1,sizeof(buffer),input)))if(fwrite(buffer,1,n,output)!=n){ok=false;break;}if(ferror(input))ok=false;}
        if(input&&fclose(input))ok=false;if(output&&fclose(output))ok=false;
        if(!ok||rename(staged,target)){remove(staged);return false;}
    }
    }
    FILE *marker=fopen("/sdcard/apps/.tools-defaults-v2","wb");if(!marker)return false;
    bool ok=fputs("native-development-tools-v2\n",marker)>=0;if(fclose(marker))ok=false;return ok;
#else
    return true;
#endif
}
static void state(bool ok,const char *error){bool changed=save_state();ps_view_t v=ps_view();printf("{\"ok\":%s,\"version\":\"%s\",\"width\":%d,\"height\":%d,\"logical_width\":%d,\"logical_height\":%d,\"route\":%d,\"route_name\":\"%s\",\"frame\":\"%s\",\"revision\":%llu,\"changed\":%s,\"now_ms\":%lld,\"panel\":%d,\"fan\":%s,\"fan_selected\":%d,\"fan_side\":%d,\"framebuffer\":\"packed4\",\"presents\":%u,\"active_app\":\"%s\",\"font_weight\":%d,\"rotation_locked\":%s,\"dual_locked\":%s",ok?"true":"false",PS_VERSION,epd_rotated_display_width(),epd_rotated_display_height(),v.w,v.h,(int)ps_route(),runtime_app==&app_font_pick?"字体选择":runtime_app==&app_transfer?"传书":route_names[ps_route()],runtime_frame,(unsigned long long)runtime_revision,changed?"true":"false",(long long)runtime_now,s_panel,s_fan?"true":"false",s_fan_selected,s_fan_side,native_preview_present_count(),runtime_app==&app_book?"book":runtime_app==&app_font_pick?"font":runtime_app==&app_transfer?"transfer":"system",ttf_get_weight(),ps_rotation_locked()?"true":"false",ps_dual_rotation_locked()?"true":"false");
    printf(",\"rotation\":%d,\"physical_hash\":\"%llx\"",epd_get_rotation(),(unsigned long long)runtime_digest);
    dl_status_t dual_status;ps_dual_status(&dual_status);printf(",\"dual_role\":%d,\"dual_connected\":%s,\"dual_synced\":%s,\"dual_scanning\":%s,\"dual_books\":%u,\"dual_spread\":%u,\"dual_revision\":%u",ps_dual_role(),dual_status.connected?"true":"false",dual_status.synced?"true":"false",dual_status.scanning?"true":"false",dual_status.books,dual_status.reading.spread,dual_status.reading.revision);
    printf(",\"dual_left_ready\":%s",dual_status.left_ready?"true":"false");
    printf(",\"dual_base_rotation\":%d,\"dual_primary_flip\":%s,\"dual_secondary_flip\":%s,\"dual_guard\":%s,\"dual_peer_reading\":%s,\"dual_remote_open\":%s,\"dual_game_active\":%s,\"dual_game_session\":%u",dual_status.orientation.inverted?EPD_ROT_INVERTED_PORTRAIT:EPD_ROT_PORTRAIT,dual_status.orientation.primary_flip?"true":"false",dual_status.orientation.secondary_flip?"true":"false",dual_status.guard?"true":"false",dual_status.peer_reading?"true":"false",dual_status.remote_open?"true":"false",dual_status.game_active?"true":"false",(unsigned)dual_status.game_session);
    printf(",\"dual_reused\":%u,\"dual_verified_bytes\":%u,\"dual_resumed\":%u",dual_status.reused,dual_status.verified_bytes,dual_status.resumed);
    if(runtime_app==&app_book)printf(",\"book_page\":%zu,\"book_pages\":%zu,\"book_chapter\":%zu,\"book_jump\":%s,\"book_tools\":%s,\"selection\":%s",native_preview_book_page()+1,native_preview_book_pages(),native_preview_book_chapter()+1,native_preview_book_jump_open()?"true":"false",native_preview_book_tools_open()?"true":"false",native_preview_book_selection_active()?"true":"false");
    if(runtime_app==&app_book)printf(",\"book_px\":%u",native_preview_book_px());
    printf(",\"dual_font_px\":%u,\"dual_reflow\":%s",dual_status.reading.font_px,dual_status.reading.reflow_pending?"true":"false");
    if(runtime_app==&app_book){printf(",\"dual_target\":%u,\"dual_page\":%u,\"dual_blank\":%s,\"page_start\":%zu,\"page_end\":%zu,\"book_path\":",native_preview_dual_target(),native_preview_dual_page(),native_preview_dual_blank()?"true":"false",native_preview_page_offset(false),native_preview_page_offset(true));runtime_json_string(native_preview_book_path());}
    if(runtime_app==&app_book)printf(",\"book_kind\":\"%s\",\"book_view\":\"%s\"",native_preview_book_kind(),native_preview_book_view());
    if(runtime_app==&app_book)printf(",\"book_images\":%zu,\"book_image_bytes\":%zu",native_preview_book_inline_images(),native_preview_book_image_bytes());
    if(error){printf(",\"error\":");runtime_json_string(error);}
    ps_sound_snapshot_t sound;ps_sound_snapshot(&sound);printf(",\"sound_played\":%u,\"sound_last\":%u,\"sound_options\":%u",(unsigned)sound.played,sound.last_event,sound.options);
    printf(",\"power_state\":%d,\"power_simulated\":true,\"power_long_available\":true,\"auto_sleep_minutes\":%u,\"doubletap_wake\":%s",ps_power_state(),ps_power_auto_minutes(),ps_power_doubletap()?"true":"false");
    printf(",\"games_count\":%u,\"games_error\":",ps_games_count());runtime_json_string(ps_games_last_error());
    printf(",\"dual_wifi_connected\":%s,\"dual_ble_simulated\":true",dual_status.data_connected?"true":"false");
    read_pico_ble_pager_status_t ble; read_pico_ble_pager_get_status(&ble);
    EpdRect refresh; int refresh_mode; bool partial;
    native_preview_last_refresh(&refresh,&refresh_mode,&partial);
    unsigned follow_count,default_count,gc16_count;native_preview_refresh_counts(&follow_count,&default_count,&gc16_count);
    printf(",\"refresh_waveform\":\"%s\",\"refresh_follow_count\":%u,\"refresh_default_count\":%u,\"refresh_gc16_count\":%u",native_preview_refresh_waveform(),follow_count,default_count,gc16_count);
    printf(",\"refresh_continuous_count\":%u,\"refresh_area_space\":\"%s\"",native_preview_continuous_count(),!strcmp(native_preview_refresh_waveform(),"CONTINUOUS")?"physical":"logical");
    printf(",\"ble_simulated\":true,\"ble_enabled\":%s,\"ble_connected\":%s,\"ble_scanning\":%s,\"ble_devices\":%u,\"ble_reversed\":%s,\"refresh_partial\":%s,\"refresh_mode\":%d,\"refresh_area\":[%d,%d,%d,%d]",ble.enabled?"true":"false",ble.connected?"true":"false",ble.scanning?"true":"false",ble.device_count,ble.reversed?"true":"false",partial?"true":"false",refresh_mode,refresh.x,refresh.y,refresh.width,refresh.height);
    printf(",\"services\":");native_preview_service_status_json(stdout);puts("}");fflush(stdout);
}
int main(int argc,char **argv){
    if(argc!=4){fprintf(stderr,"usage: native-preview FONT DATA_ROOT FRAME.pgm\n");return 2;}
    if(!realpath(argv[1],runtime_font)){perror("font");return 2;}mkdir(argv[2],0700);if(!realpath(argv[2],runtime_root)){perror("data root");return 2;}
    snprintf(runtime_frame,sizeof(runtime_frame),"%s",argv[3]);if(chdir(runtime_root)){perror("private data root");return 2;}
    struct stat existing_tf;bool new_tf=stat("tf",&existing_tf)!=0&&errno==ENOENT;
    mkdir("tf",0700);mkdir("tf/books",0700);mkdir("tf/books/.pico-system",0700);
    if(!runtime_seed_ime()){fprintf(stderr,"Unable to stage private TF lexicon\n");return 2;}
    if(!stage_font()){fprintf(stderr,"Unable to stage private TF font\n");return 2;}
    runtime_epoch=time(NULL);load_font_setting();native_preview_services_init();ttf_font_init();if(ttf_font_open(app_settings_font_path())!=ESP_OK){fprintf(stderr,"Unable to open requested real font\n");return 2;}
    memset(runtime_fb,255,sizeof(runtime_fb));native_preview_set_frame(runtime_fb);runtime_ctx=(app_ctx_t){.fb=runtime_fb,.hl=&runtime_hl,.now_ms=runtime_now,.touch=&runtime_touch};runtime_app=&app_pico_system;ps_global_enable(true);ps_global_page(&runtime_ctx,runtime_app);runtime_app->on_enter(&runtime_ctx);if(s_route==PS_ONBOARDING){s_config.onboard_done=1;save_config();s_depth=0;enter(PS_SHELF);}if(!runtime_seed_games(new_tf))return 2;ps_games_rescan();if(!runtime_seed_tools())return 2;ps_apps_register_navigation();app_present(&runtime_ctx,runtime_app,APP_REDRAW_FULL);
    char line[57344];while(fgets(line,sizeof(line),stdin)){
        char command[40]={0},action[80]={0};int x=0,y=0;long long ms=runtime_now;bool ok=true;const char*error=NULL;
        if(sscanf(line,"%39s",command)!=1)continue;
        if(!strcmp(command,"desktop-canvas")){extern void native_preview_desktop_canvas(const char*,FILE*);native_preview_desktop_canvas(line+15,stdout);memset(line,0,sizeof(line));continue;}
        if(!strcmp(command,"manage-content")){extern void native_preview_content_rpc(const char*,FILE*);native_preview_content_rpc(line+15,stdout);memset(line,0,sizeof(line));continue;}
        if(!strcmp(command,"manage-web")){extern void native_preview_management_rpc(const char*,FILE*);native_preview_management_rpc(line+11,stdout);memset(line,0,sizeof(line));continue;}
        if(!strcmp(command,"portal-web")){native_preview_home_portal_rpc(line+11,stdout);memset(line,0,sizeof(line));continue;}
        if(!strcmp(command,"portal-open")||!strcmp(command,"portal-status")){native_preview_home_portal_rpc(!strcmp(command,"portal-open")?"open":"status",stdout);memset(line,0,sizeof(line));continue;}
        if(!strcmp(command,"manage-open")||!strcmp(command,"manage-status")){extern void native_preview_management_rpc(const char*,FILE*);native_preview_management_rpc(!strcmp(command,"manage-open")?"open":"status",stdout);continue;}
        if(!strcmp(command,"touch")){if(sscanf(line,"%*s %79s %d %d %lld",action,&x,&y,&ms)<3){ok=false;error="invalid touch";}else set_touch(action,x,y,ms);}
        else if(!strcmp(command,"tick")){sscanf(line,"%*s %lld",&ms);runtime_now=ms>runtime_now?ms:runtime_now+1;step();}
        else if(!strcmp(command,"key")){if(sscanf(line,"%*s %d %79s",&x,action)!=2||x<0||x>2){ok=false;error="invalid key";}else{key_sample(x,true,true,false,1);if(!strcmp(action,"long"))key_sample(x,true,false,false,700);key_sample(x,false,false,true,40);if(!strcmp(action,"double")){key_sample(x,true,true,false,90);key_sample(x,false,false,true,40);}key_sample(x,false,false,false,310);}}
        else if(!strcmp(command,"rotate")){sscanf(line,"%*s %d",&x);if(!ps_power_pending()&&!ps_rotation_locked()){apply_rotation(x?EPD_ROT_INVERTED_LANDSCAPE:EPD_ROT_INVERTED_PORTRAIT);app_present(&runtime_ctx,runtime_app,APP_REDRAW_FULL);}}
        else if(!strcmp(command,"rotation")){if(sscanf(line,"%*s %d",&x)!=1||x<0||x>3){ok=false;error="invalid rotation";}else if(!ps_power_pending()&&!ps_rotation_locked()){apply_rotation((enum EpdRotation)x);app_present(&runtime_ctx,runtime_app,APP_REDRAW_FULL);}}
        else if(!strcmp(command,"display-body-failure")){extern void native_preview_fail_body(bool);sscanf(line,"%*s %d",&x);native_preview_fail_body(x!=0);step();}
        else if(!strcmp(command,"pose")){int z=0;if(sscanf(line,"%*s %d %d %d",&x,&y,&z)!=3){ok=false;error="invalid pose";}else{runtime_accel=(sc7a20h_sample_t){.x_mg=x,.y_mg=y,.z_mg=z};runtime_accel_valid=true;runtime_ctx.sensor_ready=true;step();}}
        else if(!strcmp(command,"rotation-lock")){sscanf(line,"%*s %d",&x);ps_rotation_lock(x!=0);step();}
        else if(!strcmp(command,"flip")){if(!ps_power_pending()){ps_rotate();app_present(&runtime_ctx,runtime_app,APP_REDRAW_FULL);step();}}
        else if(!strcmp(command,"usb-background")){ok=ps_usb_request_background();step();}
        else if(!strcmp(command,"route")){sscanf(line,"%*s %d",&x);if(!ps_power_pending()&&x>=0&&x<PS_ROUTE_COUNT){ps_navigate((ps_route_t)x);step();}else{ok=false;error="invalid route";}}
        else if(!strcmp(command,"sound-options")){if(sscanf(line,"%*s %d",&x)==1&&x>=0&&x<=15)ok=ps_sound_configure(x&1,x&2,x&4,x&8);else ok=false;}
        else if(!strcmp(command,"powerkey")){ps_power_toggle(&runtime_ctx,runtime_app);step();}
        else if(!strcmp(command,"powerlong")){ps_power_key_long(&runtime_ctx,runtime_app);step();}
        else if(!strcmp(command,"sleep")){ps_sleep();step();}
        else if(!strcmp(command,"power")){if(sscanf(line,"%*s %d",&x)==1&&x>=0&&x<=3){ps_power_request(x);step();}else{ok=false;error="invalid power action";}}
        else if(!strcmp(command,"wake")){ps_wake();step();}
        else if(!strcmp(command,"pager")){sscanf(line,"%*s %d",&x);ok=native_preview_ble_press(x);step();}
        else if(!strcmp(command,"scenario")){sscanf(line,"%*s %79s",action);ok=native_preview_service_scenario(action);step();}
        else if(!strcmp(command,"library")){book_store_notify_changed();ps_navigate(PS_SHELF);step();}
        else if(!strcmp(command,"app-open")){char id[32]={0},extra[2]={0};if(sscanf(line,"%*s %31s %1s",id,extra)!=1){ok=false;error="invalid app id";}else{ok=ps_apps_open(id);if(!ok)error=ps_games_last_error();step();}}
        else if(!strcmp(command,"games")){ps_games_rescan();step();}
        else if(!strcmp(command,"game-install")){
            char name[121]={0},extra[2]={0},staged[256];
            if(sscanf(line,"%*s %d %120s %1s",&x,name,extra)!=2||(x!=0&&x!=1)||!runtime_stage_name(name)){ok=false;error="invalid game upload";}
            else{snprintf(staged,sizeof(staged),"/sdcard/apps/.staging/%s",name);ok=ps_games_install(staged,x!=0);if(!ok)error=ps_games_last_error();step();}
        }
        else if(!strcmp(command,"game-remove")){
            char id[32]={0},extra[2]={0};if(sscanf(line,"%*s %31s %1s",id,extra)!=1){ok=false;error="invalid game id";}
            else{ok=ps_games_remove(id);if(!ok)error=ps_games_last_error();step();}
        }
        else if(!strcmp(command,"dual")){char host[48]={0},code[7]={0};if(sscanf(line,"%*s %d %47s %6s",&x,host,code)!=3)ok=false;else ok=ps_dual_configure(x,host,code);step();}
        else if(!strcmp(command,"dual-open")){line[strcspn(line,"\r\n")]=0;ok=ps_dual_open(line+10);step();}
        else if(!strcmp(command,"dual-wifi")){int enabled=0;ok=sscanf(line,"%*s %d",&enabled)==1;ps_dual_native_wifi(enabled!=0);step();}
        else if(!strcmp(command,"dual-radio")){int enabled=0;ok=sscanf(line,"%*s %d",&enabled)==1;pico_dual_ble_test_enabled(enabled!=0);step();}
        else if(!strcmp(command,"dual-primary")){char reply[1024];ok=ps_dual_usb_command("RPICO DUAL PRIMARY",reply,sizeof(reply));step();}
        else if(!strcmp(command,"dual-usb")){char reply[1024];line[strcspn(line,"\r\n")]=0;ok=ps_dual_usb_command(line+9,reply,sizeof(reply));if(ok){fprintf(stderr,"DUALUSB %s\n",reply);ok=strstr(reply,"\"accepted\":true")!=NULL;}step();}
        else if(!strcmp(command,"status")){}
        else if(!strcmp(command,"quit")){state(true,NULL);break;}
        else{ok=false;error="unknown command";}
        state(ok,error);
    }return 0;
}

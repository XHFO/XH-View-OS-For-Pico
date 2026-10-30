/* SPDX-License-Identifier: Apache-2.0
 * USB页面真实状态机检查，USB与文件生命周期为替身；不访问设备。
 * Actual USB-page state checks with USB/file-lifecycle doubles, without device access.
 */
#include "pico_system.h"
#include "read_pico_usb.h"
#include "read_pico_sd.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
const app_desc_t app_book={0};
static ps_view_t view={360,640,false};
static ps_route_t route=PS_USB_TRANSFER;
static int64_t now_ms=1000;
static read_pico_usb_status_t engine;
static read_pico_sd_info_t sd_card;
static bool retain_card;
static int checks,reads,starts,stops,prepares,finishes,restorations,tasks,navigations,draws,dirty;
static bool bridge,storage_busy,fail_prepare,fail_restore,fail_start,partial_start,fail_stop,fail_task,changed;
static void(*job)(void*);
#define CHECK(c) do{checks++;if(!(c)){fprintf(stderr,"USB UI failure line %d: %s\n",__LINE__,#c);assert(!"USB UI check failed");}}while(0)
ps_view_t ps_view(void){return view;}
ps_route_t ps_route(void){return route;}
int64_t ps_now(void){return now_ms;}
void ps_dirty(void){dirty++;}
void ps_toast(const char *text){CHECK(text&&*text);dirty++;}
bool ps_storage_busy(void){return storage_busy;}
bool ps_external_prepare(void){prepares++;if(fail_prepare)return false;CHECK(!bridge&&!engine.active);bridge=true;return true;}
bool ps_external_finished(bool written){CHECK(bridge&&!engine.active&&!sd_card.exported);bridge=false;changed=written;finishes++;return !fail_restore;}
void ps_media_restored(void){CHECK(!ps_usb_held()&&!bridge&&!engine.active&&!sd_card.exported);restorations++;}
void ps_navigate(ps_route_t target){if(ps_usb_before_leave(target))return;CHECK((!bridge&&!engine.active)||(ps_usb_background()&&ps_usb_local_route(target)));route=target;navigations++;if(route==PS_USB_TRANSFER)ps_usb_enter();}
void ps_legacy(const app_desc_t *app){CHECK(app==&app_book&&!bridge&&!engine.active);navigations++;}
esp_err_t read_pico_sd_get_info(read_pico_sd_info_t *out){reads++;*out=sd_card;return ESP_OK;}
esp_err_t read_pico_usb_init(void){return ESP_OK;}
void read_pico_usb_get_status(read_pico_usb_status_t *out){reads++;*out=engine;}
esp_err_t read_pico_usb_start(void){
    starts++;CHECK(bridge);engine.active=!fail_start||partial_start;engine.card_exported=engine.active;sd_card.exported=engine.active;engine.debug_restored=!engine.active;engine.state=fail_start?READ_PICO_USB_ERROR:READ_PICO_USB_EXPORTED;engine.last_error=fail_start?ESP_FAIL:ESP_OK;return engine.last_error;
}
esp_err_t read_pico_usb_stop(void){
    stops++;CHECK(bridge&&(engine.active||sd_card.exported));
    if(fail_stop){engine.state=READ_PICO_USB_ERROR;engine.last_error=ESP_FAIL;return ESP_FAIL;}
    sd_card.exported=retain_card;engine.active=engine.connected=engine.card_exported=engine.writing=false;engine.debug_restored=true;engine.state=READ_PICO_USB_IDLE;engine.last_error=ESP_OK;return ESP_OK;
}
int xTaskCreate(void(*fn)(void*),const char*n,unsigned stack,void*arg,int priority,void*handle){(void)n;(void)stack;(void)arg;(void)priority;(void)handle;tasks++;if(fail_task)return 0;CHECK(!job);job=fn;return 1;}
void vTaskDelete(void *p){(void)p;}
#ifndef USB_UI_REAL_RENDER
static void bounds(int x,int y,int w,int h){CHECK(x>=0&&y>=0&&w>=0&&h>=0&&x+w<=view.w&&y+h<=view.h);draws++;}
bool ps_hit(int x,int y,int rx,int ry,int rw,int rh){return x>=rx&&y>=ry&&x<rx+rw&&y<ry+rh;}
void ps_fill(uint8_t*fb,int x,int y,int w,int h,int gray){(void)fb;(void)gray;bounds(x,y,w,h);}
void ps_heading(uint8_t*fb,const char*a,const char*b){(void)fb;(void)a;(void)b;draws++;}
void ps_text(uint8_t*fb,int x,int y,int size,const char*t,int g){(void)fb;(void)t;(void)g;bounds(x,y,0,size);}
void ps_center(uint8_t*fb,int x,int y,int size,const char*t,int g){ps_text(fb,x,y,size,t,g);}
void ps_wrap(uint8_t*fb,int x,int y,int w,int size,int lines,const char*t,int g){(void)lines;bounds(x,y,w,size);ps_text(fb,x,y,size,t,g);}
void ps_icon(uint8_t*fb,int x,int y,int size,ps_icon_t icon,int g){(void)fb;(void)icon;(void)g;bounds(x,y,size,size);}
void ps_button(uint8_t*fb,int x,int y,int w,int h,const char*t,bool b){(void)fb;(void)t;(void)b;bounds(x,y,w,h);}
#endif
#include "pico_usb.c"
static void fixture(void){
    memset(&engine,0,sizeof(engine));memset(&sd_card,0,sizeof(sd_card));retain_card=false;engine.debug_restored=true;route=PS_USB_TRANSFER;
    pu_held=pu_running=pu_started=pu_stop=pu_confirm=pu_open_books=pu_visual_valid=pu_stop_failed=pu_eject_handled=pu_background=false;pu_target=PS_ROUTE_COUNT;pu_message[0]=0;pu_poll_at=pu_visual_at=0;atomic_store(&pu_done,false);
    reads=starts=stops=prepares=finishes=restorations=tasks=navigations=draws=dirty=0;bridge=storage_busy=fail_prepare=fail_restore=fail_start=partial_start=fail_stop=fail_task=changed=false;job=NULL;now_ms+=5000;ps_usb_init();ps_usb_enter();
}
static bool tick(void){now_ms+=251;bool result=ps_usb_tick();CHECK(restorations==finishes);return result;}
static void complete(void){CHECK(job);void(*fn)(void*)=job;job=NULL;fn(NULL);}
static void action(int id){pu_control_t c[2];int n=pu_controls(c);for(int i=0;i<n;i++)if(c[i].id==id){ps_event_t e={.type=PS_TAP,.x=c[i].x+c[i].w/2,.y=c[i].y+c[i].h/2};CHECK(ps_usb_event(&e));return;}CHECK(!"missing USB control");}
static void layout(void){
    pu_control_t c[2];int n=pu_controls(c);for(int i=0;i<n;i++){CHECK(c[i].x>=0&&c[i].y>=24&&c[i].w>0&&c[i].h>=32&&c[i].x+c[i].w<=view.w&&c[i].y+c[i].h<=view.h-52);for(int j=0;j<i;j++)CHECK(c[i].x>=c[j].x+c[j].w||c[j].x>=c[i].x+c[i].w||c[i].y>=c[j].y+c[j].h||c[j].y>=c[i].y+c[i].h);}
    int r=reads,s=starts,e=stops,p=prepares,f=finishes,t=tasks;ps_usb_render(NULL);CHECK(r==reads&&s==starts&&e==stops&&p==prepares&&f==finishes&&t==tasks);
}
static void begin(void){action(PU_START);CHECK(pu_running&&pu_held&&bridge&&!starts);tick();complete();tick();CHECK(pu_held&&!pu_running&&engine.active&&!finishes);}
static void test_flow(void){
    fixture();layout();storage_busy=true;CHECK(!ps_usb_request_start()&&!prepares);storage_busy=false;fail_prepare=true;action(PU_START);CHECK(!pu_held&&!pu_running&&!starts&&!bridge);layout();
    fixture();fail_task=true;action(PU_START);tick();tick();CHECK(!pu_held&&!pu_running&&!starts&&finishes==1&&!bridge);layout();
    fixture();fail_start=true;action(PU_START);tick();complete();tick();CHECK(!pu_held&&!pu_running&&starts==1&&finishes==1&&!bridge);layout();
    fixture();fail_start=partial_start=true;action(PU_START);tick();complete();tick();CHECK(pu_held&&engine.active&&!finishes);layout();action(PU_END);action(PU_END);tick();complete();tick();CHECK(!pu_held&&!bridge&&finishes==1);
    fixture();begin();CHECK(!ps_usb_request_start());layout();action(PU_END);CHECK(pu_confirm);layout();action(PU_CANCEL);CHECK(!pu_confirm&&pu_held);CHECK(ps_usb_before_leave(PS_HA));CHECK(pu_confirm&&pu_target==PS_HA);CHECK(ps_usb_back()&&!pu_confirm&&pu_target==PS_ROUTE_COUNT);layout();
    action(PU_END);action(PU_END);CHECK(pu_running&&pu_held);CHECK(ps_usb_back());layout();tick();complete();tick();CHECK(!pu_held&&!bridge&&finishes==1&&!navigations);layout();
    fixture();begin();pu_open_books=true;engine.host_ejected=true;tick();tick();complete();tick();CHECK(route==PS_SHELF&&!bridge&&finishes==1);
    fixture();begin();engine.connected=false;tick();CHECK(pu_held&&!pu_running&&!stops);engine.host_ejected=true;engine.writing=true;tick();CHECK(!pu_running&&!stops);engine.writing=false;tick();CHECK(pu_running&&pu_target==PS_TRANSFER);layout();tick();complete();tick();CHECK(!pu_held&&!bridge&&route==PS_TRANSFER&&navigations==1&&finishes==1);
    fixture();begin();ps_usb_before_leave(PS_HA);engine.host_ejected=true;tick();CHECK(pu_running&&pu_target==PS_HA);tick();complete();tick();CHECK(route==PS_HA&&!bridge);
    fixture();begin();engine.host_ejected=true;tick();tick();fail_stop=true;complete();tick();CHECK(pu_held&&!pu_running&&pu_stop_failed&&pu_eject_handled&&!finishes);layout();int jobs=tasks;for(int i=0;i<8;i++)tick();CHECK(tasks==jobs&&!pu_running&&stops==1&&!finishes);fail_stop=false;action(PU_RETRY);tick();complete();tick();CHECK(!pu_held&&!bridge&&route==PS_TRANSFER&&stops==2&&finishes==1);
    fixture();begin();action(PU_END);action(PU_END);fail_task=true;tick();tick();CHECK(pu_held&&pu_stop_failed&&!finishes&&!stops);layout();fail_task=false;action(PU_RETRY);tick();complete();fail_restore=true;tick();CHECK(!pu_held&&!bridge&&finishes==1&&!navigations&&strstr(pu_message,"检查 TF 卡"));layout();action(PU_STORAGE);CHECK(route==PS_STORAGE);
    fixture();begin();action(PU_END);action(PU_END);tick();retain_card=true;complete();tick();CHECK(pu_held&&pu_stop_failed&&bridge&&!finishes&&!engine.active&&sd_card.exported);layout();retain_card=false;action(PU_RETRY);tick();complete();tick();CHECK(!pu_held&&!bridge&&finishes==1);
    fixture();begin();tick();engine.written_blocks=1;CHECK(!tick());engine.written_blocks=2;tick();engine.written_blocks=3;tick();engine.written_blocks=4;CHECK(tick());engine.writing=true;CHECK(!tick());CHECK(!ps_usb_before_leave(PS_USB_TRANSFER));
}
static void background_action(int id){pu_control_t c[2];int n=pu_background_controls(c);for(int i=0;i<n;i++)if(c[i].id==id){ps_event_t e={.type=PS_TAP,.x=c[i].x+c[i].w/2,.y=c[i].y+c[i].h/2};CHECK(ps_usb_event(&e));return;}CHECK(!"missing background control");}
static void background_layout(void){int r=reads,s=starts,e=stops,p=prepares,f=finishes,t=tasks;ps_usb_background_render(NULL);CHECK(r==reads&&s==starts&&e==stops&&p==prepares&&f==finishes&&t==tasks);}
static void background_begin(void){CHECK(ps_usb_request_background());CHECK(ps_usb_background()&&route==PS_CANVAS&&!starts&&!navigations);CHECK(tick());complete();CHECK(tick());CHECK(ps_usb_background()&&!pu_running&&engine.active);background_layout();}
static void test_background(void){
    fixture();route=PS_CANVAS;fail_prepare=true;CHECK(!ps_usb_request_background()&&!ps_usb_background()&&!bridge&&route==PS_CANVAS);
    fixture();route=PS_CANVAS;fail_task=true;CHECK(ps_usb_request_background());tick();tick();CHECK(!ps_usb_background()&&!bridge&&route==PS_CANVAS&&!navigations);
    fixture();route=PS_CANVAS;background_begin();CHECK(!ps_usb_request_start()&&!ps_usb_request_background());
    CHECK(!ps_usb_before_leave(PS_DEVICE)&&!pu_confirm&&pu_target==PS_ROUTE_COUNT);CHECK(!ps_usb_back()&&!pu_confirm);
    CHECK(ps_usb_before_leave(PS_SHELF)&&!pu_confirm&&pu_target==PS_ROUTE_COUNT);
    background_action(PU_END);CHECK(pu_confirm);CHECK(!ps_usb_before_leave(PS_BLUETOOTH)&&!pu_confirm);
    engine.connected=false;tick();CHECK(ps_usb_background()&&!stops);engine.host_ejected=true;engine.writing=true;tick();CHECK(!stops&&!pu_running);
    engine.writing=false;tick();CHECK(pu_running&&pu_target==PS_ROUTE_COUNT);background_layout();tick();complete();tick();CHECK(!ps_usb_background()&&!bridge&&route==PS_CANVAS&&!navigations);
    fixture();route=PS_CANVAS;background_begin();ps_event_t outside={.type=PS_TAP,.x=20,.y=40};CHECK(!ps_usb_event(&outside)&&!pu_confirm);
    background_action(PU_END);CHECK(pu_confirm);background_layout();background_action(PU_CANCEL);CHECK(!pu_confirm&&ps_usb_background());
    background_action(PU_END);background_action(PU_END);tick();fail_stop=true;complete();tick();CHECK(ps_usb_background()&&pu_stop_failed&&bridge&&!finishes);background_layout();
    fail_stop=false;background_action(PU_RETRY);tick();complete();tick();CHECK(!ps_usb_background()&&!bridge&&route==PS_CANVAS&&!navigations);
    fixture();route=PS_CANVAS;fail_start=partial_start=true;CHECK(ps_usb_request_background());tick();complete();tick();CHECK(ps_usb_background()&&bridge);background_layout();
    background_action(PU_END);background_action(PU_END);tick();complete();tick();CHECK(!ps_usb_background()&&!bridge&&route==PS_CANVAS);
}
static void test_visual_projection(void){
    fixture();route=PS_CANVAS;background_begin();CHECK(tick());CHECK(!tick());
    int before_starts=starts,before_stops=stops,before_finishes=finishes,before_prepares=prepares;
    // 后台提示没有字节计数或写入动画；电脑读取、轮询和写批次不能反复推相同底栏。
    // The retained footer has no byte counters or write animation; host reads/polls and write batches must not present identical content repeatedly.
    for(int i=0;i<40;i++){engine.read_blocks+=8;CHECK(!tick());}
    for(int i=0;i<40;i++){engine.written_blocks+=8;engine.writing=!engine.writing;CHECK(!tick());}
    CHECK(ps_usb_background()&&bridge&&engine.active&&starts==before_starts&&stops==before_stops&&finishes==before_finishes&&prepares==before_prepares);
    engine.connected=true;CHECK(tick());CHECK(!tick());engine.connected=false;CHECK(tick());CHECK(!tick());
    engine.state=READ_PICO_USB_ERROR;engine.last_error=ESP_FAIL;CHECK(tick());CHECK(!tick());engine.last_error=ESP_ERR_NO_MEM;CHECK(!tick());
    CHECK(ps_usb_background()&&bridge&&!pu_running&&stops==before_stops);
    background_action(PU_END);background_action(PU_END);CHECK(tick());complete();CHECK(tick());CHECK(!ps_usb_background()&&!bridge&&route==PS_CANVAS);
    fixture();begin();CHECK(tick());for(int i=0;i<12;i++){engine.read_blocks+=8;engine.writing=!engine.writing;CHECK(!tick());}
    engine.written_blocks++;CHECK(tick());engine.written_blocks++;CHECK(!tick());tick();tick();CHECK(tick());
}
int main(void){for(int land=0;land<2;land++){view=land?(ps_view_t){640,360,true}:(ps_view_t){360,640,false};test_flow();test_background();test_visual_projection();}printf("USB UI: %d checks, foreground/background lifecycle/ownership, failures/retry, host eject/disconnect, retained route and safe controls in both orientations; no USB/card access.\n",checks);return 0;}

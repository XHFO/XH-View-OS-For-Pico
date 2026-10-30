/*
 * SPDX-License-Identifier: Apache-2.0
 * TF 卡的电脑U盘交接页面；只有停止USB并重新验证卡后才恢复阅读。
 * TF-card USB-drive handoff UI; resume reading only after USB stops and the card verifies.
 * 用户修订：电脑后台交接不阻挡全局操作与纯内存设置；TF 文件仍独占，只有主动结束才确认弹出。
 * User revision: background handoff permits global controls and memory-only settings; TF stays exclusive and only explicit ending confirms eject.
 * 冻结：不暴露内部flash、不自动格式化、不并行WiFi传书；阶段来自真实任务状态，后台仅可见状态变化才刷新，块统计与写活动不触发重绘。
 * Frozen: never export internal flash, auto-format, or run WiFi transfer in parallel; use real task states and redraw the background only for visible changes, never block counts or write activity.
 */
#include "pico_system.h"
#include "read_pico_usb.h"
#include "read_pico_sd.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>

static read_pico_usb_status_t pu_status,pu_visual;
static bool pu_held,pu_running,pu_started,pu_stop,pu_confirm,pu_open_books,pu_visual_valid;
static bool pu_stop_failed,pu_eject_handled,pu_background;
enum { PU_START,PU_STORAGE,PU_CANCEL,PU_END,PU_RETRY };
typedef struct {int id,x,y,w,h;const char *label;bool primary;} pu_control_t;
static ps_route_t pu_target=PS_ROUTE_COUNT;
static atomic_bool pu_done;
static esp_err_t pu_result;
static char pu_message[128];
static int64_t pu_poll_at,pu_visual_at;

bool ps_usb_held(void){return pu_held;}
bool ps_usb_busy(void){return pu_running;}
bool ps_usb_background(void){return pu_background&&pu_held;}
bool ps_usb_local_route(ps_route_t r){return r==PS_DEVICE||r==PS_BLUETOOTH||r==PS_USB_TRANSFER||r==PS_CANVAS||r==PS_SOUND;}

void ps_usb_init(void){
    read_pico_usb_init();
    read_pico_usb_get_status(&pu_status);
}
void ps_usb_enter(void){
    pu_poll_at=0;pu_visual_valid=false;
    if(!pu_held&&!pu_running){pu_confirm=false;pu_message[0]=0;pu_target=PS_ROUTE_COUNT;pu_open_books=false;}
}
static void pu_worker(void *arg){
    (void)arg;
    pu_result=pu_stop?read_pico_usb_stop():read_pico_usb_start();
    atomic_store_explicit(&pu_done,true,memory_order_release);
    vTaskDelete(NULL);
}
static void pu_run(bool stop){
    pu_stop=stop;pu_running=true;pu_started=false;pu_confirm=false;
    atomic_store_explicit(&pu_done,false,memory_order_relaxed);
}
static bool pu_request_start(bool background){
    if(pu_held||pu_running||ps_storage_busy())return false;
    if(!ps_external_prepare()){
        snprintf(pu_message,sizeof(pu_message),"请先结束传书，并在存储页检查 TF 卡");return false;
    }
    pu_held=true;pu_background=background;pu_target=PS_ROUTE_COUNT;pu_open_books=false;pu_message[0]=0;
    pu_stop_failed=false;pu_eject_handled=false;pu_run(false);ps_dirty();return true;
}
bool ps_usb_request_start(void){return pu_request_start(false);}
bool ps_usb_request_background(void){return pu_request_start(true);}
static void pu_leave(void){
    ps_route_t target=pu_target;bool books=pu_open_books;
    pu_target=PS_ROUTE_COUNT;pu_open_books=false;pu_confirm=false;
    if(books)ps_navigate(PS_SHELF);
    else if(target!=PS_ROUTE_COUNT)ps_navigate(target);
}
bool ps_usb_before_leave(ps_route_t target){
    if(!pu_held||(!pu_background&&target==PS_USB_TRANSFER))return false;
    if(pu_background){
        pu_confirm=false;pu_target=PS_ROUTE_COUNT;pu_open_books=false;
        if(!pu_running&&ps_usb_local_route(target))return false;
        ps_toast("TF 卡正由电脑管理，请先安全结束传输");return true;
    }
    if(!pu_running){pu_target=pu_background?PS_ROUTE_COUNT:target;pu_open_books=false;pu_confirm=true;ps_dirty();}
    return true;
}
bool ps_usb_back(void){
    if(pu_running)return true;
    if(pu_confirm){pu_confirm=false;pu_target=PS_ROUTE_COUNT;return true;}
    if(pu_background)return false;
    if(pu_held){pu_confirm=true;pu_target=pu_background?PS_ROUTE_COUNT:PS_TRANSFER;return true;}
    return false;
}
bool ps_usb_tick(void){
    if(pu_running&&!pu_started){
        pu_started=true;
        if(xTaskCreate(pu_worker,"pico_usb_ui",6144,NULL,4,NULL)!=pdPASS){
            pu_result=ESP_ERR_NO_MEM;atomic_store_explicit(&pu_done,true,memory_order_release);
        }
        return true;
    }
    if(pu_running&&atomic_load_explicit(&pu_done,memory_order_acquire)){
        pu_running=false;read_pico_usb_get_status(&pu_status);
        read_pico_sd_info_t card={0};
        if(!pu_status.active)read_pico_sd_get_info(&card);
        if(!pu_status.active&&!card.exported){
            bool was_stop=pu_stop;
            bool restored=ps_external_finished(pu_status.written_blocks!=0);
            pu_held=false;pu_background=false;pu_stop_failed=false;
            ps_media_restored();
            snprintf(pu_message,sizeof(pu_message),"%s",pu_result==ESP_OK&&restored?
                "已安全退出，TF 卡读写检查通过":was_stop?
                "USB 已结束，请到存储页检查 TF 卡":"未能开启 USB，已恢复本机连接");
            if(pu_result==ESP_OK&&was_stop&&restored)pu_leave();
        }else if(pu_result!=ESP_OK||!pu_status.active){
            pu_stop_failed=pu_stop||!pu_status.active;pu_eject_handled=pu_status.host_ejected;
            snprintf(pu_message,sizeof(pu_message),"USB 交接未完成，请重试安全结束；本机文件仍保持暂停");
        }
        pu_visual_valid=false;return true;
    }
    if(pu_poll_at&&ps_now()-pu_poll_at<250)return false;
    pu_poll_at=ps_now();read_pico_usb_get_status(&pu_status);
    // 只有电脑明确发出弹出命令才自动接回卡；普通断线不等于写缓存已落盘。
    // Automatically reclaim only after a host eject command; disconnect alone does not attest host cache flush.
    if(!pu_status.host_ejected)pu_eject_handled=false;
    if(pu_held&&!pu_running&&pu_status.host_ejected&&!pu_status.writing&&!pu_eject_handled){
        pu_eject_handled=true;
        if(!pu_background&&pu_target==PS_ROUTE_COUNT&&!pu_open_books)pu_target=PS_TRANSFER;
        pu_run(true);return true;
    }
    // 仅比较画面实际显示的状态；后台不显示块计数或写活动，电脑轮询不能反复推相同提示。
    // Compare displayed state only; the background omits block counters/write activity, so host polling must not present identical hints.
    bool structural=!pu_visual_valid||pu_status.connected!=pu_visual.connected||
        (pu_status.state==READ_PICO_USB_ERROR)!=(pu_visual.state==READ_PICO_USB_ERROR);
    bool changed=structural||(!pu_background&&pu_status.written_blocks!=pu_visual.written_blocks);
    if(changed&&!structural&&ps_now()-pu_visual_at<1000)return false;
    if(changed){pu_visual=pu_status;pu_visual_valid=true;pu_visual_at=ps_now();}
    return changed&&(pu_background||ps_route()==PS_USB_TRANSFER);
}
static int pu_background_controls(pu_control_t out[2]){
    if(pu_running)return 0;
    ps_view_t v=ps_view();int y=v.h-48,w=(v.w-132)/2;
    if(pu_confirm){
        out[0]=(pu_control_t){PU_CANCEL,60,y,w,36,"继续传输",false};
        out[1]=(pu_control_t){PU_END,72+w,y,w,36,"已弹出，结束",true};return 2;
    }
    out[0]=(pu_control_t){pu_stop_failed?PU_RETRY:PU_END,v.w-192,y,132,36,
        pu_stop_failed?"重试安全结束":"安全结束",true};return 1;
}
void ps_usb_background_render(uint8_t *fb){
    ps_view_t v=ps_view();int y=v.h-92;
    ps_fill(fb,0,y,v.w,92,15);
    const char *state=pu_running?(pu_stop?"正在接回 TF 卡":"正在连接电脑"):
        pu_confirm?"请先在电脑安全弹出 TF 卡":pu_stop_failed||pu_status.state==READ_PICO_USB_ERROR?"USB 交接待重试 · 本机读写暂停":
        pu_status.connected?"USB 已连接 · 本机读写暂停":"USB 等待电脑 · 本机读写暂停";
    ps_text(fb,12,y+9,14,state,2);
    pu_control_t controls[2];int n=pu_background_controls(controls);
    for(int i=0;i<n;i++){pu_control_t *c=&controls[i];ps_button(fb,c->x,c->y,c->w,c->h,c->label,c->primary);}
}
static int pu_controls(pu_control_t out[2]){
    if(pu_running)return 0;
    ps_view_t v=ps_view();int width=(v.w-52)/2;
    if(pu_confirm){
        int y=v.landscape?254:464;
        out[0]=(pu_control_t){PU_CANCEL,20,y,width,44,"继续传输",false};
        out[1]=(pu_control_t){PU_END,32+width,y,width,44,"已弹出，结束",true};return 2;
    }
    if(!pu_held){
        int y=v.landscape?270:492,w=v.landscape?width:v.w-40,h=v.landscape?38:44;
        out[0]=(pu_control_t){PU_START,20,y,w,h,"开启 USB 传输",true};
        out[1]=(pu_control_t){PU_STORAGE,v.landscape?32+w:20,v.landscape?270:548,w,v.landscape?38:36,"检查 TF 卡",false};return 2;
    }
    out[0]=(pu_control_t){pu_stop_failed?PU_RETRY:PU_END,20,v.landscape?270:518,v.w-40,v.landscape?38:44,
        pu_stop_failed?"重试安全结束":"安全结束 USB 传输",true};return 1;
}
void ps_usb_render(uint8_t *fb){
    ps_view_t v=ps_view();
    ps_heading(fb,"USB 传书",NULL);
    int x=v.landscape?294:20,w=v.landscape?326:v.w-40;
    if(pu_running){
        ps_icon(fb,v.w/2-25,98,50,PI_REFRESH,2);
        ps_center(fb,v.w/2,172,20,pu_stop?"正在安全接回 TF 卡":"正在连接电脑",2);
        ps_wrap(fb,24,218,v.w-48,14,v.landscape?2:4,
            pu_stop?"正在停止 USB，并重新挂载和检查卡片。请暂时不要拔卡。":
            "正在暂停设备读卡，将 TF 卡交给电脑。USB 端口会重新连接一次。",4);
        return;
    }
    if(pu_confirm){
        ps_text(fb,20,96,21,"电脑已弹出磁盘了吗？",2);
        ps_wrap(fb,20,144,v.w-40,15,v.landscape?3:5,
            "请先在电脑上弹出 TF 卡磁盘，再结束传输。电脑仍在写文件时直接结束可能损坏文件。",3);
    }else{
        ps_icon(fb,v.landscape?104:v.w/2-30,92,60,PI_IMPORT,2);
        if(!pu_held){
            if(v.landscape)ps_text(fb,x,94,20,"像 U 盘一样传文件",2);
            else ps_center(fb,v.w/2,172,22,"像 U 盘一样传文件",2);
            ps_wrap(fb,x,v.landscape?137:226,w,15,v.landscape?3:5,
                "用数据线连接电脑，开启后在电脑磁盘中拖入图书。图书放入 books，字体放入 fonts。",3);
            ps_wrap(fb,v.landscape?20:x,v.landscape?178:354,v.landscape?250:w,12,4,
                pu_message[0]?pu_message:"传输期间暂停阅读和无线传书。完成后先在电脑弹出磁盘，设备会重新检查卡片。",5);
        }else{
            const char *state=pu_stop_failed||pu_status.state==READ_PICO_USB_ERROR?"USB 需要检查":pu_status.connected?"电脑已连接":"等待电脑连接";
            if(v.landscape)ps_text(fb,x,94,22,state,2);else ps_center(fb,v.w/2,172,22,state,2);
            ps_wrap(fb,x,v.landscape?138:226,w,15,v.landscape?3:5,
                "打开电脑里的 TF 卡磁盘，将 TXT、EPUB 拖入 books 文件夹，TTF 字体拖入 fonts。",3);
            char amount[100];snprintf(amount,sizeof(amount),"电脑已写入 %.1f MB",pu_status.written_blocks*512.0/1048576.0);
            ps_text(fb,x,v.landscape?215:354,14,amount,3);
            ps_wrap(fb,v.landscape?20:x,v.landscape?178:404,v.landscape?250:w,12,v.landscape?4:3,
                pu_message[0]?pu_message:"先在电脑弹出磁盘，完成后设备会自动恢复。",5);
        }
    }
    pu_control_t controls[2];int count=pu_controls(controls);
    for(int i=0;i<count;i++){pu_control_t *c=&controls[i];ps_button(fb,c->x,c->y,c->w,c->h,c->label,c->primary);}
}
bool ps_usb_event(const ps_event_t *e){
    if(pu_running)return !pu_background;
    if(e->type!=PS_TAP)return false;
    pu_control_t controls[2];int count=pu_background&&ps_route()!=PS_USB_TRANSFER?pu_background_controls(controls):pu_controls(controls),id=-1;
    for(int i=0;i<count;i++)if(ps_hit(e->x,e->y,controls[i].x,controls[i].y,controls[i].w,controls[i].h)){id=controls[i].id;break;}
    if(id<0)return false;
    if(id==PU_START)ps_usb_request_start();
    else if(id==PU_STORAGE)ps_navigate(PS_STORAGE);
    else if(id==PU_CANCEL){pu_confirm=false;pu_target=PS_ROUTE_COUNT;pu_open_books=false;}
    else if(id==PU_RETRY||(id==PU_END&&pu_confirm))pu_run(true);
    else if(id==PU_END){pu_confirm=true;pu_target=PS_ROUTE_COUNT;pu_open_books=false;}
    return true;
}

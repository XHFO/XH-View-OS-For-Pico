/*
 * SPDX-License-Identifier: Apache-2.0
 * 手机式真实WiFi管理与同网/热点传书，统一使用产品逻辑坐标。
 * Phone-style real WiFi management and LAN/hotspot transfer in product coordinates.
 * 冻结：render仅画快照，扫描和连接属于网络worker；密码只提交给worker，不在页面保存。
 * 结束传书并等待HTTP汇合后才能恢复字体、进入书库或维护存储。
 * 用户修订：零记忆也能开启并扫描；扫描可自动开启WiFi，手动添加不以已开启为前提，连接成功后才记忆。
 * Frozen: render only paints snapshots; the network worker scans/connects. The page never retains passwords.
 * Stop and join HTTP before resuming fonts, opening the library or maintaining storage.
 * User revision: enable/scan without saved networks, let scanning enable WiFi, and allow manual entry while off; remember only successful connections.
 */
#include "pico_system.h"
#include "pico_i18n.h"
#include "pico_connectivity.h"
#include "read_pico_connectivity.h"
#include "read_pico_sd.h"
#include "ui_wifi_qr.h"
#include "ttf_font.h"
#include <stdio.h>
#include <string.h>
#include "pico_wireless.inc"


enum { CC_LIST,CC_DETAIL };
enum { CC_CONFIRM_NONE,CC_CONFIRM_FORGET,CC_CONFIRM_LEAVE };
enum { CC_LEAVE_BACK,CC_LEAVE_ROUTE,CC_LEAVE_BOOKS };
enum {
    CC_WIFI_TOGGLE,CC_NEARBY,CC_SAVED,CC_SCAN,CC_PREVIOUS,CC_NEXT,CC_OPEN_TRANSFER,
    CC_CONNECT,CC_PASSWORD,CC_FORGET,CC_AUTO_JOIN,CC_CONFIRM_YES,CC_CONFIRM_CANCEL,
    CC_TRANSFER_STA,CC_TRANSFER_AP,CC_TRANSFER_USB,CC_QR_JOIN,CC_QR_URL,CC_BOOKS,CC_TRANSFER_RESET,CC_STORAGE,CC_ADD,CC_SECURITY,
    CC_MANAGE_OPEN,CC_MANAGE_STOP,CC_MANAGE_WIFI,CC_LOCALE,CC_ROW=32
};
typedef struct {int x,y,w,h,id;char label[64];} cc_control_t;

static read_pico_connectivity_status_t cc_status;
static read_pico_connectivity_status_t cc_visual;
static read_pico_transfer_network_t cc_selected;
static ps_route_t cc_page=PS_ROUTE_COUNT,cc_destination;
static int cc_view,cc_tab,cc_scroll,cc_confirm,cc_leave_kind;
static bool cc_selected_saved,cc_qr_url,cc_qr_ready,cc_stopping,cc_session,cc_auth_known;
static bool cc_scanned_session,cc_scan_requested,cc_manual,cc_storage_problem,cc_stop_failed,cc_visual_valid;
static char cc_message[128],cc_qr_key[136];
static int64_t cc_poll_at,cc_visual_at,cc_scan_retry_at;
static unsigned cc_changed,cc_stop_seq,cc_scan_submission_failures;
static uint64_t cc_free;
static size_t cc_limit;
static bool cc_is_flash;
static read_pico_sd_info_t cc_card;
static read_pico_management_status_t cc_management;
static bool cc_manage_weread;

static bool cc_manage_sync(void){
    bool was_ready=cc_qr_ready;
    read_pico_management_status_t next;read_pico_management_get_status(&next);
    bool changed=next.active!=cc_management.active||strcmp(next.url,cc_management.url)||strcmp(next.error,cc_management.error)||
        (next.seconds_left+59)/60!=(cc_management.seconds_left+59)/60;
    if(next.active&&(strcmp(next.url,cc_management.url)||!cc_qr_ready)){
        char target[224];int bytes=snprintf(target,sizeof(target),"%s%s",next.url,cc_manage_weread?"&view=weread":"");
        cc_qr_ready=bytes>0&&(size_t)bytes<sizeof(target)&&ui_wifi_qr_prepare_url(target);
    }
    if(!next.active&&cc_management.active){ui_wifi_qr_clear();cc_qr_ready=false;}
    cc_management=next;return changed||was_ready!=cc_qr_ready;
}

static int cc_rows(void){return ps_view().landscape?3:5;}
static int cc_total(void){return (int)(cc_tab?cc_status.saved_count:cc_status.network_count);}
static int cc_saved_index(const char *ssid){
    for(size_t i=0;i<cc_status.saved_count;i++)if(!strcmp(cc_status.saved[i].ssid,ssid))return (int)i;
    return -1;
}
static const char *cc_signal(int rssi){return ps_tr(rssi>=-55?"settings.wifi.signal.strong":rssi>=-67?"settings.wifi.signal.good":rssi>=-78?"settings.wifi.signal.fair":"settings.wifi.signal.weak");}
static void cc_copy(char *out,size_t cap,const char *text){
    if(cap)snprintf(out,cap,"%s",text?text:"");
}
static void cc_fit(char *out,size_t cap,const char *text,int width,int size){
    cc_copy(out,cap,text);
    while(out[0]&&ttf_text_width_px((size*19+5)/10,out)>(width*19)/10){
        size_t n=strlen(out)-1;
        while(n&&((unsigned char)out[n]&0xc0)==0x80)n--;
        out[n]=0;
    }
}
static void cc_size(char *out,size_t cap,uint64_t bytes){
    if(bytes>=1073741824ULL)snprintf(out,cap,"%.1f GB",bytes/1073741824.0);
    else if(bytes>=1048576)snprintf(out,cap,"%.1f MB",bytes/1048576.0);
    else snprintf(out,cap,"%u KB",(unsigned)(bytes/1024));
}
static void cc_capacity_text(char *out,size_t cap){
    char amount[24];cc_size(amount,sizeof(amount),cc_free);
    snprintf(out,cap,ps_tr("settings.transfer.capacity"),ps_tr(cc_is_flash?"settings.transfer.capacity.flash":"settings.transfer.capacity.card"),amount,
             cc_is_flash?ps_tr("settings.transfer.capacity.limit"):"");
}
static bool cc_accept(esp_err_t result){
    if(result!=ESP_OK){cc_copy(cc_message,sizeof(cc_message),ps_tr("settings.wifi.toast.pending"));ps_toast(cc_message);return false;}
    cc_message[0]=0;
    read_pico_connectivity_get_status(&cc_status);
    cc_poll_at=0;
    return true;
}
static void cc_clear_qr(void){ui_wifi_qr_clear();cc_qr_ready=false;cc_qr_key[0]=0;}
static void cc_prepare_qr(void){
    const read_pico_transfer_status_t *t=&cc_status.transfer;
    if(!cc_status.transfer_active||!t->network_ready){if(cc_qr_ready||cc_qr_key[0])cc_clear_qr();return;}
    char key[136];
    bool url=t->mode==READ_PICO_TRANSFER_MODE_STA||cc_qr_url;
    snprintf(key,sizeof(key),"%c:%s",url?'U':'W',url?t->url:t->ssid);
    if(!strcmp(key,cc_qr_key))return;
    cc_copy(cc_qr_key,sizeof(cc_qr_key),key);
    cc_qr_ready=url?ui_wifi_qr_prepare_url(t->url):ui_wifi_qr_prepare(t->ssid,READ_PICO_TRANSFER_PASSWORD);
}
static bool cc_holds_transfer(void){return cc_status.transfer_active||(ps_transfer_prepared()&&cc_status.busy);}

static void cc_add(cc_control_t out[24],int *n,int id,int x,int y,int w,int h,const char *label){
    if(*n>=24)return;
    cc_control_t *c=&out[(*n)++];*c=(cc_control_t){.x=x,.y=y,.w=w,.h=h,.id=id};
    cc_fit(c->label,sizeof(c->label),label,w-16,20);
}

// 同一矩形驱动绘制与命中；底部52逻辑像素留给全局手势和返回。 / One rectangle drives rendering and hits; reserve the bottom 52 logical pixels for global gestures/back.
static int cc_controls(cc_control_t out[24]){
    ps_view_t v=ps_view();int n=0;
    if(cc_page==PS_WIRELESS_MANAGE){
        cc_add(out,&n,CC_MANAGE_WIFI,v.w-100,32,84,30,"Wi-Fi");
        int y=v.landscape?258:544,w=(v.w-44)/2;
        if(cc_management.active){
            cc_add(out,&n,CC_MANAGE_OPEN,16,y,w,40,ps_tr("settings.manage.refresh"));
            cc_add(out,&n,CC_MANAGE_STOP,28+w,y,w,40,ps_tr("settings.manage.stop"));
        }else cc_add(out,&n,cc_status.connected?CC_MANAGE_OPEN:CC_MANAGE_WIFI,16,y,v.w-32,40,ps_tr(cc_status.connected?"settings.manage.start":"settings.manage.connect"));
        return n;
    }
    if(cc_confirm){
        if(cc_stopping)return 0;
        int w=(v.w-44)/2,y=v.landscape?258:446;
        cc_add(out,&n,CC_CONFIRM_CANCEL,16,y,w,42,ps_tr("common.cancel"));
        cc_add(out,&n,CC_CONFIRM_YES,28+w,y,w,42,ps_tr(cc_confirm==CC_CONFIRM_FORGET?"settings.wifi.forget_short":cc_stop_failed?"dialog.transfer.retry":"dialog.transfer.stop"));
        return n;
    }
    if(cc_page==PS_WIFI){
        if(cc_view==CC_DETAIL){
            int x=v.landscape?334:16,y=v.landscape?98:262,w=v.landscape?290:v.w-32;
            bool connected=cc_status.connected&&!strcmp(cc_status.ssid,cc_selected.ssid);
            cc_add(out,&n,CC_CONNECT,x,y,w,42,ps_tr(connected?"settings.wifi.connected":"settings.wifi.connect"));
            if(cc_selected.requires_password)cc_add(out,&n,CC_PASSWORD,x,y+52,w,42,ps_tr("settings.wifi.password"));
            if(cc_selected_saved){
                int at=cc_saved_index(cc_selected.ssid);bool automatic=at>=0&&cc_status.saved[at].auto_join;
                cc_add(out,&n,CC_AUTO_JOIN,x,y+104,w,42,ps_tr(automatic?"settings.wifi.auto.on":"settings.wifi.auto.off"));
                cc_add(out,&n,CC_FORGET,x,y+156,w,42,ps_tr("settings.wifi.forget"));
            }
            if(cc_manual)cc_add(out,&n,CC_SECURITY,x,y+104,w,42,ps_tr(cc_selected.requires_password?"settings.wifi.security.pass":"settings.wifi.security.open"));
            return n;
        }
        cc_add(out,&n,CC_LOCALE,v.w-160,32,PS_I18N_CHIP_W,PS_I18N_CHIP_H,ps_tr("settings.locale.chip"));
        cc_add(out,&n,CC_WIFI_TOGGLE,v.w-88,32,72,30,ps_tr(cc_status.wifi_enabled?"settings.wifi.on":"settings.wifi.off"));
        int x=v.landscape?252:16,width=v.landscape?372:v.w-32,y=v.landscape?86:178;
        int tab=(width-108)/2;
        cc_add(out,&n,CC_NEARBY,x,y,tab,32,ps_tr("settings.wifi.nearby"));
        cc_add(out,&n,CC_SAVED,x+tab+4,y,tab,32,ps_tr("settings.wifi.saved"));
        cc_add(out,&n,CC_SCAN,x+width-100,y,100,32,ps_tr((cc_status.scanning||cc_scan_requested)?"settings.wifi.scanning":"settings.wifi.rescan"));
        int top=v.landscape?128:220,rh=v.landscape?44:54,pitch=v.landscape?48:58;
        int total=cc_total(),rows=cc_rows();
        for(int i=0;i<rows&&cc_scroll+i<total;i++)cc_add(out,&n,CC_ROW+i,x,top+i*pitch,width,rh,"");
        int bottom=v.landscape?276:512;
        cc_add(out,&n,CC_PREVIOUS,x,bottom,80,30,ps_tr("common.prev"));
        cc_add(out,&n,CC_NEXT,x+width-80,bottom,80,30,ps_tr("common.next"));
        cc_add(out,&n,CC_ADD,x+90,bottom,width-180,30,ps_tr("settings.wifi.add"));
        cc_add(out,&n,CC_OPEN_TRANSFER,16,v.landscape?238:548,v.landscape?220:v.w-32,40,ps_tr("settings.wifi.transfer"));
        return n;
    }
    if(cc_page==PS_TRANSFER){
        if(!cc_session&&!cc_status.transfer_active&&!ps_transfer_prepared()){
            int w=v.landscape?(v.w-56)/3:v.w-32;
            cc_add(out,&n,CC_TRANSFER_STA,16,v.landscape?140:166,w,v.landscape?70:58,ps_tr("settings.transfer.same_wifi"));
            cc_add(out,&n,CC_TRANSFER_AP,v.landscape?28+w:16,v.landscape?140:270,w,v.landscape?70:58,ps_tr("settings.transfer.hotspot"));
            cc_add(out,&n,CC_TRANSFER_USB,v.landscape?40+2*w:16,v.landscape?140:374,w,v.landscape?70:58,ps_tr("settings.transfer.usb"));
            if(v.landscape){
                cc_add(out,&n,CC_STORAGE,16,270,w,38,ps_tr("settings.transfer.storage"));
                cc_add(out,&n,CC_BOOKS,28+w,270,v.w-w-44,38,ps_tr("settings.transfer.books"));
            }else{
                cc_add(out,&n,CC_STORAGE,16,493,v.w-32,36,ps_tr("settings.transfer.storage"));
                cc_add(out,&n,CC_BOOKS,16,545,v.w-32,40,ps_tr("settings.transfer.books"));
            }
        }
        else{
            const read_pico_transfer_status_t *t=&cc_status.transfer;
            if(t->mode==READ_PICO_TRANSFER_MODE_AP&&t->network_ready){
                int x=v.landscape?268:16,y=v.landscape?176:352,w=v.landscape?172:(v.w-44)/2;
                cc_add(out,&n,CC_QR_JOIN,x,y,w,32,ps_tr("settings.transfer.join"));
                cc_add(out,&n,CC_QR_URL,x+w+12,y,w,32,ps_tr("settings.transfer.open_page"));
            }
            if(!cc_status.transfer_active&&!cc_status.busy){
                int x=v.landscape?268:16,y=v.landscape?276:548,w=v.landscape?172:(v.w-44)/2;
                cc_add(out,&n,CC_TRANSFER_RESET,x,y,w,v.landscape?32:40,ps_tr("settings.transfer.reselect"));
                cc_add(out,&n,CC_BOOKS,x+w+12,y,w,v.landscape?32:40,ps_tr("settings.transfer.open_shelf"));
            }
            else cc_add(out,&n,CC_BOOKS,v.landscape?268:16,v.landscape?276:548,v.landscape?356:v.w-32,v.landscape?32:40,ps_tr("settings.transfer.done_shelf"));
        }
    }
    return n;
}

static void cc_draw_signal(uint8_t *fb,int x,int y,int rssi){
    int bars=rssi>=-55?4:rssi>=-67?3:rssi>=-78?2:1;
    for(int i=0;i<4;i++)ps_fill(fb,x+i*5,y+15-i*4,3,5+i*4,i<bars?2:11);
}
static void cc_network_row(uint8_t *fb,const cc_control_t *c){
    int index=cc_scroll+c->id-CC_ROW;
    if(index<0||index>=cc_total())return;
    const char *ssid=cc_tab?cc_status.saved[index].ssid:cc_status.networks[index].ssid;
    int saved=cc_saved_index(ssid);
    bool connected=cc_status.connected&&!strcmp(ssid,cc_status.ssid);
    bool connecting=cc_status.connecting&&!strcmp(ssid,cc_status.ssid);
    const read_pico_transfer_network_t *network=cc_tab?NULL:&cc_status.networks[index];
    ps_box(fb,c->x,c->y,c->w,c->h,5,connected?3:10,false);
    ps_icon(fb,c->x+10,c->y+12,22,connected?PI_CHECK:PI_WIFI,2);
    char name[40],detail[96];cc_fit(name,sizeof(name),ssid,c->w-100,19);
    ps_text(fb,c->x+42,c->y+5,15,name,2);
    snprintf(detail,sizeof(detail),"%s%s%s",ps_tr(connected?"settings.wifi.row.connected":connecting?"settings.wifi.row.connecting":saved>=0?"settings.wifi.row.saved":network&&!network->supported?"settings.wifi.row.unsupported":network&&network->requires_password?"settings.wifi.row.secured":"settings.wifi.row.open"),
             network?ps_tr("settings.wifi.signal.sep"):"",network?cc_signal(network->rssi):"");
    ps_text(fb,c->x+42,c->y+27,10,detail,6);
    if(network)cc_draw_signal(fb,c->x+c->w-54,c->y+12,network->rssi);
    ps_text(fb,c->x+c->w-24,c->y+12,16,"›",5);
}

static void cc_render_confirm(uint8_t *fb){
    ps_view_t v=ps_view();
    ps_heading(fb,ps_tr(cc_confirm==CC_CONFIRM_FORGET?"dialog.wifi.forget.title":"dialog.transfer.stop.title"),NULL);
    int y=v.landscape?98:164,x=v.landscape?88:24;
    ps_icon(fb,v.landscape?24:v.w/2-22,y,44,cc_confirm==CC_CONFIRM_FORGET?PI_WIFI:PI_IMPORT,2);
    if(cc_stopping)ps_wrap(fb,x,v.landscape?104:y+68,v.w-x-24,19,2,ps_tr("dialog.transfer.stopping"),2);
    else if(cc_confirm==CC_CONFIRM_FORGET){
        ps_wrap(fb,x,v.landscape?104:y+68,v.w-x-24,18,2,cc_selected.ssid,2);
        ps_wrap(fb,24,v.landscape?172:y+128,v.w-48,13,2,ps_tr("dialog.wifi.forget.body"),5);
    }
    else ps_wrap(fb,x,v.landscape?104:y+68,v.w-x-24,15,3,ps_tr(cc_status.transfer.state==READ_PICO_TRANSFER_UPLOADING?
            "dialog.transfer.uploading":"dialog.transfer.stop_body"),5);
    if(cc_stop_failed)ps_wrap(fb,24,v.landscape?194:360,v.w-48,12,2,ps_tr("dialog.transfer.stop_failed"),3);
}

static const char *cc_scan_failure_message(void){
    switch(cc_status.scan_failure){
        case READ_PICO_CONNECTIVITY_SCAN_START:return ps_tr("settings.wifi.scan.start");
        case READ_PICO_CONNECTIVITY_SCAN_RESULT:return ps_tr("settings.wifi.scan.result");
        case READ_PICO_CONNECTIVITY_SCAN_TIMEOUT:return ps_tr("settings.wifi.scan.timeout");
        case READ_PICO_CONNECTIVITY_SCAN_CANCELLED:return ps_tr("settings.wifi.scan.cancelled");
        default:return ps_tr("settings.wifi.scan.incomplete");
    }
}
static const char *cc_nearby_empty_message(void){
    if(cc_status.scanning)return ps_tr("settings.wifi.scan.searching");
    if(cc_scan_requested)return ps_tr(cc_status.connecting?"settings.wifi.scan.after_connect":"settings.wifi.scan.preparing");
    if(!cc_status.wifi_enabled)return ps_tr("settings.wifi.scan.off_hint");
    if(cc_message[0])return cc_message;
    if(cc_status.scan_error!=ESP_OK)return cc_scan_failure_message();
    if(cc_status.scan_completed_seq)return ps_tr("settings.wifi.scan.empty");
    return ps_tr("settings.wifi.scan.never");
}

static void cc_render_wifi(uint8_t *fb){
    ps_view_t v=ps_view();
    if(cc_view==CC_DETAIL){
        ps_heading(fb,ps_tr("settings.wifi.detail"),NULL);
        int width=v.landscape?280:v.w-32;
        ps_icon(fb,24,91,30,PI_WIFI,2);
        ps_wrap(fb,64,91,width-48,19,2,cc_selected.ssid,2);
        char info[112];
        bool current=cc_status.connected&&!strcmp(cc_selected.ssid,cc_status.ssid);
        snprintf(info,sizeof(info),"%s · %s",ps_tr(current?"settings.wifi.detail.connected":cc_selected_saved?"settings.wifi.detail.saved":"settings.wifi.detail.unsaved"),ps_tr(!cc_auth_known?"settings.wifi.detail.known":cc_selected.requires_password?"settings.wifi.detail.secured":"settings.wifi.detail.open"));
        ps_text(fb,24,156,13,info,5);
        if(current){snprintf(info,sizeof(info),ps_tr("settings.wifi.ip_label"),cc_status.ip);ps_wrap(fb,24,188,width-8,13,2,info,5);}
        else if(!cc_selected.supported)ps_wrap(fb,24,192,width-8,13,2,ps_tr("settings.wifi.unsupported_body"),5);
        if(cc_message[0])ps_wrap(fb,24,v.landscape?220:496,width-8,12,2,cc_message,3);
        return;
    }
    ps_heading(fb,ps_tr("settings.wifi.title"),NULL);
    int w=v.landscape?220:v.w-32;
    ps_box(fb,16,82,w,v.landscape?136:80,6,10,false);
    ps_icon(fb,26,98,26,PI_WIFI,2);
    const char *state=ps_tr(!cc_status.ready?"settings.wifi.state.preparing":!cc_status.wifi_enabled?"settings.wifi.state.disabled":cc_status.connecting?"settings.wifi.state.connecting":cc_status.connected?"settings.wifi.state.connected":cc_status.scanning||cc_scan_requested?"settings.wifi.state.searching":cc_status.state==READ_PICO_CONNECTIVITY_ERROR?"settings.wifi.state.failed":"settings.wifi.state.idle");
    ps_text(fb,64,92,15,state,2);
    if(cc_status.connected||cc_status.connecting){
        char name[40];cc_fit(name,sizeof(name),cc_status.ssid,w-62,18);ps_text(fb,64,119,13,name,5);
        if(v.landscape&&cc_status.connected){char ip[64];snprintf(ip,sizeof(ip),ps_tr("settings.wifi.ip"),cc_status.ip);ps_text(fb,26,162,12,ip,6);}
    }
    else ps_wrap(fb,26,v.landscape?136:128,w-20,11,v.landscape?2:1,
                 ps_tr(cc_status.wifi_enabled?"settings.wifi.hint.on":"settings.wifi.hint.off"),6);
    int x=v.landscape?252:16,top=v.landscape?128:220,width=v.landscape?372:v.w-32;
    if(!cc_total())ps_wrap(fb,x+8,top+34,width-16,14,3,cc_tab?ps_tr("settings.wifi.saved_empty"):cc_nearby_empty_message(),6);
    char page[64];int total=cc_total();snprintf(page,sizeof(page),"%d / %d",total?cc_scroll/cc_rows()+1:1,total?(total+cc_rows()-1)/cc_rows():1);
    ps_center(fb,x+width/2,v.landscape?65:164,10,page,6);
    if(cc_message[0]&&v.landscape)ps_wrap(fb,18,286,218,11,1,cc_message,3);
}

static void cc_render_transfer(uint8_t *fb){
    ps_view_t v=ps_view();const read_pico_transfer_status_t *t=&cc_status.transfer;
    ps_heading(fb,ps_tr(!cc_session&&!ps_transfer_prepared()?"settings.transfer.hub":"settings.transfer.title"),NULL);
    if(!cc_session&&!cc_status.transfer_active&&!ps_transfer_prepared()){
        ps_box(fb,16,82,v.w-32,v.landscape?44:62,5,10,false);
        const char *card=ps_tr(!cc_card.present?"settings.transfer.card.missing":cc_card.exported?"settings.transfer.card.exported":
            cc_card.mounted?cc_card.read_verified&&cc_card.write_verified?"settings.transfer.card.verified":"settings.transfer.card.mounted":"settings.transfer.card.bad");
        ps_text(fb,28,90,14,card,2);
        if(!v.landscape){char line[80],amount[24];cc_size(amount,sizeof(amount),cc_card.free_bytes);
            snprintf(line,sizeof(line),"%s",cc_card.mounted?amount:ps_tr("settings.transfer.card.manage"));
            ps_text(fb,28,119,11,line,5);}
        int w=v.landscape?(v.w-56)/3:v.w-32;
        ps_wrap(fb,24,v.landscape?221:232,w-16,12,2,ps_tr(cc_status.connected?"settings.transfer.hint.lan_ready":"settings.transfer.hint.lan"),5);
        ps_wrap(fb,v.landscape?36+w:24,v.landscape?221:336,w-16,12,2,ps_tr("settings.transfer.hint.ap"),5);
        ps_wrap(fb,v.landscape?48+2*w:24,v.landscape?221:440,w-16,12,2,ps_tr("settings.transfer.hint.usb"),5);
        if(cc_storage_problem)ps_wrap(fb,24,v.landscape?251:475,v.w-48,11,1,cc_message,3);
        return;
    }
    const char *state=ps_tr(cc_status.busy?"settings.transfer.state.preparing":!cc_status.transfer_active&&cc_status.last_error!=ESP_OK?"settings.transfer.state.not_started":t->state==READ_PICO_TRANSFER_UPLOADING?"settings.transfer.state.receiving":
        t->state==READ_PICO_TRANSFER_READY?"settings.transfer.state.waiting":t->state==READ_PICO_TRANSFER_ERROR?"settings.transfer.state.incomplete":
        t->state==READ_PICO_TRANSFER_STARTING?"settings.transfer.state.connecting":"settings.transfer.state.stopped");
    int right=v.landscape?268:16,width=v.landscape?356:v.w-32;
    ps_box(fb,right,82,width,v.landscape?48:62,5,10,false);
    ps_text(fb,right+12,90,17,state,2);
    char line[192],name[40];cc_fit(name,sizeof(name),t->mode==READ_PICO_TRANSFER_MODE_AP?t->ssid:cc_status.ssid,width-24,17);
    snprintf(line,sizeof(line),"%s · %s",ps_tr(t->mode==READ_PICO_TRANSFER_MODE_AP?"settings.transfer.mode.ap":"settings.transfer.mode.sta"),name);
    ps_text(fb,right+12,v.landscape?113:119,10,line,6);
    int qx=v.landscape?26:94,qy=v.landscape?86:154,side=v.landscape?196:172;
    if(cc_qr_ready)ui_wifi_qr_draw(fb,ps_rect(qx,qy,side,side));
    else{
        ps_box(fb,qx,qy,side,side,6,11,false);ps_icon(fb,qx+side/2-20,qy+side/2-30,40,PI_WIFI,5);
        ps_center(fb,qx+side/2,qy+side/2+25,12,ps_tr(cc_status.busy||t->state==READ_PICO_TRANSFER_STARTING?"settings.transfer.qr.preparing":"settings.transfer.qr.pending"),6);
    }
    bool ap=t->mode==READ_PICO_TRANSFER_MODE_AP;
    ps_center(fb,qx+side/2,v.landscape?286:332,12,ps_tr(ap&&!cc_qr_url?"settings.transfer.qr.step":"settings.transfer.qr.browser"),5);
    if(ap){
        snprintf(line,sizeof(line),ps_tr("settings.transfer.password"),READ_PICO_TRANSFER_PASSWORD);
        ps_text(fb,right+4,v.landscape?214:396,13,line,3);
    }
    else ps_wrap(fb,right+4,v.landscape?176:374,width-8,13,v.landscape?1:2,ps_tr("settings.transfer.same_net"),5);
    cc_fit(line,sizeof(line),t->network_ready?t->url:cc_message[0]?cc_message:!cc_status.transfer_active&&!cc_status.busy?ps_tr("settings.transfer.url.reselect"):ps_tr("settings.transfer.url.pending"),width-8,19);
    ps_text(fb,right+4,v.landscape?144:426,15,line,t->network_ready?2:6);
    cc_capacity_text(line,sizeof(line));
    ps_text(fb,right+4,v.landscape?239:460,10,line,6);
    if(v.landscape){
        if(t->state==READ_PICO_TRANSFER_UPLOADING){
            unsigned percent=t->cur_total?(unsigned)((uint64_t)t->cur_bytes*100/t->cur_total):0;
            snprintf(line,sizeof(line),ps_tr("settings.transfer.progress.receiving"),percent>100?100:percent,t->done_count);
        }
        else snprintf(line,sizeof(line),ps_tr("settings.transfer.progress.files_retry"),t->done_count,t->last_error!=ESP_OK?ps_tr("settings.transfer.progress.retry"):"");
        ps_text(fb,right+4,256,10,line,3);
    }
    else{
        ps_box(fb,16,484,width,52,5,10,false);
        if(t->state==READ_PICO_TRANSFER_UPLOADING){
            cc_fit(line,sizeof(line),t->cur_name,width-24,17);ps_text(fb,28,489,13,line,2);
            char have[24],total[24];cc_size(have,sizeof(have),t->cur_bytes);cc_size(total,sizeof(total),t->cur_total);
            snprintf(line,sizeof(line),ps_tr("settings.transfer.progress.bytes"),have,total,t->done_count);ps_text(fb,28,514,10,line,5);
            int progress=t->cur_total?(int)((uint64_t)t->cur_bytes*(width-24)/t->cur_total):0;
            if(progress>width-24)progress=width-24;
            ps_fill(fb,28,531,width-24,3,11);if(progress>0)ps_fill(fb,28,531,progress,3,2);
        }
        else{snprintf(line,sizeof(line),ps_tr("settings.transfer.progress.done"),t->done_count);ps_text(fb,28,495,16,line,2);}
    }
}
static void cc_render_manage(uint8_t *fb){
    ps_view_t v=ps_view();ps_heading(fb,ps_tr(cc_manage_weread?"settings.manage.weread_title":"settings.manage.title"),NULL);
    int x=v.landscape?30:84,y=v.landscape?91:124,side=v.landscape?168:192;
    if(cc_management.active&&cc_qr_ready)ui_wifi_qr_draw(fb,ps_rect(x,y,side,side));
    else{ps_box(fb,x,y,side,side,8,12,true);ps_icon(fb,x+side/2-20,y+46,40,cc_status.connected?PI_LINK:PI_WIFI,5);ps_center(fb,x+side/2,y+112,12,ps_tr(cc_status.connected?"settings.manage.qr_on":"settings.manage.qr_off"),5);}
    int text_x=v.landscape?232:20,text_y=v.landscape?94:344,width=v.w-text_x-20;
    ps_wrap(fb,text_x,text_y,width,17,2,ps_tr(cc_manage_weread?"settings.manage.weread_head":cc_management.active?"settings.manage.active_head":"settings.manage.idle_head"),2);
    ps_wrap(fb,text_x,text_y+54,width,12,3,ps_tr(cc_manage_weread?"settings.manage.weread_body":cc_management.active?"settings.manage.active_body":"settings.manage.idle_body"),5);
    if(cc_management.active){char line[96];snprintf(line,sizeof(line),ps_tr("settings.manage.ttl"),(unsigned)((cc_management.seconds_left+59)/60));ps_text(fb,text_x,text_y+112,11,line,5);snprintf(line,sizeof(line),"http://%s/manage",cc_management.host);ps_wrap(fb,text_x,text_y+138,width,11,1,line,5);}
    else if(cc_management.error[0])ps_wrap(fb,text_x,text_y+112,width,11,2,cc_management.error,4);
}

void ps_connectivity_render(uint8_t *fb){
    if(cc_page==PS_WIRELESS_MANAGE){cc_render_manage(fb);}
    else if(cc_confirm)cc_render_confirm(fb);
    else if(cc_page==PS_WIFI)cc_render_wifi(fb);
    else cc_render_transfer(fb);
    cc_control_t controls[24];int n=cc_controls(controls);
    for(int i=0;i<n;i++){
        cc_control_t *c=&controls[i];
        if(c->id>=CC_ROW){cc_network_row(fb,c);continue;}
        bool on=(c->id==CC_WIFI_TOGGLE&&cc_status.wifi_enabled)||(c->id==CC_NEARBY&&!cc_tab)||
            (c->id==CC_SAVED&&cc_tab)||(c->id==CC_QR_JOIN&&!cc_qr_url)||(c->id==CC_QR_URL&&cc_qr_url)||
            c->id==CC_CONFIRM_YES||c->id==CC_TRANSFER_STA||c->id==CC_BOOKS;
        ps_button(fb,c->x,c->y,c->w,c->h,c->label,on);
    }
}

static void cc_release_finished(void){
    if(!ps_transfer_prepared()||cc_status.transfer_active||cc_status.busy)return;
    if(cc_status.transfer.changed_count>cc_changed)cc_changed=cc_status.transfer.changed_count;
    ps_transfer_finished(cc_changed);
}

static void cc_finish_leave(void){
    int kind=cc_leave_kind;ps_route_t target=cc_destination;
    cc_confirm=CC_CONFIRM_NONE;cc_stopping=false;cc_session=false;cc_stop_failed=false;
    cc_clear_qr();
    if(kind==CC_LEAVE_BOOKS)ps_navigate(PS_SHELF);
    else if(kind==CC_LEAVE_ROUTE)ps_navigate(target);
    else ps_back();
}

static void cc_leave_request(int kind,ps_route_t target){
    read_pico_connectivity_get_status(&cc_status);
    cc_leave_kind=kind;cc_destination=target;cc_stop_failed=false;
    if(cc_holds_transfer())cc_confirm=CC_CONFIRM_LEAVE;
    else{cc_release_finished();cc_finish_leave();}
}

static void cc_password_done(const char *password){
    // 命令复制密码后立即返回，页面不建立任何密码副本。 / The command copies the password before returning; the page keeps no password copy.
    if(!cc_selected.ssid[0])return;
    if(cc_accept(read_pico_connectivity_connect(cc_selected.ssid,password))){
        cc_view=CC_LIST;cc_tab=0;cc_scroll=0;
        ps_back();
    }
}

static void cc_password_editor(void){
    if(!cc_selected.supported){ps_toast(ps_tr("settings.wifi.toast.unsupported"));return;}
    ps_edit_password(ps_tr("settings.wifi.editor.password"),64,cc_password_done);
}
static void cc_name_done(const char *ssid){
    size_t bytes=ssid?strlen(ssid):0;
    if(!bytes||bytes>32){ps_toast(ps_tr("settings.wifi.toast.ssid"));return;}
    memset(&cc_selected,0,sizeof(cc_selected));
    cc_copy(cc_selected.ssid,sizeof(cc_selected.ssid),ssid);
    cc_selected.supported=cc_selected.requires_password=true;
    cc_auth_known=cc_manual=true;cc_selected_saved=false;cc_message[0]=0;cc_view=CC_DETAIL;
    // 先返回网络详情再选择安全方式，避免在同一个编辑回调内嵌套另一个编辑器。
    // Return to network details before choosing security rather than nesting another editor in this callback.
    ps_back();
}
static void cc_scan_submit_failed(bool enabling){
    cc_scan_requested=true;cc_scan_retry_at=ps_now()+500;
    if(++cc_scan_submission_failures<3)return;
    cc_scan_requested=false;cc_scanned_session=true;cc_visual_valid=false;
    cc_copy(cc_message,sizeof(cc_message),ps_tr(enabling?"settings.wifi.toast.enable_fail":"settings.wifi.toast.scan_fail"));
    ps_toast(cc_message);
}
static void cc_scan_if_ready(void){
    if(ps_route()!=PS_WIFI||!cc_status.ready||cc_status.suspended||cc_status.transfer_active||
       cc_status.busy||cc_status.connecting||cc_status.scanning||ps_now()<cc_scan_retry_at)return;
    if(!cc_scan_requested&&(cc_status.network_count||cc_scanned_session))return;
    if(!cc_status.wifi_enabled){
        if(!cc_scan_requested)return;
        esp_err_t err=read_pico_connectivity_set_enabled(true);
        if(err==ESP_OK){cc_scan_submission_failures=0;read_pico_connectivity_get_status(&cc_status);cc_poll_at=0;cc_scan_retry_at=ps_now()+500;}
        else cc_scan_submit_failed(true);
        return;
    }
    esp_err_t err=read_pico_connectivity_scan();
    if(err==ESP_OK){
        cc_scanned_session=true;cc_scan_requested=false;cc_message[0]=0;cc_scan_submission_failures=0;
        read_pico_connectivity_get_status(&cc_status);cc_poll_at=0;
    }else{
        // 队列忙时保留一次意图；不把命令接受当扫描结果。
        // Retain one intent when the queue is busy; command acceptance is not a scan result.
        cc_scan_submit_failed(false);
    }
}

bool ps_connectivity_request_scan(void){
    if(ps_route()!=PS_WIFI||ps_usb_held())return false;
    read_pico_connectivity_get_status(&cc_status);
    if(cc_status.suspended||cc_status.transfer_active)return false;
    cc_scan_requested=true;cc_scanned_session=false;cc_scan_retry_at=0;cc_scan_submission_failures=0;cc_scan_submission_failures=0;cc_message[0]=0;
    cc_scan_if_ready();cc_visual_valid=false;
    return true;
}

static void cc_select_row(int row,bool details){
    int index=cc_scroll+row;
    if(index<0||index>=cc_total())return;
    memset(&cc_selected,0,sizeof(cc_selected));
    cc_manual=false;
    cc_auth_known=!cc_tab;
    if(cc_tab){
        cc_copy(cc_selected.ssid,sizeof(cc_selected.ssid),cc_status.saved[index].ssid);
        cc_selected.requires_password=true;cc_selected.supported=true;
        for(size_t i=0;i<cc_status.network_count;i++)if(!strcmp(cc_status.networks[i].ssid,cc_selected.ssid)){
            cc_selected=cc_status.networks[i];cc_auth_known=true;break;
        }
    }
    else cc_selected=cc_status.networks[index];
    cc_selected_saved=cc_saved_index(cc_selected.ssid)>=0;cc_message[0]=0;
    if(details||(cc_status.connected&&!strcmp(cc_status.ssid,cc_selected.ssid))||!cc_selected.supported){cc_view=CC_DETAIL;return;}
    if(cc_selected_saved)cc_accept(read_pico_connectivity_connect_saved(cc_selected.ssid));
    else if(cc_selected.requires_password)cc_password_editor();
    else cc_accept(read_pico_connectivity_connect(cc_selected.ssid,""));
}

static void cc_start_transfer(read_pico_transfer_mode_t mode){
    if(cc_status.busy){ps_toast(ps_tr("settings.wifi.toast.busy"));return;}
    if(mode==READ_PICO_TRANSFER_MODE_STA&&!cc_status.connected){
        ps_toast(ps_tr("settings.wifi.toast.need_lan"));ps_navigate(PS_WIFI);return;
    }
    read_pico_transfer_cfg_t cfg={0};
    esp_err_t err=ps_transfer_prepare(&cfg);
    if(err!=ESP_OK){cc_storage_problem=true;cc_copy(cc_message,sizeof(cc_message),ps_tr("settings.wifi.toast.storage"));ps_toast(cc_message);return;}
    cc_storage_problem=false;
    cfg.mode=mode;
    ps_transfer_capacity(&cc_free,&cc_is_flash,&cc_limit);
    cc_changed=0;cc_qr_url=mode==READ_PICO_TRANSFER_MODE_STA;cc_clear_qr();
    if(cc_accept(read_pico_connectivity_start_transfer(&cfg))){cc_session=true;cc_status.transfer.mode=mode;}
    else cc_release_finished();
}
bool ps_connectivity_request_transfer(read_pico_transfer_mode_t mode){
    if(ps_route()!=PS_TRANSFER || ps_usb_held())return false;
    read_pico_connectivity_get_status(&cc_status);
    cc_start_transfer(mode);
    return cc_session;
}

static void cc_action(int id,bool details){
    if(id==CC_LOCALE){
        if(!ps_locale_set(ps_locale()==PS_LOCALE_EN?PS_LOCALE_ZH_HANS:PS_LOCALE_EN))ps_toast(ps_tr("settings.locale.save_failed"));
        else ps_toast(ps_tr("settings.locale.saved"));
        return;
    }
    if(id==CC_MANAGE_WIFI){ps_navigate(PS_WIFI);return;}
    if(id==CC_MANAGE_OPEN){read_pico_management_start();cc_manage_sync();return;}
    if(id==CC_MANAGE_STOP){read_pico_management_stop();cc_manage_sync();return;}
    if(cc_confirm){
        if(cc_stopping)return;
        if(id==CC_CONFIRM_CANCEL){cc_confirm=CC_CONFIRM_NONE;cc_stop_failed=false;return;}
        if(id!=CC_CONFIRM_YES)return;
        if(cc_confirm==CC_CONFIRM_FORGET){
            if(cc_accept(read_pico_connectivity_forget(cc_selected.ssid))){cc_confirm=CC_CONFIRM_NONE;cc_view=CC_LIST;}
        }
        else if(cc_accept(read_pico_connectivity_stop_transfer())){
            cc_stopping=true;cc_stop_failed=false;cc_stop_seq=cc_status.submitted_seq;cc_clear_qr();
        }
        return;
    }
    if(id==CC_PREVIOUS){cc_scroll-=cc_rows();if(cc_scroll<0)cc_scroll=0;return;}
    if(id==CC_NEXT){if(cc_scroll+cc_rows()<cc_total())cc_scroll+=cc_rows();return;}
    if(id==CC_NEARBY||id==CC_SAVED){cc_tab=id==CC_SAVED;cc_scroll=0;return;}
    if(id==CC_OPEN_TRANSFER){ps_navigate(PS_TRANSFER);return;}
    if(id==CC_ADD){cc_manual=false;ps_edit(ps_tr("settings.wifi.editor.ssid"),"",32,cc_name_done);return;}
    if(id==CC_SECURITY&&cc_manual){cc_selected.requires_password=!cc_selected.requires_password;return;}
    if(id==CC_TRANSFER_USB){ps_navigate(PS_USB_TRANSFER);return;}
    if(id==CC_STORAGE){ps_navigate(PS_STORAGE);return;}
    if(id==CC_QR_JOIN||id==CC_QR_URL){cc_qr_url=id==CC_QR_URL;cc_qr_key[0]=0;cc_prepare_qr();return;}
    if(id==CC_BOOKS){cc_leave_request(CC_LEAVE_BOOKS,PS_SHELF);return;}
    if(id==CC_TRANSFER_RESET){cc_release_finished();cc_session=false;cc_message[0]=0;cc_clear_qr();return;}
    if(id==CC_TRANSFER_STA||id==CC_TRANSFER_AP){cc_start_transfer(id==CC_TRANSFER_STA?READ_PICO_TRANSFER_MODE_STA:READ_PICO_TRANSFER_MODE_AP);return;}
    if(id>=CC_ROW){
        if(cc_status.busy){ps_toast(ps_tr("settings.wifi.toast.busy"));return;}
        cc_select_row(id-CC_ROW,details);return;
    }
    if(id==CC_SCAN){ps_connectivity_request_scan();return;}
    if(cc_status.busy){ps_toast(ps_tr("settings.wifi.toast.busy"));return;}
    if(id==CC_WIFI_TOGGLE){
        bool enable=!cc_status.wifi_enabled;
        cc_scanned_session=false;cc_scan_retry_at=0;
        if(cc_accept(read_pico_connectivity_set_enabled(enable)))cc_scan_requested=enable;
    }
    if(id==CC_PASSWORD)cc_password_editor();
    if(id==CC_CONNECT){
        if(cc_status.connected&&!strcmp(cc_status.ssid,cc_selected.ssid))return;
        if(cc_selected_saved)cc_accept(read_pico_connectivity_connect_saved(cc_selected.ssid));
        else if(cc_selected.requires_password)cc_password_editor();
        else cc_accept(read_pico_connectivity_connect(cc_selected.ssid,""));
    }
    if(id==CC_FORGET)cc_confirm=CC_CONFIRM_FORGET;
    if(id==CC_AUTO_JOIN){int at=cc_saved_index(cc_selected.ssid);if(at>=0)cc_accept(read_pico_connectivity_set_auto_join(cc_selected.ssid,!cc_status.saved[at].auto_join));}
}

bool ps_connectivity_event(const ps_event_t *event){
    if(event->type==PS_SWIPE_U||event->type==PS_SWIPE_L||event->type==PS_SWIPE_D||event->type==PS_SWIPE_R){
        if(cc_confirm||cc_page!=PS_WIFI||cc_view!=CC_LIST)return false;
        cc_action(event->type==PS_SWIPE_U||event->type==PS_SWIPE_L?CC_NEXT:CC_PREVIOUS,false);return true;
    }
    if(event->type!=PS_TAP)return false;
    cc_control_t controls[24];int count=cc_controls(controls);
    for(int i=0;i<count;i++){
        const cc_control_t *c=&controls[i];
        if(!ps_hit(event->x,event->y,c->x,c->y,c->w,c->h))continue;
        cc_action(c->id,c->id>=CC_ROW&&event->x>=c->x+c->w-34);return true;
    }
    return false;
}

bool ps_connectivity_back(void){
    if(cc_stopping)return true;
    if(cc_confirm){cc_confirm=CC_CONFIRM_NONE;cc_stop_failed=false;return true;}
    if(cc_page==PS_WIFI&&cc_view==CC_DETAIL){cc_view=CC_LIST;return true;}
    if(cc_page==PS_TRANSFER&&cc_holds_transfer()){
        cc_leave_kind=CC_LEAVE_BACK;cc_confirm=CC_CONFIRM_LEAVE;cc_stop_failed=false;return true;
    }
    cc_release_finished();
    return false;
}

bool ps_connectivity_before_leave(ps_route_t target){
    if(ps_route()!=PS_TRANSFER||target==PS_TRANSFER)return false;
    if(cc_stopping)return true;
    if(!cc_holds_transfer()){cc_release_finished();return false;}
    cc_leave_kind=CC_LEAVE_ROUTE;cc_destination=target;cc_confirm=CC_CONFIRM_LEAVE;cc_stop_failed=false;
    return true;
}

void ps_connectivity_init(void){
    ps_i18n_init();
    cc_copy(cc_message,sizeof(cc_message),read_pico_connectivity_init()==ESP_OK?"":ps_tr("settings.wifi.toast.unavailable"));
    read_pico_connectivity_get_status(&cc_status);
    cc_visual_valid=false;cc_scanned_session=cc_scan_requested=cc_manual=cc_manage_weread=false;cc_stop_failed=false;cc_poll_at=cc_visual_at=cc_scan_retry_at=0;cc_stop_seq=cc_scan_submission_failures=0;
}

bool ps_connectivity_open_weread_setup(void){
    cc_manage_weread=true;cc_qr_ready=false;
    ps_navigate(PS_WIRELESS_MANAGE);
    if(ps_route()==PS_WIRELESS_MANAGE)return true;
    cc_manage_weread=false;return false;
}

void ps_connectivity_enter(ps_route_t route){
    // 网络名称/密码编辑器是配置子页，保留二维码目标与网络详情。 / Network-name/password editors retain the setup QR scope and network details.
    if(route==PS_EDITOR)return;
    if(route!=PS_WIFI&&route!=PS_TRANSFER&&route!=PS_WIRELESS_MANAGE){cc_manage_weread=false;return;}
    if(route==PS_TRANSFER)cc_manage_weread=false;
    read_pico_connectivity_get_status(&cc_status);
    bool changed=cc_page!=route;
    cc_page=route;cc_poll_at=0;
    if(changed){cc_view=CC_LIST;cc_confirm=CC_CONFIRM_NONE;cc_message[0]=0;cc_scroll=0;cc_storage_problem=false;cc_visual_valid=false;}
    read_pico_sd_get_info(&cc_card);
    if(route==PS_TRANSFER&&changed&&!ps_transfer_prepared()){
        cc_session=cc_status.transfer_active;cc_changed=0;cc_qr_url=cc_status.transfer.mode==READ_PICO_TRANSFER_MODE_STA;
    }
    if(route==PS_WIRELESS_MANAGE){
        memset(&cc_management,0,sizeof(cc_management));cc_qr_ready=false;
        if(changed&&cc_status.connected&&!read_pico_management_active())read_pico_management_start();
        cc_manage_sync();
    }else cc_prepare_qr();
    if(route==PS_WIFI)cc_scan_if_ready();
}

static bool cc_same_visible(const read_pico_connectivity_status_t *a,const read_pico_connectivity_status_t *b,bool progress){
    if(a->ready!=b->ready||a->busy!=b->busy||a->wifi_enabled!=b->wifi_enabled||a->connected!=b->connected||
       a->connecting!=b->connecting||a->scanning!=b->scanning||a->transfer_active!=b->transfer_active||a->state!=b->state||
       a->last_error!=b->last_error||a->last_operation!=b->last_operation||a->scan_completed_seq!=b->scan_completed_seq||a->scan_error!=b->scan_error||a->scan_failure!=b->scan_failure||strcmp(a->ssid,b->ssid)||strcmp(a->ip,b->ip)||a->network_count!=b->network_count||a->saved_count!=b->saved_count)return false;
    for(size_t i=0;i<a->saved_count;i++)if(strcmp(a->saved[i].ssid,b->saved[i].ssid)||a->saved[i].auto_join!=b->saved[i].auto_join)return false;
    for(size_t i=0;i<a->network_count;i++){
        const read_pico_transfer_network_t *x=&a->networks[i],*y=&b->networks[i];
        if(strcmp(x->ssid,y->ssid)||x->supported!=y->supported||x->requires_password!=y->requires_password||strcmp(cc_signal(x->rssi),cc_signal(y->rssi)))return false;
    }
    const read_pico_transfer_status_t *x=&a->transfer,*y=&b->transfer;
    if(x->mode!=y->mode||x->state!=y->state||x->network_ready!=y->network_ready||strcmp(x->url,y->url)||strcmp(x->ssid,y->ssid)||
       x->done_count!=y->done_count||x->last_error!=y->last_error||strcmp(x->cur_name,y->cur_name))return false;
    return !progress||(x->cur_bytes==y->cur_bytes&&x->cur_total==y->cur_total);
}

bool ps_connectivity_tick(void){
    ps_connectivity_wireless_tick();
    bool manage_changed=ps_route()==PS_WIRELESS_MANAGE&&cc_manage_sync();
    if(cc_poll_at&&ps_now()-cc_poll_at<500)return manage_changed;
    cc_poll_at=ps_now();
    read_pico_connectivity_status_t next;read_pico_connectivity_get_status(&next);
    unsigned old_done=cc_status.transfer.done_count;
    uint32_t old_result=cc_status.result_seq,old_scan=cc_status.scan_completed_seq;
    bool was_enabled=cc_status.wifi_enabled;
    cc_status=next;
    read_pico_sd_info_t before_card=cc_card;
    read_pico_sd_get_info(&cc_card);
    if(cc_status.wifi_enabled&&!was_enabled)cc_scanned_session=false;
    if(cc_status.transfer.changed_count>cc_changed)cc_changed=cc_status.transfer.changed_count;
    // 上传中不与接收任务争用存储；仅在等待状态且文件计数改变后刷新容量。 / Do not compete with uploads for storage; refresh capacity only when idle after the file count changes.
    if(ps_transfer_prepared()&&cc_status.transfer.state==READ_PICO_TRANSFER_READY&&!cc_status.busy&&cc_status.transfer.done_count!=old_done)
        ps_transfer_capacity(&cc_free,&cc_is_flash,&cc_limit);
    cc_release_finished();
    if(cc_stopping&&!cc_status.transfer_active&&!cc_status.busy){cc_finish_leave();return true;}
    if(cc_stopping&&!cc_status.busy&&cc_status.transfer_active&&cc_status.completed_seq>=cc_stop_seq){
        cc_stopping=false;cc_stop_failed=true;cc_visual_valid=false;
    }
    if(cc_status.scan_completed_seq!=old_scan){
        cc_scanned_session=true;
        if(cc_status.scan_error==ESP_OK)cc_message[0]=0;
    }
    if(cc_status.result_seq!=old_result&&ps_route()==PS_WIFI){
        if(cc_status.last_error!=ESP_OK){
            const char *message=ps_tr("settings.wifi.toast.generic");
            switch(cc_status.last_operation){
                case READ_PICO_CONNECTIVITY_OP_SCAN:message=cc_scan_failure_message();break;
                case READ_PICO_CONNECTIVITY_OP_WIFI:
                    cc_scan_requested=false;cc_scanned_session=true;
                    message=ps_tr("settings.wifi.toast.wifi_on");break;
                case READ_PICO_CONNECTIVITY_OP_CONNECT:message=ps_tr("settings.wifi.toast.connect");break;
                case READ_PICO_CONNECTIVITY_OP_PROFILE:message=ps_tr(cc_status.connected?"settings.wifi.toast.profile_saved":"settings.wifi.toast.profile");break;
                case READ_PICO_CONNECTIVITY_OP_TRANSFER:message=ps_tr("settings.wifi.toast.transfer");break;
                default:break;
            }
            cc_copy(cc_message,sizeof(cc_message),message);ps_toast(cc_message);
        }else if(cc_status.last_operation!=READ_PICO_CONNECTIVITY_OP_SCAN||cc_status.scan_error==ESP_OK)cc_message[0]=0;
    }
    if(ps_route()!=PS_WIRELESS_MANAGE)cc_prepare_qr();
    if(cc_total()&&cc_scroll>=cc_total())cc_scroll=((cc_total()-1)/cc_rows())*cc_rows();
    if(!cc_total())cc_scroll=0;
    cc_scan_if_ready();
    bool changed=!cc_visual_valid||!cc_same_visible(&cc_status,&cc_visual,true)||memcmp(&before_card,&cc_card,sizeof(cc_card));
    bool only_progress=cc_visual_valid&&cc_same_visible(&cc_status,&cc_visual,false);
    if(changed&&only_progress&&cc_status.transfer.state==READ_PICO_TRANSFER_UPLOADING&&ps_now()-cc_visual_at<1000)return false;
    if(changed){cc_visual=cc_status;cc_visual_valid=true;cc_visual_at=ps_now();}
    return manage_changed||(changed&&(ps_route()==PS_WIFI||ps_route()==PS_TRANSFER||ps_route()==PS_WIRELESS_MANAGE));
}

bool ps_connectivity_shutdown(void){
    if(read_pico_connectivity_quiesce(15000)!=ESP_OK)return false;
    read_pico_connectivity_get_status(&cc_status);
    if(cc_status.transfer_active||cc_status.busy)return false;
    cc_release_finished();cc_confirm=CC_CONFIRM_NONE;cc_stopping=false;cc_session=false;cc_stop_failed=false;cc_clear_qr();
    return true;
}

void ps_connectivity_media_lost(void){
    // 壳在此通知前已等待shutdown成功；不在失卡后读容量或尝试切换目录。 / The shell has already joined shutdown; never read capacity or switch directories after media loss.
    cc_free=0;cc_session=false;cc_confirm=CC_CONFIRM_NONE;cc_stopping=false;cc_stop_failed=false;cc_clear_qr();
    cc_copy(cc_message,sizeof(cc_message),ps_tr("settings.wifi.toast.card_gone"));
}

const char *ps_network_label(void){return cc_status.connected&&cc_status.ssid[0]?cc_status.ssid:ps_tr(cc_status.wifi_enabled?"settings.wifi.label.idle":"settings.wifi.label.off");}
int ps_wifi_count(void){return (int)cc_status.saved_count;}
const char *ps_wifi_name(int index){return index>=0&&(size_t)index<cc_status.saved_count?cc_status.saved[index].ssid:"";}
int ps_wifi_connected_index(void){return cc_status.connected?cc_saved_index(cc_status.ssid):-1;}

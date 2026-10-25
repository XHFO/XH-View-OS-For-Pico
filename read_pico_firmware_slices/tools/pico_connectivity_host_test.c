/* SPDX-License-Identifier: Apache-2.0
 * 真实连接页面的状态与逻辑坐标检查，网络/存储仅为替身。 / Actual connection-page state/geometry checks with network/storage doubles.
 */
#include "pico_system.h"
#include "pico_connectivity.h"
#include "read_pico_connectivity.h"
#include "read_pico_management.h"
#include "read_pico_sd.h"
#include "ui_wifi_qr.h"
#include "ttf_font.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

const app_desc_t app_book={0};
static ps_view_t view={360,640,false};
static ps_route_t route=PS_WIFI,history[32];
static int depth;
static int64_t now_ms=1000;
static read_pico_connectivity_status_t engine;
static read_pico_sd_info_t sd_card;
static bool usb_held;
static read_pico_management_status_t management;
static unsigned management_starts,management_stops;
bool ps_storage_busy(void){return false;}
bool ps_content_held(void){return false;}
void read_pico_management_get_status(read_pico_management_status_t *out){*out=management;}
bool read_pico_management_active(void){return management.active;}
bool read_pico_management_start(void){if(!engine.connected||!engine.ip[0])return false;management_starts++;management.active=true;management.seconds_left=900;snprintf(management.host,sizeof(management.host),"%s",engine.ip);snprintf(management.url,sizeof(management.url),"http://%s/manage#key=%032x",engine.ip,management_starts);return true;}
void read_pico_management_stop(void){management_stops++;memset(&management,0,sizeof(management));}
void read_pico_management_tick(void){if(management.active&&!engine.connected)read_pico_management_stop();}
static int card_reads;
static int commands,reads,capacity_reads,prepares,finished,books,qr_prepares,qr_clears,qr_draws,draws,checks;
static int job,fail_command,fail_prepare,fail_quiesce,fail_stop;
static bool prepared;
static unsigned finished_changes;
static char last_toast[160],command_ssid[33],command_secret[65];
static bool command_flag;
static read_pico_transfer_mode_t command_mode;
static void (*password_done)(const char*);
static void (*name_done)(const char*);
enum { JOB_NONE,JOB_ENABLED,JOB_SCAN,JOB_CONNECT,JOB_FORGET,JOB_AUTOMATIC,JOB_START,JOB_STOP };
#define CHECK(c) do{checks++;if(!(c)){fprintf(stderr,"Connectivity check failed: %s at line %d\n",#c,__LINE__);abort();}}while(0)
bool ps_usb_held(void){return usb_held;}
esp_err_t read_pico_sd_get_info(read_pico_sd_info_t *out){card_reads++;*out=sd_card;return sd_card.error;}
ps_view_t ps_view(void){return view;}
ps_route_t ps_route(void){return route;}
int64_t ps_now(void){return now_ms;}
void ps_toast(const char *s){snprintf(last_toast,sizeof(last_toast),"%s",s);}
void ps_dirty(void){}
void ps_navigate(ps_route_t next){if(ps_connectivity_before_leave(next))return;if(next!=route){CHECK(depth<32);history[depth++]=route;route=next;}ps_connectivity_enter(next);}
void ps_back(void){route=depth?history[--depth]:PS_WIFI;ps_connectivity_enter(route);}
void ps_legacy(const app_desc_t *app){CHECK(app==&app_book&&!engine.transfer_active&&!engine.busy&&!prepared);books++;}
void ps_edit_password(const char *title,size_t limit,void (*done)(const char*)){CHECK(title&&limit==64);password_done=done;ps_navigate(PS_EDITOR);}
void ps_edit(const char *title,const char *initial,size_t limit,void (*done)(const char*)){CHECK(title&&initial&&!initial[0]&&limit==32);name_done=done;ps_navigate(PS_EDITOR);}
esp_err_t ps_transfer_prepare(read_pico_transfer_cfg_t *cfg){prepares++;if(fail_prepare)return ESP_FAIL;CHECK(!prepared);prepared=true;memset(cfg,0,sizeof(*cfg));cfg->root_dir="/test/books";return ESP_OK;}
void ps_transfer_finished(unsigned changed){CHECK(prepared&&!engine.transfer_active&&!engine.busy);finished++;finished_changes=changed;prepared=false;}
bool ps_transfer_prepared(void){return prepared;}
void ps_transfer_capacity(uint64_t *free_bytes,bool *flash,size_t *limit){CHECK(engine.transfer.state!=READ_PICO_TRANSFER_UPLOADING);capacity_reads++;*free_bytes=4200000000ULL;*flash=false;*limit=SIZE_MAX;}
esp_err_t read_pico_connectivity_init(void){engine.ready=true;return ESP_OK;}
void read_pico_connectivity_get_status(read_pico_connectivity_status_t *out){reads++;*out=engine;}
static esp_err_t queue(int kind){commands++;if(fail_command)return ESP_FAIL;CHECK(!engine.busy);job=kind;engine.busy=true;engine.submitted_seq++;return ESP_OK;}
esp_err_t read_pico_connectivity_set_enabled(bool enabled){command_flag=enabled;return queue(JOB_ENABLED);}
esp_err_t read_pico_connectivity_scan(void){esp_err_t e=queue(JOB_SCAN);if(e==ESP_OK)engine.scanning=true;return e;}
esp_err_t read_pico_connectivity_connect(const char *ssid,const char *password){snprintf(command_ssid,sizeof(command_ssid),"%s",ssid);snprintf(command_secret,sizeof(command_secret),"%s",password);return queue(JOB_CONNECT);}
esp_err_t read_pico_connectivity_connect_saved(const char *ssid){return read_pico_connectivity_connect(ssid,"");}
esp_err_t read_pico_connectivity_forget(const char *ssid){snprintf(command_ssid,sizeof(command_ssid),"%s",ssid);return queue(JOB_FORGET);}
esp_err_t read_pico_connectivity_set_auto_join(const char *ssid,bool enabled){snprintf(command_ssid,sizeof(command_ssid),"%s",ssid);command_flag=enabled;return queue(JOB_AUTOMATIC);}
esp_err_t read_pico_connectivity_start_transfer(const read_pico_transfer_cfg_t *cfg){CHECK(prepared&&cfg->root_dir);command_mode=cfg->mode;return queue(JOB_START);}
esp_err_t read_pico_connectivity_stop_transfer(void){return queue(JOB_STOP);}
esp_err_t read_pico_connectivity_quiesce(uint32_t timeout){CHECK(timeout==15000);commands++;if(fail_quiesce)return ESP_FAIL;job=JOB_NONE;engine.transfer_active=engine.busy=engine.scanning=false;engine.suspended=true;engine.transfer.network_ready=false;return ESP_OK;}
esp_err_t read_pico_connectivity_resume(void){engine.suspended=false;return ESP_OK;}
#ifndef CONNECTIVITY_REAL_QR
bool ui_wifi_qr_prepare(const char *ssid,const char *password){CHECK(ssid[0]&&password[0]);qr_prepares++;return true;}
bool ui_wifi_qr_prepare_url(const char *url){CHECK(!strncmp(url,"http://",7));qr_prepares++;return true;}
void ui_wifi_qr_clear(void){qr_clears++;}
#endif

#ifndef CONNECTIVITY_REAL_RENDER
static void rect(int x,int y,int w,int h){CHECK(x>=0&&y>=0&&w>=0&&h>=0&&x+w<=view.w&&y+h<=view.h);draws++;}
int ttf_text_width_px(int size,const char *text){int width=0;for(;*text;text++)if(((unsigned char)*text&0xc0)!=0x80)width+=(unsigned char)*text<128?size/2:size;return width;}
EpdRect ps_rect(int x,int y,int w,int h){return (EpdRect){x*19/10,y*19/10,w*19/10,h*19/10};}
bool ps_hit(int x,int y,int rx,int ry,int rw,int rh){return x>=rx&&y>=ry&&x<rx+rw&&y<ry+rh;}
void ps_fill(uint8_t *fb,int x,int y,int w,int h,int gray){(void)fb;(void)gray;rect(x,y,w,h);}
void ps_line(uint8_t *fb,int x,int y,int x1,int y1,int gray){(void)fb;(void)gray;CHECK(x>=0&&x<=view.w&&x1>=0&&x1<=view.w&&y>=0&&y<=view.h&&y1>=0&&y1<=view.h);draws++;}
void ps_box(uint8_t *fb,int x,int y,int w,int h,int radius,int gray,bool fill){(void)fb;(void)radius;(void)gray;(void)fill;rect(x,y,w,h);}
void ps_text(uint8_t *fb,int x,int y,int size,const char *text,int gray){(void)fb;(void)gray;CHECK(!strstr(text,"dBm"));rect(x,y,0,size);}
void ps_center(uint8_t *fb,int x,int y,int size,const char *text,int gray){ps_text(fb,x,y,size,text,gray);}
void ps_wrap(uint8_t *fb,int x,int y,int w,int size,int lines,const char *text,int gray){(void)w;(void)lines;ps_text(fb,x,y,size,text,gray);}
void ps_icon(uint8_t *fb,int x,int y,int size,ps_icon_t icon,int gray){(void)fb;(void)icon;(void)gray;rect(x,y,size,size);}
void ps_button(uint8_t *fb,int x,int y,int w,int h,const char *label,bool on){(void)fb;(void)on;CHECK(ttf_text_width_px(38,label)<=w*19/10);rect(x,y,w,h);}
void ps_heading(uint8_t *fb,const char *title,const char *subtitle){(void)fb;(void)title;(void)subtitle;draws++;}
#endif
#ifndef CONNECTIVITY_REAL_QR
void ui_wifi_qr_draw(uint8_t *fb,EpdRect area){(void)fb;CHECK(area.x>=0&&area.y>=0&&area.x+area.width<=view.w*19/10&&area.y+area.height<=view.h*19/10);qr_draws++;}
#endif

#include "../main/system/pico_connectivity.c"

static void fixture(void){
    memset(&management,0,sizeof(management));cw_state=CW_NONE;cw_operation=0;cw_poll_at=0;memset(cw_pair_url,0,sizeof(cw_pair_url));
    memset(&engine,0,sizeof(engine));engine.ready=true;engine.wifi_enabled=true;engine.state=READ_PICO_CONNECTIVITY_IDLE;
    engine.network_count=16;engine.saved_count=8;
    for(int i=0;i<16;i++){
        read_pico_transfer_network_t *n=&engine.networks[i];snprintf(n->ssid,sizeof(n->ssid),"Home-%02d",i);n->supported=true;n->requires_password=i%2;n->rssi=(int8_t)(-40-i*3);
    }
    strcpy(engine.networks[8].ssid,"这是很长的中文网络名");strcpy(engine.networks[9].ssid,"ABCDEFGHIJKLMNOPQRSTUVWXYZ123456");
    for(int i=0;i<8;i++){strcpy(engine.saved[i].ssid,engine.networks[i].ssid);engine.saved[i].auto_join=true;}
    cc_page=PS_ROUTE_COUNT;cc_view=CC_LIST;cc_tab=cc_scroll=cc_confirm=0;cc_session=cc_stopping=cc_storage_problem=cc_stop_failed=false;
    cc_visual_valid=cc_scanned_session=cc_scan_requested=cc_manual=cc_qr_ready=false;cc_poll_at=cc_visual_at=cc_scan_retry_at=0;cc_message[0]=cc_qr_key[0]=0;cc_changed=cc_stop_seq=cc_scan_submission_failures=0;prepared=false;job=0;depth=0;route=PS_WIFI;password_done=name_done=NULL;
    commands=reads=capacity_reads=prepares=finished=books=qr_prepares=qr_clears=qr_draws=draws=card_reads=0;usb_held=false;memset(&sd_card,0,sizeof(sd_card));fail_command=fail_prepare=fail_quiesce=fail_stop=0;
    now_ms+=2000;ps_connectivity_enter(PS_WIFI);
}
static void complete_job(void){
    CHECK(engine.busy&&job);engine.completed_seq=engine.submitted_seq;engine.busy=false;engine.last_error=ESP_OK;
    if(job==JOB_ENABLED){engine.wifi_enabled=command_flag;engine.connected=engine.connecting=false;engine.state=command_flag?READ_PICO_CONNECTIVITY_IDLE:READ_PICO_CONNECTIVITY_DISABLED;}
    engine.result_seq++;
    engine.last_operation=job==JOB_ENABLED?READ_PICO_CONNECTIVITY_OP_WIFI:job==JOB_SCAN?READ_PICO_CONNECTIVITY_OP_SCAN:job==JOB_CONNECT?READ_PICO_CONNECTIVITY_OP_CONNECT:job==JOB_FORGET||job==JOB_AUTOMATIC?READ_PICO_CONNECTIVITY_OP_PROFILE:READ_PICO_CONNECTIVITY_OP_TRANSFER;
    if(job==JOB_SCAN){engine.scanning=false;engine.scan_error=ESP_OK;engine.scan_failure=READ_PICO_CONNECTIVITY_SCAN_NONE;engine.scan_completed_seq++;}
    if(job==JOB_CONNECT){engine.wifi_enabled=engine.connected=true;strcpy(engine.ssid,command_ssid);strcpy(engine.ip,"192.168.50.23");engine.state=READ_PICO_CONNECTIVITY_CONNECTED;memset(command_secret,0,sizeof(command_secret));}
    if(job==JOB_FORGET){for(size_t i=0;i<engine.saved_count;i++)if(!strcmp(engine.saved[i].ssid,command_ssid)){memmove(&engine.saved[i],&engine.saved[i+1],(engine.saved_count-i-1)*sizeof(engine.saved[0]));engine.saved_count--;break;}}
    if(job==JOB_AUTOMATIC){for(size_t i=0;i<engine.saved_count;i++)if(!strcmp(engine.saved[i].ssid,command_ssid))engine.saved[i].auto_join=command_flag;}
    if(job==JOB_START){engine.transfer_active=true;engine.transfer.mode=command_mode;engine.transfer.state=READ_PICO_TRANSFER_READY;engine.transfer.network_ready=true;strcpy(engine.transfer.url,"http://192.168.4.1");strcpy(engine.transfer.ssid,"Read-Pico-1234");}
    if(job==JOB_STOP){if(fail_stop){engine.last_error=ESP_FAIL;}else{engine.transfer_active=false;engine.transfer.network_ready=false;engine.transfer.state=READ_PICO_TRANSFER_STOPPED;}}
    job=0;
}
static bool tick(void){now_ms+=501;return ps_connectivity_tick();}
static cc_control_t control(int id){cc_control_t c[24];int n=cc_controls(c);for(int i=0;i<n;i++)if(c[i].id==id)return c[i];CHECK(!"missing control");return (cc_control_t){0};}
static void action(int id){cc_control_t c=control(id);ps_event_t e={.type=PS_TAP,.x=c.x+c.w/2,.y=c.y+c.h/2};CHECK(ps_connectivity_event(&e));}
static void detail(int row){cc_control_t c=control(CC_ROW+row);ps_event_t e={.type=PS_TAP,.x=c.x+c.w-5,.y=c.y+c.h/2};CHECK(ps_connectivity_event(&e));CHECK(cc_view==CC_DETAIL);}
static void layout(void){
    cc_control_t c[24];int n=cc_controls(c);
    for(int i=0;i<n;i++){
        CHECK(c[i].x>=0&&c[i].y>=24&&c[i].w>0&&c[i].h>=30&&c[i].x+c[i].w<=view.w&&c[i].y+c[i].h<=view.h-52);
        for(int j=0;j<i;j++)CHECK(c[i].x>=c[j].x+c[j].w||c[j].x>=c[i].x+c[i].w||c[i].y>=c[j].y+c[j].h||c[j].y>=c[i].y+c[i].h);
    }
    int card_before=card_reads;
    int read_before=reads,commands_before=commands,capacity_before=capacity_reads,prepares_before=prepares,qr_before=qr_prepares,clear_before=qr_clears;
    ps_connectivity_render(NULL);
    CHECK(card_before==card_reads&&read_before==reads&&commands_before==commands&&capacity_before==capacity_reads&&prepares_before==prepares&&qr_before==qr_prepares&&clear_before==qr_clears);
}
static void test_wifi(void){
    fixture();layout();CHECK(ps_wifi_count()==8&&!strcmp(ps_wifi_name(0),"Home-00")&&ps_wifi_connected_index()==-1);
    for(int page=0;page<6;page++){layout();action(CC_NEXT);}action(CC_SAVED);layout();action(CC_NEXT);layout();action(CC_PREVIOUS);detail(0);layout();
    action(CC_FORGET);CHECK(!commands&&cc_confirm==CC_CONFIRM_FORGET);layout();action(CC_CONFIRM_CANCEL);CHECK(engine.saved_count==8);
    action(CC_AUTO_JOIN);CHECK(job==JOB_AUTOMATIC&&engine.busy);complete_job();tick();CHECK(!engine.saved[0].auto_join);
    action(CC_FORGET);action(CC_CONFIRM_YES);CHECK(job==JOB_FORGET);complete_job();tick();CHECK(engine.saved_count==7&&cc_view==CC_LIST);
    action(CC_NEARBY);cc_scroll=cc_rows()*2;if(cc_scroll>=16)cc_scroll=cc_rows();layout();
    cc_tab=0;cc_scroll=view.landscape?9:10; // 未记忆加密网络走统一密码输入。 / Unknown secured networks use the shared password editor.
    engine.networks[cc_scroll].requires_password=true;cc_status=engine;action(CC_ROW);CHECK(route==PS_EDITOR&&password_done&&cc_selected.ssid[0]);
    char selected[33];strcpy(selected,cc_selected.ssid);ps_back();CHECK(cc_page==PS_WIFI&&!strcmp(selected,cc_selected.ssid));
    cc_password_editor();password_done("correct-horse");CHECK(route==PS_WIFI&&job==JOB_CONNECT&&!strcmp(command_ssid,selected));complete_job();tick();CHECK(engine.connected&&!strcmp(ps_network_label(),selected));
    fixture();engine.network_count=0;engine.ready=false;engine.wifi_enabled=false;cc_status=engine;tick();CHECK(!commands);engine.ready=true;engine.wifi_enabled=true;tick();CHECK(job==JOB_SCAN&&commands==1);complete_job();tick();tick();CHECK(commands==1);
    fixture();action(CC_WIFI_TOGGLE);CHECK(engine.busy);complete_job();tick();CHECK(!engine.wifi_enabled);layout();action(CC_WIFI_TOGGLE);complete_job();engine.network_count=0;tick();CHECK(job==JOB_SCAN);complete_job();tick();CHECK(commands==3);
    fixture();engine.network_count=0;engine.suspended=true;cc_status=engine;ps_connectivity_enter(PS_WIFI);tick();CHECK(!commands);engine.suspended=false;tick();CHECK(job==JOB_SCAN);complete_job();tick();
    fixture();engine.connected=true;strcpy(engine.ssid,"Home-00");cc_status=engine;engine.last_error=ESP_FAIL;engine.last_operation=READ_PICO_CONNECTIVITY_OP_PROFILE;engine.result_seq++;tick();CHECK(strstr(last_toast,"已连接，但网络未保存"));
    fixture();tick();int drawn=draws;engine.rssi=-51;engine.networks[0].rssi=-41;CHECK(!tick()&&draws==drawn);engine.networks[0].rssi=-70;CHECK(tick());
}
static void first_use_fixture(void){
    fixture();engine.saved_count=engine.network_count=0;engine.wifi_enabled=false;
    engine.state=READ_PICO_CONNECTIVITY_DISABLED;cc_status=engine;cc_scanned_session=cc_scan_requested=false;
    memset(&cc_selected,0,sizeof(cc_selected));cc_manual=false;layout();
}
static void test_first_use(void){
    // 真正零记忆状态：开启无需预先添加，扫描完成后直接输入密码连接。
    // With no saved network, enabling needs no profile; the scan can lead directly to password entry.
    first_use_fixture();action(CC_WIFI_TOGGLE);
    CHECK(job==JOB_ENABLED&&command_flag&&engine.saved_count==0&&!engine.wifi_enabled&&!engine.connected);
    complete_job();tick();CHECK(job==JOB_SCAN&&engine.wifi_enabled&&!engine.connected);
    strcpy(engine.networks[0].ssid,"First Home");engine.networks[0].supported=engine.networks[0].requires_password=true;engine.network_count=1;
    complete_job();tick();layout();action(CC_ROW);
    CHECK(route==PS_EDITOR&&password_done&&commands==2);
    password_done("first-password");CHECK(job==JOB_CONNECT&&!strcmp(command_ssid,"First Home")&&!engine.connected);
    complete_job();tick();CHECK(route==PS_WIFI&&engine.connected);
    // 关闭状态点扫描自动开启，且保留已存记录；关闭/开启后即使有旧列表也重新扫描。
    // Scan enables WiFi while off, preserves profiles, and refreshes even a cached list after re-enabling.
    fixture();action(CC_WIFI_TOGGLE);complete_job();tick();CHECK(!engine.wifi_enabled&&engine.saved_count==8);
    action(CC_SCAN);CHECK(job==JOB_ENABLED&&command_flag&&!engine.wifi_enabled&&engine.saved_count==8);
    complete_job();tick();CHECK(job==JOB_SCAN&&cc_scanned_session&&!cc_scan_requested&&engine.saved_count==8);
    complete_job();tick();unsigned after_scan=(unsigned)commands;tick();CHECK((unsigned)commands==after_scan);
    // 手动输入不依赖开启状态，安全方式可明确切换为无密码。
    // Manual SSID entry works while off, and explicitly supports selecting an open network.
    first_use_fixture();action(CC_ADD);CHECK(route==PS_EDITOR&&name_done&&!commands);
    name_done("Hidden Home");CHECK(route==PS_WIFI&&cc_view==CC_DETAIL&&cc_manual&&!engine.wifi_enabled&&!commands);
    layout();action(CC_CONNECT);CHECK(route==PS_EDITOR&&password_done&&!commands);
    password_done("manual-password");CHECK(route==PS_WIFI&&job==JOB_CONNECT&&!strcmp(command_ssid,"Hidden Home")&&!engine.wifi_enabled);
    complete_job();tick();CHECK(engine.connected);
    first_use_fixture();action(CC_ADD);name_done("Open Home");action(CC_SECURITY);
    CHECK(!cc_selected.requires_password);layout();action(CC_CONNECT);
    CHECK(job==JOB_CONNECT&&!strcmp(command_ssid,"Open Home")&&!command_secret[0]&&!engine.wifi_enabled);
    complete_job();tick();CHECK(engine.connected);
    // 输入取消或非法名称不提交网络命令，开启失败不能伪装为已开启。
    // Cancellation/invalid names submit nothing; a failed enable never claims the radio is on.
    first_use_fixture();action(CC_ADD);ps_back();CHECK(route==PS_WIFI&&!commands&&!engine.wifi_enabled);
    action(CC_ADD);name_done("");CHECK(!commands&&!cc_selected.ssid[0]);ps_back();
    fail_command=1;action(CC_WIFI_TOGGLE);CHECK(!engine.wifi_enabled&&!engine.connected&&!job);fail_command=0;
}
static void scan_failure(read_pico_connectivity_scan_failure_t phase){
    engine.busy=engine.scanning=false;job=JOB_NONE;engine.completed_seq=engine.submitted_seq;
    engine.last_error=engine.scan_error=ESP_FAIL;engine.scan_failure=phase;
    engine.last_operation=READ_PICO_CONNECTIVITY_OP_SCAN;engine.result_seq++;engine.scan_completed_seq++;
}
static void test_scan_lifecycle(void){
    first_use_fixture();action(CC_WIFI_TOGGLE);CHECK(engine.busy&&job==JOB_ENABLED);
    // busy期间的重扫不会丢失，也不会重复提交启用命令。
    // Scan during busy enable retains intent without submitting duplicate enable commands.
    action(CC_SCAN);CHECK(cc_scan_requested&&commands==1&&job==JOB_ENABLED);layout();
    tick();CHECK(commands==1);complete_job();tick();CHECK(job==JOB_SCAN&&commands==2);layout();
    scan_failure(READ_PICO_CONNECTIVITY_SCAN_START);tick();
    CHECK(strstr(last_toast,"扫描启动")&&!strstr(last_toast,"密码")&&!cc_scan_requested);
    CHECK(strstr(cc_nearby_empty_message(),"扫描启动"));layout();
    unsigned before=(unsigned)commands;tick();CHECK((unsigned)commands==before);
    action(CC_SCAN);CHECK(job==JOB_SCAN);scan_failure(READ_PICO_CONNECTIVITY_SCAN_TIMEOUT);tick();
    CHECK(strstr(last_toast,"扫描超时")&&!strstr(last_toast,"密码"));layout();
    action(CC_SCAN);CHECK(job==JOB_SCAN);scan_failure(READ_PICO_CONNECTIVITY_SCAN_RESULT);tick();
    CHECK(strstr(last_toast,"扫描结果")&&!strstr(last_toast,"密码"));layout();
    action(CC_SCAN);complete_job();tick();
    CHECK(engine.scan_error==ESP_OK&&engine.network_count==0&&strstr(cc_nearby_empty_message(),"扫描完成"));layout();
    before=(unsigned)commands;tick();tick();CHECK((unsigned)commands==before);
    // 正在扫描时的下一次请求保留为一次；完成后才提交，结果不会误报旧连接密码错误。
    // A scan requested during scanning waits once for completion and cannot inherit a stale connection error.
    action(CC_SCAN);action(CC_SCAN);CHECK(cc_scan_requested&&job==JOB_SCAN);
    complete_job();tick();CHECK(job==JOB_SCAN&&!cc_scan_requested);complete_job();tick();
    fixture();engine.last_operation=READ_PICO_CONNECTIVITY_OP_CONNECT;engine.last_error=ESP_FAIL;engine.result_seq++;tick();
    CHECK(strstr(last_toast,"连接未成功"));action(CC_SCAN);CHECK(job==JOB_SCAN);
    scan_failure(READ_PICO_CONNECTIVITY_SCAN_CANCELLED);tick();CHECK(strstr(last_toast,"扫描已中断")&&!strstr(last_toast,"密码"));
    // 队列暂拒绝时保留扫描，时钟前进后重试；异步开启失败则终止本次自动请求。
    // Temporary enqueue rejection retains scan intent for a timed retry; asynchronous enable failure ends the request.
    first_use_fixture();fail_command=1;action(CC_SCAN);CHECK(cc_scan_requested&&!job&&!engine.wifi_enabled);
    fail_command=0;tick();CHECK(job==JOB_ENABLED);complete_job();tick();CHECK(job==JOB_SCAN);complete_job();tick();
    first_use_fixture();action(CC_SCAN);engine.busy=false;job=JOB_NONE;engine.completed_seq=engine.submitted_seq;
    engine.last_operation=READ_PICO_CONNECTIVITY_OP_WIFI;engine.last_error=ESP_FAIL;engine.result_seq++;tick();
    CHECK(!engine.wifi_enabled&&!cc_scan_requested&&strstr(last_toast,"Wi-Fi 开启未成功"));
    before=(unsigned)commands;tick();tick();CHECK((unsigned)commands==before);
    fixture();fail_command=1;action(CC_SCAN);tick();tick();CHECK(!cc_scan_requested&&!job&&strstr(last_toast,"扫描请求未提交"));
    CHECK(strstr(cc_nearby_empty_message(),"扫描请求未提交"));fail_command=0;
    fixture();engine.busy=true;engine.submitted_seq++;job=JOB_AUTOMATIC;CHECK(ps_connectivity_request_scan()&&cc_scan_requested);
    complete_job();tick();CHECK(job==JOB_SCAN);complete_job();tick();
    ps_navigate(PS_TRANSFER);CHECK(!ps_connectivity_request_scan());
}
static void start_hotspot(void){ps_navigate(PS_TRANSFER);layout();action(CC_TRANSFER_AP);CHECK(prepared&&cc_session&&job==JOB_START);layout();complete_job();tick();CHECK(engine.transfer_active&&cc_qr_ready);layout();}
static void test_transfer(void){
    fixture();ps_navigate(PS_TRANSFER);layout();action(CC_TRANSFER_USB);CHECK(route==PS_USB_TRANSFER&&!prepared&&!commands);
    fixture();ps_navigate(PS_TRANSFER);action(CC_TRANSFER_STA);CHECK(route==PS_WIFI&&!prepared);
    fixture();ps_navigate(PS_TRANSFER);usb_held=true;CHECK(!ps_connectivity_request_transfer(READ_PICO_TRANSFER_MODE_AP)&&!prepared);usb_held=false;CHECK(ps_connectivity_request_transfer(READ_PICO_TRANSFER_MODE_AP)&&prepared);complete_job();tick();CHECK(ps_connectivity_shutdown());
    fixture();ps_navigate(PS_TRANSFER);fail_prepare=1;action(CC_TRANSFER_AP);CHECK(cc_storage_problem&&!prepared);layout();action(CC_STORAGE);CHECK(route==PS_STORAGE);
    fixture();start_hotspot();int qr=qr_prepares;layout();CHECK(qr==qr_prepares);action(CC_QR_URL);CHECK(qr_prepares==qr+1);layout();
    engine.transfer.state=READ_PICO_TRANSFER_UPLOADING;engine.transfer.cur_bytes=500000;engine.transfer.cur_total=1000000;strcpy(engine.transfer.cur_name,"一部很长很长的小说.epub");engine.transfer.done_count=2;engine.transfer.changed_count=2;
    int capacities=capacity_reads;tick();CHECK(capacity_reads==capacities);layout();engine.transfer.cur_bytes++;CHECK(!tick());engine.transfer.cur_bytes++;CHECK(tick());
    engine.transfer.state=READ_PICO_TRANSFER_READY;engine.transfer.done_count=3;engine.transfer.changed_count=3;tick();CHECK(capacity_reads==capacities+1);
    CHECK(ps_connectivity_before_leave(PS_HA)&&cc_confirm==CC_CONFIRM_LEAVE&&!finished);layout();action(CC_CONFIRM_CANCEL);CHECK(engine.transfer_active&&!finished);
    action(CC_BOOKS);action(CC_CONFIRM_YES);CHECK(cc_stopping&&engine.busy&&!finished&&!books);tick();layout();CHECK(!finished);complete_job();tick();CHECK(finished==1&&finished_changes==3&&books==0&&route==PS_SHELF&&!prepared);
    fixture();start_hotspot();CHECK(ps_connectivity_back());action(CC_CONFIRM_YES);fail_stop=1;complete_job();tick();CHECK(!cc_stopping&&cc_stop_failed&&engine.transfer_active&&!finished);layout();CHECK(ps_connectivity_back()&&!cc_confirm&&!cc_stop_failed&&engine.transfer_active);
    CHECK(ps_connectivity_before_leave(PS_DEVICE));action(CC_CONFIRM_YES);fail_stop=0;complete_job();tick();CHECK(route==PS_DEVICE&&!prepared&&finished==1);
    fixture();start_hotspot();fail_quiesce=1;CHECK(!ps_connectivity_shutdown()&&prepared&&!finished);fail_quiesce=0;CHECK(ps_connectivity_shutdown()&&!prepared&&finished==1);capacities=capacity_reads;int command_count=commands;ps_connectivity_media_lost();CHECK(capacity_reads==capacities&&commands==command_count);
    fixture();ps_navigate(PS_TRANSFER);action(CC_TRANSFER_AP);engine.busy=false;engine.last_error=ESP_FAIL;engine.transfer.state=READ_PICO_TRANSFER_ERROR;engine.transfer_active=false;job=0;tick();CHECK(!prepared&&cc_session);layout();action(CC_TRANSFER_RESET);CHECK(!cc_session);layout();
}
static void test_management(void){
    fixture();engine.connected=false;engine.saved_count=engine.network_count=0;engine.wifi_enabled=false;
    unsigned initial=management_starts;ps_navigate(PS_WIRELESS_MANAGE);layout();
    CHECK(!management.active&&management_starts==initial&&route==PS_WIRELESS_MANAGE);
    action(CC_MANAGE_WIFI);CHECK(route==PS_WIFI);
    fixture();engine.connected=true;strcpy(engine.ip,"192.0.2.30");
    initial=management_starts;ps_navigate(PS_WIRELESS_MANAGE);CHECK(management_starts==initial+1&&management.active&&cc_qr_ready);layout();
    tick();int prepares_before=qr_prepares;CHECK(!tick()&&qr_prepares==prepares_before);
    management.seconds_left=899;CHECK(!tick()&&qr_prepares==prepares_before);
    management.seconds_left=840;CHECK(tick()&&qr_prepares==prepares_before);
    action(CC_MANAGE_OPEN);CHECK(management_starts==initial+2&&qr_prepares==prepares_before+1);layout();
    ps_navigate(PS_DEVICE);CHECK(management.active);ps_navigate(PS_WIRELESS_MANAGE);CHECK(management_starts==initial+2&&cc_qr_ready);layout();
    action(CC_MANAGE_STOP);CHECK(!management.active&&!cc_qr_ready);layout();
    action(CC_MANAGE_OPEN);CHECK(management.active&&cc_qr_ready);engine.connected=false;tick();CHECK(!management.active&&!cc_qr_ready);layout();
}
int main(void){
    for(int land=0;land<2;land++){view=land?(ps_view_t){640,360,true}:(ps_view_t){360,640,false};test_wifi();test_first_use();test_scan_lifecycle();test_transfer();test_management();}
    printf("PASS: connection UI %d checks, real controls/state machine with network/storage doubles; physical WiFi/upload acceptance remains.\n",checks);
    return 0;
}

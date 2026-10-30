/* SPDX-License-Identifier: Apache-2.0
 * 真实TF状态机与命中检查，卡/任务为替身，不访问或格式化真实卡。
 * Actual TF state-machine/hit checks with card/task doubles; never access a physical card.
 */
#include "pico_system.h"
#include "pico_reader.h"
#include "read_pico_sd.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static int64_t now_ms=1000;
static ps_view_t view={360,640,false};
static read_pico_sd_info_t card;
static int reads,formats,verifications,remounts,prepares,rebinds,draws,resumes,tasks,checks;
static bool fail_task,fail_format,fail_prepare,fail_verify,fail_rebind,rebound_formatted;
static void (*job)(void*);
static char toast[160];
static bool roots_available=true,other_available=true,current_is_sd;
static unsigned import_calls;
static ps_reader_records_import_status_t import_status=PS_READER_IMPORT_OK;
bool ps_storage_record_roots(char *current,char *other,size_t cap,bool *is_sd){snprintf(current,cap,"/flash/books/.pico-system");snprintf(other,cap,"%s",other_available?"/sdcard/books/.pico-system":"");*is_sd=current_is_sd;return roots_available;}
bool ps_reader_records_import(const char *root,ps_reader_records_import_result_t *out){assert(root&&strstr(root,"/sdcard/"));import_calls++;*out=(ps_reader_records_import_result_t){.status=import_status,.source_count=4,.target_count=3,.added=2,.duplicates=2,.total_count=5};return import_status==PS_READER_IMPORT_OK;}
#define CHECK(c) do{checks++;if(!(c)){fprintf(stderr,"TF check failed line %d: %s\n",__LINE__,#c);assert(c);}}while(0)
ps_view_t ps_view(void){return view;}
ps_route_t ps_route(void){return PS_STORAGE;}
int64_t ps_now(void){return now_ms;}
bool ps_storage_prepare(void){prepares++;return !fail_prepare;}
void ps_storage_resume(void){resumes++;}
bool ps_storage_rebind(bool formatted){rebinds++;rebound_formatted=formatted;return !fail_rebind;}
const char *ps_storage_target(void){return "笔记存储：TF 卡";}
void ps_toast(const char *s){snprintf(toast,sizeof(toast),"%s",s);}
static void bounds(int x,int y,int w,int h){CHECK(x>=0&&y>=0&&w>=0&&h>=0&&x+w<=view.w&&y+h<=view.h);draws++;}
bool ps_hit(int x,int y,int rx,int ry,int rw,int rh){return x>=rx&&x<rx+rw&&y>=ry&&y<ry+rh;}
void ps_heading(uint8_t*a,const char*b,const char*c){(void)a;(void)b;(void)c;draws++;}
void ps_icon(uint8_t*a,int b,int c,int d,ps_icon_t e,int f){(void)a;(void)e;(void)f;bounds(b,c,d,d);}
void ps_text(uint8_t*a,int b,int c,int d,const char*e,int f){(void)a;(void)e;(void)f;bounds(b,c,0,d);}
void ps_center(uint8_t*a,int b,int c,int d,const char*e,int f){ps_text(a,b,c,d,e,f);}
void ps_wrap(uint8_t*a,int b,int c,int d,int e,int f,const char*g,int h){(void)f;bounds(b,c,d,e);ps_text(a,b,c,e,g,h);}
void ps_button(uint8_t*a,int b,int c,int d,int e,const char*f,bool g){(void)a;(void)f;(void)g;bounds(b,c,d,e);}
void ps_box(uint8_t*a,int b,int c,int d,int e,int f,int g,bool h){(void)a;(void)f;(void)g;(void)h;bounds(b,c,d,e);}
esp_err_t read_pico_sd_get_info(read_pico_sd_info_t *out){reads++;*out=card;return card.error;}
esp_err_t read_pico_sd_format(void){
    formats++;card.format_started=true;card.stage=READ_PICO_SD_STAGE_FAILED;
    if(fail_format){card.error=ESP_FAIL;card.state=READ_PICO_SD_STATE_IO_ERROR;return ESP_FAIL;}
    card.mounted=true;card.error=ESP_OK;card.state=READ_PICO_SD_STATE_READY;card.stage=READ_PICO_SD_STAGE_COMPLETE;card.read_verified=card.write_verified=true;return ESP_OK;
}
esp_err_t read_pico_sd_verify(void){
    verifications++;card.read_verified=card.write_verified=!fail_verify;card.error=fail_verify?ESP_FAIL:ESP_OK;card.stage=fail_verify?READ_PICO_SD_STAGE_FAILED:READ_PICO_SD_STAGE_COMPLETE;card.state=fail_verify?READ_PICO_SD_STATE_CHECK_FAILED:READ_PICO_SD_STATE_READY;return card.error;
}
esp_err_t read_pico_sd_remount(void){remounts++;card.mounted=false;card.read_verified=card.write_verified=false;card.error=ESP_ERR_NOT_FINISHED;card.state=READ_PICO_SD_STATE_DETECTING;card.stage=READ_PICO_SD_STAGE_DETECTING;return ESP_ERR_NOT_FINISHED;}
int xTaskCreate(void (*fn)(void*),const char*n,unsigned stack,void*a,int pri,void*h){(void)n;(void)stack;(void)a;(void)pri;(void)h;tasks++;if(fail_task)return 0;CHECK(!job);job=fn;return 1;}
void vTaskDelete(void*p){(void)p;}
#include "pico_storage.c"
static void reset(void){
    sd_running=sd_started=sd_success=false;sd_view=sd_action=sd_work=0;sd_message[0]=toast[0]=0;sd_poll_at=sd_start_at=sd_confirm_at=0;atomic_store(&sd_done,0);
    reads=formats=verifications=remounts=prepares=rebinds=draws=resumes=tasks=0;import_calls=0;roots_available=other_available=true;current_is_sd=false;import_status=PS_READER_IMPORT_OK;fail_task=fail_format=fail_prepare=fail_verify=fail_rebind=rebound_formatted=false;job=NULL;
    memset(&card,0,sizeof(card));card.state=READ_PICO_SD_STATE_ABSENT;card.error=ESP_FAIL;card.media_generation=1;now_ms+=40000;ps_storage_enter();
}
static void present(void){card.present=true;card.physical_bytes=card.capacity_bytes=16000000000ULL;card.free_bytes=12000000000ULL;card.state=READ_PICO_SD_STATE_FILESYSTEM_ERROR;strcpy(card.name,"TEST");}
static void mounted(void){present();card.error=ESP_OK;card.state=READ_PICO_SD_STATE_READY;card.stage=READ_PICO_SD_STAGE_COMPLETE;card.mounted=true;card.filesystem=READ_PICO_SD_FS_FAT;}
static bool tick(void){now_ms+=1001;return ps_storage_tick();}
static void action(int id){sd_control_t c[2];int n=sd_controls(c);for(int i=0;i<n;i++)if(c[i].id==id){ps_event_t e={.type=PS_TAP,.x=c[i].x+c[i].w/2,.y=c[i].y+c[i].h/2};CHECK(ps_storage_event(&e));return;}CHECK(!"missing TF control");}
static void complete(void){CHECK(job);void(*fn)(void*)=job;job=NULL;fn(NULL);}
static void layout(void){
    sd_control_t c[2];int n=sd_controls(c);for(int i=0;i<n;i++){CHECK(c[i].x>=0&&c[i].y>=24&&c[i].w>0&&c[i].h>=32&&c[i].x+c[i].w<=view.w&&c[i].y+c[i].h<=view.h-52);for(int j=0;j<i;j++)CHECK(c[i].x>=c[j].x+c[j].w||c[j].x>=c[i].x+c[i].w||c[i].y>=c[j].y+c[j].h||c[j].y>=c[i].y+c[i].h);}
    int old_reads=reads,old_formats=formats,old_mounts=remounts,old_verifies=verifications,old_tasks=tasks;ps_storage_render(NULL);
    CHECK(reads==old_reads&&formats==old_formats&&remounts==old_mounts&&verifications==old_verifies&&tasks==old_tasks);
}
static void confirmations(void){action(SD_OPEN_FORMAT);CHECK(sd_view==SD_BACKUP&&!formats&&!prepares);layout();action(SD_CONTINUE);CHECK(sd_view==SD_ERASE&&!formats&&!prepares);layout();now_ms+=600;action(SD_CONFIRM);}
static void format_cases(void){
    reset();layout();action(SD_OPEN_FORMAT);CHECK(!sd_view&&!formats&&!prepares);CHECK(!ps_storage_request_format());
    present();ps_storage_enter();layout();action(SD_OPEN_FORMAT);CHECK(sd_view==SD_BACKUP);CHECK(ps_storage_back()&&sd_view==SD_OVERVIEW&&!formats);
    action(SD_OPEN_FORMAT);action(SD_CANCEL);CHECK(!sd_view&&!formats);action(SD_OPEN_FORMAT);action(SD_CONTINUE);action(SD_CANCEL);CHECK(!sd_view&&!formats);
    action(SD_OPEN_FORMAT);action(SD_CONTINUE);action(SD_CONFIRM);CHECK(sd_view==SD_ERASE&&!sd_running&&!formats&&!prepares);now_ms+=600;card.present=false;action(SD_CONFIRM);CHECK(!sd_running&&!formats&&!prepares&&!sd_view);
    present();ps_storage_enter();action(SD_OPEN_FORMAT);card.media_generation++;tick();CHECK(!sd_view&&!formats);action(SD_OPEN_FORMAT);card.media_generation++;action(SD_CONTINUE);CHECK(!sd_view&&!formats);
    action(SD_OPEN_FORMAT);action(SD_CONTINUE);card.media_generation++;now_ms+=600;action(SD_CONFIRM);CHECK(!sd_view&&!formats);
    ps_storage_enter();confirmations();CHECK(sd_running&&prepares==1&&!formats);layout();CHECK(ps_storage_back());CHECK(!ps_storage_request_format());tick();CHECK(job&&!formats);complete();CHECK(formats==1);tick();CHECK(!sd_running&&sd_view==SD_RESULT&&sd_success&&rebinds==1&&rebound_formatted&&resumes==1);layout();for(int i=0;i<4;i++)tick();CHECK(formats==1);action(SD_DONE);CHECK(!sd_view);
    reset();present();ps_storage_enter();fail_format=true;confirmations();tick();complete();tick();CHECK(!sd_running&&!sd_success&&!rebinds&&formats==1&&strstr(sd_message,"可能已被清除"));layout();for(int i=0;i<4;i++)tick();CHECK(formats==1);
    reset();present();ps_storage_enter();fail_task=true;confirmations();tick();tick();CHECK(!sd_running&&!formats&&!rebinds&&resumes==1);layout();
    reset();present();ps_storage_enter();fail_prepare=true;confirmations();CHECK(!sd_running&&!job&&!formats&&!rebinds&&!resumes&&sd_view==SD_RESULT);layout();
    reset();mounted();ps_storage_enter();fail_rebind=true;CHECK(ps_storage_request_format());tick();complete();tick();CHECK(!sd_running&&!sd_success&&rebinds==1&&strstr(sd_message,"尚未启用"));layout();
    reset();present();card.state=READ_PICO_SD_STATE_DETECTING;card.error=ESP_ERR_NOT_FINISHED;CHECK(!ps_storage_request_format());card.state=READ_PICO_SD_STATE_EXPORTED;card.error=ESP_OK;CHECK(!ps_storage_request_format());card.state=READ_PICO_SD_STATE_NEEDS_REMOUNT;CHECK(!ps_storage_request_format());
    card.state=READ_PICO_SD_STATE_UNSUPPORTED_FILESYSTEM;CHECK(ps_storage_request_format()&&sd_running&&prepares==1&&!formats);tick();complete();tick();CHECK(formats==1&&!sd_running);
}
static void detect_cases(void){
    reset();mounted();ps_storage_enter();CHECK(!strcmp(sd_state_title(),"TF 卡已挂载"));action(SD_DETECT);CHECK(sd_running&&prepares==1);tick();complete();tick();CHECK(sd_running&&remounts==1&&!verifications&&!formats);layout();
    card.stage=READ_PICO_SD_STAGE_MOUNTING;CHECK(tick());layout();mounted();tick();CHECK(sd_work==SD_WORK_VERIFY&&!sd_started&&sd_running);tick();CHECK(job);complete();tick();CHECK(!sd_running&&verifications==1&&rebinds==1&&!rebound_formatted&&!formats&&sd_success&&resumes==1);layout();CHECK(!strcmp(sd_state_title(),"TF 卡可读写"));
    reset();present();ps_storage_enter();action(SD_DETECT);tick();complete();card.error=ESP_FAIL;card.state=READ_PICO_SD_STATE_UNSUPPORTED_FILESYSTEM;tick();CHECK(!sd_running&&!verifications&&!rebinds&&!formats);layout();
    reset();mounted();ps_storage_enter();action(SD_DETECT);tick();complete();mounted();tick();tick();fail_verify=true;complete();tick();CHECK(!sd_running&&!sd_success&&verifications==1&&!rebinds&&!formats);layout();
    reset();present();ps_storage_enter();action(SD_DETECT);tick();complete();now_ms+=31000;ps_storage_tick();CHECK(!sd_running&&!verifications&&!formats&&resumes==1);layout();
    reset();present();card.state=READ_PICO_SD_STATE_EXPORTED;ps_storage_enter();action(SD_DETECT);CHECK(!sd_running&&!prepares&&!remounts&&!verifications&&!formats);
    for(int state=READ_PICO_SD_STATE_ABSENT;state<=READ_PICO_SD_STATE_EXPORTED;state++){
        reset();present();card.state=(read_pico_sd_state_t)state;card.present=state!=READ_PICO_SD_STATE_ABSENT;card.error=state==READ_PICO_SD_STATE_DETECTING?ESP_ERR_NOT_FINISHED:ESP_OK;ps_storage_enter();layout();
    }
}
static void import_cases(void){
    reset();mounted();ps_storage_enter();
    ps_event_t open={.type=PS_TAP,.x=view.landscape?340:40,.y=view.landscape?250:490};
    CHECK(ps_storage_event(&open)&&sd_view==SD_RECORDS);layout();
    action(SD_IMPORT);CHECK(sd_view==SD_IMPORT_PROGRESS&&!import_calls);layout();
    CHECK(ps_storage_back()&&sd_view==SD_IMPORT_PROGRESS);tick();CHECK(import_calls==1&&sd_view==SD_IMPORT_RESULT&&strstr(sd_message,"新增 2"));layout();
    for(int i=0;i<4;i++)tick();CHECK(import_calls==1);action(SD_DONE);CHECK(sd_view==SD_OVERVIEW);
    ps_storage_event(&open);card.media_generation++;action(SD_IMPORT);CHECK(sd_view==SD_RECORDS&&import_calls==1);
    roots_available=false;ps_storage_back();ps_storage_event(&open);sd_control_t controls[2];CHECK(sd_controls(controls)==1&&controls[0].id==SD_CANCEL);CHECK(sd_view==SD_RECORDS&&import_calls==1);layout();
    roots_available=true;other_available=false;current_is_sd=true;ps_storage_back();ps_storage_event(&open);CHECK(sd_records_current_available&&!sd_records_available&&sd_records_current_is_sd);CHECK(sd_controls(controls)==1);layout();
    other_available=true;current_is_sd=false;
    roots_available=true;ps_storage_back();ps_storage_event(&open);action(SD_IMPORT);card.media_generation++;tick();CHECK(sd_view==SD_IMPORT_RESULT&&import_calls==1&&sd_import_result.status==PS_READER_IMPORT_UNAVAILABLE);layout();
    for(int status=PS_READER_IMPORT_NO_SOURCE;status<=PS_READER_IMPORT_BUSY;status++){
        reset();mounted();ps_storage_enter();ps_storage_event(&open);import_status=status;action(SD_IMPORT);tick();CHECK(import_calls==1&&sd_import_result.status==(ps_reader_records_import_status_t)status&&sd_view==SD_IMPORT_RESULT);layout();
    }
}
int main(void){for(int land=0;land<2;land++){view=land?(ps_view_t){640,360,true}:(ps_view_t){360,640,false};format_cases();detect_cases();import_cases();}printf("TF actual UI: %d checks, both orientations, trusted request/two confirmations, identity changes, actual phases, read/write verification and failure recovery; no physical card access.\n",checks);return 0;}

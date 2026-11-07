/* SPDX-License-Identifier: Apache-2.0
 * 中文：TF应用启动器与平台接口；游戏规则、AI及游戏页面只存在于卡上模块。
 * English: TF app launcher and platform services; rules, AI and game screens live only in card modules.
 * 冻结：每次只载入一个应用；卸载前停止任务并保存，失败留在原应用供重试或明确放弃，外点只取消；删除包保留存档，绘图不执行文件IO。
 * Frozen: Load one app at a time; stop workers and save before unloading, retain failures in the original app for retry or explicit discard, dismiss outside non-destructively, and keep rendering free of file IO.
 */
#include "pico_system.h"
#include "pico_scroll.h"
#include "pico_i18n.h"
#include "pico_power.h"
#include "pico_theme.h"
#include "pico_navigation.h"
#include "pico_connectivity.h"
#include "pico_weread.h"
#include "pico_weread_cover.h"
#include "pico_home_pga.h"
#include "pico_home_portal.h"
#include "pico_dual.h"
#include "epdiy.h"
#include "ui_wifi_qr.h"
#include "pico_app_sdk.h"
#include "pico_app_package.h"
#include "pico_app_loader.h"
#include "read_pico_sd.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "esp_random.h"

#ifdef ESP_PLATFORM
#include "esp_attr.h"
#define PICO_UI_CACHE_ATTR EXT_RAM_BSS_ATTR
#else
#define PICO_UI_CACHE_ATTR
#endif
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/idf_additions.h"
#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

typedef struct { pico_app_package_t info; } game_item_t;
// 应用目录仅由UI线程扫描/显示；载入器及AI不借用此表。 / Only the UI scans/paints this catalog; the loader and AI never borrow it.
static PICO_UI_CACHE_ATTR game_item_t s_items[PICO_APP_CATALOG_MAX];
static unsigned s_count,s_bad,s_page;
static ps_scroll_track_t games_scroll;
_Static_assert(PS_PRESS==0&&PS_MOVE==1&&PS_RELEASE==2&&PS_TAP==3&&PS_LONG==4&&PS_SWIPE_L==5&&PS_SWIPE_R==6&&PS_SWIPE_U==7&&PS_SWIPE_D==8&&PS_CANCEL==9,"pico_scroll.h event numbers");
static ps_route_t s_origin=PS_GAMES;
static bool s_scanned,s_in_call,s_pinned,s_catalog_dirty,s_discard;
static bool s_power_paused,s_recovery_outside;
static bool s_weread_setup;
static bool s_weread_edit,s_weread_input_ready,s_weread_link;
static char s_weread_input[160];
static PICO_UI_CACHE_ATTR ps_weread_status_t s_weread_status;
enum { APP_NAV_WEREAD_SETUP=PGA_PAGE_APP+1,APP_NAV_WEREAD_EDIT };
static int s_pending_route=-1;
static char s_pending_id[32];
static bool s_pending_tool;
static int s_remove=-1,s_nav=-1;
static bool s_dual_game_applying;
static struct {
    bool valid;
    char event[24];
    int kind;
    unsigned generation, aux;
} s_dual_game_status;
static char s_error[160];
static pico_app_package_t s_active_info;
static const pico_app_exports_t *s_app;
static void *s_code;
static TaskHandle_t s_worker;
static uint32_t s_active_media_generation;
static bool s_ink_reference;
static int s_ink_width,s_ink_height;
static struct {char id[32],title[64];uint16_t navigation;} s_tools[8];
static unsigned s_tool_count;
static void unregister_tools(void) {
    for(unsigned i=0;i<s_tool_count;i++)ps_navigation_unregister(s_tools[i].navigation);
    s_tool_count=0;
}

static bool error(const char *message) { snprintf(s_error,sizeof(s_error),"%s",message);return false; }
const char *ps_games_last_error(void) { return s_error; }
unsigned ps_games_count(void) { return s_count; }
bool ps_games_item(unsigned index,const char **id,const char **title) {
    if(index>=s_count||!id||!title)return false;
    *id=s_items[index].info.id;*title=s_items[index].info.title;return true;
}
static bool game_route(ps_route_t r) { return r==PS_PLAYGAME||r==PS_ARCADEGAME||r==PS_APPLICATION; }
static ps_route_t app_route(uint32_t kind) { return kind>=128?PS_APPLICATION:kind>=PGA_2048?PS_ARCADEGAME:PS_PLAYGAME; }
static void game_rotation(void);
static void portrait_rotation(void);
static bool storage_held(void) { return ps_storage_busy()||ps_transfer_prepared()||ps_usb_held(); }
static bool ready(void) { read_pico_sd_info_t info={0};return !storage_held()&&read_pico_sd_get_info(&info)==ESP_OK&&info.mounted; }
static bool directory(void) {
    return (mkdir("/sdcard/apps",0700)==0||errno==EEXIST)&&(mkdir(PICO_APP_DIRECTORY,0700)==0||errno==EEXIST);
}
static void package_path(char *out,size_t n,const char *id) { snprintf(out,n,PICO_APP_DIRECTORY "/%s.pga",id); }
static bool ends(const char *s,const char *suffix) { size_t a=strlen(s),b=strlen(suffix);return a>b&&!strcmp(s+a-b,suffix); }
static int compare_item(const void *a,const void *b) {
    const game_item_t *x=a,*y=b;if(x->info.kind!=y->info.kind)return x->info.kind<y->info.kind?-1:1;return strcmp(x->info.id,y->info.id);
}
static void recover_packages(const char *directory) {
    DIR *d=opendir(directory);if(!d)return;struct dirent *entry;
    while((entry=readdir(d))){
        if(!ends(entry->d_name,".pga.bak"))continue;
        char id[32];size_t n=strlen(entry->d_name)-8;if(!n||n>=sizeof(id))continue;memcpy(id,entry->d_name,n);id[n]=0;if(!pico_app_id_valid(id))continue;
        char main[160],backup[168],reason[160]={0};pico_app_package_t p;snprintf(main,sizeof(main),"%s/%s.pga",directory,id);snprintf(backup,sizeof(backup),"%s.bak",main);
        if(pico_app_package_read(main,&p,true,reason,sizeof(reason))&&!strcmp(p.id,id)) { (void)unlink(backup); }
        else if(pico_app_package_read(backup,&p,true,reason,sizeof(reason))&&!strcmp(p.id,id)){
            if(unlink(main)==0||errno==ENOENT)(void)rename(backup,main);
        }
    }closedir(d);
}
void ps_games_rescan(void) {
    // USB恢复挂载时可能尚未释放所有权；保留目录，交接完成后再扫描。
    // A USB remount may precede ownership release; retain the catalog until handoff completes.
    if(s_in_call||storage_held()){s_catalog_dirty=true;return;}
    s_count=s_bad=0;s_remove=-1;s_scanned=true;s_catalog_dirty=false;
    if(!s_app){s_discard=false;s_pending_route=-1;s_pending_id[0]=0;}
    if(!ready()){unregister_tools();error("请插入并挂载 TF 卡");return;}
    if(!directory()){error("无法读取应用目录");return;}
    recover_packages(PICO_APP_DIRECTORY);DIR *d=opendir(PICO_APP_DIRECTORY);if(!d){error("无法读取应用目录");return;}
    struct dirent *entry;
    while((entry=readdir(d))){
        if(!ends(entry->d_name,".pga"))continue;
        char path[192],expected[40],reason[160]={0};pico_app_package_t p;
        if(strlen(entry->d_name)>36){s_bad++;continue;}
        snprintf(path,sizeof(path),PICO_APP_DIRECTORY "/%.36s",entry->d_name);
        if(!pico_app_package_read(path,&p,false,reason,sizeof(reason))){s_bad++;continue;}
        snprintf(expected,sizeof(expected),"%s.pga",p.id);
        if(strcmp(expected,entry->d_name)||p.kind>=128||!pico_app_package_payload(&p)){s_bad++;continue;}
        if(s_count==PICO_APP_CATALOG_MAX){s_bad++;continue;}
        s_items[s_count++].info=p;
    }closedir(d);qsort(s_items,s_count,sizeof(*s_items),compare_item);
    if(s_page>=(s_count+5)/6)s_page=0;
    /* 家庭桥在首次 home_link 调用时才分配 PSRAM。扫描目录不占内部 RAM，否则 USB MSC 会以 257 失败。 */
    /* The home bridge allocates PSRAM on the first home_link call. Catalog scan must not take internal RAM or USB MSC fails with 257. */
    ps_apps_register_navigation();s_error[0]=0;ps_dirty();
}
static pico_app_view_t host_view(void) { ps_view_t v=ps_view();return (pico_app_view_t){v.w,v.h,v.landscape}; }
static int32_t host_page(void) { return s_active_info.kind>=128?PGA_PAGE_APP:s_active_info.kind>=PGA_2048?PGA_PAGE_ARCADE:PGA_PAGE_BOARD; }
static void host_navigate(int32_t page) { if(page>=PGA_PAGE_LAUNCHER&&page<=PGA_PAGE_APP)s_nav=page; }
static bool same_card(void) {
    read_pico_sd_info_t info={0};
    return ready()&&read_pico_sd_get_info(&info)==ESP_OK&&info.media_generation==s_active_media_generation;
}
static bool state_path(char *out,size_t n,const char *key,const char *suffix) {
    if(!pico_app_id_valid(key)||!pico_app_id_valid(s_active_info.id)||!same_card())return false;
    return snprintf(out,n,"/sdcard/apps/data/%s/%s.%s",s_active_info.id,key,suffix)<(int)n;
}
static bool state_read(const char *path,void *out,size_t bytes) {
    FILE *f=fopen(path,"rb");if(!f)return false;
    uint32_t h[3],crc=UINT32_MAX;uint8_t block[1024];size_t done=0;
    bool ok=fread(h,1,sizeof(h),f)==sizeof(h)&&h[0]==0x31535058u&&h[1]==bytes;
    while(ok&&done<bytes){
        size_t n=bytes-done;if(n>sizeof(block))n=sizeof(block);
        if(fread(block,1,n,f)!=n){ok=false;break;}
        for(size_t i=0;i<n;i++){crc^=block[i];for(unsigned j=0;j<8;j++)crc=(crc>>1)^(0xedb88320u&-(crc&1u));}
        if(out)memcpy((uint8_t*)out+done,block,n);
        done+=n;
    }
    ok=ok&&fgetc(f)==EOF&&~crc==h[2];
    if(fclose(f)!=0)ok=false;
    return ok;
}
static bool host_load(const char *key,void *data,size_t bytes) {
    char path[192];if(!data||!bytes||bytes>PICO_APP_MAX_STATE||!state_path(path,sizeof(path),key,"dat"))return false;
    if(state_read(path,data,bytes))return true;
    return state_path(path,sizeof(path),key,"bak")&&state_read(path,data,bytes);
}
static bool host_store(const char *key,const void *data,size_t bytes) {
    char path[192],tmp[192],backup[192],directory_path[128];
    if(!data||!bytes||bytes>PICO_APP_MAX_STATE||!state_path(path,sizeof(path),key,"dat")||!state_path(tmp,sizeof(tmp),key,"tmp")||!state_path(backup,sizeof(backup),key,"bak"))return false;
    if(mkdir("/sdcard/apps/data",0700)!=0&&errno!=EEXIST)return false;
    snprintf(directory_path,sizeof(directory_path),"/sdcard/apps/data/%s",s_active_info.id);
    if(mkdir(directory_path,0700)!=0&&errno!=EEXIST)return false;
    FILE *f=fopen(tmp,"wb");if(!f)return false;
    uint32_t h[3]={0x31535058u,(uint32_t)bytes,pico_app_crc32(data,bytes)};
    bool ok=fwrite(h,1,sizeof(h),f)==sizeof(h)&&fwrite(data,1,bytes,f)==bytes&&fflush(f)==0;
    if(ok&&fsync(fileno(f))!=0&&errno!=ENOSYS&&errno!=EINVAL)ok=false;
    if(fclose(f)!=0)ok=false;
    if(!ok||!same_card()||!state_read(tmp,NULL,bytes)){(void)unlink(tmp);return false;}
    // 仅以完整主档替换备份，损坏主档不得覆盖仍有效的恢复副本。
    // Only a complete primary may replace the backup; a damaged primary must not overwrite recovery data.
    struct stat st;bool exists=stat(path,&st)==0;
    bool valid=exists&&state_read(path,NULL,bytes);
    if(valid){
        if(unlink(backup)!=0&&errno!=ENOENT){(void)unlink(tmp);return false;}
        if(rename(path,backup)!=0){(void)unlink(tmp);return false;}
    }else if(exists&&unlink(path)!=0){(void)unlink(tmp);return false;}
    if(rename(tmp,path)!=0){if(valid)(void)rename(backup,path);return false;}

    return true;
}
static bool host_legacy(const char *key,void *data,size_t bytes) { return (!strcmp(key,"games")||!strcmp(key,"arcade1"))&&bytes<=PICO_APP_MAX_STATE&&ps_load(key,data,bytes); }
static int host_worker_create(void (*fn)(void*),void *arg,void **handle) {
    if(s_worker||!fn||!handle)return 0;
    int result=xTaskCreateWithCaps(fn,"pico_app_ai",PICO_APP_WORKER_STACK,arg,1,&s_worker,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    *handle=result==pdPASS?s_worker:NULL;return result==pdPASS;
}
static void host_worker_delete(void *handle) { if(handle&&handle==s_worker){vTaskDeleteWithCaps(s_worker);s_worker=NULL;} }
static void host_worker_suspend(void) { vTaskSuspend(NULL); }
static uint32_t host_worker_stack_free(void) { return (uint32_t)uxTaskGetStackHighWaterMark(NULL); }
static void host_delay(uint32_t ms) { vTaskDelay(pdMS_TO_TICKS(ms)); }
static void host_diagnostic(const char *event,int type,unsigned generation,unsigned stack_free) {
    if (event && !strncmp(event, "duo_", 4)) {
        snprintf(s_dual_game_status.event, sizeof(s_dual_game_status.event), "%s", event);
        s_dual_game_status.kind = type;
        s_dual_game_status.generation = generation;
        s_dual_game_status.aux = stack_free;
        s_dual_game_status.valid = true;
    }
    ESP_LOGI("pico_apps","%s %s kind=%d generation=%u stack_free=%u",s_active_info.id,event,type,generation,stack_free);
}
bool ps_games_duo_status(char *event,size_t capacity,int *kind,unsigned *generation,unsigned *aux) {
    if(!event||!capacity||!kind||!generation||!aux||!s_dual_game_status.valid)return false;
    snprintf(event,capacity,"%s",s_dual_game_status.event);
    *kind=s_dual_game_status.kind;*generation=s_dual_game_status.generation;*aux=s_dual_game_status.aux;
    return true;
}
static bool host_weread(uint32_t command,pico_app_weread_status_t *out){
    if(!out||out->bytes<sizeof(*out)||!s_app||strcmp(s_active_info.id,"weread"))return false;
    bool ok=true;
    if(command==PGA_WEREAD_SYNC)ok=ps_weread_sync_saved();
    else if(command==PGA_WEREAD_OPEN_SETUP){s_weread_setup=true;s_nav=APP_NAV_WEREAD_SETUP;}
    else if(command!=PGA_WEREAD_STATUS)return false;
    ps_weread_get_status(&s_weread_status);memset(out,0,sizeof(*out));out->bytes=sizeof(*out);
    out->flags=(s_weread_status.configured?PGA_WEREAD_CONFIGURED:0u)|(s_weread_status.wifi?PGA_WEREAD_WIFI:0u)|(s_weread_status.busy?PGA_WEREAD_BUSY:0u);
    out->state=!strcmp(s_weread_status.state,"shelf")?PGA_WEREAD_SHELF:!strcmp(s_weread_status.state,"sync")?PGA_WEREAD_SYNCING:
        !strcmp(s_weread_status.state,"done")?PGA_WEREAD_DONE:!strcmp(s_weread_status.state,"error")?PGA_WEREAD_ERROR:
        !strcmp(s_weread_status.state,"query")?PGA_WEREAD_QUERYING:PGA_WEREAD_IDLE;
    out->revision=s_weread_status.revision;out->books=s_weread_status.synced_books;out->notes=s_weread_status.synced_notes;
    out->progress=s_weread_status.progress;out->total=s_weread_status.total;snprintf(out->error,sizeof(out->error),"%s",s_weread_status.error);
    return ok;
}
static bool weread_active(void){return s_app&&!strcmp(s_active_info.id,"weread");}
static bool host_weread_query(const pico_app_weread_query_t *request,pico_app_weread_query_status_t *out){
    _Static_assert(sizeof(pico_app_weread_query_t)==sizeof(ps_weread_query_t),"query ABI");
    _Static_assert(sizeof(pico_app_weread_query_status_t)==sizeof(ps_weread_query_status_t),"query status ABI");
    if(!weread_active()||!out||out->bytes<sizeof(*out)||(request&&request->bytes<sizeof(*request)))return false;
    ps_weread_query_t query;ps_weread_query_status_t result={.bytes=sizeof(result)};
    if(request)memcpy(&query,request,sizeof(query));
    bool ok=ps_weread_query(request?&query:NULL,&result);memcpy(out,&result,sizeof(result));return ok;
}
static bool host_weread_query_read(uint32_t request_id,uint32_t offset,void *out,size_t capacity,size_t *read){
    if(read)*read=0;
    return weread_active()&&ps_weread_query_read(request_id,offset,out,capacity,read);
}
static bool host_weread_input(uint32_t command,char *value,size_t capacity){
    if(!weread_active())return false;
    if(command==PGA_WEREAD_INPUT_CANCEL){s_weread_input_ready=false;memset(s_weread_input,0,sizeof(s_weread_input));return true;}
    if(!value||!capacity)return false;
    if(command==PGA_WEREAD_INPUT_OPEN){
        size_t n=strnlen(value,capacity<sizeof(s_weread_input)?capacity:sizeof(s_weread_input));
        if(n>=capacity||n>=sizeof(s_weread_input)||s_weread_edit)return false;
        memcpy(s_weread_input,value,n+1);s_weread_input_ready=false;s_nav=APP_NAV_WEREAD_EDIT;return true;
    }
    if(command!=PGA_WEREAD_INPUT_TAKE||!s_weread_input_ready||strlen(s_weread_input)>=capacity)return false;
    memcpy(value,s_weread_input,strlen(s_weread_input)+1);s_weread_input_ready=false;
    memset(s_weread_input,0,sizeof(s_weread_input));return true;
}
static void weread_input_done(const char *text){
    if(!weread_active()||!s_weread_edit)return;
    snprintf(s_weread_input,sizeof(s_weread_input),"%s",text?text:"");s_weread_input_ready=true;
}
static bool host_weread_open_link(const char *url){
    if(!weread_active()||!ui_wifi_qr_prepare_weread(url))return false;
    s_weread_link=true;ps_dirty();return true;
}
static bool home_link_active(void){return s_app&&s_app->kind==PGA_HOME_LINK;}
/* 0 monitor, 1 connection setup, 2 printers. 0xff means the next home-link open is not a deep link. */
static uint8_t s_home_entry=0xff;
static bool s_home_entry_armed;
/* Swallow one later games-enter of the route that deep-linked HA, so the shell cannot unload the package it just opened. */
static bool s_home_hold;
static bool s_home_portal;
static ps_route_t s_home_hold_route;
static bool s_home_route_set;
static ps_route_t s_home_route_hint;
void ps_apps_expect_home_route(ps_route_t route){s_home_route_set=true;s_home_route_hint=route;}
void ps_apps_prepare_home(uint8_t page){
    if(page>2){s_home_entry=0xff;s_home_entry_armed=false;return;}
    s_home_entry=page;s_home_entry_armed=true;
}
static bool host_home_link(uint32_t command,pico_app_home_status_t *out){
    if(!home_link_active()||!out)return false;
    if(!out->bytes)out->bytes=sizeof(*out);
    if(command==PGA_HOME_ENTRY){
        uint8_t page=s_home_entry;
        s_home_entry=0xff;
        if(out->bytes<sizeof(*out)||page>2)return false;
        memset(out,0,sizeof(*out));
        out->bytes=sizeof(*out);
        out->result=page;
        out->flags=PGA_HOME_ENTRY_OK;
        return true;
    }
    if(command==PGA_HOME_OPEN_SETUP){
        bool ok=ps_home_portal_start(PS_HOME_PORTAL_OWNER_GAMES);
        ps_home_portal_view_t view;ps_home_portal_view(&view);
        if(ok&&(!view.url[0]||!ui_wifi_qr_prepare_url(view.url))){ps_home_portal_stop();ok=false;}
        if(ok){s_home_portal=true;ps_dirty();}
        (void)ps_home_pga_command(PGA_HOME_STATUS,out);
        if(!ok){
            const char *message=view.error[0]?view.error:"手机配置二维码生成失败";
            size_t n=strlen(message);
            if(n>=sizeof(out->error)){
                n=sizeof(out->error)-1;
                while(n&&((unsigned char)message[n]&0xc0)==0x80)n--;
            }
            memcpy(out->error,message,n);out->error[n]=0;
        }
        return ok;
    }
    return ps_home_pga_command(command,out);
}
static bool host_home_link_config(uint32_t command,pico_app_home_config_t *cfg){
    if(!home_link_active()||!cfg)return false;
    if(!cfg->bytes)cfg->bytes=sizeof(*cfg);
    return ps_home_pga_config(command,cfg);
}
static bool host_home_link_entity(uint32_t index,pico_app_home_entity_t *out){
    if(!home_link_active()||!out)return false;
    if(!out->bytes)out->bytes=sizeof(*out);
    return ps_home_pga_entity(index,out);
}
static bool host_home_link_printer(uint32_t slot,pico_app_home_printer_t *out){
    if(!home_link_active()||!out)return false;
    if(!out->bytes)out->bytes=sizeof(*out);
    return ps_home_pga_printer(slot,out);
}
static bool host_home_link_control(const char *entity_id,const char *action,float value){
    return home_link_active()&&ps_home_pga_control(entity_id,action,value);
}
static bool s_home_edit;
static void home_portal_dismiss(void) {
    if (!s_home_portal) return;
    s_home_portal = false;
    ps_home_portal_stop();
    ui_wifi_qr_clear();
}
static void home_input_done(const char *text){
    if(!home_link_active()||!s_home_edit)return;
    ps_home_pga_input_done(text);s_home_edit=false;
}
static bool host_home_link_input(uint32_t command,char *value,size_t capacity){
    if(!home_link_active())return false;
    if(command==PGA_HOME_INPUT_CANCEL){s_home_edit=false;return ps_home_pga_input(command,value,capacity);}
    if(command==PGA_HOME_INPUT_OPEN){
        if(!value||s_home_edit)return false;
        char seed[256];size_t n=capacity<sizeof(seed)?capacity:sizeof(seed);
        snprintf(seed,n,"%s",value);
        if(!ps_home_pga_input(PGA_HOME_INPUT_OPEN,seed,sizeof(seed)))return false;
        s_home_edit=true;ps_edit(ps_tr("settings.home_link.input_title"),seed,sizeof(seed)-1,home_input_done);return true;
    }
    return ps_home_pga_input(command,value,capacity);
}
static bool host_weread_cover(uint32_t request_id,uint8_t *fb,int x,int y,int width,int height){
    ps_view_t v=ps_view();
    if(!weread_active()||x<0||y<0||width<=0||height<=0||width>v.w-x||height>v.h-y)return false;
    return ps_weread_cover_draw(request_id,fb,x,y,width,height);
}
static void weread_child_clear(void){
    if(s_weread_link)ui_wifi_qr_clear();
    home_portal_dismiss();
    s_weread_edit=s_weread_input_ready=s_weread_link=false;memset(s_weread_input,0,sizeof(s_weread_input));s_home_edit=false;ps_home_pga_input(PGA_HOME_INPUT_CANCEL,NULL,0);
}
static int32_t host_duo_role(void){return (int32_t)ps_dual_game_role();}
static bool host_duo_send(uint8_t input){return ps_dual_game_send(input);}
static int32_t host_duo_recv(void){return ps_dual_game_recv();}
static bool host_duo_packet_send(const void *data,size_t bytes){return ps_dual_game_packet_send(data,bytes);}
static int32_t host_duo_packet_recv(void *data,size_t capacity){return (int32_t)ps_dual_game_packet_recv(data,capacity);}
static const pico_app_host_t s_host={
    .abi=PICO_APP_ABI,.bytes=sizeof(pico_app_host_t),.view=host_view,.page=host_page,.navigate=host_navigate,
    .dirty=ps_dirty,.toast=ps_toast,.load=host_load,.store=host_store,.legacy_load=host_legacy,.time_us=esp_timer_get_time,.random=esp_random,
    .fill=ps_fill,.line=ps_line,.box=ps_box,.text=ps_text,.center=ps_center,.wrap=ps_wrap,.button=ps_button,.heading=ps_heading,
    .worker_create=host_worker_create,.worker_delete=host_worker_delete,.worker_suspend=host_worker_suspend,.worker_stack_free=host_worker_stack_free,
    .delay_ms=host_delay,.diagnostic=host_diagnostic,.format=snprintf,.copy=memcpy,.move=memmove,.set=memset,.compare=memcmp,.weread=host_weread,
    .weread_query=host_weread_query,.weread_query_read=host_weread_query_read,.weread_input=host_weread_input,.weread_open_link=host_weread_open_link,
    .weread_cover=host_weread_cover,
    .home_link=host_home_link,.home_link_config=host_home_link_config,.home_link_entity=host_home_link_entity,
    .home_link_printer=host_home_link_printer,.home_link_control=host_home_link_control,.home_link_input=host_home_link_input,
    .tr=ps_tr,
    .duo_role=host_duo_role,.duo_input_send=host_duo_send,.duo_input_recv=host_duo_recv,
    .duo_packet_send=host_duo_packet_send,.duo_packet_recv=host_duo_packet_recv
};
static bool close_active(void) {
    if(!s_app)return true;
    s_app->stop();
    if(s_worker){error("应用任务未停止，暂不卸载");s_pinned=true;return false;}
    if(!s_app->save()){error("应用尚未保存，请检查 TF 卡后重试");s_pinned=true;return false;}
    weread_child_clear();pico_app_code_close(s_code);s_code=NULL;s_app=NULL;s_pinned=false;s_discard=false;s_ink_reference=false;s_weread_setup=false;memset(&s_active_info,0,sizeof(s_active_info));return true;
}
static bool discard_active(void) {
    if(!s_app)return true;
    s_app->stop();
    if(s_worker)return error("应用任务未停止，请稍后重试");
    weread_child_clear();pico_app_code_close(s_code);s_code=NULL;s_app=NULL;s_pinned=false;s_discard=false;s_ink_reference=false;s_weread_setup=false;
    memset(&s_active_info,0,sizeof(s_active_info));return true;
}
static void clear_pending(void) { s_pending_route=-1;s_pending_id[0]=0; }
static void recover_active(void) {
    s_discard=true;s_ink_reference=false;
    if(s_app&&!game_route(ps_route()))ps_navigate(app_route(s_active_info.kind));
    ps_dirty();
}
static void retain_active(void) {
    s_discard=false;clear_pending();
    if(s_app&&!s_worker&&same_card())s_pinned=false;
    ps_dirty();
}
static void finish_call(void) {
    s_in_call=false;
    int nav=s_nav;s_nav=-1;
    if(nav==PGA_PAGE_LAUNCHER){
        ps_route_t origin=s_origin;
        if(close_active())ps_navigate(origin);
        else {clear_pending();s_pending_route=origin;recover_active();}
    }
    else if(nav>=PGA_PAGE_BOARD&&nav<=PGA_PAGE_APP){ps_route_t route=nav==PGA_PAGE_APP?PS_APPLICATION:nav==PGA_PAGE_BOARD?PS_PLAYGAME:PS_ARCADEGAME;if(ps_route()!=route)ps_navigate(route);}
    else if(nav==APP_NAV_WEREAD_SETUP&&!ps_connectivity_open_weread_setup()){s_weread_setup=false;ps_toast(ps_tr("settings.manage.weread_fail"));}
    else if(nav==APP_NAV_WEREAD_EDIT&&weread_active()){
        s_weread_edit=true;ps_edit("在线搜索微信读书",s_weread_input,sizeof(s_weread_input)-1,weread_input_done);
    }
    if(s_catalog_dirty)ps_games_rescan();
}
static bool open_package(pico_app_package_t info,const char *path) {
    if(!ready())return error("应用暂不可用，请检查 TF 卡");
    if(s_app&&!strcmp(s_active_info.id,info.id)){
        if(!same_card()){
            clear_pending();snprintf(s_pending_id,sizeof(s_pending_id),"%s",info.id);s_pending_tool=info.kind>=128;
            recover_active();return error("请先保留或处理未保存的应用");
        }
        retain_active();ps_navigate(app_route(info.kind));return true;
    }
    if(!close_active()){
        clear_pending();snprintf(s_pending_id,sizeof(s_pending_id),"%s",info.id);s_pending_tool=info.kind>=128;
        recover_active();return false;
    }
    if(!pico_app_package_read(path,&info,true,s_error,sizeof(s_error)))return false;
    const pico_app_payload_t *payload=pico_app_package_payload(&info);if(!payload)return error("应用不支持当前设备");
    uint8_t *image=heap_caps_malloc(payload->bytes,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    if(!image)return error("内存不足，请稍后重试");
    FILE *f=fopen(path,"rb");bool ok=f&&fseek(f,(long)payload->offset,SEEK_SET)==0&&fread(image,1,payload->bytes,f)==payload->bytes;if(f)fclose(f);
    if(!ok||pico_app_crc32(image,payload->bytes)!=payload->crc){free(image);return error("应用读取失败，请重新导入");}
    const pico_app_exports_t *app=NULL;void *code=NULL;
    ok=pico_app_code_open(image,payload->bytes,&app,&code,s_error,sizeof(s_error));free(image);if(!ok)return false;
    if(!app||app->abi!=PICO_APP_ABI||app->bytes<PICO_APP_EXPORTS_V1_BYTES||app->kind!=info.kind||app->version!=info.version||!app->id||strcmp(app->id,info.id)||
       !app->start||!app->render||!app->event||!app->key||!app->tick||!app->action||!app->status||!app->save||!app->stop){pico_app_code_close(code);return error("应用接口不兼容");}
    read_pico_sd_info_t media={0};
    if(read_pico_sd_get_info(&media)!=ESP_OK||!media.mounted){pico_app_code_close(code);return error("TF 卡状态已改变");}
    s_active_media_generation=media.media_generation;
    s_active_info=info;s_app=app;s_code=code;s_pinned=false;s_ink_reference=false;s_nav=-1;s_in_call=true;
    if(!app->start(&s_host)){s_home_entry=0xff;s_in_call=false;app->stop();if(!s_worker){pico_app_code_close(code);s_code=NULL;s_app=NULL;}return error("应用启动失败");}
    /* start() already applied a deep link. Drop a leftover so an older package cannot apply it on a later tick. */
    s_home_entry=0xff;
    s_in_call=false;s_nav=-1;
    if(info.kind<128){
        // 游戏统一横屏；主设备同时通知从设备打开同一个游戏。
        // 街机横屏；棋类严格保持双机纵向排布。
        // Arcade is landscape; board games stay in the dual portrait arrangement.
        if(info.kind==PGA_SNAKE)game_rotation();
        else portrait_rotation();
        if(!s_dual_game_applying)(void)ps_dual_game_open((uint8_t)info.kind);
    }
    s_origin=ps_route()==PS_APPS?PS_APPS:info.kind>=128?PS_SHELF:PS_GAMES;
    ps_navigate(app_route(info.kind));return true;
}
static bool open_item(unsigned index) {
    if(index>=s_count)return error("应用暂不可用，请检查 TF 卡");
    char path[160];package_path(path,sizeof(path),s_items[index].info.id);
    return open_package(s_items[index].info,path);
}
bool ps_games_open_id(const char *id) {
    if(!pico_app_id_valid(id)||!ready())return error("应用暂不可用，请检查 TF 卡");
    for(unsigned i=0;i<s_count;i++)if(!strcmp(id,s_items[i].info.id))return open_item(i);
    return error("应用已移除，请刷新应用集");
}
bool ps_apps_open(const char *id) {
    bool armed=s_home_entry_armed&&id&&!strcmp(id,"home-link");
    ps_route_t opened_from=s_home_route_set?s_home_route_hint:ps_route();
    ps_route_t origin=armed?ps_previous_route():ps_route();
    // Deep links first enter a shell page; retain the source before open_package navigates again.
    // 深链接先经过宿主页，须在模块二次导航前保留真正来源，避免应用集返回到书架。
    if(origin==PS_APPLICATION&&home_link_active())origin=s_origin;
    if(origin==PS_HA||origin==PS_HA_SETTINGS||origin==PS_PRINTERS||origin==PS_PRINTER_SETTINGS||origin==PS_APPLICATION)
        origin=PS_SHELF;
    s_home_route_set=false;
    s_home_entry_armed=false;
    if(!armed)s_home_entry=0xff;
    if(armed){s_home_hold=true;s_home_hold_route=opened_from;}
    if(!pico_app_id_valid(id)||!ready()){s_home_entry=0xff;s_home_hold=false;return error("请插入并挂载 TF 卡");}
    char path[160];pico_app_package_t info;
    package_path(path,sizeof(path),id);struct stat conflict;
    if(stat(path,&conflict)==0){s_home_entry=0xff;s_home_hold=false;return error("游戏与工具目录存在同名应用，请先解决冲突");}
    snprintf(path,sizeof(path),PICO_APP_TOOLS_DIRECTORY "/%s.pga",id);
    if(!pico_app_package_read(path,&info,false,s_error,sizeof(s_error))||strcmp(info.id,id)||info.kind<128){s_home_entry=0xff;s_home_hold=false;return error("工具应用不可用，请重新导入");}
    bool opened=open_package(info,path);
    if(opened&&armed)s_origin=origin;
    if(!opened){s_home_entry=0xff;s_home_hold=false;}
    return opened;
}
static void launch_tool(uint16_t id) {
    for(unsigned i=0;i<s_tool_count;i++)if(s_tools[i].navigation==id){if(!ps_apps_open(s_tools[i].id))ps_toast(s_error);return;}
}
void ps_apps_register_navigation(void) {
    if(storage_held()){s_catalog_dirty=true;return;}
    unregister_tools();
    if(!ready())return;
    recover_packages(PICO_APP_TOOLS_DIRECTORY);
    DIR *d=opendir(PICO_APP_TOOLS_DIRECTORY);if(!d)return;
    struct dirent *entry;
    while(s_tool_count<8&&(entry=readdir(d))){
        if(!ends(entry->d_name,".pga")||strlen(entry->d_name)>35)continue;
        char path[192],expected[40],reason[80];pico_app_package_t info;
        snprintf(path,sizeof(path),PICO_APP_TOOLS_DIRECTORY "/%.35s",entry->d_name);
        if(!pico_app_package_read(path,&info,false,reason,sizeof(reason))||info.kind<128||!pico_app_package_payload(&info))continue;
        snprintf(expected,sizeof(expected),"%s.pga",info.id);if(strcmp(expected,entry->d_name))continue;
        char conflicting[160];struct stat st;package_path(conflicting,sizeof(conflicting),info.id);
        if(stat(conflicting,&st)==0)continue;
        bool duplicate=false;for(unsigned i=0;i<s_tool_count;i++)if(!strcmp(s_tools[i].id,info.id))duplicate=true;
        if(duplicate)continue;
        unsigned slot=s_tool_count;snprintf(s_tools[slot].id,32,"%s",info.id);snprintf(s_tools[slot].title,64,"%s",info.title);
        s_tools[slot].navigation=(uint16_t)(PS_NAV_CUSTOM_FIRST+(pico_app_crc32(info.id,strlen(info.id))%65000));
        ps_navigation_entry_t nav={s_tools[slot].navigation,PS_APPLICATION,PI_NOTE,s_tools[slot].title,launch_tool};
        if(ps_navigation_register(&nav))s_tool_count++;
    }
    closedir(d);
}
bool ps_games_install(const char *staged_path,bool replace) {
    const char *prefix="/sdcard/apps/.staging/";
    if(!staged_path||strncmp(staged_path,prefix,strlen(prefix)))return error("应用临时路径无效");
    const char *name=staged_path+strlen(prefix);size_t len=strlen(name);
    if(!len||len>96||strstr(name,"..")||!ends(name,".tmp"))return error("应用临时文件名无效");
    for(size_t i=0;i<len;i++)if(!((name[i]>='a'&&name[i]<='z')||(name[i]>='A'&&name[i]<='Z')||(name[i]>='0'&&name[i]<='9')||name[i]=='.'||name[i]=='-'||name[i]=='_'))return error("应用临时文件名无效");
    if(!ready()||!directory())return error("请先挂载 TF 卡");
    pico_app_package_t info;if(!pico_app_package_read(staged_path,&info,true,s_error,sizeof(s_error)))return false;
    if(!pico_app_package_payload(&info))return error("此应用包没有当前平台的代码");
    bool utility=info.kind>=128;
    if(utility&&mkdir(PICO_APP_TOOLS_DIRECTORY,0700)!=0&&errno!=EEXIST)return error("无法创建工具应用目录");
    const char *destination=utility?PICO_APP_TOOLS_DIRECTORY:PICO_APP_DIRECTORY;
    char path[160],backup[168];snprintf(path,sizeof(path),"%s/%s.pga",destination,info.id);snprintf(backup,sizeof(backup),"%s.bak",path);
    char other[160];snprintf(other,sizeof(other),"%s/%s.pga",utility?PICO_APP_DIRECTORY:PICO_APP_TOOLS_DIRECTORY,info.id);
    struct stat conflict;if(stat(other,&conflict)==0)return error("游戏与工具目录存在同名应用，请先移除冲突包");
    recover_packages(destination);
    struct stat st;bool exists=stat(path,&st)==0;if(exists&&!replace)return error("应用已安装，请确认替换");
    if(s_app&&!strcmp(s_active_info.id,info.id)){
        if(!close_active())return false;
        if(game_route(ps_route()))ps_navigate(utility?PS_SHELF:PS_GAMES);
    }
    pico_app_package_t previous;char reason[160];
    bool valid=exists&&pico_app_package_read(path,&previous,true,reason,sizeof(reason))&&!strcmp(previous.id,info.id);
    if(valid){
        if(unlink(backup)!=0&&errno!=ENOENT)return error("无法准备应用更新");
        if(rename(path,backup)!=0)return error("无法保留原应用");
    }else if(exists&&unlink(path)!=0)return error("无法替换原应用");
    if(rename(staged_path,path)!=0){
        bool recovered=rename(backup,path)==0;
        return error(recovered?"应用安装失败，原版本已恢复":"应用安装失败，请重试");
    }
    if(!pico_app_package_read(path,&previous,true,s_error,sizeof(s_error))||strcmp(previous.id,info.id)){
        (void)unlink(path);(void)rename(backup,path);return error("应用提交校验失败，请重新导入");
    }
    (void)unlink(backup);ps_games_rescan();s_error[0]=0;return true;

}
bool ps_games_remove(const char *id) {
    if(!pico_app_id_valid(id)||!ready())return error("应用无法移除，请检查 TF 卡");
    char path[160],other[160],backup[168];struct stat st;
    package_path(path,sizeof(path),id);snprintf(other,sizeof(other),PICO_APP_TOOLS_DIRECTORY "/%s.pga",id);
    bool game=stat(path,&st)==0,utility=stat(other,&st)==0;
    if(game&&utility)return error("同名应用位于两个目录，请先解决文件冲突");
    if(utility)snprintf(path,sizeof(path),"%s",other);
    if(!game&&!utility)return error("应用文件不存在，请刷新列表");
    if(s_app&&!strcmp(s_active_info.id,id)){
        if(!close_active())return false;
        if(game_route(ps_route()))ps_navigate(utility?PS_SHELF:PS_GAMES);
    }
    snprintf(backup,sizeof(backup),"%s.bak",path);
    if(unlink(backup)!=0&&errno!=ENOENT)return error("无法清理旧应用备份，尚未移除");
    if(unlink(path)!=0)return error("应用移除失败");
    ps_games_rescan();ps_toast("应用已移除，存档已保留");return true;
}
static void card_rect(unsigned slot,int *x,int *y,int *w,int *h) {
    ps_view_t v=ps_view();int cols=v.landscape?3:2;*w=(v.w-32-(cols-1)*10)/cols;*h=v.landscape?82:132;*x=16+(int)(slot%(unsigned)cols)*(*w+10);*y=143+(int)(slot/(unsigned)cols)*(*h+10);
}
static void game_mark(uint8_t *fb,int x,int y,int kind) {
    ps_box(fb,x,y,38,38,11,13,true);
    if(kind==PGA_2048){ps_center(fb,x+19,y+11,13,"2048",2);return;}
    if(kind==PGA_SNAKE){
        ps_box(fb,x+9,y+8,20,7,3,2,true);ps_box(fb,x+9,y+8,7,21,3,2,true);
        ps_box(fb,x+9,y+23,14,7,3,2,true);ps_box(fb,x+23,y+23,7,7,3,5,true);return;
    }
    for(int i=0;i<3;i++){ps_line(fb,x+8+i*11,y+7,x+8+i*11,y+31,10);ps_line(fb,x+7,y+8+i*11,x+31,y+8+i*11,10);}
    ps_box(fb,x+13,y+13,12,12,6,2,true);
    if(kind==PGA_GO)ps_box(fb,x+24,y+24,9,9,5,15,true);
    if(kind==PGA_XIANGQI||kind==PGA_CHESS){ps_box(fb,x+11,y+11,16,16,8,15,true);ps_center(fb,x+19,y+12,12,kind==PGA_XIANGQI?"象":"棋",2);}
}
static void draw_launcher(uint8_t *fb) {
    ps_view_t v=ps_view();char text[128];ps_fill(fb,0,76,v.w,v.h-76,14);ps_tabs(fb,2);ps_text(fb,16,87,25,"小游戏",1);ps_button(fb,v.w-90,87,74,30,"刷新",false);
    snprintf(text,sizeof(text),"TF 应用 %u · 长按移除，保留存档",s_count);ps_text(fb,16,121,11,text,5);
    if(!s_count){ps_wrap(fb,24,190,v.w-48,18,3,"将游戏包放入 TF 卡的 apps/games 目录后点击刷新",2);if(s_error[0])ps_wrap(fb,24,310,v.w-48,13,3,s_error,4);}
    for(unsigned i=s_page*6;i<s_count&&i<(s_page+1)*6;i++){
        int x,y,w,h;card_rect(i%6,&x,&y,&w,&h);pico_app_package_t *p=&s_items[i].info;
        ps_surface(fb,x,y,w,h,false);game_mark(fb,x+12,y+(v.landscape?21:14),p->kind);
        ps_text(fb,x+58,y+(v.landscape?15:19),18,p->title,1);
        if(v.landscape)ps_wrap(fb,x+58,y+44,w-70,10,1,p->description,5);
        else {
            ps_wrap(fb,x+12,y+60,w-24,11,2,p->description,5);
            ps_text(fb,x+12,y+h-27,11,"打开 / 继续",5);
        }
        int ax=x+w-17,ay=y+h-20;
        ps_line(fb,ax-4,ay-4,ax,ay,5);ps_line(fb,ax,ay,ax-4,ay+4,5);
    }
    if(s_count>6){int y=v.h-44;ps_button(fb,16,y,76,28,"上一页",false);ps_button(fb,v.w-92,y,76,28,"下一页",false);snprintf(text,sizeof(text),"%u / %u",s_page+1,(s_count+5)/6);ps_center(fb,v.w/2,y+6,12,text,3);}
    if(s_bad){snprintf(text,sizeof(text),"%u 个包不兼容或损坏，请重新导入",s_bad);ps_center(fb,v.w/2,v.h-68,11,text,4);}
    if(s_remove>=0&&(unsigned)s_remove<s_count){int w=v.w-40;if(w>500)w=500;int x=(v.w-w)/2,y=(v.h-220)/2;ps_box(fb,x,y,w,220,8,15,true);ps_box(fb,x,y,w,220,8,0,false);ps_text(fb,x+20,y+20,20,"移除这个游戏？",0);ps_wrap(fb,x+20,y+65,w-40,13,2,"只移除应用，存档和成绩会保留",4);int bw=(w-52)/2;ps_button(fb,x+20,y+150,bw,42,"取消",false);ps_button(fb,x+32+bw,y+150,bw,42,"移除",true);}
}
static void recovery_rect(int *x,int *y,int *w,int *bw) {
    ps_view_t v=ps_view();*w=v.w-40;if(*w>500)*w=500;*x=(v.w-*w)/2;*y=(v.h-260)/2;*bw=(*w-52)/2;
}
static void render_recovery(uint8_t *fb) {
    ps_view_t v=ps_view();
    if(s_pinned&&!s_discard)ps_button(fb,16,v.h-38,v.w-32,30,"未保存内容 · 点击处理",false);
    if(!s_discard)return;
    int x,y,w,bw;recovery_rect(&x,&y,&w,&bw);
    ps_box(fb,x,y,w,260,8,15,true);ps_box(fb,x,y,w,260,8,0,false);
    ps_text(fb,x+20,y+18,20,"应用尚未保存",0);
    ps_wrap(fb,x+20,y+56,w-40,13,4,"原稿暂留内存，请保持开机。\n检查 TF 卡后重试保存。\n放弃不会删除已有存档。",4);
    ps_button(fb,x+20,y+148,bw,40,"继续保留",false);
    ps_button(fb,x+32+bw,y+148,bw,40,"重试保存",true);
    ps_button(fb,x+20,y+204,w-40,40,"放弃未存内容",false);
}
static void resolve_recovery(bool discard) {
    bool utility=s_active_info.kind>=128,tool=s_pending_tool;int route=s_pending_route;
    char id[sizeof(s_pending_id)];snprintf(id,sizeof(id),"%s",s_pending_id);
    if(!(discard?discard_active():close_active())){ps_toast(s_error);ps_dirty();return;}
    s_discard=false;clear_pending();
    if(id[0]){
        bool opened=false;
        if(tool)opened=ps_apps_open(id);
        else for(unsigned i=0;i<s_count;i++)if(!strcmp(id,s_items[i].info.id)){opened=open_item(i);break;}
        if(opened)return;
        ps_toast(s_error);
    }
    ps_navigate(route>=0?(ps_route_t)route:utility?PS_SHELF:PS_GAMES);
}
void ps_games_init(void) { s_catalog_dirty=true;s_scanned=false;s_tool_count=0; }
static void game_rotation(void) {
    epd_set_rotation(ps_dual_role()==2?EPD_ROT_INVERTED_LANDSCAPE:EPD_ROT_LANDSCAPE);
}
static void portrait_rotation(void) {
    epd_set_rotation(ps_dual_enabled()?(enum EpdRotation)ps_dual_rotation():EPD_ROT_INVERTED_PORTRAIT);
}
void ps_games_enter(ps_route_t route) {
    if(s_in_call)return;
    if(s_home_hold){
        bool same=route==s_home_hold_route;
        bool keep=s_app&&home_link_active()&&(same||game_route(route));
        if(same||!keep)s_home_hold=false;
        if(keep&&same)return;
    }
    // 微信读书配置是活动模块的短暂子页；返回仍保留书架和阅读位置。
    // WeRead setup temporarily opens a child page while retaining the active shelf and reading position.
    if(s_weread_setup&&s_app&&!strcmp(s_active_info.id,"weread")&&(route==PS_WIRELESS_MANAGE||route==PS_WIFI||route==PS_EDITOR))return;
    if(s_weread_edit&&weread_active()&&route==PS_EDITOR)return;
    if(!game_route(route)&&s_app&&s_active_info.kind<128&&!s_dual_game_applying)(void)ps_dual_game_close();
    if(game_route(route)){
        s_weread_edit=false;s_weread_setup=false;
        if(route==PS_ARCADEGAME&&s_app&&s_active_info.kind==PGA_SNAKE)game_rotation();
        else if(route==PS_ARCADEGAME||route==PS_PLAYGAME)portrait_rotation();
    }
    if(!game_route(route)&&s_app&&!close_active()){
        clear_pending();s_pending_route=route<PS_ROUTE_COUNT?route:-1;
        recover_active();return;
    }
    if(route==PS_GAMES)ps_games_rescan();
}
void ps_games_media_lost(void) {
    weread_child_clear();
    if(s_app){s_app->stop();s_pinned=true;}
    unregister_tools();s_count=0;s_scanned=false;s_remove=-1;s_catalog_dirty=false;error("TF 卡已移除，应用暂不可用");
}
bool ps_games_prepare_storage(void) {
    if(close_active())return true;
    clear_pending();recover_active();return false;
}
bool ps_games_storage_save(void) { return !s_app||s_app->save(); }
bool ps_games_power_prepare(void) {
    if(!s_app)return true;
    if(s_in_call)return false;
    // stop()取消并join搜索，保留代码和静态局面；落盘在join之后，不与AI并行读写。
    // stop() cancels/joins search while retaining code/state; persist after joining, never alongside AI.
    if(s_home_portal){s_home_portal=false;ui_wifi_qr_clear();ps_home_portal_stop();}
    s_power_paused=true;s_app->stop();
    if(s_worker||!s_app->save()){
        s_power_paused=false;s_pinned=true;error("应用尚未保存，暂不休眠");clear_pending();recover_active();return false;
    }
    return true;
}
void ps_games_power_resume(void) { s_power_paused=false; }
void ps_games_render(uint8_t *fb) {
    if(game_route(ps_route())&&s_app){s_app->render(fb);ps_view_t v=ps_view();s_ink_reference=true;s_ink_width=v.w;s_ink_height=v.h;}
    else draw_launcher(fb);
    if(s_home_portal&&home_link_active()){
        ps_home_portal_view_t portal;ps_home_portal_view(&portal);
        ps_view_t v=ps_view();int side=v.landscape?180:220,x=(v.w-side)/2,y=v.landscape?88:132;
        ps_fill(fb,0,0,v.w,v.h,15);ps_button(fb,16,20,72,36,"关闭",false);
        ps_text(fb,100,26,20,"手机填写",0);
        if(portal.active&&portal.url[0])ui_wifi_qr_draw(fb,ps_rect(x,y,side,side));
        ps_center(fb,v.w/2,y+side+16,14,portal.active?"扫码打开局域网填写页":(portal.error[0]?portal.error:"门户已关闭"),0);
        ps_wrap(fb,24,y+side+40,v.w-48,13,3,"请连接同一 Wi-Fi。酒店或客户端隔离的热点下，手机可能打不开。保存后配置留在设备上。",4);
        if(portal.seconds_left){char left[32];snprintf(left,sizeof(left),"剩余 %u 秒",(unsigned)portal.seconds_left);ps_center(fb,v.w/2,v.h-28,12,left,5);}
    }
    if(s_weread_link&&weread_active()){
        ps_view_t v=ps_view();int side=v.landscape?196:248,x=(v.w-side)/2,y=v.landscape?94:144;
        ps_fill(fb,0,0,v.w,v.h,15);ps_button(fb,16,20,58,32,"返回",false);
        ps_text(fb,88,25,20,"在微信读书阅读",0);
        ui_wifi_qr_draw(fb,ps_rect(x,y,side,side));
        ps_center(fb,v.w/2,v.landscape?304:420,14,"用手机扫码打开官方阅读器",0);
        if(!v.landscape)ps_wrap(fb,28,466,v.w-56,14,3,"正文在微信读书中阅读，\n阅读范围由账户与图书权限决定。",4);
    }
    render_recovery(fb);
}
bool ps_games_event(const ps_event_t *event) {
    if(s_home_portal){
        if(event->type==PS_TAP||event->type==PS_SWIPE_R){home_portal_dismiss();ps_dirty();}
        return true;
    }
    if(s_weread_link){
        if(event->type==PS_TAP||event->type==PS_SWIPE_R){s_weread_link=false;ui_wifi_qr_clear();ps_dirty();}
        return true;
    }
    if(s_discard){
        int x,y,w,bw;recovery_rect(&x,&y,&w,&bw);
        if(event->type==PS_PRESS)s_recovery_outside=!ps_hit(event->x,event->y,x,y,w,260);
        if(event->type==PS_TAP){
            if(s_recovery_outside&&ps_hit(event->x,event->y,x,y,w,260))return true;
            if(!ps_hit(event->x,event->y,x,y,w,260)||ps_hit(event->x,event->y,x+20,y+148,bw,40))retain_active();
            else if(ps_hit(event->x,event->y,x+32+bw,y+148,bw,40))resolve_recovery(false);
            else if(ps_hit(event->x,event->y,x+20,y+204,w-40,40))resolve_recovery(true);
        }
        return true;
    }
    ps_view_t v=ps_view();
    if(s_pinned&&event->type==PS_TAP&&ps_hit(event->x,event->y,16,v.h-38,v.w-32,30)){clear_pending();recover_active();return true;}
    if(ps_route()==PS_GAMES){
        if(s_remove>=0){if(event->type==PS_TAP){int w=v.w-40;if(w>500)w=500;int x=(v.w-w)/2,y=(v.h-220)/2,bw=(w-52)/2;if(!ps_hit(event->x,event->y,x,y,w,220)||ps_hit(event->x,event->y,x+20,y+150,bw,42))s_remove=-1;else if(ps_hit(event->x,event->y,x+32+bw,y+150,bw,42)){char id[32];snprintf(id,sizeof(id),"%s",s_items[s_remove].info.id);s_remove=-1;if(!ps_games_remove(id))ps_toast(s_error);}ps_dirty();}return true;}
        if(event->type==PS_TAP&&ps_hit(event->x,event->y,v.w-90,87,74,30)){ps_games_rescan();return true;}
        // 上下滑动越过阈值即翻页；底栏点击保持原范围。左右滑动不在此消费。
        // Vertical drags turn the page once they clear the threshold; the bottom bar keeps its hit area. Horizontal swipes are not consumed here.
        {
            bool consume=false;
            int d=ps_scroll_step(&games_scroll,event->type,event->x0,event->y0,event->x,event->y,PS_SCROLL_AXIS_Y,&consume);
            bool bar=!consume&&event->type==PS_TAP&&event->y>=v.h-44;
            if(bar) d=event->x>v.w/2?1:-1;
            if(d||consume||bar){
                if(d){int page=(int)s_page+d;if(page>=0&&(unsigned)page<(s_count+5)/6){s_page=(unsigned)page;ps_dirty();}}
                return true;
            }
        }
        if(event->type!=PS_TAP&&event->type!=PS_LONG)return false;
        for(unsigned i=s_page*6;i<s_count&&i<(s_page+1)*6;i++){int x,y,w,h;card_rect(i%6,&x,&y,&w,&h);if(ps_hit(event->x,event->y,x,y,w,h)){if(event->type==PS_LONG)s_remove=(int)i;else if(!open_item(i))ps_toast(s_error);ps_dirty();return true;}}
        return false;
    }
    if(!s_app||s_pinned||s_power_paused||!same_card())return false;
    pico_app_event_t e={event->type,event->x0,event->y0,event->x,event->y,event->ms};s_in_call=true;bool changed=s_app->event(&e);finish_call();return changed;
}
bool ps_games_key(int key) {
    if(s_home_portal){if(key==-1){home_portal_dismiss();ps_dirty();}return true;}
    if(s_weread_link){if(key==-1){s_weread_link=false;ui_wifi_qr_clear();ps_dirty();}return true;}
    if(key==-1&&s_discard){retain_active();return true;}
    if(key==-1&&s_pinned){clear_pending();recover_active();return true;}
    if(ps_route()==PS_GAMES){if(key==-1&&s_remove>=0){s_remove=-1;ps_dirty();return true;}return false;}
    if(!s_app||s_pinned||s_power_paused||!same_card())return false;
    s_in_call=true;bool changed=s_app->key(key);finish_call();return changed;
}
bool ps_games_tick(void) {
    uint32_t dual_request=0,dual_session=0;bool dual_result=false;
    if(ps_dual_game_open_result_take(&dual_request,&dual_result)&&!dual_result)
        ps_toast("另一台无法打开同一游戏");
    if(ps_dual_game_close_take(&dual_session,&dual_request)){
        s_dual_game_applying=true;
        if(game_route(ps_route()))ps_navigate(PS_GAMES);
        s_dual_game_applying=false;
        ps_dual_game_close_finish(dual_request,!game_route(ps_route()));
        return true;
    }
    // 从机可能先进入游戏，此时主机仍在书架且目录尚未扫描；先就绪目录，
    // 未取走 BLE 打开请求，下一轮继续处理，不能过早 NACK。
    // The peer may enter first while this unit is still on the shelf; prepare
    // the catalog before consuming the BLE request so it is not NACKed early.
    if(ps_dual_game_open_pending()&&!storage_held()&&ps_background_io_allowed())ps_games_rescan();
    uint8_t open_kind=0;
    if(s_count>0&&ps_dual_game_open_take(&open_kind,&dual_session,&dual_request)){
        bool opened=false;
        for(int attempt=0;attempt<2&&!opened;attempt++){
            for(unsigned i=0;i<s_count;i++)if(s_items[i].info.kind==open_kind){
                s_dual_game_applying=true;
                opened=open_item(i);
                if(!opened)ps_toast(s_error);
                s_dual_game_applying=false;
                break;
            }
            if(!opened&&attempt==0)ps_games_rescan();
        }
        ps_dual_game_open_finish(dual_request,dual_session,opened);
        return true;
    }
    if(s_catalog_dirty&&!storage_held()&&ps_background_io_allowed()){ps_games_rescan();return true;}
    if(s_home_portal)return ps_home_portal_tick();
    if(!s_app||!game_route(ps_route())||s_pinned||s_discard||s_power_paused||s_weread_link||!same_card())return false;
    if(s_active_info.kind>=128&&!ps_background_io_allowed())return false;
    s_in_call=true;bool changed=s_app->tick();finish_call();return changed;
}
void ps_games_action(int action) { if(s_app&&!s_pinned&&!s_discard&&!s_power_paused&&same_card()&&game_route(ps_route())){s_in_call=true;s_app->action(action);finish_call();} }
bool ps_games_is_arcade(void) { return s_app&&(s_app->status()&PGA_STATUS_ARCADE); }
bool ps_games_is_snake(void) { return s_app&&(s_app->status()&PGA_STATUS_SNAKE); }
bool ps_games_paused(void) { return s_app&&(s_app->status()&PGA_STATUS_PAUSED); }
bool ps_apps_ink_mode(void) {
    return ps_route()==PS_APPLICATION&&s_app&&!s_pinned&&!s_discard&&!s_power_paused&&s_app->bytes>=sizeof(*s_app)&&s_app->ink&&
        (s_app->status()&PGA_STATUS_INK);
}
void ps_apps_ink_invalidate(void) { s_ink_reference=false; }
void ps_apps_ink_cancel(void) {
    if(s_in_call||!ps_apps_ink_mode())return;
    pico_app_event_t e={.type=PGA_CANCEL};s_in_call=true;(void)s_app->event(&e);s_in_call=false;
}
bool ps_apps_ink_render_partial(uint8_t *fb,EpdRect areas[4],int *count) {
    *count=0;ps_view_t v=ps_view();
    if(!ps_apps_ink_mode()||!s_ink_reference||v.w!=s_ink_width||v.h!=s_ink_height||!same_card())return false;
    int32_t boxes[4][4];int32_t n=s_app->ink(fb,boxes);
    if(n<0||n>4){s_ink_reference=false;return false;}
    for(int32_t i=0;i<n;i++){
        if(boxes[i][0]<0||boxes[i][1]<0||boxes[i][0]>=v.w||boxes[i][1]>=v.h||boxes[i][2]<=0||boxes[i][3]<=0||boxes[i][2]>v.w-boxes[i][0]||boxes[i][3]>v.h-boxes[i][1]){s_ink_reference=false;return false;}
        areas[i]=(EpdRect){boxes[i][0],boxes[i][1],boxes[i][2],boxes[i][3]};
    }
    *count=n;return true;
}

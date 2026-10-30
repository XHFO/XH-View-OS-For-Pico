/* SPDX-License-Identifier: Apache-2.0
 * 中文：实际时间页的日历、时区、RTC写回、失败处理和横竖布局回归。
 * English: Actual datetime page regression: calendar, timezone, RTC writes, failures and both layouts.
 */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#ifndef PICO_DATETIME_NATIVE_PREVIEW_TEST
#define PICO_DATETIME_HOST_TEST
#endif
#include "../main/system/pico_datetime.c"

static ps_view_t test_view={360,640,false};
static ps_route_t test_route=PS_DATETIME;
static bool pmu_ready=true,fail_sync,fail_get,null_snapshot,system_clock_ok=true,clock_valid=true,store_ok=true,have_saved;
static pmu_snapshot_t snapshot;
static int readback_delta;
static uint32_t requested_utc,anchored_utc,host_utc;
static unsigned sync_calls,get_calls,anchor_calls,host_clock_calls,store_calls,draw_calls,dirty_calls,checks;
static int64_t current_epoch=1706744520,system_epoch;
static int64_t test_now;
static bool network_online,ntp_start_ok=true;
static unsigned ntp_starts,ntp_stops;
static const char *ntp_server;
static datetime_config_t saved_config;

ps_view_t ps_view(void){return test_view;}
ps_route_t ps_route(void){return test_route;}
bool ps_load(const char *key,void *out,size_t bytes){assert(!strcmp(key,"datetime_v1")&&bytes==sizeof(saved_config));if(!have_saved)return false;memcpy(out,&saved_config,bytes);return true;}
bool ps_store(const char *key,const void *data,size_t bytes){assert(!strcmp(key,"datetime_v1")&&bytes==sizeof(saved_config));store_calls++;if(!store_ok)return false;memcpy(&saved_config,data,bytes);have_saved=true;return true;}
bool ps_wall_time(struct tm *out){if(!clock_valid)return false;time_t t=(time_t)(current_epoch+s_offset*60);return gmtime_r(&t,out)!=NULL;}
void ps_clock_set_utc(uint32_t utc){anchor_calls++;anchored_utc=utc;current_epoch=utc;clock_valid=true;}
bool read_pico_pmu_ready(void){return pmu_ready;}
esp_err_t read_pico_pmu_cmd(uint16_t command,const uint8_t *data,uint8_t bytes){
    assert(command==PMU_CMD_TIME_SYNC||command==PMU_CMD_TIME_GET);
    if(command==PMU_CMD_TIME_SYNC){
        assert(bytes==4&&data);sync_calls++;if(fail_sync)return ESP_FAIL;
        requested_utc=(uint32_t)data[0]|(uint32_t)data[1]<<8|(uint32_t)data[2]<<16|(uint32_t)data[3]<<24;
    }else{
        assert(!bytes&&!data);get_calls++;if(fail_get)return ESP_FAIL;
        snapshot.unix_sec=(uint32_t)((int64_t)requested_utc+readback_delta);
    }
    return ESP_OK;
}
const pmu_snapshot_t *read_pico_pmu_get(void){return null_snapshot?NULL:&snapshot;}
bool datetime_test_set_clock(uint32_t utc){host_clock_calls++;host_utc=utc;if(system_clock_ok)system_epoch=utc;return system_clock_ok;}
int64_t datetime_test_get_clock(void){return system_epoch;}
int64_t ps_now(void){return test_now;}
bool datetime_test_network_online(void){return network_online;}
bool datetime_test_ntp_start(const char *server){ntp_starts++;ntp_server=server;return ntp_start_ok;}
void datetime_test_ntp_stop(void){ntp_stops++;}
void ps_dirty(void){dirty_calls++;}
bool ps_hit(int x,int y,int rx,int ry,int rw,int rh){return x>=rx&&y>=ry&&x<rx+rw&&y<ry+rh;}
static void rect(int x,int y,int w,int h){assert(x>=0&&y>=0&&w>0&&h>0&&x+w<=test_view.w&&y+h<=test_view.h);draw_calls++;}
void ps_heading(uint8_t *fb,const char *a,const char *b){(void)fb;(void)b;assert(a);}
void ps_box(uint8_t *fb,int x,int y,int w,int h,int radius,int gray,bool filled){(void)fb;(void)radius;(void)gray;(void)filled;rect(x,y,w,h);}
void ps_button(uint8_t *fb,int x,int y,int w,int h,const char *label,bool selected){(void)fb;(void)selected;assert(label);rect(x,y,w,h);}
void ps_text_fixed(uint8_t *fb,int x,int y,int size,const char *label,int gray){(void)fb;(void)gray;assert(label);rect(x,y,1,size);}
void ps_center(uint8_t *fb,int x,int y,int size,const char *label,int gray){ps_text_fixed(fb,x,y,size,label,gray);}
void ps_wrap(uint8_t *fb,int x,int y,int width,int size,int lines,const char *label,int gray){(void)fb;(void)gray;assert(label);rect(x,y,width,size*lines);}
static void reset(void){
    s_initialized=false;s_offset=480;s_result[0]=0;s_display_minute=-1;
    s_clock_source=s_ntp_state=0;s_ntp_started=s_ntp_accepting=false;
    s_pending_utc=s_last_ntp_utc=0;s_pending_ms=s_service_at=s_ntp_next=s_ntp_deadline=s_commit_at=0;s_ntp_server=0;
    test_now=0;network_online=false;ntp_start_ok=true;ntp_starts=ntp_stops=0;ntp_server=NULL;
    pmu_ready=true;fail_sync=fail_get=null_snapshot=false;system_clock_ok=clock_valid=store_ok=true;have_saved=false;readback_delta=0;
    snapshot=(pmu_snapshot_t){.time_ok=true,.time_synced=1,.last_op_status=PMU_STATUS_OK};
    requested_utc=anchored_utc=host_utc=0;sync_calls=get_calls=anchor_calls=host_clock_calls=store_calls=0;
    test_view=(ps_view_t){360,640,false};test_route=PS_DATETIME;current_epoch=1706715720;system_epoch=0;
}
static void tap_rect(EpdRect r){ps_event_t event={.type=PS_TAP,.x=r.x+r.width/2,.y=r.y+r.height/2};assert(ps_datetime_event(&event));checks++;}
static void calendar(void){
    uint32_t utc;
    assert(ps_datetime_local_to_utc(2000,1,1,0,0,0,&utc)&&utc==946684800u);checks++;
    assert(ps_datetime_local_to_utc(2000,1,1,8,0,480,&utc)&&utc==946684800u);checks++;
    assert(!ps_datetime_local_to_utc(2000,1,1,7,59,480,&utc)&&!utc);checks++;
    assert(ps_datetime_local_to_utc(2099,12,31,23,59,0,&utc)&&utc==4102444740u);checks++;
    assert(!ps_datetime_local_to_utc(2099,12,31,12,0,-720,&utc));checks++;
    const int invalid[][6]={{1999,1,1,0,0,0},{2100,1,1,0,0,0},{2023,2,29,12,0,0},{2024,2,30,12,0,0},
        {2024,0,1,0,0,0},{2024,13,1,0,0,0},{2024,1,0,0,0,0},{2024,1,32,0,0,0},
        {2024,1,1,-1,0,0},{2024,1,1,24,0,0},{2024,1,1,0,-1,0},{2024,1,1,0,60,0},
        {2024,1,1,0,0,-735},{2024,1,1,0,0,855},{2024,1,1,0,0,1}};
    for(size_t i=0;i<sizeof(invalid)/sizeof(invalid[0]);i++){const int *v=invalid[i];utc=1;assert(!ps_datetime_local_to_utc(v[0],v[1],v[2],v[3],v[4],v[5],&utc)&&!utc);checks++;}
    assert(!ps_datetime_local_to_utc(2024,1,1,0,0,0,NULL));checks++;
    for(int year=2000;year<=2099;year++)for(int month=1;month<=12;month++)for(int day=1;day<=days_in_month(year,month);day++){
        assert(ps_datetime_local_to_utc(year,month,day,12,34,480,&utc));
        time_t local=(time_t)utc+480*60;struct tm decoded;assert(gmtime_r(&local,&decoded));
        assert(decoded.tm_year+1900==year&&decoded.tm_mon+1==month&&decoded.tm_mday==day&&decoded.tm_hour==12&&decoded.tm_min==34);checks++;
    }
    for(int offset=-720;offset<=840;offset+=15){
        assert(ps_datetime_local_to_utc(2024,2,29,0,0,offset,&utc));
        time_t local=(time_t)utc+offset*60;struct tm decoded;assert(gmtime_r(&local,&decoded));
        assert(decoded.tm_year==124&&decoded.tm_mon==1&&decoded.tm_mday==29&&decoded.tm_hour==0&&decoded.tm_min==0);checks++;
    }
}
static void rtc_transaction(void){
#ifdef PICO_DATETIME_NATIVE_PREVIEW_TEST
    reset();system_clock_ok=false;assert(ps_datetime_apply_utc(1700000000)&&!host_clock_calls&&anchor_calls==1&&anchored_utc==1700000000);checks++;
    reset();fail_get=true;assert(!ps_datetime_apply_utc(1700000000)&&!host_clock_calls&&!anchor_calls);checks++;
#else
    reset();assert(ps_datetime_apply_utc(1700000000));assert(sync_calls==1&&get_calls==1&&host_clock_calls==1&&anchor_calls==1&&requested_utc==1700000000&&host_utc==1700000000&&anchored_utc==1700000000);checks++;
    reset();readback_delta=2;assert(ps_datetime_apply_utc(1700000000)&&anchored_utc==1700000002);checks++;
    reset();assert(!ps_datetime_apply_utc(0)&&!sync_calls&&!anchor_calls);checks++;
    reset();pmu_ready=false;assert(!ps_datetime_apply_utc(1700000000)&&!sync_calls&&!anchor_calls);checks++;
    reset();fail_sync=true;assert(!ps_datetime_apply_utc(1700000000)&&sync_calls==1&&!get_calls&&!anchor_calls);checks++;
    reset();fail_get=true;assert(!ps_datetime_apply_utc(1700000000)&&sync_calls==1&&get_calls==1&&!host_clock_calls&&!anchor_calls&&strstr(ps_datetime_last_result(),"读回"));checks++;
    for(int failure=0;failure<6;failure++){
        reset();if(failure==0)readback_delta=-1;else if(failure==1)readback_delta=6;else if(failure==2)null_snapshot=true;
        else if(failure==3)snapshot.time_ok=false;else if(failure==4)snapshot.time_synced=0;else snapshot.last_op_status=1;
        assert(!ps_datetime_apply_utc(1700000000)&&!host_clock_calls&&!anchor_calls&&ps_datetime_last_result()[0]);checks++;
    }
    reset();system_clock_ok=false;assert(!ps_datetime_apply_utc(1700000000)&&host_clock_calls==1&&anchor_calls==1&&anchored_utc==1700000000&&strstr(ps_datetime_last_result(),"系统时间"));checks++;
#endif
}
static void rtc_bridge(void){
    const uint32_t utc=1792382400u;
    const struct {uint32_t utc;bool valid,synced;} invalid[]={
        {utc,false,true},{utc,true,false},{0,true,true},{946684799u,true,true},{4102444800u,true,true}
    };
    for(size_t i=0;i<sizeof(invalid)/sizeof(invalid[0]);i++){
        reset();assert(!ps_datetime_sync_from_rtc(invalid[i].utc,invalid[i].valid,invalid[i].synced));
        assert(!host_clock_calls&&!anchor_calls&&!sync_calls&&!get_calls);checks++;
    }
#ifdef PICO_DATETIME_NATIVE_PREVIEW_TEST
    reset();system_clock_ok=false;
    assert(ps_datetime_sync_from_rtc(utc,true,true)&&anchor_calls==1&&anchored_utc==utc&&!host_clock_calls&&system_epoch==0);checks++;
    assert(ps_datetime_sync_from_rtc(utc+5,true,true)&&anchored_utc==utc+5&&!host_clock_calls&&system_epoch==0);checks++;
#else
    reset();assert(ps_datetime_sync_from_rtc(utc,true,true));
    assert(system_epoch==utc&&host_clock_calls==1&&anchor_calls==1&&!sync_calls&&!get_calls);checks++;
    system_epoch+=5;assert(ps_datetime_sync_from_rtc(utc+5,true,true)&&host_clock_calls==1&&anchor_calls==2);checks++;
    for(int delta=-5;delta<=5;delta++){
        system_epoch=(int64_t)utc+delta;
        assert(ps_datetime_sync_from_rtc(utc,true,true)&&host_clock_calls==1&&system_epoch==(int64_t)utc+delta);checks++;
    }
    system_epoch=utc+6;assert(ps_datetime_sync_from_rtc(utc,true,true)&&host_clock_calls==2&&system_epoch==utc);checks++;
    system_epoch=0;assert(ps_datetime_sync_from_rtc(utc+10,true,true)&&host_clock_calls==3&&system_epoch==utc+10);checks++;
    reset();system_epoch=4102444800LL;assert(ps_datetime_sync_from_rtc(utc,true,true)&&host_clock_calls==1&&system_epoch==utc);checks++;
    reset();system_clock_ok=false;assert(!ps_datetime_sync_from_rtc(utc,true,true));
    assert(host_clock_calls==1&&anchor_calls==1&&anchored_utc==utc&&system_epoch==0);checks++;
    system_clock_ok=true;assert(ps_datetime_sync_from_rtc(utc+5,true,true)&&host_clock_calls==2&&system_epoch==utc+5&&!sync_calls&&!get_calls);checks++;
    reset();system_epoch=1700000000;assert(ps_datetime_apply_utc(1700000000)&&host_clock_calls==1);checks++;
#endif
}
static void page_and_preferences(void){
    for(int landscape=0;landscape<2;landscape++){
        reset();test_view=landscape?(ps_view_t){640,360,true}:(ps_view_t){360,640,false};
        assert(ps_datetime_timezone_minutes()==480);ps_datetime_enter();assert(s_draft.value[DT_YEAR]==2024&&s_draft.value[DT_MONTH]==1&&s_draft.value[DT_DAY]==31);checks++;
        ps_datetime_render(NULL);assert(!sync_calls&&!store_calls&&!host_clock_calls);checks++;
        tap_rect(adjust_rect(DT_MONTH,true));assert(s_draft.value[DT_MONTH]==2&&s_draft.value[DT_DAY]==29);checks++;
        tap_rect(adjust_rect(DT_YEAR,true));assert(s_draft.value[DT_YEAR]==2025&&s_draft.value[DT_DAY]==28);checks++;
        tap_rect(field_rect(DT_MINUTE));assert(ps_datetime_key(2));assert(ps_datetime_key(0));assert(!ps_datetime_key(1)&&!ps_datetime_key(3));checks++;
        assert(!sync_calls&&!store_calls);checks++;
        EpdRect zone=zone_rect();tap_rect((EpdRect){zone.x+zone.width-40,zone.y,40,zone.height});assert(s_draft.offset==495&&s_offset==480);checks++;
        uint32_t expected;assert(ps_datetime_local_to_utc(s_draft.value[0],s_draft.value[1],s_draft.value[2],s_draft.value[3],s_draft.value[4],495,&expected));
        tap_rect(save_rect());assert(requested_utc==expected&&anchored_utc==expected&&s_offset==495&&saved_config.offset==495&&store_calls==1&&!s_draft.modified);checks++;
        s_initialized=false;s_offset=480;assert(ps_datetime_timezone_minutes()==495);checks++;
        s_draft.offset=-720;tap_rect((EpdRect){zone.x,zone.y,40,zone.height});assert(s_draft.offset==840);checks++;
        tap_rect((EpdRect){zone.x+zone.width-40,zone.y,40,zone.height});assert(s_draft.offset==-720);checks++;
        s_draft.offset=480;store_ok=false;tap_rect(save_rect());assert(s_draft.modified&&strstr(ps_datetime_last_result(),"时区保存失败"));checks++;
        unsigned before=sync_calls;s_draft.value[DT_MONTH]=2;s_draft.value[DT_DAY]=31;tap_rect(save_rect());assert(sync_calls==before&&strstr(ps_datetime_last_result(),"日期"));checks++;
        ps_event_t ignored={.type=PS_MOVE};assert(!ps_datetime_event(&ignored)&&!ps_datetime_event(NULL));checks++;
    }
    reset();have_saved=true;saved_config=(datetime_config_t){1,495};assert(ps_datetime_timezone_minutes()==495);checks++;
    reset();have_saved=true;saved_config=(datetime_config_t){1,1000};assert(ps_datetime_timezone_minutes()==480);checks++;
    reset();have_saved=true;saved_config=(datetime_config_t){2,0};assert(ps_datetime_timezone_minutes()==480);checks++;
    reset();clock_valid=false;ps_datetime_enter();assert(s_draft.value[0]>=2000&&s_draft.value[0]<=2099);ps_datetime_render(NULL);assert(!sync_calls);checks++;
}
static void refresh_guard(void){
    reset();ps_datetime_enter();assert(ps_datetime_tick());assert(!ps_datetime_tick());current_epoch++;assert(!ps_datetime_tick());current_epoch+=60;assert(ps_datetime_tick());checks++;
    test_route=PS_SHELF;current_epoch+=60;assert(!ps_datetime_tick());assert(!sync_calls&&!store_calls&&!host_clock_calls);checks++;
}
static void network_sync(void){
    reset();ps_datetime_service_tick(true);assert(!ntp_starts&&!sync_calls&&s_ntp_state==PS_NTP_OFFLINE);checks++;
#ifdef PICO_DATETIME_NATIVE_PREVIEW_TEST
    network_online=true;
    for(int i=1;i<=10;i++){test_now=i*60000;ps_datetime_service_tick(true);}
    assert(!ntp_starts&&!ntp_stops&&!sync_calls&&!host_clock_calls&&!anchor_calls&&s_ntp_state==PS_NTP_OFFLINE);checks++;
#else
    const uint32_t utc=1792382400u;
    network_online=true;test_now=1000;ps_datetime_service_tick(true);
    assert(ntp_starts==1&&s_ntp_state==PS_NTP_SYNCING&&!strcmp(ntp_server,"ntp.aliyun.com"));checks++;
    ps_datetime_service_tick(true);test_now=1999;ps_datetime_service_tick(true);assert(ntp_starts==1);checks++;
    ntp_received(0);ntp_received(1699999999u);ntp_received(4102444800u);
    assert(!s_pending_utc&&!sync_calls&&!get_calls&&!anchor_calls&&!host_clock_calls);checks++;
    ntp_received(utc);
    assert(s_pending_utc==utc&&!sync_calls&&!get_calls&&!anchor_calls&&!host_clock_calls);checks++;
    assert(!ps_datetime_sync_from_rtc(utc-86400,true,true)&&!anchor_calls&&!host_clock_calls);checks++;
    test_now=2000;ps_datetime_service_tick(false);
    assert(ntp_stops==1&&s_pending_utc==utc&&s_ntp_state==PS_NTP_SAVING&&!sync_calls);checks++;
    ntp_received(utc+900);assert(s_pending_utc==utc);checks++;
    test_now=4000;ps_datetime_service_tick(true);
    assert(sync_calls==1&&get_calls==1&&requested_utc==utc+2&&anchored_utc==utc+2&&system_epoch==utc+2&&!s_pending_utc);checks++;
    ps_datetime_status_t status;ps_datetime_get_status(&status);ps_datetime_get_status(NULL);
    assert(status.source==PS_TIME_NETWORK&&status.ntp_state==PS_NTP_SYNCED&&!status.ntp_pending&&status.last_ntp_utc==utc+2);checks++;
    test_now=3603999;ps_datetime_service_tick(true);assert(ntp_starts==1);checks++;
    test_now=3605000;ps_datetime_service_tick(true);assert(ntp_starts==2&&s_ntp_state==PS_NTP_SYNCING);checks++;
    network_online=false;test_now+=1000;ps_datetime_service_tick(true);
    assert(ntp_stops==2&&s_ntp_state==PS_NTP_OFFLINE);ntp_received(utc+900);assert(!s_pending_utc);checks++;
    // 所有RTC事务和系统钟失败都保留样本，五秒后用经过时间重试。
    // RTC transaction/system-clock failures retain the sample for elapsed-time-adjusted retries after five seconds.
    for(int fault=0;fault<5;fault++){
        reset();network_online=true;ps_datetime_service_tick(true);ntp_received(utc);
        if(fault==0)pmu_ready=false;else if(fault==1)fail_sync=true;else if(fault==2)fail_get=true;
        else if(fault==3)readback_delta=6;else system_clock_ok=false;
        test_now=1000;ps_datetime_service_tick(true);unsigned before=sync_calls;
        assert(s_pending_utc==utc&&s_ntp_state==PS_NTP_SAVING&&s_clock_source!=PS_TIME_RTC);checks++;
        assert(!ps_datetime_sync_from_rtc(utc-999,true,true));checks++;
        test_now=5000;ps_datetime_service_tick(true);assert(sync_calls==before);checks++;
        pmu_ready=true;fail_sync=fail_get=false;readback_delta=0;system_clock_ok=true;
        test_now=6000;ps_datetime_service_tick(true);
        assert(!s_pending_utc&&s_ntp_state==PS_NTP_SYNCED&&requested_utc==utc+6&&anchored_utc==utc+6&&system_epoch==utc+6);checks++;
    }
    reset();network_online=true;ps_datetime_service_tick(true);
    test_now=29999;ps_datetime_service_tick(true);assert(ntp_stops==0&&ntp_starts==1);checks++;
    test_now=31000;ps_datetime_service_tick(true);assert(ntp_stops==1&&s_ntp_state==PS_NTP_RETRY);checks++;
    test_now=90000;ps_datetime_service_tick(true);assert(ntp_starts==1);checks++;
    test_now=91000;ps_datetime_service_tick(true);assert(ntp_starts==2&&!strcmp(ntp_server,"time.cloudflare.com"));checks++;
    test_now=121000;ps_datetime_service_tick(true);test_now=181000;ps_datetime_service_tick(true);
    assert(ntp_starts==3&&!strcmp(ntp_server,"pool.ntp.org"));checks++;
    reset();network_online=true;ntp_start_ok=false;ps_datetime_service_tick(true);
    assert(ntp_starts==1&&!s_ntp_started&&!s_ntp_accepting&&s_ntp_state==PS_NTP_RETRY);checks++;
    ntp_received(utc);assert(!s_pending_utc);ntp_start_ok=true;test_now=60000;ps_datetime_service_tick(true);
    assert(ntp_starts==2&&!strcmp(ntp_server,"time.cloudflare.com"));checks++;
    ntp_received(utc);assert(ps_datetime_apply_utc(utc+600));
    assert(!s_pending_utc&&!s_ntp_started&&ntp_stops==1&&s_clock_source==PS_TIME_MANUAL);checks++;
    test_now+=1000;ps_datetime_service_tick(true);ntp_received(utc+900);assert(ntp_starts==2&&!s_pending_utc&&system_epoch==utc+600);checks++;
    reset();assert(ps_datetime_apply_utc(utc)&&s_clock_source==PS_TIME_MANUAL&&!ntp_starts);checks++;
    // 离线后已到达的有效样本仍可提交，断网不破坏RTC事务。
    // A valid sample already received can commit offline; disconnecting does not discard the RTC transaction.
    reset();network_online=true;ps_datetime_service_tick(true);ntp_received(utc);network_online=false;
    test_now=2000;ps_datetime_service_tick(true);assert(!s_pending_utc&&system_epoch==utc+2&&ntp_stops==1);checks++;
    // 刚到达的回调时间可能略先于本轮主循环快照，不把负经过时间减入UTC。
    // A just-arrived callback can lead the main-loop snapshot; negative elapsed time must not subtract UTC seconds.
    reset();network_online=true;ps_datetime_service_tick(true);test_now=3000;ntp_received(utc);
    test_now=1000;ps_datetime_service_tick(true);assert(!s_pending_utc&&requested_utc==utc&&system_epoch==utc);checks++;
#endif
}
int main(void){calendar();rtc_transaction();rtc_bridge();page_and_preferences();refresh_guard();network_sync();printf("Datetime: %u checks PASS; real-source RTC/calendar/NTP/failure/layout tests, %u paint calls\n",checks,draw_calls);return 0;}

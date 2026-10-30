/* SPDX-License-Identifier: Apache-2.0
 * 中文：独立时间设置页、合法日期换算与PMU RTC校时事务。
 * English: Independent date/time page, calendar validation and PMU RTC synchronization.
 * 冻结：绘制/时间页tick不校时；硬件轮询可将有效已同步RTC桥接到ESP系统钟，误差5秒内不重设。
 * Frozen: Rendering/datetime ticks never set clocks; hardware polling may bridge valid synchronized RTC to ESP time, leaving differences within five seconds unchanged.
 * 冻结：手动校时和RTC桥接共用系统钟入口；预览只更新界面锚点，绝不修改电脑时间、电源或VCOM。
 * Frozen: Manual changes and RTC bridging share one system-clock boundary; preview only anchors its UI, never host time, power or VCOM.
 * 冻结：Wi-Fi自动授时只在主循环校验写回RTC；网络回调仅缓存样本，预览不发起真实NTP。
 * Frozen: Wi-Fi auto-sync verifies RTC commits on the main loop; network callbacks only cache samples and preview never starts real NTP.
 */
#ifdef PICO_DATETIME_HOST_TEST
#include "pico_datetime_host_env.h"
#else
#include "pico_datetime.h"
#include "read_pico_pmu.h"
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/time.h>
#ifdef ESP_PLATFORM
#include "esp_netif_sntp.h"
#include "esp_sntp.h"
#include "esp_timer.h"
#include "read_pico_connectivity.h"
#include "freertos/FreeRTOS.h"
#endif

enum { DT_YEAR,DT_MONTH,DT_DAY,DT_HOUR,DT_MINUTE,DT_FIELDS };
enum { DT_FIRST_UTC=946684800u,DT_LAST_UTC=4102444799u };
typedef struct {uint32_t version;int32_t offset;} datetime_config_t;
typedef struct {int value[DT_FIELDS],offset,selected;bool modified;} datetime_draft_t;
static bool s_initialized;
static int s_offset=480;
static datetime_draft_t s_draft;
static char s_result[128];
static int64_t s_display_minute=-1;
static uint8_t s_clock_source,s_ntp_state;
static bool s_ntp_started,s_ntp_accepting;
static uint32_t s_pending_utc,s_last_ntp_utc;
static int64_t s_pending_ms,s_service_at,s_ntp_next,s_ntp_deadline,s_commit_at;
static unsigned s_ntp_server;
#ifdef ESP_PLATFORM
static portMUX_TYPE s_time_lock=portMUX_INITIALIZER_UNLOCKED;
#define TIME_LOCK() portENTER_CRITICAL(&s_time_lock)
#define TIME_UNLOCK() portEXIT_CRITICAL(&s_time_lock)
#else
#define TIME_LOCK() ((void)0)
#define TIME_UNLOCK() ((void)0)
#endif
static void ntp_stop(void){
    TIME_LOCK();s_ntp_accepting=false;TIME_UNLOCK();
    if(!s_ntp_started)return;
#ifdef PICO_DATETIME_HOST_TEST
    datetime_test_ntp_stop();
#elif defined(ESP_PLATFORM)
    esp_netif_sntp_deinit();
#endif
    s_ntp_started=false;
}
#if defined(ESP_PLATFORM) || defined(PICO_DATETIME_HOST_TEST)
static void ntp_received(uint32_t utc){
    if(utc<1700000000u||utc>DT_LAST_UTC)return;
#ifdef ESP_PLATFORM
    int64_t now=esp_timer_get_time()/1000;
#else
    int64_t now=ps_now();
#endif
    TIME_LOCK();
    if(s_ntp_accepting){s_pending_utc=utc;s_pending_ms=now;}
    TIME_UNLOCK();
}
#endif
#ifdef ESP_PLATFORM
// 覆盖IDF弱回调：网络线程只发布样本，校时与PMU事务留给主循环。
// Override the IDF weak hook: the network thread only publishes a sample; the main loop owns clock/PMU commits.
void sntp_sync_time(struct timeval *value){
    if(value&&value->tv_sec>=1700000000LL&&value->tv_sec<=DT_LAST_UTC)ntp_received((uint32_t)value->tv_sec);
}
#endif
static bool ntp_start(void){
    static const char *const servers[]={"ntp.aliyun.com","time.cloudflare.com","pool.ntp.org"};
    TIME_LOCK();s_ntp_accepting=true;TIME_UNLOCK();
    bool ok=false;
#ifdef PICO_DATETIME_HOST_TEST
    ok=datetime_test_ntp_start(servers[s_ntp_server%3]);
#elif defined(ESP_PLATFORM)
    esp_sntp_config_t config=ESP_NETIF_SNTP_DEFAULT_CONFIG(servers[s_ntp_server%3]);
    config.wait_for_sync=false;
    ok=esp_netif_sntp_init(&config)==ESP_OK;
#else
    (void)servers;
#endif
    s_ntp_started=ok;
    if(!ok){TIME_LOCK();s_ntp_accepting=false;TIME_UNLOCK();}
    return ok;
}
static bool ntp_pending(void){TIME_LOCK();bool pending=s_pending_utc!=0;TIME_UNLOCK();return pending;}
void ps_datetime_get_status(ps_datetime_status_t *out){
    if(out)*out=(ps_datetime_status_t){.source=s_clock_source,.ntp_state=s_ntp_state,.ntp_pending=ntp_pending(),.last_ntp_utc=s_last_ntp_utc};
}

static bool offset_valid(int offset){return offset>=-720&&offset<=840&&offset%15==0;}
static bool leap(int year){return year%4==0&&(year%100!=0||year%400==0);}
static int days_in_month(int year,int month){
    static const int days[]={31,28,31,30,31,30,31,31,30,31,30,31};
    return month>=1&&month<=12?days[month-1]+(month==2&&leap(year)):0;
}
bool ps_datetime_local_to_utc(int year,int month,int day,int hour,int minute,int offset,uint32_t *utc){
    if(utc)*utc=0;
    if(!utc||year<2000||year>2099||month<1||month>12||day<1||day>days_in_month(year,month)||
       hour<0||hour>23||minute<0||minute>59||!offset_valid(offset))return false;
    int64_t days=0;
    for(int y=1970;y<year;y++)days+=365+leap(y);
    for(int m=1;m<month;m++)days+=days_in_month(year,m);
    days+=day-1;
    int64_t seconds=days*86400+hour*3600+minute*60-(int64_t)offset*60;
    if(seconds<DT_FIRST_UTC||seconds>DT_LAST_UTC)return false;
    *utc=(uint32_t)seconds;return true;
}
void ps_datetime_init(void){
    if(s_initialized)return;
    datetime_config_t config;
    if(ps_load("datetime_v1",&config,sizeof(config))&&config.version==1&&offset_valid(config.offset))s_offset=config.offset;
    s_initialized=true;
}
int ps_datetime_timezone_minutes(void){ps_datetime_init();return s_offset;}
const char *ps_datetime_last_result(void){return s_result;}
static bool outcome(const char *text,bool success){snprintf(s_result,sizeof(s_result),"%s",text);return success;}
static bool update_clocks(uint32_t utc,bool force){
    bool system_ok=true;
#if defined(PICO_DATETIME_HOST_TEST) || defined(ESP_PLATFORM)
#ifdef PICO_DATETIME_HOST_TEST
    int64_t current=datetime_test_get_clock();
#else
    int64_t current=(int64_t)time(NULL);
#endif
    int64_t delta=current-(int64_t)utc;
    if(force||current<DT_FIRST_UTC||current>DT_LAST_UTC||delta < -5||delta > 5){
#ifdef PICO_DATETIME_HOST_TEST
        system_ok=datetime_test_set_clock(utc);
#else
        struct timeval value={.tv_sec=(time_t)utc,.tv_usec=0};
        system_ok=settimeofday(&value,NULL)==0;
#endif
    }
#else
    (void)force;
#endif
    // RTC已校验；系统钟失败仍保留界面锚点，下次硬件轮询可重试。
    // Preserve the verified RTC UI anchor even if system time fails; the next hardware poll can retry.
    ps_clock_set_utc(utc);
    return system_ok;
}
bool ps_datetime_sync_from_rtc(uint32_t utc,bool valid,bool synced){
    if(!valid||!synced||utc<DT_FIRST_UTC||utc>DT_LAST_UTC||ntp_pending())return false;
    bool ok=update_clocks(utc,false);
    if(!s_clock_source)s_clock_source=PS_TIME_RTC;
    return ok;
}
static bool apply_utc(uint32_t utc,bool manual){
    if(utc<DT_FIRST_UTC||utc>DT_LAST_UTC)return outcome("时间超出RTC支持范围",false);
    if(!read_pico_pmu_ready())return outcome("RTC尚未就绪，请稍后重试",false);
    uint8_t payload[4];for(unsigned i=0;i<4;i++)payload[i]=(uint8_t)(utc>>(i*8));
    if(read_pico_pmu_cmd(PMU_CMD_TIME_SYNC,payload,sizeof(payload))!=ESP_OK)return outcome("RTC写入失败，请重试",false);
    if(read_pico_pmu_cmd(PMU_CMD_TIME_GET,NULL,0)!=ESP_OK)return outcome("RTC已写入，读回校验失败",false);
    const pmu_snapshot_t *snapshot=read_pico_pmu_get();
    int64_t delta=snapshot?(int64_t)snapshot->unix_sec-utc:INT64_MAX;
    if(!snapshot||!snapshot->time_ok||!snapshot->time_synced||snapshot->last_op_status!=PMU_STATUS_OK||
       snapshot->unix_sec<DT_FIRST_UTC||snapshot->unix_sec>DT_LAST_UTC||delta<0||delta>5)
        return outcome("RTC读回时间不一致，请重试",false);
    uint32_t verified=snapshot->unix_sec;
    if(manual){
        ntp_stop();TIME_LOCK();s_pending_utc=0;TIME_UNLOCK();
        s_ntp_next=ps_now()+3600000;s_ntp_state=PS_NTP_WAITING;
    }
    bool system_ok=update_clocks(verified,true);
    s_clock_source=manual?PS_TIME_MANUAL:PS_TIME_NETWORK;
    return outcome(system_ok?"时间已保存到RTC":"RTC已更新，系统时间同步失败",system_ok);
}
bool ps_datetime_apply_utc(uint32_t utc){return apply_utc(utc,true);}
void ps_datetime_service_tick(bool allow_commit){
    int64_t now=ps_now();
    if(s_service_at&&now<s_service_at)return;
    s_service_at=now+1000;
    bool online=false;
#ifdef PICO_DATETIME_HOST_TEST
    online=datetime_test_network_online();
#elif defined(ESP_PLATFORM)
    online=read_pico_connectivity_online();
#endif
    TIME_LOCK();uint32_t pending=s_pending_utc;int64_t received=s_pending_ms;TIME_UNLOCK();
    if(pending){
        ntp_stop();s_ntp_state=PS_NTP_SAVING;
        if(!allow_commit||now<s_commit_at)return;
        s_commit_at=now+5000;
        int64_t elapsed=now>=received?now-received:0;
        int64_t utc=(int64_t)pending+elapsed/1000;
        if(utc>=1700000000LL&&utc<=DT_LAST_UTC&&apply_utc((uint32_t)utc,false)){
            TIME_LOCK();s_pending_utc=0;TIME_UNLOCK();
            s_last_ntp_utc=(uint32_t)utc;s_ntp_next=now+3600000;s_ntp_state=PS_NTP_SYNCED;s_ntp_server=0;
        }
        return;
    }
    if(!online){ntp_stop();s_ntp_state=PS_NTP_OFFLINE;return;}
    if(s_ntp_started){
        if(now<s_ntp_deadline)return;
        ntp_stop();s_ntp_server++;s_ntp_next=now+60000;s_ntp_state=PS_NTP_RETRY;
        return;
    }
    if(now<s_ntp_next){if(s_ntp_state==PS_NTP_OFFLINE)s_ntp_state=s_last_ntp_utc?PS_NTP_SYNCED:PS_NTP_WAITING;return;}
    if(ntp_start()){s_ntp_deadline=now+30000;s_ntp_state=PS_NTP_SYNCING;}
    else{s_ntp_server++;s_ntp_next=now+60000;s_ntp_state=PS_NTP_RETRY;}
}

static void initial_date(struct tm *out){
    if(ps_wall_time(out)&&out->tm_year>=100&&out->tm_year<=199)return;
    memset(out,0,sizeof(*out));
    const char build_date[]=__DATE__,build_time[]=__TIME__;
    char month[4];memcpy(month,build_date,3);month[3]=0;
    static const char months[]="JanFebMarAprMayJunJulAugSepOctNovDec";
    const char *found=strstr(months,month);
    out->tm_year=atoi(build_date+7)-1900;out->tm_mon=found?(int)(found-months)/3:0;
    out->tm_mday=atoi(build_date+4);out->tm_hour=atoi(build_time);out->tm_min=atoi(build_time+3);
    if(out->tm_year<100||out->tm_year>199)out->tm_year=100;
}
void ps_datetime_enter(void){
    ps_datetime_init();struct tm now;initial_date(&now);
    s_draft=(datetime_draft_t){.value={now.tm_year+1900,now.tm_mon+1,now.tm_mday,now.tm_hour,now.tm_min},.offset=s_offset};
    s_result[0]=0;s_display_minute=-1;
}
static EpdRect field_rect(int field){
    ps_view_t v=ps_view();
    int columns=v.landscape?5:field<DT_HOUR?3:2;
    int gap=v.landscape||field<DT_HOUR?8:12;
    int index=v.landscape?field:field<DT_HOUR?field:field-DT_HOUR;
    int width=(v.w-32-(columns-1)*gap)/columns;
    return (EpdRect){16+index*(width+gap),v.landscape?120:field<DT_HOUR?162:304,width,v.landscape?100:126};
}
static EpdRect zone_rect(void){ps_view_t v=ps_view();return(EpdRect){16,v.landscape?248:464,v.landscape?v.w/2-24:v.w-32,v.landscape?36:40};}
static EpdRect save_rect(void){ps_view_t v=ps_view();return(EpdRect){v.landscape?v.w/2+8:16,v.landscape?248:526,v.landscape?v.w/2-24:v.w-32,v.landscape?36:42};}
static EpdRect adjust_rect(int field,bool plus){
    EpdRect r=field_rect(field);int gap=6,width=(r.width-18)/2;
    return(EpdRect){r.x+6+(plus?width+gap:0),r.y+(ps_view().landscape?64:84),width,ps_view().landscape?30:34};
}
static void zone_text(int offset,char *text,size_t bytes){int absolute=offset<0?-offset:offset;snprintf(text,bytes,"UTC%c%02d:%02d",offset<0?'-':'+',absolute/60,absolute%60);}
void ps_datetime_render(uint8_t *fb){
    ps_view_t v=ps_view();ps_heading(fb,"日期与时间",NULL);
    struct tm now;char current[80];
    if(ps_wall_time(&now))snprintf(current,sizeof(current),"当前  %04d-%02d-%02d  %02d:%02d",now.tm_year+1900,now.tm_mon+1,now.tm_mday,now.tm_hour,now.tm_min);
    else snprintf(current,sizeof(current),"时间尚未校准，请手动设置");
    ps_text_fixed(fb,16,v.landscape?82:84,v.landscape?14:16,current,3);
    if(!v.landscape)ps_text_fixed(fb,16,120,11,"调整后保存，不会随输入立即改动时钟",5);
    static const char *const labels[]={"年","月","日","时","分"};
    for(int field=0;field<DT_FIELDS;field++){
        EpdRect r=field_rect(field);ps_box(fb,r.x,r.y,r.width,r.height,5,field==s_draft.selected?8:12,false);
        ps_center(fb,r.x+r.width/2,r.y+8,12,labels[field],5);
        char value[12];snprintf(value,sizeof(value),field==DT_YEAR?"%04d":"%02d",s_draft.value[field]);
        ps_center(fb,r.x+r.width/2,r.y+(v.landscape?30:38),field==DT_YEAR?20:24,value,1);
        EpdRect minus=adjust_rect(field,false),plus=adjust_rect(field,true);
        ps_button(fb,minus.x,minus.y,minus.width,minus.height,"−",false);
        ps_button(fb,plus.x,plus.y,plus.width,plus.height,"+",false);
    }
    EpdRect zone=zone_rect();char timezone[24];zone_text(s_draft.offset,timezone,sizeof(timezone));
    ps_text_fixed(fb,zone.x,zone.y-20,11,"本地时区 · 每次15分钟",5);
    ps_button(fb,zone.x,zone.y,40,zone.height,"−",false);
    ps_center(fb,zone.x+zone.width/2,zone.y+10,15,timezone,s_draft.offset==s_offset?3:1);
    ps_button(fb,zone.x+zone.width-40,zone.y,40,zone.height,"+",false);
    EpdRect save=save_rect();ps_button(fb,save.x,save.y,save.width,save.height,"保存日期与时间",s_draft.modified);
    uint32_t utc;bool valid=ps_datetime_local_to_utc(s_draft.value[0],s_draft.value[1],s_draft.value[2],s_draft.value[3],s_draft.value[4],s_draft.offset,&utc);
    const char *message=s_result[0]?s_result:valid?"保存后写入RTC，断开Wi-Fi仍可走时":"日期或时区超出RTC支持范围";
    if(v.landscape)ps_text_fixed(fb,16,306,11,message,valid?5:1);
    else ps_wrap(fb,16,584,v.w-32,12,2,message,valid?5:1);
}
static void adjust_field(int field,int direction){
    const int minimum[]={2000,1,1,0,0},maximum[]={2099,12,31,23,59};
    int max=field==DT_DAY?days_in_month(s_draft.value[DT_YEAR],s_draft.value[DT_MONTH]):maximum[field];
    int value=s_draft.value[field]+direction;
    if(value<minimum[field])value=max;else if(value>max)value=minimum[field];
    s_draft.value[field]=value;s_draft.selected=field;s_draft.modified=true;s_result[0]=0;
    // 月/年切换时收紧日，避免把1月31日保留成无效的2月31日。
    // Clamp the day after month/year changes instead of leaving January 31 as an invalid February 31.
    int days=days_in_month(s_draft.value[DT_YEAR],s_draft.value[DT_MONTH]);
    if(s_draft.value[DT_DAY]>days)s_draft.value[DT_DAY]=days;
}
static void save_draft(void){
    uint32_t utc;
    if(!ps_datetime_local_to_utc(s_draft.value[0],s_draft.value[1],s_draft.value[2],s_draft.value[3],s_draft.value[4],s_draft.offset,&utc)){
        outcome("日期或时区超出RTC支持范围",false);return;
    }
    if(!ps_datetime_apply_utc(utc))return;
    s_offset=s_draft.offset;datetime_config_t config={.version=1,.offset=s_offset};
    bool saved=ps_store("datetime_v1",&config,sizeof(config));
    s_draft.modified=!saved;
    if(!saved)outcome("时间已更新，时区保存失败，请重试",false);
    ps_dirty();
}
bool ps_datetime_event(const ps_event_t *event){
    if(!event||event->type!=PS_TAP)return false;
    for(int field=0;field<DT_FIELDS;field++){
        EpdRect minus=adjust_rect(field,false),plus=adjust_rect(field,true),r=field_rect(field);
        if(ps_hit(event->x,event->y,minus.x,minus.y,minus.width,minus.height)){adjust_field(field,-1);return true;}
        if(ps_hit(event->x,event->y,plus.x,plus.y,plus.width,plus.height)){adjust_field(field,1);return true;}
        if(ps_hit(event->x,event->y,r.x,r.y,r.width,r.height)){s_draft.selected=field;return true;}
    }
    EpdRect zone=zone_rect();
    if(ps_hit(event->x,event->y,zone.x,zone.y,zone.width,zone.height)){
        if(event->x<zone.x+40)s_draft.offset=s_draft.offset<=-720?840:s_draft.offset-15;
        else if(event->x>=zone.x+zone.width-40)s_draft.offset=s_draft.offset>=840?-720:s_draft.offset+15;
        else return true;
        s_draft.modified=true;s_result[0]=0;return true;
    }
    EpdRect save=save_rect();
    if(ps_hit(event->x,event->y,save.x,save.y,save.width,save.height)){save_draft();return true;}
    return false;
}
bool ps_datetime_key(int key){
    if(key!=0&&key!=2)return false;
    adjust_field(s_draft.selected,key==0?-1:1);return true;
}
bool ps_datetime_tick(void){
    if(ps_route()!=PS_DATETIME)return false;
    struct tm now;int64_t minute=-2;
    if(ps_wall_time(&now))minute=(int64_t)(now.tm_year+1900)*527040+(now.tm_yday*24+now.tm_hour)*60+now.tm_min;
    bool changed=minute!=s_display_minute;s_display_minute=minute;return changed;
}

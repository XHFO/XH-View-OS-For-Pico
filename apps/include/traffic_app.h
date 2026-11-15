/* SPDX-License-Identifier: Apache-2.0
 * 中文：网站访问看板完整TF应用；配置、解析、布局及快照均在包内，宿主只提供TLS与配对门户。
 * English: Full TF analytics app owns configuration, parsing, layout and snapshots; the host provides TLS and paired setup only.
 */
#include <stdatomic.h>
static unsigned traffic_length(const char *s){unsigned n=0;while(s&&s[n])n++;return n;}
#include "traffic_json.h"
#include "traffic_ui.h"
#define TRAFFIC_MAGIC 0x32414643u
typedef struct {uint64_t requests,bytes,cached;char date[11];} traffic_day_t;
typedef struct {uint64_t requests;char country[32];} traffic_country_t;
typedef struct {
    uint32_t magic,revision,source,style,interval;
    char label[64],zone[33],token[256];
    int64_t updated;
    traffic_day_t days[7];traffic_country_t countries[5];
} traffic_record_t;
static traffic_record_t traffic_record,traffic_draft;
static char traffic_response[32768],traffic_query[1024],traffic_message[128];
static void *traffic_worker;
static atomic_int traffic_phase;
static int traffic_http;
static bool traffic_settings,traffic_full;
static int64_t traffic_due;
static bool traffic_same(const char *a,const char *b){return traffic_length(a)==traffic_length(b)&&!H->compare(a,b,traffic_length(a));}
static void traffic_note(const char *text){H->format(traffic_message,sizeof(traffic_message),"%s",text);}
static bool traffic_interval(uint32_t n){return n==300||n==900||n==1800||n==3600;}
static bool traffic_id(const char *s){if(traffic_length(s)!=32)return false;for(unsigned i=0;i<32;i++)if(wrj_hex(s[i])>15)return false;return true;}
static bool traffic_token(const char *s){unsigned n=traffic_length(s);if(!n||n>255)return false;for(unsigned i=0;i<n;i++){int c=s[i];if(!((c>='A'&&c<='Z')||(c>='a'&&c<='z')||(c>='0'&&c<='9')||c=='_'||c=='-'))return false;}return true;}
static bool traffic_valid_record(const traffic_record_t *r){
    if(r->magic!=TRAFFIC_MAGIC||!r->revision||r->style>2||r->source>1||!traffic_interval(r->interval)||r->label[63]||r->zone[32]||r->token[255])return false;
    if(r->token[0]&&(!traffic_id(r->zone)||!traffic_token(r->token)))return false;
    for(unsigned i=0;i<7;i++)if(r->days[i].date[10])return false;
    for(unsigned i=0;i<5;i++)if(r->countries[i].country[31])return false;
    return true;
}
static void traffic_default(traffic_record_t *r){H->set(r,0,sizeof(*r));r->magic=TRAFFIC_MAGIC;r->revision=1;r->interval=900;}
// UTC公历转换不依赖模块libc或设备时区。/ UTC civil dates require neither module libc nor the device time zone.
static void traffic_date(int64_t epoch,char date[11]){
    int64_t z=epoch/86400+719468,era=z/146097;unsigned doe=(unsigned)(z-era*146097),yoe=(doe-doe/1460+doe/36524-doe/146096)/365;
    int y=(int)yoe+(int)era*400;unsigned doy=doe-(365*yoe+yoe/4-yoe/100),mp=(5*doy+2)/153,d=doy-(153*mp+2)/5+1,m=mp+(mp<10?3:-9);y+=m<=2;
    H->format(date,11,"%04d-%02u-%02u",y,m,d);
}
static bool traffic_store(const traffic_record_t *r){return H->store("cloudflare2",r,sizeof(*r));}
static void traffic_worker_fn(void *unused){
    (void)unused;
    traffic_http=H->traffic_fetch(traffic_query,traffic_draft.token,traffic_response,sizeof(traffic_response));
    atomic_store_explicit(&traffic_phase,2,memory_order_release);
    H->worker_suspend();
}
static bool traffic_launch(bool validating){
    if(atomic_load(&traffic_phase)||traffic_worker)return false;
    int64_t now=H->utc_seconds();
    if(now<1704067200){traffic_note("设备时间未校准，请先设置日期与时间。");return false;}
    char from[11],to[11];traffic_date(now-6*86400,from);traffic_date(now,to);
    H->format(traffic_query,sizeof(traffic_query),"{\"query\":\"{viewer{zones(filter:{zoneTag:\\\"%s\\\"}){httpRequests1dGroups(limit:7,orderBy:[date_ASC],filter:{date_geq:\\\"%s\\\",date_leq:\\\"%s\\\"}){dimensions{date}sum{requests bytes cachedRequests countryMap{clientCountryName requests}}}}}}\"}",traffic_draft.zone,from,to);
    H->set(traffic_response,0,sizeof(traffic_response));
    atomic_store(&traffic_phase,1);
    if(!H->worker_create(traffic_worker_fn,NULL,&traffic_worker)){atomic_store(&traffic_phase,0);traffic_note("内存不足，请关闭其他任务后重试。");return false;}
    traffic_note(validating?"正在验证站点与令牌，成功后保存…":"正在更新，继续显示最近数据…");return true;
}
static bool traffic_unsigned(wrj_token_t t,uint64_t *out){
    if(!t.end||t.end-t.start>19)return false;
    uint64_t n=0;for(unsigned i=t.start;i<t.end;i++){int c=wrj_char(i);if(c<'0'||c>'9'||n>(UINT64_MAX-(unsigned)(c-'0'))/10)return false;n=n*10+(unsigned)(c-'0');}*out=n;return true;
}
static wrj_token_t traffic_first(wrj_token_t a){return a.type=='['&&wrj_space(a.start+1)<a.end-1?wrj_token(a.start+1,1):(wrj_token_t){0};}
static bool traffic_response_parse(void){
    wrj_begin(traffic_response,traffic_length(traffic_response));wrj_token_t root=wrj_token(0,0),errors=wrj_field(root,"errors");
    if(wrj_failed||root.type!='{'||wrj_space(root.end)!=wrj_bytes)return false;
    if(errors.end&&!(errors.type=='n'&&errors.end-errors.start==4&&!H->compare(traffic_response+errors.start,"null",4))&&!(errors.type=='['&&!traffic_first(errors).end))return false;
    wrj_token_t data=wrj_field(root,"data"),viewer=wrj_field(data,"viewer"),zones=wrj_field(viewer,"zones"),zone=traffic_first(zones);
    if(zone.type!='{')return false;
    wrj_token_t groups=wrj_field(zone,"httpRequests1dGroups");if(groups.type!='[')return false;
    H->set(traffic_draft.days,0,sizeof(traffic_draft.days));H->set(traffic_draft.countries,0,sizeof(traffic_draft.countries));
    int64_t now=H->utc_seconds();for(unsigned i=0;i<7;i++)traffic_date(now-(6-i)*86400,traffic_draft.days[i].date);
    uint32_t at=wrj_space(groups.start+1);unsigned count=0;bool seen[7]={0};
    while(at<groups.end-1){
        if(++count>7)return false;
        wrj_token_t group=wrj_token(at,1),dim=wrj_field(group,"dimensions"),sum=wrj_field(group,"sum");char date[11];
        if(!wrj_string(wrj_field(dim,"date"),date,sizeof(date)))return false;
        unsigned slot=7;for(unsigned i=0;i<7;i++)if(traffic_same(date,traffic_draft.days[i].date))slot=i;
        if(slot==7||seen[slot])return false;
        seen[slot]=true;
        traffic_day_t *day=&traffic_draft.days[slot];
        if(!traffic_unsigned(wrj_field(sum,"requests"),&day->requests)||!traffic_unsigned(wrj_field(sum,"bytes"),&day->bytes)||!traffic_unsigned(wrj_field(sum,"cachedRequests"),&day->cached)||day->cached>day->requests)return false;
        if(slot==6){
            wrj_token_t countries=wrj_field(sum,"countryMap");if(countries.type!='[')return false;
            unsigned ca=wrj_space(countries.start+1),total=0;
            while(ca<countries.end-1){
                if(++total>256)return false;
                wrj_token_t country=wrj_token(ca,2);traffic_country_t value={0};
                if(!wrj_string(wrj_field(country,"clientCountryName"),value.country,sizeof(value.country))||!traffic_unsigned(wrj_field(country,"requests"),&value.requests))return false;
                for(unsigned i=0;i<5;i++)if(value.requests>traffic_draft.countries[i].requests){for(unsigned j=4;j>i;j--)traffic_draft.countries[j]=traffic_draft.countries[j-1];traffic_draft.countries[i]=value;break;}
                ca=wrj_space(country.end);if(wrj_char(ca)==',')ca=wrj_space(ca+1);else break;
            }
        }
        at=wrj_space(group.end);if(wrj_char(at)==',')at=wrj_space(at+1);else break;
    }
    if(wrj_failed)return false;
    traffic_draft.updated=now;return true;
}
static bool traffic_poll(void){
    if(atomic_load_explicit(&traffic_phase,memory_order_acquire)!=2)return false;
    H->worker_delete(traffic_worker);traffic_worker=NULL;atomic_store(&traffic_phase,0);
    bool ok=traffic_http==200&&traffic_response_parse();
    if(ok){
        traffic_draft.revision=traffic_record.revision+1;
        if(!traffic_store(&traffic_draft)){ok=false;traffic_note("TF 保存失败，原配置与最近数据已保留。");}
        else {traffic_record=traffic_draft;traffic_note("更新成功 · 今日数据可能存在统计延迟");}
    }else traffic_note(traffic_http==401||traffic_http==403?"授权失败，请检查令牌和区域分析读取权限。":traffic_http==-2?"设备时间未校准，请先设置日期与时间。":traffic_http==-3?"响应超过 32 KB，保留最近数据。":traffic_http==429?"Cloudflare 请求限流，请稍后重试。":traffic_http==200?"站点不可用或分析权限不支持，保留原配置。":"网络请求失败，保留原配置与最近数据。");
    H->set(traffic_response,0,sizeof(traffic_response));H->set(traffic_draft.token,0,sizeof(traffic_draft.token));
    traffic_due=H->time_us()+(int64_t)(ok?traffic_record.interval:300)*1000000;
    return true;
}
static bool start(const pico_app_host_t *host){
    if(!host||host->abi!=1||host->bytes<offsetof(pico_app_host_t,board_model_abi)+sizeof(host->board_model_abi)||host->board_model_abi!=PICO_BOARD_MODEL_ABI)return false;
    H=host;traffic_full=host->bytes>=offsetof(pico_app_host_t,traffic_snapshot)+sizeof(host->traffic_snapshot)&&host->traffic_fetch&&host->traffic_portal&&host->utc_seconds&&host->traffic_snapshot&&host->worker_create&&host->worker_delete&&host->worker_suspend;
    if(!traffic_full)return true;
    traffic_default(&traffic_record);
    if(H->load("cloudflare2",&traffic_draft,sizeof(traffic_draft))&&traffic_valid_record(&traffic_draft))traffic_record=traffic_draft;
    H->set(&traffic_draft,0,sizeof(traffic_draft));traffic_settings=false;traffic_worker=NULL;atomic_store(&traffic_phase,0);traffic_due=0;
    if(traffic_record.source)H->traffic_snapshot(NULL);
    traffic_note(traffic_record.token[0]?"已配置 · 打开看板后自动刷新":"首次使用：设置 → 手机扫码配置 Cloudflare");return true;
}
static void traffic_number(char out[40],uint64_t n){
    if(n>=1000000000)H->format(out,40,"%llu.%lluB",(unsigned long long)(n/1000000000),(unsigned long long)(n/100000000%10));
    else if(n>=1000000)H->format(out,40,"%llu.%lluM",(unsigned long long)(n/1000000),(unsigned long long)(n/100000%10));
    else if(n>=10000)H->format(out,40,"%llu.%lluK",(unsigned long long)(n/1000),(unsigned long long)(n/100%10));
    else H->format(out,40,"%llu",(unsigned long long)n);
}
static void traffic_metric(uint8_t *fb,int x,int y,int w,int h,const char *title,const char *value){H->box(fb,x,y,w,h,10,0,false);H->text(fb,x+14,y+12,14,title,4);H->text(fb,x+14,y+38,w<120?20:26,value,0);}
static void traffic_chart(uint8_t *fb,int x,int y,int w,int h){
    H->text(fb,x,y,15,"近七天 · 请求次数（UTC）",0);int top=y+30,bottom=y+h-26;uint64_t max=1;
    for(unsigned i=0;i<7;i++)if(traffic_record.days[i].requests>max)max=traffic_record.days[i].requests;
    H->line(fb,x,bottom,x+w,bottom,5);
    for(unsigned i=0;i<7;i++){int step=w/7,bw=step/2,bh=(int)((long double)traffic_record.days[i].requests/max*(bottom-top));H->fill(fb,x+(int)i*step+(step-bw)/2,bottom-bh,bw,bh,i==6?0:7);H->text(fb,x+(int)i*step+2,bottom+9,11,traffic_record.days[i].date+5,4);}
}
static void render(uint8_t *fb){
    if(!traffic_full)return;
    pico_app_view_t v=H->view();int w=v.w,h=v.h;
    H->fill(fb,0,0,w,h,15);
    if(traffic_settings){
        H->heading(fb,"看板设置","数据源与外观 · 保存在 TF 卡");
        H->button(fb,16,116,w-32,44,"手机扫码配置数据源",false);
        H->text(fb,20,174,14,traffic_record.source?"当前：多屏灵犀电脑快照":traffic_record.token[0]?traffic_record.label:"尚未配置 Cloudflare",4);
        int gap=8,bw=(w-32-2*gap)/3;static const char *const styles[]={"概览","趋势","极简"};
        for(int i=0;i<3;i++)H->button(fb,16+i*(bw+gap),208,bw,44,styles[i],traffic_record.style==(unsigned)i);
        H->wrap(fb,20,272,w-40,14,3,"直连模式由设备独立更新。扫码页可修改站点、令牌、刷新频率及数据来源。",4);
        H->wrap(fb,20,v.landscape?h-24:372,w-40,13,3,traffic_message,4);
        return;
    }
    if(traffic_record.source){if(!H->traffic_snapshot(fb)){H->heading(fb,"网站访问看板","多屏灵犀电脑快照");H->text(fb,24,132,24,"尚无电脑快照",0);H->wrap(fb,24,183,w-48,16,4,"请在多屏灵犀中编辑并推送看板，或在设置中切换为 Cloudflare 直连。",4);}H->button(fb,w-86,30,70,36,"设置",false);return;}
    H->heading(fb,"网站访问看板",traffic_record.token[0]?traffic_record.label:"Cloudflare · 由设备独立更新");
    H->button(fb,w-86,30,70,36,"设置",false);
    if(!traffic_record.updated){H->text(fb,24,132,24,"让网站数据留在桌面",0);H->wrap(fb,24,183,w-48,16,4,"在设置中扫码填写 Cloudflare 区域 ID 与只读令牌，选择喜欢的看板样式。",4);H->button(fb,24,v.landscape?276:320,w-48,48,"设置数据源",false);return;}
    traffic_day_t *today=&traffic_record.days[6];char requests[40],bandwidth[40],cached[40];traffic_number(requests,today->requests);
    uint64_t mib=today->bytes/1048576;H->format(bandwidth,sizeof(bandwidth),"%llu MB",(unsigned long long)mib);
    H->format(cached,sizeof(cached),"%u%%",today->requests?(unsigned)((long double)today->cached*100/today->requests):0);
    if(traffic_record.style==2){
        H->text(fb,24,116,16,"今日请求 · UTC",4);H->text(fb,24,154,v.landscape?64:54,requests,0);
        H->line(fb,24,244,w-24,244,0);H->text(fb,24,264,18,"带宽",4);H->text(fb,w/2,264,22,bandwidth,0);
        if(!v.landscape){H->text(fb,24,316,18,"缓存命中",4);H->text(fb,w/2,316,22,cached,0);}
    }else{
        int gap=10,mw=(w-32-2*gap)/3;
        if(traffic_record.style==0){traffic_metric(fb,16,112,mw,86,"今日请求",requests);traffic_metric(fb,16+mw+gap,112,mw,86,"带宽",bandwidth);traffic_metric(fb,16+2*(mw+gap),112,mw,86,"缓存命中",cached);}
        int y=traffic_record.style==0?216:112,ch=v.landscape?h-y-60:210;
        traffic_chart(fb,20,y,w-40,ch);
        if(!v.landscape){int cy=y+ch+24;H->text(fb,20,cy,15,"今日访问地区 · 请求数",0);for(unsigned i=0;i<3;i++)if(traffic_record.countries[i].country[0]){char n[40];traffic_number(n,traffic_record.countries[i].requests);H->text(fb,20,cy+30+(int)i*29,15,traffic_record.countries[i].country,4);H->text(fb,w-100,cy+30+(int)i*29,15,n,0);}}
    }
    char date[11],info[112];traffic_date(traffic_record.updated,date);H->format(info,sizeof(info),"更新于 %s %02u:%02u UTC%s",date,(unsigned)(traffic_record.updated%86400/3600),(unsigned)(traffic_record.updated%3600/60),atomic_load(&traffic_phase)?" · 更新中":"");
    H->wrap(fb,20,h-62,w-40,12,1,traffic_message,4);
    H->text(fb,20,h-38,12,info,4);
}
static bool event(const pico_app_event_t *e){
    if(!traffic_full||e->type!=PGA_TAP)return false;
    pico_app_view_t v=H->view();
    if(traffic_settings){
        if(e->y>=116&&e->y<160){if(!H->traffic_portal(true))traffic_note("请连接 Wi-Fi，并关闭其他手机管理窗口后重试。");return true;}
        if(e->y>=208&&e->y<252){int bw=(v.w-48)/3,index=(e->x-16)/(bw+8);if(e->x>=16&&index>=0&&index<3){if(atomic_load(&traffic_phase)){traffic_note("正在验证，请完成后切换样式。");return true;}traffic_draft=traffic_record;traffic_draft.style=(unsigned)index;traffic_draft.revision++;if(traffic_store(&traffic_draft)){traffic_record=traffic_draft;traffic_note("显示样式已保存");}else traffic_note("TF 保存失败，原样式已保留。");return true;}}
    }else if((e->x>=v.w-86&&e->y<62)||(!traffic_record.updated&&e->y>=(v.landscape?276:320)&&e->y<(v.landscape?324:368))){traffic_settings=true;return true;}
    return false;
}
static bool key(int k){if(k!=-1)return false;if(traffic_settings){traffic_settings=false;return true;}H->navigate(PGA_PAGE_LAUNCHER);return true;}
static bool tick(void){
    if(!traffic_full)return false;
    if(traffic_poll())return true;
    if(!traffic_record.source&&traffic_record.token[0]&&!atomic_load(&traffic_phase)&&H->time_us()>=traffic_due){traffic_draft=traffic_record;if(!traffic_launch(false))traffic_due=H->time_us()+300000000;return true;}
    return false;
}
static void action(int a){if(a==0)key(-1);}
static uint32_t status(void){return atomic_load(&traffic_phase)?PGA_STATUS_BUSY:0;}
static bool save(void){return !atomic_load(&traffic_phase);}
static void stop(void){
    if(!traffic_full)return;
    while(atomic_load_explicit(&traffic_phase,memory_order_acquire)==1)H->delay_ms(20);
    if(traffic_worker){H->worker_delete(traffic_worker);traffic_worker=NULL;}
    atomic_store(&traffic_phase,0);H->set(traffic_response,0,sizeof(traffic_response));H->set(&traffic_draft,0,sizeof(traffic_draft));
}
static void paint(const void *model,const void *painter){if(H)ps_board_model_paint(model,painter);}
// 所有字符串经JSON转义；令牌永不进入状态或错误响应。/ Escape all strings; tokens never appear in status or error replies.
static bool traffic_json_string(char *out,size_t cap,const char *s){
    size_t n=0;if(cap<3)return false;
    out[n++]='"';
    for(unsigned i=0;s[i];i++){unsigned char c=(unsigned char)s[i];if(c<32||c=='"'||c=='\\'){if(n+6>=cap)return false;H->format(out+n,cap-n,"\\u%04x",c);n+=6;}else {if(n+2>=cap)return false;
    out[n++]=(char)c;}}
    if(n+2>cap)return false;
    out[n++]='"';out[n]=0;return true;
}
static int traffic_error(char *out,size_t cap,int code,const char *message){H->format(out,cap,"{\"ok\":false,\"error\":\"%s\"}",message);return code;}
static bool traffic_integer(wrj_token_t obj,const char *key,uint32_t *out){uint64_t n;if(!traffic_unsigned(wrj_field(obj,key),&n)||n>UINT32_MAX)return false;*out=(uint32_t)n;return true;}
static int manage(const char *method,const char *path,const void *body,size_t bytes,char *out,size_t cap){
    if(!traffic_full)return traffic_error(out,cap,409,"请升级固件并重新打开应用");
    if(traffic_same(path,"/api/manage/pico/traffic/ui")&&traffic_same(method,"GET")){
        if(cap<16)return 500;
        H->copy(out,"{\"html\":",8);if(!traffic_json_string(out+8,cap-9,traffic_ui))return traffic_error(out,cap,507,"页面容量不足");unsigned n=traffic_length(out);out[n++]='}';out[n]=0;return 200;
    }
    bool config=traffic_same(path,"/api/manage/pico/traffic/config"),refresh=traffic_same(path,"/api/manage/pico/traffic/refresh");
    if(config&&traffic_same(method,"GET")){
        char label[400],message[800];traffic_json_string(label,sizeof(label),traffic_record.label);traffic_json_string(message,sizeof(message),traffic_message);
        H->format(out,cap,"{\"ok\":true,\"revision\":%u,\"source\":%u,\"style\":%u,\"interval\":%u,\"label\":%s,\"zone\":\"%s\",\"has_token\":%s,\"busy\":%s,\"message\":%s}",traffic_record.revision,traffic_record.source,traffic_record.style,traffic_record.interval,label,traffic_record.zone,traffic_record.token[0]?"true":"false",atomic_load(&traffic_phase)?"true":"false",message);return 200;
    }
    if(!config&&!refresh)return traffic_error(out,cap,404,"接口不存在");
    if(atomic_load(&traffic_phase))return traffic_error(out,cap,409,"正在验证或刷新，请稍后再试");
    if(refresh&&traffic_same(method,"POST")){
        if(traffic_record.source||!traffic_record.token[0])return traffic_error(out,cap,409,"请先配置 Cloudflare 直连");
        traffic_draft=traffic_record;if(!traffic_launch(false))return traffic_error(out,cap,503,"无法启动更新，请检查设备提示");
    }else if(config&&(traffic_same(method,"PUT")||traffic_same(method,"DELETE"))){
        if(!body||!bytes||bytes>2048)return traffic_error(out,cap,422,"配置格式无效");
        wrj_begin(body,(uint32_t)bytes);wrj_token_t root=wrj_token(0,0);uint32_t rev;
        if(wrj_failed||root.type!='{'||wrj_space(root.end)!=bytes||!traffic_integer(root,"revision",&rev))return traffic_error(out,cap,422,"配置格式无效");
        if(rev!=traffic_record.revision)return traffic_error(out,cap,409,"配置已变化，请重新读取后保存");
        if(traffic_same(method,"DELETE")){
            traffic_default(&traffic_draft);traffic_draft.revision=rev+1;
            if(!traffic_store(&traffic_draft))return traffic_error(out,cap,507,"TF 保存失败，配置未清除");
            traffic_record=traffic_draft;
            if(!traffic_store(&traffic_draft)){traffic_note("配置已清除，但旧备份覆盖失败，请重试清除。");return traffic_error(out,cap,507,"配置已清除，旧备份覆盖失败，请重新读取后重试");}
            traffic_note("数据源与缓存已清除，电脑推送快照保留。");
        }else{
            traffic_draft=traffic_record;char token[256]={0};
            if(!traffic_integer(root,"source",&traffic_draft.source)||traffic_draft.source>1||!traffic_integer(root,"style",&traffic_draft.style)||traffic_draft.style>2||!traffic_integer(root,"interval",&traffic_draft.interval)||!traffic_interval(traffic_draft.interval)||!wrj_string(wrj_field(root,"label"),traffic_draft.label,sizeof(traffic_draft.label))||!wrj_string(wrj_field(root,"zone"),traffic_draft.zone,sizeof(traffic_draft.zone))||!wrj_string(wrj_field(root,"token"),token,sizeof(token)))return traffic_error(out,cap,422,"配置字段无效");
            bool changed=!traffic_same(traffic_record.zone,traffic_draft.zone);
            if(token[0]){if(!traffic_token(token)){H->set(token,0,sizeof(token));return traffic_error(out,cap,422,"令牌格式无效");}H->copy(traffic_draft.token,token,sizeof(token));}else if(changed)H->set(traffic_draft.token,0,sizeof(traffic_draft.token));
            H->set(token,0,sizeof(token));
            if(traffic_draft.zone[0]&&!traffic_id(traffic_draft.zone))return traffic_error(out,cap,422,"区域ID格式无效");
            if(!traffic_draft.source&&(!traffic_id(traffic_draft.zone)||!traffic_draft.label[0]||!traffic_token(traffic_draft.token)))return traffic_error(out,cap,422,"请填写名称、32位区域ID和只读令牌");
            if(changed){traffic_draft.updated=0;H->set(traffic_draft.days,0,sizeof(traffic_draft.days));H->set(traffic_draft.countries,0,sizeof(traffic_draft.countries));}
            if(!traffic_draft.source){if(!traffic_launch(true))return traffic_error(out,cap,503,"无法验证，请检查设备时间和内存");}
            else {traffic_draft.revision=rev+1;if(!traffic_store(&traffic_draft))return traffic_error(out,cap,507,"TF 保存失败，原配置保留");traffic_record=traffic_draft;H->traffic_snapshot(NULL);traffic_note("已切换为电脑快照，请在多屏灵犀编辑内容。");}
        }
    }else return traffic_error(out,cap,405,"请求方式不支持");
    H->dirty();H->format(out,cap,"{\"ok\":true,\"message\":\"%s\"}",atomic_load(&traffic_phase)?"正在验证，成功后保存":"设置已保存");return atomic_load(&traffic_phase)?202:200;
}
static const pico_app_exports_t app={.abi=1,.bytes=sizeof(app),.version=10100,.kind=PGA_WEBSITE_TRAFFIC,.id="website-traffic",.start=start,.render=render,.event=event,.key=key,.tick=tick,.action=action,.status=status,.save=save,.stop=stop,.board_paint=paint,.manage=manage};
__attribute__((visibility("default"))) const pico_app_exports_t *pico_app_query(void){return &app;}

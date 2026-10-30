/* SPDX-License-Identifier: Apache-2.0
 * 中文：可信USB无线引导，复用异步Wi-Fi控制器和限时管理服务。
 * English: Trusted USB wireless setup reuses the asynchronous Wi-Fi owner and timed management service.
 * 冻结：仅UI线程调用；不切页、不读写TF、不保留密码；只有身份匹配的open/connect/poll可获得本次临时密钥。
 * Frozen: UI-thread only; no navigation, TF access or password retention. Only identity-matched open/connect/poll may receive the current temporary key.
 */
#include "read_pico_management.h"
#include <stdarg.h>
#include <stdlib.h>
#include <strings.h>
#ifdef ESP_PLATFORM
#include "esp_mac.h"
#endif

typedef struct {
    uint32_t id,version,operation;
    unsigned fields;
    char op[12],serial[13],ssid[45],password[89];
} cw_request_t;
enum { CW_NONE,CW_ENABLE,CW_SCAN,CW_CONNECT,CW_DONE,CW_READY,CW_FAILED };
static read_pico_connectivity_status_t cw_network;
static read_pico_transfer_network_t cw_results[READ_PICO_TRANSFER_SCAN_MAX];
static size_t cw_result_count;
static uint32_t cw_next,cw_operation,cw_submitted,cw_scan_before;
static int cw_state,cw_error_status;
static bool cw_scanning;
static int64_t cw_deadline,cw_poll_at;
static char cw_target[33],cw_pair_url[192],cw_error[32];

static void cw_clear(void *data,size_t size){volatile unsigned char *p=data;while(size--)*p++=0;}
static bool cw_serial(char out[13]){
#ifdef ESP_PLATFORM
    uint8_t mac[6];
    if(esp_read_mac(mac,ESP_MAC_WIFI_STA)!=ESP_OK){out[0]=0;return false;}
    snprintf(out,13,"%02X%02X%02X%02X%02X%02X",mac[0],mac[1],mac[2],mac[3],mac[4],mac[5]);
#else
    // 私有宿主预览身份，不探测电脑或真实USB。/ Private host-preview identity; never probe host or physical USB.
    memcpy(out,"020000000001",13);
#endif
    return true;
}
static void cw_space(const char **p){while(**p==' '||**p=='\t')++*p;}
static bool cw_string(const char **p,char *out,size_t size){
    if(*(*p)++!='"')return false;
    size_t n=0;
    while(**p&&**p!='"'){
        unsigned char c=(unsigned char)*(*p)++;
        if(c<32||c>126||c=='\\'||n+1>=size)return false;
        out[n++]=(char)c;
    }
    if(**p!='"')return false;
    ++*p;out[n]=0;return true;
}
static bool cw_uint(const char **p,uint32_t *out){
    const char *start=*p;uint32_t value=0;
    while(**p>='0'&&**p<='9'){
        unsigned digit=(unsigned)(*(*p)++-'0');
        if(value>(2147483647u-digit)/10u)return false;
        value=value*10u+digit;
    }
    if(*p==start||(*p>start+1&&*start=='0'))return false;
    *out=value;return true;
}
static bool cw_parse(const char *p,cw_request_t *r){
    if(!p||strnlen(p,PS_WIRELESS_REQUEST_MAX)>=PS_WIRELESS_REQUEST_MAX)return false;
    memset(r,0,sizeof(*r));cw_space(&p);if(*p++!='{')return false;
    while(true){
        char key[24];cw_space(&p);if(!cw_string(&p,key,sizeof(key)))return false;
        cw_space(&p);if(*p++!=':')return false;cw_space(&p);
        unsigned field=0;bool ok=false;
        if(!strcmp(key,"v")){field=1;ok=cw_uint(&p,&r->version);}
        else if(!strcmp(key,"id")){field=2;ok=cw_uint(&p,&r->id);}
        else if(!strcmp(key,"op")){field=4;ok=cw_string(&p,r->op,sizeof(r->op));}
        else if(!strcmp(key,"expected_serial")){field=8;ok=cw_string(&p,r->serial,sizeof(r->serial));}
        else if(!strcmp(key,"ssid_b64")){field=16;ok=cw_string(&p,r->ssid,sizeof(r->ssid));}
        else if(!strcmp(key,"password_b64")){field=32;ok=cw_string(&p,r->password,sizeof(r->password));}
        else if(!strcmp(key,"op_id")){field=64;ok=cw_uint(&p,&r->operation);}
        if(!ok||(r->fields&field))return false;
        r->fields|=field;cw_space(&p);
        if(*p=='}'){p++;break;}
        if(*p++!=',')return false;
    }
    cw_space(&p);
    if(*p||r->version!=1||!r->id||(r->fields&7u)!=7u)return false;
    unsigned allowed=!strcmp(r->op,"connect")?63u:!strcmp(r->op,"poll")?79u:!strcmp(r->op,"open")?15u:7u;
    return r->fields==allowed&&(!(allowed&64u)||r->operation);
}
static int cw_b64_value(unsigned char c){
    if(c>='A'&&c<='Z')return c-'A';
    if(c>='a'&&c<='z')return c-'a'+26;
    if(c>='0'&&c<='9')return c-'0'+52;
    return c=='+'?62:c=='/'?63:-1;
}
static bool cw_decode(const char *text,char *out,size_t capacity){
    size_t n=strlen(text),used=0;if(n%4)return false;
    for(size_t i=0;i<n;i+=4){
        int a=cw_b64_value(text[i]),b=cw_b64_value(text[i+1]);
        int c=text[i+2]=='='?0:cw_b64_value(text[i+2]),d=text[i+3]=='='?0:cw_b64_value(text[i+3]);
        bool pad2=text[i+2]=='=',pad1=text[i+3]=='=';
        if(a<0||b<0||c<0||d<0||(pad2&&!pad1)||((pad1||pad2)&&i+4!=n)||(pad2&&(b&15))||(pad1&&!pad2&&(c&3)))return false;
        unsigned char bytes[3]={(unsigned char)((a<<2)|(b>>4)),(unsigned char)((b<<4)|(c>>2)),(unsigned char)((c<<6)|d)};
        size_t count=pad2?1:pad1?2:3;
        for(size_t k=0;k<count;k++){if(!bytes[k]||bytes[k]<32||bytes[k]==127||used+1>=capacity)return false;out[used++]=(char)bytes[k];}
    }
    out[used]=0;return true;
}
static void cw_encode(const char *text,char out[45]){
    static const char abc[]="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    size_t n=strnlen(text,32),used=0;
    for(size_t i=0;i<n;i+=3){unsigned a=(unsigned char)text[i],b=i+1<n?(unsigned char)text[i+1]:0,c=i+2<n?(unsigned char)text[i+2]:0;
        out[used++]=abc[a>>2];out[used++]=abc[((a&3)<<4)|(b>>4)];out[used++]=i+1<n?abc[((b&15)<<2)|(c>>6)]:'=';out[used++]=i+2<n?abc[c&63]:'=';
    }
    out[used]=0;
}
static bool cw_utf8(const char *text){
    const unsigned char *p=(const unsigned char*)text;
    while(*p){unsigned c=*p++,n=0;uint32_t v=0;
        if(c<128)continue;
        if(c>=0xc2&&c<=0xdf){n=1;v=c&31;}else if(c>=0xe0&&c<=0xef){n=2;v=c&15;}else if(c>=0xf0&&c<=0xf4){n=3;v=c&7;}else return false;
        unsigned width=n;while(n--){if((*p&0xc0)!=0x80)return false;v=(v<<6)|(*p++&63);}
        if((width==2&&v<0x800)||(width==3&&v<0x10000)||(v>=0xd800&&v<=0xdfff)||v>0x10ffff)return false;
    }
    return true;
}
static bool cw_ipv4(const char *text){
    const char *p=text;
    for(unsigned part=0;part<4;part++){unsigned v=0,digits=0;while(*p>='0'&&*p<='9'){if(++digits>3)return false;v=v*10+(unsigned)(*p++-'0');}if(!digits||v>255)return false;if(part<3&&*p++!='.')return false;}
    return !*p&&strcmp(text,"0.0.0.0");
}
static bool cw_pending(void){return cw_state==CW_ENABLE||cw_state==CW_SCAN||cw_state==CW_CONNECT;}
static void cw_fail(int status,const char *error){cw_state=CW_FAILED;cw_error_status=status;snprintf(cw_error,sizeof(cw_error),"%s",error);cw_clear(cw_pair_url,sizeof(cw_pair_url));cw_clear(cw_target,sizeof(cw_target));}
static bool cw_open(void){
    if(!cw_network.connected||!cw_ipv4(cw_network.ip)||!read_pico_management_start())return false;
    read_pico_management_status_t status;read_pico_management_get_status(&status);
    bool ok=status.active&&status.seconds_left&&strstr(status.url,"#key=");
    if(ok)snprintf(cw_pair_url,sizeof(cw_pair_url),"%s",status.url);
    cw_clear(&status,sizeof(status));return ok;
}
void ps_connectivity_wireless_tick(void){
    if(ps_usb_held())return;
    read_pico_management_tick();
    if(cw_poll_at&&ps_now()<cw_poll_at)return;
    cw_poll_at=ps_now()+100;read_pico_connectivity_get_status(&cw_network);
    if(cw_pair_url[0]){
        read_pico_management_status_t management;read_pico_management_get_status(&management);
        bool expired=!management.active||!management.seconds_left||strcmp(management.url,cw_pair_url);
        cw_clear(&management,sizeof(management));
        if(expired){cw_clear(cw_pair_url,sizeof(cw_pair_url));if(cw_state==CW_READY)cw_fail(410,"pairing_expired");}
    }
    if(!cw_pending())return;
    if(ps_storage_busy()||ps_content_held()||cw_network.suspended||cw_network.transfer_active){cw_fail(409,"busy");return;}
    if(ps_now()>=cw_deadline){cw_fail(504,"timeout");return;}
    if(cw_state==CW_ENABLE){
        if(cw_network.busy||cw_network.completed_seq<cw_submitted)return;
        if(!cw_network.wifi_enabled){cw_fail(503,"operation_failed");return;}
        cw_scan_before=cw_network.scan_completed_seq;
        if(read_pico_connectivity_scan()!=ESP_OK){cw_fail(503,"operation_failed");return;}
        cw_state=CW_SCAN;return;
    }
    if(cw_state==CW_SCAN&&cw_network.scan_completed_seq!=cw_scan_before){
        if(cw_network.scan_error!=ESP_OK)cw_fail(503,"operation_failed");
        else{
            cw_result_count=cw_network.network_count<READ_PICO_TRANSFER_SCAN_MAX?cw_network.network_count:READ_PICO_TRANSFER_SCAN_MAX;
            memcpy(cw_results,cw_network.networks,cw_result_count*sizeof(*cw_results));cw_state=CW_DONE;
        }
    }else if(cw_state==CW_CONNECT&&cw_network.completed_seq>=cw_submitted&&!cw_network.busy){
        if(cw_network.connected&&!strcmp(cw_network.ssid,cw_target)&&!cw_network.connecting){
            if(cw_open())cw_state=CW_READY;else cw_fail(503,"start_failed");
            cw_clear(cw_target,sizeof(cw_target));
        }else if(!cw_network.connecting&&cw_network.last_error!=ESP_OK)cw_fail(503,"operation_failed");
    }
}
static bool cw_append(char *out,size_t capacity,size_t *used,const char *format,...){
    if(*used>=capacity)return false;
    va_list args;va_start(args,format);int n=vsnprintf(out+*used,capacity-*used,format,args);va_end(args);
    if(n<0||(size_t)n>=capacity-*used)return false;
    *used+=(size_t)n;return true;
}
static bool cw_manage(char *out,size_t capacity,size_t *used){
    read_pico_management_status_t s;read_pico_management_get_status(&s);
    const char *key=strstr(s.url,"#key=");
    bool valid=s.active&&s.seconds_left&&s.seconds_left<=900&&!strcmp(s.url,cw_pair_url)&&cw_ipv4(s.host)&&key&&strlen(key+5)==32;
    if(valid)for(unsigned i=0;i<32;i++)if(!((key[5+i]>='0'&&key[5+i]<='9')||(key[5+i]>='a'&&key[5+i]<='f')||(key[5+i]>='A'&&key[5+i]<='F')))valid=false;
    bool ok=valid&&cw_append(out,capacity,used,",\"manage\":{\"url\":\"http://%s/\",\"key\":\"%s\",\"ttl_seconds\":%u}",s.host,key+5,(unsigned)s.seconds_left);
    cw_clear(&s,sizeof(s));return ok;
}
void ps_connectivity_wireless_request(const char *text,char *out,size_t capacity){
    if(!out||!capacity)return;
    out[0]=0;if(capacity<256)return;
    cw_request_t r={0};char serial[13]={0},ssid[33]={0},password[65]={0};
    int status=200;const char *error=NULL,*phase="done";bool result_manage=false,result_scan=false;
    if(!cw_parse(text,&r)){status=400;error="invalid_request";r.op[0]=0;goto reply;}
    if(!cw_serial(serial)){status=503;error="identity_unavailable";goto reply;}
    if(r.fields&8u){if(strlen(r.serial)!=12||strcasecmp(r.serial,serial)){status=403;error="identity_mismatch";goto reply;}}
    if(ps_usb_held()){status=409;error="msc_active";goto reply;}
    if(ps_storage_busy()||ps_content_held()){status=409;error="busy";goto reply;}
    ps_connectivity_wireless_tick();read_pico_connectivity_get_status(&cw_network);
    if(!strcmp(r.op,"status"))goto reply;
    if(!cw_network.ready||cw_network.suspended){status=503;error="not_ready";goto reply;}
    if(!strcmp(r.op,"poll")){
        if(r.operation!=cw_operation||cw_state==CW_NONE){status=404;error="unknown_operation";goto reply;}
        if(cw_pending()){status=202;phase="pending";}
        else if(cw_state==CW_FAILED){status=cw_error_status;error=cw_error;}
        else if(cw_state==CW_READY){phase="ready";result_manage=true;}
        else result_scan=cw_scanning;
        goto reply;
    }
    if(cw_pending()||cw_network.busy||cw_network.scanning||cw_network.connecting||cw_network.transfer_active){status=409;error="busy";goto reply;}
    if(!strcmp(r.op,"open")){
        if(!cw_network.connected||!cw_ipv4(cw_network.ip)){status=409;error="not_connected";goto reply;}
        if(!cw_open()){status=503;error="start_failed";goto reply;}
        cw_state=CW_NONE;cw_operation=0;
        phase="ready";result_manage=true;goto reply;
    }
    if(strcmp(r.op,"scan")&&strcmp(r.op,"connect")){status=404;error="unknown_operation";goto reply;}
    bool scanning=!strcmp(r.op,"scan");esp_err_t result;
    if(!scanning&&(!cw_decode(r.ssid,ssid,sizeof(ssid))||!ssid[0]||!cw_utf8(ssid)||!cw_decode(r.password,password,sizeof(password))||!cw_utf8(password))){status=400;error="invalid_request";goto reply;}
    if(scanning){cw_scan_before=cw_network.scan_completed_seq;result=cw_network.wifi_enabled?read_pico_connectivity_scan():read_pico_connectivity_set_enabled(true);}
    else result=read_pico_connectivity_connect(ssid,password);
    cw_clear(password,sizeof(password));
    if(result!=ESP_OK){status=503;error="operation_failed";goto reply;}
    cw_operation=++cw_next;if(!cw_operation||cw_operation>2147483647u)cw_operation=cw_next=1;
    cw_state=scanning?(cw_network.wifi_enabled?CW_SCAN:CW_ENABLE):CW_CONNECT;cw_scanning=scanning;
    read_pico_connectivity_get_status(&cw_network);cw_submitted=cw_network.submitted_seq;
    cw_deadline=ps_now()+(scanning?20000:45000);cw_poll_at=0;cw_clear(cw_pair_url,sizeof(cw_pair_url));cw_error[0]=0;snprintf(cw_target,sizeof(cw_target),"%s",ssid);
    status=202;phase="pending";
reply:
    if(!serial[0])cw_serial(serial);
    if(result_manage){size_t n=0;char scratch[384];if(!cw_manage(scratch,sizeof(scratch),&n)){status=410;error="pairing_expired";result_manage=false;cw_clear(cw_pair_url,sizeof(cw_pair_url));}cw_clear(scratch,sizeof(scratch));}
    if(error)phase="error";
    size_t used=0;bool ok=cw_append(out,capacity,&used,"{\"v\":1,\"id\":%u,\"device\":\"read-pico\",\"usb_serial\":\"%s\",\"ok\":%s,\"status\":%d,\"op\":\"%s\",\"phase\":\"%s\"",(unsigned)r.id,serial,status<400?"true":"false",status,r.op,phase);
    if(error)ok=ok&&cw_append(out,capacity,&used,",\"error\":\"%s\"",error);
    if(cw_operation&&(!strcmp(r.op,"poll")||status==202))ok=ok&&cw_append(out,capacity,&used,",\"op_id\":%u",(unsigned)cw_operation);
    if(!strcmp(r.op,"status")&&status==200){char encoded[45];cw_encode(cw_network.ssid,encoded);ok=ok&&cw_append(out,capacity,&used,",\"ready\":%s,\"enabled\":%s,\"connected\":%s,\"busy\":%s,\"ip\":\"%s\",\"ssid_b64\":\"%s\"",cw_network.ready?"true":"false",cw_network.wifi_enabled?"true":"false",cw_network.connected?"true":"false",cw_network.busy?"true":"false",cw_network.connected&&cw_ipv4(cw_network.ip)?cw_network.ip:"",encoded);}
    if(result_manage)ok=ok&&cw_manage(out,capacity,&used);
    if(result_scan){ok=ok&&cw_append(out,capacity,&used,",\"networks\":[");
        for(size_t i=0;i<cw_result_count;i++){char encoded[45];const read_pico_transfer_network_t *n=&cw_results[i];cw_encode(n->ssid,encoded);ok=ok&&cw_append(out,capacity,&used,"%s{\"ssid_b64\":\"%s\",\"rssi\":%d,\"requires_password\":%s,\"supported\":%s}",i?",":"",encoded,(int)n->rssi,n->requires_password?"true":"false",n->supported?"true":"false");}
        ok=ok&&cw_append(out,capacity,&used,"]");
    }
    ok=ok&&cw_append(out,capacity,&used,"}");
    if(!ok){cw_clear(out,capacity);snprintf(out,capacity,"{\"v\":1,\"id\":%u,\"device\":\"read-pico\",\"usb_serial\":\"%s\",\"ok\":false,\"status\":503,\"op\":\"%s\",\"phase\":\"error\",\"error\":\"capacity\"}",(unsigned)r.id,serial,r.op);}
    cw_clear(&r,sizeof(r));cw_clear(ssid,sizeof(ssid));cw_clear(password,sizeof(password));
}

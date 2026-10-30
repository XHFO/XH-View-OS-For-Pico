/* SPDX-License-Identifier: Apache-2.0
 * 真实USB无线协议的无设备测试，所有Wi-Fi与管理生命周期均为受控替身。
 * Device-free tests of the production USB wireless protocol with controlled Wi-Fi/management lifecycles.
 */
#include "pico_connectivity.h"
#include "read_pico_connectivity.h"
#include "read_pico_management.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static read_pico_connectivity_status_t network;
static read_pico_management_status_t management;
static int64_t now=1000;
static bool usb,storage,content,fail_submit,fail_start;
static unsigned checks,submits,starts,stops,scan_calls,enable_calls;
static char submitted_ssid[33],submitted_password[65];
static int64_t manager_deadline;
int64_t ps_now(void){return now;}
bool ps_usb_held(void){return usb;}
bool ps_storage_busy(void){return storage;}
bool ps_content_held(void){return content;}
void read_pico_connectivity_get_status(read_pico_connectivity_status_t *out){*out=network;}
static esp_err_t submit(void){submits++;if(fail_submit)return ESP_FAIL;network.submitted_seq++;network.busy=true;return ESP_OK;}
esp_err_t read_pico_connectivity_scan(void){scan_calls++;esp_err_t e=submit();if(e==ESP_OK)network.scanning=true;return e;}
esp_err_t read_pico_connectivity_set_enabled(bool enabled){assert(enabled);enable_calls++;return submit();}
esp_err_t read_pico_connectivity_connect(const char *ssid,const char *password){snprintf(submitted_ssid,sizeof(submitted_ssid),"%s",ssid);snprintf(submitted_password,sizeof(submitted_password),"%s",password);return submit();}
void read_pico_management_get_status(read_pico_management_status_t *out){*out=management;}
bool read_pico_management_active(void){return management.active;}
bool read_pico_management_start(void){
    starts++;if(fail_start)return false;
    management.active=true;management.seconds_left=900;manager_deadline=now+900000;
    snprintf(management.host,sizeof(management.host),"%s",network.ip);
    snprintf(management.url,sizeof(management.url),"http://%s/manage#key=%032x",network.ip,starts);
    return true;
}
void read_pico_management_stop(void){stops++;memset(&management,0,sizeof(management));}
void read_pico_management_tick(void){if(management.active&&(!network.connected||network.transfer_active||now>=manager_deadline))read_pico_management_stop();}
#include "../main/system/pico_wireless.inc"
#define CHECK(x) do{checks++;if(!(x)){fprintf(stderr,"wireless FAIL %d: %s\n",__LINE__,#x);abort();}}while(0)
static char response[4096];
static void request(const char *text,int status){
    memset(response,0x7c,sizeof(response));ps_connectivity_wireless_request(text,response,sizeof(response));
    char expected[32];snprintf(expected,sizeof(expected),"\"status\":%d",status);CHECK(strstr(response,expected));
    CHECK(strstr(response,"\"device\":\"read-pico\"")&&strstr(response,"\"usb_serial\":\"020000000001\""));
    CHECK(!strstr(response,"\"password\"")&&!strstr(response,"\"password_b64\"")&&!strstr(response,"c2VjcmV0MTIz"));
}
static void settle(void){network.completed_seq=network.submitted_seq;network.busy=false;now+=200;ps_connectivity_wireless_tick();}
static void reset(void){
    memset(&network,0,sizeof(network));memset(&management,0,sizeof(management));network.ready=true;
    usb=storage=content=fail_submit=fail_start=false;now+=1000;cw_state=CW_NONE;cw_operation=0;cw_poll_at=0;
    memset(cw_pair_url,0,sizeof(cw_pair_url));submitted_password[0]=0;
}
static void poll(int status){char text[180];snprintf(text,sizeof(text),"{\"v\":1,\"id\":2,\"op\":\"poll\",\"expected_serial\":\"020000000001\",\"op_id\":%u}",cw_operation);request(text,status);CHECK(strstr(response,"\"id\":2")&&strstr(response,"\"op\":\"poll\""));}
static const char *connect_request="{\"v\":1,\"id\":1,\"op\":\"connect\",\"expected_serial\":\"020000000001\",\"ssid_b64\":\"5Lit5paHIFdpRmk=\",\"password_b64\":\"c2VjcmV0MTIz\"}";
static void contracts(void){
    reset();request("{\"v\":1,\"id\":7,\"op\":\"status\"}",200);CHECK(strstr(response,"\"id\":7")&&strstr(response,"\"connected\":false")&&!strstr(response,"manage")&&!strstr(response,"key"));
    unsigned before=submits;request("{\"v\":1,\"id\":1,\"op\":\"connect\",\"expected_serial\":\"FFFFFFFFFFFF\",\"ssid_b64\":\"QQ==\",\"password_b64\":\"\"}",403);CHECK(submits==before);
    request("{\"v\":1,\"id\":1,\"op\":\"open\",\"expected_serial\":\"020000000001\"}",409);CHECK(!strstr(response,"\"key\""));
    const char *bad[]={"", "{}", "[]", "{\"v\":1,\"id\":1,\"op\":\"status\",\"id\":2}","{\"v\":2,\"id\":1,\"op\":\"status\"}","{\"v\":1,\"id\":0,\"op\":\"status\"}","{\"v\":1,\"id\":2147483648,\"op\":\"status\"}","{\"v\":1,\"id\":01,\"op\":\"status\"}","{\"v\":1,\"id\":1e0,\"op\":\"status\"}","{\"v\":1,\"id\":1,\"op\":\"status\",\"extra\":false}","{\"v\":1,\"id\":1,\"op\":\"stat\\u0075s\"}","{\"v\":1,\"id\":1,\"op\":\"status\"}x","{\"v\":1,\"id\":1,\"op\":\"connect\",\"ssid_b64\":\"QQ==\",\"password_b64\":\"\"}"};
    for(size_t i=0;i<sizeof(bad)/sizeof(*bad);i++)request(bad[i],400);
    char long_request[513];memset(long_request,' ',sizeof(long_request)-1);long_request[512]=0;request(long_request,400);
    usb=true;request("{\"v\":1,\"id\":1,\"op\":\"status\"}",409);usb=false;
    content=true;request("{\"v\":1,\"id\":1,\"op\":\"scan\"}",409);content=false;
    network.ready=false;request("{\"v\":1,\"id\":1,\"op\":\"scan\"}",503);network.ready=true;
    request("{\"v\":1,\"id\":1,\"op\":\"unknown\"}",404);
    reset();request("{\"v\":1,\"id\":1,\"op\":\"scan\"}",202);CHECK(cw_state==CW_ENABLE&&enable_calls);poll(202);
    network.wifi_enabled=true;settle();CHECK(cw_state==CW_SCAN&&scan_calls);network.scanning=false;network.scan_completed_seq++;network.scan_error=ESP_OK;network.network_count=16;
    for(unsigned i=0;i<16;i++){memset(network.networks[i].ssid,'x',32);network.networks[i].ssid[32]=0;network.networks[i].rssi=-40;network.networks[i].supported=true;}
    settle();poll(200);CHECK(strstr(response,"\"networks\":[")&&!strstr(response,"\"key\"")&&strlen(response)<4096);
    char frozen[4096];strcpy(frozen,response);network.network_count=0;network.scan_completed_seq++;now+=200;poll(200);CHECK(!strcmp(frozen,response));
    char small[258];memset(small,0x6d,sizeof(small));char p[180];snprintf(p,sizeof(p),"{\"v\":1,\"id\":2,\"op\":\"poll\",\"expected_serial\":\"020000000001\",\"op_id\":%u}",cw_operation);
    ps_connectivity_wireless_request(p,small+1,256);CHECK(small[0]==0x6d&&small[257]==0x6d&&strstr(small+1,"\"error\":\"capacity\"")&&!strstr(small+1,"\"key\""));
    reset();network.wifi_enabled=true;request("{\"v\":1,\"id\":1,\"op\":\"scan\"}",202);network.scan_completed_seq++;network.network_count=0;network.scanning=false;settle();poll(200);CHECK(strstr(response,"\"networks\":[]"));
    reset();request(connect_request,202);CHECK(!strcmp(submitted_ssid,"中文 WiFi")&&!strcmp(submitted_password,"secret123"));CHECK(cw_state==CW_CONNECT);poll(202);
    unsigned started=starts;network.connecting=true;settle();CHECK(starts==started);network.connected=true;network.connecting=false;snprintf(network.ssid,sizeof(network.ssid),"中文 WiFi");strcpy(network.ip,"192.0.2.4");settle();poll(200);CHECK(strstr(response,"\"phase\":\"ready\"")&&strstr(response,"\"url\":\"http://192.0.2.4/\"")&&strstr(response,"\"ttl_seconds\":900"));
    request("{\"v\":1,\"id\":8,\"op\":\"status\"}",200);CHECK(!strstr(response,"\"key\""));
    now=manager_deadline;ps_connectivity_wireless_tick();CHECK(!cw_pair_url[0]);poll(410);CHECK(!strstr(response,"\"key\""));
    reset();request(connect_request,202);now+=45000;ps_connectivity_wireless_tick();poll(504);CHECK(!cw_target[0]);
    reset();request("{\"v\":1,\"id\":1,\"op\":\"scan\"}",202);now+=20000;ps_connectivity_wireless_tick();poll(504);
    reset();fail_submit=true;request(connect_request,503);CHECK(!cw_operation);
    reset();request(connect_request,202);network.last_error=ESP_FAIL;settle();poll(503);
    reset();request(connect_request,202);request("{\"v\":1,\"id\":9,\"op\":\"scan\"}",409);
    reset();network.connected=true;strcpy(network.ip,"192.0.2.9");fail_start=true;request("{\"v\":1,\"id\":1,\"op\":\"open\",\"expected_serial\":\"020000000001\"}",503);
    fail_start=false;request("{\"v\":1,\"id\":1,\"op\":\"open\",\"expected_serial\":\"020000000001\"}",200);CHECK(strstr(response,"\"key\""));
    const char *invalid_b64[]={"A===","QQ=A","QR==","AAA=AAAA","AA==","wA==","YQ"};
    for(unsigned i=0;i<sizeof(invalid_b64)/sizeof(*invalid_b64);i++){char text[256];snprintf(text,sizeof(text),"{\"v\":1,\"id\":1,\"op\":\"connect\",\"expected_serial\":\"020000000001\",\"ssid_b64\":\"%s\",\"password_b64\":\"\"}",invalid_b64[i]);request(text,400);}
    // 任意截断均不得读越界，也不得触发网络提交。/ Every truncation must remain in bounds and submit no network commands.
    before=submits;for(size_t i=0;i<strlen(connect_request);i++){char *text=malloc(i+1);CHECK(text);memcpy(text,connect_request,i);text[i]=0;request(text,400);free(text);}CHECK(submits==before);
    for(size_t capacity=0;capacity<256;capacity++){char *out=malloc(capacity+2);CHECK(out);memset(out,0x55,capacity+2);ps_connectivity_wireless_request(connect_request,out+1,capacity);CHECK(out[0]==0x55&&out[capacity+1]==0x55);if(capacity)CHECK(!out[1]);free(out);}CHECK(submits==before);
    uint32_t seed=7;for(unsigned i=0;i<2000;i++){size_t length=i%200;char *text=malloc(length+1);CHECK(text);for(size_t k=0;k<length;k++){seed=seed*1664525u+1013904223u;text[k]=(char)(32+seed%95);}text[length]=0;request(text,400);free(text);}CHECK(submits==before);
}
int main(void){contracts();printf("wireless protocol: %u checks PASS; no hardware, sockets or persistence\n",checks);return 0;}

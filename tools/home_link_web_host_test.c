/* SPDX-License-Identifier: Apache-2.0
 * Management API contracts with runtime doubles. No real account or network.
 */
#include "home_link_web.h"
#include "../components/read_pico_home_link/home_link_json.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static rp_home_config_t config;
static rp_home_snapshot_t snapshot;
static bool store_fail,queue_ok=true,control_ok=true,control_home,request_home;
static int stores,requests,controls;
static char output[RP_HOME_WEB_OUTPUT_LIMIT],last_action[32];
static unsigned checks,canaries;
static rp_home_bambu_state_t bambu_state;
static rp_home_bambu_input_t bambu_input;
static bool bambu_queue=true;
#define CHECK(x) do{checks++;assert(x);}while(0)
void read_pico_home_link_get_config(rp_home_config_t *c){*c=config;}
void read_pico_home_link_get_snapshot(rp_home_snapshot_t *s){*s=snapshot;}
bool read_pico_home_link_configure(const rp_home_config_t *c){if(store_fail)return false;CHECK(rp_home_config_valid(c));config=*c;stores++;return true;}
bool read_pico_home_link_request(bool online,bool home){request_home=home;requests++;return online&&config.enabled&&queue_ok;}
bool read_pico_home_link_control(const rp_home_entity_t *e,const char *action,float value,bool home){CHECK(e&&e->available);(void)value;controls++;control_home=home;snprintf(last_action,sizeof(last_action),"%s",action);return control_ok;}
void read_pico_bambu_auth_get(rp_home_bambu_state_t *s){*s=bambu_state;}
bool read_pico_bambu_auth_submit(const rp_home_bambu_input_t *i,bool online){
    if(!online||!bambu_queue||i->session!=bambu_state.session)return false;
    if(i->op==RP_BAMBU_REUSE) {
        bool found=false;for(unsigned k=0;k<bambu_state.saved_count;k++)if(i->saved_slot==bambu_state.saved_accounts[k].slot)found=true;
        if(i->config_revision!=bambu_state.config_revision||!found)return false;
    }
    bambu_input=*i;bambu_state.phase=RP_BAMBU_BUSY;bambu_state.revision++;return true;
}
bool read_pico_bambu_auth_cancel_session(uint32_t session){if(session!=bambu_state.session)return false;memset(&bambu_state,0,sizeof(bambu_state));bambu_state.session=session+1;return true;}
void read_pico_bambu_auth_cancel(void){(void)read_pico_bambu_auth_cancel_session(bambu_state.session);}
bool read_pico_bambu_auth_prepare_import(uint32_t session,unsigned index,rp_home_printer_config_t *out){
    if(session!=bambu_state.session||bambu_state.phase!=RP_BAMBU_READY||index>=bambu_state.count)return false;
    *out=(rp_home_printer_config_t){.source=RP_HOME_BAMBU_CLOUD,.region=1,.name="Imported",.serial="BOUND123",.username="u_PRIVATE_AUTH",.secret="PRIVATE_AUTH_TOKEN"};return true;
}

static const char *wifi[]={"Home", "Office"};
static rp_home_web_response_t call(const char *method,const char *path,const char *query,const char *body,bool online,bool home,size_t capacity) {
    rp_home_web_request_t r={.method=method,.path=path,.query=query,.body=body,.body_bytes=body?strlen(body):0,
        .network_online=online,.at_home=home,.saved_wifi=wifi,.saved_wifi_count=2};
    rp_home_web_response_t result={0};memset(output,0xa5,sizeof(output));
    CHECK(rp_home_web_handle(&r,&result,output,capacity));CHECK(result.body_bytes<capacity);
    CHECK(output[result.body_bytes]==0);for(size_t i=capacity;i<sizeof(output);i++){canaries++;assert((unsigned char)output[i]==0xa5);}
    cJSON *j=cJSON_ParseWithLength(output,result.body_bytes);CHECK(cJSON_IsObject(j));cJSON_Delete(j);return result;
}
static int api(const char *method,const char *path,const char *query,const char *body){return call(method,path,query,body,true,true,sizeof(output)).http_status;}
static cJSON *response(void){cJSON *j=cJSON_Parse(output);CHECK(j);return j;}
static cJSON *get(cJSON *j,const char *key){return cJSON_GetObjectItemCaseSensitive(j,key);}
static void initialize(void) {
    memset(&config,0,sizeof(config));memset(&snapshot,0,sizeof(snapshot));
    config=(rp_home_config_t){.version=RP_HOME_CONFIG_VERSION,.enabled=1,.external_enabled=1,
        .local="http://ha.local:8123",.external="https://ha.example",.home_wifi="Home",.ha_token="PRIVATE_HA_TOKEN"};
    config.printers[0]=(rp_home_printer_config_t){.source=RP_HOME_FORMLABS,.name="工作室 Form 4",.serial="FORM123",.client_id="PRIVATE_CLIENT_ID",.client_secret="PRIVATE_FORM_SECRET"};
    config.printers[1]=(rp_home_printer_config_t){.source=RP_HOME_BAMBU_CLOUD,.name="Cloud",.serial="BAMBU123",.username="u_PRIVATE",.secret="PRIVATE_CLOUD_TOKEN"};
    config.printers[2]=(rp_home_printer_config_t){.source=RP_HOME_BAMBU_LAN,.name="LAN",.serial="LAN123",.host="printer.local",.port=8883,.secret="PRIVATE_LAN_CODE",.certificate="PRIVATE_CA"};
    snapshot.result=RP_HOME_OK;snapshot.revision=17;snapshot.entity_count=RP_HOME_ENTITIES;snapshot.entity_total=1000;snapshot.truncated=1;
    for(unsigned i=0;i<RP_HOME_ENTITIES;i++) {
        rp_home_entity_t *e=&snapshot.entities[i];e->kind=(uint8_t)(i%6);e->available=1;e->number=(float)i;
        const char *domains[]={"light","switch","climate","fan","sensor","sensor"};
        snprintf(e->id,sizeof(e->id),"%s.fixture_%u",domains[e->kind],i);snprintf(e->name,sizeof(e->name),"设备 %u",i);
        snprintf(e->room,sizeof(e->room),"房间 %u",i/4);snprintf(e->state,sizeof(e->state),"on");
    }
    snapshot.entities[4].number=NAN;stores=requests=controls=0;store_fail=false;queue_ok=control_ok=true;
    CHECK(rp_home_config_valid(&config));
}
int main(void) {
    initialize();rp_home_config_t before=config;cJSON *j;
    CHECK(api("GET","/api/manage/home",NULL,NULL)==200);
    CHECK(!strstr(output,"PRIVATE_")&&strstr(output,"\"has_token\":true"));j=response();CHECK(cJSON_GetArraySize(get(j,"saved_wifi"))==2);cJSON_Delete(j);
    // 时间错误公开为独立结果，不降级成 TLS 或认证错误。/ Clock errors remain separate from TLS and authentication in public snapshots.
    snapshot.result=RP_HOME_TIME;CHECK(api("GET","/api/manage/home",NULL,NULL)==200);j=response();CHECK(get(get(j,"status"),"result")->valueint==11);cJSON_Delete(j);snapshot.result=RP_HOME_OK;
    snapshot.printers[0].result=RP_HOME_TIME;CHECK(api("GET","/api/manage/printers",NULL,NULL)==200);j=response();CHECK(get(get(cJSON_GetArrayItem(get(j,"printers"),0),"status"),"result")->valueint==11);cJSON_Delete(j);snapshot.printers[0].result=RP_HOME_EMPTY;
    CHECK(api("GET","/api/manage/printers",NULL,NULL)==200);CHECK(!strstr(output,"PRIVATE_")&&!strstr(output,"u_PRIVATE"));
    CHECK(strstr(output,"has_client_secret")&&strstr(output,"has_certificate"));
    CHECK(api("PUT","/api/manage/home",NULL,"{\"local\":\"http://new.local:8123\",\"ha_token\":\"\"}")==200);
    CHECK(!strcmp(config.ha_token,"PRIVATE_HA_TOKEN")&&!strcmp(config.local,"http://new.local:8123"));
    CHECK(!memcmp(config.printers,before.printers,sizeof(config.printers)));
    CHECK(api("PUT","/api/manage/home",NULL,"{\"home_wifi\":\"NotSaved\"}")==400);
    CHECK(api("PUT","/api/manage/home",NULL,"{\"home_wifi\":\"Office\",\"rooms\":[{\"entity\":\"light.fixture_0\",\"room\":\"客厅\"}]}")==200);CHECK(config.room_count==1&&!strcmp(config.home_wifi,"Office"));
    CHECK(api("PUT","/api/manage/home",NULL,"{\"clear\":[\"ha_token\"]}")==200);CHECK(!*config.ha_token);
    CHECK(api("PUT","/api/manage/home",NULL,"{\"ha_token\":\"NEW_PRIVATE_TOKEN\"}")==200);
    config=before;store_fail=true;CHECK(api("PUT","/api/manage/home",NULL,"{\"home_wifi\":\"Office\"}")==507);CHECK(!memcmp(&config,&before,sizeof(config)));store_fail=false;
    CHECK(api("PUT","/api/manage/printers","slot=0","{\"name\":\"新名称\",\"client_secret\":\"\"}")==200);CHECK(!strcmp(config.printers[0].client_secret,"PRIVATE_FORM_SECRET"));
    CHECK(!memcmp(&config.printers[1],&before.printers[1],2*sizeof(config.printers[0])));
    config=before;
    CHECK(api("PUT","/api/manage/printers","slot=0","{\"source\":3,\"serial\":\"NEWBAMBU\",\"username\":\"u_new\"}")==400);CHECK(!memcmp(&config,&before,sizeof(config)));
    CHECK(api("PUT","/api/manage/printers","slot=0","{\"source\":3,\"serial\":\"NEWBAMBU\",\"username\":\"u_new\",\"secret\":\"new-token\",\"region\":1}")==200);
    CHECK(config.printers[0].source==RP_HOME_BAMBU_CLOUD&&!strcmp(config.printers[0].name,"工作室 Form 4")&&!*config.printers[0].client_id&&!*config.printers[0].client_secret&&!config.printers[0].port);
    CHECK(api("PUT","/api/manage/printers","slot=0","{\"client_secret\":\"foreign\"}")==400);
    CHECK(api("PUT","/api/manage/printers","slot=0","{\"clear\":[\"secret\"]}")==400);CHECK(!strcmp(config.printers[0].secret,"new-token"));
    config.printers[0].secret[500]='x';CHECK(api("PUT","/api/manage/printers","slot=0","{\"secret\":\"short\"}")==200);CHECK(!config.printers[0].secret[500]);
    config.enabled=0;CHECK(api("PUT","/api/manage/printers","slot=0","{\"name\":\"cloud\"}")==200);CHECK(config.enabled);
    CHECK(api("PUT","/api/manage/printers","slot=0","{\"source\":2,\"serial\":\"LANNEW\",\"host\":\"printer.local\",\"secret\":\"code\",\"certificate\":\"trusted-ca\"}")==200);
    CHECK(config.printers[0].port==8883&&!config.printers[0].region&&!*config.printers[0].username&&!*config.printers[0].client_secret);
    CHECK(api("PUT","/api/manage/printers","slot=0","{\"source\":4,\"serial\":\"FORMNEW\",\"client_id\":\"id\",\"client_secret\":\"secret\"}")==200);
    CHECK(!config.printers[0].port&&!*config.printers[0].host&&!*config.printers[0].certificate&&!*config.printers[0].secret);
    CHECK(api("PUT","/api/manage/printers","slot=0","{\"source\":1,\"serial\":\"sensor.ha_bambu\"}")==200);CHECK(!*config.printers[0].client_id&&!*config.printers[0].client_secret);
    CHECK(api("PUT","/api/manage/printers","slot=0","{\"source\":0}")==200);CHECK(!memcmp(&config.printers[0],&(rp_home_printer_config_t){0},sizeof(config.printers[0])));
    config=before;store_fail=true;CHECK(api("DELETE","/api/manage/printers","slot=1",NULL)==507);CHECK(!memcmp(&config,&before,sizeof(config)));store_fail=false;
    CHECK(api("DELETE","/api/manage/printers","slot=1",NULL)==200);CHECK(!config.printers[1].source&&!memcmp(&config.printers[0],&before.printers[0],sizeof(config.printers[0]))&&!memcmp(&config.printers[2],&before.printers[2],sizeof(config.printers[0])));
    const char *bad_queries[]={NULL,"slot=3","slot=-1","slot=0&slot=1","slot=nan"};
    for(unsigned i=0;i<sizeof(bad_queries)/sizeof(*bad_queries);i++)CHECK(api("DELETE","/api/manage/printers",bad_queries[i],NULL)==400);
    config=before;
    CHECK(api("GET","/api/manage/ha/entities","offset=24&limit=24",NULL)==200);j=response();CHECK(cJSON_GetArraySize(get(j,"items"))==24);CHECK(get(j,"total")->valueint==96);CHECK(cJSON_GetArraySize(get(j,"rooms"))==24);cJSON_Delete(j);
    CHECK(api("GET","/api/manage/ha/entities","kind=4&limit=3",NULL)==200);j=response();CHECK(get(j,"total")->valueint==16);CHECK(cJSON_GetArraySize(get(j,"items"))==3);CHECK(cJSON_IsNull(get(cJSON_GetArrayItem(get(j,"items"),0),"number")));cJSON_Delete(j);
    CHECK(api("GET","/api/manage/ha/entities","domain=sensor&room=%E6%88%BF%E9%97%B4%201",NULL)==200);j=response();CHECK(get(j,"total")->valueint==2);cJSON_Delete(j);
    CHECK(api("GET","/api/manage/ha/entities","q=LIGHT.FIXTURE_0",NULL)==200);j=response();CHECK(get(j,"total")->valueint==1);cJSON_Delete(j);
    const char *bad_filter[]={"limit=0","limit=25","kind=6","domain=lock","offset=97","room=%00","q=%xz","kind=0&kind=1"};
    for(unsigned i=0;i<sizeof(bad_filter)/sizeof(*bad_filter);i++)CHECK(api("GET","/api/manage/ha/entities",bad_filter[i],NULL)==400);
    CHECK(api("POST","/api/manage/ha/control",NULL,"{\"entity\":\"light.fixture_0\",\"action\":\"turn_off\"}")==202);CHECK(controls==1&&control_home&&!strcmp(last_action,"turn_off"));
    CHECK(api("POST","/api/manage/ha/control",NULL,"{\"entity\":\"climate.fixture_2\",\"action\":\"set_temperature\",\"value\":26}")==202);
    CHECK(api("POST","/api/manage/ha/control",NULL,"{\"entity\":\"sensor.fixture_4\",\"action\":\"turn_off\"}")==403);
    CHECK(api("POST","/api/manage/ha/control",NULL,"{\"entity\":\"light.missing\",\"action\":\"turn_off\"}")==404);
    CHECK(api("POST","/api/manage/ha/control",NULL,"{\"entity\":\"light.fixture_0\",\"action\":\"toggle\"}")==400);
    CHECK(api("POST","/api/manage/ha/control",NULL,"{\"entity\":\"climate.fixture_2\",\"action\":\"set_temperature\",\"value\":41}")==400);
    const char *body="{\"entity\":\"light.fixture_0\",\"action\":\"turn_on\"}";
    CHECK(call("POST","/api/manage/ha/control",NULL,body,true,false,sizeof(output)).http_status==403);
    config.external_control=1;CHECK(call("POST","/api/manage/ha/control",NULL,body,true,false,sizeof(output)).http_status==202&&!control_home);
    snapshot.external=1;CHECK(api("POST","/api/manage/ha/control",NULL,body)==202&&!control_home);
    config.external_control=0;CHECK(api("POST","/api/manage/ha/control",NULL,body)==403);snapshot.external=0;
    snapshot.busy=1;CHECK(api("POST","/api/manage/ha/control",NULL,body)==409);snapshot.busy=0;
    CHECK(call("POST","/api/manage/ha/control",NULL,body,false,true,sizeof(output)).http_status==503);
    CHECK(api("POST","/api/manage/refresh",NULL,"{}")==202);queue_ok=false;CHECK(api("POST","/api/manage/refresh",NULL,"{}")==409);queue_ok=true;
    CHECK(call("POST","/api/manage/refresh",NULL,"{}",false,true,sizeof(output)).http_status==503);
    const char *bad_bodies[]={"{}junk","[]","{\"local\":\"x\",\"local\":\"y\"}","{\"ha_token\":null}","{\"ha_token\":\"a\\u0000b\"}","{\"enabled\":2}","{\"rooms\":[{\"entity\":\"light.a\",\"room\":\"a\"},{\"entity\":\"light.a\",\"room\":\"b\"}]}","{\"clear\":[\"secret\"]}","{\"foreign\":true}"};
    before=config;int n=stores;
    for(unsigned i=0;i<sizeof(bad_bodies)/sizeof(*bad_bodies);i++){CHECK(api("PUT","/api/manage/home",NULL,bad_bodies[i])==400);CHECK(stores==n&&!memcmp(&config,&before,sizeof(config)));}
    CHECK(api("PATCH","/api/manage/home",NULL,"{}")==405);
    rp_home_web_request_t too_large={.method="PUT",.path="/api/manage/home",.body="{}",.body_bytes=RP_HOME_WEB_BODY_LIMIT+1};
    rp_home_web_response_t limit_status;CHECK(rp_home_web_handle(&too_large,&limit_status,output,sizeof(output))&&limit_status.http_status==413);
    char long_query[RP_HOME_WEB_QUERY_LIMIT+2];memset(long_query,'x',sizeof(long_query)-1);long_query[sizeof(long_query)-1]=0;
    CHECK(api("GET","/api/manage/ha/entities",long_query,NULL)==400);
    char escaped_name[]="{\"name\":\"quote \\\" and \\\\ and \\n newline\"}";
    CHECK(api("PUT","/api/manage/printers","slot=1",escaped_name)==200);CHECK(api("GET","/api/manage/printers",NULL,NULL)==200);CHECK(strstr(output,"quote \\\" and \\\\ and \\u000a newline"));
    const char *new_network="{\"home_wifi\":\"Office\"}";
    rp_home_web_request_t changed_wifi={.method="PUT",.path="/api/manage/home",.body=new_network,.body_bytes=strlen(new_network),.network_online=true,.at_home=false,.current_wifi="Office",.saved_wifi=wifi,.saved_wifi_count=2};
    rp_home_web_response_t changed_status;CHECK(rp_home_web_handle(&changed_wifi,&changed_status,output,sizeof(output)));CHECK(changed_status.http_status==200&&request_home);
    CHECK(call("GET","/api/manage/ha/entities",NULL,NULL,true,true,64).http_status==507);CHECK(strstr(output,"output_limit"));
    CHECK(call("PUT","/api/manage/home",NULL,"{\"foreign\":true}",true,true,32).http_status==507);CHECK(strstr(output,"output_limit"));
    rp_home_web_request_t unknown={.method="GET",.path="/api/other"};rp_home_web_response_t response_status;CHECK(!rp_home_web_handle(&unknown,&response_status,output,sizeof(output)));
    // 手机登录只排队、公开状态不含凭据；导入只接受当前已验证会话中的设备。
    // Phone login only queues work and public state contains no credentials; imports require a current verified device.
    const char *auth="/api/manage/bambu/auth";
    bambu_state=(rp_home_bambu_state_t){.session=7,.revision=1,.phase=RP_BAMBU_IDLE};
    CHECK(api("GET",auth,NULL,NULL)==200);CHECK(strstr(output,"\"session\":7")&&!strstr(output,"PRIVATE_"));
    bambu_state.config_revision=17;bambu_state.saved_count=1;
    bambu_state.saved_accounts[0]=(rp_home_bambu_saved_account_t){.slot=1,.region=1,.printer_count=2,.name="Study & living room"};
    CHECK(api("GET",auth,NULL,NULL)==200);j=response();CHECK(get(j,"config_revision")->valueint==17);
    CHECK(cJSON_GetArraySize(get(j,"saved_accounts"))==1);
    CHECK(get(cJSON_GetArrayItem(get(j,"saved_accounts"),0),"printer_count")->valueint==2);cJSON_Delete(j);
    CHECK(!strstr(output,"u_PRIVATE")&&!strstr(output,"PRIVATE_")&&strstr(output,"Study & living room"));
    CHECK(api("POST",auth,NULL,"{\"action\":\"reuse\",\"session\":7,\"config_revision\":17,\"saved_slot\":1}")==202);
    CHECK(bambu_input.op==RP_BAMBU_REUSE&&bambu_input.saved_slot==1&&bambu_input.config_revision==17&&!*bambu_input.account&&!*bambu_input.password&&!*bambu_input.code);
    CHECK(api("POST",auth,NULL,"{\"action\":\"reuse\",\"session\":7,\"config_revision\":16,\"saved_slot\":1}")==409);
    CHECK(api("POST",auth,NULL,"{\"action\":\"reuse\",\"session\":7,\"config_revision\":17,\"saved_slot\":0}")==409);
    CHECK(api("POST",auth,NULL,"{\"action\":\"login\",\"session\":7,\"region\":1,\"account\":\"test@example.test\",\"password\":\"PRIVATE_PASSWORD\"}")==202);
    CHECK(bambu_input.op==RP_BAMBU_LOGIN&&bambu_input.region==1&&!strcmp(bambu_input.password,"PRIVATE_PASSWORD"));CHECK(!strstr(output,"PRIVATE_")&&!strstr(output,"test@example"));
    CHECK(api("POST",auth,NULL,"{\"action\":\"login\",\"session\":6,\"region\":0,\"account\":\"a@b.c\",\"password\":\"pass\"}")==409);
    CHECK(api("POST",auth,NULL,"{\"action\":\"email_code\",\"session\":7,\"code\":\"123456\"}")==202);CHECK(bambu_input.op==RP_BAMBU_EMAIL_CODE&&!strcmp(bambu_input.code,"123456"));
    const char *invalid_auth[]={"{}","{\"action\":\"login\",\"session\":7,\"region\":2,\"account\":\"x\",\"password\":\"p\"}",
        "{\"action\":\"login\",\"session\":7,\"account\":\"x\",\"password\":\"p\"}","{\"action\":\"login\",\"session\":7,\"region\":0,\"account\":\"x\"}",
        "{\"action\":\"email_code\",\"session\":7,\"code\":\"12ab56\"}","{\"action\":\"email_code\",\"session\":7,\"code\":\"123\"}",
        "{\"action\":\"resend\",\"session\":7,\"password\":\"foreign\"}","{\"action\":\"tfa_code\",\"session\":7,\"code\":\"123456\"}",
        "{\"action\":\"cancel\",\"session\":-1}","{\"action\":\"cancel\",\"session\":4294967296}","{\"action\":\"cancel\",\"session\":1.2}",
        "{\"action\":\"import\",\"session\":7,\"slot\":0}","{\"action\":\"import\",\"session\":7,\"slot\":3,\"index\":0}","{\"action\":\"cancel\",\"session\":7,\"session\":7}",
        "{\"action\":\"reuse\",\"session\":7,\"saved_slot\":1}","{\"action\":\"reuse\",\"session\":7,\"config_revision\":17}",
        "{\"action\":\"reuse\",\"session\":7,\"config_revision\":-1,\"saved_slot\":1}","{\"action\":\"reuse\",\"session\":7,\"config_revision\":4294967296,\"saved_slot\":1}",
        "{\"action\":\"reuse\",\"session\":7,\"config_revision\":1.5,\"saved_slot\":1}","{\"action\":\"reuse\",\"session\":7,\"config_revision\":17,\"saved_slot\":3}",
        "{\"action\":\"reuse\",\"session\":7,\"config_revision\":17,\"saved_slot\":-1}","{\"action\":\"reuse\",\"session\":7,\"config_revision\":17,\"saved_slot\":0.5}",
        "{\"action\":\"reuse\",\"session\":7,\"config_revision\":17,\"saved_slot\":1,\"password\":\"foreign\"}",
        "{\"action\":\"reuse\",\"session\":7,\"config_revision\":17,\"saved_slot\":1,\"saved_slot\":2}"};
    before=config;n=stores;for(unsigned i=0;i<sizeof(invalid_auth)/sizeof(*invalid_auth);i++){CHECK(api("POST",auth,NULL,invalid_auth[i])==400);CHECK(stores==n&&!memcmp(&config,&before,sizeof(config)));}
    CHECK(call("POST",auth,NULL,"{\"action\":\"resend\",\"session\":7}",false,true,sizeof(output)).http_status==503);
    CHECK(api("PUT",auth,NULL,"{}")==405);CHECK(api("GET",auth,"slot=0",NULL)==400);
    bambu_state.phase=RP_BAMBU_READY;bambu_state.count=1;strcpy(bambu_state.devices[0].serial,"BOUND123");strcpy(bambu_state.devices[0].name,"<name>");
    CHECK(api("GET",auth,NULL,NULL)==200);CHECK(strstr(output,"BOUND123")&&!strstr(output,"PRIVATE_"));
    CHECK(call("GET",auth,NULL,NULL,true,true,64).http_status==507);
    CHECK(api("POST",auth,NULL,"{\"action\":\"import\",\"session\":6,\"slot\":0,\"index\":0}")==409);
    CHECK(api("POST",auth,NULL,"{\"action\":\"import\",\"session\":7,\"slot\":0,\"index\":1}")==409);
    store_fail=true;CHECK(api("POST",auth,NULL,"{\"action\":\"import\",\"session\":7,\"slot\":0,\"index\":0}")==507);CHECK(!memcmp(&config,&before,sizeof(config)));store_fail=false;
    CHECK(api("POST",auth,NULL,"{\"action\":\"import\",\"session\":7,\"slot\":0,\"index\":0,\"name\":\"My Bambu\"}")==200);
    CHECK(config.printers[0].source==RP_HOME_BAMBU_CLOUD&&!strcmp(config.printers[0].name,"My Bambu")&&!strcmp(config.printers[0].secret,"PRIVATE_AUTH_TOKEN"));
    CHECK(!memcmp(&config.printers[1],&before.printers[1],sizeof(config.printers[1]))&&!memcmp(&config.printers[2],&before.printers[2],sizeof(config.printers[2])));CHECK(!strstr(output,"PRIVATE_"));
    // 新登录只更新同区域同身份凭据，保留名称/序列号；写失败必须保持所有配置。
    // A fresh login updates only the same regional identity, retaining names/serials; failed saves preserve every profile.
    config.printers[1]=config.printers[0];strcpy(config.printers[1].name,"Existing shared printer");strcpy(config.printers[1].serial,"OTHER123");strcpy(config.printers[1].secret,"OLDER_SHARED_TOKEN");
    config.printers[2]=config.printers[1];config.printers[2].region=0;strcpy(config.printers[2].serial,"GLOBAL123");strcpy(config.printers[2].secret,"GLOBAL_SECRET");
    before=config;store_fail=true;
    CHECK(api("POST",auth,NULL,"{\"action\":\"import\",\"session\":7,\"slot\":0,\"index\":0}")==507);
    CHECK(!memcmp(&config,&before,sizeof(config))&&bambu_state.phase==RP_BAMBU_READY);store_fail=false;
    CHECK(api("POST",auth,NULL,"{\"action\":\"import\",\"session\":7,\"slot\":0,\"index\":0}")==200);
    rp_home_printer_config_t expected=before.printers[1];memset(expected.secret,0,sizeof(expected.secret));strcpy(expected.secret,"PRIVATE_AUTH_TOKEN");
    CHECK(!memcmp(&config.printers[1],&expected,sizeof(expected)));
    CHECK(!memcmp(&config.printers[2],&before.printers[2],sizeof(expected)));
    CHECK(bambu_state.session==7&&bambu_state.phase==RP_BAMBU_READY);
    // 复用会话不使其它槽的更新凭据倒退；同区域的不同账户也始终隔离。
    // Reused sessions never downgrade sibling credentials, and distinct accounts in one region remain isolated.
    bambu_state.reused=1;strcpy(config.printers[1].secret,"NEWER_SHARED_TOKEN");before=config;
    CHECK(api("POST",auth,NULL,"{\"action\":\"import\",\"session\":7,\"slot\":0,\"index\":0}")==200);
    CHECK(!memcmp(&config.printers[1],&before.printers[1],sizeof(expected))&&!memcmp(&config.printers[2],&before.printers[2],sizeof(expected)));
    CHECK(api("GET",auth,NULL,NULL)==200);CHECK(strstr(output,"\"reused\":true")&&!strstr(output,"PRIVATE_")&&!strstr(output,"NEWER_SHARED_TOKEN"));
    bambu_state.reused=0;strcpy(config.printers[1].username,"u_ANOTHER_ACCOUNT");before=config;
    CHECK(api("POST",auth,NULL,"{\"action\":\"import\",\"session\":7,\"slot\":0,\"index\":0}")==200);
    CHECK(!memcmp(&config.printers[1],&before.printers[1],sizeof(expected))&&!memcmp(&config.printers[2],&before.printers[2],sizeof(expected)));
    CHECK(api("POST",auth,NULL,"{\"action\":\"cancel\",\"session\":7}")==200);CHECK(bambu_state.phase==RP_BAMBU_IDLE&&bambu_state.session==8);
    CHECK(api("POST",auth,NULL,"{\"action\":\"cancel\",\"session\":7}")==409);
    printf("home-link-web: %u API checks + %u output canaries passed; masked GET, source isolation, save failures, 96-entity paging, explicit controls, async refresh\n",checks,canaries);
    return 0;
}

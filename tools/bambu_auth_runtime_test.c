/* SPDX-License-Identifier: Apache-2.0
 * 原样运行生产会话/工作线程，仅以固定样本替换TLS边界。
 * Run production sessions/threads verbatim, replacing only TLS with fixtures.
 */
#include "read_pico_home_link.h"
static rp_home_result_t fixture_http(void *, const rp_home_http_request_t *, char *, size_t, size_t *, int *);
#include "bambu_runtime_test.inc"
#include <assert.h>

static unsigned checks;
#define CHECK(x) do { ++checks; assert(x); } while (0)
static pthread_mutex_t fixture_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t fixture_ready = PTHREAD_COND_INITIALIZER;
static int fixture_mode, fixture_calls, control_calls, login_calls;
static bool fixture_blocked, fixture_entered, fixture_block_reuse, fixture_many;
static bool all_zero(const void *p, size_t n) { const unsigned char *v=p; for(size_t i=0;i<n;++i)if(v[i])return false;return true; }
static void mode(int value) { pthread_mutex_lock(&fixture_lock); fixture_mode=value; pthread_mutex_unlock(&fixture_lock); }
static rp_home_result_t fixture_http(void *unused, const rp_home_http_request_t *r, char *out, size_t capacity, size_t *bytes, int *status) {
    (void)unused; const char *reply="{}"; *status=200;
    pthread_mutex_lock(&fixture_lock); fixture_calls++; int current=fixture_mode;
    bool login=strstr(r->url,"/user/login")!=NULL, preference=strstr(r->url,"/my/preference")!=NULL;
    if(login)login_calls++;
    if(login||(fixture_block_reuse&&preference)){fixture_entered=true;pthread_cond_broadcast(&fixture_ready);while(fixture_blocked)pthread_cond_wait(&fixture_ready,&fixture_lock);}
    bool many=fixture_many;
    if(strstr(r->url,"/api/services/"))control_calls++;
    pthread_mutex_unlock(&fixture_lock);
    if(strstr(r->url,"/user/login")) {
        if(current==1)reply="{\"loginType\":\"verifyCode\"}";
        else if(current==2)reply="{\"loginType\":\"tfa\",\"tfaKey\":\"NOT_RETAINED\"}";
        else if(current==3){*status=400;reply="{\"code\":2}";}
        else reply="{\"accessToken\":\"runtime-fixture-token\"}";
    }else if(preference){
        reply=r->bearer&&!strcmp(r->bearer,"other-account-token")?"{\"uid\":2222222222}":"{\"uid\":1234567890}";
        if(current==4)reply="{\"uid\":9999999999}";else if(current==5)*status=401;
    }else if(strstr(r->url,"/api/user/bind")){
        reply=many?"{\"devices\":[{\"dev_id\":\"PRINTER123\",\"name\":\"试验打印机\"},{\"dev_id\":\"SECOND123\",\"name\":\"第二台\"},{\"dev_id\":\"THIRD123\",\"name\":\"第三台\"}]}":"{\"devices\":[{\"dev_id\":\"PRINTER123\",\"name\":\"试验打印机\",\"dev_product_name\":\"P1S\"}]}";
        if(current==6)*status=401;
    }
    else if(strstr(r->url,"/api/states"))reply="[{\"entity_id\":\"light.test\",\"state\":\"on\",\"attributes\":{}}]";
    *bytes=strlen(reply);assert(*bytes<capacity);memcpy(out,reply,*bytes+1);return RP_HOME_OK;
}
static rp_home_bambu_state_t public_state(void) { rp_home_bambu_state_t s;read_pico_bambu_auth_get(&s);return s; }
static rp_home_bambu_state_t wait_phase(unsigned phase) {
    for(unsigned i=0;i<2000;++i){rp_home_bambu_state_t s=public_state();if(s.phase==phase)return s;pause_ms(1);}
    CHECK(0);return public_state();
}
static void wait_idle_worker(void) {
    for(unsigned i=0;i<2000;++i){lock();bool busy=state->busy||state->auth_pending||state->queued;unlock();if(!busy)return;pause_ms(1);}CHECK(0);
}
static bool login_now(void) {
    rp_home_bambu_state_t s=public_state();rp_home_bambu_input_t input={.session=s.session,.op=RP_BAMBU_LOGIN,.account="fixture@example.invalid",.password="not-persisted"};
    return read_pico_bambu_auth_submit(&input,true);
}
static bool step(unsigned op,const char *code) {
    rp_home_bambu_input_t input={.session=public_state().session,.op=(uint8_t)op};if(code)snprintf(input.code,sizeof(input.code),"%s",code);
    return read_pico_bambu_auth_submit(&input,true);
}
static void block_login(void) {
    pthread_mutex_lock(&fixture_lock);fixture_blocked=true;fixture_entered=false;fixture_block_reuse=false;pthread_mutex_unlock(&fixture_lock);
}
static void block_reuse(void) {block_login();pthread_mutex_lock(&fixture_lock);fixture_block_reuse=true;pthread_mutex_unlock(&fixture_lock);}
static void wait_entered(void) {
    for(unsigned i=0;i<2000;++i){pthread_mutex_lock(&fixture_lock);bool entered=fixture_entered;pthread_mutex_unlock(&fixture_lock);if(entered)return;pause_ms(1);}CHECK(0);
}
static void unblock(void) {pthread_mutex_lock(&fixture_lock);fixture_blocked=false;pthread_cond_broadcast(&fixture_ready);pthread_mutex_unlock(&fixture_lock);}
static bool reuse_saved(unsigned slot) {
    rp_home_bambu_state_t s=public_state();rp_home_bambu_input_t input={.session=s.session,.op=RP_BAMBU_REUSE,.saved_slot=(uint8_t)slot,.config_revision=s.config_revision};
    return read_pico_bambu_auth_submit(&input,true);
}
int main(int argc,char **argv) {
    (void)http_request;
    read_pico_home_link_init();CHECK(state);
    if(argc==2) {
        rp_home_bambu_state_t saved=public_state();rp_home_config_t cfg;read_pico_home_link_get_config(&cfg);
        if(!strcmp(argv[1],"persist-seed")) {
            CHECK(!saved.saved_count&&login_now());saved=wait_phase(RP_BAMBU_READY);
            CHECK(read_pico_bambu_auth_prepare_import(saved.session,0,&cfg.printers[0]));CHECK(read_pico_home_link_configure(&cfg));
            CHECK(public_state().saved_count==1);printf("Bambu restart seed: %u checks passed\n",checks);return 0;
        }
        CHECK(!strcmp(argv[1],"persist-load")&&saved.phase==RP_BAMBU_IDLE&&saved.saved_count==1&&!*state->auth.token);
        CHECK(reuse_saved(saved.saved_accounts[0].slot));saved=wait_phase(RP_BAMBU_READY);
        CHECK(saved.reused&&saved.count==1&&!login_calls);
        rp_home_printer_config_t printer;CHECK(read_pico_bambu_auth_prepare_import(saved.session,0,&printer));
        CHECK(!strcmp(printer.secret,cfg.printers[0].secret)&&!strcmp(printer.username,cfg.printers[0].username));
        printf("Bambu restart reuse: %u checks passed, no login requests\n",checks);return 0;
    }
    CHECK(!state->config.enabled);
    rp_home_bambu_state_t first=public_state();CHECK(first.phase==RP_BAMBU_IDLE&&first.session);
    rp_home_bambu_input_t offline={.session=first.session,.op=RP_BAMBU_LOGIN,.account="fixture@example.invalid",.password="not-persisted"};
    CHECK(!read_pico_bambu_auth_submit(&offline,false));
    CHECK(login_now());rp_home_bambu_state_t ready=wait_phase(RP_BAMBU_READY);CHECK(ready.session!=first.session&&ready.count==1&&ready.seconds_left<=300);
    CHECK(!read_pico_bambu_auth_submit(&offline,true));CHECK(!read_pico_bambu_auth_cancel_session(first.session));
    rp_home_printer_config_t printer;CHECK(read_pico_bambu_auth_prepare_import(ready.session,0,&printer));
    CHECK(printer.source==RP_HOME_BAMBU_CLOUD&&!strcmp(printer.serial,"PRINTER123")&&!strcmp(printer.username,"u_1234567890")&&!strcmp(printer.secret,"runtime-fixture-token"));
    rp_home_config_t config;read_pico_home_link_get_config(&config);CHECK(!config.enabled&&config.printers[0].source==RP_HOME_NONE);
    config.printers[0]=printer;CHECK(rp_home_config_valid(&config));rp_home_bambu_wipe(&printer,sizeof(printer));
    CHECK(!read_pico_bambu_auth_prepare_import(ready.session,1,&printer)&&all_zero(&printer,sizeof(printer)));
    CHECK(!read_pico_bambu_auth_prepare_import(first.session,0,&printer)&&all_zero(&printer,sizeof(printer)));
    lock();CHECK(all_zero(&state->auth_input,sizeof(state->auth_input)));unlock();
    CHECK(read_pico_bambu_auth_cancel_session(ready.session));CHECK(public_state().phase==RP_BAMBU_IDLE);
    mode(1);CHECK(login_now());rp_home_bambu_state_t code=wait_phase(RP_BAMBU_EMAIL);CHECK(code.retry_after==0);
    CHECK(step(RP_BAMBU_RESEND,NULL));code=wait_phase(RP_BAMBU_EMAIL);CHECK(code.retry_after>0&&code.retry_after<=60);
    CHECK(!step(RP_BAMBU_RESEND,NULL));mode(3);CHECK(step(RP_BAMBU_EMAIL_CODE,"123456"));code=wait_phase(RP_BAMBU_EMAIL);CHECK(code.error==RP_BAMBU_ERROR_CODE_INCORRECT);
    lock();state->auth_retry=now_ms()-1;unlock();CHECK(step(RP_BAMBU_RESEND,NULL));code=wait_phase(RP_BAMBU_EMAIL);CHECK(code.retry_after>0&&!step(RP_BAMBU_RESEND,NULL));
    mode(0);CHECK(step(RP_BAMBU_EMAIL_CODE,"123456"));ready=wait_phase(RP_BAMBU_READY);CHECK(ready.result==RP_HOME_OK);
    mode(2);CHECK(login_now());wait_phase(RP_BAMBU_TFA);pthread_mutex_lock(&fixture_lock);int calls=fixture_calls;pthread_mutex_unlock(&fixture_lock);
    CHECK(step(RP_BAMBU_TFA_CODE,"123456"));code=wait_phase(RP_BAMBU_TFA);CHECK(code.error==RP_BAMBU_ERROR_TFA_UNSUPPORTED);
    pthread_mutex_lock(&fixture_lock);CHECK(fixture_calls==calls);pthread_mutex_unlock(&fixture_lock);
    CHECK(step(RP_BAMBU_RESEND,NULL));wait_phase(RP_BAMBU_EMAIL);
    mode(0);block_login();CHECK(login_now());wait_entered();uint32_t cancelled=public_state().session;read_pico_bambu_auth_cancel();
    CHECK(public_state().session!=cancelled&&public_state().phase==RP_BAMBU_IDLE);unblock();wait_idle_worker();CHECK(public_state().phase==RP_BAMBU_IDLE);
    // 新账户任务替换在途旧任务；旧结果不能覆盖新会话，也不能擦除新任务。
    // A new login replaces an in-flight session; stale completion cannot overwrite it or erase the new queued job.
    block_login();CHECK(login_now());wait_entered();uint32_t previous=public_state().session;CHECK(login_now());CHECK(public_state().session!=previous);unblock();ready=wait_phase(RP_BAMBU_READY);CHECK(ready.session!=previous);
    // 不调用GET，工作器仅在绝对到期时醒一次，主动清除RAM令牌。
    // Without GET polling, the worker wakes once at the absolute deadline and wipes the RAM token.
    wait_idle_worker();lock();state->auth_expires=now_ms()+40;pthread_cond_signal(&worker_ready);unlock();
    for(unsigned i=0;i<2000;++i){lock();bool expired=state->auth.public.phase==RP_BAMBU_EXPIRED;unlock();if(expired)break;pause_ms(1);}
    lock();CHECK(state->auth.public.phase==RP_BAMBU_EXPIRED&&!*state->auth.token&&!*state->auth.account&&!state->auth_expires);uint32_t expired_revision=state->auth.public.revision;unlock();
    pause_ms(50);lock();CHECK(state->auth.public.revision==expired_revision);unlock();CHECK(!read_pico_bambu_auth_prepare_import(ready.session,0,&printer));
    // 认证优先调度后仍执行已排队的HA控制，不消耗它的queued/control_pending。
    // Auth priority preserves and subsequently executes queued HA control.
    config=(rp_home_config_t){.version=RP_HOME_CONFIG_VERSION,.enabled=1,.local="http://ha.fixture",.ha_token="fixture-ha-token"};CHECK(read_pico_home_link_configure(&config));
    block_login();CHECK(login_now());wait_entered();
    lock();state->control_entity=(rp_home_entity_t){.id="light.test",.kind=RP_HOME_LIGHT,.available=1};strcpy(state->control_action,"turn_on");state->control_pending=state->queued=true;state->at_home=true;unlock();
    unblock();wait_phase(RP_BAMBU_READY);wait_idle_worker();pthread_mutex_lock(&fixture_lock);CHECK(control_calls==1);pthread_mutex_unlock(&fixture_lock);
    block_login();CHECK(login_now());wait_entered();uint32_t suspended=public_state().session;CHECK(!read_pico_home_link_suspend(1));CHECK(public_state().session!=suspended&&public_state().phase==RP_BAMBU_IDLE);
    unblock();wait_idle_worker();read_pico_home_link_resume();CHECK(public_state().phase==RP_BAMBU_IDLE);
    CHECK(login_now());wait_phase(RP_BAMBU_READY);CHECK(!read_pico_home_link_request(false,true));CHECK(public_state().phase==RP_BAMBU_IDLE);
    lock();CHECK(!*state->auth.token&&!*state->auth.account&&all_zero(&state->auth_input,sizeof(state->auth_input)));unlock();
    // 登录一次，连续导入三个槽位，随后RAM会话消失仍可使用NVS内的已保存账户。
    // One login imports all three slots; a later RAM-session loss can reuse the account already stored in NVS.
    mode(0);pthread_mutex_lock(&fixture_lock);fixture_many=true;int logins=login_calls;pthread_mutex_unlock(&fixture_lock);
    CHECK(login_now());ready=wait_phase(RP_BAMBU_READY);CHECK(ready.count==3&&!ready.reused);
    config=(rp_home_config_t){.version=RP_HOME_CONFIG_VERSION};
    for(unsigned i=0;i<3;++i){CHECK(read_pico_bambu_auth_prepare_import(ready.session,i,&config.printers[i]));CHECK(read_pico_home_link_configure(&config));}
    ready=public_state();CHECK(ready.phase==RP_BAMBU_READY&&ready.saved_count==1&&ready.saved_accounts[0].slot==0&&ready.saved_accounts[0].printer_count==3);
    pthread_mutex_lock(&fixture_lock);CHECK(login_calls==logins+1);logins=login_calls;pthread_mutex_unlock(&fixture_lock);
    read_pico_bambu_auth_cancel();CHECK(reuse_saved(0));ready=wait_phase(RP_BAMBU_READY);CHECK(ready.reused&&ready.count==3);
    lock();state->auth_expires=now_ms()-1;unlock();CHECK(public_state().phase==RP_BAMBU_EXPIRED);CHECK(reuse_saved(0));ready=wait_phase(RP_BAMBU_READY);CHECK(ready.reused);
    pthread_mutex_lock(&fixture_lock);CHECK(login_calls==logins);pthread_mutex_unlock(&fixture_lock);
    // 同账号不同历史token仍只有一个入口；账号和区域分别形成独立来源。
    // Historical token variants share one entry; identity and region remain separate account sources.
    strcpy(config.printers[1].secret,"older-valid-token");config.printers[2].region=1;CHECK(read_pico_home_link_configure(&config));
    ready=public_state();CHECK(ready.saved_count==2&&ready.saved_accounts[0].printer_count==2&&ready.saved_accounts[1].slot==2&&ready.saved_accounts[1].region==1);
    CHECK(!reuse_saved(1));CHECK(reuse_saved(2));ready=wait_phase(RP_BAMBU_READY);CHECK(ready.reused&&ready.region==1);
    config.printers[2].region=0;strcpy(config.printers[2].username,"u_2222222222");strcpy(config.printers[2].secret,"other-account-token");CHECK(read_pico_home_link_configure(&config));
    ready=public_state();CHECK(ready.saved_count==2&&ready.saved_accounts[1].region==0);CHECK(reuse_saved(2));ready=wait_phase(RP_BAMBU_READY);
    CHECK(read_pico_bambu_auth_prepare_import(ready.session,0,&printer)&&!strcmp(printer.username,"u_2222222222"));
    rp_home_bambu_input_t stale={.session=ready.session,.op=RP_BAMBU_REUSE,.config_revision=ready.config_revision,.saved_slot=0};
    strcpy(config.printers[0].name,"renamed");CHECK(read_pico_home_link_configure(&config));CHECK(!read_pico_bambu_auth_submit(&stale,true));
    memset(&config.printers[2],0,sizeof(config.printers[2]));config.printers[2].source=RP_HOME_HA;strcpy(config.printers[2].serial,"printer_fixture");CHECK(read_pico_home_link_configure(&config));
    CHECK(public_state().saved_count==1&&!reuse_saved(2));
    for(int failure_mode=4;failure_mode<=6;++failure_mode) {
        mode(failure_mode);CHECK(reuse_saved(0));ready=wait_phase(RP_BAMBU_ERROR);
        CHECK(ready.reused&&ready.result==RP_HOME_AUTH&&ready.error==RP_BAMBU_ERROR_CREDENTIALS&&!ready.count);
        lock();CHECK(!*state->auth.token&&!*state->auth.username);unlock();
        rp_home_config_t unchanged;read_pico_home_link_get_config(&unchanged);CHECK(!memcmp(&unchanged,&config,sizeof(config)));
    }
    mode(0);block_reuse();CHECK(reuse_saved(0));wait_entered();uint32_t reuse_session=public_state().session;
    strcpy(config.printers[0].name,"changed while checking");CHECK(read_pico_home_link_configure(&config));unblock();ready=wait_phase(RP_BAMBU_ERROR);
    CHECK(ready.session!=reuse_session&&ready.error==RP_BAMBU_ERROR_ACCOUNT_CHANGED&&!read_pico_bambu_auth_prepare_import(reuse_session,0,&printer));
    // 旧账户复用任务尚在队列时若配置变化，不允许它发起任何新请求。
    // A queued reuse whose configuration changed cannot initiate new requests.
    block_login();CHECK(login_now());wait_entered();CHECK(reuse_saved(0));strcpy(config.printers[0].name,"changed before checking");CHECK(read_pico_home_link_configure(&config));
    pthread_mutex_lock(&fixture_lock);int queued_calls=fixture_calls;pthread_mutex_unlock(&fixture_lock);unblock();ready=wait_phase(RP_BAMBU_ERROR);CHECK(ready.error==RP_BAMBU_ERROR_ACCOUNT_CHANGED);
    pthread_mutex_lock(&fixture_lock);CHECK(fixture_calls==queued_calls+2);pthread_mutex_unlock(&fixture_lock);
    block_reuse();CHECK(reuse_saved(0));wait_entered();read_pico_bambu_auth_cancel();unblock();wait_idle_worker();CHECK(public_state().phase==RP_BAMBU_IDLE&&!public_state().reused);
    mode(0);CHECK(login_now());ready=wait_phase(RP_BAMBU_READY);CHECK(!ready.reused);
    printf("Bambu runtime fixtures: %u checks passed\n",checks);return 0;
}

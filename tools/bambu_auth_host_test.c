/* SPDX-License-Identifier: Apache-2.0
 * 登录协议的离线固定样本测试，不登录任何生产账户。
 * Offline protocol fixtures, never a production-account login.
 */
#include "bambu_auth_internal.h"
#include "home_link_json.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned checks;
#define CHECK(x) do { ++checks; assert(x); } while (0)
static struct { int mode, calls, region, password_requests, code_requests, sms_requests, email_requests; const char *bind, *uid, *token, *account; } fixture;
static rp_home_result_t http(void *unused, const rp_home_http_request_t *r, char *out, size_t cap, size_t *bytes, int *status) {
    (void)unused; fixture.calls++; *status = 200; const char *reply = "{}";
    CHECK(!strncmp(r->url, "https://", 8));
    if (strstr(r->url, "/user/login")) {
        CHECK(!strcmp(r->method, "POST") && !r->bearer && !strcmp(r->content_type, "application/json"));
        CHECK(strstr(r->url, fixture.region ? "api.bambulab.cn/" : "api.bambulab.com/"));
        cJSON *j = cJSON_Parse(r->body); CHECK(j); cJSON *pw = cJSON_GetObjectItemCaseSensitive(j, "password");
        cJSON *account = cJSON_GetObjectItemCaseSensitive(j, "account"); CHECK(cJSON_IsString(account));
        if (fixture.account) CHECK(!strcmp(account->valuestring, fixture.account));
        if (pw) { fixture.password_requests++; CHECK(!strcmp(pw->valuestring, "pass\"word\\秘密")); CHECK(cJSON_GetObjectItemCaseSensitive(j, "apiError")); CHECK(cJSON_GetArraySize(j)==3); }
        else { fixture.code_requests++; CHECK(cJSON_IsString(cJSON_GetObjectItemCaseSensitive(j, "code"))); CHECK(cJSON_GetArraySize(j)==2); }
        cJSON_Delete(j);
        if (fixture.mode == 1) reply = "{\"loginType\":\"verifyCode\"}";
        else if (fixture.mode == 2) reply = "{\"loginType\":\"tfa\",\"tfaKey\":\"NEVER_RETAIN\"}";
        else if (fixture.mode == 3) { *status = 400; reply = "{\"code\":2}"; }
        else if (fixture.mode == 4) { *status = 400; reply = "{\"code\":1}"; }
        else if (fixture.mode == 5) { *status = 403; reply = "<html>cloudflare challenge</html>"; }
        else if (fixture.mode == 6) { *status = 429; reply = "{\"error\":\"slow down\"}"; }
        else if (fixture.mode == 7) reply = "{\"accessToken\":\"bad\\r\\ntoken\"}";
        else if (fixture.mode == 8) reply = "{\"accessToken\":\"first\",\"accessToken\":\"second\"}";
        else if (fixture.mode == 9) reply = "{\"accessToken\":\"opaque\\u0000tail\"}";
        else reply = fixture.token ? fixture.token : "{\"accessToken\":\"opaque-fixture-token\"}";
    } else if (strstr(r->url, "sendemail/code") || strstr(r->url, "sendsmscode")) {
        CHECK(!r->bearer && !strcmp(r->method, "POST") && !strcmp(r->content_type, "application/json"));
        cJSON *j = cJSON_Parse(r->body); CHECK(j && cJSON_GetArraySize(j)==2);
        cJSON *kind = cJSON_GetObjectItemCaseSensitive(j, "type"); CHECK(cJSON_IsString(kind) && !strcmp(kind->valuestring, "codeLogin"));
        cJSON *phone = cJSON_GetObjectItemCaseSensitive(j, "phone"), *email = cJSON_GetObjectItemCaseSensitive(j, "email");
        CHECK(cJSON_IsString(phone) != cJSON_IsString(email));
        if (phone) { fixture.sms_requests++; CHECK(!strcmp(r->url, "https://bambulab.cn/api/v1/user-service/user/sendsmscode")); }
        else { fixture.email_requests++; CHECK(strstr(r->url, fixture.region ? "https://api.bambulab.cn/v1/user-service/user/sendemail/code" : "https://api.bambulab.com/v1/user-service/user/sendemail/code")); }
        if (fixture.account) CHECK(!strcmp((phone ? phone : email)->valuestring, fixture.account));
        cJSON_Delete(j);
    } else if (strstr(r->url, "/my/preference")) {
        CHECK(!strcmp(r->method, "GET") && !r->body && r->bearer && !strcmp(r->bearer, "opaque-fixture-token"));
        reply = fixture.uid ? fixture.uid : "{\"uid\":\"1234567890\"}";
        if (fixture.mode == 10) *status = 401;
    } else if (strstr(r->url, "/api/user/bind")) {
        CHECK(!strcmp(r->method, "GET") && !r->body && r->bearer && !strcmp(r->bearer, "opaque-fixture-token"));
        reply = fixture.bind ? fixture.bind : "{\"devices\":[{\"dev_id\":\"PRINTER123\",\"name\":\"工作室打印机\",\"dev_product_name\":\"P1S\",\"dev_access_code\":\"NOT_RETAINED\"}]}";
        if (fixture.mode == 11) *status = 401;
    } else CHECK(0);
    *bytes = strlen(reply); CHECK(*bytes < cap); memcpy(out, reply, *bytes + 1); return RP_HOME_OK;
}
static rp_home_bambu_session_t session;
static rp_home_bambu_input_t login = {.op=RP_BAMBU_LOGIN,.account="fixture@example.invalid",.password="pass\"word\\秘密"};
static void run(rp_home_bambu_input_t input) { rp_home_transport_t transport = {.http=http}; rp_home_bambu_execute(&input, &session, &transport); }
static void reset(void) { memset(&fixture, 0, sizeof(fixture)); memset(&session, 0, sizeof(session)); }
int main(void) {
    CHECK(rp_home_bambu_input_valid(&login)); rp_home_bambu_input_t bad=login;
    memset(bad.password, 'x', sizeof(bad.password)); CHECK(!rp_home_bambu_input_valid(&bad));
    bad=login; strcpy(bad.account,"bad\r\naccount"); CHECK(!rp_home_bambu_input_valid(&bad));
    bad=(rp_home_bambu_input_t){.op=RP_BAMBU_EMAIL_CODE,.code="123456"}; CHECK(rp_home_bambu_input_valid(&bad));
    strcpy(bad.code,"123"); CHECK(!rp_home_bambu_input_valid(&bad)); strcpy(bad.code,"1234567890123"); CHECK(!rp_home_bambu_input_valid(&bad));
    strcpy(bad.code,"12345a"); CHECK(!rp_home_bambu_input_valid(&bad));
    reset(); run(login); CHECK(session.public.phase==RP_BAMBU_READY && session.public.result==RP_HOME_OK && fixture.calls==3);
    CHECK(!strcmp(session.username,"u_1234567890") && !strcmp(session.token,"opaque-fixture-token"));
    CHECK(session.public.count==1 && !strcmp(session.public.devices[0].serial,"PRINTER123"));
    for (int region=0;region<2;++region) { reset(); fixture.region=region; rp_home_bambu_input_t input=login; input.region=(uint8_t)region; run(input); CHECK(session.public.phase==RP_BAMBU_READY); }
    for (int mode=1;mode<=9;++mode) {
        reset(); fixture.mode=mode; run(login);
        CHECK(session.public.phase==(mode==1?RP_BAMBU_EMAIL:mode==2?RP_BAMBU_TFA:RP_BAMBU_ERROR));
        CHECK(!*session.token && !*session.username);
        if(mode==5)CHECK(session.public.error==RP_BAMBU_ERROR_BLOCKED); if(mode==6)CHECK(session.public.error==RP_BAMBU_ERROR_RATE_LIMIT);
    }
    for(int mode=3;mode<=4;++mode) {
        reset(); fixture.mode=1; run(login); fixture.mode=mode;
        run((rp_home_bambu_input_t){.op=RP_BAMBU_EMAIL_CODE,.code="123456"});
        CHECK(session.public.phase==RP_BAMBU_EMAIL && session.public.error==(mode==3?RP_BAMBU_ERROR_CODE_INCORRECT:RP_BAMBU_ERROR_CODE_EXPIRED));
    }
    reset(); fixture.mode=2; run(login); int before=fixture.calls;
    run((rp_home_bambu_input_t){.op=RP_BAMBU_TFA_CODE,.code="123456"}); CHECK(fixture.calls==before && session.public.error==RP_BAMBU_ERROR_TFA_UNSUPPORTED);
    run((rp_home_bambu_input_t){.op=RP_BAMBU_RESEND}); CHECK(session.public.phase==RP_BAMBU_EMAIL);
    strcpy(session.account,"+8613800000000"); run((rp_home_bambu_input_t){.op=RP_BAMBU_RESEND}); CHECK(session.public.phase==RP_BAMBU_EMAIL);
    // 中国区手机号原样用于密码、短信发送与验证码提交；进入验证码阶段本身不替用户发送短信。
    // Preserve mainland phone input through password, explicit SMS and code submission; a challenge alone never sends an SMS.
    const char *phones[]={"13800000000", "+8613800000000"};
    for(unsigned i=0;i<sizeof(phones)/sizeof(*phones);++i) {
        reset(); fixture.region=1; fixture.account=phones[i]; fixture.mode=1;
        rp_home_bambu_input_t phone_login=login; phone_login.region=1; strcpy(phone_login.account,phones[i]); run(phone_login);
        CHECK(session.public.phase==RP_BAMBU_EMAIL && session.public.region==1 && !strcmp(session.account,phones[i]));
        CHECK(fixture.password_requests==1 && !fixture.sms_requests && !fixture.email_requests && !fixture.code_requests);
        run((rp_home_bambu_input_t){.op=RP_BAMBU_RESEND});
        CHECK(session.public.phase==RP_BAMBU_EMAIL && fixture.sms_requests==1 && !fixture.email_requests);
        fixture.mode=0; run((rp_home_bambu_input_t){.op=RP_BAMBU_EMAIL_CODE,.code="123456"});
        CHECK(session.public.phase==RP_BAMBU_READY && session.public.region==1 && fixture.calls==5);
        CHECK(fixture.password_requests==1 && fixture.sms_requests==1 && fixture.code_requests==1);
    }
    // 中国区邮箱仍走邮箱发送，不因区域或验证器回退而切换收件目标。
    // Mainland email remains email delivery, including authenticator fallback, without changing the recipient.
    reset(); fixture.region=1; fixture.account="fixture@example.invalid"; fixture.mode=2;
    rp_home_bambu_input_t china_email=login; china_email.region=1; run(china_email);
    CHECK(session.public.phase==RP_BAMBU_TFA && !fixture.email_requests && !fixture.sms_requests);
    run((rp_home_bambu_input_t){.op=RP_BAMBU_RESEND});
    CHECK(session.public.phase==RP_BAMBU_EMAIL && fixture.email_requests==1 && !fixture.sms_requests);
    fixture.mode=0; run((rp_home_bambu_input_t){.op=RP_BAMBU_EMAIL_CODE,.code="123456"});
    CHECK(session.public.phase==RP_BAMBU_READY && session.public.region==1 && fixture.code_requests==1);
    const char *uids[]={"{\"uid\":1234567890}","{\"uid\":1.5}","{\"uid\":9007199254740992}","{\"uid\":-1}","{\"uid\":\"12x\"}","{\"uid\":\"1\",\"uid\":\"2\"}","{}"};
    for(unsigned i=0;i<sizeof(uids)/sizeof(uids[0]);++i) { reset(); fixture.uid=uids[i]; run(login); CHECK(session.public.phase==(i?RP_BAMBU_ERROR:RP_BAMBU_READY)); }
    const char *bad_devices[]={"{}","{\"devices\":{}}","{\"devices\":[{\"dev_id\":\"bad/serial\"}]}","{\"devices\":[{\"dev_id\":\"A\"},{\"dev_id\":\"A\"}]}","{\"devices\":[{\"dev_id\":\"A\",\"dev_id\":\"B\"}]}","{\"devices\":[{\"dev_id\":\"A\",\"name\":\"\\ud800\"}]}","{\"devices\":[{\"dev_id\":\"A\",\"name\":\"line\\nbreak\"}]}"};
    for(unsigned i=0;i<sizeof(bad_devices)/sizeof(bad_devices[0]);++i) { reset(); fixture.bind=bad_devices[i]; run(login); CHECK(session.public.phase==RP_BAMBU_ERROR && !session.public.count && !*session.token); }
    reset(); fixture.bind="{\"devices\":[]}"; run(login); CHECK(session.public.phase==RP_BAMBU_READY && !session.public.count);
    char many[4096]; strcpy(many,"{\"devices\":[");
    for(unsigned i=0;i<10;++i) { char item[240]; snprintf(item,sizeof(item),"%s{\"dev_id\":\"SERIAL%u\",\"name\":\"中文中文中文中文中文中文中文中文中文中文中文中文中文中文中文中文\"}",i?",":"",i);strcat(many,item); }
    strcat(many,"]}"); reset(); fixture.bind=many; run(login); CHECK(session.public.phase==RP_BAMBU_READY && session.public.count==8 && session.public.truncated);
    CHECK(strlen(session.public.devices[0].name)==45); CHECK(!strncmp(session.public.devices[0].name,"中文",6));
    char malformed[]={ '{','"','d','e','v','i','c','e','s','"',':','[','{','"','d','e','v','_','i','d','"',':','"','A','"',',','"','n','a','m','e','"',':','"',(char)0xc0,(char)0xaf,'"','}',']','}',0};
    reset(); fixture.bind=malformed; run(login); CHECK(session.public.phase==RP_BAMBU_ERROR);
    rp_home_bambu_input_t reuse={.op=RP_BAMBU_REUSE,.saved_slot=0}; CHECK(rp_home_bambu_input_valid(&reuse));
    bad=reuse;bad.saved_slot=RP_HOME_PRINTERS;CHECK(!rp_home_bambu_input_valid(&bad));bad=reuse;bad.region=1;CHECK(!rp_home_bambu_input_valid(&bad));
    bad=reuse;strcpy(bad.password,"unexpected");CHECK(!rp_home_bambu_input_valid(&bad));
    for(int mode=0;mode<4;++mode) {
        reset();strcpy(session.token,"opaque-fixture-token");strcpy(session.username,"u_1234567890");session.public.reused=1;
        if(mode==1)fixture.uid="{\"uid\":\"987654321\"}";else if(mode==2)fixture.mode=10;else if(mode==3)fixture.mode=11;
        run(reuse);
        CHECK(session.public.phase==(mode?RP_BAMBU_ERROR:RP_BAMBU_READY));CHECK(session.public.reused);
        CHECK(fixture.calls==(mode==1||mode==2?1:2));
        if(mode)CHECK(session.public.result==RP_HOME_AUTH&&session.public.error==RP_BAMBU_ERROR_CREDENTIALS&&!*session.token&&!*session.username&&!session.public.count);
    }
    reset();run(reuse);CHECK(session.public.phase==RP_BAMBU_ERROR&&!fixture.calls);
    unsigned char secret[31];memset(secret,0xa5,sizeof(secret));rp_home_bambu_wipe(secret,sizeof(secret));for(unsigned i=0;i<sizeof(secret);++i)CHECK(!secret[i]);
    printf("Bambu protocol fixtures: %u checks passed\n",checks); return 0;
}

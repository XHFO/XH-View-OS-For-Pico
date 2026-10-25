/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 *
 * 设备与预览共享的有界管理处理器；配置、快照与 JSON 节点使用堆，设备优先 PSRAM。
 * Shared bounded management handler; authentication belongs to the HTTP adapter.
 * All configuration/snapshots/JSON nodes use heap (PSRAM on the device).
 *
 * 冻结：不得回传凭据或阻塞访问网络；HTTP 认证仍由适配层负责。
 * Frozen: never return credentials or block on network I/O; the adapter owns HTTP authentication.
 * 用户优化：账户由多台云打印机共享；只有新登录导入才事务更新同身份其它槽的令牌，复用旧凭据不覆盖。
 * User refinement: Cloud printers share accounts; only fresh-login imports update sibling tokens atomically, never reused credentials.
 */
#include "home_link_web.h"
#include "home_link_json.h"
#include <ctype.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef ESP_PLATFORM
#include "esp_heap_caps.h"
#endif

typedef struct { char *out; size_t capacity, used; bool ok; } writer_t;
static void *web_alloc(size_t n) {
#ifdef ESP_PLATFORM
    return heap_caps_calloc(1, n, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
#else
    return calloc(1, n);
#endif
}
static void wipe(void *p, size_t n) { volatile unsigned char *v=p; while(n--)*v++=0; }
static void writef(writer_t *w,const char *format,...) {
    if(!w->ok)return;
    va_list args;va_start(args,format);int n=vsnprintf(w->out+w->used,w->capacity-w->used,format,args);va_end(args);
    if(n<0||(size_t)n>=w->capacity-w->used)w->ok=false;else w->used+=(size_t)n;
}
static void quoted(writer_t *w,const char *s) {
    writef(w,"\"");
    for(const unsigned char *p=(const unsigned char *)s;*p&&w->ok;p++) {
        if(*p=='"'||*p=='\\')writef(w,"\\%c",*p);
        else if(*p<32)writef(w,"\\u%04x",*p);
        else writef(w,"%c",*p);
    }
    writef(w,"\"");
}
static const char *boolean(bool v){return v?"true":"false";}
static cJSON *item(const cJSON *j,const char *key){return cJSON_GetObjectItemCaseSensitive(j,key);}
static bool string_is(const cJSON *j,const char *s){return cJSON_IsString(j)&&!strcmp(j->valuestring,s);}
static void json_wipe(cJSON *j) {
    for(cJSON *p=j;p;p=p->next){if(p->child)json_wipe(p->child);if(p->valuestring)wipe(p->valuestring,strlen(p->valuestring));}
}
static cJSON *body_json(const rp_home_web_request_t *r) {
    if(!r->body||!r->body_bytes||r->body_bytes>RP_HOME_WEB_BODY_LIMIT||memchr(r->body,0,r->body_bytes))return NULL;
    // 拒绝 JSON 内嵌 NUL，避免 C 字符串模型字段静默截断。/ Reject JSON NULs to prevent silent C-string truncation.
    for(size_t i=0;i+6<=r->body_bytes;i++)if(!memcmp(r->body+i,"\\u0000",6))return NULL;
    const char *end=NULL;cJSON *j=cJSON_ParseWithLengthOpts(r->body,r->body_bytes,&end,false);
    if(j){while(end<r->body+r->body_bytes&&isspace((unsigned char)*end))end++;}
    if(!cJSON_IsObject(j)||end!=r->body+r->body_bytes){json_wipe(j);cJSON_Delete(j);return NULL;}
    return j;
}
// 严格匹配且禁止重复键，避免错拼或其他来源字段静默改变凭据或被同名键遮蔽。
// Exact nonduplicate keys prevent misspellings or foreign-source fields from silently changing or hiding credentials.
static bool keys(const cJSON *j,const char *allowed) {
    for(const cJSON *p=j->child;p;p=p->next) {
        if(!p->string||!*p->string)return false;
        for(const cJSON *q=j->child;q!=p;q=q->next)if(!strcmp(q->string,p->string))return false;
        const char *at=allowed;bool found=false;size_t n=strlen(p->string);
        while(*at){const char *space=strchr(at,' ');size_t count=space?(size_t)(space-at):strlen(at);if(n==count&&!memcmp(at,p->string,n))found=true;if(!space)break;at=space+1;}
        if(!found)return false;
    }
    return true;
}
static bool text(cJSON *j,const char *key,char *dest,size_t size,bool secret) {
    cJSON *v=item(j,key);if(!v)return true;
    if(!cJSON_IsString(v)||strlen(v->valuestring)>=size)return false;
    if(!secret||*v->valuestring){memset(dest,0,size);memcpy(dest,v->valuestring,strlen(v->valuestring));}
    return true;
}
static bool flag(cJSON *j,const char *key,uint8_t *dest) {
    cJSON *v=item(j,key);if(!v)return true;if(!cJSON_IsBool(v))return false;*dest=cJSON_IsTrue(v);return true;
}
static bool integer(cJSON *j,const char *key,int max,int *dest) {
    cJSON *v=item(j,key);if(!v)return true;
    if(!cJSON_IsNumber(v)||!isfinite(v->valuedouble)||v->valuedouble<0||v->valuedouble>max||(int)v->valuedouble!=v->valuedouble)return false;
    *dest=(int)v->valuedouble;return true;
}
static bool clears(cJSON *j,const char *key,char *dest,size_t size,const char *allowed) {
    cJSON *array=item(j,"clear");if(!array)return true;if(!cJSON_IsArray(array))return false;
    for(cJSON *v=array->child;v;v=v->next) {
        if(!cJSON_IsString(v))return false;
        bool found=false;const char *at=allowed;size_t n=strlen(v->valuestring);
        while(*at){const char *space=strchr(at,' ');size_t count=space?(size_t)(space-at):strlen(at);if(n==count&&!memcmp(at,v->valuestring,n))found=true;if(!space)break;at=space+1;}
        if(!found)return false;
        if(!strcmp(v->valuestring,key))memset(dest,0,size);
    }
    return true;
}
static bool saved_wifi(const rp_home_web_request_t *r,const char *ssid) {
    if(!*ssid||!r->saved_wifi)return true;
    for(size_t i=0;i<r->saved_wifi_count;i++)if(r->saved_wifi[i]&&!strcmp(ssid,r->saved_wifi[i]))return true;
    return false;
}
static bool at_home(const rp_home_web_request_t *r,const rp_home_config_t *c) {
    return r->current_wifi ? r->network_online&&*c->home_wifi&&!strcmp(r->current_wifi,c->home_wifi) : r->at_home;
}
static bool home_update(const rp_home_web_request_t *r,cJSON *j,rp_home_config_t *c) {
    if(!keys(j,"enabled local external home_wifi external_enabled external_control ha_token rooms clear")||
       !flag(j,"enabled",&c->enabled)||!flag(j,"external_enabled",&c->external_enabled)||!flag(j,"external_control",&c->external_control)||
       !text(j,"local",c->local,sizeof(c->local),false)||!text(j,"external",c->external,sizeof(c->external),false)||
       !text(j,"home_wifi",c->home_wifi,sizeof(c->home_wifi),false)||!text(j,"ha_token",c->ha_token,sizeof(c->ha_token),true)||
       !clears(j,"ha_token",c->ha_token,sizeof(c->ha_token),"ha_token"))return false;
    if(item(j,"home_wifi")&&!saved_wifi(r,c->home_wifi))return false;
    cJSON *rooms=item(j,"rooms");if(!rooms)return true;
    if(!cJSON_IsArray(rooms)||cJSON_GetArraySize(rooms)>RP_HOME_ROOMS)return false;
    c->room_count=0;memset(c->rooms,0,sizeof(c->rooms));
    for(cJSON *v=rooms->child;v;v=v->next) {
        rp_home_room_t *p=&c->rooms[c->room_count++];
        if(!cJSON_IsObject(v)||!keys(v,"entity room")||!item(v,"entity")||!item(v,"room")||
           !text(v,"entity",p->entity,sizeof(p->entity),false)||!text(v,"room",p->room,sizeof(p->room),false))return false;
        for(unsigned i=0;i+1<c->room_count;i++)if(!strcmp(c->rooms[i].entity,p->entity))return false;
    }
    return true;
}
static bool printer_update(cJSON *j,rp_home_printer_config_t *p) {
    int source=p->source,region=p->region,port=p->port;
    if(!integer(j,"source",RP_HOME_FORMLABS,&source))return false;
    if(source!=p->source){char name[sizeof(p->name)];memcpy(name,p->name,sizeof(name));memset(p,0,sizeof(*p));memcpy(p->name,name,sizeof(name));p->source=(uint8_t)source;p->port=source==RP_HOME_BAMBU_LAN?8883:0;region=p->region;port=p->port;}
    const char *allowed[]={"source clear","source name serial clear","source name serial host port secret certificate clear",
        "source name serial region username secret clear","source name serial client_id client_secret clear"};
    const char *secret_keys[]={"","","secret certificate","username secret","client_id client_secret"};
    if(!keys(j,allowed[source])||!text(j,"name",p->name,sizeof(p->name),false)||!text(j,"serial",p->serial,sizeof(p->serial),false))return false;
    if(source==RP_HOME_NONE){memset(p,0,sizeof(*p));return !item(j,"clear");}
    if(source==RP_HOME_BAMBU_LAN) {
        if(!integer(j,"port",65535,&port)||!text(j,"host",p->host,sizeof(p->host),false)) {
            return false;
        }
        p->port=(uint16_t)port;
    }
    if(source==RP_HOME_BAMBU_CLOUD){if(!integer(j,"region",1,&region))return false;p->region=(uint8_t)region;}
    return text(j,"username",p->username,sizeof(p->username),true)&&text(j,"secret",p->secret,sizeof(p->secret),true)&&
        text(j,"client_id",p->client_id,sizeof(p->client_id),true)&&text(j,"client_secret",p->client_secret,sizeof(p->client_secret),true)&&
        text(j,"certificate",p->certificate,sizeof(p->certificate),true)&&
        clears(j,"username",p->username,sizeof(p->username),secret_keys[source])&&clears(j,"secret",p->secret,sizeof(p->secret),secret_keys[source])&&
        clears(j,"client_id",p->client_id,sizeof(p->client_id),secret_keys[source])&&clears(j,"client_secret",p->client_secret,sizeof(p->client_secret),secret_keys[source])&&
        clears(j,"certificate",p->certificate,sizeof(p->certificate),secret_keys[source]);
}
static int hex(unsigned char c){if(c>='0'&&c<='9')return c-'0';c=(unsigned char)tolower(c);return c>='a'&&c<='f'?c-'a'+10:-1;}
// 0 缺失，1 找到，-1 无效、重复或超限。/ 0 absent, 1 found, -1 invalid, duplicate or oversized.
static int query(const char *raw,const char *key,char *out,size_t size) {
    int found=0;out[0]=0;if(!raw)return 0;
    if(strlen(raw)>RP_HOME_WEB_QUERY_LIMIT)return -1;
    for(const char *p=raw;*p;) {
        const char *amp=strchr(p,'&');const char *end=amp?amp:p+strlen(p);const char *equal=memchr(p,'=',(size_t)(end-p));
        if(equal&&(size_t)(equal-p)==strlen(key)&&!memcmp(p,key,strlen(key))) {
            if(found++) {
                return -1;
            }
            size_t n=0;
            for(const char *v=equal+1;v<end;v++) {
                unsigned char c=(unsigned char)*v;
                if(c=='%'){if(end-v<3||hex((unsigned char)v[1])<0||hex((unsigned char)v[2])<0)return -1;c=(unsigned char)(hex((unsigned char)v[1])*16+hex((unsigned char)v[2]));v+=2;}
                else if(c=='+')c=' ';
                if(!c||c<32||n+1>=size) {
                    return -1;
                }
                out[n++]=(char)c;
            }
            out[n]=0;
        }
        if(!amp) {
            break;
        }
        p=amp+1;
    }
    return found;
}
static bool query_number(const char *q,const char *key,int max,int *value) {
    char b[16];int found=query(q,key,b,sizeof(b));if(found<0)return false;if(!found)return true;
    if(!*b||strspn(b,"0123456789")!=strlen(b))return false;
    int n=0;for(const char *p=b;*p;p++){if(n>max/10)return false;n=n*10+*p-'0';if(n>max)return false;}*value=n;return true;
}
static void status_json(writer_t *w,const rp_home_snapshot_t *s) {
    writef(w,"{\"result\":%d,\"http_status\":%d,\"revision\":%u,\"busy\":%s,\"external\":%s,\"sampled_ms\":%lld}",s->result,s->http_status,(unsigned)s->revision,boolean(s->busy),boolean(s->external),(long long)s->sampled_ms);
}
static void home_json(writer_t *w,const rp_home_web_request_t *r,const rp_home_config_t *c,const rp_home_snapshot_t *s) {
    writef(w,"{\"config\":{\"enabled\":%s,\"local\":",boolean(c->enabled));quoted(w,c->local);
    writef(w,",\"external\":");quoted(w,c->external);writef(w,",\"home_wifi\":");quoted(w,c->home_wifi);
    writef(w,",\"external_enabled\":%s,\"external_control\":%s,\"has_token\":%s,\"rooms\":[",boolean(c->external_enabled),boolean(c->external_control),boolean(*c->ha_token));
    for(unsigned i=0;i<c->room_count&&i<RP_HOME_ROOMS;i++){if(i)writef(w,",");writef(w,"{\"entity\":");quoted(w,c->rooms[i].entity);writef(w,",\"room\":");quoted(w,c->rooms[i].room);writef(w,"}");}
    writef(w,"]},\"status\":");status_json(w,s);writef(w,",\"network_online\":%s,\"at_home\":%s,\"saved_wifi\":[",boolean(r->network_online),boolean(at_home(r,c)));
    size_t count=r->saved_wifi?r->saved_wifi_count:0;for(size_t i=0;i<count&&i<RP_HOME_WEB_WIFI_LIMIT;i++){if(i)writef(w,",");quoted(w,r->saved_wifi[i]?r->saved_wifi[i]:"");}
    writef(w,"],\"wifi_truncated\":%s}",boolean(count>RP_HOME_WEB_WIFI_LIMIT));
}
static void printers_json(writer_t *w,const rp_home_config_t *c,const rp_home_snapshot_t *s) {
    writef(w,"{\"printers\":[");
    for(unsigned i=0;i<RP_HOME_PRINTERS;i++) {
        const rp_home_printer_config_t *p=&c->printers[i];const rp_home_printer_t *v=&s->printers[i];
        writef(w,"%s{\"slot\":%u,\"source\":%u,\"name\":",i?",":"",i,p->source);quoted(w,p->name);
        writef(w,",\"serial\":");quoted(w,p->serial);writef(w,",\"host\":");quoted(w,p->host);
        writef(w,",\"port\":%u,\"region\":%u,\"has_username\":%s,\"has_secret\":%s,\"has_client_id\":%s,\"has_client_secret\":%s,\"has_certificate\":%s,\"status\":{\"result\":%d,\"http_status\":%d,\"connected\":%s,\"has_data\":%s,\"percent\":%d,\"remaining_minutes\":%d,\"layer\":%d,\"layers\":%d,\"state\":",p->port,p->region,boolean(*p->username),boolean(*p->secret),boolean(*p->client_id),boolean(*p->client_secret),boolean(*p->certificate),v->result,v->http_status,boolean(v->connected),boolean(v->has_data),v->percent,v->remaining_minutes,v->layer,v->layers);
        quoted(w,v->state);writef(w,",\"task\":");quoted(w,v->task);writef(w,",\"material\":");quoted(w,v->material);writef(w,"}}");
    }
    writef(w,"],\"imports\":[");for(unsigned i=0;i<s->import_count&&i<RP_HOME_IMPORTS;i++){if(i)writef(w,",");writef(w,"{\"name\":");quoted(w,s->imports[i].name);writef(w,",\"serial\":");quoted(w,s->imports[i].serial);writef(w,"}");}writef(w,"]}");
}
static bool contains(const char *s,const char *q){if(!*q)return true;for(;*s;s++){size_t n=0;while(q[n]&&s[n]&&tolower((unsigned char)q[n])==tolower((unsigned char)s[n]))n++;if(!q[n])return true;}return false;}
static bool can_control(const rp_home_web_request_t *r,const rp_home_config_t *c,const rp_home_snapshot_t *s) {
    return r->network_online&&c->enabled&&*c->ha_token&&s->result==RP_HOME_OK&&!s->busy&&((at_home(r,c)&&!s->external)||(c->external_enabled&&c->external_control&&*c->external));
}
static bool entities_json(writer_t *w,const rp_home_web_request_t *r,const rp_home_config_t *c,const rp_home_snapshot_t *s) {
    int offset=0,limit=24,kind=6;char q[96],room[40],domain[24];
    if(!query_number(r->query,"offset",RP_HOME_ENTITIES,&offset)||!query_number(r->query,"limit",24,&limit)||!limit||
       !query_number(r->query,"kind",5,&kind)||query(r->query,"q",q,sizeof(q))<0||query(r->query,"room",room,sizeof(room))<0||query(r->query,"domain",domain,sizeof(domain))<0)return false;
    const char *domains[]={"light","switch","climate","fan","temperature","humidity"};bool sensor=!strcmp(domain,"sensor");int domain_kind=6;
    if(*domain&&!sensor){for(int i=0;i<6;i++)if(!strcmp(domain,domains[i]))domain_kind=i;if(domain_kind==6)return false;}
    int total=0,shown=0;writef(w,"{\"items\":[");
    unsigned retained=s->entity_count<RP_HOME_ENTITIES?s->entity_count:RP_HOME_ENTITIES;
    for(unsigned i=0;i<retained;i++) {
        const rp_home_entity_t *e=&s->entities[i];if(e->kind>RP_HOME_HUMIDITY||(kind<6&&kind!=e->kind)||(sensor&&e->kind<RP_HOME_TEMPERATURE)||(domain_kind<6&&domain_kind!=e->kind)||(*room&&strcmp(room,e->room))||(!contains(e->id,q)&&!contains(e->name,q)&&!contains(e->room,q)))continue;
        if(total++<offset||shown>=limit)continue;
        writef(w,"%s{\"id\":",shown++?",":"");quoted(w,e->id);writef(w,",\"name\":");quoted(w,e->name);writef(w,",\"state\":");quoted(w,e->state);writef(w,",\"unit\":");quoted(w,e->unit);writef(w,",\"room\":");quoted(w,e->room);
        writef(w,",\"kind\":%u,\"available\":%s,\"on\":%s,\"number\":",e->kind,boolean(e->available),boolean(e->on));if(isfinite(e->number))writef(w,"%.6g",(double)e->number);else writef(w,"null");writef(w,"}");
    }
    writef(w,"],\"offset\":%d,\"limit\":%d,\"total\":%d,\"retained\":%u,\"total_supported\":%u,\"truncated\":%s,\"busy\":%s,\"revision\":%u,\"external\":%s,\"can_control\":%s,\"rooms\":[",offset,limit,total,retained,s->entity_total,boolean(s->truncated),boolean(s->busy),(unsigned)s->revision,boolean(s->external),boolean(can_control(r,c,s)));
    unsigned rooms=0;for(unsigned i=0;i<retained;i++){bool seen=false;for(unsigned j=0;j<i;j++)if(!strcmp(s->entities[i].room,s->entities[j].room))seen=true;if(!seen&&s->entities[i].kind<=RP_HOME_HUMIDITY){if(rooms++)writef(w,",");quoted(w,s->entities[i].room);}}
    writef(w,"]}");return true;
}
static int control(const rp_home_web_request_t *r,cJSON *j,const rp_home_config_t *c,const rp_home_snapshot_t *s) {
    if(!keys(j,"entity action value")||!cJSON_IsString(item(j,"entity"))||!cJSON_IsString(item(j,"action")))return 400;
    if(!can_control(r,c,s))return !r->network_online?503:s->busy?409:403;
    const rp_home_entity_t *e=NULL;for(unsigned i=0;i<s->entity_count&&i<RP_HOME_ENTITIES;i++)if(string_is(item(j,"entity"),s->entities[i].id))e=&s->entities[i];
    if(!e) {
        return 404;
    }
    if(!e->available||e->kind>RP_HOME_FAN)return 403;
    const char *action=item(j,"action")->valuestring;bool temp=e->kind==RP_HOME_CLIMATE&&!strcmp(action,"set_temperature");float value=0;
    if(strcmp(action,"turn_on")&&strcmp(action,"turn_off")&&!temp)return 400;
    cJSON *n=item(j,"value");if(n&&(!cJSON_IsNumber(n)||!isfinite(n->valuedouble)))return 400;
    if(temp){if(!n||n->valuedouble<5||n->valuedouble>40)return 400;value=(float)n->valuedouble;}
    return read_pico_home_link_control(e,action,value,at_home(r,c)&&!s->external)?202:409;
}
// 认证响应只包含公开阶段和保留设备；云端令牌永不经过手机JSON。
// Authentication JSON exposes only public phases/devices; cloud tokens never pass through phone JSON.
static void bambu_json(writer_t *w,const rp_home_bambu_state_t *s) {
    writef(w,"{\"session\":%u,\"revision\":%u,\"phase\":%u,\"region\":%u,\"count\":%u,\"error\":%u,\"result\":%d,\"http_status\":%d,\"seconds_left\":%u,\"retry_after\":%u,\"truncated\":%s,\"devices\":[",
        (unsigned)s->session,(unsigned)s->revision,s->phase,s->region,s->count,s->error,s->result,s->http_status,(unsigned)s->seconds_left,(unsigned)s->retry_after,boolean(s->truncated));
    for(unsigned i=0;i<s->count&&i<RP_HOME_BAMBU_DEVICES;i++) {
        if(i)writef(w,",");
        writef(w,"{\"serial\":");quoted(w,s->devices[i].serial);
        writef(w,",\"name\":");quoted(w,s->devices[i].name);writef(w,",\"model\":");quoted(w,s->devices[i].model);writef(w,"}");
    }
    writef(w,"],\"config_revision\":%u,\"reused\":%s,\"saved_count\":%u,\"saved_accounts\":[",
        (unsigned)s->config_revision,boolean(s->reused),s->saved_count);
    for(unsigned i=0;i<s->saved_count&&i<RP_HOME_PRINTERS;i++) {
        if(i)writef(w,",");
        const rp_home_bambu_saved_account_t *a=&s->saved_accounts[i];
        writef(w,"{\"slot\":%u,\"region\":%u,\"printer_count\":%u,\"name\":",a->slot,a->region,a->printer_count);
        quoted(w,a->name);writef(w,"}");
    }
    writef(w,"]}");
}
static bool uint32_value(cJSON *j,const char *key,uint32_t *out) {
    cJSON *v=item(j,key);
    if(!cJSON_IsNumber(v)||!isfinite(v->valuedouble)||v->valuedouble<0||v->valuedouble>UINT32_MAX||floor(v->valuedouble)!=v->valuedouble)return false;
    *out=(uint32_t)v->valuedouble;return true;
}
static int bambu_auth(const rp_home_web_request_t *r,cJSON *j,writer_t *w,const char **error) {
    int status=200;rp_home_bambu_state_t *s=web_alloc(sizeof(*s));rp_home_bambu_input_t *input=NULL;rp_home_config_t *config=NULL;
    if(!s){*error="memory";return 503;}
    if(r->query&&*r->query){status=400;*error="invalid_query";goto done;}
    if(!strcmp(r->method,"GET")){read_pico_bambu_auth_get(s);bambu_json(w,s);goto done;}
    uint32_t session=0;cJSON *action=item(j,"action");
    if(!cJSON_IsString(action)||!uint32_value(j,"session",&session)){status=400;*error="invalid_authentication";goto done;}
    if(string_is(action,"cancel")) {
        if(!keys(j,"action session")){status=400;*error="invalid_authentication";goto done;}
        if(!read_pico_bambu_auth_cancel_session(session)){status=409;*error="stale_session";goto done;}
    } else if(string_is(action,"import")) {
        int slot=-1,index=-1;
        if(!keys(j,"action session slot index name")||!integer(j,"slot",RP_HOME_PRINTERS-1,&slot)||slot<0||
           !integer(j,"index",RP_HOME_BAMBU_DEVICES-1,&index)||index<0){status=400;*error="invalid_authentication";goto done;}
        config=web_alloc(sizeof(*config));if(!config){status=503;*error="memory";goto done;}
        read_pico_home_link_get_config(config);
        read_pico_bambu_auth_get(s);
        bool update_account=s->session==session&&!s->reused;
        if(!read_pico_bambu_auth_prepare_import(session,(unsigned)index,&config->printers[slot])){status=409;*error="stale_session";goto done;}
        if(!text(j,"name",config->printers[slot].name,sizeof(config->printers[slot].name),false)){status=400;*error="invalid_configuration";goto done;}
        // 同一身份新登录获得的令牌覆盖旧凭据；复用仍有效旧令牌时不倒退其它槽的凭据。
        // A fresh login refreshes the same identity; reusing an older valid token never downgrades sibling credentials.
        const rp_home_printer_config_t *selected=&config->printers[slot];
        if(update_account)for(unsigned i=0;i<RP_HOME_PRINTERS;i++) {
            rp_home_printer_config_t *p=&config->printers[i];
            if(i!=(unsigned)slot&&p->source==RP_HOME_BAMBU_CLOUD&&p->region==selected->region&&
               !strcmp(p->username,selected->username)) {
                wipe(p->secret,sizeof(p->secret));memcpy(p->secret,selected->secret,sizeof(p->secret));
            }
        }
        config->enabled=1;
        if(!rp_home_config_valid(config)){status=400;*error="invalid_configuration";goto done;}
        if(!read_pico_home_link_configure(config)){status=507;*error="save_failed";goto done;}
        bool queued=read_pico_home_link_request(r->network_online,at_home(r,config));
        // 同一账户继续填充其它槽位；选择设备不会关闭共享账户会话。
        // The account can fill other slots; choosing a printer does not close the shared account session.
        writef(w,"{\"ok\":true,\"queued\":%s}",boolean(queued));goto done;
    } else {
        input=web_alloc(sizeof(*input));if(!input){status=503;*error="memory";goto done;}input->session=session;
        if(string_is(action,"login")) {
            int region=-1;input->op=RP_BAMBU_LOGIN;
            if(!keys(j,"action session region account password")||!integer(j,"region",1,&region)||region<0||
               !item(j,"account")||!item(j,"password")||!text(j,"account",input->account,sizeof(input->account),false)||
               !text(j,"password",input->password,sizeof(input->password),false)||!*input->account||!*input->password){status=400;*error="invalid_authentication";goto done;}
            input->region=(uint8_t)region;
        } else if(string_is(action,"email_code")) {
            input->op=RP_BAMBU_EMAIL_CODE;
            if(!keys(j,"action session code")||!item(j,"code")||!text(j,"code",input->code,sizeof(input->code),false)||
               strlen(input->code)<4||strlen(input->code)>12||strspn(input->code,"0123456789")!=strlen(input->code)){status=400;*error="invalid_authentication";goto done;}
        } else if(string_is(action,"resend")) {
            input->op=RP_BAMBU_RESEND;if(!keys(j,"action session")){status=400;*error="invalid_authentication";goto done;}
        } else if(string_is(action,"reuse")) {
            int saved_slot=-1;input->op=RP_BAMBU_REUSE;
            if(!keys(j,"action session config_revision saved_slot")||
               !uint32_value(j,"config_revision",&input->config_revision)||
               !integer(j,"saved_slot",RP_HOME_PRINTERS-1,&saved_slot)||saved_slot<0) {
                status=400;*error="invalid_authentication";goto done;
            }
            input->saved_slot=(uint8_t)saved_slot;
        } else {status=400;*error="invalid_authentication";goto done;}
        if(!r->network_online){status=503;*error="offline";goto done;}
        if(!read_pico_bambu_auth_submit(input,true)){status=409;*error="stale_session_or_busy";goto done;}
        status=202;
    }
    read_pico_bambu_auth_get(s);bambu_json(w,s);
 done:
    if(input){wipe(input,sizeof(*input));free(input);}if(config){wipe(config,sizeof(*config));free(config);}wipe(s,sizeof(*s));free(s);return status;
}
bool rp_home_web_handle(const rp_home_web_request_t *r,rp_home_web_response_t *result,char *out,size_t capacity) {
    if(!r||!r->path||!r->method||!result||!out||capacity<32)return false;
    const char *paths[]={"/api/manage/home","/api/manage/printers","/api/manage/ha/entities","/api/manage/ha/control","/api/manage/refresh","/api/manage/bambu/auth"};int route=-1;
    for(int i=0;i<6;i++) {
        if(!strcmp(r->path,paths[i]))route=i;
    }
    if(route<0)return false;
    writer_t w={out,capacity>RP_HOME_WEB_OUTPUT_LIMIT?RP_HOME_WEB_OUTPUT_LIMIT:capacity,0,true};result->http_status=200;result->body_bytes=0;
    const char *error=NULL;int status=200;bool get=!strcmp(r->method,"GET"),put=!strcmp(r->method,"PUT"),post=!strcmp(r->method,"POST"),del=!strcmp(r->method,"DELETE");
    rp_home_config_t *c=NULL;rp_home_snapshot_t *s=NULL;cJSON *j=NULL;
    if(r->body_bytes>RP_HOME_WEB_BODY_LIMIT){status=413;error="too_large";goto done;}
    if(r->query&&strlen(r->query)>RP_HOME_WEB_QUERY_LIMIT){status=400;error="invalid_query";goto done;}
    if(!((route<=1&&(get||put||(route==1&&del)))||(route==2&&get)||((route==3||route==4)&&post)||(route==5&&(get||post)))){status=405;error="method";goto done;}
    if(put||post){j=body_json(r);if(!j){status=400;error="bad_json";goto done;}}
    if(route==5){status=bambu_auth(r,j,&w,&error);goto done;}
    c=web_alloc(sizeof(*c));s=web_alloc(sizeof(*s));if(!c||!s){status=503;error="memory";goto done;}
    read_pico_home_link_get_config(c);read_pico_home_link_get_snapshot(s);
    if(get){if(route==0)home_json(&w,r,c,s);else if(route==1)printers_json(&w,c,s);else if(!entities_json(&w,r,c,s)){status=400;error="invalid_query";}goto done;}
    if(route<=1) {
        bool valid=true;
        if(route==0)valid=home_update(r,j,c);
        else {int slot=-1;valid=query_number(r->query,"slot",RP_HOME_PRINTERS-1,&slot)&&slot>=0;if(valid){if(del)memset(&c->printers[slot],0,sizeof(c->printers[slot]));else {valid=printer_update(j,&c->printers[slot]);if(valid&&c->printers[slot].source)c->enabled=1;}}}
        if(!valid||!rp_home_config_valid(c)){status=400;error="invalid_configuration";goto done;}
        if(!read_pico_home_link_configure(c)){status=507;error="save_failed";goto done;}
        bool queued=c->enabled&&read_pico_home_link_request(r->network_online,at_home(r,c));writef(&w,"{\"ok\":true,\"queued\":%s}",boolean(queued));
    } else if(route==3) {
        status=control(r,j,c,s);if(status==202)writef(&w,"{\"ok\":true,\"queued\":true}");else error=status==400?"invalid_action":status==404?"not_found":status==503?"offline":status==409?"busy":"not_allowed";
    } else {
        if(!keys(j,"")){status=400;error="invalid_configuration";goto done;}
        bool queued=read_pico_home_link_request(r->network_online,at_home(r,c));if(queued){status=202;writef(&w,"{\"ok\":true,\"queued\":true}");}else {status=r->network_online?409:503;error=r->network_online?"busy_or_disabled":"offline";}
    }
done:
    if(j){json_wipe(j);cJSON_Delete(j);}if(c){wipe(c,sizeof(*c));free(c);}if(s){wipe(s,sizeof(*s));free(s);}
    if(!w.ok&&!error){status=507;error="output_limit";}
    if(error){w.used=0;w.ok=true;writef(&w,"{\"error\":\"%s\"}",error);if(!w.ok){status=507;w.used=0;w.ok=true;writef(&w,"{\"error\":\"output_limit\"}");}}
    result->http_status=status;result->body_bytes=w.ok?w.used:0;return true;
}

/* SPDX-License-Identifier: Apache-2.0
 * 中文：真实TF模块的事务配置、离线快照、刷新与生命周期测试；不访问真实Cloudflare。
 * English: Real TF module configuration transactions, offline snapshots, refresh and lifecycle; no live Cloudflare access.
 */
#include "pico_app_sdk.h"
#include <assert.h>
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
static uint8_t saved[4096],frame[1];static size_t saved_bytes;
static bool fail_store,fail_worker,landscape;static unsigned stores,fetches,snapshots;static int response_status=200;
static int64_t clock_us=1000000,epoch=1794787200;
static void (*pending_fn)(void*);static void *pending_arg;
static const char *response="{\"errors\":null,\"data\":{\"viewer\":{\"zones\":[{\"httpRequests1dGroups\":[{\"dimensions\":{\"date\":\"2026-11-16\"},\"sum\":{\"requests\":12345,\"bytes\":10485760,\"cachedRequests\":6000,\"countryMap\":[{\"clientCountryName\":\"CN\",\"requests\":9000},{\"clientCountryName\":\"US\",\"requests\":3345}]}}]}]}}}";
static pico_app_view_t view(void){return (pico_app_view_t){landscape?640:360,landscape?360:640,landscape};}
static bool load(const char *key,void *out,size_t n){assert(!strcmp(key,"cloudflare2"));if(n!=saved_bytes)return false;memcpy(out,saved,n);return true;}
static bool store(const char *key,const void *out,size_t n){assert(!strcmp(key,"cloudflare2")&&n<=sizeof(saved));if(fail_store)return false;memcpy(saved,out,n);saved_bytes=n;stores++;return true;}
static int64_t now(void){return clock_us;}static int64_t utc(void){return epoch;}
static void fill(uint8_t *f,int x,int y,int w,int h,int g){(void)f;(void)x;(void)y;(void)w;(void)h;(void)g;}
static void line(uint8_t *f,int x,int y,int w,int h,int g){fill(f,x,y,w,h,g);}
static void box(uint8_t *f,int x,int y,int w,int h,int r,int g,bool b){(void)r;(void)b;fill(f,x,y,w,h,g);}
static char texts[16384];
static void text(uint8_t*f,int x,int y,int size,const char*t,int g){(void)f;(void)x;(void)y;(void)size;(void)g;if(strlen(texts)+strlen(t)+2<sizeof(texts)){strcat(texts,t);strcat(texts,"\n");}}
static void wrap(uint8_t*f,int x,int y,int w,int size,int lines,const char*t,int g){(void)w;(void)lines;text(f,x,y,size,t,g);}
static void button(uint8_t*f,int x,int y,int w,int h,const char*t,bool b){(void)w;(void)h;(void)b;text(f,x,y,14,t,0);}
static void heading(uint8_t*f,const char*t,const char*s){text(f,0,0,20,t,0);if(s)text(f,0,0,12,s,4);}
static void dirty(void){}static void nav(int32_t n){(void)n;}
static int create(void(*fn)(void*),void*arg,void**handle){if(fail_worker)return 0;assert(!pending_fn);pending_fn=fn;pending_arg=arg;*handle=(void*)1;return 1;}
static void destroy(void*h){assert(h==(void*)1);pending_fn=NULL;}
static void suspend_worker(void){}static void delay(uint32_t n){(void)n;}
static int fetch(const char*q,const char*t,char*out,size_t cap){assert(strstr(q,"httpRequests1dGroups")&&strstr(q,"date_geq")&&strstr(q,"2026-11-10"));assert(t[0]);fetches++;assert(strlen(response)<cap);strcpy(out,response);return response_status;}
static bool portal(bool open){(void)open;return true;}static bool snapshot(uint8_t*f){if(f)snapshots++;return false;}
static pico_app_host_t host={.abi=1,.bytes=sizeof(host),.view=view,.navigate=nav,.dirty=dirty,.load=load,.store=store,.time_us=now,.fill=fill,.line=line,.box=box,.text=text,.wrap=wrap,.button=button,.heading=heading,.worker_create=create,.worker_delete=destroy,.worker_suspend=suspend_worker,.delay_ms=delay,.format=snprintf,.copy=memcpy,.set=memset,.compare=memcmp,.board_model_abi=1,.traffic_fetch=fetch,.traffic_portal=portal,.utc_seconds=utc,.traffic_snapshot=snapshot};
static const pico_app_exports_t *app;static char out[32768];
static int rpc(const char *method,const char *path,const char *body){memset(out,0,sizeof(out));int r=app->manage(method,path,body,body?strlen(body):0,out,sizeof(out));assert(out[0]=='{');assert(!strstr(out,"fixture-secret"));return r;}
static unsigned revision(void){assert(rpc("GET","/api/manage/pico/traffic/config",NULL)==200);const char*p=strstr(out,"\"revision\":");assert(p);return (unsigned)strtoul(p+11,NULL,10);}
static int config(unsigned rev,unsigned style,const char *zone,const char *token){char body[2048];snprintf(body,sizeof(body),"{\"revision\":%u,\"source\":0,\"style\":%u,\"interval\":900,\"label\":\"XHFO.NET\",\"zone\":\"%s\",\"token\":\"%s\"}",rev,style,zone,token);return rpc("PUT","/api/manage/pico/traffic/config",body);}
static void finish(void){assert(pending_fn);pending_fn(pending_arg);assert(app->tick());assert(!pending_fn);}
int main(int argc,char **argv){assert(argc==2);void *lib=dlopen(argv[1],RTLD_NOW|RTLD_LOCAL);if(!lib){fprintf(stderr,"%s\n",dlerror());return 1;}pico_app_query_t query=(pico_app_query_t)dlsym(lib,"pico_app_query");assert(query);app=query();assert(app->version==10100&&app->manage&&app->start(&host));
    assert(revision()==1);assert(strstr(out,"\"has_token\":false"));
    assert(rpc("GET","/api/manage/pico/traffic/ui",NULL)==200&&strstr(out,"Cloudflare"));
    const char *zone="1234567890abcdef1234567890abcdef";
    assert(config(1,0,zone,"fixture-secret")==202&&stores==0);assert(config(1,0,zone,"x")==409);finish();assert(revision()==2&&stores==1&&strstr(out,"\"has_token\":true"));
    texts[0]=0;app->render(frame);assert(strstr(texts,"12.3K")&&strstr(texts,"CN")&&strstr(texts,"10 MB"));
    uint8_t baseline[4096];memcpy(baseline,saved,saved_bytes);size_t bytes=saved_bytes;
    assert(config(1,0,zone,"")==409);assert(config(2,9,zone,"")==422);
    response_status=403;assert(config(2,1,zone,"replacement-secret")==202);finish();assert(revision()==2&&memcmp(saved,baseline,bytes)==0&&strstr(out,"授权失败"));
    response_status=200;fail_store=true;assert(config(2,1,zone,"")==202);finish();assert(revision()==2&&memcmp(saved,baseline,bytes)==0&&strstr(out,"保存失败"));fail_store=false;
    const char *good=response;response="{\"errors\":[{\"message\":\"fixture-secret\"}],\"data\":null}";assert(config(2,1,zone,"")==202);finish();assert(revision()==2);response=good;
    response="{\"errors\":null,\"data\":{\"viewer\":{\"zones\":[]}}}";assert(config(2,1,zone,"")==202);finish();assert(revision()==2);response=good;
    for(unsigned style=1;style<3;style++){assert(config(revision(),style,zone,"")==202);finish();for(int rotate=0;rotate<2;rotate++){landscape=rotate;texts[0]=0;app->render(frame);assert(strstr(texts,style==1?"近七天":"今日请求"));}}
    app->stop();assert(app->save());assert(app->start(&host));assert(revision()==4&&strstr(out,"\"has_token\":true"));
    response_status=-1;assert(rpc("POST","/api/manage/pico/traffic/refresh","{}")==202);finish();assert(revision()==4&&strstr(out,"网络请求失败"));texts[0]=0;app->render(frame);assert(strstr(texts,"12.3K"));response_status=200;
    assert(config(4,0,"abcdef1234567890abcdef1234567890","")==422);
    fail_worker=true;assert(config(4,0,zone,"")==503);fail_worker=false;assert(revision()==4);
    epoch=0;assert(config(4,0,zone,"")==503);epoch=1794787200;
    assert(rpc("PUT","/api/manage/pico/traffic/config","{\"revision\":4,\"source\":1,\"style\":0,\"interval\":900,\"label\":\"XHFO.NET\",\"zone\":\"1234567890abcdef1234567890abcdef\",\"token\":\"\"}")==200);
    texts[0]=0;app->render(frame);assert(snapshots==1);assert(rpc("POST","/api/manage/pico/traffic/refresh","{}")==409);
    unsigned before=stores;assert(rpc("DELETE","/api/manage/pico/traffic/config","{\"revision\":5}")==200&&stores==before+2);assert(revision()==6&&strstr(out,"\"has_token\":false"));
    app->stop();assert(app->start(&host));assert(revision()==6&&strstr(out,"\"has_token\":false"));assert(!strstr((char*)saved,"fixture-secret"));
    assert(rpc("PUT","/api/manage/pico/traffic/config","{\"revision\":6,\"source\":0,\"style\":0,\"interval\":1}")==422);
    app->stop();dlclose(lib);printf("PASS real TF traffic module: transaction, auth, cache, styles, offline, restart, clear, token redaction; fetches=%u stores=%u state=%zu B\n",fetches,stores,saved_bytes);return 0;
}

/* SPDX-License-Identifier: Apache-2.0
 * 中文：运行生产 ESP 下载任务与安装接力，只替换 HTTP、调度和私有 TF 边界。
 * English: Run production ESP workers and install handoffs with mocked HTTP, scheduling and private TF boundaries.
 */
#include <assert.h>
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#ifndef PICO_UPDATES_TEST_SOURCE
#define PICO_UPDATES_TEST_SOURCE "../main/system/pico_updates.c"
#endif
#include PICO_UPDATES_TEST_SOURCE
#include "../components/read_pico_apps/pico_app_package.c"

static char root[512], requests[32][32], installs[32][32];
static unsigned request_count,install_count,catalog_reads;
static bool online=true,fail_download,fail_install;
static void (*queued)(void *);static void *queued_arg;static jmp_buf task_end;
static const char *default_feed="game=alpha\ngame=beta\ngame=gamma\ntool=delta\ngame=epsilon\napp_version=alpha,2\napp_version=beta,2\napp_version=gamma,2\napp_version=delta,2\napp_version=epsilon,2\n";
static const char *feed;
struct native_http_client { const char *body;size_t pos,size;int status; };
static char package[600];
static uint32_t served_version=2;static bool wrong_id;
static void wr32(char *p,uint32_t v){for(unsigned i=0;i<4;i++)p[i]=(char)(v>>(8*i));}
static void make_package(const char *id,uint32_t version){
    memset(package,0,sizeof(package));memcpy(package,"PICOAPP1",8);
    wr32(package+8,1);wr32(package+12,512);wr32(package+16,sizeof(package));wr32(package+20,1);wr32(package+24,version);
    wr32(package+28,!strcmp(id,"delta")?128:0);snprintf(package+32,32,"%s",id);strcpy(package+64,"Test");strcpy(package+128,"Test package");
    wr32(package+228,1);strcpy(package+256,"esp32s3-xtensa");wr32(package+272,512);wr32(package+276,88);
    wr32(package+280,pico_app_crc32(package+512,88));wr32(package+232,pico_app_crc32(package,512));
}
static void local_package(const char *id,uint32_t version){
    char path[96];app_path(path,sizeof(path),id,!strcmp(id,"delta"));make_package(id,version);
    FILE *f=native_preview_vfs_fopen(path,"wb");assert(f&&fwrite(package,1,sizeof(package),f)==sizeof(package)&&fclose(f)==0);
}
// 路径仅指向本次临时会话。/ Paths only reach the private test session.
static const char *mapped(const char *path){static char value[1024];assert(!strncmp(path,"/sdcard",7));snprintf(value,sizeof(value),"%s%s",root,path+7);return value;}
#undef fopen
#undef access
#undef mkdir
#undef unlink
#undef stat
#undef rename
FILE *native_preview_vfs_fopen(const char *p,const char *m){return fopen(mapped(p),m);}
int native_preview_vfs_access(const char *p,int m){return access(mapped(p),m);}
int native_preview_vfs_mkdir(const char *p,mode_t m){return mkdir(mapped(p),m);}
int native_preview_vfs_unlink(const char *p){return unlink(mapped(p));}
int native_preview_vfs_stat(const char *p,struct stat *s){return stat(mapped(p),s);}
int native_preview_vfs_rename(const char *a,const char *b){char from[1024];snprintf(from,sizeof(from),"%s",mapped(a));return rename(from,mapped(b));}
bool read_pico_connectivity_online(void){return online;}
bool ps_background_io_allowed(void){return false;}
int64_t ps_now(void){return 0;}
bool ps_store(const char *key,const void *p,size_t n){(void)key;(void)p;(void)n;return true;}
bool ps_load(const char *key,void *p,size_t n){(void)key;(void)p;(void)n;return false;}
const char *ps_version(void){return "0.40.0-dual97";}
bool ps_games_install(const char *staged,bool replace){
    assert(replace);if(fail_install)return false;
    const char *name=strrchr(staged,'/');assert(name);name++;
    size_t n=strcspn(name,".");assert(n&&n<32&&install_count<32);
    memcpy(installs[install_count],name,n);installs[install_count][n]=0;install_count++;
    char local[96];app_path(local,sizeof(local),installs[install_count-1],!strcmp(installs[install_count-1],"delta"));
    assert(native_preview_vfs_rename(staged,local)==0);return true;
}
int xTaskCreate(void (*fn)(void *),const char *name,unsigned stack,void *arg,int priority,TaskHandle_t *handle){
    (void)name;(void)priority;assert(stack==8192&&!queued);queued=fn;queued_arg=arg;*handle=(void *)1;return pdPASS;
}
void vTaskDelete(TaskHandle_t task){(void)task;longjmp(task_end,1);}
static void run_worker(void){assert(queued);void (*fn)(void *)=queued;void *arg=queued_arg;queued=NULL;if(!setjmp(task_end)){fn(arg);assert(!"worker returned without deletion");}}
esp_err_t esp_crt_bundle_attach(void *x){(void)x;return ESP_OK;}
esp_http_client_handle_t esp_http_client_init(const esp_http_client_config_t *c){
    struct native_http_client *h=calloc(1,sizeof(*h));assert(h);h->status=200;
    if(strstr(c->url,"apps.txt")){assert(!strcmp(c->url,s_channel==PS_UPDATE_CHANNEL_TEST?APPS_URL_TEST:APPS_URL));catalog_reads++;h->body=feed;h->size=strlen(feed);}
    else {const char *id=strrchr(c->url,'/');assert(id&&request_count<32);id++;
        const char *prefix=s_channel==PS_UPDATE_CHANNEL_TEST?PACKAGE_URL_TEST:PACKAGE_URL;
        assert(!strncmp(c->url,prefix,strlen(prefix)));
        size_t n=strcspn(id,".");assert(n<32);memcpy(requests[request_count],id,n);requests[request_count][n]=0;request_count++;
        make_package(wrong_id?"wrong":requests[request_count-1],served_version);
        h->body=package;h->size=sizeof(package);if(fail_download)h->status=500;}
    return h;
}
esp_err_t esp_http_client_open(esp_http_client_handle_t h,int n){(void)h;(void)n;return ESP_OK;}
int64_t esp_http_client_fetch_headers(esp_http_client_handle_t h){return h->size;}
int esp_http_client_get_status_code(esp_http_client_handle_t h){return h->status;}
int esp_http_client_read(esp_http_client_handle_t h,char *out,int n){size_t count=h->size-h->pos;if(count>(size_t)n)count=n;memcpy(out,h->body+h->pos,count);h->pos+=count;return (int)count;}
bool esp_http_client_is_complete_data_received(esp_http_client_handle_t h){return h->pos==h->size;}
esp_err_t esp_http_client_cleanup(esp_http_client_handle_t h){free(h);return ESP_OK;}
static void drain(void){for(int i=0;i<20;i++){if(queued)run_worker();else if(s_dl_state==DL_READY)ps_updates_tick();else break;}assert(!queued&&s_dl_state==DL_DONE);}
static void reset(const char *base,unsigned index){
    snprintf(root,sizeof(root),"%s/case-%u",base,index);assert(mkdir(root,0700)==0);
    char dir[1024];const char *dirs[]={"/apps","/apps/games","/apps/tools"};
    for(unsigned i=0;i<3;i++){snprintf(dir,sizeof(dir),"%s%s",root,dirs[i]);assert(mkdir(dir,0700)==0);}
    online=true;fail_install=fail_download=false;request_count=install_count=catalog_reads=0;
    feed=default_feed;served_version=2;wrong_id=false;
    s_dl_state=DL_IDLE;s_manifest_loaded=s_manifest_catalog_ready=false;s_task=NULL;queued=NULL;
    s_channel=PS_UPDATE_CHANNEL_STABLE;s_channel_loaded=true;
    assert(ps_updates_apps_catalog_refresh());run_worker();assert(catalog_reads==1&&request_count==0);
}
int main(int argc,char **argv){
    assert(argc==2);const char *one[]={"beta"},*some[]={"alpha","delta"},*unknown[]={"unknown"};
    reset(argv[1],1);assert(ps_updates_download_apps_selected(one,1));drain();
    assert(request_count==1&&install_count==1&&!strcmp(requests[0],"beta")&&catalog_reads==1);
    puts("PASS: selecting beta downloads and installs only beta across the install handoff");
    reset(argv[1],2);assert(ps_updates_download_apps_selected(some,2));
    feed="game=epsilon\ntool=delta\ngame=gamma\ngame=beta\ngame=alpha\n";
    run_worker();assert(s_dl_state==DL_READY);
    online=false;assert(!ps_updates_apps_catalog_refresh());assert(s_dl_state==DL_READY);online=true;
    assert(!ps_updates_apps_catalog_refresh()&&!ps_updates_download_apps()&&!ps_updates_check());drain();
    assert(request_count==2&&install_count==2&&!strcmp(requests[0],"alpha")&&!strcmp(requests[1],"delta")&&catalog_reads==1);
    puts("PASS: nonadjacent selection survives changed website ordering; concurrent refresh is rejected");
    reset(argv[1],3);assert(!ps_updates_download_apps_selected(NULL,0));assert(!ps_updates_download_apps_selected(unknown,1));assert(!queued&&!request_count);
    assert(ps_updates_download_apps_selected(one,1));fail_download=true;run_worker();assert(s_dl_state==DL_FAILED&&request_count==1&&!install_count&&!queued);
    fail_download=false;assert(ps_updates_download_apps_selected(some+1,1));drain();assert(request_count==2&&install_count==1&&!strcmp(installs[0],"delta"));
    puts("PASS: empty and unknown selections start no work; failure stops and retry uses the new selection");
    reset(argv[1],4);assert(ps_updates_download_apps_selected(some,2));run_worker();fail_install=true;ps_updates_tick();assert(s_dl_state==DL_FAILED&&request_count==1&&!queued);
    puts("PASS: installation failure does not continue to unselected packages");
    reset(argv[1],5);assert(ps_updates_download_apps());drain();assert(request_count==5&&install_count==5&&catalog_reads==2);
    puts("PASS: explicit all-app download remains supported and reads the catalog only once per batch");
    reset(argv[1],6);local_package("alpha",1);local_package("beta",2);local_package("gamma",3);
    assert(ps_updates_apps_catalog_refresh());run_worker();
    ps_updates_app_t apps[12];assert(ps_updates_apps_catalog(apps,12)==5);
    assert(apps[0].installed&&apps[0].update_available&&apps[0].local_version==1&&apps[0].remote_version==2);
    assert(apps[1].installed&&!apps[1].update_available&&apps[2].installed&&!apps[2].update_available);
    assert(!ps_updates_download_apps_selected(one,1));
    const char *upgrade[]={"alpha","delta","beta","gamma"};assert(ps_updates_download_apps_selected(upgrade,4));drain();
    assert(request_count==2&&install_count==2&&!strcmp(requests[0],"alpha")&&!strcmp(requests[1],"delta"));
    assert(!ps_updates_download_apps_selected(upgrade,4));
    puts("PASS: select old installed apps plus missing apps; same and newer local versions never download, including after installation");
    reset(argv[1],7);local_package("beta",1);assert(ps_updates_apps_catalog_refresh());run_worker();
    assert(ps_updates_download_apps_selected(one,1));served_version=1;run_worker();assert(s_dl_state==DL_FAILED&&!install_count);
    served_version=2;wrong_id=true;assert(ps_updates_download_apps_selected(one,1));run_worker();assert(s_dl_state==DL_FAILED&&!install_count);
    wrong_id=false;assert(ps_updates_download_apps_selected(one,1));drain();assert(install_count==1);
    puts("PASS: stale catalog payload, same-version and wrong-ID packages cannot replace the installed app; retry succeeds");
    reset(argv[1],8);local_package("beta",1);feed="game=beta\n";
    assert(ps_updates_apps_catalog_refresh());run_worker();assert(request_count==1);
    assert(ps_updates_apps_catalog(apps,12)==1&&apps[0].update_available&&apps[0].remote_version==2);
    assert(ps_updates_download_apps_selected(one,1));drain();assert(request_count==2&&install_count==1);
    online=false;assert(!ps_updates_apps_catalog_refresh());assert(!ps_updates_download_apps_selected(one,1));
    puts("PASS: legacy catalog probes the remote header and offers an update without requiring website changes; offline starts no downloads");
    assert(!parse_apps("game=beta\napp_version=beta,-1\n"));
    assert(!parse_apps("game=beta\napp_version=beta,4294967296\n"));
    assert(!parse_apps("game=beta\ngame=beta\n"));
    assert(!parse_apps("game=beta\napp_version=beta,2\napp_version=beta,3\n"));
    puts("PASS: malformed versions and ambiguous duplicate catalogs are rejected");
    reset(argv[1],9);
    assert(ps_updates_channel_set(PS_UPDATE_CHANNEL_TEST));
    assert(!s_manifest_loaded&&!s_manifest_catalog_ready&&!s_manifest_count);
    assert(!ps_updates_download_apps_selected(one,1));
    assert(ps_updates_apps_catalog_refresh());
    assert(!ps_updates_channel_set(PS_UPDATE_CHANNEL_STABLE));
    run_worker();assert(ps_updates_download_apps_selected(one,1));
    run_worker();assert(s_dl_state==DL_READY&&!ps_updates_channel_set(PS_UPDATE_CHANNEL_STABLE));
    drain();assert(request_count==1&&install_count==1);
    assert(ps_updates_channel_set(PS_UPDATE_CHANNEL_STABLE));
    assert(!s_manifest_loaded&&!s_manifest_catalog_ready&&!s_manifest_count);
    assert(ps_updates_apps_catalog_refresh());run_worker();
    puts("PASS: test catalog and package URLs stay isolated; switching clears stale selections and waits for downloads/installations");
    return 0;
}

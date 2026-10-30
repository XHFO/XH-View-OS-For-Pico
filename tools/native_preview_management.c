/* SPDX-License-Identifier: Apache-2.0
 * 原生预览仅替换HTTP边界；配置、实体与控制均经过固件JSON控制器。
 * Native preview replaces only HTTP I/O; configuration/entities/control use the firmware JSON controller.
 */
#include "native_preview_hal.h"
#include "read_pico_management.h"
#include "read_pico_content.h"
#include "read_pico_connectivity.h"
#include "home_link_web.h"
#include "../components/read_pico_home_link/home_link_json.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <fcntl.h>
#include <unistd.h>
#include <pthread.h>
#include <stdatomic.h>
static read_pico_management_status_t nm_status;
static int64_t nm_expires;
static char nm_key[33];
static read_pico_management_extension_t nm_extension;
static read_pico_management_image_extension_t nm_image_extension;
void read_pico_management_set_image_extension(read_pico_management_image_extension_t handler){nm_image_extension=handler;}
void read_pico_management_set_extension(read_pico_management_extension_t handler){nm_extension=handler;}
static int64_t nm_now(void) { struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return (int64_t)t.tv_sec*1000+t.tv_nsec/1000000; }
void read_pico_management_stop(void) {read_pico_content_cancel();read_pico_bambu_auth_cancel();memset(&nm_status,0,sizeof(nm_status));memset(nm_key,0,sizeof(nm_key));nm_expires=0;}
bool read_pico_management_start(void) {
    read_pico_management_stop();read_pico_connectivity_status_t net;read_pico_connectivity_get_status(&net);
    if(!net.connected||net.suspended||net.busy||net.transfer_active){snprintf(nm_status.error,sizeof(nm_status.error),"请先连接 Wi-Fi");return false;}
    unsigned char random[16];int fd=open("/dev/urandom",O_RDONLY);ssize_t n=fd>=0?read(fd,random,sizeof(random)):-1;if(fd>=0)close(fd);
    if(n!=(ssize_t)sizeof(random))return false;
    for(size_t i=0;i<sizeof(random);++i)snprintf(nm_key+i*2,3,"%02x",random[i]);memset(random,0,sizeof(random));
    const char *host=getenv("RPICO_MANAGEMENT_HOST");if(!host||!*host)host="127.0.0.1:8782";
    snprintf(nm_status.host,sizeof(nm_status.host),"%s",host);
    snprintf(nm_status.url,sizeof(nm_status.url),"http://%s/manage#key=%s",host,nm_key);
    nm_status.active=true;nm_expires=nm_now()+15*60*1000;nm_status.seconds_left=900;return true;
}
void read_pico_management_get_status(read_pico_management_status_t *out) {
    if(!out)return;*out=nm_status;int64_t left=nm_expires-nm_now();out->seconds_left=out->active&&left>0?(uint32_t)((left+999)/1000):0;
}
void read_pico_management_tick(void) {
    if(!nm_status.active)return;read_pico_connectivity_status_t net;read_pico_connectivity_get_status(&net);
    if(!net.connected||net.suspended||net.transfer_active||nm_now()>=nm_expires)read_pico_management_stop();
}
bool read_pico_management_active(void) {return nm_status.active&&nm_now()<nm_expires;}
// 仅成功的同一有效会话请求延长空闲期限。/ Renew idle lifetime only for a successful request in the same active session.
static void nm_renew(const char *key,int status){if(status>=200&&status<300&&key&&read_pico_management_active()&&!strcmp(key,nm_key))nm_expires=nm_now()+15*60*1000;}
typedef struct {
    read_pico_content_request_t request;
    FILE *input,*output;
    char key[33],result[32768];
    size_t downloaded;
    int status;
    atomic_bool done;
} nm_content_job_t;
static bool nm_content_auth(void *p){nm_content_job_t *j=p;return read_pico_management_active()&&!strcmp(j->key,nm_key);}
static int nm_content_receive(void *p,void *data,size_t n){nm_content_job_t *j=p;if(!j->input)return -1;size_t got=fread(data,1,n,j->input);return got?(int)got:-1;}
static bool nm_content_download(void *p,const void *data,size_t n){nm_content_job_t *j=p;if(!j->output)return false;if(n&&fwrite(data,1,n,j->output)!=n)return false;j->downloaded+=n;return n||!fflush(j->output);}
static void *nm_content_worker(void *p){nm_content_job_t *j=p;j->status=read_pico_content_handle(&j->request,j->result,sizeof(j->result));atomic_store_explicit(&j->done,true,memory_order_release);return NULL;}
void native_preview_content_rpc(const char *line,FILE *out){
    extern void native_preview_content_pump(void);
    char *decoded=NULL;cJSON *envelope=NULL;nm_content_job_t *job=calloc(1,sizeof(*job));int status=400;char result[128]="{\"ok\":false,\"error\":\"invalid_request\"}",output_path[160]={0};
    if(!job){status=503;goto done;}
    // 文本封包只包含固定暂存名，原文件从请求私有流读取。/ Envelope contains only a fixed staging name; original bytes remain in the private request stream.
    size_t len=strcspn(line,"\r\n");if(!len||len%2||len>57300)goto done;decoded=calloc(1,len/2+1);if(!decoded){status=503;goto done;}
    for(size_t i=0;i<len;i+=2){unsigned v=0;for(unsigned k=0;k<2;k++){unsigned char c=line[i+k];unsigned d=c>='0'&&c<='9'?c-'0':c>='a'&&c<='f'?c-'a'+10:16;if(d>15)goto done;v=v*16+d;}if(!v)goto done;decoded[i/2]=(char)v;}
    envelope=cJSON_Parse(decoded);if(!cJSON_IsObject(envelope))goto done;
    const cJSON *key=cJSON_GetObjectItemCaseSensitive(envelope,"key"),*method=cJSON_GetObjectItemCaseSensitive(envelope,"method"),*path=cJSON_GetObjectItemCaseSensitive(envelope,"path"),*query=cJSON_GetObjectItemCaseSensitive(envelope,"query"),*sha=cJSON_GetObjectItemCaseSensitive(envelope,"sha256"),*name=cJSON_GetObjectItemCaseSensitive(envelope,"name"),*bytes=cJSON_GetObjectItemCaseSensitive(envelope,"bytes");
    if(!cJSON_IsString(key)||strlen(key->valuestring)!=32||!cJSON_IsString(method)||!cJSON_IsString(path)||!cJSON_IsString(query)||!cJSON_IsString(name)||!cJSON_IsNumber(bytes)||bytes->valuedouble<0||bytes->valuedouble>READ_PICO_CONTENT_MAX||bytes->valuedouble!=(size_t)bytes->valuedouble)goto done;
    if(strlen(name->valuestring)!=40||strncmp(name->valuestring,"content-",8))goto done;for(unsigned i=8;i<40;i++)if(!((name->valuestring[i]>='0'&&name->valuestring[i]<='9')||(name->valuestring[i]>='a'&&name->valuestring[i]<='f')))goto done;
    strcpy(job->key,key->valuestring);if(!nm_content_auth(job)){status=401;goto done;}
    if(strcmp(path->valuestring,"/api/manage/pico/files")&&strcmp(path->valuestring,"/api/manage/pico/file"))goto done;
    char input_path[160];snprintf(input_path,sizeof(input_path),"/sdcard/.desktop-inbox/%s.bin",name->valuestring);snprintf(output_path,sizeof(output_path),"/sdcard/.desktop-inbox/%s.out",name->valuestring);
    if(bytes->valuedouble){int fd=open(input_path,O_RDONLY|O_NOFOLLOW);struct stat st;if(fd<0||fstat(fd,&st)||!S_ISREG(st.st_mode)||(size_t)st.st_size!=(size_t)bytes->valuedouble){if(fd>=0)close(fd);goto done;}job->input=fdopen(fd,"rb");if(!job->input){close(fd);goto done;}}
    int fd=open(output_path,O_WRONLY|O_CREAT|O_EXCL,0600);if(fd<0)goto done;job->output=fdopen(fd,"wb");if(!job->output){close(fd);goto done;}
    job->request=(read_pico_content_request_t){.method=method->valuestring,.endpoint=path->valuestring,.query=query->valuestring,.sha256=cJSON_IsString(sha)?sha->valuestring:NULL,.bytes=(size_t)bytes->valuedouble,.context=job,.authorized=nm_content_auth,.receive=nm_content_receive,.download=nm_content_download};
    pthread_t thread;if(pthread_create(&thread,NULL,nm_content_worker,job)){status=503;goto done;}
    while(!atomic_load_explicit(&job->done,memory_order_acquire)){native_preview_content_pump();vTaskDelay(1);}pthread_join(thread,NULL);native_preview_content_pump();status=job->status;
    if(status==200&&!job->result[0]){nm_renew(job->key,status);fprintf(out,"{\"management_response\":{\"status\":200,\"download\":\"%s.out\",\"bytes\":%zu}}\n",name->valuestring,job->downloaded);goto cleanup;}
done:
    if(job)nm_renew(job->key,status);
    fprintf(out,"{\"management_response\":{\"status\":%d,\"body\":%s}}\n",status,job&&job->result[0]?job->result:result);if(*output_path)unlink(output_path);
cleanup:
    if(job){if(job->input)fclose(job->input);if(job->output)fclose(job->output);memset(job->key,0,sizeof(job->key));free(job);}cJSON_Delete(envelope);free(decoded);fflush(out);
}
void native_preview_desktop_canvas(const char *args,FILE *out){
    char key[33]={0},name[81]={0},extra,path[160],result[512]="{\"ok\":false,\"error\":\"invalid_request\"}";int status=400;
    uint8_t *data=NULL;read_pico_management_tick();
    if(sscanf(args,"%32s %80s %c",key,name,&extra)!=2)goto done;
    if(!read_pico_management_active()||strcmp(key,nm_key)){status=401;strcpy(result,"{\"ok\":false,\"error\":\"setup_expired\"}");goto done;}
    if((strncmp(name,"canvas-",7)&&strncmp(name,"lock-",5)&&strncmp(name,"cloudflare-",11))||strstr(name,".."))goto done;
    for(const char*p=name;*p;p++)if(!((*p>='a'&&*p<='z')||(*p>='0'&&*p<='9')||*p=='-'||*p=='.'))goto done;
    snprintf(path,sizeof(path),"/sdcard/.desktop-inbox/%s",name);
    FILE *f=fopen(path,"rb");if(!f)goto done;
    if(fseek(f,0,SEEK_END)){fclose(f);goto done;}long bytes=ftell(f);rewind(f);
    bool app=!strncmp(name,"cloudflare-",11);
    bool model=!strncmp(name,"cloudflare-model-",17)||!strncmp(name,"canvas-model-",13);
    bool pair=(app&&!model)||!strncmp(name,"canvas-pair-",12);
    if(bytes<1||bytes>(model?65536:pair?840032:420000)){fclose(f);status=413;goto done;}
    data=malloc((size_t)bytes);bool read_ok=data&&fread(data,1,(size_t)bytes,f)==(size_t)bytes;fclose(f);
    if(!read_ok){status=503;goto done;}
    if(!read_pico_management_active()||strcmp(key,nm_key)){status=401;goto done;}
    const char *endpoint=app?(!strncmp(name,"cloudflare-show-",16)?"/api/manage/pico/apps/cloudflare/pair/show":"/api/manage/pico/apps/cloudflare/pair"):pair?(!strncmp(name,"canvas-pair-show-",17)?"/api/manage/pico/canvas/pair/show":"/api/manage/pico/canvas/pair"):!strncmp(name,"lock-",5)?"/api/manage/pico/lock-image":!strncmp(name,"canvas-show-",12)?"/api/manage/pico/canvas/show":"/api/manage/pico/canvas";
    if(model)endpoint=app?(!strncmp(name,"cloudflare-model-show-",22)?"/api/manage/pico/apps/cloudflare/model/show":"/api/manage/pico/apps/cloudflare/model"):(!strncmp(name,"canvas-model-show-",18)?"/api/manage/pico/canvas/model/show":"/api/manage/pico/canvas/model");
    status=nm_extension?nm_extension("POST",endpoint,model?"application/vnd.readpico.board-model+json":pair?"application/vnd.readpico.canvas-pair":"image/bmp",data,(size_t)bytes,result,sizeof(result)):404;
done:
    nm_renew(key,status);free(data);memset(key,0,sizeof(key));fprintf(out,"{\"management_response\":{\"status\":%d,\"body\":%s}}\n",status,result);fflush(out);
}
static void nm_link(FILE *f) {
    read_pico_management_status_t s;read_pico_management_get_status(&s);
    cJSON *j=cJSON_CreateObject();cJSON_AddBoolToObject(j,"active",s.active);cJSON_AddStringToObject(j,"url",s.url);cJSON_AddStringToObject(j,"error",s.error);char *raw=cJSON_PrintUnformatted(j);fputs(raw?raw:"{}",f);free(raw);cJSON_Delete(j);
}
static char *nm_decode(const char *p) {
    size_t n=strcspn(p,"\r\n");if(!n||n%2||n>57300)return NULL;char *out=calloc(1,n/2+1);if(!out)return NULL;
    for(size_t i=0;i<n;i+=2){unsigned v=0;for(int k=0;k<2;++k){unsigned char c=p[i+k];unsigned d=c>='0'&&c<='9'?c-'0':c>='a'&&c<='f'?c-'a'+10:16;if(d>15){free(out);return NULL;}v=v*16+d;}if(!v){free(out);return NULL;}out[i/2]=(char)v;}return out;
}
void native_preview_management_rpc(const char *line,FILE *f) {
    if(!strcmp(line,"open")){read_pico_management_start();fputs("{\"management\":",f);nm_link(f);fputs("}\n",f);fflush(f);return;}
    if(!strcmp(line,"status")){fputs("{\"management\":",f);nm_link(f);fputs("}\n",f);fflush(f);return;}
    char *decoded=nm_decode(line);cJSON *j=decoded?cJSON_Parse(decoded):NULL;char *out=calloc(1,RP_HOME_WEB_OUTPUT_LIMIT);int status=400;
    if(out)strcpy(out,"{\"error\":\"invalid_request\"}");
    if(j&&out){
        cJSON *key=cJSON_GetObjectItemCaseSensitive(j,"key"),*method=cJSON_GetObjectItemCaseSensitive(j,"method"),*path=cJSON_GetObjectItemCaseSensitive(j,"path"),*query=cJSON_GetObjectItemCaseSensitive(j,"query"),*body=cJSON_GetObjectItemCaseSensitive(j,"body");
        read_pico_management_tick();
        if(!cJSON_IsString(key)||!nm_status.active||strcmp(key->valuestring,nm_key)){status=401;strcpy(out,"{\"error\":\"setup_expired\"}");}
        else if(cJSON_IsString(method)&&cJSON_IsString(path)&&cJSON_IsString(query)&&cJSON_IsString(body)){
            if(nm_image_extension&&!strcmp(method->valuestring,"GET")&&!strcmp(path->valuestring,"/api/manage/pico/lock-image")){
                uint8_t *image=NULL;size_t bytes=0;status=nm_image_extension(path->valuestring,&image,&bytes);
                if(status==200){
                    nm_renew(key->valuestring,status);
                    fprintf(f,"{\"management_response\":{\"status\":200,\"image_hex\":\"");
                    for(size_t i=0;i<bytes;i++)fprintf(f,"%02x",image[i]);
                    fputs("\"}}\n",f);fflush(f);free(image);goto done;
                }
                free(image);strcpy(out,"{\"error\":\"image_unavailable\"}");
            }else if(nm_extension&&!strncmp(path->valuestring,"/api/manage/pico/",17)){
                status=nm_extension(method->valuestring,path->valuestring,"application/json",body->valuestring,strlen(body->valuestring),out,RP_HOME_WEB_OUTPUT_LIMIT);
            }else{
            read_pico_connectivity_status_t net;read_pico_connectivity_get_status(&net);rp_home_config_t *cfg=calloc(1,sizeof(*cfg));
            if(!cfg){status=503;strcpy(out,"{\"error\":\"memory\"}");}
            else{read_pico_home_link_get_config(cfg);const char *saved[READ_PICO_CONNECTIVITY_SAVED_MAX];size_t count=net.saved_count<READ_PICO_CONNECTIVITY_SAVED_MAX?net.saved_count:READ_PICO_CONNECTIVITY_SAVED_MAX;for(size_t i=0;i<count;++i)saved[i]=net.saved[i].ssid;
                rp_home_web_request_t r={.method=method->valuestring,.path=path->valuestring,.query=query->valuestring,.body=body->valuestring,.body_bytes=strlen(body->valuestring),.network_online=net.connected&&!net.suspended&&!net.transfer_active,.at_home=net.connected&&*cfg->home_wifi&&!strcmp(cfg->home_wifi,net.ssid),.current_wifi=net.ssid,.saved_wifi=saved,.saved_wifi_count=count};rp_home_web_response_t response={0};
                if(rp_home_web_handle(&r,&response,out,RP_HOME_WEB_OUTPUT_LIMIT))status=response.http_status;else{status=404;strcpy(out,"{\"error\":\"not_found\"}");}memset(cfg,0,sizeof(*cfg));free(cfg);
            }
            }
        }
    }
    if(j){cJSON *key=cJSON_GetObjectItemCaseSensitive(j,"key");if(cJSON_IsString(key))nm_renew(key->valuestring,status);}
    fprintf(f,"{\"management_response\":{\"status\":%d,\"body\":%s}}\n",status,out?out:"{\"error\":\"memory\"}");fflush(f);
done:
    if(decoded){memset(decoded,0,strlen(decoded));free(decoded);}if(j){/* 释放前清除解析副本中的口令。/ Wipe credential copies before release. */for(cJSON *p=j->child;p;p=p->next)if(cJSON_IsString(p)&&p->valuestring)memset(p->valuestring,0,strlen(p->valuestring));cJSON_Delete(j);}if(out){memset(out,0,RP_HOME_WEB_OUTPUT_LIMIT);free(out);}
}

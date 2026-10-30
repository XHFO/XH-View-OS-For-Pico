/* SPDX-License-Identifier: Apache-2.0
 * ESP HTTP client/server I/O adapters for the same-source native preview.
 * No listener is bound, no fake Home response is generated, and offline tests never use the network.
 */
#include "native_home_http_stub.h"
#include "pico_home_portal.h"
#include "home_link_json.h"
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <signal.h>
#include <spawn.h>
#include <string.h>
#include <strings.h>
#include <sys/wait.h>
#include <unistd.h>
extern char **environ;

#define NATIVE_HTTP_LIMIT (196608u)
typedef struct { char *name,*value; } native_header_t;
struct native_http_client {
    char *url,*body; size_t body_bytes,body_expected;
    native_header_t headers[16]; size_t header_count;
    esp_http_client_method_t method;
    int timeout_ms,status;
    char *response; size_t response_bytes,read_offset;
    bool complete,executed,invalid;
};
static void wipe(void *data,size_t bytes){volatile unsigned char *p=data;while(bytes--)*p++=0;}
static void wipe_free(char *p){if(p){wipe(p,strlen(p));free(p);}}
bool native_preview_home_network_enabled(void){const char *p=getenv("RPICO_HOME_LINK_OFFLINE");return !p||strcmp(p,"1");}
esp_err_t esp_crt_bundle_attach(void *ctx){(void)ctx;return ESP_OK;}
esp_http_client_handle_t esp_http_client_init(const esp_http_client_config_t *cfg){
    if(!cfg||!cfg->url||(strncmp(cfg->url,"http://",7)&&strncmp(cfg->url,"https://",8)))return NULL;
    esp_http_client_handle_t h=calloc(1,sizeof(*h));if(!h)return NULL;
    h->url=strdup(cfg->url);h->method=cfg->method;h->timeout_ms=cfg->timeout_ms;
    if(!h->url){free(h);return NULL;}return h;
}
esp_err_t esp_http_client_set_header(esp_http_client_handle_t h,const char *name,const char *value){
    if(!h||!name||!value||strpbrk(name,"\r\n:")||strpbrk(value,"\r\n")){if(h)h->invalid=true;return ESP_ERR_INVALID_ARG;}
    size_t index=h->header_count;for(size_t i=0;i<h->header_count;i++)if(!strcasecmp(name,h->headers[i].name)){index=i;break;}
    if(index>=16){h->invalid=true;return ESP_ERR_NO_MEM;}
    char *n=strdup(name),*v=strdup(value);if(!n||!v){free(n);wipe_free(v);h->invalid=true;return ESP_ERR_NO_MEM;}
    if(index<h->header_count){free(h->headers[index].name);wipe_free(h->headers[index].value);}else h->header_count++;
    h->headers[index]=(native_header_t){n,v};return ESP_OK;
}
esp_err_t esp_http_client_open(esp_http_client_handle_t h,int bytes){
    if(!h||h->invalid||bytes<0||(unsigned)bytes>NATIVE_HTTP_LIMIT)return ESP_ERR_INVALID_ARG;
    if(!native_preview_home_network_enabled())return ESP_FAIL;
    h->body_expected=(size_t)bytes;h->body=calloc(1,(size_t)bytes+1);return h->body?ESP_OK:ESP_ERR_NO_MEM;
}
int esp_http_client_write(esp_http_client_handle_t h,const char *data,int bytes){
    if(!h||!h->body||bytes<0||(size_t)bytes>h->body_expected-h->body_bytes)return -1;
    memcpy(h->body+h->body_bytes,data,(size_t)bytes);h->body_bytes+=(size_t)bytes;return bytes;
}
static bool config_line(FILE *f,const char *name,const char *value){
    if(fprintf(f,"%s = \"",name)<0)return false;
    for(const unsigned char *p=(const unsigned char*)value;*p;p++){
        if(*p=='\r'||*p=='\n')return false;
        if((*p=='\\'||*p=='\"')&&fputc('\\',f)==EOF)return false;
        if(fputc(*p,f)==EOF)return false;
    }return fputs("\"\n",f)!=EOF;
}
static bool config_body(FILE *f,const char *body){
    if(fputs("data-binary = \"",f)==EOF)return false;
    for(const unsigned char *p=(const unsigned char*)body;*p;p++){
        if(*p=='\n'){if(fputs("\\n",f)==EOF)return false;}
        else if(*p=='\r'){if(fputs("\\r",f)==EOF)return false;}
        else {if((*p=='\\'||*p=='\"')&&fputc('\\',f)==EOF)return false;if(fputc(*p,f)==EOF)return false;}
    }return fputs("\"\n",f)!=EOF;
}
static bool write_pipe(int fd,const char *p,size_t bytes){
    /* A rejected curl config must not deliver SIGPIPE to the preview process. */
    sigset_t blocked,old,pending;sigemptyset(&blocked);sigaddset(&blocked,SIGPIPE);pthread_sigmask(SIG_BLOCK,&blocked,&old);sigpending(&pending);bool was_pending=sigismember(&pending,SIGPIPE),ok=true;
    while(bytes){ssize_t n=write(fd,p,bytes);if(n<0&&errno==EINTR)continue;if(n<=0){ok=false;break;}p+=n;bytes-=(size_t)n;}
    if(!was_pending){sigpending(&pending);if(sigismember(&pending,SIGPIPE)){int number;sigwait(&blocked,&number);}}
    pthread_sigmask(SIG_SETMASK,&old,NULL);return ok;
}
static void execute_http(esp_http_client_handle_t h){
    h->executed=true;if(h->invalid||!native_preview_home_network_enabled()||h->body_bytes!=h->body_expected)return;
    char *script=NULL;size_t script_bytes=0;FILE *f=open_memstream(&script,&script_bytes);if(!f)return;
    bool ok=fprintf(f,"silent\nmax-time = %u\nconnect-timeout = 6\nmax-filesize = %u\nwrite-out = \"\\n%%{http_code}\"\n",(unsigned)(h->timeout_ms>0?(h->timeout_ms+999)/1000:8),NATIVE_HTTP_LIMIT)>=0;
    ok=ok&&config_line(f,"url",h->url)&&config_line(f,"request",h->method==HTTP_METHOD_POST?"POST":"GET");
    for(size_t i=0;i<h->header_count&&ok;i++){
        size_t n=strlen(h->headers[i].name)+strlen(h->headers[i].value)+3;char *header=malloc(n);if(!header){ok=false;break;}
        snprintf(header,n,"%s: %s",h->headers[i].name,h->headers[i].value);ok=config_line(f,"header",header);wipe_free(header);
    }
    if(h->body_expected)ok=ok&&config_body(f,h->body);
    if(fclose(f))ok=false;
    int input[2]={-1,-1},output[2]={-1,-1};pid_t pid=-1;posix_spawn_file_actions_t actions;
    if(ok&&pipe(input)==0&&pipe(output)==0){
        posix_spawn_file_actions_init(&actions);
        posix_spawn_file_actions_adddup2(&actions,input[0],STDIN_FILENO);posix_spawn_file_actions_adddup2(&actions,output[1],STDOUT_FILENO);
        posix_spawn_file_actions_addopen(&actions,STDERR_FILENO,"/dev/null",O_WRONLY,0);
        for(size_t i=0;i<2;i++){posix_spawn_file_actions_addclose(&actions,input[i]);posix_spawn_file_actions_addclose(&actions,output[i]);}
        char *argv[]={"/usr/bin/curl","--disable","--proto","=http,https","--config","-",NULL};
        if(posix_spawn(&pid,argv[0],&actions,NULL,argv,environ))pid=-1;
        posix_spawn_file_actions_destroy(&actions);
    }
    if(input[0]>=0)close(input[0]);if(output[1]>=0)close(output[1]);
    if(pid<0)ok=false;
    if(ok)ok=write_pipe(input[1],script,script_bytes);
    if(input[1]>=0)close(input[1]);if(script){wipe(script,script_bytes);free(script);}
    h->response=malloc(NATIVE_HTTP_LIMIT+5);if(!h->response)ok=false;
    int64_t deadline=esp_timer_get_time()/1000+(h->timeout_ms>0?h->timeout_ms:8000)+2000;
    bool eof=false;
    while(ok&&!eof&&esp_timer_get_time()/1000<deadline){
        struct pollfd p={.fd=output[0],.events=POLLIN};int ready=poll(&p,1,100);
        if(ready<0&&errno==EINTR)continue;if(ready<0){ok=false;break;}if(!ready)continue;
        size_t left=NATIVE_HTTP_LIMIT+4-h->response_bytes;if(!left){ok=false;break;}
        ssize_t n=read(output[0],h->response+h->response_bytes,left);
        if(n<0&&errno==EINTR)continue;if(n<0){ok=false;break;}if(!n)eof=true;else h->response_bytes+=(size_t)n;
    }
    if(output[0]>=0)close(output[0]);
    if(pid>=0){if(!ok||!eof)kill(pid,SIGKILL);int status=0;while(waitpid(pid,&status,0)<0&&errno==EINTR){}ok=ok&&eof&&WIFEXITED(status)&&WEXITSTATUS(status)==0;}
    if(ok&&h->response_bytes>=4){
        char *tail=h->response+h->response_bytes-4;
        if(tail[0]=='\n'&&tail[1]>='0'&&tail[1]<='9'&&tail[2]>='0'&&tail[2]<='9'&&tail[3]>='0'&&tail[3]<='9'){
            h->status=(tail[1]-'0')*100+(tail[2]-'0')*10+tail[3]-'0';h->response_bytes-=4;h->response[h->response_bytes]=0;h->complete=true;
        }
    }
}
int64_t esp_http_client_fetch_headers(esp_http_client_handle_t h){if(!h)return -1;if(!h->executed)execute_http(h);return h->complete?(int64_t)h->response_bytes:-1;}
int esp_http_client_get_status_code(esp_http_client_handle_t h){return h?h->status:0;}
int esp_http_client_read(esp_http_client_handle_t h,char *out,int bytes){if(!h||!h->complete||bytes<0)return -1;size_t n=h->response_bytes-h->read_offset;if(n>(size_t)bytes)n=(size_t)bytes;memcpy(out,h->response+h->read_offset,n);h->read_offset+=n;return (int)n;}
bool esp_http_client_is_complete_data_received(esp_http_client_handle_t h){return h&&h->complete&&h->read_offset==h->response_bytes;}
esp_err_t esp_http_client_close(esp_http_client_handle_t h){return h?ESP_OK:ESP_ERR_INVALID_ARG;}
esp_err_t esp_http_client_cleanup(esp_http_client_handle_t h){if(!h)return ESP_ERR_INVALID_ARG;wipe_free(h->url);if(h->body){wipe(h->body,h->body_expected+1);free(h->body);}for(size_t i=0;i<h->header_count;i++){free(h->headers[i].name);wipe_free(h->headers[i].value);}if(h->response){wipe(h->response,NATIVE_HTTP_LIMIT+5);free(h->response);}wipe(h,sizeof(*h));free(h);return ESP_OK;}

/* HTTPD captures production callbacks, but never binds a simulated LAN address. */
struct native_http_server {httpd_uri_t routes[16];size_t route_count;};
static httpd_handle_t server;
typedef struct {cJSON *headers,*response_headers;const char *body;size_t offset;int status;char *response;size_t response_bytes;} native_http_request_t;
esp_err_t httpd_start(httpd_handle_t *out,const httpd_config_t *cfg){if(!out||!cfg||server)return ESP_ERR_INVALID_STATE;server=calloc(1,sizeof(*server));if(!server)return ESP_ERR_NO_MEM;*out=server;return ESP_OK;}
esp_err_t httpd_stop(httpd_handle_t h){if(!h||h!=server)return ESP_ERR_INVALID_ARG;free(server);server=NULL;return ESP_OK;}
esp_err_t httpd_register_uri_handler(httpd_handle_t h,const httpd_uri_t *route){if(!h||h!=server||!route||h->route_count>=16)return ESP_ERR_INVALID_ARG;h->routes[h->route_count++]=*route;return ESP_OK;}
static const char *header(httpd_req_t *r,const char *name){native_http_request_t *q=r->native_context;for(cJSON *p=q->headers?q->headers->child:NULL;p;p=p->next)if(p->string&&!strcasecmp(p->string,name)&&cJSON_IsString(p))return p->valuestring;return NULL;}
size_t httpd_req_get_hdr_value_len(httpd_req_t *r,const char *name){const char *p=header(r,name);return p?strlen(p):0;}
esp_err_t httpd_req_get_hdr_value_str(httpd_req_t *r,const char *name,char *out,size_t cap){const char *p=header(r,name);if(!p||strlen(p)>=cap)return ESP_ERR_INVALID_SIZE;memcpy(out,p,strlen(p)+1);return ESP_OK;}
size_t httpd_req_get_url_query_len(httpd_req_t *r){const char *p=strchr(r->uri,'?');return p?strlen(p+1):0;}
esp_err_t httpd_req_get_url_query_str(httpd_req_t *r,char *out,size_t cap){const char *p=strchr(r->uri,'?');if(!p||strlen(p+1)>=cap)return ESP_ERR_INVALID_SIZE;strcpy(out,p+1);return ESP_OK;}
int httpd_req_recv(httpd_req_t *r,char *out,size_t bytes){native_http_request_t *q=r->native_context;size_t left=r->content_len-q->offset;if(bytes>left)bytes=left;memcpy(out,q->body+q->offset,bytes);q->offset+=bytes;return (int)bytes;}
esp_err_t httpd_resp_set_hdr(httpd_req_t *r,const char *name,const char *value){native_http_request_t *q=r->native_context;cJSON_DeleteItemFromObjectCaseSensitive(q->response_headers,name);return cJSON_AddStringToObject(q->response_headers,name,value)?ESP_OK:ESP_ERR_NO_MEM;}
esp_err_t httpd_resp_set_status(httpd_req_t *r,const char *status){native_http_request_t *q=r->native_context;q->status=atoi(status);return ESP_OK;}
esp_err_t httpd_resp_set_type(httpd_req_t *r,const char *value){return httpd_resp_set_hdr(r,"Content-Type",value);}
esp_err_t httpd_resp_send(httpd_req_t *r,const char *body,ssize_t bytes){native_http_request_t *q=r->native_context;if(bytes<0)bytes=(ssize_t)strlen(body);if((size_t)bytes>NATIVE_HTTP_LIMIT)return ESP_ERR_INVALID_SIZE;char *copy=calloc(1,(size_t)bytes+1);if(!copy)return ESP_ERR_NO_MEM;memcpy(copy,body,(size_t)bytes);free(q->response);q->response=copy;q->response_bytes=(size_t)bytes;return ESP_OK;}
void esp_fill_random(void *out,size_t bytes){unsigned char *p=out;while(bytes){uint32_t r=esp_random();size_t n=bytes<sizeof(r)?bytes:sizeof(r);memcpy(p,&r,n);p+=n;bytes-=n;}}
static char *decode_hex(const char *line){size_t n=strcspn(line,"\r\n");if(!n||(n&1)||n>57300)return NULL;char *out=calloc(1,n/2+1);if(!out)return NULL;for(size_t i=0;i<n;i+=2){unsigned v=0;for(size_t k=0;k<2;k++){unsigned char c=line[i+k];unsigned d=c>='0'&&c<='9'?c-'0':c>='a'&&c<='f'?c-'a'+10:16;if(d>15){wipe_free(out);return NULL;}v=v*16+d;}if(!v){wipe_free(out);return NULL;}out[i/2]=(char)v;}return out;}
static char *encode_base64(const unsigned char *data,size_t bytes){
    static const char alphabet[]="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    if(bytes>NATIVE_HTTP_LIMIT)return NULL;
    size_t output_bytes=((bytes+2)/3)*4;char *encoded=malloc(output_bytes+1);if(!encoded)return NULL;
    size_t out=0;for(size_t i=0;i<bytes;i+=3){unsigned a=data[i],b=i+1<bytes?data[i+1]:0,c=i+2<bytes?data[i+2]:0;
        encoded[out++]=alphabet[a>>2];encoded[out++]=alphabet[((a&3)<<4)|(b>>4)];encoded[out++]=i+1<bytes?alphabet[((b&15)<<2)|(c>>6)]:'=';encoded[out++]=i+2<bytes?alphabet[c&63]:'=';
    }encoded[out]=0;return encoded;
}
void native_preview_home_portal_rpc(const char *line,FILE *out){
    if(!strcmp(line,"open")||!strcmp(line,"status")){
        if(!strcmp(line,"open"))ps_home_portal_start(PS_HOME_PORTAL_OWNER_HOME);
        ps_home_portal_view_t view;ps_home_portal_view(&view);cJSON *j=cJSON_CreateObject();
        cJSON_AddBoolToObject(j,"active",view.active);cJSON_AddStringToObject(j,"url",view.url);cJSON_AddStringToObject(j,"phase",view.phase);cJSON_AddStringToObject(j,"error",view.error);cJSON_AddNumberToObject(j,"revision",view.revision);cJSON_AddNumberToObject(j,"seconds_left",view.seconds_left);
        char *s=cJSON_PrintUnformatted(j);fprintf(out,"{\"portal\":%s}\n",s?s:"{}");free(s);cJSON_Delete(j);fflush(out);return;
    }
    char *decoded=decode_hex(line);cJSON *envelope=decoded?cJSON_Parse(decoded):NULL;
    native_http_request_t q={.status=400,.response_headers=cJSON_CreateObject()};
    if(cJSON_IsObject(envelope)){
        cJSON *method=cJSON_GetObjectItemCaseSensitive(envelope,"method"),*path=cJSON_GetObjectItemCaseSensitive(envelope,"path"),*headers=cJSON_GetObjectItemCaseSensitive(envelope,"headers"),*body=cJSON_GetObjectItemCaseSensitive(envelope,"body");
        if(cJSON_IsString(method)&&cJSON_IsString(path)&&cJSON_IsObject(headers)&&cJSON_IsString(body)){
            q.headers=headers;q.body=body->valuestring;httpd_method_t m=!strcmp(method->valuestring,"GET")?HTTP_GET:!strcmp(method->valuestring,"PUT")?HTTP_PUT:!strcmp(method->valuestring,"DELETE")?HTTP_DELETE:HTTP_POST;
            httpd_req_t request={.uri=path->valuestring,.method=m,.content_len=strlen(q.body),.native_context=&q};
            q.status=server?404:503;
            if(server)for(size_t i=0;i<server->route_count;i++){httpd_uri_t route=server->routes[i];size_t n=strcspn(request.uri,"?");if(m==route.method&&strlen(route.uri)==n&&!strncmp(route.uri,request.uri,n)){q.status=200;request.user_ctx=route.user_ctx;route.handler(&request);break;}}
        }
    }
    const unsigned char *response=(const unsigned char*)(q.response?q.response:"{\"error\":\"invalid_request\"}");size_t response_bytes=q.response?q.response_bytes:strlen((const char*)response);
    char *encoded=encode_base64(response,response_bytes);
    cJSON *reply=cJSON_CreateObject(),*result=cJSON_AddObjectToObject(reply,"portal_response");cJSON_AddNumberToObject(result,"status",encoded?q.status:503);cJSON_AddItemToObject(result,"headers",q.response_headers);q.response_headers=NULL;cJSON_AddStringToObject(result,"body_base64",encoded?encoded:"");cJSON_AddNumberToObject(result,"body_bytes",encoded?response_bytes:0);
    char *raw=cJSON_PrintUnformatted(reply);fprintf(out,"%s\n",raw?raw:"{\"portal_response\":{\"status\":503,\"headers\":{},\"body_base64\":\"\",\"body_bytes\":0}}");fflush(out);wipe_free(raw);wipe_free(encoded);cJSON_Delete(reply);cJSON_Delete(q.response_headers);if(q.response){wipe(q.response,q.response_bytes);free(q.response);}if(envelope){for(cJSON *p=envelope->child;p;p=p->next)if(cJSON_IsString(p))wipe(p->valuestring,strlen(p->valuestring));cJSON *headers=cJSON_GetObjectItemCaseSensitive(envelope,"headers");for(cJSON *p=headers?headers->child:NULL;p;p=p->next)if(cJSON_IsString(p))wipe(p->valuestring,strlen(p->valuestring));cJSON_Delete(envelope);}wipe_free(decoded);
}

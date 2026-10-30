/*
 * SPDX-License-Identifier: Apache-2.0
 * 中文：官方图书封面后台下载及16灰阶缩略图；与资料查询共用任务边界。
 * English: Background official-cover download and 16-gray thumbnails, sharing the information-query task boundary.
 * 冻结：仅官方CDN HTTPS，不发授权头、不写TF、不改波形；render不取图，失败只保留占位。
 * Frozen: Official CDN HTTPS only, no authorization headers, TF writes or waveform changes; rendering never fetches and failure leaves a placeholder.
 */
#define _POSIX_C_SOURCE 200809L
#include "pico_weread_cover.h"
#include "pico_system.h"
#include "book_image.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#ifdef ESP_PLATFORM
#include "esp_crt_bundle.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
static SemaphoreHandle_t lock;
#define LOCK() xSemaphoreTake(lock,portMAX_DELAY)
#define UNLOCK() xSemaphoreGive(lock)
static void *allocate(size_t n){return heap_caps_malloc(n,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);}
#else
#include <pthread.h>
static pthread_mutex_t lock=PTHREAD_MUTEX_INITIALIZER;
#define LOCK() pthread_mutex_lock(&lock)
#define UNLOCK() pthread_mutex_unlock(&lock)
static void *allocate(size_t n){return malloc(n);}
#endif
#define COVER_MAX 262144u
static uint8_t *pixels;
static unsigned image_width,image_height;
static uint32_t image_id;
static bool available(void){
#ifdef ESP_PLATFORM
    if(!lock)lock=xSemaphoreCreateMutex();
    return lock!=NULL;
#else
    return true;
#endif
}
static bool cover_url(const char *url){
    static const char *const prefixes[]={"https://wfqqreader-1252317822.image.myqcloud.com/cover/","https://cdn.weread.qq.com/cover/","https://res.weread.qq.com/cover/"};
    if(!url||strlen(url)>768)return false;
    bool allowed=false;
    for(unsigned i=0;i<sizeof(prefixes)/sizeof(prefixes[0]);i++)if(!strncmp(url,prefixes[i],strlen(prefixes[i])))allowed=true;
    if(!allowed)return false;
    for(const unsigned char *p=(const unsigned char*)url;*p;p++)if(!((*p>='a'&&*p<='z')||(*p>='A'&&*p<='Z')||(*p>='0'&&*p<='9')||strchr(":/?&=._%+-",*p)))return false;
    return true;
}
static size_t download(const char *url,uint8_t *data,const atomic_bool *cancelled){
    size_t used=0;
#ifdef ESP_PLATFORM
    time_t now=time(NULL);if(now<1700000000||now>4102444799LL)return 0;
    int64_t started=esp_timer_get_time();
    esp_http_client_config_t config={.url=url,.timeout_ms=750,.buffer_size=1024,.crt_bundle_attach=esp_crt_bundle_attach,.disable_auto_redirect=true};
    esp_http_client_handle_t client=esp_http_client_init(&config);if(!client)return 0;
    if(esp_http_client_open(client,0)==ESP_OK){
        int64_t length=esp_http_client_fetch_headers(client);
        if(esp_http_client_get_status_code(client)==200&&length<(int64_t)COVER_MAX){
            while(used<COVER_MAX&&!atomic_load(cancelled)&&esp_timer_get_time()-started<1500000){
                int n=esp_http_client_read(client,(char*)data+used,(int)(COVER_MAX-used));if(n<=0)break;used+=(size_t)n;
            }
            if(!esp_http_client_is_complete_data_received(client))used=0;
        }
        esp_http_client_close(client);
    }
    esp_http_client_cleanup(client);
#elif defined(PICO_NATIVE_PREVIEW)
    const char *fixture=getenv("RPICO_WEREAD_FIXTURE_ROOT");
    const char *offline=getenv("RPICO_WEREAD_OFFLINE");
    if(fixture&&*fixture){
        char path[1024];int n=snprintf(path,sizeof(path),"%s/cover.jpg",fixture);if(n<=0||(size_t)n>=sizeof(path))return 0;
        FILE *file=fopen(path,"rb");if(!file)return 0;used=fread(data,1,COVER_MAX,file);if(!feof(file)||ferror(file))used=0;fclose(file);
    }else if(!offline||!strcmp(offline,"0")){
        // URL字符集在调用前验证，单引号不可能进入命令；curl不跟随跳转。
        // Validate the URL alphabet first so quotes cannot enter the command; curl never follows redirects.
        char command[1024];int n=snprintf(command,sizeof(command),"/usr/bin/curl --disable --silent --fail --proto '=https' --max-time 1.5 --max-filesize %u '%s'",COVER_MAX,url);
        if(n<=0||(size_t)n>=sizeof(command))return 0;
        FILE *pipe=popen(command,"r");if(!pipe)return 0;
        while(used<COVER_MAX&&!atomic_load(cancelled)){size_t n=fread(data+used,1,COVER_MAX-used,pipe);if(!n)break;used+=n;}
        if(pclose(pipe)||used==COVER_MAX)used=0;
    }
#else
    (void)url;(void)data;
#endif
    return atomic_load(cancelled)?0:used;
}
void ps_weread_cover_clear(void){
    if(!available())return;
    LOCK();free(pixels);pixels=NULL;image_width=image_height=0;image_id=0;UNLOCK();
}
void ps_weread_cover_prepare(uint32_t request_id,const char *url,const atomic_bool *cancelled){
    if(!request_id||!cancelled||!available()||!cover_url(url)||atomic_load(cancelled))return;
    uint8_t *data=allocate(COVER_MAX);if(!data)return;
    size_t bytes=download(url,data,cancelled);uint8_t *source=NULL;uint16_t w=0,h=0;
    bool ok=bytes&&book_image_decode(data,bytes,512u*1024u,&source,&w,&h);free(data);
    if(!ok||atomic_load(cancelled)){free(source);return;}
    unsigned tw=w,th=h;
    if(tw>128){th=th*128/tw;tw=128;}
    if(th>192){tw=tw*192/th;th=192;}
    if(!tw||!th){free(source);return;}
    uint8_t *thumb=allocate(tw*th);if(!thumb){free(source);return;}
    for(unsigned y=0;y<th;y++)for(unsigned x=0;x<tw;x++){
        unsigned gray=source[((size_t)y*h/th)*w+(size_t)x*w/tw];thumb[y*tw+x]=(uint8_t)(((gray+8)/17)*17);
    }
    free(source);
    if(atomic_load(cancelled)){free(thumb);return;}
    LOCK();free(pixels);pixels=thumb;image_width=tw;image_height=th;image_id=request_id;UNLOCK();
}
bool ps_weread_cover_draw(uint32_t request_id,uint8_t *fb,int x,int y,int width,int height){
    if(!request_id||!fb||width<=0||height<=0)return false;
#ifdef ESP_PLATFORM
    if(!lock)return false;
#endif
    LOCK();bool ok=pixels&&image_id==request_id;
    if(ok){
        if((int64_t)width*image_height>(int64_t)height*image_width){int fit=height*(int)image_width/(int)image_height;x+=(width-fit)/2;width=fit;}
        else{int fit=width*(int)image_height/(int)image_width;y+=(height-fit)/2;height=fit;}
        EpdRect target=ps_rect(x,y,width,height);
        for(int row=0;row<target.height;row++)for(int col=0;col<target.width;col++)
            epd_draw_pixel(target.x+col,target.y+row,pixels[((size_t)row*image_height/(unsigned)target.height)*image_width+(size_t)col*image_width/(unsigned)target.width],fb);
    }
    UNLOCK();return ok;
}

/*
 * SPDX-License-Identifier: Apache-2.0
 * 中文：接收独立的电脑画布和自定义锁屏BMP，保存TF后由UI循环采纳，双方向快照原子保留。
 * English: Persist separate desktop-canvas and custom-lock BMPs on TF and adopt atomic orientation snapshots through the UI loop for offline use.
 * 冻结：为保持锁屏16灰显示，画布与锁屏均接受1/4位BMP；仅自有目录，拒绝压缩/超限/非屏幕尺寸，失败保留上一幅，不在render读盘。
 * Frozen: Preserve 16-gray lock images with compatible 1/4-bit BMPs for canvas and locks; own directories only, reject compressed/oversized/wrong-size files, retain the previous image on failure, and no render I/O.
 * 用户修订：后台接收不切换阅读或画稿；saved仅表示已保存，displayed必须来自真实推屏完成。
 * User revision: Background reception never switches reading/drawing; saved means persisted, while displayed requires an actual completed present.
 */
#include "pico_desktop.h"
#include "pico_navigation.h"
#include "pico_weread.h"
#include "pico_power.h"
#include "../../components/read_pico_home_link/home_link_json.h"
#include "read_pico_home_link.h"
#ifndef PICO_HOME_LINK_FULL
#define PICO_HOME_LINK_FULL 0
#endif
#include "read_pico_management.h"
#include "read_pico_sd.h"
#include "read_pico_pmu.h"
#include "esp_heap_caps.h"
#include "epdiy.h"
#include <stdatomic.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <errno.h>
#include <limits.h>
#include <unistd.h>

#define CANVAS_LIMIT 420000u
#define PAIR_LIMIT (2u*CANVAS_LIMIT+32u)
#define PAIR_TYPE "application/vnd.readpico.canvas-pair"
#define LOCK_LIMIT 420000u
#define MONO_LIMIT 110000u
#define CANVAS_DIR "/sdcard/readpico/desktop"
#define CANVAS_FILE CANVAS_DIR "/canvas.bmp"
#define CLOUDFLARE_FILE CANVAS_DIR "/cloudflare.bmp"
#define USB_BOARD_FILE CANVAS_DIR "/usb-board.pcb"
#define LOCK_DIR "/sdcard/readpico/lockscreen"
#define LOCK_FILE LOCK_DIR "/custom.bmp"
static atomic_bool receiving;
static _Atomic(uint8_t*) pending;
static atomic_bool pending_show;
static atomic_uint revision,state;
static atomic_bool pending_app;
static atomic_uint cloudflare_revision,cloudflare_state;
static atomic_uint *app_state(bool app){return app?&cloudflare_state:&state;}
static atomic_uint *app_revision(bool app){return app?&cloudflare_revision:&revision;}
static const char *app_file(bool app){return app?CLOUDFLARE_FILE:CANVAS_FILE;}
static const char *app_name(bool app){return app?"cloudflare":"canvas";}
static bool app_visible(bool app){return ps_power_active()?(!app&&ps_power_board_visible()):ps_route()==(app?PS_CLOUDFLARE:PS_CANVAS);}
static const char *canvas_states[]={"idle","pending","displayed","error","saved"};
// 中文：主循环采纳PMU缓存，HTTP和USB只读同一原子投影，不触发I2C或画屏。
// English: The UI loop publishes cached PMU data; HTTP/USB read one atomic projection without I2C or display work.
static atomic_uint live_power;
static void power_sample(void){
    const pmu_snapshot_t *p=read_pico_pmu_get();unsigned next=0;
    if(p&&p->present&&p->status_ok&&p->last_err==0){
        if((p->flags&PMU_STATUS_BATTERY_VALID)&&p->soc_permille<=1000)next=(p->soc_permille+5)/10+1;
        if(p->charge_state>=PMU_CHARGE_NOT_CHARGING&&p->charge_state<=PMU_CHARGE_FULL_INFERRED){
            next|=128u;
            if(p->charge_state==PMU_CHARGE_CHARGING)next|=256u;
            if(p->charge_state!=PMU_CHARGE_NOT_CHARGING)next|=512u;
        }
    }
    unsigned previous=atomic_load_explicit(&live_power,memory_order_relaxed);
    if((previous&1023u)!=next)atomic_store_explicit(&live_power,((previous+1024u)&~1023u)|next,memory_order_relaxed);
}
void ps_desktop_power_status(char *out,size_t capacity){
    unsigned p=atomic_load_explicit(&live_power,memory_order_relaxed);
    snprintf(out,capacity,"{\"known\":%s,\"charging\":%s,\"external_power\":%s,\"battery_percent\":%d,\"revision\":%u}",
        p&128u?"true":"false",p&256u?"true":"false",p&512u?"true":"false",(int)(p&127u)-1,p>>10);
}
static bool usb_scan=true,usb_pending;
static char usb_task[33];
static unsigned usb_state;
static bool usb_app;
static unsigned usb_revision;
// USB停等分块只占PSRAM；接收期间TF留在设备，完成校验后才一次原子提交。
// USB stop-and-wait chunks occupy PSRAM only; TF remains device-owned until a validated atomic commit.
static uint8_t *direct_image;
static size_t direct_bytes,direct_used,direct_chunk;
static uint32_t direct_crc;
static char direct_task[33];
static int64_t direct_last;
static bool direct_show,direct_pair,direct_app;
static atomic_uint power_snapshot,power_request,power_state,power_revision;
static atomic_uint lock_state,lock_revision,lock_dimensions,lock_style_snapshot,cover_layout_snapshot;
static atomic_bool lock_available;
static _Atomic(uint8_t*) lock_pending;
static uint8_t *lock_image;
static uint8_t *image;
static bool image_app;
static unsigned image_revision;
static unsigned image_width,image_height,image_offset,image_stride;
static bool image_top,image_dark_one;
static int valid_file(const char *path);
static uint32_t usb_crc(const uint8_t *p,size_t n);
static uint32_t u32(const uint8_t*p);
static bool paired(const uint8_t *p){return p&&!memcmp(p,"PICOPAIR",8);}
static const char *orientation(void){return epd_rotated_display_width()>epd_rotated_display_height()?"landscape":"portrait";}
static const uint8_t *oriented(const uint8_t *p){return paired(p)?p+32+(epd_rotated_display_width()>epd_rotated_display_height()?u32(p+16):0):p;}
static unsigned u16(const uint8_t*p){return (unsigned)p[0]|(unsigned)p[1]<<8;}
static uint32_t u32(const uint8_t*p){return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static bool valid(const uint8_t *p,size_t n){
    if(!p||n<62||n>CANVAS_LIMIT||p[0]!='B'||p[1]!='M'||u32(p+2)!=n||u32(p+14)!=40||
       u16(p+26)!=1||(u16(p+28)!=1&&u16(p+28)!=4)||u32(p+30)!=0)return false;
    uint32_t w=u32(p+18);int32_t signed_h=(int32_t)u32(p+22);
    if(signed_h==INT32_MIN)return false;
    unsigned h=(unsigned)(signed_h<0?-signed_h:signed_h),off=u32(p+10),bits=u16(p+28);
    if(!((w==684&&h==1216)||(w==1216&&h==684)))return false;
    unsigned stride=((w*bits+31)/32)*4;
    if(off>n||stride*h!=n-off)return false;
    if(bits==1){
        if(n>MONO_LIMIT||off<62||(u32(p+46)!=0&&u32(p+46)!=2))return false;
        unsigned a=p[54]*11u+p[55]*59u+p[56]*30u,b=p[58]*11u+p[59]*59u+p[60]*30u;
        return (a<12800u)!=(b<12800u);
    }
    // 4位协议仅接受无透明度的16个原生灰阶；色板顺序可变，像素索引不能当亮度。
    // The 4-bit protocol accepts all 16 native grays without alpha; palette order is independent of pixel luminance.
    if(n<118||off!=118||u16(p+6)||u16(p+8)||
       (u32(p+34)!=0&&u32(p+34)!=stride*h)||
       (u32(p+46)!=0&&u32(p+46)!=16)||(u32(p+50)!=0&&u32(p+50)!=16))return false;
    unsigned seen=0;
    for(unsigned i=0;i<16;i++){
        const uint8_t *color=p+54+i*4;unsigned gray=color[0];
        if(color[1]!=gray||color[2]!=gray||color[3]||gray%17)return false;
        unsigned bit=1u<<(gray/17);if(seen&bit)return false;seen|=bit;
    }
    return seen==65535u;
}
// 双方向快照在同一个容器中校验、保存；任一方向损坏都不提交。/ Validate and persist both orientations as one snapshot; either invalid member rejects the transaction.
static bool valid_pair(const uint8_t *p,size_t n){
    if(!p||n<32+236||n>PAIR_LIMIT||!paired(p)||u32(p+8)!=1||u32(p+12)!=32)return false;
    size_t a=u32(p+16),b=u32(p+24);
    if(a>CANVAS_LIMIT||b>CANVAS_LIMIT||a<118||b<118||a+b!=n-32)return false;
    const uint8_t *portrait=p+32,*landscape=portrait+a;
    return valid(portrait,a)&&valid(landscape,b)&&u16(portrait+28)==4&&u16(landscape+28)==4&&
        u32(portrait+18)==684&&u32(landscape+18)==1216&&
        usb_crc(portrait,a)==u32(p+20)&&usb_crc(landscape,b)==u32(p+28);
}
static bool valid_canvas(const uint8_t *p,size_t n){return valid(p,n)||valid_pair(p,n);}
static bool card_ready(void){read_pico_sd_info_t sd={0};return !read_pico_sd_get_info(&sd)&&sd.present&&sd.mounted&&!sd.exported;}
static void *allocate(size_t n){return heap_caps_malloc(n,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);}
static void adopt(uint8_t *next){
    free(image);image=next;next=(uint8_t*)oriented(next);image_width=u32(next+18);
    int32_t h=(int32_t)u32(next+22);image_top=h<0;image_height=(unsigned)(h<0?-h:h);
    image_stride=((image_width*u16(next+28)+31)/32)*4;image_offset=u32(next+10);
    image_dark_one=next[58]*11u+next[59]*59u+next[60]*30u<12800u;
}
static bool save_at(const char *file,const char *dir,const uint8_t *p,size_t n){
    const char *dirs[]={"/sdcard/readpico",dir};char temporary[128],backup[128];
    snprintf(temporary,sizeof(temporary),"%s.tmp",file);snprintf(backup,sizeof(backup),"%s.bak",file);
    for(unsigned i=0;i<2;i++)if(mkdir(dirs[i],0777)&&errno!=EEXIST)return false;
    FILE *f=fopen(temporary,"wb");if(!f)return false;
    bool ok=fwrite(p,1,n,f)==n&&fflush(f)==0;
    if(ok&&fsync(fileno(f)))ok=false;
    if(fclose(f))ok=false;
    if(!ok)goto failed;
    struct stat st;bool existed=!stat(file,&st),rotated=false;
    if(existed){
        int previous=valid_file(file);
        if(previous<0)goto failed;
        if(previous){
            // 损坏的主档不能覆盖唯一有效备份。/ A corrupt primary must never replace the only valid backup.
            unlink(backup);if(rename(file,backup))goto failed;rotated=true;
        }else if(unlink(file))goto failed;
    }
    if(rename(temporary,file)){if(rotated)rename(backup,file);return false;}
    return true;
failed:
    unlink(temporary);return false;
}
static bool save_image(const uint8_t *p,size_t n){return save_at(CANVAS_FILE,CANVAS_DIR,p,n);}
// 中文：校验与载入共用同一读盘路径，保留损坏/IO失败的区别和备份选择。
// English: Validation and loading share one read path, retaining corrupt-vs-I/O failure and backup selection.
static int read_image(const char *path,uint8_t **data,size_t *bytes){
    *data=NULL;FILE *f=fopen(path,"rb");if(!f)return -1;
    struct stat st;if(fstat(fileno(f),&st)){fclose(f);return -1;}
    bool canvas=!strncmp(path,CANVAS_FILE,sizeof(CANVAS_FILE)-1)||!strncmp(path,CLOUDFLARE_FILE,sizeof(CLOUDFLARE_FILE)-1);
    if(st.st_size<62||(uint64_t)st.st_size>(canvas?PAIR_LIMIT:CANVAS_LIMIT)){fclose(f);return 0;}
    uint8_t *p=allocate((size_t)st.st_size);if(!p){fclose(f);return -1;}
    bool ok=fread(p,1,(size_t)st.st_size,f)==(size_t)st.st_size;fclose(f);
    int result=ok?((canvas?valid_canvas(p,(size_t)st.st_size):valid(p,(size_t)st.st_size))?1:0):-1;
    if(result==1){*data=p;if(bytes)*bytes=(size_t)st.st_size;}else free(p);return result;
}
static int valid_file(const char *path){uint8_t *p;int result=read_image(path,&p,NULL);free(p);return result;}
static uint8_t *load_at(const char *file,size_t *bytes){
    if(!card_ready())return NULL;
    char backup[128];snprintf(backup,sizeof(backup),"%s.bak",file);
    const char *paths[]={file,backup};
    for(unsigned i=0;i<2;i++){uint8_t *p;if(read_image(paths[i],&p,bytes)==1)return p;}
    return NULL;
}
static bool prepare_app_owned(bool app){
    uint8_t *p=load_at(app_file(app),NULL);
    free(image);image=NULL;image_app=app;image_revision=atomic_load(app_revision(app));
    if(p)adopt(p);
    return p!=NULL;
}
bool ps_desktop_prepare_app(bool app){
    if(image&&image_app==app&&image_revision==atomic_load(app_revision(app)))return true;
    bool expected=false;if(!atomic_compare_exchange_strong(&receiving,&expected,true))return false;
    bool loaded=prepare_app_owned(app);atomic_store(&receiving,false);return loaded;
}
bool ps_desktop_prepare(void){return ps_desktop_prepare_app(false);}
bool ps_desktop_app_available(bool app){return image&&image_app==app;}
unsigned ps_desktop_revision(void){return image_revision;}
static void lock_adopt(uint8_t *p){
    free(lock_image);lock_image=p;atomic_store(&lock_available,p!=NULL);
    unsigned w=p?u32(p+18):0;int32_t h=p?(int32_t)u32(p+22):0;
    atomic_store(&lock_dimensions,w|((unsigned)(h<0?-h:h)<<16));
}
bool ps_lock_image_prepare(void){
    bool expected=false;if(!atomic_compare_exchange_strong(&receiving,&expected,true))return false;
    uint8_t *p=load_at(LOCK_FILE,NULL);lock_adopt(p);atomic_store(&receiving,false);return p!=NULL;
}
bool ps_lock_image_available(void){return atomic_load(&lock_available);}
void ps_lock_image_release(void){free(lock_image);lock_image=NULL;}
int ps_desktop_image_read(const char *path,uint8_t **data,size_t *bytes){
    *data=NULL;*bytes=0;if(strcmp(path,"/api/manage/pico/lock-image"))return 404;
    bool expected=false;if(!atomic_compare_exchange_strong(&receiving,&expected,true))return 409;
    int status=409;
    if(card_ready()){
        *data=load_at(LOCK_FILE,bytes);status=*data?200:404;
    }
    atomic_store(&receiving,false);return status;
}
static void launch(uint16_t id){if(id==512){ps_navigate(PS_CLOUDFLARE);return;}if(!ps_desktop_prepare()){ps_toast("尚无灵犀画板，请先从多屏灵犀推送");return;}ps_navigate(PS_CANVAS);}
void ps_desktop_init(void){
    power_sample();usb_scan=true;
    read_pico_management_set_extension(ps_desktop_handle);
    read_pico_management_set_image_extension(ps_desktop_image_read);
    (void)ps_lock_image_prepare();
    ps_lock_image_release();
    atomic_store(&lock_style_snapshot,(unsigned)ps_sleep_style());
    atomic_store(&cover_layout_snapshot,(unsigned)ps_power_cover_layout());
    const ps_navigation_entry_t entry={.id=511,.route=PS_CANVAS,.icon=PI_LAYOUT,.label="灵犀画板",.launch=launch};
    ps_navigation_register(&entry);
    const ps_navigation_entry_t cloudflare={.id=512,.route=PS_CLOUDFLARE,.icon=PI_LINK,.label="网站流量",.launch=launch};
    ps_navigation_register(&cloudflare);
    atomic_store(&power_snapshot,ps_power_auto_minutes()|(ps_power_doubletap()?256u:0u));
}
// 中文：配置整数字段共用边界与精确整数验证，失败使用不可接受的哨兵值。
// English: Configuration integers share bounded exact-integer validation; failure yields an inadmissible sentinel.
static unsigned config_integer(const cJSON *item,unsigned maximum){
    if(!cJSON_IsNumber(item)||item->valuedouble<0||item->valuedouble>maximum)return 999u;
    unsigned value=(unsigned)item->valuedouble;return item->valuedouble==value?value:999u;
}
static int error_reply(char *out,size_t capacity,const char *error,int status){
    snprintf(out,capacity,"{\"ok\":false,\"error\":\"%s\"}",error);return status;
}
int ps_desktop_handle(const char *method,const char *path,const char *type,const void *body,size_t bytes,char *out,size_t capacity){
    if(!strncmp(path,"/api/manage/pico/weread/",24))return ps_weread_handle(method,path,type,body,bytes,out,capacity);
    if(!strcmp(path,"/api/manage/pico/status")&&!strcmp(method,"GET")){
        char power[144];ps_desktop_power_status(power,sizeof(power));
        unsigned s=atomic_load(&state);if(s>4)s=3;
        unsigned options=atomic_load(&power_snapshot);
        static const char *lock_states[]={"idle","pending","saved","error"};unsigned ls=atomic_load(&lock_state);if(ls>3)ls=3;
        unsigned dimensions=atomic_load(&lock_dimensions);
        unsigned cs=atomic_load(&cloudflare_state);if(cs>4)cs=3;
        snprintf(out,capacity,"{\"protocol\":1,\"version\":\"%s\",\"home_link_mode\":\"%s\",\"content\":{\"supported\":true,\"max_bytes\":268435456,\"max_page_items\":64},\"config\":{\"auto_sleep_minutes\":%u,\"double_tap_wake\":%s,\"lock_style\":%u,\"cover_layout\":%u,\"state\":%u,\"revision\":%u},\"canvas\":{\"supported\":true,\"width\":684,\"height\":1216,\"formats\":[\"image/bmp\"],\"max_bytes\":%u,\"bit_depths\":[1,4],\"gray_levels\":16,\"display_intent\":true,\"dual_orientation\":true,\"pair_max_bytes\":840032,\"orientation\":\"%s\",\"state\":\"%s\",\"revision\":%u,\"app_channels\":[\"cloudflare\"],\"app_receipts\":{\"cloudflare\":{\"state\":\"%s\",\"revision\":%u,\"orientation\":\"%s\"}}},\"image_lock\":{\"supported\":true,\"available\":%s,\"width\":%u,\"height\":%u,\"formats\":[\"image/bmp\"],\"max_bytes\":%u,\"bit_depths\":[1,4],\"gray_levels\":16,\"state\":\"%s\",\"revision\":%u},\"power\":%s}",ps_version(),(PICO_HOME_LINK_FULL?"full":"thin"),options&255u,options&256u?"true":"false",atomic_load(&lock_style_snapshot),atomic_load(&cover_layout_snapshot),atomic_load(&power_state),atomic_load(&power_revision),CANVAS_LIMIT,orientation(),canvas_states[s],atomic_load(&revision),canvas_states[cs],atomic_load(&cloudflare_revision),orientation(),atomic_load(&lock_available)?"true":"false",dimensions&65535u,dimensions>>16,LOCK_LIMIT,lock_states[ls],atomic_load(&lock_revision),power);return 200;
    }
    if(!strcmp(path,"/api/manage/pico/config")&&!strcmp(method,"PUT")){
        const char *end=NULL;
        cJSON *j=bytes&&bytes<512?cJSON_ParseWithLengthOpts(body,bytes,&end,false):NULL;
        bool complete=end!=NULL;
        if(end){const char *limit=(const char*)body+bytes;while(end<limit&&(*end==' '||*end=='\r'||*end=='\n'||*end=='\t'))end++;complete=end==limit;}
        cJSON *minutes=cJSON_GetObjectItemCaseSensitive(j,"auto_sleep_minutes"),*tap=cJSON_GetObjectItemCaseSensitive(j,"double_tap_wake"),*lock=cJSON_GetObjectItemCaseSensitive(j,"lock_style"),*cover=cJSON_GetObjectItemCaseSensitive(j,"cover_layout");
        unsigned lock_value=config_integer(lock,5);
        unsigned cover_value=config_integer(cover,4);
        bool lock_ok=!lock||lock_value<=5,cover_ok=!cover||cover_value<=4;
        unsigned value=config_integer(minutes,60);
        bool allowed=value==0||value==1||value==3||value==5||value==10||value==30;
        if(!complete||!cJSON_IsObject(j)||!allowed||!cJSON_IsBool(tap)||!lock_ok||!cover_ok||cJSON_GetArraySize(j)!=2+(lock?1:0)+(cover?1:0)){cJSON_Delete(j);return error_reply(out,capacity,"invalid_config",422);}
        if(lock&&lock_value==4&&!atomic_load(&lock_available)){cJSON_Delete(j);return error_reply(out,capacity,"lock_image_missing",409);}
        value|=cJSON_IsTrue(tap)?256u:0u;if(lock)value|=512u|(lock_value<<10);if(cover)value|=(1u<<17)|(cover_value<<18);cJSON_Delete(j);unsigned zero=0;
        if(!atomic_compare_exchange_strong(&power_request,&zero,0x80000000u))return error_reply(out,capacity,"config_pending",409);
        atomic_store(&power_state,1);atomic_store(&power_request,value|65536u);snprintf(out,capacity,"{\"ok\":true,\"accepted\":true}");return 202;
    }
    bool is_lock=!strcmp(path,"/api/manage/pico/lock-image");
    bool app=!strcmp(path,"/api/manage/pico/apps/cloudflare/pair")||!strcmp(path,"/api/manage/pico/apps/cloudflare/pair/show");
    bool pair=app||!strcmp(path,"/api/manage/pico/canvas/pair")||!strcmp(path,"/api/manage/pico/canvas/pair/show");
    bool show=!strcmp(path,"/api/manage/pico/canvas/show")||!strcmp(path,"/api/manage/pico/canvas/pair/show")||!strcmp(path,"/api/manage/pico/apps/cloudflare/pair/show");
    if((!is_lock&&!pair&&!show&&strcmp(path,"/api/manage/pico/canvas"))||strcmp(method,"POST"))return error_reply(out,capacity,"not_found",404);
    int code=422;const char *error="invalid_bitmap";uint8_t *copy=NULL;
    bool owned=false,expected=false;
    if(!type||strcmp(type,pair?PAIR_TYPE:"image/bmp")){code=415;error="bitmap_required";goto done;}
    if(bytes>(pair?PAIR_LIMIT:is_lock?LOCK_LIMIT:CANVAS_LIMIT)){code=413;error="too_large";goto done;}
    // 中文：端点已经匹配两个固定目标；位图验证不再重复比较相同的自有常量路径。
    // English: Both fixed owned targets were matched above; bitmap validation need not compare those same constant paths again.
    if(!(pair?valid_pair(body,bytes):valid(body,bytes)))goto done;
    if(!atomic_compare_exchange_strong(&receiving,&expected,true)){code=409;error="canvas_busy";goto done;}
    owned=true;
    // 等待上一幅真正推屏完成；拒绝请求不改变已有任务状态。/ Wait for the prior present; rejection preserves its state.
    if((is_lock?(atomic_load(&lock_state)==1||atomic_load(&lock_pending)):(atomic_load(&state)==1||atomic_load(&cloudflare_state)==1||atomic_load(&pending)))){atomic_store(&receiving,false);owned=false;code=409;error=is_lock?"lock_image_pending":"canvas_pending";goto done;}
    if(!card_ready()){code=409;error="storage_unavailable";goto done;}
    copy=allocate(bytes);if(!copy){code=503;error="memory";goto done;}memcpy(copy,body,bytes);
    if(!save_at(is_lock?LOCK_FILE:app_file(app),is_lock?LOCK_DIR:CANVAS_DIR,copy,bytes)){code=507;error="save_failed";goto done;}
    unsigned accepted_revision;
    if(is_lock){atomic_store(&lock_state,1);accepted_revision=atomic_fetch_add(&lock_revision,1)+1;atomic_store(&lock_pending,copy);}
    else{atomic_store(app_state(app),1);accepted_revision=atomic_fetch_add(app_revision(app),1)+1;atomic_store(&pending_show,show);atomic_store(&pending_app,app);atomic_store(&pending,copy);}copy=NULL;
    if(is_lock)snprintf(out,capacity,"{\"ok\":true,\"accepted\":true,\"revision\":%u}",accepted_revision);
    else snprintf(out,capacity,"{\"ok\":true,\"accepted\":true,\"channel\":\"%s\",\"revision\":%u}",app_name(app),accepted_revision);
    atomic_store(&receiving,false);return 202;
done:
    free(copy);if(owned){if(is_lock)atomic_store(&lock_state,3);else atomic_store(app_state(app),3);atomic_store(&receiving,false);}
    return error_reply(out,capacity,error,code);
}
// 本地USB只消费固定目录的完整提交；文件名不携带任意路径。/ Local USB consumes a complete fixed-directory commit, never arbitrary paths.
void ps_desktop_usb_reclaimed(void){usb_scan=true;}
void ps_desktop_usb_status(char *out,size_t capacity){
    char power[144];ps_desktop_power_status(power,sizeof(power));
    snprintf(out,capacity,"{\"usb_board\":true,\"protocol\":1,\"direct_protocol\":1,\"chunk_bytes\":1024,\"display_intent\":true,\"gray_levels\":16,\"dual_orientation\":true,\"pair_max_bytes\":840032,\"app_channels\":[\"cloudflare\"],\"channel\":\"%s\",\"revision\":%u,\"orientation\":\"%s\",\"max_bytes\":%u,\"task\":\"%s\",\"state\":\"%s\",\"power\":%s}",app_name(usb_app),usb_revision,orientation(),CANVAS_LIMIT,usb_task,canvas_states[usb_state<=4?usb_state:3],power);
}
static uint32_t usb_crc(const uint8_t *p,size_t n){
    uint32_t crc=0xffffffffu;for(size_t i=0;i<n;i++){crc^=p[i];for(unsigned b=0;b<8;b++)crc=(crc>>1)^((crc&1)?0xedb88320u:0);}return crc^0xffffffffu;
}
static void direct_discard(void){
    if(!direct_image)return;
    free(direct_image);direct_image=NULL;direct_bytes=direct_used=direct_chunk=0;direct_task[0]=0;direct_last=0;direct_show=false;direct_pair=false;direct_app=false;
    atomic_store(&receiving,false);
}
bool ps_desktop_usb_direct_active(void){return direct_image!=NULL;}
void ps_desktop_usb_direct_tick(void){
    int64_t now=ps_now();
    if(direct_image&&(now<direct_last||now-direct_last>=15000||!card_ready()))direct_discard();
}
static bool task_valid(const char *task){
    if(strlen(task)!=32)return false;
    for(unsigned i=0;i<32;i++)if(!((task[i]>='0'&&task[i]<='9')||(task[i]>='a'&&task[i]<='f')))return false;
    return true;
}
static void direct_reply(char *out,size_t capacity,const char *task,const char *phase,bool ok,size_t offset,bool app){
    snprintf(out,capacity,"{\"board_direct\":true,\"protocol\":1,\"task\":\"%s\",\"phase\":\"%s\",\"next_offset\":%u,\"accepted\":%s,\"channel\":\"%s\"}",task_valid(task)?task:"",phase,(unsigned)offset,ok?"true":"false",app_name(app));
}
bool ps_desktop_usb_direct_command(const char *line,char *out,size_t capacity,size_t *binary_bytes){
    *binary_bytes=0;out[0]=0;
    if(strncmp(line,"RPICO BOARD ",12)||!strcmp(line,"RPICO BOARD STATUS"))return false;
    ps_desktop_usb_direct_tick();
    char task[33]={0},extra=0;unsigned long length=0,offset=0,crc=0;unsigned show=0;
    const char *body=line+12;
    bool app=!strncmp(body,"BEGINAPP cloudflare ",20);
    bool pair=app||!strncmp(body,"BEGINPAIR ",10);
    if((pair||!strncmp(body,"BEGIN ",6))&&sscanf(body+(app?20:pair?10:6),"%32s %lu %lx %u %c",task,&length,&crc,&show,&extra)==4){
        bool expected=false;
        bool ok=task_valid(task)&&length>=118&&length<=(pair?PAIR_LIMIT:CANVAS_LIMIT)&&crc<=UINT32_MAX&&show<=1&&
            !direct_image&&!atomic_load(&pending)&&atomic_load(&state)!=1&&atomic_load(&cloudflare_state)!=1&&card_ready();
        if(ok)ok=atomic_compare_exchange_strong(&receiving,&expected,true);
        if(ok){
            direct_image=allocate(length);
            if(!direct_image){atomic_store(&receiving,false);ok=false;}
            else{direct_bytes=length;direct_used=direct_chunk=0;direct_crc=(uint32_t)crc;memcpy(direct_task,task,33);direct_last=ps_now();direct_show=show!=0;direct_pair=pair;direct_app=app;}
        }
        direct_reply(out,capacity,task,"ready",ok,0,app);return true;
    }
    if(!strncmp(body,"CHUNK ",6)&&sscanf(body+6,"%32s %lu %lu %c",task,&offset,&length,&extra)==3){
        bool ok=direct_image&&task_valid(task)&&!strcmp(task,direct_task)&&!direct_chunk&&offset==direct_used&&length>0&&length<=1024&&length<=direct_bytes-direct_used;
        if(ok){direct_chunk=length;direct_last=ps_now();*binary_bytes=length;}
        else direct_reply(out,capacity,task,"chunk",false,direct_used,direct_app);
        return true;
    }
    if(!strncmp(body,"COMMIT ",7)&&sscanf(body+7,"%32s %c",task,&extra)==1){
        bool owned=direct_image&&task_valid(task)&&!strcmp(task,direct_task),app=direct_app;
        size_t committed=owned?direct_used:0;
        bool ok=owned&&!direct_chunk&&direct_used==direct_bytes&&card_ready()&&(direct_pair?valid_pair(direct_image,direct_bytes):(valid(direct_image,direct_bytes)&&u16(direct_image+28)==4))&&usb_crc(direct_image,direct_bytes)==direct_crc;
        if(ok)ok=save_at(app_file(app),CANVAS_DIR,direct_image,direct_bytes);
        if(ok){
            memcpy(usb_task,task,33);usb_pending=true;usb_app=app;usb_state=1;atomic_store(app_state(app),1);usb_revision=atomic_fetch_add(app_revision(app),1)+1;
            atomic_store(&pending_show,direct_show);atomic_store(&pending_app,app);atomic_store(&pending,direct_image);direct_image=NULL;
            direct_bytes=direct_used=direct_chunk=0;direct_task[0]=0;direct_last=0;direct_show=false;direct_pair=false;direct_app=false;atomic_store(&receiving,false);
        }else if(owned)direct_discard();
        direct_reply(out,capacity,task,"committed",ok,ok?committed:0,app);return true;
    }
    if(!strncmp(body,"ABORT ",6)&&sscanf(body+6,"%32s %c",task,&extra)==1){
        bool app=direct_app,ok=direct_image&&task_valid(task)&&!strcmp(task,direct_task);
        if(ok)direct_discard();
        direct_reply(out,capacity,task,"aborted",ok,0,app);return true;
    }
    direct_reply(out,capacity,"","rejected",false,0,false);return true;
}
void ps_desktop_usb_direct_payload(const void *data,size_t bytes,char *out,size_t capacity){
    char task[33];memcpy(task,direct_task,33);bool app=direct_app;
    bool ok=direct_image&&direct_chunk&&bytes==direct_chunk&&bytes<=direct_bytes-direct_used;
    if(ok){memcpy(direct_image+direct_used,data,bytes);direct_used+=bytes;direct_chunk=0;direct_last=ps_now();}
    else direct_discard();
    direct_reply(out,capacity,task,"chunk",ok,direct_used,app);
}
static void usb_receive(void){
    if(!usb_scan||!card_ready()||atomic_load(&pending)||atomic_load(&state)==1||atomic_load(&cloudflare_state)==1)return;
    bool expected=false;if(!atomic_compare_exchange_strong(&receiving,&expected,true))return;
    usb_scan=false;FILE *f=fopen(USB_BOARD_FILE,"rb");if(!f){atomic_store(&receiving,false);return;}
    uint8_t header[64],*copy=NULL;struct stat st;bool ok=!fstat(fileno(f),&st)&&st.st_size>=126&&st.st_size<=CANVAS_LIMIT+64&&fread(header,1,64,f)==64;
    if(ok)ok=!memcmp(header,"PICOBRD1",8)&&u32(header+8)==1&&u32(header+12)==64&&u32(header+16)==(uint32_t)st.st_size-64;
    if(ok){for(unsigned i=24;i<56;i++)if(!((header[i]>='0'&&header[i]<='9')||(header[i]>='a'&&header[i]<='f')))ok=false;for(unsigned i=56;i<64;i++)if(header[i])ok=false;}
    if(ok){memcpy(usb_task,header+24,32);usb_task[32]=0;copy=allocate((size_t)st.st_size-64);ok=copy&&fread(copy,1,(size_t)st.st_size-64,f)==(size_t)st.st_size-64;}
    fclose(f);
    if(ok)ok=valid(copy,(size_t)st.st_size-64)&&u16(copy+28)==4&&usb_crc(copy,(size_t)st.st_size-64)==u32(header+20);
    if(ok)ok=save_image(copy,(size_t)st.st_size-64);
    if(ok){usb_pending=true;usb_app=false;usb_state=1;atomic_store(&state,1);usb_revision=atomic_fetch_add(&revision,1)+1;atomic_store(&pending_show,false);atomic_store(&pending_app,false);atomic_store(&pending,copy);copy=NULL;}
    else usb_state=3;
    free(copy);unlink(USB_BOARD_FILE);atomic_store(&receiving,false);
}
bool ps_desktop_tick(void){
    power_sample();ps_desktop_usb_direct_tick();
    if(!ps_background_io_allowed())return false;
    usb_receive();
    bool changed=false;unsigned options=atomic_load(&power_request);
    if(options&&options!=0x80000000u&&atomic_compare_exchange_strong(&power_request,&options,0x80000000u)){
        int next=(int)((options>>10)&7u),previous=ps_sleep_style(),previous_cover=ps_power_cover_layout();
        bool change_style=(options&512u)!=0,change_cover=(options&(1u<<17))!=0;
        bool ok=!change_style||next!=4||ps_lock_image_prepare();
        if(ok&&change_cover){ps_power_set_cover_layout((int)((options>>18)&7u));ok=ps_power_cover_layout()==(int)((options>>18)&7u);}
        if(ok&&change_style)ok=ps_apply_sleep_style(next);
        if(ok&&!ps_power_configure(options&255u,(options&256u)!=0)){ok=false;if(change_style)(void)ps_apply_sleep_style(previous);}
        if(!ok&&change_cover&&ps_power_cover_layout()!=previous_cover)ps_power_set_cover_layout(previous_cover);
        atomic_store(&power_state,ok?2:3);if(ok)atomic_fetch_add(&power_revision,1);atomic_store(&power_request,0);changed=true;
    }
    atomic_store(&power_snapshot,ps_power_auto_minutes()|(ps_power_doubletap()?256u:0u));
    atomic_store(&lock_style_snapshot,(unsigned)ps_sleep_style());
    atomic_store(&cover_layout_snapshot,(unsigned)ps_power_cover_layout());
    uint8_t *lock_next=atomic_exchange(&lock_pending,NULL);if(lock_next){lock_adopt(lock_next);ps_lock_image_release();atomic_store(&lock_state,2);}
    // 频道元数据与指针同时认领，避免接收线程在UI采纳期间覆盖展示意图。
    // Claim metadata with the pointer so a new receiver cannot replace its presentation intent.
    bool expected=false;if(!atomic_compare_exchange_strong(&receiving,&expected,true))return changed;
    uint8_t *next=atomic_exchange(&pending,NULL);
    if(!next){
        bool app=ps_route()==PS_CLOUDFLARE;
        unsigned saved_state=atomic_load(app_state(app));
        if(!ps_power_active()&&(app||ps_route()==PS_CANVAS)&&(saved_state==2||saved_state==4)&&
           (!image||image_app!=app||image_revision!=atomic_load(app_revision(app)))){
            bool loaded=prepare_app_owned(app);atomic_store(app_state(app),loaded?1:3);
            atomic_store(&receiving,false);return changed||loaded;
        }
        atomic_store(&receiving,false);
        return changed;
    }
    bool app=atomic_load(&pending_app),show=atomic_exchange(&pending_show,false);
    ps_route_t route=app?PS_CLOUDFLARE:PS_CANVAS;
    bool visible=app_visible(app);
    bool occupied=ps_power_active()?(!ps_power_board_visible()||app):(image&&image_app!=app&&app_visible(image_app));
    if(!occupied||(show&&!ps_power_active())){adopt(next);image_app=app;image_revision=atomic_load(app_revision(app));}
    else free(next);
    atomic_store(app_state(app),visible?1:4);
    if(usb_pending&&usb_app==app){usb_state=visible?1:4;if(!visible)usb_pending=false;}
    atomic_store(&receiving,false);
    if(show&&!ps_power_active()){ps_navigate(route);visible=ps_route()==route;atomic_store(app_state(app),visible?1:4);if(usb_app==app&&usb_revision==image_revision){usb_pending=visible;usb_state=visible?1:4;}}
    return changed||visible;
}
void ps_desktop_presented(bool success){
    if(!image||!app_visible(image_app)||image_revision!=atomic_load(app_revision(image_app)))return;
    atomic_store(app_state(image_app),success?2:3);
    if(usb_app==image_app&&usb_revision==image_revision){usb_state=success?2:3;usb_pending=false;}
}
static void render_bitmap(uint8_t *fb,const uint8_t *p){
    int sw=epd_rotated_display_width(),sh=epd_rotated_display_height();
    memset(fb,255,(size_t)epd_width()*epd_height()/2);if(!p)return;
    unsigned iw=u32(p+18),off=u32(p+10),bits=u16(p+28),stride=((iw*bits+31)/32)*4;int32_t signed_h=(int32_t)u32(p+22);unsigned ih=(unsigned)(signed_h<0?-signed_h:signed_h);bool top_first=signed_h<0;
    bool dark_one=p[58]*11u+p[59]*59u+p[60]*30u<12800u;
    int w=sw,h=(int)((uint64_t)w*ih/iw);
    if(h>sh){h=sh;w=(int)((uint64_t)h*iw/ih);}
    int left=(sw-w)/2,top=(sh-h)/2;
    for(int y=0;y<h;y++){
        unsigned sy=(unsigned)((uint64_t)y*ih/h);if(!top_first)sy=ih-1-sy;
        const uint8_t *row=p+off+(size_t)sy*stride;
        for(int x=0;x<w;x++){
            unsigned sx=(unsigned)((uint64_t)x*iw/w);uint8_t gray;
            if(bits==4){unsigned index=(row[sx/2]>>(sx%2?0:4))&15;gray=p[54+index*4];}
            else{bool bit=(row[sx/8]>>(7-sx%8))&1;gray=bit==dark_one?0:255;}
            epd_draw_pixel(left+x,top+y,gray,fb);
        }
    }
}
void ps_desktop_render_app(uint8_t *fb,bool app){render_bitmap(fb,image_app==app?oriented(image):NULL);}
void ps_desktop_render(uint8_t *fb){ps_desktop_render_app(fb,false);}
void ps_lock_image_render(uint8_t *fb){render_bitmap(fb,lock_image);}

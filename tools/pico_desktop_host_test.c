/* SPDX-License-Identifier: Apache-2.0
 * 中文：直接编译生产画布实现，替换存储/像素边界并注入故障。
 * English: Compile the production canvas implementation, replacing storage/pixel boundaries with failure injection.
 */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <errno.h>
#include <unistd.h>

static unsigned checks,io_count,rotation,navigations,toasts;
static unsigned model_renders;
static int test_home_package;
static bool background=true,present=true,mounted=true,exported,allocation_fail,locked,lock_board,recovery_probe;
static int64_t now_ms;
static char root[1024];
enum { FAIL_NONE,FAIL_OPEN,FAIL_WRITE,FAIL_FLUSH,FAIL_SYNC,FAIL_CLOSE,FAIL_OLD_RENAME,FAIL_NEW_RENAME,FAIL_ROLLBACK };
static int failure;
static bool fail_old_model_allocation,model_allocation_failed;
void *test_board_allocate(size_t n){return model_allocation_failed?NULL:malloc(n);}
static unsigned power_minutes=5,power_calls;
static bool power_tap=true,power_fail,power_probe,style_fail,cover_fail;
static int sleep_style,current_route=29,test_cover_layout;
static FILE *test_fopen(const char*,const char*);
static size_t test_fwrite(const void*,size_t,size_t,FILE*),test_fread(void*,size_t,size_t,FILE*);
static int test_fflush(FILE*),test_fclose(FILE*),test_fsync(int),test_mkdir(const char*,mode_t),test_rename(const char*,const char*),test_unlink(const char*),test_stat(const char*,struct stat*),test_fstat(int,struct stat*);
#define CHECK(value) do { checks++;assert(value); } while(0)
#define fopen test_fopen
#define fwrite test_fwrite
#define fread test_fread
#define fflush test_fflush
#define fclose test_fclose
#define fsync test_fsync
#define mkdir test_mkdir
#define rename test_rename
#define unlink test_unlink
#define stat(...) test_stat(__VA_ARGS__)
#define fstat test_fstat
#include "pico_desktop.c"
#undef fopen
#undef fwrite
#undef fread
#undef fflush
#undef fclose
#undef fsync
#undef mkdir
#undef rename
#undef unlink
#undef stat
#undef fstat

// 传输测试链接真实模型解析器，绘制边界在独立渲染测试覆盖。/ Transport tests use the real parser; the independent renderer suite covers drawing.
void ps_board_model_render(uint8_t *fb,const ps_board_model_t *model){
    CHECK(model);model_renders++;memset(fb,255,684u*1216u/2u);
}

static pmu_snapshot_t test_pmu;
const pmu_snapshot_t *read_pico_pmu_get(void){return &test_pmu;}
static ps_navigation_entry_t registered;
static read_pico_management_extension_t extension;
static read_pico_management_image_extension_t image_extension;
bool ps_home_link_package_installed(void) { return test_home_package != 0; }
void ps_home_portal_register(void){}
void ps_home_portal_status_suffix(char *out,size_t capacity){if(capacity)out[0]=0;}
int ps_home_portal_mac_handle(const char *method,char *out,size_t capacity){
    if(strcmp(method,"GET"))return 405;
    snprintf(out,capacity,"{\"mode\":\"pga\",\"package_installed\":%s}",test_home_package?"true":"false");return 200;
}
int ps_weread_handle(const char *method,const char *path,const char *type,const void *body,size_t bytes,char *out,size_t cap){
    (void)method;(void)type;(void)body;(void)bytes;
    CHECK(!strncmp(path,"/api/manage/pico/weread/",24));
    snprintf(out,cap,"{\"weread_route\":true}");return 202;
}
static const char *mapped(const char *path,char out[1400]){CHECK(!strncmp(path,"/sdcard",7));CHECK(snprintf(out,1400,"%s%s",root,path+7)<1400);return out;}
static FILE *test_fopen(const char *path,const char *mode){io_count++;if(failure==FAIL_OPEN&&strstr(path,".tmp")){errno=EIO;return NULL;}char p[1400];return fopen(mapped(path,p),mode);}
static size_t test_fwrite(const void *p,size_t n,size_t count,FILE *f){io_count++;if(failure==FAIL_WRITE)return fwrite(p,n,count/2,f);return fwrite(p,n,count,f);}
static size_t test_fread(void *p,size_t n,size_t count,FILE *f){io_count++;size_t read=fread(p,n,count,f);if(read&&fail_old_model_allocation)model_allocation_failed=true;return read;}
static int test_fflush(FILE*f){io_count++;if(failure==FAIL_FLUSH){errno=EIO;return -1;}return fflush(f);}
static int test_fclose(FILE*f){io_count++;int code=fclose(f);if(failure==FAIL_CLOSE){errno=EIO;return EOF;}return code;}
static int test_fsync(int fd){io_count++;if(failure==FAIL_SYNC){errno=EIO;return -1;}return fsync(fd);}
static int test_mkdir(const char *path,mode_t mode){io_count++;char p[1400];return mkdir(mapped(path,p),mode);}
static int test_rename(const char *from,const char *to){
    io_count++;bool old=!strcmp(from,CANVAS_FILE)||!strcmp(from,LOCK_FILE)||!strcmp(from,CLOUDFLARE_FILE),next=!strcmp(from,CANVAS_FILE ".tmp")||!strcmp(from,LOCK_FILE ".tmp")||!strcmp(from,CLOUDFLARE_FILE ".tmp"),restore=!strcmp(from,CANVAS_FILE ".bak")||!strcmp(from,LOCK_FILE ".bak")||!strcmp(from,CLOUDFLARE_FILE ".bak");
    if((failure==FAIL_OLD_RENAME&&old)||((failure==FAIL_NEW_RENAME||failure==FAIL_ROLLBACK)&&next)||(failure==FAIL_ROLLBACK&&restore)){errno=EIO;return -1;}
    char a[1400],b[1400];return rename(mapped(from,a),mapped(to,b));
}
static int test_unlink(const char *path){io_count++;char p[1400];return unlink(mapped(path,p));}
static int test_stat(const char *path,struct stat *st){io_count++;char p[1400];return stat(mapped(path,p),st);}
static int test_fstat(int fd,struct stat *st){io_count++;return fstat(fd,st);}
int read_pico_sd_get_info(read_pico_sd_info_t *sd){*sd=(read_pico_sd_info_t){.present=present,.mounted=mounted,.exported=exported};return 0;}
void *heap_caps_malloc(size_t n,unsigned caps){
    CHECK(caps==(MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));
    if(recovery_probe){
        char out[2048];size_t bytes;CHECK(atomic_load(&receiving));
        CHECK(ps_desktop_usb_direct_command("RPICO BOARD BEGINPAIR aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa 834444 00000000 0",out,sizeof(out),&bytes));
        CHECK(strstr(out,"\"accepted\":false")&&!direct_image&&bytes==0);
    }
    return allocation_fail?NULL:malloc(n);
}
bool ps_background_io_allowed(void){return background;}
int64_t ps_now(void){return now_ms;}
bool ps_power_active(void){return locked;}
bool ps_power_board_visible(void){return lock_board;}
const char *ps_version(void){return "desktop-host-test";}
int ps_sleep_style(void){return sleep_style;}
ps_route_t ps_route(void){return current_route;}
bool ps_apply_sleep_style(int style){if(style_fail)return false;sleep_style=style;return true;}
unsigned ps_power_auto_minutes(void){return power_minutes;}
bool ps_power_doubletap(void){return power_tap;}
int ps_power_cover_layout(void){return test_cover_layout;}
void ps_power_set_cover_layout(int layout){if(!cover_fail)test_cover_layout=layout;}
bool ps_power_configure(unsigned minutes,bool tap){
    power_calls++;
    if(power_probe){char out[2048];const char *body="{\"auto_sleep_minutes\":10,\"double_tap_wake\":false}";CHECK(atomic_load(&power_request)==0x80000000u);CHECK(ps_desktop_handle("PUT","/api/manage/pico/config","application/json",body,strlen(body),out,sizeof(out))==409);}
    if(power_fail)return false;power_minutes=minutes;power_tap=tap;return true;
}
void ps_navigate(ps_route_t route){CHECK(route==PS_CANVAS||route==PS_CLOUDFLARE);current_route=route;navigations++;}
void ps_toast(const char *text){CHECK(text&&*text);toasts++;}
bool ps_navigation_register(const ps_navigation_entry_t *entry){if(entry->id==511)registered=*entry;return true;}
void read_pico_management_set_image_extension(read_pico_management_image_extension_t handler){image_extension=handler;}
void read_pico_management_set_extension(read_pico_management_extension_t handler){extension=handler;}
int epd_rotated_display_width(void){return rotation%2?1216:684;}
int epd_rotated_display_height(void){return rotation%2?684:1216;}
int epd_width(void){return 684;}
int epd_height(void){return 1216;}
static size_t pixel_index(int x,int y){
    int px=x,py=y;
    if(rotation==1){px=y;py=1215-x;}else if(rotation==2){px=683-x;py=1215-y;}else if(rotation==3){px=683-y;py=x;}
    CHECK(px>=0&&px<684&&py>=0&&py<1216);return (size_t)py*684+px;
}
EpdRect epd_full_screen(void){return(EpdRect){0,0,epd_rotated_display_width(),epd_rotated_display_height()};}
// 中文：真实epdiy接受8位灰阶，高4位才写入4位物理画布。
// English: Real epdiy accepts 8-bit gray and writes its high nibble into the packed framebuffer.
void epd_draw_pixel(int x,int y,uint8_t gray,uint8_t *fb){size_t at=pixel_index(x,y);unsigned shift=at&1?4:0;fb[at/2]=(uint8_t)((fb[at/2]&~(15u<<shift))|(unsigned)(gray>>4)<<shift);}
void epd_fill_rect(EpdRect r,uint8_t gray,uint8_t *fb){for(int y=r.y;y<r.y+r.height;y++)for(int x=r.x;x<r.x+r.width;x++)epd_draw_pixel(x,y,gray,fb);}
static void put16(uint8_t *p,unsigned value){p[0]=value;p[1]=value>>8;}
static void put32(uint8_t *p,uint32_t value){for(int i=0;i<4;i++)p[i]=(uint8_t)(value>>(i*8));}
static bool pattern(unsigned x,unsigned y,unsigned w,unsigned h){return(x<w/4&&y<h/3)||(x>3*w/4&&y>2*h/3);}
static uint8_t *bitmap(unsigned w,unsigned h,bool top,bool dark_one,size_t padding,size_t *bytes){
    unsigned stride=((w+31)/32)*4;*bytes=62+padding+(size_t)stride*h;uint8_t *p=calloc(1,*bytes);CHECK(p);
    p[0]='B';p[1]='M';put32(p+2,(uint32_t)*bytes);put32(p+10,62+(uint32_t)padding);put32(p+14,40);put32(p+18,w);put32(p+22,top?(uint32_t)(-(int32_t)h):h);put16(p+26,1);put16(p+28,1);put32(p+34,stride*h);put32(p+46,2);
    memset(p+(dark_one?54:58),255,3);
    for(unsigned y=0;y<h;y++)for(unsigned x=0;x<w;x++){bool bit=pattern(x,y,w,h)?dark_one:!dark_one;unsigned row=top?y:h-1-y;if(bit)p[62+padding+(size_t)row*stride+x/8]|=(uint8_t)(1u<<(7-x%8));}
    return p;
}
static unsigned gray_pattern(unsigned x,unsigned y,unsigned w,unsigned h){return(x*16u/w+y*7u/h)&15u;}
static uint8_t *gray_bitmap(unsigned w,unsigned h,bool top,bool reversed,size_t *bytes){
    unsigned stride=((w*4+31)/32)*4;*bytes=118+(size_t)stride*h;uint8_t *p=calloc(1,*bytes);CHECK(p);
    p[0]='B';p[1]='M';put32(p+2,(uint32_t)*bytes);put32(p+10,118);put32(p+14,40);put32(p+18,w);put32(p+22,top?(uint32_t)(-(int32_t)h):h);put16(p+26,1);put16(p+28,4);put32(p+34,stride*h);put32(p+46,16);
    for(unsigned i=0;i<16;i++)memset(p+54+i*4,(reversed?15-i:i)*17,3);
    for(unsigned y=0;y<h;y++){
        unsigned row=top?y:h-1-y;uint8_t *to=p+118+(size_t)row*stride;
        for(unsigned x=0;x<w;x++){unsigned gray=gray_pattern(x,y,w,h),index=reversed?15-gray:gray;to[x/2]|=(uint8_t)(index<<(x%2?0:4));}
        for(unsigned x=w/2;x<stride;x++)to[x]=0xa5;
    }
    return p;
}
static void reset_memory(void){ps_board_model_free(board_model);board_model=NULL;paint_revision=0;model_renders=0;direct_discard();atomic_store(&pending_app,false);atomic_store(&cloudflare_state,0);atomic_store(&cloudflare_revision,0);image_app=usb_app=false;usb_revision=0;atomic_store(&pending_show,false);now_ms=0;locked=lock_board=false;current_route=PS_CANVAS;image_revision=0;usb_scan=true;usb_pending=false;usb_task[0]=0;usb_state=0;free(image);image=NULL;free(atomic_exchange(&pending,NULL));free(lock_image);lock_image=NULL;free(atomic_exchange(&lock_pending,NULL));atomic_store(&lock_state,0);atomic_store(&lock_revision,0);atomic_store(&lock_dimensions,0);atomic_store(&lock_available,false);atomic_store(&lock_style_snapshot,0);sleep_style=0;style_fail=false;test_cover_layout=0;cover_fail=false;atomic_store(&cover_layout_snapshot,0);atomic_store(&receiving,false);atomic_store(&state,0);atomic_store(&revision,0);atomic_store(&power_request,0);atomic_store(&power_state,0);atomic_store(&power_revision,0);atomic_store(&power_snapshot,0);image_width=image_height=image_offset=image_stride=0;background=present=mounted=true;exported=allocation_fail=false;failure=FAIL_NONE;navigations=toasts=power_calls=0;power_minutes=5;power_tap=true;power_fail=power_probe=false;}
static void erase_files(void){char p[1400];for(const char **s=(const char*[]){CANVAS_FILE,CANVAS_FILE ".tmp",CANVAS_FILE ".bak",CLOUDFLARE_FILE,CLOUDFLARE_FILE ".tmp",CLOUDFLARE_FILE ".bak",USB_BOARD_FILE,LOCK_FILE,LOCK_FILE ".tmp",LOCK_FILE ".bak",NULL};*s;s++)unlink(mapped(*s,p));}
static uint8_t *read_file(const char *path,size_t *bytes){char p[1400];FILE*f=fopen(mapped(path,p),"rb");if(!f)return NULL;CHECK(!fseek(f,0,SEEK_END));long n=ftell(f);CHECK(n>=0);rewind(f);uint8_t *data=malloc((size_t)n);CHECK(data);CHECK(fread(data,1,(size_t)n,f)==(size_t)n);CHECK(!fclose(f));*bytes=(size_t)n;return data;}
static void same_file(const char *path,const uint8_t *p,size_t n){size_t bytes=0;uint8_t*read=read_file(path,&bytes);CHECK(read&&bytes==n&&!memcmp(p,read,n));free(read);}
static int request(const char*method,const char*path,const char*type,const void*body,size_t bytes,char out[2048]){return ps_desktop_handle(method,path,type,body,bytes,out,2048);}
static int push(const void*p,size_t n){char out[2048];return request("POST","/api/manage/pico/canvas","image/bmp",p,n,out);}
static void parser_checks(void){
    size_t n;uint8_t *p=bitmap(684,1216,false,false,0,&n);CHECK(valid(p,n));CHECK(!valid(NULL,n));CHECK(!valid(p,61));CHECK(!valid(p,n-1));CHECK(!valid(p,110001));
    struct {unsigned at,bytes;uint32_t value;} cases[]={
        {0,1,'X'},{2,4,0},{10,4,61},{10,4,UINT32_MAX},{14,4,108},{18,4,0},{18,4,UINT32_MAX},{18,4,683},
        {22,4,0},{22,4,INT32_MIN},{22,4,1215},{26,2,0},{28,2,8},{30,4,1},{46,4,3}};
    for(unsigned i=0;i<sizeof(cases)/sizeof(*cases);i++){uint8_t old[4];memcpy(old,p+cases[i].at,cases[i].bytes);if(cases[i].bytes==1)p[cases[i].at]=(uint8_t)cases[i].value;else if(cases[i].bytes==2)put16(p+cases[i].at,cases[i].value);else put32(p+cases[i].at,cases[i].value);CHECK(!valid(p,n));CHECK(push(p,n)==422);memcpy(p+cases[i].at,old,cases[i].bytes);}
    uint8_t palette[8];memcpy(palette,p+54,8);memset(p+54,0,8);CHECK(!valid(p,n));memset(p+54,255,8);CHECK(!valid(p,n));memcpy(p+54,palette,8);
    char out[2048];CHECK(request("POST","/api/manage/pico/canvas","text/plain",p,n,out)==415);CHECK(push(p,420001)==413);CHECK(push(p,0)==422);CHECK(request("PUT","/api/manage/pico/canvas","image/bmp",p,n,out)==404);CHECK(request("GET","/unknown",NULL,NULL,0,out)==404);
    size_t extra=110000-n;free(p);p=bitmap(684,1216,true,true,extra,&n);CHECK(n==110000&&valid(p,n));CHECK(push(p,n)==202);CHECK(atomic_load(&state)==1&&navigations==0);background=false;CHECK(!ps_desktop_tick()&&atomic_load(&pending));background=true;CHECK(ps_desktop_tick()&&atomic_load(&state)==1&&navigations==0);ps_desktop_presented(true);CHECK(atomic_load(&state)==2);ps_desktop_presented(false);CHECK(atomic_load(&state)==3);free(p);reset_memory();erase_files();
}
static void contain_checks(void){
    const size_t bytes=684u*1216/2;uint8_t *frame=malloc(bytes+32),*lock_frame=malloc(bytes+32);CHECK(frame&&lock_frame);memset(frame,0xa5,bytes+32);memset(lock_frame,0xa5,bytes+32);
    for(unsigned orientation=0;orientation<2;orientation++)for(unsigned top=0;top<2;top++)for(unsigned palette=0;palette<2;palette++){
        size_t n;unsigned iw=orientation?1216:684,ih=orientation?684:1216;uint8_t*p=bitmap(iw,ih,top,palette,0,&n);adopt(p);
        for(rotation=0;rotation<4;rotation++){
            unsigned before=io_count;ps_desktop_render(frame);CHECK(io_count==before);
            int sw=epd_rotated_display_width(),sh=epd_rotated_display_height(),w,h;
            if((uint64_t)sw*ih>(uint64_t)sh*iw){h=sh;w=(int)((uint64_t)sh*iw/ih);}else{w=sw;h=(int)((uint64_t)sw*ih/iw);}
            int left=(sw-w)/2,top_y=(sh-h)/2;
            for(int y=0;y<sh;y++)for(int x=0;x<sw;x++){
                uint8_t expected=15;if(x>=left&&x<left+w&&y>=top_y&&y<top_y+h){unsigned sx=(unsigned)((uint64_t)(x-left)*iw/w),sy=(unsigned)((uint64_t)(y-top_y)*ih/h);expected=pattern(sx,sy,iw,ih)?0:15;}
                size_t at=pixel_index(x,y);CHECK(((frame[at/2]>>(at&1?4:0))&15)==expected);
            }
            for(unsigned guard=0;guard<32;guard++)CHECK(frame[bytes+guard]==0xa5);
            present=false;before=io_count;ps_desktop_render(frame);CHECK(before==io_count);present=true;
        }
        reset_memory();
    }
    free(frame);rotation=0;
}
static int push_lock(const void *p,size_t n);
static void gray_checks(void){
    reset_memory();erase_files();
    size_t bytes=684u*1216/2;uint8_t *frame=malloc(bytes+32),*lock_frame=malloc(bytes+32);CHECK(frame&&lock_frame);memset(frame,0xa5,bytes+32);memset(lock_frame,0xa5,bytes+32);
    for(unsigned orientation=0;orientation<2;orientation++)for(unsigned top=0;top<2;top++)for(unsigned reverse=0;reverse<2;reverse++){
        size_t n;unsigned iw=orientation?1216:684,ih=orientation?684:1216;uint8_t*p=gray_bitmap(iw,ih,top,reverse,&n);
        CHECK(n==(orientation?415990u:418422u)&&valid(p,n));
        CHECK(push_lock(p,n)==202);CHECK(!ps_desktop_tick()&&!navigations&&atomic_load(&lock_state)==2);same_file(LOCK_FILE,p,n);
        uint8_t *original=NULL;size_t original_bytes=0;CHECK(ps_desktop_image_read("/api/manage/pico/lock-image",&original,&original_bytes)==200&&original_bytes==n&&!memcmp(original,p,n));free(original);
        CHECK(push(p,n)==202);CHECK(ps_desktop_tick());ps_desktop_presented(true);same_file(CANVAS_FILE,p,n);
        char status[2048];CHECK(ps_desktop_handle("GET","/api/manage/pico/status",NULL,NULL,0,status,sizeof(status))==200);
        const char *cap=strstr(status,"\"image_lock\":");CHECK(cap&&strstr(cap,"\"max_bytes\":420000")&&strstr(cap,"\"bit_depths\":[1,4]")&&strstr(cap,"\"gray_levels\":16"));CHECK(ps_lock_image_prepare());
        for(rotation=0;rotation<4;rotation++){
            unsigned before=io_count;ps_desktop_render(frame);ps_lock_image_render(lock_frame);CHECK(io_count==before&&!memcmp(frame,lock_frame,bytes));
            int sw=epd_rotated_display_width(),sh=epd_rotated_display_height(),w=sw,h=(int)((uint64_t)sw*ih/iw);
            if(h>sh){h=sh;w=(int)((uint64_t)h*iw/ih);}int left=(sw-w)/2,ty=(sh-h)/2;unsigned seen=0;
            for(int y=0;y<sh;y++)for(int x=0;x<sw;x++){
                unsigned expected=15;
                if(x>=left&&x<left+w&&y>=ty&&y<ty+h){unsigned sx=(unsigned)((uint64_t)(x-left)*iw/w),sy=(unsigned)((uint64_t)(y-ty)*ih/h);expected=gray_pattern(sx,sy,iw,ih);seen|=1u<<expected;}
                size_t at=pixel_index(x,y);CHECK(((frame[at/2]>>(at&1?4:0))&15)==expected);
            }
            CHECK(seen==65535u);for(unsigned i=0;i<32;i++)CHECK(frame[bytes+i]==0xa5&&lock_frame[bytes+i]==0xa5);
        }
        reset_memory();ps_desktop_init();CHECK(ps_lock_image_available()&&!lock_image&&!navigations);CHECK(ps_lock_image_prepare()&&u16(lock_image+28)==4&&!memcmp(lock_image,p,n));ps_lock_image_release();
        registered.launch(511);CHECK(image&&u16(image+28)==4);free(p);reset_memory();erase_files();
    }
    rotation=0;free(frame);free(lock_frame);
    size_t n;uint8_t*p=gray_bitmap(684,1216,false,false,&n);
    struct{unsigned at,bytes;uint32_t value;}bad[]={{6,2,1},{8,2,1},{10,4,117},{14,4,108},{28,2,8},{30,4,1},{34,4,42},{46,4,15},{50,4,17},{54,1,1},{55,1,1},{56,1,1},{57,1,255},{58,1,0}};
    for(unsigned i=0;i<sizeof(bad)/sizeof(*bad);i++){
        uint8_t old[4];memcpy(old,p+bad[i].at,bad[i].bytes);
        if(bad[i].bytes==1)p[bad[i].at]=(uint8_t)bad[i].value;else if(bad[i].bytes==2)put16(p+bad[i].at,bad[i].value);else put32(p+bad[i].at,bad[i].value);
        CHECK(!valid(p,n)&&push(p,n)==422&&push_lock(p,n)==422);memcpy(p+bad[i].at,old,bad[i].bytes);
    }
    put32(p+34,0);put32(p+46,0);CHECK(valid(p,n));
    CHECK(push(p,n)==202);CHECK(ps_desktop_tick());ps_desktop_presented(true);
    for(int mode=FAIL_OPEN;mode<=FAIL_NEW_RENAME;mode++){failure=mode;CHECK(push(p,n)==507&&!atomic_load(&pending));failure=FAIL_NONE;same_file(CANVAS_FILE,p,n);}
    allocation_fail=true;CHECK(push(p,n)==503);allocation_fail=false;same_file(CANVAS_FILE,p,n);
    exported=true;CHECK(push(p,n)==409);exported=false;reset_memory();ps_desktop_init();registered.launch(511);CHECK(image&&u16(image+28)==4);
    free(p);reset_memory();erase_files();
}
static void storage_checks(void){
    size_t n;uint8_t *first=bitmap(684,1216,false,false,0,&n),*second=bitmap(684,1216,true,true,0,&n);char out[2048];
    CHECK(push(first,n)==202);CHECK(strstr((request("GET","/api/manage/pico/status",NULL,NULL,0,out),out),"\"pending\""));CHECK(!navigations);CHECK(ps_desktop_tick());CHECK(atomic_load(&state)==1&&push(second,n)==409&&atomic_load(&state)==1);ps_desktop_presented(true);same_file(CANVAS_FILE,first,n);
    for(int mode=FAIL_OPEN;mode<=FAIL_NEW_RENAME;mode++){
        failure=mode;CHECK(push(second,n)==507);CHECK(!atomic_load(&pending));CHECK(image&&!memcmp(image,first,n));failure=FAIL_NONE;same_file(CANVAS_FILE,first,n);
    }
    allocation_fail=true;CHECK(push(second,n)==503);allocation_fail=false;same_file(CANVAS_FILE,first,n);
    for(unsigned which=0;which<3;which++){present=which!=0;mounted=which!=1;exported=which==2;CHECK(push(second,n)==409);same_file(CANVAS_FILE,first,n);}present=mounted=true;exported=false;
    CHECK(push(second,n)==202);uint8_t *waiting=atomic_load(&pending);CHECK(waiting&&push(first,n)==409&&atomic_load(&pending)==waiting);CHECK(atomic_load(&state)==1);CHECK(ps_desktop_tick());ps_desktop_presented(true);same_file(CANVAS_FILE,second,n);same_file(CANVAS_FILE ".bak",first,n);
    reset_memory();ps_desktop_init();CHECK(extension==ps_desktop_handle&&registered.id==511&&registered.route==PS_CANVAS&&registered.launch);registered.launch(511);CHECK(image&&!memcmp(image,second,n)&&navigations==1);
    char routed[128];CHECK(extension("GET","/api/manage/pico/weread/status","",NULL,0,routed,sizeof(routed))==202&&strstr(routed,"weread_route"));
    reset_memory();char path[1400];FILE*f=fopen(mapped(CANVAS_FILE,path),"wb");CHECK(f);CHECK(fwrite("broken",1,6,f)==6);CHECK(!fclose(f));ps_desktop_init();registered.launch(511);CHECK(image&&!memcmp(image,first,n));
    // 恢复失败也保留完整备份；可由重开入口读取，不得把新图当作已接受。
    // Even a failed rollback preserves the old backup for a later reopen and never reports acceptance.
    failure=FAIL_ROLLBACK;CHECK(push(second,n)==507);failure=FAIL_NONE;same_file(CANVAS_FILE ".bak",first,n);
    reset_memory();erase_files();ps_desktop_init();registered.launch(511);CHECK(!image&&toasts==1&&!navigations);
    free(first);free(second);
}
static int configure(const char *json){char out[2048];return request("PUT","/api/manage/pico/config","application/json",json,strlen(json),out);}
static int push_lock(const void *p,size_t n){char out[2048];return ps_desktop_handle("POST","/api/manage/pico/lock-image","image/bmp",p,n,out,sizeof(out));}
static void lock_checks(void){
    reset_memory();erase_files();ps_desktop_init();CHECK(image_extension==ps_desktop_image_read&&!ps_lock_image_available());
    size_t n;uint8_t *first=bitmap(684,1216,false,false,0,&n),*second=bitmap(684,1216,true,true,0,&n);char out[2048];
    CHECK(configure("{\"auto_sleep_minutes\":5,\"double_tap_wake\":true,\"lock_style\":4}")==409);
    CHECK(push_lock(first,n)==202);CHECK(!navigations&&sleep_style==0&&atomic_load(&lock_state)==1);
    CHECK(push_lock(second,n)==409&&atomic_load(&lock_state)==1);same_file(LOCK_FILE,first,n);
    CHECK(!read_file(CANVAS_FILE,&n));n=u32(first+2);
    background=false;CHECK(!ps_desktop_tick()&&atomic_load(&lock_state)==1);background=true;
    CHECK(!ps_desktop_tick()&&!navigations&&atomic_load(&lock_state)==2&&ps_lock_image_available());
    CHECK(ps_desktop_handle("GET","/api/manage/pico/status",NULL,NULL,0,out,sizeof(out))==200);
    CHECK(strstr(out,"\"image_lock\":{\"supported\":true,\"available\":true")&&strstr(out,"\"state\":\"saved\""));
    CHECK(configure("{\"auto_sleep_minutes\":3,\"double_tap_wake\":false,\"lock_style\":4}")==202);CHECK(sleep_style==0);
    CHECK(ps_desktop_tick()&&sleep_style==4&&atomic_load(&power_state)==2);
    CHECK(configure("{\"auto_sleep_minutes\":5,\"double_tap_wake\":true}")==202);CHECK(ps_desktop_tick()&&sleep_style==4);
    const char *bad[]={"null","true","-1","6","1.5","\"4\""};
    for(unsigned i=0;i<sizeof(bad)/sizeof(*bad);i++){char body[128];snprintf(body,sizeof(body),"{\"auto_sleep_minutes\":5,\"double_tap_wake\":true,\"lock_style\":%s}",bad[i]);CHECK(configure(body)==422);}
    style_fail=true;unsigned previous=atomic_load(&power_revision);CHECK(configure("{\"auto_sleep_minutes\":5,\"double_tap_wake\":true,\"lock_style\":1}")==202);CHECK(ps_desktop_tick()&&sleep_style==4&&atomic_load(&power_state)==3&&atomic_load(&power_revision)==previous);style_fail=false;
    uint8_t *copy=NULL;size_t bytes=0;CHECK(image_extension("/api/manage/pico/lock-image",&copy,&bytes)==200&&bytes==n&&!memcmp(copy,first,n));free(copy);
    CHECK(image_extension("/unknown",&copy,&bytes)==404&&!copy&&!bytes);
    atomic_store(&receiving,true);CHECK(image_extension("/api/manage/pico/lock-image",&copy,&bytes)==409);CHECK(!ps_lock_image_prepare());atomic_store(&receiving,false);
    for(unsigned i=0;i<3;i++){present=i!=0;mounted=i!=1;exported=i==2;CHECK(push_lock(second,n)==409);same_file(LOCK_FILE,first,n);}present=mounted=true;exported=false;
    for(int mode=FAIL_OPEN;mode<=FAIL_CLOSE;mode++){failure=mode;CHECK(push_lock(second,n)==507);failure=FAIL_NONE;same_file(LOCK_FILE,first,n);}
    allocation_fail=true;CHECK(push_lock(second,n)==503);allocation_fail=false;
    CHECK(push_lock(second,n)==202);CHECK(!ps_desktop_tick()&&!navigations&&sleep_style==4);same_file(LOCK_FILE,second,n);same_file(LOCK_FILE ".bak",first,n);
    // 重启仅恢复TF图片，不进入推送画布或更改锁屏样式。/ Reboot restores image availability, never opens canvas or changes style.
    reset_memory();ps_desktop_init();CHECK(ps_lock_image_available()&&!lock_image&&!navigations);CHECK(ps_lock_image_prepare());CHECK(lock_image&&!memcmp(lock_image,second,n));
    uint8_t *frame=malloc(684u*1216/2);CHECK(frame);for(rotation=0;rotation<4;rotation++){unsigned before=io_count;ps_lock_image_render(frame);CHECK(io_count==before);}rotation=0;free(frame);ps_lock_image_release();CHECK(!lock_image&&ps_lock_image_available());
    char path[1400];FILE *f=fopen(mapped(LOCK_FILE,path),"wb");CHECK(f&&fwrite("broken",1,6,f)==6&&!fclose(f));CHECK(ps_lock_image_prepare()&&!memcmp(lock_image,first,n));
    CHECK(push_lock(second,n)==202);CHECK(!ps_desktop_tick());same_file(LOCK_FILE ".bak",first,n);
    reset_memory();erase_files();CHECK(!ps_lock_image_prepare()&&!ps_lock_image_available());free(first);free(second);
}
static void gray_lock_storage_checks(void){
    reset_memory();erase_files();ps_desktop_init();
    size_t n;uint8_t *first=gray_bitmap(684,1216,false,false,&n),*second=gray_bitmap(684,1216,true,true,&n);
    CHECK(push_lock(first,n)==202);CHECK(!ps_desktop_tick());
    for(int mode=FAIL_OPEN;mode<=FAIL_NEW_RENAME;mode++){
        failure=mode;CHECK(push_lock(second,n)==507&&!atomic_load(&lock_pending));failure=FAIL_NONE;same_file(LOCK_FILE,first,n);
    }
    allocation_fail=true;CHECK(push_lock(second,n)==503);allocation_fail=false;same_file(LOCK_FILE,first,n);
    for(unsigned which=0;which<3;which++){present=which!=0;mounted=which!=1;exported=which==2;CHECK(push_lock(second,n)==409);same_file(LOCK_FILE,first,n);}present=mounted=true;exported=false;
    CHECK(push_lock(second,n)==202);CHECK(push_lock(first,n)==409);CHECK(!ps_desktop_tick());same_file(LOCK_FILE,second,n);same_file(LOCK_FILE ".bak",first,n);
    reset_memory();char path[1400];FILE*f=fopen(mapped(LOCK_FILE,path),"wb");CHECK(f&&fwrite("broken",1,6,f)==6&&!fclose(f));
    CHECK(ps_lock_image_prepare()&&u16(lock_image+28)==4&&!memcmp(lock_image,first,n));
    failure=FAIL_ROLLBACK;CHECK(push_lock(second,n)==507&&!atomic_load(&lock_pending));failure=FAIL_NONE;same_file(LOCK_FILE ".bak",first,n);
    reset_memory();CHECK(ps_lock_image_prepare()&&!memcmp(lock_image,first,n));
    CHECK(push_lock(second,n)==202);CHECK(!ps_desktop_tick());same_file(LOCK_FILE ".bak",first,n);
    // 旧1位文件与4位主档互换仍保持原始字节，且1位上限不随总体上限扩大。
    // One-bit and four-bit primaries interchange byte-exactly; the one-bit limit stays independent.
    size_t mono_n;uint8_t *mono=bitmap(684,1216,false,false,0,&mono_n);CHECK(push_lock(mono,mono_n)==202);CHECK(!ps_desktop_tick());same_file(LOCK_FILE,mono,mono_n);same_file(LOCK_FILE ".bak",second,n);
    free(mono);mono=bitmap(684,1216,false,false,110001-mono_n,&mono_n);CHECK(mono_n==110001&&!valid(mono,mono_n)&&push_lock(mono,mono_n)==422);CHECK(push_lock(first,420001)==413);free(mono);
    reset_memory();erase_files();free(first);free(second);
}
static void config_checks(void){
    reset_memory();ps_desktop_init();char out[2048];CHECK(request("GET","/api/manage/pico/status",NULL,NULL,0,out)==200);CHECK(strstr(out,"\"version\":\"desktop-host-test\"")&&strstr(out,"\"auto_sleep_minutes\":5")&&strstr(out,"\"home_link_mode\":\"pga\""));
    test_home_package=1;CHECK(request("GET","/api/manage/pico/status",NULL,NULL,0,out)==200);CHECK(strstr(out,"\"home_link_mode\":\"pga\""));test_home_package=0;
    const char *bad[]={"{}","[]","null","{\"auto_sleep_minutes\":2,\"double_tap_wake\":true}","{\"auto_sleep_minutes\":1.5,\"double_tap_wake\":true}","{\"auto_sleep_minutes\":-1,\"double_tap_wake\":false}","{\"auto_sleep_minutes\":5,\"double_tap_wake\":1}","{\"auto_sleep_minutes\":true,\"double_tap_wake\":false}","{\"auto_sleep_minutes\":5,\"double_tap_wake\":true,\"extra\":1}","{\"auto_sleep_minutes\":5,\"double_tap_wake\":true}garbage"};
    for(unsigned i=0;i<sizeof(bad)/sizeof(*bad);i++)CHECK(configure(bad[i])==422);CHECK(!power_calls&&!atomic_load(&power_request));
    for(unsigned i=0;i<6;i++){
        const unsigned options[]={0,1,3,5,10,30};char json[96];snprintf(json,sizeof(json),"{\"auto_sleep_minutes\":%u,\"double_tap_wake\":false}",options[i]);unsigned calls=power_calls,rev=atomic_load(&power_revision);
        CHECK(configure(json)==202);CHECK(atomic_load(&power_state)==1&&power_calls==calls&&atomic_load(&power_revision)==rev);CHECK(configure(json)==409);
        background=false;CHECK(!ps_desktop_tick()&&power_calls==calls);background=true;power_probe=true;CHECK(ps_desktop_tick());power_probe=false;CHECK(power_calls==calls+1&&power_minutes==options[i]&&!power_tap&&atomic_load(&power_state)==2&&atomic_load(&power_revision)==rev+1);CHECK(!ps_desktop_tick()&&power_calls==calls+1);
    }
    power_fail=true;CHECK(configure("{\"auto_sleep_minutes\":3,\"double_tap_wake\":true}")==202);unsigned rev=atomic_load(&power_revision);CHECK(ps_desktop_tick()&&atomic_load(&power_state)==3&&atomic_load(&power_revision)==rev&&power_minutes==30&&!power_tap);power_fail=false;
    const char *bad_covers[]={"-1","5","1.5","\"3\"","null","true"};
    for(unsigned i=0;i<sizeof(bad_covers)/sizeof(*bad_covers);i++){char json[128];snprintf(json,sizeof(json),"{\"auto_sleep_minutes\":5,\"double_tap_wake\":true,\"cover_layout\":%s}",bad_covers[i]);CHECK(configure(json)==422);}
    for(int layout=0;layout<5;layout++){
        char json[128];snprintf(json,sizeof(json),"{\"auto_sleep_minutes\":5,\"double_tap_wake\":true,\"lock_style\":2,\"cover_layout\":%d}",layout);
        CHECK(configure(json)==202);CHECK(ps_desktop_tick()&&sleep_style==2&&test_cover_layout==layout);
        CHECK(request("GET","/api/manage/pico/status",NULL,NULL,0,out)==200);char expected[32];snprintf(expected,sizeof(expected),"\"cover_layout\":%d",layout);CHECK(strstr(out,expected));
    }
    CHECK(configure("{\"auto_sleep_minutes\":3,\"double_tap_wake\":true}")==202);CHECK(ps_desktop_tick()&&test_cover_layout==4);
    rev=atomic_load(&power_revision);cover_fail=true;
    CHECK(configure("{\"auto_sleep_minutes\":5,\"double_tap_wake\":true,\"cover_layout\":3}")==202);
    CHECK(ps_desktop_tick()&&test_cover_layout==4&&atomic_load(&power_state)==3&&atomic_load(&power_revision)==rev);cover_fail=false;
    power_fail=true;CHECK(configure("{\"auto_sleep_minutes\":5,\"double_tap_wake\":true,\"cover_layout\":3}")==202);
    CHECK(ps_desktop_tick()&&test_cover_layout==4&&atomic_load(&power_state)==3&&atomic_load(&power_revision)==rev);power_fail=false;
    size_t n;uint8_t *p=bitmap(684,1216,false,false,0,&n);CHECK(push(p,n)==202);CHECK(configure("{\"auto_sleep_minutes\":1,\"double_tap_wake\":true}")==202);CHECK(ps_desktop_tick()&&power_minutes==1&&power_tap&&image&&navigations==0);CHECK(atomic_load(&state)==1);free(p);
    reset_memory();erase_files();
}
static void write_usb_job(const uint8_t *p,size_t n,unsigned damage){
    uint8_t header[64]={0};memcpy(header,"PICOBRD1",8);put32(header+8,1);put32(header+12,64);put32(header+16,(uint32_t)n);put32(header+20,usb_crc(p,n));memset(header+24,'a',32);
    if(damage==1)header[20]^=1;if(damage==2)header[24]='z';if(damage==3)header[56]=1;
    char path[1400];FILE*f=fopen(mapped(USB_BOARD_FILE,path),"wb");CHECK(f);CHECK(fwrite(header,1,64,f)==64);CHECK(fwrite(p,1,n-(damage==4),f)==n-(damage==4));CHECK(!fclose(f));
}
static void usb_checks(void){
    reset_memory();erase_files();ps_desktop_init();size_t n;uint8_t*p=gray_bitmap(684,1216,false,false,&n);char out[2048];
    CHECK(push(p,n)==202&&ps_desktop_tick());ps_desktop_presented(true);
    write_usb_job(p,n,0);ps_desktop_usb_reclaimed();exported=true;CHECK(!ps_desktop_tick()&&usb_scan);exported=false;
    background=false;unsigned io=io_count;CHECK(!ps_desktop_tick()&&io==io_count);background=true;
    CHECK(ps_desktop_tick()&&usb_pending&&usb_state==1);ps_desktop_usb_status(out,sizeof(out));CHECK(strstr(out,"\"pending\"")&&strstr(out,"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"));
    CHECK(!ps_desktop_tick());ps_desktop_presented(true);ps_desktop_usb_status(out,sizeof(out));CHECK(strstr(out,"\"displayed\"")&&!usb_pending);same_file(CANVAS_FILE,p,n);
    io=io_count;for(unsigned i=0;i<10;i++)CHECK(!ps_desktop_tick());CHECK(io_count==io);
    for(unsigned damage=1;damage<=4;damage++){write_usb_job(p,n,damage);ps_desktop_usb_reclaimed();CHECK(!ps_desktop_tick()&&usb_state==3&&!atomic_load(&pending));same_file(CANVAS_FILE,p,n);}
    write_usb_job(p,n,0);ps_desktop_usb_reclaimed();failure=FAIL_WRITE;CHECK(!ps_desktop_tick()&&usb_state==3);failure=FAIL_NONE;same_file(CANVAS_FILE,p,n);
    write_usb_job(p,n,0);ps_desktop_usb_reclaimed();CHECK(ps_desktop_tick());ps_desktop_presented(false);CHECK(usb_state==3);free(p);reset_memory();erase_files();
}
static void power_checks(void){
    char out[1024],power[144];test_pmu=(pmu_snapshot_t){0};power_sample();
    ps_desktop_power_status(power,sizeof(power));CHECK(strstr(power,"\"known\":false")&&strstr(power,"\"battery_percent\":-1"));
    for(unsigned charge=0;charge<=5;charge++){
        test_pmu=(pmu_snapshot_t){.present=true,.status_ok=true,.flags=PMU_STATUS_BATTERY_VALID,.soc_permille=865,.charge_state=charge};
        unsigned before_io=io_count;power_sample();unsigned published=atomic_load(&live_power);ps_desktop_power_status(power,sizeof(power));
        cJSON *j=cJSON_Parse(power);CHECK(j);CHECK(cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(j,"known"))==(charge>=1&&charge<=3));
        CHECK(cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(j,"charging"))==(charge==2));
        CHECK(cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(j,"external_power"))==(charge==2||charge==3));
        CHECK(cJSON_GetObjectItemCaseSensitive(j,"battery_percent")->valueint==87);cJSON_Delete(j);
        for(int i=0;i<8;i++){power_sample();ps_desktop_power_status(power,sizeof(power));CHECK(atomic_load(&live_power)==published);}
        CHECK(io_count==before_io);ps_desktop_usb_status(out,sizeof(out));j=cJSON_Parse(out);CHECK(j&&cJSON_GetObjectItemCaseSensitive(j,"power"));cJSON_Delete(j);
        CHECK(ps_desktop_handle("GET","/api/manage/pico/status",NULL,NULL,0,out,sizeof(out))==200);j=cJSON_Parse(out);CHECK(j&&cJSON_GetObjectItemCaseSensitive(j,"power"));cJSON_Delete(j);
    }
    test_pmu.status_ok=false;power_sample();ps_desktop_power_status(power,sizeof(power));CHECK(strstr(power,"\"known\":false")&&strstr(power,"\"external_power\":false"));
    test_pmu.status_ok=true;test_pmu.last_err=-1;power_sample();ps_desktop_power_status(power,sizeof(power));CHECK(strstr(power,"\"known\":false"));
    test_pmu.last_err=0;test_pmu.charge_state=2;test_pmu.soc_permille=1001;power_sample();ps_desktop_power_status(power,sizeof(power));CHECK(strstr(power,"\"known\":true")&&strstr(power,"\"battery_percent\":-1"));
    test_pmu.soc_permille=800;test_pmu.flags=0;power_sample();ps_desktop_power_status(power,sizeof(power));CHECK(strstr(power,"\"battery_percent\":-1"));
}
static void background_board_checks(void){
    reset_memory();erase_files();size_t n;uint8_t*p=gray_bitmap(684,1216,false,false,&n);char out[2048];
    for(int route=1;route<=2;route++){
        current_route=route;unsigned old=ps_desktop_revision();CHECK(push(p,n)==202);
        CHECK(ps_desktop_revision()==old&&!navigations);
        background=false;CHECK(!ps_desktop_tick()&&ps_desktop_revision()==old);
        background=true;CHECK(!ps_desktop_tick()&&ps_desktop_revision()==old+1&&!navigations);
        CHECK(atomic_load(&state)==4);request("GET","/api/manage/pico/status",NULL,NULL,0,out);
        CHECK(strstr(out,"\"canvas\":{\"supported\":true")&&strstr(out,"\"state\":\"saved\""));
        CHECK(ps_desktop_prepare()&&current_route==route);
    }
    current_route=PS_CANVAS;ps_desktop_presented(true);CHECK(atomic_load(&state)==2);
    reset_memory();CHECK(ps_desktop_prepare()&&image&&current_route==PS_CANVAS&&!navigations);
    free(p);reset_memory();erase_files();
}

static const char *direct_id="0123456789abcdef0123456789abcdef";
static void direct_control(const char *command,const char *phase,bool accepted,size_t expected_binary){
    char out[2048];size_t binary=0;
    CHECK(ps_desktop_usb_direct_command(command,out,sizeof(out),&binary));CHECK(binary==expected_binary);
    if(expected_binary){CHECK(!*out);return;}
    cJSON *j=cJSON_Parse(out);CHECK(j);CHECK(cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(j,"accepted"))==accepted);
    CHECK(!strcmp(cJSON_GetObjectItemCaseSensitive(j,"phase")->valuestring,phase));cJSON_Delete(j);
}
static void direct_begin(const uint8_t *p,size_t n,bool show,uint32_t crc_delta){
    char command[128];snprintf(command,sizeof(command),"RPICO BOARD %s %s %zu %08x %u",paired(p)?"BEGINPAIR":"BEGIN",direct_id,n,usb_crc(p,n)^crc_delta,show?1u:0u);
    direct_control(command,"ready",true,0);CHECK(direct_image&&atomic_load(&receiving)&&!atomic_load(&pending));
}
static void direct_send(const uint8_t *p,size_t n){
    for(size_t offset=0;offset<n;){
        size_t count=n-offset>1024?1024:n-offset;char command[128],out[2048];
        snprintf(command,sizeof(command),"RPICO BOARD CHUNK %s %zu %zu",direct_id,offset,count);
        direct_control(command,"chunk",true,count);
        unsigned before=io_count;ps_desktop_usb_direct_payload(p+offset,count,out,sizeof(out));
        CHECK(io_count==before&&direct_used==offset+count&&strstr(out,"\"accepted\":true"));
        offset+=count;now_ms+=20;
    }
}
static void direct_checks(void){
    reset_memory();erase_files();size_t n;uint8_t *p=gray_bitmap(684,1216,false,false,&n);char command[128],out[2048];
    current_route=1;unsigned before=io_count;
    direct_begin(p,n,false,0);CHECK(io_count==before); /* card readiness is cached; staging never touches a file. */
    CHECK(push(p,n)==409&&!navigations&&current_route==1);
    direct_send(p,n);CHECK(current_route==1&&!navigations&&!exported&&mounted);
    snprintf(command,sizeof(command),"RPICO BOARD COMMIT %s",direct_id);direct_control(command,"committed",true,0);
    CHECK(!direct_image&&!atomic_load(&receiving));same_file(CANVAS_FILE,p,n);
    CHECK(!ps_desktop_tick()&&usb_state==4&&atomic_load(&state)==4&&current_route==1&&!navigations);
    ps_desktop_usb_status(out,sizeof(out));CHECK(strstr(out,"\"direct_protocol\":1")&&strstr(out,"\"chunk_bytes\":1024")&&strstr(out,"\"state\":\"saved\""));
    for(unsigned condition=0;condition<6;condition++){
        direct_begin(p,n,false,condition==2?1u:0u);
        if(condition==0){snprintf(command,sizeof(command),"RPICO BOARD ABORT %s",direct_id);direct_control(command,"aborted",true,0);}
        if(condition==1){now_ms+=15000;ps_desktop_usb_direct_tick();CHECK(!direct_image&&!atomic_load(&receiving));}
        if(condition==2){direct_send(p,n);snprintf(command,sizeof(command),"RPICO BOARD COMMIT %s",direct_id);direct_control(command,"committed",false,0);}
        if(condition==3){direct_send(p,n);failure=FAIL_NEW_RENAME;snprintf(command,sizeof(command),"RPICO BOARD COMMIT %s",direct_id);direct_control(command,"committed",false,0);failure=FAIL_NONE;}
        if(condition==4){snprintf(command,sizeof(command),"RPICO BOARD CHUNK %s 1 1024",direct_id);direct_control(command,"chunk",false,0);CHECK(!direct_used);snprintf(command,sizeof(command),"RPICO BOARD COMMIT %s",direct_id);direct_control(command,"committed",false,0);}
        if(condition==5){exported=true;ps_desktop_usb_direct_tick();CHECK(!direct_image&&!atomic_load(&receiving));exported=false;}
        same_file(CANVAS_FILE,p,n);CHECK(!navigations&&current_route==1);
    }
    allocation_fail=true;snprintf(command,sizeof(command),"RPICO BOARD BEGIN %s %zu %08x 0",direct_id,n,usb_crc(p,n));direct_control(command,"ready",false,0);allocation_fail=false;
    direct_begin(p,n,true,0);direct_send(p,n);snprintf(command,sizeof(command),"RPICO BOARD COMMIT %s",direct_id);direct_control(command,"committed",true,0);
    background=false;CHECK(!ps_desktop_tick()&&!navigations&&atomic_load(&pending));background=true;
    CHECK(ps_desktop_tick()&&navigations==1&&current_route==PS_CANVAS&&usb_state==1);ps_desktop_presented(true);CHECK(usb_state==2);
    current_route=1;navigations=0;locked=true;CHECK(request("POST","/api/manage/pico/canvas/show","image/bmp",p,n,out)==202);CHECK(!ps_desktop_tick()&&current_route==1&&!navigations&&atomic_load(&state)==4);locked=false;
    CHECK(request("POST","/api/manage/pico/canvas/show","image/bmp",p,n,out)==202);CHECK(ps_desktop_tick()&&current_route==PS_CANVAS&&navigations==1);ps_desktop_presented(true);
    CHECK(request("GET","/api/manage/pico/status",NULL,NULL,0,out)==200);CHECK(strstr(out,"\"display_intent\":true"));
    reset_memory();erase_files();free(p);
}

static uint8_t *pair_bitmap(size_t *bytes){
    size_t a,b;uint8_t *p=gray_bitmap(684,1216,false,false,&a),*l=gray_bitmap(1216,684,true,true,&b);
    *bytes=32+a+b;uint8_t *result=calloc(1,*bytes);CHECK(result);memcpy(result,"PICOPAIR",8);
    put32(result+8,1);put32(result+12,32);put32(result+16,(uint32_t)a);put32(result+20,usb_crc(p,a));put32(result+24,(uint32_t)b);put32(result+28,usb_crc(l,b));
    memcpy(result+32,p,a);memcpy(result+32+a,l,b);free(p);free(l);return result;
}
static int push_pair(const void *p,size_t n,bool show){char out[2048];return request("POST",show?"/api/manage/pico/canvas/pair/show":"/api/manage/pico/canvas/pair",PAIR_TYPE,p,n,out);}
static void pair_checks(void){
    reset_memory();erase_files();size_t n;uint8_t *p=pair_bitmap(&n);char out[2048],command[128];CHECK(n==834444&&valid_pair(p,n));
    CHECK(!valid_pair(NULL,n)&&!valid_pair(p,31)&&!valid_pair(p,n-1)&&!valid_pair(p,PAIR_LIMIT+1));
    CHECK(push_pair(p,PAIR_LIMIT+1,false)==413);CHECK(request("POST","/api/manage/pico/canvas/pair","image/bmp",p,n,out)==415);
    unsigned offsets[]={0,8,12,16,20,24,28,32,32+418422};
    for(unsigned i=0;i<sizeof(offsets)/sizeof(*offsets);i++){p[offsets[i]]^=1;CHECK(!valid_pair(p,n)&&push_pair(p,n,false)==422);p[offsets[i]]^=1;}
    current_route=1;CHECK(push_pair(p,n,false)==202);CHECK(!ps_desktop_tick()&&!navigations&&current_route==1&&atomic_load(&state)==4);same_file(CANVAS_FILE,p,n);
    unsigned rev=atomic_load(&revision);CHECK(rev==1&&paired(image));
    for(int mode=FAIL_OPEN;mode<=FAIL_NEW_RENAME;mode++){failure=mode;CHECK(push_pair(p,n,false)==507);failure=FAIL_NONE;same_file(CANVAS_FILE,p,n);CHECK(atomic_load(&revision)==rev&&!atomic_load(&pending));}
    allocation_fail=true;CHECK(push_pair(p,n,false)==503);allocation_fail=false;exported=true;CHECK(push_pair(p,n,false)==409);exported=false;
    direct_begin(p,n,false,0);direct_send(p,n);CHECK(!exported&&mounted&&current_route==1&&!navigations);snprintf(command,sizeof(command),"RPICO BOARD COMMIT %s",direct_id);direct_control(command,"committed",true,0);
    CHECK(!ps_desktop_tick()&&usb_state==4&&atomic_load(&revision)==rev+1);ps_desktop_usb_status(out,sizeof(out));cJSON *status=cJSON_Parse(out);CHECK(status&&cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(status,"dual_orientation"))&&cJSON_GetObjectItemCaseSensitive(status,"pair_max_bytes")->valueint==PAIR_LIMIT);cJSON_Delete(status);
    same_file(CANVAS_FILE,p,n);rev=atomic_load(&revision);uint8_t *frame=malloc(684u*1216u/2u+32);CHECK(frame);
    current_route=PS_CANVAS;
    for(rotation=0;rotation<4;rotation++){
        memset(frame,0xa5,684u*1216u/2u+32);unsigned before=io_count;ps_desktop_render(frame);CHECK(before==io_count&&atomic_load(&revision)==rev);unsigned sw=rotation%2?1216:684,sh=rotation%2?684:1216;
        for(unsigned y=0;y<sh;y++)for(unsigned x=0;x<sw;x++){size_t at=pixel_index((int)x,(int)y);CHECK(((frame[at/2]>>(at&1?4:0))&15)==gray_pattern(x,y,sw,sh));}
        for(unsigned k=0;k<32;k++)CHECK(frame[684u*1216u/2u+k]==0xa5);
        ps_desktop_usb_status(out,sizeof(out));CHECK(strstr(out,rotation%2?"\"orientation\":\"landscape\"":"\"orientation\":\"portrait\""));
    }
    rotation=0;free(frame);ps_desktop_presented(true);CHECK(usb_state==2);
    current_route=1;navigations=0;direct_begin(p,n,true,1);direct_send(p,n);direct_control(command,"committed",false,0);same_file(CANVAS_FILE,p,n);CHECK(atomic_load(&revision)==rev&&!navigations);
    p[n-1]^=1;direct_begin(p,n,true,0);direct_send(p,n);direct_control(command,"committed",false,0);p[n-1]^=1;same_file(CANVAS_FILE,p,n);CHECK(atomic_load(&revision)==rev);
    direct_begin(p,n,true,0);direct_send(p,n);failure=FAIL_NEW_RENAME;direct_control(command,"committed",false,0);failure=FAIL_NONE;same_file(CANVAS_FILE,p,n);CHECK(atomic_load(&revision)==rev);
    locked=true;CHECK(push_pair(p,n,true)==202);CHECK(!ps_desktop_tick()&&!navigations&&current_route==1&&atomic_load(&state)==4);locked=false;
    CHECK(push_pair(p,n,true)==202&&ps_desktop_tick()&&navigations==1&&current_route==PS_CANVAS);ps_desktop_presented(true);
    reset_memory();CHECK(ps_desktop_prepare()&&paired(image));same_file(CANVAS_FILE,p,n);
    size_t single_n;uint8_t *single=gray_bitmap(684,1216,false,false,&single_n);CHECK(push(single,single_n)==202&&ps_desktop_tick());ps_desktop_presented(true);CHECK(!paired(image));same_file(CANVAS_FILE,single,single_n);same_file(CANVAS_FILE ".bak",p,n);
    reset_memory();CHECK(ps_desktop_prepare()&&!paired(image));free(single);free(p);reset_memory();erase_files();
}

// Channels are isolated in storage, loaded pixels, task receipts and presentation intent.
static void app_channel_checks(void){
    reset_memory();erase_files();size_t n;uint8_t *board=pair_bitmap(&n),*app=malloc(n);CHECK(app);memcpy(app,board,n);
    app[32+118]^=0xf0;put32(app+20,usb_crc(app+32,u32(app+16)));CHECK(valid_pair(app,n));
    char out[2048],command[160];
    CHECK(push_pair(board,n,false)==202&&ps_desktop_tick());ps_desktop_presented(true);
    CHECK(image&&!image_app&&atomic_load(&state)==2);same_file(CANVAS_FILE,board,n);
    CHECK(request("POST","/api/manage/pico/apps/unknown/pair",PAIR_TYPE,app,n,out)==404);
    CHECK(request("POST","/api/manage/pico/apps/cloudflare/pair","image/bmp",app,n,out)==415);
    CHECK(request("POST","/api/manage/pico/apps/cloudflare/pair",PAIR_TYPE,app,PAIR_LIMIT+1,out)==413);
    CHECK(request("POST","/api/manage/pico/apps/cloudflare/pair",PAIR_TYPE,app,n,out)==202);
    CHECK(strstr(out,"\"channel\":\"cloudflare\"")&&atomic_load(&cloudflare_revision)==1);
    CHECK(!ps_desktop_tick()&&current_route==PS_CANVAS&&!navigations);
    CHECK(image&&!image_app&&!memcmp(image,board,n)&&atomic_load(&state)==2&&atomic_load(&cloudflare_state)==4);
    ps_desktop_presented(true);CHECK(atomic_load(&cloudflare_state)==4);same_file(CLOUDFLARE_FILE,app,n);same_file(CANVAS_FILE,board,n);
    CHECK(ps_desktop_handle("GET","/api/manage/pico/status",NULL,NULL,0,out,sizeof(out))==200);
    cJSON *j=cJSON_Parse(out);CHECK(j);cJSON *canvas=cJSON_GetObjectItemCaseSensitive(j,"canvas");
    cJSON *receipt=cJSON_GetObjectItemCaseSensitive(cJSON_GetObjectItemCaseSensitive(canvas,"app_receipts"),"cloudflare");
    CHECK(cJSON_GetObjectItemCaseSensitive(canvas,"revision")->valueint==1&&cJSON_GetObjectItemCaseSensitive(receipt,"revision")->valueint==1);
    CHECK(!strcmp(cJSON_GetObjectItemCaseSensitive(receipt,"state")->valuestring,"saved"));cJSON_Delete(j);
    CHECK(ps_desktop_prepare_app(true)&&image_app&&!memcmp(image,app,n)&&ps_desktop_app_available(true)&&!ps_desktop_app_available(false));
    current_route=PS_CLOUDFLARE;ps_desktop_presented(true);CHECK(atomic_load(&cloudflare_state)==2);
    CHECK(push_pair(board,n,false)==202&&!ps_desktop_tick()&&current_route==PS_CLOUDFLARE);
    CHECK(image_app&&!memcmp(image,app,n)&&atomic_load(&state)==4);ps_desktop_presented(true);CHECK(atomic_load(&state)==4);
    CHECK(ps_desktop_prepare()&&!image_app&&!memcmp(image,board,n));current_route=PS_CANVAS;ps_desktop_presented(true);CHECK(atomic_load(&state)==2);
    for(int mode=FAIL_OPEN;mode<=FAIL_NEW_RENAME;mode++){
        failure=mode;CHECK(request("POST","/api/manage/pico/apps/cloudflare/pair",PAIR_TYPE,app,n,out)==507);failure=FAIL_NONE;
        same_file(CLOUDFLARE_FILE,app,n);same_file(CANVAS_FILE,board,n);CHECK(!atomic_load(&pending)&&!image_app);
    }
    allocation_fail=true;CHECK(request("POST","/api/manage/pico/apps/cloudflare/pair",PAIR_TYPE,app,n,out)==503);allocation_fail=false;
    exported=true;CHECK(request("POST","/api/manage/pico/apps/cloudflare/pair",PAIR_TYPE,app,n,out)==409);exported=false;
    locked=true;CHECK(request("POST","/api/manage/pico/apps/cloudflare/pair/show",PAIR_TYPE,app,n,out)==202&&!ps_desktop_tick());
    CHECK(current_route==PS_CANVAS&&!navigations&&!image_app&&atomic_load(&cloudflare_state)==4);locked=false;
    snprintf(command,sizeof(command),"RPICO BOARD BEGINAPP unknown %s %zu %08x 0",direct_id,n,usb_crc(app,n));direct_control(command,"rejected",false,0);
    snprintf(command,sizeof(command),"RPICO BOARD BEGINAPP cloudflare %s %zu %08x 1",direct_id,n,usb_crc(app,n));direct_control(command,"ready",true,0);
    CHECK(direct_app&&direct_pair&&!exported&&mounted);direct_send(app,n);
    snprintf(command,sizeof(command),"RPICO BOARD COMMIT %s",direct_id);direct_control(command,"committed",true,0);
    CHECK(ps_desktop_tick()&&current_route==PS_CLOUDFLARE&&navigations==1&&image_app&&!memcmp(image,app,n));
    CHECK(usb_app&&usb_state==1&&usb_revision==atomic_load(&cloudflare_revision));
    current_route=PS_CANVAS;ps_desktop_presented(true);CHECK(usb_state==1);current_route=PS_CLOUDFLARE;ps_desktop_presented(true);CHECK(usb_state==2);
    ps_desktop_usb_status(out,512);j=cJSON_Parse(out);CHECK(j);CHECK(!strcmp(cJSON_GetObjectItemCaseSensitive(j,"channel")->valuestring,"cloudflare"));cJSON_Delete(j);
    uint8_t *fb=malloc(684u*1216/2);CHECK(fb);unsigned before=io_count;
    for(rotation=0;rotation<4;rotation++){ps_desktop_render_app(fb,true);CHECK(io_count==before);ps_desktop_render_app(fb,false);for(unsigned k=0;k<684u*1216/2;k++)CHECK(fb[k]==255);}
    rotation=0;free(fb);same_file(CLOUDFLARE_FILE,app,n);same_file(CANVAS_FILE,board,n);
    // Charging canvas lock is visible independently of the underlying Reader/Cloudflare route.
    for(unsigned origin=0;origin<2;origin++){
        current_route=origin?PS_CLOUDFLARE:1;CHECK(ps_desktop_prepare());locked=lock_board=true;
        unsigned old=image_revision;
        CHECK(request("POST","/api/manage/pico/apps/cloudflare/pair/show",PAIR_TYPE,app,n,out)==202);
        CHECK(!ps_desktop_tick()&&!image_app&&image_revision==old&&!memcmp(image,board,n)&&atomic_load(&cloudflare_state)==4);
        ps_desktop_presented(true);CHECK(atomic_load(&cloudflare_state)==4);
        CHECK(push_pair(board,n,false)==202&&ps_desktop_tick()&&!image_app&&image_revision==atomic_load(&revision));
        ps_desktop_presented(true);CHECK(atomic_load(&state)==2&&atomic_load(&cloudflare_state)==4);
        locked=lock_board=false;
        if(origin){CHECK(ps_desktop_tick()&&image_app&&!memcmp(image,app,n));ps_desktop_presented(true);CHECK(atomic_load(&cloudflare_state)==2);}
    }
    // A retained-page/clock lock protects cached app pixels; deferred content loads after wake.
    current_route=PS_CLOUDFLARE;CHECK(ps_desktop_prepare_app(true));locked=true;
    CHECK(push_pair(board,n,false)==202&&!ps_desktop_tick()&&image_app);
    CHECK(request("POST","/api/manage/pico/apps/cloudflare/pair",PAIR_TYPE,app,n,out)==202&&!ps_desktop_tick()&&image_app);
    CHECK(image_revision!=atomic_load(&cloudflare_revision));locked=false;
    CHECK(ps_desktop_tick()&&image_app&&image_revision==atomic_load(&cloudflare_revision));ps_desktop_presented(true);
    // 无新推送时，充电画板锁屏唤醒仍须恢复原网站流量缓存。/ Restore the app cache after canvas lock even without a new push.
    current_route=PS_CLOUDFLARE;CHECK(atomic_load(&cloudflare_state)==2&&image_app);
    unsigned restored_revision=atomic_load(&cloudflare_revision);
    CHECK(ps_desktop_prepare()&&!image_app);locked=lock_board=true;
    CHECK(!ps_desktop_tick()&&!image_app&&atomic_load(&cloudflare_state)==2);
    ps_desktop_presented(true);CHECK(atomic_load(&cloudflare_state)==2);
    locked=lock_board=false;recovery_probe=true;CHECK(ps_desktop_tick()&&image_app&&image_revision==restored_revision);recovery_probe=false;
    CHECK(!memcmp(image,app,n)&&atomic_load(&cloudflare_state)==1);ps_desktop_presented(true);
    CHECK(atomic_load(&cloudflare_state)==2&&atomic_load(&cloudflare_revision)==restored_revision);
    CHECK(!ps_desktop_tick());
    reset_memory();CHECK(ps_desktop_prepare_app(true)&&image_app&&!memcmp(image,app,n));CHECK(ps_desktop_prepare()&&!image_app&&!memcmp(image,board,n));
    reset_memory();erase_files();CHECK(!ps_desktop_prepare_app(true)&&!ps_desktop_app_available(true));free(board);free(app);
}

// 数据模型保留传输事务、后台语义、频道隔离与掉电恢复。/ Semantic models retain transactions, background intent, channel isolation and restart recovery.
static void model_checks(void){
    reset_memory();erase_files();
    const char *model="{\"schema\":1,\"board_id\":\"test-board\",\"generated_at\":1700000000,\"columns\":1,\"dark\":false,\"modules\":[{\"id\":\"weather-1\",\"kind\":\"weather\",\"title\":\"天气\",\"subtitle\":\"上海\",\"metrics\":[{\"label\":\"温度\",\"value\":\"23\",\"unit\":\"°C\"}],\"rows\":[{\"label\":\"风速\",\"value\":\"3 m/s\"}],\"series\":[20,21,23,24]}]}";
    const size_t n=strlen(model);char out[2048],command[160];
    CHECK(valid_model(model,n)&&valid_canvas((const uint8_t*)model,n));
    CHECK(!valid_model(NULL,n)&&!valid_model(model,n+1));
    CHECK(request("POST","/api/manage/pico/canvas/model","image/bmp",model,n,out)==415);
    CHECK(request("POST","/api/manage/pico/canvas/model",MODEL_TYPE,model,MODEL_LIMIT+1,out)==413);
    CHECK(request("POST","/api/manage/pico/canvas/model",MODEL_TYPE,"{}",2,out)==422);
    current_route=1;
    CHECK(request("POST","/api/manage/pico/canvas/model",MODEL_TYPE,model,n,out)==202);
    CHECK(!ps_desktop_tick()&&current_route==1&&!navigations&&board_model&&atomic_load(&state)==4);
    same_file(CANVAS_FILE,(const uint8_t*)model,n);unsigned rev=atomic_load(&revision);
    for(int fault=FAIL_OPEN;fault<=FAIL_NEW_RENAME;fault++){
        failure=fault;CHECK(request("POST","/api/manage/pico/canvas/model",MODEL_TYPE,model,n,out)==507);failure=FAIL_NONE;
        same_file(CANVAS_FILE,(const uint8_t*)model,n);CHECK(atomic_load(&revision)==rev&&board_model);
    }
    fail_old_model_allocation=true;failure=FAIL_NEW_RENAME;
    CHECK(request("POST","/api/manage/pico/canvas/model",MODEL_TYPE,model,n,out)==507);
    fail_old_model_allocation=model_allocation_failed=false;failure=FAIL_NONE;
    same_file(CANVAS_FILE,(const uint8_t*)model,n);CHECK(atomic_load(&revision)==rev&&board_model);
    exported=true;CHECK(request("POST","/api/manage/pico/canvas/model",MODEL_TYPE,model,n,out)==409);exported=false;
    CHECK(request("POST","/api/manage/pico/canvas/model",MODEL_TYPE,"{\"schema\":99}",13,out)==422);
    CHECK(atomic_load(&revision)==rev&&board_model);
    reset_memory();CHECK(ps_desktop_prepare()&&board_model);same_file(CANVAS_FILE,(const uint8_t*)model,n);
    uint8_t *fb=malloc(684u*1216u/2u);CHECK(fb);unsigned before=io_count;
    for(rotation=0;rotation<4;rotation++)ps_desktop_render(fb);
    CHECK(model_renders==4&&io_count==before);rotation=0;free(fb);
    CHECK(ps_desktop_handle("GET","/api/manage/pico/status",NULL,NULL,0,out,sizeof(out))==200);
    cJSON *status=cJSON_Parse(out);CHECK(status);cJSON *canvas=cJSON_GetObjectItemCaseSensitive(status,"canvas");
    CHECK(cJSON_GetObjectItemCaseSensitive(canvas,"model_protocol")->valueint==1&&cJSON_GetObjectItemCaseSensitive(canvas,"model_max_bytes")->valueint==MODEL_LIMIT);cJSON_Delete(status);
    current_route=1;
    snprintf(command,sizeof(command),"RPICO BOARD BEGINMODEL canvas %s %zu %08x 0",direct_id,n,usb_crc((const uint8_t*)model,n));
    direct_control(command,"ready",true,0);CHECK(direct_model&&!direct_pair&&!direct_app&&!exported&&mounted);
    direct_send((const uint8_t*)model,n);snprintf(command,sizeof(command),"RPICO BOARD COMMIT %s",direct_id);direct_control(command,"committed",true,0);
    CHECK(!ps_desktop_tick()&&current_route==1&&!navigations&&usb_state==4);same_file(CANVAS_FILE,(const uint8_t*)model,n);
    rev=atomic_load(&revision);
    snprintf(command,sizeof(command),"RPICO BOARD BEGINMODEL canvas %s %zu %08x 1",direct_id,n,usb_crc((const uint8_t*)model,n)^1);
    direct_control(command,"ready",true,0);direct_send((const uint8_t*)model,n);snprintf(command,sizeof(command),"RPICO BOARD COMMIT %s",direct_id);direct_control(command,"committed",false,0);
    CHECK(atomic_load(&revision)==rev&&current_route==1&&!navigations);
    locked=true;CHECK(request("POST","/api/manage/pico/canvas/model/show",MODEL_TYPE,model,n,out)==202&&!ps_desktop_tick());
    CHECK(current_route==1&&!navigations&&atomic_load(&state)==4);locked=false;
    snprintf(command,sizeof(command),"RPICO BOARD BEGINMODEL cloudflare %s %zu %08x 1",direct_id,n,usb_crc((const uint8_t*)model,n));
    direct_control(command,"ready",true,0);CHECK(direct_model&&direct_app&&!direct_pair);direct_send((const uint8_t*)model,n);
    snprintf(command,sizeof(command),"RPICO BOARD COMMIT %s",direct_id);direct_control(command,"committed",true,0);
    CHECK(ps_desktop_tick()&&current_route==PS_CLOUDFLARE&&image_app&&board_model&&usb_state==1);
    CHECK(atomic_load(&state)==4);ps_desktop_presented(true);CHECK(usb_state==2&&atomic_load(&cloudflare_state)==2);
    same_file(CLOUDFLARE_FILE,(const uint8_t*)model,n);
    ps_desktop_usb_status(out,512);status=cJSON_Parse(out);CHECK(status&&cJSON_GetObjectItemCaseSensitive(status,"model_protocol")->valueint==1);cJSON_Delete(status);
    size_t bytes;uint8_t *bmp=gray_bitmap(684,1216,false,false,&bytes);
    CHECK(request("POST","/api/manage/pico/canvas/show","image/bmp",bmp,bytes,out)==202&&ps_desktop_tick());
    CHECK(!board_model&&current_route==PS_CANVAS&&!image_app);ps_desktop_presented(true);free(bmp);
    reset_memory();CHECK(ps_desktop_prepare_app(true)&&board_model);reset_memory();erase_files();
}

// 中文：给真实Swift串口发送器使用生产接收器，标准输出仅包含协议帧。
// English: Expose the production receiver to the real Swift serial sender with protocol-only stdout.
static int direct_consumer(void){
    char line[256],out[2048];uint8_t block[1024];current_route=1;ps_desktop_init();
    setvbuf(stdout,NULL,_IONBF,0);
    while(fgets(line,sizeof(line),stdin)){
        size_t length=strlen(line);while(length&&(line[length-1]=='\n'||line[length-1]=='\r'))line[--length]=0;
        if(!strcmp(line,"RPICO BOARD STATUS"))ps_desktop_usb_status(out,sizeof(out));
        else{
            size_t binary=0;if(!ps_desktop_usb_direct_command(line,out,sizeof(out),&binary))return 3;
            if(binary){CHECK(binary<=sizeof(block));size_t got=fread(block,1,binary,stdin);if(got!=binary)return 4;ps_desktop_usb_direct_payload(block,binary,out,sizeof(out));}
        }
        if(*out)printf("\nRPICO %s\n",out);
        if(ps_desktop_tick()){
            uint8_t *fb=malloc(684u*1216u/2u);CHECK(fb);ps_desktop_render_app(fb,current_route==PS_CLOUDFLARE);free(fb);ps_desktop_presented(true);
        }
        now_ms+=5;
    }
    reset_memory();return ferror(stdin)?5:0;
}
int main(int argc,char**argv){CHECK(argc==2||argc==3);CHECK(snprintf(root,sizeof(root),"%s",argv[1])<(int)sizeof(root));reset_memory();if(argc==3&&!strcmp(argv[2],"direct"))return direct_consumer();if(argc==3){ps_desktop_init();bool changed=ps_desktop_tick();char out[2048];ps_desktop_usb_status(out,sizeof(out));printf("%s\n",out);if(changed){size_t bytes=684u*1216u/2u;uint8_t *fb=malloc(bytes);CHECK(fb);ps_desktop_render(fb);char raw_path[1400];CHECK(snprintf(raw_path,sizeof(raw_path),"%s/native-framebuffer.bin",root)<(int)sizeof(raw_path));FILE *raw=fopen(raw_path,"wb");CHECK(raw&&fwrite(fb,1,bytes,raw)==bytes&&!fclose(raw));free(fb);ps_desktop_presented(true);}ps_desktop_usb_status(out,sizeof(out));printf("%s\n",out);return usb_state==2?0:2;}model_checks();app_channel_checks();pair_checks();direct_checks();background_board_checks();power_checks();parser_checks();contain_checks();gray_checks();config_checks();storage_checks();lock_checks();gray_lock_storage_checks();usb_checks();reset_memory();erase_files();printf("Desktop canvas: %u checks PASS\n",checks);return 0;}

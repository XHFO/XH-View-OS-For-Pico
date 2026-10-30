/* SPDX-License-Identifier: Apache-2.0
 * 壳界面同源函数截图，不连接硬件、不保存配置。 / Capture source-extracted shell UI without hardware or persistence.
 */
#include "pico_system.h"
#include "pico_navigation.h"
#include "pico_theme.h"
#include "pico_framebuffer_host.h"
#include "ttf_font.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {int x_mg,y_mg,z_mg;} sc7a20h_sample_t;
static const app_desc_t app_book={0};
static bool app_settings_test_awake(void){return true;}
int64_t ps_now(void){return 10000;}
bool ps_demo_home_online(void){return true;}
const char *ps_home_connection_label(void){return "家庭连接未配置";}
void ps_reader_render(uint8_t *fb){(void)fb;}
void ps_home_render(uint8_t *fb){(void)fb;}
void ps_games_render(uint8_t *fb){(void)fb;}
void ps_storage_render(uint8_t *fb){(void)fb;}
void ps_connectivity_render(uint8_t *fb){(void)fb;}
void ps_usb_render(uint8_t *fb){(void)fb;}
bool ps_usb_background(void){return false;}
void ps_usb_background_render(uint8_t *fb){(void)fb;}
void ps_desktop_render(uint8_t *fb){(void)fb;}
static void fan_threshold(uint8_t *fb){(void)fb;}
void ps_bluetooth_render(uint8_t *fb){(void)fb;}
void ps_datetime_render(uint8_t *fb){(void)fb;}
void ps_power_render(uint8_t *fb){(void)fb;}
void ps_sound_render(uint8_t *fb){(void)fb;}
bool ps_load(const char *key,void *out,size_t bytes){(void)key;(void)out;(void)bytes;return false;}
bool ps_store(const char *key,const void *data,size_t bytes){(void)key;(void)data;(void)bytes;return true;}
void ps_toast(const char *text){(void)text;}
void ps_back(void){}
bool ps_home_launcher_visible(void);
static bool app_book_global_is_open(void){return true;}
static bool app_book_global_has_selection(void){return false;}
bool ps_reader_is_open(void){return true;}
bool ps_games_is_arcade(void){return false;}
bool ps_games_is_snake(void){return false;}
#include "native-system-functions.inc"
bool ps_home_launcher_visible(void){return s_route==PS_HA||s_route==PS_PRINTERS;}
static void shortcut_mask(unsigned mask){s_shortcuts.count=0;for(int i=0;i<13&&s_shortcuts.count<QUICK_MAX;i++)if(mask&(1u<<i))s_shortcuts.ids[s_shortcuts.count++]=i;}


static void save_scene(const char *dir,const char *name){
    char path[512];snprintf(path,sizeof(path),"%s/%s-%s.pgm",dir,name,ps_view().landscape?"landscape":"portrait");
    uint8_t *fb=pico_fb_new();assert(fb);render(NULL,fb);assert(pico_fb_save(path,fb));free(fb);
}
static void reset_scene(void){ps_navigation_init();s_panel=0;s_fan=false;s_route=PS_SHELF;s_fan_selected=-1;s_config.quick=8191;quick_defaults();s_quick_edit.active=false;s_quick_mode=0;s_quick_scroll=0;s_group=0;s_device_page=0;s_config.onboard_step=0;}
static bool overlap(int ax,int ay,int aw,int ah,int bx,int by,int bw,int bh){return ax<bx+bw&&ax+aw>bx&&ay<by+bh&&ay+ah>by;}
typedef struct {int x,y,w,h;} fan_rect_t;
static int fan_px(int logical){return (logical*19+5)/10;}
static fan_rect_t fan_circle(const fan_item_t *item){
    return (fan_rect_t){fan_px(item->x-item->r),fan_px(item->y-item->r),fan_px(item->r*2),fan_px(item->r*2)};
}
static void check_all_fans(void){
    unsigned checks=0;for(int land=0;land<2;land++)for(int side=0;side<2;side++)for(int group=0;group<5;group++){
        quick_defaults();s_config.landscape=land;s_fan_side=side;
        fan_item_t a[QUICK_COUNT];int n=fan_layout(a);assert(n==14);ps_view_t v=ps_view();
        for(int i=0;i<n;i++){
            fan_rect_t r=fan_circle(&a[i]);assert(r.x>=0&&r.y>=0&&r.x+r.w<=fan_px(v.w)&&r.y+r.h<=fan_px(v.h));checks++;
            for(int j=i+1;j<n;j++){assert(hypotf(a[i].x-a[j].x,a[i].y-a[j].y)>a[i].r+a[j].r);checks++;}
        }
    }
    printf("native fan: %u geometry checks PASS; fixed prompt replaces moving per-icon captions\n",checks);
}
static void check_catalog_fans(void){
    for(int land=0;land<2;land++)for(int group=0;group<5;group++)for(int count=0;count<=QUICK_MAX;count++){
        s_config.landscape=land;uint8_t raw[QUICK_COUNT];for(int i=0;i<count;i++)raw[i]=i;quick_import(raw,count);
        fan_item_t a[QUICK_COUNT];int n=fan_layout(a);assert(n==14);
    }
    puts("all supported legacy action counts retain fixed direct-slot geometry: PASS");
}
static void check_transparent_corners(void) {
    for(int land=0;land<2;land++) {
        s_config.landscape=land;uint8_t *fb=pico_fb_new();assert(fb);memset(fb,0x77,1216u*684u);ps_corner_controls(fb);
        ps_view_t v=ps_view();int w=fan_px(v.w),h=fan_px(v.h),changed=0;
        for(int y=0;y<h;y++)for(int x=0;x<w;x++){size_t at=(size_t)y*w+x;bool corner=(x<fan_px(48)||x>=w-fan_px(48))&&y>=h-fan_px(48);if(!corner)assert(fb[at]==0x77);else if(fb[at]!=0x77)changed++;}
        assert(changed>0);free(fb);
    }
    puts("transparent corners preserve all non-corner pixels including bottom center: PASS");
}
static void check_stationary_fan_pixels(void) {
    unsigned long pixels=0;unsigned layouts=0;
    for(int land=0;land<2;land++)for(int side=0;side<2;side++)for(int group=0;group<5;group++) {
        reset_scene();s_config.landscape=land;s_fan=true;s_fan_side=side;
        fan_item_t original[QUICK_COUNT],current[QUICK_COUNT];int count=fan_layout(original);
        uint8_t *base=pico_fb_new(),*selected=pico_fb_new();assert(base&&selected);
        render_fan(base);int stride=land?1216:684;
        for(int active=0;active<count;active++) {
            s_fan_selected=active;render_fan(selected);assert(fan_layout(current)==count);
            assert(!memcmp(original,current,(size_t)count*sizeof(*original)));layouts++;
            // 光圈移动不能反转按钮或改变图标像素；圆边保留4像素以避开轮廓取整。
            // A moving outline cannot invert buttons/icons; erode four pixels to exclude rounded-edge quantization.
            for(int button=0;button<count;button++) {
                int cx=fan_px(original[button].x),cy=fan_px(original[button].y),r=fan_px(original[button].r)-4;
                for(int dy=-r;dy<=r;dy++)for(int dx=-r;dx<=r;dx++)if(dx*dx+dy*dy<=r*r) {
                    size_t at=(size_t)(cy+dy)*stride+cx+dx;assert(base[at]==selected[at]);pixels++;
                }
            }
            // 静态说明行不随选中项变化；动态标题只允许替换固定区域中的内容。
            // Static detail rows stay stationary; the changing title only replaces content in its fixed area.
            int prompt_x=fan_px(land?(side?16:340):16),prompt_w=fan_px(land?260:320);
            for(int row=0;row<2;row++)for(int y=fan_px(row?145:80);y<fan_px((row?145:80)+16);y++)
                for(int x=prompt_x;x<prompt_x+prompt_w;x++) {
                    size_t at=(size_t)y*stride+x;assert(base[at]==selected[at]);pixels++;
                }
        }
        free(base);free(selected);
    }
    printf("native fan raster: %u stationary layouts and %lu unchanged button/prompt pixels PASS\n",layouts,pixels);
}
static void report_fan(const char *name){fan_item_t a[QUICK_COUNT];int n=fan_layout(a);printf("fan %s: %d icons / fixed prompt at 80,106,145\n",name,n);}
int main(int argc,char **argv){
    assert(argc==3);assert(pico_fb_init(argv[1]));check_all_fans();check_catalog_fans();check_transparent_corners();check_stationary_fan_pixels();s_config.version=1;s_config.interval=15;s_config.wifi_count=8;s_config.demo_wifi=1;s_config.wifi_current=0;s_battery=86;
    const char *networks[]={"Home","Office","Studio","Guest Wi-Fi","Read Pico","Bedroom","Lab","Mobile Hotspot"};
    for(int i=0;i<8;i++){snprintf(s_config.wifi[i].ssid,sizeof(s_config.wifi[i].ssid),"%s",networks[i]);s_config.wifi[i].auto_join=1;}
    for(int land=0;land<2;land++){
        s_config.landscape=land;reset_scene();
        for(int side=0;side<2;side++){
            quick_defaults();s_fan=true;s_fan_side=side;
            for(int group=0;group<5;group++){
                s_fan_selected=3+group;char name[64];
                snprintf(name,sizeof(name),"fan-%s-selected-middle%d",side?"right":"left",group);save_scene(argv[2],name);report_fan(name);
            }
            s_fan_selected=10;char name[48];snprintf(name,sizeof(name),"fan-%s-selected-outer",side?"right":"left");save_scene(argv[2],name);
            memset(s_quick_slots,QUICK_EMPTY,FAN_SLOTS);s_fan_selected=-1;snprintf(name,sizeof(name),"fan-%s-empty",side?"right":"left");save_scene(argv[2],name);
        }
        reset_scene();save_scene(argv[2],"root-bookshelf");s_route=PS_HA;save_scene(argv[2],"root-home");s_route=PS_GAMES;save_scene(argv[2],"root-games");s_panel=3;save_scene(argv[2],"page-switcher");s_panel=1;save_scene(argv[2],"panel-main");s_route=PS_READ;save_scene(argv[2],"panel-reading");s_panel=2;
        for(int group=0;group<3;group++){s_group=group;char name[32];snprintf(name,sizeof(name),"panel-group-%d",group);save_scene(argv[2],name);}
        reset_scene();s_route=PS_NAV_SETTINGS;ps_navigation_enter();save_scene(argv[2],"navigation-settings");
        ps_event_t add={.type=PS_TAP,.x=40,.y=land?310:513};assert(ps_navigation_event(&add));
        if(land){ps_event_t next={.type=PS_TAP,.x=560,.y=285};assert(ps_navigation_event(&next));}
        ps_event_t printer={.type=PS_TAP,.x=80,.y=land?145:335};assert(ps_navigation_event(&printer));
        ps_event_t done={.type=PS_TAP,.x=ps_view().w-70,.y=ps_view().h-(land?30:50)};assert(ps_navigation_event(&done));assert(ps_navigation_count()==4);
        s_route=PS_PRINTERS;save_scene(argv[2],"root-printers-four-tabs");s_panel=3;save_scene(argv[2],"page-switcher-four");
        reset_scene();s_route=PS_DEVICE;save_scene(argv[2],"device-page1");s_device_page=1;save_scene(argv[2],"device-page2");s_device_page=2;save_scene(argv[2],"device-page3");
        s_route=PS_QUICK_SETTINGS;quick_edit_begin();s_quick_edit.selected=0;save_scene(argv[2],"quick-editor-left");s_quick_edit.side=1;save_scene(argv[2],"quick-editor-right");s_quick_edit.side=0;s_quick_edit.slots[0]=QUICK_EMPTY;save_scene(argv[2],"quick-editor-empty-inner");s_quick_edit.dragging=true;s_quick_edit.target=7;save_scene(argv[2],"quick-editor-drag-target");s_quick_edit.dragging=false;s_quick_edit.save_failed=true;save_scene(argv[2],"quick-editor-save-failure");s_quick_mode=1;s_quick_scroll=0;save_scene(argv[2],"quick-settings-add");s_quick_scroll=25;save_scene(argv[2],"quick-settings-last");s_quick_edit.active=false;
        s_route=PS_ONBOARDING;for(int step=0;step<4;step++){s_config.onboard_step=(uint8_t)step;char name[32];snprintf(name,sizeof(name),"onboarding-%d",step);save_scene(argv[2],name);}
    }
    // 外部书页故意与已存方向相反，确保截图采用实际画布而非旧偏好。
    // Make the external book oppose the saved preference so captures must follow the actual canvas.
    for(int rotation=0;rotation<4;rotation++){
        reset_scene();host_rotation=(enum EpdRotation)rotation;s_global_app=&app_book;
        bool landscape=host_rotation==EPD_ROT_LANDSCAPE||host_rotation==EPD_ROT_INVERTED_LANDSCAPE;
        s_config.landscape=!landscape;assert(ps_view().landscape==landscape);
        assert(ps_view().w==(landscape?640:360));assert(ps_view().h==(landscape?360:640));
        for(int side=0;side<2;side++){
            s_fan=true;s_fan_side=side;char name[64];
            snprintf(name,sizeof(name),"real-book-rotation%d-fan-%s",rotation,side?"right":"left");save_scene(argv[2],name);
        }
        s_fan=false;s_panel=1;char name[64];snprintf(name,sizeof(name),"real-book-rotation%d-panel",rotation);save_scene(argv[2],name);
        s_global_app=NULL;
    }
    pico_fb_close();return 0;
}

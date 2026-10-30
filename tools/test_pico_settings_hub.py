#!/usr/bin/env python3
"""同源设置分组、命中及真实导航返回检查；不连接硬件。 / Exact grouped settings and navigation/back checks without hardware."""
from pathlib import Path
import argparse
import ast
import hashlib
import json
import os
import re
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
SOURCE = (ROOT / 'main/system/pico_system.c').read_text()
HEADER = (ROOT / 'main/system/pico_system.h').read_text()
tree = ast.parse((ROOT / 'tools/test_pico_system.py').read_text())
node = next(n for n in tree.body if isinstance(n, ast.FunctionDef) and n.name == 'function')
namespace = {'re': re, 'SOURCE': SOURCE}
exec(compile(ast.Module(body=[node], type_ignores=[]), 'extract', 'exec'), namespace)
function = namespace['function']
types = '\n'.join(re.findall(r'typedef enum\s*\{[^}]*\}\s*\w+;', HEADER, re.S))
types += '\n' + '\n'.join(re.findall(r'typedef struct\s*\{[^}]*\}\s*ps_(?:event|view)_t;', HEADER, re.S))


def settings_source():
    """Keep real descriptors and helpers together so tests exercise the actual tables."""
    start = re.search(r'(?:^|\n)enum\s*\{\s*DEVICE_[^}]*\};', SOURCE).start()
    end = SOURCE.index('static int quick_catalog_row_height', start)
    return SOURCE[start:end] + '\n' + function('device_event')


PRELUDE = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#define ARRAY_COUNT(a) ((int)(sizeof(a)/sizeof((a)[0])))
#define PICO_STORE_COLD
#define ESP_LOGI(...) ((void)0)
typedef struct {int x,y,width,height;} EpdRect;
typedef struct {bool request_return;} app_ctx_t;
typedef struct {bool product_shell;} app_desc_t;
enum {BOOK_GLOBAL_FONT,BOOK_GLOBAL_BACK};
'''

STUBS = r'''
static ps_view_t v;
static int s_device_page,s_depth,s_section,s_panel,s_group,s_focus,s_quick_mode,s_fan_selected,s_global_redraw,s_book_return_depth;
static ps_route_t s_route=PS_SHELF,s_history[10],s_workspace[2]={PS_SHELF,PS_HA},s_sleep_origin;
static bool s_dirty,s_force_full,s_tracking,s_fan,s_fan_candidate,s_long,s_book_return,s_real_book_preferred;
static struct {bool active;} s_quick_edit;
static struct {int onboard_step;bool landscape;} s_config;
static int64_t s_last_input;
static app_ctx_t test_ctx,*s_ctx=&test_ctx;
static const app_desc_t app_book={0};
static const app_desc_t *s_global_app;
static bool awake,leave_blocked,connectivity_blocked,storage_busy,usb_background,usb_back_consumed,power_back_consumed;
static unsigned config_saves,departures,draw_calls,book_actions;
static int last_book_action;
static char toast[160],painted[32768];
static ps_view_t ps_view(void){return v;}
static int64_t ps_now(void){return 500;}
static bool app_settings_test_awake(void){return awake;}
static void app_settings_set_test_awake(bool value){awake=value;}
static void ps_toast(const char*t){snprintf(toast,sizeof(toast),"%s",t);}
static bool ps_hit(int x,int y,int a,int b,int w,int h){return x>=a&&y>=b&&x<a+w&&y<b+h;}
static void geometry(int x,int y,int w,int h,int gray){assert(x>=0&&y>=0&&w>=0&&h>=0&&x+w<=v.w&&y+h<=v.h&&gray>=0&&gray<=15);draw_calls++;}
static void text(const char*t){if(!t)return;size_t n=strlen(painted),more=strlen(t);assert(n+more+2<sizeof(painted));memcpy(painted+n,t,more);painted[n+more]='\n';painted[n+more+1]=0;}
static void ps_fill(uint8_t*f,int x,int y,int w,int h,int gray){(void)f;geometry(x,y,w,h,gray);}
static void ps_surface(uint8_t*f,int x,int y,int w,int h,bool raised){(void)f;(void)raised;geometry(x,y,w,h,15);}
static void ps_line(uint8_t*f,int x,int y,int xx,int yy,int gray){(void)f;geometry(x,y,0,0,gray);geometry(xx,yy,0,0,gray);}
static void ps_icon(uint8_t*f,int x,int y,int size,ps_icon_t icon,int gray){(void)f;(void)icon;geometry(x,y,size,size,gray);}
static void ps_text(uint8_t*f,int x,int y,int size,const char*t,int gray){(void)f;geometry(x,y,0,size,gray);text(t);}
static void ps_center(uint8_t*f,int x,int y,int size,const char*t,int gray){ps_text(f,x,y,size,t,gray);}
static void ps_button(uint8_t*f,int x,int y,int w,int h,const char*t,bool selected){(void)f;(void)selected;geometry(x,y,w,h,15);text(t);}
static void ps_heading(uint8_t*f,const char*t,const char*sub){(void)f;text(t);text(sub);draw_calls++;}
static void ps_global_cancel_input(void){}
static void quick_edit_begin(void){s_quick_edit.active=true;}
static void quick_edit_contact_cancel(void){}
static void ps_storage_enter(void){}
static void ps_usb_enter(void){}
static void ps_bluetooth_enter(void){}
static void ps_datetime_enter(void){}
static void ps_power_enter(void){}
static void ps_dual_enter(void){}
static void ps_navigation_enter(void){}
static void ps_connectivity_enter(ps_route_t r){(void)r;}
static void ps_reader_enter(ps_route_t r){(void)r;}
static void ps_home_enter(ps_route_t r){(void)r;}
static void ps_games_enter(ps_route_t r){(void)r;}
static void ps_launcher_enter(void){}
static void ps_desktop_prepare_app(bool cloudflare){(void)cloudflare;}
static bool local_before_leave(ps_route_t r){(void)r;return leave_blocked;}
static bool ps_connectivity_before_leave(ps_route_t r){(void)r;return connectivity_blocked;}
static void ps_power_preview(int which){(void)which;}
static int app_book_global_action(app_ctx_t*c,int action){assert(c==s_ctx);book_actions++;last_book_action=action;return 73;}
static void global_depart(void){departures++;}
static bool ps_storage_busy(void){return storage_busy;}
static bool ps_power_back(void){return power_back_consumed;}
static bool ps_usb_background(void){return usb_background;}
static bool ps_usb_back(void){return usb_back_consumed;}
static bool ps_storage_back(void){return false;}
static bool ps_navigation_back(void){return false;}
static bool local_hold(void){return usb_background;}
static bool ps_usb_local_route(ps_route_t r){return r==PS_DEVICE||r==PS_BLUETOOTH||r==PS_USB_TRANSFER||r==PS_CANVAS||r==PS_SOUND;}
static void ps_wake(void){}
static bool ps_games_key(int key){(void)key;return false;}
static bool ps_reader_key(int key){(void)key;return false;}
static bool ps_home_key(int key){(void)key;return false;}
static bool ps_connectivity_back(void){return false;}
static bool resume_real_book(void){return false;}
static void save_config(void){config_saves++;}
static void enter(ps_route_t r);
static void ps_switch(int section){s_depth=0;enter(section==2?PS_GAMES:section==1?PS_HA:PS_SHELF);}
void ps_navigate(ps_route_t r);
void ps_back(void);
'''

TESTS = r'''
static void reset(ps_route_t origin){
 s_route=origin;s_depth=s_device_page=s_panel=s_group=s_focus=0;s_section=origin==PS_HA?1:0;
 s_fan=s_fan_candidate=s_tracking=s_book_return=s_real_book_preferred=s_dirty=false;
 s_quick_edit.active=false;s_quick_mode=0;s_global_app=NULL;
 awake=leave_blocked=connectivity_blocked=storage_busy=usb_background=usb_back_consumed=power_back_consumed=false;
 s_config.onboard_step=3;toast[0]=0;
}
static void render_settings(void){uint8_t fb=0;painted[0]=0;device_render(&fb);assert(draw_calls);}
static EpdRect expected_rect(int index,bool secondary){
 int rows=secondary?(v.landscape?2:4):(v.landscape?3:6),cols=v.landscape?2:1;
 int rh=secondary?(v.landscape?60:76):(v.landscape?48:60),top=secondary?(v.landscape?112:126):(v.landscape?146:158);
 int cw=(v.w-32-(cols-1)*12)/cols;
 return(EpdRect){16+(index/rows)*(cw+12),top+(index%rows)*rh,cw,rh};
}
static void tap_rect(EpdRect r){assert(r.height>=44&&r.width>=44);ps_event_t e={.type=PS_TAP,.x=r.x+r.width/2,.y=r.y+r.height/2};assert(device_event(&e));}
static void open_settings(ps_route_t origin){reset(origin);ps_navigate(PS_DEVICE);assert(s_route==PS_DEVICE&&!s_device_page&&s_depth==1&&s_history[0]==origin);}
static void open_group(int group){assert(s_route==PS_DEVICE&&!s_device_page&&group>0&&group<6);int depth=s_depth;tap_rect(expected_rect(group,false));assert(s_route==PS_DEVICE&&s_device_page==group&&s_depth==depth);}
static void test_routing(void){
 const ps_route_t expected[5][4]={{PS_WIFI,PS_WIRELESS_MANAGE,PS_BLUETOOTH},{PS_READING_SETTINGS,PS_INPUT_SETTINGS,PS_DUAL},{PS_NAV_SETTINGS,PS_QUICK_SETTINGS,PS_SOUND},{PS_APPS,PS_HA_SETTINGS,PS_PRINTER_SETTINGS,PS_GAMES},{PS_POWER,PS_DATETIME,PS_DEVICE}};
 const int counts[]={3,3,3,4,3};
 assert(DEVICE_GROUP_COUNT==6&&DEVICE_ABOUT==6&&DEVICE_SETTING_COUNT==17);
 assert(ARRAY_COUNT(device_groups)==6&&ARRAY_COUNT(device_settings)==17);
 unsigned seen=0;
 for(int i=0;i<DEVICE_SETTING_COUNT;i++){
  assert(device_settings[i].route!=PS_STORAGE);
  for(int j=0;j<i;j++)assert(device_settings[i].route!=device_settings[j].route);
  if(device_settings[i].route!=PS_DEVICE)seen++;
 }
 assert(seen==16&&device_settings[0].route==PS_TRANSFER);
 for(int land=0;land<2;land++){
  v=land?(ps_view_t){640,360,true}:(ps_view_t){360,640,false};s_config.landscape=land;
  open_settings(PS_SHELF);render_settings();
  const char* roots[]={"存储与传输","网络与连接","阅读与输入","外观与操作","应用与服务","系统与电源"};
  for(int i=0;i<6;i++){
   assert(strstr(painted,roots[i]));EpdRect a=expected_rect(i,false),b=device_row_rect(i,true);assert(!memcmp(&a,&b,sizeof(a)));
  }
  tap_rect(expected_rect(0,false));assert(s_route==PS_TRANSFER&&s_depth==2&&!s_device_page);ps_back();assert(s_route==PS_DEVICE&&!s_device_page&&s_depth==1);
  for(int group=1;group<6;group++)for(int i=0;i<counts[group-1];i++){
   open_settings(PS_HA);open_group(group);render_settings();int depth=s_depth;
   EpdRect a=expected_rect(i,true),b=device_row_rect(i,false);assert(!memcmp(&a,&b,sizeof(a)));
   bool before=awake;tap_rect(a);
   if(group==5&&i==2){assert(s_route==PS_DEVICE&&s_device_page==5&&s_depth==depth&&awake!=before&&s_last_input==500);tap_rect(a);assert(awake==before&&s_depth==depth);}
   else{assert(s_route==expected[group-1][i]&&s_depth==depth+1&&s_device_page==group);ps_back();assert(s_route==PS_DEVICE&&s_device_page==group&&s_depth==depth);}
   ps_back();assert(s_route==PS_DEVICE&&!s_device_page&&s_depth==depth);ps_back();assert(s_route==PS_HA&&!s_depth);
  }
  open_settings(PS_SHELF);tap_rect((EpdRect){16,82,v.w-32,56});assert(s_route==PS_DEVICE&&s_device_page==6&&s_depth==1&&!awake);render_settings();
  assert(strstr(painted,"XH View OS For Pico")&&strstr(painted,"双机阅读实验版")&&strstr(painted,PS_VERSION)&&strstr(painted,"首次使用引导"));
  tap_rect((EpdRect){16,v.h-110,v.w-32,44});assert(s_route==PS_ONBOARDING&&s_config.onboard_step==0&&s_depth==2&&s_device_page==6);
  s_config.onboard_step=2;unsigned saves=config_saves;ps_back();assert(s_route==PS_ONBOARDING&&s_config.onboard_step==1&&config_saves==saves+1);
  ps_back();assert(s_route==PS_ONBOARDING&&!s_config.onboard_step);ps_back();assert(s_route==PS_DEVICE&&s_device_page==6&&s_depth==1);
  ps_back();assert(s_route==PS_DEVICE&&!s_device_page&&s_depth==1);ps_back();assert(s_route==PS_SHELF&&!s_depth);
  for(int page=0;page<7;page++){
   open_settings(PS_SHELF);s_device_page=page;render_settings();int depth=s_depth;
   const ps_event_type_t ignored[]={PS_PRESS,PS_MOVE,PS_RELEASE,PS_LONG,PS_SWIPE_L,PS_SWIPE_R,PS_SWIPE_U,PS_SWIPE_D,PS_CANCEL};
   for(unsigned j=0;j<sizeof(ignored)/sizeof(ignored[0]);j++){ps_event_t e={.type=ignored[j],.x=1,.y=1};(void)device_event(&e);assert(s_route==PS_DEVICE&&s_device_page==page&&s_depth==depth);}
  }
 }
}
static void test_history_and_guards(void){
 v=(ps_view_t){360,640,false};open_settings(PS_HA);open_group(1);tap_rect(expected_rect(0,true));assert(s_route==PS_WIFI&&s_device_page==1&&s_depth==2);
 leave_blocked=true;ps_navigate(PS_DEVICE);assert(s_route==PS_WIFI&&s_device_page==1&&s_depth==2);leave_blocked=false;
 connectivity_blocked=true;ps_navigate(PS_DEVICE);assert(s_route==PS_WIFI&&s_device_page==1&&s_depth==2);connectivity_blocked=false;
 ps_navigate(PS_ROUTE_COUNT);assert(s_route==PS_WIFI&&s_device_page==1&&s_depth==2);
 ps_navigate(PS_DEVICE);assert(s_route==PS_DEVICE&&!s_device_page&&s_depth==1);
 open_group(3);ps_navigate(PS_DEVICE);assert(s_route==PS_DEVICE&&!s_device_page&&s_depth==1);
 open_group(2);s_fan=true;ps_back();assert(!s_fan&&s_device_page==2&&s_depth==1);
 s_panel=2;ps_back();assert(s_panel==1&&s_device_page==2);ps_back();assert(!s_panel&&s_device_page==2);ps_back();assert(!s_device_page&&s_depth==1);
 open_group(5);storage_busy=true;ps_back();assert(s_device_page==5&&s_depth==1);storage_busy=false;
 usb_background=usb_back_consumed=true;ps_back();assert(s_device_page==5&&s_depth==1);usb_back_consumed=false;
 ps_back();assert(s_route==PS_DEVICE&&!s_device_page&&s_depth==1);usb_background=false;
 open_group(5);tap_rect(expected_rect(0,true));assert(s_route==PS_POWER);power_back_consumed=true;ps_back();assert(s_route==PS_POWER&&s_device_page==5);power_back_consumed=false;ps_back();assert(s_route==PS_DEVICE&&s_device_page==5);
 ps_back();open_group(2);s_book_return=s_real_book_preferred=true;toast[0]=0;tap_rect(expected_rect(0,true));assert(s_route==PS_DEVICE&&s_device_page==2&&s_depth==1&&toast[0]);
}
static void test_hidden_settings_real_book_back(void){
 // 从二级设置继续真实读书时，壳的分组状态仍保留；返回必须交给真实书籍。
 // Resuming a real book preserves the hidden shell group; Back must reach that book.
 for(int land=0;land<2;land++)for(int page=1;page<=6;page++){
  v=land?(ps_view_t){640,360,true}:(ps_view_t){360,640,false};s_config.landscape=land;
  open_settings(PS_SHELF);
  if(page<6)open_group(page);else tap_rect((EpdRect){16,82,v.w-32,56});
  assert(s_route==PS_DEVICE&&s_device_page==page&&s_depth==1);
  s_global_app=&app_book;s_global_redraw=-1;unsigned before=book_actions;
  ps_back();
  assert(book_actions==before+1&&last_book_action==BOOK_GLOBAL_BACK&&s_global_redraw==73);
  assert(s_route==PS_DEVICE&&s_device_page==page&&s_depth==1);
 }
}
static void test_module_settings_return(void){
 for(int land=0;land<2;land++)for(int entry=1;entry<=2;entry++){
  v=land?(ps_view_t){640,360,true}:(ps_view_t){360,640,false};s_config.landscape=land;
  open_settings(PS_SHELF);open_group(4);tap_rect(expected_rect(entry,true));
  assert(s_route==(entry==1?PS_HA_SETTINGS:PS_PRINTER_SETTINGS));
  ps_navigate(PS_APPLICATION);ps_navigate(PS_DEVICE);
  assert(s_route==PS_DEVICE&&s_device_page==4&&s_depth==1);
  ps_back();assert(s_route==PS_DEVICE&&!s_device_page&&s_depth==1);
  open_group(4);tap_rect(expected_rect(entry,true));ps_navigate(PS_APPLICATION);
  ps_global_open_settings(s_ctx);assert(s_route==PS_DEVICE&&!s_device_page&&s_depth==1);
  reset(PS_APPLICATION);s_device_page=4;ps_navigate(PS_DEVICE);
  assert(s_route==PS_DEVICE&&!s_device_page&&s_depth==1);
 }
}
int main(void){test_routing();test_history_and_guards();test_hidden_settings_real_book_back();test_module_settings_return();puts("PASS: six root settings groups and About; 17 setting entries including dual-reader and keep-awake; shared portrait/landscape targets; child/group/root history; TF Home/printer return retains its group and explicit Settings opens root; onboarding; overlay/USB/storage/leave/real-book guards; hidden grouped settings do not swallow real-book Back; 16-gray bounded rendering.");}
'''


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, help='Keep the generated harness, binary and report in this directory.')
    args = parser.parse_args()
    constants = '\n'.join(re.findall(r'^#define PS_(?:VERSION|OS_NAME|RELEASE_LABEL)[^\n]+', SOURCE, re.M))
    names = ('reader_route', 'home_route', 'enter', 'ps_navigate', 'ps_back', 'ps_global_open_settings')
    unit = PRELUDE + types + '\n' + constants + '\n' + STUBS
    unit += '\n'.join(function(name) for name in names) + '\n' + settings_source() + '\n' + TESTS
    with tempfile.TemporaryDirectory(prefix='pico-settings-hub-') as temporary:
        output = args.output.resolve() if args.output else Path(temporary)
        output.mkdir(parents=True, exist_ok=True)
        source, binary = output / 'test.c', output / 'test'
        source.write_text(unit)
        command = [os.environ.get('CC', 'cc'), '-std=c11', '-Wall', '-Wextra', '-Werror', '-g', '-fsanitize=address,undefined', '-fno-omit-frame-pointer', str(source), '-o', str(binary)]
        subprocess.run(command, check=True)
        result = subprocess.run([str(binary)], text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        print(result.stdout, end='')
        result.check_returncode()
        if args.output:
            (output / 'test.log').write_text(result.stdout)
            report = {'result': 'PASS', 'hardware_access': False, 'sanitizers': ['address', 'undefined'], 'orientations': 2, 'root_groups': 6, 'setting_entries': 17, 'source_sha256': hashlib.sha256(SOURCE.encode()).hexdigest(), 'test_sha256': hashlib.sha256(Path(__file__).read_bytes()).hexdigest(), 'binary_sha256': hashlib.sha256(binary.read_bytes()).hexdigest()}
            (output / 'report.json').write_text(json.dumps(report, ensure_ascii=False, indent=2) + '\n')


if __name__ == '__main__':
    main()

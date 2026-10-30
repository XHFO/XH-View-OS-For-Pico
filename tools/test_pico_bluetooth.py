#!/usr/bin/env python3
"""同源蓝牙页面和外部翻页守卫检查；不访问真实蓝牙。 / Exact-source Bluetooth UI and pager guards, without radio access."""
from pathlib import Path
import argparse
import os
import re
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def function(text, name):
    match = re.search(r'^(?:static\s+)?[\w* ]+\b' + name + r'\([^;]*?\)\s*\{', text, re.M)
    assert match, name
    at, depth, quote = match.end(), 1, None
    while depth:
        c = text[at]
        if quote:
            if c == '\\':
                at += 2
                continue
            if c == quote:
                quote = None
        elif c in '\"\'':
            quote = c
        elif c == '{':
            depth += 1
        elif c == '}':
            depth -= 1
        at += 1
    return text[match.start():at]


PRELUDE = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stddef.h>
#include <string.h>
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_FAIL -1
typedef struct { int x,y,width,height; } EpdRect;
typedef struct { bool touched;int count; } touch_t;
typedef struct app_desc_s { int id; } app_desc_t;
typedef struct { const touch_t *touch;bool consumed,pressed,released,request_return,request_menu,request_developer_menu;const app_desc_t *request_app; } app_ctx_t;
typedef enum { APP_REDRAW_NONE,APP_REDRAW_AREA,APP_REDRAW_PAGE,APP_REDRAW_FULL,APP_REDRAW_DONE } app_redraw_t;
static const app_desc_t app_book={1},app_pico_system={2},other_app={3};
static bool s_product_session=true,s_toolbar,s_selection,s_image_confirm_open,s_cover_open,s_image_open,s_scrubbing,s_progress_pending,
    s_page_jump_open,s_clear_confirm,s_batch_confirm,s_scan_pending;
static char *s_text="loaded prose",s_open_path[64],s_resume_path[64];
enum { SHELF,READING,TOC,MANAGE,BULK,SEARCH };
static int s_view=READING,turns,last_direction;
static bool dual_enabled,dual_accept;
static unsigned s_dual_target;static struct {bool pending;} s_dual_anchor;
static bool ps_dual_enabled(void){return dual_enabled;}
static bool ps_dual_turn(int direction){turns++;last_direction=direction;return dual_accept;}
static app_redraw_t turn_page(app_ctx_t *ctx,int direction){assert(ctx);turns++;last_direction=direction;return APP_REDRAW_PAGE;}
'''

STUBS = r'''
static ps_view_t test_view={360,640,false};
static ps_route_t test_route=PS_BLUETOOTH;
static void ps_navigate(ps_route_t route){test_route=route;}
static void ps_text(uint8_t *fb,int x,int y,int px,const char *text,int gray){(void)fb;(void)x;(void)y;(void)px;(void)text;(void)gray;}
static bool storage_busy,usb_busy;
static unsigned reader_keys,polled,render_operations,init_calls,enable_calls,scan_calls,disconnect_calls,forget_calls,reverse_calls;
static int event_direction,connected_index=-1;static bool requested_enabled,requested_reversed;
static read_pico_ble_pager_status_t test_status;
static size_t test_device_count;
static read_pico_ble_pager_device_t test_devices[READ_PICO_BLE_PAGER_MAX_DEVICES];
static unsigned list_rows,wrapped_names;
static ps_view_t ps_view(void){return test_view;}
static ps_route_t ps_route(void){return test_route;}
static bool ps_storage_busy(void){return storage_busy;}
static bool ps_usb_busy(void){return usb_busy;}
static void ps_toast(const char *s){assert(s);}
static bool ps_hit(int x,int y,int bx,int by,int w,int h){return x>=bx&&x<bx+w&&y>=by&&y<by+h;}
static void rect_ok(int x,int y,int w,int h){assert(x>=0&&y>=0&&w>0&&h>0&&x+w<=test_view.w&&y+h<=test_view.h);render_operations++;}
static void ps_heading(uint8_t *fb,const char *title,const char *sub){(void)fb;(void)sub;assert(title);}
static void ps_button(uint8_t *fb,int x,int y,int w,int h,const char *label,bool selected){(void)fb;(void)selected;assert(label);rect_ok(x,y,w,h);}
static void ps_box(uint8_t *fb,int x,int y,int w,int h,int r,int gray,bool fill){(void)fb;(void)r;(void)gray;(void)fill;rect_ok(x,y,w,h);if(h==48)list_rows++;}
static void ps_text_fixed(uint8_t *fb,int x,int y,int size,const char *label,int gray){(void)fb;(void)gray;assert(label);rect_ok(x,y,1,size);}
static void ps_center(uint8_t *fb,int x,int y,int size,const char *label,int gray){ps_text_fixed(fb,x,y,size,label,gray);}
static void ps_wrap(uint8_t *fb,int x,int y,int w,int size,int lines,const char *label,int gray){(void)fb;(void)gray;assert(label&&lines==1);int cols=test_view.landscape?2:1;assert(w==(test_view.w-32-(cols-1)*12)/cols-20);rect_ok(x,y,w,size);wrapped_names++;}
static esp_err_t read_pico_ble_pager_init(void){init_calls++;return ESP_OK;}
bool read_pico_ble_pager_light_sleep_safe(void){return !test_status.enabled;}
static void read_pico_ble_pager_get_status(read_pico_ble_pager_status_t *out){*out=test_status;}
static size_t read_pico_ble_pager_get_devices(read_pico_ble_pager_device_t *out,size_t capacity){size_t n=test_device_count<capacity?test_device_count:capacity;memcpy(out,test_devices,n*sizeof(*out));return n;}
static esp_err_t read_pico_ble_pager_set_enabled(bool enabled){enable_calls++;requested_enabled=enabled;return ESP_OK;}
static esp_err_t read_pico_ble_pager_scan(void){scan_calls++;return ESP_OK;}
static esp_err_t read_pico_ble_pager_disconnect(void){disconnect_calls++;return ESP_OK;}
static esp_err_t read_pico_ble_pager_forget(void){forget_calls++;return ESP_OK;}
static esp_err_t read_pico_ble_pager_set_reversed(bool reversed){reverse_calls++;requested_reversed=reversed;return ESP_OK;}
static esp_err_t read_pico_ble_pager_connect(size_t index){assert(index<test_device_count);connected_index=(int)index;return ESP_OK;}
static int read_pico_ble_pager_poll_event(void){int value=event_direction;event_direction=0;polled++;return value;}
'''

TESTS = r'''
static void tap(int x,int y){ps_event_t event={.type=PS_TAP,.x=x,.y=y};assert(ps_bluetooth_event(&event));}
static void defaults(void){
    test_status=(read_pico_ble_pager_status_t){.enabled=true,.ready=true,.device_count=8};test_device_count=8;
    for(int i=0;i<8;i++){snprintf(test_devices[i].name,sizeof(test_devices[i].name),"BLE HID Page Turner %d",i);test_devices[i].rssi=-40-i;if(i%2){memset(test_devices[i].name,'W',sizeof(test_devices[i].name)-1);test_devices[i].name[sizeof(test_devices[i].name)-1]=0;}}
    s_device_page=0;
}
static void test_geometry_and_controls(void){
    ps_bluetooth_init();assert(init_calls==1);
    for(int landscape=0;landscape<2;landscape++){
        test_view=landscape?(ps_view_t){640,360,true}:(ps_view_t){360,640,false};defaults();
        for(int page=0;page<(8+rows()-1)/rows();page++){
            ps_bluetooth_enter();s_device_page=page;list_rows=wrapped_names=0;unsigned before=enable_calls+scan_calls+disconnect_calls+forget_calls+reverse_calls;
            ps_bluetooth_render(NULL);assert(enable_calls+scan_calls+disconnect_calls+forget_calls+reverse_calls==before);
            int expected=8-page*rows();if(expected>rows())expected=rows();assert((int)list_rows==expected&&(int)wrapped_names==expected);
            for(int row=0;row<expected;row++){EpdRect r=device_rect(row);connected_index=-1;tap(r.x+r.width/2,r.y+r.height/2);assert(connected_index==page*rows()+row);}
        }
        ps_bluetooth_enter();int split=landscape?test_view.w/2:test_view.w;
        unsigned before=enable_calls;tap(16+(split-32)/2,100);assert(enable_calls==before+1&&!requested_enabled);
        int y=landscape?136:222,cw=(test_view.w-56)/3;
        before=scan_calls;tap(16+cw/2,y+20);assert(scan_calls==before+1);
        before=disconnect_calls;tap(28+cw+cw/2,y+20);assert(disconnect_calls==before+1);
        before=forget_calls;tap(40+2*cw+cw/2,y+20);assert(forget_calls==before+1);
        before=reverse_calls;tap(30,landscape?195:295);assert(reverse_calls==before+1&&requested_reversed);
        ps_event_t non_tap={.type=PS_SWIPE_U};assert(!ps_bluetooth_event(&non_tap));
        unsigned old=(unsigned)s_device_page;tap(test_view.w-60,test_view.h-48);assert(s_device_page==(int)old+1);
        tap(60,test_view.h-48);assert(s_device_page==(int)old);
        test_status.enabled=false;test_status.ready=false;before=enable_calls;tap(16+cw/2,y+20);assert(enable_calls==before+1&&requested_enabled&&s_scan_when_ready);
        test_status.enabled=true;test_status.ready=true;before=scan_calls;ps_bluetooth_tick();assert(scan_calls==before+1&&!s_scan_when_ready);
    }
}
static void test_page_clamping(void){
    defaults();test_view=(ps_view_t){640,360,true};s_device_page=3;ps_bluetooth_render(NULL);
    test_view=(ps_view_t){360,640,false};list_rows=0;ps_bluetooth_render(NULL);assert(list_rows==2);
    EpdRect r=device_rect(0);tap(r.x+10,r.y+10);assert(s_device_page==2&&connected_index==6);
    test_status.device_count=1;test_device_count=1;list_rows=0;ps_bluetooth_render(NULL);assert(list_rows==1);
    tap(r.x+10,r.y+10);assert(!s_device_page&&connected_index==0);
}
static void test_external_turn_guards(void){
    app_ctx_t ctx={0};unsigned before=(unsigned)turns;
    assert(app_book_external_turn(&ctx,1)==APP_REDRAW_PAGE&&turns==(int)before+1&&last_direction==1);
    assert(app_book_external_turn(&ctx,-1)==APP_REDRAW_PAGE&&last_direction==-1);
    before=(unsigned)turns;assert(app_book_external_turn(NULL,1)==APP_REDRAW_NONE);assert(app_book_external_turn(&ctx,0)==APP_REDRAW_NONE);assert(app_book_external_turn(&ctx,2)==APP_REDRAW_NONE);
    bool *guards[]={&s_toolbar,&s_selection,&s_image_confirm_open,&s_image_open,&s_scrubbing,&s_progress_pending,&s_page_jump_open,&s_clear_confirm,&s_batch_confirm,&s_scan_pending};
    for(size_t i=0;i<sizeof(guards)/sizeof(*guards);i++){*guards[i]=true;assert(app_book_external_turn(&ctx,1)==APP_REDRAW_NONE);*guards[i]=false;}
    for(int view=SHELF;view<=SEARCH;view++)if(view!=READING){s_view=view;assert(app_book_external_turn(&ctx,1)==APP_REDRAW_NONE);}s_view=READING;
    s_product_session=false;assert(app_book_external_turn(&ctx,1)==APP_REDRAW_NONE);s_product_session=true;
    s_text=NULL;assert(app_book_external_turn(&ctx,1)==APP_REDRAW_NONE);s_text="loaded prose";
    strcpy(s_open_path,"pending");assert(app_book_external_turn(&ctx,1)==APP_REDRAW_NONE);s_open_path[0]=0;
    strcpy(s_resume_path,"pending");assert(app_book_external_turn(&ctx,1)==APP_REDRAW_NONE);s_resume_path[0]=0;assert(turns==(int)before);
}
static void test_dual_external_turn(void){
    app_ctx_t ctx={0};dual_enabled=true;dual_accept=true;
    assert(app_book_external_turn(&ctx,1)==APP_REDRAW_DONE&&last_direction==1);
    assert(app_book_external_turn(&ctx,-1)==APP_REDRAW_DONE&&last_direction==-1);
    dual_accept=false;assert(app_book_external_turn(&ctx,1)==APP_REDRAW_NONE);
    dual_accept=true;unsigned before=turns;s_dual_target=3;
    assert(app_book_external_turn(&ctx,1)==APP_REDRAW_NONE&&turns==(int)before);s_dual_target=0;
    s_dual_anchor.pending=true;assert(app_book_external_turn(&ctx,1)==APP_REDRAW_NONE&&turns==(int)before);s_dual_anchor.pending=false;
    dual_enabled=false;
}
static void blocked_input(app_ctx_t *ctx,bool blocked){app_redraw_t redraw=APP_REDRAW_NONE;int before=turns;unsigned polls=polled;event_direction=1;assert(!ps_bluetooth_input(ctx,&app_book,blocked,&redraw));assert(turns==before&&event_direction==0&&polled==polls+1);}
static void test_input_routing(void){
    app_ctx_t ctx={0};app_redraw_t redraw=APP_REDRAW_NONE;test_route=PS_SHELF;
    int before=turns;event_direction=1;assert(ps_bluetooth_input(&ctx,&app_book,false,&redraw)&&redraw==APP_REDRAW_PAGE&&turns==before+1);
    blocked_input(&ctx,true);ctx.consumed=true;blocked_input(&ctx,false);ctx.consumed=false;
    ctx.pressed=true;blocked_input(&ctx,false);ctx.pressed=false;ctx.released=true;blocked_input(&ctx,false);ctx.released=false;
    touch_t touch={.touched=true};ctx.touch=&touch;blocked_input(&ctx,false);ctx.touch=NULL;
    storage_busy=true;blocked_input(&ctx,false);storage_busy=false;usb_busy=true;blocked_input(&ctx,false);usb_busy=false;
    ctx.request_app=&app_pico_system;blocked_input(&ctx,false);ctx.request_app=NULL;
    ctx.request_return=true;blocked_input(&ctx,false);ctx.request_return=false;ctx.request_menu=true;blocked_input(&ctx,false);ctx.request_menu=false;ctx.request_developer_menu=true;blocked_input(&ctx,false);ctx.request_developer_menu=false;
    before=turns;unsigned keys=reader_keys;
    for(int route=0;route<PS_ROUTE_COUNT;route++)if(route!=PS_READ){test_route=(ps_route_t)route;event_direction=1;assert(!ps_bluetooth_input(&ctx,&app_pico_system,false,&redraw));}
    assert(turns==before&&reader_keys==keys);
    event_direction=1;assert(!ps_bluetooth_input(&ctx,&other_app,false,&redraw));assert(turns==before);
    event_direction=1;assert(!ps_bluetooth_input(NULL,&app_book,false,&redraw));event_direction=1;assert(!ps_bluetooth_input(&ctx,NULL,false,&redraw));
    event_direction=1;assert(!ps_bluetooth_input(&ctx,&app_book,false,NULL));assert(turns==before);
}
int main(void){test_geometry_and_controls();test_page_clamping();test_external_turn_guards();test_dual_external_turn();test_input_routing();printf("PASS: Bluetooth controls/list geometry both orientations, paging clamp, deferred scan, modal/selection/TOC and queued-input guards (%u paint operations).\n",render_operations);}
'''


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--sanitize', action='store_true')
    args = parser.parse_args()
    header = (ROOT / 'main/system/pico_system.h').read_text()
    types = '\n'.join(re.findall(r'typedef enum\s*\{[^}]*\}\s*\w+;', header, re.S))
    types += '\n' + '\n'.join(re.findall(r'typedef struct\s*\{[^}]*\}\s*ps_(?:event|view)_t;', header, re.S))
    ble = (ROOT / 'components/read_pico_ble_pager/include/read_pico_ble_pager.h').read_text()
    ble_types = '\n'.join(re.findall(r'#define READ_PICO_BLE_PAGER_[^\n]+', ble))
    ble_types += '\n' + '\n'.join(re.findall(r'typedef struct\s*\{[^}]*\}\s*read_pico_ble_pager_\w+;', ble, re.S))
    ui = re.sub(r'^#include[^\n]+\n', '', (ROOT / 'main/system/pico_bluetooth.c').read_text(), flags=re.M)
    book = function((ROOT / 'main/apps/app_book.c').read_text(), 'app_book_external_turn')
    unit = PRELUDE + '\n' + types + '\n' + ble_types + '\n' + STUBS + '\n' + book + '\n' + ui + '\n' + TESTS
    with tempfile.TemporaryDirectory(prefix='pico-ble-ui-') as temp:
        path, binary = Path(temp) / 'test.c', Path(temp) / 'test'
        path.write_text(unit)
        cmd = [os.environ.get('CC', 'cc'), '-std=gnu11', '-Wall', '-Wextra', '-Werror', '-g']
        if args.sanitize:
            cmd += ['-fsanitize=address,undefined', '-fno-omit-frame-pointer']
        subprocess.run(cmd + [str(path), '-o', str(binary)], check=True)
        subprocess.run([str(binary)], check=True)


if __name__ == '__main__':
    main()

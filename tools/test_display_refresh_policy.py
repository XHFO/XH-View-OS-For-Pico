#!/usr/bin/env python3
"""SPDX-License-Identifier: Apache-2.0
中文：真实显示出口的跟随刷新与周期清屏隔离回归，仅替换硬件接口。
English: Test real display policy with hardware doubles; motion must not trigger periodic clears.
"""
from pathlib import Path
import re
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
source = (ROOT / 'main/display.c').read_text()
source = re.sub(r'^#include "[^"\n]+"\s*$', '', source, flags=re.M)
epd = (ROOT / 'components/epdiy/src/epdiy.h').read_text()
modes = re.search(r'enum EpdDrawMode\s*\{[^}]*\};', epd).group()
every = re.search(r'^#define APP_GC16_EVERY\s+(\d+)', (ROOT / 'main/app/app_config.h').read_text(), re.M).group(1)
prelude = r'''
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <assert.h>
#include <stdio.h>
typedef struct { int value; uint8_t *front_fb,*back_fb; } EpdiyHighlevelState;
typedef struct { int value; } EpdWaveform;
typedef struct { int x,y,width,height; } EpdRect;
enum EpdDrawError { EPD_DRAW_SUCCESS=0, EPD_DRAW_EMPTY_LINE_QUEUE=512 };
enum EpdDrawError update_display_full(EpdiyHighlevelState *h);
enum EpdDrawError update_display_area_with(EpdiyHighlevelState*,const EpdWaveform*,enum EpdDrawMode,EpdRect);
static EpdWaveform E0470_WAVEFORM={0},E0470_FOLLOW_WAVEFORM={1};
enum { READ_PICO_EPD_SCAN_FAST=1, READ_PICO_EPD_SCAN_FULL=2 };
#define DISPLAY_PCLK_DEFAULT_MHZ 18
#define DISPLAY_PCLK_SAFE_MHZ 12
#define ESP_LOGI(tag,...) ((void)(tag))
#define ESP_LOGW(tag,...) ((void)(tag))
static unsigned checks;
static int last_mode,last_full,last_area,last_scan,last_prefill,pushes,powerons;
static EpdRect last_rect;
static int epd_width(void){return 1216;}
static int epd_height(void){return 684;}
static enum EpdRotation rotation;
static enum EpdRotation epd_get_rotation(void){return rotation;}
static const EpdWaveform *last_wave;
static int64_t now;
#define CHECK(x) do { ++checks; assert(x); } while(0)
static int64_t esp_timer_get_time(void){return now;}
static void read_pico_epd_use_scan(int scan){last_scan=scan;}
static void read_pico_epd_set_pclk(int mhz){(void)mhz;}
static void epd_lcd_set_prefill_lines(int lines){last_prefill=lines;}
static void epd_poweron(void){powerons++;}
static void epd_poweroff(void){}
static void epd_clear(void){}
static void epd_hl_set_all_white(EpdiyHighlevelState *h){(void)h;}
static void epd_hl_waveform(EpdiyHighlevelState *h,const EpdWaveform *w){(void)h;last_wave=w;}
static enum EpdDrawError record(enum EpdDrawMode m,int full,int area){pushes++;last_mode=m;last_full=full;last_area=area;return EPD_DRAW_SUCCESS;}
static enum EpdDrawError epd_hl_update_screen(EpdiyHighlevelState*h,enum EpdDrawMode m,int t){(void)h;(void)t;return record(m,0,0);}
static enum EpdDrawError epd_hl_update_screen_full(EpdiyHighlevelState*h,enum EpdDrawMode m,int t){(void)h;(void)t;return record(m,1,0);}
static enum EpdDrawError epd_hl_update_area(EpdiyHighlevelState*h,enum EpdDrawMode m,int t,EpdRect a){(void)h;(void)t;last_rect=a;return record(m,0,1);}
static enum EpdDrawError epd_hl_update_area_full(EpdiyHighlevelState*h,enum EpdDrawMode m,int t,EpdRect a){(void)h;(void)t;last_rect=a;return record(m,1,1);}
static enum EpdDrawError epd_hl_update_screen_from_white(EpdiyHighlevelState*h,enum EpdDrawMode m,int t){(void)h;(void)t;return record(m,1,0);}
'''
tests = r'''
int main(void){
 EpdiyHighlevelState h={0}; EpdRect area={0,0,100,100};
 CHECK(APP_GC16_EVERY==14);
 update_display_full(&h); CHECK(last_mode==MODE_GC16 && last_full);
 for(int i=0;i<7;i++){update_display_mode(&h,MODE_DU);CHECK(last_mode==MODE_DU && !last_full);}
 for(int i=0;i<300;i++){
  update_display_with(&h,&E0470_FOLLOW_WAVEFORM,MODE_DU);
  CHECK(last_mode==MODE_DU && !last_full && !last_area);
  CHECK(last_scan==READ_PICO_EPD_SCAN_FAST && last_prefill==64);
  CHECK(last_wave==&E0470_WAVEFORM);
 }
 for(int i=0;i<100;i++){
  update_display_area_with(&h,&E0470_FOLLOW_WAVEFORM,MODE_DU,area);
  CHECK(last_mode==MODE_DU && !last_full && last_area);
 }
 for(int i=0;i<6;i++){update_display_mode(&h,MODE_DU);CHECK(last_mode==MODE_DU);}
 update_display_mode(&h,MODE_DU);CHECK(last_mode==MODE_GC16 && last_full);
 update_display_mode(&h,MODE_GL16);CHECK(last_mode==MODE_GL16 && last_full);
 CHECK(last_scan==READ_PICO_EPD_SCAN_FULL && last_prefill==32);
 update_display_full(&h);
 for(int i=0;i<13;i++){update_display_mode(&h,MODE_GL16);CHECK(last_mode==MODE_GL16);}
 update_display_mode(&h,MODE_GL16);CHECK(last_mode==MODE_GC16 && last_full);
 // 中文：四种转向下保持实际变化包围盒；相同画面不耗刷新计数。
 // English: All rotations retain the actual changed bounds; identical frames do not consume refresh cadence.
 static uint8_t front[1216*684/2],back[1216*684/2];
 h.front_fb=front;h.back_fb=back;
 for(int turn=0;turn<4;turn++){
  rotation=(enum EpdRotation)turn;memset(front,255,sizeof(front));memset(back,255,sizeof(back));
  update_display_full(&h);int before=pushes,rail_before=powerons;
  for(int repeat=0;repeat<30;repeat++)CHECK(update_display_changed_mode(&h,MODE_GL16)==EPD_DRAW_SUCCESS);
  CHECK(pushes==before&&powerons==rail_before);
  front[200*608+150]=0xf0;front[400*608+400]=0x0f;
  EpdRect expected[4]={{300,200,502,201},{200,414,201,502},{414,283,502,201},{283,300,201,502}};
  update_display_changed_mode(&h,MODE_GL16);
  CHECK(last_area&&last_full&&last_mode==MODE_GL16);
  CHECK(!memcmp(&last_rect,&expected[turn],sizeof(EpdRect)));
  for(int repeat=0;repeat<12;repeat++)update_display_changed_mode(&h,MODE_GL16);
  CHECK(last_mode==MODE_GL16&&last_area);
  update_display_changed_mode(&h,MODE_GL16);
  CHECK(last_mode==MODE_GC16&&last_full&&last_area);
  CHECK(!memcmp(&last_rect,&expected[turn],sizeof(EpdRect)));
  memset(front,255,sizeof(front));front[0]=0xf0;front[sizeof(front)-1]=0x0f;
  update_display_changed_mode(&h,MODE_GL16);CHECK(last_area&&last_mode==MODE_GL16);
  EpdRect whole=turn%2?(EpdRect){0,0,684,1216}:(EpdRect){0,0,1216,684};
  CHECK(!memcmp(&last_rect,&whole,sizeof(EpdRect)));
 }
 printf("display refresh policy: %u checks PASS; 400 motion frames without periodic GC16, normal cleanup preserved\n",checks);
 return 0;
}
'''
with tempfile.TemporaryDirectory(prefix='pico-refresh-policy-') as directory:
    work = Path(directory)
    code = work / 'display-test.c'
    rotation = re.search(r'enum EpdRotation\s*\{[^}]*\};', epd).group()
    code.write_text(modes + '\n' + rotation + '\n#define APP_GC16_EVERY ' + every + '\n' + prelude + '\n' + source + '\n' + tests)
    exe = work / 'test'
    subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-O1','-g','-fsanitize=address,undefined','-fno-omit-frame-pointer',str(code),'-o',str(exe)],check=True)
    subprocess.run([str(exe)],check=True)

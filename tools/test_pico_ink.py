#!/usr/bin/env python3
"""中文：真实连续DU与共享笔迹推屏的四方向、排空、中断与错误回归，不访问设备。
English: Real continuous-DU/presenter rotation, draining, interruption and fault tests, without hardware.
"""
from pathlib import Path
import subprocess,tempfile
from render_pico_reader import ROOT,extract_function

with tempfile.TemporaryDirectory(prefix='pico-ink-') as td:
    d=Path(td)
    def put(n,s): (d/n).write_text(s)
    put('epd_waveform.h',(ROOT/'components/epdiy/include/epd_waveform.h').read_text())
    put('epdiy.h',r'''#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "epd_waveform.h"
typedef struct {int x,y,width,height;} EpdRect;
enum EpdRotation {EPD_ROT_LANDSCAPE,EPD_ROT_PORTRAIT,EPD_ROT_INVERTED_LANDSCAPE,EPD_ROT_INVERTED_PORTRAIT};
enum EpdDrawError {EPD_DRAW_SUCCESS=0,EPD_DRAW_ERROR=1};
enum EpdDrawMode {MODE_DU=1,MODE_PACKING_1PPB_DIFFERENCE=0x100};
int epd_width(void);int epd_height(void);enum EpdRotation epd_get_rotation(void);EpdRect epd_full_screen(void);void epd_poweron(void);
enum EpdDrawError epd_draw_base(EpdRect,const uint8_t*,EpdRect,enum EpdDrawMode,int,const bool*,const uint8_t*,const EpdWaveform*);
''')
    put('epd_highlevel.h','#pragma once\n#include "epdiy.h"\ntypedef struct {uint8_t *front_fb,*back_fb,*difference_fb,*dirty_columns;bool *dirty_lines;} EpdiyHighlevelState;\n')
    put('app.h','#pragma once\n#include "epd_highlevel.h"\ntypedef struct {EpdiyHighlevelState *hl;uint8_t *fb;} app_ctx_t;\n')
    put('display.h','#pragma once\n#include "app.h"\nvoid rails_keepalive(void);enum EpdDrawError update_display_area_with(EpdiyHighlevelState*,const EpdWaveform*,enum EpdDrawMode,EpdRect);\n')
    put('ui_kit.h','#pragma once\n#include "app.h"\nEpdRect ui_rotate_rect_to_fb(EpdRect);\n')
    put('read_pico_epd_timing.h','#pragma once\nenum {READ_PICO_EPD_SCAN_FAST};\nvoid read_pico_epd_use_scan(int);\n')
    put('esp_heap_caps.h','#pragma once\n#include <stdlib.h>\n#define MALLOC_CAP_SPIRAM 1\n#define MALLOC_CAP_8BIT 2\nvoid *heap_caps_calloc(size_t,size_t,unsigned);void heap_caps_free(void*);\n')
    put('esp_log.h','#pragma once\n#define ESP_LOGE(...) ((void)TAG)\n')
    put('e0470_epaper_waveform.h','#pragma once\n#include "epd_waveform.h"\nextern const EpdWaveform E0470_FOLLOW_WAVEFORM;void e0470_follow_lut_build(int,uint8_t (*)[16][4]);\n')
    harness=r'''
#include "pico_ink.h"
#include "continuous_du.h"
#include "ui_kit.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
static unsigned checks,scans,drives,rails,powers,follow;
static bool oom,fail;
static enum EpdRotation rotation;
static int8_t physical[1216*684];
const EpdWaveform E0470_FOLLOW_WAVEFORM={0};
#define C(c) do{checks++;assert(c);}while(0)
int epd_width(void){return 1216;}int epd_height(void){return 684;}
enum EpdRotation epd_get_rotation(void){return rotation;}
EpdRect epd_full_screen(void){return (EpdRect){0,0,1216,684};}
void *heap_caps_calloc(size_t a,size_t b,unsigned caps){(void)caps;return oom?NULL:calloc(a,b);}
void heap_caps_free(void *p){free(p);}
void e0470_follow_lut_build(int n,uint8_t (*p)[16][4]){C(n==1);memset(p,0,64);}
void rails_keepalive(void){rails++;}void epd_poweron(void){powers++;}
void read_pico_epd_use_scan(int mode){C(mode==0);}
enum EpdDrawError update_display_area_with(EpdiyHighlevelState *h,const EpdWaveform *w,enum EpdDrawMode m,EpdRect r){(void)h;(void)r;C(w==&E0470_FOLLOW_WAVEFORM&&m==MODE_DU);follow++;return fail?EPD_DRAW_ERROR:EPD_DRAW_SUCCESS;}
enum EpdDrawError epd_draw_base(EpdRect area,const uint8_t *data,EpdRect crop,enum EpdDrawMode mode,int temp,const bool *lines,const uint8_t *columns,const EpdWaveform *w){
 (void)area;(void)crop;C(temp==25);C(mode==(MODE_DU|MODE_PACKING_1PPB_DIFFERENCE));C(w->mode_data[0]->range_data[0]->phases==1);scans++;if(fail)return EPD_DRAW_ERROR;
 for(int y=0;y<684;y++)if(lines[y])for(int x=0;x<1216;x++)if(columns[x/2]&(x&1?0xf0:0x0f)){
  int code=data[y*1216+x];if(code!=0x88){C(code==0x0f||code==0xf0);physical[y*1216+x]+=(code==0x0f?1:-1);drives++;}
 }
 return EPD_DRAW_SUCCESS;
}
'''
    geometry=(ROOT/'main/ui/ui_kit.c').read_text()
    start=geometry.index('EpdRect ui_rotate_rect_to_fb(')
    geometry=geometry[start:geometry.index('\n}\n',start)+3]
    harness+='\n'+geometry+r'''
static uint8_t front[1216*684/2],back[sizeof front],diff[1216*684],columns[608];static bool lines[684];
static EpdiyHighlevelState hl={front,back,diff,columns,lines},other;
static app_ctx_t ctx={&hl,front};
static void pixel(int x,int y,int value){size_t at=(size_t)y*608+x/2;int s=(x&1)*4;front[at]=(front[at]&~(15<<s))|(value<<s);}
static void drain(void){unsigned start=scans,steps=0;while(ps_ink_pending()){C(ps_ink_step(&ctx));C(scans==start+(++steps));C(steps<8);}C(!ps_ink_cancel());}
int main(void){
 for(int rot=0;rot<4;rot++){
  rotation=(enum EpdRotation)rot;memset(front,255,sizeof front);memset(back,255,sizeof back);memset(physical,0,sizeof physical);
  EpdRect logical={11,20,17,19},r=ui_rotate_rect_to_fb(logical);int x=r.x+3,y=r.y+4;
  pixel(x,y,0);unsigned start=scans;C(ps_ink_present(&ctx,logical));C(scans==start+1);C(physical[y*1216+x]==1);C(ps_ink_pending());
  C(!memcmp(front,back,sizeof front));
  C(ps_ink_present(&ctx,logical));C(scans==start+2);drain();C(scans==start+6);C(physical[y*1216+x]==6);
  C(ps_ink_present(&ctx,logical));C(!ps_ink_pending());C(scans==start+6);
  pixel(x,y,15);C(ps_ink_present(&ctx,logical));drain();C(physical[y*1216+x]==0);
  pixel(x,y,0);C(ps_ink_present(&ctx,logical));C(ps_ink_cancel());C(!ps_ink_pending());C(physical[y*1216+x]==1);
  memcpy(back,front,sizeof back);memset(physical,0,sizeof physical);
  pixel(x,y,15);C(ps_ink_present(&ctx,logical));pixel(x,y,0);C(ps_ink_present(&ctx,logical));drain();C(physical[y*1216+x]==5);
  pixel(x,y,15);C(ps_ink_present(&ctx,logical));rotation=(enum EpdRotation)((rot+1)%4);start=scans;C(!ps_ink_step(&ctx));C(scans==start);C(ps_ink_cancel());rotation=(enum EpdRotation)rot;
  memcpy(back,front,sizeof back);pixel(x,y,0);C(ps_ink_present(&ctx,logical));ctx.hl=&other;C(!ps_ink_step(&ctx));C(!ps_ink_present(&ctx,logical));ctx.hl=&hl;C(ps_ink_cancel());
  memcpy(back,front,sizeof back);pixel(x,y,15);fail=true;C(!ps_ink_present(&ctx,logical));C(ps_ink_cancel());fail=false;C(!ps_ink_pending());
 }
 continuous_du_deinit();oom=true;unsigned before=follow;C(ps_ink_present(&ctx,(EpdRect){10,10,20,20}));C(follow==before+1);C(!ps_ink_pending());fail=true;C(!ps_ink_present(&ctx,(EpdRect){10,10,20,20}));fail=false;oom=false;
 C(rails==powers);printf("PASS ink/actual continuous DU: %u checks, %u one-phase scans, %u pixel drives; all rotations, drain, reversal, interrupt, error and OOM\n",checks,scans,drives);return 0;
}
'''
    put('test.c',harness)
    exe=d/'ink-test'
    subprocess.run(['cc','-std=c11','-O1','-g','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-fno-omit-frame-pointer','-I',str(d),'-I',str(ROOT/'main/system'),'-I',str(ROOT/'components/continuous_du/include'),str(ROOT/'main/system/pico_ink.c'),str(ROOT/'components/continuous_du/continuous_du.c'),str(d/'test.c'),'-o',str(exe)],check=True)
    subprocess.run([str(exe)],check=True)

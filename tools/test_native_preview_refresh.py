#!/usr/bin/env python3
"""网页原生推屏边界统计，不模拟设备刷新性能。 / Native display-boundary counters, not device timing simulation."""
from pathlib import Path
import re
import subprocess
import tempfile
from render_pico_reader import ROOT, extract_function

source = re.sub(r'^#include "[^"\n]+"\s*$', '', (ROOT / 'tools/native_preview_framebuffer.c').read_text(), flags=re.M)
epd = (ROOT / 'components/epdiy/src/epdiy.h').read_text()
modes = re.search(r'enum EpdDrawMode\s*\{[^}]*\};', epd).group()
rotations = re.search(r'enum EpdRotation\s*\{[^}]*\};', epd).group()
prelude = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef struct {int x,y,width,height;} EpdRect;
typedef struct {uint8_t *front_fb,*back_fb;} EpdiyHighlevelState;
typedef struct {int value;} EpdWaveform;
enum EpdDrawError {EPD_DRAW_SUCCESS=0};
#define CONTINUOUS_DARK_PHASES 6
static EpdRect ui_rotate_rect_to_fb(EpdRect r){return r;}
static void epd_fill_circle_helper(int x,int y,int r,int corners,int delta,uint8_t color,uint8_t *fb){(void)x;(void)y;(void)r;(void)corners;(void)delta;(void)color;(void)fb;}
'''
tests = r'''
static unsigned checks;
#define CHECK(c) do {checks++;assert(c);}while(0)
static void counts(unsigned follow,unsigned standard,unsigned gc16){unsigned f,d,g;native_preview_refresh_counts(&f,&d,&g);CHECK(f==follow&&d==standard&&g==gc16);CHECK(native_preview_present_count()==f+d+native_preview_continuous_count());}
int main(void){
 static uint8_t frame[1216u*684u/2u];EpdiyHighlevelState hl={0};EpdRect area={30,40,60,70};
 epd_set_rotation(EPD_ROT_LANDSCAPE);native_preview_set_frame(frame);memset(frame,255,sizeof(frame));
 counts(0,0,0);update_display_mode(&hl,MODE_GL16);counts(0,1,0);CHECK(!strcmp(native_preview_refresh_waveform(),"DEFAULT"));
 for(int i=0;i<32;i++){frame[4]^=15;update_display_with(&hl,&E0470_FOLLOW_WAVEFORM,MODE_DU);counts((unsigned)i+1,1,0);CHECK(!strcmp(native_preview_refresh_waveform(),"FOLLOW"));}
 update_display_area_with(&hl,&E0470_FOLLOW_WAVEFORM,MODE_DU,area);counts(33,1,0);
 EpdRect last;int mode;bool partial;native_preview_last_refresh(&last,&mode,&partial);CHECK(partial&&mode==MODE_DU&&!memcmp(&area,&last,sizeof(last)));
 update_display_with(&hl,&E0470_WAVEFORM,MODE_GC16);counts(33,2,1);CHECK(!strcmp(native_preview_refresh_waveform(),"DEFAULT"));
 update_display_from_white_with(&hl,&E0470_FOLLOW_WAVEFORM,MODE_DU);counts(34,2,1);CHECK(!strcmp(native_preview_refresh_waveform(),"FOLLOW"));
 update_display_from_white(&hl);counts(34,3,2);update_display_white(&hl);counts(34,4,3);update_display_full(&hl);counts(34,5,4);
 unsigned before=native_preview_present_count();
 for(int i=0;i<40;i++)CHECK(update_display_changed_mode(&hl,MODE_GL16)==EPD_DRAW_SUCCESS);
 CHECK(native_preview_present_count()==before);counts(34,5,4);
 frame[200u*608u+150u]=0xf0;update_display_changed_mode(&hl,MODE_GL16);counts(34,6,4);
 native_preview_last_refresh(&last,&mode,&partial);CHECK(partial&&mode==MODE_GL16);CHECK(!strcmp(native_preview_refresh_waveform(),"DEFAULT"));
 before=native_preview_present_count();for(int i=0;i<40;i++)CHECK(update_display_changed_mode(&hl,MODE_GL16)==EPD_DRAW_SUCCESS);CHECK(native_preview_present_count()==before);
 update_display_area_with(&hl,&E0470_WAVEFORM,(enum EpdDrawMode)(MODE_GC16|0x100),area);counts(34,7,5);
 for(int i=0;i<4;i++){epd_set_rotation((enum EpdRotation)i);update_display_full(&hl);before=native_preview_present_count();for(int n=0;n<16;n++)CHECK(update_display_changed_mode(&hl,MODE_GL16)==EPD_DRAW_SUCCESS);CHECK(native_preview_present_count()==before);}
 counts(34,11,9);
 continuous_du_init();CHECK(!continuous_du_busy());frame[10]^=15;
 continuous_du_mark_diff(frame,hl.back_fb,(EpdRect){0,0,32,1},continuous_du_dark_phases(),false);CHECK(continuous_du_busy());
 for(unsigned n=0;n<6;n++){continuous_du_scan(&hl,(EpdRect){0,0,32,1});CHECK(native_preview_continuous_count()==n+1);CHECK(!strcmp(native_preview_refresh_waveform(),"CONTINUOUS"));counts(34,11,9);}
 CHECK(!continuous_du_busy());continuous_du_scan(&hl,(EpdRect){0,0,32,1});CHECK(native_preview_continuous_count()==6);
 continuous_du_reset();CHECK(!continuous_du_busy());
 printf("PASS: native refresh telemetry %u checks; 34 FOLLOW requests and unchanged-frame accounting verified.\n",checks);return 0;
}
'''
with tempfile.TemporaryDirectory(prefix='pico-native-refresh-') as directory:
    work = Path(directory)
    code = work / 'test.c'
    code.write_text(prelude + '\n' + modes + '\n' + rotations + '\n' + source + '\n' + extract_function((ROOT / 'main/display.c').read_text(), 'update_display_changed_mode') + '\n' + tests)
    exe = work / 'test'
    subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra', '-Werror', '-O1', '-g', '-fsanitize=address,undefined', '-fno-omit-frame-pointer', str(code), '-o', str(exe)], check=True)
    subprocess.run([str(exe)], check=True)

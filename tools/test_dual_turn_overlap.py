#!/usr/bin/env python3
"""Run the actual scan loop against a deterministic panel clock; no physical waveform proof."""
import ast,re,subprocess,tempfile
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
t=ast.parse((ROOT/'tools/test_dual_control_flush.py').read_text());f=next(n for n in t.body if isinstance(n,ast.FunctionDef) and n.name=='function');ns={'re':re};exec(compile(ast.Module(body=[f],type_ignores=[]),'<extractor>','exec'),ns)
source=r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>
typedef struct {int x,y,width,height;}EpdRect;
typedef struct {uint8_t*front_fb,*back_fb,*difference_fb;bool*dirty_lines;uint8_t*dirty_columns;}EpdiyHighlevelState;
typedef struct {const uint8_t*luts;int phases;const int*phase_times;}EpdWaveformPhases;
typedef struct {int unused;}EpdWaveform;
typedef enum {E0470_TURN_LTR,E0470_TURN_RTL,E0470_TURN_TTB,E0470_TURN_BTT}e0470_turn_dir_t;
enum EpdDrawError {EPD_DRAW_SUCCESS,EPD_DRAW_NO_PHASES_AVAILABLE,EPD_DRAW_INVALID_CROP};
#define TURN_BANDS_MAX 32
#define TURN_PHASE_CAP 64
#define TURN_LINE_MAX 128
#define MODE_GL16 5
#define MODE_DU 1
#define MODE_PACKING_1PPB_DIFFERENCE 8
#define ESP_LOGI(...) ((void)0)
#define ESP_LOG_INFO 3
static int s_tick_us=11090,s_band0[32],s_band1[32];static int8_t s_band_phase[32],s_line_phase[128];
static const uint8_t*s_lut_ptr[64];static void(*s_relay)(void);
static const EpdWaveform E0470_APPLY_WAVEFORM;
static const uint8_t dummy=1;static EpdWaveformPhases phases={&dummy,41,NULL};
static int64_t now,hook_at;static int scans,copies,hooks,scan_us,fail_at;static bool line_mode,valid=true;
static int64_t esp_timer_get_time(void){return now;}
static void esp_rom_delay_us(uint32_t us){now+=us;}
static const EpdWaveformPhases*e0470_waveform_phases(const EpdWaveform*w,int mode){(void)w;assert(mode==5);return valid?&phases:NULL;}
static int epd_width(void){return 128;}static int epd_height(void){return 128;}
static EpdRect rotate_to_fb(EpdRect r){return r;}static void clip_rect(EpdRect*r,int w,int h){(void)r;(void)w;(void)h;}
static bool dir_is_lr(e0470_turn_dir_t d){(void)d;return true;}static bool dir_is_reverse(e0470_turn_dir_t d){return d==E0470_TURN_RTL;}
static bool uses_line_phase(e0470_turn_dir_t d){(void)d;return line_mode;}
static void assign_bands(int*a,int*b,int bands,int lo,int hi,int step,bool cols,int origin,int span,bool lr,bool reverse){(void)step;(void)cols;(void)origin;(void)span;(void)lr;(void)reverse;for(int i=0;i<bands;i++){a[i]=lo+(hi-lo)*i/bands;b[i]=lo+(hi-lo)*(i+1)/bands;}}
static bool build_luts(const EpdWaveformPhases*p){assert(p==&phases);return true;}
static void epd_poweron(void){}
static void epd_difference_image_cropped(uint8_t*a,uint8_t*b,EpdRect r,uint8_t*c,bool*d,uint8_t*e){(void)a;(void)b;(void)r;(void)c;(void)d;(void)e;}
static void epd_set_line_phase_luts(const uint8_t**p,int8_t*q){(void)p;(void)q;}
static void epd_set_col_phase_luts(const uint8_t**p,int*a,int*b,int8_t*c,int n){(void)p;(void)a;(void)b;(void)c;assert(n==8);}
static void epd_clear_phase_luts(void){}
static EpdRect epd_full_screen(void){return(EpdRect){0,0,128,128};}
static enum EpdDrawError epd_draw_base(EpdRect a,uint8_t*b,EpdRect c,int mode,int temp,bool*d,uint8_t*e,const EpdWaveform*w){(void)a;(void)b;(void)c;(void)d;(void)e;assert(mode==9&&temp==25&&w==&E0470_APPLY_WAVEFORM);scans++;now+=scan_us;return scans==fail_at?EPD_DRAW_INVALID_CROP:EPD_DRAW_SUCCESS;}
static void copy_front_to_back(EpdiyHighlevelState*h,EpdRect r){(void)h;(void)r;copies++;}
static void relay(void){assert(scans>0);hooks++;hook_at=now;}
'''
body=(ROOT/'components/e0470_page_turn/e0470_page_turn.c').read_text()
source+='\n'+ns['function'](body,'e0470_page_turn_set_relay')+'\n'+ns['function'](body,'page_turn_run')
source+=r'''
static void reset(void){now=hook_at=scans=copies=hooks=fail_at=0;valid=true;}
int main(void){
 uint8_t a[1],b[1],diff[1],cols[64];bool lines[128];EpdiyHighlevelState hl={a,b,diff,lines,cols};EpdRect area={0,0,128,128};EpdWaveform wave={0};
 for(int mode=0;mode<2;mode++)for(int us=11090;us<=30000;us+=18910){
  reset();line_mode=mode;scan_us=us;e0470_page_turn_set_relay(relay);
  assert(page_turn_run(&hl,area,E0470_TURN_RTL,&wave,8,true)==0);
  assert(scans==48&&copies==1&&hooks==1&&hook_at>0&&now-hook_at<=500000&&now-hook_at>500000-us);
  assert(!s_relay);assert(page_turn_run(&hl,area,E0470_TURN_LTR,&wave,8,true)==0&&hooks==1&&scans==96);
 }
 reset();scan_us=30000;fail_at=5;e0470_page_turn_set_relay(relay);assert(page_turn_run(&hl,area,0,&wave,8,true)!=0&&!hooks&&!copies);
 reset();fail_at=42;e0470_page_turn_set_relay(relay);assert(page_turn_run(&hl,area,0,&wave,8,true)!=0&&hooks==1&&!copies);
 reset();valid=false;e0470_page_turn_set_relay(relay);assert(page_turn_run(&hl,area,0,&wave,8,true)!=0&&!hooks&&!scans&&!s_relay);
 valid=true;e0470_page_turn_set_relay(relay);assert(page_turn_run(NULL,area,0,&wave,8,true)!=0&&!s_relay);
 puts("PASS: actual48-tick loop, both phase mappings, fast/slow scans, remaining500ms one-shot, pre/late failure and invalid input without fake completion; ASan/UBSan (no physical panel)");
}
'''
with tempfile.TemporaryDirectory(prefix='turn-overlap-') as d:
 p=Path(d);(p/'test.c').write_text(source)
 for level in (2,3):
  subprocess.run(['cc','-std=gnu11','-O1','-g','-fsanitize=address,undefined','-DLOG_LOCAL_LEVEL='+str(level),str(p/'test.c'),'-o',str(p/'test')],check=True);subprocess.run([str(p/'test')],check=True)

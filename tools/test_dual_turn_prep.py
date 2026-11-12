#!/usr/bin/env python3
"""Exercise production prefetch scheduling and deferred session checkpoints with sanitizers."""
import argparse
import ast
from pathlib import Path
import re
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
p = argparse.ArgumentParser()
p.add_argument('--book-source', type=Path, default=ROOT / 'main/apps/app_book.c')
p.add_argument('--dual-source', type=Path, default=ROOT / 'main/system/pico_dual.c')
a = p.parse_args()
# Share the source-function extractor without executing that test's main body.
tree = ast.parse((ROOT / 'tools/test_dual_control_flush.py').read_text())
extractor = next(n for n in tree.body if isinstance(n, ast.FunctionDef) and n.name == 'function')
namespace = {'re': re}
exec(compile(ast.Module(body=[extractor], type_ignores=[]), '<extractor>', 'exec'), namespace)
function = namespace['function']
source = r'''
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#define READING 1
static bool paired,s_single_reading,s_dual_solo;
static int s_view=READING;
static bool s_image_open,s_image_confirm_open,s_toolbar,s_selection,s_scrubbing;
static bool s_progress_pending,s_page_jump_open,s_clear_confirm;
static char s_open_path[8];static unsigned s_dual_target;
static struct {bool pending;}s_dual_anchor;
static void *s_prep_task=(void*)1,*s_prep_done=(void*)2;
static size_t s_page,pages=30;static int s_next_page=-1,s_prep_page=-1;
static unsigned notifications;
static bool s_turn_anim_pending;
static uint8_t frame[1],*s_next_fb=frame;
static unsigned staged;
static void ensure_prep(void){}
static void draw_reader(uint8_t* fb,size_t page){assert(fb==frame&&page==s_page);staged++;}
static bool ps_dual_enabled(void){return paired;}
static size_t book_layout_page_count(void){return pages;}
static int xSemaphoreTake(void*p,unsigned ticks){assert(p==s_prep_done&&!ticks);return 0;}
static void xTaskNotifyGive(void*p){assert(p==s_prep_task);notifications++;s_next_page=s_prep_page;}
static struct {unsigned role;}settings={1};
static struct {struct {uint32_t revision;bool reflow_pending;} reading;} status;
static void *dual_session=(void*)3;
static uint32_t stored,checkpoint_revision;
static int64_t checkpoint_changed;
static bool fail,has_path=true;
static unsigned writes;
static uint32_t durable_revision;
static const char* dl_reading_path(void*p){assert(p==dual_session);return has_path?"book":NULL;}
static bool ps_store(const char*key,const void*data,size_t len){
 assert(!strcmp(key,"dualreading2")&&data==&status.reading&&len==sizeof(status.reading));
 writes++;if(fail)return false;durable_revision=status.reading.revision;return true;
}
'''
source += r"""
typedef struct {int x,y,width,height;} EpdRect;
#define UI_GRAY_LIGHT 12
#define UI_GRAY_BLACK 0
#define EPD_DRAW_ALIGN_LEFT 0
#define EPD_DRAW_ALIGN_CENTER 1
#define EPD_DRAW_ALIGN_RIGHT 2
static uint32_t s_dual_displayed,s_scrub_target;
static size_t s_scrub_chapter,s_chapter;
static bool s_jump_back_valid;
static char s_title[160]="Chapter",rendered_label[160];
static struct {size_t done,chapters,total;size_t *counts;}s_page_index;
static EpdRect progress_rect(void){return (EpdRect){60,0,600,84};}
static EpdRect progress_back_rect(void){return (EpdRect){0,0,60,84};}
static int reader_width(void){return 684;}
static unsigned percent(size_t page){return (unsigned)page;}
static size_t book_total_bytes(void){return 100000;}
static size_t book_chapter_count(void){return 4;}
static bool page_index_matches(void){return false;}
static bool book_layout_complete(void){return true;}
static int ps_dual_role(void){return settings.role;}
static void copy_text(char*out,size_t n,const char*src){snprintf(out,n,"%s",src);}
static int ttf_text_width_px(int px,const char*t){(void)px;return (int)strlen(t);}
static void fit_text(char*t,int px,int w){(void)t;(void)px;(void)w;}
static void ui_clear_rect_fast(uint8_t*fb,EpdRect r){(void)fb;(void)r;}
static void ui_hairline(uint8_t*fb,int y,int x,int w,int color){(void)fb;(void)y;(void)x;(void)w;(void)color;}
static void epd_fill_rect(EpdRect r,int color,uint8_t*fb){(void)r;(void)color;(void)fb;}
static void epd_fill_circle(int x,int y,int r,int color,uint8_t*fb){(void)x;(void)y;(void)r;(void)color;(void)fb;}
static void ps_notice_draw(uint8_t*fb,int y){(void)fb;(void)y;}
static void ui_text_vc(uint8_t*fb,int x,int y,int px,const char*t,int align,bool invert){
 (void)fb;(void)x;(void)y;(void)px;(void)align;(void)invert;
 if(strstr(t,"主机")||strstr(t,"从机"))copy_text(rendered_label,sizeof(rendered_label),t);
}
"""
source += function(a.book_source.read_text(), 'draw_compact_header') + '\n'
source += function(a.book_source.read_text(), 'kick_prep') + '\n'
source += function(a.book_source.read_text(), 'prepare_turn_frame') + '\n'
source += function(a.dual_source.read_text(), 'reading_checkpoint') + '\n'
source += r'''
int main(void){
 s_page=5;s_turn_anim_pending=true;
 assert(prepare_turn_frame()&&staged==1&&s_next_page==5);
 assert(prepare_turn_frame()&&staged==1);
 s_page=6;assert(prepare_turn_frame()&&staged==2&&s_next_page==6);
 s_next_fb=NULL;s_page=7;assert(!prepare_turn_frame()&&staged==2);s_next_fb=frame;
 s_turn_anim_pending=false;assert(!prepare_turn_frame()&&staged==2);s_next_page=-1;
 s_page=5;assert(kick_prep()&&s_next_page==6&&notifications==1);
 assert(!kick_prep()&&notifications==1);
 paired=true;assert(kick_prep()&&s_next_page==7&&notifications==2);
 s_page=7;assert(kick_prep()&&s_next_page==9&&notifications==3);
 s_dual_solo=true;assert(kick_prep()&&s_next_page==8);s_dual_solo=false;
 s_single_reading=true;assert(!kick_prep());s_single_reading=false;
 s_toolbar=true;assert(!kick_prep());s_toolbar=false;
 s_dual_target=12;assert(!kick_prep());s_dual_target=0;
 s_dual_anchor.pending=true;assert(!kick_prep());s_dual_anchor.pending=false;
 s_open_path[0]='x';assert(!kick_prep());s_open_path[0]=0;
 s_page=28;assert(!kick_prep());s_page=29;paired=false;assert(!kick_prep());
 s_page=0;s_prep_task=NULL;assert(!kick_prep());
 paired=true;s_page=5;s_dual_displayed=21;settings.role=1;
 draw_compact_header(NULL,7);assert(!strcmp(rendered_label,"主机 · 第 23 页"));
 s_dual_displayed=22;settings.role=2;
 draw_compact_header(NULL,7);assert(!strcmp(rendered_label,"从机 · 第 24 页"));
 draw_compact_header(NULL,5);assert(!strcmp(rendered_label,"从机 · 第 22 页"));
 settings.role=1;
 status.reading.revision=1;reading_checkpoint(false,0);assert(!writes);
 reading_checkpoint(false,1499);assert(!writes);
 status.reading.revision=2;reading_checkpoint(false,1500);assert(!writes);
 reading_checkpoint(false,2999);assert(!writes);
 reading_checkpoint(false,3000);assert(writes==1&&stored==2&&durable_revision==2);
 reading_checkpoint(false,5000);assert(writes==1);
 status.reading.revision=3;reading_checkpoint(true,5010);assert(writes==2&&stored==3);
 status.reading.revision=4;fail=true;reading_checkpoint(true,5020);assert(writes==3&&stored==3);
 fail=false;reading_checkpoint(true,5030);assert(writes==4&&stored==4);
 status.reading.revision=5;status.reading.reflow_pending=true;
 reading_checkpoint(true,5040);assert(writes==4);status.reading.reflow_pending=false;
 settings.role=2;reading_checkpoint(true,5050);assert(writes==4);
 settings.role=1;has_path=false;reading_checkpoint(true,5060);assert(writes==4);
 has_path=true;reading_checkpoint(true,5070);assert(writes==5&&durable_revision==5);
 puts("PASS: paired stride, solo/local stride, busy/end guards, cache reuse; idle/exit checkpoints, failed-save retry, role/reflow guards");
}
'''
with tempfile.TemporaryDirectory(prefix='dual-turn-prep-') as d:
    c, exe = Path(d)/'test.c', Path(d)/'test'
    c.write_text(source)
    subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-fno-omit-frame-pointer',str(c),'-o',str(exe)], check=True)
    subprocess.run([str(exe)], check=True)

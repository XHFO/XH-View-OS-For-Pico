#!/usr/bin/env python3
"""原更新器绘图函数的横竖坐标及失败进度验证。/ Verify production updater drawing coordinates and failed progress."""
import argparse,subprocess
from pathlib import Path
from test_native_preview_shelf_ble import write_png,read_pgm
ROOT=Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser();p.add_argument('--output',type=Path,required=True);a=p.parse_args();out=a.output.resolve();out.mkdir(parents=True,exist_ok=True)
source=(ROOT/'main/updater_main.c').read_text();body=source[source.index('enum {'):source.index('static void return_to_system')]
prefix=r'''
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef struct{int x,y,width,height;} EpdRect;
enum { EPD_ROT_LANDSCAPE,EPD_ROT_PORTRAIT,EPD_ROT_INVERTED_LANDSCAPE,EPD_ROT_INVERTED_PORTRAIT,MODE_GC16 };
static uint8_t pixels[1216*684];static struct{uint8_t *framebuffer;int hl;} s_hw={pixels,0};
static int updates;
static int epd_width(void){return 1216;}static int epd_height(void){return 684;}
static int epd_get_rotation(void){return EPD_ROT_INVERTED_PORTRAIT;}
static int epd_rotated_display_width(void){return 684;}
static void epd_hl_set_all_white(int *hl){(void)hl;memset(pixels,255,sizeof(pixels));}
static void epd_fill_rect(EpdRect r,uint8_t color,uint8_t *fb){
 assert(r.x>=0&&r.y>=0&&r.x+r.width<=1216&&r.y+r.height<=684);
 for(int y=r.y;y<r.y+r.height;y++)memset(fb+y*1216+r.x,color*17,r.width);
}
static void epd_poweron(void){}
static void epd_hl_update_screen(int *hl,int mode,int temp){(void)hl;(void)mode;(void)temp;updates++;}
'''
suffix=r'''
static uint8_t at(int x,int y){return pixels[(683-x)*1216+y];}
int main(int argc,char **argv){assert(argc==2);const int stages[]={UPDATE_VERIFYING,UPDATE_WRITING,UPDATE_COMPLETE,UPDATE_FAILED};const int pct[]={0,50,100,100};
 for(int i=0;i<4;i++){
  status(pct[i],stages[i]);assert(updates==i+1);
  unsigned ink=0;for(int y=590;y<672;y++)for(int x=77;x<607;x++)ink+=at(x,y)==0;
  assert(stages[i]==UPDATE_FAILED?ink==0:stages[i]==UPDATE_WRITING?ink>0&&ink<530*82:1);
  char path[1024];snprintf(path,sizeof(path),"%s/updater-%d.pgm",argv[1],i);FILE *f=fopen(path,"wb");assert(f);fprintf(f,"P5\n684 1216\n255\n");
  for(int y=0;y<1216;y++)for(int x=0;x<684;x++)fputc(at(x,y),f);assert(!fclose(f));
 }
 puts("PASS: updater stages stay inside the framebuffer; failure has no filled progress; one refresh per state");return 0;
}
'''
path=out/'updater-render.c';path.write_text(prefix+body+suffix);exe=out/'updater-render'
subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',str(path),'-o',str(exe)],check=True)
subprocess.run([str(exe),str(out)],check=True)
for f in out.glob('updater-*.pgm'):
    w,h,data=read_pgm(f);write_png(f.with_suffix('.png'),w,h,data)

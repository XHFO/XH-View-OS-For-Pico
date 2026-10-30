#!/usr/bin/env python3
"""输出同源C控制器+真实builtin.ttf的原生灰阶截图；不是物理墨水屏照片。 / Render native C UI with the real font, not a physical screen photograph."""
from pathlib import Path
import argparse
import os
import re
import shutil
import subprocess
import sys
import tempfile
ROOT=Path(__file__).resolve().parents[1]

def extract_function(source,name):
    pattern=re.compile(r'^(?:static\s+)?(?:void|int|enum\s+\w+)\s+'+re.escape(name)+r'\s*\(',re.M)
    match=pattern.search(source)
    if not match:raise RuntimeError('Function not found: '+name)
    begin=source.index('{',match.start());depth=1;at=begin+1
    # 绘图函数不含带花括号的字符串；逐层提取当前源实现。 / Geometry functions contain no brace strings; copy their current implementation by brace depth.
    while depth:
        if source[at]=='{':depth+=1
        elif source[at]=='}':depth-=1
        at+=1
    return source[match.start():at]

def prepare_native_renderer(build):
    """供其它原生控制器检查共享，返回需要链接的真实绘图源。 / Shared real-drawing sources for other native controller harnesses."""
    ui=(ROOT/'main/ui/ui_kit.c').read_text()
    epd=(ROOT/'components/epdiy/src/epdiy.c').read_text()
    geometry=['#include "ui_kit.h"','#include <stdlib.h>','#define _swap_int(a,b) do { int t=(a);(a)=(b);(b)=t; } while(0)']
    for name in ['epd_write_line','epd_draw_line','epd_draw_circle','epd_fill_circle_helper']:geometry.append(extract_function(epd,name))
    for name in ['clamp_radius','draw_arc','ui_draw_round_rect','ui_fill_round_rect']:geometry.append(extract_function(ui,name))
    path=build/'native-geometry.c';path.write_text('\n\n'.join(geometry)+'\n')
    return [ROOT/'tools/pico_framebuffer_host.c',ROOT/'main/system/pico_draw.c',path]

def prepare_assets(build):
    section='.section __TEXT,__const' if sys.platform=='darwin' else '.section .rodata'
    assembly=section+'\n'
    for name,stem in [('preview_riverside.txt','preview_riverside_txt'),('preview_comic.bin','preview_comic_bin')]:
        shutil.copyfile(ROOT/'main/assets'/name,build/name)
        assembly+=f'.global _binary_{stem}_start\n_binary_{stem}_start:\n.incbin "{name}"\n.global _binary_{stem}_end\n_binary_{stem}_end:\n'
    if sys.platform.startswith('linux'):assembly+='.section .note.GNU-stack,"",@progbits\n'
    (build/'assets.S').write_text(assembly)

def renderer_flags():
    flags=['-std=gnu11','-Wall','-Wextra','-Werror','-O1','-g']
    for include in ['tools/pico_reader_stubs','tools/preview_reading_stubs','tools','main/system','main/book','main/font']:
        flags+=['-I',str(ROOT/include)]
    return flags

def prepare_fonts(build,default_font):
    """私有同目录字体集使外部默认字体也能切换真实内置宋体/楷体，源文件只读。
    Stage a private sibling font set so an external default can switch to real bundled serif/kai without touching sources.
    """
    fonts=build/'fonts';fonts.mkdir()
    for name,source in [('builtin',default_font),('serif',ROOT/'main/assets/serif.ttf'),('kai',ROOT/'main/assets/kai.ttf')]:
        shutil.copyfile(source,fonts/(name+'.ttf'))
    return fonts/'builtin.ttf'

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output',type=Path,default=ROOT/'build/native-reader-preview')
    parser.add_argument('--font',type=Path,default=ROOT/'main/assets/builtin.ttf')
    args=parser.parse_args();args.output.mkdir(parents=True,exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='pico-framebuffer-') as temp:
        build=Path(temp);prepare_assets(build);sources=prepare_native_renderer(build)
        staged_font=prepare_fonts(build,args.font.resolve())
        exe=build/'render-reader'
        command=[os.environ.get('CC','cc')]+renderer_flags()+[str(ROOT/'tools/pico_reader_render.c')]+[str(p) for p in sources]+['assets.S','-lm','-o',str(exe)]
        subprocess.run(command,cwd=build,check=True)
        subprocess.run([str(exe),str(staged_font),str(args.output.resolve())],cwd=build,check=True)
    try:
        from PIL import Image,ImageDraw
    except ImportError:
        print('PGM screenshots saved; Pillow unavailable, PNG conversion skipped.');return
    images=[]
    for pgm in sorted(args.output.glob('*.pgm')):
        with Image.open(pgm) as image:
            image.save(pgm.with_suffix('.png'))
            im=image.copy();im.thumbnail((342,608));images.append((pgm.stem,im))
    columns=4;cellw=370;cellh=650
    sheet=Image.new('RGB',(columns*cellw,((len(images)+columns-1)//columns)*cellh),'#e7e7e7');draw=ImageDraw.Draw(sheet)
    for i,(name,image) in enumerate(images):
        x=(i%columns)*cellw;y=(i//columns)*cellh;draw.text((x+12,y+10),name,fill='black');sheet.paste(image,(x+12,y+34))
    sheet.save(args.output/'contact.png')
    print('Contact sheet: '+str(args.output/'contact.png'))
if __name__=='__main__':main()

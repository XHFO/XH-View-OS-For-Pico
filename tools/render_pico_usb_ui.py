#!/usr/bin/env python3
"""Render actual USB UI, drawing/font; no USB or serial device access.
真实 USB 页面和字体截图；不连接 USB 或串口设备。
"""
from pathlib import Path
import argparse
import os
import shlex
import shutil
import subprocess
import tempfile
from render_pico_reader import ROOT,prepare_native_renderer,renderer_flags

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output',type=Path,default=ROOT/'build/native-usb-ui-preview')
    parser.add_argument('--font',type=Path,default=ROOT/'main/assets/builtin.ttf')
    args=parser.parse_args();args.output.mkdir(parents=True,exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='pico-usb-ui-render-') as tmp:
        build=Path(tmp);sources=prepare_native_renderer(build)
        (build/'sdmmc_cmd.h').write_text('#pragma once\ntypedef struct {int unused;} sdmmc_card_t;\n')
        for name in ['esp_log.h','esp_err.h']:shutil.copyfile(ROOT/'tools/wifi_qr_stubs'/name,build/name)
        host=sources[0].read_text();signature='void ttf_draw_text_px(';assert host.count(signature)==1
        traced=build/'usb-ui-framebuffer.c'
        traced.write_text(host.replace(signature,'void usb_ui_native_text(',1)+'\nbool usb_ui_font_has_codepoint(uint32_t cp){return stbtt_FindGlyphIndex(&host_font,(int)cp)!=0;}\n')
        sources[0]=traced;exe=build/'render-usb-ui'
        (build/'freertos').mkdir()
        (build/'freertos/FreeRTOS.h').write_text('#pragma once\n#define pdPASS 1\n')
        (build/'freertos/task.h').write_text('#pragma once\nint xTaskCreate(void (*fn)(void*),const char*,unsigned,void*,int,void*);\nvoid vTaskDelete(void*);\n')
        command=[os.environ.get('CC','cc'),'-I',str(build)]+renderer_flags()+shlex.split(os.environ.get('CFLAGS',''))
        command+=['-I',str(ROOT/'components/read_pico_usb/include'),'-I',str(ROOT/'components/read_pico/include')]
        command+=[str(ROOT/'tools/pico_usb_ui_render.c')]+[str(p) for p in sources]+['-lm','-o',str(exe)]
        subprocess.run(command,cwd=build,check=True)
        result=subprocess.run([str(exe),str(args.font.resolve()),str(args.output.resolve())],cwd=build)
    from PIL import Image,ImageDraw
    for orientation in ['portrait','landscape']:
        images=[]
        for pgm in sorted(args.output.glob('*-'+orientation+'.pgm')):
            with Image.open(pgm) as image:
                image.save(pgm.with_suffix('.png'));thumb=image.copy();thumb.thumbnail((342,608) if orientation=='portrait' else (608,342));images.append((pgm.stem,thumb))
        columns=4;cellw,cellh=(370,650) if orientation=='portrait' else (630,385)
        sheet=Image.new('RGB',(columns*cellw,((len(images)+columns-1)//columns)*cellh),'#e7e7e7');draw=ImageDraw.Draw(sheet)
        for i,(name,image) in enumerate(images):
            x=(i%columns)*cellw;y=(i//columns)*cellh;draw.text((x+12,y+10),name,fill='black');sheet.paste(image,(x+12,y+34))
        path=args.output/('contact-'+orientation+'.png');sheet.save(path);print(path)
    raise SystemExit(result.returncode)
if __name__=='__main__':main()

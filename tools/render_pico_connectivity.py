#!/usr/bin/env python3
"""Render actual connectivity UI, drawing/font/QR; no network or serial device access.
真实网络页面、字体和二维码截图；不连接网络或串口设备。
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
    parser.add_argument('--output',type=Path,default=ROOT/'build/native-connectivity-preview')
    parser.add_argument('--font',type=Path,default=ROOT/'main/assets/builtin.ttf')
    args=parser.parse_args();args.output.mkdir(parents=True,exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='pico-connectivity-render-') as tmp:
        build=Path(tmp);sources=prepare_native_renderer(build)
        (build/'sdmmc_cmd.h').write_text('#pragma once\ntypedef struct {int unused;} sdmmc_card_t;\n')
        for name in ['esp_log.h','esp_err.h']:shutil.copyfile(ROOT/'tools/wifi_qr_stubs'/name,build/name)
        host=sources[0].read_text();signature='void ttf_draw_text_px(';assert host.count(signature)==1
        traced=build/'connectivity-framebuffer.c'
        traced.write_text(host.replace(signature,'void connectivity_native_text(',1)+'\nbool connectivity_font_has_codepoint(uint32_t cp){return stbtt_FindGlyphIndex(&host_font,(int)cp)!=0;}\n')
        sources[0]=traced;qr=ROOT/'managed_components/espressif__qrcode';exe=build/'render-connectivity'
        command=[os.environ.get('CC','cc'),'-I',str(build)]+renderer_flags()+shlex.split(os.environ.get('CFLAGS',''))
        for path in [ROOT/'main/ui',ROOT/'components/read_pico_transfer/include',ROOT/'components/read_pico/include',ROOT/'tools/wifi_qr_stubs',qr/'include',qr]:command+=['-I',str(path)]
        command+=[str(ROOT/'tools/pico_connectivity_render.c')]+[str(p) for p in sources]
        command+=[str(p) for p in [ROOT/'main/ui/ui_wifi_qr.c',qr/'esp_qrcode_main.c',qr/'esp_qrcode_wrapper.c',qr/'qrcodegen.c']]+['-lm','-o',str(exe)]
        subprocess.run(command,cwd=build,check=True)
        result=subprocess.run([str(exe),str(args.font.resolve()),str(args.output.resolve())],cwd=build,check=True)
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

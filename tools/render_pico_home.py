#!/usr/bin/env python3
"""真实原生家庭、打印和休眠页面截图；网络数据为固定示例，不是实机照片。
Render real native home, printer and standby routes; networking is fixture data, not physical photographs.
"""
from pathlib import Path
import argparse
import os
import subprocess
import tempfile
from render_pico_reader import ROOT, prepare_native_renderer, renderer_flags


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output',type=Path,default=ROOT/'build/native-home-preview')
    parser.add_argument('--font',type=Path,default=ROOT/'main/assets/builtin.ttf')
    args=parser.parse_args();args.output.mkdir(parents=True,exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='pico-home-render-') as tmp:
        build=Path(tmp);sources=prepare_native_renderer(build);exe=build/'render-home'
        command=[os.environ.get('CC','cc')]+renderer_flags()+['-I',str(ROOT/'main/ui'),'-I',str(ROOT/'components/read_pico_home_link/include'),'-I',str(ROOT/'components/read_pico_transfer/include'),str(ROOT/'tools/pico_home_render.c')]+[str(p) for p in sources]+['-lm','-o',str(exe)]
        subprocess.run(command,cwd=build,check=True)
        subprocess.run([str(exe),str(args.font.resolve()),str(args.output.resolve())],cwd=build,check=True)
    from PIL import Image,ImageDraw
    for orientation in ['portrait','landscape']:
        images=[]
        for pgm in sorted(args.output.glob('*-'+orientation+'.pgm')):
            with Image.open(pgm) as image:
                image.save(pgm.with_suffix('.png'));im=image.copy();im.thumbnail((342,608) if orientation=='portrait' else (608,342));images.append((pgm.stem,im))
        columns=4;cellw=370 if orientation=='portrait' else 630;cellh=650 if orientation=='portrait' else 385
        sheet=Image.new('RGB',(columns*cellw,((len(images)+columns-1)//columns)*cellh),'#e7e7e7');draw=ImageDraw.Draw(sheet)
        for i,(name,image) in enumerate(images):
            x=(i%columns)*cellw;y=(i//columns)*cellh;draw.text((x+12,y+10),name,fill='black');sheet.paste(image,(x+12,y+34))
        path=args.output/('contact-'+orientation+'.png');sheet.save(path);print(path)


if __name__=='__main__':
    main()

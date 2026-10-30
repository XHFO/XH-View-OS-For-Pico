#!/usr/bin/env python3
"""中文：生成同源 C + 内置 TTF 的四棋横竖截图。 / English: Render four native games with the device font."""
from pathlib import Path
import argparse
import os
import subprocess
import tempfile
from render_pico_reader import ROOT, prepare_native_renderer, renderer_flags

parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--output',type=Path,default=ROOT/'build/native-games-preview')
args=parser.parse_args();args.output.mkdir(parents=True,exist_ok=True)
with tempfile.TemporaryDirectory(prefix='pico-games-frame-') as temp:
    build=Path(temp);sources=prepare_native_renderer(build);exe=build/'render-games'
    subprocess.run([os.environ.get('CC','cc')]+renderer_flags()+[str(ROOT/'tools/pico_games_render.c')]+[str(p) for p in sources]+['-lm','-o',str(exe)],check=True)
    subprocess.run([str(exe),str(ROOT/'main/assets/builtin.ttf'),str(args.output.resolve())],check=True)
try:
    from PIL import Image,ImageDraw
except ImportError:
    print('PGM screenshots: '+str(args.output));raise SystemExit(0)
images=[]
for pgm in sorted(args.output.glob('*.pgm')):
    with Image.open(pgm) as image:
        image.save(pgm.with_suffix('.png'));im=image.copy();im.thumbnail((456,608));images.append((pgm.stem,im))
cols=3;cw=480;ch=645
sheet=Image.new('RGB',(cw*cols,((len(images)+cols-1)//cols)*ch),'#e8e8e8');draw=ImageDraw.Draw(sheet)
for i,(name,im) in enumerate(images):
    x=i%cols*cw;y=i//cols*ch;draw.text((x+12,y+8),name,fill='black');sheet.paste(im,(x+12,y+30))
sheet.save(args.output/'contact.png');print('Contact sheet: '+str(args.output/'contact.png'))

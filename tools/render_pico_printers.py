#!/usr/bin/env python3
"""Render shared printer selection and phone configuration pages with fixture-only data; no accounts or browser."""
from pathlib import Path
import argparse
import os
import subprocess
import shutil
import tempfile
from render_pico_reader import ROOT, prepare_native_renderer, renderer_flags


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, default=ROOT/'build/printer-connect-review/screenshots')
    parser.add_argument('--font', type=Path, default=ROOT/'main/assets/builtin.ttf')
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='pico-printer-render-') as temp:
        build = Path(temp)
        sources = prepare_native_renderer(build)
        exe = build/'render-printers'
        for name in ['esp_log.h', 'esp_err.h']:
            shutil.copyfile(ROOT/'tools/wifi_qr_stubs'/name, build/name)
        qr = ROOT/'managed_components/espressif__qrcode'
        command = [os.environ.get('CC', 'cc'), '-I', str(build), *renderer_flags(), '-I', str(qr/'include'), '-I', str(qr), '-I', str(ROOT/'main/ui'), '-I', str(ROOT/'components/read_pico_home_link/include'), '-I', str(ROOT/'components/read_pico_transfer/include'), str(ROOT/'tools/pico_printer_render.c'),
                   *map(str, sources), str(ROOT/'main/ui/ui_wifi_qr.c'), *[str(qr/name) for name in ['esp_qrcode_main.c', 'esp_qrcode_wrapper.c', 'qrcodegen.c']], '-lm', '-o', str(exe)]
        subprocess.run(command, cwd=build, check=True)
        subprocess.run([str(exe), str(args.font.resolve()), str(args.output.resolve())], cwd=build, check=True)
    from PIL import Image, ImageDraw
    for orientation in ['portrait', 'landscape']:
        images = []
        for pgm in sorted(args.output.glob('*-'+orientation+'.pgm')):
            with Image.open(pgm) as raw:
                raw.save(pgm.with_suffix('.png'))
                im = raw.convert('RGB')
            im.thumbnail((342, 608) if orientation == 'portrait' else (608, 342))
            images.append((pgm.stem, im))
        columns = 4
        cellw, cellh = (370, 650) if orientation == 'portrait' else (630, 385)
        sheet = Image.new('RGB', (columns*cellw, ((len(images)+columns-1)//columns)*cellh), '#e7e7e7')
        draw = ImageDraw.Draw(sheet)
        for i, (name, im) in enumerate(images):
            x, y = (i % columns)*cellw, (i//columns)*cellh
            draw.text((x+12, y+10), name, fill='black')
            sheet.paste(im, (x+12, y+34))
        path = args.output/('contact-'+orientation+'.png')
        sheet.save(path)
        print(path)


if __name__ == '__main__':
    main()

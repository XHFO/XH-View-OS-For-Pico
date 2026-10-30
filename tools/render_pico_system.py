#!/usr/bin/env python3
"""从当前壳提取完整同源绘图函数，复用真实原生画布；不是实屏照片或触摸验收。
Extract exact current shell rendering functions using the real native canvas, not physical-screen or touch acceptance.
"""
from pathlib import Path
import argparse
import ast
import os
import re
import shlex
import subprocess
import tempfile
from render_pico_reader import ROOT, prepare_native_renderer, renderer_flags


def prepare_shell(build):
    source=(ROOT/'main/system/pico_system.c').read_text()
    test=(ROOT/'tools/test_pico_system.py').read_text()
    tree=ast.parse(test);definition=next(n for n in tree.body if isinstance(n,ast.FunctionDef) and n.name=='function')
    namespace={'re':re,'SOURCE':source}
    exec(compile(ast.Module(body=[definition],type_ignores=[]),'shell-source-extractor','exec'),namespace)
    extract=namespace['function']
    prefix=source[source.index('#define PS_VERSION'):source.index('static uint32_t crc32(')]
    fan_type=re.search(r'typedef struct\s*\{[^}]*\}\s*fan_item_t;',source).group()
    rotation_type=re.search(r'enum EpdRotation\s*\{[^}]*\};',(ROOT/'components/epdiy/src/epdiy.h').read_text()).group()
    # 外部书页的覆盖层跟随实际方向；只在主机提供可控旋转，不模拟触摸或设备。
    # External-book overlays follow the actual direction; provide controllable host rotation without device/touch simulation.
    rotation_host=rotation_type+'\nstatic enum EpdRotation host_rotation=EPD_ROT_INVERTED_PORTRAIT;\nstatic enum EpdRotation epd_get_rotation(void){return host_rotation;}\n'
    names=['ps_view','ps_route','reader_route','home_route','launcher_root','quick_canonical','quick_catalog','quick_enabled','quick_slot_of','quick_placed','quick_seed_slots','quick_import','quick_defaults','quick_edit_contact_cancel','quick_edit_begin','enabled','quick_unavailable','fan_editor_active','fan_frame_active','fan_frame_selected','fan_frame_side','fan_scale','fan_layout_slots','fan_layout','fan_prompt_x','render_fan_base','render_fan_cursor','render_fan','group_items','panel_group_rect','panel_reading_rect','render_panel','device_page_count','device_render','quick_catalog_row_height','quick_catalog_rows','quick_catalog_rect','quick_settings_render','onboarding_render','render']
    device_constants=re.search(r'enum\s*\{\s*DEVICE_SETTING_COUNT[^}]*\};',source).group()
    (build/'native-system-functions.inc').write_text(rotation_host+prefix+'\n'+fan_type+'\n'+device_constants+'\n\n'+'\n\n'.join(extract(name) for name in names)+'\n')


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output',type=Path,default=ROOT/'build/native-system-preview');parser.add_argument('--font',type=Path,default=ROOT/'main/assets/builtin.ttf');args=parser.parse_args();args.output.mkdir(parents=True,exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='pico-system-render-') as tmp:
        build=Path(tmp);prepare_shell(build);sources=prepare_native_renderer(build);sources.append(ROOT/'main/system/pico_navigation.c');exe=build/'render-system'
        command=[os.environ.get('CC','cc')]+renderer_flags()+shlex.split(os.environ.get('CFLAGS',''))+['-Wno-unused-variable','-Wno-unused-function','-I',str(build),str(ROOT/'tools/pico_system_render.c')]+[str(p) for p in sources]+['-lm','-o',str(exe)]
        subprocess.run(command,cwd=build,check=True);subprocess.run([str(exe),str(args.font.resolve()),str(args.output.resolve())],cwd=build,check=True)
    try:
        from PIL import Image,ImageDraw
    except ImportError:
        print('PGM screenshots saved; Pillow unavailable, PNG conversion skipped.');return
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

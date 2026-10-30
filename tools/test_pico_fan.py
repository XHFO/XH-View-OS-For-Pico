#!/usr/bin/env python3
"""提取真实扇形控制器验证触摸目标；不连接串口。 / Test actual fan targets without serial I/O."""
from pathlib import Path
import argparse
import ast
import os
import re
import shlex
import subprocess
import tempfile

ROOT=Path(__file__).resolve().parents[1]

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output',type=Path)
    args=parser.parse_args()
    source=(ROOT/'main/system/pico_system.c').read_text()
    tree=ast.parse((ROOT/'tools/test_pico_system.py').read_text())
    definition=next(n for n in tree.body if isinstance(n,ast.FunctionDef) and n.name=='function')
    namespace={'re':re,'SOURCE':source}
    exec(compile(ast.Module(body=[definition],type_ignores=[]),'fan-source-extractor','exec'),namespace)
    extract=namespace['function']
    prefix=source[source.index('#define PS_VERSION'):source.index('static uint32_t crc32(')]
    fan_type=re.search(r'typedef struct\s*\{[^}]*\}\s*fan_item_t;',source).group()
    rotation_type=re.search(r'enum EpdRotation\s*\{[^}]*\};',(ROOT/'components/epdiy/src/epdiy.h').read_text()).group()
    # 外部页以实际旋转为准，主机适配只提供旋转读数。/ External pages use actual rotation; the host adapter only supplies that reading.
    rotation_host=rotation_type+'\nstatic enum EpdRotation host_rotation=EPD_ROT_INVERTED_PORTRAIT;\nstatic enum EpdRotation epd_get_rotation(void){return host_rotation;}\n'
    functions='\n\n'.join(extract(n) for n in ['ps_view','enabled','quick_canonical','quick_catalog','quick_enabled','quick_slot_of','quick_placed','quick_seed_slots','quick_import','quick_defaults','fan_editor_active','fan_frame_active','fan_frame_selected','fan_frame_side','fan_scale','fan_layout_slots','fan_layout','fan_pick','fan_move'])
    with tempfile.TemporaryDirectory(prefix='pico-fan-host-') as directory:
        build=Path(directory)
        (build/'native-fan-functions.inc').write_text('#include <stdatomic.h>\n'+rotation_host+prefix+'\n'+fan_type+'\n'+functions)
        command=[os.environ.get('CC','cc'),'-std=c11','-O2','-Wall','-Wextra','-Werror','-Wno-unused-variable','-Wno-unused-function']
        command+=shlex.split(os.environ.get('CFLAGS',''))
        for include in [build,ROOT/'tools/pico_reader_stubs',ROOT/'main/system']:
            command+=['-I',str(include)]
        exe=build/'fan-test';command += [str(ROOT/'tools/pico_fan_host_test.c'),'-lm','-o',str(exe)]
        subprocess.run(command,check=True)
        env=os.environ.copy()
        if args.output:
            args.output.mkdir(parents=True,exist_ok=True);env['PICO_FAN_TEST_OUTPUT']=str(args.output.resolve())
        subprocess.run([str(exe)],env=env,check=True)
    if args.output:
        from PIL import Image
        for path in sorted(args.output.glob('hit-*.pgm')):
            with Image.open(path) as image:
                image.resize((round(image.width*1.9),round(image.height*1.9))).save(path.with_suffix('.png'))

if __name__=='__main__':main()

#!/usr/bin/env python3
"""原生阅读产品控制器的可复跑主机检查，不代表实机字形与性能。 / Host reader checks, not physical glyph or performance acceptance."""
from pathlib import Path
import argparse
import os
import shutil
import subprocess
import sys
import tempfile
ROOT=Path(__file__).resolve().parents[1]
def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--sanitize',action='store_true')
    args=parser.parse_args()
    with tempfile.TemporaryDirectory(prefix='preader-') as directory, tempfile.TemporaryDirectory(prefix='pr-',dir='/tmp') as data:
        build=Path(directory)
        section='.section __TEXT,__const' if sys.platform=='darwin' else '.section .rodata'
        assembly=section+'\n'
        for name,stem in [('preview_riverside.txt','preview_riverside_txt'),('preview_comic.bin','preview_comic_bin')]:
            shutil.copyfile(ROOT/'main/assets'/name,build/name)
            assembly+=f'.global _binary_{stem}_start\n_binary_{stem}_start:\n.incbin "{name}"\n.global _binary_{stem}_end\n_binary_{stem}_end:\n'
        if sys.platform.startswith('linux'):assembly+='.section .note.GNU-stack,"",@progbits\n'
        (build/'assets.S').write_text(assembly)
        cmd=[os.environ.get('CC','cc'),'-std=gnu11','-Wall','-Wextra','-Werror','-Wno-unused-parameter','-g']
        if args.sanitize:cmd+=['-fsanitize=address,undefined','-fno-omit-frame-pointer']
        for include in ['tools/pico_reader_stubs','tools/preview_reading_stubs','main/system','main/book']:cmd+=['-I',str(ROOT/include)]
        exe=build/'reader-test'
        cmd += [str(ROOT/'tools/pico_reader_host_test.c'),'assets.S','-o',str(exe)]
        subprocess.run(cmd,cwd=build,check=True)
        subprocess.run([str(exe),data],cwd=build,check=True)
        # 第二个可执行文件不提供任何示例资产，直接编译产品分支。/ Link the production branch without providing any sample assets.
        product=build/'reader-product-test'
        product_cmd=[value for value in cmd[:-2] if value!='assets.S']
        product_cmd+=['-DPICO_READER_NO_FIXTURES','-Wno-unused-function','-o',str(product)]
        subprocess.run(product_cmd,cwd=build,check=True)
        symbols=subprocess.run(['nm',str(product)],check=True,text=True,stdout=subprocess.PIPE).stdout
        assert '_binary_preview_' not in symbols and 'reader_text_' not in symbols
        with tempfile.TemporaryDirectory(prefix='pp-',dir='/tmp') as product_data:
            subprocess.run([str(product),product_data],cwd=build,check=True)
if __name__=='__main__':main()

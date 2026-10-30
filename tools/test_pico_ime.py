#!/usr/bin/env python3
"""输入法真实事件与布局回归；不访问设备。 / Input event/layout regression without device access."""
from pathlib import Path
import argparse
import os
import shutil
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--sanitize', action='store_true')
    parser.add_argument('--case', choices=['all', 'continuous', 'separators', 'paging', 'touch', 'modes', 'password', 'dictionary', 'cache', 'partial', 'burst'], default='all')
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix='pime-') as directory, tempfile.TemporaryDirectory(prefix='pi-', dir='/tmp') as data:
        build = Path(directory)
        shutil.copyfile(ROOT / 'assets/tf-pack/readpico/ime/pinyin-v1.bin', Path(data) / 'pinyin-v1.bin')
        assembly = '.section __TEXT,__const\n' if sys.platform == 'darwin' else '.section .rodata\n'
        for name, stem in [('preview_riverside.txt', 'preview_riverside_txt'), ('preview_comic.bin', 'preview_comic_bin')]:
            shutil.copyfile(ROOT / 'main/assets' / name, build / name)
            assembly += f'.global _binary_{stem}_start\n_binary_{stem}_start:\n.incbin "{name}"\n.global _binary_{stem}_end\n_binary_{stem}_end:\n'
        if sys.platform.startswith('linux'):
            assembly += '.section .note.GNU-stack,"",@progbits\n'
        (build / 'assets.S').write_text(assembly)
        cmd = [os.environ.get('CC', 'cc'), '-std=gnu11', '-Wall', '-Wextra', '-Werror', '-g']
        if args.sanitize:
            cmd += ['-fsanitize=address,undefined', '-fno-omit-frame-pointer']
        for include in ['tools/pico_reader_stubs', 'tools/preview_reading_stubs', 'main/system', 'main/book']:
            cmd += ['-I', str(ROOT / include)]
        exe = build / 'ime-test'
        cmd += [str(ROOT / 'tools/pico_ime_host_test.c'), 'assets.S', '-o', str(exe)]
        subprocess.run(cmd, cwd=build, check=True)
        subprocess.run([str(exe), data, args.case], cwd=build, check=True)


if __name__ == '__main__':
    main()

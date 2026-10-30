#!/usr/bin/env python3
"""真字体局部绘制等价性回归；不访问设备。 / Real-font partial-render equivalence without device access."""
from pathlib import Path
import argparse
import os
import subprocess
import tempfile
from render_pico_reader import ROOT, prepare_assets, prepare_fonts, prepare_native_renderer, renderer_flags


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--sanitize', action='store_true')
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix='pico-editor-render-') as directory:
        build = Path(directory)
        prepare_assets(build)
        sources = prepare_native_renderer(build)
        font = prepare_fonts(build, ROOT / 'main/assets/builtin.ttf')
        flags = renderer_flags()
        if args.sanitize:
            flags += ['-fsanitize=address,undefined', '-fno-omit-frame-pointer']
        exe = build / 'editor-render-test'
        command = [os.environ.get('CC', 'cc')] + flags + [str(ROOT / 'tools/pico_editor_render_test.c')] + [str(p) for p in sources] + ['assets.S', '-lm', '-o', str(exe)]
        subprocess.run(command, cwd=build, check=True)
        subprocess.run([str(exe), str(font)], cwd=build, check=True)


if __name__ == '__main__':
    main()

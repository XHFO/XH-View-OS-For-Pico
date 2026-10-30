#!/usr/bin/env python3
"""中文：真实TF画板模块的保存、重开、增量绘图和旧ABI兼容回归；无设备访问。
English: Check real TF drawing save/resume, incremental ink and legacy ABI compatibility without devices.
"""
from pathlib import Path
import os
import subprocess
import tempfile
from build_tool_packages import build_packages, ROOT

with tempfile.TemporaryDirectory(prefix='drawing-notes-') as directory:
    output = Path(directory)
    report = build_packages(output / 'packages')
    legacy = ROOT / 'build/dev35-native-v3/game-packages/chess.pga'
    for sanitize in (False, True):
        binary = output / ('test-sanitized' if sanitize else 'test')
        flags = ['-std=gnu11', '-O1', '-g', '-Wall', '-Wextra', '-Werror']
        if sanitize:
            flags += ['-fsanitize=address,undefined', '-fno-omit-frame-pointer']
        subprocess.run([os.environ.get('CC', 'cc'), *flags,
                        '-I', str(ROOT / 'components/read_pico_apps/include'),
                        str(ROOT / 'tools/test_drawing_notes_module.c'),
                        str(ROOT / 'components/read_pico_apps/pico_app_package.c'),
                        str(ROOT / 'components/read_pico_apps/pico_app_loader.c'),
                        '-o', str(binary)], check=True)
        command = [str(binary), str(output / 'packages/drawing-notes.pga')]
        if legacy.is_file():
            command.append(str(legacy))
        subprocess.run(command, check=True)

#!/usr/bin/env python3
"""Bounded management API host tests, no real accounts or network access."""
from pathlib import Path
import os
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]

with tempfile.TemporaryDirectory(prefix='pico-home-web-') as temp:
    exe = Path(temp)/'home-web-test'
    subprocess.run([os.environ.get('CC', 'cc'), '-std=c11', '-Wall', '-Wextra', '-Werror', '-g',
                    '-fsanitize=address,undefined', '-fno-omit-frame-pointer',
                    '-I', str(ROOT/'components/read_pico_home_link/include'),
                    '-I', str(ROOT/'managed_components/espressif__cjson/cJSON'),
                    str(ROOT/'tools/home_link_web_host_test.c'),
                    *[str(ROOT/'components/read_pico_home_link'/n) for n in ['home_link_web.c', 'home_link_model.c', 'home_link_json.c']],
                    '-lm', '-o', str(exe)], cwd=ROOT, check=True)
    subprocess.run([str(exe)], cwd=ROOT, check=True)

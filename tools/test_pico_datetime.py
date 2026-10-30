#!/usr/bin/env python3
"""中文：时间页真实源码宿主测试，不修改电脑时钟。 / English: Exact-source datetime tests without changing the host clock."""
import argparse
import os
from pathlib import Path
import subprocess
import tempfile

ROOT=Path(__file__).resolve().parents[1]
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--sanitize',action='store_true')
args=parser.parse_args()
with tempfile.TemporaryDirectory(prefix='pico-datetime-') as directory:
    binary=Path(directory)/'checks'
    command=[os.environ.get('CC','cc'),'-std=gnu11','-O1','-g','-Wall','-Wextra','-Werror',
             '-I'+str(ROOT/'tools/pico_datetime_stubs'),'-I'+str(ROOT/'tools/pico_games_test'),
             '-I'+str(ROOT/'main/system'),'-I'+str(ROOT/'components/read_pico_pmu/include')]
    # 允许页面独立开发先于导航enum接线；集成后自动使用真实路由。
    # Permit isolated page work before route wiring; integrated builds use the actual route enum.
    if 'PS_DATETIME' not in (ROOT/'main/system/pico_system.h').read_text():command+=['-DPS_DATETIME=PS_ROUTE_COUNT']
    if args.sanitize:command+=['-fsanitize=address,undefined','-fno-omit-frame-pointer']
    for preview in (False,True):
        flags=['-DPICO_DATETIME_NATIVE_PREVIEW_TEST'] if preview else []
        subprocess.run(command+flags+[str(ROOT/'tools/pico_datetime_host_test.c'),'-o',str(binary)],check=True)
        subprocess.run([str(binary)],check=True)

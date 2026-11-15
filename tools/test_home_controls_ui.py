#!/usr/bin/env python3
"""房间选择页面的真实动态应用回归。/ Test the real dynamic room-selection UI."""
import os,subprocess,tempfile
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix='home-rooms-') as temp:
    p=Path(temp);cc=os.environ.get('CC','cc');sdk=ROOT/'components/read_pico_apps/include'
    flags=['-std=gnu11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-I',str(sdk)]
    subprocess.run([cc,*flags,'-fPIC','-fno-builtin','-fno-stack-protector','-fvisibility=hidden','-shared',str(ROOT/'apps/src/home_link.c'),'-o',str(p/'home.so')],check=True)
    subprocess.run([cc,*flags,str(ROOT/'tools/home_controls_ui_host_test.c'),'-ldl','-o',str(p/'test')],check=True)
    subprocess.run([str(p/'test'),str(p/'home.so')],check=True)

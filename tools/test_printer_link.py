#!/usr/bin/env python3
"""中文：独立打印机实际应用回归。/ English: Regression for the actual independent printer app."""
import os,subprocess,tempfile
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix='printer-app-') as temp:
    p=Path(temp);cc=os.environ.get('CC','cc');sdk=ROOT/'components/read_pico_apps/include'
    flags=['-std=gnu11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-I',str(sdk)]
    subprocess.run([cc,*flags,'-fPIC','-fno-builtin','-fno-stack-protector','-fvisibility=hidden','-shared',str(ROOT/'apps/src/printer_link.c'),'-o',str(p/'printer.so')],check=True)
    subprocess.run([cc,*flags,str(ROOT/'tools/printer_link_host_test.c'),'-ldl','-o',str(p/'test')],check=True)
    subprocess.run([str(p/'test'),str(p/'printer.so')],check=True)

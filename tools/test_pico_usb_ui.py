#!/usr/bin/env python3
"""Actual USB page state tests with USB/card doubles; no physical USB access.
使用USB/卡替身检查真实页面，不操作真实USB。
"""
from pathlib import Path
import argparse,os,shlex,subprocess,tempfile
from test_pico_storage import prepare_stubs
ROOT=Path(__file__).resolve().parents[1]
def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--sanitize',action='store_true');args=parser.parse_args()
    with tempfile.TemporaryDirectory(prefix='pico-usb-ui-') as tmp:
        build=Path(tmp);prepare_stubs(build);exe=build/'test'
        command=[os.environ.get('CC','cc'),'-std=c11','-O1','-g','-Wall','-Wextra','-Werror']+shlex.split(os.environ.get('CFLAGS',''))
        if args.sanitize:command+=['-fsanitize=address,undefined','-fno-omit-frame-pointer']
        for path in [build,ROOT/'main/system',ROOT/'components/read_pico_usb/include',ROOT/'components/read_pico/include']:command+=['-I',str(path)]
        subprocess.run(command+[str(ROOT/'tools/pico_usb_ui_host_test.c'),'-o',str(exe)],check=True);subprocess.run([str(exe)],check=True)
if __name__=='__main__':main()

#!/usr/bin/env python3
"""Exercise the actual product connection page without hardware. / 不连接硬件，检查真实产品连接页面。"""
from pathlib import Path
import argparse
import os
import subprocess
import tempfile

ROOT=Path(__file__).resolve().parents[1]

def flags():
    result=['-std=gnu11','-O1','-Wall','-Wextra','-Werror','-g']
    for path in ['tools/pico_reader_stubs','tools/preview_reading_stubs','main/system','main/ui','components/read_pico_transfer/include','components/read_pico/include','tools/pico_usb_stubs']:
        result+=['-I',str(ROOT/path)]
    return result

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--sanitize',action='store_true')
    args=parser.parse_args()
    with tempfile.TemporaryDirectory(prefix='pico-connectivity-ui-') as tmp:
        binary=Path(tmp)/'test'
        command=[os.environ.get('CC','cc')]+flags()
        if args.sanitize:command+=['-fsanitize=address,undefined','-fno-omit-frame-pointer']
        command+=[str(ROOT/'tools/pico_connectivity_host_test.c'),'-o',str(binary)]
        subprocess.run(command,check=True)
        subprocess.run([str(binary)],check=True)

if __name__=='__main__':main()

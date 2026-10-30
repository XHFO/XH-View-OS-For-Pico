#!/usr/bin/env python3
"""Run production dual-reader protocol over real loopback sockets and separate book roots."""
from pathlib import Path
import argparse, subprocess
root=Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser();p.add_argument('--sanitize',action='store_true');a=p.parse_args()
out=root/'build/dual-link-test';out.mkdir(parents=True,exist_ok=True)
binary=out/('test-asan' if a.sanitize else 'test')
flags=['-fsanitize=address,undefined','-fno-omit-frame-pointer'] if a.sanitize else []
subprocess.run(['cc','-std=gnu11','-O1','-g','-Wall','-Wextra','-Wno-deprecated-declarations',*flags,str(root/'tools/test_dual_link.c'),'-o',str(binary)],check=True)
subprocess.run([str(binary)],check=True)

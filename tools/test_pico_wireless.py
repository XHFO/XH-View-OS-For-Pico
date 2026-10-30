#!/usr/bin/env python3
"""Run production USB wireless contracts, normal and ASan/UBSan, without hardware.
不连接设备，验证真实USB无线协议及其内存边界。
"""
from pathlib import Path
import hashlib
import json
import subprocess
import tempfile
from test_pico_connectivity import flags

ROOT=Path(__file__).resolve().parents[1]
names=['main/system/pico_wireless.inc','main/system/pico_connectivity.h','tools/pico_wireless_host_test.c']
hashes={p:hashlib.sha256((ROOT/p).read_bytes()).hexdigest() for p in names}
runs=[]
with tempfile.TemporaryDirectory(prefix='pico-wireless-') as temp:
    for sanitized in (False,True):
        binary=Path(temp)/('sanitized' if sanitized else 'normal')
        command=['cc']+flags()
        if sanitized:command+=['-fsanitize=address,undefined','-fno-omit-frame-pointer']
        subprocess.run(command+[str(ROOT/'tools/pico_wireless_host_test.c'),'-o',str(binary)],check=True)
        result=subprocess.run([str(binary)],text=True,capture_output=True)
        print(result.stdout,end='')
        if result.returncode:print(result.stderr,end='');result.check_returncode()
        runs.append({'sanitized':sanitized,'output':result.stdout.strip()})
assert hashes=={p:hashlib.sha256((ROOT/p).read_bytes()).hexdigest() for p in names}
output=ROOT/'build/wireless-contract/report.json';output.parent.mkdir(exist_ok=True)
output.write_text(json.dumps({'result':'PASS','hardware_access':False,'source_sha256':hashes,'runs':runs},indent=2)+'\n')

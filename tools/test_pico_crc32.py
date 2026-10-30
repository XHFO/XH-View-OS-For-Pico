#!/usr/bin/env python3
"""中文：以官方ROM兼容实现与zlib核对存档CRC及任意分块，不连接设备。
English: Compare actual storage CRC paths with the official ROM-compatible implementation and zlib, without hardware.
"""
from pathlib import Path
import argparse
import ctypes
import hashlib
import json
import os
import random
import re
import shlex
import subprocess
import tempfile
import zlib

ROOT=Path(__file__).resolve().parents[1]

def extract(source,name):
    start=re.search(r'(?:^|\n)(static uint32_t '+name+r'\([^\n]+\) \{)',source).start(1)
    at=source.index('{',start);depth=0
    for end in range(at,len(source)):
        if source[end]=='{':depth+=1
        if source[end]=='}':
            depth-=1
            if not depth:return source[start:end+1]
    raise ValueError(name)

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output',type=Path)
    args=parser.parse_args()
    path=ROOT/'main/system/pico_system.c';source=path.read_text()
    implementation='\n'.join(extract(source,name) for name in ['crc32_update','crc32'])
    vendor=ROOT.parent/'esp-idf-v6.1/components/esp_rom/patches/esp_rom_crc.c'
    checks=0
    with tempfile.TemporaryDirectory(prefix='pico-crc-') as temp:
        temp=Path(temp)
        (temp/'esp_rom_caps.h').write_text('#define ESP_ROM_HAS_CRC_BE 1\n#define ESP_ROM_HAS_CRC_LE 0\n')
        body='#include <stdint.h>\n#include <stddef.h>\nuint32_t esp_rom_crc32_le(uint32_t,const uint8_t*,uint32_t);\n'+implementation+'\nuint32_t test_crc(uint32_t previous,const void *p,size_t n){return previous?crc32_update(previous,p,n):crc32(p,n);}\n'
        (temp/'crc.c').write_text(body)
        libraries=[]
        for mode in ['host','rom']:
            out=temp/(mode+'.dylib')
            cmd=['cc','-std=c11','-O2','-shared','-fPIC','-Wall','-Wextra','-Werror','-I'+str(temp),str(temp/'crc.c')]
            if mode=='rom':cmd+=['-DESP_PLATFORM',str(vendor)]
            cmd+=['-o',str(out)]+shlex.split(os.environ.get('CFLAGS',''))
            subprocess.run(cmd,check=True)
            lib=ctypes.CDLL(str(out));lib.test_crc.argtypes=[ctypes.c_uint32,ctypes.c_void_p,ctypes.c_size_t];lib.test_crc.restype=ctypes.c_uint32
            libraries.append(lib)
        rng=random.Random(50)
        values=[b'',b'123456789',bytes(range(256)),b'\x00'*65536,b'\xff'*65536]
        values+=[rng.randbytes(rng.randrange(65537)) for _ in range(100)]
        assert zlib.crc32(b'123456789')==0xcbf43926
        for data in values:
            expected=zlib.crc32(data);buffer=ctypes.create_string_buffer(data)
            for lib in libraries:
                assert lib.test_crc(0,buffer,len(data))==expected;checks+=1
                for block in [1,17,256,4096]:
                    crc=0
                    for at in range(0,len(data),block):
                        crc=lib.test_crc(crc,ctypes.byref(buffer,at),min(block,len(data)-at))
                    assert crc==expected;checks+=1
                assert lib.test_crc(expected,buffer,0)==expected;checks+=1
    report={'result':'PASS','checks':checks,'source_sha256':hashlib.sha256(path.read_bytes()).hexdigest(),'vendor_path':str(vendor),'vendor_sha256':hashlib.sha256(vendor.read_bytes()).hexdigest(),'known_vector':'cbf43926','max_payload':65536,'modes':['actual host bitwise path','actual ESP branch against official ROM-compatible implementation'],'hardware_access':False}
    if args.output:
        args.output.mkdir(parents=True,exist_ok=True);(args.output/'report.json').write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps(report,indent=2))

if __name__=='__main__':main()

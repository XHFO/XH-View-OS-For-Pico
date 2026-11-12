#!/usr/bin/env python3
"""Single version source and actual firmware comparison boundary regression."""
from pathlib import Path
import argparse,re,subprocess
root=Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser();p.add_argument('--output',type=Path,required=True);p.add_argument('--image',type=Path);a=p.parse_args();a.output.mkdir(parents=True,exist_ok=True)
version=re.search(r'#define PICO_FIRMWARE_VERSION "([^"]+)"',(root/'main/system/pico_version.h').read_text())[1]
assert 'pico_version.h' in (root/'CMakeLists.txt').read_text()
assert '#define PS_VERSION PICO_FIRMWARE_VERSION' in (root/'main/system/pico_system.c').read_text()
if a.image:
    data=a.image.read_bytes();assert data[0]==0xe9
    assert data[0x30:0x50].split(b'\0')[0].decode()==version
source=(root/'main/system/pico_updates.c').read_text()
code='#include <stdbool.h>\n#include <stdlib.h>\n#include <string.h>\n#include <assert.h>\nstatic const char* current;\nstatic const char* ps_version(void){return current;}\n'
code+=source[source.index('static int suffix('):source.index('static bool line_value(')]
code+='''
int main(void){
 current="0.40.0-dual99";assert(newer_than_current("0.40.0-dual100"));
 current="0.40.0-dual100";assert(!newer_than_current("0.40.0-dual99"));assert(!newer_than_current("0.40.0-dual100"));assert(newer_than_current("0.40.0-dual101"));
 current="0.40.0-dual101";assert(!newer_than_current("0.40.0-dual100"));assert(!newer_than_current("0.40.0-dual101"));assert(newer_than_current("0.40.0-dual102"));
 assert(!newer_than_current("0.40.0-dual999999"));return 0;
}
'''
path=a.output/'version-contract.c';path.write_text(code);binary=a.output/'version-contract';subprocess.run(['cc','-fsanitize=address,undefined',str(path),'-o',str(binary)],check=True);subprocess.run([str(binary)],check=True)
print('PASS: CMake/UI/image share',version,'; 99/100/101/102 ordering, same version and downgrade rejection')

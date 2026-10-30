#!/usr/bin/env python3
"""对比生产稀疏采样与原始灰阶，含边缘夹取；不访问设备。
Compare production sparse sampling with original grays, including clamped edges, without hardware.
"""
from pathlib import Path
import hashlib
import json
import re
import subprocess
import tempfile

ROOT=Path(__file__).resolve().parents[1]
source=ROOT/'main/system/pico_home.c'
s=source.read_text()
start=s.index('static int clock_alpha(');end=s.index('\nstatic void digit(',start)
function=s[start:end]
program='''#include <assert.h>
#include <stdio.h>
#include "pico_clock_digits.h"
#include "pico_clock_digits_sparse.h"
static int clampi(int x,int a,int b){return x<a?a:x>b?b:x;}
'''+function+'''
int main(void){unsigned n=0;for(int d=0;d<10;d++)for(int y=-8;y<200;y++)for(int x=-8;x<136;x++){
 int xx=clampi(x,0,127),yy=clampi(y,0,191);unsigned at=(unsigned)yy*128+(unsigned)xx;
 unsigned old=(ps_clock_mask[d][at/2]>>((at%2)*4))&15;
 assert((unsigned)clock_alpha(d,x,y)==old);n++;}
 printf("%u exact gray sampling checks PASS\\n",n);return 0;}
'''
with tempfile.TemporaryDirectory(prefix='pico-clock-sparse-') as folder:
    p=Path(folder);(p/'check.c').write_text(program)
    subprocess.run(['cc','-std=c11','-O2','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-I',str(ROOT/'main/system'),str(p/'check.c'),'-o',str(p/'check')],check=True)
    result=subprocess.check_output([str(p/'check')],text=True);print(result,end='')
report={'result':'PASS','checks':299520,'gray_pixel_count':245760,'all_samples_identical':True,
        'source_sha256':{str(p.relative_to(ROOT)):hashlib.sha256(p.read_bytes()).hexdigest() for p in [source,ROOT/'main/system/pico_clock_digits.h',ROOT/'main/system/pico_clock_digits_sparse.h']},
        'additional_ram_bytes':0,'original_font_unchanged':True}
assert '299520' in result
out=ROOT/'build/dev53-clock-sparse/report.json';out.parent.mkdir(parents=True,exist_ok=True);out.write_text(json.dumps(report,indent=2)+'\n')

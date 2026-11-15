#!/usr/bin/env python3
"""生产 ESP 分支下载选择回归，模拟 HTTP、私有 TF。/ Production ESP branch selection tests with mock HTTP and private TF."""
import argparse,os,subprocess,tempfile
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser();p.add_argument('--stubs',type=Path,required=True);p.add_argument('--source',type=Path);p.add_argument('--output',type=Path,required=True);a=p.parse_args()
a.output.mkdir(parents=True,exist_ok=True)
cmd=[os.environ.get('CC','cc'),'-std=c11','-g','-Wall','-Wextra','-ffunction-sections','-fdata-sections','-fsanitize=address,undefined','-fno-omit-frame-pointer','-DESP_PLATFORM=1']
for path in [ROOT/'tools/updates_host_stubs',a.stubs,ROOT/'tools',ROOT/'main/system',ROOT/'main/app',ROOT/'components/read_pico_apps/include']:cmd+=['-I',str(path.resolve())]
if a.source:cmd+=['-DPICO_UPDATES_TEST_SOURCE="'+str(a.source.resolve())+'"']
exe=a.output.resolve()/'updates-download-test'
cmd+=[str(ROOT/'tools/updates_download_host_test.c'),'-Wl,-dead_strip','-o',str(exe)]
subprocess.run(cmd,cwd=ROOT,check=True)
with tempfile.TemporaryDirectory(prefix='selected-download-',dir=a.output) as session:
    subprocess.run([str(exe),session],check=True)

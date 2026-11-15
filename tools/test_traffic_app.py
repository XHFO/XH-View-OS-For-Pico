#!/usr/bin/env python3
"""真实TF看板模块隔离测试。/ Isolated checks of the real TF analytics module."""
from pathlib import Path
import os, subprocess, tempfile, sys
ROOT=Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix='traffic-check-') as tmp:
    tmp=Path(tmp)
    for san in (False,True):
        flags=['-std=gnu11','-O1','-g','-Wall','-Wextra','-Werror','-I',str(ROOT/'components/read_pico_apps/include')]
        if san:flags+=['-fsanitize=address,undefined','-fno-omit-frame-pointer']
        library=tmp/('traffic.dylib' if sys.platform=='darwin' else 'traffic.so')
        subprocess.run([os.environ.get('CC','cc'),*flags,'-fno-builtin','-fno-stack-protector','-fPIC','-fvisibility=hidden','-dynamiclib' if sys.platform=='darwin' else '-shared',str(ROOT/'apps/src/website_traffic.c'),'-o',str(library)],check=True)
        binary=tmp/'checks'
        subprocess.run([os.environ.get('CC','cc'),*flags,str(ROOT/'tools/traffic_app_host_test.c'),'-o',str(binary),*(['-ldl'] if sys.platform!='darwin' else [])],check=True)
        env=os.environ.copy();env.update(ASAN_OPTIONS='halt_on_error=1:detect_stack_use_after_return=1',UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1')
        print('ASan/UBSan' if san else 'normal',flush=True)
        subprocess.run([str(binary),str(library)],env=env,check=True)

#!/usr/bin/env python3
"""中文：应用包格式验证正常及sanitizer测试。 / English: Package validator checks, normal and sanitizers."""
from pathlib import Path
import os,subprocess,tempfile
ROOT=Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix='pico-app-package-') as temp:
    temp=Path(temp)
    for sanitize in (False,True):
        flags=['-std=c11','-Wall','-Wextra','-Werror','-Wno-misleading-indentation','-O1','-g']
        if sanitize:flags+=['-fsanitize=address,undefined','-fno-omit-frame-pointer']
        binary=temp/('checks-san' if sanitize else 'checks')
        subprocess.run([os.environ.get('CC','cc'),*flags,'-I',str(ROOT/'components/read_pico_apps/include'),str(ROOT/'tools/pico_app_package_host_test.c'),str(ROOT/'components/read_pico_apps/pico_app_package.c'),'-o',str(binary)],check=True)
        environment=os.environ.copy()
        if sanitize:
            environment['ASAN_OPTIONS']='halt_on_error=1:detect_stack_use_after_return=1'
            environment['UBSAN_OPTIONS']='halt_on_error=1:print_stacktrace=1'
        print('PGA1 canonical header / UTF-8 / bounds / payload CRC:', 'ASan+UBSan' if sanitize else 'normal',flush=True)
        subprocess.run([str(binary),str(temp/'test.pga')],check=True,env=environment)

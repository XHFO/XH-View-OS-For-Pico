#!/usr/bin/env python3
"""Execute actual hardware-side product power control with fault-injected platform boundaries."""
from pathlib import Path
import argparse
import subprocess
import tempfile
from pico_i18n_host import sources_and_flags
ROOT=Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser(description=__doc__);p.add_argument('--sanitize',action='store_true');args=p.parse_args()
with tempfile.TemporaryDirectory(prefix='pico-power-host-') as temp:
    temp=Path(temp)
    i18n_sources,i18n_flags=sources_and_flags(temp)
    for name,target in [('main/system/pico_power.c','power_under_test.c'),('main/system/pico_power.h','power_api_under_test.h')]:
        temp.joinpath(target).write_text('\n'.join(line for line in (ROOT/name).read_text().splitlines() if not line.lstrip().startswith('#include '))+'\n')
    flags=['-std=gnu11','-O1','-g','-Wall','-Wextra','-Werror','-I'+str(temp)]
    if args.sanitize:flags+=['-fsanitize=address,undefined','-fno-omit-frame-pointer']
    subprocess.run(['cc',*flags,*i18n_flags,str(ROOT/'tools/pico_power_host_test.c'),*i18n_sources,'-lz','-o',str(temp/'test')],check=True)
    subprocess.run([str(temp/'test')],check=True)

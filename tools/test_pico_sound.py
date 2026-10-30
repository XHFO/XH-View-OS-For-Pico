#!/usr/bin/env python3
"""Run actual sound policy/worker with RTOS and GPIO boundary doubles, without any hardware."""
from pathlib import Path
import argparse, hashlib, json, os, subprocess, tempfile
ROOT=Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--sanitize',action='store_true')
p.add_argument('--output',type=Path)
args=p.parse_args()
tracked=[ROOT/f for f in ['main/system/pico_sound.c','main/system/pico_sound.h','tools/pico_sound_host_test.c','tools/test_pico_sound.py']]
hashes={str(f.relative_to(ROOT)):hashlib.sha256(f.read_bytes()).hexdigest() for f in tracked}
with tempfile.TemporaryDirectory(prefix='pico-sound-host-') as folder:
    work=Path(folder)
    for source,target in [('main/system/pico_sound.c','sound_under_test.c'),('main/system/pico_sound.h','sound_api_under_test.h')]:
        text='\n'.join(s for s in (ROOT/source).read_text().splitlines() if not s.lstrip().startswith('#include '))+'\n'
        if target=='sound_under_test.c':
            # Scheduling-only probe between busy publication and the real pending exchange.
            marker='            unsigned event=atomic_exchange(&pending,PS_SOUND_NONE);'
            assert text.count(marker)==1
            text=text.replace(marker,'            sound_claim_probe();\n'+marker)
        work.joinpath(target).write_text(text)
    flags=['-std=gnu11','-O1','-g','-pthread','-Wall','-Wextra','-Werror','-Wno-unused-function','-I'+str(work)]
    if args.sanitize:flags+=['-fsanitize=address,undefined','-fno-omit-frame-pointer']
    command=[os.environ.get('CC','cc'),*flags,str(ROOT/'tools/pico_sound_host_test.c'),'-o',str(work/'test')]
    subprocess.run(command,check=True)
    result=subprocess.run([str(work/'test')],check=True,capture_output=True,text=True)
    print(result.stdout,end='')
    assert hashes=={str(f.relative_to(ROOT)):hashlib.sha256(f.read_bytes()).hexdigest() for f in tracked}
    if args.output:
        args.output.mkdir(parents=True,exist_ok=True)
        (args.output/'sound-checks.txt').write_text(result.stdout+result.stderr)
        (args.output/'source-binding.json').write_text(json.dumps({'result':'PASS','sha256':hashes,'scope':'Actual worker and policy; pthread/recorded tone replace hardware only. No physical sound or power measurement.'},indent=2)+'\n')

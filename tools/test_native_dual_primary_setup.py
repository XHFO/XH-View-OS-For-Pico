#!/usr/bin/env python3
"""Touch-test primary transport choices; simulated networking, no hardware."""
import argparse, hashlib, json, struct, tempfile, zlib
from pathlib import Path
from test_native_preview_shelf_ble import Runtime, ROOT

p=argparse.ArgumentParser();p.add_argument('--binary',type=Path,required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args()
out=a.output.resolve();out.mkdir(parents=True,exist_ok=True);session=Path(tempfile.mkdtemp(prefix='primary-setup-',dir=out))
r=Runtime(a.binary.resolve(),ROOT/'sdcard/fonts/MiSansVF.ttf',session,out)
checks=[]
def stored():
 files=list(session.glob('tf/**/dualconfig1.dat'))
 if not files:return None
 d=files[0].read_bytes();magic,size,crc=struct.unpack_from('<III',d);payload=d[12:]
 assert magic==0x31535058 and len(payload)==size and zlib.crc32(payload)==crc
 return {'role':payload[4],'host':payload[5:53].split(b'\0')[0].decode(),
         'code':payload[53:60].split(b'\0')[0].decode(),'net':payload[60],'keep':payload[61]}
def tap(x,y):
 s=r.tap(x,y);r.tick(100);return s
try:
 r.start();r.command('rotate 0');r.command('route 34')
 assert stored() is None
 s=tap(179,124)
 assert s['route']==34 and s['dual_role']==0 and stored() is None,s
 r.capture('primary-transport-choice',s)
 checks.append('selecting-primary-does-not-save-before-transport-choice')
 s=tap(180,260)
 assert s['route']==34 and stored() is None,s
 r.capture('primary-after-sync-choice',s)
 checks.append('lan-choice-requires-post-sync-policy')
 s=tap(180,348)
 state=stored();assert s['route']==34 and s['dual_role']==1,s
 assert state and state['role']==1 and state['net']==1 and state['keep']==0,state
 checks.append('lan-plus-ble-only-saved')
 s=tap(179,580)
 assert s['route']==34 and stored()==state,s
 s=tap(180,260)
 assert s['route']==34 and stored()==state,s
 s=tap(180,290)
 state=stored();assert s['route']==34 and s['dual_role']==1,s
 assert state and state['net']==1 and state['keep']==1,state
 checks.append('lan-plus-keep-data-saved')
 s=tap(179,580)
 assert s['route']==34 and stored()==state,s
 r.capture('primary-transport-change',s)
 s=tap(180,318)
 assert s['route']==34 and stored()==state,s
 s=tap(180,290)
 state=stored();assert s['route']==34 and s['dual_role']==1,s
 assert state and state['role']==1 and state['net']==2 and state['keep']==1,state
 r.capture('primary-hotspot-kept',s)
 checks.append('hotspot-plus-keep-data-saved')
finally:
 r.close();(session/'transcript.json').write_text(json.dumps(r.transcript,ensure_ascii=False,indent=2))
(out/'report.json').write_text(json.dumps({'passed':True,'hardware_access':False,'binary_sha256':hashlib.sha256(a.binary.read_bytes()).hexdigest(),'checks':checks},ensure_ascii=False,indent=2)+'\n')
print('PASS',checks)

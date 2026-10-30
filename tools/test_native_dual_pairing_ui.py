#!/usr/bin/env python3
"""真实触摸验证地址→配对码及持久化；模拟网络，不接触硬件。
Exercise address-to-code navigation and persistence through real touch handlers; simulated network, no hardware.
"""
import argparse, hashlib, json, struct, tempfile, zlib
from pathlib import Path
from test_native_preview_shelf_ble import Runtime, ROOT

p=argparse.ArgumentParser();p.add_argument('--binary',type=Path,required=True);p.add_argument('--output',type=Path,required=True);p.add_argument('--legacy-repro',action='store_true');a=p.parse_args()
out=a.output.resolve();out.mkdir(parents=True,exist_ok=True);checks=[]
for wide in ([False] if a.legacy_repro else [False,True]):
 session=Path(tempfile.mkdtemp(prefix='wide-' if wide else 'tall-',dir=out))
 r=Runtime(a.binary.resolve(),ROOT/'sdcard/fonts/MiSansVF.ttf',session,out)
 def stored():
  files=list(session.glob('tf/**/dualconfig1.dat'))
  if not files:return None
  d=files[0].read_bytes();magic,size,crc=struct.unpack_from('<III',d);payload=d[12:]
  assert len(payload)==size and zlib.crc32(payload)==crc
  return {'role':payload[4],'host':payload[5:53].split(b'\0')[0].decode(),'code':payload[53:60].split(b'\0')[0].decode()}
 def tap(x,y):
  s=r.tap(x,y);r.tick(100);return s
 def digit(c):
  x,w,y,ph=(224,400,86,51) if wide else (16,328,358,54)
  col,row=(1,3) if c=='0' else (0,3) if c=='.' else (2,3) if c=='<' else ((int(c)-1)%3,(int(c)-1)//3)
  return tap(x+col*(w//3)+(w//3-8)//2,y+row*ph+(ph-6)//2)
 def type_(text):
  for c in text:digit(c)
 def submit():return tap((640 if wide else 360)-46,47)
 def begin():
  r.command('route 34');s=tap(550 if wide else 290,124);assert s['route']==19,s
 def clear(n):
  for _ in range(n):digit('<')
 try:
  r.start();r.command('rotate '+str(int(wide)));begin()
  if a.legacy_repro:
   tap(307,293)
   for c in '192.168.50.7':
    tap(76,506) if c=='.' else tap(8+'1234567890'.index(c)*34+15,410)
   s=submit();assert s['route']==34 and s['dual_role']==0 and stored() is None,s
   r.capture('dual5-broken-code-step',s);checks.append('dual5-address-submit-closes-code-editor');continue
  tag='landscape' if wide else 'portrait'
  r.capture(tag+'-host-input',r.command('status'))
  # Invalid input stays editable; back cancels without creating a configuration.
  for bad in ('','0','255','256','123'):
   type_(bad);s=submit();assert s['route']==19 and stored() is None,s;clear(len(bad))
  tap(22,46);assert r.command('status')['route']==34 and stored() is None
  checks.append(tag+'-invalid-address-and-cancel')
  # A one-digit suffix resolves against the live IP prefix; five-digit codes stay in step two.
  begin();type_('7');s=submit();assert s['route']==19 and stored() is None,s
  r.tick(3000);r.capture(tag+'-code-input',r.command('status'));type_('12345');s=submit();assert s['route']==19 and stored() is None,s
  digit('6');s=submit();assert s['route']==34 and s['dual_role']==2,s
  assert stored()=={'role':2,'host':'192.168.50.7','code':'123456'},stored()
  checks.append(tag+'-next-step-short-address-code-validation-save')
  # Reconfiguration cancellation leaves the original pair intact.
  before=stored();begin();type_('8');submit();tap(22,46);assert stored()==before
  # Full IPv4 remains available on the same keypad, including a different subnet.
  begin();type_('10.0.2.9');submit();type_('654321');submit();assert stored()['host']=='10.0.2.9'
  checks.append(tag+'-cancel-preserves-pair-and-full-ip')
  r.close();r.start();assert r.command('status')['dual_role']==2 and stored()['code']=='654321'
  r.command('rotate '+str(int(wide)));r.command('route 34');r.command('scenario offline');s=tap(550 if wide else 290,124)
  assert s['route']==34 and stored()['host']=='10.0.2.9'
  checks.append(tag+'-restart-and-offline-guard')
 finally:
  r.close();(session/'transcript.json').write_text(json.dumps(r.transcript,ensure_ascii=False,indent=2))
(out/'report.json').write_text(json.dumps({'passed':True,'hardware_access':False,'binary_sha256':hashlib.sha256(a.binary.read_bytes()).hexdigest(),'checks':checks},ensure_ascii=False,indent=2))
print('PASS',len(checks),checks)

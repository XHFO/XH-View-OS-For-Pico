#!/usr/bin/env python3
"""BLE-first pair discovery through production UI handlers; simulated radio, no hardware."""
import argparse, hashlib, json, socket, struct, tempfile, zlib
from pathlib import Path
from test_native_preview_shelf_ble import Runtime, ROOT

p=argparse.ArgumentParser();p.add_argument('--binary',type=Path,required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args()
out=a.output.resolve();out.mkdir(parents=True,exist_ok=True);checks=[]
def stored(session):
 files=list(session.glob('tf/**/dualconfig1.dat'))
 if not files:return None
 d=files[0].read_bytes();magic,size,crc=struct.unpack_from('<III',d);payload=d[12:]
 assert magic==0x31535058 and len(payload)==size and zlib.crc32(payload)==crc
 return {'role':payload[4],'host':payload[5:53].split(b'\0')[0].decode(),
         'code':payload[53:60].split(b'\0')[0].decode(),'net':payload[60],'keep':payload[61]}
def digit(r,c):
 x,w,y,ph=16,328,358,54
 col,row=(1,3) if c=='0' else ((int(c)-1)%3,(int(c)-1)//3)
 r.tap(x+col*(w//3)+(w//3-8)//2,y+row*ph+(ph-6)//2)
def submit(r):return r.tap(360-46,47)
for direct,host,ssid,tag in ((1,'192.168.4.1','PicoDual-ABCD','hotspot'),(0,'192.168.50.7','','lan'),(0,'192.168.50.7','','lan-udp')):
 session=Path(tempfile.mkdtemp(prefix=tag+'-',dir=out))
 r=Runtime(a.binary.resolve(),ROOT/'sdcard/fonts/MiSansVF.ttf',session,out)
 try:
  r.start();r.command('rotate 0');r.command('route 34')
  state=r.command('dual-scan');assert state['route']==34 and stored(session) is None,state
  if tag=='lan-udp':
   with socket.socket(socket.AF_INET,socket.SOCK_DGRAM) as sender:
    sender.sendto(f'PICOPAIR2 {host} -'.encode(),('127.0.0.1',18772))
  else:r.command(f'dual-peer {host} {direct} {ssid or "lan"}')
  state=r.tick(100)
  assert state['route']==19 and stored(session) is None,state
  r.capture(tag+'-ble-code-prompt',state)
  for c in '123456':digit(r,c)
  state=submit(r)
  saved=stored(session)
  assert state['route']==34 and state['dual_role']==2,saved
  assert saved and saved['role']==2 and saved['host']==host and saved['code']=='123456',saved
  checks.append(tag+'-discovery-opens-code-and-saves-endpoint')
 finally:
  r.close();(session/'transcript.json').write_text(json.dumps(r.transcript,ensure_ascii=False,indent=2))
(out/'report.json').write_text(json.dumps({'passed':True,'hardware_access':False,'binary_sha256':hashlib.sha256(a.binary.read_bytes()).hexdigest(),'checks':checks},ensure_ascii=False,indent=2)+'\n')
print('PASS',checks)

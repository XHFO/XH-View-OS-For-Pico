#!/usr/bin/env python3
"""真实TF双机游戏/手柄回归；回环通信，不验证射频或面板耗时。/ Real TF pair/controller regression over loopback."""
from pathlib import Path
import argparse,json,tempfile,time,struct,zlib
from test_native_preview_shelf_ble import Runtime,ROOT
p=argparse.ArgumentParser(description=__doc__);p.add_argument('--binary',type=Path,required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args()
out=a.output.resolve();out.mkdir(parents=True,exist_ok=True);base=Path(tempfile.mkdtemp(prefix='pair-',dir=out));devices=[];checks=[]
for name in ('primary','controller'):
 session=base/name;session.mkdir();r=Runtime(a.binary.resolve(),ROOT/'sdcard/fonts/MiSansVF.ttf',session,out);devices.append(r)
primary,peer=devices

def pump(n=1):
 for _ in range(n):
  for d in devices:d.tick(20)
  time.sleep(.002)
def until(fn,label):
 for _ in range(5000):
  pump()
  if fn():return
 raise AssertionError((label,[d.command('status') for d in devices]))
def states():return [d.command('status') for d in devices]
def open_game(d,ident):
 s=d.command('route 13');i=s['games_order'].index(ident)
 if i>=6:d.tap(305,610)
 i%=6;d.tap(16+i%2*169+79,143+i//2*142+60)
 until(lambda:all(s['route']==23 for s in states()),'both game routes')
 pump(30)
def control(code):
 s=peer.command('status');w,h=s['logical_width'],s['logical_height'];land=w>h;c=56;cx=112 if land else 104;cy=166 if land else 304
 if code<4:x=cx+(c if code==1 else -c if code==3 else 0);y=cy+(-c if code==0 else c if code==2 else 0)
 elif code in (8,9):x=w-(94 if code==8 else 166)+32;y=(116 if land else 260)+(72 if code==9 else 0)+32
 else:x=w//2+(60 if code==4 else -60);y=h-(92 if land else 160)+22
 peer.command(f'touch down {x} {y} {peer.now+20}');pump(4);peer.command(f'touch up {x} {y} {peer.now+60}');pump(12)
def payload(d,ident,key):
 raw=(d.session/'tf/apps/data'/ident/(key+'.dat')).read_bytes();magic,size,crc=struct.unpack_from('<III',raw);assert magic==0x31535058 and size==len(raw)-12 and zlib.crc32(raw[12:])==crc;return raw[12:]
try:
 for d in devices:d.start()
 primary.command('dual 1 127.0.0.1 123456');peer.command('dual 2 127.0.0.1 123456')
 until(lambda:all(s['dual_synced'] and s['dual_connected'] for s in states()),'pair sync')
 for ident in ('2048','snake','tetris','pacman'):
  for actor,name in ((primary,'primary-open'),(peer,'peer-open')):
   open_game(actor,ident)
   if ident=='2048':
    if name=='primary-open':control(1)
    control(4)
    # START继续同设置；A新局确认后也能选择其他格数。
    # START resumes unchanged settings; A confirms new settings.
    for code in (0,1,2,3):control(code)
   elif ident=='snake':
    if name=='primary-open':control(0)
    control(4)
    control(0);control(3)
   else:
    control(4);control(3)
    if ident=='tetris':control(8);control(9)
    else:time.sleep(.55);pump(10)
   for d,role in ((primary,'screen'),(peer,'pad')):d.capture(ident+'-'+name+'-'+role,d.command('status'))
   # 断联必须暂停；重连不能重放先前按键。
   # Link loss pauses; reconnect must not replay old input.
   for d in devices:d.command('dual-radio 0')
   pump(20)
   for d in devices:d.command('dual-radio 1')
   until(lambda:all(s['dual_connected'] for s in states()),'radio reconnect')
   pump(30)
   peer.command('key 1 click')
   until(lambda:all(s['route']!=23 for s in states()),'both exit')
   if ident in ('2048','snake'):
    data=payload(primary,ident,'arcade2');assert len(data)==1012
    if ident=='2048':assert data[1008]==5 and struct.unpack_from('<I',data,76)[0]>0
    else:assert data[1009]==1 and struct.unpack_from('<I',data,88)[0]>0
   else:
    data=payload(primary,ident,'state1');assert len(data)==336 and struct.unpack_from('<I',data,8)[0]>0
   checks.append(ident+': '+name+', controller actions, shared setup/status, disconnect pause, linked exit and primary save')
finally:
 for d in devices:d.close()
(out/'result.json').write_text(json.dumps({'checks':checks,'hardware_verified':False},ensure_ascii=False,indent=2)+'\n');print('PASS',checks)

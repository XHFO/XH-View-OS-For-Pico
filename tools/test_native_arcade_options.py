#!/usr/bin/env python3
"""实际TF格数/模式设置、取消及截图。/ Real TF grid/mode selection, cancellation and captures."""
from pathlib import Path
import argparse,json,tempfile,struct,zlib,time
from test_native_preview_shelf_ble import Runtime,ROOT
p=argparse.ArgumentParser(description=__doc__);p.add_argument('--binary',type=Path,required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args()
out=a.output.resolve();out.mkdir(parents=True,exist_ok=True);session=Path(tempfile.mkdtemp(prefix='options-',dir=out));r=Runtime(a.binary.resolve(),ROOT/'sdcard/fonts/MiSansVF.ttf',session,out);checks=[]
def open_game(ident):
 r.command('route 13');s=r.command('rotate 0');i=s['games_order'].index(ident)
 if i>=6:r.tap(305,610)
 i%=6;s=r.tap(16+i%2*169+79,143+i//2*142+60);assert s['route']==23 and not s['games_error'];return s

def payload(ident):
 raw=(session/'tf/apps/data'/ident/'arcade2.dat').read_bytes();magic,size,crc=struct.unpack_from('<III',raw);assert magic==0x31535058 and size==1012 and zlib.crc32(raw[12:])==crc;return raw[12:]
try:
 r.start()
 for grid in (3,4,5):
  s=open_game('2048');r.capture('2048-setup-'+str(grid),s)
  r.tap(68+(grid-3)*112,192);s=r.tap(180,586)
  data=payload('2048')
  if data[1008]!=grid:s=r.tap(180,586)
  assert payload('2048')[1008]==grid
  r.capture('2048-grid-'+str(grid),s)
  for x,y in ((120,220),(220,220),(180,160),(180,280)):
   r.command(f'touch down 180 220 {r.now+20}');r.command(f'touch move {x} {y} {r.now+60}');r.command(f'touch up {x} {y} {r.now+60}')
  r.command('key 1 click');before=payload('2048');assert struct.unpack_from('<I',before,76)[0]>0
  open_game('2048');r.tap(68+((grid-2)%3)*112,192);r.tap(180,586);r.capture('2048-confirm-'+str(grid),r.command('status'))
  r.tap(180,530);r.tap(180,530);r.command('key 1 click');assert payload('2048')==before
  checks.append(f'{grid}x{grid}: applied only for new game; cancel preserves played save')
 for wrap in (0,1):
  s=open_game('snake');assert s['logical_width']==640 and s['logical_height']==360;r.tap(166+wrap*308,136);r.capture('snake-mode-'+str(wrap),r.command('status'));r.tap(320,306)
  if payload('snake')[1009]!=wrap:r.tap(320,306)
  assert payload('snake')[1009]==wrap
  time.sleep(.5);s=r.tick(500);r.capture('snake-play-'+str(wrap),s);r.command('key 1 click');checks.append('snake mode '+str(wrap)+': persisted')
finally:r.close()
(out/'result.json').write_text(json.dumps({'checks':checks,'real_tf_modules':True,'hardware_verified':False},ensure_ascii=False,indent=2)+'\n');print('PASS',checks)

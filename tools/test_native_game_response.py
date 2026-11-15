#!/usr/bin/env python3
"""真实包快速刷新回归；不代表实屏耗时。/ Actual package refresh regression, not panel timing."""
from pathlib import Path
import argparse
import json
import tempfile
import time
from test_native_preview_shelf_ble import Runtime, ROOT
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--binary',type=Path,required=True)
p.add_argument('--output',type=Path,required=True)
a=p.parse_args();out=a.output.resolve();out.mkdir(parents=True,exist_ok=True)
session=Path(tempfile.mkdtemp(prefix='games-',dir=out))
r=Runtime(a.binary.resolve(),ROOT/'sdcard/fonts/MiSansVF.ttf',session,out)
checks=[]
def open_game(ident):
 s=r.command('route 13');i=s['games_order'].index(ident)
 if i>=6:r.tap(305,610)
 i%=6
 s=r.tap(16+i%2*169+79,143+i//2*142+60)
 assert s['route']==23 and not s['games_error'],s
 return s
try:
 r.start()
 open_game('2048')
 r.tap(180,530)
 # 四个方向总能找到有效移动，达到阈值时就需要DU，不能等松手。
 # At least one direction moves; threshold feedback must be DU before release.
 for dx,dy in ((-45,0),(45,0),(0,-45),(0,45)):
  r.command(f'touch down 180 220 {r.now+20}')
  s=r.command(f'touch move {180+dx} {220+dy} {r.now+80}')
  r.command(f'touch up {180+dx} {220+dy} {r.now+60}')
  if s['refresh_mode']==1:break
 else:raise AssertionError('2048 had no early DU feedback')
 r.capture('2048-fast',s)
 time.sleep(1);s=r.tick(1000);assert s['refresh_mode']==5,s
 r.capture('2048-grayscale',s);checks.append('2048: DU before lift, grayscale settle')
 r.command('key 1 click')
 for ident in ('snake','tetris','pacman'):
  open_game(ident)
  if ident=='snake':
   assert r.command('status')['logical_width']==640
   r.tap(320,250)
   r.tap(548,292);x,y=80,90
  else:
   r.tap(312,568);x,y=40,568
  s=r.command(f'touch down {x} {y} {r.now+20}')
  assert s['refresh_mode']==1,(ident,s)
  r.capture(ident+'-press',s)
  r.command(f'touch up {x} {y} {r.now+60}')
  r.command('key 1 click');checks.append(ident+': DU during press, exit saves')
finally:r.close()
(out/'result.json').write_text(json.dumps({'checks':checks,'real_tf_modules':True,'physical_panel_verified':False},indent=2)+'\n')
print('PASS',checks)

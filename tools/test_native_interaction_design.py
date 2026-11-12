#!/usr/bin/env python3
"""Actual firmware gestures, unified menus and settings captures on private synthetic TF roots."""
import argparse,hashlib,json,os,tempfile
from pathlib import Path
from test_native_preview_shelf_ble import Runtime,ROOT,make_fixtures,stored_style
from test_native_shelf_cover_pages import stored_sort
p=argparse.ArgumentParser();p.add_argument('--binary',type=Path,required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args()
out=a.output.resolve();out.mkdir(parents=True,exist_ok=True)
os.environ.update(RPICO_HOME_LINK_OFFLINE='1',RPICO_WEREAD_OFFLINE='1')
report={'passed':False,'binary_sha256':hashlib.sha256(a.binary.read_bytes()).hexdigest(),'hardware':False,'checks':[],'scenes':{}}
for land in (False,True):
 label='landscape' if land else 'portrait';session=Path(tempfile.mkdtemp(prefix=label+'-',dir=out));books=session/'tf/books';books.mkdir(parents=True);make_fixtures(books)
 r=Runtime(a.binary.resolve(),ROOT/'sdcard/fonts/MiSansVF.ttf',session,out)
 try:
  r.start();r.command(f'rotate {int(land)}');r.command('route 0');r.settle(50)
  w,h=(640,360) if land else (360,640)
  def capture(name):
   state=r.command('status');r.capture(label+'-'+name,state);report['scenes'][label+'-'+name]=state
  def contact(start,end=None,hold=0,move=True):
   x,y=start;r.command(f'touch down {x} {y} {r.now+20}')
   if hold:r.tick(hold)
   if end:
    x,y=end
    if move:r.command(f'touch move {x} {y} {r.now+80}')
   return r.command(f'touch up {x} {y} {r.now+50}')
  for side in (0,1):
   x=18 if side==0 else w-18;dx=1 if side==0 else -1
   for end,hold in ((None,0),(None,700),((x+dx*5,h-24),0),((x,h-90),0),((x+dx*70,h-16),0),((x-dx*35,h-80),0)):
    state=contact((x,h-18),end,hold);assert not state['fan'] and not state['panel'],state
   r.command(f'touch down {x} {h-18} {r.now+20}')
   state=r.command(f'touch move {x+dx*40} {h-48} {r.now+80}');assert state['fan'],state
   r.command(f'touch up {x+dx*40} {h-48} {r.now+50}')
   state=contact((x,h-18),(x+dx*40,h-48),move=False)
   assert state['fan'] and not state['panel'],state
   r.tap(w-24,40)
  report['checks'].append(label+': both corners reject tap/hold/jitter/wrong axis/outward; deliberate and fast swipe open')
  capture('shelf')
  r.tap(530 if land else 230,146 if land else 263);capture('shelf-style-sort')
  r.tap(430 if land else 260,273 if land else 432);assert stored_sort(session)==1
  r.tap(180 if land else 90,273 if land else 432);assert stored_sort(session)==0
  r.tap(2,74);r.style(1,land);assert stored_style(session)==1
  report['checks'].append(label+': both sort options persist inside shelf style menu; styles retain working touch geometry')
  r.tap(530 if land else 230,100 if land else 106);assert r.command('status')['route']==2
  r.tap(23,47);assert r.command('status')['route']==0
  r.command('route 5');capture('settings-root')
  for group in range(1,6):
   rows,top,rh=(3,146,48) if land else (6,158,60);cw=(w-32-(12 if land else 0))//(2 if land else 1)
   r.tap(16+(group//rows)*(cw+12)+cw//2,top+(group%rows)*rh+rh//2)
   capture('settings-group-'+str(group))
   if group==3:
    r.tap(50,202 if land else 240);assert r.command('status')['quick_mode']==2
    capture('shortcut-settings-visible')
    r.tap(w-54,154);assert not r.command('status')['corner_indicators'];capture('shortcut-settings-hidden')
    r.tap(23,47)
   r.tap(23,47)
  r.command('route 0');capture('indicator-hidden');state=contact((18,h-18),(58,h-48),move=False);assert state['fan'];r.tap(w-24,40)
  assert not r.command('status')['corner_indicators']
  r.close();r.start();r.command(f'rotate {int(land)}');r.command('route 0');r.settle(40);assert not r.command('status')['corner_indicators']
  report['checks'].append(label+': hidden indicators persist across restart without disabling inward gesture')
  r.command('corner-indicators 1');r.settle(10);base=r.capture(label+'-notice-before',r.command('status'))
  state=r.command('toast 操作已完成');assert state['toast_active'];notice=r.capture(label+'-notice-top',state)
  top=round(24*1.9)
  assert base[2][top*base[0]:]==notice[2][top*base[0]:],'Notification altered body/footer'
  r.tick(3100);assert not r.command('status')['toast_active'];after=r.capture(label+'-notice-expired',r.command('status'));assert base[2]==after[2],'Status icons were not restored'
  report['checks'].append(label+': transient notice uses top bar only and restores exact prior framebuffer after expiry')
  r.command('book-select /sdcard/books/001-long.txt');r.settle(15)
  if r.command('status')['active_app']=='book':
   r.command('toast 阅读提示');r.settle(3);capture('book-notice');r.tick(3100);assert not r.command('status')['toast_active']
   for side in (0,1):
    x=18 if side==0 else w-18;assert not contact((x,h-18))['fan']
   report['checks'].append(label+': real book receives corner taps without menu capture; notice expires on external book page')
 finally:r.close()
report['passed']=True;(out/'report.json').write_text(json.dumps(report,ensure_ascii=False,indent=2));print('PASS',len(report['checks']),'interaction groups',len(report['scenes']),'captures')

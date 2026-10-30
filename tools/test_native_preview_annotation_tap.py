#!/usr/bin/env python3
"""横竖屏真实触摸命中已划线记录并持久删除；不连接设备。 / Portrait/landscape real reader touch-to-annotation and persisted deletion, without hardware."""
from pathlib import Path
import argparse,json,subprocess,tempfile,struct,hashlib
p=argparse.ArgumentParser();p.add_argument('--binary',type=Path,required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args()
root=Path(__file__).resolve().parents[1];a.output.mkdir(parents=True,exist_ok=True)
session=None;trace=[];now=10000;proc=None
def command(x):
 global now
 proc.stdin.write(x+'\n');proc.stdin.flush();line=proc.stdout.readline();assert line,log.name
 s=json.loads(line);assert s['ok'],s;now=s['now_ms'];trace.append({'command':x,'state':s});return s
def tick(t=100):return command('tick '+str(now+t))
def tap(x,y):
 command(f'touch down {x} {y} {now+20}');return command(f'touch up {x} {y} {now+60}')
def hold(x,y,tx,ty):
 command(f'touch down {x} {y} {now+20}');tick(700);command(f'touch move {tx} {ty} {now+40}');return command(f'touch up {tx} {ty} {now+40}')
def count():
 files=list(session.glob('tf/**/reader-r3-index.dat'));assert len(files)==1,files
 return struct.unpack_from('<I',files[0].read_bytes(),16)[0]
def capture(name):
 (a.output/(name+'.pgm')).write_bytes((session/'frame.pgm').read_bytes())
cases=[]
for landscape in (False,True):
 orientation='landscape' if landscape else 'portrait'
 session=Path(tempfile.mkdtemp(prefix=orientation+'-',dir=a.output));books=session/'tf/books';books.mkdir(parents=True)
 (books/'001.txt').write_text('第一章\n'+('这是批注删除测试正文。轻点已有划线可查看并取消，普通文本保留阅读翻页。\n'*100))
 trace=[];now=10000
 log=(session/'runtime.log').open('w');proc=subprocess.Popen([str(a.binary.resolve()),str(root/'sdcard/fonts/MiSansVF.ttf'),str(session.resolve()),str((session/'frame.pgm').resolve())],stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=log,text=True)
 try:
  command('route 0')
  for _ in range(14):s=tick()
  tap(60,330)
  for _ in range(14):s=tick()
  assert s['active_app']=='book' and s['book_view']=='reading',s
  if landscape:
   command('rotate 1')
   for _ in range(14):s=tick()
  assert s['logical_width']==(640 if landscape else 360),s
  s=hold(38,115,210,151);assert s['selection'],s
  s=tap(60,75);assert not s['selection'] and count()==1,s
  capture(orientation+'-highlight-saved')
  s=tap(38,115);assert s['active_app']=='system' and s['route']==2,s
  capture(orientation+'-annotation-open')
  # 几何直接取真实记录页的按钮中点，横竖屏不复用竖屏坐标。 / Hit each orientation's rendered record button center.
  s=tap((s['logical_width']*3)//4,s['logical_height']-66);assert count()==0,s
  capture(orientation+'-annotation-removed')
  command('key 1 single')
  for _ in range(3):s=tick()
  if s['active_app']!='book':
   command('key 1 single')
   for _ in range(3):s=tick()
  assert s['active_app']=='book',s
  # 已删除范围恢复普通正文操作，不能再次跳回记录。 / Removed ranges restore normal reading, never reopen the old record.
  s=tap(38,115);assert s['active_app']=='book',s
  cases.append({'orientation':orientation,'session':str(session),'real_touch_to_saved_annotation':True,'deletion_persisted':True,'trace':trace})
 finally:
  proc.terminate();proc.wait(timeout=10);log.close()
  (session/'trace.json').write_text(json.dumps(trace,ensure_ascii=False,indent=2)+'\n')
(a.output/'report.json').write_text(json.dumps({'result':'PASS','version':s['version'],'binary_sha256':hashlib.sha256(a.binary.read_bytes()).hexdigest(),'scenarios':cases},ensure_ascii=False,indent=2)+'\n')
print('PASS: both orientations long-press/drag/save, underline opens exact record, delete commits, return restores ordinary reading')

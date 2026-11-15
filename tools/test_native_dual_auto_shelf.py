#!/usr/bin/env python3
"""Verify paired sync returns to shelf and Back from dual settings stays on shelf."""
import argparse, json, select, subprocess, tempfile, time
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser();p.add_argument('--binary',type=Path,required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args()
out=a.output.resolve();out.mkdir(parents=True,exist_ok=True);run=Path(tempfile.mkdtemp(prefix='auto-shelf-',dir=out))
class Device:
 def __init__(self,name,font):
  self.name=name;self.root=run/name;self.books=self.root/'tf/books';self.books.mkdir(parents=True);self.now=10000
  (self.books/f'only-{name}.txt').write_text((f'{name} book\n'*5000))
  self.log=(self.root/'runtime.log').open('w')
  self.p=subprocess.Popen([str(a.binary.resolve()),str(font),str(self.root),str(self.root/'frame.pgm')],stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=self.log,text=True)
  self.state={}
 def cmd(self,line):
  self.p.stdin.write(line+'\n');self.p.stdin.flush()
  if not select.select([self.p.stdout],[],[],30)[0]:raise RuntimeError(f'{self.name} timeout {line}')
  self.state=json.loads(self.p.stdout.readline());assert self.state['ok'];self.now=self.state['now_ms'];return self.state
 def tick(self):self.now+=20;return self.cmd(f'tick {self.now}')
 def close(self):
  if self.p.poll() is None:
   try:self.cmd('quit');self.p.wait(timeout=5)
   except Exception:self.p.terminate();self.p.wait(timeout=5)
  self.log.close()
d=[]
try:
 a_dev=Device('primary',ROOT/'main/assets/serif.ttf');b=Device('secondary',ROOT/'main/assets/kai.ttf');d=[a_dev,b]
 a_dev.cmd('dual 1 127.0.0.1 123456');b.cmd('dual 2 127.0.0.1 123456')
 def pump(n=1):
  for _ in range(n):a_dev.tick();b.tick();time.sleep(.001)
 for _ in range(10000):
  pump()
  if a_dev.state['dual_synced'] and b.state['dual_synced']:break
 assert a_dev.state['dual_synced'] and b.state['dual_synced']
 for _ in range(100):
  pump()
  if a_dev.state['route']==b.state['route']==0 and a_dev.state['active_app']!='book' and b.state['active_app']!='book':break
 assert a_dev.state['route']==b.state['route']==0,(a_dev.state,b.state)
 assert a_dev.state['dual_fast_verified'] and b.state['dual_fast_verified']
 a_dev.cmd('dual-usb RPICO DUAL OPEN 0')
 for _ in range(1000):
  pump()
  if a_dev.state['active_app']==b.state['active_app']=='book':break
 assert a_dev.state['active_app']==b.state['active_app']=='book'
 a_dev.cmd('route 34');pump(4);a_dev.cmd('key 1 short');pump(20)
 assert a_dev.state['route']==0 and a_dev.state['active_app']!='book',a_dev.state
 print('PASS: completed file sync returns both readers to shelf; Back from dual settings does not reopen last book')
finally:
 for x in d:
  try:x.close()
  except Exception:pass

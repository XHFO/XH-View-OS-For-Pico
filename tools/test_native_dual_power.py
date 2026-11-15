#!/usr/bin/env python3
"""Linked power in production readers; native radio boundary, no physical power claim."""
from pathlib import Path
import argparse, json, subprocess, tempfile, time, select, hashlib, shutil, struct, zlib
from test_native_preview_book import png_from_pgm
ROOT=Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser();p.add_argument('--binary',type=Path,default=ROOT/'build/dual-native-v2/native-preview');p.add_argument('--primary-binary',type=Path);p.add_argument('--output',type=Path);args=p.parse_args()
out=args.output or ROOT/'build/dual-native-tests';out.mkdir(parents=True,exist_ok=True)
run=Path(tempfile.mkdtemp(prefix='pair-',dir=out));processes=[];history=[]
class Device:
    def __init__(self,name,font):
        self.name=name;self.root=run/name;self.books=self.root/'tf/books';self.books.mkdir(parents=True);self.now=10000
        (self.books/'same.txt').write_text(''.join(f'第{i}章 Chapter {i}\n'+(('Primary text. Continuous pagination and shared fonts.\n' if name=='primary' else 'Secondary text. A different book with the same name.\n')*120) for i in range(1,5)))
        self.log=(self.root/'runtime.log').open('w');binary=args.primary_binary if name=='primary' and args.primary_binary else args.binary;self.p=subprocess.Popen([str(binary.resolve()),str(font),str(self.root),str(self.root/'frame.pgm')],stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=self.log,text=True);processes.append(self)
        self.state={};self.cmd('status');self.cmd('route 34')
    def cmd(self,line):
        self.p.stdin.write(line+'\n');self.p.stdin.flush()
        if not select.select([self.p.stdout],[],[],30)[0]:raise RuntimeError(f'{self.name} timed out: {line}')
        result=self.p.stdout.readline()
        if not result:raise RuntimeError(f'{self.name} exited: {self.log.name}')
        self.state=json.loads(result);assert self.state['ok'],(line,self.state)
        self.now=self.state['now_ms'];history.append({'device':self.name,'command':line,'state':self.state});return self.state
    def tick(self):self.now+=20;return self.cmd(f'tick {self.now}')
    def tap(self,x,y):
        self.cmd(f'touch down {x} {y} {self.now+20}')
        return self.cmd(f'touch up {x} {y} {self.now+60}')
    def capture(self,name):png_from_pgm(self.root/'frame.pgm',run/f'{name}-{self.name}.png')
try:
    a=Device('primary',ROOT/'main/assets/serif.ttf');b=Device('secondary',ROOT/'main/assets/kai.ttf')
    a.cmd('dual 1 127.0.0.1 123456');b.cmd('dual 2 127.0.0.1 123456')
    def pump(n=1):
        for _ in range(n):a.tick();b.tick();time.sleep(.001)
    def until(f,label,limit=5000):
        for _ in range(limit):
            pump()
            if f():return
        raise AssertionError((label,a.state,b.state))
    def stable():return all(d.state.get('active_app')=='book' and d.state.get('dual_target')==0 for d in (a,b)) and a.state['dual_spread']==b.state['dual_spread']
    until(lambda:a.state['dual_synced'] and b.state['dual_synced'] and a.state['route']==b.state['route']==0,'initial sync')
    a.cmd('book-select /sdcard/books/same.txt');until(stable,'primary explicit open');pump(120)
    for _ in range(3):b.cmd('key 2 short');pump(100)
    before=[(d.state['dual_spread'],d.state['book_chapter'],d.state['page_start']) for d in (a,b)]
    for actor in (a,b,a,b):
        actor.cmd('powerkey')
        until(lambda:all(d.state['power_state']==1 for d in (a,b)),'both locked')
        pump(200)
        assert all(d.state['dual_connected'] for d in (a,b)),(a.state,b.state)
        for i,d in enumerate((a,b)):
            assert (d.state['dual_spread'],d.state['book_chapter'],d.state['page_start'])==before[i]
        a.capture('lock');b.capture('lock')
        peer=b if actor is a else a
        peer.cmd('powerkey')
        until(lambda:all(d.state['power_state']==0 for d in (a,b)) and stable(),'both awake')
        pump(100)
        for i,d in enumerate((a,b)):
            assert (d.state['dual_spread'],d.state['book_chapter'],d.state['page_start'])==before[i],(before,a.state,b.state)
    for d in (a,b):d.cmd('powerkey')
    until(lambda:all(d.state['power_state']==1 for d in (a,b)),'simultaneous lock')
    for d in (a,b):d.cmd('dual-radio 0')
    pump(50)
    assert not a.state['dual_connected'] and not b.state['dual_connected']
    for d in (a,b):d.cmd('dual-radio 1')
    pump(150)
    b.cmd('powerkey')
    until(lambda:all(d.state['power_state']==0 for d in (a,b)) and stable(),'radio loss during lock recovers')
    pump(150)
    assert a.state['dual_spread']==b.state['dual_spread']==before[0][0]
    b.cmd('key 2 short');until(lambda:stable() and a.state['dual_spread']>before[0][0],'reading continues after wake')
    print('PASS: both-role linked lock and peer wake x4, retained BLE, unchanged anchors/spread, simultaneous lock, radio loss while locked, continued reading')

finally:
    (run/'transcript.json').write_text(json.dumps(history,ensure_ascii=False,indent=2)+'\n')
    for d in processes:
        if d.p.poll() is None:
            try:d.cmd('quit');d.p.wait(timeout=5)
            except Exception:d.p.terminate();d.p.wait(timeout=5)
        d.log.close()
    print(run,flush=True)

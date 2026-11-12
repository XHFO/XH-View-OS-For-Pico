#!/usr/bin/env python3
"""Two production C readers and independent TFs; real Wi-Fi sockets, simulated BLE transport."""
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
    for d in (a,b):d.cmd('dual-radio 0')
    pump(100)
    for _ in range(4):b.cmd('powerkey');pump(100)
    furthest=(b.state['book_chapter'],b.state['page_start']);b.capture('offline-furthest')
    for d in (a,b):d.cmd('dual-radio 1')
    until(lambda:stable() and a.state['dual_synced'] and b.state['dual_synced'],'adopt furthest');pump(400)
    assert (b.state['book_chapter'],b.state['page_start'])>=furthest,('regressed',furthest,a.state,b.state)
    a.capture('reconnected');b.capture('reconnected');settled=a.state['dual_spread']
    for d in (a,b):d.cmd('dual-radio 0')
    pump(80)
    for d in (a,b):d.cmd('dual-radio 1')
    until(lambda:stable() and a.state['dual_synced'] and b.state['dual_synced'],'repeat reconnect');pump(300)
    assert a.state['dual_spread']==settled,('drift',settled,a.state,b.state)
    for d in (a,b):d.cmd('key 1 short')
    pump(100)
    other=next(a.books.glob('dual-*.txt'))
    a.cmd('book-select /sdcard/books/'+other.name)
    until(lambda:stable() and a.state.get('book_path','').endswith(other.name),'left opens different book and right follows');pump(100)
    assert a.state['page_end']==b.state['page_start']
    a.cmd('key 1 short');b.cmd('key 1 short');pump(100)
    a.cmd('book-select /sdcard/books/'+other.name)
    until(stable,'same book explicit reopen from both shelves');pump(100)
    assert a.state['page_end']==b.state['page_start']
    for d in (a,b):
        files=list(d.books.glob('.pico-system/b_*.dat'));assert files
        for f in files:
            raw=f.read_bytes();magic,size,crc=struct.unpack_from('<III',raw);assert size==len(raw)-12 and zlib.crc32(raw[12:])==crc
    print('PASS: real paired text, offline secondary furthest, repeat reconnect without drift, primary shelf new/same-book opens both, TF progress CRC',flush=True)
finally:
    (run/'transcript.json').write_text(json.dumps(history,ensure_ascii=False,indent=2)+'\n')
    for d in processes:
        if d.p.poll() is None:
            try:d.cmd('quit');d.p.wait(timeout=5)
            except Exception:d.p.terminate();d.p.wait(timeout=5)
        d.log.close()
    print(run,flush=True)

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
    # 双机投影不得覆盖日常方向和锁定偏好。/ Paired projection must preserve ordinary direction and lock preferences.
    def global_preferences(d):
        raw=next(d.root.glob('tf/**/system_v1.dat')).read_bytes()
        magic,size,crc=struct.unpack_from('<III',raw)
        assert magic==0x31535058 and size==len(raw)-12 and zlib.crc32(raw[12:])==crc
        assert struct.unpack_from('<I',raw,12)[0]==1
        return tuple(raw[18:20])
    a.cmd('rotate 1');a.cmd('rotation-lock 1')
    b.cmd('rotate 0');b.cmd('rotation-lock 1')
    preferences=(global_preferences(a),global_preferences(b))
    assert preferences==((1,1),(1,0)),preferences
    a.cmd('dual 1 127.0.0.1 123456')
    # The deployment command must be idempotent and preserve the existing pairing secret.
    config=a.books/'.pico-system/dualconfig1.dat';saved=config.read_bytes()
    a.cmd('dual-primary');assert a.state['dual_role']==1 and config.read_bytes()==saved
    b.cmd('dual 2 127.0.0.1 123456')
    def pump(count=1):
        for _ in range(count):a.tick();b.tick();time.sleep(.001)
    def until(predicate,label,limit=5000):
        for i in range(limit):
            pump()
            if predicate():return
            if i%500==0:print(label,i,a.state.get('dual_books'),b.state.get('dual_books'),a.state.get('dual_scanning'),b.state.get('dual_scanning'),flush=True)
        raise AssertionError((label,a.state,b.state))
    pump(3)
    assert not b.state['dual_synced']
    b.cmd('route 0')
    b.cmd('dual-open /sdcard/books/same.txt')
    b.capture('queued-open')
    until(lambda:a.state['dual_synced'] and b.state['dual_synced'],'initial library and font sync')
    until(lambda:a.state.get('active_app')==b.state.get('active_app')=='book' and a.state.get('dual_page')==1 and b.state.get('dual_page')==2 and a.state.get('dual_target')==b.state.get('dual_target')==0,'queued open completes after font sync')
    assert a.state['page_end']==b.state['page_start']
    pump(800)
    for d in (a,b):assert any(d.books.glob('.pico-system/b_*.dat')),'TF progress missing: '+d.name
    a.capture('paired');b.capture('paired')
    assert (a.books/'same.txt').read_bytes()!=(b.books/'same.txt').read_bytes()
    def contents(d):return sorted(hashlib.sha256(f.read_bytes()).hexdigest() for f in d.books.iterdir() if f.suffix in ('.txt','.epub'))
    assert contents(a)==contents(b) and len(contents(a))==2
    def stable():
        return all(d.state.get('active_app')=='book' and d.state.get('dual_target')==0 for d in (a,b)) and a.state['dual_spread']==b.state['dual_spread']
    for _ in range(4):
        page=a.state['dual_spread'];b.cmd('powerkey');until(lambda:stable() and a.state['dual_spread']>page,'turn settled');pump(60)
    for d in (a,b):
        identity=a.state['dual_spread'];print('checkpoint',d.name,identity,flush=True)
        d.cmd('key 1 short');pump(100)
        assert d.state['active_app']!='book'
        assert d.state['dual_has_reading']
        for style in range(4):
            d.cmd(f'shelf-card-style {style}');d.capture(f'resume-card-{style}')
        d.tap(180,185);until(stable,'banner resume');pump(120)
        assert a.state['dual_spread']==identity,('resume changed spread',identity,a.state,b.state)
        d.cmd('key 1 short');pump(100)
        d.cmd('route 3');pump(100);d.cmd('route 0');pump(100)
        assert d.state['dual_has_reading'],('home lost paired session',d.state)
        d.tap(180,185);until(stable,'resume after home');pump(120)
        assert a.state['dual_spread']==identity,('home resume changed spread',identity,a.state,b.state)
    for _ in range(22):
        page=a.state['dual_spread'];b.cmd('powerkey');until(lambda:stable() and a.state['dual_spread']>page,'cross chapter turn');pump(40)
    assert a.state['book_chapter']>1
    for away,reader in ((a,b),(b,a)):
        away.cmd('key 1 short');pump(100)
        old=a.state['dual_spread']
        for _ in range(3):reader.cmd('powerkey');pump(100)
        until(lambda:a.state['dual_spread']!=old,'solo relay changes authority')
        identity=a.state['dual_spread'];away.tap(180,185);until(stable,'rejoin after peer continued');pump(100)
        assert a.state['dual_spread']>=identity,('rejoin regressed authority',identity,a.state,b.state)
    identity=a.state['dual_spread']
    for d in (a,b):d.cmd('key 1 short')
    pump(100)
    for d in (a,b):assert d.state['active_app']!='book' and d.state['dual_has_reading']
    a.tap(180,185);pump(100);assert b.state['active_app']!='book'
    b.tap(180,185);until(stable,'both exited then explicitly resume');pump(100)
    assert a.state['dual_spread']==identity
    anchors=(a.state['page_start'],b.state['page_start']);anchor_before=(b.state['book_chapter'],b.state['page_start'])
    for d in (a,b):d.cmd('dual-radio 0')
    pump(30)
    for d in (a,b):d.cmd('dual-radio 1')
    until(lambda:stable() and a.state['dual_connected'] and b.state['dual_connected'],'radio reconnect');pump(100)
    # 新规则：保留过往单机更远位置，合并可能前进，绝不回退。/ Furthest-history merge may advance but never regress.
    assert a.state['dual_spread']>=identity and (b.state['book_chapter'],b.state['page_start'])>=anchor_before
    assert a.state['page_end']==b.state['page_start']
    for d in (a,b):d.cmd('dual-radio 0')
    pump(100)
    for _ in range(4):b.cmd('powerkey');pump(100)
    furthest=(b.state['book_chapter'],b.state['page_start'])
    b.capture('secondary-offline-furthest')
    for d in (a,b):d.cmd('dual-radio 1')
    until(lambda:stable() and a.state['dual_synced'] and b.state['dual_synced'],'reconnect adopts furthest solo record');pump(300)
    assert (b.state['book_chapter'],b.state['page_start'])>=furthest,('offline progress regressed',furthest,a.state,b.state)
    settled=a.state['dual_spread']
    for d in (a,b):d.cmd('dual-radio 0')
    pump(80)
    for d in (a,b):d.cmd('dual-radio 1')
    until(lambda:stable() and a.state['dual_synced'] and b.state['dual_synced'],'repeat reconnect stable');pump(300)
    assert a.state['dual_spread']==settled,('repeat reconnect advanced',settled,a.state,b.state)
    print('PASS: disconnected secondary advances; reconnect merges higher anchor; repeated reconnect does not advance',flush=True)
    print('PASS: cross-chapter solo relay, both exited, explicit resume, radio reconnect and anchors',flush=True)
    print('PASS: paired resume through shelf and home on both roles',flush=True)
finally:
    (run/'transcript.json').write_text(json.dumps(history,ensure_ascii=False,indent=2)+'\n')
    for d in processes:
        if d.p.poll() is None:
            try:d.cmd('quit');d.p.wait(timeout=5)
            except Exception:d.p.terminate();d.p.wait(timeout=5)
        d.log.close()
    print(run,flush=True)

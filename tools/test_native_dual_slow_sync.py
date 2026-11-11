#!/usr/bin/env python3
"""Regression: long synchronization must not restart itself or sleep before opening a book."""
from pathlib import Path
import argparse, json, subprocess, tempfile, time, select, hashlib, shutil
from test_native_preview_book import png_from_pgm
ROOT=Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser();p.add_argument('--binary',type=Path,default=ROOT/'build/dual-native-v2/native-preview');p.add_argument('--primary-binary',type=Path);args=p.parse_args()
out=ROOT/'build/dual-native-tests';out.mkdir(parents=True,exist_ok=True)
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
    def tick(self):self.now+=1000;return self.cmd(f'tick {self.now}')
    def capture(self,name):png_from_pgm(self.root/'frame.pgm',run/f'{name}-{self.name}.png')
try:
    a=Device('primary',ROOT/'main/assets/serif.ttf');b=Device('secondary',ROOT/'main/assets/kai.ttf')
    a.cmd('dual 1 127.0.0.1 123456')
    # The deployment command must be idempotent and preserve the existing pairing secret.
    config=a.books/'.pico-system/dualconfig1.dat';saved=config.read_bytes()
    a.cmd('dual-primary');assert a.state['dual_role']==1 and config.read_bytes()==saved
    b.cmd('dual 2 127.0.0.1 123456')
    def pump(count=1):
        for _ in range(count):a.tick();b.tick();time.sleep(.001)
    def until(predicate,label,limit=350):
        for i in range(limit):
            pump()
            if predicate():return
            if i%500==0:print(label,i,a.state.get('dual_books'),b.state.get('dual_books'),a.state.get('dual_scanning'),b.state.get('dual_scanning'),flush=True)
        raise AssertionError((label,a.state,b.state))
    pump(3)
    a.cmd('key 1 double')
    assert a.state['panel']==3
    until(lambda:a.state['dual_synced'] and b.state['dual_synced'],'library and font sync while switcher remains open')
    for _ in range(180):
        pump()
        assert a.state['dual_synced'] and b.state['dual_synced']
        assert not a.state['dual_scanning'] and not b.state['dual_scanning']
        assert a.state['power_state']==b.state['power_state']==0
        assert a.state['panel']==3
    b.cmd('dual-usb RPICO DUAL OPEN 0')
    pump(20)
    assert a.state['panel']==3 and a.state.get('active_app')!='book'
    a.cmd('key 1 single')
    assert not a.state['panel']
    until(lambda:a.state.get('active_app')==b.state.get('active_app')=='book' and a.state.get('dual_page')==1 and b.state.get('dual_page')==2 and a.state.get('dual_target')==b.state.get('dual_target')==0,'slow-sync open')
    assert a.state['page_end']==b.state['page_start']
    until(lambda:a.state['dual_left_ready'] and b.state['dual_left_ready'],'slow-sync pair ready')
    b.cmd('powerkey')
    pump(10)
    if (a.state.get('dual_page'),b.state.get('dual_page'))==(1,2):b.cmd('powerkey')
    until(lambda:a.state.get('dual_page')==3 and b.state.get('dual_page')==4,'slow-sync next')
    a.cmd('powerkey')
    until(lambda:a.state.get('dual_page')==1 and b.state.get('dual_page')==2,'slow-sync previous')
    # 暂停会销毁链路会话，但必须保留双机方向配置。/ Suspending destroys the link session while preserving paired orientation settings.
    a.cmd('flip')
    until(lambda:all(d.state['dual_primary_flip'] for d in (a,b)),'primary offset before lock')
    b.cmd('flip')
    until(lambda:all(d.state['dual_secondary_flip'] for d in (a,b)),'secondary offset before lock')
    assert a.state['rotation']==b.state['rotation']==1
    a.cmd('power 0')
    until(lambda:a.state['power_state']==1,'explicit lock remains available')
    a.cmd('wake')
    until(lambda:a.state['dual_synced'] and b.state['dual_synced'],'wake reconnect')
    until(lambda:a.state['rotation']==b.state['rotation']==1,'wake restores paired physical orientation')
    assert all(d.state['dual_primary_flip'] and d.state['dual_secondary_flip'] for d in (a,b))
    (run/'report.json').write_text(json.dumps({'passed':True,'checks':['slow sync beyond one minute with switcher open','three minutes ready without full rescan or idle sleep','remote open waits for overlay dismissal','secondary open','adjacent text boundaries','next/previous spread','explicit lock and wake','role offsets retained across suspend and wake'],'physical_hardware':False},indent=2)+'\n')
    print('PASS: slow sync, stable ready, automatic-sleep hold and actual paired reading',flush=True)

finally:
    (run/'transcript.json').write_text(json.dumps(history,ensure_ascii=False,indent=2)+'\n')
    for d in processes:
        if d.p.poll() is None:
            try:d.cmd('quit');d.p.wait(timeout=5)
            except Exception:d.p.terminate();d.p.wait(timeout=5)
        d.log.close()
    print(run,flush=True)

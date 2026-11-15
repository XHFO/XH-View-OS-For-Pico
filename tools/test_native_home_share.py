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
import os,threading
from http.server import BaseHTTPRequestHandler,ThreadingHTTPServer
os.environ['RPICO_HOME_LINK_OFFLINE']='0'
os.environ['NO_PROXY']=os.environ['no_proxy']='127.0.0.1,localhost'
requests=[]
class HTTP(BaseHTTPRequestHandler):
    def do_POST(self):
        q=json.loads(self.rfile.read(int(self.headers.get('Content-Length','0'))));requests.append(q)
        if 'pico_total' not in q.get('template',''):
            payload={'total':1,'items':[{'id':'office','name':'测试书房','devices':120}]}
        else:
            v=q.get('variables',{});ids=v.get('entities',[]) if v.get('selected') else [f'light.room_{i}' for i in range(120)]
            rows=[{'entity_id':i,'state':'on','attributes':{'friendly_name':'阅读灯','brightness':127,'supported_color_modes':['brightness']}} for i in ids]
            payload=rows[v.get('offset',0):v.get('offset',0)+v.get('limit',96)]+[{'pico_total':len(rows),'pico_cut':False}]
        raw=json.dumps(payload,ensure_ascii=False).encode();self.send_response(200);self.send_header('Content-Length',str(len(raw)));self.end_headers();self.wfile.write(raw)
    def log_message(self,*args):pass
server=ThreadingHTTPServer(('127.0.0.1',0),HTTP);threading.Thread(target=server.serve_forever,daemon=True).start()
try:
    a=Device('primary',ROOT/'main/assets/serif.ttf');b=Device('secondary',ROOT/'main/assets/kai.ttf')
    a.cmd('dual 1 127.0.0.1 123456');b.cmd('dual 2 127.0.0.1 123456')
    def pump(count=1):
        for _ in range(count):a.tick();b.tick();time.sleep(.003)
    def until(predicate,label,limit=8000):
        for i in range(limit):
            pump()
            if predicate():return
        raise AssertionError((label,a.state,b.state))
    until(lambda:a.state['dual_synced'] and b.state['dual_synced'],'paired')
    url=f'http://127.0.0.1:{server.server_port}'
    a.cmd('home-test-config '+url);a.cmd('home-test-rooms -1');pump(100);a.cmd('home-test-rooms 1');a.cmd('home-test-select 5')
    def state(d):return d.cmd('home-test-state')
    until(lambda:state(b)['local']==url and state(b)['selected']==5,'primary HA/selection shared')
    b.cmd('home-test-select 3')
    until(lambda:state(a)['selected']==3 and state(b)['selected']==3 and not state(b)['pending'],'secondary proposal committed by primary')
    for d in (a,b):d.cmd('dual-radio 0')
    pump(60);b.cmd('home-test-select 4');assert state(b)['pending'] and state(b)['selected']==3
    a.cmd('home-test-select 2')
    for d in (a,b):d.cmd('dual-radio 1')
    until(lambda:state(a)['selected']==2 and state(b)['selected']==2 and not state(b)['pending'],'primary wins reconnect conflict')
    # 断开后副机使用已存地址和选择独立联网。/ A detached secondary polls its saved HA configuration independently.
    for d in (a,b):d.cmd('dual-radio 0')
    pump(60);b.cmd('route 3');pump(250)
    assert state(b)['entities']==2
    for d in (a,b):d.cmd('dual-radio 1')
    b.cmd('route 0');pump(200);a.cmd('home-test-config clear')
    until(lambda:state(b)['local']=='','clear credentials shared')
    assert any(q.get('variables',{}).get('selected') for q in requests)
    (run/'report.json').write_text(json.dumps({'passed':True,'real_radio':False,'real_HA':False,'synthetic_loopback':True,'HA_requests':len(requests),'checks':['primary configuration and selection','secondary online proposal','offline draft','primary conflict wins','detached secondary independent HA','clear credentials propagated']},indent=2))
    print('PASS: real two-process home configuration sharing, secondary draft/authority conflict, independent HA fetch and credential clear',flush=True)
finally:
    (run/'transcript.json').write_text(json.dumps(history,ensure_ascii=False,indent=2))
    for d in processes:
        if d.p.poll() is None:
            try:d.cmd('quit');d.p.wait(timeout=5)
            except Exception:d.p.terminate();d.p.wait(timeout=5)
        d.log.close()
    server.shutdown();server.server_close();print(run,flush=True)

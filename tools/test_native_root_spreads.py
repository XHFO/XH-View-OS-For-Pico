#!/usr/bin/env python3
"""Two real native controllers and actual TF Home Link modules; synthetic BLE/HA only."""
import argparse,hashlib,json,os,tempfile,threading,time
from pathlib import Path
from http.server import BaseHTTPRequestHandler,ThreadingHTTPServer
from test_native_preview_shelf_ble import Runtime,ROOT,stored_style
p=argparse.ArgumentParser();p.add_argument('--binary',type=Path,required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args()
out=a.output.resolve();out.mkdir(parents=True,exist_ok=True);run=Path(tempfile.mkdtemp(prefix='pair-',dir=out));devices=[];report={'passed':False,'physical_radio':False,'real_HA':False,'checks':[],'binary_sha256':hashlib.sha256(a.binary.read_bytes()).hexdigest()}
os.environ.update(RPICO_HOME_LINK_OFFLINE='0',RPICO_WEREAD_OFFLINE='1',NO_PROXY='127.0.0.1,localhost',no_proxy='127.0.0.1,localhost')
class HTTP(BaseHTTPRequestHandler):
 def do_POST(self):
  q=json.loads(self.rfile.read(int(self.headers.get('Content-Length','0'))));v=q.get('variables',{})
  if 'pico_total' not in q.get('template',''):data={'total':1,'items':[{'id':'office','name':'测试书房','devices':20}]}
  else:
   ids=v.get('entities',[]) if v.get('selected') else [f'light.room_{i}' for i in range(20)]
   rows=[{'entity_id':i,'state':'on','attributes':{'friendly_name':'灯 '+i.split('_')[-1],'brightness':127,'supported_color_modes':['brightness']}} for i in ids]
   data=rows[v.get('offset',0):v.get('offset',0)+v.get('limit',96)]+[{'pico_total':len(rows),'pico_cut':False}]
  raw=json.dumps(data,ensure_ascii=False).encode();self.send_response(200);self.send_header('Content-Length',str(len(raw)));self.end_headers();self.wfile.write(raw)
 def log_message(self,*a):pass
server=ThreadingHTTPServer(('127.0.0.1',0),HTTP);threading.Thread(target=server.serve_forever,daemon=True).start()
try:
 for name in ('primary','secondary'):
  session=run/name;books=session/'tf/books';books.mkdir(parents=True)
  for i in range(20):(books/f'{i:02d}-fixture.txt').write_text((f'Fixture book {i}. Original private test data.\n'*300))
  r=Runtime(a.binary.resolve(),ROOT/'sdcard/fonts/MiSansVF.ttf',session,out);r.start();r.command('route 0');devices.append(r)
 primary,secondary=devices
 def pump(n=1):
  for _ in range(n):
   for d in devices:d.tick(40)
   time.sleep(.002)
 def state(d):return d.command('status')
 def until(check,label,limit=1800):
  for _ in range(limit):
   pump()
   if check():return
  raise AssertionError((label,state(primary),state(secondary)))
 def pages(left,right):return all(state(d)['root_paired'] and state(d)['root_page']==p for d,p in zip(devices,(left,right)))
 def swipe(d,left=True):
  d.command(f'touch down {280 if left else 100} 170 {d.now+20}');d.command(f'touch move {100 if left else 280} 170 {d.now+80}');d.command(f'touch up {100 if left else 280} 170 {d.now+50}')
 primary.command('dual 1 127.0.0.1 123456');secondary.command('dual 2 127.0.0.1 123456')
 until(lambda:state(primary)['dual_synced'] and state(secondary)['dual_synced'],'file handshake');pump(100)
 primary.style(0);until(lambda:stored_style(secondary.session)==0,'primary shelf style followed');until(lambda:pages(0,1),'first spread')
 primary.capture('shelf-left',state(primary));secondary.capture('shelf-right',state(secondary))
 swipe(secondary);until(lambda:pages(2,3),'secondary advances group');pump(30)
 assert state(primary)['root_pages']==3;primary.capture('shelf-last-left',state(primary));secondary.capture('shelf-empty-right',state(secondary))
 revision=state(primary)['dual_revision'];swipe(primary);pump(40);assert pages(2,3),'last spread repeated/backtracked'
 primary.command('route 5');until(lambda:not state(secondary)['root_paired'] and state(secondary)['root_page']==2,'empty secondary shelf detaches to valid last page')
 primary.command('route 0');until(lambda:pages(2,3),'tail rejoins without primary rewind')
 swipe(primary,False);until(lambda:pages(0,1),'primary reverses group')
 report['checks'].append('shelf roles 0/1 -> 2/3, right tail empty, boundary stable, either side controls')
 secondary.command('route 5');until(lambda:not state(primary)['root_paired'],'settings detaches only its root');swipe(primary);pump(30);assert state(primary)['route']==0 and state(primary)['root_page']==1
 secondary.command('route 0');until(lambda:pages(1,2),'rejoin preserves primary current page')
 for d in devices:d.command('dual-radio 0')
 pump(50);assert all(not state(d)['root_paired'] for d in devices)
 for d in devices:d.command('dual-radio 1')
 until(lambda:pages(1,2),'radio reconnection');assert state(primary)['dual_revision']==revision
 report['checks'].append('leaving roots/radio disconnect/rejoin preserve primary page and reading revision')
 primary.command('home-test-config '+f'http://127.0.0.1:{server.server_port}');primary.command('home-test-rooms -1');pump(150);primary.command('home-test-rooms 1');primary.command('home-test-select 18');pump(400)
 for d in devices:d.command('route 3')
 until(lambda:all(state(d)['root_kind']==2 and state(d)['root_pages']==3 for d in devices),'actual Home Link monitor pages');until(lambda:pages(0,1),'home spread')
 primary.capture('home-left',state(primary));secondary.capture('home-right',state(secondary));swipe(secondary);until(lambda:pages(2,3),'home next spread');secondary.capture('home-empty-right',state(secondary))
 primary.command('route 5');until(lambda:not state(secondary)['root_paired'] and state(secondary)['root_page']==2,'empty Home side detaches to valid last page')
 primary.command('route 3');until(lambda:pages(0,1),'Home root rejoin')
 swipe(secondary);until(lambda:pages(2,3),'Home next after rejoin')
 swipe(primary,False);until(lambda:pages(0,1),'home previous spread')
 report['checks'].append('actual TF Home Link monitor shares paired pages, both directions and empty last side')
 primary.tap(40,608);pump(60);assert not state(secondary)['root_paired'],'setup remained paired'
 primary.capture('home-setup-one-back',state(primary));primary.tap(35,47);pump(60);assert state(primary)['route']!=0
 report['checks'].append('left heading returns Home setup to monitor without duplicate header action')
 report['passed']=True;print('PASS',report['checks'])
finally:
 for d in devices:
  (d.session/'transcript.json').write_text(json.dumps(d.transcript,ensure_ascii=False,indent=2));d.close()
 server.shutdown();server.server_close();(out/'report.json').write_text(json.dumps(report,ensure_ascii=False,indent=2))

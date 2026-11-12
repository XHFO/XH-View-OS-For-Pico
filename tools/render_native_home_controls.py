#!/usr/bin/env python3
"""Render actual firmware and Home Link controls against a synthetic HA service."""
import argparse,json,os,sys,threading,time
from http.server import BaseHTTPRequestHandler,ThreadingHTTPServer
from pathlib import Path
sys.path.insert(0,str(Path.cwd()/'tools'))
from test_native_preview_shelf_ble import Runtime,ROOT
parser=argparse.ArgumentParser();parser.add_argument('--binary',type=Path,required=True);parser.add_argument('--output',type=Path,required=True);args=parser.parse_args()
out=args.output.resolve();out.mkdir(parents=True,exist_ok=True)
os.environ['RPICO_HOME_LINK_OFFLINE']='0';os.environ['NO_PROXY']=os.environ['no_proxy']='127.0.0.1,localhost'
states=[{'entity_id':'light.living','state':'on','attributes':{'friendly_name':'客厅主灯','brightness':153,'supported_color_modes':['brightness']}},
{'entity_id':'climate.study','state':'cool','attributes':{'friendly_name':'书房空调','temperature':24,'hvac_modes':['off','heat','cool','auto','dry','fan_only']}},
{'entity_id':'switch.desk','state':'off','attributes':{'friendly_name':'书桌电源'}},
{'entity_id':'fan.office','state':'on','attributes':{'friendly_name':'工作室风扇'}},
{'entity_id':'sensor.temperature','state':'23.6','attributes':{'friendly_name':'室内温度','device_class':'temperature','unit_of_measurement':'°C'}},
{'entity_id':'sensor.humidity','state':'48','attributes':{'friendly_name':'室内湿度','device_class':'humidity','unit_of_measurement':'%'}}]
for item in states:item['attributes']['area_name']='书房与客厅'
requests=[]
class HTTP(BaseHTTPRequestHandler):
 def do_POST(self):
  q=json.loads(self.rfile.read(int(self.headers.get('Content-Length','0'))));requests.append(self.path)
  if self.path.startswith('/api/services/'):
   if 'brightness_pct' in q:states[0]['attributes']['brightness']=round(q['brightness_pct']*255/100)
   if 'hvac_mode' in q:states[1]['state']=q['hvac_mode']
   data=[]
  elif 'pico_total' not in q.get('template',''):data={'total':1,'items':[{'id':'office','name':'书房与客厅','devices':6}]}
  else:
   v=q.get('variables',{});chosen=v.get('entities',[]) if v.get('selected') else [s['entity_id'] for s in states]
   items=[s for s in states if s['entity_id'] in chosen]
   data=items[v.get('offset',0):v.get('offset',0)+v.get('limit',96)]+[{'pico_total':len(items),'pico_cut':False}]
  raw=json.dumps(data,ensure_ascii=False).encode();self.send_response(200);self.send_header('Content-Length',str(len(raw)));self.end_headers();self.wfile.write(raw)
 def log_message(self,*a):pass
server=ThreadingHTTPServer(('127.0.0.1',0),HTTP);threading.Thread(target=server.serve_forever,daemon=True).start()
try:
 for land in (False,True):
  label='landscape' if land else 'portrait';session=out/('session-'+label+'-'+str(time.time_ns()));session.mkdir()
  r=Runtime(args.binary.resolve(),ROOT/'sdcard/fonts/MiSansVF.ttf',session,out)
  try:
   r.start();r.command(f'rotate {int(land)}');r.command('home-test-config '+f'http://127.0.0.1:{server.server_port}');r.command('route 3')
   for _ in range(40):time.sleep(.01);r.tick(100)
   r.capture('home-rooms-'+label,r.command('status'))
   r.tap(50,138 if land else 184);r.tap(524 if land else 292,328 if land else 608)
   for _ in range(40):time.sleep(.01);r.tick(100)
   r.capture('home-catalog-'+label,r.command('status'))
   for i in range(6):r.tap(50+(310*(i%2) if land else 0),(130+(i//2)*46) if land else 176+i*46)
   r.capture('home-selected-'+label,r.command('status'));r.tap(524 if land else 292,328 if land else 608)
   for _ in range(40):time.sleep(.01);r.tick(100)
   r.capture('home-controls-'+label,r.command('status'))
   r.tap(590 if land else 310,210)
   r.capture('home-climate-modes-'+label,r.command('status'))
   r.tap(320 if land else 180,308 if land else 492)
   r.tap(60,328 if land else 608)
   r.capture('home-settings-switches-'+label,r.command('status'))
  finally:r.close()
 (out/'report.json').write_text(json.dumps({'passed':True,'hardware_access':False,'synthetic_HA':True,'requests':requests},indent=2))
 print('PASS: current C firmware and actual Home Link package rendering, synthetic HA',out)
finally:server.shutdown();server.server_close()

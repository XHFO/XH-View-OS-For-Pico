#!/usr/bin/env python3
"""无socket验证真实原生会话和网页HTTP边界。 / Socket-free verification of real native sessions and web HTTP boundaries."""
import argparse
import importlib.util
import io
import json
import os
from pathlib import Path
import tempfile
import sys
import time
from urllib.parse import urlsplit
ROOT=Path(__file__).resolve().parents[1]
SITE=ROOT.parents[1]/'outputs/firmware-preview/server.py'

def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--build',type=Path,required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args()
 spec=importlib.util.spec_from_file_location('pico_server',SITE);module=importlib.util.module_from_spec(spec);sys.modules[spec.name]=module;spec.loader.exec_module(module)
 module.BUILD=a.build.resolve();module.REPO=Path(json.loads((module.BUILD/'native-preview-build.json').read_text())['source_root'])
 checks=0
 def check(value):
  nonlocal checks
  assert value;checks+=1
 session=None
 previous_offline=os.environ.get('RPICO_HOME_LINK_OFFLINE')
 os.environ['RPICO_HOME_LINK_OFFLINE']='1'
 def request(method,path,key='',body=None,headers=None):
  handler=module.Handler.__new__(module.Handler);handler.command=method;handler.path=path
  data=b'' if body is None else (body if isinstance(body,bytes) else json.dumps(body).encode())
  handler.headers={'Host':'127.0.0.1:8782','Origin':'http://127.0.0.1:8782','X-Pico-Preview-Session':session.id,'X-Pico-Setup-Key':key,'Content-Length':str(len(data)),'Content-Type':'application/json',**(headers or {})}
  handler.rfile=io.BytesIO(data);handler.server=type('Server',(),{'server_port':8782})();got=[]
  handler.respond=lambda status,body,*args:got.append((status,body))
  if method in ('PUT','DELETE'):handler.management_write()
  elif method=='GET':handler.do_GET()
  else:handler.do_POST()
  return got[0]
 def worker_offline(source):
  deadline=time.monotonic()+2
  while time.monotonic()<deadline:
   status,body=request('GET','/api/manage/printers',key)
   row=body['printers'][0]
   if row['source']==source and row['status']['result']==3:
    check(status==200 and not row['status']['connected'] and not row['status']['has_data'])
    home=request('GET','/api/manage/home',key)[1]
    check(home['status']['result']==3 and home['status']['http_status']==0 and not home['status']['busy'])
    return
   time.sleep(.01)
  check(False)
 try:
  session=module.Session();module.SESSIONS[session.id]=session
  management=session.command('manage-open')['management'];check(management['active']);key=management['url'].split('#key=')[1];check(len(key)==32)
  check(request('GET','/api/manage/home','0'*32)[0]==401)
  check(request('GET','/api/manage/home',key,headers={'Origin':'http://untrusted.example'})[0]==400)
  check(request('GET','/api/manage/home',key,headers={'Host':'untrusted.example'})[0]==400)
  status,body=request('GET','/api/manage/home',key);check(status==200);check('ha_token' not in body);check(body['saved_wifi'])
  status,_=request('PUT','/api/manage/home',key,{'enabled':False,'local':'http://homeassistant.local:8123','home_wifi':body['saved_wifi'][0],'ha_token':'native-test-token'});check(status==200)
  status,body=request('GET','/api/manage/home',key);check(status==200 and body['config']['has_token']);check('native-test-token' not in json.dumps(body))
  check(request('PUT','/api/manage/home',key,{'ha_token':''})[0]==200)
  check(request('GET','/api/manage/home',key)[1]['config']['has_token'])
  check(request('PUT','/api/manage/home',key,{'home_wifi':'unknown-network'})[0]==400)
  check(request('PUT','/api/manage/printers?slot=0',key,{'source':4,'name':'实验打印机','serial':'FormTEST001','client_id':'test-client','client_secret':'test-secret'})[0]==200)
  status,body=request('GET','/api/manage/printers',key);check(status==200);check('test-secret' not in json.dumps(body) and 'test-client' not in json.dumps(body));check(body['printers'][0]['has_client_secret'])
  worker_offline(4)
  check(request('PUT','/api/manage/printers?slot=0',key,{'source':3,'name':'Bambu测试','serial':'01PTEST001','region':1,'username':'u_123456','secret':'bambu-test-token'})[0]==200)
  row=request('GET','/api/manage/printers',key)[1]['printers'][0];check(row['source']==3 and not row['has_client_secret'] and row['has_secret'])
  worker_offline(3)
  check(request('PUT','/api/manage/printers?slot=0',key,{'source':3,'client_secret':'wrong-brand'})[0]==400)
  # 多台打印机复用保存的账户，重新读取仅公开代表槽位，设备独占秘密。
  # Multiple printers reuse one saved account; rereads expose representative slots while the device owns secrets.
  auth='/api/manage/bambu/auth'
  status,saved=request('GET',auth,key);check(status==200 and saved['saved_count']==1)
  check(saved['saved_accounts'][0]['slot']==0 and saved['saved_accounts'][0]['region']==1)
  check(request('PUT','/api/manage/printers?slot=1',key,{'source':3,'name':'同账户第二台','serial':'01PTEST002','region':1,'username':'u_123456','secret':'bambu-test-token'})[0]==200)
  status,shared=request('GET',auth,key);check(status==200 and shared['saved_count']==1 and shared['saved_accounts'][0]['printer_count']==2)
  check('u_123456' not in json.dumps(shared) and 'bambu-test-token' not in json.dumps(shared))
  check(request('POST',auth,key,{'action':'reuse','session':shared['session'],'config_revision':saved['config_revision'],'saved_slot':0})[0]==409)
  status,shared=request('POST',auth,key,{'action':'reuse','session':shared['session'],'config_revision':shared['config_revision'],'saved_slot':0})
  check(status==202 and shared['reused'] and shared['phase'] in (1,5))
  deadline=time.monotonic()+2
  while time.monotonic()<deadline:
   status,shared=request('GET',auth,key)
   if shared['phase']!=1:break
   time.sleep(.01)
  check(status==200 and shared['phase']==5 and shared['result']==3 and shared['saved_count']==1)
  check('bambu-test-token' not in json.dumps(shared))
  check(request('POST',auth,key,{'action':'cancel','session':shared['session']})[0]==200)
  check(request('DELETE','/api/manage/printers?slot=0',key)[0]==200)
  check(request('GET','/api/manage/printers',key)[1]['printers'][0]['source']==0)
  status,shared=request('GET',auth,key);check(status==200 and shared['saved_count']==1 and shared['saved_accounts'][0]['slot']==1 and shared['saved_accounts'][0]['printer_count']==1)
  check(request('DELETE','/api/manage/printers?slot=1',key)[0]==200)
  check(request('GET',auth,key)[1]['saved_count']==0)
  check(request('PUT','/api/manage/home',key,b'x'*12289)[0]==413)
  check(request('PUT','/api/manage/home',key,{},headers={'Content-Type':'text/plain'})[0]==415)
  check(request('GET','/api/manage/home?'+'a'*769,key)[0]==400)
  check(request('GET','/api/manage/'+'a'*96,key)[0]==400)
  check(request('POST','/api/manage/home',key,{})[0]==405)
  check(request('GET','/api/manage/ha/entities?limit=24',key)[0]==200)
  check(request('POST','/api/manage/ha/control',key,{'entity':'switch.missing','action':'turn_on'})[0] in (404,503,403))
  # 真实认证工作器仍在显式离线传输下运行；不向Bambu发送测试密码。
  # Exercise the real auth worker with forced-offline transport; never send fixture passwords to Bambu.
  auth='/api/manage/bambu/auth'
  status,auth_state=request('GET',auth,key);check(status==200 and auth_state['phase']==0)
  check(request('POST',auth,key,{'action':'login','session':auth_state['session'],'region':2,'account':'fixture@example.test','password':'fixture-password'})[0]==400)
  check(request('PUT','/api/manage/home',key,{'enabled':False})[0]==200)
  status,auth_state=request('POST',auth,key,{'action':'login','session':auth_state['session'],'region':0,'account':'fixture@example.test','password':'fixture-password'})
  check(status==202 and 'fixture' not in json.dumps(auth_state))
  deadline=time.monotonic()+2
  while time.monotonic()<deadline:
   status,auth_state=request('GET',auth,key)
   if auth_state['phase']!=1:break
   time.sleep(.01)
  check(status==200 and auth_state['phase']==5 and auth_state['result']==3 and auth_state['count']==0)
  check('fixture' not in json.dumps(auth_state))
  check(request('POST',auth,key,{'action':'import','session':auth_state['session'],'slot':0,'index':0})[0]==409)
  old_auth_session=auth_state['session']
  status,auth_state=request('POST',auth,key,{'action':'cancel','session':old_auth_session})
  check(status==200 and auth_state['phase']==0 and auth_state['session']!=old_auth_session)
  check(request('POST',auth,key,{'action':'cancel','session':old_auth_session})[0]==409)
  check(request('GET',auth,'0'*32)[0]==401)
  private=session.private/'home-link.dat';check(private.is_file());check(not list((session.root/'tf').rglob('home-link.dat')))
  old=key;new=session.command('manage-open')['management']['url'].split('#key=')[1];check(old!=new);check(request('GET','/api/manage/home',old)[0]==401)
  check(request('GET','/api/manage/home',new)[1]['config']['has_token'])
  session.close();replacement=module.Session(session.id);session=replacement;module.SESSIONS[session.id]=session
  check(request('GET','/api/manage/home',new)[0]==401)
  key=session.command('manage-open')['management']['url'].split('#key=')[1];check(request('GET','/api/manage/home',key)[1]['config']['has_token'])
  session.command('scenario first-use');check(request('GET','/api/manage/home',key)[0]==401)
 finally:
  try:
   if session is not None:session.close();module.SESSIONS.pop(session.id,None)
  finally:
   if previous_offline is None:os.environ.pop('RPICO_HOME_LINK_OFFLINE',None)
   else:os.environ['RPICO_HOME_LINK_OFFLINE']=previous_offline
 a.output.parent.mkdir(parents=True,exist_ok=True);a.output.write_text(json.dumps({'result':'PASS','checks':checks,'transport':'socket-free actual shared native core + Python request handlers; host transport forced offline','host_network_attempted':False,'real_account_tested':False},indent=2)+'\n');print(f'PASS: {checks} native and HTTP boundary checks')
if __name__=='__main__':main()

#!/usr/bin/env python3
"""Exercise production content management through isolated native and Python HTTP boundaries."""
import argparse,hashlib,importlib.util,io,json,os,sys
from pathlib import Path
from urllib.parse import quote,urlsplit
ROOT=Path(__file__).resolve().parents[1]
SITE=ROOT.parents[1]/'outputs/firmware-preview/server.py'
def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--build',type=Path,required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args()
    spec=importlib.util.spec_from_file_location('native_content_server',SITE);m=importlib.util.module_from_spec(spec);sys.modules[spec.name]=m;spec.loader.exec_module(m);m.BUILD=a.build.resolve();m.REPO=ROOT
    checks=0;session=None;before=os.environ.get('RPICO_HOME_LINK_OFFLINE');os.environ['RPICO_HOME_LINK_OFFLINE']='1'
    def check(v):
        nonlocal checks
        assert v;checks+=1
    def request(method,path,data=b'',key=None,headers=None):
        h=m.Handler.__new__(m.Handler);h.command=method;h.path=path;h.rfile=io.BytesIO(data);h.wfile=io.BytesIO();h.close_connection=False
        h.headers={'X-Pico-Preview-Session':session.id,'X-Pico-Setup-Key':key if key is not None else pairing,'Content-Length':str(len(data)),'Content-Type':'application/octet-stream',**(headers or {})};responses=[]
        h.respond=lambda status,body,*args:responses.append((status,body));h.send_response=lambda status:responses.append((status,None));h.send_header=lambda *args:None;h.end_headers=lambda:None
        h.management_api(urlsplit(path));status,body=responses[-1];return status,h.wfile.getvalue() if body is None else body
    try:
        session=m.Session();m.SESSIONS[session.id]=session;pairing=session.command('manage-open')['management']['url'].split('#key=')[1]
        status,body=request('GET','/api/manage/pico/files?offset=0&limit=64');check(status==200 and body['ok']);total=body['total'];check(total>=7)
        original=b'Pico Wi-Fi content management\n'*5000;sha=hashlib.sha256(original).hexdigest();path='books/stream test.txt';query='?path='+quote(path,safe='')
        status,receipt=request('PUT','/api/manage/pico/files'+query+'&replace=0',original,headers={'X-Pico-Content-SHA256':sha});check(status==200 and receipt['bytes']==len(original) and receipt['sha256']==sha)
        check((session.root/'tf'/path).read_bytes()==original)
        status,receipt=request('GET','/api/manage/pico/file'+query+'&metadata=1');check(status==200 and receipt['sha256']==sha)
        status,data=request('GET','/api/manage/pico/file'+query);check(status==200 and data==original)
        status,_=request('PUT','/api/manage/pico/files'+query+'&replace=0',original,headers={'X-Pico-Content-SHA256':sha});check(status==409)
        status,_=request('PUT','/api/manage/pico/files'+query+'&replace=1',b'wrong',headers={'X-Pico-Content-SHA256':sha});check(status==422);check((session.root/'tf'/path).read_bytes()==original)
        status,_=request('GET','/api/manage/pico/files?offset=0&limit=1');check(status==200)
        status,_=request('GET','/api/manage/pico/file?path=..%2Fprivate%2Fhome-link.dat');check(status==403)
        status,_=request('DELETE','/api/manage/pico/files?path=apps%2Fdata%2Fdrawing-notes%2Fboard01.dat');check(status==403)
        status,_=request('GET','/api/manage/pico/files?limit=65');check(status==400)
        status,_=request('GET','/api/manage/pico/files',key='0'*32);check(status==401)
        status,_=request('PUT','/api/manage/pico/files'+query,original,headers={'X-Pico-Content-SHA256':'bad'});check(status==400)
        status,body=request('DELETE','/api/manage/pico/files'+query);check(status==200 and body['path']==path);check(not (session.root/'tf'/path).exists())
        status,_=request('GET','/api/manage/pico/file'+query);check(status==404)
        check(not list((session.root/'tf').rglob('*.pico-part')));check(not list((session.root/'tf/.desktop-inbox').glob('content-*')))
        status,body=request('GET','/api/manage/pico/files?offset=0&limit=64');check(status==200 and body['total']==total)
    finally:
        if session is not None:session.close();m.SESSIONS.pop(session.id,None)
        if before is None:os.environ.pop('RPICO_HOME_LINK_OFFLINE',None)
        else:os.environ['RPICO_HOME_LINK_OFFLINE']=before
    a.output.parent.mkdir(parents=True,exist_ok=True);a.output.write_text(json.dumps({'result':'PASS','checks':checks,'hardware_access':False,'source_root':str(ROOT),'native_build':str(a.build.resolve()),'boundary':'Production C content protocol + real UI handoff + Python HTTP handlers; private temporary preview session'},indent=2)+'\n');print(f'PASS: {checks} native content/HTTP checks')
if __name__=='__main__':main()

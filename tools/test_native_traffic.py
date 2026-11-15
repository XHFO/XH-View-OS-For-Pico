#!/usr/bin/env python3
"""实际TF看板、配对邮箱、布局和私有存档回归；无真实账户。/ Real TF dashboard, paired mailbox, layout and private state; no live account."""
from pathlib import Path
import argparse,json,os,struct,time,urllib.parse,zlib
from test_native_preview_shelf_ble import ROOT,Runtime
p=argparse.ArgumentParser();p.add_argument('--binary',type=Path,required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args();out=a.output.resolve();out.mkdir(parents=True,exist_ok=True)
os.environ['RPICO_HOME_LINK_OFFLINE']='1';checks=[];scenes={}
def fixture(style):
    r=bytearray(864);struct.pack_into('<IIIII',r,0,0x32414643,20,0,style,900);r[20:20+8]=b'XHFO.NET';r[84:116]=b'1234567890abcdef1234567890abcdef';r[117:117+12]=b'fixture-only';epoch=int(time.time());struct.pack_into('<q',r,376,epoch)
    for i,n in enumerate([13560,27820,18650,21490,25412,28640,32480]):
        at=384+i*40;struct.pack_into('<QQQ',r,at,n,n*8500,n*3//4);date=time.strftime('%Y-%m-%d',time.gmtime(epoch-(6-i)*86400)).encode();r[at+24:at+34]=date
    for i,(name,n) in enumerate([('CN',24400),('US',5200),('JP',1850)]):struct.pack_into('<Q',r,664+i*40,n);r[672+i*40:674+i*40]=name.encode()
    return struct.pack('<III',0x31535058,len(r),zlib.crc32(r))+r
for landscape in (False,True):
    label='landscape' if landscape else 'portrait';session=out/('session-'+label);session.mkdir(exist_ok=True);r=Runtime(a.binary.resolve(),ROOT/'sdcard/fonts/MiSansVF.ttf',session,out)
    def raw(command):
        r.process.stdin.write((command+'\n').encode());r.process.stdin.flush()
        while b'\n' not in r.buffer:
            assert r.selector.select(30),command;r.buffer+=os.read(r.process.stdout.fileno(),65536)
        line,r.buffer=r.buffer.split(b'\n',1);data=json.loads(line);r.transcript.append({'command':command,'result':data});return data
    def capture(name):
        state=r.command('status');r.capture(name,state);scenes[name]=state
    try:
        r.start();r.command('rotate '+str(int(landscape)));assert r.command('app-open website-traffic')['route']==28
        capture('traffic-empty-'+label)
        r.tap(300 if landscape else 180,290 if landscape else 338);capture('traffic-settings-empty-'+label)
        r.tap(180,136);capture('traffic-qr-'+label)
        status=raw('manage-status')['management'];assert status['active'];key=urllib.parse.parse_qs(urllib.parse.urlsplit(status['url']).fragment)['key'][0]
        def rpc(method,path,body=None,k=key):
            data={'key':k,'method':method,'path':'/api/manage/pico/traffic/'+path,'query':'','body':json.dumps(body or {})}
            return raw('traffic-web '+json.dumps(data).encode().hex())['management_response']
        assert rpc('GET','config')['status']==200
        assert rpc('GET','ui')['status']==200
        assert rpc('GET','config',k='0'*32)['status']==401
        result=rpc('PUT','config',{'revision':1,'source':0,'style':0,'interval':900,'label':'Synthetic','zone':'1234567890abcdef1234567890abcdef','token':'synthetic-only'})
        assert result['status']==202,result
        for _ in range(20):
            if not rpc('GET','config')['body']['busy']:break
            time.sleep(.01)
        assert not rpc('GET','config')['body']['has_token']
        r.tap(40,38);assert not raw('manage-status')['management']['active']
        assert rpc('GET','config')['status']==401
        r.command('key 1 click');r.command('key 1 click')
        checks.append(label+': real TF setup, authenticated UI/config, queued async validation, offline preserves unconfigured state, revoke on close')
        data=session/'tf/apps/data/website-traffic/cloudflare2.dat';data.parent.mkdir(parents=True,exist_ok=True)
        for style in range(3):
            data.write_bytes(fixture(style));r.command('app-open website-traffic');capture(f'traffic-style-{style}-'+label)
            if style==0:
                r.tap((640 if landscape else 360)-50,46);capture('traffic-settings-'+label);r.command('key 1 click')
            r.command('key 1 click')
        assert data.is_file();r.command('game-remove website-traffic');assert data.is_file()
        checks.append(label+': persisted dashboard three styles, return, remove retains configuration')
    finally:r.close()
(out/'report.json').write_text(json.dumps({'result':'PASS','hardware_access':False,'checks':checks,'scenes':scenes},ensure_ascii=False,indent=2)+'\n');print('PASS native traffic: '+str(len(checks))+' grouped scenarios')

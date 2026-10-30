#!/usr/bin/env python3
"""Local dual-reader lab: two C readers, Wi-Fi sockets and a separate simulated BLE channel."""
from pathlib import Path
import argparse, http.server, json, select, subprocess, tempfile, threading, time, urllib.parse
from test_native_preview_book import png_from_pgm
ROOT=Path(__file__).resolve().parents[1]
PAGE='''<!doctype html><html lang="zh-CN"><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>XH View OS · 双机阅读实验版</title><style>
*{box-sizing:border-box}body{margin:0;background:#eceae6;color:#252522;font:15px -apple-system,BlinkMacSystemFont,sans-serif}header{max-width:1100px;margin:30px auto 20px;padding:0 24px}h1{font-size:24px;margin:0 0 10px}p{color:#666;line-height:1.7;margin:8px 0}.pair{display:flex;justify-content:center;gap:32px;padding:0 24px 32px;flex-wrap:wrap}article{width:min(390px,100%)}.shell{background:#d5d3cf;padding:14px 14px 22px;border-radius:22px;border:1px solid #bcbab6;box-shadow:0 8px 28px #00000010}img{display:block;width:100%;height:auto;background:#fff;aspect-ratio:684/1216;touch-action:none;border:1px solid #aaa;border-radius:4px}.buttons{display:flex;gap:8px;margin:12px 0;flex-wrap:wrap}button{background:#fff;border:1px solid #c4c2bd;border-radius:12px;padding:10px 14px;cursor:pointer;font:inherit}button:hover{background:#e3e1dc}h2{font-size:18px;margin:10px 0}small{display:block;min-height:36px;color:#666;line-height:1.5}.tag{font-size:12px;background:#dfddd8;padding:4px 8px;border-radius:6px}a{color:inherit}</style>
<header><span class="tag">双屏原生预览 · 独立实验分支</span><h1 style="margin-top:16px">一册书，两块屏幕。</h1><p>两侧运行独立的原生固件进程和 TF 书库。Wi-Fi 数据与蓝牙控制使用分离通道；此处蓝牙由本机回环模拟，实机使用 BLE。屏幕直接显示固件绘制结果。</p></header><div class="pair"></div><script>
const ids=['primary','secondary'];let states={},revisions={},busy=false;
for(const [i,id] of ids.entries()){const a=document.createElement('article');a.innerHTML=`<h2>${i?'从机 · 下一页':'主机 · 起始页'}</h2><div class="shell"><img id="${id}" alt="${i?'从机':'主机'}原生画面"></div><div class="buttons"><button data-cmd="powerkey">${i?'电源短按 · 下一组':'电源短按 · 上一组'}</button><button data-cmd="setup">配对设置</button><button data-cmd="powerlong">长按电源</button><button data-cmd="book">打开示例书</button><button data-cmd="flip">上下翻转 180°</button></div><small id="${id}-state">正在启动</small>`;document.querySelector('.pair').append(a);a.querySelectorAll('button').forEach(b=>b.onclick=()=>send(id,{command:b.dataset.cmd}));const im=a.querySelector('img');function point(e){const r=im.getBoundingClientRect();const w=states[id]?.logical_width||360,h=states[id]?.logical_height||640;return {x:Math.max(0,Math.min(w-1,Math.floor((e.clientX-r.left)/r.width*w))),y:Math.max(0,Math.min(h-1,Math.floor((e.clientY-r.top)/r.height*h)))}}im.onpointerdown=e=>{im.setPointerCapture(e.pointerId);send(id,{command:'touch',phase:'down',...point(e)})};im.onpointerup=e=>send(id,{command:'touch',phase:'up',...point(e)});im.onpointercancel=e=>send(id,{command:'touch',phase:'up',...point(e)});}
async function send(id,body){await fetch('/command',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({device:id,...body})});refresh()}
async function refresh(){if(busy)return;busy=true;try{states=await(await fetch('/state')).json();for(const id of ids){const s=states[id];if(!s)continue;if(revisions[id]!==s.revision){document.getElementById(id).src='/frame/'+id+'.png?v='+s.revision;revisions[id]=s.revision}document.getElementById(id+'-state').textContent=s.dual_scanning?'正在校验本机书库…':!s.dual_connected?'等待另一台连接…':!s.dual_synced?'正在补齐书库和共享字库…':s.active_app==='book'?(s.dual_blank?'本书已结束':`全书第 ${s.dual_page||1} 页 · ${s.dual_target?'正在定位':'双机已连接'}`):'书库同步完成 · 可点击打开示例书';}}finally{busy=false}}
setInterval(refresh,500);refresh();</script></html>'''

class Device:
    def __init__(self,root,name,binary,font):
        self.name=name;self.root=root/name;self.root.mkdir();books=self.root/'tf/books';books.mkdir(parents=True)
        text=''.join(f'第{i}章 Chapter {i}\n'+''.join(f'{j:02d}. Read together. Two screens continue the same story. A new page begins exactly where the other ends.\n\n' for j in range(1,36)) for i in range(1,5))
        if name=='primary':(books/'Together.txt').write_text(text)
        self.log=(self.root/'runtime.log').open('w');self.p=subprocess.Popen([str(binary),str(font),str(self.root),str(self.root/'frame.pgm')],stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=self.log,text=True)
        self.state={};self.png_revision=-1;self.cmd('status');self.cmd('route 34');self.cmd(f'dual {1 if name=="primary" else 2} 127.0.0.1 764291')
    def cmd(self,value):
        self.p.stdin.write(value+'\n');self.p.stdin.flush()
        if not select.select([self.p.stdout],[],[],30)[0]:raise RuntimeError(self.name+' did not respond')
        raw=self.p.stdout.readline()
        if not raw:raise RuntimeError(self.name+' stopped; see '+str(self.log.name))
        self.state=json.loads(raw);return self.state
    def frame(self):
        if self.png_revision!=self.state['revision']:
            png_from_pgm(self.root/'frame.pgm',self.root/'frame.png');self.png_revision=self.state['revision']
        return (self.root/'frame.png').read_bytes()
    def close(self):
        if self.p.poll() is None:
            try:self.cmd('quit');self.p.wait(timeout=5)
            except Exception:self.p.terminate();self.p.wait(timeout=5)
        self.log.close()

def main():
    p=argparse.ArgumentParser();p.add_argument('--port',type=int,default=8818);p.add_argument('--binary',type=Path,default=ROOT/'build/dual16-native/native-preview');args=p.parse_args()
    area=ROOT/'build/dual-preview';area.mkdir(exist_ok=True);session=Path(tempfile.mkdtemp(prefix='session-',dir=area));devices={};lock=threading.RLock();stop=threading.Event();opened=False
    try:
        for name in ('primary','secondary'):devices[name]=Device(session,name,args.binary.resolve(),ROOT/'main/assets/serif.ttf')
        def worker():
            nonlocal opened
            while not stop.wait(.025):
                try:
                    with lock:
                        for d in devices.values():d.cmd('tick '+str(d.state['now_ms']+25))
                        if not opened and all(d.state.get('dual_synced') for d in devices.values()):devices['primary'].cmd('dual-open /sdcard/books/Together.txt');opened=True
                except Exception as exc:print('preview runtime error:',exc,flush=True);stop.set()
        threading.Thread(target=worker,daemon=True).start()
        class Handler(http.server.BaseHTTPRequestHandler):
            def log_message(self,*_):pass
            def response(self,data,mime,status=200):
                self.send_response(status);self.send_header('Content-Type',mime);self.send_header('Content-Length',str(len(data)));self.send_header('Cache-Control','no-store');self.end_headers();self.wfile.write(data)
            def do_GET(self):
                path=urllib.parse.urlsplit(self.path).path
                with lock:
                    if path=='/':return self.response(PAGE.encode(),'text/html; charset=utf-8')
                    if path=='/state':return self.response(json.dumps({k:v.state for k,v in devices.items()}).encode(),'application/json')
                    for name,d in devices.items():
                        if path==f'/frame/{name}.png':return self.response(d.frame(),'image/png')
                self.response(b'not found','text/plain',404)
            def do_POST(self):
                if self.path!='/command':return self.response(b'not found','text/plain',404)
                try:
                    n=int(self.headers.get('Content-Length','0'))
                    if not 0<n<=512:raise ValueError('size')
                    data=json.loads(self.rfile.read(n));command=data['command']
                    with lock:
                        d=devices[data['device']]
                        if command in ('powerkey','powerlong','flip'):d.cmd(command)
                        elif command=='setup':d.cmd('route 34')
                        elif command=='book':d.cmd('route 0');d.cmd('dual-open /sdcard/books/Together.txt')
                        elif command=='touch' and data['phase'] in ('down','up'):
                            x=int(data['x']);y=int(data['y'])
                            if not 0<=x<d.state['logical_width'] or not 0<=y<d.state['logical_height']:raise ValueError('coordinates')
                            d.cmd(f'touch {data["phase"]} {x} {y} {d.state["now_ms"]+20}')
                        else:raise ValueError('command')
                    self.response(b'{"ok":true}','application/json')
                except (ValueError,KeyError):self.response(b'{"ok":false}','application/json',400)
        server=http.server.ThreadingHTTPServer(('127.0.0.1',args.port),Handler)
        print(json.dumps({'url':f'http://127.0.0.1:{args.port}/','session':str(session),'native_processes':{k:v.p.pid for k,v in devices.items()}},ensure_ascii=False),flush=True)
        server.serve_forever()
    finally:
        stop.set()
        with lock:
            for d in devices.values():d.close()
if __name__=='__main__':main()

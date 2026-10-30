#!/usr/bin/env python3
"""中文：共享固件全页面16灰阶、菜单局刷与输入定稿像素回归，不模拟面板耗时。
English: Shared-firmware 16-gray pages, menu damage and input settling; no panel timing simulation.
"""
from pathlib import Path
import argparse
import hashlib
import json
import math
import tempfile
from test_native_preview_shelf_ble import Runtime, ROOT, read_pgm


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary',required=True,type=Path)
    parser.add_argument('--output',required=True,type=Path)
    args=parser.parse_args();output=args.output.resolve();output.mkdir(parents=True,exist_ok=True)
    session=Path(tempfile.mkdtemp(prefix='gray16-',dir=output))
    runtime=Runtime(args.binary.resolve(),ROOT/'sdcard/fonts/MiSansVF.ttf',session,output)
    scenes=[];checks=[]
    def inspect(name,minimum=3):
        w,h,pixels=read_pgm(runtime.frame);levels=sorted(set(pixels))
        assert all(value%17==0 for value in levels),(name,levels)
        assert len(levels)>=minimum,(name,levels)
        scenes.append({'name':name,'width':w,'height':h,'levels':levels,'sha256':hashlib.sha256(pixels).hexdigest()})
        return pixels
    try:
        runtime.start()
        for landscape in (False,True):
            runtime.command(f'rotate {int(landscape)}')
            for route in range(30):
                state=runtime.command(f'route {route}')
                inspect(f'route-{route}-{int(landscape)}')
                if state['power_state']:runtime.command('powerkey')
            checks.append(f'{int(landscape)}:all 30 stable routes use native 4-bit levels and retain gray')
            w,h=(640,360) if landscape else (360,640)
            for side in (0,1):
                runtime.command('route 3');edge=w-3 if side else 3
                runtime.command(f'touch down {edge} {h-3} {runtime.now+20}')
                scale=min(w,h)/348;idx=0
                for count,radius in zip((3,5,6),(152,225,298)):
                    edge_guard=(44*scale+360*10/58.5)/2+3*scale
                    guard=math.asin(min(.99,edge_guard/(radius*scale)))
                    for j in range(count):
                        angle=guard+j*(math.pi/2-2*guard)/(count-1)
                        x=round((w if side else 0)+(-1 if side else 1)*radius*scale*math.cos(angle))
                        y=round(h-radius*scale*math.sin(angle))
                        state=runtime.command(f'touch move {x} {y} {runtime.now+90}')
                        assert state['fan'] and state['fan_selected']==idx,(landscape,side,idx,state)
                        assert state['refresh_mode']==5 and state['refresh_waveform']=='DEFAULT',state
                        assert state['refresh_partial'] or idx==0,state
                        inspect(f'fan-{int(landscape)}-{side}-{idx}')
                        before=state['presents']
                        state=runtime.command(f'touch move {x} {y} {runtime.now+90}')
                        assert state['presents']==before,'unchanged menu selection refreshed'
                        idx+=1
                runtime.command(f'touch up {w-2 if side else 2} {h-2} {runtime.now+60}')
            checks.append(f'{int(landscape)}:all 28 corner positions retain gray and skip repeated selections')
            runtime.command('route 10');runtime.tap(320 if landscape else 180,294 if landscape else 574)
            assert runtime.transcript[-1]['state']['route']==19
            initial=inspect(f'editor-initial-{int(landscape)}')
            top=136 if landscape else 390;key_x=224 if landscape else 8;width=404 if landscape else 344
            pitch=width//10
            # 拼音n键位在第三行第六列；真实触摸派发不借用内部编辑状态。
            # Real touch dispatch on third-row n; do not mutate editor internals.
            x=key_x+(width-7*pitch)//2+5*pitch+(pitch-4)//2
            y=top+2*44+20 if landscape else top+2*48+22
            # 按键只入队，输入停顿85ms后才统一绘制；与生产editor_render_due及键盘回归一致。
            # A key queues logical input; production waits 85ms of idle before presenting merged damage.
            before=runtime.transcript[-1]['state']
            released=runtime.tap(x,y)
            assert released['presents']==before['presents'],'key release synchronously repainted input'
            assert released['refresh_continuous_count']==before['refresh_continuous_count'],released
            state=runtime.tick(90)
            assert state['presents']==before['presents']+1,'real n touch did not produce one queued input target'
            assert state['refresh_continuous_count']==before['refresh_continuous_count']+1,state
            assert state['refresh_default_count']==before['refresh_default_count'],state
            assert state['refresh_waveform']=='CONTINUOUS' and state['refresh_partial'],state
            transient=inspect(f'editor-input-{int(landscape)}')
            image_w=1216 if landscape else 684
            assert transient[:image_w*40]==initial[:image_w*40],'input destroyed stable gray header'
            for _ in range(8):state=runtime.tick(10)
            state=runtime.tick(1000)
            assert state['refresh_mode']==5 and state['refresh_waveform']=='DEFAULT' and state['refresh_partial'],state
            settled=inspect(f'editor-settled-{int(landscape)}')
            assert settled!=transient,'input gray was never restored'
            before=state['presents']
            for _ in range(20):state=runtime.tick(100)
            assert state['presents']==before,'idle settling repeated'
            runtime.capture(f'editor-gray16-{int(landscape)}',state)
            checks.append(f'{int(landscape)}:continuous input preserves static gray, settles once, then skips idle')
        result={'result':'PASS','version':runtime.transcript[-1]['state']['version'],'binary_sha256':hashlib.sha256(args.binary.read_bytes()).hexdigest(),'checks':checks,'scenes':scenes,'commands':len(runtime.transcript),'hardware_access':False,'limitation':'Pixel levels and scheduling only; no optical grayscale or physical key latency measurement.'}
        (output/'report.json').write_text(json.dumps(result,ensure_ascii=False,indent=2)+'\n')
        print(json.dumps({k:v for k,v in result.items() if k!='scenes'},ensure_ascii=False,indent=2))
    finally:
        runtime.close();(output/'events.json').write_text(json.dumps(runtime.transcript,ensure_ascii=False,indent=2)+'\n')


if __name__=='__main__':main()

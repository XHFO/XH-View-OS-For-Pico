#!/usr/bin/env python3
"""实际固件状态栏、设置几何、迟滞与旧偏好迁移。/ Production status drawing, settings geometry, hysteresis and migration."""
from pathlib import Path
import argparse,json,tempfile,struct,zlib
from test_native_preview_shelf_ble import ROOT,Runtime
p=argparse.ArgumentParser();p.add_argument('--binary',type=Path,required=True);p.add_argument('--output',type=Path,required=True);args=p.parse_args()
out=args.output.resolve();out.mkdir(parents=True,exist_ok=True)
session=Path(tempfile.mkdtemp(prefix='status-',dir=out));r=Runtime(args.binary.resolve(),ROOT/'sdcard/fonts/MiSansVF.ttf',session,out);checks=[]
try:
    assert r.start()['status_style']==0
    for landscape in (False,True):
        label='landscape' if landscape else 'portrait';r.command(f'rotate {int(landscape)}');r.command('route 0')
        for style in range(2):
            for percent in range(2):
                s=r.command(f'status-style {style}');s=r.command(f'status-percent {percent}');r.settle(6)
                assert s['status_style']==style and s['status_percent']==bool(percent)
                r.capture(f'status-{label}-{style}-percent{percent}',r.command('status'))
        r.command('route 5');r.settle(4);r.tap(480 if landscape else 180,170 if landscape else 368);r.settle(4)
        r.capture('appearance-'+label,r.command('status'))
        # Percentage lives in the style picker. / 百分比开关在样式弹窗内。
        r.tap(480 if landscape else 180,202 if landscape else 468);r.settle(4)
        r.capture('style-picker-'+label,r.command('status'))
        panel_y=(360-312)//2 if landscape else (640-390)//2
        s=r.tap(160,panel_y+(198 if landscape else 264)+22);assert not s['status_percent']
        r.capture('style-picker-percent-off-'+label,r.command('status'))
        sy=panel_y+(198 if landscape else 264)+22
        left=60 if landscape else 16;w=520 if landscape else 328
        sx=left+w-70
        # A right swipe explicitly enables, even if repeated; left explicitly disables.
        for dx,want in [(38,True),(38,True),(-38,False)]:
            x=sx if dx>0 else sx+38
            r.command(f'touch down {x} {sy} {r.now+20}')
            s=r.command(f'touch up {x+dx} {sy} {r.now+80}')
            assert s['status_percent']==want,s
        s=r.tap(160,panel_y+(62+64+29 if landscape else 72+92+42));assert s['status_style']==1,s
        r.command('key 1 click');r.command('route 0')
        checks.append(label+': two themes, percentage switch inside picker, explicit left/right gestures without inversion')
        r.command('sound-options 15');r.command('route 31');r.settle(35)
        for dx,want in [(-38,14),(-38,14),(38,15)]:
            sx=(640 if landscape else 360)-70;sy=132
            x=sx if dx>0 else sx+38
            r.command(f'touch down {x} {sy} {r.now+20}')
            s=r.command(f'touch up {x+dx} {sy} {r.now+80}')
            assert s['sound_options']==want,s
            r.capture(('switches-enabled-' if want==15 else 'switches-disabled-')+label,s)
        r.command('route 26');r.settle(4)
        for dx,want in [(-38,False),(38,True),(38,True)]:
            sx=(640 if landscape else 360)-70;sy=94 if landscape else 352
            x=sx if dx>0 else sx+38
            r.command(f'touch down {x} {sy} {r.now+20}')
            s=r.command(f'touch up {x+dx} {sy} {r.now+80}')
            assert s['doubletap_wake']==want,s
        r.capture('power-switches-'+label,r.settle(30))
        checks.append(label+': shared sound and power switches, explicit swipes, disabled dependent options')
    r.command('status-percent 1');r.command('status-style 1');r.close();s=r.start();assert s['status_style']==1 and s['status_percent']
    r.command('route 0')
    for rssi,want in [(-43,3),(-63,3),(-65,2),(-77,2),(-80,1),(-73,1),(-70,2),(-58,2),(-55,3)]:
        r.command(f'scenario rssi{rssi}');s=r.tick(5100);assert s['wifi_level']==want,(rssi,s)
        r.capture(f'wifi-{rssi}-{want}',s)
    r.command('scenario offline');s=r.tick(5100);assert s['wifi_level']==0;r.capture('wifi-offline',s)
    checks.append('cached RSSI tiers with 4dB hysteresis and offline glyph')
    r.close()
    pref=next(session.glob('tf/**/statusstyle1.dat'));raw=b'\2';pref.write_bytes(struct.pack('<III',0x31535058,1,zlib.crc32(raw))+raw)
    for f in session.glob('tf/**/statuspct1.*'):f.unlink()
    s=r.start();assert s['status_style']==0 and s['status_percent'];checks.append('legacy third theme migrates to minimal with percentage')
finally:r.close()
(out/'report.json').write_text(json.dumps({'passed':True,'hardware_access':False,'checks':checks},ensure_ascii=False,indent=2)+'\n');print('PASS',checks)

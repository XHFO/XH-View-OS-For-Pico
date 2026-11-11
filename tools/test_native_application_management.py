#!/usr/bin/env python3
"""可见管理入口、稳定删除目标与保留数据。/ Visible management, stable removal targets and preserved data."""
import argparse,json,tempfile
from pathlib import Path
from test_native_preview_shelf_ble import Runtime,ROOT
p=argparse.ArgumentParser();p.add_argument('--binary',type=Path,required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args();out=a.output.resolve();out.mkdir(parents=True,exist_ok=True);checks=[]
for wide in (False,True):
    session=Path(tempfile.mkdtemp(prefix='wide-' if wide else 'tall-',dir=out));r=Runtime(a.binary.resolve(),ROOT/'sdcard/fonts/MiSansVF.ttf',session,out)
    def capture(name):r.settle(35);r.capture(('landscape-' if wide else 'portrait-')+name,r.command('status'))
    def manage():
        r.tap(590 if wide else 310,100)
        r.tap(440 if wide else 250,290 if wide else 430)
    def confirm():return r.tap(440 if wide else 255,240 if wide else 380)
    try:
        r.start();r.command('rotate '+str(int(wide)));r.command('route 33');manage()
        r.tap(590 if wide else 310,310 if wide else 590)
        save=session/'tf/apps/data/gomoku/sentinel.dat';save.parent.mkdir(parents=True,exist_ok=True);save.write_bytes(b'KEEP GAME DATA')
        r.tap(110 if wide else 260,250 if wide else 380);capture('remove-gomoku')
        r.command('key 1 click');assert (session/'tf/apps/games/gomoku.pga').is_file()
        r.tap(110 if wide else 260,250 if wide else 380);confirm()
        assert not (session/'tf/apps/games/gomoku.pga').exists() and save.read_bytes()==b'KEEP GAME DATA'
        checks.append(('wide' if wide else 'tall')+'-visible-game-remove-cancel-and-preserve-save')
        # 导航中的独立 TF 工具也使用稳定包 ID。/ Independent TF tools also use stable IDs.
        tools={f.stem for f in (session/'tf/apps/tools').glob('*.pga')}
        for id in tools:
            data=session/('tf/apps/data/'+id+'/sentinel.dat');data.parent.mkdir(parents=True,exist_ok=True);data.write_bytes(b'KEEP TOOL DATA')
        r.tap(110 if wide else 90,195 if wide else 260);capture('remove-tool')
        confirm();remaining={f.stem for f in (session/'tf/apps/tools').glob('*.pga')}
        assert remaining<tools and len(tools-remaining)==1
        assert all((session/('tf/apps/data/'+id+'/sentinel.dat')).read_bytes()==b'KEEP TOOL DATA' for id in tools)
        assert r.command('status')['route']==33
        checks.append(('wide' if wide else 'tall')+'-tool-remove-preserves-data-and-settings-origin')
    finally:r.close()
(out/'report.json').write_text(json.dumps({'passed':True,'hardware_access':False,'checks':checks},ensure_ascii=False,indent=2));print('PASS',checks)

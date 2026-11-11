#!/usr/bin/env python3
"""横竖屏真实触摸检查，合成 TF、不访问硬件。/ Touch checks with synthetic TF and no hardware."""
import argparse, json, tempfile, struct
from pathlib import Path
from test_native_preview_shelf_ble import Runtime,ROOT

p=argparse.ArgumentParser();p.add_argument('--binary',type=Path,required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args()
out=a.output.resolve();out.mkdir(parents=True,exist_ok=True);checks=[]
for wide in (False,True):
    label='landscape' if wide else 'portrait';session=Path(tempfile.mkdtemp(prefix=label+'-',dir=out))
    r=Runtime(a.binary.resolve(),ROOT/'sdcard/fonts/MiSansVF.ttf',session,out)
    def capture(name):r.settle(40);return r.capture(label+'-'+name,r.command('status'))
    try:
        r.start();r.command('rotate '+str(int(wide)));r.command('route 5');r.tap(180,110)
        capture('about')
        # 点标题和长按不得触发检查。/ Heading taps and long presses must not start updates.
        r.tap(70,104);before=r.command('status');r.hold(130,268)
        assert r.command('status')['route']==5
        r.tap(275 if wide else 305,174);capture('test-channel-confirm')
        assert r.command('status')['update_channel']==0
        r.tick(25000);r.tap(275 if wide else 305,174)
        assert r.command('status')['update_channel']==0
        # 确认层等待超过旧时限仍保留；取消不修改频道。/ The modal survives the old timeout and cancel preserves the channel.
        r.tap(185 if wide else 100,275 if wide else 415)
        assert r.command('status')['update_channel']==0
        r.tap(275 if wide else 305,174)
        r.tap(440 if wide else 260,275 if wide else 415)
        assert r.command('status')['update_channel']==1
        checks.append(label+'-explicit-persistent-channel-confirm')
        r.command('route 31');capture('sound')
        r.command('route 5')
        r.tap(470 if wide else 180,170 if wide else 368)
        r.tap(470 if wide else 180,202 if wide else 468);capture('status-picker')
        r.command('key 1 click');r.command('route 5')
        r.tap(470 if wide else 180,170 if wide else 368)
        r.tap(470 if wide else 180,262 if wide else 544);capture('ticket-picker')
        r.command('key 1 click');r.command('route 33');capture('apps')
        r.tap(590 if wide else 310,100);capture('app-management')
        # 明确选择名称排序，再进入删除模式。/ Select name ordering then enter removal mode.
        r.tap(460 if wide else 250,102 if wide else 242)
        assert r.command('status')['games_sort']=='名称'
        r.tap(460 if wide else 250,290 if wide else 430)
        r.tap(590 if wide else 310,100)
        r.tap(180,138 if wide else 177);capture('app-download')
        r.command('key 1 click');r.command('route 34')
        r.tap(320 if wide else 180,124);capture('primary-step1')
        r.tap(480 if wide else 180,190 if wide else 260);capture('primary-step2')
        r.tap(480 if wide else 180,298 if wide else 458)
        capture('primary-back')
        # 返回上一步必须在屏内可点。/ Back must remain reachable inside both viewports.
        assert r.command('status')['dual_role']==0
        checks.append(label+'-primary-transport-back')
        r.command('route 34');r.tap(570 if wide else 290,124);capture('secondary-search')
        r.command('key 1 click');r.command('app-open home-link')
        # 工具右侧底部设置键。/ The tool's setup action.
        r.tap(80,320 if wide else 600);capture('home-link-basic')
        r.tap(530 if wide else 170,108 if wide else 184);capture('home-link-advanced')
    finally:
        r.close();(session/'transcript.json').write_text(json.dumps(r.transcript,ensure_ascii=False,indent=2))
for wide in (False,True):
    label='landscape' if wide else 'portrait';session=Path(tempfile.mkdtemp(prefix=label+'-firmware-',dir=out))
    directory=session/'tf/readpico/firmware';directory.mkdir(parents=True)
    data=bytearray(512);data[0]=0xe9;struct.pack_into('<I',data,0x20,0xabcd5432)
    data[0x30:0x30+len('0.40.0-dual999')]=b'0.40.0-dual999';data[0x50:0x59]=b'Read_Pico'
    (directory/'update.bin').write_bytes(data);(directory/'update.sha256').write_text('0'*64+'\n')
    r=Runtime(a.binary.resolve(),ROOT/'sdcard/fonts/MiSansVF.ttf',session,out)
    try:
        assert r.start()['firmware_ready'];r.command('rotate '+str(int(wide)));r.command('route 5');r.tap(180,110)
        r.tap(130,268);r.settle(5);r.capture(label+'-install-confirm',r.command('status'))
        r.command('key 1 click');assert r.command('status')['route']==5 and (directory/'update.bin').is_file()
        checks.append(label+'-cached-image-confirm-and-cancel')
    finally:r.close()
(out/'report.json').write_text(json.dumps({'passed':True,'hardware_access':False,'checks':checks},ensure_ascii=False,indent=2))
print('PASS',checks)

#!/usr/bin/env python3
"""中文：真实共享界面中验证TF画板入口、触摸绘画、恢复和应用增删，不访问设备。
English: Exercise TF drawing navigation, touch ink, resume and package lifecycle in the real shared UI, without devices.
"""
from __future__ import annotations
import argparse
import hashlib
import json
from pathlib import Path
import struct
import time
import zlib
from test_native_preview_shelf_ble import Runtime, ROOT


def saved(session):
    path=session/'tf/apps/data/drawing-notes/board01.dat'
    raw=path.read_bytes();magic,size,crc=struct.unpack_from('<III',raw)
    body=raw[12:]
    assert magic==0x31535058 and size==len(body) and zlib.crc32(body)==crc
    assert len(body)==49168
    return path,struct.unpack_from('<H',body,10)[0]


def stroke(runtime,y=180):
    runtime.command(f'touch down 110 {y} {runtime.now+20}')
    for x,dy in ((150,20),(200,40),(250,55),(285,45)):
        state=runtime.command(f'touch move {x} {y+dy} {runtime.now+90}')
    return runtime.command(f'touch up 285 {y+45} {runtime.now+40}')


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary',type=Path,default=ROOT/'build/dev36-app-check-v2/native-preview')
    parser.add_argument('--output',type=Path,default=ROOT/'build/dev36-drawing-integration')
    args=parser.parse_args();binary=args.binary.resolve();output=args.output.resolve()
    manifest=binary.parent/'native-preview-build.json'
    desktop_available=manifest.is_file() and 'main/system/pico_desktop.c' in json.loads(manifest.read_text()).get('sources',{})
    output.mkdir(parents=True,exist_ok=False);session=output/'session';session.mkdir()
    runtime=Runtime(binary,ROOT/'sdcard/fonts/MiSansVF.ttf',session,output)
    report={'result':'RUNNING','hardware_access':False,'binary_sha256':hashlib.sha256(binary.read_bytes()).hexdigest(),'scenes':{}}
    try:
        state=runtime.start();assert state['games_count']==6
        package=session/'tf/apps/tools/drawing-notes.pga';assert package.is_file()
        # 入口必须通过真实首页编排加入，不借用小游戏页。
        # Add through the actual home editor, never through the game launcher.
        runtime.command('route 27');runtime.tap(80,520);runtime.tap(130,424 if desktop_available else 380)
        runtime.capture('navigation-added',runtime.command('status'))
        runtime.tap(300,590);runtime.command('route 0')
        state=runtime.tap(265,48);assert state['route']==28,state
        runtime.capture('drawing-list',state)
        state=runtime.tap(70,155);assert state['route']==28
        runtime.capture('drawing-empty',state)
        initial=state['refresh_continuous_count'];state=stroke(runtime)
        assert state['refresh_continuous_count']>initial,state
        runtime.capture('drawing-ink',state)
        time.sleep(1.95);runtime.settle(25,100);path,points=saved(session);assert points>=4,points
        runtime.command('key 1 single');state=runtime.command('status');runtime.capture('drawing-list-saved',state)
        runtime.close();state=runtime.start();runtime.command('route 0');state=runtime.tap(265,48);assert state['route']==28
        state=runtime.tap(70,155);runtime.capture('drawing-reopened',state)
        stroke(runtime,210);time.sleep(1.95);runtime.settle(25,100);_,continued=saved(session);assert continued>points
        # 撤销后保存，再重开；清空确认的外点关闭不得删除原稿。
        # Save undo, reopen, and dismiss clear by tapping outside without deleting originals.
        runtime.tap(180,90);runtime.tap(310,90);runtime.settle(35,100);_,undone=saved(session);assert undone<continued
        runtime.tap(245,90);state=runtime.tap(4,25);runtime.capture('clear-dismissed',state)
        runtime.command('rotate 1');runtime.capture('drawing-landscape',runtime.command('status'))
        runtime.command('rotate 0');runtime.command('route 0')
        data_sha=hashlib.sha256(path.read_bytes()).hexdigest()
        state=runtime.command('game-remove drawing-notes');assert not package.exists() and state['games_count']==6
        assert hashlib.sha256(path.read_bytes()).hexdigest()==data_sha
        runtime.command('app-open drawing-notes',ok=False)
        runtime.close();runtime.start();assert not package.exists(),'restart reinstalled a removed utility'
        stage=session/'tf/apps/.staging/drawing.tmp';stage.parent.mkdir(parents=True,exist_ok=True)
        payload=(binary.parent/'tool-packages/drawing-notes.pga').read_bytes();stage.write_bytes(payload)
        runtime.command('game-install 0 drawing.tmp');assert package.read_bytes()==payload and not stage.exists()
        for _ in range(12):runtime.command('games')
        runtime.command('route 0');state=runtime.tap(265,48);assert state['route']==28
        runtime.tap(70,155);runtime.capture('drawing-reinstalled-resume',runtime.command('status'))
        assert saved(session)[1]==undone
        runtime.command('route 0')
        conflicting=session/'tf/apps/games/drawing-notes.pga';conflicting.write_bytes(payload)
        runtime.command('games');runtime.command('app-open drawing-notes',ok=False)
        runtime.command('game-remove drawing-notes',ok=False)
        stage.write_bytes(payload);runtime.command('game-install 1 drawing.tmp',ok=False)
        assert stage.exists() and package.exists() and conflicting.exists()
        conflicting.unlink();stage.unlink();runtime.command('games')
        # 用户直接删TF文件后，重扫须去掉入口；重新安装恢复同一保存排序。
        # Direct file deletion unregisters the destination; reinstall restores its saved position.
        package.unlink();runtime.command('games');runtime.command('app-open drawing-notes',ok=False)
        stage.write_bytes(payload);runtime.command('game-install 0 drawing.tmp')
        runtime.command('route 0');state=runtime.tap(265,48);assert state['route']==28
        report.update(result='PASS',initial_points=points,continued_points=continued,undo_points=undone,
                      package_sha256=hashlib.sha256(payload).hexdigest(),preserved_data_sha256=data_sha)
    finally:
        runtime.close();report['commands']=len(runtime.transcript);report['scenes']=runtime.scenes
        (output/'report.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n')
        (output/'events.json').write_text(json.dumps(runtime.transcript,ensure_ascii=False,indent=2)+'\n')
    print(json.dumps({k:v for k,v in report.items() if k!='scenes'},ensure_ascii=False,indent=2))


if __name__=='__main__':main()

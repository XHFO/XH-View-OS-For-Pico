#!/usr/bin/env python3
"""中文：在私有TF运行同源画板，注入保存故障并验证保稿、重试、放弃、旋转及USB交接。
English: Run the shared drawing app against a private TF with save failures, retention, retry, discard, rotation and USB handoff.

The native process uses a private directory and simulated services. It never
opens a serial port, device TF, existing preview session or running server.
"""
from pathlib import Path
import argparse
import hashlib
import json
import os
import struct
import time

from test_native_preview_drawing import saved, stroke
from test_native_preview_shelf_ble import ROOT, Runtime, read_pgm


def points(path):
    data = path.read_bytes()
    return data[28:]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', required=True, type=Path)
    parser.add_argument('--output', required=True, type=Path)
    args = parser.parse_args()
    binary, output = args.binary.resolve(), args.output.resolve()
    output.mkdir(parents=True, exist_ok=False)
    session = output / 'session'; session.mkdir()
    os.environ['RPICO_HOME_LINK_OFFLINE'] = '1'
    runtime = Runtime(binary, ROOT / 'sdcard/fonts/MiSansVF.ttf', session, output)
    report = {'result': 'RUNNING', 'hardware_access': False,
              'binary_sha256': hashlib.sha256(binary.read_bytes()).hexdigest()}

    def open_drawing():
        state = runtime.command('app-open drawing-notes')
        assert state['route'] == 28, state
        return runtime.tap(60, 145)

    def recover_button(action):
        return runtime.tap(*{'retry': (260, 358), 'discard': (180, 414)}[action])

    try:
        runtime.start(); open_drawing(); stroke(runtime)
        runtime.tap(310, 90)
        path, initial_points = saved(session)
        assert initial_points >= 4
        originals = {str(p.relative_to(session)): hashlib.sha256(p.read_bytes()).hexdigest()
                     for p in (session / 'tf/apps').glob('*/*.pga')}
        stroke(runtime, 220)
        blocked = path.with_suffix('.tmp'); blocked.mkdir()
        original = path.read_bytes()
        state = runtime.command('route 0')
        assert state['route'] == 28 and '保存' in state['games_error'], state
        assert path.read_bytes() == original
        runtime.capture('retained-after-failed-close', state)

        # An outside-origin drag into Retry must neither confirm nor dismiss.
        runtime.command(f'touch down 5 200 {runtime.now+20}')
        runtime.command(f'touch move 260 358 {runtime.now+90}')
        state = runtime.command(f'touch up 260 358 {runtime.now+40}')
        assert state['route'] == 28 and blocked.is_dir() and path.read_bytes() == original
        runtime.capture('outside-drag-does-not-confirm', state)
        # A simple outside tap cancels only the overlay and restores the drawing.
        state = runtime.tap(5, 180)
        assert state['route'] == 28 and path.read_bytes() == original
        runtime.capture('outside-cancel-keeps-original', state)
        state = runtime.command('route 0')
        state = recover_button('retry')
        assert state['route'] == 28 and path.read_bytes() == original, state
        runtime.capture('retry-still-failed', state)
        blocked.rmdir(); state = recover_button('retry')
        assert state['route'] == 0 and saved(session)[1] > initial_points, state
        committed = path.read_bytes()

        # Explicit discard alone releases RAM; the previous committed file stays.
        open_drawing(); stroke(runtime, 260); blocked.mkdir()
        state = runtime.command('route 0'); assert state['route'] == 28
        state = recover_button('discard')
        assert state['route'] == 0 and path.read_bytes() == committed, state
        blocked.rmdir(); open_drawing()
        assert path.read_bytes() == committed

        # 横屏左栏让纸面与竖屏互为转置；整纸旋转不修改文档坐标。
        # The landscape left toolbar exposes the transposed whole paper without changing document coordinates.
        before_points = saved(session)[1]
        runtime.command('rotate 1')
        for x, y, end in ((154, 150, 180), (620, 170, 590)):
            runtime.command(f'touch down {x} {y} {runtime.now+20}')
            runtime.command(f'touch move {end} {y+15} {runtime.now+60}')
            runtime.command(f'touch up {end} {y+20} {runtime.now+40}')
        runtime.tap(74, 282)
        assert saved(session)[1] >= before_points + 6
        stable_data, stable_points = path.read_bytes(), points(path)
        runtime.tick(3000)  # expire notices, then repaint without their transient pixels
        runtime.command("rotate 0"); runtime.command("rotate 1")
        runtime.capture('landscape-full-width-ink', runtime.command('status'))
        wide_frame = read_pgm(runtime.frame)
        for _ in range(8):
            runtime.command('rotate 0'); runtime.command('rotate 1')
            assert path.read_bytes() == stable_data
            assert read_pgm(runtime.frame) == wide_frame
        runtime.command('route 0'); open_drawing()
        runtime.capture('landscape-saved-reopened', runtime.command('status'))
        assert points(path) == stable_points and read_pgm(runtime.frame) == wide_frame
        # 移除旧平移入口；完整纸面不再依赖保存视口中心。
        # The complete page no longer needs the former pan controls or a saved viewport center.
        runtime.tap(80, 334)
        assert path.read_bytes() == stable_data and points(path) == stable_points
        runtime.command('route 0'); open_drawing()
        assert path.read_bytes() == stable_data
        runtime.capture('whole-paper-reopened', runtime.command('status'))

        # USB cannot acquire storage with unsaved RAM. Retry returns to its page.
        runtime.command('rotate 0'); stroke(runtime, 200); blocked.mkdir()
        state = runtime.command('route 22')
        assert state['route'] == 28 and not state['services']['usbActive'], state
        runtime.capture('usb-blocked-by-save-failure', state)
        blocked.rmdir(); state = recover_button('retry')
        assert state['route'] == 22 and not state['services']['usbActive'], state
        runtime.tap(180, 514)
        for _ in range(50):
            time.sleep(.01); state = runtime.tick(100)
            if state['services']['usbActive']:
                break
        assert state['services']['usbActive'], state
        runtime.command('scenario usb-eject')
        for _ in range(60):
            time.sleep(.01); state = runtime.tick(100)
            if not state['services']['usbActive']:
                break
        assert not state['services']['usbActive'] and state['services']['tfMounted'], state
        runtime.settle(8); runtime.command("route 0"); runtime.settle(4)
        open_drawing(); assert saved(session)[1] >= before_points + 6

        # A failed tool switch must preserve the requested tool ID for retry.
        runtime.command('route 13'); state = runtime.tap(90, 190)
        assert state['route'] == 14, state
        game_block = session / 'tf/apps/data/gomoku/games.tmp'
        game_block.parent.mkdir(parents=True, exist_ok=True); game_block.mkdir()
        state = runtime.command('app-open drawing-notes', ok=False)
        assert state['route'] == 14, state
        runtime.capture('tool-switch-failed-keeps-game', state)
        game_block.rmdir(); state = recover_button('retry')
        assert state['route'] == 28, state
        runtime.tap(60, 145)
        assert saved(session)[1] >= before_points + 6
        assert {str(p.relative_to(session)): hashlib.sha256(p.read_bytes()).hexdigest()
                for p in (session / 'tf/apps').glob('*/*.pga')} == originals
        report.update(result='PASS', initial_points=initial_points,
                      final_points=saved(session)[1], rotation_round_trips=8,
                      contracts=['failed close remains in the original application',
                                 'outside drag consumes without action; outside tap cancels safely',
                                 'failed retry preserves RAM; successful retry completes requested route',
                                 'explicit discard preserves the last committed original',
                                 'transposed landscape whole-paper input and stable document coordinates',
                                 'complete original survives rotation and save/reopen without panning',
                                 'USB cannot acquire unsaved storage',
                                 'pending tool ID survives a failed application switch',
                                 'all seven installed package hashes unchanged'])
    finally:
        runtime.close()
        report.update(commands=len(runtime.transcript), scenes=runtime.scenes,
                      transcript=runtime.transcript)
        (output / 'report.json').write_text(json.dumps(report, ensure_ascii=False, indent=2) + '\n')
    print(json.dumps({k: report[k] for k in ('result', 'commands', 'rotation_round_trips')}, ensure_ascii=False))


if __name__ == '__main__':
    main()

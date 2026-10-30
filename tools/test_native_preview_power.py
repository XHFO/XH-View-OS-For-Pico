#!/usr/bin/env python3
"""Exercise real retained pages, TF game modules and all settings through the native runtime; no device access."""
from pathlib import Path
import argparse
import hashlib
import json
import struct
import tempfile
import time
import zlib
from test_native_preview_shelf_ble import Runtime, make_fixtures, page_position, read_pgm
from test_native_settings_groups import root_rect, child_rect, center, keep_awake

ROOT = Path(__file__).resolve().parents[1]


def saved_payload(path):
    data = path.read_bytes()
    magic, size, crc = struct.unpack_from('<III', data)
    payload = data[12:]
    assert magic == 0x31535058 and size == len(payload) and zlib.crc32(payload) == crc, path
    return payload


def board_moves(session, name):
    return struct.unpack_from('<H', saved_payload(session / f'tf/apps/data/{name}/games.dat'), 6)[0]


def set_style(runtime, style, landscape=False):
    runtime.command('route 11')
    width = 640 if landscape else 360
    columns, height = (3, 38) if landscape else (2, 44)
    cell = (width - 32 - (columns - 1)*8)//columns
    runtime.tap(16 + (style % columns)*(cell+8) + cell//2, 84 + (style//columns)*(height+8) + height//2)


def set_keep_awake(runtime, enabled):
    state = runtime.command('route 5')
    landscape = state['logical_width'] == 640
    runtime.tap(*center(root_rect(5, landscape)))
    if keep_awake(runtime.session) != enabled:
        runtime.tap(*center(child_rect(2, landscape)))
    assert keep_awake(runtime.session) is enabled


def set_power_options(runtime, minutes, doubletap):
    state = runtime.command('route 26')
    landscape = state['logical_width'] == 640
    choices = (0, 1, 3, 5, 10, 30)
    index = choices.index(minutes)
    column = 296 if landscape else 328
    width = (column-12)//3
    state = runtime.tap(16+(index % 3)*(width+6)+width//2,
                        (198 if landscape else 222)+(index//3)*36+16)
    assert state['auto_sleep_minutes'] == minutes, state
    if state['doubletap_wake'] is not doubletap:
        state = runtime.tap(476 if landscape else 180, 94 if landscape else 354)
    assert state['doubletap_wake'] is doubletap, state


def ignored_screen_inputs(runtime, locked, landscape=False):
    """锁屏/休眠接触不得唤醒或转发给原页面。 / Locked contacts never wake or reach the retained page."""
    expected = locked['power_state'], locked['route'], locked['active_app']

    def unchanged(state):
        assert (state['power_state'], state['route'], state['active_app']) == expected, state
        return state

    x, y = (300, 150) if landscape else (180, 300)
    unchanged(runtime.tap(x, y))
    # 两次快速完整接触也不能解除锁屏。 / Two rapid completed contacts must not unlock either.
    unchanged(runtime.tap(x, y))
    unchanged(runtime.tap(x, y))
    unchanged(runtime.hold(x, y))
    unchanged(runtime.command(f'touch down {x-70} {y} {runtime.now+20}'))
    unchanged(runtime.command(f'touch move {x+70} {y-60} {runtime.now+80}'))
    return unchanged(runtime.command(f'touch up {x+70} {y-60} {runtime.now+60}'))


def quick_power_cycles(runtime, before, position=None, count=3):
    """连续单击电源必须交替锁定和恢复，保留画面与阅读位置。 / Rapid power presses alternate lock/wake without losing state."""
    for _ in range(count):
        locked = runtime.command('powerkey')
        assert locked['power_state'] == 1, locked
        state = runtime.command('powerkey')
        assert not state['power_state'] and read_pgm(runtime.frame) == before, state
        if position is not None:
            assert page_position(state) == position, state
    return state


def settings(runtime, landscape):
    runtime.command(f'rotate {int(landscape)}')
    width, height = (640, 360) if landscape else (360, 640)
    groups = ((6, 30, 24), (9, 10, 34), (27, 12, 31), (33, 7, 8, 13), (26, 25, None))
    runtime.command('route 5')
    root_frame = read_pgm(runtime.frame)
    assert runtime.tap(*center(root_rect(0, landscape)))['route'] == 21
    assert runtime.tap(23, 47)['route'] == 5
    assert read_pgm(runtime.frame) == root_frame
    for group, expected in enumerate(groups, 1):
        runtime.tap(*center(root_rect(group, landscape)))
        parent_frame = read_pgm(runtime.frame)
        for index, route in enumerate(expected):
            before_awake = keep_awake(runtime.session)
            state = runtime.tap(*center(child_rect(index, landscape)))
            if route is None:
                assert state['route'] == 5 and keep_awake(runtime.session) != before_awake
                runtime.tap(*center(child_rect(index, landscape)))
                assert keep_awake(runtime.session) == before_awake
                runtime.settle(35)
            else:
                module = route in (7, 8)
                assert state['route'] == (28 if module else route) and state['active_app'] == 'system', (landscape, group, index, state)
                assert state['dual_role'] == 0 and not state['dual_connected'], state
                runtime.capture(f'settings-{int(landscape)}-{group}-{index}', state)
                if module:
                    child_frame = read_pgm(runtime.frame)
                    assert runtime.tap(23, 47)['route'] == 28
                    assert read_pgm(runtime.frame) != child_frame, 'Module Back did not return to monitoring'
                assert runtime.tap(23, 47)['route'] == 5
            assert read_pgm(runtime.frame) == parent_frame, (landscape, group, index, 'parent group changed')
        assert runtime.tap(23, 47)['route'] == 5
        assert read_pgm(runtime.frame) == root_frame, (landscape, group, 'root settings changed')
    assert runtime.tap(23, 47)['route'] == 0
    # 关于及引导保留独立返回层级。/ About and onboarding preserve separate Back levels.
    runtime.command('route 5');runtime.tap(width//2, 110)
    about = read_pgm(runtime.frame)
    assert runtime.tap(width//2, height-88)['route'] == 15
    assert runtime.command('key 1 single')['route'] == 5
    assert read_pgm(runtime.frame) == about
    assert runtime.tap(23, 47)['route'] == 5
    assert read_pgm(runtime.frame) == root_frame
    # Dedicated power-style link shares painted/hit geometry in either orientation.
    runtime.command('route 26')
    state = runtime.tap(476 if landscape else 180, 208 if landscape else 458)
    assert state['route'] == 11, state
    runtime.command('route 25')
    save_x, save_y = (480, 266) if landscape else (180, 547)
    state = runtime.tap(save_x, save_y)
    assert state['route'] == 25 and not state['power_state'], state
    runtime.capture(f'datetime-save-{int(landscape)}', state)
    runtime.command('route 5')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', type=Path, default=ROOT/'build/pending25-power-native/native-preview')
    parser.add_argument('--font', type=Path, default=ROOT/'sdcard/fonts/MiSansVF.ttf')
    parser.add_argument('--output', type=Path, default=ROOT/'build/pending25-power-integration')
    parser.add_argument('--scope', choices=('all', 'lifecycle'), default='all',
                        help='Lifecycle isolates power checks; the default also requires grouped settings/navigation.')
    args = parser.parse_args()
    output = args.output.resolve(); output.mkdir(parents=True, exist_ok=True)
    session = Path(tempfile.mkdtemp(prefix='session-', dir=output))
    runtime = Runtime(args.binary.resolve(), args.font.resolve(), session, output)
    checks = []
    report = {'result': 'RUNNING', 'scope': args.scope, 'settings_exercised': args.scope == 'all',
              'hardware_access': False, 'checks': checks,
              'binary_sha256': hashlib.sha256(args.binary.read_bytes()).hexdigest()}
    try:
        state = runtime.start()
        assert state['games_count'] == 6, state
        make_fixtures(session/'tf/books')
        book_hashes = {path.name: hashlib.sha256(path.read_bytes()).hexdigest()
                       for path in (session/'tf/books').iterdir() if path.is_file()}
        runtime.command('library'); runtime.settle(35)
        if args.scope == 'all':
            for landscape in (False, True):
                settings(runtime, landscape)
                checks.append(f'17-grouped-settings-and-about-guide-power-date-links-{int(landscape)}')
            # 管理服务按设计阻止自动休眠；通过真实按钮结束它再计时。/ Management blocks auto-sleep; stop it through its real button before timing.
            state = runtime.command('route 30')
            landscape = state['logical_width'] == 640
            before_management = read_pgm(runtime.frame)
            runtime.tap(476 if landscape else 264, 278 if landscape else 564)
            assert read_pgm(runtime.frame) != before_management, 'Stop management did not change the active service UI'
            checks.append('wireless-management-stopped-before-auto-sleep')
        set_power_options(runtime, 1, False)
        for landscape in (False, True):
            runtime.command(f'rotate {int(landscape)}')
            for style in range(4):
                set_style(runtime, style, landscape)
                runtime.command('route 0'); state = runtime.settle(30)
                before = read_pgm(runtime.frame)
                state = runtime.command('powerkey')
                assert state['power_state'] == 1 and state['route'] == 0 and state['active_app'] == 'system', state
                runtime.capture(f'lock-{int(landscape)}-{style}', state)
                assert runtime.command('route 5', ok=False)['power_state'] == 1
                ignored_screen_inputs(runtime, state, landscape)
                state = runtime.command('powerkey')
                assert not state['power_state'] and read_pgm(runtime.frame) == before, (landscape, style, state)
                runtime.capture(f'restored-{int(landscape)}-{style}', state)
                runtime.settle(10)
                assert read_pgm(runtime.frame) == before, 'Restored shelf or cached covers repainted differently'
                quick_power_cycles(runtime, before)
                checks.append(f'power-key-only-exact-shelf-restore-and-quick-cycles-style-{style}-orientation-{int(landscape)}')
        assert all(hashlib.sha256((session/'tf/books'/name).read_bytes()).hexdigest() == digest
                   for name, digest in book_hashes.items()), 'Power cycles modified fixture books'
        runtime.command('rotate 0'); set_style(runtime, 3)
        runtime.command('route 0'); runtime.settle(25); runtime.style(1)
        state = runtime.tap(70, 350); state = runtime.settle(8)
        assert state['active_app'] == 'book', state
        runtime.command('key 2 single'); state = runtime.command('status')
        position, before = page_position(state), read_pgm(runtime.frame)
        state = runtime.command('powerkey')
        assert state['power_state'] == 1 and page_position(state) == position, state
        ignored_screen_inputs(runtime, state)
        state = runtime.command('powerkey')
        assert not state['power_state'] and page_position(state) == position and read_pgm(runtime.frame) == before, state
        state = quick_power_cycles(runtime, before, position)
        runtime.capture('real-book-powerkey-restored', state)
        checks.append('real-book-power-key-only-wake-exact-page-frame-and-quick-cycles')
        state = runtime.command('power 1')
        assert state['power_state'] == 2 and page_position(state) == position, state
        state = ignored_screen_inputs(runtime, state)
        assert state['power_state'] == 2 and page_position(state) == position, state
        state = runtime.command('powerkey')
        assert not state['power_state'] and page_position(state) == position and read_pgm(runtime.frame) == before, state
        runtime.capture('real-book-restored', state); checks.append('real-book-instance-position-frame-and-sleep-no-doubletap')
        # The shared timer also runs in the actual reader child; keep-awake defaults on in host sessions.
        set_keep_awake(runtime, False)
        runtime.command('route 0'); runtime.settle(20); runtime.tap(70, 350); runtime.settle(8)
        runtime.command('power 0'); runtime.command('powerkey')
        assert runtime.tick(59999)['power_state'] == 0
        assert runtime.tick(1)['power_state'] == 1
        runtime.command('powerkey'); checks.append('configured-60s-idle-in-real-book')
        runtime.command('route 0')
        runtime.command('power 0'); runtime.command('powerkey')
        assert runtime.tick(59999)['power_state'] == 0
        assert runtime.tick(1)['power_state'] == 1
        runtime.command('powerkey'); checks.append('configured-60s-idle-on-shelf')
        set_keep_awake(runtime, True); runtime.command('route 0')
        assert runtime.tick(240000)['power_state'] == 0
        assert runtime.command('power 0')['power_state'] == 1
        runtime.command('powerkey'); checks.append('keep-awake-blocks-only-automatic-entry')
        # Every actual independently loaded board module must be stopped before persistence and resume after wake.
        for kind, name in enumerate(('gomoku', 'go', 'xiangqi', 'chess')):
            runtime.command('route 13'); runtime.tap(90 if kind%2 == 0 else 260, 210+(kind//2)*142)
            runtime.tap(282, 168); runtime.tap(180, 380)
            if kind < 2:
                runtime.tap(180, 260)
            elif kind == 2:
                runtime.tap(30, 334); runtime.tap(30, 296)
            else:
                runtime.tap(200, 362); runtime.tap(200, 280)
            state = runtime.command('power 0')
            assert state['power_state'] == 1 and board_moves(session, name) == 1, (name, state)
            path = session/f'tf/apps/data/{name}/games.dat'; stored = path.read_bytes()
            runtime.tick(5000)
            assert path.read_bytes() == stored, name
            state = runtime.command('powerkey')
            assert not state['power_state'] and state['route'] == 14, state
            for attempt in range(50):
                runtime.tick(100); time.sleep(.02)
                if board_moves(session, name) == 2: break
            assert board_moves(session, name) == 2, name
            runtime.capture('ai-resumed-'+name, state); checks.append('joined-locked-and-resumed-'+name)
        runtime.command('route 13'); state = runtime.tap(260, 493)
        assert state['route'] == 23, state
        runtime.tap(240, 576)
        # Game modules use the real platform monotonic clock, not the UI tick clock.
        time.sleep(.8); runtime.tick(750)
        state = runtime.command('power 0'); assert state['power_state'] == 1, state
        path = session/'tf/apps/data/snake/arcade1.dat'; paused = path.read_bytes()
        assert struct.unpack_from('<I', saved_payload(path), 88)[0] == 1
        runtime.tick(5000); assert path.read_bytes() == paused
        runtime.command('powerkey'); runtime.tick(100)
        before = read_pgm(runtime.frame)
        runtime.tick(5000)
        assert path.read_bytes() == paused and read_pgm(runtime.frame) == before
        runtime.tap(240, 576); time.sleep(.8); runtime.tick(750); runtime.command('power 0')
        assert struct.unpack_from('<I', saved_payload(path), 88)[0] == 2
        runtime.command('powerkey'); checks.append('snake-paused-through-lock-and-wake-until-explicit-resume')
        state = runtime.command('power 2')
        assert not state['power_state'], state
        state = runtime.command('power 3'); assert state['power_state'] == 4, state
        runtime.capture('simulated-off', state); checks.append('restart-and-off-native-simulation')
        report.update(result='PASS',session=str(session),commands=len(runtime.transcript),power_key_semantics='shared-toggle-lock-wake',native_screen_inputs_ignored=['single-tap','double-tap','hold','drag'],native_multitouch_injected=False,physical_power_or_wake_measured=False,physical_bluetooth_or_panel_tested=False)
        (output/'report.json').write_text(json.dumps(report, ensure_ascii=False, indent=2)+'\n')
        print(json.dumps(report, ensure_ascii=False, indent=2))
    finally:
        (session/'transcript.json').write_text(json.dumps(runtime.transcript, ensure_ascii=False, indent=2)+'\n')
        runtime.close()
        if report['result'] == 'RUNNING':
            report['result'] = 'FAILED'
        report['checks'] = checks
        (output/'report.json').write_text(json.dumps(report, ensure_ascii=False, indent=2)+'\n')


if __name__ == '__main__':
    main()

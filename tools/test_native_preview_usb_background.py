#!/usr/bin/env python3
"""同源后台USB的全局交互回归，仅使用合成图书和私有虚拟TF。
Exercise native background USB/global controls with synthetic books and private virtual TF only.
"""
from pathlib import Path
import argparse
import hashlib
import json
import tempfile

from test_native_preview_shelf_ble import Runtime

ROOT = Path(__file__).resolve().parents[1]


def files(root):
    return {str(p.relative_to(root)): (p.stat().st_mtime_ns, hashlib.sha256(p.read_bytes()).hexdigest())
            for p in root.rglob('*') if p.is_file()}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', type=Path, default=ROOT / 'build/native-preview-candidate/native-preview')
    parser.add_argument('--font', type=Path, default=ROOT / 'sdcard/fonts/MiSansVF.ttf')
    parser.add_argument('--output', type=Path, default=ROOT / 'build/native-preview-usb-background')
    args = parser.parse_args()
    binary, font, output = args.binary.resolve(), args.font.resolve(), args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    (output / 'report.json').unlink(missing_ok=True)
    cases = []
    for landscape in (False, True):
        for book in (False, True):
            session = Path(tempfile.mkdtemp(prefix='session-', dir=output))
            books = session / 'tf/books'
            books.mkdir(parents=True)
            (books / 'synthetic.txt').write_text('清晨的窗前放着一本书，这是合成测试内容。\n' * 800)
            runtime = Runtime(binary, font, session, output)
            case = {'landscape': landscape, 'legacy_book': book}
            try:
                runtime.start()
                runtime.settle()
                if book:
                    runtime.tap(70, 350)
                    state = runtime.settle(8)
                    assert state['active_app'] == 'book', state
                runtime.command(f'rotate {int(landscape)}')
                runtime.command('usb-background')
                state = runtime.settle(8)
                assert state['services']['usbActive'] and not state['services']['tfMounted'], state
                retained = (state['route'], state['active_app'], state.get('book_page'))
                before = files(session / 'tf')
                for key in (0, 2):
                    state = runtime.command(f'key {key} single')
                    assert (state['route'], state['active_app'], state.get('book_page')) == retained, state
                state = runtime.command('key 1 single')
                assert state['active_app'] == 'system' and state['route'] == 5 and not state['panel'], state
                runtime.command('route 24')
                state = runtime.tap(100, 100)
                state = runtime.settle(8)
                assert state['route'] == 24 and state['ble_enabled'], state
                runtime.capture(f'usb-live-bluetooth-{int(landscape)}-{int(book)}', state)
                state = runtime.command('key 1 single')
                assert state['route'] == 5 and not state['panel'], state
                state = runtime.command('key 1 double')
                assert state['panel'] == 3 and state['services']['usbActive'], state
                state = runtime.command('key 1 single')
                assert state['panel'] == 0, state
                state = runtime.tap(8, 347 if landscape else 627)
                assert state['fan'], state
                state = runtime.command('key 1 single')
                assert not state['fan'], state
                for route in (0, 1, 13, 20, 26, 28):
                    state = runtime.command(f'route {route}')
                    assert state['route'] == 5 and not state['panel'] and not state['fan'], state
                for command in ('powerkey', 'powerlong', 'sleep'):
                    state = runtime.command(command)
                    assert state['power_state'] == 0 and state['route'] == 5, state
                runtime.settle(40)
                stable = runtime.command('status')
                runtime.command('scenario usb-writing')
                state = runtime.settle(8)
                assert state['presents'] == stable['presents'], (stable, state)
                assert files(session / 'tf') == before, 'Local UI changed TF files during host ownership'
                state = runtime.command('scenario usb-eject')
                state = runtime.settle(20)
                assert not state['services']['usbActive'] and state['services']['tfMounted'] and state['route'] == 5, state
                state = runtime.command('route 0')
                assert state['route'] == 0, state
                case.update({'result': 'PASS', 'retained_origin': retained, 'commands': len(runtime.transcript),
                             'safe_ble_touch': True, 'global_center_fan_switcher': True,
                             'tf_routes_power_blocked': True, 'unchanged_tf_while_exported': True,
                             'write_activity_does_not_present': True, 'host_eject_reclaimed': True})
                cases.append(case)
            finally:
                runtime.close()
                (session / 'transcript.json').write_text(json.dumps(runtime.transcript, ensure_ascii=False, indent=2) + '\n')
    report = {'result': 'PASS', 'binary_sha256': hashlib.sha256(binary.read_bytes()).hexdigest(),
              'cases': cases, 'physical_usb_tested': False, 'physical_epd_tested': False,
              'private_virtual_tf_only': True}
    (output / 'report.json').write_text(json.dumps(report, ensure_ascii=False, indent=2) + '\n')
    print(json.dumps({'result': 'PASS', 'cases': len(cases), 'commands': sum(c['commands'] for c in cases), 'output': str(output)}))


if __name__ == '__main__':
    main()

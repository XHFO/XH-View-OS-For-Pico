#!/usr/bin/env python3
"""Shared firmware board-lock/background-reception regression using private TF data; no device access."""
import argparse
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import tempfile
import urllib.parse

from test_native_desktop import gray_bitmap

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    build = args.build.resolve()
    args.output.mkdir(parents=True, exist_ok=True)
    spec = importlib.util.spec_from_file_location('board_lock_preview', ROOT.parents[1] / 'outputs/firmware-preview/server.py')
    server = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(server)
    server.REPO = ROOT
    server.MANAGEMENT_PORT = 1
    os.environ['RPICO_HOME_LINK_OFFLINE'] = '1'
    checks = []
    with tempfile.TemporaryDirectory(prefix='board-lock-', dir=args.output) as temporary:
        # Use the existing native Session transport without opening a listening socket or a live browser.
        server.BUILD = Path(temporary)
        for name in ('native-preview', 'game-packages', 'tool-packages'):
            (server.BUILD / name).symlink_to(build / name)
        session = server.Session()
        try:
            def command(text):
                result = session.command(text)
                assert result.get('ok', True), (text.split()[0], result)
                return result

            def tick():
                return command('tick ' + str(session.state['now_ms'] + 200))

            def rpc(method, path, body=None):
                value = {'key': key, 'method': method, 'path': path, 'query': '', 'body': json.dumps(body or {})}
                return command('manage-web ' + json.dumps(value).encode().hex())['management_response']

            def status():
                result = rpc('GET', '/api/manage/pico/status')
                assert result['status'] == 200, result
                return result['body']

            def push(name, reverse=False):
                target = session.root / 'tf/.desktop-inbox' / name
                target.parent.mkdir(parents=True, exist_ok=True)
                payload = gray_bitmap()
                if reverse:
                    payload = payload[:118] + bytes(value ^ 255 for value in payload[118:])
                target.write_bytes(payload)
                response = command('desktop-canvas ' + key + ' ' + name)['management_response']
                assert response['status'] == 202, response
                return tick()

            command('scenario default')
            opened = command('manage-open')['management']
            assert opened['active'], opened
            key = urllib.parse.parse_qs(urllib.parse.urlsplit(opened['url']).fragment)['key'][0]
            response = rpc('PUT', '/api/manage/pico/config', {'auto_sleep_minutes': 0, 'double_tap_wake': True, 'lock_style': 5})
            assert response['status'] == 202, response
            tick()
            assert status()['config']['lock_style'] == 5
            command('route 1')
            before = session.frame.read_bytes()
            received = push('canvas-background.bmp')
            assert received['route'] == 1 and not received['power_state'] and session.frame.read_bytes() == before
            assert status()['canvas']['state'] == 'saved'
            assert received['services']['tfMounted'] and not received['services']['usbActive']
            checks.append('network-reception-retains-reading-frame-and-TF-with-saved-receipt')

            for scenario in ('power-charging', 'power-full'):
                command('scenario ' + scenario)
                lock = command('powerkey')
                assert lock['power_state'] == 1 and lock['route'] == 1
                assert status()['canvas']['state'] == 'displayed'
                assert status()['power']['external_power'] is True
                assert status()['power']['charging'] is (scenario == 'power-charging')
                lock_frame = session.frame.read_bytes()
                (args.output / (scenario + '.png')).write_bytes(session.cached_frame)
                lock_revision = lock['revision']
                for _ in range(5):
                    assert tick()['revision'] == lock_revision
                assert session.frame.read_bytes() == lock_frame
                assert command('powerkey')['power_state'] == 0 and session.frame.read_bytes() == before
                checks.append(scenario + '-supports-board-lock-no-idle-repaints-and-exact-wake-restore')

            command('scenario power-full')
            command('powerkey')
            displayed = status()['canvas']['revision']
            previous_lock_frame = session.frame.read_bytes()
            after = push('canvas-new.bmp', reverse=True)
            assert after['power_state'] == 1 and after['route'] == 1 and session.frame.read_bytes() != previous_lock_frame
            assert status()['canvas']['revision'] > displayed and status()['canvas']['state'] == 'displayed'
            checks.append('locked-board-adopts-new-network-frame-with-real-display-receipt')
            command('scenario power-battery')
            tick()
            assert session.state['power_state'] == 1 and session.state['route'] == 1
            assert rpc('GET', '/api/manage/pico/status')['status'] == 401
            assert command('powerkey')['power_state'] == 0 and session.frame.read_bytes() == before
            checks.append('unplug-stops-management-retains-locked-frame-and-power-key-restores-page')

            command('scenario default')
            opened = command('manage-open')['management']
            key = urllib.parse.parse_qs(urllib.parse.urlsplit(opened['url']).fragment)['key'][0]
            state = command('app-open drawing-notes')
            tick()
            before_drawing = session.frame.read_bytes()
            after = push('canvas-drawing.bmp')
            assert after['route'] == state['route'] and after['active_app'] == state['active_app']
            assert session.frame.read_bytes() == before_drawing and status()['canvas']['state'] == 'saved'
            checks.append('network-reception-does-not-interrupt-drawing-application')
        finally:
            session.close()
    report = {'result': 'PASS', 'hardware_access': False, 'host_network_access': False,
              'binary_sha256': hashlib.sha256((build / 'native-preview').read_bytes()).hexdigest(), 'checks': checks,
              'boundary': 'Shared native firmware state/display checks; no physical power or panel timing claim'}
    (args.output / 'report.json').write_text(json.dumps(report, ensure_ascii=False, indent=2) + '\n')
    print(json.dumps(report, ensure_ascii=False))


if __name__ == '__main__':
    main()

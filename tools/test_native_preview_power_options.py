#!/usr/bin/env python3
"""中文：冻结候选的电源设置/锁屏唤醒/长按菜单原生与独立 HTTP 专项。
English: Focused power-options, lock-wake and long-key menu checks against a frozen native candidate.
No hardware, browser, production preview service, or game regression suite is used.
"""
from __future__ import annotations

import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import shutil
import struct
import threading
import urllib.request
import zlib

from test_native_preview_shelf_ble import Runtime, page_position, write_png

ROOT = Path(__file__).resolve().parents[1]
SERVER = ROOT.parents[1] / 'outputs/firmware-preview/server.py'
CHOICES = (0, 1, 3, 5, 10, 30)


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def decoded_frame(data):
    if data.startswith(b'P5\n'):
        signature, dimensions, maximum, pixels = data.split(b'\n', 3)
        width, height = map(int, dimensions.split())
        assert signature == b'P5' and maximum == b'255' and len(pixels) == width*height
        return width, height, pixels
    assert data.startswith(b'\x89PNG\r\n\x1a\n')
    offset, compressed, dimensions = 8, bytearray(), None
    while offset < len(data):
        size, = struct.unpack_from('>I', data, offset)
        tag, body = data[offset+4:offset+8], data[offset+8:offset+8+size]
        if tag == b'IHDR':
            width, height, depth, color, compression, filtering, interlace = struct.unpack('>IIBBBBB', body)
            assert (depth, color, compression, filtering, interlace) == (8, 0, 0, 0, 0)
            dimensions = width, height
        elif tag == b'IDAT':
            compressed.extend(body)
        offset += size+12
    assert dimensions
    width, height = dimensions
    raw = zlib.decompress(compressed)
    assert len(raw) == (width+1)*height
    assert all(raw[y*(width+1)] == 0 for y in range(height)), 'Preview PNG must use unfiltered gray rows'
    return width, height, b''.join(raw[y*(width+1)+1:(y+1)*(width+1)] for y in range(height))


class Case:
    def __init__(self, output):
        self.output = output
        self.output.mkdir(parents=True)
        self.state, self.now, self.transcript, self.checks = {}, 10000, [], []
        self.keep_awake = True

    def remember(self, value, state):
        assert state['ok'], (value, state)
        self.state, self.now = state, state['now_ms']
        self.transcript.append({'command': value, 'state': state})
        return state

    def tick(self, delta=100):
        return self.command(f'tick {self.now+delta}')

    def tap(self, x, y):
        self.command(f'touch down {x} {y} {self.now+20}')
        return self.command(f'touch up {x} {y} {self.now+60}')

    def settle(self, count=12, delta=100):
        for _ in range(count):
            self.tick(delta)
        return self.state

    def capture(self, name):
        width, height, pixels = decoded_frame(self.frame_bytes())
        write_png(self.output/(name+'.png'), width, height, pixels)
        return width, height, pixels

    def check(self, name, **details):
        self.checks.append({'name': name, **details})

    def set_keep_awake(self, enabled):
        self.command('route 5')
        observed = self.state.get('keep_awake', self.keep_awake)
        if observed != enabled:
            self.tap(self.state['logical_width']-80, 110)
        self.keep_awake = enabled
        if 'keep_awake' in self.state:
            assert self.state['keep_awake'] is enabled, self.state
        return self.command('route 26')

    def set_minutes(self, minutes, landscape=False):
        self.command('route 26')
        index = CHOICES.index(minutes)
        column = 296 if landscape else 328
        width = (column-12)//3
        x = 16+(index % 3)*(width+6)+width//2
        y = (198 if landscape else 222)+(index//3)*36+16
        state = self.tap(x, y)
        assert state['auto_sleep_minutes'] == minutes, (landscape, minutes, state)
        return state

    def set_doubletap(self, enabled, landscape=False):
        self.command('route 26')
        if self.state['doubletap_wake'] is not enabled:
            self.tap(476 if landscape else 180, 94 if landscape else 354)
        assert self.state['doubletap_wake'] is enabled, self.state
        return self.state

    def menu(self, index, landscape=False):
        if landscape:
            self.tap(164+312*(index % 2), 122+80*(index//2))
        else:
            self.tap(180, 116+68*index)
        # UI handlers enqueue the selected transition; the next app-loop step executes it.
        return self.tick(1)

    def open_book(self):
        self.command('rotate 0')
        self.command('route 0')
        self.settle(25)
        self.tap(70, 350)
        self.settle(10)
        state = self.state
        assert state['active_app'] == 'book' and state['book_pages'] > 1, state
        return self.state

    def restart(self):
        raise NotImplementedError


class NativeCase(Case):
    def __init__(self, source, build, output):
        super().__init__(output)
        self.session = output/'session'
        (self.session/'tf/books').mkdir(parents=True)
        self.runtime = Runtime(build/'native-preview', source/'sdcard/fonts/MiSansVF.ttf', self.session, output)
        self.book = self.session/'tf/books/power-options.txt'
        self.book.write_text('电源设置合成阅读测试\n'+'这是专门生成的电源测试文字，用于确认保留阅读页、恢复画面与阅读位置，不含用户资料。\n'*2000)

    def start(self):
        return self.remember('process-start', self.runtime.start())

    def command(self, value):
        return self.remember(value, self.runtime.command(value))

    def frame_bytes(self):
        return self.runtime.frame.read_bytes()

    def restart(self):
        self.runtime.close()
        return self.start()

    def close(self):
        self.runtime.close()


class HttpCase(Case):
    def __init__(self, source, build, output):
        super().__init__(output)
        spec = importlib.util.spec_from_file_location('isolated_power_options_server', SERVER)
        self.module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(self.module)
        self.module.REPO = source
        self.module.BUILD = output/'server'
        self.module.BUILD.mkdir()
        shutil.copy2(build/'native-preview', self.module.BUILD/'native-preview')
        if (build/'game-packages').is_dir():
            shutil.copytree(build/'game-packages', self.module.BUILD/'game-packages')
        manifest = json.loads((build/'native-preview-build.json').read_text())
        self.module.BASELINE = {'version': manifest['version'], 'source_root': str(source), 'candidate': True,
                                'source_sha256': manifest['sources'], 'hardware_access': False}
        self.server = self.module.ThreadingHTTPServer(('127.0.0.1', 0), self.module.Handler)
        assert self.server.server_port not in (8775, 8777), 'Do not access existing preview ports'
        self.worker = threading.Thread(target=self.server.serve_forever, daemon=True)
        self.worker.start()
        self.origin = 'http://127.0.0.1:'+str(self.server.server_port)
        self.id = None

    def request(self, path, data=None, headers=None):
        if isinstance(data, dict):
            data = json.dumps(data).encode()
            headers = {'Content-Type': 'application/json', **(headers or {})}
        with urllib.request.urlopen(urllib.request.Request(self.origin+path, data=data, headers=headers or {}), timeout=45) as response:
            body = response.read()
            return json.loads(body) if 'application/json' in response.headers.get('Content-Type', '') else body

    def start(self):
        state = self.request('/api/session', {})
        self.id = state['session']
        self.session = self.module.SESSIONS[self.id].root
        self.book = self.session/'tf/books/power-options.txt'
        contents = ('电源设置合成阅读测试\n'+'这是专门生成的电源测试文字，用于确认保留阅读页、恢复画面与阅读位置，不含用户资料。\n'*2000).encode()
        result = self.request('/api/import?session='+self.id, contents,
                              {'Content-Type': 'application/octet-stream', 'X-File-Name': self.book.name})
        assert result['ok']
        return self.remember('http-session-and-book-import', result['state'])

    def command(self, value):
        words = value.split()
        action, data = words[0], {}
        if action == 'touch':
            data = dict(phase=words[1], x=int(words[2]), y=int(words[3]), ms=int(words[4]))
        elif action == 'tick':
            data = dict(ms=int(words[1]))
        elif action in ('route', 'rotate', 'power'):
            data = {dict(route='route', rotate='landscape', power='action_id')[action]: int(words[1])}
        elif action == 'key':
            data = dict(key=int(words[1]), mode='click' if words[2] == 'single' else words[2])
        elif action == 'scenario':
            data = dict(name=words[1])
        result = self.request('/api/command', {'session': self.id, 'action': action, **data})
        return self.remember(value, result)

    def frame_bytes(self):
        return self.request(self.state['image_url'])

    def restart(self):
        return self.remember('http-process-restart', self.request('/api/restart', {'session': self.id}))

    def close(self):
        self.server.shutdown()
        self.worker.join(timeout=5)
        self.server.server_close()
        for session in list(self.module.SESSIONS.values()):
            session.close()


def doubletap(case, expected, completed=True):
    x, y = case.state['logical_width']//2, case.state['logical_height']//2
    case.tap(x, y)
    assert case.state['power_state'] == expected, 'First completed tap woke the device'
    case.command(f'touch down {x} {y} {case.now+90}')
    assert case.state['power_state'] == expected, 'Second down woke before release'
    if completed:
        return case.command(f'touch up {x} {y} {case.now+60}')
    return case.state


def exercise(case):
    state = case.start()
    assert state.get('power_simulated') is True and state.get('power_long_available') is True, state
    assert state['auto_sleep_minutes'] == 3 and state['doubletap_wake'] is True, state
    case.check('fresh-defaults-three-minutes-and-doubletap-enabled')
    book_hash = sha(case.book)
    for landscape in (False, True):
        case.command(f'rotate {int(landscape)}')
        for minutes in CHOICES:
            case.set_minutes(minutes, landscape)
            if not landscape:
                state = case.restart()
                assert state['auto_sleep_minutes'] == minutes, state
                case.command('rotate 0')
        for enabled in (False, True):
            case.set_doubletap(enabled, landscape)
        case.capture('power-settings-'+('landscape' if landscape else 'portrait'))
        case.check('all-six-time-options-and-doubletap-hit-layout-'+str(int(landscape)))
    case.set_doubletap(False, True)
    assert case.restart()['doubletap_wake'] is False
    case.check('doubletap-preference-persists-across-process-restart')
    case.command('rotate 0')
    case.set_keep_awake(False)
    case.set_minutes(0)
    case.command('route 0')
    case.tap(346, 83)
    assert case.tick(12*60*60000)['power_state'] == 0, case.state
    case.check('auto-off-does-not-lock-after-twelve-simulated-hours')
    for minutes in CHOICES[1:]:
        case.set_minutes(minutes)
        case.command('route 0')
        case.tap(346, 83)
        assert case.tick(minutes*60000-1)['power_state'] == 0, (minutes, case.state)
        assert case.tick(1)['power_state'] == 1, (minutes, case.state)
        assert case.command('powerkey')['power_state'] == 0
    case.check('all-five-positive-timeouts-at-exact-shared-clock-boundary')
    case.set_minutes(1)
    case.set_keep_awake(True)
    case.capture('keep-awake-priority-notice')
    case.command('route 0')
    assert case.tick(30*60000)['power_state'] == 0
    assert case.command('powerkey')['power_state'] == 1
    assert case.command('powerkey')['power_state'] == 0
    case.check('keep-awake-blocks-auto-but-never-blocks-manual-power-click')
    case.set_keep_awake(False)
    case.set_minutes(1)
    case.open_book()
    case.tap(346, 83)
    assert case.tick(59999)['power_state'] == 0
    assert case.tick(1)['power_state'] == 1
    assert case.command('powerkey')['power_state'] == 0
    case.check('same-one-minute-clock-applies-in-actual-reader-child')
    case.set_keep_awake(True)
    case.set_doubletap(False)
    case.open_book()
    case.command('key 2 single')
    case.settle(5)
    before, position = case.frame_bytes(), page_position(case.state)
    for _ in range(3):
        assert case.command('powerkey')['power_state'] == 1
        assert doubletap(case, 1)['power_state'] == 1
        assert case.command('powerkey')['power_state'] == 0
        assert page_position(case.state) == position and case.frame_bytes() == before
    case.check('disabled-doubletap-and-rapid-power-cycles-retain-reader-frame-position')
    case.set_doubletap(True)
    case.open_book()
    before, position = case.frame_bytes(), page_position(case.state)
    assert case.command('powerkey')['power_state'] == 1
    case.tap(180, 300)
    assert case.state['power_state'] == 1
    case.tick(600)
    assert doubletap(case, 1)['power_state'] == 0
    assert page_position(case.state) == position and case.frame_bytes() == before
    case.check('enabled-doubletap-wakes-only-on-second-completed-release')
    assert case.command('powerkey')['power_state'] == 1
    case.command(f'touch down 180 300 {case.now+20}')
    case.tick(450)
    case.command(f'touch up 180 300 {case.now+20}')
    case.tap(180, 300)
    assert case.state['power_state'] == 1
    case.command('powerkey')
    case.check('hold-then-one-tap-does-not-count-as-a-doubletap')
    case.command('route 26')
    case.tap(264, 98)
    assert case.tick(1)['power_state'] == 2
    assert doubletap(case, 2)['power_state'] == 2
    assert case.command('powerkey')['power_state'] == 0
    case.open_book()
    assert page_position(case.state) == position
    case.check('explicit-deep-sleep-ignores-screen-doubletap-even-when-enabled')
    for landscape in (False, True):
        # The physical Home action clears the reader-return stack; route is navigation, not Home.
        case.command('key 1 long')
        assert case.state['route'] == 0 and case.state['active_app'] == 'system', case.state
        case.command(f'rotate {int(landscape)}')
        assert case.command('powerlong')['power_state'] == 0
        assert case.state['route'] == 26, case.state
        if 'power_menu_open' in case.state:
            assert case.state['power_menu_open'] is True
        assert case.tick(1800)['power_state'] == 0
        case.capture('long-key-menu-'+str(int(landscape)))
        case.menu(3, landscape)
        assert case.state['power_state'] == 0 and case.state['route'] == 0, case.state
        case.command('powerlong')
        assert case.menu(2, landscape)['power_state'] == 1
        state = case.command('powerlong')
        assert state['power_state'] == 0 and state['route'] == 26, state
        case.menu(3, landscape)
        assert case.state['power_state'] == 0
        case.check('long-key-opens-without-off-cancel-lock-and-locked-entry-'+str(int(landscape)))
    case.command('rotate 0')
    case.command('route 0')
    case.command('powerlong')
    presents = case.state['presents']
    state = case.menu(1)
    # The host app loop can paint "restarting" and restore within one command.
    assert state['power_state'] == 0 and state['route'] == 0 and state['presents'] >= presents+2, state
    case.capture('after-restart-native-simulation')
    case.check('restart-runs-only-after-explicit-menu-choice-native-simulation')
    case.command('route 11')
    case.tap(262, 164)
    for landscape in (False, True):
        case.command('key 1 long')
        case.command(f'rotate {int(landscape)}')
        case.settle(12)
        before_pixels = decoded_frame(case.frame_bytes())
        case.command('powerlong')
        presents = case.state['presents']
        state = case.menu(0, landscape)
        assert state['power_state'] == 4 and state['presents'] > presents and not state['refresh_partial'], state
        assert state['refresh_area'] == [0, 0, state['width'], state['height']], state
        width, height, off_pixels = case.capture('off-keeps-selected-lock-screen-and-bottom-marker-'+('landscape' if landscape else 'portrait'))
        assert (width, height) == before_pixels[:2]
        logical_height = state['logical_height']
        stable_rows = height*(logical_height-100)//logical_height
        assert off_pixels[:stable_rows*width] == before_pixels[2][:stable_rows*width], 'Retained-screen style was replaced on simulated shutdown'
        assert off_pixels[stable_rows*width:] != before_pixels[2][stable_rows*width:], 'Shutdown marker missing from bottom region'
        off = case.frame_bytes()
        assert case.tick(10000)['power_state'] == 4 and case.frame_bytes() == off
        assert doubletap(case, 4)['power_state'] == 4 and case.frame_bytes() == off
        assert case.command('powerkey')['power_state'] == 4 and case.frame_bytes() == off
        assert case.command('wake')['power_state'] == 4 and case.frame_bytes() == off
        case.check('off-full-refresh-preserves-retained-style-bottom-marker-and-static-frame-native-simulation-'+str(int(landscape)))
        state = case.command('powerlong')
        assert state['power_state'] == 0 and state['route'] == 0, state
        case.check('off-ignores-short-click-compatibility-wake-and-touch-long-key-simulates-home-start-'+str(int(landscape)))
    assert sha(case.book) == book_hash
    case.check('synthetic-book-unchanged-by-all-power-transitions')
    return case.checks


def verify_frozen(source, build):
    file = build/'native-preview-build.json'
    manifest = json.loads(file.read_text())
    assert Path(manifest['source_root']).resolve() == source, manifest['source_root']
    assert sha(build/'native-preview') == manifest['binary_sha256']
    for name, digest in manifest['sources'].items():
        assert sha(source/name) == digest, 'Frozen candidate changed: '+name
    return manifest, sha(file)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source-root', type=Path, required=True)
    parser.add_argument('--build-dir', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--mode', choices=('native', 'http', 'both'), default='both')
    args = parser.parse_args()
    source, build, output = args.source_root.resolve(), args.build_dir.resolve(), args.output.resolve()
    if output.exists() and any(output.iterdir()):
        raise SystemExit('Use a fresh output directory; existing acceptance evidence is never overwritten')
    output.mkdir(parents=True, exist_ok=True)
    manifest, manifest_hash = verify_frozen(source, build)
    baseline = SERVER.with_name('baseline.json')
    baseline_hash = sha(baseline)
    runs = {}
    try:
        for name, cls in (('native', NativeCase), ('http', HttpCase)):
            if args.mode not in (name, 'both'):
                continue
            case = cls(source, build, output/name)
            try:
                checks = exercise(case)
                runs[name] = {'result': 'PASS', 'checks': checks, 'commands': len(case.transcript)}
            finally:
                (output/name/'transcript.json').write_text(json.dumps(case.transcript, ensure_ascii=False, indent=2)+'\n')
                case.close()
        assert verify_frozen(source, build)[1] == manifest_hash and sha(baseline) == baseline_hash
        report = {'result': 'PASS', 'version': manifest['version'], 'source_root': str(source),
                  'binary_sha256': manifest['binary_sha256'], 'manifest_sha256': manifest_hash,
                  'source_files_verified': len(manifest['sources']), 'runs': runs,
                  'existing_preview_services_accessed': False, 'baseline_unchanged': True,
                  'browser_access': False, 'hardware_access': False,
                  'physical_power_long_duration_verified': False, 'physical_power_off_verified': False,
                  'shutdown_refresh_failure_requires_hardware_side_fault_test': True}
        (output/'report.json').write_text(json.dumps(report, ensure_ascii=False, indent=2)+'\n')
        print(f'PASS: {sum(len(run["checks"]) for run in runs.values())} focused native/HTTP power scenarios; {output/"report.json"}')
    except BaseException as error:
        (output/'failure.json').write_text(json.dumps({'result': 'FAIL', 'error': repr(error), 'completed_runs': runs}, ensure_ascii=False, indent=2)+'\n')
        raise


if __name__ == '__main__':
    main()

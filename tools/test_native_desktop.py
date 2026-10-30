#!/usr/bin/env python3
"""中文：临时HTTP端口与私有TF验证真实原生桌面画布服务；不接触设备或既有预览。
English: Verify the real native desktop canvas through a temporary HTTP port/private TF, without devices or live previews.
"""
import argparse
import hashlib
import http.client
import importlib.util
import json
import os
from pathlib import Path
import re
import shutil
import struct
import tempfile
import threading
import urllib.error
import urllib.parse
import urllib.request
import zlib
from http.server import ThreadingHTTPServer

ROOT = Path(__file__).resolve().parents[1]
SITE = ROOT.parents[1] / 'outputs/firmware-preview/server.py'


def bitmap(width=684, height=1216):
    stride = ((width + 31) // 32) * 4
    pixels = bytearray(stride * height)
    for y in range(height // 4, 3 * height // 4):
        for x in range(width // 4, width // 2):
            pixels[(height - 1 - y) * stride + x // 8] |= 1 << (7 - x % 8)
    header = struct.pack('<2sIHHI', b'BM', 62 + len(pixels), 0, 0, 62)
    header += struct.pack('<IiiHHIIiiII', 40, width, height, 1, 1, 0, len(pixels), 0, 0, 2, 0)
    return header + bytes((255, 255, 255, 0, 0, 0, 0, 0)) + pixels


def gray_bitmap(width=684, height=1216, top_down=False, reversed_palette=False):
    stride = ((width * 4 + 31) // 32) * 4
    pixels = bytearray(stride * height)
    for y in range(height):
        row = y if top_down else height - 1 - y
        for x in range(width):
            gray = (x * 16 // width + y * 7 // height) & 15
            index = 15 - gray if reversed_palette else gray
            pixels[row * stride + x // 2] |= index << (0 if x % 2 else 4)
        for padding in range(width // 2, stride):
            pixels[row * stride + padding] = 0xa5
    header = struct.pack('<2sIHHI', b'BM', 118 + len(pixels), 0, 0, 118)
    header += struct.pack('<IiiHHIIiiII', 40, width, -height if top_down else height, 1, 4, 0, len(pixels), 0, 0, 16, 0)
    palette = bytes(channel for index in range(16) for channel in ([(15 - index if reversed_palette else index) * 17] * 3 + [0]))
    return header + palette + pixels


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    original = args.build.resolve()
    manifest = json.loads((original / 'native-preview-build.json').read_text())
    binary_hash = hashlib.sha256((original / 'native-preview').read_bytes()).hexdigest()
    assert binary_hash == manifest['binary_sha256']
    assert 'main/system/pico_desktop.c' in manifest['sources']
    args.output.mkdir(parents=True, exist_ok=False)
    spec = importlib.util.spec_from_file_location('pico_native_desktop_http', SITE)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    module.REPO = Path(manifest['source_root'])
    module.BASELINE = {'version': manifest['version'], 'candidate': True, 'hardware_access': False}
    previous_offline = os.environ.get('RPICO_HOME_LINK_OFFLINE')
    os.environ['RPICO_HOME_LINK_OFFLINE'] = '1'
    checks = []
    report = {'result': 'RUNNING', 'binary_sha256': binary_hash, 'hardware_access': False,
              'live_preview_access': False, 'host_network_attempted': False, 'checks': checks}
    try:
        with tempfile.TemporaryDirectory(prefix='pico-native-desktop-http-') as temporary:
            module.BUILD = Path(temporary)
            shutil.copy2(original / 'native-preview', module.BUILD / 'native-preview')
            for directory in ('game-packages', 'tool-packages'):
                shutil.copytree(original / directory, module.BUILD / directory)
            server = ThreadingHTTPServer(('127.0.0.1', 0), module.Handler)
            module.MANAGEMENT_PORT = server.server_port
            thread = threading.Thread(target=server.serve_forever, daemon=True)
            thread.start()
            base = 'http://127.0.0.1:' + str(server.server_port)
            opener = urllib.request.build_opener(urllib.request.ProxyHandler({}))
            sid = None
            key = '0' * 32

            def request(path, data=None, method=None, headers=None):
                values = {'X-Pico-Setup-Key': key}
                if sid:
                    values['X-Pico-Preview-Session'] = sid
                values.update(headers or {})
                req = urllib.request.Request(base + path, data=data, method=method, headers=values)
                try:
                    with opener.open(req, timeout=45) as response:
                        body = response.read()
                        return response.status, json.loads(body) if 'json' in response.headers.get('Content-Type', '') else body
                except urllib.error.HTTPError as error:
                    return error.code, json.load(error)

            def rejected_length(path, size):
                # 无效大体积请求在读体前拒绝；只发头，避免客户端继续写已关闭socket。
                # Rejected lengths fail before body reception; send headers only rather than writing into a closed socket.
                connection = http.client.HTTPConnection('127.0.0.1', server.server_port, timeout=45)
                connection.putrequest('POST', path)
                for name, value in {'X-Pico-Setup-Key': key, 'X-Pico-Preview-Session': sid, 'Content-Type': 'image/bmp', 'Content-Length': str(size)}.items():
                    connection.putheader(name, value)
                connection.endheaders()
                response = connection.getresponse()
                code = response.status
                response.read()
                connection.close()
                return code

            def post(path, body):
                code, result = request(path, json.dumps(body).encode(), headers={'Content-Type': 'application/json'})
                assert code == 200, (code, result)
                return result

            def command(**data):
                return post('/api/command', {'session': sid, **data})

            def managed_status():
                code, result = request('/api/manage/pico/status')
                assert code == 200, (code, result)
                return result

            def push(data, display=True, **headers):
                endpoint = '/api/manage/pico/canvas/show' if display else '/api/manage/pico/canvas'
                return request(endpoint, data, headers={'Content-Type': 'image/bmp', **headers})

            def pair():
                nonlocal key
                result = post('/api/manage-open', {'session': sid})['management']
                assert result['active']
                key = urllib.parse.parse_qs(urllib.parse.urlsplit(result['url']).fragment)['key'][0]
                assert len(key) == 32

            def capture(name, state, source_width, source_height, gray=False):
                code, pixels = request(state['image_url'])
                assert code == 200 and pixels.startswith(b'\x89PNG\r\n\x1a\n')
                (args.output / (name + '.png')).write_bytes(pixels)
                # 中文：同时读取真实原生画布；避开全局状态栏和角落提示，逐像素核对黑白内容与contain布局。
                # English: Verify actual native pixels and contain layout outside global status/corner overlays.
                pgm = session.frame.read_bytes()
                header = re.match(rb'P5\s+(\d+)\s+(\d+)\s+255\s', pgm)
                assert header, 'native framebuffer is not a PGM'
                sw, sh = map(int, header.groups())
                frame = pgm[header.end():]
                assert len(frame) == sw * sh
                width = sw
                height = width * source_height // source_width
                if height > sh:
                    height = sh
                    width = height * source_width // source_height
                left, top = (sw - width) // 2, (sh - height) // 2
                dark = light = 0
                gray_levels = set()
                for y in range(80, sh - (180 if name.startswith("custom-image-") else 80)):
                    for x in range(80, sw - 80):
                        expected = 255
                        if left <= x < left + width and top <= y < top + height:
                            sx = (x - left) * source_width // width
                            sy = (y - top) * source_height // height
                            if gray:
                                expected = ((sx * 16 // source_width + sy * 7 // source_height) & 15) * 17
                                gray_levels.add(expected)
                            elif source_width // 4 <= sx < source_width // 2 and source_height // 4 <= sy < 3 * source_height // 4:
                                expected = 0
                        actual = frame[y * sw + x]
                        assert actual == expected, (name, x, y, actual, expected)
                        dark += actual == 0
                        light += actual == 255
                if gray:
                    assert gray_levels == set(range(0, 256, 17)), (name, gray_levels)
                else:
                    assert dark > 1000 and light > 1000, (name, dark, light)

            try:
                state = post('/api/session', {})
                sid = state['session']
                session = module.SESSIONS[sid]
                assert request('/api/manage/pico/status')[0] == 401
                assert push(bitmap())[0] == 401
                assert not list((session.root / 'tf/.desktop-inbox').glob('*'))
                checks.append('unpaired status and binary upload rejected by the real native manager')
                pair()
                initial = managed_status()
                assert initial['version'] == manifest['version'] and initial['canvas']['state'] == 'idle'
                assert initial['canvas']['max_bytes'] == 420000 and initial['canvas']['formats'] == ['image/bmp']
                assert initial['canvas']['gray_levels'] == 16 and initial['canvas']['bit_depths'] == [1, 4]
                assert initial['image_lock']['gray_levels'] == 16 and initial['image_lock']['bit_depths'] == [1, 4] and initial['image_lock']['max_bytes'] == 420000
                assert request('/api/manage/pico/status', headers={'X-Pico-Setup-Key': '0' * 32})[0] == 401
                checks.append('paired status protocol/version/platform capability and wrong-key rejection')
                clock = state['now_ms']

                def touch(phase, x, y):
                    nonlocal clock
                    clock = max(clock + 80, command(action='status')['now_ms'] + 80)
                    return command(action='touch', phase=phase, x=x, y=y, ms=clock)

                def tap(x, y):
                    touch('down', x, y)
                    return touch('up', x, y)

                def tick():
                    nonlocal clock
                    clock = max(clock + 100, command(action='status')['now_ms'] + 100)
                    return command(action='tick', ms=clock)

                command(action='route', route=27)
                tap(80, 520)
                tap(130, 424)
                tap(300, 590)
                command(action='route', route=0)
                # Settle the shelf's independent initial cover loading before measuring push-only redraws.
                before_background = command(action='status')
                stable = 0
                for _ in range(80):
                    settled = tick()
                    stable = stable + 1 if settled['revision'] == before_background['revision'] else 0
                    before_background = settled
                    if stable >= 5:
                        break
                assert stable >= 5, 'Shelf never settled before background push'

                background = gray_bitmap()
                assert push(background, display=False)[0] == 202
                state = tick()
                assert managed_status()['canvas']['state'] == 'saved'
                assert state['route'] == before_background['route'] and state['revision'] == before_background['revision'], (before_background,state)
                assert (session.root / 'tf/readpico/desktop/canvas.bmp').read_bytes() == background
                checks.append('background canvas persists without navigating or refreshing the current page; explicit /canvas/show retains manual display intent')
                for width, height, top, reverse in ((684, 1216, False, False), (1216, 684, True, True)):
                    gray = gray_bitmap(width, height, top, reverse)
                    assert len(gray) == (418422 if width == 684 else 415990)
                    assert push(gray)[0] == 202
                    assert managed_status()['canvas']['state'] == 'pending'
                    state = tick()
                    assert managed_status()['canvas']['state'] == 'displayed'
                    capture('gray16-' + str(width) + '-portrait', state, width, height, gray=True)
                    state = command(action='rotate', landscape=1)
                    capture('gray16-' + str(width) + '-landscape', state, width, height, gray=True)
                    command(action='rotate', landscape=0)
                    gray_target = session.root / 'tf/readpico/desktop/canvas.bmp'
                    assert gray_target.read_bytes() == gray
                    invalid_gray = bytearray(gray)
                    invalid_gray[57] = 255
                    assert push(invalid_gray)[0] == 422 and gray_target.read_bytes() == gray
                    assert rejected_length('/api/manage/pico/lock-image', 420001) == 413
                checks.append('native 16-gray protocol preserves all levels through padded portrait/reordered landscape palettes and real render/present boundaries; custom-lock upload shares the 420000-byte transport bound')
                portrait = bitmap()
                code, accepted = push(portrait)
                assert code == 202 and accepted['ok'] and accepted['accepted'] and 'displayed' not in accepted
                assert accepted['channel'] == 'canvas' and accepted['revision'] == managed_status()['canvas']['revision']
                assert managed_status()['canvas']['state'] == 'pending'
                target = session.root / 'tf/readpico/desktop/canvas.bmp'
                assert target.read_bytes() == portrait
                assert not list((session.root / 'tf/.desktop-inbox').glob('*'))
                state = tick()
                assert state['route'] == 29 and managed_status()['canvas']['state'] == 'displayed', state
                capture('portrait-displayed', state, 684, 1216)
                checks.append('HTTP acceptance stays pending until the real UI/present boundary displays the persisted bitmap')
                previous = target.read_bytes()
                invalid = bytearray(portrait)
                struct.pack_into('<I', invalid, 18, 0xffffffff)
                assert push(invalid)[0] == 422 and target.read_bytes() == previous
                assert rejected_length('/api/manage/pico/canvas', 420001) == 413
                assert push(portrait, **{'Content-Type': 'text/plain'})[0] == 415
                assert request('/api/manage/pico/canvas?ignored=1', portrait, headers={'Content-Type': 'image/bmp'})[0] == 400
                assert target.read_bytes() == previous
                checks.append('actual BMP overflow/malformed, upload limit, MIME and query rejection preserve the previous file')
                model_type = 'application/vnd.readpico.board-model+json'
                model_path = '/api/manage/pico/canvas/model'
                model = json.dumps({'schema': 1, 'board_id': 'http-test', 'generated_at': 1700000000,
                                    'columns': 1, 'dark': False, 'modules': [
                                        {'kind': 'text', 'id': 'note', 'title': '设备原生画板', 'body': 'HTTP只发送内容数据。'}]},
                                   ensure_ascii=False, separators=(',', ':')).encode()
                assert initial['canvas']['model_protocol'] == 1 and initial['canvas']['model_max_bytes'] == 65536
                assert request(model_path, model, headers={'Content-Type': model_type, 'X-Pico-Setup-Key': '0' * 32})[0] == 401
                assert request(model_path, model, headers={'Content-Type': 'application/json'})[0] == 415
                assert request(model_path, b' ' * 65537, headers={'Content-Type': model_type})[0] == 413
                assert request(model_path, b'{}', headers={'Content-Type': model_type})[0] == 422
                command(action='route', route=1)
                reading = session.frame.read_bytes()
                assert request(model_path, model, headers={'Content-Type': model_type})[0] == 202
                state = tick()
                assert state['route'] == 1 and session.frame.read_bytes() == reading
                assert managed_status()['canvas']['state'] == 'saved' and target.read_bytes() == model
                assert request(model_path + '/show', model, headers={'Content-Type': model_type})[0] == 202
                state = tick()
                assert state['route'] == 29 and managed_status()['canvas']['state'] == 'displayed'
                assert not list((session.root / 'tf/.desktop-inbox').glob('*'))
                assert push(portrait)[0] == 202
                tick()
                assert target.read_bytes() == portrait
                checks.append('HTTP semantic model routes enforce authorization/MIME/64KiB/schema, save in background, and display only after the native UI boundary')
                code, accepted = request('/api/manage/pico/config', json.dumps({'auto_sleep_minutes': 1, 'double_tap_wake': True}).encode(), method='PUT', headers={'Content-Type': 'application/json'})
                assert code == 202 and accepted.get('accepted')
                pending = managed_status()['config']
                assert pending['state'] == 1 and pending['revision'] == initial['config']['revision']
                tick()
                saved_config = managed_status()['config']
                assert saved_config['state'] == 2 and saved_config['auto_sleep_minutes'] == 1 and saved_config['double_tap_wake']
                assert saved_config['revision'] == pending['revision'] + 1
                checks.append('power settings are queued by HTTP and saved once by the shared UI loop')
                lock_path = '/api/manage/pico/lock-image'
                assert request(lock_path)[0] == 404
                assert initial['image_lock']['supported'] and not initial['image_lock']['available']
                lock_target = session.root / 'tf/readpico/lockscreen/custom.bmp'
                before_state = command(action='status')
                code, accepted = request(lock_path, portrait, headers={'Content-Type': 'image/bmp'})
                assert code == 202 and accepted['accepted'] and accepted['revision'] > 0
                assert managed_status()['image_lock']['state'] == 'pending'
                assert lock_target.read_bytes() == portrait and target.read_bytes() == portrait
                assert request(lock_path, portrait, headers={'Content-Type': 'image/bmp'})[0] == 409
                state = tick()
                lock = managed_status()['image_lock']
                assert lock['state'] == 'saved' and lock['revision'] == accepted['revision'] and lock['available']
                assert state['route'] == before_state['route'] and state['revision'] == before_state['revision']
                assert request(lock_path) == (200, portrait)
                assert request(lock_path, headers={'X-Pico-Setup-Key': '0' * 32})[0] == 401
                assert request(lock_path + '?ignored=1')[0] == 400
                invalid_lock = bytearray(portrait)
                struct.pack_into('<I', invalid_lock, 28, 8)
                assert request(lock_path, invalid_lock, headers={'Content-Type': 'image/bmp'})[0] == 422
                assert lock_target.read_bytes() == portrait and target.read_bytes() == portrait
                lock_config = {'auto_sleep_minutes': 1, 'double_tap_wake': True, 'lock_style': 4}
                assert request('/api/manage/pico/config', json.dumps(lock_config).encode(), method='PUT', headers={'Content-Type': 'application/json'})[0] == 202
                tick()
                assert managed_status()['config']['lock_style'] == 4
                legacy = {'auto_sleep_minutes': 1, 'double_tap_wake': True}
                assert request('/api/manage/pico/config', json.dumps(legacy).encode(), method='PUT', headers={'Content-Type': 'application/json'})[0] == 202
                tick()
                assert managed_status()['config']['lock_style'] == 4
                checks.append('separate custom-lock upload stays pending until adoption, retains page/pixels, reads through authenticated BMP GET, and old config preserves selected style')
                landscape = bitmap(1216, 684)
                assert push(landscape)[0] == 202
                state = tick()
                capture('landscape-contained-in-portrait', state, 1216, 684)
                state = command(action='rotate', landscape=1)
                assert (state['width'], state['height']) == (1216, 684)
                capture('landscape-displayed', state, 1216, 684)
                command(action='rotate', landscape=0)
                checks.append('both bitmap orientations render through the shared canvas in portrait/landscape screens')
                if os.geteuid() != 0:
                    old_file = target.read_bytes()
                    target.parent.chmod(0o500)
                    try:
                        assert push(portrait)[0] == 507 and target.read_bytes() == old_file
                    finally:
                        target.parent.chmod(0o700)
                    checks.append('real filesystem save failure returns 507 and preserves the committed previous file')
                session.command('scenario tf-absent')
                old_file = target.read_bytes()
                assert push(portrait)[0] == 409 and target.read_bytes() == old_file
                session.command('scenario tf-ready')
                checks.append('simulated missing-card gate refuses new writes without deleting the saved canvas')
                old_key = key
                post('/api/restart', {'session': sid})
                session = module.SESSIONS[sid]
                assert request('/api/manage/pico/status')[0] == 401
                assert push(portrait)[0] == 401
                pair()
                assert key != old_key and request('/api/manage/pico/status', headers={'X-Pico-Setup-Key': old_key})[0] == 401
                assert managed_status()['canvas']['state'] == 'idle'
                command(action='route', route=0)
                state = tap(265, 48)
                assert state['route'] == 29 and managed_status()['canvas']['state'] == 'displayed'
                assert target.read_bytes() == old_file
                capture('reopened-from-tf-home-entry', state, 1216, 684)
                config = managed_status()['config']
                assert config['auto_sleep_minutes'] == 1 and config['double_tap_wake'] and config['lock_style'] == 4
                assert managed_status()['image_lock']['available'] and request(lock_path) == (200, portrait)
                state = command(action='powerkey')
                assert state['power_state'] == 1
                capture('custom-image-locked', state, 684, 1216)
                state = command(action='powerkey')
                assert state['power_state'] == 0
                pair()
                command(action='rotate', landscape=1)
                state = command(action='powerkey')
                assert state['power_state'] == 1
                capture('custom-image-locked-landscape', state, 684, 1216)
                state = command(action='powerkey')
                assert state['power_state'] == 0
                checks.append('custom image/style persist after restart and offline power-key lock/wake uses aspect-fit image in both orientations')
                pair()
                for width, height, top, reverse in ((684, 1216, False, False), (1216, 684, True, True)):
                    gray = gray_bitmap(width, height, top, reverse)
                    before_state = command(action='status')
                    code, accepted = request(lock_path, gray, headers={'Content-Type': 'image/bmp'})
                    assert code == 202 and accepted['accepted']
                    assert managed_status()['image_lock']['state'] == 'pending'
                    assert request(lock_path, gray, headers={'Content-Type': 'image/bmp'})[0] == 409
                    state = tick()
                    saved = managed_status()['image_lock']
                    assert saved['state'] == 'saved' and saved['revision'] == accepted['revision']
                    assert state['route'] == before_state['route'] and state['revision'] == before_state['revision']
                    assert lock_target.read_bytes() == gray and request(lock_path) == (200, gray)
                    invalid = bytearray(gray)
                    invalid[57] = 255
                    assert request(lock_path, invalid, headers={'Content-Type': 'image/bmp'})[0] == 422
                    assert lock_target.read_bytes() == gray
                    for landscape in (0, 1):
                        command(action='rotate', landscape=landscape)
                        state = command(action='powerkey')
                        assert state['power_state'] == 1
                        capture('custom-image-gray16-' + str(width) + ('-landscape' if landscape else '-portrait'), state, width, height, gray=True)
                        state = command(action='powerkey')
                        assert state['power_state'] == 0
                        pair()
                    post('/api/restart', {'session': sid})
                    session = module.SESSIONS[sid]
                    pair()
                    assert managed_status()['image_lock']['available'] and request(lock_path) == (200, gray)
                    assert managed_status()['config']['lock_style'] == 4
                checks.append('4-bit custom locks preserve all 16 grays in portrait/landscape through native UI lock render, authenticated original-BMP readback, pending/revision adoption and reboot; invalid palette retains prior image')
                checks.append('restart revokes pairing, restores power settings and reopens the TF canvas from its saved homepage entry')
                # 同一网络事务保存两版，旋转只从本地完整快照选择，失败不能替换任一版。
                # One network transaction persists both layouts; local rotation selects a member, and failure replaces neither.
                portrait_gray, landscape_gray = gray_bitmap(), gray_bitmap(1216, 684, True, True)
                paired_canvas = b'PICOPAIR' + struct.pack('<IIIIII', 1, 32, len(portrait_gray), zlib.crc32(portrait_gray), len(landscape_gray), zlib.crc32(landscape_gray)) + portrait_gray + landscape_gray
                def push_both(data, display=False, content_type='application/vnd.readpico.canvas-pair'):
                    return request('/api/manage/pico/canvas/pair/show' if display else '/api/manage/pico/canvas/pair', data, headers={'Content-Type': content_type})
                assert managed_status()['canvas']['dual_orientation'] and managed_status()['canvas']['pair_max_bytes'] == 840032
                clock = command(action='status')['now_ms']
                command(action='route', route=0)
                tick(); tick()
                before_pair = command(action='status')
                assert push_both(paired_canvas)[0] == 202
                state = tick()
                pair_revision = managed_status()['canvas']['revision']
                assert managed_status()['canvas']['state'] == 'saved' and state['route'] == before_pair['route'] and state['revision'] == before_pair['revision']
                assert target.read_bytes() == paired_canvas
                assert push_both(paired_canvas, display=True)[0] == 202
                state = tick()
                pair_revision = managed_status()['canvas']['revision']
                assert state['route'] == 29 and managed_status()['canvas']['state'] == 'displayed'
                for landscape in (0, 1, 0, 1):
                    state = command(action='rotate', landscape=landscape)
                    cap = managed_status()['canvas']
                    assert cap['revision'] == pair_revision and cap['orientation'] == ('landscape' if landscape else 'portrait')
                    capture('paired-local-' + ('landscape' if landscape else 'portrait'), state, 1216 if landscape else 684, 684 if landscape else 1216, gray=True)
                    assert target.read_bytes() == paired_canvas
                malformed = bytearray(paired_canvas); malformed[-1] ^= 1
                assert push_both(malformed)[0] == 422
                assert push_both(b'x', content_type='image/bmp')[0] == 415
                assert rejected_length('/api/manage/pico/canvas/pair', 840033) == 413
                assert managed_status()['canvas']['revision'] == pair_revision and target.read_bytes() == paired_canvas
                post('/api/restart', {'session': sid}); session = module.SESSIONS[sid]; pair()
                clock = command(action='status')['now_ms']
                command(action='rotate', landscape=0)
                command(action='route', route=0); state = tap(265, 48)
                assert state['route'] == 29 and target.read_bytes() == paired_canvas
                for landscape in (0, 1):
                    state = command(action='rotate', landscape=landscape)
                    capture('paired-reboot-' + ('landscape' if landscape else 'portrait'), state, 1216 if landscape else 684, 684 if landscape else 1216, gray=True)
                checks.append('atomic PICOPAIR upload saves portrait and landscape together; native sensor rotation and reboot select the matching local 16-gray layout without reupload, while malformed second member/MIME/oversize retain the entire committed pair')
                report['result'] = 'PASS'
            finally:
                if sid:
                    for suffix in ('runtime.log',):
                        path = module.BUILD / 'sessions' / sid / suffix
                        if path.is_file():
                            shutil.copy2(path, args.output / suffix)
                    post('/api/close', {'session': sid})
                server.shutdown()
                server.server_close()
                thread.join(timeout=5)
                for session in list(module.SESSIONS.values()):
                    session.close()
    finally:
        if previous_offline is None:
            os.environ.pop('RPICO_HOME_LINK_OFFLINE', None)
        else:
            os.environ['RPICO_HOME_LINK_OFFLINE'] = previous_offline
        (args.output / 'report.json').write_text(json.dumps(report, ensure_ascii=False, indent=2) + '\n')
    print(json.dumps(report, ensure_ascii=False, indent=2))


if __name__ == '__main__':
    main()

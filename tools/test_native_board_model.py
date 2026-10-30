#!/usr/bin/env python3
"""Exercise semantic board data through the real native controller and framebuffer.

Private TF data only; no physical device, live preview, or external network.
"""
import argparse
import copy
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import re
import tempfile
import urllib.parse

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--fixture', type=Path, help='Optional Swift-produced model containing Cloudflare geometry')
    args = parser.parse_args()
    build = args.build.resolve()
    args.output.mkdir(parents=True, exist_ok=True)
    manifest = json.loads((build / 'native-preview-build.json').read_text())
    digest = hashlib.sha256((build / 'native-preview').read_bytes()).hexdigest()
    assert digest == manifest['binary_sha256']
    spec = importlib.util.spec_from_file_location('native_board_model_server', ROOT.parents[1] / 'outputs/firmware-preview/server.py')
    server = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(server)
    server.REPO = ROOT
    server.MANAGEMENT_PORT = 1
    os.environ['RPICO_HOME_LINK_OFFLINE'] = '1'
    model = {
        'schema': 1, 'board_id': 'native-board-regression', 'generated_at': 1793116800,
        'columns': 2, 'dark': False, 'text_scale': 3,
        'modules': [
            {'kind': 'weather', 'id': 'weather', 'title': '天气', 'subtitle': '上海',
             'metrics': [{'label': '温度', 'value': '23', 'unit': '°C'}, {'label': '湿度', 'value': '56', 'unit': '%'}],
             'rows': [{'label': '风速', 'value': '3 m/s'}], 'series': [20, 21, 23, 22, 25, 24]},
            {'kind': 'reading_excerpt', 'id': 'excerpt', 'title': '阅读摘录',
             'body': '真正的发现之旅，不在于寻找新的风景，而在于拥有新的眼光。', 'footer': '书中划线 · 离线保存'},
            {'kind': 'text', 'id': 'note', 'title': '灵犀画板', 'span': 2,
             'body': '文字、数值与排布均由设备绘制。横竖屏使用同一份数据。'}
        ]
    }
    checks = []
    with tempfile.TemporaryDirectory(prefix='board-model-', dir=args.output) as temporary:
        server.BUILD = Path(temporary)
        for name in ('native-preview', 'game-packages', 'tool-packages'):
            (server.BUILD / name).symlink_to(build / name)
        session = server.Session()
        key = ''
        try:
            def command(line):
                result = session.command(line)
                assert result.get('ok', True), (line.split()[0], result)
                return result

            def tick():
                return command('tick ' + str(session.state['now_ms'] + 200))

            def pair():
                nonlocal key
                opened = command('manage-open')['management']
                assert opened['active']
                key = urllib.parse.parse_qs(urllib.parse.urlsplit(opened['url']).fragment)['key'][0]

            def rpc(method, path, body=None):
                request = {'key': key, 'method': method, 'path': path, 'query': '', 'body': json.dumps(body or {})}
                return command('manage-web ' + json.dumps(request).encode().hex())['management_response']

            def status():
                response = rpc('GET', '/api/manage/pico/status')
                assert response['status'] == 200
                return response['body']

            def push(value, show=False, app=False):
                name = ('cloudflare' if app else 'canvas') + '-model-' + ('show-' if show else '') + 'test.json'
                target = session.root / 'tf/.desktop-inbox' / name
                target.parent.mkdir(parents=True, exist_ok=True)
                target.write_text(json.dumps(value, ensure_ascii=False, separators=(',', ':')))
                return command('desktop-canvas ' + key + ' ' + name)['management_response']

            def capture(name):
                raw = session.frame.read_bytes()
                header = re.match(rb'P5\s+(\d+)\s+(\d+)\s+255\s', raw)
                assert header
                w, h = map(int, header.groups())
                pixels = raw[header.end():]
                assert len(pixels) == w * h and set(pixels) <= set(range(0, 256, 17))
                assert pixels.count(0) > 1000 and pixels.count(255) > 1000
                (args.output / (name + '.png')).write_bytes(session.cached_frame)
                return w, h

            command('scenario default')
            pair()
            assert status()['canvas']['model_protocol'] == 1
            assert status()['canvas']['model_max_bytes'] == 65536
            assert rpc('PUT', '/api/manage/pico/config', {'auto_sleep_minutes': 0, 'double_tap_wake': True, 'lock_style': 5})['status'] == 202
            tick()
            command('route 1')
            before = session.frame.read_bytes()
            assert push(model)['status'] == 202
            received = tick()
            assert received['route'] == 1 and session.frame.read_bytes() == before
            assert status()['canvas']['state'] == 'saved'
            assert received['services']['tfMounted'] and not received['services']['usbActive']
            checks.append('background semantic data preserves reading framebuffer and TF ownership')

            assert push(model, show=True)['status'] == 202
            shown = tick()
            canvas_route = shown['route']
            assert canvas_route != 1 and status()['canvas']['state'] == 'displayed'
            w, h = capture('board-portrait')
            assert w < h
            receipt = status()['canvas']['revision']
            command('rotate 1')
            w, h = capture('board-landscape')
            assert w > h and status()['canvas']['revision'] == receipt
            checks.append('same semantic snapshot renders portrait and landscape in hardware gray16 without a new transfer')

            if args.fixture:
                cloud = json.loads(args.fixture.read_text())
                cloud['modules'] = [next(m for m in cloud['modules'] if m['kind'] == 'cloudflare')]
                cloud['columns'] = 1
                cloud['text_scale'] = 3
                cloud.pop('assets', None)
                command('rotate 0')
                assert push(cloud, show=True, app=True)['status'] == 202
                tick()
                capture('cloudflare-map-portrait')
                command('rotate 1')
                capture('cloudflare-map-landscape')
                assert status()['canvas']['revision'] == receipt
                assert status()['canvas']['app_receipts']['cloudflare']['state'] == 'displayed'
                cloud['modules'][0]['presentation'] = 'ranking'
                cloud.pop('world_map', None)
                assert push(cloud, show=True, app=True)['status'] == 202
                tick()
                capture('cloudflare-ranking-landscape')
                command('route ' + str(canvas_route))
                tick()
                checks.append('Swift Cloudflare geometry renders map/ranking in an independent channel without replacing the board snapshot')

            broken = copy.deepcopy(model)
            broken['modules'][0]['kind'] = 'unknown'
            before = session.frame.read_bytes()
            assert push(broken, show=True)['status'] == 422
            tick()
            assert session.frame.read_bytes() == before and status()['canvas']['revision'] == receipt
            checks.append('invalid model retains prior frame and transfer revision')

            command('route 1')
            command('scenario power-charging')
            assert command('powerkey')['power_state'] == 1
            capture('board-lock')
            changed = copy.deepcopy(model)
            changed['modules'][0]['metrics'][0]['value'] = '28'
            assert push(changed)['status'] == 202
            locked = tick()
            assert locked['power_state'] == 1 and locked['route'] == 1
            assert status()['canvas']['state'] == 'displayed'
            assert command('powerkey')['power_state'] == 0
            checks.append('charging board lock adopts data and power key returns to reading')

            command('scenario default')
            pair()
            command('app-open drawing-notes')
            tick()
            drawing = session.frame.read_bytes()
            active = session.state['active_app']
            assert push(model)['status'] == 202
            result = tick()
            assert result['active_app'] == active and session.frame.read_bytes() == drawing
            assert status()['canvas']['state'] == 'saved'
            checks.append('background data does not replace drawing application or its frame')

            sid = session.id
            session.close()
            session = server.Session(sid)
            command('scenario offline')
            command('route ' + str(canvas_route))
            tick()
            capture('board-offline-reopened')
            stored = json.loads((session.root / 'tf/readpico/desktop/canvas.bmp').read_text())
            assert stored == model
            checks.append('saved semantic data reopens offline after native process restart')
        finally:
            session.close()
    report = {'result': 'PASS', 'binary_sha256': digest, 'hardware_access': False,
              'checks': checks, 'boundary': 'Real native firmware renderer and controllers; physical panel latency is untested.'}
    (args.output / 'report.json').write_text(json.dumps(report, ensure_ascii=False, indent=2) + '\n')
    print(json.dumps(report, ensure_ascii=False))


if __name__ == '__main__':
    main()

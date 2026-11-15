#!/usr/bin/env python3
"""实际TF引擎/游戏、保存与卸载回归；仅私有TF。/ Real TF engine/game lifecycle on private TFs."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import tempfile
import time
import urllib.parse

from test_native_preview_shelf_ble import ROOT, Runtime
from test_native_preview_drawing import stroke, saved as drawing_saved

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--binary', type=Path, required=True)
p.add_argument('--output', type=Path, required=True)
p.add_argument('--legacy-drawing', type=Path)
a = p.parse_args()
out = a.output.resolve()
out.mkdir(parents=True, exist_ok=True)
session = Path(tempfile.mkdtemp(prefix='tf-', dir=out))
r = Runtime(a.binary.resolve(), ROOT / 'sdcard/fonts/MiSansVF.ttf', session, out)
checks = []

def raw(command):
    r.process.stdin.write((command + '\n').encode())
    r.process.stdin.flush()
    while b'\n' not in r.buffer:
        assert r.selector.select(45), command
        part = os.read(r.process.stdout.fileno(), 65536)
        assert part, command
        r.buffer += part
    line, r.buffer = r.buffer.split(b'\n', 1)
    result = json.loads(line)
    r.transcript.append({'command': command, 'result': result})
    return result

def capture(name):
    s = r.command('status')
    r.capture(name, s)
    return s

def open_game(ident):
    s = r.command('route 13')
    i = s['games_order'].index(ident)
    if i >= 6:
        r.tap(305, 610)
    i %= 6
    s = r.tap(16 + i % 2 * 169 + 79, 143 + i // 2 * 142 + 60)
    assert s['route'] == 23 and not s['games_error'], s
    return s

try:
    s = r.start()
    assert s['games_count'] == 8
    for ident in ('tetris', 'pacman'):
        open_game(ident)
        capture(ident + '-paused')
        r.tap(312, 568)
        for _ in range(5):
            time.sleep(.5)
            r.tick(500)
        capture(ident + '-playing')
        r.command('powerkey')
        assert r.settle(3)['power_state'] == 1
        r.command('powerkey')
        assert r.settle(3)['power_state'] == 0
        capture(ident + '-wake-paused')
        r.command('key 1 click')
        data = session / 'tf/apps/data' / ident / 'state1.dat'
        assert data.is_file() and len(data.read_bytes()) == 348
        saved = data.read_bytes()
        r.command('game-remove ' + ident)
        assert data.read_bytes() == saved
        package = a.binary.resolve().parent / 'game-packages' / (ident + '.pga')
        staged = session / 'tf/apps/.staging/reinstall.tmp'
        staged.parent.mkdir(parents=True, exist_ok=True)
        staged.write_bytes(package.read_bytes())
        r.command('game-install 0 reinstall.tmp')
        open_game(ident)
        r.command('key 1 click')
        assert data.read_bytes() == saved
        checks.append(ident + ': real package, tick, lock/wake, saved state, remove/reinstall')

    opened = raw('manage-open')['management']
    key = urllib.parse.parse_qs(urllib.parse.urlsplit(opened['url']).fragment)['key'][0]
    def rpc(method, path, body=None):
        req = {'key': key, 'method': method, 'path': path, 'query': '', 'body': json.dumps(body or {})}
        return raw('manage-web ' + json.dumps(req).encode().hex())['management_response']
    model = {'schema': 1, 'board_id': 'tf-engine-test', 'generated_at': 1793116800,
             'columns': 2, 'dark': False, 'text_scale': 3,
             'modules': [{'kind': 'weather', 'id': 'weather', 'title': '天气',
                          'metrics': [{'label': '温度', 'value': '23', 'unit': '°C'}]},
                         {'kind': 'text', 'id': 'note', 'title': '灵犀画板',
                          'body': '应用从 TF 卡载入，快照与设置保留。'}]}
    # 直接走已认证管理接口，快照与显示都由生产代码处理。/ Use the authenticated production snapshot/display path.
    inbox = session / 'tf/.desktop-inbox/canvas-model-show-test.json'
    inbox.parent.mkdir(parents=True, exist_ok=True)
    inbox.write_text(json.dumps(model, ensure_ascii=False))
    result = raw('desktop-canvas ' + key + ' ' + inbox.name)['management_response']
    assert result['status'] == 202, result
    r.settle(3)
    r.command('app-open lingxi-board')
    r.settle(30)
    capture('lingxi-tf-engine')
    data_files = {str(f.relative_to(session)): hashlib.sha256(f.read_bytes()).hexdigest()
                  for f in (session / 'tf/readpico/desktop').rglob('*') if f.is_file()}
    assert data_files
    r.command('route 33')
    r.command('game-remove lingxi-board')
    r.command('app-open lingxi-board', ok=False)
    assert all(hashlib.sha256((session / name).read_bytes()).hexdigest() == digest
               for name, digest in data_files.items())
    r.command('route 29')
    capture('lingxi-package-removed')
    checks.append('TF renderer removed without deleting snapshots; missing package rejects launch')
    r.command('route 33')
    assert r.command('app-open website-traffic')['route'] == 32
    capture('website-traffic-tf')
    if a.legacy_drawing:
        r.command('rotate 0')
        r.command('route 33')
        staged = session / 'tf/apps/.staging/legacy.tmp'
        staged.write_bytes(a.legacy_drawing.read_bytes())
        r.command('game-install 1 legacy.tmp')
        time.sleep(2)
        r.settle(30, 100)
        r.command('app-open drawing-notes')
        s = r.tap(70, 155)
        initial = s['refresh_continuous_count']
        s = stroke(r)
        assert s['route'] == 28 and not s['games_error'], s
        assert s['refresh_continuous_count'] > initial, s
        capture('legacy-drawing-ink')
        time.sleep(1.95)
        r.settle(25, 100)
        assert drawing_saved(session)[1] >= 4
        checks.append('dual108 drawing package opens and receives pen strokes on appended SDK')
    print('PASS', checks)
finally:
    r.close()
    (out / 'transcript.json').write_text(json.dumps(r.transcript, ensure_ascii=False))
    (out / 'report.json').write_text(json.dumps({'checks': checks, 'physical_hardware': False,
        'scenes': r.scenes}, ensure_ascii=False, indent=2))

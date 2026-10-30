#!/usr/bin/env python3
"""中文：在独立临时HTTP服务验证游戏上传确实经过原生安装，不操作用户预览或设备。
English: Verify game uploads through an isolated temporary HTTP service and the actual native installer.
"""
from __future__ import annotations
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import shutil
import threading
import urllib.error
import urllib.request

ROOT = Path(__file__).resolve().parents[1]
SERVER = ROOT.parents[1] / 'outputs/firmware-preview/server.py'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', type=Path, default=ROOT / 'build/native-preview-candidate/native-preview')
    parser.add_argument('--packages', type=Path)
    parser.add_argument('--output', type=Path, default=ROOT / 'build/native-game-http-tests')
    args = parser.parse_args()
    output = args.output.resolve()
    if (output / 'server').exists():
        raise SystemExit('Use a fresh output directory')
    output.mkdir(parents=True, exist_ok=True)
    binary = args.binary.resolve()
    packages = args.packages.resolve() if args.packages else binary.parent / 'game-packages'
    binary_sha = hashlib.sha256(binary.read_bytes()).hexdigest()
    spec = importlib.util.spec_from_file_location('isolated_game_preview_server', SERVER)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    module.BUILD = output / 'server'
    module.BUILD.mkdir()
    shutil.copy2(binary, module.BUILD / 'native-preview')
    shutil.copytree(packages, module.BUILD / 'game-packages')
    server = module.ThreadingHTTPServer(('127.0.0.1', 0), module.Handler)
    worker = threading.Thread(target=server.serve_forever, daemon=True)
    worker.start()
    base = 'http://127.0.0.1:' + str(server.server_port)
    checks = []
    def request(path, data=None, headers=None, expected=200):
        if isinstance(data, dict):
            data = json.dumps(data).encode()
            headers = {'Content-Type': 'application/json', **(headers or {})}
        req = urllib.request.Request(base + path, data=data, headers=headers or {})
        try:
            response = urllib.request.urlopen(req, timeout=45)
        except urllib.error.HTTPError as error:
            response = error
        with response:
            body = response.read()
            assert response.status == expected, (path, response.status, body.decode(errors='replace'))
            return json.loads(body) if 'application/json' in response.headers.get('Content-Type', '') else body
    try:
        state = request('/api/session', {})
        session = state['session']
        assert state['games_count'] == 6
        root = module.BUILD / 'sessions' / session
        package = (packages / 'gomoku.pga').read_bytes()
        headers = {'Content-Type': 'application/octet-stream', 'X-File-Name': 'gomoku.pga'}
        downloaded = request('/game-packages/gomoku.pga')
        assert downloaded == package
        catalog = request('/game-packages/catalog.json')
        assert len(catalog['games']) == 6
        metadata = request('/api/bootstrap')['game_packages']
        compatible = all('esp32s3-xtensa' in item['targets'] for item in catalog['games'])
        assert metadata['device_compatible'] == compatible and metadata['notice']
        assert ('同时适用于阅读器' if compatible else '不能复制到阅读器') in metadata['notice']
        checks.append('curated-package-downloads-and-catalog')
        request('/game-packages/../server.py', expected=404)
        request('/api/games/import?session=' + session, b'not a package', {**headers, 'X-File-Name': 'book.txt'}, expected=400)
        request('/api/games/import?session=' + session, b'', headers, expected=400)
        request('/api/games/import?session=' + session + '&replace=2', package, headers, expected=400)
        checks.append('extension-empty-file-and-replacement-input-rejected')
        installed = root / 'tf/apps/games/gomoku.pga'
        original = installed.read_bytes()
        damaged = bytearray(package); damaged[-1] ^= 1
        result = request('/api/games/import?session=' + session + '&replace=1', bytes(damaged), headers, expected=422)
        assert not result['ok'] and result['state']['games_count'] == 6 and installed.read_bytes() == original
        assert not list((root / 'tf/apps/.staging').glob('upload-*.tmp'))
        checks.append('actual-native-crc-rejection-preserves-installed-package-and-cleans-staging')
        result = request('/api/games/import?session=' + session, package, headers, expected=409)
        assert result['conflict'] and installed.read_bytes() == original
        result = request('/api/games/import?session=' + session + '&replace=1', package, headers)
        assert result['installed'] and result['state']['games_count'] == 6
        checks.append('same-id-requires-explicit-replacement')
        request('/api/command', {'session': session, 'action': 'game_remove', 'id': '../outside'}, expected=400)
        request('/api/command', {'session': session, 'action': 'game-install', 'path': '/etc/passwd'}, expected=400)
        checks.append('web-command-cannot-select-native-staging-or-host-path')
        state = request('/api/command', {'session': session, 'action': 'game_remove', 'id': 'gomoku'})
        assert state['games_count'] == 5 and not installed.exists()
        state = request('/api/restart', {'session': session})
        assert state['games_count'] == 5 and not installed.exists()
        checks.append('remove-and-process-restart-do-not-reseed')
        second = request('/api/session', {})
        assert second['games_count'] == 6
        result = request('/api/games/import?session=' + session, package, headers)
        assert result['state']['games_count'] == 6 and installed.read_bytes() == package
        assert request('/api/command', {'session': second['session'], 'action': 'status'})['games_count'] == 6
        assert not list((root / 'tf/books').glob('*.pga'))
        checks.append('native-install-uses-private-app-directory-and-isolated-session')
        result = request('/api/import?session=' + session, '合成图书\n正文'.encode(),
                         {'Content-Type': 'application/octet-stream', 'X-File-Name': 'synthetic.txt'})
        assert result['ok'] and (root / 'tf/books/synthetic.txt').exists()
        assert result['state']['games_count'] == 6
        checks.append('book-import-remains-independent')
        for name in ('index.html', 'preview.js', 'preview.css'):
            content = request('/' + name)
            assert content
        checks.append('preview-assets-available')
    finally:
        server.shutdown()
        worker.join(timeout=5)
        server.server_close()
        for session in list(module.SESSIONS.values()):
            session.close()
    assert hashlib.sha256(binary.read_bytes()).hexdigest() == binary_sha
    report = {'result': 'PASS', 'binary_sha256': binary_sha, 'checks': checks,
              'isolated_temporary_http_service': True, 'existing_user_service_accessed': False,
              'browser_access': False, 'hardware_access': False}
    (output / 'report.json').write_text(json.dumps(report, ensure_ascii=False, indent=2) + '\n')
    print(f"PASS: {len(checks)} isolated native HTTP scenarios; {output / 'report.json'}")


if __name__ == '__main__':
    main()

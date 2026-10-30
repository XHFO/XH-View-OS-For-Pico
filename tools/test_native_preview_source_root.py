#!/usr/bin/env python3
"""中文：显式源码目录的启动校验、构建选择与独立临时HTTP验证；不操作已有服务或设备。
English: Verify explicit-source startup checks, builder selection and a temporary HTTP fixture, without touching user services or devices.
"""
from pathlib import Path
import argparse
import contextlib
import hashlib
import importlib.util
import io
import json
import queue
import re
import sys
import tempfile
import threading
import urllib.request

ROOT = Path(__file__).resolve().parents[1]
SERVER = ROOT.parents[1] / 'outputs/firmware-preview/server.py'


def load_server():
    spec = importlib.util.spec_from_file_location('isolated_source_root_server', SERVER)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source-root', type=Path, required=True)
    parser.add_argument('--build-dir', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    source, build, output = args.source_root.resolve(), args.build_dir.resolve(), args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    baseline_path = SERVER.with_name('baseline.json')
    baseline_hash = hashlib.sha256(baseline_path.read_bytes()).hexdigest()
    manifest = json.loads((build / 'native-preview-build.json').read_text())
    assert manifest['source_root'] == str(source)
    checks = []
    powerkey_evidence = {}

    # 真正运行参数入口，由临时随机端口提供实际原生会话；禁止隐式重编。
    # Exercise the actual argument entry on a random temporary port, with real native sessions and no implicit rebuild.
    module = load_server()
    started, errors = queue.Queue(), []
    original_server = module.ThreadingHTTPServer
    class FixtureServer(original_server):
        def serve_forever(self, *args, **kwargs):
            started.put(self)
            return super().serve_forever(poll_interval=.02)
    module.ThreadingHTTPServer = FixtureServer
    old_argv = sys.argv
    sys.argv = [str(SERVER), '--candidate', '--source-root', str(source), '--build-dir', str(build), '--port', '0']
    def run():
        try:
            module.main()
        except BaseException as error:
            errors.append(error)
    worker = threading.Thread(target=run, daemon=True)
    worker.start()
    http = None
    try:
        try:
            http = started.get(timeout=15)
        except queue.Empty:
            raise AssertionError('Fixture server did not start: ' + repr(errors))
        origin = 'http://127.0.0.1:' + str(http.server_port)
        def request(path, data=None, headers=None):
            if isinstance(data, dict):
                data = json.dumps(data).encode()
                headers = {'Content-Type': 'application/json', **(headers or {})}
            with urllib.request.urlopen(urllib.request.Request(origin + path, data=data, headers=headers or {}), timeout=45) as response:
                value = response.read()
                return json.loads(value) if 'application/json' in response.headers.get('Content-Type', '') else value
        bootstrap = request('/api/bootstrap')
        assert bootstrap['version'] == manifest['version'] and bootstrap['source_root'] == str(source)
        assert module.REPO == source and module.BUILD == build
        state = request('/api/session', {})
        session_id = state['session']
        native = module.SESSIONS[session_id]
        assert native.process.args[0] == str(build / 'native-preview')
        assert native.process.args[1] == str(source / 'sdcard/fonts/MiSansVF.ttf')
        assert state['version'] == manifest['version'] and state['games_count'] == 6
        assert request(state['image_url']).startswith(b'\x89PNG\r\n\x1a\n')
        result = request('/api/import?session=' + session_id, '独立源码阅读测试\n正文使用选定树的真实字体。'.encode(), {'Content-Type': 'application/octet-stream', 'X-File-Name': 'source-root.txt'})
        assert result['ok'] and (native.root / 'tf/books/source-root.txt').is_file()

        # HTTP 电源按钮恒发同一事件，由共享控制器决定锁屏或恢复；不依赖前端旧状态。
        # The HTTP power button always sends one event; the shared controller decides lock/wake without stale UI state.
        frontend = request('/preview.js').decode('utf-8')
        handler = re.search(r"\$\('power'\)\.onclick\s*=\s*[^\n]+", frontend)
        assert handler and re.search(r"action\s*:\s*['\"]powerkey['\"]", handler.group()), handler
        assert not re.search(r"state(?:\?\.|\.)power_state|['\"](?:sleep|wake)['\"]", handler.group()), handler.group()
        assert module.command_text({'action': 'powerkey'}) == 'powerkey'
        checks.append('frontend-power-button-always-sends-powerkey')

        def command(action, **arguments):
            return request('/api/command', {'session': session_id, 'action': action, **arguments})

        state = command('status')
        for _ in range(20):
            state = command('tick', ms=state['now_ms'] + 100)
        before_frame = request(state['image_url'])
        before_route, before_app = state['route'], state['active_app']
        book_hash = hashlib.sha256((native.root / 'tf/books/source-root.txt').read_bytes()).hexdigest()
        state = command('powerkey')
        assert state['power_state'] == 1, state
        checks.append('http-powerkey-enters-shared-lock')
        # 一次完整接触与紧接的双击都不能唤醒，原页面也不接收触摸。
        # A completed tap and the following double tap cannot wake or dispatch to the retained page.
        for _ in range(3):
            state = command('touch', phase='down', x=180, y=300, ms=state['now_ms'] + 20)
            assert state['power_state'] == 1, state
            state = command('touch', phase='up', x=180, y=300, ms=state['now_ms'] + 60)
            assert state['power_state'] == 1 and (state['route'], state['active_app']) == (before_route, before_app), state
        checks.append('http-single-and-double-screen-taps-do-not-wake')
        state = command('powerkey')
        assert not state['power_state'] and request(state['image_url']) == before_frame, state
        for _ in range(3):
            state = command('powerkey')
            assert state['power_state'] == 1, state
            state = command('powerkey')
            assert not state['power_state'] and request(state['image_url']) == before_frame, state
        assert (state['route'], state['active_app']) == (before_route, before_app), state
        assert hashlib.sha256((native.root / 'tf/books/source-root.txt').read_bytes()).hexdigest() == book_hash
        checks.append('http-powerkey-quick-cycles-restore-exact-frame-and-preserve-book')
        powerkey_evidence = {'action': 'powerkey', 'quick_cycles': 3, 'locked_completed_screen_taps': 3,
                             'restored_frame_sha256': hashlib.sha256(before_frame).hexdigest(),
                             'frontend_sha256': hashlib.sha256(frontend.encode()).hexdigest(), 'handler': handler.group()}
        request('/api/close', {'session': session_id})
        checks += ['explicit-source-cli-serves-real-native-session', 'bootstrap-declares-selected-source', 'font-and-binary-use-selected-tree', 'native-book-import-and-frame']
    finally:
        if http:
            http.shutdown()
        worker.join(timeout=10)
        sys.argv = old_argv
    assert not worker.is_alive() and not errors, errors

    def invoke(instance, arguments):
        previous = sys.argv
        sys.argv = [str(SERVER)] + arguments
        try:
            with contextlib.redirect_stdout(io.StringIO()):
                instance.main()
        finally:
            sys.argv = previous

    with tempfile.TemporaryDirectory(prefix='pico-source-root-') as folder:
        fixture = Path(folder).resolve()
        fake_build = fixture / 'build'
        fake_build.mkdir()
        (fake_build / 'native-preview').write_bytes(b'fake-native')
        base = {'version': manifest['version'], 'binary_sha256': hashlib.sha256(b'fake-native').hexdigest(), 'source_root': str(source), 'sources': {}}
        for name, patch, use_explicit, message in [
            ('wrong-root', {'source_root': str(ROOT)}, True, '其他源码目录'),
            ('missing-root', {'source_root': None}, True, '缺少源码目录'),
            ('default-root-rejects-isolated', {}, False, '其他源码目录'),
            ('missing-bound-source', {'sources': {'missing-file.c': '0'*64}}, True, '代码已更新'),
            ('wrong-binary', {'binary_sha256': '0'*64}, True, '程序与构建记录不一致'),
        ]:
            (fake_build / 'native-preview-build.json').write_text(json.dumps({**base, **patch}))
            instance = load_server()
            arguments = ['--candidate', '--build-dir', str(fake_build)]
            if use_explicit:
                arguments += ['--source-root', str(source)]
            try:
                invoke(instance, arguments)
            except SystemExit as error:
                assert message in str(error), (name, error)
            else:
                raise AssertionError('Invalid source/binary was accepted: ' + name)
            checks.append(name)

        # 明确构建时只调用选定树的工具，并在所选树下使用默认候选输出路径。
        # An explicit build calls only the selected tree's builder and defaults the candidate output beneath that tree.
        fake_source = fixture / 'source'
        (fake_source / 'tools').mkdir(parents=True)
        (fake_source / 'tools/native_preview_build.py').write_text('# fixture only\n')
        instance = load_server()
        calls = []
        def build_stub(command, **kwargs):
            expected = fake_source / 'build/native-preview-candidate'
            assert command == [sys.executable, str(fake_source/'tools/native_preview_build.py'), '--output', str(expected)]
            calls.append(command)
            expected.mkdir(parents=True)
            (expected/'native-preview').write_bytes(b'fake-native')
            (expected/'native-preview-build.json').write_text(json.dumps({**base, 'source_root': str(fake_source)}))
        class NoopServer:
            def __init__(self, *args):
                pass
            def serve_forever(self):
                pass
            def server_close(self):
                pass
        original_run = instance.subprocess.run
        instance.subprocess.run = build_stub
        instance.ThreadingHTTPServer = NoopServer
        try:
            invoke(instance, ['--candidate', '--build', '--source-root', str(fake_source), '--port', '0'])
        finally:
            instance.subprocess.run = original_run
        assert len(calls) == 1
        checks.append('explicit-build-selects-source-tool-and-default-output')

    assert hashlib.sha256(baseline_path.read_bytes()).hexdigest() == baseline_hash
    report = {'result': 'PASS', 'source_root': str(source), 'binary_sha256': manifest['binary_sha256'], 'checks': checks,
              'powerkey_http': powerkey_evidence,
              'baseline_unchanged': True, 'isolated_temporary_http_service': True, 'existing_user_service_accessed': False,
              'browser_access': False, 'hardware_access': False}
    (output/'report.json').write_text(json.dumps(report, ensure_ascii=False, indent=2)+'\n')
    print('PASS: ' + str(len(checks)) + ' explicit-source/native HTTP checks; ' + str(output/'report.json'))


if __name__ == '__main__':
    main()

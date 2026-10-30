#!/usr/bin/env python3
"""中文：真实微信读书模块经共享键盘提交与取消搜索；只用合成内容，不访问设备。
English: Exercise real WeRead searches through the shared keyboard, using synthetic data and no device.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import time
import urllib.parse

from test_native_preview_cover_layouts import ManagementRuntime
from test_native_preview_shelf_ble import ROOT
from test_native_weread_ui import DECODER


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', type=Path, required=True)
    parser.add_argument('--package', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=False)
    fixture = output / 'fixtures'
    fixture.mkdir()
    (fixture / 'search.json').write_text(json.dumps({'results': [{'title': '电子书', 'scope': 17,
        'books': [{'searchIdx': 1, 'bookInfo': {'bookId': 'fixture1', 'title': '合成搜索结果',
        'author': '本地测试', 'readerLink': 'https://weread.qq.com/web/reader/fixture1'}}]}], 'hasMore': 0}, ensure_ascii=False))
    os.environ.update(RPICO_WEREAD_FIXTURE_ROOT=str(fixture), RPICO_WEREAD_OFFLINE='1', RPICO_HOME_LINK_OFFLINE='1')
    report = {'result': 'RUNNING', 'checks': [], 'qr': [], 'hardware_access': False, 'network_access': False,
              'binary_sha256': hashlib.sha256(args.binary.read_bytes()).hexdigest(),
              'package_sha256': hashlib.sha256(args.package.read_bytes()).hexdigest()}
    expected_qr = {}
    try:
        for landscape in (False, True):
            label = 'landscape' if landscape else 'portrait'
            session = output / label
            session.mkdir()
            runtime = ManagementRuntime(args.binary.resolve(), ROOT / 'sdcard/fonts/MiSansVF.ttf', session, output)
            try:
                runtime.start()
                tools = session / 'tf/apps/tools'
                tools.mkdir(parents=True, exist_ok=True)
                shutil.copyfile(args.package, tools / 'weread.pga')
                management = runtime.command('manage-open')['management']
                key = urllib.parse.parse_qs(urllib.parse.urlsplit(management['url']).fragment)['key'][0]

                def rpc(method, action, body=None):
                    request = {'key': key, 'method': method, 'path': '/api/manage/pico/weread/' + action,
                               'query': '', 'body': json.dumps(body or {})}
                    response = runtime.command('manage-web ' + json.dumps(request).encode().hex())['management_response']
                    assert response['status'] in (200, 202), response
                    data = response['body']
                    return json.loads(data) if isinstance(data, str) else data

                def query():
                    return rpc('GET', 'query')

                def letter(value):
                    wide = runtime.transcript[-1]['state'].get('logical_width', 0) > 360
                    width, x, top, pitch = (404, 224, 136, 44) if wide else (344, 8, 390, 48)
                    step = width // 10
                    for row, letters in enumerate(('qwertyuiop', 'asdfghjkl', 'zxcvbnm')):
                        if value in letters:
                            runtime.tap(x + (width - len(letters) * step) // 2 + letters.index(value) * step + (step - 4) // 2,
                                        top + row * pitch + (pitch - 4) // 2)
                            return
                    raise AssertionError(value)

                rpc('POST', 'authorize', {'api_key': 'wrk-keyboard-fixture'})
                runtime.command('rotate ' + str(int(landscape)))
                state = runtime.command('app-open weread')
                assert state['route'] == 28 and not state['games_error'], state
                width = 640 if landscape else 360
                runtime.tap(width - 48, 35)
                before = query()['request_id']
                state = runtime.tap(90, 137)
                assert state['route'] == 19 and not state['games_error'], state
                # 真正进入宿主编辑器，输入不借助测试专用文本命令。
                # Enter the real host editor and type without a test-only text injection command.
                runtime.tap(62 if landscape else 222, 251 if landscape else 293)
                runtime.tick(100)
                for char in 'aurora':
                    letter(char)
                runtime.tick(100)
                runtime.capture('keyboard-entered-' + label, runtime.command('status'))
                state = runtime.tap(width - 46, 47)
                assert state['route'] == 28 and not state['games_error'], state
                deadline = time.monotonic() + 5
                while time.monotonic() < deadline:
                    runtime.tick(100)
                    status = query()
                    if status['request_id'] != before and status['ready']:
                        break
                    time.sleep(.01)
                assert status['request_id'] != before and status['ready'] and status['kind'] == 1, status
                time.sleep(.4)
                runtime.settle(8)
                runtime.capture('keyboard-search-' + label, runtime.command('status'))
                submitted = status['request_id']
                state = runtime.tap(90, 97)
                assert state['route'] == 19, state
                for char in 'cancel':
                    letter(char)
                state = runtime.tap(24, 47)
                assert state['route'] == 28 and not state['games_error'], state
                runtime.settle(10)
                assert query()['request_id'] == submitted, 'cancel submitted a search'
                runtime.capture('keyboard-cancel-' + label, runtime.command('status'))
                # 编辑器旋转后返回同一模块；离开再重开不得保留已取消输入。
                # Return to the retained module after rotating the editor; reopening must not submit cancelled input.
                state = runtime.tap(90, 97)
                assert state['route'] == 19, state
                state = runtime.command('rotate ' + str(int(not landscape)))
                assert state['route'] == 19, state
                state = runtime.command('key 1 click')
                assert state['route'] == 28 and not state['games_error'], state
                runtime.settle(5)
                assert query()['request_id'] == submitted
                runtime.command('route 0')
                state = runtime.command('app-open weread')
                assert state['route'] == 28 and not state['games_error'], state
                runtime.settle(5)
                assert query()['request_id'] == submitted
                runtime.command('rotate ' + str(int(landscape)))
                runtime.tap(width - 140, 88)
                state = runtime.tap(60, 260 if landscape else 284)
                assert state['route'] == 30 and not state['games_error'], state
                state = runtime.tap(width - 58, 47)
                assert state['route'] == 6, state
                # 从扫码设置经添加网络进入真实编辑器，再返回原配置与模块。
                # Enter a real network-name editor from QR setup and return to the same setup/module.
                state = runtime.tap(438 if landscape else 180, 291 if landscape else 527)
                assert state['route'] == 19 and not state['games_error'], state
                state = runtime.tap(24, 47)
                assert state['route'] == 6, state
                state = runtime.tap(24, 47)
                assert state['route'] == 30, state
                runtime.settle(5)
                runtime.capture('keyboard-wifi-setup-return-' + label, runtime.command('status'))
                management = runtime.command('manage-status')['management']
                expected_qr[output / ('keyboard-wifi-setup-return-' + label + '.png')] = management['url'] + '&view=weread'
                state = runtime.tap(24, 47)
                assert state['route'] == 28 and not state['games_error'], state
                report['checks'] += [label + '-real-keyboard-submit-retains-module',
                                     label + '-tap-back-cancels-search',
                                     label + '-rotation-and-physical-back-retain-module',
                                     label + '-unload-reopen-no-stale-input',
                                     label + '-wifi-setup-editor-retains-module-and-qr']
            finally:
                runtime.close()
                (output / ('transcript-' + label + '.json')).write_text(json.dumps(runtime.transcript, ensure_ascii=False, indent=2) + '\n')
        source, decoder = output / 'decode.swift', output / 'decode-qr'
        source.write_text(DECODER)
        subprocess.run(['swiftc', str(source), '-o', str(decoder)], check=True)
        decoded = subprocess.run([str(decoder), *(str(path) for path in expected_qr)], capture_output=True, text=True, check=True)
        for line in decoded.stdout.splitlines():
            result = json.loads(line)
            assert result['values'] == [expected_qr[Path(result['file'])]], result
            report['qr'].append(result)
        assert len(report['qr']) == 2
        report['result'] = 'PASS'
    except Exception as error:
        report['error'] = str(error)
        raise
    finally:
        (output / 'report.json').write_text(json.dumps(report, ensure_ascii=False, indent=2) + '\n')
    print('Native WeRead real keyboard: 10 lifecycle checks PASS')


if __name__ == '__main__':
    main()

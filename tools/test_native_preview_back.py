#!/usr/bin/env python3
"""中文：真实原生触摸验证页头返回与逐层退回，不访问硬件或用户TF。
English: Exercise heading Back through native touch dispatch and nested dismissal using a private TF.
"""
from pathlib import Path
import argparse
import hashlib
import json
import os

from test_native_preview_shelf_ble import ROOT, Runtime


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    os.environ['RPICO_HOME_LINK_OFFLINE'] = '1'
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=False)
    session = output / 'session'
    session.mkdir()
    books = session / 'tf/books'
    books.mkdir(parents=True)
    (books / '001-back-test.txt').write_text('阅读返回测试\n' + '这是本地生成的返回导航测试文字，退出阅读记录后应恢复同一本书和原来页码。\n' * 300)
    binary = args.binary.resolve()
    runtime = Runtime(binary, ROOT / 'sdcard/fonts/MiSansVF.ttf', session, output)
    report = {'result': 'RUNNING', 'device_access': False,
              'binary_sha256': hashlib.sha256(binary.read_bytes()).hexdigest(), 'checks': [], 'not_applicable': []}
    try:
        runtime.start()
        runtime.settle(3)
        for landscape in (0, 1):
            runtime.command('rotate ' + str(landscape))
            for route, name in [(2, 'records'), (19, 'editor'), (5, 'settings'),
                                (25, 'datetime'), (24, 'bluetooth'), (30, 'wireless-management')]:
                runtime.command('route 0')
                runtime.command('route ' + str(route))
                runtime.settle(2)
                runtime.capture(f'{landscape}-{name}-before', runtime.command('status'))
                after = runtime.tap(23, 47)
                assert after['route'] == 0, (name, after)
                report['checks'].append(f'{landscape}-{name}-back')
                runtime.capture(f'{landscape}-{name}-after', after)
            runtime.command('route 0')
            home = runtime.command('route 3')
            assert not home['services']['homeConfigured'], home
            runtime.settle(2)
            runtime.capture(f'{landscape}-home-unconfigured-before', runtime.command('status'))
            after = runtime.tap(23, 47)
            assert after['route'] == 0, after
            report['checks'].append(f'{landscape}-home-unconfigured-heading-back')
            report['not_applicable'].append({
                'check': f'{landscape}-legacy-home-room-picker',
                'reason': 'Private fixture has no HA account/entities; current Home Link TF module has monitor/setup pages, not the retired built-in room picker.'})
            runtime.capture(f'{landscape}-home-unconfigured-after', after)
            runtime.command('route 0')
            runtime.command('route 26')
            runtime.settle(2)
            base = hashlib.sha256(runtime.frame.read_bytes()).hexdigest()
            for index in (2, 3):
                runtime.tap(80 if index == 2 else (240 if landscape else 260), 140 if landscape else 155)
                runtime.capture(f'{landscape}-power-confirm-{index}', runtime.command('status'))
                assert hashlib.sha256(runtime.frame.read_bytes()).hexdigest() != base
                dismissed = runtime.tap(23, 47)
                assert dismissed['route'] == 26 and dismissed['power_state'] == 0, dismissed
                assert hashlib.sha256(runtime.frame.read_bytes()).hexdigest() == base
                report['checks'].append(f'{landscape}-power-confirm-{index}-dismiss-before-navigation')
            assert runtime.tap(23, 47)['route'] == 0
        runtime.command('route 0')
        runtime.command('rotate 0')
        runtime.command('app-open drawing-notes')
        runtime.tap(90, 165)
        runtime.capture('drawing-edit-before', runtime.command('status'))
        first = runtime.tap(23, 47)
        assert first['route'] == 28, first
        runtime.capture('drawing-list-after-first-back', first)
        second = runtime.tap(23, 47)
        assert second['route'] == 0, second
        report['checks'].append('drawing-edit-back-to-list-then-origin')
        runtime.settle(15)
        runtime.tap(60, 330)
        reading = runtime.settle(15)
        assert reading['active_app'] == 'book' and reading['book_pages'] > 1, reading
        position = (reading['book_chapter'], reading['book_page'])
        runtime.command('route 2')
        restored = runtime.tap(23, 47)
        restored = runtime.settle(4)
        assert restored['active_app'] == 'book' and (restored['book_chapter'], restored['book_page']) == position, restored
        report['checks'].append('records-heading-back-restores-real-book-position')
        runtime.capture('real-book-restored', restored)
        report.update(result='PASS', transcript=runtime.transcript, scenes=runtime.scenes)
    finally:
        runtime.close()
        if report['result'] == 'RUNNING':
            report['result'] = 'FAILED'
        report.update(transcript=runtime.transcript, scenes=runtime.scenes)
        (output / 'report.json').write_text(json.dumps(report, ensure_ascii=False, indent=2) + '\n')
    print(json.dumps({'result': report['result'], 'checks': len(report['checks'])}))


if __name__ == '__main__':
    main()

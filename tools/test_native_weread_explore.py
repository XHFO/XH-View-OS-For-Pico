#!/usr/bin/env python3
"""中文：真实TF在线微信读书9类内容、横竖布局与官方阅读二维码回归；仅合成响应。
English: Test all nine online WeRead kinds, both orientations and official-reader QR using synthetic responses.
"""
from pathlib import Path
import argparse
import hashlib
import json
import os
import shutil
import subprocess
import time
from urllib.parse import parse_qs, urlsplit

from test_native_preview_cover_layouts import ManagementRuntime
from test_native_preview_shelf_ble import ROOT, read_pgm, write_png
from test_native_weread_ui import DECODER


def fixture_files(root):
    root.mkdir()
    book = {'bookId': 'fixture-book', 'title': '三体 · 合成图书', 'author': '刘慈欣', 'newRating': 96,
            'readerLink': 'https://weread.qq.com/web/reader/fixture',
            'cover': 'https://cdn.weread.qq.com/cover/native-fixture.jpg'}
    content = {
        'search': {'hasMore': 1, 'results': [{'scope': 17, 'books': [
            {'searchIdx': 100+i, 'bookInfo': dict(book, title=f'搜索图书 {i+1:02d}')} for i in range(12)]}]},
        'info': dict(book, intro='这是一份合成图书资料，用于验证设备页面布局。\n' * 60 + '简介最后一段。'),
        'chapters': {'chapters': [{'chapterUid': i+1, 'title': f'第 {i+1:02d} 章 · 合成目录',
                                 'wordCount': 2048, 'level': 1} for i in range(400)]},
        'progress': {'book': {'progress': 37, 'recordReadingTime': 7260}},
        'recommend': {'books': [dict(book, title='为你推荐 · 合成图书', searchIdx=50)]},
        'similar': {'booksimilar': {'sessionId': 'native-fixture-session', 'books': [
            {'idx': 66, 'book': {'bookInfo': dict(book, title='相似图书 · 合成资料')}}]}},
        'stats': {'totalReadTime': 7260, 'readDays': 7, 'dayAverageReadTime': 1200,
                  'preferCategoryWord': '偏爱科幻与文学', 'preferTimeWord': '常在夜间阅读',
                  'readStat': [{'stat': '已读图书', 'counts': '12 本'}],
                  'readLongest': [{'book': book, 'readTime': 6000}]},
        'highlights': {'totalCount': 100, 'items': [{'totalCount': 28, 'chapterUid': 1,
                       'markText': '热门划线全文，保留段落。\n' * 70 + '划线最后一行。'}]},
        'reviews': {'reviewsHasMore': 1, 'synckey': 999, 'reviews': [{'idx': 88, 'review': {'review': {
            'author': {'name': '合成读者'}, 'content': '完整书评内容，支持本地逐页查看。\n' * 80 + '书评最后一行。'}}}]},
    }
    for name, value in content.items():
        (root/(name+'.json')).write_text(json.dumps(value, ensure_ascii=False), encoding='utf-8')
    write_png(root/'cover.jpg', 160, 240, bytes((x//10)*17 for y in range(240) for x in range(160)))
    return book['readerLink']


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', type=Path, required=True)
    parser.add_argument('--package', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    binary, package, output = args.binary.resolve(), args.package.resolve(), args.output.resolve()
    output.mkdir(parents=True, exist_ok=False)
    fixture_root = output/'responses'; reader_link = fixture_files(fixture_root)
    os.environ.update(RPICO_WEREAD_FIXTURE_ROOT=str(fixture_root), RPICO_WEREAD_OFFLINE='1', RPICO_HOME_LINK_OFFLINE='1')
    report = {'result': 'RUNNING', 'hardware_access': False, 'network_access': False, 'real_tf_package': True,
              'binary_sha256': hashlib.sha256(binary.read_bytes()).hexdigest(),
              'package_sha256': hashlib.sha256(package.read_bytes()).hexdigest(), 'checks': [], 'qr': [], 'scenes': {}}
    expected_qr = {}
    try:
        for landscape in (False, True):
            orientation = 'landscape' if landscape else 'portrait'
            session = output/('session-'+orientation); session.mkdir()
            runtime = ManagementRuntime(binary, ROOT/'sdcard/fonts/MiSansVF.ttf', session, output)
            width, height = (640, 360) if landscape else (360, 640)
            kinds = set()
            try:
                runtime.start()
                tools = session/'tf/apps/tools'; tools.mkdir(parents=True, exist_ok=True)
                shutil.copyfile(package, tools/'weread.pga')
                runtime.command('rotate '+str(int(landscape)))
                link = runtime.command('manage-open')['management']
                key = parse_qs(urlsplit(link['url']).fragment)['key'][0]

                def rpc(method, path, body=None):
                    request = {'key': key, 'method': method, 'path': '/api/manage/pico/weread/'+path,
                               'query': '', 'body': json.dumps(body or {})}
                    response = runtime.command('manage-web '+json.dumps(request).encode().hex())['management_response']
                    assert response['status'] in (200, 202), response
                    return response['body']

                def capture(label, state=None):
                    state = state or runtime.command('status')
                    assert state['route'] == 28 and not state['games_error'], state
                    name = 'online-'+label+'-'+orientation
                    runtime.capture(name, state)
                    _, _, pixels = read_pgm(runtime.frame)
                    assert all(pixel % 17 == 0 for pixel in pixels), name+' is not 16 gray'
                    report['scenes'][name] = hashlib.sha256(pixels).hexdigest()
                    return state

                def ready(kind, label):
                    deadline = time.monotonic()+15
                    while True:
                        status = rpc('GET', 'query')
                        if status['kind'] == kind and status['ready'] and not status['busy']:
                            assert not status['busy'] and not status['error'], status
                            break
                        assert time.monotonic() < deadline, (kind, status)
                        runtime.tick(600); time.sleep(.01)
                    # 模块轮询使用真实host时间，模拟逻辑tick本身不会让其到期。
                    # Module polling uses real host time, which synthetic logical ticks do not advance.
                    time.sleep(.55)
                    runtime.settle(3, 600)
                    kinds.add(kind); return capture(label)

                assert rpc('POST', 'authorize', {'api_key': 'wrk-native-explore-fixture'})['configured']
                state = runtime.command('app-open weread'); capture('shelf', state)
                runtime.tap(width-50, 35); capture('discover')
                state = runtime.tap(60, 130); assert state['route'] == 19, state
                # 使用真实共享键盘输入，不绕过宿主的确认/返回生命周期。
                # Use the real shared keyboard rather than bypass host confirmation/return.
                runtime.tap(62 if landscape else 222, 251 if landscape else 293)
                keyboard_width, keyboard_x, keyboard_top, pitch = (404, 224, 136, 44) if landscape else (344, 8, 390, 48)
                kp = keyboard_width//10
                for letter in 'book':
                    for row, letters in enumerate(('qwertyuiop', 'asdfghjkl', 'zxcvbnm')):
                        if letter in letters:
                            runtime.tap(keyboard_x+(keyboard_width-len(letters)*kp)//2+letters.index(letter)*kp+(kp-4)//2,
                                        keyboard_top+row*pitch+(pitch-4)//2)
                            break
                runtime.tick(600); runtime.tap(width-45, 45); ready(1, 'search')
                runtime.tap(50, 180); ready(2, 'info')
                # 原生图片解码和灰阶封面绘制必须真的落进封面区域，不能只验证占位框。
                # Exercise real native image decoding/drawing inside the cover region, not just its placeholder.
                frame_width, frame_height, pixels = read_pgm(runtime.frame)
                scale_x, scale_y = frame_width/width, frame_height/height
                cover_w, cover_h = (64, 90) if landscape else (80, 112)
                crop = b''.join(pixels[y*frame_width+int(20*scale_x):y*frame_width+int((20+cover_w)*scale_x)]
                                for y in range(int(192*scale_y), int((192+cover_h)*scale_y)))
                assert len(set(crop)) == 16, (orientation, sorted(set(crop)))
                report['checks'].append(orientation+':real-image-decoder-cover-all-16-grays')
                runtime.tap(width-45, 35); capture('official-reader-qr')
                expected_qr[output/('online-official-reader-qr-'+orientation+'.png')] = reader_link
                runtime.tap(30, 35); capture('qr-return')
                tabs_width = (width-48)//3
                runtime.tap(16+tabs_width+8+20, 92); ready(3, 'chapters')
                runtime.command('key 2 click'); capture('chapters-page2')
                runtime.command('key 0 click'); runtime.tap(50, 200); ready(8, 'chapter-highlights')
                runtime.tap(50, 200); capture('highlight-full')
                runtime.command('key 2 click'); capture('highlight-full-page2')
                runtime.tap(30, 35); ready(8, 'highlights-return')
                runtime.tap(16+2*(tabs_width+8)+20, 92); ready(4, 'progress')
                runtime.tap(16+tabs_width+8+20, 135); ready(9, 'reviews')
                runtime.tap(50, 200); capture('review-full')
                runtime.command('key 2 click'); capture('review-full-page2')
                runtime.tap(30, 35); ready(9, 'reviews-return')
                runtime.tap(16+2*(tabs_width+8)+20, 135); ready(6, 'similar')
                runtime.tap(30, 35); ready(3, 'chapters-return')
                runtime.tap(30, 35); ready(1, 'search-return')
                runtime.tap(30, 35); capture('discover-return')
                runtime.tap(45, 250); ready(5, 'recommend')
                runtime.tap(30, 35); capture('recommend-return')
                runtime.tap(width-60, 250); ready(7, 'stats')
                runtime.command('key 2 click'); capture('stats-page2')
                for mode in (0, 2, 3):
                    stats_width = (width-44)//4
                    runtime.tap(16+(stats_width+4)*mode+15, 90); ready(7, 'stats-mode-'+str(mode))
                assert kinds == set(range(1, 10)), kinds
                # 完成查询后静置不得继续重绘，二维码返回后TF模块保持活动。
                # Completed content stays idle without repeated repaint; the TF module survives QR return.
                before = runtime.command('status')['presents']; runtime.settle(5, 600)
                assert runtime.command('status')['presents'] == before
                report['checks'] += [orientation+':all-nine-kinds', orientation+':real-host-search-input',
                                     orientation+':chapter-pagination-and-context-highlights', orientation+':long-text-pagination',
                                     orientation+':stats-modes', orientation+':16-gray', orientation+':idle-no-repaint']
            finally:
                runtime.close()
                (output/('transcript-'+orientation+'.json')).write_text(json.dumps(runtime.transcript, ensure_ascii=False, indent=2)+'\n')
        decoder_source = output/'decode.swift'; decoder_source.write_text(DECODER)
        decoder = output/'decode-qr'; subprocess.run(['swiftc', str(decoder_source), '-o', str(decoder)], check=True)
        decoded = subprocess.run([str(decoder), *(str(path) for path in expected_qr)], text=True, capture_output=True, check=True)
        for line in decoded.stdout.splitlines():
            value = json.loads(line); assert value['values'] == [expected_qr[Path(value['file'])]], value
            report['qr'].append(value)
        assert len(report['qr']) == 2
        report['result'] = 'PASS'
    except Exception as error:
        report['error'] = str(error); raise
    finally:
        (output/'report.json').write_text(json.dumps(report, ensure_ascii=False, indent=2)+'\n')
    print('Native WeRead exploration PASS:', len(report['checks']), 'checks;', len(report['scenes']), 'screens; 2 official QR decodes')


if __name__ == '__main__':
    main()

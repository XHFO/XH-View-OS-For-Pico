#!/usr/bin/env python3
"""真实控制器的五种封面锁屏/书架横竖集成；仅生成合成 EPUB，不访问硬件。
Exercise five real cover-lock/shelf layouts in both orientations using synthetic EPUBs only, without hardware.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import tempfile
import time
import urllib.parse
import zipfile

from test_native_preview_shelf_ble import ROOT, Runtime, write_png


class ManagementRuntime(Runtime):
    def command(self, value, ok=True):
        self.process.stdin.write((value + '\n').encode()); self.process.stdin.flush()
        deadline = time.monotonic() + 45
        while b'\n' not in self.buffer:
            assert self.selector.select(max(0, deadline-time.monotonic())), ('timeout', value, self.session)
            part = os.read(self.process.stdout.fileno(), 65536)
            assert part, ('ended', value, self.session)
            self.buffer += part
        line, self.buffer = self.buffer.split(b'\n', 1); state = json.loads(line)
        if 'ok' in state: assert state['ok'] is ok, (value, state)
        self.now = state.get('now_ms', self.now)
        self.transcript.append({'run': self.run, 'command': value, 'state': state})
        return state


def fixtures(books):
    for index in range(6):
        image = books / 'cover.png'
        pixels = bytes(((index * 2 + x // 24 + y // 32) % 16) * 17 for y in range(288) for x in range(192))
        write_png(image, 192, 288, pixels)
        with zipfile.ZipFile(books / f'{index+1:02d}-合成封面图书.epub', 'w', zipfile.ZIP_DEFLATED) as epub:
            epub.writestr('mimetype', 'application/epub+zip')
            epub.writestr('META-INF/container.xml', '<container><rootfiles><rootfile full-path="OPS/book.opf" media-type="application/oebps-package+xml"/></rootfiles></container>')
            epub.writestr('OPS/book.opf', '<package xmlns="http://www.idpf.org/2007/opf" version="3.0"><metadata><title>合成封面图书</title></metadata><manifest><item id="chapter" href="chapter.xhtml" media-type="application/xhtml+xml"/><item id="cover" href="cover.png" media-type="image/png" properties="cover-image"/></manifest><spine><itemref idref="chapter"/></spine></package>')
            epub.writestr('OPS/chapter.xhtml', '<html><body><h1>合成图书</h1><p>仅供本地集成测试的合成文字与原创几何封面。</p></body></html>')
            epub.writestr('OPS/cover.png', image.read_bytes())
        image.unlink()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    output = args.output.resolve(); output.mkdir(parents=True, exist_ok=True)
    session = Path(tempfile.mkdtemp(prefix='covers-', dir=output))
    books = session / 'tf/books'; books.mkdir(parents=True); fixtures(books)
    runtime = ManagementRuntime(args.binary.resolve(), ROOT / 'sdcard/fonts/MiSansVF.ttf', session, output)
    checks, snapshots = [], {}
    try:
        runtime.start(); runtime.command('route 0'); state = runtime.settle(45)
        assert len(list(books.glob('.pico-covers/*.pcv'))) >= 4, state
        key = ''

        def open_management():
            opened = runtime.command('manage-open')['management']
            assert opened['active'], opened
            return urllib.parse.parse_qs(urllib.parse.urlsplit(opened['url']).fragment)['key'][0]

        def rpc(method, path, body=None):
            request = {'key': key, 'method': method, 'path': path, 'query': '', 'body': json.dumps(body or {})}
            return runtime.command('manage-web ' + json.dumps(request).encode().hex())['management_response']

        def status():
            response = rpc('GET', '/api/manage/pico/status')
            assert response['status'] == 200, response
            return response['body']

        key = open_management()
        for landscape in (False, True):
            runtime.command('rotate ' + str(int(landscape)))
            orientation = 'landscape' if landscape else 'portrait'
            for mode, label in enumerate(('covers', 'details', 'compact', 'gallery', 'focus')):
                state = runtime.style(mode, landscape)
                runtime.capture('real-art-shelf-' + label + '-' + orientation, state)
                checks.append('real-art-shelf-' + label + '-' + orientation)
            for layout in range(5):
                response = rpc('PUT', '/api/manage/pico/config', {'auto_sleep_minutes': 0, 'double_tap_wake': True, 'lock_style': 2, 'cover_layout': layout})
                assert response['status'] == 202, response
                runtime.settle(2)
                config = status()['config']
                assert config['state'] == 2 and config['cover_layout'] == layout and config['lock_style'] == 2, config
                before = runtime.frame.read_bytes()
                locked = runtime.command('powerkey')
                assert locked['power_state'] == 1, locked
                label = 'cover-lock-' + str(layout) + '-' + orientation
                runtime.capture(label, locked)
                frame = runtime.frame.read_bytes(); snapshots[label] = hashlib.sha256(frame).hexdigest()
                runtime.settle(5)
                assert runtime.frame.read_bytes() == frame, label + ' repaints while idle'
                assert runtime.command('powerkey')['power_state'] == 0
                assert runtime.frame.read_bytes() == before, label + ' fails to restore shelf'
                checks.append(label + '-commit-lock-idle-wake')
        assert len(set(snapshots.values())) == 10, snapshots
        runtime.close(); runtime.start(); runtime.settle(); key = open_management()
        assert status()['config']['cover_layout'] == 4
        checks.append('cover-layout-persists-process-restart')
        report = {'result': 'PASS', 'binary_sha256': hashlib.sha256(args.binary.read_bytes()).hexdigest(), 'hardware_access': False,
                  'synthetic_books_only': True, 'checks': checks, 'snapshots': snapshots, 'scenes': runtime.scenes}
        (output / 'report.json').write_text(json.dumps(report, ensure_ascii=False, indent=2) + '\n')
        print('Cover layout native integration PASS:', len(checks), 'checks;', len(snapshots), 'distinct lock frames')
    finally:
        runtime.close()


if __name__ == '__main__':
    main()

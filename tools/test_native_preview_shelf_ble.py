#!/usr/bin/env python3
"""同源进程书架样式、局部刷新和BLE翻页集成；使用合成图书，不访问设备。 / Native shelf/refresh/BLE integration with synthetic books only."""
from pathlib import Path
import argparse
import ctypes
import hashlib
import json
import os
import selectors
import struct
import subprocess
import tempfile
import time
import zipfile
import zlib

ROOT = Path(__file__).resolve().parents[1]


def png_chunk(kind, data):
    return struct.pack('>I', len(data)) + kind + data + struct.pack('>I', zlib.crc32(kind + data))


def write_png(path, width, height, pixels):
    rows = b''.join(b'\0' + pixels[y*width:(y+1)*width] for y in range(height))
    path.write_bytes(b'\x89PNG\r\n\x1a\n' + png_chunk(b'IHDR', struct.pack('>IIBBBBB', width, height, 8, 0, 0, 0, 0)) + png_chunk(b'IDAT', zlib.compress(rows)) + png_chunk(b'IEND', b''))


def read_pgm(path):
    signature, dimensions, maximum, pixels = path.read_bytes().split(b'\n', 3)
    width, height = map(int, dimensions.split())
    assert signature == b'P5' and maximum == b'255' and len(pixels) == width*height
    return width, height, pixels


def make_fixtures(books):
    prose = ''.join(f'第{i}章 合成阅读测试\n' + ('清晨的窗前放着一本书，阅读使人发现日常里值得停留的风景。这是自动生成的测试文字，不包含私人图书内容。\n' * 80) for i in range(1, 5))
    (books / '001-long.txt').write_text(prose)
    for i in range(3, 21):
        (books / f'{i:03d}-synthetic.txt').write_text(f'合成图书 {i}\n' + prose[:15000])
    with tempfile.TemporaryDirectory(prefix='cover-') as tmp:
        cover = Path(tmp) / 'cover.png'
        write_png(cover, 128, 192, bytes(35 if y < 30 or y > 155 else 210 if x < 100 else 100 for y in range(192) for x in range(128)))
        with zipfile.ZipFile(books / '002-synthetic.epub', 'w', zipfile.ZIP_DEFLATED) as epub:
            epub.writestr('mimetype', 'application/epub+zip')
            epub.writestr('META-INF/container.xml', '<container><rootfiles><rootfile full-path="OPS/book.opf" media-type="application/oebps-package+xml"/></rootfiles></container>')
            epub.writestr('OPS/book.opf', '<package xmlns="http://www.idpf.org/2007/opf" version="3.0"><metadata><title>合成 EPUB 封面测试</title></metadata><manifest><item id="nav" href="nav.xhtml" media-type="application/xhtml+xml" properties="nav"/><item id="c1" href="c1.xhtml" media-type="application/xhtml+xml"/><item id="c2" href="c2.xhtml" media-type="application/xhtml+xml"/><item id="cover" href="cover.png" media-type="image/png" properties="cover-image"/></manifest><spine><itemref idref="c1"/><itemref idref="c2"/></spine></package>')
            epub.writestr('OPS/nav.xhtml', '<html><body><nav epub:type="toc"><ol><li><a href="c1.xhtml">第一章</a></li><li><a href="c2.xhtml">第二章</a></li></ol></nav></body></html>')
            for i in (1, 2):
                epub.writestr(f'OPS/c{i}.xhtml', f'<html><body><h1>第{i}章</h1>' + ('<p>这是合成电子书，用真实 EPUB 引擎显示正文、目录和封面，翻页器应该只操作正文并保留原有阅读工具。</p>' * 80) + '</body></html>')
            epub.writestr('OPS/cover.png', cover.read_bytes())


class Progress(ctypes.Structure):
    _fields_ = [('offset', ctypes.c_uint32), ('recent', ctypes.c_uint32), ('favorite', ctypes.c_uint8), ('done', ctypes.c_uint8), ('started', ctypes.c_uint8), ('comic_page', ctypes.c_uint8)]


class PrefsHead(ctypes.Structure):
    _fields_ = [('version', ctypes.c_uint32), ('serial', ctypes.c_uint32), ('books', Progress * 6), ('last', ctypes.c_uint8), ('size', ctypes.c_uint8), ('spacing', ctypes.c_uint8), ('weight', ctypes.c_uint8), ('input', ctypes.c_uint8), ('list', ctypes.c_uint8)]


def stored_style(session):
    paths = list(session.glob('tf/**/reader-prefs-v1.dat'))
    assert len(paths) == 1, paths
    magic, size, crc = struct.unpack_from('<III', paths[0].read_bytes())
    payload = paths[0].read_bytes()[12:]
    assert magic == 0x31535058 and len(payload) == size and zlib.crc32(payload) == crc
    return payload[PrefsHead.list.offset]


class Runtime:
    def __init__(self, binary, font, session, output):
        self.binary, self.font, self.session, self.output = binary, font, session, output
        self.frame = session / 'frame.pgm'
        self.transcript, self.scenes, self.now = [], {}, 10000
        self.process = None
        self.run = 0

    def start(self):
        self.run += 1
        self.log = (self.session / f'runtime-{self.run}.log').open('w')
        self.process = subprocess.Popen([str(self.binary), str(self.font), str(self.session), str(self.frame)], stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=self.log)
        self.selector = selectors.DefaultSelector()
        self.selector.register(self.process.stdout, selectors.EVENT_READ)
        self.buffer = b''
        self.now = 10000
        return self.command('status')

    def command(self, value, ok=True):
        self.process.stdin.write((value + '\n').encode())
        self.process.stdin.flush()
        deadline = time.monotonic() + 45
        while b'\n' not in self.buffer:
            ready = self.selector.select(max(0, deadline-time.monotonic()))
            if not ready:
                raise TimeoutError(f'Native command timed out: {value}; see {self.session}')
            part = os.read(self.process.stdout.fileno(), 65536)
            if not part:
                raise RuntimeError(f'Native process ended: {value}; see {self.session}')
            self.buffer += part
        line, self.buffer = self.buffer.split(b'\n', 1)
        state = json.loads(line)
        assert state['ok'] is ok, (value, state)
        self.now = state['now_ms']
        self.transcript.append({'run': self.run, 'command': value, 'state': state})
        return state

    def tick(self, delta=100):
        return self.command(f'tick {self.now + delta}')

    def settle(self, count=20, delta=100):
        for _ in range(count):
            state = self.tick(delta)
        return state

    def tap(self, x, y):
        self.command(f'touch down {x} {y} {self.now + 20}')
        return self.command(f'touch up {x} {y} {self.now + 60}')

    def swipe(self, down=False, landscape=False):
        y, start, end = (280, 500, 380) if landscape else (520, 270, 150)
        if down:
            start, end = end, start
        self.command(f'touch down {start} {y} {self.now + 20}')
        self.command(f'touch move {end} {y} {self.now + 80}')
        return self.command(f'touch up {end} {y} {self.now + 60}')

    def hold(self, x, y, target=None):
        self.command(f'touch down {x} {y} {self.now + 20}')
        self.tick(700)
        if target:
            x, y = target
            self.command(f'touch move {x} {y} {self.now + 60}')
        return self.command(f'touch up {x} {y} {self.now + 40}')

    def capture(self, name, state):
        width, height, pixels = read_pgm(self.frame)
        write_png(self.output / (name + '.png'), width, height, pixels)
        self.scenes[name] = state
        return width, height, pixels

    def style(self, mode, landscape=False):
        self.tap(530 if landscape else 230, 146 if landscape else 263)
        width, height, card_width = (640, 360, 456) if landscape else (360, 640, 328)
        cols = 3 if landscape else 2
        rows = (5 + cols - 1) // cols
        panel_height = 54 + rows * 56 + 24 + 48 + 20
        left, top, cell = (width-card_width)//2, (height-panel_height)//2, (card_width-24-(cols-1)*12)//cols
        state = self.tap(left+12+(mode % cols)*(cell+12)+cell//2, top+54+(mode//cols)*56+24)
        self.tap(2, 74)
        assert stored_style(self.session) == mode, state
        return self.settle()

    def close(self):
        if self.process and self.process.poll() is None:
            try:
                self.command('quit')
                self.process.wait(timeout=10)
            except Exception:
                self.process.terminate()
                try:
                    self.process.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    self.process.kill()
                    self.process.wait(timeout=5)
        if self.process:
            self.selector.close()
            self.log.close()
        self.process = None


def page_position(state):
    assert state['active_app'] == 'book', state
    return state['book_chapter'], state['book_page']


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', type=Path, default=ROOT / 'build/native-preview-candidate/native-preview')
    parser.add_argument('--font', type=Path, default=ROOT / 'sdcard/fonts/MiSansVF.ttf')
    parser.add_argument('--output', type=Path, default=ROOT / 'build/native-preview-shelf-ble')
    args = parser.parse_args()
    binary, font, output = args.binary.resolve(), args.font.resolve(), args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    (output / 'report.json').unlink(missing_ok=True)
    session = Path(tempfile.mkdtemp(prefix='session-', dir=output))
    books = session / 'tf/books'
    books.mkdir(parents=True)
    make_fixtures(books)
    runtime = Runtime(binary, font, session, output)
    refresh_checks = []
    try:
        state = runtime.start()
        assert 'refresh_partial' in state and 'ble_simulated' in state, state
        assert state['ble_simulated'] and not state['ble_enabled'], state
        runtime.command('route 0')
        state = runtime.settle()
        runtime.capture('shelf-default', state)
        caches = list(books.glob('.pico-covers/*.pcv'))
        assert len(caches) == 1, 'Synthetic EPUB must use the real cover cache'
        cache_digest = hashlib.sha256(caches[0].read_bytes()).hexdigest()
        cache_mtime = caches[0].stat().st_mtime_ns
        for landscape in (False, True):
            runtime.command(f'rotate {int(landscape)}')
            for mode in range(5):
                state = runtime.style(mode, landscape)
                label = ('landscape' if landscape else 'portrait') + '-' + ('covers', 'details', 'compact', 'gallery', 'focus')[mode]
                width, height, before_pixels = runtime.capture('shelf-' + label, state)
                # 封面后续页收起续读卡并上移控件；导航区仍保持原像素。
                # Later cover pages collapse continuation and move controls; navigation stays identical.
                content_top = 80 if mode == 0 else 180 if landscape else 294
                header_end = content_top * 19 // 10
                old_presents = state['presents']
                state = runtime.swipe(landscape=landscape)
                _, _, pixels = read_pgm(runtime.frame)
                assert state['presents'] > old_presents and state['refresh_partial'], (label, state)
                assert pixels[:header_end*width] == before_pixels[:header_end*width], 'Shelf paging changed stable header: ' + label
                x, y, rw, rh = state['refresh_area']
                assert y >= header_end and rw > 0 and rh > 0 and x >= 0 and x+rw <= width and y+rh <= height, (label, state)
                refresh_checks.append({'layout': label, 'action': 'page-next', 'area': state['refresh_area'], 'mode': state['refresh_mode'], 'stable_header_identical': True})
                runtime.capture('shelf-page2-' + label, state)
                runtime.swipe(down=True, landscape=landscape)
                state = runtime.settle()
                presents, revision = state['presents'], state['revision']
                state = runtime.swipe(down=True, landscape=landscape)
                assert state['presents'] == presents, 'An unchanged boundary swipe physically presented: ' + label
                state = runtime.settle(10)
                assert state['presents'] == presents and state['revision'] == revision, 'Idle shelf repeatedly presents: ' + label
                # Add an independent file after the last slot; the final footer changes only after discovery commits.
                # 新合成文件排在书单末尾，扫描完成前保留已提交书架，最后仅更新页脚。
                (books / ('9999-' + label + '.txt')).write_text('合成的重新扫描测试文件。\n' * 10)
                state = runtime.tap(606 if landscape else 320, 146 if landscape else 263)
                assert state['presents'] == presents and state['revision'] == revision, 'Refresh immediately repainted the existing shelf'
                state = runtime.settle(30)
                assert state['presents'] == presents + 1 and state['refresh_partial'], (label, 'refresh commits once', state)
                runtime.capture('shelf-refreshed-' + label, state)
                stable = state['presents']
                assert runtime.settle(10)['presents'] == stable
        assert caches[0].stat().st_mtime_ns == cache_mtime and hashlib.sha256(caches[0].read_bytes()).hexdigest() == cache_digest
        runtime.command('rotate 0')
        runtime.style(1)
        state = runtime.command('route 24')
        runtime.capture('bluetooth-disabled', state)
        state = runtime.tap(100, 100)
        assert state['ble_enabled'], state
        state = runtime.settle(12)
        assert state['ble_devices'] == 1 and not state['ble_scanning'], state
        runtime.capture('bluetooth-discovered', state)
        runtime.tap(90, 396)
        state = runtime.settle(8)
        assert state['ble_connected'], state
        runtime.capture('bluetooth-connected', state)
        state = runtime.command('rotate 1')
        runtime.capture('bluetooth-connected-landscape', state)
        state = runtime.tap(40, 195)
        assert state['ble_reversed'], state
        state = runtime.tap(40, 195)
        assert not state['ble_reversed'], state
        runtime.command('rotate 0')
        state = runtime.settle(3)
        # A Bluetooth page receives and drops a pager event without routing it to the retained reader.
        # 蓝牙设置页消费并丢弃翻页输入，不向背后的真实阅读页面传递。
        before = state['presents']
        state = runtime.command('pager 1')
        assert state['route'] == 24 and state['active_app'] == 'system' and state['presents'] == before, state
        runtime.command('route 0')
        runtime.settle()
        state = runtime.tap(70, 350)
        state = runtime.settle(8)
        assert state['active_app'] == 'book' and state['book_kind'] == 'txt' and state['book_page'] == 1, state
        runtime.capture('txt-pager-ready', state)
        state = runtime.command('pager 1')
        assert state['book_page'] == 2 and not state['book_tools'], state
        state = runtime.command('pager -1')
        assert state['book_page'] == 1, state
        runtime.capture('txt-pager-turn', state)
        state = runtime.tap(110, 20)
        assert state['book_jump'], state
        position, presents = page_position(state), state['presents']
        state = runtime.command('pager 1')
        assert state['book_jump'] and page_position(state) == position and state['presents'] == presents, state
        runtime.capture('page-modal-pager-suppressed', state)
        state = runtime.command('key 1 single')
        assert state['active_app'] == 'book' and not state['book_jump'], state
        state = runtime.tap(22, 20)
        assert state['book_view'] == 'toc', state
        position = page_position(state)
        state = runtime.command('pager 1')
        assert state['book_view'] == 'toc' and page_position(state) == position, state
        runtime.command('key 1 single')
        state = runtime.hold(110, 20)
        assert state['book_tools'], state
        position = page_position(state)
        state = runtime.command('pager 1')
        assert state['book_tools'] and page_position(state) == position, state
        runtime.command('key 1 single')
        state = runtime.command('status')
        if state['active_app'] != 'book':
            runtime.command('route 0')
            runtime.settle()
            runtime.tap(100, 180)
            state = runtime.settle(8)
        state = runtime.hold(38, 115, (210, 151))
        assert state['selection'], state
        position = page_position(state)
        state = runtime.command('pager 1')
        assert state['selection'] and page_position(state) == position, state
        runtime.capture('selection-pager-suppressed', state)
        runtime.command('key 1 single')
        state = runtime.command('status')
        assert state['active_app'] == 'book' and not state['selection'], state
        state = runtime.command('pager 1')
        assert state['book_page'] == position[1] + 1, state
        before_sleep = page_position(state)
        runtime.tap(8, 627)
        state = runtime.command('status')
        assert state['fan'], state
        state = runtime.command('pager 1')
        assert state['fan'] and page_position(state) == before_sleep, state
        runtime.capture('corner-menu-pager-suppressed', state)
        runtime.command(f'touch cancel 8 627 {runtime.now + 40}')
        state = runtime.command('sleep')
        assert state['power_state'] == 1 and page_position(state) == before_sleep, state
        state = runtime.command('pager 1')
        assert state['power_state'] == 1 and page_position(state) == before_sleep, state
        runtime.command('wake')
        state = runtime.settle(8)
        assert page_position(state) == before_sleep, state
        state = runtime.command('route 24')
        state = runtime.tap(100, 295)
        assert state['ble_reversed'], state
        runtime.capture('bluetooth-reversed', state)
        runtime.command('key 1 single')
        state = runtime.settle(8)
        position = page_position(state)
        state = runtime.command('pager 1')
        assert state['book_page'] == position[1] - 1, state
        state = runtime.command('pager -1')
        assert page_position(state) == position, state
        runtime.command('route 24')
        state = runtime.tap(180, 242)
        assert not state['ble_connected'] and state['ble_enabled'], state
        state = runtime.command('pager 1', ok=False)
        assert state['route'] == 24, state
        runtime.capture('bluetooth-disconnected', state)
        runtime.command('key 1 single')
        state = runtime.settle(8)
        position = page_position(state)
        state = runtime.command('pager 1', ok=False)
        assert page_position(state) == position, state
        runtime.command('route 24')
        state = runtime.tap(100, 100)
        assert not state['ble_enabled'] and not state['ble_connected'], state
        runtime.capture('bluetooth-off', state)
        runtime.command('pager -1', ok=False)
        state = runtime.tap(100, 100)
        assert state['ble_enabled'] and state['ble_connected'], state
        state = runtime.tap(100, 295)
        assert not state['ble_reversed'], state
        runtime.command('key 1 long')
        runtime.command('route 0')
        runtime.settle()
        # The TXT was opened most recently and remains first; EPUB is still the second item.
        state = runtime.tap(70, 420)
        state = runtime.settle(8)
        assert state['active_app'] == 'book' and state['book_kind'] == 'epub', state
        assert state['book_view'] == 'cover', state
        state = runtime.command('pager 1')
        assert state['book_view'] == 'reading', state
        position = page_position(state)
        state = runtime.command('pager 1')
        assert state['book_page'] == position[1] + 1, state
        state = runtime.command('pager -1')
        assert page_position(state) == position, state
        runtime.capture('epub-pager-turn', state)
        runtime.command('key 1 long')
        runtime.command('route 0')
        runtime.settle()
        state = runtime.style(2)
        assert state['ble_connected'] and stored_style(session) == 2, state
        runtime.command('route 24')
        state = runtime.tap(100, 295)
        assert state['ble_reversed'], state
        runtime.command('route 0')
        runtime.settle()
        runtime.close()
        state = runtime.start()
        runtime.command('route 0')
        state = runtime.settle()
        assert state['ble_enabled'] and state['ble_connected'] and state['ble_reversed'] and stored_style(session) == 2, state
        runtime.capture('restart-compact-and-remembered-pager', state)
        assert caches[0].stat().st_mtime_ns == cache_mtime and hashlib.sha256(caches[0].read_bytes()).hexdigest() == cache_digest
        state = runtime.tap(100, 180)
        state = runtime.settle(8)
        assert state['active_app'] == 'book' and state['book_kind'] == 'epub', state
        position = page_position(state)
        state = runtime.command('pager -1')
        assert state['book_page'] == position[1] + 1, state
        runtime.capture('restart-remembered-pager-reading', state)
        report = {'result': 'PASS', 'binary_sha256': hashlib.sha256(binary.read_bytes()).hexdigest(), 'session': str(session), 'synthetic_books_only': True, 'shelf_modes': 5, 'orientations': 2, 'refresh_checks': refresh_checks, 'cover_cache_reused': True, 'actual_txt_epub_pager': True, 'modal_selection_toc_tools_sleep_suppressed': True, 'corner_menu_suppressed': True, 'bluetooth_enable_scan_connect_reverse_disconnect_off': True, 'process_restart_style_and_bond': True, 'reversed_preference_survives_restart': True, 'physical_bluetooth_tested': False, 'physical_epd_tested': False, 'scenes': runtime.scenes, 'commands': len(runtime.transcript)}
        (output / 'report.json').write_text(json.dumps(report, ensure_ascii=False, indent=2) + '\n')
        print(json.dumps({'result': 'PASS', 'commands': len(runtime.transcript), 'scenes': len(runtime.scenes), 'output': str(output)}, ensure_ascii=False))
    finally:
        (session / 'transcript.json').write_text(json.dumps(runtime.transcript, ensure_ascii=False, indent=2) + '\n')
        runtime.close()


if __name__ == '__main__':
    main()

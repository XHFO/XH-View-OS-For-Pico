#!/usr/bin/env python3
"""封面书架 6/9 分页、真实九封面与触摸身份回归；仅使用私有合成 TF 会话。
Exercise the actual shelf controller with original barcode-cover EPUB fixtures, without hardware.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import struct
import tempfile
import zipfile
import zlib

from test_native_preview_shelf_ble import ROOT, PrefsHead, Runtime, write_png


def fixture_name(number):
    suffix = {9: '-A very long English book title about a quiet library and reading together',
              15: '-这是一本用于检查书架长标题省略和布局边界的合成图书',
              24: '-最后一格的中文长书名也应该完整保留封面和收藏按钮'}.get(number, '')
    return f'pico{number:02d}{suffix}.epub'


def cover_pixels(number):
    """Five binary columns plus inverted columns and three independent framing bands."""
    result = bytearray(128 * 192)
    for y in range(192):
        for x in range(128):
            if y < 24 or y >= 168:
                value = 0
            elif y < 48:
                value = 255
            else:
                bit = (number >> min(4, x * 5 // 128)) & 1
                value = 255 * (bit if y < 112 else 1 - bit)
            result[y * 128 + x] = value
    return bytes(result)


def fixture(books, number):
    """The unique identity belongs to the original image, filename and chapter text."""
    image = books / 'fixture-cover.png'
    write_png(image, 128, 192, cover_pixels(number))
    try:
        with zipfile.ZipFile(books / fixture_name(number), 'w', zipfile.ZIP_DEFLATED) as epub:
            epub.writestr('mimetype', 'application/epub+zip')
            epub.writestr('META-INF/container.xml', '<container><rootfiles><rootfile full-path="OPS/book.opf" media-type="application/oebps-package+xml"/></rootfiles></container>')
            epub.writestr('OPS/book.opf', f'<package xmlns="http://www.idpf.org/2007/opf" version="3.0"><metadata><title>PICO{number:02d}</title></metadata><manifest><item id="chapter" href="chapter.xhtml" media-type="application/xhtml+xml"/><item id="cover" href="cover.png" media-type="image/png" properties="cover-image"/></manifest><spine><itemref idref="chapter"/></spine></package>')
            epub.writestr('OPS/chapter.xhtml', f'<html><body><h1>PICO{number:02d} UNIQUE CHAPTER</h1>' + (f'<p>Original synthetic book {number:02d}. This private regression fixture contains no personal text.</p>' * 24) + '</body></html>')
            epub.writestr('OPS/cover.png', image.read_bytes())
    finally:
        image.unlink(missing_ok=True)


def px(value):
    return (value * 19 + 5) // 10


def geometry(page, landscape, index):
    """Acceptance geometry: six covers on page one; later pages have three columns/rows."""
    width = 640 if landscape else 360
    if page == 1:
        columns, top, row, cover_height = (6, 180, 152, 92) if landscape else (3, 294, 160, 100)
    else:
        columns, top, row, cover_height = (3, 128, 68, 58) if landscape else (3, 186, 142, 82)
    cell_width = (width - 32 - (columns - 1) * 8) // columns
    x, y = 16 + index % columns * (cell_width + 8), top + index // columns * row
    if page > 1 and landscape:
        return (x, y + 3, 36, cover_height), (x + 18, y + 25)
    return (x, y, cell_width, cover_height), (x + cell_width // 2, y + cover_height // 2)


def image_target(box):
    """Fit the 2:3 fixture to its promised cover bounds and sample well inside each stripe."""
    x, y, w, h = box
    pw, ph = w - 3, h - 3
    fitted = 192 * pw // 128
    if fitted > ph:
        pw = 128 * ph // 192
    else:
        ph = fitted
    bx, by = x + (w - pw - 2) // 2, y + (h - ph - 2) // 2
    iw, ih = pw - 4, ph - 4
    fitted = 192 * iw // 128
    if fitted > ih:
        iw = 128 * ih // 192
    else:
        ih = fitted
    return px(bx + 2 + (pw - 4 - iw) // 2), px(by + 2 + (ph - 4 - ih) // 2), px(iw), px(ih)


def star_position(page, landscape, index):
    columns = 6 if page == 1 and landscape else 3
    width = 640 if landscape else 360
    cell_width = (width - 32 - (columns - 1) * 8) // columns
    box, _ = geometry(page, landscape, index)
    x = 16 + index % columns * (cell_width + 8)
    if page > 1:
        y = (128 + index // 3 * 68 + 11) if landscape else (186 + index // 3 * 142 + 82 + 10)
    else:
        y = box[1] + box[3] + 10
    return x + cell_width - 22, y + 22


def decode_cover(frame, box):
    width, height, pixels = frame
    x, y, w, h = image_target(box)

    def sample(nx, ny):
        sx, sy = x + int(nx * w), y + int(ny * h)
        assert 1 <= sx < width - 1 and 1 <= sy < height - 1, (box, sx, sy, width, height)
        values = sorted(pixels[(sy + dy) * width + sx + dx] for dy in (-1, 0, 1) for dx in (-1, 0, 1))
        return values[4]

    anchors = [sample(.5, yy) for yy in (.06, .18, .94)]
    if not (anchors[0] < 40 and anchors[1] > 215 and anchors[2] < 40):
        return None
    number = 0
    for column in range(5):
        first, inverted = sample((column + .5) / 5, .42), sample((column + .5) / 5, .72)
        if not ((first < 40 and inverted > 215) or (first > 215 and inverted < 40)):
            return None
        number |= int(first > 215) << column
    return number


def stored_sort(session):
    paths = list(session.glob('tf/**/reader-prefs-v1.dat'))
    assert len(paths) == 1, paths
    raw = paths[0].read_bytes()
    magic, size, crc = struct.unpack_from('<III', raw)
    payload = raw[12:]
    assert magic == 0x31535058 and size == len(payload) and zlib.crc32(payload) == crc
    # PrefsHead ends at list; the actual serialized sort byte immediately follows it.
    return payload[PrefsHead.list.offset + 1]


def toolbar(page, landscape):
    if page > 1:
        return (156, 84, 150, 314, 414, 586) if landscape else (16, 137, 168, 192, 300, 306)
    return (16, 128, 160, 482, 586, 590) if landscape else (16, 245, 168, 192, 300, 306)


def key(runtime, letter, landscape):
    width, x, top, pitch = (404, 224, 136, 44) if landscape else (344, 8, 390, 48)
    step = width // 10
    for row, letters in enumerate(('qwertyuiop', 'asdfghjkl', 'zxcvbnm')):
        if letter in letters:
            return runtime.tap(x + (width - len(letters) * step) // 2 + letters.index(letter) * step + (step - 4) // 2,
                               top + row * pitch + (pitch - 4) // 2)
    raise AssertionError(letter)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', type=Path, required=True)
    parser.add_argument('--font', type=Path, default=ROOT / 'sdcard/fonts/MiSansVF.ttf')
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    binary, output = args.binary.resolve(), args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    os.environ.update(RPICO_WEREAD_OFFLINE='1', RPICO_HOME_LINK_OFFLINE='1')
    report = {'result': 'RUNNING', 'binary_sha256': hashlib.sha256(binary.read_bytes()).hexdigest(),
              'hardware_access': False, 'real_tf_access': False, 'network_access': False,
              'fixture': '24 original EPUBs with independently decodable binary-cover images', 'checks': [], 'opened': [], 'pages': []}
    try:
        for landscape in (False, True):
            label = 'landscape' if landscape else 'portrait'
            session = Path(tempfile.mkdtemp(prefix=label + '-', dir=output))
            books = session / 'tf/books'
            books.mkdir(parents=True)
            for number in range(1, 25):
                fixture(books, number)
            runtime = Runtime(binary, args.font.resolve(), session, output)
            try:
                runtime.start()
                runtime.command('rotate ' + str(int(landscape)))
                runtime.command('route 0')
                runtime.settle(45)
                runtime.style(0, landscape)
                # Use the actual unified shelf-style menu for alphabetical ordering.
                runtime.tap(530 if landscape else 230, 146 if landscape else 263)
                runtime.tap(430 if landscape else 260, 273 if landscape else 432)
                assert stored_sort(session) == 1, 'Could not select alphabetical shelf ordering'
                runtime.tap(2, 74)
                runtime.settle(30)

                def check(page, numbers, name, blanks=True):
                    state = runtime.settle(35)
                    assert state['active_app'] == 'system' and state['route'] == 0, state
                    frame = runtime.capture(label + '-' + name, state)
                    capacity = 6 if page == 1 else 9
                    decoded = [decode_cover(frame, geometry(page, landscape, index)[0]) for index in range(capacity)]
                    assert decoded[:len(numbers)] == list(numbers), (label, name, 'visible original cover identities', decoded, list(numbers), state)
                    if blanks:
                        assert all(number is None for number in decoded[len(numbers):]), (label, name, 'unexpected extra book', decoded)
                    report['pages'].append({'orientation': label, 'scene': name, 'page': page, 'covers': decoded, 'presents': state['presents']})
                    return state, frame

                check(1, range(1, 7), 'page1-six-original-layout')
                for page, numbers in ((2, range(7, 16)), (3, range(16, 25))):
                    runtime.swipe(landscape=landscape)
                    check(page, numbers, 'page' + str(page) + '-nine')
                before = runtime.command('status')
                runtime.swipe(landscape=landscape)
                after = runtime.settle(10)
                assert (after['presents'], after['revision']) == (before['presents'], before['revision']), 'Last-page boundary repainted or advanced'
                check(3, range(16, 25), 'last-page-boundary')
                for page, numbers in ((2, range(7, 16)), (1, range(1, 7))):
                    runtime.swipe(down=True, landscape=landscape)
                    check(page, numbers, 'page' + str(page) + '-back')
                before = runtime.command('status')
                runtime.swipe(down=True, landscape=landscape)
                after = runtime.settle(10)
                assert (after['presents'], after['revision']) == (before['presents'], before['revision']), 'First-page boundary repainted or advanced'
                report['checks'].append(label + ': consecutive 6/9/9 forward/back pages and both boundaries')

                # Open every later-page touch cell, including bottom-right book 15, through real touch.
                for number in range(7, 16):
                    runtime.command('key 1 long')
                    runtime.settle(30)
                    for _ in range(4):
                        runtime.swipe(down=True, landscape=landscape)
                    runtime.swipe(landscape=landscape)
                    check(2, range(7, 16), 'before-open-' + str(number))
                    x, y = geometry(2, landscape, number - 7)[1]
                    runtime.tap(x, y)
                    opened = runtime.settle(12)
                    assert opened['active_app'] == 'book' and opened['book_kind'] == 'epub', (label, number, opened)
                    runtime.capture(label + '-opened-' + str(number), opened)
                    runtime.command('key 1 long')
                    runtime.settle(30)
                    last = session / 'tf/books/.pico-system/progress-last.dat'
                    assert last.exists(), ('Actual reader did not persist the selected book identity', number)
                    saved = last.read_bytes()
                    magic, size, checksum = struct.unpack_from('<III', saved)
                    assert magic == 0x31535058 and size == len(saved) - 12 and zlib.crc32(saved[12:]) == checksum
                    identity = saved[12:].rstrip(b'\0').decode()
                    assert Path(identity).name == fixture_name(number), (label, number, identity)
                    report['opened'].append({'orientation': label, 'cell': number - 7, 'book': number, 'persisted_path': identity})
                report['checks'].append(label + ': all nine page-two cells open the matching actual EPUB')

                # The actual star touch rectangles must also match every displayed item, including row three.
                for _ in range(4):
                    runtime.swipe(down=True, landscape=landscape)
                runtime.swipe(landscape=landscape)
                check(2, range(7, 16), 'before-nine-favorites')
                for index in range(9):
                    state = runtime.tap(*star_position(2, landscape, index))
                    assert state['active_app'] == 'system' and state['route'] == 0, ('Star opened a book', label, index, state)
                tx, ty, tw, _, _, _ = toolbar(2, landscape)
                runtime.tap(tx + tw * 5 // 6, ty + 17)
                check(1, range(7, 13), 'nine-favorites-first-page')
                runtime.swipe(landscape=landscape)
                check(2, range(13, 16), 'nine-favorites-last-page')
                tx, ty, tw, _, _, _ = toolbar(2, landscape)
                runtime.tap(tx + tw // 6, ty + 17)
                check(1, range(1, 7), 'all-after-favorites')
                report['checks'].append(label + ': all nine star targets favorite the matching book without opening it')

                # Filtering must return to the original six-book first page, then allow a partial nine-slot page.
                for _ in range(4):
                    runtime.swipe(down=True, landscape=landscape)
                runtime.swipe(landscape=landscape)
                runtime.swipe(landscape=landscape)
                tx, ty, tw, _, _, _ = toolbar(3, landscape)
                runtime.tap(tx + tw // 2, ty + 17)
                check(1, range(7, 13), 'in-progress-filter-resets-page')
                runtime.swipe(landscape=landscape)
                check(2, range(13, 16), 'in-progress-partial-page')
                tx, ty, tw, _, _, _ = toolbar(2, landscape)
                runtime.tap(tx + tw // 6, ty + 17)
                check(1, range(1, 7), 'all-filter-resets-page')
                report['checks'].append(label + ': filter changes reset offset and preserve a partial last page')

                # Submit an actual keyboard query that matches all fixtures; a stale page offset is visible.
                runtime.swipe(landscape=landscape)
                runtime.swipe(landscape=landscape)
                _, _, _, _, _, sx = toolbar(3, landscape)
                state = runtime.tap(sx + 15, 100 if landscape else 105)
                assert state['route'] == 19, state
                runtime.tap(62 if landscape else 222, 251 if landscape else 293)
                key(runtime, 'p', landscape)
                runtime.tick(100)
                runtime.tap((640 if landscape else 360) - 46, 47)
                check(1, range(1, 7), 'search-submit-resets-page')
                report['checks'].append(label + ': actual keyboard search resets the cover-page offset')

                # Clear the one-letter query through the real delete key, then warm precisely books 7..15.
                _, _, _, _, _, sx = toolbar(1, landscape)
                runtime.tap(sx + 15, 100 if landscape else 105)
                runtime.tap(602 if landscape else 329, 244 if landscape else 508)
                runtime.tick(100)
                runtime.tap((640 if landscape else 360) - 46, 47)
                check(1, range(1, 7), 'search-cleared')
                runtime.swipe(landscape=landscape)
                before, _ = check(2, range(7, 16), 'cache-old-nine')
                cache_before = {p.name: (p.stat().st_mtime_ns, hashlib.sha256(p.read_bytes()).hexdigest()) for p in books.glob('.pico-covers/*.pcv')}
                # This is the LRU overlap counterexample: preserve A/B (7/8), replace seven other visible books.
                for number in range(9, 16):
                    (books / fixture_name(number)).unlink()
                for number in range(25, 32):
                    fixture(books, number)
                _, ty, _, _, rx, _ = toolbar(2, landscape)
                refreshed = runtime.tap(rx + 20, ty + 17)
                assert refreshed['presents'] == before['presents'], ('Refresh prematurely replaced the committed nine covers', label, refreshed)
                presents = before['presents']
                for _ in range(45):
                    refreshed = runtime.tick(100)
                    assert refreshed['presents'] <= presents + 1, ('Cover refresh committed more than once', label, refreshed)
                assert refreshed['presents'] == presents + 1, ('Changed shelf was not committed once', label, refreshed)
                after, _ = check(2, [7, 8, *range(16, 23)], 'cache-overlap-new-nine')
                unchanged_cache = 0
                for path in books.glob('.pico-covers/*.pcv'):
                    if path.name in cache_before:
                        assert (path.stat().st_mtime_ns, hashlib.sha256(path.read_bytes()).hexdigest()) == cache_before[path.name], 'Known disk thumbnail was unnecessarily rewritten'
                        unchanged_cache += 1
                assert unchanged_cache >= 9, unchanged_cache
                stable_frame = runtime.frame.read_bytes()
                idle = runtime.settle(30)
                assert (idle['presents'], idle['revision']) == (after['presents'], after['revision']), ('Idle cover shelf repeatedly refreshed', label, idle)
                assert runtime.frame.read_bytes() == stable_frame
                report['checks'].append(label + ': refresh retains two known plus seven newly loaded covers in one stable commit; cached files are unchanged')
                # Overlay redraw must retain all nine actual decoded cover images, including the ninth one.
                _, ty, _, style_x, _, _ = toolbar(2, landscape)
                runtime.tap(style_x + 30, ty + 17)
                runtime.capture(label + '-style-overlay-on-nine', runtime.command('status'))
                runtime.tap(4, 125 if landscape else 180)
                check(2, [7, 8, *range(16, 23)], 'overlay-preserves-nine-original-covers')
                report['checks'].append(label + ': style-overlay dismissal retains all nine real covers')
            finally:
                runtime.close()
                (output / ('transcript-' + label + '.json')).write_text(json.dumps(runtime.transcript, ensure_ascii=False, indent=2) + '\n')
        report['result'] = 'PASS'
    except BaseException as error:
        report['result'], report['error'] = 'FAIL', repr(error)
        raise
    finally:
        (output / 'report.json').write_text(json.dumps(report, ensure_ascii=False, indent=2) + '\n')
    print('Native shelf cover pagination PASS:', len(report['checks']), 'checks;', len(report['opened']), 'actual book opens;', len(report['pages']), 'pixel-verified page scenes')


if __name__ == '__main__':
    main()

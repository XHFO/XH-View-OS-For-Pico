#!/usr/bin/env python3
"""原创可导入图文EPUB：独立封面、正文封面、横竖图、多图及失败资源。
Original importable EPUBs: standalone/spine covers, varied illustrations and failed resources.
Only standard-library generation; never reads user books, browser sessions or devices.
"""
from pathlib import Path
import argparse
import hashlib
import json
import struct
import zipfile
import zlib


def png(width, height, kind=0):
    def chunk(tag, data):
        return struct.pack('>I', len(data)) + tag + data + struct.pack('>I', zlib.crc32(tag + data))
    def gray(x, y):
        if kind == 0:  # Original mountain/water drawing. / 原创山水几何图。
            if y > height * 3 // 4: return 130 if (y // 7) % 2 else 220
            peak = height // 5 + abs(x - width // 3) * height // max(width, 1)
            return 75 if y > peak else 245
        if kind == 1:  # Original portrait cover with a book and a window. / 原创书与窗封面。
            if x < 8 or y < 8 or x >= width - 8 or y >= height - 8: return 35
            if width//6 < x < width*5//6 and height//6 < y < height//2:
                return 90 if abs(x-width//2) < 3 or abs(y-height//3) < 3 else 210
            if width//5 < x < width*4//5 and height*2//3 < y < height*5//6: return 60 if x < width//2 else 130
            return 245
        return (x // 8 * 23 + y // 8 * 17) % 180 + 35
    pixels = b''.join(b'\0' + bytes(gray(x, y) for x in range(width)) for y in range(height))
    return b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', width, height, 8, 0, 0, 0, 0)) + chunk(b'IDAT', zlib.compress(pixels, 9)) + chunk(b'IEND', b'')


def jpeg(width, height):
    # Original constant-gray JPEG encoded with one-symbol Huffman tables. / 原创单色JPEG。
    def marker(tag, data): return b'\xff' + bytes([tag]) + struct.pack('>H', len(data) + 2) + data
    table = bytes([1]) + bytes(15) + b'\0'
    bits = ((width + 7) // 8) * ((height + 7) // 8) * 2
    stream = '0' * bits + '1' * (-bits % 8)
    entropy = bytes(int(stream[i:i+8], 2) for i in range(0, len(stream), 8)).replace(b'\xff', b'\xff\0')
    return b'\xff\xd8' + marker(0xdb, b'\0' + bytes([1])*64) + marker(0xc0, struct.pack('>BHHBBBB', 8, height, width, 1, 1, 0x11, 0)) + marker(0xc4, b'\0'+table+b'\x10'+table) + marker(0xda, bytes([1,1,0,0,63,0])) + entropy + b'\xff\xd9'


def make_fixture(path, cover_in_spine=False):
    images = {'cover.png': png(432,648,1), 'landscape.png': png(800,300), 'portrait.png': png(320,600,2),
              'small.png': png(96,64,2), 'flat.png': png(648,1), 'thin.png': png(1,1000),
              'baseline.jpg': jpeg(1000,1333), 'broken.png': png(200,120)[:33]}
    before = '<h1>第一章 窗前与山间</h1><p>清晨推开窗，远处的山和近处的书都在柔和的光线里。这是一份原创测试文字，用于验证图片在文字之间自动显示。</p>'
    chapters = [before+'<img src="images/landscape.png"/><p>横向山水图之后的文字应顺序接上，保留完整的图片比例。</p><img src="images/portrait.png"/><p>竖向插图之后，阅读继续。</p><img src="images/baseline.jpg"/><p>以上是本章全部原创图文。</p>',
                '<h1>第二章 小图画廊</h1>' + ''.join(f'<p>画廊记录 {i+1}</p><img src="images/small.png"/>' for i in range(72)),
                '<h1>第三章 边界与回退</h1><p>缺失或损坏图片不会阻塞阅读，也不会改变已有文字页码。</p><img src="images/broken.png"/><p>图片解码失败后仍然可以继续阅读和翻页。</p><img src="images/missing.png"/><p>以下两图分别极宽和极高。</p><img src="images/flat.png"/><img src="images/thin.png"/><p>测试结束。</p>']
    if cover_in_spine: chapters.insert(0, '<img src="images/cover.png"/>')
    items = ''.join(f'<item id="c{i}" href="c{i}.xhtml" media-type="application/xhtml+xml"/>' for i in range(len(chapters)))
    items += ''.join(f'<item id="img{i}" href="images/{name}" media-type="image/{"jpeg" if name.endswith("jpg") else "png"}"'+(' properties="cover-image"' if name == 'cover.png' else '')+'/>' for i,name in enumerate(images))
    spine = ''.join(f'<itemref idref="c{i}"/>' for i in range(len(chapters)))
    nav = ''.join(f'<li><a href="c{i}.xhtml">{("封面" if cover_in_spine and i == 0 else "图文章节 "+str(i+1))}</a></li>' for i in range(len(chapters)))
    resources = {'mimetype': b'application/epub+zip', 'META-INF/container.xml': b'<container><rootfiles><rootfile full-path="OPS/book.opf" media-type="application/oebps-package+xml"/></rootfiles></container>',
                 'OPS/book.opf': f'<package version="3.0" xmlns="http://www.idpf.org/2007/opf"><metadata xmlns:dc="http://purl.org/dc/elements/1.1/"><dc:title>窗前与山间：原创图文测试</dc:title><dc:language>zh-CN</dc:language><dc:identifier>urn:read-pico:original-image-fixture:{int(cover_in_spine)}</dc:identifier></metadata><manifest><item id="nav" href="nav.xhtml" media-type="application/xhtml+xml" properties="nav"/>{items}</manifest><spine>{spine}</spine></package>'.encode(),
                 'OPS/nav.xhtml': f'<html xmlns="http://www.w3.org/1999/xhtml" xmlns:epub="http://www.idpf.org/2007/ops"><body><nav epub:type="toc"><ol>{nav}</ol></nav></body></html>'.encode()}
    resources.update({f'OPS/c{i}.xhtml': ('<html xmlns="http://www.w3.org/1999/xhtml"><body>'+chapter+'</body></html>').encode() for i,chapter in enumerate(chapters)})
    resources.update({'OPS/images/'+name: data for name,data in images.items()})
    with zipfile.ZipFile(path, 'w') as archive:
        for name,data in resources.items():
            info = zipfile.ZipInfo(name, (2026,1,1,0,0,0)); info.create_system = 3; info.external_attr = 0o100644 << 16
            info.compress_type = zipfile.ZIP_STORED if name == 'mimetype' else zipfile.ZIP_DEFLATED
            archive.writestr(info, data)
    return {'file': path.name, 'bytes': path.stat().st_size, 'sha256': hashlib.sha256(path.read_bytes()).hexdigest(), 'cover_in_spine': cover_in_spine, 'chapters': len(chapters), 'original_content': True}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, default=Path(__file__).resolve().parents[1]/'build/native-preview-image-fixtures')
    args = parser.parse_args(); args.output.mkdir(parents=True, exist_ok=True)
    report = [make_fixture(args.output/'001-original-illustrated.epub'), make_fixture(args.output/'002-original-spine-cover.epub', True)]
    (args.output/'manifest.json').write_text(json.dumps(report, ensure_ascii=False, indent=2)+'\n')
    print(json.dumps(report, ensure_ascii=False))

if __name__ == '__main__': main()

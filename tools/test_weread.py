#!/usr/bin/env python3
"""中文：微信读书独立TF模块正常、故障及ASan/UBSan回归；仅合成快照，不访问设备。
English: Exercise the standalone WeRead module normally and under ASan/UBSan with synthetic snapshots only.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import struct
import subprocess
import zlib

from build_tool_packages import ROOT, build_packages

INDEX_BYTES = 16448
CHUNK_BYTES = 32768


def envelope(payload: bytes) -> bytes:
    return struct.pack('<III', 0x31535058, len(payload), zlib.crc32(payload)) + payload


def padded(value: str, size: int) -> bytes:
    encoded = value.encode('utf-8')
    assert len(encoded) < size
    return encoded.ljust(size, b'\0')


def index(records: list[tuple[str, str, str, int, int]], generation: int = 1) -> bytearray:
    data = bytearray(INDEX_BYTES)
    struct.pack_into('<8sIIIIIQI', data, 0, b'PWRIDX01', 1, INDEX_BYTES, generation, len(records),
                     sum(record[3] for record in records), 1792828800, 0)
    for slot, (book_id, title, author, notes, chunks) in enumerate(records):
        at = 64 + slot * 256
        data[at:at+64] = padded(book_id, 64)
        data[at+64:at+192] = padded(title, 128)
        data[at+192:at+240] = padded(author, 48)
        struct.pack_into('<II', data, at+240, notes, chunks)
    return data


def chunk(text: str | bytes, *, generation: int = 1, slot: int = 0, part: int = 0, count: int = 2) -> bytearray:
    encoded = text.encode('utf-8') if isinstance(text, str) else text
    assert 0 < len(encoded) <= CHUNK_BYTES-64
    data = bytearray(CHUNK_BYTES)
    struct.pack_into('<8sIIIIIIII', data, 0, b'PWRCHK01', 1, CHUNK_BYTES, generation, slot, part, count, len(encoded), 0)
    data[64:64+len(encoded)] = encoded
    return data


def fixtures(root: Path) -> None:
    root.mkdir(parents=True, exist_ok=True)
    records = [('b123', '三体', '刘慈欣', 270, 2), ('empty', '没有笔记的书', '', 0, 0)]
    records += [(f'book{i}', f'第{i}本书', '作者', 0, 0) for i in range(2, 9)]
    texts = [''.join(f'划线{i:04d}：保持每一条真实内容，不下载书籍正文。\n' for i in range(200)),
             ''.join(f'想法{i:04d}：离线查看个人想法，保留段落与标点。\n' for i in range(1000, 1070))]
    base = index(records)
    valid = root/'valid'
    valid.mkdir(exist_ok=True)
    (valid/'index.dat').write_bytes(envelope(base))
    for part, text in enumerate(texts):
        (valid/f'g00000001-b00-c{part:03x}.dat').write_bytes(envelope(chunk(text, part=part)))
    (root/'expected.txt').write_text(''.join(texts), encoding='utf-8')

    def case(name: str, *, idx: bytes | None = None, content: bytes | None = None) -> Path:
        destination = root/name
        if destination.exists():
            shutil.rmtree(destination)
        shutil.copytree(valid, destination)
        if idx is not None:
            (destination/'index.dat').write_bytes(envelope(idx))
        if content is not None:
            (destination/'g00000001-b00-c000.dat').write_bytes(envelope(content))
        return destination

    for name, offset, value in [('bad-index', 0, 0), ('bad-index-padding', 36, 1), ('bad-total', 24, 1),
                                 ('bad-id', 64, ord('/')), ('bad-title', 128, 0xff), ('bad-unused', 64+9*256, 1)]:
        data = base.copy()
        data[offset] = value
        case(name, idx=data)
    for name, offset, value in [('bad-count', 20, 65), ('bad-chunk-count', 64+244, 257),
                                 ('bad-note-count', 64+240, 0), ('bad-index-size', 12, INDEX_BYTES-1)]:
        data = base.copy()
        struct.pack_into('<I', data, offset, value)
        case(name, idx=data)
    data = base.copy()
    data[64+256:64+256+64] = data[64:128]
    case('duplicate-id', idx=data)
    bad_crc = case('bad-index-crc')/'index.dat'
    data = bytearray(bad_crc.read_bytes());data[-1] ^= 1;bad_crc.write_bytes(data)
    case('empty', idx=index([]))
    missing = case('missing-chunk');(missing/'g00000001-b00-c000.dat').unlink()
    invalid = chunk(texts[0]);invalid[64:68] = b'\xf0\x80\x80\x80';case('bad-utf8', content=invalid)
    invalid = chunk(texts[0]);invalid[64:67] = b'\xed\xa0\x80';case('bad-surrogate', content=invalid)
    invalid = chunk(texts[0]);invalid[64] = 0;case('bad-control', content=invalid)
    for name, offset, value in [('bad-chunk-generation', 16, 2), ('bad-chunk-slot', 20, 1),
                                 ('bad-chunk-index', 24, 1), ('bad-chunk-total', 28, 3),
                                 ('bad-chunk-padding', 36, 1), ('bad-text-count', 32, CHUNK_BYTES),
                                 ('bad-text-zero', 32, 0), ('bad-chunk-size', 12, CHUNK_BYTES-1)]:
        invalid = chunk(texts[0]);struct.pack_into('<I', invalid, offset, value);case(name, content=invalid)
    bad_crc = case('bad-chunk-crc')/'g00000001-b00-c000.dat'
    data = bytearray(bad_crc.read_bytes());data[-1] ^= 1;bad_crc.write_bytes(data)
    fallback = case('fallback')
    for name in ('index', 'g00000001-b00-c000'):
        (fallback/f'{name}.dat').rename(fallback/f'{name}.bak')
        (fallback/f'{name}.dat').write_bytes(b'truncated')
    case('full-block', idx=index([('full', '完整边界', '', 1, 1)]), content=chunk(b'A'*(CHUNK_BYTES-64), count=1))
    maximum = root/'maximum'
    maximum.mkdir(exist_ok=True)
    (maximum/'index.dat').write_bytes(envelope(index([(f'book{i}', f'最大书槽{i}', '', 256, 256) for i in range(64)])))
    for part in range(256):
        (maximum/f'g00000001-b3f-c{part:03x}.dat').write_bytes(envelope(chunk(f'最大块{part}', slot=63, part=part, count=256)))


def run(output: Path, external: Path | None = None) -> dict:
    output = output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    package = build_packages(output/'packages', app_ids=('weread',))
    fixture_root = output/'fixtures'
    fixtures(fixture_root)
    reports = []
    for mode in ('package', 'direct-sanitized'):
        binary = output/f'test-{mode}'
        flags = ['-std=gnu11', '-O1', '-g', '-Wall', '-Wextra', '-Werror']
        sources = [ROOT/'tools/test_weread_module.c', ROOT/'components/read_pico_apps/pico_app_package.c',
                   ROOT/'components/read_pico_apps/pico_app_loader.c']
        if mode == 'direct-sanitized':
            flags += ['-DWRS_DIRECT', '-fsanitize=address,undefined', '-fno-omit-frame-pointer']
            sources += [ROOT/'apps/src/weread.c']
        subprocess.run([os.environ.get('CC', 'cc'), *flags, '-I', str(ROOT/'components/read_pico_apps/include'),
                        *(str(source) for source in sources), '-o', str(binary)], check=True)
        command = [str(binary), str(output/'packages/weread.pga'), str(fixture_root)]
        result = subprocess.run(command, text=True, capture_output=True, check=True)
        print(result.stdout, end='')
        reports.append({'mode': mode, 'stdout': result.stdout.strip()})
        if external:
            result = subprocess.run([str(binary), str(output/'packages/weread.pga'), str(external.resolve()), '--external'],
                                    text=True, capture_output=True, check=True)
            print(result.stdout, end='')
            reports.append({'mode': mode+'-swift-fixture', 'stdout': result.stdout.strip()})
    result = {'schema': 1, 'package': package, 'tests': reports,
              'inputs': {str(path.relative_to(ROOT)): hashlib.sha256(path.read_bytes()).hexdigest() for path in
                         (ROOT/'apps/src/weread.c', ROOT/'apps/include/weread_snapshot.h',
                          ROOT/'apps/include/weread_explore.h', ROOT/'apps/include/weread_explore_json.h',
                          ROOT/'components/read_pico_apps/include/pico_app_sdk.h', ROOT/'tools/build_tool_packages.py',
                          ROOT/'tools/test_weread_module.c', ROOT/'tools/weread_explore_module_test.h', ROOT/'tools/test_weread.py')}}
    (output/'report.json').write_text(json.dumps(result, ensure_ascii=False, indent=2)+'\n', encoding='utf-8')
    return result


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, default=ROOT/'build/weread-tests')
    parser.add_argument('--fixture', type=Path)
    args = parser.parse_args()
    run(args.output, args.fixture)

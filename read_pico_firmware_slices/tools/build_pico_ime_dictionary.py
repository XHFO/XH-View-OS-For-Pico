#!/usr/bin/env python3
"""Build the optional TF lexicon from pinned MIT dictionaries; never embed it in APP.
从固定版本的 MIT 词典生成 TF 离线词库，不嵌入 APP。安装依赖：jieba==0.42.1 pypinyin==0.55.0。
"""
from pathlib import Path
import argparse
import hashlib
import importlib.metadata
import json
import math
import struct
import jieba
import pypinyin
from pypinyin import lazy_pinyin

ROOT = Path(__file__).resolve().parents[1]

def encode(entries):
    entries = sorted(entries, key=lambda row: (row[0], row[2], row[1]))
    data = bytearray()
    offsets = []
    for pinyin, text, rank in entries:
        py, utf = pinyin.encode('ascii'), text.encode('utf-8')
        offsets.append(len(data))
        data += bytes((len(py), len(utf), rank)) + py + b'\0' + utf + b'\0'
    digits = str.maketrans('abcdefghijklmnopqrstuvwxyz', '22233344455566677778889999')
    t9 = sorted(range(len(entries)), key=lambda index: (entries[index][0].translate(digits), entries[index][2], entries[index][1]))
    payload = struct.pack('<' + 'I' * len(offsets), *offsets)
    payload += struct.pack('<' + 'I' * len(offsets), *(offsets[index] for index in t9)) + data
    checksum = 2166136261
    for byte in payload:
        checksum = ((checksum ^ byte) * 16777619) & 0xffffffff
    assert len(entries) <= 65535 and len(payload) <= 2 * 1024 * 1024
    return b'PICOIME1' + struct.pack('<III12x', len(entries), len(payload), checksum) + payload

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, default=ROOT / 'assets/tf-pack/readpico/ime/pinyin-v1.bin')
    parser.add_argument('--phrases', type=int, default=32000)
    args = parser.parse_args()
    assert importlib.metadata.version('jieba') == '0.42.1'
    assert importlib.metadata.version('pypinyin') == '0.55.0'
    words = {}
    source = Path(jieba.__file__).with_name('dict.txt')
    for line in source.read_text().splitlines():
        text, frequency, *_ = line.split()
        if not 1 <= len(text) <= 8 or not all('\u3400' <= char <= '\u9fff' for char in text):
            continue
        words[text] = max(words.get(text, 0), int(frequency))
    chosen = sorted(((text, freq) for text, freq in words.items() if len(text) > 1), key=lambda row: (-row[1], row[0]))[:args.phrases]
    chosen += [(text, freq) for text, freq in words.items() if len(text) == 1]
    # GB2312常用/次常用字全覆盖，不能再以每音节8字截断。 / Retain the complete GB2312 character set rather than eight characters per syllable.
    for first in range(0xb0, 0xf8):
        for second in range(0xa1, 0xff):
            try:
                text = bytes((first, second)).decode('gb2312')
            except UnicodeDecodeError:
                continue
            if text not in words:
                chosen.append((text, 1))
    rows = []
    for text, frequency in chosen:
        pinyin = ''.join(lazy_pinyin(text, errors=lambda chars: list(chars))).lower().replace('ü', 'v')
        if not 1 <= len(pinyin) <= 39 or not all('a' <= char <= 'z' for char in pinyin):
            continue
        rank = max(0, min(31, 19 - int(math.log2(max(1, frequency)))))
        rows.append((pinyin, text, rank))
    content = encode(rows)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(content)
    report = {'format': 'PICOIME1', 'entries': len(rows), 'phrases': sum(len(row[1]) > 1 for row in rows), 'characters': sum(len(row[1]) == 1 for row in rows), 'bytes': len(content), 'sha256': hashlib.sha256(content).hexdigest(), 'sources': {'jieba': '0.42.1', 'pypinyin': '0.55.0'}, 'jieba_dict_sha256': hashlib.sha256(source.read_bytes()).hexdigest()}
    args.output.with_suffix('.json').write_text(json.dumps(report, indent=2, ensure_ascii=False) + '\n')
    print(json.dumps(report, ensure_ascii=False))

if __name__ == '__main__':
    main()

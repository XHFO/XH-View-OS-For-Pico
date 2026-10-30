#!/usr/bin/env python3
"""中文：只读检查产品及 main 界面字符串的三字体 cmap 覆盖，不生成或保存字体。
English: Read-only cmap audit of product/main UI literals; never generate or save fonts.
"""
from __future__ import annotations
import argparse
import hashlib
import json
from pathlib import Path
import re

import gen_product_fonts as product
from fontTools.ttLib import TTFont

TOKENS = re.compile(r'(?P<string>(?:u8|L|u|U)?"(?:\\.|[^"\\])*")|(?P<comment>//[^\n]*|/\*[\s\S]*?\*/)')
ESCAPES = {'a': 7, 'b': 8, 'f': 12, 'n': 10, 'r': 13, 't': 9, 'v': 11,
           '\\': 92, '"': 34, "'": 39, '?': 63}


def decode_c(literal: str) -> str:
    body = literal[literal.index('"') + 1:-1]
    out = bytearray()
    index = 0
    while index < len(body):
        char = body[index]
        index += 1
        if char != '\\':
            out.extend(char.encode('utf-8'))
            continue
        if index == len(body):
            break
        char = body[index]
        index += 1
        if char in ESCAPES:
            out.append(ESCAPES[char])
        elif char in '\r\n':
            if char == '\r' and index < len(body) and body[index] == '\n':
                index += 1
        elif char in 'uU':
            count = 4 if char == 'u' else 8
            out.extend(chr(int(body[index:index + count], 16)).encode('utf-8'))
            index += count
        elif char == 'x' or char in '01234567':
            start = index if char == 'x' else index - 1
            limit = len(body) if char == 'x' else min(start + 3, len(body))
            digits = '0123456789abcdefABCDEF' if char == 'x' else '01234567'
            index = start
            while index < limit and body[index] in digits:
                index += 1
            out.append(int(body[start:index], 16 if char == 'x' else 8) & 255)
        else:
            out.extend(char.encode('utf-8'))
    return out.decode('utf-8')


def sha(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def collect(root: Path, files: list[Path]) -> tuple[dict[int, list[dict]], list[dict]]:
    references: dict[int, list[dict]] = {}
    binary_literals = []
    for path in files:
        text = path.read_text(encoding='utf-8')
        for match in TOKENS.finditer(text):
            if not match.group('string'):
                continue
            location = {'path': str(path.relative_to(root)), 'line': text.count('\n', 0, match.start()) + 1}
            try:
                value = decode_c(match.group('string'))
            except (UnicodeError, ValueError):
                binary_literals.append(location)
                continue
            for codepoint in {ord(char) for char in value if not char.isspace() and ord(char) >= 32 and ord(char) != 127}:
                record = {**location, 'text': value[:96]}
                if record not in references.setdefault(codepoint, []):
                    references[codepoint].append(record)
    return references, binary_literals


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source-root', type=Path, default=Path(__file__).resolve().parents[1])
    parser.add_argument('--output', type=Path)
    parser.add_argument('--strict', action='store_true', help='Fail when a product UI literal is missing / 产品界面缺字时失败')
    args = parser.parse_args()
    root = args.source_root.resolve()
    product.ROOT = root
    product_files = product.ui_sources()
    main_files = sorted(path for path in (root / 'main').rglob('*')
                        if path.suffix in {'.c', '.h', '.inc'} and path.name not in product.UI_HEADER_EXCLUDES)
    all_files = sorted(set(main_files + product_files))
    scopes = {'product_ui': collect(root, product_files), 'all_main_and_product': collect(root, all_files)}
    report = {'schema': 1, 'read_only': True, 'comments_excluded': True,
              'note': 'All-main scope also includes protocol/engineering literals; product_ui matches the product font generator inputs.',
              'sources': {str(path.relative_to(root)): sha(path) for path in all_files}, 'fonts': {}}
    for name in ('builtin', 'serif', 'kai'):
        path = root / 'main/assets' / f'{name}.ttf'
        before = sha(path)
        font = TTFont(path, lazy=True)
        cmap = set(font.getBestCmap())
        font.close()
        assert sha(path) == before, 'Read-only font audit changed font bytes'
        audit = {'sha256': before, 'bytes': path.stat().st_size, 'cmap_codepoints': len(cmap), 'scopes': {}}
        for scope, (references, binary_literals) in scopes.items():
            missing = [{'codepoint': f'U+{codepoint:04X}', 'character': chr(codepoint), 'references': references[codepoint]}
                       for codepoint in sorted(references.keys() - cmap)]
            audit['scopes'][scope] = {'source_files': len(product_files if scope == 'product_ui' else all_files),
                                      'requested_codepoints': len(references), 'missing': missing,
                                      'non_utf8_literals_excluded': binary_literals}
        report['fonts'][name] = audit
        print(name, 'product missing:', ''.join(item['character'] for item in audit['scopes']['product_ui']['missing']) or 'none',
              '; all-main missing:', ''.join(item['character'] for item in audit['scopes']['all_main_and_product']['missing']) or 'none')
    missing = any(font['scopes']['product_ui']['missing'] for font in report['fonts'].values())
    report['result'] = 'MISSING_GLYPHS' if missing else 'PASS'
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(json.dumps(report, ensure_ascii=False, indent=2) + '\n')
    if args.strict and missing:
        raise SystemExit(1)


if __name__ == '__main__':
    main()

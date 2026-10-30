#!/usr/bin/env python3
"""中文：验证补字前所有GID、轮廓指令、度量与Unicode映射均完整保留，不改字体。
English: Verify original glyph IDs, contours/instructions, metrics and cmap survive additions unchanged.
"""
from __future__ import annotations
import argparse
import hashlib
import json
from pathlib import Path
from fontTools.ttLib import TTFont


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--before', type=Path, required=True)
    parser.add_argument('--source-root', type=Path, default=Path(__file__).resolve().parents[1])
    parser.add_argument('--output', type=Path)
    args = parser.parse_args()
    report = {'schema': 1, 'result': 'PASS', 'read_only': True, 'fonts': {}}
    total = 0
    for name in ('builtin', 'serif', 'kai'):
        before_path = args.before / f'{name}.ttf'
        after_path = args.source_root / 'main/assets' / f'{name}.ttf'
        before_hash, after_hash = digest(before_path), digest(after_path)
        before, after = TTFont(before_path), TTFont(after_path)
        old_order, new_order = before.getGlyphOrder(), after.getGlyphOrder()
        old_cmap, new_cmap = before.getBestCmap(), after.getBestCmap()
        assert new_order[:len(old_order)] == old_order, f'{name}: original glyph IDs/order changed'
        assert all(new_cmap.get(cp) == glyph for cp, glyph in old_cmap.items()), f'{name}: original cmap changed'
        outlines, metrics = [], []
        for glyph in old_order:
            if before['glyf'][glyph].compile(before['glyf']) != after['glyf'][glyph].compile(after['glyf']):
                outlines.append(glyph)
            if before['hmtx'][glyph] != after['hmtx'][glyph]:
                metrics.append(glyph)
        assert not outlines, f'{name}: original contours/hints changed: {outlines}'
        assert not metrics, f'{name}: original metrics changed: {metrics}'
        assert before['head'].unitsPerEm == after['head'].unitsPerEm
        for key in ('ascent', 'descent', 'lineGap'):
            assert getattr(before['hhea'], key) == getattr(after['hhea'], key), f'{name}: line metric {key} changed'
        before.close(); after.close()
        assert digest(before_path) == before_hash and digest(after_path) == after_hash
        total += after_path.stat().st_size
        report['fonts'][name] = {'before_sha256': before_hash, 'sha256': after_hash,
                                'bytes': after_path.stat().st_size,
                                'retained_glyph_ids': len(old_order), 'retained_codepoints': len(old_cmap),
                                'added_codepoints': [f'U+{cp:04X}' for cp in sorted(new_cmap.keys() - old_cmap.keys())],
                                'original_outlines_instructions_metrics_and_cmap_unchanged': True}
        print(f'{name}: retained {len(old_order)} glyph IDs, {len(old_cmap)} mappings; existing contours/hints/metrics unchanged')
    assert total < 1712 * 1024, 'Existing 1712 KiB product font budget exceeded'
    report['total_bytes'] = total
    report['budget_bytes'] = 1712 * 1024
    report['remaining_bytes'] = report['budget_bytes'] - total
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(json.dumps(report, ensure_ascii=False, indent=2) + '\n')


if __name__ == '__main__':
    main()

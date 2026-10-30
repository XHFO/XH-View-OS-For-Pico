#!/usr/bin/env python3
"""Real native reader/power regression with the experimental pairing disabled."""
from pathlib import Path
import argparse, hashlib, json, tempfile
from test_native_preview_shelf_ble import Runtime, page_position

ROOT = Path(__file__).resolve().parents[1]
p = argparse.ArgumentParser()
p.add_argument('--binary', type=Path, required=True)
p.add_argument('--output', type=Path, required=True)
a = p.parse_args()
a.output.mkdir(parents=True, exist_ok=True)
session = Path(tempfile.mkdtemp(prefix='single-', dir=a.output)).resolve()
books = session/'tf/books'
books.mkdir(parents=True)
book = books/'ordinary.txt'
book.write_text('第一章\n' + '普通阅读保留原有逐页翻页、方向和电源键锁屏功能。\n'*200)
digest = hashlib.sha256(book.read_bytes()).hexdigest()
r = Runtime(a.binary.resolve(), ROOT/'sdcard/fonts/MiSansVF.ttf', session, a.output.resolve())
checks = []
try:
    s = r.start()
    assert s['dual_role'] == 0
    r.command('route 0'); r.settle(15); r.tap(60, 330); s = r.settle(15)
    assert s['active_app'] == 'book' and s['book_page'] == 1
    s = r.command('key 2 single'); assert s['book_page'] == 2
    s = r.command('key 0 single'); assert s['book_page'] == 1
    checks.append('ordinary one-page navigation')
    before = page_position(s)
    r.command('powerkey')
    for _ in range(40):
        s = r.tick(50)
        if s['power_state'] == 1: break
    assert s['power_state'] == 1, s
    r.command('powerkey'); s = r.settle(10)
    assert not s['power_state'] and page_position(s) == before
    checks.append('short power lock/wake preserves real reading position')
    s = r.command('rotate 1'); r.settle(10)
    assert s['logical_width'] == 640 and not s['rotation_locked']
    checks.append('normal reading retains rotation')
    s = r.command('powerlong')
    assert s['route'] == 26 and not s['power_state'], s
    checks.append('long power opens original power menu')
    assert hashlib.sha256(book.read_bytes()).hexdigest() == digest
    checks.append('original book preserved')
    (a.output/'report.json').write_text(json.dumps({'passed': True, 'checks': checks,
        'binary_sha256': hashlib.sha256(a.binary.read_bytes()).hexdigest()}, ensure_ascii=False, indent=2)+'\n')
    print('PASS: '+', '.join(checks))
finally:
    r.close()
    (a.output/'transcript.json').write_text(json.dumps(r.transcript, ensure_ascii=False, indent=2)+'\n')

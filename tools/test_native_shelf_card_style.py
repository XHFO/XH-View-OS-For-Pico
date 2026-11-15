#!/usr/bin/env python3
"""Verify the shelf continue-reading card styles, picker and persistence."""
from pathlib import Path
import argparse
import hashlib
import json
import tempfile

from test_native_preview_shelf_ble import ROOT, Runtime


parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--binary", type=Path, required=True)
parser.add_argument("--output", type=Path, required=True)
args = parser.parse_args()
out = args.output.resolve()
out.mkdir(parents=True, exist_ok=True)


def root_rect(index, landscape):
    width = 640 if landscape else 360
    columns, rows, top, row_height = (2, 3, 146, 48) if landscape else (1, 6, 158, 60)
    cell_width = (width - 32 - (columns - 1) * 12) // columns
    return (16 + index // rows * (cell_width + 12), top + index % rows * row_height,
            cell_width, row_height)


def child_rect(index, landscape):
    width = 640 if landscape else 360
    columns, rows, top, row_height = (2, 3, 112, 60) if landscape else (1, 6, 126, 76)
    cell_width = (width - 32 - (columns - 1) * 12) // columns
    return (16 + index // rows * (cell_width + 12), top + index % rows * row_height,
            cell_width, row_height)


def option_rect(index, landscape):
    if landscape:
        panel = ((640 - 560) // 2, 20, 560, 320)
        width = (panel[2] - 52) // 2
        return (panel[0] + 20 + (index % 2) * (width + 12),
                panel[1] + 60 + (index // 2) * 112, width, 100)
    panel = (16, 76, 328, 490)
    return (panel[0] + 14, panel[1] + 64 + index * 88, panel[2] - 28, 78)


def center(rect):
    return rect[0] + rect[2] // 2, rect[1] + rect[3] // 2


session = Path(tempfile.mkdtemp(prefix="shelf-card-style-", dir=out))
books = session / "tf/books"
books.mkdir(parents=True)
(books / "001-card-style.txt").write_text(
    "第1章 卡片样式\n" + "合成测试文本，不包含私人图书内容。" * 800 + "\n第2章 继续阅读\n" + "用于验证续读卡书名和进度。" * 800)
(books / "002-next.txt").write_text("合成测试图书二\n" + "用于书架分页。" * 400)
runtime = Runtime(args.binary.resolve(), ROOT / "sdcard/fonts/MiSansVF.ttf", session, out)
checks = []
frames = {}
try:
    state = runtime.start()
    assert state["shelf_card_style"] == 0, state
    runtime.command("route 0")
    runtime.settle(30)
    state = runtime.tap(70, 350)
    assert state["active_app"] == "book", state
    runtime.command("route 0")
    runtime.settle(20)
    checks.append("default-card")
    for landscape in (False, True):
        suffix = "landscape" if landscape else "portrait"
        runtime.command("rotate " + str(int(landscape)))
        runtime.command("route 0")
        runtime.settle(8)
        for style, name in enumerate(("default", "boarding", "train", "cinema")):
            state = runtime.command("shelf-card-style " + str(style))
            assert state["shelf_card_style"] == style, state
            runtime.settle(5)
            _, _, pixels = runtime.capture(f"shelf-card-{name}-{suffix}", runtime.command("status"))
            frames[(suffix, style)] = hashlib.sha256(pixels).hexdigest()
        assert len(set(frames[(suffix, style)] for style in range(4))) == 4, suffix
        checks.append("four-distinct-cards-" + suffix)

    runtime.command("shelf-card-style 0")
    runtime.command("rotate 0")
    runtime.command("route 5")
    runtime.settle(8)
    runtime.tap(*center(root_rect(3, False)))
    runtime.settle(6)
    runtime.tap(*center(child_rect(5, False)))
    runtime.settle(6)
    runtime.capture("shelf-card-picker-portrait", runtime.command("status"))
    state = runtime.tap(*center(option_rect(3, False)))
    assert state["shelf_card_style"] == 3, state
    runtime.command("route 0")
    runtime.settle(5)
    runtime.capture("shelf-card-cinema-selected-portrait", runtime.command("status"))
    checks.append("picker-select-cinema")

    runtime.close()
    state = runtime.start()
    assert state["shelf_card_style"] == 3, state
    checks.append("card-style-persists")
    state = runtime.command("shelf-card-style 0")
    assert state["shelf_card_style"] == 0, state
    checks.append("restore-default-card")
finally:
    runtime.close()

(out / "report.json").write_text(json.dumps({
    "passed": True,
    "hardware_access": False,
    "checks": checks,
}, ensure_ascii=False, indent=2) + "\n")
print("PASS", checks)

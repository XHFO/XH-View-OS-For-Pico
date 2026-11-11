#!/usr/bin/env python3
"""Verify the device-level status icon appearance selector and persistence."""
from pathlib import Path
import argparse
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
    columns, rows, top, row_height = (2, 3, 112, 60) if landscape else (1, 5, 126, 76)
    cell_width = (width - 32 - (columns - 1) * 12) // columns
    return (16 + index // rows * (cell_width + 12), top + index % rows * row_height,
            cell_width, row_height)


def option_rect(index, landscape):
    if landscape:
        panel = ((640 - 520) // 2, 28, 520, 320)
        return (panel[0] + 24, panel[1] + 62 + index * 70, panel[2] - 48, 62)
    panel = (16, 76, 328, 490)
    return (panel[0] + 14, panel[1] + 64 + index * 92, panel[2] - 28, 80)


def center(rect):
    return rect[0] + rect[2] // 2, rect[1] + rect[3] // 2


session = Path(tempfile.mkdtemp(prefix="status-style-", dir=out))
runtime = Runtime(args.binary.resolve(), ROOT / "sdcard/fonts/MiSansVF.ttf", session, out)
checks = []


def assert_rounded_battery(pixels, width):
    # Portrait status battery: logical (302, 6), 19x10 at the preview's 1.9x scale.
    x, y = 574, 11
    corner = pixels[y * width + x]
    top = pixels[y * width + x + 18]
    assert corner > 180, corner
    assert top < 128, top


try:
    state = runtime.start()
    assert state["status_style"] == 0, state
    checks.append("default-line-minimal")
    for landscape in (False, True):
        runtime.command("rotate " + str(int(landscape)))
        runtime.command("route 0")
        runtime.settle(5)
        runtime.capture("status-line-" + ("landscape" if landscape else "portrait"), runtime.command("status"))
        runtime.command("route 5")
        runtime.settle(8)
        runtime.tap(*center(root_rect(3, landscape)))
        runtime.settle(8)
        runtime.tap(*center(child_rect(4, landscape)))
        runtime.settle(5)
        runtime.capture("status-style-picker-" + ("landscape" if landscape else "portrait"),
                        runtime.command("status"))
        state = runtime.tap(*center(option_rect(1, landscape)))
        runtime.settle(10)
        assert state["status_style"] == 1, state
        runtime.command("key 1 click")
        runtime.settle(6)
        runtime.command("route 0")
        runtime.settle(5)
        runtime.capture("status-book-" + ("landscape" if landscape else "portrait"),
                        runtime.command("status"))
        checks.append("book-style-" + ("landscape" if landscape else "portrait"))
        runtime.command("route 5")
        runtime.settle(6)
        runtime.tap(*center(root_rect(3, landscape)))
        runtime.settle(6)
        runtime.tap(*center(child_rect(4, landscape)))
        runtime.settle(4)
        state = runtime.tap(*center(option_rect(2, landscape)))
        runtime.settle(10)
        assert state["status_style"] == 2, state
        runtime.command("key 1 click")
        runtime.settle(6)
        runtime.command("route 0")
        runtime.settle(5)
        width_px, _, pixels = runtime.capture(
            "status-percent-" + ("landscape" if landscape else "portrait"),
            runtime.command("status"))
        if not landscape:
            assert_rounded_battery(pixels, width_px)
        checks.append("percent-style-" + ("landscape" if landscape else "portrait"))
        runtime.command("route 5")
        runtime.settle(6)
        runtime.tap(*center(root_rect(3, landscape)))
        runtime.settle(6)
        runtime.tap(*center(child_rect(4, landscape)))
        runtime.settle(4)
        state = runtime.tap(*center(option_rect(0, landscape)))
        runtime.settle(8)
        assert state["status_style"] == 0, state
    state = runtime.command("status-style 2")
    assert state["status_style"] == 2, state
    checks.append("command-select-percent")
    runtime.close()
    state = runtime.start()
    assert state["status_style"] == 2, state
    checks.append("percent-style-persists")
    state = runtime.command("status-style 0")
    assert state["status_style"] == 0, state
    checks.append("restore-default-line")
finally:
    runtime.close()

(out / "report.json").write_text(json.dumps({
    "passed": True,
    "hardware_access": False,
    "checks": checks,
}, ensure_ascii=False, indent=2) + "\n")
print("PASS", checks)

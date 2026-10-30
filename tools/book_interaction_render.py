#!/usr/bin/env python3
"""真实MiSans生产引擎、分页命中与数字跳页弹窗验证及灰阶预览；不访问设备。
Exercise real MiSans, layout hits and numeric-page drawing; previews are not physical-screen acceptance.
"""
from pathlib import Path
import argparse
import ast
import hashlib
import json
import os
import re
import shlex
import struct
import subprocess
import tempfile
import zlib

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("font", type=Path)
    parser.add_argument("--output", type=Path, default=ROOT / "build/book-interaction-preview")
    args = parser.parse_args()
    font = args.font.resolve()
    before = hashlib.sha256(font.read_bytes()).hexdigest()
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    snapshots = {name: (ROOT / name).read_text() for name in ("main/font/ttf_font.c", "main/book/book_layout.c", "main/book/book_layout.h", "main/apps/app_book.c", "main/ui/ui_kit.h", "main/ui/ui_kit.c", "components/epdiy/src/epdiy.c", "main/book/book_store.h")}
    engine = snapshots["main/font/ttf_font.c"]
    engine = re.sub(r'#include "(?:ttf_font.h|settings.h|esp_heap_caps.h|esp_log.h|esp_timer.h)"', "", engine)
    engine, count = re.subn(r'extern const uint8_t (builtin|serif|kai)_ttf_(start|end)\[\] asm\([^;]+;', r'static const uint8_t *\1_ttf_\2;', engine)
    assert count == 6
    # 提取实际边界/手柄/数字弹窗绘图；不复制阅读器状态机。/ Extract exact bounds/handles/modal drawing, never the reader state machine.
    tree = ast.parse((ROOT / "tools/test_pico_system.py").read_text())
    definition = next(n for n in tree.body if isinstance(n, ast.FunctionDef) and n.name == "function")
    namespace = {"re": re, "SOURCE": snapshots["main/apps/app_book.c"]}
    exec(compile(ast.Module(body=[definition], type_ignores=[]), "book-geometry-extractor", "exec"), namespace)
    names = ("reader_width", "reader_height", "reader_footer_top", "body_rect", "draw_selection_handle", "handle_hit", "progress_rect", "progress_back_rect")
    geometry = "\n".join(namespace["function"](name) for name in names)
    modal = "\n".join(namespace["function"](name) for name in ("copy_text", "fit_text", "page_jump_rect", "page_jump_key_rect", "page_jump_button_rect", "page_jump_control_at", "page_jump_control_draw", "page_jump_draw", "page_jump_status_rect", "page_jump_feedback"))
    index_type = re.search(r"typedef struct \{[^}]+\} page_index_t;", snapshots["main/apps/app_book.c"]).group()
    modal_constants = "\n".join(re.search(r"#define " + name + r"[^\n]+", snapshots["main/apps/app_book.c"]).group() for name in ("BOOK_PAGE_CONTROL", "BOOK_PAGE_DIGITS"))
    modal_constants += "\n" + re.search(r"#define BOOK_STORE_PATH_MAX[^\n]+", snapshots["main/book/book_store.h"]).group()
    primitives = "\n".join(namespace["function"](name, snapshots["main/ui/ui_kit.c"]) for name in ("clamp_radius", "draw_arc", "ui_draw_round_rect", "ui_fill_round_rect", "ui_rect_hit"))
    text_primitives = "\n".join(namespace["function"](name, snapshots["main/ui/ui_kit.c"]).replace(name + "(", "production_" + name + "(", 1) for name in ("ui_text", "ui_text_vc"))
    primitives = namespace["function"]("epd_fill_circle_helper", snapshots["components/epdiy/src/epdiy.c"]) + "\n" + primitives
    ui = snapshots["main/ui/ui_kit.h"]
    constants = "\n".join(re.search(r"#define " + name + r"[^\n]+", ui).group() for name in ("UI_MARGIN", "UI_GRAY_BLACK", "UI_GRAY_WHITE", "UI_GRAY_LIGHT", "UI_INK_BLACK", "UI_INK_WHITE", "UI_PX_BODY", "UI_PX_CAPTION", "UI_PX_BTN", "UI_BTN_RADIUS"))
    constants += "\n" + "\n".join(re.search(r"#define " + name + r"[^\n]+", snapshots["main/ui/ui_kit.c"]).group() for name in ("UI_SEL_INSET", "UI_SEL_RING"))
    with tempfile.TemporaryDirectory(prefix="pico-real-book-interaction-") as temporary:
        build = Path(temporary)
        (build / "ttf_font-under-test.c").write_text(engine)
        (build / "ttf_font.h").write_text((ROOT / "main/font/ttf_font.h").read_text())
        (build / "book_layout.c").write_text(snapshots["main/book/book_layout.c"])
        (build / "book_layout.h").write_text(snapshots["main/book/book_layout.h"])
        (build / "book-interaction-geometry.inc").write_text(constants + "\n" + geometry)
        (build / "book-text-primitives.inc").write_text(text_primitives)
        (build / "book-ui-primitives.inc").write_text(primitives)
        (build / "book-reading-header.inc").write_text(namespace["function"]("draw_compact_header"))
        (build / "book-page-modal.inc").write_text(modal_constants + "\n" + index_type + "\nstatic page_index_t s_page_index;\nstatic bool s_page_jump_waiting;\nstatic char s_page_jump_input[BOOK_PAGE_DIGITS + 1], s_page_jump_message[128];\n" + modal)
        (build / "epdiy.h").write_text("#pragma once\n#include <stdint.h>\ntypedef struct {int x,y,width,height;} EpdRect;\nenum EpdFontFlags {EPD_DRAW_ALIGN_LEFT=0,EPD_DRAW_ALIGN_CENTER=1,EPD_DRAW_ALIGN_RIGHT=2};\nvoid epd_draw_pixel(int,int,uint8_t,uint8_t*);\nvoid epd_fill_rect(EpdRect,uint8_t,uint8_t*);\nvoid epd_fill_circle(int,int,int,uint8_t,uint8_t*);\nint epd_rotated_display_width(void);\nint epd_rotated_display_height(void);\n")
        (build / "esp_err.h").write_text("#pragma once\ntypedef int esp_err_t;\n#define ESP_OK 0\n#define ESP_FAIL -1\n#define ESP_ERR_NO_MEM -2\n#define ESP_ERR_INVALID_RESPONSE -3\n#define ESP_ERR_NOT_FOUND -4\n#define ESP_ERR_INVALID_STATE -5\n")
        binary = build / "book-interaction"
        command = [os.environ.get("CC", "cc"), "-std=c11", "-Wall", "-Wextra", "-Werror", "-O1", "-g"]
        command += shlex.split(os.environ.get("CFLAGS", "-fsanitize=address,undefined"))
        command += ["-I" + str(build), "-I" + str(ROOT / "main/font"), "-I" + str(ROOT / "main/book"), "-I" + str(ROOT / "tools/book_layout_stubs"), str(ROOT / "tools/book_interaction_render.c"), str(build / "book_layout.c"), "-lm", "-o", str(binary)]
        subprocess.run(command, check=True)
        env = os.environ.copy()
        env["UBSAN_OPTIONS"] = env.get("UBSAN_OPTIONS", "") + ":halt_on_error=1"
        completed = subprocess.run([str(binary), str(font), str(output)], env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
        print(completed.stdout, end="")
        completed.check_returncode()
    assert hashlib.sha256(font.read_bytes()).hexdigest() == before, "Original font changed"
    assert all((ROOT / name).read_text() == contents for name, contents in snapshots.items()), "Source changed during validation; rerun after freeze"
    counts = re.search(r"(\d+) checks, (\d+) pages, (\d+) orientation/size/weight scenes passed", completed.stdout)
    assert counts, "Missing validation totals"
    modal_counts = re.search(r"page-jump modal: (\d+) checks, (\d+) real-font scenes passed", completed.stdout)
    assert modal_counts, "Missing numeric-modal validation totals"
    header_counts = re.search(r"compact reading header: (\d+) checks, (\d+) real-font scenes passed", completed.stdout)
    assert header_counts, "Missing chapter/page header validation totals"
    (output / "validation.log").write_text(completed.stdout)
    report = {"font_sha256": before, "real_production_font_engine": True, "physical_screen_acceptance": False,
              "checks": int(counts[1]), "pages": int(counts[2]), "scenes": int(counts[3]),
              "page_modal_checks": int(modal_counts[1]), "page_modal_scenes": int(modal_counts[2]),
              "reading_header_checks": int(header_counts[1]), "reading_header_scenes": int(header_counts[2]),
              "sources": {name: hashlib.sha256(contents.encode()).hexdigest() for name, contents in snapshots.items()}}
    (output / "source-evidence.json").write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps(report, indent=2))
    try:
        from PIL import Image
    except ImportError:
        Image = None
    for pgm in output.glob("*.pgm"):
        if Image:
            with Image.open(pgm) as image:
                image.save(pgm.with_suffix(".png"))
        else:
            # 标准库保留原始灰阶，不依赖额外图像软件。/ Keep exact grayscale using the standard library without image dependencies.
            signature, dimensions, maximum, pixels = pgm.read_bytes().split(b"\n", 3)
            width, height = map(int, dimensions.split())
            assert signature == b"P5" and maximum == b"255" and len(pixels) == width * height
            def chunk(kind, contents):
                return struct.pack(">I", len(contents)) + kind + contents + struct.pack(">I", zlib.crc32(kind + contents))
            scanlines = b"".join(b"\0" + pixels[row * width:(row + 1) * width] for row in range(height))
            png = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 0, 0, 0, 0))
            png += chunk(b"IDAT", zlib.compress(scanlines, 9)) + chunk(b"IEND", b"")
            pgm.with_suffix(".png").write_bytes(png)


if __name__ == "__main__":
    main()

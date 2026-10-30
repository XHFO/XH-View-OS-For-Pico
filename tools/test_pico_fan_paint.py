#!/usr/bin/env python3
"""用真实几何与四方向4bpp缓冲验证扇形局部推屏，不连接设备。
Test fan partial presentation using actual geometry and rotated packed 4bpp, without devices.
"""
from pathlib import Path
import ast
import os
import re
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def extractor(source):
    tree = ast.parse((ROOT / "tools/test_pico_system.py").read_text())
    definition = next(n for n in tree.body if isinstance(n, ast.FunctionDef) and n.name == "function")
    namespace = {"re": re, "SOURCE": source}
    exec(compile(ast.Module(body=[definition], type_ignores=[]), "paint-source-extractor", "exec"), namespace)
    return namespace["function"]


def prepare(build):
    source = (ROOT / "main/system/pico_system.c").read_text()
    extract = extractor(source)
    prefix = source[source.index("#define PS_VERSION"):source.index("static uint32_t crc32(")]
    fan_type = re.search(r"typedef struct\s*\{[^}]*\}\s*fan_item_t;", source).group()
    names = [
        "ps_view", "ps_route", "quick_canonical", "quick_catalog", "quick_enabled",
        "quick_slot_of", "quick_placed", "quick_seed_slots", "quick_import", "quick_defaults",
        "quick_edit_begin", "quick_edit_snapshot", "quick_edit_assign", "quick_unavailable",
        "fan_editor_active", "fan_frame_active", "fan_frame_selected", "fan_frame_side",
        "fan_scale", "fan_layout_slots", "fan_layout", "fan_pick", "fan_prompt_x",
        "render_fan_base", "render_fan_cursor", "render_fan",
        "fan_clip_area", "fan_add_area", "fan_cursor_area", "fan_area_pixels", "fast_damage_pixels",
        "fan_paint_key", "fan_present",
    ]
    # 共享出口必须实际调用被测函数，不能让只测辅助函数掩盖集成遗漏。
    # Both real outlets must dispatch to the tested helper, rather than testing an unconnected helper.
    for name in ["present", "ps_global_present"]:
        if "return fan_present(ctx)" not in extract(name):
            raise RuntimeError(f"{name} does not dispatch active fans through the shared painter")
    (build / "native-paint-functions.inc").write_text("#include <stdatomic.h>\n" + prefix + "\n" + fan_type + "\n" + "\n\n".join(extract(n) for n in names))

    epd = (ROOT / "components/epdiy/src/epdiy.c").read_text()
    ui = (ROOT / "main/ui/ui_kit.c").read_text()
    epd_extract, ui_extract = extractor(epd), extractor(ui)
    geometry = [
        '#include "ui_kit.h"', "#include <stdlib.h>", "#include <string.h>",
        "typedef struct { int x,y; } Coord_xy;",
        "#define _swap_int(a,b) do { int temporary=(a); (a)=(b); (b)=temporary; } while (0)",
        "#define UI_LOCK_WIDTH 1216", "#define UI_LOCK_HEIGHT 684",
        "#define display_rotation epd_get_rotation()",
    ]
    for name in ["_rotate", "epd_draw_pixel", "epd_draw_hline", "epd_draw_vline", "epd_fill_rect", "epd_write_line", "epd_draw_line", "epd_draw_circle", "epd_fill_circle_helper", "epd_get_pixel"]:
        geometry.append(epd_extract(name))
    for name in ["clamp_radius", "draw_arc", "ui_draw_round_rect", "ui_fill_round_rect", "ui_rotate_rect_to_fb", "ui_fill_rect_fast"]:
        geometry.append(ui_extract(name))
    (build / "native-paint-geometry.c").write_text("\n\n".join(geometry))

    epd_header = (ROOT / "tools/pico_reader_stubs/epdiy.h").read_text()
    rotation = re.search(r"enum EpdRotation\s*\{[^}]*\};", (ROOT / "components/epdiy/src/epdiy.h").read_text()).group()
    (build / "epdiy.h").write_text(epd_header + "\n" + rotation + r'''
typedef struct { int unused; } EpdiyHighlevelState;
typedef struct { int unused; } EpdWaveform;
enum EpdDrawError { EPD_DRAW_SUCCESS=0, EPD_DRAW_EMPTY_LINE_QUEUE=0x400 };
enum EpdDrawMode { MODE_DU=1, MODE_GC16=2, MODE_GL16=5 };
enum EpdRotation epd_get_rotation(void);
int epd_width(void);
int epd_height(void);
int epd_rotated_display_width(void);
int epd_rotated_display_height(void);
''')
    (build / "app.h").write_text(r'''
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "epdiy.h"
typedef struct { bool product_shell; } app_desc_t;
typedef struct { uint8_t *fb; EpdiyHighlevelState *hl; int64_t now_ms; } app_ctx_t;
typedef enum { APP_REDRAW_NONE, APP_REDRAW_AREA, APP_REDRAW_PAGE, APP_REDRAW_FULL, APP_REDRAW_DONE } app_redraw_t;
''')
    for name in ["ui_kit.h", "ttf_font.h"]:
        (build / name).write_text((ROOT / "tools/pico_reader_stubs" / name).read_text())


def main():
    with tempfile.TemporaryDirectory(prefix="pico-fan-paint-") as directory:
        build = Path(directory)
        prepare(build)
        command = [os.environ.get("CC", "cc"), "-std=gnu11", "-O1", "-g", "-Wall", "-Wextra", "-Werror", "-Wno-unused-function", "-Wno-unused-variable"]
        command += shlex.split(os.environ.get("CFLAGS", ""))
        for include in [build, ROOT / "tools/pico_reader_stubs", ROOT / "main/system"]:
            command += ["-I", str(include)]
        executable = build / "fan-paint-test"
        command += [str(ROOT / "tools/pico_fan_paint_host_test.c"), str(ROOT / "main/system/pico_draw.c"), str(build / "native-paint-geometry.c"), "-lm", "-o", str(executable)]
        subprocess.run(command, check=True)
        subprocess.run([str(executable)], check=True)


if __name__ == "__main__":
    main()

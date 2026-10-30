#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 mindreset
# SPDX-License-Identifier: Apache-2.0
"""真实变量字体流式回归；临时参考实例不写入发布文件。 / Real streaming VF regression with private temporary references."""
from pathlib import Path
import argparse
import hashlib
import os
import re
import shlex
import subprocess
import tempfile
from fontTools.ttLib import TTFont
from fontTools.varLib.instancer import instantiateVariableFont

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("font", type=Path)
    parser.add_argument("--weights", default="150,330,400,600,700")
    args = parser.parse_args()
    font_path = args.font.resolve()
    before = hashlib.sha256(font_path.read_bytes()).hexdigest()
    font = TTFont(font_path)
    cmap = font.getBestCmap()
    axes = [(x.axisTag, x.minValue, x.defaultValue, x.maxValue) for x in font["fvar"].axes]
    assert len(axes) == 1 and axes[0][0] == "wght"
    print("original sha256=" + before + " cmap=" + str(len(cmap)) + " axes=" + str(axes), flush=True)
    source = re.sub(r'#include "(?:ttf_font.h|settings.h|esp_heap_caps.h|esp_log.h|esp_timer.h)"', "", (ROOT / "main/font/ttf_font.c").read_text())
    source, count = re.subn(r'extern const uint8_t (builtin|serif|kai)_ttf_(start|end)\[\] asm\([^;]+;', r'static const uint8_t *\1_ttf_\2;', source)
    assert count == 6
    with tempfile.TemporaryDirectory(prefix="pico-variable-font-") as directory:
        work = Path(directory)
        (work / "ttf_font-under-test.c").write_text(source)
        (work / "ttf_font.h").write_text((ROOT / "main/font/ttf_font.h").read_text())
        (work / "epdiy.h").write_text("#pragma once\n#include <stdint.h>\nenum EpdFontFlags { EPD_DRAW_ALIGN_LEFT=0,EPD_DRAW_ALIGN_CENTER=1,EPD_DRAW_ALIGN_RIGHT=2 };\nvoid epd_draw_pixel(int,int,uint8_t,uint8_t*);\n")
        (work / "esp_err.h").write_text("#pragma once\ntypedef int esp_err_t;\n#define ESP_OK 0\n#define ESP_FAIL -1\n#define ESP_ERR_NO_MEM -2\n#define ESP_ERR_INVALID_RESPONSE -3\n#define ESP_ERR_NOT_FOUND -4\n#define ESP_ERR_INVALID_STATE -5\n")
        binary = work / "variable-font"
        subprocess.run([os.environ.get("CC", "cc"), "-std=c11", "-O1", "-g"] + shlex.split(os.environ.get("CFLAGS", "-fsanitize=address,undefined")) + ["-I" + str(work), "-I" + str(ROOT / "main/font"), str(ROOT / "tools/variable_font_host_test.c"), "-lm", "-o", str(binary)], check=True)
        points = work / "codepoints.txt"
        targeted = set(map(ord, "阅读世界你好字体测试小纸明月光床前疑是地上霜龘飝𠮷AaWi0123éàöïǘɒʕʢ，。！？‘’“”（）—…￥€"))
        targeted.update(cp for cp, name in cmap.items() if font["glyf"][name].isComposite())
        points.write_text("".join(f"{cp:x} 48\n" for cp in sorted(cmap)) + "".join(f"{cp:x} {px}\n" for cp in sorted(targeted & cmap.keys()) for px in range(12,81,4)))
        failed = False
        for weight in map(int, args.weights.split(",")):
            if weight == axes[0][2]:
                reference = font_path
            else:
                reference = work / f"reference-{weight}.ttf"
                print(f"creating private full-font reference {weight}", flush=True)
                instance = instantiateVariableFont(TTFont(font_path), {"wght": weight}, inplace=True)
                instance.save(reference)
                instance.close()
            for mode in (1,0):
                result = subprocess.run([str(binary), str(font_path), str(points), str(weight), str(reference), str(mode)])
                failed = failed or result.returncode != 0
        assert hashlib.sha256(font_path.read_bytes()).hexdigest() == before, "Original font changed"
        if failed:
            raise SystemExit(1)


if __name__ == "__main__":
    main()

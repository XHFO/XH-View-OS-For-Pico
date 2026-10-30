#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 mindreset
# SPDX-License-Identifier: Apache-2.0
"""实际固件字体打包与栅格化回归；不访问硬件、不修改生产源码。
Exercise the actual firmware font packer/rasterizer without hardware or source changes.
"""
from pathlib import Path
import hashlib
import os
import re
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def main():
    original = (ROOT / "main/font/ttf_font.c").read_text()
    # 仅替换硬件接口和嵌入资源地址，保留完整生产字体逻辑。 / Replace only hardware interfaces and embedded-resource addresses.
    source = re.sub(
        r'#include "(?:ttf_font.h|settings.h|esp_heap_caps.h|esp_log.h|esp_timer.h)"',
        "", original,
    )
    source, count = re.subn(
        r'extern const uint8_t (builtin|serif|kai)_ttf_(start|end)\[\] asm\([^;]+;',
        r'static const uint8_t *\1_ttf_\2;', source,
    )
    assert count == 6, "Embedded font declaration changed; review the host adapter"
    with tempfile.TemporaryDirectory(prefix="pico-native-font-audit-") as directory:
        work = Path(directory)
        (work / "ttf_font-under-test.c").write_text(source)
        (work / "ttf_font.h").write_text((ROOT / "main/font/ttf_font.h").read_text())
        (work / "epdiy.h").write_text(
            "#pragma once\n#include <stdint.h>\n"
            "enum EpdFontFlags { EPD_DRAW_ALIGN_LEFT=0,EPD_DRAW_ALIGN_CENTER=1,EPD_DRAW_ALIGN_RIGHT=2 };\n"
            "void epd_draw_pixel(int,int,uint8_t,uint8_t*);\n"
        )
        (work / "esp_err.h").write_text(
            "#pragma once\ntypedef int esp_err_t;\n#define ESP_OK 0\n#define ESP_FAIL -1\n"
            "#define ESP_ERR_NO_MEM -2\n#define ESP_ERR_INVALID_RESPONSE -3\n"
            "#define ESP_ERR_NOT_FOUND -4\n#define ESP_ERR_INVALID_STATE -5\n"
        )
        binary = work / "product-fonts"
        flags = shlex.split(os.environ.get("CFLAGS", "-fsanitize=address,undefined"))
        subprocess.run(
            [os.environ.get("CC", "cc"), "-std=c11", "-O1", "-g"] + flags
            + ["-I" + str(work), "-I" + str(ROOT / "main/font"),
               str(ROOT / "tools/product_fonts_host_test.c"), "-lm", "-o", str(binary)],
            check=True,
        )
        fonts = [ROOT / "main/assets" / (name + ".ttf") for name in ("builtin", "serif", "kai")]
        subprocess.run([str(binary)] + [str(font) for font in fonts], check=True)
        for font in fonts:
            print(font.name + " sha256=" + hashlib.sha256(font.read_bytes()).hexdigest())


if __name__ == "__main__":
    main()

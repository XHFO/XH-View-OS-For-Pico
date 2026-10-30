#!/usr/bin/env python3
"""校验无损资源、旧EPD逐像素方向结果和解码器失败原子性（ASan/UBSan）。
Verify lossless assets, real legacy EPD rotation output, and decoder failure atomicity (ASan/UBSan).
"""
from pathlib import Path
import os
import random
import re
import shlex
import subprocess
import tempfile
from generate_packed_images import packbits, packed, unpack

root = Path(__file__).resolve().parents[1]
subprocess.run(["python3", str(root / "tools/generate_packed_images.py"), "--check"], check=True)
rng = random.Random(721)
for length in [2,4,126,128,130,254,256,258,1024,4096]:
    for raw in [bytes([127]) * length, bytes(i % 256 for i in range(length)), bytes(rng.randrange(256) for _ in range(length)), bytes([1,1,2,2,2,3]) * (length // 6) + bytes(length % 6)]:
        assert unpack(packed(raw,2,len(raw))) == raw

# 从当前仓库抽取旧绘图函数作为回归基准，避免用新解码器自身当参考。
# Extract current legacy drawing functions as the regression oracle, rather than mirror the new decoder.
legacy = (root / "components/epdiy/src/epdiy.c").read_text()
names = ["epd_rotated_display_width", "epd_rotated_display_height", "_rotate", "epd_draw_pixel", "epd_copy_to_framebuffer", "epd_get_pixel", "draw_rotated_transparent_image", "epd_draw_rotated_image"]
functions = []
for name in names:
    match = re.search(r"^(?:static )?[A-Za-z_][A-Za-z_0-9 *]*\b" + re.escape(name) + r"\(", legacy, re.M)
    assert match, name
    start = legacy.index("{",match.start())
    depth = 1
    end = start + 1
    while depth:
        depth += (legacy[end] == "{") - (legacy[end] == "}")
        end += 1
    functions.append(legacy[match.start():end])

with tempfile.TemporaryDirectory(prefix="pico-packed-image-") as tmp:
    temp = Path(tmp)
    (temp / "legacy_image_functions.inc").write_text("\n\n".join(functions))
    executable = temp / "image-test"
    compiler = shlex.split(os.environ.get("CC", "cc"))
    command = compiler + ["-std=c11", "-O1", "-Wall", "-Wextra", "-Werror", "-fsanitize=address,undefined", "-fno-omit-frame-pointer", "-I", str(temp), "-I", str(root / "main/ui"), str(root / "main/ui/ui_image.c"), str(root / "tools/ui_image_host_test.c"), "-o", str(executable)]
    subprocess.run(command, check=True)
    for name in ["loading", "lock"]:
        subprocess.run([str(executable),str(root / f"main/assets/{name}_4bpp.bin"),str(root / f"main/assets/{name}_4bpp.p4b")],check=True)

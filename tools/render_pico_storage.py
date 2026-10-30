#!/usr/bin/env python3
"""Render the actual TF page with native drawing and real fonts; no card or serial access.
使用真实 TF 页面、原生绘图与字体生成截图；不访问存储卡或串口。
"""
from pathlib import Path
import argparse
import os
import shlex
import subprocess
import tempfile

from render_pico_reader import ROOT, prepare_native_renderer, renderer_flags


def prepare_stubs(build):
    (build / "sdmmc_cmd.h").write_text("#pragma once\ntypedef struct {int unused;} sdmmc_card_t;\n")
    (build / "freertos").mkdir()
    (build / "freertos/FreeRTOS.h").write_text("#pragma once\n#define pdPASS 1\n")
    (build / "freertos/task.h").write_text(
        "#pragma once\nint xTaskCreate(void (*fn)(void*),const char*,unsigned,void*,int,void*);\n"
        "void vTaskDelete(void*);\n"
    )
    (build / "esp_err.h").write_text(
        "#pragma once\ntypedef int esp_err_t;\n#define ESP_OK 0\n#define ESP_FAIL -1\n"
        "#define ESP_ERR_NO_MEM -2\n#define ESP_ERR_NOT_FINISHED -3\n"
    )


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, default=ROOT / "build/native-storage-preview")
    parser.add_argument("--font", type=Path, default=ROOT / "main/assets/builtin.ttf")
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="pico-storage-render-") as tmp:
        build = Path(tmp)
        prepare_stubs(build)
        sources = prepare_native_renderer(build)
        # 仅包装真实字形出口，收集实际墨迹边界；不复制页面排版或字体实现。
        # Wrap the real glyph output to collect actual ink bounds, without duplicating layout or font code.
        host = sources[0].read_text()
        signature = "void ttf_draw_text_px("
        assert host.count(signature) == 1
        traced_host = build / "storage-framebuffer.c"
        traced_host.write_text(
            host.replace(signature, "void storage_native_draw_text_px(", 1)
            + "\nbool storage_font_has_codepoint(uint32_t cp) { return stbtt_FindGlyphIndex(&host_font, (int)cp) != 0; }\n"
        )
        sources[0] = traced_host
        exe = build / "render-storage"
        command = [os.environ.get("CC", "cc"), "-I", str(build)] + renderer_flags()
        command += shlex.split(os.environ.get("CFLAGS", ""))
        command += ["-I", str(ROOT / "components/read_pico/include")]
        # 本地错误与任务桩优先；渲染器永不执行 worker 或格式化函数。
        # Prefer local error/task stubs; rendering never runs a worker or a formatter.
        command += [str(ROOT / "tools/pico_storage_render.c")]
        command += [str(path) for path in sources] + ["-lm", "-o", str(exe)]
        subprocess.run(command, cwd=build, check=True)
        result = subprocess.run([str(exe), str(args.font.resolve()), str(args.output.resolve())], cwd=build)

    try:
        from PIL import Image, ImageDraw
    except ImportError:
        print("PGM screenshots and geometry reports saved; Pillow unavailable.")
        raise SystemExit(result.returncode)
    for orientation in ("portrait", "landscape"):
        images = []
        for pgm in sorted(args.output.glob("*-" + orientation + ".pgm")):
            with Image.open(pgm) as image:
                image.save(pgm.with_suffix(".png"))
                thumb = image.copy()
                thumb.thumbnail((342, 608) if orientation == "portrait" else (608, 342))
                images.append((pgm.stem, thumb))
        columns = 4
        cellw, cellh = (370, 650) if orientation == "portrait" else (630, 385)
        sheet = Image.new("RGB", (columns * cellw, ((len(images) + columns - 1) // columns) * cellh), "#e7e7e7")
        draw = ImageDraw.Draw(sheet)
        for i, (name, image) in enumerate(images):
            x, y = (i % columns) * cellw, (i // columns) * cellh
            draw.text((x + 12, y + 10), name, fill="black")
            sheet.paste(image, (x + 12, y + 34))
        path = args.output / ("contact-" + orientation + ".png")
        sheet.save(path)
        print(path)
    # 失败截图仍然交付，退出状态保留碰撞/缺字检查结论。
    # Preserve screenshots on failure while retaining the collision/missing-glyph exit status.
    raise SystemExit(result.returncode)


if __name__ == "__main__":
    main()

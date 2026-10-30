#!/usr/bin/env python3
"""中文：同源导航编排双方向截图。/ English: Same-source navigation editor screenshots in both orientations."""
from pathlib import Path
import argparse
import os
import subprocess
import tempfile
from render_pico_reader import ROOT, prepare_native_renderer, renderer_flags

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--output", type=Path, default=ROOT / "build/native-navigation-preview")
args = parser.parse_args()
args.output.mkdir(parents=True, exist_ok=True)
with tempfile.TemporaryDirectory(prefix="pico-navigation-render-") as tmp:
    build = Path(tmp)
    sources = prepare_native_renderer(build)
    executable = build / "render-navigation"
    command = [os.environ.get("CC", "cc")] + renderer_flags() + [
        "-I", str(ROOT / "main/ui"), str(ROOT / "tools/pico_navigation_render.c"),
    ] + [str(p) for p in sources] + ["-lm", "-o", str(executable)]
    subprocess.run(command, cwd=build, check=True)
    subprocess.run([str(executable), str(ROOT / "main/assets/builtin.ttf"), str(args.output.resolve())], cwd=build, check=True)
from PIL import Image, ImageDraw
images = []
for pgm in sorted(args.output.glob("*.pgm")):
    with Image.open(pgm) as image:
        image.save(pgm.with_suffix(".png"))
        copy = image.copy()
        copy.thumbnail((342,608) if image.height > image.width else (608,342))
        images.append((pgm.stem,copy))
sheet = Image.new("RGB", (1010, len(images)//2*650), "#e7e7e7")
draw = ImageDraw.Draw(sheet)
portrait_row = landscape_row = 0
for name, image in images:
    landscape = image.width > image.height
    row = landscape_row if landscape else portrait_row
    x = 382 if landscape else 12
    y = row*650+34
    draw.text((x,y-22), name, fill="black")
    sheet.paste(image,(x,y))
    if landscape:landscape_row += 1
    else:portrait_row += 1
path = args.output / "contact.png"
sheet.save(path)
print(path)

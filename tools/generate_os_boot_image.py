#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Generate the branded, static 16-gray boot image without changing boot code.

The P4B header retains the panel-native 1216 x 684 dimensions. At boot the
existing UI_IMAGE_INVERTED_PORTRAIT decoder reads these raw bytes as a logical
684 x 1216 image, with the left pixel in the low nibble. Its mapping is
(x, y) -> (native_x=y, native_y=683-x). No image or panel decoder is changed.

By default only candidates are written under build/dev64-boot-brand. Pass
--asset-dir main/assets explicitly to install reviewed assets. --check only
compares deterministic in-memory output with the selected assets; it writes
nothing. Pillow and the repository's existing MiSans font are build tools,
never additional firmware dependencies.
"""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import re
import sys

from PIL import Image, ImageDraw, ImageFont, __version__ as PILLOW_VERSION

from compress_boot_image import compress_packed_image
from generate_packed_images import packed, unpack


ROOT = Path(__file__).resolve().parents[1]
LOGICAL_WIDTH, LOGICAL_HEIGHT = 684, 1216
NATIVE_WIDTH, NATIVE_HEIGHT = 1216, 684
MAX_OUTER_BYTES = 6747
# Five entries from the hardware's exact n * 17 gray ramp; no dithering.
GRAY_INDICES = (0, 3, 5, 10, 15)
PALETTE = tuple(value * 17 for value in GRAY_INDICES)


def read_identity(root: Path = ROOT) -> dict[str, str]:
    source = (root / "main/system/pico_system.c").read_text(encoding="utf-8")
    result = {}
    for field, macro in (("version", "PS_VERSION"), ("os_name", "PS_OS_NAME"),
                         ("release_label", "PS_RELEASE_LABEL")):
        match = re.search(r'^#define\s+' + macro + r'\s+"([^"\n]+)"\s*$', source, re.M)
        if not match:
            raise ValueError(f"missing literal {macro}")
        result[field] = match.group(1)
    return result


def image_to_raw(image: Image.Image) -> bytes:
    """Pack logical portrait pixels, low nibble first, without altering layout."""
    if image.mode != "L" or image.size != (LOGICAL_WIDTH, LOGICAL_HEIGHT):
        raise ValueError("boot image must be logical 684 x 1216 grayscale")
    pixels = image.tobytes()
    if any(value % 17 for value in pixels):
        raise ValueError("pixels are not exact hardware gray levels")
    return bytes((pixels[at] // 17) | ((pixels[at + 1] // 17) << 4)
                 for at in range(0, len(pixels), 2))


def raw_to_logical_image(raw: bytes) -> Image.Image:
    """Decode the production raw source to the upright boot-screen PNG pixels."""
    if len(raw) != NATIVE_WIDTH * NATIVE_HEIGHT // 2:
        raise ValueError("boot raw dimensions mismatch")
    pixels = bytearray(len(raw) * 2)
    for at, value in enumerate(raw):
        pixels[at * 2] = (value & 15) * 17
        pixels[at * 2 + 1] = (value >> 4) * 17
    return Image.frombytes("L", (LOGICAL_WIDTH, LOGICAL_HEIGHT), bytes(pixels))


def boot_native_framebuffer(raw: bytes) -> bytes:
    """Mirror only the fixed rotation-3 coordinate mapping for offline checks."""
    if len(raw) != NATIVE_WIDTH * NATIVE_HEIGHT // 2:
        raise ValueError("boot raw dimensions mismatch")
    framebuffer = bytearray([255]) * len(raw)
    for at, byte in enumerate(raw):
        for half in (0, 1):
            logical = at * 2 + half
            x, y = logical % LOGICAL_WIDTH, logical // LOGICAL_WIDTH
            nx, ny = y, NATIVE_HEIGHT - 1 - x
            index = ny * (NATIVE_WIDTH // 2) + nx // 2
            value = (byte >> (half * 4)) & 15
            framebuffer[index] = ((framebuffer[index] & 15) | (value << 4)
                                  if nx & 1 else (framebuffer[index] & 240) | value)
    return bytes(framebuffer)


def render_boot(identity: dict[str, str], font_path: Path) -> tuple[Image.Image, list[dict]]:
    image = Image.new("L", (LOGICAL_WIDTH, LOGICAL_HEIGHT), 255)
    draw = ImageDraw.Draw(image)
    layout = []

    def text(value: str, y: int, size: int, gray: int, weight: int) -> None:
        font = ImageFont.truetype(str(font_path), size)
        axes = font.get_variation_axes()
        if axes:
            font.set_variation_by_axes([weight if axis["name"] == b"Weight" else axis["default"]
                                        for axis in axes])
        bounds = draw.textbbox((LOGICAL_WIDTH // 2, y), value, font=font, anchor="mm")
        if bounds[0] < 40 or bounds[2] > LOGICAL_WIDTH - 40 or bounds[1] < 0 or bounds[3] > LOGICAL_HEIGHT:
            raise ValueError(f"text exceeds boot safe area: {value}: {bounds}")
        draw.text((LOGICAL_WIDTH // 2, y), value, font=font, fill=gray * 17, anchor="mm")
        layout.append({"text": value, "font_px": size, "weight": weight,
                       "gray_index": gray, "bounds": list(bounds)})

    # A quiet typographic identity; no decorative illustration or fake progress.
    text(identity["os_name"], 510, 44, 0, 600)
    draw.line((318, 562, 366, 562), fill=5 * 17, width=2)
    text(identity["release_label"], 608, 28, 3, 500)
    text(identity["version"], 657, 23, 5, 450)
    text("正在启动", 1024, 26, 3, 500)
    text("Starting up", 1067, 22, 5, 450)
    # Antialiasing is quantized to a subset of the same 16-level panel ramp.
    table = [min(PALETTE, key=lambda entry: abs(entry - value)) for value in range(256)]
    return image.point(table), layout


def generate(font_path: Path, root: Path = ROOT) -> tuple[bytes, bytes, Image.Image, dict]:
    identity = read_identity(root)
    image, layout = render_boot(identity, font_path)
    raw = image_to_raw(image)
    p4b = packed(raw, NATIVE_WIDTH, NATIVE_HEIGHT)
    if unpack(p4b) != raw:
        raise ValueError("P4B round-trip differs")
    outer = compress_packed_image(p4b)
    if len(outer) > MAX_OUTER_BYTES:
        raise ValueError(f"boot DEFLATE {len(outer)} exceeds {MAX_OUTER_BYTES}-byte budget")
    # The exported PNG comes from the final packed raw, never a parallel design.
    preview = raw_to_logical_image(unpack(p4b))
    if preview.tobytes() != image.tobytes():
        raise ValueError("preview differs from final raw")
    report = {
        "identity": identity, "logical_size": [LOGICAL_WIDTH, LOGICAL_HEIGHT],
        "p4b_header_size": [NATIVE_WIDTH, NATIVE_HEIGHT], "boot_rotation": 3,
        "boot_mapping": "native_x=logical_y; native_y=683-logical_x",
        "nibble_order": "left-low-right-high", "gray_indices": sorted({pixel // 17 for pixel in preview.tobytes()}),
        "raw_bytes": len(raw), "p4b_bytes": len(p4b), "outer_deflate_bytes": len(outer),
        "outer_budget_bytes": MAX_OUTER_BYTES, "outer_saved_bytes": MAX_OUTER_BYTES - len(outer),
        "raw_sha256": hashlib.sha256(raw).hexdigest(), "p4b_sha256": hashlib.sha256(p4b).hexdigest(),
        "logical_pixels_sha256": hashlib.sha256(preview.tobytes()).hexdigest(),
        "font_sha256": hashlib.sha256(font_path.read_bytes()).hexdigest(),
        "pillow_version": PILLOW_VERSION, "layout": layout,
        "progress_indicator": False,
    }
    return raw, p4b, preview, report


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--asset-dir", type=Path, default=ROOT / "build/dev64-boot-brand")
    parser.add_argument("--font", type=Path, default=ROOT / "sdcard/fonts/MiSansVF.ttf")
    parser.add_argument("--preview", type=Path, default=ROOT / "build/dev64-boot-brand/preview.png")
    parser.add_argument("--report", type=Path, default=ROOT / "build/dev64-boot-brand/report.json")
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()
    raw, p4b, preview, report = generate(args.font)
    assets = {"loading_4bpp.bin": raw, "loading_4bpp.p4b": p4b}
    if args.check:
        for name, content in assets.items():
            if (args.asset_dir / name).read_bytes() != content:
                raise ValueError(f"regenerate {args.asset_dir / name}")
    else:
        args.asset_dir.mkdir(parents=True, exist_ok=True)
        for name, content in assets.items():
            (args.asset_dir / name).write_bytes(content)
        args.preview.parent.mkdir(parents=True, exist_ok=True)
        preview.save(args.preview, optimize=True)
        report["preview"] = str(args.preview.resolve())
        report["asset_dir"] = str(args.asset_dir.resolve())
        args.report.parent.mkdir(parents=True, exist_ok=True)
        args.report.write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(report, ensure_ascii=False, indent=2))


if __name__ == "__main__":
    try:
        main()
    except (OSError, ValueError) as error:
        print(f"boot asset error: {error}", file=sys.stderr)
        raise SystemExit(1)

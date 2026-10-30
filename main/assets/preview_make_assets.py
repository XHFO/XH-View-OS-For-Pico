#!/usr/bin/env python3
"""将原创演示转换为无 TF 的固件资源。 / Build bounded, card-free demo assets."""

from __future__ import annotations

import argparse
import io
from pathlib import Path
import struct
import zipfile

from PIL import Image, ImageOps


ROOT = Path(__file__).resolve().parents[2]
OUT = Path(__file__).resolve().parent
WIDTH, HEIGHT, COUNT = 600, 848, 8


def encode_rle(raw: bytes) -> bytes:
    result = bytearray()
    at = 0
    while at < len(raw):
        end = at + 1
        while end < len(raw) and end - at < 255 and raw[end] == raw[at]:
            end += 1
        result.extend((end - at, raw[at]))
        at = end
    return bytes(result)


def decode_rle(packed: bytes) -> bytes:
    if len(packed) % 2:
        raise ValueError("RLE pair truncated")
    return b"".join(bytes((packed[i + 1],)) * packed[i] for i in range(0, len(packed), 2))


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--demos", type=Path, default=ROOT.parents[1] / "outputs/reader-preview/assets/reading-demos")
    args = parser.parse_args()
    text = (args.demos / "riverside-long.txt").read_text(encoding="utf-8")
    (OUT / "preview_riverside.txt").write_text(text, encoding="utf-8")
    pages, thumbnails = [], []
    with zipfile.ZipFile(args.demos / "rainy-bookshop.cbz") as archive:
        for page in range(1, COUNT + 1):
            name = f"page{page}.{'jpg' if page == COUNT else 'png'}"
            image = Image.open(io.BytesIO(archive.read(name))).convert("L")
            image = ImageOps.contain(image, (WIDTH, HEIGHT), Image.Resampling.LANCZOS)
            canvas = Image.new("L", (WIDTH, HEIGHT), 255)
            canvas.paste(image, ((WIDTH - image.width) // 2, (HEIGHT - image.height) // 2))
            mono = canvas.point(lambda value: 255 if value >= 190 else 0).convert("1")
            raw = mono.tobytes()
            assert len(raw) == WIDTH // 8 * HEIGHT
            packed = encode_rle(raw)
            assert decode_rle(packed) == raw
            pages.append(packed)
            thumbnails.append(mono.convert("RGB").resize((300, 424)))

    # 12 字节头 + 有界页偏移表，随后是 count/value 字节对。 / Header, bounded page offsets, then count/value byte pairs.
    header = struct.pack("<4sHHH2x", b"PVC1", WIDTH, HEIGHT, COUNT)
    offset = len(header) + COUNT * 8
    entries = []
    for page in pages:
        entries.append(struct.pack("<II", offset, len(page)))
        offset += len(page)
    result = header + b"".join(entries) + b"".join(pages)
    (OUT / "preview_comic.bin").write_bytes(result)
    sheet = Image.new("RGB", (4 * 300, 2 * 424), "white")
    for index, thumbnail in enumerate(thumbnails):
        sheet.paste(thumbnail, ((index % 4) * 300, (index // 4) * 424))
    sheet.save(OUT / "preview_comic_contact.png")
    print(f"Text: {len(text)} characters, {len(text.encode('utf-8'))} bytes")
    print(f"Comic: {COUNT} pages, {WIDTH}x{HEIGHT}, {len(result)} bytes total; decoded page {WIDTH // 8 * HEIGHT} bytes")


if __name__ == "__main__":
    main()

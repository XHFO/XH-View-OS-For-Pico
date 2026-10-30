#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 mindreset
# SPDX-License-Identifier: Apache-2.0
"""给原始启动/锁屏P4B套确定性的raw DEFLATE，不改图像；保留已有CLI和开机函数名。
Deterministically wrap original splash/lock P4B bytes in raw DEFLATE; preserve the existing CLI and boot function name.
"""
from pathlib import Path
import struct
import sys
import zlib


MAGIC = b"PICO4DF1"
MAX_IMAGE_BYTES = 64 * 1024


def compress_packed_image(image: bytes) -> bytes:
    if not 24 <= len(image) <= MAX_IMAGE_BYTES or image[:8] != b"PICO4BP1":
        raise ValueError("P4B header/size invalid")
    encoder = zlib.compressobj(level=9, wbits=-zlib.MAX_WBITS)
    stream = encoder.compress(image) + encoder.flush()
    if zlib.decompress(stream, -zlib.MAX_WBITS) != image:
        raise ValueError("P4B round-trip mismatch")
    return MAGIC + struct.pack("<II", len(image), zlib.crc32(image)) + stream


def compress_boot_image(image: bytes) -> bytes:
    """保留开机工具调用的兼容入口。/ Keep the existing boot-tool import compatible."""
    return compress_packed_image(image)


def main() -> None:
    if len(sys.argv) != 3:
        raise SystemExit("usage: compress_boot_image.py INPUT.p4b OUTPUT.deflate")
    destination = Path(sys.argv[2])
    destination.parent.mkdir(parents=True, exist_ok=True)
    destination.write_bytes(compress_boot_image(Path(sys.argv[1]).read_bytes()))


if __name__ == "__main__":
    main()

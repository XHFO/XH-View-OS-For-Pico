#!/usr/bin/env python3
"""无损压缩原4bpp启动/锁屏资源；--check只校验确定性输出和精确回读。
Losslessly pack original 4bpp splash/lock assets; --check verifies deterministic output and exact roundtrip.
"""
from pathlib import Path
import argparse
import struct
import zlib

MAGIC = b"PICO4BP1"
WIDTH, HEIGHT = 1216, 684


def packbits(raw: bytes) -> bytes:
    output = bytearray()
    at = 0
    while at < len(raw):
        run = 1
        while at + run < len(raw) and run < 128 and raw[at + run] == raw[at]:
            run += 1
        if run >= 3:
            output.extend((257 - run, raw[at]))
            at += run
            continue
        start = at
        at += run
        while at < len(raw) and at - start < 128:
            run = 1
            while at + run < len(raw) and run < 3 and raw[at + run] == raw[at]:
                run += 1
            if run >= 3:
                break
            at += min(run, 128 - (at - start))
        output.append(at - start - 1)
        output.extend(raw[start:at])
    return bytes(output)


def packed(raw: bytes, width=WIDTH, height=HEIGHT) -> bytes:
    if len(raw) != width * height // 2 or width % 2 or height % 2:
        raise ValueError("raw 4bpp dimensions mismatch")
    body = packbits(raw)
    return MAGIC + struct.pack("<HHIII", width, height, len(raw), len(body), zlib.crc32(raw)) + body


def unpack(data: bytes) -> bytes:
    if len(data) < 24 or data[:8] != MAGIC:
        raise ValueError("header")
    width, height, size, body_size, crc = struct.unpack_from("<HHIII", data, 8)
    if not width or not height or width % 2 or height % 2 or size != width * height // 2 or body_size != len(data) - 24:
        raise ValueError("dimensions/length")
    output = bytearray()
    at = 24
    while at < len(data):
        control = data[at]
        at += 1
        if control == 128:
            raise ValueError("reserved control")
        count = control + 1 if control < 128 else 257 - control
        needed = count if control < 128 else 1
        if at + needed > len(data) or len(output) + count > size:
            raise ValueError("bounds")
        output.extend(data[at:at + count] if control < 128 else bytes([data[at]]) * count)
        at += needed
    if len(output) != size or zlib.crc32(output) != crc:
        raise ValueError("raw length/CRC")
    return bytes(output)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()
    assets = Path(__file__).resolve().parents[1] / "main/assets"
    saved = 0
    for name in ["loading", "lock"]:
        source = assets / f"{name}_4bpp.bin"
        target = assets / f"{name}_4bpp.p4b"
        raw = source.read_bytes()
        data = packed(raw)
        assert unpack(data) == raw
        if args.check:
            assert target.read_bytes() == data, f"regenerate {target.name}"
        else:
            target.write_bytes(data)
        saved += len(raw) - len(data)
        print(f"{target.name}: {len(raw)} -> {len(data)} bytes; exact roundtrip")
    print(f"total saved: {saved} bytes ({saved / 1024:.1f} KiB)")


if __name__ == "__main__":
    main()

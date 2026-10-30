#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 mindreset
# SPDX-License-Identifier: Apache-2.0
"""生成正常/损坏 ZIP 并运行解析器。/ Generate valid/malformed ZIPs and exercise the parser."""
import argparse
import io
from pathlib import Path
import struct
import subprocess
import tempfile
import warnings
import zipfile

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--sanitize", action="store_true", help="enable AddressSanitizer and UndefinedBehaviorSanitizer")
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix="zip-reader-") as temp:
        work = Path(temp)
        exe = work / "test"
        flags = ["-fsanitize=address,undefined", "-fno-omit-frame-pointer"] if args.sanitize else []
        subprocess.run(["cc", "-std=c11", "-Wall", "-Wextra", "-Werror", "-g", *flags,
                        "-I" + str(ROOT / "tools/zip_host_stubs"), "-I" + str(ROOT / "main/book"),
                        str(ROOT / "tools/zip_reader_host_test.c"), str(ROOT / "main/book/zip_reader.c"),
                        "-lz", "-o", str(exe)], check=True)
        cases = 0

        def run(mode, data, name="entry.txt", expected=None):
            nonlocal cases
            path = work / "test.zip"
            path.write_bytes(data)
            command = [str(exe), mode, str(path), name]
            if expected is not None:
                payload = work / "expected"
                payload.write_bytes(expected)
                command.append(str(payload))
            subprocess.run(command, check=True)
            cases += 1

        def archive(content, method=zipfile.ZIP_DEFLATED, comment=b"", stream=False):
            class Streaming(io.BytesIO):
                def seekable(self): return False
                def seek(self, *args): raise OSError("stream")
            buffer = Streaming() if stream else io.BytesIO()
            with zipfile.ZipFile(buffer, "w", compression=method) as z:
                z.writestr("entry.txt", content)
                z.comment = comment
            return buffer.getvalue()

        payload = ("中文文本\n" * 1000).encode()
        for method in (zipfile.ZIP_STORED, zipfile.ZIP_DEFLATED):
            for data in (b"", b"hello", payload, bytes(range(256)) * 100):
                run("read", archive(data, method), expected=data)
        # bit4只是旧编码提示，不能绕过方法、双头一致性和完整性检查。
        # Bit 4 is a legacy hint, never a bypass for method, matching-header or integrity checks.
        for method in (zipfile.ZIP_STORED, zipfile.ZIP_DEFLATED):
            bit4 = bytearray(archive(payload, method))
            bit4_cd = bit4.index(b"PK\x01\x02")
            struct.pack_into("<H", bit4, 6, 0x0010)
            struct.pack_into("<H", bit4, bit4_cd + 8, 0x0010)
            run("read", bit4, expected=payload)
            # 实际书籍中的组合提示位也保持原始内容和CRC。/ Preserve content and CRC for the combined hints seen in real books.
            combined = bytearray(bit4)
            struct.pack_into("<H", combined, 6, 0x0816)
            struct.pack_into("<H", combined, bit4_cd + 8, 0x0816)
            run("read", combined, expected=payload)

            central_only = bytearray(bit4)
            struct.pack_into("<H", central_only, 6, 0)
            run("reject-extract", central_only)
            local_only = bytearray(bit4)
            struct.pack_into("<H", local_only, bit4_cd + 8, 0)
            run("reject-extract", local_only)

            # 加密及其余禁止位逐位检查，避免把允许一个提示位扩大为放行其它位。
            # Check encryption and every other forbidden bit individually to keep the compatibility exception narrow.
            for forbidden in (0x0001, 0x0020, 0x0040, 0x0080, 0x0100, 0x0200,
                              0x0400, 0x1000, 0x2000, 0x4000, 0x8000):
                unsafe = bytearray(bit4)
                struct.pack_into("<H", unsafe, 6, 0x0010 | forbidden)
                struct.pack_into("<H", unsafe, bit4_cd + 8, 0x0010 | forbidden)
                run("reject-open", unsafe)

            unsupported = bytearray(bit4)
            struct.pack_into("<H", unsupported, 8, 9)
            struct.pack_into("<H", unsupported, bit4_cd + 10, 9)
            run("reject-open", unsupported)

            damaged = bytearray(bit4)
            name_len, extra_len = struct.unpack_from("<HH", damaged, 26)
            data_pos = 30 + name_len + extra_len
            if method == zipfile.ZIP_STORED:
                damaged[data_pos] ^= 0x80
                run("reject-crc", damaged)
            else:
                # raw DEFLATE 的保留块类型必须被拒绝。/ Reserved raw-DEFLATE block types must still fail.
                damaged[data_pos] = (damaged[data_pos] & ~0x06) | 0x06
                run("reject-extract", damaged)

            wrong_crc = bytearray(bit4)
            crc = struct.unpack_from("<I", wrong_crc, bit4_cd + 16)[0] ^ 1
            struct.pack_into("<I", wrong_crc, 14, crc)
            struct.pack_into("<I", wrong_crc, bit4_cd + 16, crc)
            run("reject-crc", wrong_crc)
        run("read", archive(payload, stream=True), expected=payload)
        run("read", archive(payload, comment=b"x" * 65535), expected=payload)
        run("read", archive(b"x" * (2 * 1024 * 1024)), expected=b"x" * (2 * 1024 * 1024))
        run("reject-open", archive(b"x" * (4 * 1024 * 1024 + 1)))
        good = archive(payload)
        cd = good.index(b"PK\x01\x02")
        eocd = good.rindex(b"PK\x05\x06")

        def changed(offset, fmt, value):
            data = bytearray(good)
            struct.pack_into(fmt, data, offset, value)
            return data

        for offset, fmt, value in ((cd + 8, "<H", 1), (cd + 10, "<H", 99),
                                   (cd + 20, "<I", 0xffffffff), (cd + 24, "<I", 0xffffffff),
                                   (cd + 42, "<I", cd), (cd + 34, "<H", 1),
                                   (eocd + 4, "<H", 1), (eocd + 10, "<H", 513),
                                   (eocd + 16, "<I", 0xffffffff), (cd + 28, "<H", 1025)):
            run("reject-open", changed(offset, fmt, value))
        for offset, fmt, value in ((0, "<I", 0), (8, "<H", 0), (26, "<H", 500),
                                   (28, "<H", 65535), (14, "<I", 0), (30, "<B", 33)):
            run("reject-extract", changed(offset, fmt, value))
        damaged = bytearray(good)
        damaged[40] ^= 0xff
        run("reject-extract", damaged)
        wrong_crc = changed(cd + 16, "<I", 42)
        struct.pack_into("<I", wrong_crc, 14, 42)
        run("reject-extract", wrong_crc)
        too_small = changed(cd + 24, "<I", 1)
        struct.pack_into("<I", too_small, 22, 1)
        run("reject-extract", too_small)
        run("reject-extract", changed(18, "<I", 0xffffffff))
        run("reject-open", good[:-1])
        run("reject-open", b"not a ZIP")
        buffer = io.BytesIO()
        with zipfile.ZipFile(buffer, "w"): pass
        run("empty", buffer.getvalue())
        for count in (512, 513, 2048, 2049, 4096, 4097, 32768, 32769):
            buffer = io.BytesIO()
            with zipfile.ZipFile(buffer, "w") as z:
                for i in range(count): z.writestr(f"entry{i}.txt", b"")
            run("empty" if count <= 32768 else "reject-open", buffer.getvalue())
        # ZIP64 扩展不依赖哨兵值也必须拒绝。/ Reject ZIP64 extras even without sentinel sizes.
        buffer = io.BytesIO()
        with zipfile.ZipFile(buffer, "w") as z:
            info = zipfile.ZipInfo("entry.txt")
            info.extra = struct.pack("<HHQQ", 1, 16, 0, 0)
            z.writestr(info, b"")
        run("reject-open", buffer.getvalue())
        buffer = io.BytesIO()
        with warnings.catch_warnings():
            warnings.simplefilter("ignore", UserWarning)
            with zipfile.ZipFile(buffer, "w") as z:
                z.writestr("entry.txt", b"one")
                z.writestr("entry.txt", b"two")
        run("reject-open", buffer.getvalue())
        # FNV-1a 冲突仍按完整路径区分。/ Distinguish full paths despite an FNV-1a collision.
        buffer = io.BytesIO()
        with zipfile.ZipFile(buffer, "w") as z:
            z.writestr("costarring", b"first")
            z.writestr("liquid", b"second")
        run("read", buffer.getvalue(), name="costarring", expected=b"first")
        run("read", buffer.getvalue(), name="liquid", expected=b"second")
        buffer = io.BytesIO()
        with zipfile.ZipFile(buffer, "w", compression=zipfile.ZIP_DEFLATED) as z:
            z.writestr("正文/第一章.xhtml", payload)
        run("read", buffer.getvalue(), name="正文/第一章.xhtml", expected=payload)
        build = "ASan/UBSan" if args.sanitize else "ordinary"
        print(f"ZIP parser: {cases} normal, limit and malformed cases passed ({build}; host zlib inflate adapter)")


if __name__ == "__main__":
    main()

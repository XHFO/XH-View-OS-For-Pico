#!/usr/bin/env python3
"""复跑原生阅读演示的主机逻辑检查，不代表实机验收。

Run native preview host logic checks. Font advances and UI are mocks: reported
page counts do not establish device pagination, glyph quality or performance.
Real book_layout.c and the actual embedded text/comic assets are exercised.
Only temporary build files are written; firmware sources/assets stay unchanged.
"""

from __future__ import annotations

import argparse
import os
from pathlib import Path
import shlex
import shutil
import subprocess
import sys
import tempfile


ROOT = Path(__file__).resolve().parents[1]


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cc", default=os.environ.get("CC", "cc"), help="C compiler (default: CC or cc)")
    parser.add_argument("--sanitize", action="store_true", help="enable AddressSanitizer and UndefinedBehaviorSanitizer")
    args = parser.parse_args()
    compiler = shlex.split(args.cc)
    if not compiler or not shutil.which(compiler[0]):
        parser.error("C compiler not found; install a host C toolchain or set --cc")

    with tempfile.TemporaryDirectory(prefix="read-pico-preview-host-") as directory:
        build = Path(directory)
        shutil.copyfile(ROOT / "main/assets/preview_riverside.txt", build / "text.bin")
        shutil.copyfile(ROOT / "main/assets/preview_comic.bin", build / "comic.bin")
        section = ".section __TEXT,__const" if sys.platform == "darwin" else ".section .rodata"
        assembly = section + "\n"
        for asset, stem in (("text.bin", "preview_riverside_txt"), ("comic.bin", "preview_comic_bin")):
            assembly += (
                f".global _binary_{stem}_start\n_binary_{stem}_start:\n"
                f'.incbin "{asset}"\n'
                f".global _binary_{stem}_end\n_binary_{stem}_end:\n"
            )
        if sys.platform.startswith("linux"):
            assembly += '.section .note.GNU-stack,"",@progbits\n'
        (build / "assets.S").write_text(assembly, encoding="utf-8")

        command = compiler + ["-std=gnu11", "-Wall", "-Wextra", "-Werror", "-g"]
        if args.sanitize:
            command += ["-fsanitize=address,undefined", "-fno-omit-frame-pointer"]
        for include in ("tools/preview_reading_stubs", "main/ui", "main/book", "main/app"):
            command += ["-I", str(ROOT / include)]
        executable = build / "preview-reading-test"
        command += [str(ROOT / "tools/preview_reading_host_test.c"),
                    str(ROOT / "main/book/book_layout.c"), "assets.S", "-o", str(executable)]
        subprocess.run(command, cwd=build, check=True)
        subprocess.run([str(executable)], cwd=build, check=True)


if __name__ == "__main__":
    main()

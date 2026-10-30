#!/usr/bin/env python3
"""宿主检查首页路由、绘制边界与时钟节流。 / Host-check home routes, bounds, and clock cadence.

Run: python3 tools/test_preview_home.py
Requires a C11 compiler; no ESP-IDF, serial port, or Python packages are needed.
UI/font/PMU services are test doubles. This does not validate real glyphs,
touch hardware, display refresh, sleep, networking, or physical device behavior.
"""

from __future__ import annotations

import argparse
import os
from pathlib import Path
import shlex
import shutil
import subprocess
import tempfile


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cc", default=os.environ.get("CC", "cc"), help="C compiler command")
    parser.add_argument("--no-sanitize", action="store_true", help="omit address/UB sanitizers")
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    compiler = shlex.split(args.cc)
    if not compiler or shutil.which(compiler[0]) is None:
        parser.error("C compiler not found; install one or pass --cc")
    with tempfile.TemporaryDirectory(prefix="read-pico-home-") as temporary:
        executable = Path(temporary) / "preview-home-test"
        command = compiler + ["-std=c11", "-D_POSIX_C_SOURCE=200809L", "-Wall", "-Wextra", "-Werror", "-g"]
        if not args.no_sanitize:
            command += ["-fsanitize=address,undefined", "-fno-omit-frame-pointer"]
        for directory in ["tools/preview_home_stubs", "main/app", "main/ui", "main/font", "components/read_pico_pmu/include"]:
            command += ["-I", str(root / directory)]
        command += [str(root / "tools/preview_home_host_test.c"), "-o", str(executable)]
        subprocess.run(command, cwd=root, check=True)
        subprocess.run([str(executable)], cwd=root, check=True)


if __name__ == "__main__":
    main()

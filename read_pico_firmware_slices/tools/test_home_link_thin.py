#!/usr/bin/env python3
"""Thin-host manage stubs stay 503 home_module_missing with no persistence."""
from pathlib import Path
import os
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def run():
    with tempfile.TemporaryDirectory(prefix="pico-home-thin-") as temp:
        binary = Path(temp) / "home-link-thin"
        subprocess.run(
            [
                os.environ.get("CC", "cc"),
                "-std=c11",
                "-Wall",
                "-Wextra",
                "-Werror",
                "-g",
                "-fsanitize=address,undefined",
                "-fno-omit-frame-pointer",
                "-I",
                str(ROOT / "components/read_pico_home_link/include"),
                str(ROOT / "tools/home_link_thin_host_test.c"),
                str(ROOT / "components/read_pico_home_link/home_link_stubs.c"),
                "-o",
                str(binary),
            ],
            check=True,
            cwd=ROOT,
        )
        subprocess.run([str(binary)], check=True)
        print("home-link thin stubs passed")


if __name__ == "__main__":
    run()

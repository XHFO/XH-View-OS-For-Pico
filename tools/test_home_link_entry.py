#!/usr/bin/env python3
"""Run the actual Home Link module with one-shot setup/printer entries and legacy hosts."""
from pathlib import Path
import os
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def main() -> None:
    sdk = ROOT / "components/read_pico_apps/include"
    with tempfile.TemporaryDirectory(prefix="pico-home-entry-") as temporary:
        temp = Path(temporary)
        library, binary = temp / "home-link.so", temp / "home-link-entry"
        cc = os.environ.get("CC", "cc")
        subprocess.run(
            [cc, "-std=gnu11", "-O2", "-fPIC", "-fno-builtin", "-fno-stack-protector",
             "-fvisibility=hidden", "-Wall", "-Wextra", "-Werror", "-shared",
             "-I", str(sdk), str(ROOT / "apps/src/home_link.c"), "-o", str(library)], check=True, cwd=ROOT)
        subprocess.run(
            [cc, "-std=gnu11", "-Wall", "-Wextra", "-Werror", "-I", str(sdk),
             str(ROOT / "tools/home_link_entry_host_test.c"), "-ldl", "-o", str(binary)], check=True, cwd=ROOT)
        subprocess.run([str(binary), str(library)], check=True, cwd=ROOT)
    print("PASS: actual Home Link 10106 entry pages and legacy host compatibility")


if __name__ == "__main__":
    main()

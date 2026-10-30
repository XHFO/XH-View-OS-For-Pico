#!/usr/bin/env python3
"""Production thin-home lifecycle and resource isolation, without hardware or account access."""
from pathlib import Path
import os
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix="pico-home-thin-life-") as temporary:
    binary = Path(temporary) / "test"
    command = shlex.split(os.environ.get("CC", "cc")) + [
        "-std=c11", "-Wall", "-Wextra", "-Werror", "-g",
        "-fsanitize=address,undefined", "-fno-omit-frame-pointer",
    ]
    for include in ["tools/preview_home_stubs", "main/app", "main/ui", "main/font", "main/system",
                    "components/read_pico_apps/include", "components/read_pico_home_link/include",
                    "components/read_pico_transfer/include"]:
        command += ["-I", str(ROOT / include)]
    command += [str(ROOT / "tools/pico_home_thin_lifecycle_host_test.c"), "-o", str(binary)]
    subprocess.run(command, cwd=ROOT, check=True)
    subprocess.run([str(binary)], cwd=ROOT, check=True)

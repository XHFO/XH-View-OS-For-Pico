#!/usr/bin/env python3
"""中文：应用集原生契约测试，不连接设备或用户TF。/ English: Native app-collection contracts without devices or user TF access."""
from pathlib import Path
import os
import shlex
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix="pico-launcher-") as tmp:
    executable = str(Path(tmp) / "launcher-test")
    command = shlex.split(os.environ.get("CC", "cc")) + [
        "-std=c11", "-Wall", "-Wextra", "-Werror", "-g",
        "-fsanitize=address,undefined", "-fno-omit-frame-pointer",
    ]
    for include in ["tools/preview_home_stubs", "main/app", "main/ui", "main/font", "main/system"]:
        command += ["-I", str(root / include)]
    command += [str(root / "tools/pico_launcher_host_test.c"), "-o", executable]
    subprocess.run(command, cwd=root, check=True)
    subprocess.run([executable], cwd=root, check=True)

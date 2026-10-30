#!/usr/bin/env python3
"""中文：首页注册、草稿保存和双方向边界；非实体显示验收。/ English: Navigation registry, draft persistence and bounds; not physical display acceptance."""
from pathlib import Path
import os
import shlex
import subprocess
import tempfile
from pico_i18n_host import sources_and_flags

root = Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix="pico-navigation-") as tmp:
    executable = str(Path(tmp) / "navigation-test")
    i18n_sources, i18n_flags = sources_and_flags(tmp)
    command = shlex.split(os.environ.get("CC", "cc")) + [
        "-std=c11", "-Wall", "-Wextra", "-Werror", "-g",
        "-fsanitize=address,undefined", "-fno-omit-frame-pointer",
    ]
    for include in ["tools/preview_home_stubs", "main/app", "main/ui", "main/font", "main/system"]:
        command += ["-I", str(root / include)]
    command += i18n_flags + [str(root / "tools/pico_navigation_host_test.c"), *i18n_sources, "-lz", "-o", executable]
    subprocess.run(command, cwd=root, check=True)
    subprocess.run([executable], cwd=root, check=True)

#!/usr/bin/env python3
"""家庭/打印/待机宿主契约检查；不替代实体显示、功耗或真实网络验收。
Home/printer/standby host contracts, not physical display, power, or networking acceptance.
"""
from pathlib import Path
import os
import shlex
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix="pico-system-home-") as tmp:
    executable = str(Path(tmp) / "home-test")
    command = shlex.split(os.environ.get("CC", "cc")) + [
        "-std=c11", "-Wall", "-Wextra", "-Werror", "-g",
        "-fsanitize=address,undefined", "-fno-omit-frame-pointer",
    ]
    for include in ["tools/preview_home_stubs", "main/app", "main/ui", "main/font", "main/system", "components/read_pico_home_link/include", "components/read_pico_transfer/include"]:
        command += ["-I", str(root / include)]
    command += [str(root / "tools/pico_home_host_test.c"), "-o", executable]
    subprocess.run(command, cwd=root, check=True)
    subprocess.run([executable], cwd=root, check=True)

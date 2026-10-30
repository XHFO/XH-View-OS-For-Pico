#!/usr/bin/env python3
"""Exercise bounded HA DNS-SD parsing without a live Home Assistant account."""
from pathlib import Path
import os
import sys
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix="pico-ha-discovery-") as tmp:
    binary = Path(tmp) / "discovery"
    subprocess.run([os.environ.get("CC", "cc"), "-std=c11", "-Wall", "-Wextra", "-Werror", "-g",
                    "-fsanitize=address,undefined", "-fno-omit-frame-pointer", "-DRP_HOME_DISCOVERY_PARSER_ONLY",
                    "-I", str(ROOT / "components/read_pico_home_link/include"),
                    "-I", str(ROOT / "components/read_pico_home_link"),
                    str(ROOT / "tools/home_link_discovery_host_test.c"),
                    str(ROOT / "components/read_pico_home_link/home_link_discovery.c"),
                    "-o", str(binary)], cwd=ROOT, check=True)
    subprocess.run([str(binary)], cwd=ROOT, check=True)

    if "--sockets" in sys.argv:
        socket_binary = Path(tmp) / "discovery-sockets"
        subprocess.run([os.environ.get("CC", "cc"), "-std=c11", "-Wall", "-Wextra", "-Werror", "-g",
                        "-fsanitize=address,undefined", "-fno-omit-frame-pointer", "-pthread",
                        "-I", str(ROOT / "components/read_pico_home_link/include"),
                        "-I", str(ROOT / "components/read_pico_home_link"),
                        str(ROOT / "tools/home_link_discovery_socket_test.c"),
                        "-o", str(socket_binary)], cwd=ROOT, check=True)
        subprocess.run([str(socket_binary)], cwd=ROOT, check=True, timeout=10)

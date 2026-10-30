#!/usr/bin/env python3
"""Compile the actual MSC lifecycle and block callbacks against host doubles.

This tests storage ownership, data and recovery boundaries, not real USB/TF
enumeration. Final target builds and device acceptance are separate checks.
"""
import os
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix="pico-usb-test-") as temporary:
    for name, flags in (("normal", []), ("asan-ubsan", ["-fsanitize=address,undefined", "-fno-omit-frame-pointer"])):
        binary = Path(temporary) / name
        command = [os.environ.get("CC", "cc"), "-std=c11", "-Wall", "-Wextra", "-Werror", "-pthread", "-g",
                   "-DREAD_PICO_USB_HOST_TEST", "-I" + str(ROOT / "tools/pico_usb_stubs"),
                   "-I" + str(ROOT / "components/read_pico_usb/include"), "-I" + str(ROOT / "components/read_pico/include"),
                   *flags, str(ROOT / "tools/pico_usb_host_test.c"),
                   str(ROOT / "components/read_pico_usb/read_pico_usb.c"), "-o", str(binary)]
        subprocess.run(command, check=True)
        environment = dict(os.environ, ASAN_OPTIONS="detect_leaks=0:abort_on_error=1", UBSAN_OPTIONS="halt_on_error=1")
        result = subprocess.run([str(binary)], check=True, text=True, capture_output=True, env=environment, timeout=30)
        print(f"{name}: {result.stdout.strip()}")

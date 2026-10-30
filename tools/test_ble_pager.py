#!/usr/bin/env python3
"""Compile/run the shared bounded BLE HID parser, optionally under ASan/UBSan."""
import argparse
import pathlib
import subprocess

root = pathlib.Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser()
parser.add_argument("--sanitize", action="store_true")
args = parser.parse_args()
out = root / "build" / "ble-pager-tests"
out.mkdir(parents=True, exist_ok=True)
binary = out / ("hid-sanitize" if args.sanitize else "hid")
cmd = ["cc", "-std=c11", "-Wall", "-Wextra", "-Werror", "-g", "-O1"]
if args.sanitize:
    cmd += ["-fsanitize=address,undefined", "-fno-omit-frame-pointer"]
cmd += ["-I", str(root / "components/read_pico_ble_pager"), str(root / "components/read_pico_ble_pager/pager_hid.c"), str(root / "tools/ble_pager_hid_host_test.c"), "-o", str(binary)]
subprocess.run(cmd, check=True)
subprocess.run([str(binary)], check=True)

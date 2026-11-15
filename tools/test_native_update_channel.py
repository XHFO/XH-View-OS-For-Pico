#!/usr/bin/env python3
"""Verify stable/test update-channel persistence without network or hardware."""
from pathlib import Path
import argparse, json, tempfile
from test_native_preview_shelf_ble import Runtime, ROOT

p = argparse.ArgumentParser()
p.add_argument("--binary", type=Path, required=True)
p.add_argument("--output", type=Path, required=True)
a = p.parse_args()
out = a.output.resolve()
out.mkdir(parents=True, exist_ok=True)
session = Path(tempfile.mkdtemp(prefix="update-channel-", dir=out))
runtime = Runtime(a.binary.resolve(), ROOT / "sdcard/fonts/MiSansVF.ttf", session, out)
checks = []
try:
    state = runtime.start()
    assert state["update_channel"] == 0, state
    checks.append("default-stable")
    state = runtime.command("update-channel 1")
    assert state["update_channel"] == 1, state
    checks.append("switch-test")
    runtime.close()
    state = runtime.start()
    assert state["update_channel"] == 1, state
    checks.append("test-persists")
    state = runtime.command("update-channel 0")
    assert state["update_channel"] == 0, state
    checks.append("switch-stable")
    runtime.command("update-channel 2", ok=False)
finally:
    runtime.close()
(out / "report.json").write_text(json.dumps({
    "passed": True,
    "hardware_access": False,
    "checks": checks,
}, ensure_ascii=False, indent=2) + "\n")
print("PASS", checks)

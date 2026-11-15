#!/usr/bin/env python3
"""Exercise the About-page stable/test selector, risk confirmation and persistence."""
from pathlib import Path
import argparse, json, tempfile
from test_native_preview_shelf_ble import Runtime, ROOT

p = argparse.ArgumentParser()
p.add_argument("--binary", type=Path, required=True)
p.add_argument("--output", type=Path, required=True)
a = p.parse_args()
out = a.output.resolve()
out.mkdir(parents=True, exist_ok=True)
session = Path(tempfile.mkdtemp(prefix="update-channel-ui-", dir=out))
runtime = Runtime(a.binary.resolve(), ROOT / "sdcard/fonts/MiSansVF.ttf", session, out)
checks = []
try:
    runtime.start()
    runtime.command("route 5")
    runtime.tap(180, 110)
    runtime.settle(4)
    assert runtime.command("status")["update_channel"] == 0

    runtime.tap(303, 174)
    state = runtime.command("status")
    assert state["update_channel"] == 0, state
    runtime.capture("test-channel-warning", state)
    checks.append("first-test-tap-requires-confirmation")

    runtime.tap(260, 415)
    state = runtime.command("status")
    assert state["update_channel"] == 1, state
    runtime.capture("test-channel-selected", state)
    checks.append("explicit-risk-confirm-selects")

    runtime.close()
    runtime.start()
    runtime.command("route 5")
    runtime.tap(180, 110)
    runtime.settle(4)
    assert runtime.command("status")["update_channel"] == 1
    checks.append("test-channel-persists")

    runtime.tap(247, 174)
    state = runtime.command("status")
    assert state["update_channel"] == 0, state
    runtime.capture("stable-channel-selected", state)
    checks.append("stable-selects-without-warning")
finally:
    runtime.close()
(out / "report.json").write_text(json.dumps({
    "passed": True,
    "hardware_access": False,
    "checks": checks,
}, ensure_ascii=False, indent=2) + "\n")
print("PASS", checks)

#!/usr/bin/env python3
"""Verify installed and older TF firmware caches are removed, while newer remains installable."""
from pathlib import Path
import argparse, json, struct, tempfile
from test_native_preview_shelf_ble import Runtime, ROOT

p = argparse.ArgumentParser()
p.add_argument("--binary", type=Path, required=True)
p.add_argument("--output", type=Path, required=True)
a = p.parse_args()
out = a.output.resolve()
out.mkdir(parents=True, exist_ok=True)


def image(version):
    data = bytearray(0x200)
    data[0] = 0xe9
    struct.pack_into("<I", data, 0x20, 0xabcd5432)
    data[0x30:0x30 + len(version)] = version.encode()
    data[0x50:0x59] = b"Read_Pico"
    return bytes(data)


def case(version, expect_ready, expect_files):
    session = Path(tempfile.mkdtemp(prefix=f"fw-{version.replace('.', '-')}-", dir=out))
    directory = session / "tf/readpico/firmware"
    directory.mkdir(parents=True)
    binary = directory / "update.bin"
    digest = directory / "update.sha256"
    binary.write_bytes(image(version))
    digest.write_text("0" * 64 + "\n")
    runtime = Runtime(a.binary.resolve(), ROOT / "sdcard/fonts/MiSansVF.ttf", session, out)
    try:
        state = runtime.start()
        ready = state["firmware_ready"]
        assert ready is expect_ready, (version, ready, state)
        assert binary.exists() is expect_files and digest.exists() is expect_files, version
        return {"version": version, "ready": ready, "files_retained": binary.exists()}
    finally:
        runtime.close()


checks = [
    case("0.40.0-dual99", False, False),
    case("0.40.0-dual100", False, False),
    case("0.40.0-dual101", False, False),
    case("0.40.0-dual102", True, True),
    case("0.40.0-dual97", False, False),
    case("0.40.0-dual96", False, False),
    case("0.40.0-dual95", False, False),
    case("0.40.0-dual94", False, False),
    case("0.40.0-dual93", False, False),
    case("0.40.0-dual91", False, False),
    case("0.40.0-dual999", True, True),
]
(out / "report.json").write_text(json.dumps({
    "passed": True,
    "hardware_access": False,
    "checks": checks,
}, ensure_ascii=False, indent=2) + "\n")
print("PASS", checks)

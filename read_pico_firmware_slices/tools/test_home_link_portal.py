#!/usr/bin/env python3
"""LAN portal / Mac readback contract. Downloads cJSON only to compile the host test."""
from pathlib import Path
import os
import subprocess
import tempfile
import urllib.request

ROOT = Path(__file__).resolve().parents[1]
CJSON = "https://raw.githubusercontent.com/DaveGamble/cJSON/v1.7.18/"
CONTRACT = (ROOT / "docs/HOME-LINK-CONTRACT.md").read_text()
for needle in (
    "契约变更",
    "home_portal_active",
    "X-Pico-Portal-Key",
    "/api/manage/pico/home-link",
    "home_link_mode",
    "has_cf_access_client_secret",
    "unsupported_source",
):
    if needle not in CONTRACT:
        raise SystemExit(f"contract missing {needle}")

with tempfile.TemporaryDirectory(prefix="pico-home-portal-") as temp:
    vendor = Path(temp) / "cjson"
    vendor.mkdir()
    for name in ("cJSON.h", "cJSON.c"):
        urllib.request.urlretrieve(CJSON + name, vendor / name)
    exe = Path(temp) / "home-portal-test"
    subprocess.run(
        [
            os.environ.get("CC", "gcc"),
            "-std=c11",
            "-Wall",
            "-Wextra",
            "-Werror",
            "-g",
            "-fsanitize=address,undefined",
            "-fno-omit-frame-pointer",
            "-I",
            str(ROOT / "components/read_pico_home_link/include"),
            "-I",
            str(ROOT / "components/read_pico_home_link"),
            "-I",
            str(vendor),
            str(ROOT / "tools/home_link_portal_host_test.c"),
            *[
                str(ROOT / "components/read_pico_home_link" / name)
                for name in (
                    "home_link_portal.c",
                    "home_link_stubs.c",
                    "home_link_model.c",
                    "home_link_json.c",
                )
            ],
            "-lm",
            "-o",
            str(exe),
        ],
        cwd=ROOT,
        check=True,
    )
    subprocess.run([str(exe)], cwd=ROOT, check=True)

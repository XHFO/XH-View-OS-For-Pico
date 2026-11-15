#!/usr/bin/env python3
"""LAN portal/Mac readback field contract; uses the local pinned cJSON dependency."""
from pathlib import Path
import gzip
import os
import re
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def main() -> None:
    subprocess.run(["python3", str(ROOT / "tools/build_home_portal_asset.py"), "--check"], check=True, cwd=ROOT)
    cjson = ROOT / "managed_components/espressif__cjson/cJSON"
    if not (cjson / "cJSON.c").is_file():
        raise SystemExit("Install the repository's locked cJSON component before running this test; no download is performed.")
    with tempfile.TemporaryDirectory(prefix="pico-home-portal-") as temporary:
        temp = Path(temporary)
        binary = temp / "home-portal"
        subprocess.run(
            [os.environ.get("CC", "cc"), "-std=c11", "-Wall", "-Wextra", "-Werror", "-g",
             "-fsanitize=address,undefined", "-fno-omit-frame-pointer",
             "-I", str(ROOT / "components/read_pico_home_link/include"),
             "-I", str(ROOT / "components/read_pico_home_link"), "-I", str(cjson),
             str(ROOT / "tools/home_link_portal_host_test.c"),
             *[str(ROOT / "components/read_pico_home_link" / name) for name in
               ("home_link_portal.c", "home_link_stubs.c", "home_link_model.c", "home_link_json.c")],
             "-lm", "-o", str(binary)], check=True, cwd=ROOT)
        subprocess.run([str(binary)], check=True, cwd=ROOT)
        html = temp / "portal.html"
        encoded = subprocess.check_output([str(binary), "--html-gzip"], cwd=ROOT)
        decoded = gzip.decompress(encoded)
        assert decoded == (ROOT / "components/read_pico_home_link/assets/home_link_portal.html").read_bytes()
        text = decoded.decode("utf-8", errors="strict")
        assert all(item in text for item in ("X-Pico-Portal-Key", "/api/lan/home", "ha_token", "cf_access_client_id", "原直连凭据", "隔离"))
        assert not re.search(r"[0-9a-fA-F]{32}", text)
        html.write_bytes(decoded)
        subprocess.run([os.environ.get("NODE", "node"), str(ROOT / "tools/home_link_portal_frontend_test.js"),
                        str(html)], check=True, cwd=ROOT)


if __name__ == "__main__":
    main()

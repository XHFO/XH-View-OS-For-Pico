#!/usr/bin/env python3
"""home-link opens the armed page, and the thin host deep-links HA to setup."""
from pathlib import Path
import os
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def main() -> None:
    games = (ROOT / "main/system/pico_games.c").read_text()
    home = (ROOT / "main/system/pico_home.c").read_text()
    thin = (ROOT / "main/system/pico_home_link_ui_thin.inc").read_text()
    package = (ROOT / "apps/src/home_link.c").read_text()
    manage = (ROOT / "components/read_pico_transfer/manage.html").read_text()
    assert "PGA_HOME_ENTRY" in games and "ps_apps_prepare_home" in games
    assert "s_home_hold" in games and "if(keep&&same)return;" in games
    assert "ps_apps_expect_home_route(route)" in home
    assert "thin_open_home(THIN_HOME_SETUP,false)" in home
    assert "__attribute__((weak))" not in thin
    assert "thin_open_home(THIN_HOME_SETUP, true)" in thin
    assert "thin_open_home(THIN_HOME_PRINTERS, true)" in thin
    assert "PGA_HOME_ENTRY_OK" in package and "HOME_LINK_VERSION = 10101" in package
    assert "ps_apps_prepare_home" not in manage

    sdk = ROOT / "components/read_pico_apps/include"
    with tempfile.TemporaryDirectory(prefix="pico-home-entry-") as temporary:
        temporary = Path(temporary)
        library = temporary / "home-link.so"
        binary = temporary / "home-link-entry"
        cc = os.environ.get("CC", "cc")
        subprocess.run(
            [cc, "-std=gnu11", "-O2", "-fPIC", "-fno-builtin", "-fno-stack-protector",
             "-fvisibility=hidden", "-Wall", "-Wextra", "-Werror", "-shared",
             "-I", str(sdk), str(ROOT / "apps/src/home_link.c"), "-o", str(library)],
            check=True,
        )
        subprocess.run(
            [cc, "-std=gnu11", "-Wall", "-Wextra", "-Werror", "-I", str(sdk),
             str(ROOT / "tools/home_link_entry_host_test.c"), "-ldl", "-o", str(binary)],
            check=True,
        )
        subprocess.run([str(binary), str(library)], check=True)
    print("PASS: HA deep-link source wiring and home-link entry pages")


if __name__ == "__main__":
    main()

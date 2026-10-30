#!/usr/bin/env python3
"""Host check: thin-host PGA state must not return to internal BSS.

dev66 kept rp_home_config_t + rp_home_snapshot_t + tokens as file-scope BSS
(~53276 bytes) and copied them onto the 8192-byte home_pga stack and the UI
stack (16800-byte pack buffer). USB MSC then failed at start with usb_error
257 (0x101 ESP_ERR_NO_MEM). ESP_ERR_NOT_SUPPORTED is 0x106 (262).

There is no read_pico_usb host test in this slice. This guards the regression
that is actually present. Device smoke is in the PR.
"""
from pathlib import Path
import os
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
BRIDGE = (ROOT / "main/system/pico_home_pga.c").read_text()
GAMES = (ROOT / "main/system/pico_games.c").read_text()

forbidden = [
    "static rp_home_config_t",
    "static rp_home_snapshot_t",
    "static rp_home_token_t",
    "malloc(",
]
for needle in forbidden:
    assert needle not in BRIDGE, needle
# Pack buffer and snapshot copies exist only as fields of the single PSRAM store.
assert BRIDGE.count("uint8_t packed[RP_HOME_CONFIG_PACK_LIMIT]") == 1
assert "rp_home_snapshot_t snap;" in BRIDGE and "rp_home_snapshot_t snap =" not in BRIDGE
assert "rp_home_config_t next =" not in BRIDGE and "rp_home_config_t config =" not in BRIDGE
assert "heap_caps_calloc(1, sizeof(*s_store), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)" in BRIDGE
assert "xTaskCreate(worker, \"home_pga\", 8192" in BRIDGE
assert "xTaskCreateWithCaps" not in BRIDGE  # TLS worker stack must stay internal
assert "ps_home_pga_init()" not in GAMES

probe = r"""
#include "read_pico_home_link.h"
#include <stdio.h>
int main(void) {
    unsigned bss = (unsigned)(sizeof(rp_home_config_t) + sizeof(rp_home_snapshot_t)
                              + 3u * sizeof(rp_home_token_t));
    unsigned store = (unsigned)(3u * sizeof(rp_home_config_t) + 2u * sizeof(rp_home_snapshot_t)
                                + 3u * sizeof(rp_home_token_t) + 16800u);
    if (bss < 50000 || store < 100000) return 1;
    if (0x101 != 257 || 0x106 != 262) return 2;
    printf("internal_bss_if_static=%u psram_store=%u\n", bss, store);
    return 0;
}
"""
with tempfile.TemporaryDirectory(prefix="pico-home-pga-mem-") as temporary:
    path = Path(temporary)
    source = path / "sizes.c"
    binary = path / "sizes"
    source.write_text(probe)
    subprocess.run(
        [os.environ.get("CC", "cc"), "-std=c11", "-Wall", "-Wextra", "-Werror",
         f"-I{ROOT / 'components/read_pico_home_link/include'}", str(source), "-o", str(binary)],
        check=True,
    )
    subprocess.run([str(binary)], check=True)
print("PASS: home PGA state is PSRAM-sized and not internal BSS")

# Home Link TF PGA (thin host + PGA bridge)

- Firmware `0.39.0-dev67+` with `PICO_HOME_LINK_FULL=OFF`.
- PGA bridge state (config, snapshot, tokens, pack buffer) is one PSRAM allocation. It must not return to internal BSS: dev66's ~52 KiB internal copies made `RPICO USB START` fail with `usb_error: 257` (`ESP_ERR_NO_MEM`). `esp_app_desc` follows `PROJECT_VER`. `PS_VERSION` is not in this slice.
- Manage `/api/manage/home|printers|bambu` still **503** `home_module_missing` (contract).
- Host adds optional `home_link*` ABI + `pico_home_pga` bridge (NVS `pico_home_pga`, HTTP/TLS, CF Access headers on external).
- Package: `/sdcard/apps/tools/home-link.pga` (kind 130, abi 1, version **10101**).
- On-device setup in the PGA (not phone manage). Independent Bambu MQTT still limited (`RP_HOME_UNSUPPORTED` on MQTT transport).
- See [HOME-LINK-CONTRACT.md](HOME-LINK-CONTRACT.md).

## Opening HA on the device

Thin-host「家庭」and「家庭设置」do not stop on the「打开家庭工具包」page when `home-link.pga` is already on TF. The host calls `ps_apps_prepare_home(1)` and opens the package on **连接设置** (`PGA_HOME_ENTRY`, `result=1`, `flags & PGA_HOME_ENTRY_OK`).「打印机」uses `result=2`. Opening「家庭连接」from 应用集 does not arm an entry, so the package still starts on the monitor page.

The phone/Mac manage page does not gain an HA shortcut. Manage `/api/manage/home|printers|bambu` stays **503** `home_module_missing`.

A package older than 10101 ignores `PGA_HOME_ENTRY` and still opens the monitor; copy a rebuilt `home-link.pga` to `apps/tools/` to land on 连接设置. `build-artifacts/home-link.pga` in this repo stays the previous device binary until `python3 tools/build_tool_packages.py --device --app home-link` is run on a machine with the ESP32-S3 toolchain. Back from the tool still returns to the shelf (same as other tools opened outside 应用集), which avoids re-opening HA in a loop.

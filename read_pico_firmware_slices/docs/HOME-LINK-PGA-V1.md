# Home Link TF PGA (thin host + PGA bridge)

- Firmware `0.39.0-dev67+` with `PICO_HOME_LINK_FULL=OFF`.
- PGA bridge state (config, snapshot, tokens, pack buffer) is one PSRAM allocation. It must not return to internal BSS: dev66's ~52 KiB internal copies made `RPICO USB START` fail with `usb_error: 257` (`ESP_ERR_NO_MEM`). `esp_app_desc` follows `PROJECT_VER`. `PS_VERSION` is not in this slice.
- Manage `/api/manage/home|printers|bambu` still **503** `home_module_missing` (contract).
- Host adds optional `home_link*` ABI + `pico_home_pga` bridge (NVS `pico_home_pga`, HTTP/TLS, CF Access headers on external).
- Package: `/sdcard/apps/tools/home-link.pga` (kind 130, abi 1). Source version is **10101**: the entity list pages once a horizontal drag passes 36px, before finger lift. `build-artifacts/home-link.pga` is still the previous device blob (10100); rebuild with the ESP32 toolchain before copying it to TF.
- On-device setup in the PGA (not phone manage). Independent Bambu MQTT still limited (`RP_HOME_UNSUPPORTED` on MQTT transport).
- See [HOME-LINK-CONTRACT.md](HOME-LINK-CONTRACT.md).

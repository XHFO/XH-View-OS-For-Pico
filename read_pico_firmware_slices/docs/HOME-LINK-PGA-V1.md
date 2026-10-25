# Home Link TF PGA (thin host + PGA bridge)

- Firmware `0.39.0-dev66+` with `PICO_HOME_LINK_FULL=OFF`.
- Manage `/api/manage/home|printers|bambu` still **503** `home_module_missing` (contract).
- Host adds optional `home_link*` ABI + `pico_home_pga` bridge (NVS `pico_home_pga`, HTTP/TLS, CF Access headers on external).
- Package: `/sdcard/apps/tools/home-link.pga` (kind 130, abi 1, version **10100**).
- On-device setup in the PGA (not phone manage). Independent Bambu MQTT still limited (`RP_HOME_UNSUPPORTED` on MQTT transport).
- See [HOME-LINK-CONTRACT.md](HOME-LINK-CONTRACT.md).

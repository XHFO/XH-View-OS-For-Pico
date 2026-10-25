# Home Link TF PGA (thin host + PGA bridge)

- Firmware `0.39.0-dev67+` with `PICO_HOME_LINK_FULL=OFF`. `GET /api/manage/pico/status` reports `home_link_mode:"pga"`.
- PGA bridge state (config, snapshot, tokens, pack buffer) is one PSRAM allocation. It must not return to internal BSS: dev66's ~52 KiB internal copies made `RPICO USB START` fail with `usb_error: 257` (`ESP_ERR_NO_MEM`). `esp_app_desc` follows `PROJECT_VER`. `PS_VERSION` is not in this slice.
- Exact `GET/PUT /api/manage/home` and `GET/PUT/DELETE /api/manage/printers` persist NVS `pico_home_pga` using the manage field names, plus `portal`. Writes return **409** `home_portal_active` while the LAN portal is up. `/api/manage/bambu` stays **503** `home_module_missing`.
- Host adds optional `home_link*` ABI + `pico_home_pga` bridge (NVS `pico_home_pga`, HTTP/TLS, CF Access headers on external). Phone setup is a short-lived LAN page; the screen shows a QR. Mac forms stay the backup writer.
- Package: `/sdcard/apps/tools/home-link.pga` (kind 130, abi 1, version **10101**).
- Independent Bambu MQTT still limited (`RP_HOME_UNSUPPORTED` on MQTT transport). The phone form cannot edit source 2–4.
- See [HOME-LINK-CONTRACT.md](HOME-LINK-CONTRACT.md).

# Home Link TF PGA v1 (thin host)

- Firmware with `PICO_HOME_LINK_FULL=OFF` (default). See [HOME-LINK-CONTRACT.md](HOME-LINK-CONTRACT.md).
- Static HA/printer stack replaced by stubs + empty Home UI; manage home/printers/bambu → **503** `home_module_missing` (PUT does **not** fake-ok or persist).
- Optional package: `/sdcard/apps/tools/home-link.pga` (kind 130, abi 1).
- Skeleton ≠ usable full HA; migration continues against the contract.
- Rebuild full static stack: `-DPICO_HOME_LINK_FULL=ON`.
- Install PGA: USB-copy to `apps/tools/home-link.pga`, eject, end transfer, open from empty Home button or 首页导航.
- Cloudflare Access JSON (full stack): `cf_access_client_id` / `cf_access_client_secret` write-only; GET `has_*` only.

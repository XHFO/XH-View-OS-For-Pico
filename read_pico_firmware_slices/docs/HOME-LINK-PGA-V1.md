# Home Link TF PGA v1 (thin host)

- Firmware `0.39.0-dev65` with `PICO_HOME_LINK_FULL=OFF` (default).
- Static HA/printer stack replaced by stubs + empty Home UI.
- Optional package: `/sdcard/apps/tools/home-link.pga` (kind 130).
- Rebuild full static stack: `-D PICO_HOME_LINK_FULL=ON`.
- Install PGA: USB-copy to `apps/tools/home-link.pga`, eject, end transfer, open from empty Home button or 首页导航.

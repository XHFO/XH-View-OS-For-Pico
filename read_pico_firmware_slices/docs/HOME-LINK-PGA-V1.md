# Home Link TF PGA (thin host + PGA bridge)

Firmware `0.39.0-dev66+` with `PICO_HOME_LINK_FULL=OFF`. Package `/sdcard/apps/tools/home-link.pga` (kind **130**, abi **1**, version **10200**).

Phone manage `/api/manage/home|printer|bambu` stays **503** `home_module_missing` for both `home_link_mode=thin` and `home_link_mode=pga`. The frozen phone JSON lives only on `full`. See [HOME-LINK-CONTRACT.md](HOME-LINK-CONTRACT.md).

## What `home_link_mode=pga` means

| Mode | Phone manage home/printer/bambu | Where HA is configured |
| --- | --- | --- |
| `thin` | 503 `home_module_missing` | Nowhere until the package is installed |
| `pga` | 503 `home_module_missing` | On the device, inside Home Link |
| `full` | Existing manage API | Phone manage page |

Mac/desktop code should branch on `home_link_mode` from `GET /api/manage/pico/status`:

- Missing field: treat as `thin`.
- `thin`: tell the user to copy `home-link.pga` to `apps/tools/` and rescan. Do not PUT `/api/manage/home`.
- `pga`: do **not** PUT `/api/manage/home`, `/api/manage/printers`, or `/api/manage/bambu/auth`. Those calls still fail and do not persist. Point the user at the device UI. Do not synthesize a parallel phone schema.
- `full`: keep the existing phone manage client.

`pga` is reported only when `/sdcard/apps/tools/home-link.pga` validates as id `home-link`, kind 130, abi 1. USB disk mode can make that read fail and the field fall back to `thin` until the card is mounted again.

## What the package actually does

Host bridge (`pico_home_pga`, NVS namespace `pico_home_pga`):

- Save/load HA enable, local URL, external HTTPS, home Wi‑Fi, long-lived token, and the Cloudflare Access service-token pair.
- Empty credential fields keep the saved value. Explicit clear wipes the token and both Access fields together.
- Fetch HA `/api/states`, area template, and supported entity control on a background worker (not on the draw/touch stack).
- Attach `CF-Access-Client-Id` and `CF-Access-Client-Secret` only when the model marks the request external/remote. Local home Wi‑Fi requests, including `https://` on the LAN, do not get those headers.
- Access login HTML (302/403) is `RP_HOME_ACCESS` (12), distinct from a bad HA token (`RP_HOME_AUTH`).

Printers:

- Three slots can store an **HA entity prefix** (source HA), for example `sensor.bambu_a`. Status comes from the HA snapshot.
- Bambu LAN, Bambu cloud MQTT, and Bambu account login are **not** implemented on this thin host. The MQTT transport returns `RP_HOME_UNSUPPORTED` (10). A failed/unsupported poll clears stale telemetry so a slot cannot keep looking online.
- Formlabs OAuth is not a PGA setup form. Do not report those paths as working.

## Before / after

| | Before (v10100) | After (v10200) |
| --- | --- | --- |
| `home_link_mode` | Always `thin` or `full` from the compile flag | `pga` when the package file validates |
| HA setup | PGA could store URL/token/CF | Same fields, plus explicit clear; worker no longer keeps the snapshot on a small task stack |
| Entity control | Blocking HTTP on the UI call | Queued on the home worker, then refreshed |
| Access headers | Model already omitted them on the local path | Covered by host tests: local (including local HTTPS) has none; external and local-failure fallback send both |
| Printers | Read-only slots, copy said MQTT was pending | HA prefix editor; MQTT stays an honest unsupported stub |
| Phone manage | 503 `home_module_missing` | Unchanged |

## Smoke checklist (no cloud flash)

1. Build or copy `home-link.pga` **10200** to `apps/tools/home-link.pga` on the TF card. Rescan tools. `GET /api/manage/pico/status` shows `"home_link_mode":"pga"`.
2. `GET` and `PUT /api/manage/home` still return **503** and `error=home_module_missing`. Confirm the PUT did not change on-device HA settings.
3. Open Home Link on the device. Enter local URL, home Wi‑Fi, HA token. Save. Sync. Supported entities appear. Turn a light/switch on or off and confirm HA sees it.
4. External HTTPS only: save the external URL, enable external and external control, enter both Access service-token fields (Zero Trust → Access → Service Auth). Away from home Wi‑Fi, sync. A missing/wrong token should surface Access (not “bad HA token”) when Cloudflare returns the login page.
5. On home Wi‑Fi, repeat a sync and a control. Capture or log the local request: it must **not** include `CF-Access-Client-Id` or `CF-Access-Client-Secret`. The external request must include both, and only together.
6. Printer page: set an HA prefix and save. Matching HA printer entities show status. There is no Bambu login and no LAN MQTT form. An unsupported MQTT result must not show a percent or “online”.
7. Remove the package (or unmount TF) and refresh status: mode returns to `thin`. Manage routes stay 503.

Rebuild the device package on a machine with the ESP32-S3 toolchain:

```sh
export PICO_XTENSA_CC=/path/to/xtensa-esp32s3-elf-gcc
python3 tools/build_tool_packages.py --device --app home-link --output build/tool-packages
```

Copy `home-link.pga` to the card. Do not flash firmware from this private repo’s cloud agent.

The checked-in `build-artifacts/home-link.pga` **10200** carries `esp32s3-xtensa` and `linux-x86_64` (sha256 `0a7aefbbcf343efb57471b968d184a6cc960fbeb2c52801f28b2899477e13534`, 35372 bytes). Device install uses the Xtensa payload. It does not include `macos-arm64` (v10100 did). Rebuild the native payload on a Mac for desktop preview; do not splice the old macos bytes into this package.

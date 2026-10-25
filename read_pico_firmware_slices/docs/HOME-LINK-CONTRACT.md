# Home Link thin-host / PGA contract (frozen for teammate sync)

Asia/Shanghai reference. Applies to firmware with `PICO_HOME_LINK_FULL=OFF` (default thin host, e.g. `0.39.0-dev65+`) and the optional TF package `apps/tools/home-link.pga` (kind **130**, abi **1**).

Mac desktop Resources/Firmware refresh to Pico `dev65` is **out of scope** here (needs explicit XH approval; Mac may remain on `0.36.0-dev60`).

## Modes: `home_link_mode`

Proposed on `GET /api/manage/pico/status` (additive field; older clients ignore unknown keys):

| Value | Meaning |
| --- | --- |
| `thin` | Older `PICO_HOME_LINK_FULL=OFF` builds that do **not** register the PGA store bridge. Manage home/printer/bambu return `home_module_missing`. **This firmware does not report `thin`.** If the field is absent, keep treating the device as `thin`. |
| `full` | `PICO_HOME_LINK_FULL=ON`: in-firmware stack + embedded monitoring UI. Manage API unchanged. `GET /api/manage/pico/home-link` is **404**. |
| `pga` | This firmware, `PICO_HOME_LINK_FULL=OFF`. The device owns the HA/printer store in NVS `pico_home_pga`. Exact manage home/printer routes read and write that store. Bambu stays `home_module_missing`. |

`home_link_mode:"pga"` means the PGA store is the copy Mac should display. It does not mean Bambu MQTT or the full entity browser is served by manage HTTP.

## Pairing (unchanged)

- QR / URL fragment: `#key=` + **32 hex** characters (same-origin manage page).
- API header: `X-Pico-Setup-Key: <32 hex>`.
- Expiry: existing management session lifetime; stop/leave/disconnect/expiry revoke.
- This contract does **not** change key length, header name, or expiry rules.

## Thin-host manage API (no store bridge)

Applies when `home_link_mode` is `thin` or absent, and when a `pga` build has not registered the store bridge. It does **not** describe `home_link_mode:"pga"` on this firmware. See [LAN portal and Mac readback](#lan-portal-and-mac-readback).

All of the following paths are claimed by `rp_home_web_handle` stubs (prefix match). Any method returns the same body:

**HTTP status:** `503`

**Body (exact JSON object):**

```json
{"ok":false,"error":"home_module_missing","message":"Install TF tools/home-link.pga"}
```

**Paths covered (thin):**

- `/api/manage/home` (and longer prefixes starting with that string)
- `/api/manage/printer`… (includes `/api/manage/printers`)
- `/api/manage/bambu`… (includes `/api/manage/bambu/auth`)

Also stubbed when the full web router would have served them under FULL: HA entities/control/refresh are **not** registered by the stub handler (management falls through to `404` if the path is not one of the three prefixes above). Practical phone UI hits `home` / `printers` / `bambu/auth` first and receives `home_module_missing`.

### Semantics (important)

| Call | Thin-host behavior |
| --- | --- |
| `GET /api/manage/home` | **503** + `home_module_missing`. No config JSON. |
| `PUT /api/manage/home` | **503** + `home_module_missing`. **Not** a fake `ok`. **Not** persisted to NVS. `read_pico_home_link_configure` returns `false`. |
| `GET/PUT/DELETE …/printers` | **503** + `home_module_missing`. No persistence. |
| `GET/POST …/bambu/auth` | **503** + `home_module_missing`. No login session. |
| Snapshot / control / refresh via runtime | Empty / unsupported; Home UI is thin empty-state pointing at the TF package. |
| Power suspend | Always succeeds (`read_pico_home_link_suspend` → `true`). |

There is **no** “optimistic OK” on thin PUT. Clients must treat `error=home_module_missing` as “install or enable home module,” not as saved settings.

## Full-stack manage API (`PICO_HOME_LINK_FULL=ON`)

Unchanged relative to [HOME-LINK-WEB.md](HOME-LINK-WEB.md), plus Cloudflare Access fields below. Mutations return `{ "ok": true, "queued": true/false }` on success; persistence failure remains **507**.

### Cloudflare Access Service Token (agreed JSON)

Write-only credentials for **external / remote** HA HTTPS only. Local home Wi‑Fi path must **not** attach Access headers.

**PUT `/api/manage/home` additional fields:**

| Field | Direction | Notes |
| --- | --- | --- |
| `cf_access_client_id` | write-only | Max 95 UTF-8 bytes; omit/empty keeps saved value. |
| `cf_access_client_secret` | write-only | Max 63 UTF-8 bytes; omit/empty keeps saved value. |
| `clear` | write | May include `cf_access_client_id` and/or `cf_access_client_secret` (and existing `ha_token`). |

Both ID and secret must be present together after apply (either newly written or already stored). Device sends headers:

- `CF-Access-Client-Id`
- `CF-Access-Client-Secret`

**GET `/api/manage/home` config flags (never raw secrets):**

| Field | Type |
| --- | --- |
| `has_cf_access_client_id` | boolean |
| `has_cf_access_client_secret` | boolean |

Browser cookie / One-time PIN Access flows are **not** supported on device. If Access returns 302/403 HTML login (or CF Access markers in body), result is `RP_HOME_ACCESS` (distinct from `RP_HOME_AUTH` bad HA token).

Manage UI copy documents Zero Trust → Access → Service Auth token creation.

## PGA package

| Item | Value |
| --- | --- |
| Path on TF | `/sdcard/apps/tools/home-link.pga` (USB disk: `apps/tools/home-link.pga`) |
| kind | `130` (`PGA_HOME_LINK`) |
| abi | `1` |
| Catalog id | `home-link` |
| Package version | `10101` |

The package monitors HA and can open the LAN QR. Bambu MQTT is still not a live manage login.

## LAN portal and Mac readback

Firmware owns the phone form: the device starts a short-lived page on the connected Wi-Fi and shows a QR. Mac keeps the existing Home Link and printer forms as the backup writer. Both write one PGA store. They do not both hold port 80.

### Who may write

| State | Writer | Other side |
| --- | --- | --- |
| QR page is up (`portal.phase` is `open` or `saved` and the phone server holds port 80) | Phone `PUT /api/lan/home` and `PUT /api/lan/printers` | Mac cannot open the manage server. On-device PGA save is refused（手机正在填写，本机暂不保存）. |
| Computer manage session holds port 80 | Mac `PUT/DELETE` on the manage routes below | Portal start is refused. On-device PGA save is refused（请先关闭电脑管理会话）. |
| Neither server is up | On-device PGA keyboard | Mac is not connected. |

Opening computer manage stops the phone server first. Starting the phone server fails while manage is active. A manage write that still arrives while the portal owns the store returns **409** and does not change NVS:

```json
{"error":"home_portal_active"}
```

Mac should disable submit while the user is on the device QR flow. After manage connects again, GET before showing a form. On **409**, discard the unsent body and GET again. Do not background-PUT.

### What the phone submits

QR text is only `http://<device-ipv4>/h#t=<32 hex>`. The fragment is not a query parameter and is not an HA/CF/printer secret. The page sends it as `X-Pico-Portal-Key`. Lifetime is 10 minutes and is not renewed. This is not the manage pairing key.

Phone routes (portal key, not `X-Pico-Setup-Key`):

| Method | Path | Success |
| --- | --- | --- |
| `GET` | `/h` | HTML form |
| `GET` `PUT` | `/api/lan/home` | same field names as manage home |
| `GET` `PUT` | `/api/lan/printers?slot=N` | same printer objects as manage printers |

No phone `DELETE`. Clearing a slot is `PUT` `{"source":0}`.

`PUT /api/lan/home` success:

```json
{"ok":true,"queued":true,"portal":{"active":true,"phase":"saved","revision":5,"seconds_left":500,"writer":"device"}}
```

`queued` means the PGA worker was notified (`enabled` and Wi-Fi online and the worker exists). It does **not** mean the HA token was accepted. Poll `status.result` later. `false` still means the public config was stored.

The phone page omits `rooms`, so an omitted `rooms` key keeps the saved map. A Mac body may include `rooms` with the same rules as full manage (replace the whole map, at most 24, unique entity ids).

### What Mac reads after the form closes

While the QR server holds port 80, Mac manage calls do not reach this API. Read back **after** that server stops (the screen leaves the QR, or the 10 minutes end). `portal.active` is then `false`. `portal.phase` stays `saved` when a commit happened, or becomes `idle` / `offline` / `error` when the page closed without a newer commit.

`GET /api/manage/pico/status` adds this object on `pga` and `full` (the rest of the status document is unchanged):

```json
"home_portal":{"active":false,"phase":"saved","revision":5,"seconds_left":0,"writer":"device"}
```

On `full`, `writer` is `"manage"` and `active` is false. On `pga`, `writer` is always `"device"`. That marks the PGA store as the copy of record. It does not mean the device keyboard was the last client.

`portal.revision` is a RAM counter of successful home or printer commits this boot (phone or Mac). It resets on reboot. Compare public fields across reboots; do not treat a smaller revision after reboot as a rollback.

`status.revision` inside home/home-link is the existing HA snapshot counter. It is not `portal.revision`.

`GET /api/manage/pico/home-link` (manage pairing key, because it is under `/api/manage/pico/`). `PUT` is **405**. `full` returns **404**.

```json
{"ok":true,"home_link_mode":"pga","writer":"device","portal":{"active":false,"phase":"saved","revision":5,"seconds_left":0,"writer":"device"},"config":{},"status":{},"network_online":true,"at_home":false,"printers":[]}
```

`config` and each printer match `GET /api/manage/home` and `GET /api/manage/printers`. This aggregate does **not** include `saved_wifi`, `wifi_truncated`, or `imports`. Use the home GET for the SSID list.

### Field names (same as manage)

`GET /api/manage/home` and `GET /api/lan/home` on `pga`:

| Object | Fields |
| --- | --- |
| `config` | `enabled`, `local`, `external`, `home_wifi`, `external_enabled`, `external_control`, `has_token`, `has_cf_access_client_id`, `has_cf_access_client_secret`, `rooms[]` of `{entity, room}` |
| `status` | `result` (0–12, including `12` = `RP_HOME_ACCESS`), `http_status`, `revision`, `busy`, `external`, `sampled_ms` |
| top level | `network_online`, `at_home`, `saved_wifi[]`, `wifi_truncated`, `portal` |

GET never returns `ha_token`, `cf_access_client_id`, `cf_access_client_secret`, printer `secret`, `client_secret`, or `certificate`.

`PUT` home accepts the same write-only names as [HOME-LINK-WEB.md](HOME-LINK-WEB.md): `ha_token`, `cf_access_client_id` (max 95), `cf_access_client_secret` (max 63), `clear` of those three keys. Empty secret strings keep the saved value. Omitted keys keep the saved value. Unknown or duplicate keys are **400** `invalid_configuration` and do not write. `home_wifi`, when present, must be a remembered SSID if the adapter supplied a list.

`GET` printers: `printers[]` plus `imports` plus `portal`. Each printer has `slot`, `source`, `name`, `serial`, `host`, `port`, `region`, `has_username`, `has_secret`, `has_client_id`, `has_client_secret`, `has_certificate`, and `status` (`result`, `http_status`, `connected`, `has_data`, `percent`, `remaining_minutes`, `layer`, `layers`, `state`, `task`, `material`).

On `pga`, `imports` is always `[]`. Source `0` clears a slot. Source `1` (HA) accepts only `source`, `name`, and `serial`. Source `2`–`4` returns **400** `unsupported_source` and leaves that slot unchanged. `DELETE /api/manage/printers?slot=N` clears a slot the same way as full manage. The phone server does not expose `DELETE`.

`PUT` success for these routes is **200** `{"ok":true,"queued":true|false,"portal":{...}}`. **507** `save_failed` leaves the store unchanged. Other errors use fixed strings and do not echo the submitted body: **400** `bad_json` / `invalid_configuration` / `invalid_query` / `unsupported_source`, **405** `method`, **413** `too_large`, **503** `memory`.

`pga` body limit for these home/printer routes is **4096** bytes (full manage remains 12288). IPv4 only. A hotel AP that isolates clients can stop the phone from opening the QR URL; the screen and the HTML say so. The page still ships.

Phases: `idle`, `open`, `saved`, `offline`, `error`.

## 契约变更

Relative to the previous thin-host contract (`home_link_mode:"thin"`, all three prefixes **503**). Unchanged items are listed so Mac can keep them.

Unchanged:

- Pairing `#key=` / `X-Pico-Setup-Key` / session expiry.
- Full-mode manage field names, CF header names, and snapshot `result` 0–12.
- `queued` is not HA login success.
- `/api/manage/bambu` on this build stays **503** `{"ok":false,"error":"home_module_missing","message":"Install TF tools/home-link.pga"}`.
- Secrets stay out of GET and out of the QR.
- If `home_link_mode` is missing, assume `thin` and do not treat a PUT as saved.

Changed on this firmware when `home_link_mode` is `pga`:

1. The status string is `pga`, not `thin`.
2. Exact `GET/PUT /api/manage/home` and `GET/PUT/DELETE /api/manage/printers` persist NVS `pico_home_pga` with the manage JSON names. They are no longer **503**. Longer prefixes under those paths, and every bambu path, stay **503**.
3. GET home, GET printers, and PUT success add `portal`. `GET /api/manage/pico/status` adds `home_portal`. New `GET /api/manage/pico/home-link` is the single readback document. `full` does not grow that route (404) and reports `home_portal.writer:"manage"`.
4. Manage writes during the portal are **409** `home_portal_active` and do not write. On the device this overlaps port 80, so Mac usually sees a failed connection while the QR is up, then `phase:"saved"` plus the public fields after it stops.
5. Printer source `2`–`4` is **400** `unsupported_source` (full manage still accepts Bambu/Formlabs). Source `0` and manage `DELETE` clear the slot, including secrets already stored there.
6. `imports` on the pga printers GET is always `[]`.
7. Home/printer body limit on the pga routes is 4096.
8. `portal.revision` / `home_portal.revision` is a new boot-local commit counter. Do not confuse it with `status.revision`.

## Compatibility checklist for Mac teammate

1. Read `home_link_mode`. Absent or `thin`: home/printers/bambu stay **503** `home_module_missing`; do not assume PUT persisted. `full`: keep today's manage forms; ignore a missing home-link route. `pga`: use the field names above and treat the device store as the copy of record.
2. Do not submit Mac forms while `home_portal.active` is true or while the device is showing the QR. After reconnect, GET `/api/manage/pico/home-link` (or home + printers). Honor **409** `home_portal_active` by reloading. Honor **503** `home_module_missing` as "not this contract".
3. Display `has_token`, `has_cf_access_client_id`, `has_cf_access_client_secret`, and printer `has_*`. Never expect the raw secrets in status or GET.
4. A phone save is visible as `portal.phase:"saved"`, a higher `portal.revision` this boot, and the public `config` / `printers` fields. `queued` only means a refresh was queued.
5. Leave Bambu/Formlabs slots read-only on `pga`. Clearing them is an explicit source `0` or `DELETE`.
6. CF JSON names stay `cf_access_client_id` / `cf_access_client_secret` + `has_*` on GET. Pairing `#key=` / `X-Pico-Setup-Key` / expiry unchanged.
7. Do not require a Mac Firmware resource bump to Pico `dev65` without XH approval. This readback is the manage API above, not a new firmware-bundle requirement.

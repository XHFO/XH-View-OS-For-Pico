# Home Link thin-host / PGA contract (frozen for teammate sync)

Asia/Shanghai reference. Applies to firmware with `PICO_HOME_LINK_FULL=OFF` (default thin host, e.g. `0.39.0-dev65+`) and the optional TF package `apps/tools/home-link.pga` (kind **130**, abi **1**).

Mac desktop Resources/Firmware refresh to Pico `dev65` is **out of scope** here (needs explicit XH approval; Mac may remain on `0.36.0-dev60`).

## Modes: `home_link_mode`

Proposed on `GET /api/manage/pico/status` (additive field; older clients ignore unknown keys):

| Value | Meaning |
| --- | --- |
| `thin` | `PICO_HOME_LINK_FULL=OFF`: stubs linked; manage home/printer/bambu return `home_module_missing`. |
| `full` | `PICO_HOME_LINK_FULL=ON`: in-firmware stack + embedded monitoring UI. |
| `pga` | Reserved: TF `home-link.pga` supplies monitoring UI / future module behavior while host keeps Wi‑Fi/HTTP/TLS/power. **Not claimed usable for full HA until migration completes.** |

Skeleton PGA ≠ usable HA. Do not advertise full HA/printer flows until the package is migrated beyond placeholder UI and backed by host services.

## Pairing (unchanged)

- QR / URL fragment: `#key=` + **32 hex** characters (same-origin manage page).
- API header: `X-Pico-Setup-Key: <32 hex>`.
- Expiry: existing management session lifetime; stop/leave/disconnect/expiry revoke.
- This contract does **not** change key length, header name, or expiry rules.

## Thin-host manage API (`PICO_HOME_LINK_FULL=OFF`)

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

v1 skeleton only shows empty-state guidance. Migration work continues against **this** contract; do not claim full HA until progressive UI is backed by host services and thin/full/`pga` mode semantics above.

Device navigation (thin host, package present):「家庭」and「家庭设置」open the installed package on its connection-setup page instead of an intermediate「打开家庭工具包」screen. Phone/Mac manage does not add an HA entry; the 503 paths above are unchanged. See [HOME-LINK-PGA-V1.md](HOME-LINK-PGA-V1.md).

## Compatibility checklist for Mac teammate

1. On thin Pico: expect **503** `home_module_missing` for home/printers/bambu — do not assume PUT persisted.
2. Prefer reading `home_link_mode` from `pico/status` when present; default assume `thin` if absent on older builds.
3. CF JSON names frozen as `cf_access_client_id` / `cf_access_client_secret` + `has_*` on GET.
4. Pairing `#key=` / `X-Pico-Setup-Key` / expiry unchanged.
5. Do not require Mac Firmware resource bump to Pico `dev65` without XH approval.

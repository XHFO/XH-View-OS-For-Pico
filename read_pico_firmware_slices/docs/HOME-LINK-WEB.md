# Device management API / 设备管理 API

The HTTP adapter authenticates every request with an expiring setup key, validates Host/Origin, reads bounded JSON and wipes input buffers. `home_link_web.c` implements the same bounded controller for firmware and native preview. It performs local configuration transactions and queues existing workers; it never performs blocking network I/O. JSON uses snake_case; all responses need `Cache-Control: no-store`.

HTTP 层负责临时扫码密钥、Host/Origin、输入限额及请求缓冲清理；共享控制器负责参数验证、事务保存、凭据隔离、快照查询与后台任务排队。不能把 HTTP 200/202 等同于真实服务器验证通过。

Limits: body 12,288 bytes, query 768 bytes, output 32,768 bytes, at most 24 entities per page. Device config/snapshot/parser allocations use PSRAM. No TF card is required or used for credentials. The HTTP owner supplies current connection and saved SSIDs, never Wi-Fi passwords.

限额：正文 12,288 字节、查询 768 字节、输出 32,768 字节，每页最多 24 个实体。设备配置、快照与解析节点使用 PSRAM；凭据不依赖也不写入 TF 卡。HTTP 层仅提供当前连接与已记忆 SSID，不传入 Wi-Fi 密码。

## 接口 / Endpoints

| 方法/路径 / Method/path | 行为 / Behavior |
| --- | --- |
| `GET /api/manage/home` | 隐去凭据的 HA 配置、快照状态、记忆 SSID 与网络归类。/ Masked HA configuration, snapshot status, remembered SSIDs and network classification. |
| `PUT /api/manage/home` | 部分更新 HA 配置，经验证后事务保存。/ Partial HA update; validated/persisted transaction. |
| `GET /api/manage/printers` | 三个隐藏凭据的槽位与最多八个缓存 HA 导入候选。/ Three masked printer slots and up to eight cached HA import candidates. |
| `PUT /api/manage/printers?slot=0` | 更新一个槽位，仅允许对应来源的字段。/ Partial update of one slot, source-specific fields only. |
| `DELETE /api/manage/printers?slot=0` | 仅清除指定槽位。/ Clear that slot only. |
| `GET /api/manage/ha/entities` | 搜索、筛选与分页最近一次完整实体快照。/ Search/filter/page the last complete supported-entity snapshot. |
| `POST /api/manage/ha/control` | 排队执行明确支持的 HA 操作。/ Queue an explicit supported HA action. |
| `POST /api/manage/refresh` | 排队验证连接与状态，正文为 `{}`。/ Queue connection/state verification; body `{}`. |
| `GET /api/manage/bambu/auth` | 公开的限时云登录状态与绑定列表。/ Public transient cloud-login state and bound printers. |
| `POST /api/manage/bambu/auth` | 登录、复用已存账户、验证码、请求新码、取消或导入。/ Login, reuse a saved account, code, resend, cancel or import. |

Mutations return `{ "ok": true, "queued": true/false }`; save/delete use 200, queued refresh/control use 202. A successful save of an active printer enables the shared service, matching the previous device setup flow. Delete/none preserves the global enabled switch. Poll GET status for the actual worker outcome. A failed persistence call returns 507 and leaves the runtime/disk configuration unchanged.

写操作返回 `{ "ok": true, "queued": true/false }`；保存/删除使用 200，刷新/控制成功入队使用 202。成功保存有效打印机时开启共享服务；删除/空槽位保留全局开关。网页在保存后读取配置，核对公开字段与凭据存在状态，确认一致后才清除草稿；回读失败或不一致时保留输入并显示“已提交，尚未确认”，配对失效仍按安全规则清除秘密。须轮询 GET 状态取得 worker 的实际结果。持久化失败返回 507，内存与磁盘配置均保持原值。

## HA 配置 / HA configuration

`GET /home`:

```json
{
  "config": {
    "enabled": true,
    "local": "http://homeassistant.local:8123",
    "external": "https://ha.example",
    "home_wifi": "Home",
    "external_enabled": false,
    "external_control": false,
    "has_token": true,
    "rooms": [{"entity":"light.desk","room":"书房"}]
  },
  "status": {"result":2,"http_status":200,"revision":17,"busy":false,"external":false,"sampled_ms":12345},
  "network_online": true,
  "at_home": true,
  "saved_wifi": ["Home","Office"],
  "wifi_truncated": false
}
```

`PUT /home` accepts the config fields above, plus write-only `ha_token` and `clear:["ha_token"]`. GET never returns `ha_token`. Full builds also accept write-only `cf_access_client_id` / `cf_access_client_secret` and `clear` of those keys; GET returns only `has_cf_access_client_id` / `has_cf_access_client_secret`. `home_link_mode:"pga"` serves this same schema for exact `/api/manage/home` and `/api/manage/printers`, with the additive `portal` object and the limits in [HOME-LINK-CONTRACT.md](HOME-LINK-CONTRACT.md). Older thin builds and any path under `/api/manage/bambu` still return `home_module_missing`. Missing fields preserve existing values. Empty credential strings also preserve old values; explicit clear is required. `rooms` replaces the entire manual map, at most 24 unique entity IDs; `[]` clears mappings. An explicitly supplied `home_wifi` must match a remembered SSID when the adapter supplies the SSID list.

`PUT /home` 接受上述配置字段及只写的 `ha_token`、`clear:["ha_token"]`。GET 不返回令牌；缺失字段保留原值，空凭据也保留，清除必须明确指定。`rooms` 全量替换手动映射，最多 24 个不重复实体 ID；`[]` 清空映射。HTTP 层提供记忆网络时，显式提交的 `home_wifi` 必须属于该列表。

The adapter may provide `current_wifi`; the controller compares it against the newly saved `home_wifi` when queueing work. This avoids classifying a first-time home network against the pre-save configuration.

HTTP 层可提供 `current_wifi`；处理器按新保存的 `home_wifi` 判断任务使用的家庭网络状态，避免首次保存时沿用旧配置的归类结果。

## 打印机槽位 / Printer slots

`source` values: 0 none, 1 HA, 2 Bambu LAN, 3 Bambu cloud compatibility, 4 Formlabs Dashboard cloud. `region`: 0 global, 1 mainland China.

`source`：0 未配置、1 HA、2 Bambu 局域网、3 Bambu 云端兼容、4 Formlabs Dashboard 云端。`region`：0 全球、1 中国大陆。

| 来源 / Source | 可写字段 / Writable fields |
| --- | --- |
| HA | `source`, `name`, `serial` (HA entity prefix) |
| Bambu LAN | `source`, `name`, `serial`, `host`, `port`, `secret` (LAN code), `certificate` (trusted PEM CA) |
| Bambu cloud compatibility | `source`, `name`, `serial`, `region`, `username` (`u_…`), `secret` (existing token) |
| Formlabs | `source`, `name`, `serial` (Dashboard API serial), `client_id`, `client_secret` |

GET returns only noncredential fields `slot/source/name/serial/host/port/region`, flags `has_username/has_secret/has_client_id/has_client_secret/has_certificate`, and a status object with `result/http_status/connected/has_data/percent/remaining_minutes/layer/layers/state/task/material`. Import candidates expose `name/serial` only.

GET 只返回非凭据字段 `slot/source/name/serial/host/port/region`、凭据存在标记 `has_username/has_secret/has_client_id/has_client_secret/has_certificate` 和状态 `result/http_status/connected/has_data/percent/remaining_minutes/layer/layers/state/task/material`。导入候选只公开 `name/serial`。

Each PUT accepts applicable fields only. Changing source keeps the display name, zeros every other source field, then applies the submitted fields. Thus a blank/missing new secret cannot reuse a previous brand's credentials. Same-source missing/empty credential fields preserve saved values; `clear:["secret"]`, for example, explicitly clears a field. Active sources must remain complete/valid, so clearing a required secret without replacing/removing the profile fails with 400. DELETE removes the profile. Source 0 also clears the slot.

每次 PUT 只接受对应来源的字段。切换来源保留显示名，先清零其他来源字段再应用新值，空/缺失的新凭据不会误用旧品牌凭据。同来源更新中缺失/空凭据保留已存值，`clear:["secret"]` 等显式清除。有效来源必须保持配置完整；仅清除必需凭据而不替换或移除配置会返回 400。DELETE 或来源 0 均清除指定槽位。

Formlabs uses official Dashboard `client_credentials` OAuth. Bambu cloud supports the source-reviewed community account/code login below and retains advanced existing-token setup. LAN CA entry is advanced TLS setup, with no TF certificate file picker in this release. See [official contracts and limitations](HOME-LINK.md).

Formlabs 使用官方 Dashboard 的 `client_credentials` OAuth。Bambu 云端支持下述经过源码核对的社区账号／验证码登录，并保留既有令牌高级配置。局域网 CA 是高级 TLS 配置，本版不提供 TF 卡证书选择器。参见[官方契约与限制](HOME-LINK.md)。

## Bambu 账号会话 / Bambu account session

`GET /bambu/auth` returns `session/revision/phase/region/count/error/result/http_status/seconds_left/retry_after/truncated/devices`, plus `config_revision/reused/saved_count/saved_accounts`. The shared RAM device list exposes `serial/name/model` only, at most eight items. `reused` is a boolean indicating a session opened from saved credentials. Phases 0..6: idle, busy, email/SMS code, authenticator fallback, ready, error, expired. Error values 0..8: none, credentials, expired code, incorrect code, rate limit, cloud block, invalid response, authenticator unsupported, configuration changed. `result` uses the existing Home Link result enum. Account, password, verification challenge, username and token are never returned.

`GET /bambu/auth` 公开会话、版本、阶段、区域、设备数、固定错误、连接结果、上游状态码、剩余秒数、新码冷却、截断标记，以及 `config_revision/reused/saved_count/saved_accounts`。共享 RAM 列表 `devices` 最多八项，仅 `serial/name/model`；布尔值 `reused` 表示会话由已存凭据开启。phase 0..6：空闲、处理、邮箱／短信码、验证器回退提示、就绪、错误、过期；error 0..8：无、凭据、码过期、码错误、限流、云端拦截、响应无效、验证器不支持、配置已改变。不回传账号、密码、挑战秘密、MQTT 用户名或令牌。

`saved_accounts` has at most three objects `{slot,region,printer_count,name}`, deduplicated server-side by cloud source, region and MQTT username. `slot` is the current representative printer slot, `printer_count` counts matching saved profiles, and `name` is that printer's display name (serial fallback), not the account identity. `config_revision` identifies the configuration used to generate the groups. Groups are derived from saved printer profiles, with no separate password or account database.

`saved_accounts` 最多三项 `{slot,region,printer_count,name}`，服务端按 Bambu 云端来源、区域和 MQTT 用户名去重。`slot` 是当前代表打印机槽位，`printer_count` 为同组已保存打印机数量；`name` 为代表打印机显示名（无名称时使用序列号），不是账号标识。`config_revision` 表示生成分组时的配置版本。账户组从已存槽位派生，不新增密码或账户数据库。

Every POST requires the current GET `session` and `action`. Unknown, repeated or foreign fields are rejected. Actions:

每次 POST 必须包含当前 GET 的 `session` 与 `action`，拒绝未知、重复或不适用字段：

| Action | Additional fields / 其它字段 |
| --- | --- |
| `login` | `region` 0 global / 1 China, `account` (1..127 UTF-8 bytes), `password` (1..255 bytes) |
| `reuse` | `saved_slot` from current `saved_accounts`, `config_revision` from the same GET / 当前账户组的代表槽位及同次 GET 的配置版本 |
| `email_code` | `code` (4..12 ASCII digits; email or SMS / 邮箱或短信) |
| `resend` | none; explicitly request a code or switch from authenticator to code login / 无，主动请求新码或从验证器改用验证码 |
| `cancel` | none / 无 |
| `import` | `slot` 0..2, `index` in retained device list, optional `name` up to47 UTF-8 bytes / 当前保留列表索引及可选名称 |

Queued actions return 202 and the public state above; GET/cancel use 200. Poll state to obtain real results. A cloud 401 is a state `result`, never a management HTTP401: only the latter revokes phone pairing. Bad input400, offline503 and stale/busy/cooldown409 retain fixed error strings without submitted values. No direct TOTP submission endpoint is offered; phase3 explains the limitation and allows explicit `resend` to the source-reviewed `codeLogin` method.

入队返回202及同一公开状态，查询／取消返回200，须轮询取得真实结果。云端401作为状态结果显示，不冒充会使手机退出配对的管理HTTP401。输入错误400、离线503、旧会话／忙碌／冷却409只返回固定错误文字。phase3不提供未实现的验证器输入，仅说明限制并允许主动请求已核对的验证码登录。

China supports both phone and email accounts. `login` submits the unchanged account value and password; only the upstream challenge leads to phase2/3. UI labels use the entered account (`@` for email, otherwise phone), without altering its destination or country code. When a page reopens mid-challenge without the original account, labels remain generic. Entering phase2 does not itself send a code or start a cooldown; `resend` explicitly requests one and starts the 60-second limit.

中国区支持手机号和邮箱账号；`login` 原样提交账号及密码，仅在上游要求验证后进入 phase2/3。网页按本次输入账号（含 `@` 为邮箱，否则为手机号）显示提示，不改变收件地址或国家代码。中途重开网页而无原账号时保留通用提示。进入 phase2 本身不发码、不启动冷却；`resend` 主动请求验证码并开始 60 秒限流。

`reuse` queues server-side validation of the saved token through the regional preference and bound-device endpoints; the returned identity must match the stored MQTT username. Only a representative slot from the current saved-account groups is accepted. Check both `session` and `config_revision` at submission and before committing worker results. Refused credentials require explicit reauthentication and never erase saved profiles. Tokens and passwords are not sent back to the browser.

`reuse` 在后台使用已存令牌访问对应区域的账户信息与绑定列表接口，返回身份必须匹配已存 MQTT 用户名。仅接受当前账户组的代表槽位；提交与后台结果提交前均核对 `session/config_revision`。凭据被拒绝时须用户明确重新登录，不删除已存配置，不向浏览器回传令牌或密码。

Import returns the normal `{ok,queued}`200 only after transactional persistence into the latest configuration; it accepts only a retained device in the current ready session. With `reused:false`, import also updates tokens in other cloud slots having the same region and MQTT username in that single transaction, preserving their printer fields. With `reused:true`, only the selected slot changes; possibly older saved credentials never overwrite sibling tokens. Other accounts, sources and HA remain unchanged. Validation failure or persistence failure507 preserves the entire original configuration. Login/list retrieval never saves a printer automatically.

导入只在事务保存到最新配置后返回正常 `{ok,queued}`200，必须为当前就绪会话中的保留设备。`reused:false` 时，在同次事务中更新其它同区域、同 MQTT 用户名的 Bambu 云端槽位令牌，保留其设备字段；`reused:true` 时仅写选中槽位，避免较旧已存凭据覆盖其它令牌。其它账户、来源和 HA 不变；验证失败或保存失败507时全部原配置保留。仅登录／读取列表不自动保存打印机。

Import keeps the shared ready list available across slots without extending its five-minute RAM lifetime. Resend is locally cooled for60seconds. Management stop/closure/expiry, disconnect or suspend wipes temporary secrets and rejects late worker results. Session IDs and configuration revisions are concurrency guards, not replacements for setup-key/Origin authentication. Deleting a printer removes only that slot; remaining same-account slots still support reuse.

导入后共享就绪列表可跨槽位继续使用，不延长五分钟 RAM 有效期；本机发送新码冷却60秒。停止／关闭／到期管理入口、断网或挂起时擦除临时秘密并丢弃迟到结果。session 和配置版本是并发保护，不能替代配对密钥及 Origin 认证。删除打印机仅移除该槽位，其它同账户槽位仍可用于复用。

## 实体与控制 / Entities and controls

Query parameters: `offset=0` (0..96), `limit=24` (1..24), `q` (ID/name/room substring, ASCII case-insensitive), exact `room`, `domain`, and/or `kind`.

查询参数：`offset=0`（0..96）、`limit=24`（1..24）、`q`（ID/名称/房间子串，ASCII 忽略大小写），以及精确 `room`、`domain`、`kind` 筛选。

Kinds 0..5 are light, switch, climate, fan, temperature, humidity. Domains accept `light/switch/climate/fan/sensor`; the aliases `temperature/humidity` select the corresponding sensor kind. Other sensor/device domains are unsupported. Query values must be URL encoded; duplicate numeric parameters, malformed escapes and overflow are rejected.

kind 0..5 依次为灯、开关、空调、风扇、温度、湿度。domain 接受 `light/switch/climate/fan/sensor`；`temperature/humidity` 别名选取对应传感器类型，不支持其他类型。查询值须 URL 编码；重复数值参数、无效转义与溢出均被拒绝。

Response contains `items`, `offset/limit/total` (filtered count), `retained`, `total_supported`, `truncated`, `busy`, `revision`, `external`, `can_control`, and all distinct `rooms` from the retained supported snapshot. Room options are independent of the current filter/page. Each item contains `id/name/state/unit/room/kind/available/on/number`; unknown numeric values are JSON `null`.

响应包含 `items`、`offset/limit/total`（筛选后数量）、`retained`、`total_supported`、`truncated`、`busy`、`revision`、`external`、`can_control`，以及保留快照内全部不重复 `rooms`。房间选项不受当前分页/筛选影响。每项包含 `id/name/state/unit/room/kind/available/on/number`；未知数值用 JSON `null`。

Control body: `{"entity":"light.desk","action":"turn_on"}` or `{"entity":"climate.ac","action":"set_temperature","value":26}`. Allowed actions are `turn_on/turn_off`; climate additionally allows `set_temperature` between 5 and 40 °C. Entities must exist in the retained current snapshot and be available. Temperature/humidity sensors are read-only. Offline/busy requests are rejected; control over an external/fallback endpoint requires explicit external-control permission. The API exposes no printer-control action or arbitrary HA service pass-through.

控制正文示例为 `{"entity":"light.desk","action":"turn_on"}` 或 `{"entity":"climate.ac","action":"set_temperature","value":26}`。只允许 `turn_on/turn_off`，空调额外允许 5..40 °C 的 `set_temperature`。实体须存在于当前保留快照并可用；温湿度只读。离线/忙碌时拒绝操作；外部或回退地址控制须明确授权。API 不提供打印机控制或任意 HA 服务透传。

## 错误与验证 / Errors and verification

Errors use fixed strings, e.g. `{"error":"invalid_configuration"}`; submitted values, tokens and upstream response bodies are never echoed. Statuses: 400 invalid input/config/action, 403 disallowed control, 404 unknown entity/path, 405 method, 409 busy/disabled, 413 body too large, 503 offline/memory, 507 persistence/output capacity.

错误使用固定字符串，如 `{"error":"invalid_configuration"}`，不回显提交值、令牌或上游响应正文。状态码：400 输入/配置/操作无效，403 禁止控制，404 实体/路径不存在，405 方法错误，409 忙碌/未启用，413 正文超限，503 离线/内存不足，507 持久化失败/输出容量不足。

Snapshot `result` values: 0 unconfigured, 1 connecting, 2 updated, 3 offline, 4 auth/permissions, 5 HTTP, 6 TLS secure-connection/certificate failure, 7 invalid, 8 oversized, 9 no memory, 10 unsupported, 11 device time not set. Values 0–10 remain unchanged. A local unauthenticated HA probe may confirm server presence; `has_token:false` must not be presented as authenticated access.

快照 `result`：0 未配置、1 连接中、2 已更新、3 离线、4 认证/权限、5 HTTP、6 TLS 安全连接／证书失败、7 数据无效、8 数据超限、9 内存不足、10 不支持、11 设备时间未校准；原有 0–10 数值保持不变。本地无令牌探测可以确认服务器存在，但 `has_token:false` 不代表已通过授权。

Result 11 also applies to Bambu account login and saved-account reuse. The phone shows “设备时间尚未校准，请在设备设置日期与时间后重试”; automatic Wi-Fi synchronization or manually setting valid RTC date/time updates the secure-connection clock. This is distinct from result 6 and does not revoke pairing or indicate a wrong account password. DNS/unreachable-network failures use result 3. No certificate-validation bypass is offered.

结果 11 同样适用于 Bambu 账号登录和已保存账户续用；手机提示“设备时间尚未校准，请在设备设置日期与时间后重试”。设备通过 Wi-Fi 自动授时，或手动设定有效 RTC 日期与时间后，安全连接时钟同步生效。该结果区别于 6，不撤销设备配对，也不表示账号密码错误。DNS／网络不可达使用结果 3；不提供忽略证书校验的回退。

`python3 tools/test_home_link_web.py` runs sanitizer contracts with runtime doubles: masked GET, per-source transactions, explicit clear/preservation, storage failure, paging/filtering/UTF-8, malformed/duplicate/oversized input, control permissions, async refresh and output bounds. It does not exercise real accounts, real TLS endpoints, the HTTP adapter's key/Origin layer, or physical hardware. Those are separate acceptance layers.

`python3 tools/test_home_link_web.py` 使用运行时替身和 sanitizer 检查：GET 隐藏凭据、来源事务、显式清除/保留、存储失败、分页/筛选/UTF-8、无效/重复/超限输入、控制权限、异步刷新与输出边界。真实账号、TLS 端点、HTTP 密钥/Origin 层和实机均须独立验收。

Native management boundary tests set `RPICO_HOME_LINK_OFFLINE=1` before starting sessions and restore the previous environment in `finally`. The real shared API, worker and HTTP request adapter still run, while host HTTP/MQTT transports return `RP_HOME_OFFLINE` without launching curl/OpenSSL. This explicit host-only switch is absent from ESP builds and never reports a simulated successful connection.

原生管理边界测试在启动会话前设置 `RPICO_HOME_LINK_OFFLINE=1`，并在 `finally` 恢复原环境。真实共享 API、工作线程与 HTTP 请求适配器仍运行，宿主 HTTP/MQTT 传输直接返回 `RP_HOME_OFFLINE`，不启动 curl/OpenSSL。该显式开关仅存在于宿主版本，不编入 ESP，也不伪造连接成功。

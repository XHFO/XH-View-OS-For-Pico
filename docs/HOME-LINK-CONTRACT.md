# Home Link 宿主、门户与桌面回读契约 / Host, portal and desktop contract

本契约对应当前 `0.39.1-dev69` 集成源码，默认 `PICO_HOME_LINK_FULL=OFF`，TF 工具包 `apps/tools/home-link.pga` 为 kind **130**、ABI **1**、版本 **10104**。旧版 **10200** 文档和包不能据此当作当前本机构建产物；安装时须重新构建并核对包头。

This contract describes the current dev69 integration, the default thin host and Home Link package **10104**. Rebuild and inspect package headers rather than reusing an earlier **10200** artifact.

## 契约变更 / Contract change

旧 thin-host 文档把所有家庭接口写成 `503 home_module_missing`。当前宿主注册 `pico_home_pga` 存储桥后，精确路径 `/api/manage/home` 和 `/api/manage/printers` 已接到同一份 NVS 配置；手机填写门户和 Mac 回读也使用这份配置。只有未注册桥的旧宿主，以及当前未实现的 Bambu 登录等路径继续返回 503。

After the PGA store bridge is registered, the exact home/printers management routes use the same NVS configuration as the device and phone portal. The old blanket-503 contract applies only without that bridge; unsupported Bambu login remains 503.

`GET /api/manage/pico/status` 的 `home_link_mode` 是能力提示，不是保存成功凭证。旧固件可能只有 `thin`/`full` 或没有此字段；当前默认宿主报告 `pga`，完整静态栈报告 `full`。`pga` 不保证 TF 包存在、可执行或带有特定平台代码；调用方仍需独立检查应用目录。不能仅凭 mode 显示“已连接 HA”。

`home_link_mode` is a capability hint, not a persistence receipt. The current default host reports `pga`; a full static build reports `full`. A `pga` value does not prove the TF package exists or executes on the current platform. Older builds may omit the field or report `thin`.

## 两种限时会话 / Two timed sessions

| 会话 / Session | 打开方式 / Entry | 密钥 / Key | 生命周期 / Lifetime |
| --- | --- | --- | --- |
| 家庭填写门户 / Home portal | 设备家庭/打印机设置或 Home Link 的“手机填写” / Device Home/printer settings or package Phone Setup | `http://<IPv4>/h#t=<32 hex>`；每个 API 请求使用 `X-Pico-Portal-Key` / API header per request | 10 分钟；关闭、离页、休眠、网络丢失/IP 改变、传输或管理服务接管时撤销 / Ten minutes; revoked on close, leave, sleep, network/IP loss or transfer/management takeover |
| 通用管理 / General management | 设备“手机管理”等既有入口 / Existing phone-management entry | `#key=<32 hex>`；`X-Pico-Setup-Key` | 保持原有 15 分钟及离页/断网撤销规则 / Existing fifteen-minute session and revocation rules |

两种密钥不互换、不保存、不出现在公开状态或 Mac 汇总里。门户只在当前已连接 Wi-Fi 的 IPv4 上提供服务，不开启热点、不扫描网段、不占用 TF 卡。请求必须使用对应设备 IP 的 Host；有 Origin 时须严格同源。页面与 JSON 使用 `Cache-Control: no-store`，页面禁止嵌入第三方框架。

Keys are separate, transient and omitted from public/readback state. The portal uses the connected Wi-Fi IPv4 without an access point, subnet scan or TF ownership. Host and optional Origin must match that device origin. HTML/JSON are not cached.

门户与通用管理互斥占用端口 80。门户打开时本机配置保存和通用管理写入暂停，避免两处编辑覆盖；Mac 仍可读取公开状态。到期后即使鉴权已判无效，也须由生命周期 tick 关闭 HTTP 任务并释放写入所有权。

The two services exclusively share port 80. Device setup and management mutations pause while the portal owns writes; readback remains available. Expiry must stop HTTP and release ownership even after authentication already rejects the expired key.

## 当前精确路径 / Current exact routes

通用管理路径仍须经过原有的管理密钥鉴权；门户路径使用门户密钥。控制器不直接访问网络，只验证、保存和排队。

Management routes retain management-key authentication; LAN routes use the portal key. Controllers validate, persist and queue without blocking network I/O.

| 方法 / Method | 门户 / Portal | 通用管理 / Management | 结果 / Result |
| --- | --- | --- | --- |
| GET | `/api/lan/home` | `/api/manage/home` | 隐去密钥的 HA 配置、最近快照、SSID、`portal` / Masked HA config, snapshot, SSIDs and portal |
| PUT | `/api/lan/home` | `/api/manage/home` | 验证后保存部分 HA 字段 / Validate and persist partial HA fields |
| GET | `/api/lan/printers` | `/api/manage/printers` | 三槽公开配置与状态，`imports:[]` / Three masked slots and states; empty imports |
| PUT | `/api/lan/printers?slot=0` | `/api/manage/printers?slot=0` | 只写一个 HA 槽或清空槽 / Update one HA slot or clear it |
| DELETE | `/api/lan/printers?slot=0` | `/api/manage/printers?slot=0` | 仅移除指定槽 / Remove that slot only |
| GET | 无 / None | `/api/manage/pico/home-link` | Mac 只读汇总 / Read-only desktop aggregate |

`slot` 必须为 0、1、2；空值、重复参数、越界值和编码的 NUL 被拒绝。门户正文上限 **4096** 字节、query **768** 字节、输出 **8192** 字节。未知/重复 JSON 字段、尾随垃圾、NUL、超长字段和不完整配置均不保存。

Slots are 0–2. Empty, duplicated, out-of-range or NUL query values are rejected. The portal caps body/query/output at 4096/768/8192 bytes and rejects unknown/duplicate JSON fields, trailing garbage, NUL and invalid configurations.

写入成功返回 `200 {"ok":true,"queued":true|false,"portal":{...}}`。`queued` 只表示宿主接受后续同步，不表示 HA 登录或状态获取成功。保存失败为 `507 save_failed`，失败的单次事务保留旧配置；禁用状态或离线时可以保存配置而不排队同步。

A 200 save receipt confirms local persistence. `queued` is queue admission, not upstream authentication or live data. A failed single transaction returns 507 and retains its prior configuration.

手机表单保存 HA 后按槽顺序保存打印机，并非整个表单的跨请求原子事务。页面先验证所有槽、阻止重复提交；若后续请求失败，明确提示已有部分设置保存。不能把最后一次失败误写成前面全部回滚。

The phone form commits HA and then individual slots. It validates all slots and blocks repeated submissions, but the requests are not one atomic transaction; a later failure explicitly reports partial persistence.

## 字段与凭据 / Fields and credentials

HA 可写 `enabled`、`local`、`external`、`home_wifi`、`external_enabled`、`external_control`、`rooms`，以及只写的 `ha_token`、`cf_access_client_id`、`cf_access_client_secret`。遗漏或空凭据保留旧值；`clear:["ha_token"]` 等明确清除。Access ID/Secret 在应用完成后必须成对存在或同时为空。

HA accepts the fields above and write-only credentials. Omitted/empty credentials retain stored values; explicit `clear` removes them. Cloudflare Access ID and secret must remain a complete pair.

GET 仅返回 `has_token`、`has_cf_access_client_id`、`has_cf_access_client_secret`，从不回传令牌。Access 头仅添加到外部/远程 HTTPS 请求；匹配家庭 Wi-Fi 的本地请求不携带这些头。浏览器 Cookie 或一次性 PIN 不是设备支持的 Access 认证方式。

GET exposes presence flags only. Access headers attach to remote/external HTTPS, never the selected local home endpoint. Browser cookies and one-time PIN Access login are unsupported.

打印机可写来源 **0 空槽** 或 **1 HA**，字段为 `source/name/serial`，其中 serial 是 HA 实体前缀。Bambu LAN、云端 MQTT、Formlabs OAuth 不在此 thin/PGA 表单范围；提交来源 2–4 返回 `400 unsupported_source`。已有其他来源的槽可回读、保持原样或明确删除，不自动重用或覆盖其凭据。

The thin/PGA form writes only empty or HA-prefix printer slots. Bambu MQTT and Formlabs OAuth are outside its scope. Existing other-source slots may be read, preserved or explicitly removed; unsupported writes are rejected.

## Mac 回读 / Desktop readback

`GET /api/manage/pico/home-link` 返回 `ok/home_link_mode/writer/portal/config/status/network_online/at_home/printers`；`writer` 为 `device`。与上表相同的公开字段无需另建桌面配置副本。汇总不包含 `saved_wifi`、导入候选、会话 URL、门户密钥或任何凭据；非 GET 为 **405**。

The desktop aggregate uses the same public fields with `writer:"device"`. It omits saved SSID lists, import candidates, session URLs, portal keys and credentials. Mutating methods return 405.

`portal` 的公开结构为 `active/phase/revision/seconds_left/writer`，phase 为 `idle/open/saved/offline/error`。桌面在门户打开时展示“手机正在填写”状态，写入遇到 **409 home_portal_active** 时保留草稿，等待门户结束后回读。

Public portal state contains only the fields above. A management write during a portal session returns **409 home_portal_active**; retain the draft and read back after the session ends.

## 入口与执行 / Entries and execution

家庭/应用集进入监看，HA 设置进入连接表单，打印机入口进入打印机页；未配置不会强制困在表单。模块绘制只读快照，控制和同步由宿主工作任务执行。“已提交”表示排队，结果见后续快照。

Home/app entries open monitoring; HA Settings opens its form; Printers opens its work page. An unconfigured app still has an exit. Rendering reads snapshots, while control/sync execute on the worker; queued does not mean completed.

休眠暂停取消尚未执行的工作并禁止新排队。`suspend(0)` 仅立即门禁与检查，不额外等待。进行中的 HTTP 仍需在有界超时内收口，暂停或配置变化后的旧结果不会发布；唤醒 resume 后才允许重新排队。

Suspend cancels pending work and rejects new requests. A zero timeout never adds a wait. An in-flight HTTP request still needs its bounded shutdown; stale results cannot publish after suspend/configuration changes. Resume enables future requests.

## 边界与兼容 / Limits and compatibility

- 新 `pico_home_pga/settings` 缺失时才只读回退到旧 `pico_home_link/settings`；不自动写入或擦除。已有新记录（包括明确清空）、损坏记录和 I/O 失败不回退。独立打印机旧凭据原样保留，但不会激活薄宿主未支持的协议。/ Only an absent new record permits read-only legacy fallback. Existing/cleared/corrupt/unreadable new records never resurrect legacy credentials; unsupported printer protocols remain inactive.
- 未注册存储桥：`/api/manage/home`、`/api/manage/printer...`、`/api/manage/bambu...` 继续返回 **503** `home_module_missing`，不伪造成功。/ Without the bridge, the existing prefix stubs remain 503 and do not persist.
- 当前 thin/PGA 的 Bambu 账号登录、独立 MQTT、Formlabs 和管理端 HA entities/control/refresh 没有因门户而获得完整实现。设备实体控制仍走 PGA 宿主 ABI。/ The portal does not add full Bambu/Formlabs or management HA control APIs; device entity control uses the host ABI.
- `PICO_HOME_LINK_FULL=ON` 保持 [HOME-LINK-WEB.md](HOME-LINK-WEB.md) 中完整管理 API；专用 `/h` 门户和 Mac PGA 汇总不用于 full。/ Full builds retain their full management API and do not use the dedicated PGA portal/aggregate.
- ABI 仍为 1；追加命令 `PGA_HOME_ENTRY` 的结果只有带 `PGA_HOME_ENTRY_OK=32` 才有效；`PGA_HOME_PORTAL=64`、`PGA_HOME_MANAGE=128` 不得冲突。旧宿主返回普通 status 时不能误跳页。/ Entry results require their dedicated flag; portal/manage flags use distinct bits, preserving legacy host behavior.

## 可复跑检查 / Reproducible checks

```sh
python3 tools/test_home_link_entry.py
python3 tools/test_home_pga_transaction.py
python3 tools/test_home_link_draft.py
python3 tools/test_home_link_portal.py
python3 tools/test_home_link_portal_runtime.py
```

这些测试运行真实包/控制器/HTTP 包装器与本机依赖，使用假时钟、HTTP 与内存失败注入，不连接真实 HA、打印机或设备。真实 Wi-Fi、物理屏幕、休眠与 USB 资源恢复仍须单独实机验证。

These tests exercise the actual module, controller and wrapper with deterministic clock/HTTP/allocation stubs. They do not establish real Wi-Fi, physical UI, sleep or USB acceptance.

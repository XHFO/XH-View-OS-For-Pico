# 家庭与打印机连接 / Home and printer connections

家庭页默认显示未配置状态。书架、阅读、笔记和游戏可独立离线使用，家庭服务失败不会跳离正在阅读的书。连接任务在后台执行，画面只读取完整提交的状态快照。

Home starts unconfigured. Reading, notes and games work offline; service failures never redirect an open book. Workers fetch data in the background and the UI reads committed snapshots.

## Home Assistant

先在设备上连接 Wi-Fi，在 Home Assistant 或打印机设置点“手机配置”。手机连接同一网络后扫码打开设备管理页，开启连接，从已记忆的 Wi-Fi 中选择家庭网络，再填写本地地址和长期访问令牌。地址示例为 `http://homeassistant.local:8123`；外部访问使用有有效证书的 HTTPS 地址。设备屏幕保留状态、同步、实体筛选和面板编排，详细连接配置统一放在手机管理页。

Connect the device to Wi-Fi, then choose Phone Setup in HA/printer settings. Scan from a phone on the same network, enable the connection, choose a remembered home Wi-Fi and enter a local URL/token. Remote access requires verified HTTPS. The device retains monitoring, synchronization, entity filtering and dashboard ordering; detailed connection forms live in the phone management page.

手机管理页的“探测本地 HA”会在明确选择家庭 Wi-Fi、开启家庭连接后保存并尝试解析和探测 `homeassistant.local:8123`，不扫描整个网段；替换已有不同地址前会确认。没有令牌时仅确认地址有响应，输入令牌后才验证并读取家庭状态。本地连接必须指定已记忆的家庭网络；仅用外部地址时可以不指定。

Probe Local HA in phone management saves, resolves and probes `homeassistant.local:8123` after explicitly choosing home Wi-Fi and enabling Home. Replacing a different address requires confirmation; no subnet scan occurs. An unauthenticated response establishes presence only; a token is required to authenticate and read Home data. Local access requires a remembered home network; remote-only access may leave it unset.

- 家庭 Wi-Fi 匹配时优先访问本地地址；启用外部地址后，离家或本地网络连接失败时使用外部地址。外部控制默认关闭，需明确开启；身份验证错误不会通过更换地址绕过。
  Prefer the local endpoint on the selected home Wi-Fi. Enabling the remote endpoint allows access away from home and fallback after local network failure. Remote control is opt-in; authentication failures are not retried against another endpoint.
- 外部 HA 若前置 **Cloudflare Access**：在手机管理页填写 Access **Service Token** 的 `cf_access_client_id` / `cf_access_client_secret`（只写；GET 仅 `has_*`）。设备在**外部**请求上附加 `CF-Access-Client-Id` / `CF-Access-Client-Secret`；本地家庭 Wi‑Fi 路径不加这些头。不支持浏览器 Cookie / 一次性 PIN。Access 登录页（302/403 HTML）与 HA 令牌错误区分提示。详见 [HOME-LINK-CONTRACT.md](HOME-LINK-CONTRACT.md)。
  If remote HA sits behind **Cloudflare Access**, enter an Access **Service Token** (`cf_access_client_id` / `cf_access_client_secret`, write-only; GET exposes `has_*` only). The device attaches `CF-Access-Client-Id` / `CF-Access-Client-Secret` on **external** requests only—not on the local home Wi‑Fi path. Browser cookie / one-time PIN flows are unsupported. Access login HTML (302/403) is reported separately from a bad HA token. See [HOME-LINK-CONTRACT.md](HOME-LINK-CONTRACT.md).
- 默认只接收标准开关、灯、空调、风扇、温度与湿度实体。支持按房间、名称、种类排序，排除实体和不可用实体，以及手动顺序。房间读取 HA 的 `area_name()`，也可手动覆盖。
  Accept standard switches, lights, climate, fans, temperature and humidity sensors. Sort by room/name/type, exclude individual or unavailable entities and retain manual order. Read HA areas through `area_name()` with manual overrides.
- 每个完整快照最多保留 96 个支持的实体，超过实体限额明确提示。手动房间映射和房间选择器最多保存 24 项；这不限制已保留实体携带的 HA 房间名称，也不会因为出现第 25 个房间就丢弃其实体。HTTP 响应上限 192 KiB；过大响应保留上次成功数据，不显示半份结果。大型家庭建议在 HA 侧提供适合本设备的实体范围。
  Retain at most 96 supported entities, reporting entity truncation. Manual room mappings and the room picker retain up to 24 entries; this does not cap HA area names on retained entities or discard entities from a twenty-fifth room. HTTP responses are capped at 192 KiB. Oversized responses retain the last successful state rather than a partial result; large installations should expose a suitable device-specific set.
- 数据约每 30 秒同步，手动操作后重新同步。这里使用 REST 快照及服务调用，不声称 WebSocket 实时订阅。离线状态不能发送控制。
  Synchronize about every 30 seconds and after an explicit action. This uses REST snapshots and service calls, not WebSocket streaming. Controls are unavailable offline.

## 打印机 / Printers

可保存三个只读监看配置，优先推荐从已连接的 HA 导入 Bambu 打印机；导入候选最多八台。独立 MQTT 与云端模式不会发送启动、暂停或其他打印命令。

Save three read-only monitoring profiles. Prefer importing Bambu printers already integrated with HA; show up to eight import candidates. MQTT and cloud modes never send print-control commands.

手机管理页先选择接入方式，再显示对应字段。Formlabs 云端只出现 Dashboard OAuth 字段；Bambu LAN 显示设备地址、访问码与 CA；Bambu 云端显示区域、账号登录与绑定设备，已有 MQTT 凭据放在高级入口；HA 只显示导入与实体前缀。设备名称为可选项。

Phone management starts with a source chooser and shows only the relevant form. Formlabs uses Dashboard OAuth fields; Bambu LAN uses the printer address, access code and CA; Bambu cloud uses regional account login and bound-printer selection, with existing MQTT credentials in advanced setup; HA offers entity import/prefix. The display name is optional.

| 模式 / Mode | 需要的信息 / Required information |
| --- | --- |
| Home Assistant | 已连接的 HA 及受支持的 Bambu 状态实体命名 / Configured HA and supported Bambu status entities |
| Bambu 局域网 / LAN | 打印机 IP、序列号、LAN 访问码与受信任的 CA/证书；TLS MQTT，用户名 `bblp` / IP, serial, LAN access code and trusted CA/certificate; TLS MQTT as `bblp` |
| Bambu 云端 / Cloud | 区域、账号和密码，按需邮箱／短信验证码，从绑定列表选择；高级方式仍可输入序列号、`u_` 用户标识和已有令牌 / Region, account/password, optional email/SMS verification and bound-printer selection; advanced setup still accepts serial, `u_` MQTT ID and existing token |
| Formlabs 云端 / Cloud | Dashboard 开发者工具生成的 OAuth Client ID、Client Secret 与 API `serial`；不是设备别名 / OAuth client ID/secret from Dashboard Developer Tools and the API `serial`, not the printer alias |

Bambu 云端按已核对的社区协议登录，密码只在当前请求中临时使用，不保存；后续监看仅保存区域、序列号、MQTT 用户标识和令牌。Formlabs 使用 `client_credentials`，按有效期更新令牌；账户及设备权限仍由服务提供方决定。状态实体、第三方集成或服务协议变化可能需要适配。

Bambu cloud uses the source-reviewed community login protocol, keeping passwords transient and persisting only region, serial, MQTT ID and token for monitoring. Formlabs uses `client_credentials` and refreshes expiring tokens; access remains governed by the provider. Changes to integrations, entities or cloud protocols may require adaptation.

- Formlabs 官方 Dashboard Web API 使用 `POST https://api.formlabs.com/developer/v1/o/token/`，Content-Type 为 `application/x-www-form-urlencoded`，grant 为 `client_credentials`；随后以 Bearer 令牌读取 `GET /developer/v1/printers/{printer_serial}/`。令牌按有效期缓存，401 后刷新并只重试一次。解析真实 `printer_status.current_print_run` 的层数、剩余时间、名称及材料；无当前任务时清除旧任务字段，界面不显示喷嘴/热床。
  Formlabs uses the official OAuth form endpoint above, caches expiring tokens and makes one refresh/retry after a 401. The nested Dashboard printer/job response supplies layers, remaining time, name and material; a null current job clears the previous job. Resin/powder monitoring does not display FDM nozzle/bed fields.
- Bambu LAN 使用 TLS MQTT（默认 8883）、固定用户名 `bblp`、访问码和 `device/{serial}/report`；部分机型与新版固件需 LAN/开发者模式。CA 和主机名必须验证成功。云端兼容模式使用所选区域的 `cn.mqtt.bambulab.com:8883` 或 `us.mqtt.bambulab.com:8883`、用户的 `u_…` 标识和已有令牌。Bambu 官方网络端口文档确认这些服务域名，但未据此声称已有公开的官方第三方云账号登录 API。
  Bambu LAN subscribes over verified TLS MQTT as `bblp`, using the access code and serial-specific report topic; some models/firmware require LAN/developer mode. Cloud compatibility selects the official regional MQTT host with the supplied user ID/token. Official host documentation does not establish a public third-party cloud login API.

选择另一种接入方式只清当前编辑草稿的非适用字段，设备名称保留；保存前不会改写已连接的设备。保存时未提交或留空的凭据保持已存值，明确清除通过 `clear` 字段完成；切换来源时不会继承旧凭据。保存失败保留原配置，移除只影响当前槽位。版本 1 的配置按原接入方式迁移为版本 2，保留适用凭据及其它设备，清除混入的其它品牌认证字段；成功迁移后尝试写回，存储不可写时仍保留可用的内存配置。

Source changes clear irrelevant fields in the current draft while retaining its display name. Saved devices remain unchanged until a successful save. Omitted/empty credential inputs keep their saved values; the explicit `clear` field removes them. Changed sources never inherit old credentials. Failed saves preserve the old configuration, and remove affects only the selected slot. Version 1 migrates by source to version 2, preserving applicable credentials and other devices while clearing foreign authentication fields. A normalized writeback is attempted; storage failure keeps the verified runtime configuration usable.

独立 LAN 的 CA 可在手机管理页选择手机上的 PEM 证书文件或粘贴到“LAN CA 证书 · PEM”字段，没有 TF 证书文件选择入口，属于高级 TLS 配置；仅填写 IP 和访问码仍无法通过连接验证。日常使用推荐先通过 HA 导入。迁移写回只从当前配置记录中去除异源字段，不代表物理安全擦除旧 NVS 扇区或电脑存储介质。

The phone LAN form accepts a PEM CA file from the phone or pasted PEM text; there is no TF certificate file picker. This is advanced TLS setup, and IP/access-code fields alone cannot establish a verified connection. HA import is recommended for routine use. Migration removes foreign fields from the active configuration record, not a physical secure erase of old NVS sectors or host media.

### Bambu 账号登录 / Account login

手机管理页使用一张共享的 Bambu 账户卡，不把登录表单重复放进每个打印机槽位。先选择中国大陆或国际区域，再输入邮箱／手机号和账户密码；按需输入邮箱／短信验证码或明确请求新码。社交登录账户需先在 Bambu 应用中设置账户密码；本页不提供未经验证的 Apple／Google 登录。登录后最多保留八台绑定打印机并明确提示超额；选择设备和目标槽位后才保存，导入后保留共享列表，可继续添加同一账户的其它打印机。

Phone management uses one shared Bambu account card rather than a login form per printer slot. Choose mainland China or global, enter the account email/phone and password, then provide or explicitly request an email/SMS code when required. Social-login users must first set a Bambu account password; no Apple/Google OAuth flow is offered. Retain at most eight bound printers and report truncation. Save only after choosing a printer and destination slot; the shared list remains available after import for adding other printers from that account.

已保存的 Bambu 云端配置按“区域 + MQTT 用户标识”归为同一账户，最多三个账户组，只显示代表打印机的名称、区域和已保存数量，不回传账号标识或令牌。点选“继续添加”会在后台使用设备已保存的凭据重新验证账户身份并获取绑定列表，无需再次输入密码。凭据被拒绝、账户身份不符或配置已改变时明确提示；原有配置保留，需要重新登录时再由用户发起。

Saved cloud profiles are grouped by region and MQTT identity, with at most three account groups. Only a representative printer name, region and saved-printer count are public, never the account identity or token. Continue adding revalidates the saved credentials and identity in the background and reloads bound printers without a password. Rejection, identity mismatch or changed configuration is reported while preserving saved profiles; the user explicitly starts reauthentication when needed.

从本次密码／验证码新登录的会话导入时，选中槽位与同区域、同账户的其它 Bambu 云端槽位在一次保存事务中更新令牌，保留其设备信息。复用已存凭据导入只写选中槽位，不用可能较旧的令牌覆盖其它槽位。仅登录或获取列表不保存任何变化；验证或保存失败时全部原配置保留。删除打印机只移除该槽位，其它槽位仍可代表该账户继续添加。

Importing from a fresh password/code login updates the selected slot and tokens of sibling Bambu cloud profiles with the same region and identity in one save transaction, preserving their printer details. Importing reused credentials writes only the selected slot so an older token cannot replace sibling credentials. Login/list retrieval alone persists nothing; validation or save failure preserves the entire previous configuration. Deleting a printer removes only that slot; remaining slots can still represent the saved account.

参考 [ha-bambulab](https://github.com/greghesp/ha-bambulab/tree/0e027ff135a6d9265cb756d3e246747954c76722) 的固定提交（MIT，[许可证](../licenses/ha-bambulab-MIT.txt)），这是社区兼容协议，不是官方公开 OAuth 保证。两区域分别使用 `api.bambulab.com`／`api.bambulab.cn` 的登录、账户信息及绑定列表接口；短信请求沿用上游的 `bambulab.cn` 地址。使用标准 TLS 校验与真实应用标识，不移植 cloudscraper、浏览器指纹模拟或忽略证书校验。

The implementation references the pinned MIT community integration above, not a guaranteed official public OAuth API. Regional API hosts supply login, account and bound-printer data; SMS uses the upstream website host. Use verified TLS and an honest application identity without cloudscraper, browser fingerprint impersonation or certificate bypass.

中国大陆账户可以使用手机号或邮箱；先提交该区域的账号与密码，只有云端要求 `verifyCode`／TFA 时才进入验证码步骤。按当前账号是否含 `@` 显示邮箱或短信提示，手机号原样发送，不自动添加或删除国家代码；未收到验证码时点“发送短信验证码”或“发送邮箱验证码”。进入步骤本身不增加冷却，实际请求发送后限流 60 秒。TLS 或时间错误若发生在登录请求前，应先修复连接与时间，再进入验证码流程。

Mainland China accounts may use phone or email. Submit the regional account/password first; enter verification only when the cloud requests `verifyCode`/TFA. Labels follow whether the account contains `@`; preserve phone values without adding/removing country codes. If no code arrives, explicitly request an SMS/email code. Entering verification adds no cooldown; an actual send request is rate-limited for 60 seconds. Resolve connection/time failures before expecting a verification challenge.

当前不实现上游有 CSRF 兼容问题的验证器 TFA Cookie 流程；遇到该分支会说明限制并提供用户主动切换到邮箱／短信 `codeLogin` 的方式。验证码错误和过期可重试，发送新码有本机冷却；遇到限流、网页验证码、Cloudflare 或权限拒绝明确报错，可选 HA 导入或高级令牌配置。未发现可保证的令牌刷新契约，令牌被拒绝后需要重新登录。

The current release does not implement the upstream authenticator TFA cookie flow with known CSRF compatibility problems; it explains the limitation and allows an explicit switch to email/SMS code login. Incorrect/expired codes remain retryable, with a local resend cooldown. Rate limits, website challenges and cloud blocks fail visibly; HA import and advanced tokens remain available. No guaranteed token-refresh contract was found, so rejected tokens require reauthentication.

临时登录会话与共享绑定列表只保留在 RAM 中，五分钟有效；导入不延长会话。账号、密码、验证码和未导入令牌不写 NVS、TF 或浏览器存储；停止／关闭／到期管理入口、断网或挂起时撤销。登录、凭据复用和验证码等网络请求在后台工作器执行，手机只轮询阶段。会话编号防止旧页面、旧设备列表或晚到的响应影响新账户；复用还核对配置版本，拒绝配置变化前的选择。API 的账号／密码分别最多 127／255 个 UTF-8 字节、验证码为 4–12 位数字，是设备资源边界，不是 Bambu 官方长度要求。

Temporary sessions and the shared bound-printer list remain in RAM for five minutes; importing does not extend them. Account/password/code and unimported tokens stay outside NVS, TF and browser storage and are revoked on management stop/closure/expiry, disconnect or suspension. Workers perform login, reuse and verification requests while the phone polls state. Session IDs reject stale tabs, lists and responses; reuse also checks the configuration revision to reject a selection made before settings changed. The API bounds account/password to 127/255 UTF-8 bytes and numeric codes to 4–12 digits as device limits, not official Bambu requirements.

官方参考 / Official references:

- [Formlabs Dashboard client](https://developer.formlabs.com/core-concepts/dashboard-client/)
- [Formlabs Web API reference](https://formlabs-dashboard-api-resources.s3.amazonaws.com/formlabs-web-api-latest.html)（与本机 PreForm API 不同 / distinct from the local PreForm API）
- [Bambu printer network ports](https://wiki.bambulab.com/en/general/printer-network-ports)
- [Bambu LAN mode](https://wiki.bambulab.com/en/knowledge-sharing/enable-lan-mode)

## 凭据与验证边界 / Credentials and validation

管理服务由用户在设备上明确开启，二维码密钥短期有效；离开入口、停止、断网或过期后撤销。管理 API 使用同源访问和临时 `X-Pico-Setup-Key`，不开放跨域 CORS。GET 只返回凭据是否已设置，不提供令牌、用户名、Client ID/Secret 或 CA 原文回读。保存成功表示配置已持久化；连接验证由后台异步进行，不能把“已排队”当作账号和设备连接成功。

Management starts explicitly on the device and uses an expiring QR key, same-origin requests and `X-Pico-Setup-Key`, without cross-origin CORS. GET exposes credential-presence flags only. Saving confirms local persistence; background verification remains asynchronous, so queue acceptance is not account/device connection acceptance. See [bounded management API contract](HOME-LINK-WEB.md).

设备凭据保存在 NVS，不写 TF 游戏包、图书或笔记；设备未启用安全启动/Flash 加密时，NVS 不应被视作硬件加密保险箱。输入令牌默认遮蔽，离开输入页清除临时缓存。电脑预览仅在用户配置后访问真实服务，每个会话将私有配置存放在模拟 TF 之外（目录 0700，文件 0600）；Wi-Fi、BLE、USB 和电量仍是模拟。

Device credentials live in NVS, outside TF books, notes and game packages. Without secure boot/flash encryption, NVS is not a hardware-encrypted vault. Tokens are masked and transient editor buffers are wiped on exit. The native preview accesses real services only after configuration; each session stores its private configuration outside its simulated TF (directory 0700, file 0600). Wi-Fi, BLE, USB and battery remain simulated.

HTTP/TLS 及 MQTT 使用证书校验，不提供忽略校验的回退。自动测试用本机 HTTP/TLS 服务验证真实传输、解析、房间映射及控制契约，不能代替用户实际 HA、Bambu、Formlabs 账户和设备的验收。

HTTP/TLS and MQTT verify certificates with no insecure fallback. Tests exercise real loopback transports, parsing, area mapping and service contracts; they do not substitute for acceptance against the user's HA, Bambu or Formlabs devices/accounts.

连接 Wi-Fi 后，设备自动在后台进行网络授时，成功后每小时更新；单次 30 秒无响应时，60 秒后轮换时间服务器重试。先将时间写回 RTC 并读回确认，再同步系统与界面时钟；暂时无法提交时保留样本重试。断网仍使用有效 RTC，手动设时继续可用；手动保存会取消在途校时，下一次自动尝试延后一小时。网页预览不请求真实 NTP，也不改变电脑时间。

After Wi-Fi connects, the device synchronizes time in the background and refreshes hourly after success. A 30-second timeout retries after 60 seconds with the next time server. Write and verify the RTC before updating system/UI clocks; keep a pending sample for retry if it cannot be committed yet. Offline operation retains valid RTC time and manual setup. A manual save cancels pending synchronization and delays the next automatic attempt for one hour. The preview neither requests real NTP nor changes the computer clock.

设备 HTTPS 与 TLS MQTT 使用系统时钟验证证书有效期。启动及手动设时后，将有效 RTC 日期与时间同步到该时钟；RTC 尚未设为有效日期时，设备与手机管理页提示先在设备的“日期与时间”中设置，再重试。时间未校准、网络不可达和 TLS 安全连接／证书校验失败分别显示，不将前两者归为证书错误。手动设时见[电源、锁屏与时间](POWER-AND-TIME.md)。

Device HTTPS and TLS MQTT validate certificate dates against the system clock. Restore a valid RTC date/time at startup and after manual setup. If the RTC has not been set to a valid date, both device and phone management direct the user to the device's Date & Time settings before retrying. Unset time, unreachable networks and TLS secure-connection/certificate failure remain separate results. See [manual date/time setup](POWER-AND-TIME.md).

macOS 原生预览的 MQTT 使用 Homebrew OpenSSL 3；系统 LibreSSL 无法提供所需的主机名验证参数时，不会退回不校验的连接。设备固件使用 ESP-IDF TLS，无需电脑 OpenSSL。

MQTT in the macOS native preview uses Homebrew OpenSSL 3. System LibreSSL lacks the required hostname-verification option; the preview never falls back to unverified transport. Device firmware uses ESP-IDF TLS and does not require desktop OpenSSL.

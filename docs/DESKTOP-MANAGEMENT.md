# 电脑管理与离线画布协议 / Desktop management and offline canvas

电脑管理借用设备现有 Wi-Fi 连接，复用“手机管理页”的临时配对窗口。它提供电源偏好设置和灵犀画板原生 16 灰阶推送。接收后，设备把画布保存在 TF 卡，再由界面事件循环显示；断网后仍可查看已保存画布。

Desktop management borrows the device's existing Wi-Fi connection and the temporary pairing window used by the phone management page. It exposes power preferences and native 16-gray Lingxi Board upload. The device persists an accepted canvas on TF before the UI loop displays it; saved content remains available offline.

## 冻结契约 / Frozen contracts

- **冻结：** 所有扩展请求必须经过同一管理服务的配对鉴权、来源检查和到期检查；不另建免配对入口。**Frozen:** Every extension request passes the existing management service's pairing, origin and expiry checks; no unauthenticated alternative endpoint.
- **冻结：** HTTP 接收线程只校验、保存和排队，不切页、不画屏、不直接保存电源设置。**Frozen:** The HTTP receiver validates, persists and queues; it never navigates, paints or directly commits power settings.
- **冻结：** 画布只使用自有 TF 目录；失败保留上一幅可恢复内容，损坏主档不能覆盖有效备份。绘制回调不读卡。**Frozen:** Canvas storage stays in its owned TF directory; failures preserve recoverable prior content, corrupt primaries never replace valid backups, and rendering performs no file I/O.
- **冻结：** `202 accepted` 只表示接收成功；显示状态必须等实际显示出口完成后更新。**Frozen:** `202 accepted` means reception succeeded; display state changes only after the actual display boundary completes.

## 配对与传输 / Pairing and transport

用户在设备上开启管理入口后，服务使用当前已连接网络，生成十五分钟空闲有效的随机密钥。成功完成的已鉴权请求延长十五分钟空闲期限；无效请求、错误、浏览公开管理页面不续期，已经过期的密钥不能复活。二维码地址为 `http://<设备地址>/manage#key=<密钥>`；网页从片段读取密钥，请求通过 `X-Pico-Setup-Key` 头传递。重新开启管理、更换会话、到期、断网或转入文件传输后，旧密钥不能继续使用。设备在读取请求期间及提交前再次检查授权；慢上传不能越过有效期。

When the user opens management on the device, the service uses the connected network and generates a random key with a fifteen-minute idle lifetime. Successful authenticated requests extend that idle deadline; invalid requests, failures and public page browsing do not renew it, and an expired key cannot revive. The QR URL is `http://<device-address>/manage#key=<key>`; the page reads the fragment and sends `X-Pico-Setup-Key`. Reopening management, changing sessions, expiry, disconnecting or entering file transfer invalidates the old key. Authorization is checked again during reception and before dispatch, so a slow upload cannot outlive its pairing window.

接口使用本地 HTTP。设备校验 `Host`；有 `Origin` 时必须匹配设备当前来源。密钥不进入公开状态、日志或 TF 画布文件。电脑原生预览另外要求 `X-Pico-Preview-Session`，用来选择私有模拟 TF；该头不属于设备协议。预览暂存文件位于该会话的 `tf/.desktop-inbox/`，调用完成后删除。

The transport is local HTTP. The device validates `Host`; a supplied `Origin` must match its current origin. Keys stay out of public status, logs and TF canvas files. The native computer preview additionally requires `X-Pico-Preview-Session` to select its private simulated TF; this header is not part of the device protocol. Preview upload staging uses that session's `tf/.desktop-inbox/` and is deleted after dispatch.

## USB 无线引导 / Wireless onboarding over USB

「设置 → 手机与电脑管理」可在当前 Wi-Fi 上开启十五分钟管理窗口并显示二维码；离开页面不会关闭窗口。尚未配网时，可使用设备 Wi-Fi 页，或由已验证 USB 设备身份的电脑代填网络。USB 引导保留原页面，不自动切换到传书或设置页。临时密钥只保存在会话内存，不写 TF、配置或日志。

Settings → Phone & computer management opens a fifteen-minute management window on the current Wi-Fi and displays its QR code; leaving this page keeps the window open. A new device can join Wi-Fi through its Wi-Fi page or an identity-verified USB desktop connection. USB onboarding retains the current page. Session keys stay in RAM, outside TF, saved settings and logs.

USB 行协议前缀为 `PICO_WIFI1 `，随后为单个 ASCII JSON 对象和换行。完整请求行（含前缀、不含换行）最多 512 字节；JSON 响应最多 4096 字节。客户端应忽略其他日志行，仅接受匹配前缀、`id`、`device:"read-pico"` 和当前设备 `usb_serial` 的响应。`usb_serial` 为十二位大写十六进制，取自 Wi-Fi STA MAC，与 USB 磁盘序列号一致。客户端先用系统 USB 注册信息验证设备，并在 status、请求与 poll 全程独占同一串口连接。

The line protocol is `PICO_WIFI1 ` followed by one ASCII JSON object and a newline. A complete request line, including the prefix but excluding the newline, is at most 512 bytes; response JSON is at most 4096 bytes. Ignore unrelated log lines and accept only matching prefix, `id`, `device:"read-pico"` and the current device's `usb_serial`. This twelve-digit uppercase hexadecimal STA MAC matches the USB disk serial. Verify system USB registry identity first, then retain exclusive ownership of the same serial connection through status, requests and polling.

所有请求包含 `v:1`、`id`（整数 1–2147483647）和 `op`。不接受重复或未知字段、JSON 字符转义、非 ASCII 字节、指数或越界数字、前导零、尾随数据。SSID 与密码使用标准、规范 Base64，解码后为有效 UTF-8，不含 NUL 或控制字符；SSID 为 1–32 字节，密码为 0–64 字节。只有现有 Wi-Fi 控制器连接成功后才保存原有网络配置；协议不另存凭据。

Every request has `v:1`, an integer `id` from 1 through 2147483647, and `op`. Duplicate/unknown fields, JSON escapes, non-ASCII bytes, exponent/overflow numbers, leading zeroes and trailing data are rejected. SSID/password use canonical standard Base64 decoded to valid UTF-8 without NUL or control characters: 1–32 SSID bytes and 0–64 password bytes. Only the existing Wi-Fi controller saves a successful network profile; the protocol adds no credential store.

| `op` | 必需附加字段 / Required extra fields | 成功响应 / Success |
| --- | --- | --- |
| `status` | 无 / None | `200`, `phase:"done"`，只读 `ready`、`enabled`、`connected`、`busy`、`ip`、`ssid_b64`；没有密钥。/ Read-only fields, no key. |
| `scan` | 无 / None | `202`, `phase:"pending"`, `op_id`；未开启 Wi-Fi 时先开启再扫描。/ Enables Wi-Fi before scanning when necessary. |
| `connect` | `expected_serial`, `ssid_b64`, `password_b64` | `202`, `phase:"pending"`, `op_id`；使用现有异步连接控制器。/ Uses the existing asynchronous connection owner. |
| `poll` | `expected_serial`, `op_id` | 等待时 `202`；扫描完成 `200` 和 `networks`；连接完成 `200`, `phase:"ready"` 和 `manage`。/ Pending 202; scan results or connected management on 200. |
| `open` | `expected_serial` | 已连接网络时 `200`, `phase:"ready"` 和 `manage`；重新开启会轮换密钥并使旧操作失效。/ Opens on connected Wi-Fi, rotates the key and invalidates the previous operation. |

每个响应均包含 `v`、原请求 `id`、`device`、`usb_serial`、`ok`、数字 `status`、原请求 `op` 和 `phase`。`expected_serial` 必须精确匹配当前十二位设备身份（比较时忽略字母大小写）。异步操作同一时间只有一个；以约 500ms 间隔 poll，扫描期限 20 秒、连接期限 45 秒。协议超时不会额外强制取消控制器内部已排队的命令；控制器继续遵守自己的有界期限。

Every response includes `v`, the request's `id`, `device`, `usb_serial`, `ok`, numeric `status`, the request's `op` and `phase`. `expected_serial` must match the twelve-digit device identity, case-insensitively. Only one asynchronous operation runs at once. Poll approximately every 500ms; scanning has a 20-second protocol deadline and connection 45 seconds. A protocol timeout does not forcibly cancel an already queued controller command, whose own bounded deadline still applies.

扫描结果最多十六个，每项为 `{ssid_b64,rssi,requires_password,supported}`。只有身份匹配的 `open` 或成功连接的 `poll` 可以返回 `manage:{url:"http://<实际IPv4>/",key:"<32hex>",ttl_seconds:1..900}`。URL 不含密钥，调用管理 HTTP 时通过 `X-Pico-Setup-Key` 发送。失去连接、到期、重新配对或进入 USB 文件传输均使旧密钥失效；客户端保留未提交草稿，并重新获取本设备的新会话。

Scan results contain at most sixteen `{ssid_b64,rssi,requires_password,supported}` records. Only identity-matched `open` or a successful connection `poll` returns `manage:{url:"http://<actualIPv4>/",key:"<32hex>",ttl_seconds:1..900}`. The URL contains no key; send it as `X-Pico-Setup-Key` for management HTTP. Disconnect, expiry, re-pairing or USB file transfer invalidates the old key. Preserve unsubmitted desktop drafts while obtaining a fresh session for the same device.

错误使用 `phase:"error"` 和 `error`：`400 invalid_request`、`403 identity_mismatch`、`404 unknown_operation`、`409 busy/msc_active/not_connected`、`410 pairing_expired`、`503 not_ready/identity_unavailable/operation_failed/start_failed/capacity`、`504 timeout`。TF 被 USB MSC 独占时不读取或重配串口；电脑必须先完成下文的安全弹出和调试串口恢复，再开始无线引导。响应成功只证明协议完成，真实网络可达性仍须管理 HTTP 验证。

Errors use `phase:"error"` plus `error`: `400 invalid_request`, `403 identity_mismatch`, `404 unknown_operation`, `409 busy/msc_active/not_connected`, `410 pairing_expired`, `503 not_ready/identity_unavailable/operation_failed/start_failed/capacity`, or `504 timeout`. Serial is neither read nor reconfigured while USB MSC exclusively owns TF. Finish the safe-eject/debug-restoration handoff below before wireless onboarding. Protocol success still requires actual management HTTP reachability verification.

## 状态查询 / Status

`GET /api/manage/pico/status` 不包含请求体；成功返回 `200`：

`GET /api/manage/pico/status` has no request body and returns `200`:

```json
{
  "protocol": 1,
  "version": "<firmware version>",
  "config": {
    "auto_sleep_minutes": 5,
    "double_tap_wake": true,
    "lock_style": 0,
    "cover_layout": 0,
    "state": 0,
    "revision": 0
  },
  "canvas": {
    "supported": true,
    "width": 684,
    "height": 1216,
    "formats": ["image/bmp"],
    "max_bytes": 420000,
    "bit_depths": [1, 4],
    "gray_levels": 16,
    "state": "idle",
    "revision": 0
  }
}
```

`version` 表示正在响应的固件版本。配置值是当前已应用的快照，不是等待中的请求值。两类 `revision` 都是本次启动内的计数，重启后从零开始；它们不能代替跨设备或跨启动的身份标识。

`version` identifies the responding firmware. Config values are the currently applied snapshot, not a queued request. Both revisions are per-boot counters starting at zero after restart; they are not cross-device or persistent identities.

| 画布状态 / Canvas state | 含义 / Meaning |
| --- | --- |
| `idle` | 本次启动尚未接收或显示画布。/ No canvas received or displayed in this boot. |
| `pending` | 已保存并排队；界面忙时延后采用。/ Persisted and queued; adoption waits while the UI is busy. |
| `saved` | 已保存并由主循环采纳，当前阅读或画稿继续显示；等待用户打开画板或进入外电画板锁屏。/ Persisted and adopted while retaining reading/drawing; awaits a visible board page or external-power board lock. |
| `displayed` | 该画布已到达显示出口并报告成功，不等同于实机观感验收。/ The display boundary reported success; this is not physical visual acceptance. |
| `error` | 保存或显示发生错误；保留仍可恢复的上一幅内容。/ Persistence or display failed; recoverable prior content is retained. |

新的有效画布获接受时，`canvas.revision` 增加一次。重复提交在已有画布排队时返回 `409`，保留原队列和状态。重启后从 TF 打开画布也必须经过显示出口，才能进入 `displayed`。

Each newly accepted valid canvas increments `canvas.revision` once. A submission while another canvas is pending returns `409` and retains the existing queue/state. A canvas reopened from TF after restart must also pass the display boundary before becoming `displayed`.

## 电源偏好 / Power preferences

`PUT /api/manage/pico/config` 使用 `Content-Type: application/json`（可带 `charset=utf-8`），请求体必须包含以下两项，并可选提供 `lock_style`：

`PUT /api/manage/pico/config` uses `Content-Type: application/json` (optionally `charset=utf-8`) and requires these two fields, with optional `lock_style`:

```json
{"auto_sleep_minutes":5,"double_tap_wake":true}
```

| 字段 / Field | 可用值 / Allowed values |
| --- | --- |
| `auto_sleep_minutes` | 整数 `0`、`1`、`3`、`5`、`10`、`30`；`0` 关闭空闲自动休眠。/ Integers `0`, `1`, `3`, `5`, `10`, `30`; `0` disables idle auto-sleep. |
| `double_tap_wake` | JSON 布尔值；控制锁屏后的低功耗触摸双击唤醒偏好。/ JSON boolean controlling the low-power double-tap touch wake preference while locked. |

有效请求返回 `202` 和 `{"ok":true,"accepted":true}`。界面线程在允许后台操作时调用统一电源设置事务，成功保存后才更新快照。`config.state` 分别为 `0`（空闲）、`1`（排队）、`2`（应用成功）、`3`（保存失败）；只有成功应用才增加 `config.revision`。失败保持此前设置。已有配置请求排队时返回 `409`；配置队列与画布队列可以同时存在。

A valid request returns `202` and `{"ok":true,"accepted":true}`. When background work is permitted, the UI thread calls the unified power-setting transaction and updates the snapshot only after successful persistence. `config.state` is `0` (idle), `1` (queued), `2` (applied), or `3` (save failed). Only successful application increments `config.revision`; failure retains previous settings. A second queued config request returns `409`. Config and canvas requests may be pending independently.

## 设备原生画板 / Device-rendered boards

信息卡片发送语义数据，由 Pico 按当前方向绘制文字、数值、曲线、排行和图片素材。电脑不得将信息界面截图作为新协议的降级替代。能力 `canvas.model_protocol:1`、`canvas.model_max_bytes:65536`（USB 状态中为同名顶层字段）表示设备支持；缺少该能力时提示更新固件。

Information cards carry semantic data. Pico renders text, values, charts, rankings and media in its current orientation. Do not silently fall back to screenshots for this protocol. Capability `canvas.model_protocol:1` and `canvas.model_max_bytes:65536` (top-level fields in USB status) advertises support; clients request a firmware update when absent.

- 普通画板：`POST /api/manage/pico/canvas/model`；独立网站流量频道：`POST /api/manage/pico/apps/cloudflare/model`。追加 `/show` 仅代表明确手动展示意图，锁屏时不强行切页。/ General board and independent Cloudflare channel; append `/show` for explicit manual presentation, without forcing navigation while locked.
- 类型：`application/vnd.readpico.board-model+json`，UTF-8 JSON 最大 65536 字节；网络仍使用相同的管理会话鉴权。/ UTF-8 JSON, at most 65536 bytes, under the existing management session authorization.
- USB：`RPICO BOARD BEGINMODEL <canvas|cloudflare> <task> <bytes> <crc32> <show>`，后续 `CHUNK`、`COMMIT`、`ABORT` 和回执与原直传一致；不导出 TF。/ Reuse the direct chunk transaction and receipts without exporting TF.

```json
{
  "schema": 1,
  "board_id": "desk",
  "generated_at": 1793116800,
  "columns": 1,
  "dark": false,
  "modules": [{
    "kind": "weather", "id": "weather-home", "title": "天气",
    "subtitle": "上海",
    "metrics": [{"label": "温度", "value": "23", "unit": "°C"}],
    "rows": [{"label": "风速", "value": "3 m/s"}],
    "series": [20, 21, 23, 24]
  }]
}
```

根对象接受 `schema`、`board_id`、`generated_at`、`columns`（1 或 2）、`dark`、`modules`、可选 `assets`、`text_scale`（2／3／4，默认 3）与下述 `world_map`。模块最多 12 个，每个模块为语义种类和内容：`kind/id/title/subtitle/body/footer`，最多 8 个 `metrics`、24 个 `rows`、60 个 `series` 数字，可选 `timestamp/duration/progress/running/asset`；不接受界面坐标、绘图脚本、远程路径或凭据。所有字符串按 UTF-8 字节有界，重复字段、未知字段、无效数值或素材引用均拒绝。

The envelope also accepts optional `assets`, `text_scale` (2/3/4, default 3), and `world_map` below. Up to twelve modules carry semantic kinds/content, eight metrics, twenty-four rows, sixty numeric series samples and optional timing/progress/media references. UI coordinates, drawing scripts, remote paths and credentials are not part of the protocol. UTF-8 lengths are bounded; duplicate/unknown fields, invalid numbers and dangling asset references are rejected.

所有模块支持 `span:1|2`（默认 1；2 占满整行）。`now_playing` 单独支持 `media_layout:"horizontal"|"vertical"`（默认横排）；`cloudflare` 单独支持 `presentation:"worldMap"|"ranking"`（默认地图）、`rank_metric:"requests"|"traffic"`（默认请求数）和 `regions:[{code,value}]`。地区最多 250 个，代码为互不重复的 1–32 字节可打印 ASCII，值为 0 至 1e30 的有限数字；未识别的地区保留排行，不伪造地理位置。方向变化时设备重新排版，密集布局优先保留可读摘要，不缩成细小字。

Every module accepts `span:1|2` (default 1; 2 fills a row). Only `now_playing` accepts `media_layout:"horizontal"|"vertical"` (default horizontal). Only `cloudflare` accepts `presentation:"worldMap"|"ranking"` (default worldMap), `rank_metric:"requests"|"traffic"` (default requests), and `regions:[{code,value}]`: at most 250 distinct printable ASCII codes of 1–32 bytes, each with a finite value from 0 through 1e30. Unknown regions remain in rankings without invented coordinates. Device rotation relayouts the same data; dense layouts retain readable summaries.

地图展示必须同时携带根字段 `world_map`，内容为标准规范 Base64 编码的 `resources/cloudflare-world-dots.bwm`，解码后最多 8192 字节；没有地图模块时禁止携带。它是自然地理采样数据，不是界面位图，设备仍原生绘制国家点阵与热度。文件随模型原子保存，无单独 TF 安装步骤。当前资源 4588 字节、147 个国家／地区、1841 个点，SHA-256 为 `8cbb9e3583e5a452a4c526d860e5ea9d5ea4171d785b789e4e9453c48788dcb1`；来源为公共领域 Natural Earth 1:110m，生成依据和许可记录在多屏灵犀的 `docs/CLOUDFLARE-WORLD-MAP.md`，资源清单为 `resources/board-resources.json`。

World-map presentation requires root `world_map`, canonical Base64 of `resources/cloudflare-world-dots.bwm`, at most 8192 decoded bytes; it is forbidden without a map module. This is geographic sample data rather than a rendered UI bitmap. Pico paints country dots and heat natively, and saves the resource atomically with the model without a separate TF installation. The 4588-byte resource has 147 country/region codes and 1841 dots, SHA-256 `8cbb9e3583e5a452a4c526d860e5ea9d5ea4171d785b789e4e9453c48788dcb1`, from public-domain Natural Earth 1:110m. Provenance is recorded in Lingxi's `docs/CLOUDFLARE-WORLD-MAP.md` and the resource manifest in `resources/board-resources.json`.

`BWMAP001` 格式为小端：8 字节魔数，随后依次是 `uint16 columns=120/rows=52/country_count/point_count`、`uint32 payload_bytes/payload_crc32`，共 24 字节头；国家记录为 2 字节大写代码与 `uint16 start/count`，其后点为 `uint16 row*120+column`。解析验证总长度、CRC32、排序唯一代码、连续点区间与全图点唯一性。JSON 树、模型或地图内存不足均返回 `out_of_memory`，不得作为损坏存档删除。

The little-endian `BWMAP001` format has an 8-byte magic, `uint16 columns=120/rows=52/country_count/point_count`, then `uint32 payload_bytes/payload_crc32` (24-byte header). Six-byte country records contain two uppercase code bytes and `uint16 start/count`; following points are `uint16 row*120+column`. Validation checks exact length, CRC32, sorted unique codes, contiguous point ranges and globally unique points. JSON, model or map allocation failures return `out_of_memory` and must not cause stored snapshots to be treated as corrupt.

素材为可选内嵌 `assets:[{id,width,height,format:"gray4",data}]`：`id` 为像素数据的 SHA-256 小写摘要，`data` 为标准 Base64，行步长 `(width+1)/2`，高半字节是左像素；最多 8 张、单边最多 192 像素、解码合计最多 32768 字节，且整个 JSON 仍须在 64 KiB 内。这用于封面、专辑图和任务照片；独立壁纸和大图保留下面的图片素材协议。

Embedded assets use canonical Base64 packed gray16 pixels, high nibble first, with a lowercase SHA-256 ID. Limits are eight images, 192 pixels per side and 32768 decoded bytes combined, within the same 64 KiB envelope. These are cover/artwork/task media; standalone wallpapers and large images retain the image protocol below.

保存与显示仍分开确认。后台接收不切换读书或画稿、不交出 TF，保存失败保留上一版。时钟/进度的本地变化只改变绘制状态，不冒充新数据传输的 `revision`；客户端内容去重忽略单纯的 `generated_at` 变化。

Persistence and display remain separate receipts. Background reception preserves reading/drawing and TF ownership; failed saves retain the previous snapshot. Local clock/progress repainting does not increment the transfer revision. Client content deduplication ignores changes to `generated_at` alone.

## 图片推送 / Canvas upload

`POST /api/manage/pico/canvas` 使用精确的 `Content-Type: image/bmp`，请求体为原始 BMP 字节，不使用 JSON、Base64 或表单封装。

`POST /api/manage/pico/canvas` uses the exact `Content-Type: image/bmp`. The body is raw BMP bytes, without JSON, Base64 or multipart wrapping.

以下为兼容的 1 位 BMP 规则；新增 4 位格式见下一小节。

The following rules describe the compatible one-bit format; the new four-bit format follows below.

- 文件最多 `110000` 字节，签名 `BM`；文件头长度必须等于实际字节数。/ Maximum `110000` bytes, `BM` signature, and the declared file length equals the actual body length.
- Windows `BITMAPINFOHEADER` 为 `40` 字节；`planes=1`、`bitCount=1`、`compression=BI_RGB(0)`。/ A `40`-byte Windows `BITMAPINFOHEADER`, `planes=1`, `bitCount=1`, `compression=BI_RGB(0)`.
- 宽高只接受 `684×1216` 或 `1216×684`；负高度表示自上而下存储。/ Dimensions are exactly `684×1216` or `1216×684`; a negative height denotes top-down storage.
- 两项调色板必须分别落在深色和浅色亮度范围；`biClrUsed` 为 `0` 或 `2`。/ The two palette entries must fall on opposite dark/light luminance sides; `biClrUsed` is `0` or `2`.
- 像素偏移至少为 `62`；每行按四字节对齐，偏移后的数据长度必须精确等于行跨度乘以高度。/ Pixel offset is at least `62`; rows align to four bytes, and the remaining data length exactly equals stride times height.

设备不会拉伸画面。源图按比例居中适配当前显示方向，剩余区域留白，支持四个设备朝向。新图先保存到 `/sdcard/readpico/desktop/canvas.bmp`，使用 `.tmp` 提交和 `.bak` 恢复；卡缺失、未挂载或正以 USB 导出时拒绝写入。已载入内存的画面可继续绘制，绘制时不重新读卡。

The image is centered with aspect ratio preserved in the current display orientation, with white letterboxing across all four device orientations. New content is saved to `/sdcard/readpico/desktop/canvas.bmp` using `.tmp` commit and `.bak` recovery. Missing, unmounted or USB-exported storage refuses new writes. An image already loaded in RAM remains drawable without rereading the card.

成功接收返回 `202` 和 `{"ok":true,"accepted":true}`。客户端应继续查询状态；接收结果不包含“已显示”的承诺。已保存画布通过“设置 → 首页导航 → 添加入口 → 灵犀画板”加入首页，重启后点击该入口可从 TF 打开。

Successful reception returns `202` and `{"ok":true,"accepted":true}`. Clients poll status afterward; acceptance does not promise display completion. Add the saved canvas via “Settings → Home navigation → Add destination → Lingxi Board”; tapping that destination after restart opens the TF file.

后台推送不再隐式打开画板。`canvas.state=saved` 表示内容已保存并在主循环采纳，但尚未显示；客户端可以结束传输并说明「已保存，等待显示」，不能把它当作 `displayed`，自动轮播也不能据此推进。当前画板页面或外电画板锁屏真正完成刷新后才进入 `displayed`。下一幅画板可替换已保存待显示内容，但已有接收/排队任务仍返回 `409`。外电画板锁屏只保留既有鉴权会话，不延长十五分钟配对窗口。

Background reception no longer opens the board implicitly. Canvas state `saved` confirms persisted, UI-adopted content that has not been shown; clients may finish transfer with an awaiting-display notice but must not treat it as `displayed` or advance an automatic carousel. Only an actual completed present on the board page or externally powered board lock changes it to `displayed`. A new frame may replace saved-but-hidden content; in-flight reception/adoption still rejects another request with `409`. The external-power board lock retains existing authentication without extending its fifteen-minute pairing window.

### 灵犀画板的原生灰阶 / Native grays for Lingxi Board

新固件的 `canvas` 能力公布 `bit_depths: [1, 4]`、`gray_levels: 16` 与 `max_bytes: 420000`。画板使用 684×1216 或 1216×684 原始面板尺寸；显示管线保留每个像素的 4 位值，以 GL16／GC16 呈现，不转成黑白或抖点。旧 1 位客户端与已存画布仍然可用；旧固件缺少这些能力时，客户端应明确提示升级或选择黑白兼容推送。

New firmware advertises 1/4-bit input, 16 native gray levels and a 420000-byte canvas bound. Lingxi Board uses the native 684×1216 or 1216×684 panel size and retains each pixel's 4-bit value through GL16/GC16 presentation, without thresholding or dithering. Existing one-bit clients/files remain supported; clients encountering older capabilities should explicitly offer firmware upgrade or monochrome compatibility.

4 位 BMP 必须为 40 字节 `BITMAPINFOHEADER`、单平面、无压缩 `BI_RGB`，`bfOffBits=118`，文件长度精确等于声明长度。宽高只能为上述两组，负高度表示从上到下；每行按 4 字节对齐，因此竖幅为 418422 字节、横幅为 415990 字节。`biSizeImage` 为 0 或精确像素区长度，`biClrUsed` 与 `biClrImportant` 为 0 或 16。16 项色板必须完整且唯一覆盖 0、17、…、255，RGB 三通道相同，保留字节为 0，不接受透明度。允许色板换序，绘制时按色板还原亮度。超长、短体、压缩、彩色／透明色板和缺灰级文件均拒绝；存储失败保留上一幅及有效备份。

Four-bit BMPs require a 40-byte header, one plane, BI_RGB, offset 118 and exact declared/actual length. Only the two native sizes are valid, with negative height denoting top-down storage. Four-byte row alignment yields 418422 bytes in portrait and 415990 in landscape. Image-size fields may be zero or exact; color counts may be zero or 16. The complete unique palette must contain 0, 17, …, 255 with equal RGB channels and zero reserved/alpha bytes. Palette order may vary; rendering resolves luminance through the palette. Reject oversized, truncated, compressed, colored, alpha-bearing or incomplete-gray files, and retain prior recoverable content on storage failure.

为保持自定义图片锁屏的原生16灰显示，锁屏与画板使用相同的 1／4 位 BMP 校验。1 位文件仍保留 `110000` 字节上限，4 位文件最多 `420000` 字节；旧格式继续可用。

Custom locks share the canvas one-bit/four-bit BMP validation to retain native 16-gray images. One-bit files keep their `110000`-byte bound; four-bit files allow up to `420000` bytes. Existing one-bit images remain compatible.

## 错误与能力边界 / Errors and boundaries

| 状态码 / HTTP code | 含义 / Meaning |
| --- | --- |
| `400` | 请求、路径或查询无效；扩展端点不接受查询参数。/ Invalid request/path/query; extension endpoints accept no query parameters. |
| `401` / `403` | 未配对、到期或来源不匹配。/ Unpaired, expired or origin mismatch. |
| `404` | 未知扩展路径或方法。/ Unknown extension path or method. |
| `409` | 存储不可用，或同类请求仍在排队。/ Unavailable storage or another request of the same kind is queued. |
| `413` / `415` / `422` | 超限、媒体类型错误或内容格式无效。/ Oversize, unsupported media type or invalid content. |
| `503` / `507` | 内存不足或画布保存失败。/ Insufficient memory or canvas persistence failure. |

HTTP、原生预览和设备共享内容验证及设置控制器。预览的网络、TF 可用性、显示出口与电源是主机模拟边界；墨水屏残影、实际低功耗、触摸唤醒及无线兼容性需要设备测试。此协议不提供远程文件浏览、全库同步或任意程序执行。

HTTP, native preview and device share content validation and settings control. Preview networking, TF availability, display boundary and power remain host simulation boundaries. Ghosting, real low power, touch wake and wireless compatibility require device testing. This protocol does not expose remote file browsing, full-library sync or arbitrary program execution.

## 管理网页离开保护 / Management page departure protection

管理页有未保存的 HA、打印机、房间或温度输入，或者设备操作、Bambu 账户登录及验证尚未完成时，浏览器刷新、返回或关闭会使用浏览器自己的离开提示。手机浏览器强制关闭进程或系统回收标签页时可能不触发此提示，敏感输入不会因此写入本地存储。明确重新读取或结束管理也使用相同的状态检查；取消保留原输入，读取失败或读取期间新增输入时保留草稿。

Browser refresh, back and close use the browser's departure warning for unsaved HA, printer, room or temperature inputs, pending device actions and unfinished Bambu authentication. Mobile process termination or tab eviction may bypass this browser event; sensitive inputs are never persisted as a workaround. Explicit reread and disconnect use the same state check. Cancelling retains inputs, as do failed rereads and edits made while a reread is in flight.

网页提供同步、无参、只读的 `window.readPicoManagementDepartureState()`，仅返回 `{dirty: boolean, working: boolean}`，不返回输入或凭据。普通轮询与已完成登录不视为待办。Mac 宿主在文档开始时注入不可写的 `window.__readPicoNativeDepartureGuard = true`，声明由原生窗口逐次检查离开；网页据此避免重复的浏览器提示。此标记不授权原生关闭，也不清除草稿；旧固件缺少公开函数时 Mac 仍使用兼容检查。

The synchronous, zero-argument, read-only `window.readPicoManagementDepartureState()` returns only `{dirty: boolean, working: boolean}`, without inputs or credentials. Routine polling and completed login do not count as pending work. The Mac host injects immutable `window.__readPicoNativeDepartureGuard = true` at document start to own each departure check, avoiding a second browser warning. This marker neither authorizes native closure nor clears drafts. The Mac retains its compatibility check when older firmware lacks the public function.


## 自定义图片锁屏 / Custom image lock screen

锁屏图片与电脑信息画布独立。`GET /api/manage/pico/status` 的 `image_lock` 对象提供 `supported`、`available`、`width`、`height`、`formats`（`["image/bmp"]`）、`max_bytes`（420000）、`bit_depths`（`[1,4]`）、`gray_levels`（16）、`state` 与 `revision`。`width`／`height` 是当前图片的尺寸；没有图片时为 0，上传尺寸仍必须符合画布协议的 684×1216 或 1216×684。

Custom lock images are independent from desktop information canvases. The status `image_lock` object advertises `supported`, `available`, image `width`/`height` (zero without an image), `formats` (`["image/bmp"]`), `max_bytes` (420000), `bit_depths` (`[1,4]`), `gray_levels` (16), `state` and `revision`. Upload dimensions remain 684×1216 or 1216×684 under the same strict one-bit/four-bit BMP validation as canvases.

`POST /api/manage/pico/lock-image` 接收原始 `image/bmp`，持久保存至 `/sdcard/readpico/lockscreen/custom.bmp`，使用独立 `.tmp`／`.bak` 提交和恢复。返回 `202 {"ok":true,"accepted":true,"revision":N}` 后，客户端必须继续轮询，确认同一 `revision` 的状态从 `pending` 进入 `saved`。`idle` 表示本次启动未上传；`error` 表示保存失败。`202` 不代表已应用。上传不会切换设备页面、刷新现有内容、覆盖电脑画布或自动选择锁屏样式。

The raw-BMP POST stores its own `custom.bmp` with independent temporary and backup files. After `202` with a revision, clients must poll until that revision reports `saved`; `pending` is only accepted work, `idle` means no upload this boot, and `error` means failed storage. Upload never navigates, repaints the current page, replaces the desktop canvas, or automatically selects a lock style.

`GET /api/manage/pico/lock-image` 返回已保存的原始 `image/bmp`，供电脑预览。它使用与其他管理接口相同的临时配对密钥、Host／Origin 与到期校验，禁止查询参数；未配对或已到期为 `401`，无可用图片为 `404`，TF卡未挂载／USB占用或正在保存为 `409`。图片不会以公开静态资源提供。

GET returns the saved BMP for desktop preview under the same temporary pairing, Host/Origin and expiry checks, with no query string. Unpaired/expired requests return `401`, missing images `404`, and unavailable or busy storage `409`. Images are never public static resources.

`config.lock_style` 枚举：`0` 翻页时钟、`1` 划线摘录、`2` 藏书封面、`3` 保持当前页面、`4` 自定义图片、`5` 灵犀画板。更新配置时可选携带同名整数字段；旧客户端省略此字段时保持当前样式。选 `4` 必须已有有效图片，否则返回 `409 lock_image_missing`。仍须等待 `config.state=2` 和新 `config.revision` 才算应用完成；新样式在下一次锁屏生效，不立即遮挡当前页面。

The optional config `lock_style` integer maps to flip clock (0), excerpts (1), book covers (2), current page (3), custom image (4), and Lingxi Board (5). Omission by old clients preserves the selected style. Selecting 4 requires a valid image, otherwise `409 lock_image_missing`. Wait for config state 2 and its new revision before reporting success; appearance applies at the next lock.

设备入口为「设置 → 电源与锁屏 → 锁屏外观 → 自定义图片」，同页可预览。也可以用USB将标准BMP放到上述TF路径后重新进入该设置页。锁屏使用进入前的方向，图片等比居中留白、不拉伸；断网和重启后继续可用。图片丢失或卡不可用时，本次锁屏安全保留当前页面及锁屏提示，不改变所选样式。电源键单击、可选触屏双击唤醒、自动锁屏和关机画面标识仍由统一电源控制器处理。

On-device selection and preview live under Settings → Power & lock → Appearance → Custom image. A standard BMP may also be copied over USB to that TF path; reenter Appearance to detect it. Images aspect-fit in the orientation active before locking, retain offline and after reboot, and fall back to the current page plus lock hint when the file/card becomes unavailable without changing the preference. The unified controller retains power-key, optional double-tap, idle-lock, and powered-off-hint behavior.

## USB 直连画板 / Direct USB board transport

`RPICO BOARD STATUS` 的 `direct_protocol:1`、`chunk_bytes:1024` 和 `display_intent:true` 表示支持串口直传。电脑在同一受控串口会话中发送下列命令；不请求 USB 磁盘、不重新枚举、不把 TF 交给电脑。只允许原生 4 位 16 灰 BMP，最大 420000 字节。

`direct_protocol:1`, `chunk_bytes:1024` and `display_intent:true` in `RPICO BOARD STATUS` negotiate direct transfer on the same owned serial session, without exporting TF or reenumerating USB. Only native-size four-bit sixteen-gray BMP is accepted, bounded to 420000 bytes.

```text
RPICO BOARD BEGIN <32 lowercase hex task> <image bytes> <8 hex crc32> <0 background / 1 show>
RPICO BOARD CHUNK <task> <offset> <length 1..1024>
<exact binary bytes; no appended newline>
RPICO BOARD COMMIT <task>
RPICO BOARD ABORT <task>
```

控制行使用 LF（兼容 CRLF），载荷按精确长度收取，任意字节都不作为命令。BEGIN 返回 `ready`；每块完整载荷后返回 `chunk`，不在 CHUNK 头后提前回应；完整校验和原子保存成功后返回 `committed`。回应以独占一行的 `RPICO ` 加 JSON 编码，包含 `board_direct:true`、`protocol:1`、`task`、`phase`、`next_offset` 和 `accepted`。电脑逐块等待确认，不在未知提交结果时重复发送二进制。

Control lines use LF, with CRLF supported. Payload is exact-length binary and never parsed as commands. BEGIN replies `ready`; each complete payload replies `chunk` without an early header acknowledgment; complete CRC validation and atomic persistence precede `committed`. Replies occupy standalone `RPICO ` JSON lines containing `board_direct:true`, `protocol:1`, `task`, `phase`, `next_offset` and `accepted`. Wait for each chunk acknowledgment and never blindly retry binary after an uncertain commit.

全部分块仅暂存在 PSRAM；COMMIT 才检查 BMP 与 CRC32 并原子写入专有目录。15 秒无进展、拔卡、TF 已导出、取消或校验失败均保留之前的画板。STATUS 按任务继续报告 `saved` 或实际显示出口完成后的 `displayed`，接收成功不代表已经显示。自动更新使用 background；手动立即展示使用 show，界面只在空闲且未锁屏时切到画板。局域网对应 `POST /api/manage/pico/canvas` 与 `POST /api/manage/pico/canvas/show`；HTTP 线程只提交展示意图，不直接切页。

Chunks remain staged in PSRAM until COMMIT validates BMP and CRC32 and atomically writes the owned directory. Fifteen seconds without progress, card loss/export, abort or failed validation retain the prior board. STATUS reports task-specific `saved` or `displayed` only after the display boundary; reception is not presentation. Automatic updates use background; manual show intent opens the board only when idle and unlocked. LAN equivalents are `POST /api/manage/pico/canvas` and `POST /api/manage/pico/canvas/show`; HTTP only queues the intent.

## USB 与 TF 文件交接 / USB and TF ownership handoff

以下磁盘交接用于文件管理和旧版画板兼容；支持直连的画板推送不经过此流程。/ The disk handoff below is for file management and legacy board compatibility; negotiated direct board updates bypass it.

TF 文件与应用管理通过设备导出的 TF 磁盘进行，不能把串口连接成功视为磁盘已挂载。电脑在用户明确点击开启后，只读查询 `RPICO STATUS` 确认设备，再发送 `RPICO USB START`；收到完整 `RPICO {"accepted":true}` 行后立即关闭串口，等待系统出现 TF 磁盘并由用户选择。此时设备暂停本地读卡、网络管理与传书，内部 Flash 不导出。已有 `accepted` 只表示开始交接，电脑仍需等待磁盘；拒绝／超时不应显示已连接。

TF files/apps are managed through the device-exported TF volume, not the serial link. Following explicit user action, the desktop reads STATUS, sends USB START, closes serial after the complete accepted acknowledgment, then waits for a mounted TF volume that the user selects. Local storage, network management and transfer pause while MSC owns the card; internal Flash is never exported. Acceptance starts handoff and does not attest a mounted disk.

完成后先停止文件操作并在电脑安全弹出。设备只在收到主机明确弹出命令后自动停止 MSC、恢复调试串口并重新挂载／读写验证 TF；普通拔线不证明电脑缓存已写完，不自动重新占用卡。归还后 `RPICO STATUS` 的 `device`、`version`、`usb_held`、`usb_active`、`usb_connected`、`usb_host_ejected`、`usb_debug_restored`、`usb_error` 与原有 `mounted`／`read_verified`／`write_verified` 提供只读确认；MSC 期间电脑不得再打开或轮询串口。物理枚举、真实安全弹出与重连仍须实机验收。

Stop file operations and safely eject on the computer first. Only explicit host eject triggers automatic MSC stop, serial restoration and TF remount/read-write verification; disconnect alone does not prove cache flush. STATUS exposes device/version and read-only USB ownership, connection, eject, debug-restoration and error fields alongside existing verified-mount fields. Never reopen or poll serial during MSC. Physical enumeration, safe eject and reconnect remain device acceptance checks.

支持原位传输的固件在 `RPICO STATUS` 返回严格布尔 `usb_background_supported:true` 和当前 `usb_background`。电脑仅在能力为 `true` 时发送精确命令 `RPICO USB START BACKGROUND`，收到 `RPICO {"accepted":true,"usb_background":true}` 后关闭串口，继续等待真实 TF 磁盘。旧 `RPICO USB START` 保留切入 USB 页面行为；缺失能力字段的旧固件不能被当作原位传输。

Firmware supporting retained-page transfer reports strict boolean `usb_background_supported:true` and current `usb_background` in STATUS. Only after negotiating that capability may the desktop send the exact `RPICO USB START BACKGROUND` command. Close serial after the complete `RPICO {"accepted":true,"usb_background":true}` acknowledgment, then wait for the actual TF volume. Legacy USB START still opens the USB page; an absent capability must not be treated as retained-page support.

后台交接保留当前页面和画面，底部显示 USB 状态与安全结束入口；本地阅读、应用、字体、网络传书和旋转暂停，电源操作也等待交接结束。电脑安全弹出后自动接回并校验 TF；没有新画板时恢复原页面，有待接收的灵犀画板时才加载新画面。普通 USB 断线不会强行抢回 TF；停止失败继续保留独占状态，可重试安全结束。

The background handoff retains the current route/frame and adds a bottom USB-status/safe-end strip. Local readers, apps, card fonts, network transfer and rotation pause, and power actions wait for release. Safe host eject stops MSC and verifies TF before resuming the page or receiving a pending Lingxi board. Disconnect alone never reclaims TF, and stop failure keeps exclusive ownership with a safe-end retry.


### 书库封面排布 / Book-cover layouts

`config.cover_layout` 是可选兼容字段：`0` 全屏单封面、`1` 双封面、`2` 书库拼贴、`3` 留白海报、`4` 全屏书墙。`PUT /api/manage/pico/config` 可与电源设置和 `lock_style` 一起提供此整数字段；省略时保留设备当前排布。仅使用已提交书单的真实封面，最近阅读优先；海报按比例完整显示，书墙仅居中裁切，不拉伸。必须等待配置的新 revision 与 state=2 确认保存；下一次封面锁屏生效。

The optional `config.cover_layout` appends poster (3) and full-screen cropped wall (4) to the original single (0), double (1) and collage (2) IDs. Omission preserves the current layout. The authenticated config endpoint validates exact integers 0–4 and confirms persistence through its state/revision receipt; changes apply on the next book-cover lock screen. Rendering reads prepared, owned RAM only and never rereads books.


### 双方向原子画板 / Atomic orientation pairs

`dual_orientation:true` 与 `pair_max_bytes:840032` 表示支持同一快照的横竖两版。客户端一次渲染并提交 684×1216 竖版与 1216×684 横版；设备按传感器方向选择已保存的对应版，旋转不依赖电脑再次推送。图片或手绘的 fit/crop、缩放与位置由客户端按原配置分别渲染到两个尺寸；设备保持原生像素。旧单图端点和 USB BEGIN 继续可用。
`dual_orientation:true` and `pair_max_bytes:840032` negotiate both layouts from one data snapshot. Clients submit portrait 684×1216 and landscape 1216×684 together; sensor rotation chooses the saved local member without another desktop push. Clients apply the configured fit/crop, scale and position separately to each layout for images or drawings; the device retains native pixels. Existing single-image endpoints and USB BEGIN remain compatible.

容器为 32 字节头后顺序拼接竖、横 BMP4。头的 0..7 是 ASCII `PICOPAIR`；小端 UInt32 字段依次为 offset8 version=1、12 headerBytes=32、16 portraitBytes、20 portraitCRC32、24 landscapeBytes、28 landscapeCRC32。两个成员各不超过 420000 字节，并分别通过尺寸、16灰色板与 IEEE CRC32 校验。整个容器作为单个专有文件以一次原子替换提交，任一方向错误或保存失败保留旧整套及其备份，不会混用不同快照。
The 32-byte header precedes portrait then landscape BMP4 bytes. Bytes 0..7 are ASCII `PICOPAIR`; UInt32 little-endian fields are offset8 version=1, 12 headerBytes=32, 16 portraitBytes, 20 portraitCRC32, 24 landscapeBytes, 28 landscapeCRC32. Each member is bounded to 420000 bytes and validated for orientation dimensions, sixteen-gray palette and IEEE CRC32. One owned container is atomically replaced; a failed member or persistence keeps the entire prior pair and backup without mixing snapshots.

USB 使用 `RPICO BOARD BEGINPAIR <task> <totalBytes> <totalCRC32hex> <show0or1>`，后续 CHUNK、COMMIT 和 ABORT 与旧协议相同。HTTP 使用 `POST /api/manage/pico/canvas/pair` 或 `/api/manage/pico/canvas/pair/show`，Content-Type 为 `application/vnd.readpico.canvas-pair`。STATUS 提供统一 `revision` 与当前 `orientation`（portrait/landscape），接收保持 pending→saved/displayed 的原有回执边界。后台不切页，手动展示也不解锁。
USB uses `RPICO BOARD BEGINPAIR <task> <totalBytes> <totalCRC32hex> <show0or1>` followed by existing CHUNK, COMMIT and ABORT. HTTP uses `POST /api/manage/pico/canvas/pair` or `/api/manage/pico/canvas/pair/show` with Content-Type `application/vnd.readpico.canvas-pair`. STATUS includes one `revision` and current `orientation` (portrait/landscape), retaining pending→saved/displayed receipt boundaries. Background reception never navigates; explicit show never unlocks.


## 应用集与网站流量 / Apps and website analytics

「设置 → 应用集」集中展示现有系统入口和 TF 应用。新配置默认将应用集加入首页；已有首页顺序保持，可通过「首页导航」自行添加。上下翻页选择应用，打开 TF 模块后返回应用集。刷新只重读已有目录，不下载应用。

网站流量为独立的 Cloudflare 快照应用。数据源、账户令牌和刷新由多屏灵犀管理，设备离线展示最后成功保存的横竖双版，旋转时本地选择布局。设备不保存 Cloudflare 令牌，不把推送图中的地图和图表当作可点击控件。

独立频道通过 HTTP `canvas.app_channels:["cloudflare"]` 或 USB STATUS 根对象的同名字段协商。HTTP `POST /api/manage/pico/apps/cloudflare/pair[/show]` 接收同一 PICOPAIR 格式与840032字节上限，保存到固定自有文件 `/sdcard/readpico/desktop/cloudflare.bmp`；普通画板的 `canvas.bmp` 保持独立。USB 使用 `RPICO BOARD BEGINAPP cloudflare <task> <length> <crc32hex> <show0or1>`，其余 CHUNK/COMMIT/ABORT 不变。旧固件缺少能力时客户端应提示更新，不退回覆盖普通画板。

HTTP 按 `canvas.app_receipts.cloudflare` 的 state/revision/orientation 确认；普通画板继续使用 canvas 的原字段。USB 回执携带 channel，STATUS 的 task/channel/revision/state 对应最后一次 USB 提交。后台接收只保存或更新同一可见应用，不能覆盖另一应用的已载入画面、切换阅读或唤醒锁屏；只有显式 show 才在 UI 可用时导航到该应用。另一应用的推屏不能满足本次频道回执。

The Apps grid reuses existing system destinations and TF module loaders. Cloudflare is a separate retained snapshot channel; the Mac owns its data source and credentials. HTTP app receipts and USB task/channel receipts must be checked independently. Both channels retain the existing atomic pair validation, storage failure recovery, authorization and background presentation contract.

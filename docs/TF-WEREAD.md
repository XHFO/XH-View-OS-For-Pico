# 微信读书 · 在线发现与离线笔记应用包

`apps/tools/weread.pga` 是独立 TF 工具应用，稳定 ID 为 `weread`，kind 为 129，版本为 1.2.0，使用兼容 ABI 1 的可选服务扩展。应用既可在 Pico 本机连接微信读书官方只读 API，搜索图书、浏览在线资料并同步真实书架及个人划线、想法，也可查看 Mac 导入的快照；离线浏览并保存阅读位置。它不是微信读书官方客户端，不提供完整正文下载、DRM 转换或写回微信读书。卸载应用只移除代码，不删除快照或阅读位置。

The independent `weread.pga` utility supports online discovery and offline personal highlights/thoughts. The firmware can authorize, search and sync directly without a Mac. It does not download full books, bypass DRM or write back to WeRead; the module never receives the API key.

## 在线发现与手机阅读

设备「应用集 → 微信读书 → 发现」与手机管理页的「在线发现」使用同一设备服务，支持：

- 搜索书名、作者、全文关键词或网文；全文搜索返回命中内容，不是完整章节。
- 图书简介、可用的官方封面、章节目录和账户阅读进度。
- 个性化推荐、相似书，以及周／月／年／累计阅读统计。
- 热门划线与公开书评，长内容按当前横竖屏排版分页。
- 在图书详情点「手机读」，手机扫描二维码进入该书的微信读书官方阅读器；可读范围由官方账户和图书权限决定。

搜索输入复用系统键盘，取消输入不提交查询。联网、授权或远端服务失败时提供重试，已有离线书架与笔记不变。官方未提供完整书籍或章节正文接口，因此当前应用不能在 Pico 原生在线阅读整本微信读书书籍，也不能从该接口下载 EPUB。

Online discovery covers book/author/full-text keyword search, metadata and covers, chapters, account progress, recommendations, similar books, reading statistics, popular highlights and public reviews. Official reading links open on a phone through QR codes. Search matches and chapters are metadata, not a full-book reading API; access remains subject to the account and book permissions.

在线结果只缓存于内存，最大 524287 字节；超过上限明确报错，不静默截短。目录和长文本在已获取结果内继续分页，远端分页仅使用官方提供的游标。封面仅从允许的官方 HTTPS CDN 获取，以 16 灰阶缩略图显示；获取失败时保留占位，不影响文字资料。在线查询不写 TF 卡，也不导出 TF 卡给电脑。

### 可选查询接口

ABI 1 尾部追加 `weread_query`、`weread_query_read`、系统搜索输入、官方链接二维码和缓存封面回调；模块先检查宿主 `bytes`，旧宿主继续支持离线快照。网络、凭据与响应验证由固件负责，应用仅消费按请求 ID 绑定的有界 JSON。新查询、取消、换账户或清除授权会使旧结果失效。

手机后台沿用配对认证，路径为 `/api/manage/pico/weread/`：`POST query` 提交只读查询，`GET query` 读取元数据，`POST result` 使用 `request_id`、`offset`、`limit` 读取分块。分块是保持完整 UTF-8 的 JSON 文本片段，客户端按返回的字节偏移拼接后统一解析。`ready=true` 表示文字结果已可读取；封面仍准备时可以同时 `busy=true`，完成后 revision 更新。状态与实际查询 ID 不一致时不得继续读取旧内容。

## 在 Pico 本机连接与同步

1. 在设备「应用集 → 微信读书」打开「连接与同步」，选择「扫码设置」。尚未联网时，先在设备连接 Wi-Fi，再重新打开扫码设置。
2. 手机和 Pico 连接同一个可信局域网，扫描设备二维码。页面由 Pico 本机提供，多屏灵犀和 Mac 均不需要运行。
3. 在手机页面按引导打开[微信读书官方授权入口](https://weread.qq.com/r/weread-skills)，复制 API Key，回到设备配置页填写并保存。
4. 点击「读取书架」，勾选需要的书籍并同步个人划线与想法。设备完成所有内容写入后才发布新的离线索引；保存密钥不代表远端账号已验证。
5. 以后可在设备的「连接与同步」直接再次同步上次选择；网络断开后仍可阅读上次成功的离线内容。

API Key 保存在设备本地 NVS，不写入 TF 卡、应用快照或网页返回值，也不传给 Mac。手机页使用现有同源配对管理会话，十五分钟空闲后失效，停止管理可立即撤销链接。仅在可信局域网使用 HTTP；设备访问微信读书官方网关使用验证证书的 HTTPS。系统时间尚未校准时明确提示授时或手动设定时间，不跳过 TLS 验证。

同步在后台执行，不切换当前阅读页面，也不把 TF 卡导出给电脑。USB 文件管理、存储维护和电源操作与同步共享准入屏障；请先等待或取消同步再执行这些操作。网络、授权、容量、换卡或写入失败保留原快照；未使用的历史 generation 不自动删除。当前书架最多 64 本，超过上限会明确提示；不静默截断内容。

## 文件及所有权

设备目录固定为 `/sdcard/apps/data/weread/`。设备本机服务和桌面导入管理 `index.dat`、其恢复副本 `index.bak`，以及键名为 `g%08x-b%02x-c%03x` 的内容文件，格式参数分别为 generation、书槽号、块号，均为小写十六进制。例如 `g00000001-b00-c000.dat`。键符合宿主的 31 字节安全 ID 限制。`.tmp` 仅用于未提交事务；设备不将它视为已同步内容。

`position.dat` 及其 `.bak`、`.tmp` 属于设备模块，桌面不得覆盖、删除或以快照替换。包安装、更新、卸载均保留整个数据目录。第一版不自动回收历史 generation，避免删除仍被恢复索引引用的内容。不得清空整个目录以实现同步。

## 外层存档封装

每个文件沿用宿主存档封装：12 字节小端头，依次为 `u32 0x31535058`、`u32 payload_bytes`、`u32 CRC32/IEEE(payload)`，随后为精确长度的 payload，不得尾随字节。模块通过宿主 `load()` 请求固定精确长度；每次读取不超过 64 KiB。下表偏移均从 payload 开始，不包含 12 字节外头。协议常量见 [`weread_snapshot.h`](../apps/include/weread_snapshot.h)，不序列化原生 C/Swift 结构体。

## WRS1 索引

索引 payload 固定 **16448 字节**，包括 64 字节头和 64 个 256 字节书记录。

| 偏移 | 字节数 | 内容 |
| --- | --- | --- |
| 0 | 8 | ASCII `PWRIDX01` |
| 8 | 4 | schema = 1 |
| 12 | 4 | payload_bytes = 16448 |
| 16 | 4 | 非零 generation |
| 20 | 4 | book_count，0–64 |
| 24 | 4 | 全部书记录 note_count 之和，必须可由 u32 表示 |
| 28 | 8 | 创建时间，UTC Unix 秒；0 表示未知 |
| 36 | 28 | flags 与保留区，全零 |

书记录从偏移 64 开始：

| 记录内偏移 | 字节数 | 内容 |
| --- | --- | --- |
| 0 | 64 | 非空稳定 book_id，1–63 个 ASCII 字母、数字、`_` 或 `-`；不得为适应长度而截断 ID |
| 64 | 128 | 非空书名 UTF-8 |
| 192 | 48 | 作者 UTF-8，可空 |
| 240 | 4 | note_count：真实划线与想法记录数 |
| 244 | 4 | chunk_count，0–256；无笔记必须为 0，有笔记必须大于 0 |
| 248 | 8 | 保留，全零 |

字符串必须包含 NUL，NUL 后全零，合法 Unicode 标量 UTF-8；书名、作者的长度适配只能在完整 UTF-8 字符边界截短。重复 ID、未使用记录非零、note_count 求和溢出或不等于总数，均视为损坏。64 本书或每书 256 块是硬上限；超限导入应明确报错，不能悄悄截断选中内容。

## WRS1 内容块

每块 payload 固定 **32768 字节**，64 字节头后最多 32704 字节文本。

| 偏移 | 字节数 | 内容 |
| --- | --- | --- |
| 0 | 8 | ASCII `PWRCHK01` |
| 8 | 4 | schema = 1 |
| 12 | 4 | payload_bytes = 32768 |
| 16 | 4 | 与索引相同的 generation |
| 20 | 4 | book_slot，0–book_count-1 |
| 24 | 4 | chunk_index，0–chunk_count-1 |
| 28 | 4 | 与书记录相同的 chunk_count |
| 32 | 4 | text_bytes，1–32704 |
| 36 | 28 | flags 与保留区，全零 |
| 64 | text_bytes | 书中真实划线、想法的可读 UTF-8 文本串流 |
| 64+text_bytes | 剩余 | 全零；文本恰好填满时无终止 NUL |

文本中应明确标明“划线”“想法”和可取得的章节；段落用 LF 分开，可包含 TAB。除 LF/TAB 外禁止控制字符，包括 NUL、CR 和 DEL。模块不将文本解释为 HTML 或执行指令。拆块优先保持段落完整，必要时在 UTF-8 字符边界切分长段落；不得丢失文本。无笔记书不创建内容块，设备展示明确空态。

## 提交及故障恢复

1. 桌面导入先取得独占 TF 管理租约；设备本机服务通过同步准入屏障检查挂载、导出状态和介质 generation。两种来源不能同时提交；本机同步不导出 TF。
2. 选择非零且尚未使用的 generation。已有代的内容文件不可原地改写；编号用尽时报告错误。
3. 对每块用独占新建临时文件写入、刷新、校验封装和 WRS1 字段，再原子提升并读回。任一步失败都不发布新索引。
4. 全部块完成后，最后原子提交索引。只有已验证的旧索引可以轮换为 `index.bak`；损坏旧主档不能覆盖有效恢复副本。失败保留旧可读代。
5. 本机完成同步后按成功 revision 更新有界缓存；桌面导入在安全交还 TF 后可点击“刷新”或重开。模块不会在 `render()` 读取文件，在事件中读取快照并在内存中分页。

设备本机同步和 Mac 导入均不得在失败时清空原索引。本机网络授权属于固件服务，TF 模块通过可选宿主回调查询无密钥状态、请求同步或打开扫码设置；旧固件仍能离线浏览已有快照。程序包沿用现有应用安装通道。

## 模块阅读位置

`position` payload 固定 128 字节：0..7 为 `PWRPOS01`；8 schema=1；12 bytes=128；16 generation；20 book_slot；24 chunk_index；28 UTF-8 文本字节偏移；32 mode（0 书架、1 笔记）；36..39 零；40..103 为完整 NUL 填充 book_id；104..127 零。ID、generation、槽位和块必须同时匹配当前索引才能恢复。位置损坏或旧代位置仅忽略，不影响导入内容。模块只写此键，绝不修改导入索引或内容块。

阅读位置在翻页后空闲保存，也随宿主正常退出保存；保存失败保留内存位置以便重试。横竖屏保留字节位置并按当前可用区域重新排版；上下/左右滑动或底部按钮翻页，返回先回应用书架，再返回系统来源页。

## 定向构建与检查

```sh
python3 tools/build_tool_packages.py --app weread --device --output build/weread-package
python3 tools/test_weread.py --output build/weread-tests
```

`--app weread` 只构建微信读书应用，不重建已分发游戏或画板。测试使用合成内容和内存宿主，不读取账户、密钥、真实笔记或设备。测试可验证解析、分页、保存与异常恢复，但不代替物理屏幕观感和真实账户授权验收。

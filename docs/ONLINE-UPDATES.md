# 小纸 Pico 在线更新 / Online updates

## 设备检查 / Device check

- 固定地址：`https://xhfo.net/projects/read-pico/updates/version.txt`
- 响应上限：512 字节；超时：8 秒；只接受 HTTP 200 和完整响应。
- HTTPS 使用 IDF 证书包验证，不关闭证书检查，不跟随跨域跳转。
- 启用证书包交叉签名验证，兼容 XHFO.NET 当前使用的 GTS Root R4 交叉链。
- TLS 输入/输出记录为 16 KiB / 4 KiB，mbedTLS 缓冲固定走 PSRAM；阅读热路径不增加内部 RAM 占用。
- 后台任务只在 Wi-Fi 已连接时运行；检查失败不改变阅读、双机和 TF 状态。
- “关于本机”的“系统更新”卡片只显示固件状态、当前/网站版本和安装动作；应用数量不再混入这里。

## 固件安装 / Firmware installation

设备读取网站清单中的 `firmware_size` 与 `firmware_sha256`，先核对 TF 空闲空间，再把新镜像下载到 `/readpico/firmware/update.bin.part`。完整长度、HTTP 完成状态、flush/fsync 和 SHA-256 均通过后，按“撤销旧 SHA 标记 → 替换镜像 → 发布新 SHA”的顺序提交。网站清单可用时，本地镜像必须同时匹配大小和 SHA 才显示“已就绪”；没有网站清单时仍允许 TF 离线包。随后需要再次轻点固件卡片确认安装；设备切换到 512 KiB 的 `ota_1` 更新器，由更新器再次校验并只写 `ota_0`。下载期间当前系统、NVS、书库和双机会话保持可用。

电脑网页管理器仍保留独立 APP 更新路径，用于首次部署或 USB 恢复；它会核对设备、分区、OTA 状态、SHA-256 并完整回读。bootloader、分区表、otadata、NVS 和 TF 卡不写入。

## 应用包 / App packages

从 dual98 起，应用跟随设备更新频道：稳定使用 `updates/apps.txt` 与 `apps/packages/`，测试使用 `updates/test/apps.txt` 与 `apps/packages/test/`。切换频道会清除目录及未提交选择，下载或安装期间禁止切换；两类包分别发布，不向旧稳定固件推送依赖新宿主接口的应用。

From dual98, apps follow the update channel: stable uses `updates/apps.txt` and `apps/packages/`; test uses `updates/test/apps.txt` and `apps/packages/test/`. Switching discards the catalog and pending selection and is rejected during downloads or installation. Publish the feeds independently to protect older stable hosts.

“应用集”顶部的“应用下载与更新”横幅读取 `updates/apps.txt`，比较本地包头与云端版本，分别标记可下载、可更新、已是最新或已安装（远端版本未确认）。用户可逐项勾选或全选可安装项，确认后只下载所选包。系统固件更新与此入口独立，不自动更新应用。

Tap App downloads & updates inside Apps to compare local package headers with `updates/apps.txt`, then select missing or older apps. System firmware updates remain separate. Unknown remote versions are shown as Installed, never Up to date.

清单保留 `game=<id>` / `tool=<id>` 行，可追加 `app_version=<id>,<正整数版本>`，例如 `app_version=home-link,10108`。旧固件会忽略版本行。`tools/build_apps_update_feed.py` 从打包目录的 `catalog.json` 和实际 PGA 包头生成清单，检查版本与目录一致。旧网站无版本行时，后台对每个包仅读取 512 字节头并关闭连接；不下载整个载荷来检查更新。本地只读包头，每次显式检查一次，不在阅读或绘制时扫描文件。

The feed keeps `game=<id>` / `tool=<id>` lines and optionally adds `app_version=<id>,<positive integer>`. Old firmware ignores version lines. `tools/build_apps_update_feed.py` validates catalog entries against actual PGA headers. Legacy feeds fall back to bounded remote-header probes, with no payload download for update checking.

下载使用固定 HTTPS 来源、证书验证和 30 秒超时，先写入 `apps/.staging/<id>.tmp`，核对包 ID、类别和检查时的版本，再由现有 PGA 安装事务验证载荷并原子提交。安装失败保留原包；同版、更旧版本和未选中应用不会覆盖。目录固定到本批安装结束，避免网站重排改变选择；失败后可重新检查或重试。应用管理中的“重新扫描 TF 卡”只读本机，不联网。

Downloads use a fixed HTTPS origin, verified certificates and a 30-second timeout. The staged package must match its selected ID, class and catalog version before the existing payload-validation and atomic-install transaction. Failed installs retain the original package. Same or older versions and unselected apps are skipped. Each batch retains its catalog snapshot across installation handoffs.

PGA1 应用包继续由网站目录分发。网页管理器按目录中的字节数和 SHA-256 下载，并校验包内 ABI、当前 ESP32-S3 payload 和 CRC 后写入：

- `/sdcard/apps/games/`
- `/sdcard/apps/tools/`

安装同 ID 新版本不会删除 `apps/data/<id>/` 存档。设备版本检查只报告数量，不自动下载未知包。

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

“应用集”顶部的“应用下载”横幅读取 `updates/apps.txt`，然后打开应用选择层；用户可逐项勾选、全选缺失项或翻页查看，确认后只下载所选包。下载使用固定 HTTPS 来源、证书验证和 30 秒超时，先写入 `apps/.staging/<id>.tmp`，再由现有 PGA 安装事务校验并原子提交。已存在的包不会重复下载；需要更新或修复时先移除对应应用。应用集右侧“扫描”只重读本机 TF 应用，不联网。

Tap the App downloads banner inside Apps to read `updates/apps.txt`, then choose one or more missing packages from the selection layer. Downloads use the fixed HTTPS origin, certificate verification and a 30-second timeout; the existing PGA installer validates the staged file before an atomic commit. The Scan action only re-reads local TF packages.

PGA1 应用包继续由网站目录分发。网页管理器按目录中的字节数和 SHA-256 下载，并校验包内 ABI、当前 ESP32-S3 payload 和 CRC 后写入：

- `/sdcard/apps/games/`
- `/sdcard/apps/tools/`

安装同 ID 新版本不会删除 `apps/data/<id>/` 存档。设备版本检查只报告数量，不自动下载未知包。

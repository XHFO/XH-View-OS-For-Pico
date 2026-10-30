# Home Link TF 工具包 / TF package

当前集成固件 `0.39.1-dev69` 默认 `PICO_HOME_LINK_FULL=OFF`。工具包位置 `/sdcard/apps/tools/home-link.pga`，kind **130**、ABI **1**、当前版本 **10104**。完整字段、鉴权与兼容边界见 [HOME-LINK-CONTRACT.md](HOME-LINK-CONTRACT.md)。

The default thin host uses the optional TF package above. See the shared contract for authentication, field definitions and compatibility.

## 设备入口 / Device entry

设备家庭与应用集打开监看页；HA 设置直接打开连接设置，打印机入口打开打印机页。宿主通过一次性 `PGA_HOME_ENTRY` 传递页面，只有 `PGA_HOME_ENTRY_OK` 标志存在时应用才采用结果；旧宿主的普通状态响应不会误触发跳页。包未安装时，空状态给出安装提示和手机扫码填写入口。

Home and the launcher open monitoring; HA settings deep-link to setup and printer settings to the printer page. A one-shot flagged host command selects the page, while legacy status responses are ignored. The thin empty state retains package-install and phone-setup entries.

`home_link_mode=pga` 表示当前宿主提供 PGA 家庭桥；它不验证 TF 包是否安装、代码是否包含当前平台或 HA 是否可连接。旧固件缺少 mode 时按旧宿主处理，不猜测门户能力。

The current `pga` mode describes host capability, not package installation or HA connection success. Treat an absent mode as an older host.

## 配置和同步 / Configuration and sync

- 设备包可编辑 HA 本地/外部地址、家庭 Wi-Fi、令牌、Cloudflare Access Service Token。宿主把凭据保存在 NVS `pico_home_pga`，不写 TF 卡。/ Device setup edits HA endpoints, home Wi-Fi and write-only credentials, persisted in NVS rather than TF.
- 从旧完整固件升级时，仅当新 `pico_home_pga/settings` 记录不存在，才只读兼容 `pico_home_link/settings`。不自动写入、清除或覆盖；已有新空配置、损坏配置或读取错误都不会重新启用旧凭据。用户保存后写入新命名空间，旧记录保留。/ When the new record is absent, read the full-host namespace without automatic writes. Existing empty/invalid/unreadable new records never revive older credentials; an explicit save writes only the new namespace.
- 点“手机填写”，在相同 Wi-Fi 下扫码打开十分钟有效的局域网表单。可填写同一份 HA 配置及三个 HA 打印机实体前缀；保存仅表示本地写入，实际连接结果须查看同步快照。/ Phone Setup opens a ten-minute LAN form for the same HA store and HA-prefix printer slots; a save receipt does not prove an upstream connection.
- 手机门户打开时，本机与通用管理页不保存。门户关闭、到期、休眠或网络/传输接管后释放写入权。/ Device and management writes pause while the portal owns setup; close, expiry, sleep or network/transfer takeover releases it.
- 普通管理服务的精确 `home/printers` 路径在桥注册后也能回读/保存；Mac 可通过只读 `GET /api/manage/pico/home-link` 获取同一份公开配置。/ Registered home/printers management routes and the read-only desktop aggregate share this store.
- 空凭据保留旧值，明确清除使用 clear 字段；Cloudflare Access 头只发送给外部 HTTPS，匹配家庭网络的本地请求不带。/ Empty credentials preserve saved values; clear is explicit and Access headers attach only to remote HTTPS.

实体同步与控制通过宿主 HTTP/TLS 工作任务与后台状态快照；控制返回成功仅表示已排队，实际结果由后续快照显示。渲染只读快照，不在绘制中同步。休眠暂停立即阻止新工作并取消未执行队列，零超时不会阻塞；旧工作结果不得在配置变化或暂停后回灌。独立 Bambu LAN/云端 MQTT、Bambu 账号登录、Formlabs OAuth 不由这个薄宿主包提供；门户只允许 HA 打印机槽，其他来源返回 `unsupported_source`。兼容读到的旧独立打印机槽保留配置并显示不支持，工作任务不使用其凭据发请求。不要把原 full 固件的功能说明当作 PGA 已完成能力。

Entity monitoring/control uses the host bridge. Accepted controls run on the worker and report their actual result through status; acceptance is not execution success. Independent Bambu MQTT/account login and Formlabs OAuth remain outside this thin package; the portal writes HA printer slots only.

## 构建与验证 / Build and verification

```sh
python3 tools/build_tool_packages.py --device --app home-link --output build/tool-packages
python3 tools/test_home_link_entry.py
python3 tools/test_home_pga_transaction.py
python3 tools/test_home_link_draft.py
python3 tools/test_home_link_portal.py
python3 tools/test_home_link_portal_runtime.py
```

设备包构建需要 ESP32-S3 工具链；若自动检测不到，设置 `PICO_XTENSA_CC`。原生预览必须包含本机平台 payload，不能拼接旧包的代码。包版本与文件哈希以本次构建产物为准，不沿用其他分支 `build-artifacts/` 中的旧版 10200。

A device package requires the ESP32-S3 toolchain (`PICO_XTENSA_CC` when not detected). Rebuild native payloads for their actual platform; never splice an old payload into the current package.

本机检查覆盖入口、凭据掩码、保存/失败、手机与 Mac 互斥、实际内嵌页面脚本，以及真实 HTTP 包装器的到期清理、重开、鉴权、状态码和 PSRAM 失败。它们不连接真实设备或服务；Wi-Fi 扫码、物理显示、休眠与 USB 恢复仍需实机验证。

Host tests cover the actual module, portal fields/page and wrapper lifecycle/authentication with deterministic stubs. Real Wi-Fi, physical display, sleep and USB recovery require separate device validation.

## 配置兼容与休眠 / Compatibility and sleep

新配置不存在时，只读校验旧 `pico_home_link/settings`，保留旧文件；显式保存才写新命名空间。新配置已存在（包括明确清空）或读取失败时，不回退旧凭据。界面绘制不发起同步或消费输入，控制请求在后台执行；暂停先阻止新工作，未完成请求不回写休眠后或新配置的快照。

Read legacy `pico_home_link/settings` only when the new configuration is absent, preserving the old store. Explicit saves use the new namespace. Existing cleared settings and read failures never revive old credentials. Painting is side-effect-free; controls run in the worker, and suspension rejects new work and stale result publication.

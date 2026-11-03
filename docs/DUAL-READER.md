# 双机阅读实验分支 / Dual-reader experiment

本功能只属于 `experiment/dual-pico-reader`，以 `0.40.0-dev71` 的本地实现为基线。它不并入主系统、不替换常规版本；两台设备均使用 `0.40.0-dual29`，以使用差异同步、左页优先显示、共享字号、目录与原文定位、主机自动方向与各端独立180度翻转。

This feature belongs only to `experiment/dual-pico-reader`, based on the local dev71 implementation. It is not merged into the main system. Use dual29 on both readers for incremental sync, left-page-first presentation, shared typography, contents and source navigation, and primary-following orientation with independent 180-degree offsets.

本私有分支提供完整源码；私有仓库 `main` 原有的切片目录不受此分支上传影响。

This private branch contains the complete firmware tree; the repository’s existing slice-based main branch is maintained separately.

## 本版界面与输入修订 / UI and input revision

- **屏幕贴合防误触**：首次使用先在桌面把两台展开平放，点击「校准开合姿态」；主机会发出校准触发，两台各自采样并保存自己的重力基准。校准前防误触不启用。之后每次读取当前重力与基准的夹角：同向为展开，翻转约 180° 为另一面；两端朝向明确且相反时判为贴合并锁存：触摸离开、两台一起运动或姿态暂时不明都不会解除；只有展开并稳定同向 800ms 才恢复。贴合锁存期间底部三键仍可导航，但不会清除屏蔽；中央长按恢复已取消，避免折叠时误解除。展开状态下完全不启用鬼触判断，正常滑动不会被误判。开关通过蓝牙控制通道两端同步。
- **翻页动画**：正文翻页保留旧页，用 4 帧斜向软阴影页边推进，再以正常波形定稿；白色页边替代原来的硬黑缝，不新增整屏帧缓存，双机确认仍在最终画面成功后发生。
- **划词批注同步**：任一端提交划线或笔记后，通过 BLE 发送章内偏移和文本；对端在同一本书上写入同一条记录，并在当前页重绘。批注键盘只占本机，另一侧保持单人阅读和翻页。
- **阅读返回与接力**：从阅读返回时先保存进度，再用一次根页切换清空阅读子页/历史并回到书库主页。双机阅读中任一台离开正文/目录页去书架、其他应用或批注键盘时，对端进入单人阅读，可正常翻页；返回后以仍在阅读的一台的当前页重新合流。
- **方向与陀螺仪**：双机 180° 翻转现在同时补偿传感器候选方向，翻转端自动旋转不再反向；双机会话内的方向锁定独立于系统锁定偏好，开启后主机停止采信加速度计、从机停止跟随主机基准，手动翻转仍可用，退出双机后恢复系统方向与锁定。
- **滑动快捷菜单**：恢复旧版样式：白色实心圆、黑色描边、图标与两字短名；当前选中项用圆外光圈表示。滑动只以 MODE_DU 分别更新新旧光圈和固定提示条，不重绘整圆或合并成大矩形，既保留旧观感又避免频繁闪屏。
- **Home Link 设备卡片**：家庭控制页按灯光、开关、空调、风扇、温湿度分别绘制卡片；顶部「全部/灯光/空调/风扇/开关/传感」筛选卡片类型；长按卡片拖动到目标卡片后松手保存排序，顺序按实体 id 哈希持久化，后台同步换序不会打乱。需要 TF 工具包 `home-link.pga` 版本 10106。

- **Face-to-face ghost-touch guard**: first unfold both readers on a desk and tap Calibrate Open/Closed. The primary triggers calibration; each reader samples and stores its own baseline. The guard stays off until calibration. Afterwards the angle between current gravity and the baseline defines the open face versus the flipped face; two known opposite faces latch contact and suppress all screen touches. Quiet gaps, shared motion and ambiguous pose do not release the latch; only stable unfolded faces for 800 ms clear it. The three bottom keys still navigate during contact but do not clear the latch; central-hold recovery has been removed. Unfolded use bypasses detection entirely, so normal slides never trip it. The switch is synchronized over BLE.
- **Page-turn animation**: the old page is retained while four slanted, soft-shadowed page-edge frames advance, then the normal settle waveform finishes. A white page edge replaces the old hard black seam. No extra full-screen buffer; paired confirmation still happens only after the final frame is presented.
- **Annotation sync**: a committed highlight or note sends its chapter offsets and text over BLE; the peer writes the same record for the same book and redraws the current page. The annotation keyboard is local while the peer keeps solo reading and page turns.
- **Returning home and relay reading**: leaving reading saves progress, then a single root switch clears child pages/history. If either unit leaves the text/contents for the shelf, another app or the annotation keyboard, the peer becomes a normal single reader and can turn pages; returning rejoins at the peer's current page.
- **Orientation / gyro**: the 180-degree role flip now also compensates the accelerometer candidate, so a flipped device no longer auto-rotates in the opposite direction. The paired-session lock is independent of the system lock: it stops primary sensor turns and secondary base-following, while manual flip remains available; disabling pairing restores normal system orientation and lock.
- **Quick fan menu**: restored to the original white disc with a black outline, gray/black icon and two-character short label; the selection is an outside halo. Sliding updates only the old/new halo and the fixed prompt strip separately with MODE_DU, avoiding whole-disc repaints and flicker.
- **Home Link cards**: the Home control page renders separate light, switch, climate, fan and sensor cards; type chips filter the visible cards; long-press a card and drop it on another to save the order. Order persists by entity-id hash, so background sync reindexing does not scramble it. Requires `home-link.pga` version 10106.

## 配对与阅读 / Pair and read

1. 两台小纸 Pico 插入可读写的 TF 卡，连接同一个 Wi-Fi。无需电脑保持在线。
2. 在「设置 → 阅读与输入 → 双机阅读」中，一台选「主机」。页面显示主机 IP 地址和六位配对码。主机须有完整的 TF 字体；缺少时先安装 MiSansVF.ttf。
3. 另一台选「从机」，输入主机 IP 最后一段（或完整 IP），点击「下一步」后填写六位配对码；同一配对码同时用于 BLE 安全配对。初次校验、补书和共享字库传输完成后，页面显示「双机书库一致」。
4. 任意一台从书架打开 TXT 或 EPUB，首次打开主机显示第 1 页，从机显示第 2 页；重新打开当前共享书保留阅读位置。两端均以主机确认的阅读状态为准。
5. 主机电源键**短按上一组**，从机电源键**短按下一组**：1／2 → 3／4。屏幕左右翻页和底部翻页键同样以双页为单位。本端电源键翻页被接收后异步播放短提示音，受「提示音 → 按键与触摸」控制；另一台同步时不重复响，长按电源菜单不触发翻页音。
6. 点击顶部页码输入目标页，点黑底白字的「跳转」后两台打开包含该页的双页组合。例如第 10 页对应主机第 9 页、副机第 10 页。无效页码或连接中断保留原阅读位置，可取消。窗口仅保留数字、退格、清空及「取消／跳转」，目录仍从阅读工具进入；定位中不重复提交。
7. 主机为左页、副机为右页。任意端发起开书、翻页或跳页，主机成功推屏后才放行副机；主机仍在弹窗或排版失败时，副机保持原画面。
8. 在双机菜单的「主机」「从机」下分别设置「旋转屏幕 180°」。自动方向以主机的稳定竖握姿态为基准，从机跟随；两个开关分别叠加各自的180度翻转。从机自己的重力方向不会覆盖主机，两端系统的旋转与方向锁定偏好保持独立，关闭双机后恢复。快捷菜单的旋转等同于切换当前角色的开关；从机须连接主机才能修改，开关确认后自动保存。横屏姿态不重排正文，翻页方向不变。
9. 长按电源键仍打开电源菜单，可以锁屏、关机或重启。书库校验、文件传输、已连接的双机会话和双机正文阅读期间暂缓自动休眠；未连接且无任务时恢复原自动休眠规则。保存的休眠时间不变，主动锁屏仍有效。关闭双机功能后短按电源恢复原锁屏操作。
10. 对端停留在书架、配对页或普通系统设置页时，会跟随开书。输入框、应用编辑等页面暂不被打断，回到可阅读页面后再应用最新位置。同步未完成时，点击图书会提示等待并排队，完成后自动打开；浮层打开时继续通信，关闭浮层后再进入正文。

Both devices join the same Wi-Fi, then choose Primary/Secondary in Settings → Reading and Input → Dual Reading. Enter the primary's address and pairing code on the secondary. Either shelf can open a book. Primary short power means previous spread; secondary short power means next spread. Hold still opens the power menu. An accepted local power-key turn emits one asynchronous interaction cue, subject to the sound preferences; the synchronized peer does not echo it. Idle sleep is deferred during verification, transfer, connected pairing and reading; explicit power actions remain available. Ordinary settings can follow an open request; editing screens defer it until returning to a reading surface.

The primary is the left page. Either device may request a turn or a validated page jump; the secondary waits for the primary to finish presenting that revision. A page request opens its containing spread (page 10 gives pages 9/10). The primary supplies the automatic portrait orientation. Separate 180-degree switches below Primary and Secondary apply an offset for each role. The secondary ignores its own gravity direction; normal system orientation and lock preferences are preserved and restored after pairing is disabled. The rotation shortcut toggles the current role; the secondary must be connected to change these shared preferences. Landscape poses do not reflow text.

Both toolbars adjust one shared font size (36–72px, step 4). Reflow preserves the primary source-text anchor and releases the right page after the left is rendered. Contents, scrubbing, return-to-origin and local annotation source jumps use shared spreads; settings Back and reopening the current book retain position. An interrupted reflow restores the last completed typography checkpoint. Notes remain local to each device.

## 通信分工 / Transport split

- **Wi-Fi 局域网**：TCP 18771 传递书库目录、TXT/EPUB、主机共享字体与文件校验。不会通过此通道传递开书或翻页命令。
- **蓝牙 BLE**：主机为 GATT 服务端，从机为客户端；传递书库一致性摘要、当前书籍标识、页码和上一组/下一组指令。使用六位配对码、LE Secure Connections、MITM 认证和 16 字节密钥。加密认证、MTU 和通知订阅全部就绪后才能开始控制。
- 每条从机请求带序号和基础页码版本；主机确认最终状态，重复或过期请求不重复翻页。丢失通知通过重发最终状态恢复。
- 双机模式临时独占 BLE 控制器；暂停外部蓝牙翻页器，保留其已配对设备和开关，关闭双机后恢复。暂不同时连接外部翻页器。
- 两端均须使用 `0.40.0-dual29` 。数据协议仍为 3；BLE 控制同步独立方向状态，控制协议为 4，不能与 dual13 及更早版本混用。无需电脑维持连接。

Wi-Fi carries library/file data only. Authenticated BLE carries book identity, page state and sequenced turn requests. Matching verified libraries can continue over BLE with Wi-Fi offline. The dual-reader session temporarily leases the radio and restores HID pager preferences after complete teardown.

## 一致性与存储 / Consistency and storage

- 初次同步为两边书库的并集：SHA-256 相同的文件不重复传输；同名不同内容分别保留。保留原文件名，遇到重名时为新增副本使用内容摘要名称。
- 先检查文件清单。同一次可信 TF 会话内，路径、长度及文件元数据不变的已校验条目复用 SHA-256；新增或变化条目重新读取校验。重连只补齐内容摘要不同的文件，不反复发送完整书库。删除不会传播到另一台，双方仍保留并集。
- 网络中断时保留本会话拥有的临时文件；重连分块重读前缀并校验长度，再请求剩余内容，最后对整文件校验 SHA-256。此版按文件同步差异，不对一个已修改文件做任意字节补丁。
- 传输中只写自有 `.dual-*.part` 临时文件；长度、SHA-256、flush/fsync 和关闭均成功才原子改名。断流或校验失败不会发布半本书，不覆盖现有文件，不导出 TF 卡。
- 同步直接书库目录及 TF 根目录内的 TXT/EPUB，并共享主机所用完整 TTF 字体。每端最多 512 项（含共享字体），单文件上限 128 MiB；超限或存储失败会停止并显示错误，不能当作同步成功。
- 双机阅读使用相同的主机字体、字重 400、共享字号 36–72px（每档4px，初始48px） 和竖屏正文几何。主机选择连续双页位置，两端使用相同的真实 TXT/EPUB 解析和分页代码；跨章延续，奇数末页的从机显示结束页，不重复主机末页。
- 书库页可重新连接与同步；打开新加入的书籍会重新核对书库。书库校验一致后，Wi-Fi 断开仍可通过蓝牙翻页；蓝牙断开保留画面，重连以主机最新状态恢复，不累计断开期间的翻页请求。
- 原笔记与划线保留在各自设备。本实验同步的是图书文件、共享字库与阅读位置，不删除书籍，不合并笔记数据库。

Synchronization is an additive union, identified by SHA-256. A trusted live storage session reuses verified hashes when file metadata is unchanged and hashes new or changed entries. Only missing content is transferred. An interrupted transfer rehashes its owned partial prefix and requests the remaining bytes, then verifies the complete file before publishing. Changed files are transferred as files, not arbitrary byte patches. Conflicts preserve both originals; notes remain local. No destructive mirroring or TF export occurs.

## 当前实验边界 / Experimental limits

- 配对使用同一局域网的 IPv4 地址和配对码；本版没有直连热点配对、自动发现或跨公网同步。
- 两端的阅读工具都能调节共享字号，主机按当前原文锚点重排后统一更新双页。目录、进度拖动/返回原处及本地书签笔记的原文定位均同步到对应双页；设置返回和同书续读保留位置。重排失败回退，重启读取最后已完成的字号与位置。横屏重排和单端独立换字体仍未开放；退出双机恢复普通阅读能力。
- 字体和书籍在首次连接时需要完整校验，缺失文件需要传输；同会话重扫复用已校验条目。缓存和续传仅限当前可信存储会话，不跨重启、换卡、系统休眠、USB 文件导出或存储维护；这些边界销毁缓存和自有临时文件，恢复后重新校验。
- 配对使用随机挑战及配对码派生摘要验证，接收端在认证前不接受书籍。图书数据使用局域网 TCP（端口 18771），未加密；面向用户自己的可信局域网。布局协议变更必须更新挑战域与协议版本。
- 本机回环、原生预览和编译验证不能证明两台实机的无线稳定性、面板时延或功耗。

The data channel uses local IPv4 pairing and authenticated TCP; controls use separate authenticated BLE. Book payloads are not encrypted. Shared typography, contents and validated page jumps are supported; independent per-role portrait flips follow the primary’s automatic direction. Landscape reflow and independent per-device fonts are not available while paired. Software validation is separate from physical two-device acceptance.

## 构建与验证 / Build and verify

上游 CI 工作流原样保留在 [docs/ci/upstream-build.yml](ci/upstream-build.yml)，作为构建参考。此私有实验分支使用下面的命令进行本地构建与测试。

The upstream CI definition is preserved unchanged at `docs/ci/upstream-build.yml` as a build reference. Use the commands below to build and test this private experiment locally.

使用 ESP-IDF 6.1，保留 `sdkconfig.defaults` 的板级时序与 `partitions_device_16M.csv`。双机配置在 defaults 之后加载 `sdkconfig.dual`，对后台库按体积优化；阅读、字体、触摸和面板热路径显式保留 O2。

`sdkconfig.dual` 与已有运行时 Wi-Fi 上限一致，固定收/发缓冲各 4 个、接收窗口 4 帧、BLE 控制器最多 3 个活动。普通分配超过 1 KiB 优先使用 PSRAM，内部专用保留池为 64 KiB，用于无线控制器、任务栈及 TF DMA。保留池并非 TF 独占，仍须用两台实机验证传书和翻页时的实际余量。不参与 ISR/DMA 的书架索引、封面路径与批注草稿缓存同样放在 PSRAM，保留原存档结构。板级默认配置保持不变。

The isolated dual profile matches the existing runtime cap of four Wi-Fi RX/TX buffers and uses three BLE controller activities. Ordinary allocations above 1 KiB prefer PSRAM; a 64 KiB internal reserve serves radios, task stacks and TF DMA. The reserve is shared and does not replace physical coexistence testing. Board timing defaults are unchanged.

```sh
idf.py -B build/dual -D 'SDKCONFIG_DEFAULTS=sdkconfig.defaults;sdkconfig.dual' build
python3 tools/test_dual_link.py
python3 tools/test_dual_link.py --sanitize
python3 tools/test_dual_ble_backend.py --sanitize
python3 tools/test_ble_pager_lazy_worker.py --sanitize
python3 tools/native_preview_build.py --native-only --output build/dual-native
python3 tools/test_native_dual.py --binary build/dual-native/native-preview
python3 tools/test_native_dual_slow_sync.py --binary build/dual-native/native-preview
```

物理安装前须核对各台设备的身份、已安装分区表和生成的 `flash_args`。匹配现有设备布局时仅更新 4 MiB APP 槽，不用完整 `flash_args` 覆盖 bootloader、分区、字体或校准数据。

Check each device's identity, installed partition table and generated flash arguments before installing. For compatible devices update only the existing 4 MiB application slot; preserve bootloader, partitions, fonts and calibration.

仅本实验分支的可信 USB 维护口支持 `RPICO DUAL PRIMARY`，用于刷入后将当前设备设为主机并进入配对页；重复执行不会重置已保存的配对码。`RPICO DUAL STATUS` 仅读取角色及同步状态，不输出配对码。角色仍可在设备设置页更改。

The experimental wired maintenance port accepts `RPICO DUAL PRIMARY` for post-install setup and `RPICO DUAL STATUS` for read-only status. Repeating primary setup preserves the pairing code. Roles remain editable on-device.

`RPICO DUAL ITEM <index>` 仅读取已完成校验的目录项摘要、长度及字体标记；`OPEN <index>` 从该目录排队开书，`NEXT`、`PREVIOUS` 和 `SHELF` 走正常翻页及返回流程。`FONT <36..72>` 使用阅读工具相同的共享字号请求（每档4），`RESCAN` 等价配对页重新同步按钮，复用当前可信会话的文件校验。这些命令均加 `RPICO DUAL ` 前缀，不接受任意路径，不绕过存储或编辑状态保护。`STATUS` 同时报告实际显示页码、正文边界、已应用版本、复用条目数、读取校验字节和续传偏移，供双机验收比对。

`RPICO DUAL ITEM <index>` reads a verified catalog item's digest, size and font flag. `OPEN <index>`, `NEXT`, `PREVIOUS` and `SHELF` use normal guarded UI paths; `FONT <36..72>` uses the same shared-size request as the reading toolbar (step 4); `RESCAN` invokes the pairing page's incremental rescan. All share the same prefix and accept no arbitrary paths. Status includes displayed pages, text boundaries, applied revisions, reused entries, verification bytes and resumed offset.

原生双进程测试用独立回环通道模拟 BLE，仅验证上层同步、断线及重连逻辑；生产 BLE 回调测试替换 NimBLE/FreeRTOS 边界。二者均不能代替真实两台设备的射频、安全配对、共存与翻页时延测试。

## 从机快捷配对 / Secondary quick pairing

先将两台连接同一个 Wi-Fi，在主机配对页查看地址和六位码。从机选择「从机」，默认自动带出本机 IPv4 的前三段，只填主机地址最后一段（1–254，1～3位，无需补零），点击「下一步」，再填写六位码并点击「配对」。也可在同一数字键盘直接输入完整 IPv4，适用于地址前三段不同但网络互通的情形。这是地址前缀补全，不是自动发现主机。

Connect both readers to the same Wi-Fi and check the primary's address and six-digit code. On the secondary, select Secondary, enter the primary's last IPv4 octet (1–254), choose Next, enter the six-digit code and choose Pair. The prefix comes from the secondary's current IPv4 address. A full IPv4 address is accepted in the same field for routed networks. This completes an address prefix; it does not discover a primary.

配置保存之后才开始连接；保存成功不等于已连接。配对页分别显示 Wi-Fi 数据通道、蓝牙翻页通道和书库同步状态。错误输入留在当前步骤，取消未完成输入不会覆盖原配对；重复点选主机不会重新生成配对码。

Connection begins only after configuration is saved; saving does not establish a connection. The pairing page separately reports Wi-Fi data, Bluetooth page control and library synchronization. Invalid input remains editable. Canceling preserves an existing pair, and reselecting Primary preserves its code.

# 双机阅读实验分支 / Dual-reader experiment

本功能只属于 `experiment/dual-pico-reader`，以 `0.40.0-dev71` 的本地实现为基线。它不并入主系统、不替换常规版本；两台设备均使用 `0.40.0-dual97`，以使用完整 WiFi OTA、TF 离线固件更新、应用集内下载与应用管理、方向设备优先的翻页时序、双向书架接力、无旧页回跳的原子返回和双向缺项同步。

This feature belongs only to `experiment/dual-pico-reader`, based on the local dev71 implementation. It is not merged into the main system. Use dual97 on both readers for complete Wi-Fi OTA, TF firmware updates, app downloads and management inside Apps, direction-led page animation, shelf-first handoff, atomic resume without an old-page flash, automatic known-hotspot reconnect and bidirectional missing-file sync.

本分支提供完整源码；公共仓库 `main` 原有的切片目录不受此分支上传影响。

This private branch contains the complete firmware tree; the repository’s existing slice-based main branch is maintained separately.

## 本版界面与输入修订 / UI and input revision

- **屏幕贴合防误触**：首次使用先在桌面把两台展开平放，点击「校准开合姿态」；主机会发出校准触发，两台各自采样并保存自己的重力基准。校准前防误触不启用。之后每次读取当前重力与基准的夹角：同向为展开，翻转约 180° 为另一面；两端朝向明确且相反时判为贴合并锁存：触摸离开、两台一起运动或姿态暂时不明都不会解除；只有展开并稳定同向 800ms 才恢复。贴合锁存期间底部三键仍可导航，但不会清除屏蔽；中央长按恢复已取消，避免折叠时误解除。展开状态下完全不启用鬼触判断，正常滑动不会被误判。开关通过蓝牙控制通道两端同步。
- **翻页动画**：上一页固定从左到右，下一页固定从右到左。下一页由右侧从设备先起手，主机提前放行 revision 并让出约两轮 UI tick；上一页由左侧主设备按原确认顺序先起手。动画期间的新翻页输入排队，结束后立即提交，不丢键也不覆盖当前页组。
- **划词批注同步**：任一端提交划线或笔记后，通过 BLE 发送章内偏移和文本；对端在同一本书上写入同一条记录，并在当前页重绘。批注键盘只占本机，另一侧保持单人阅读和翻页。
- **阅读返回与接力**：从阅读返回时先保存进度，再用一次根页切换清空阅读子页/历史并回到书库主页。任一端去书架、其他应用或批注键盘时，对端进入“单机续读”并把每次有效翻页持续交给主机；两端书架都可在原续读卡位置返回最新双页。返回端先等待主机确认可用 revision，再直接打开目标页，不先显示本机旧进度。
- **方向与陀螺仪**：双机 180° 翻转现在同时补偿传感器候选方向，翻转端自动旋转不再反向；双机会话内的方向锁定独立于系统锁定偏好，开启后主机停止采信加速度计、从机停止跟随主机基准，手动翻转仍可用，退出双机后恢复系统方向与锁定。
- **滑动快捷菜单**：恢复旧版样式：白色实心圆、黑色描边、图标与两字短名；当前选中项用圆外光圈表示。滑动只以 MODE_DU 分别更新新旧光圈和固定提示条，不重绘整圆或合并成大矩形，既保留旧观感又避免频繁闪屏。
- **Home Link 设备卡片**：家庭控制页按灯光、开关、空调、风扇、温湿度分别绘制卡片；顶部「全部/灯光/空调/风扇/开关/传感」筛选卡片类型；长按卡片拖动到目标卡片后松手保存排序，顺序按实体 id 哈希持久化，后台同步换序不会打乱。需要 TF 工具包 `home-link.pga` 版本 10106。

- **Face-to-face ghost-touch guard**: first unfold both readers on a desk and tap Calibrate Open/Closed. The primary triggers calibration; each reader samples and stores its own baseline. The guard stays off until calibration. Afterwards the angle between current gravity and the baseline defines the open face versus the flipped face; two known opposite faces latch contact and suppress all screen touches. Quiet gaps, shared motion and ambiguous pose do not release the latch; only stable unfolded faces for 800 ms clear it. The three bottom keys still navigate during contact but do not clear the latch; central-hold recovery has been removed. Unfolded use bypasses detection entirely, so normal slides never trip it. The switch is synchronized over BLE.
- **Page-turn animation**: previous turns use left-to-right and next turns use right-to-left. A next turn lets the right-side secondary move first while the primary releases the revision and yields two UI ticks; a previous turn remains primary-led. Input received during this short handoff is queued and submitted after the current animation.
- **Annotation sync**: a committed highlight or note sends its chapter offsets and text over BLE; the peer writes the same record for the same book and redraws the current page. The annotation keyboard is local while the peer keeps solo reading and page turns.
- **Returning home and relay reading**: leaving reading saves progress, then a single root switch clears child pages/history. While either unit is on the shelf, in another app or in the annotation keyboard, the peer reads solo and continuously relays each accepted page. Both shelves expose the latest paired spread, and the returning unit opens at the primary-confirmed revision without first showing local stale progress.
- **Orientation / gyro**: the 180-degree role flip now also compensates the accelerometer candidate, so a flipped device no longer auto-rotates in the opposite direction. The paired-session lock is independent of the system lock: it stops primary sensor turns and secondary base-following, while manual flip remains available; disabling pairing restores normal system orientation and lock.
- **Quick fan menu**: restored to the original white disc with a black outline, gray/black icon and two-character short label; the selection is an outside halo. Sliding updates only the old/new halo and the fixed prompt strip separately with MODE_DU, avoiding whole-disc repaints and flicker.
- **Home Link cards**: the Home control page renders separate light, switch, climate, fan and sensor cards; type chips filter the visible cards; long-press a card and drop it on another to save the order. Order persists by entity-id hash, so background sync reindexing does not scramble it. Requires `home-link.pga` version 10106.

## 配对与阅读 / Pair and read

1. 两台小纸 Pico 插入可读写的 TF 卡。任一端都可先为空；连接后双方会互相补齐缺少的 TXT、EPUB 和 TTF，最终文件列表一致。可使用同一个 Wi-Fi；没有可用局域网时，让主机停留在双机阅读页，主机自动建立短时直连热点。
2. 在「设置 → 阅读与输入 → 双机阅读」中，一台选「主机」。第一步明确选择「使用同一局域网同步」或「由主机创建直连热点」；热点无需路由器，但会占用主机的 Wi-Fi。第二步明确选择同步完成后「保持数据连接」还是「仅保留蓝牙交互」。选择“仅蓝牙”会在文件补齐后关闭本次数据通道；之后可从「同步设置」重新开启。
   已配对从机再次开机或在书架停留时，会通过 BLE 自动发现主机热点名，使用已保存的六位配对码派生密码并连接该热点，然后自动恢复数据同步。首次配对仍需输入主机屏幕上的配对码；已配对设备不需要手动重新选择热点。
3. 另一台选「从机」后会直接看到「自动搜索主机 / 手动输入」选项；手动入口不再藏在搜索失败页之后。输入主机屏幕上的六位配对码即可。配对码不通过 BLE/UDP 广播，只用于 BLE 安全配对与数据通道挑战校验。同步页按“1/4 校验本机、2/4 等待设备、3/4 读取对端清单、4/4 补齐文件、完成”显示，并持续展示本机/对端项目数、已读取或接收数据量和补齐百分比。任意一端从阅读返回书架时，续读卡原本的空位显示“返回两台设备的当前双页”，点击即恢复主机确认的当前书页组。
4. 任意一台从书架打开 TXT 或 EPUB，首次打开主机显示第 1 页，从机显示第 2 页；重新打开当前共享书保留阅读位置。两端均以主机确认的阅读状态为准。
5. 主机电源键**短按上一组**，从机电源键**短按下一组**：1／2 → 3／4。屏幕左右翻页和底部翻页键同样以双页为单位。本端电源键翻页被接收后异步播放短提示音，受「提示音 → 按键与触摸」控制；另一台同步时不重复响，长按电源菜单不触发翻页音。
6. 点击顶部页码输入目标页，点黑底白字的「跳转」后两台打开包含该页的双页组合。例如第 10 页对应主机第 9 页、副机第 10 页。无效页码或连接中断保留原阅读位置，可取消。窗口仅保留数字、退格、清空及「取消／跳转」，目录仍从阅读工具进入；定位中不重复提交。
7. 主机为左页、副机为右页。任意端发起开书、翻页或跳页，最终页组仍由主机 revision 裁定。下一页从右向左：右侧从设备完整刷新后，左侧主机才开始；上一页从左向右：左侧完整刷新后右侧才开始。双方实际推屏成功才确认当前 revision，失败重试、丢包或迟到通知不会提前放行或重复推进页组。主机仍在弹窗或排版失败时，不提交新页组。
8. 在双机菜单的「主机」「从机」下分别设置「旋转屏幕 180°」。自动方向以主机的稳定竖握姿态为基准，从机跟随；两个开关分别叠加各自的180度翻转。从机自己的重力方向不会覆盖主机，两端系统的旋转与方向锁定偏好保持独立，关闭双机后恢复。快捷菜单的旋转等同于切换当前角色的开关；从机须连接主机才能修改，开关确认后自动保存。横屏姿态不重排正文，翻页方向不变。
9. 长按电源键仍打开电源菜单，可以锁屏、关机或重启。书库校验、文件传输、已连接的双机会话和双机正文阅读期间暂缓自动休眠；未连接且无任务时恢复原自动休眠规则。保存的休眠时间不变，主动锁屏仍有效。关闭双机功能后短按电源恢复原锁屏操作。
10. 对端停留在书架、配对页或普通系统设置页时，会跟随开书。输入框、应用编辑等页面暂不被打断，回到可阅读页面后再应用最新位置。同步未完成时，点击图书会提示等待并排队，完成后自动打开；浮层打开时继续通信，关闭浮层后再进入正文。

Either device may start with an empty library. After pairing, each side uploads files missing from the peer, including books and TTF fonts, until both manifests are identical. Selecting Secondary opens visible automatic/manual choices, and a configured secondary can reopen them with Edit Pairing. Either shelf can then open a book. Primary short power means previous spread; secondary short power means next spread. Hold still opens the power menu. An accepted local power-key turn emits one asynchronous interaction cue, subject to the sound preferences; the synchronized peer does not echo it.

The primary is the left page. Either device may request a turn or a validated page jump, and the primary still owns the resulting revision. Next turns complete the right screen before starting the left; previous turns complete the left before starting the right. Revision-bound acknowledgements follow successful presentation, retry after loss, and never release on a failed display or replay an older spread. A page request opens its containing spread (page 10 gives pages 9/10). The primary supplies the automatic portrait orientation. Separate 180-degree switches below Primary and Secondary apply an offset for each role. The secondary ignores its own gravity direction; normal system orientation and lock preferences are preserved and restored after pairing is disabled. The rotation shortcut toggles the current role; the secondary must be connected to change these shared preferences. Landscape poses do not reflow text.

Both toolbars adjust one shared font size (36–72px, step 4). Reflow preserves the primary source-text anchor and releases the right page after the left is rendered. Contents, scrubbing, return-to-origin and local annotation source jumps use shared spreads; settings Back and reopening the current book retain position. An interrupted reflow restores the last completed typography checkpoint. Notes remain local to each device.

## 通信分工 / Transport split

- **Wi-Fi 局域网**：TCP 18771 传递书库目录、TXT/EPUB、主机共享字体与文件校验。不会通过此通道传递开书或翻页命令。
- **蓝牙 BLE**：主机为 GATT 服务端，从机为客户端；传递书库一致性摘要、主设备阅读进度、当前书籍标识、页码和上一组/下一组指令。使用用户输入的六位配对码、LE Secure Connections、MITM 认证和 16 字节密钥。发现广播只包含可达端点和可选直连 SSID，不包含配对码。
- 每条从机请求带序号和基础页码版本；主机确认最终状态，重复或过期请求不重复翻页。丢失通知通过重发最终状态恢复。
- 双机模式临时独占 BLE 控制器；暂停外部蓝牙翻页器，保留其已配对设备和开关，关闭双机后恢复。暂不同时连接外部翻页器。
- 两端均须使用 `0.40.0-dual97`。数据协议仍为 3；BLE 控制同步独立方向状态，控制协议仍为 6，不能与 dual81 及更早版本混用。无需电脑维持连接。

## 快速校验与同步后动作 / Fast validation and completion

- 文件校验优先复用 `.dual-hash-cache`：路径、大小、inode、mtime、ctime 未变化时不重新读取整本书。
- BLE 交换稳定设备 ID（ESP Wi-Fi STA MAC）、书库目录 SHA-256 和模式信息；目录摘要一致时标记“快速校验通过”，不传输文件内容。
- 已保存上次配对设备 ID 与目录摘要；重新连接同一设备且内容未变时显示“上次设备与目录一致”，便于确认没有误连其它主机。
- 文件列表同步完成后，若两台都停留在双机页且没有打开浮层，约 1.2 秒后自动返回书架。
- 从双机设置或阅读页手动返回书架时，恢复书签标志被清除；书架不会自动打开上一本书。
- “仅保留蓝牙”策略会在文件列表一致后关闭主机热点或本次 TCP 数据通道；“保持数据连接”则保留热点，便于后续补书。

Wi-Fi carries library/file data only. Authenticated BLE carries book identity, page state and sequenced turn requests. Matching verified libraries can continue over BLE with Wi-Fi offline. The dual-reader session temporarily leases the radio and restores HID pager preferences after complete teardown.

## 一致性与存储 / Consistency and storage

- 初次同步为两边书库的并集：主设备为空的场景同样成立，任一端缺少的图书和字体都会从对端补入；SHA-256 相同的文件不重复传输，同名不同内容分别保留。保留原文件名，遇到重名时为新增副本使用内容摘要名称。
- 先检查文件清单。同一次可信 TF 会话内，路径、长度及文件元数据不变的已校验条目复用 SHA-256；新增或变化条目重新读取校验。重连只补齐内容摘要不同的文件，不反复发送完整书库。删除不会传播到另一台，双方仍保留并集。
- 网络中断时保留本会话拥有的临时文件；重连分块重读前缀并校验长度，再请求剩余内容，最后对整文件校验 SHA-256。此版按文件同步差异，不对一个已修改文件做任意字节补丁。
- 传输中只写自有 `.dual-*.part` 临时文件；长度、SHA-256、flush/fsync 和关闭均成功才原子改名。断流或校验失败不会发布半本书，不覆盖现有文件，不导出 TF 卡。
- 同步直接书库目录及 TF 根目录内的 TXT/EPUB，并共享主机所用完整 TTF 字体。每端最多 512 项（含共享字体），单文件上限 128 MiB；超限或存储失败会停止并显示错误，不能当作同步成功。
- 双机阅读使用相同的主机字体、字重 400、共享字号 36–72px（每档4px，初始48px） 和竖屏正文几何。主机选择连续双页位置，两端使用相同的真实 TXT/EPUB 解析和分页代码；跨章延续，奇数末页的从机显示结束页，不重复主机末页。
- 书库页可重新连接与同步；打开新加入的书籍会重新核对书库。书库校验一致后，Wi-Fi 断开仍可通过蓝牙翻页；蓝牙断开保留画面，重连以主机最新状态恢复，不累计断开期间的翻页请求。
- 原笔记与划线保留在各自设备。本实验同步的是图书文件、共享字库与阅读位置，不删除书籍，不合并笔记数据库。

Synchronization is an additive union, identified by SHA-256. A trusted live storage session reuses verified hashes when file metadata is unchanged and hashes new or changed entries. Only missing content is transferred. An interrupted transfer rehashes its owned partial prefix and requests the remaining bytes, then verifies the complete file before publishing. Changed files are transferred as files, not arbitrary byte patches. Conflicts preserve both originals; notes remain local. No destructive mirroring or TF export occurs.

## 当前实验边界 / Experimental limits

- 配对可使用同一局域网 IPv4，也可使用主机短时直连热点；蓝牙发现会给出主机端点，用户仍需输入屏幕配对码。本版不提供跨公网同步。
- 两端的阅读工具都能调节共享字号，主机按当前原文锚点重排后统一更新双页。目录、进度拖动/返回原处及本地书签笔记的原文定位均同步到对应双页；设置返回和同书续读保留位置。重排失败回退，重启读取最后已完成的字号与位置。横屏重排和单端独立换字体仍未开放；退出双机恢复普通阅读能力。
- 字体和书籍在首次连接时需要完整校验，缺失文件需要传输；同会话重扫复用已校验条目。当前 ESP 路径会把文件哈希与主设备进度索引持久化到 `.dual-hash-cache`，启动后仍按文件路径、长度、mtime/ctime 重新验证；换卡或外部改写不在可信范围内。
- 配对使用随机挑战及配对码派生摘要验证，接收端在认证前不接受书籍。图书数据使用局域网 TCP（端口 18771），未加密；面向用户自己的可信局域网。布局协议变更必须更新挑战域与协议版本。
- 本机回环、原生预览和编译验证不能证明两台实机的无线稳定性、面板时延或功耗。

The data channel uses local IPv4 pairing and authenticated TCP; controls use separate authenticated BLE. Book payloads are not encrypted. Shared typography, contents and validated page jumps are supported; independent per-role portrait flips follow the primary’s automatic direction. Landscape reflow and independent per-device fonts are not available while paired. Software validation is separate from physical two-device acceptance.

## 阅读进度、重连与 TF 记录 / Progress, reconnect and TF records

- 用户修订：同一本书的单机与双机记录合并时采用更靠后的正文位置，合并后由主机仲裁共享双页；两台设备均保存合并记录。书籍身份由已验证的 SHA-256 目录确定，比较章节和章内 UTF-8 偏移，不用不同字号下的裸页码或四舍五入百分比判断前后。
- 书架的“双设备打开”与普通开书共用历史恢复流程：先通过已认证的 BLE 交换该书锚点，再加载/定位。主机新开书及同书重开带独立打开标记，慢布局期间保留请求，重复通知不会重复打开。书架/家庭页暂停后仍需用户主动恢复；单纯心跳不拉回阅读。
- 双方仍在阅读时，断联重连先由主机合并锚点并定位新页组，再放行左右页显示。已连接的从机保存共同左页锚点，脱离后保存自己的单页位置，避免右页天然多一页造成反复合流推进。
- 进度文件位于 TF 的 `books/.pico-system/b_XXXXXXXX.dat`：v3 分别保留单机、双机记录和最远续读锚点，`progress-last.dat` 保存最近路径；通用存储负责 CRC、临时文件、同步和备份提升。旧 `rp_books` NVS 数据逐书保留源迁移，NVS 只继续保存排序计数。TF 出错不把新进度暗中写到 NVS；失败的会话记录保留待重试。
- 阅读记录根独立于系统设置根：内置存储可继续保留系统设置，同时有 TF 时优先把进度、批注和草稿保存到 TF；不要求用户先迁移全部设置。
- 批注、划线和书签继续使用该 TF 目录中的 `reader-r3-index.dat` 和 `reader-r3-o-*.dat` 对象；草稿也必须写 TF，卡不可写时保留编辑内容并提示。原有内置存储笔记通过设置中的显式合并入口导入，保留源库。
- 快速翻页先保留当前正文锚点，进度与共享会话均在停翻 1.5 秒或退出时保存；开书前交换可读取尚未批量落盘的位置。普通书架进度交换按轮完成、15 秒重新检查，重复锚点不重复写卡；共享阅读的当前进度优先发送。书架两端采用同一进度条绘制规则。
- 控制协议升级为 v11，两台设备均需升级（dual107 不能与 dual106 及更早固件建立控制会话）。进度帧携带独立序号，保存成功后回确认；丢包或丢确认按原序号重试，过期确认不能取消新传输。任一设备阅读时暂停全库进度扫描，当前书籍的就绪锚点仍优先同步。

For the same verified book, merge solo and paired records by the later chapter/UTF-8 anchor, then let the primary arbitrate the shared spread. Persist the merged record on both TF cards. Explicit opens exchange that book's anchor first and carry a deduplicated open intent. Reconnect resolves a new spread before presentation; use the canonical left anchor while paired and the actual solo page while detached to prevent right-page drift. TF stores progress, annotations and drafts with CRC/backup transactions; retain legacy NVS sources during lazy migration. Fast turns checkpoint after 1.5 seconds idle, and pre-open exchange includes unsaved live anchors. Both readers require protocol v11 (dual107 is incompatible with dual106 and earlier control sessions): progress packets have durable-save ACKs and sequence-preserving retries; stale ACKs cannot discard a newer transfer. Full-library progress enumeration pauses while either reader is active, while the current resolved anchor stays prioritized.

## 构建与验证 / Build and verify

“关于本机”顶部系统名称卡同时承载“系统更新”，不再另设更新卡。Wi-Fi 已连接时访问固定的 `https://xhfo.net/projects/read-pico/updates/version.txt`，显示当前版本、网站版本、下载进度和安装动作。检查有 8 秒超时和 512 字节上限。HTTPS 使用 IDF 证书包和交叉签名验证，TLS 记录的 16 KiB 输入、4 KiB 输出缓冲放在 PSRAM，不占阅读热路径的内部 RAM。下载前核对 TF 余量，把镜像写入 `/readpico/firmware/update.bin.part`，完成长度与 SHA-256 校验后先发布镜像、最后发布 `update.sha256`；本地缓存的大小或 SHA 与网站不一致时不会误报“已就绪”。再次轻点卡片确认后切换到 `ota_1` 小型更新器。更新器再次流式校验 SHA-256，只写 `ota_0`，成功后切换启动；失败会显示提示并返回原系统。TF 中预先放入 `update.bin` 与 `update.sha256` 仍可离线更新。

首次启用设备内更新时，需要让人工刷写包含 `ota_0` 主系统与 `ota_1` 更新器的分区布局：Bootloader → `0x0`，主 APP → `0x20000`，更新器 → `0x420000`。不要覆盖 NVS `0x9000`、OTADATA `0xF000`、assets 或 TF/SD 卡。完成后两台设备都应显示 `0.40.0-dual97`，以后才能使用 Wi-Fi 或 TF 镜像更新。

The top system-name card under About also owns System update. The app download entry opens a selection layer inside Apps, where users choose exactly which missing packages to download. HTTPS uses the IDF bundle and cross-signed verification; 16 KiB input and 4 KiB output TLS records live in PSRAM, outside the reading hot path. The download checks free TF space, writes `/readpico/firmware/update.bin.part`, validates length and SHA-256, then publishes the image and finally `update.sha256`. A local cache with a different size or digest is never reported ready. A second tap confirms the switch to the small `ota_1` updater. The updater rechecks SHA-256, writes only `ota_0`, and switches boot after success. A failed image shows an error and returns to the original system. Pre-staged TF files remain supported for offline updates.

The first deployment of device-side updates needs the `ota_0` system and `ota_1` updater layout: Bootloader → `0x0`, main APP → `0x20000`, updater → `0x420000`. Do not overwrite NVS at `0x9000`, OTADATA at `0xF000`, assets or the TF card. Both readers must report `0.40.0-dual97` before either update path can update them.

上游 CI 工作流原样保留在 [docs/ci/upstream-build.yml](ci/upstream-build.yml)，作为构建参考。本实验分支使用下面的命令进行本地构建与测试。

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
python3 tools/test_native_dual_handoff.py --binary build/dual-native/native-preview --output build/dual-native-handoff
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

从机选择「从机」后先显示「自动搜索主机」与「手动输入」两个明确入口。自动搜索优先通过 BLE 读取主机端点；若已有同一局域网，则自动填出主机地址，若没有局域网则使用主机直连热点。手动输入默认带出本机 IPv4 前三段，只填主机地址最后一段（1–254），或直接输入完整 IPv4，适用于地址前三段不同但网络互通的情形。已配置的从机可从双机页「修改配对」随时回到这两个入口；取消修改不会覆盖原配对。

On the secondary, select Secondary to discover the primary over BLE. An existing LAN endpoint is filled automatically; without a LAN the primary offers its direct hotspot. Enter the six-digit code shown on the primary and save. Manual entry also accepts the primary's last IPv4 octet or a complete IPv4 address for routed networks.

配置保存之后才开始连接；保存成功不等于已连接。配对页分别显示 Wi-Fi 数据通道、蓝牙翻页通道和书库同步状态。错误输入留在当前步骤，取消未完成输入不会覆盖原配对；重复点选主机不会重新生成配对码。

Connection begins only after configuration is saved; saving does not establish a connection. The pairing page separately reports Wi-Fi data, Bluetooth page control and library synchronization. Invalid input remains editable. Canceling preserves an existing pair, and reselecting Primary preserves its code.

## 当前双页恢复 / Resume the current spread

“恢复当前双页”使用主机当前图书、字体、全书双页位置与修订号，不重新打开图书或读取本地历史位置。家庭页只暂停本机阅读显示，双机会话保留；返回书架后点击恢复入口，不自动拉回正文。恢复卡复用上次阅读卡片的四种外观。单机接力使用全书页码，主机在离架时继续接收副机位置；合流先比较双方最远正文锚点，再由主机发布新修订，不补发旧位置。两端均持久化进度，主机仲裁共享双页。

Resume uses the primary's current book/font/book-wide spread/revision without reopening from local history. Home pauses local presentation while preserving the session; shelf resumes explicitly with the selected recent-reading card style. Solo relay uses absolute book positions; rejoin takes the current primary revision without replaying stale locations.

## 双屏动画衔接 / Display handover

下一页右屏先、左屏后；上一页左屏先、右屏后。主机布局就绪即允许另一端准备目标页，两方向均复用单帧缓存提前绘制、启动屏幕电源；前屏每个成功扫描拍后估计剩余扫描时间，在预计剩余约500毫秒时发送当前修订号的提前接力信号，后屏即可推屏；至少先完成一拍，保持方向设备先启动。提前信号与真正完成分别确认，双方真正完成后才接收下一次翻页。门槛前失败不放行；提前接力后发生失败可能已产生部分重叠，但不会伪造完成，仍走原恢复流程。实际提前量受绘制、电源准备和通信耗时影响，500毫秒是目标估计值。BLE 控制入队唤醒已有发送循环，不增加线程或扩大队列。预绘线程栈使用 PSRAM，创建失败保留同步绘制。

Next presents right then left; previous presents left then right. Primary layout releases peer preparation in both directions, using the existing frame cache to render and start rails ahead of handover. After each successful scan tick, estimate remaining scan time and release the trailing screen at about 500 ms remaining for the current revision, after at least one lead tick. Early relay and actual completion have separate acknowledgements; another turn requires both displays to finish. Failure before the threshold does not release the peer; a late failure may follow an early release but cannot fake completion. Rendering, rail preparation and transport affect actual overlap. Enqueue wakes the existing BLE loop, without extra workers or larger queues. The prefetch stack uses PSRAM and retains synchronous fallback.

重连后首个状态可能已是主机合并后的修订；从机收到该修订的主机布局就绪状态即可应用，不再空等另一个修订。仍须正常定位及实际推屏成功后才确认，不能以重连或布局就绪代替推屏完成。

The first reconnect state may already contain the primary reconciliation. Apply it once primary layout is ready rather than waiting for another revision. Normal positioning and successful presentation still precede completion acknowledgement; reconnect or layout readiness cannot substitute for successful display.

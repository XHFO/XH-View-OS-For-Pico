## 0.40.0-dual26（独立双机实验版）

- 从机在主机发布新页时立即开始加载/排版/推屏，不再等待主机确认推屏完成，提前约半秒。
- 修复单人接力/返回主页后本地阅读进度不恢复：对端不在阅读页时优先读取本机进度，不再强制从共享页或书首开始。
- The secondary starts loading/laying out/painting as soon as the primary publishes the target instead of waiting for left_ready, roughly half a second earlier. Honor local saved progress when the peer is not reading, so reopening a book resumes instead of starting at the beginning.

## 0.40.0-dual25（独立双机实验版）

- 单人接力阅读时顶部不再显示过期的“主机/从机第几页”，改为实际全书页码，翻页后同步更新。
- 翻页动画改为 12 帧干净滑动，使用 FOLLOW 快速 DU，去掉灰色阴影；动画后执行一次全屏 GC16，清除页缝以及顶部目录/进度区的残影。
- A solo reader now shows the real whole-book page in the header instead of a stale paired page. The page turn uses 12 clean slide frames on FOLLOW fast DU with no gray shadow, followed by one full-screen GC16 to clear gutter and header ghosting.

## 0.40.0-dual24（独立双机实验版）

- 双机阅读新增接力：一台离开正文/目录去书架、其他应用或批注键盘时，另一台进入单人阅读，可正常翻页；返回后以仍在阅读的一台当前页重新合流。
- 划线与笔记提交后通过 BLE 同步到对端当前同一本书；对端在对应页重绘显示。批注键盘只占本机，另一侧保持独立阅读与翻页。
- Add solo relay reading: when one unit leaves the text/contents for the shelf, another app or the annotation keyboard, the peer becomes a normal single reader and can turn pages; returning rejoins at the peer's current page. Committed highlights/notes sync to the peer for the same book.

## 0.40.0-dual23（独立双机实验版）

- 贴合防误触改为锁存：一旦确认两屏相对，触摸离开、设备一起运动或姿态短暂不明都不会解除；只有展开并稳定同向 800ms 才恢复。
- 贴合锁存期间底部三键仍可导航，但不会清除屏蔽；中央长按恢复取消，避免折叠时误解除。
- Latch the contact guard once opposite faces are confirmed: quiet gaps, shared motion and ambiguous pose no longer release it. Only stable unfolded faces for 800 ms clear it. Keys still navigate but do not clear the latch; the central-hold recovery is removed.

## 0.40.0-dual22（独立双机实验版）

- 修复双机书库尚未完全一致时，姿态/校准/防误触控制帧被 `control_ready` 拦截的问题；这些控制帧现在只要求蓝牙控制通道兼容，不再等待书库同步。
- 修复从机请求校准后主机不响应：校准请求、校准基准、朝向和防误触帧提升到书库就绪检查之前，避免被朝向帧长期插队。
- Send pose, guard, calibration request and baseline before the library-ready gate; calibration no longer depends on both libraries matching and cannot be starved by face updates.
- 双机阅读支持多任务：任一台离开正文/目录页去书架或其他应用时，对端保持当前页并冻结偶数页翻页；回到阅读页后恢复联动。
- Allow one reader to leave for the shelf or another app: the peer keeps its current page and freezes even-page turns; returning to the reading page resumes the pair.

## 0.40.0-dual21（独立双机实验版）

- 校准改为两端各自采样自己的重力基准；主机只负责发出校准开始/触发，避免两台传感器安装或物理旋转不同却共用同一向量。
- Each reader now samples its own gravity baseline; the primary only triggers calibration, avoiding a shared vector when sensor mounting or physical rotation differs.

## 0.40.0-dual20（独立双机实验版）

- 防误触加入桌面展开校准：两台展开平放后点一次「校准开合姿态」，主机采样重力向量并广播，从机保存同一基准；校准前防误触不启用。
- 开合判断改用当前重力向量与校准基准的夹角，贴合时仍屏蔽全部屏幕触摸；展开时完全不触发，正常滑动不受影响。
- Add desk-unfolded calibration for the guard: one tap samples the gravity vector on the primary and broadcasts it to the secondary. Contact uses the angle to the calibrated baseline; the guard stays off until calibration.

## 0.40.0-dual19（独立双机实验版）

- 贴合判定改为只认两端都有明确朝向且相反；不再假设加速度计 z 轴正负对应哪一面，避免展开使用时被误判为贴合。
- Require both readers to report a known, opposite screen face; never assume a z-axis sign, so unfolded use cannot be mistaken for face-to-face contact.

## 0.40.0-dual18（独立双机实验版）

- 姿态检测改为 200ms 低频直读加速度计，折叠/展开切换不再等待后台 500ms 采样，贴合屏蔽和展开放行更及时。
- Low-rate 200 ms direct accelerometer read for pose, so folding/unfolding takes effect without waiting for the 500 ms background sample.

## 0.40.0-dual17（独立双机实验版）

- 防误触改为姿态门控：两台屏幕相对（折叠贴合）时屏蔽所有屏幕触摸，展开同向朝上时完全不启用鬼触判断，正常滑动不再误判。
- 底部三键是独立物理触摸区，始终不被过滤；按中键返回主页会顺带解除屏蔽。屏幕中央/中键带长按 0.9 秒也可恢复，恢复手势等待抬手并给出 2.5 秒放行窗口。
- 防误触开关继续通过 BLE 两端同步；屏幕朝向也以低频率控制帧交换，用于区分贴合与展开。
- 关于本机新增 MAC 地址、芯片版本/核心数、Flash/PSRAM 容量和 PMU UID。
- 扇形快捷菜单恢复为白色实心圆、黑色描边和圈外光圈选中样式；滑动仍只更新圈外小区域，不重绘整圆。
- 双机翻页时主机不做四帧翻页动画，先确认推屏再让从机跟进，减少从机半拍延迟；从机保留动画。
- Face-to-face contact pose suppresses every screen touch while unfolded screens bypass ghost detection entirely. Physical keys always work and clear the guard; central hold recovers. Show MAC, chip, flash/PSRAM and PMU UID in About. Restore the original white-disc fan menu with an outside selection halo while keeping small DU updates. Skip the primary's four-frame animation in paired reading so the secondary starts sooner.

## 0.40.0-dual16（独立双机实验版）

- 恢复电源键翻页短提示音，仅在本机操作被接收时异步播放，遵循提示音总开关与「按键与触摸」选项；远端同步、长按菜单和被浮层阻止的操作不重复发声。
- Restore asynchronous local power-key page feedback with existing mute preferences; remote synchronization and blocked input stay silent.

## 0.40.0-dual15（独立双机实验版）

- 页码跳转窗口只保留取消与跳转，主按钮黑底白字，数字键采用单层边框与黑白按下反馈。
- 修复定位中重复点击留下按下态；保留退格、清空、无效页码提示与双机同步定位。
- Simplify numeric page entry to Cancel and Jump, with a prominent primary action and single-outline keys. Prevent stale pressed decoration during a pending seek; retain validation and paired navigation.

## 0.40.0-dual14（独立双机实验版）

- 双机菜单主机与从机下各增加「旋转屏幕 180°」开关，自动方向统一跟随主机。
- 双机方向独立保存，关闭后恢复原系统方向与锁定偏好；旋转不改变共享字号、页码与正文位置。
- Add independent 180-degree role switches with primary-following automatic orientation. Preserve normal system rotation preferences and shared reading position. Both readers require dual14 (BLE control v4).

## 0.40.0-dual13（独立双机实验版）

- 两端均可调节共享字号，保留原文锚点重新分页，失败或中断可恢复。
- 恢复目录、进度拖动与返回原处、书签笔记原文定位；修复设置返回和同书续读跳回书首。
- 保留左屏先显示与独立180度旋转；旧版阅读进度迁移，两台须同时更新。
- Shared size changes preserve the source anchor. Restore paired contents, scrub and annotation navigation; keep the current spread on resume. Preserve primary-first rendering and migrate previous reading checkpoints. Both readers require dual13.

## 0.40.0-dual12（独立双机实验版）

- 主副机各自支持竖屏180度翻转，保持相同分页；可锁定方向或手动切换。
- 任意端发起翻页时，主机左页推屏成功后再更新副机右页。
- 恢复全书页码输入：验证目标页后共同跳到对应双页，错误输入保留原位置。
- Both readers support independent portrait inversion, primary-first presentation and validated numeric jumps to a shared spread. Both devices need dual12; the main system branch is unchanged.


## 0.40.0-dual11（独立双机实验分支）

- 文件清单优先比对，同一可信存储会话内复用未变化文件的校验结果，仅补齐差异文件。
- 大文件网络断线可在同会话内续传；重读已接收前缀并最终校验完整文件，不发布不完整或损坏内容。
- 修复浮层阻断双机通信；同步期间点击图书提供等待提示，书库就绪后自动打开，编辑和电源保护保持有效。
- 数据协议升级到 3，两台须一并更新；重启、换卡或 USB 文件管理后重新完整校验。
- Compare manifests first, reuse verified unchanged files within a trusted live storage session, and transfer missing content only. Resume interrupted files with prefix rehashing and final full-file verification. Keep communication alive under idle overlays and queue book opens until ready. Protocol 3 requires both readers to update; storage ownership changes invalidate cached state.

## 0.40.0-dual8（独立双机实验分支）

- 修复长时间同步刚完成又被每分钟全量重扫打断的问题；仅在书库内容变化或用户主动请求时重新校验。
- 书库校验、传输和已连接的双机会话暂缓自动休眠；显式锁屏、休眠和关机仍有效，不改用户保存的自动休眠时间。
- 对端停在普通系统设置页时，也可跟随开书；输入框和应用编辑页面仍保留原内容，离开后再应用阅读位置。
- 增加有线诊断中的传输进度、书籍摘要和实际分页状态，支持按已校验目录序号开书，便于两台实机独立核对。
- Fix timed rescans invalidating slow synchronization; rescan only on library changes or explicit requests. Keep active pairing and transfers awake without changing saved power preferences. Follow book opens from ordinary settings while preserving editing screens; add bounded USB diagnostics for real-device verification.

## 0.40.0-dual6（独立双机实验分支）

- 修复填写主机地址后配对码输入框被错误关闭；增加自动补全地址前三段、大数字键盘和明确的两步配对流程。
- 地址/六位码校验失败保留输入；取消配置保留旧配对，重复选择主机保留原码。
- Fix the pairing-code editor closing after address submission; add IPv4 prefix completion, a large digit pad and two explicit steps. Preserve drafts on validation errors and existing pairing on cancellation.

## 0.40.0-dual5 · 保留 TF 与双无线的 DMA 余量

- 将不参与 ISR/DMA 的书架索引、封面路径缓存和批注草稿数组迁入已有 PSRAM 缓存区；存档格式和交互保持一致。
- 配合双机专用内存预算，减少常驻 UI 缓存对片内内存的占用。

## 0.40.0-dual4 · 双机内部内存预算

- 普通中型缓冲优先使用 PSRAM，增大无线、任务栈和 TF DMA 共享的内部保留池。
- Wi-Fi 编译配置与已有运行时 4+4 上限对齐；保持板级时序、分区和双机通信协议不变。

## 0.40.0-dual3 · 双机无线与 TF 内存共存

- 减少双机 BLE 控制器活动数量，改善开启蓝牙后的 TF 书库读取余量；Wi-Fi 实际缓冲仍受已有运行时 4+4 上限约束。
- USB 双机状态补充数据连接、蓝牙启动/错误与书库错误标志，便于检查实机配对。

## 0.40.0-dual2 · Wi-Fi 数据与蓝牙翻页

- Wi-Fi 局域网只传书库、字体；加密且认证的 BLE 独立传递开书、页码和翻页指令，显示两条链路状态。
- 两端校验书库一致后，无 Wi-Fi 也可经蓝牙翻页；丢失通知重发最终状态，重复或过期指令不重复翻页。
- 双机模式临时独占蓝牙，退出后恢复原 BLE 翻页器开关和配对记录。两端须一并更新，不能与 dual1 配对。

## 0.40.0-dual1 · 独立双机阅读实验版

- 同局域网主从配对、TXT/EPUB 双向补齐与共享字库；任意一端开书，两端连续双页联动。
- 主机电源短按上一组，从机短按下一组；长按保留电源菜单。独立分支，不并入主系统。
- 失败和重连保留原图书；固定共享分页，奇数末页从机显示结束页。范围见 [双机阅读](DUAL-READER.md)。

## 0.40.0-dev71

- 灵犀画板信息模块改用有界语义数据，设备本地绘制文字、图表、排行及 16 灰图片素材，横竖屏共用同一份内容。
- 局域网与 USB 直传共用数据模型；后台更新保留阅读与画稿，不导出 TF 卡，保存与实际显示分别确认。
- 本地时钟和进度重绘独立于传输回执，保留自定义图片锁屏与旧图片数据的读取能力。
- Render semantic Lingxi Board modules on-device in both orientations, share data transport over LAN/USB without exporting TF or interrupting reading, and keep local time repainting separate from transfer receipts.

## 0.39.2-dev70

- 修复 Home Assistant 从书架进入时误报 Wi-Fi 离线、已联网扫描时无法打开手机配置，以及网络状态变化后页面未更新。
- 手机配置和管理页使用局域网服务发现，列出真实地址与端口；选择后再保存，不覆盖已有连接。
- 实体同步请求 HA 提取所需字段，避免完整状态中的大型属性触发容量限制；失败保留原快照。
- Fix live Wi-Fi status and phone setup admission, add non-mutating LAN service discovery, and request bounded HA state projections while preserving prior snapshots on failure.

## 0.39.1-dev69

- TF 卡检测通信错误不再误判为拔卡；安全恢复挂载后重建字体、书架和应用目录，同一媒体会话可继续保存，换卡仍保护原会话未保存内容。/ Preserve verified TF state across card-detect communication failures; restore fonts, shelf and apps after safe remount without writing an old session onto replacement media.
- Home Link 10104 区分家庭监看、连接设置与打印机入口，修复横屏字段和底栏重叠；旧版 HA 配置可兼容读取，绘制不再触发网络，实体控制后台排队，休眠立即禁止新任务。/ Separate Home Link destinations, fix landscape and footer geometry, retain legacy HA settings, keep painting pure and queue controls behind a suspend-aware worker.
- 薄宿主移除旧家庭/打印机界面的重复存档路径；TF 工具包按当前源码统一构建。/ Thin builds stop persisting obsolete home/printer UI state and rebuild TF tools from matching sources.

## 0.39.0-dev68

- 整合 GitHub 的 Home Link 共享配置桥、直接设置入口、列表滑动、简体中文／英文和局域网扫码填写；保留 USB 与 PSRAM 修复。/ Integrate shared Home Link configuration, setup entry, list scrolling, Chinese/English and LAN QR setup while retaining USB/PSRAM fixes.
- 手机填写页修复中文乱码、到期清理与内存失败，提交前验证所有槽位并提示部分保存结果。/ Fix portal UTF-8, expiry cleanup and allocation failure; preflight all slots and explain partial saves.
- Home Link 10103 保留连续编辑和失败重试草稿；滑动翻页后松手不再误点新页条目。/ Preserve sequential edits and retry drafts in Home Link 10103; prevent a drag release from selecting a newly paged item.
- 双语文案无损压缩；电脑预览使用相同的原生门户、配置桥和界面源码。/ Losslessly pack bilingual copy and share the native portal, bridge and UI with desktop preview.

## 0.39.0-dev64

- 系统标识更新为 XH View OS For Pico · 先行预览测试版；设置按六种用途分组，存储与传输直接进入统一入口，子设置逐级返回。
- 顶部品牌卡打开关于本机和首次使用引导；保持唤醒移至系统与电源。
- 静态启动页同步显示系统名、版本与测试版标识，保持原生 16 灰阶，不显示进度条。
- Introduce XH View OS For Pico as an early preview test build, group settings by purpose with hierarchical Back, and place About/first-use guidance in the brand card and Keep Awake under System & Power.

## 0.38.1-dev63

- 书架封面视图仅第一页显示续读卡；第二页起使用 3×3 九宫格，每页九本，横竖屏共享连续的书单与点击位置。
- 九张可见封面统一准备和保留，刷新完成前保持原画面，避免缓存容量不足使后几张封面缺失。
- Keep the continuation card on the first cover-view page and use nine-book 3×3 pages thereafter, with shared portrait/landscape pagination, hit geometry and complete visible-cover retention.

## 0.38.0-dev62

- 微信读书独立应用新增在线搜书、资料与封面、目录、账户进度、推荐／相似书、阅读统计、热门划线与公开书评；设备和手机后台使用同一只读服务，保留离线书架与个人笔记。
- 搜索复用系统键盘，图书详情可扫码打开官方阅读链接；查询结果按请求隔离、分块读取，封面使用 16 灰阶缓存。正文仍在官方阅读器中阅读。
- Add on-device and phone-managed WeRead discovery, metadata, chapters, progress, recommendations, statistics and community content, with system-keyboard search and official reading-link QR codes; retain offline personal notes without claiming full-book downloads.

## 0.37.0-dev61

- 微信读书新增设备本机扫码授权与同步，手机直接打开 Pico 局域网后台填写 API Key、选择书籍并同步划线与想法，无需 Mac；密钥仅存设备 NVS。
- 本机同步异步执行并原子发布离线索引，失败保留原数据；与 USB、存储维护及电源共享准入保护。
- Add on-device WeRead QR setup and background shelf/personal-note sync without a Mac, keeping credentials in device NVS and preserving snapshots on failure.

## 0.35.1-dev59

- 灵犀画板支持同一数据快照的横竖双版原子保存；设备旋转即时选择本地对应布局，无需电脑再次推送。旧单图、后台接收及手动展示行为保持兼容。
- Lingxi Board atomically persists portrait and landscape layouts from one snapshot and selects the matching local layout on rotation, retaining single-image compatibility and existing background/manual presentation behavior.

## 0.36.0-dev60

- 新增「应用集」启动器，集中打开系统工具、首页入口与 TF 应用；设置始终保留入口，已有首页顺序不改。
- 新增独立「网站流量」应用，保留 Cloudflare 横竖两版快照；与普通灵犀画板独立保存，后台更新不打断当前页面。

## 0.35.0-dev58

- 灵犀画板新增无需导出 TF 卡的 USB 分块传送与完整校验；自动更新后台保存，手动发送可明确请求展示，锁屏保持不被打断。/ Add chunked, validated USB board transfer without exporting TF; automatic updates save in the background, explicit manual sends may request display, and locks remain uninterrupted.
- 已鉴权且成功完成的局域网管理请求延长空闲会话，持续推送不再因固定到期而断开。/ Successful authenticated LAN management requests extend the idle session, preventing active pushes from ending at a fixed deadline.

## 0.34.1-dev57

- 修复多屏灵犀无法同步内置存储中的划线与批注：USB 交接前只读导出内置存储与 TF 卡的已提交记录，保留原笔记、旧格式与草稿；双缓冲快照失败时不交接过时内容。/ Fix Lingxi annotation sync by exporting committed internal and TF records before USB handoff, preserving source notes, legacy records and drafts; failed double-buffer snapshots do not hand off stale content.

## 0.34.0-dev56

- 新增外接电源下的灵犀画板锁屏，后台接收网络画板不打断阅读与画稿；明确区分已保存与已显示。
- 书架新增画廊、聚焦排布；封面锁屏新增留白海报、全屏书墙，保留原偏好。

# 版本变更 / Changelog

## 0.33.0-dev55

- 键盘在独立触摸采样任务中保留短按，文本先更新、候选与屏幕等待短暂停顿后合并显示，灰阶定稿让位于连续输入。/ Capture keyboard taps independently, update text first, batch candidate/display work after brief idle, and defer grayscale settling while typing.
- 新增非阻塞提示音：触摸、充电接入和错误使用不同声调，设置内可分别关闭；休眠前停止声音，唤醒不补播。/ Add nonblocking distinct touch, charging and error tones with individual settings; silence before sleep without replay on wake.

## 0.32.0-dev54

- 设置新增无线管理入口，提供手机扫码管理与电脑 USB 辅助配网；限时配对信息只在明确连接时交给已核对身份的电脑。
- 修复书签与笔记、输入编辑器及家庭子页面的页头返回按钮，统一逐层返回与草稿保存检查。
- Add a discoverable wireless-management page and identity-checked USB-assisted Wi-Fi setup with temporary pairing.
- Fix heading Back controls in reading records, editors and Home child pages, preserving nested dismissal and draft checks.

## 0.31.0-dev53

- 多屏灵犀可通过同一配对 Wi-Fi 会话管理图书、字体与 TF 应用，支持上传进度、保存校验和画稿原稿导出；USB 与无线存储互斥。
- 后台 USB 连接提示改为非模态，设置与缓存灵犀画板可继续操作；主机独占 TF 时保留本机文件访问保护。
- 时钟字形无损裁去透明行边，保留全部 16 灰阶像素、采样与原字体，为文件管理接口释放 APP 空间。

## 0.30.5-dev52

- 设备管理状态及 USB 画板回执提供真实供电快照，区分充电、电池和满电外接供电，供多屏灵犀自动调整画板更新频率；沿用 PMU 缓存，不为查询额外访问硬件。
- Device management status and USB board receipts expose cached power snapshots, distinguishing charging, battery use and full external supply so Lingxi can adjust board updates without additional hardware polling.

## 0.30.4-dev51

- 后台 USB 连接提示仅在可见状态变化时刷新，电脑读取和写入计数不再反复刷新底部区域；前台传输仍显示写入量，并保留连接、错误和安全退出反馈。
- Refresh the background USB notice only when its visible state changes, avoiding repeated bottom-area refreshes for host read/write counters. The transfer page retains written-volume updates, connection/error feedback and safe ending.

## 0.30.3-dev50

- 锁屏复用已经提交且无绘制债务的原页面，不再重复绘制；阅读和家庭存档在未变化、未移交存储时复用成功检查点，保存失败仍重试。摘录按设定间隔轮换，不再每次锁屏额外推进并保存。
- Lock reuses a fully presented page without paint debt. Unchanged reader/home state reuses successful checkpoints until storage handoff, while failed saves remain retryable. Highlights rotate on their configured interval instead of advancing and committing on every lock.

## 0.30.2-dev49

- 移除锁屏前的额外提示刷新；未变化的存档经过主文件完整内容核对后免写，联网任务收束与必要保存并行，仍确认保存与停网成功后进入低功耗。/ Remove the extra pre-lock status refresh; skip writes only after complete primary-file equality checks, overlap network quiescence with necessary saves, and enter low power only after both succeed.
- 保留当前页锁屏只在画面基准可靠时局刷锁屏标识；切换锁屏样式或存在未完成刷新时保留完整清屏。/ Retained-page locking updates only its lock mark when the display baseline is reliable; other appearances and unfinished refreshes retain full cleaning.

## 0.30.1-dev48

- 系统稳定画面统一保留原生16灰阶；角落菜单保留灰阶图标与抗锯齿，输入和画板仅临时二值化变化区域，静置后统一灰阶定稿。/ Stable system UI retains native 16 grays, including corner icons and antialiasing; input and drawing threshold transient damage only and settle back to grayscale after inactivity.
- 自定义图片锁屏兼容原有黑白图片并新增16灰阶格式；锁屏前先显示状态提示，再安全保存与暂停网络。/ Custom image locks retain monochrome compatibility and add 16-gray images; locking shows immediate status before safe persistence and network quiescence.

## 0.30.0-dev47

- 后台 USB 传输保留当前画面，只显示底部连接状态；电脑独占 TF 时暂停本机文件访问，安全弹出后恢复。/ Background USB retains the current frame with a bottom status strip, pauses local TF access while the host owns it, and restores it after safe eject.
- 设置将 TF 卡、笔记存储和文件传输合并为置顶的“存储与传输”；正文中轻点已划线文字可打开对应记录，选择取消划线或删除批注。/ Storage and transfer are grouped first in Settings; tapping annotated text opens its exact saved record for highlight or note removal.
- 锁屏触摸补读短暂 GPIO 唤醒事件，避免漏掉双击首击；蓝牙停用完成后重新判断可用轻睡眠。/ Read pending short GPIO touch wakes so the first tap is not missed, and reassess available light sleep when Bluetooth shutdown completes.

## 0.29.0-dev46

- 灵犀画板支持 USB 有线传送，安全交还 TF 后读取原生 16 灰阶画面，并按任务确认实际显示结果。/ Lingxi Board supports USB transfer with safe TF handoff and task-specific confirmation after native grayscale presentation.
- 设备网页后台统一标题、键盘焦点与字段说明，保持配对和未保存内容保护。/ Device web management clarifies page titles, keyboard focus and field descriptions while retaining pairing and unsaved-input protection.

## 0.28.3-dev45

- USB 维护回应始终独占新行，避免串口里残留的调试日志干扰多屏灵犀自动连接；保留原有命令和确认规则。/ Begin every USB maintenance reply on a fresh line so leftover console logs cannot obstruct Lingxi auto-connect, retaining existing commands and acknowledgements.

## 0.28.2-dev44

- USB 传输按多扇区批量读写，减少重复卡检测和逐扇区命令，改善大容量 FAT32 卡在 Mac 挂载时超时的问题；保留同步写入、拔卡失效与安全弹出保护。/ Batch USB transfers across multiple sectors to reduce repeated card checks and single-sector commands, improving Mac mount timeouts on large FAT32 cards while retaining synchronous writes, media invalidation and safe eject.

## 0.28.1-dev43

- 灵犀画板以屏幕原生尺寸保存与显示16灰阶内容；兼容已有黑白画布，画板与自定义锁屏图片保持独立。/ Lingxi Board persists and displays native-resolution sixteen-level grayscale, while retaining monochrome canvases and separate custom lock images.
- USB管理状态明确报告TF卡的导出、主机连接与归还结果；多屏灵犀可开启传输并等待磁盘挂载。/ USB management reports TF export, host connection and return status; Lingxi can initiate transfer and wait for the mounted disk.

## 0.28.0-dev42

- 自定义图片锁屏：通过多屏灵犀单独上传、预览并选择，下次锁屏生效；TF卡离线保存，横竖屏等比显示，保持原有电源与唤醒逻辑。/ Custom image lock screens: separately upload, preview and select from Lingxi for the next lock; persisted offline on TF, aspect-fit in either orientation, retaining unified power/wake behavior.


## 0.27.1-dev41

- 合并笔记页显示当前可用的存储；缺少另一处笔记库时不提供无效合并按钮，已有笔记继续可用。
- 内置存储挂载失败后本次开机不反复尝试，不自动格式化；避免重复占用资源。
- 离开笔记编辑后可合并记录，未提交的草稿继续保留；正在编辑时仍拒绝合并。
- 迁移到空白目标存储时完整写入所有笔记对象，索引成功后才切换记录；失败保留原库与草稿。

## 0.27.0-dev40

- 笔记、划线与书签扩至1024条，单条文字2047 UTF-8字节；旧记录和草稿兼容读取，按实际内容分配内存并增量保存。
- TF卡与存储新增笔记合并入口，显式从另一处可用存储导入，保留源库并跳过相同记录；失败保留原目标库。
- 网页预览与固件共享上述记录格式与交互。

## 0.26.5-dev39

- 中文：统一手机管理页与 Mac 内嵌网页的离开保护；未保存输入、验证码和待完成操作会提醒，确认保存后解除保护。重新读取期间的新输入不会被覆盖。
- English: Unify departure protection in mobile management and the Mac embedded page. Warn for unsaved inputs, verification and pending actions, releasing protection after confirmed saves; retain new edits made during rereads.

## 0.26.4-dev38

- 中文：移除手写文字输入和离线识别模型依赖，保留拼音26键/9键、英文、数字符号及旧笔迹附件兼容。
- English: Remove handwriting text input and its offline model dependency, retaining pinyin QWERTY/T9, English, symbols and existing ink attachments.
- 中文：画板整纸随方向等比旋转，横屏工具移到侧边，新增背景网点；旧原稿保持比例与坐标，保存失败可重试或明确放弃。
- English: Rotate the complete drawing paper at its original aspect ratio, move landscape tools to the side and add background dots; preserve legacy coordinates and offer retry or explicit discard after failed saves.
- 中文：网页管理保存后核对设备回读，失败或不一致时保留草稿并提示尚未确认。
- English: Confirm web settings against device readback, retaining drafts and showing unconfirmed submissions on failures or mismatches.

## 0.26.3-dev37

- 中文：修复手写模型连续内存分配失败；拼音词库与手写模型按模式互斥加载，分时识别可被新笔画取消，保留全部汉字覆盖。
- English: Load handwriting templates in bounded memory blocks, share memory between input modes, and cancel incremental recognition on new strokes without reducing character coverage.
- 中文：键盘采用逐相连续 DU，手写和画板不再套用普通控件的 70 毫秒采样节流；交互期间延后可选后台轮询。
- English: Use one-phase continuous DU for keyboard input, remove the ordinary 70 ms move throttle from handwriting/drawing, and defer optional polling during interaction.
- 中文：修复 USB 传输结束后画板等应用入口丢失；小游戏确认弹窗支持点击外部关闭，保留原棋局。
- English: Restore application destinations after USB transfer and dismiss game confirmation panels on outside taps while retaining the position.

## 0.26.2-dev36

- 中文：手写改为连续 DU 增量笔迹与 TF 离线汉字候选；增加独立 TF 画板笔记应用，可保存与继续编辑。两角菜单合并重复推屏并保留所有图标；书名搜索表无损紧凑存储，保留全部码点和读音。
- English: Continuous-DU handwriting with offline TF Han-character candidates; an independent TF drawing-notes app with saved, editable boards. Fan selections present once and retain icons; filename-search data is packed losslessly without removing characters or readings.
- 中文：支持多屏灵犀 Mac 管理图书、TF 应用、画板原稿及固件；通过临时配对连接保存电源设置、推送图片或手绘画布，画布存入 TF 卡并可离线重开。接收提交与完成显示分别确认。
- English: Add Lingxi Mac management for books, TF apps, drawing originals and firmware. Temporary pairing supports power settings and image/drawing pushes, persisted on TF for offline reopening; request acceptance and completed display are confirmed separately.

按日期和作者简述对用户可见的功能变化；详细实现历史见 Git。使用方法见 [README](../README.zh-CN.md)。
User-visible changes by date and author; Git retains implementation history. See [README](../README.md) for usage.

## 2026-10-21 · 0.26.1-dev35 连续输入刷新 / Continuous input refresh

- 键盘使用高对比黑白显示；输入只重绘文字、拼音与候选变化区，每键合并为一次 FOLLOW DU 快速推屏，静态字母键保持不动。输入期间不插入周期强刷，退出恢复普通页面灰阶；旋转和异常保留完整恢复。刷新机制见[显示刷新契约](DISPLAY-REFRESH.md)。
  Use a high-contrast binary keyboard, repainting only text/composition/candidate changes and combining them into one FOLLOW DU present per key. Keep static letters unchanged and periodic cleans out of typing. Restore normal grayscale on exit, retaining full recovery for rotation and errors. See the [display refresh contract](DISPLAY-REFRESH.md).

## 2026-10-20 · 0.26.0-dev34 笔记、输入与导航 / Notes, input and navigation

- 笔记存档按实际记录数写入，兼容原存档；延后空闲草稿写入，草稿写入失败不再阻止正式保存。失败保留内容与原存档，显示实际存储位置和具体写入错误。
  Write compact note blocks while loading prior saves; defer idle draft writes and let formal saves proceed independently. Retain content and prior saves on failure, showing the actual storage destination and write error.
- 拼音 26 键与九键增加 TF 离线扩展词库、连续拼音与词频候选；缓存键盘布局，输入时不逐键读卡。缺少有效扩展词库时仍支持内置输入。
  Add a TF offline lexicon, continuous-pinyin candidates and frequency ranking to QWERTY/T9. Cache keyboard geometry and query in memory; retain built-in input without a valid extension.
- 阅读选文、页码、插图确认等浮层可点外部空白关闭，不触发底下的翻页或操作。
  Tap outside reading selection, page-number and image-confirmation panels to dismiss without activating underlying controls.
- 首页入口可排序、替换、添加和隐藏，顶部、左右滑动及快速切页共用顺序；设置入口固定保留，自定义应用可注册稳定标识。
  Customize home destinations with one shared order across tabs, swipes and the page switcher. Keep Settings reachable and support stable IDs for registered custom apps.
- 合并锁屏设置入口，电源页管理时间和唤醒；锁屏外观子页只展示当前样式相关的封面、摘录选项，并提供预览。
  Consolidate power/lock navigation: timing and wake controls stay in Power and Lock; Appearance shows only the selected face's cover/excerpt options and a preview.

## 2026-10-20 · 0.25.2-dev33 云端连接时间校准 / Cloud connection time

- Wi-Fi 连接后自动联网校时并写回 RTC；启动和手动设时均同步到网络安全连接使用的系统时钟，未校准时明确引导日期与时间设置。
  Automatically synchronize time over Wi-Fi and save it to the RTC; restore the secure-connection system clock at startup and after manual setup, with clear guidance while unset.
- 网页与设备分别显示时间未校准、TLS 安全连接／证书失败和网络不可达，保留证书与有效期校验。
  Device and web UI distinguish unset time, TLS secure-connection/certificate failure and unreachable networks while retaining certificate and validity checks.
- 中国区手机号与邮箱验证提示分别显示；首次请求验证码可立即操作，实际发送请求后再启用 60 秒冷却。
  Show distinct phone/email verification guidance for China accounts; allow the first code request immediately and apply the 60-second cooldown after an actual send request.

## 2026-10-19 · 0.25.1-dev32 共享 Bambu 账户 / Shared Bambu accounts

- Bambu 云端改为共享账户卡与绑定打印机列表，跨槽位连续添加；已保存账户按区域和身份归组，可不输密码继续，由后台重新验证。
  Share one Bambu account card and bound-printer list across slots; group saved accounts by region and identity and revalidate saved credentials without another password entry.
- 新登录导入时以单次事务同步同账户云端槽位令牌；复用已存凭据不覆盖其它槽位，失败保留全部原配置。账号密码不保存，共享 RAM 会话仍限五分钟，停止管理即撤销。
  Fresh-login import synchronizes same-account cloud tokens in one transaction; reuse preserves sibling tokens and failures retain the entire configuration. Never persist account passwords; shared RAM sessions still expire after five minutes and cancel when management stops.

## 2026-10-19 · 0.25.0-dev31 Bambu 账号登录 / Bambu account login

- 手机管理页支持按区域登录 Bambu 账号、邮箱／短信验证码和绑定打印机选择，保留已有 Token 高级配置；只保存监看凭据，不保存账号密码。
  Phone management supports regional Bambu account login, email/SMS verification and bound-printer selection, retaining advanced existing-token setup and persisting monitoring credentials only.
- 登录任务在设备与预览共用的后台工作器运行；临时会话限时有效，离开管理或休眠即撤销，旧页面不能导入新账户的设备。验证器分支可明确改用验证码；网页挑战不绕过。
  Device and preview share the asynchronous login worker; transient sessions expire, cancel on management exit/suspend, and reject stale imports. Authenticator challenges can explicitly switch to code login; website challenges are not bypassed.

## 2026-10-19 · 0.24.0-dev30 手机扫码管理 / Phone QR management

- Home Assistant 与打印机连接改为扫码打开设备管理网页，分别填写 HA、Bambu 局域网／云端、Formlabs Dashboard 专属字段；凭据保留在设备配置中。
  Scan a device QR code to configure HA, Bambu LAN/cloud and Formlabs Dashboard in separate phone forms; credentials remain in device configuration.
- 同一网页提供本地 HA 标准实体的房间归属、筛选、分页与控制；设备与网页预览共用配置和控制接口，保存与连通结果分别显示。
  The same webpage supports room assignment, filtering, pagination and control of standard local HA entities through the shared device/preview configuration and control API, with separate save and connection results.
- 管理入口限时开放，退出即关闭，期间暂缓自动休眠；不占用 TF 传书或暂停家庭服务。
  Management expires automatically and closes on exit, temporarily pauses idle sleep, and neither owns TF transfer nor suspends home services.

## 2026-10-19 · 0.23.1-dev29 分品牌打印机接入 / Source-specific printer setup

- 打印机先选 Home Assistant、Bambu 局域网、Bambu 云端兼容模式或 Formlabs 官方云端，再只显示对应的认证字段。Formlabs 使用 Client ID/Secret 和 Dashboard 序列号；Bambu 云端使用地区、MQTT 用户名、已有令牌和序列号，局域网使用 IP、访问码及可信 CA。
  Choose HA, Bambu LAN, Bambu cloud compatibility or official Formlabs cloud before entering only that source's authentication. Formlabs uses client credentials and Dashboard serial; Bambu cloud uses region, MQTT user, existing token and serial, while LAN uses IP, access code and trusted CA.
- 编辑采用独立草稿，保存后提交；切换来源清除不适用凭据，取消/全局切页不污染其他配置。旧版本按当前来源迁移，保留其他设备；保存失败保留原配置。
  Edit a separate transactional draft; source changes wipe foreign fields and cancel/global navigation leaves saved settings intact. Migrate legacy records by source while preserving other devices; failed saves retain prior configuration.
- 对照官方 Formlabs Dashboard API 校验 OAuth 和打印状态结构，空闲时清除旧任务。Bambu 云端明确为已有令牌兼容模式，不提供未经验证的账号登录；独立 LAN 的 PEM 证书配置属于高级选项。真实账号及设备仍待连接验收。
  Verify OAuth and printer payloads against the official Formlabs Dashboard API and clear stale idle jobs. Label Bambu cloud as existing-token compatibility without inventing account login; standalone LAN PEM setup is advanced. Production accounts/devices still require acceptance.

## 2026-10-19 · 0.23.0-dev28 电源菜单与统一图标 / Power options and consistent icons

- 电源键完整单击锁屏/唤醒，长按约1.5秒打开锁屏、关机、重启菜单；关机前完成锁屏强刷并在底部保留“已关机”，刷新失败不会直接断电。
  A complete power-key click locks/wakes; holding about 1.5 seconds opens lock, shutdown and restart. Finish a full lock-screen refresh with a bottom shutdown label before cutting power; a failed refresh cancels shutdown.
- 自动休眠提供关闭、1、3、5、10、30分钟，并保存设置。开发测试的“保持唤醒”优先。锁屏可选双击触屏唤醒，采用CPU浅睡与触摸中断；触摸NORMAL较DEEPSLEEP耗电更高，整机待机电流尚未实测。
  Persist six automatic-sleep choices (off, 1, 3, 5, 10, 30 minutes), overridden by development Keep awake. Optional lock-screen double tap uses CPU light sleep and touch interrupts; NORMAL touch consumes more than DEEPSLEEP and system standby current is not yet measured.
- 统一40个圆角线性图标与功能语义，包含书签、划线、目录、收藏、存储及电源状态；网页和设备共用同一灰阶抗锯齿绘制器，保留图标来源及许可证。
  Unify forty rounded line icons and their meaning across bookmarks, highlights, contents, favorites, storage and power. Device and webpage share the grayscale antialiased renderer, with source attribution and licenses retained.

## 2026-10-19 · 0.22.1-dev27 电源键点按切换 / Power-key click toggle

- 单次点按电源键锁屏并进入低功耗等待，再次点按恢复原页面；锁定时不再响应触屏双击。触摸进入 DEEPSLEEP，唤醒后复位恢复。两向均消费完整短按，移除产品唤醒后的两秒按键忽略，避免下一次点按被吞或同一次松手再次锁屏。
  One power-key click locks and enters low-power waiting; the next restores the retained page. Disable touch double-tap wake and deep-sleep/reset the touch controller. Consume complete short presses in both directions and remove the product post-wake two-second ignore, preventing swallowed clicks or relocking on the same release.
- 网页预览的电源按钮调用同一切换入口，不根据旧画面状态选择锁屏或唤醒命令。
  The preview power button calls the shared toggle instead of choosing a lock/wake command from stale rendered state.

## 2026-10-19 · 0.22.0-dev26 灰阶界面与功能补齐 / Grayscale UI and integration

- 统一纸白底、浅灰卡片、边框与选中层次，保留文字抗锯齿与 16 灰阶；静态页面按实际变化区域定稿，扇形选择、时钟翻页及运行中的贪吃蛇保留快速刷新。
  Unify paper backgrounds, inset cards, borders and selection hierarchy while retaining antialiased text and 16 grays. Settle static changes in their actual changed region; retain fast refresh for fan selection, clock flips and running Snake.
- 合并锁屏、休眠、重启和关机入口，支持翻页时钟、随机摘录、三种藏书封面布局及保留当前画面的锁屏；双击解除锁屏、深休眠按电源恢复，保留页面、进度和游戏任务状态。加入手动日期、时间、时区与 RTC 写入。
  Unify lock, sleep, restart and shutdown. Offer flip clock, random excerpts, three cover layouts and current-page lock, preserving page/progress/task state. Double-tap unlocks; sleep resumes with the power key. Add manual date, time, timezone and RTC setting.
- 家庭页改为未配置空状态，新增真实 HA REST、本地/外部地址、房间映射、筛选与手动排序；打印机支持 HA 导入、Bambu TLS MQTT/LAN/云端及 Formlabs OAuth 只读监看。详见连接说明，真实账户验收另行进行。
  Start Home unconfigured and add real HA REST, local/remote routing, areas, filtering and ordering. Monitor printers through HA import, Bambu TLS MQTT LAN/cloud and Formlabs OAuth. See the connection guide; production account acceptance is separate.
- 六个 TF 游戏包更新至 1.0.1；贪吃蛇在按下时接收方向，排队两个合法转向，并支持棋盘滑动。网站目录支持下载、选择 TF 目录安装/移除与保留存档。
  Update six TF packages to 1.0.1. Snake accepts directions on press, queues two legal turns and supports board swipes. The web catalog supports downloads, authorized TF-directory installation/removal and retained saves.
- 长令牌输入与 Wi-Fi 密码校验分开，保留遮蔽及离页擦除；内部 DMA 内存保护沿用已验收的 hw24a 修复，字体增补只追加缺失字形。
  Separate long-token entry from Wi-Fi password validation, retaining masking and exit wiping. Preserve hw24a's accepted internal DMA-memory fix and append only missing font glyphs.
- 产品注册表移除原始工程演示页，使设备与电脑预览使用相同的四个产品入口；出厂标定及中断自检恢复独立保留。
  Remove original engineering demos from the product registry, matching the native preview's four product entries while retaining factory calibration and interrupted self-test recovery.

## 2026-10-19 · 0.21.0-dev24 TF 卡游戏应用 / TF game applications

- 六个小游戏的规则、AI 和游戏页面迁出固件，构建为独立 PGA1 应用包；TF 启动器按实际安装内容显示，支持移除保档、同 ID 更新与旧存档迁移。应用包、ABI v1、资源与任务卸载、构建工具及网站目录格式统一说明。
  Move all six games' rules, AI and game UI into executable TF packages. The launcher lists installed packages, retains saves on removal, updates by stable ID and migrates legacy records; document PGA1, ABI v1, lifecycle, builds and distribution catalog.
  六つのゲームのルール・AI・画面をTFの実行パッケージへ移し、インストール済み一覧・セーブ保持削除・更新・旧データ移行とPGA1/ABI仕様を追加する。
- 同源电脑预览从私有 TF 动态加载应用，增加独立游戏包导入；设备使用同源 Xtensa ELF，电脑使用对应主机目标，不把预览验证视作实机验收。
  Load real modules from each preview session's private TF and add app-package import. Device and host targets share source, with hardware verification reported separately.
  同源のホスト版とXtensa版を各環境で動的に読み込み、ブラウザー側にゲームパッケージのインポートを追加する。

## 2026-10-19 · 0.20.1-dev23 插图预览确认 / Illustration preview confirmation

- 轻点正文插图先显示“全屏预览图片？”确认框；选择“继续阅读”保留原页，选择“全屏预览”才打开大图。确认框拦截翻页和残留触摸，图片区域上的左右滑动仍用于翻页。
  Ask before opening a tapped inline illustration. Continue reading keeps the page; Full-screen preview opens it explicitly. The modal blocks paging and stale touches, while swipes starting on inline images still turn text pages.
  本文画像のタップ後に全画面プレビューを確認する。読書継続で元のページを保ち、明示的な確認後のみ拡大表示する。本文画像上のスワイプによる改ページは維持する。

## 2026-10-19 · 0.20.0-hw22 书架样式、自动图文与翻页器 / Shelf styles, automatic illustrations and pager

- 书架增加封面网格、图文列表、紧凑列表及样式记忆，加强续读卡片的灰阶对比；上下翻页按真实前后帧计算更新区域，保留切换和清残影时的完整刷新。
  Add remembered cover-grid, illustrated-list and compact-list styles with a clearer last-read card. Calculate shelf paging regions from actual frame differences while retaining full updates for transitions and cleaning.
  表紙グリッド・表紙付きリスト・コンパクトリストと表示記憶を追加し、前回読んだ本のカードを見やすくする。本棚は前後フレームの差分領域を更新し、画面切替と残像除去では全体更新を維持する。
- 新增标准 BLE HID 翻页器设置、配对记忆、重连和方向反转，翻页用于正文及从书首封面进入正文；网页使用明确标注的模拟设备，真实蓝牙与墨水屏表现待实机测试。
  Add standard BLE HID pager settings, pairing memory, reconnect and direction reversal for reading and entering text from the opening cover. The webpage uses labeled simulated peers; physical Bluetooth and e-paper behavior require device testing.
  標準BLE HIDリモコンの設定・登録記憶・再接続・方向反転に対応し、読書と表紙から本文への移動に使用する。Web版の機器は明示したシミュレーションで、実際の互換性と電子ペーパー表示は実機で確認する。
- 阅读顶栏保持原高度，章节名与完整页码/百分比分两行显示，长章名省略但数值不截断，正文空间保持不变。
  Keep the reading header height unchanged, separating chapter title from complete page/percentage values; ellipsize long titles without truncating numbers or reducing the body.
  読書バーの高さを変えず、章題と完全なページ番号・割合を2行に分け、本文領域を維持する。
- EPUB本地JPEG/PNG封面与正文插图自动显示，按比例整图分页；尺寸探测与正常阅读、全书计页共用，像素解码和淘汰不改变分页或批注锚点。正文缓存上限1,296,000字节/64幅，绘图及后台预绘不做文件读取或解码。
  Automatically display local EPUB JPEG/PNG covers and illustrations with proportional whole-image rows. Reading and whole-book counting share probed geometry; decoding/eviction preserve pages and annotation anchors. Bound inline pixels to 1,296,000 bytes/64 images and keep drawing/prepainting free of file IO and decoding.
  EPUBのローカルJPEG/PNG表紙・挿絵を自動表示し、縦横比を維持して画像単位で改ページする。読書と全書ページ計算で寸法を共有し、キャッシュ上限は1,296,000バイト・64枚。描画・事前描画ではファイル読込やデコードを行わない。
- 独立封面首次无进度自动显示，续读跳过；首章第一页向前翻可重看，轻点或向后翻进入正文。封面不计入正文页码，损坏或缺失图片保留失败占位且不阻断阅读。
  Show an independent opening cover for a book without saved progress and skip it on resume; previous at the first text page revisits it, while tapping or turning forward enters text. Exclude the cover from body numbering and preserve readable text around failed image placeholders.
  未読の本では独立表紙を表示し、再開時は省略する。本文先頭で前へ送ると表紙へ戻り、タップまたは次へ送ると本文へ進む。本文ページ数には含めず、画像読込失敗時も読書を続けられる。
- 测试版“保持唤醒”默认开启，在设置顶部可切换并保存；开启时阻止空闲及横屏充电停靠自动休眠，电源键与快捷菜单手动休眠仍有效，关闭后恢复自动策略。
  Default the test build's remembered Keep awake switch to on at the top of settings. Suppress idle and landscape/charging dock standby while enabled; preserve manual power-key/shortcut sleep and restore automatic policy when disabled.
  テスト版は設定上部の「保持唤醒」スイッチを既定でオンにし、選択を保存する。無操作・横向き充電ドックの自動スリープを停止し、電源キーやショートカットの手動スリープは維持する。

## 2026-10-18 · 0.19.1-hw21 棋类对手任务 / Board-game opponent task

- 修复四种棋类对手依赖大块内部内存而无法开始计算的问题，改用扩展内存中的任务栈；保留计算失败时的重试和原有难度、规则及存档。
  Use an external-memory task stack so all four board-game opponents can start without a large internal-memory allocation; preserve retry behavior, difficulty, rules and saves.

## 2026-10-18 · 0.19.0-hw20 原位快捷编排与统一书架刷新 / Direct shortcut layout and atomic shelf refresh

- 书架刷新保留原画面，等待扫描和当前可见封面全部准备后统一显示；封面不可用时使用占位，不再提前显示半成品书单。
  Keep the existing shelf visible until scanning and all visible cover attempts finish, then publish one complete result; use placeholders for unavailable covers.
- 两角菜单保留内3／中5／外6的十四个位置，每个位置直接打开功能，取消分类跳转；在相同扇形位置拖动交换、点选替换或移除，支持撤销，完成后左右同步，取消不保存。
  Keep fourteen direct action positions in mirrored 3/5/6 rings. Swap them in place, tap to replace or remove, and undo changes; save explicitly to update both corners or cancel the draft.
- 滑选期间保留按钮与文字基底，仅更新选中标记和固定位置提示；大光圈不改变触摸判定。
  Keep button and label pixels stable while moving the selection marker and updating the fixed callout; the enlarged visual halo does not change hit testing.

## 2026-10-18 · 0.18.1-hw19 稳定加载与快捷菜单 / Stable loading and shortcuts

- 合并书库扫描与可见封面的加载结果，加载提示保持静止；操作菜单时暂停书架后台读取，避免逐本刷新。
  Batch library and visible-cover results behind a static loading view; pause shelf background reads during menu interaction to avoid per-book redraws.
- 扇形滑选采用既有跟随刷新，按钮本体保持稳定；统一快捷功能开关、分类、排序和命中映射，保留旧配置，标明分类与分页位置。
  Use the existing follow refresh for fan selection with stable button bodies. Unify shortcut toggles, groups, ordering and hit mapping while preserving old preferences and showing group/page locations.
- 同分类仅轻点翻到下一页，滑动选择不再意外翻页；内层三个主页入口保持固定。
  Tap the current group to page its actions; dragging no longer changes pages unexpectedly. The three inner root-page entries remain fixed.

## 2026-10-18 · 0.18.0-hw18 纸上目录与真实封面 / Paper index and real covers

- 默认书架采用列表目录，收紧阅读顶栏，保留目录、页码、细进度及正文操作。
  Use a list-style shelf and compact reading header while retaining contents, page entry, thin progress and text actions.
- 两角扇形采用内层3项、中层5组、外层6项的等距布局；中层可自由换选，扩大选中提示不改变命中范围，提示位置和字号固定。提供40个可配置排序的真实动作。
  Use evenly spaced 3/5/6 corner menus with freely selectable groups, a visual-only selection halo and fixed callouts. Configure and order 40 implemented actions.
- 书架按需读取EPUB声明的JPEG/PNG封面，也支持封面页内引用图片；只预取当前可见图书，缩略图缓存在书目录的`.pico-covers`内。没有可解码封面时保留占位，TXT不推测图片，不修改原书和阅读进度。
  Read declared JPEG/PNG EPUB covers, including image references in cover pages, for visible books only. Cache thumbnails in the book directory's `.pico-covers` folder; retain placeholders for unavailable covers and TXT without modifying books or reading progress.
- Home Assistant与打印机服务仍为模拟，设备实屏与触摸验收单独进行。
  Home Assistant and printer services remain simulations; verify physical display and input separately.

## 2026-10-17 · 0.17.0-hw17 统一主页与快捷操作 / Launcher and unified shortcuts

- 默认书架、Home Assistant、小游戏作为三个可左右切换的主页，设置独立进入；中间实体键单击返回、双击切换主页、长按回书架。
  Swipe between the default bookshelf, Home Assistant and games roots. Open settings separately; the center key goes back on a single press, switches roots on a double press and returns to the shelf on hold.
- 左右下角统一为透明曲线提示及扇形快捷菜单，支持添加、移除和排序；阅读标题、进度和工具入口收至顶部，保留稳定页码输入、拖选和真实图书收藏。
  Both corners use transparent curved hints and a configurable, ordered fan menu. Combine the reading title, progress and tool access at the top while preserving stable page entry, selection and real-book favorites.
- 新增可保存进度的 2048 与贪吃蛇，横竖屏均保留完整操作区；离开贪吃蛇时自动暂停。翻页时钟使用更细的灰阶字模，休眠首帧进行 GC16 强刷。
  Add saved 2048 and Snake with complete controls in both orientations and automatic Snake pause on departure. Use finer grayscale clock masks and a real GC16 refresh on standby entry.
- Home Assistant 与打印机监看仍为明确标注的模拟数据；TF 卡独立小游戏包及网站安装管理安排到后续版本。
  Home Assistant and printer views remain explicitly labelled simulations. Independent TF game packages and website installation management are deferred to a later release.

## 2026-10-17 · 0.16.0-hw16 页码输入与拖动隔离 / Stable page entry and deliberate dragging

- 进度区域单击只打开页码输入，不按触点位置直接寻址；仅先识别到明确的水平拖动，松手才定位。输入页码时暂停后台计页，确认后才计算，消除逐章统计造成的连续整页闪烁。
  A progress-area tap only opens numeric entry; seeking requires a deliberate horizontal drag before release. Count pages after confirmation so background chapter counting cannot repeatedly repaint the input dialog.

## 2026-10-17 · 0.16.0-hw15 进度区域交互 / Progress-area interaction

- 阅读底部整个进度区域支持轻点输入全书页码，覆盖滑杆、页码和留白；长按打开阅读工具。保留正文长按拖选、拖动进度定位、返回原处及全局角落手势。
  Tap anywhere in the reading progress area to enter a whole-book page number; hold it to open reading tools. Preserve text selection, drag seeking, return to origin and global corner gestures.

## 2026-10-17 · 0.16.0-hw14 全书页码与目录 / Whole-book pages and contents

- 轻点阅读进度条打开数字页码输入，按当前字体、字号和方向逐章计算真实全书页码；计算可取消，跳转失败保留原位置。拖动仍预览位置、松手定位，并可返回原处。
  Tap the reading track for numeric whole-book page entry, calculated chapter by chapter using the current font, size and orientation. Calculation can be cancelled and failed jumps retain the original location. Dragging still previews and seeks on release, with a return point.
- 阅读快捷卡片和页码面板提供目录入口，标出当前章节，点选章节后返回正文。
  Open the contents from reading shortcuts or the page dialog, identify the current chapter and select a chapter to return to its text.

- 移除固件内的示例书籍、漫画、旧阅读演示页面及对应书架入口，保留 TF 图书和历史记录；输入法字典仍保留。
  Remove bundled sample books/comics, legacy reading demos and sample-library entries while preserving TF books, historical records and the input dictionary.
- 翻页时钟可选同时展示随机划线摘录与出处，沿用九条缓存和轮换间隔；低电量时保留提醒并暂停自动刷新。
  Optionally show random highlights and sources alongside the flip clock, reusing the nine-item cache and rotation interval; low power leaves a reminder and pauses automatic refresh.

## 2026-10-17 · 0.16.0-hw13 无线内存配额 / WiFi memory budget

- 降低阅读、字体与屏幕并存时的无线静态缓冲开销，统一日常WiFi与传书的缓冲配置，修正实机内存不足导致无法初始化及扫描。
  Reduce resident WiFi buffers alongside reading, fonts and display, sharing the budget across everyday WiFi and transfer to fix device initialization and scanning failures caused by exhausted internal memory.

## 2026-10-17 · 0.16.0-hw12 拖选与阅读定位 / Drag selection and reading navigation

- 本机图书长按命中字后直接拖动选文，松手显示划线与批注操作，首尾圆形手柄可继续调整；选文与普通滑动翻页分开识别。
  Hold a character and drag to select real book text, release for highlighting/annotation, and adjust circular endpoint handles without triggering ordinary page swipes.
- 阅读进度条支持点按和拖动预览，松手定位并可返回原处；正文与控件适配产品横竖方向。
  Tap or drag the reading progress track to preview and seek on release, with a return-to-previous-position action and product-orientation-aware reading controls.
- 扫描请求等待无线启动及暂忙状态，扫描与连接错误独立显示，修正无扫描结果却提示检查密码的问题。
  Keep scans pending during radio startup/busy states and report scan failures separately from connection errors instead of incorrectly requesting a password.

## 2026-10-17 · 0.16.0-hw11 完整字库与首次配网 / Complete fonts and first WiFi setup

- 默认使用 TF 卡上的官方原版 MiSans 可变字体，保留明确字体偏好；修正可变与复合字形的缺字、裁切和读取恢复，附字体来源、原始许可及 MiSans 归属。
  Default to the original MiSans variable font on TF while retaining explicit font choices. Correct variable/composite glyph rendering and font recovery; include the original license, source and attribution.
- 点击本机图书先显示载入页面，再读取字体、解析及分页；载入前可返回取消。
  Present a loading page before font reads, parsing and pagination; Back can cancel before loading begins.
- WiFi 首次使用不依赖已保存网络；开启后自动扫描，关闭时也能直接添加网络，由连接动作启用 WiFi。
  First WiFi setup requires no saved network. Enabling starts a scan, and manual network entry is available while off; connecting enables WiFi.

- 真实本机图书接入两角扇形快捷菜单、全局返回与阅读位置恢复；长按正文选择、调整范围并保存划线、批注和书签，记录列表按类型分类，真实划线纳入休眠摘录。
  Real local books share corner shortcuts, global Back and reading-position restore. Select and adjust actual text, save highlights/annotations/bookmarks, browse records by type and include real highlights in sleep excerpts.
- 阅读记录扩展到256条，分块双代提交并保留旧记录迁移；失败保留可重试草稿，避免保存成功但草稿清理失败后生成重复笔记。
  Expand records to 256 with chunked two-generation commits and read-only migration. Keep retryable drafts on failure and avoid duplicate notes after failed draft cleanup.

## 2026-10-17 · 0.16.0-hw10 EPUB 兼容 / EPUB compatibility

- 修复部分 EPUB 携带冗余 ZIP 打包标记时无法打开的问题，继续检查头部一致性、解压结果及 CRC，拒绝加密和不支持的压缩格式。
  Accept redundant ZIP packaging flags used by some EPUBs while retaining header consistency, decompression and CRC checks, and rejecting encryption and unsupported compression.

## 2026-10-17 · 0.16.0-hw9 本机书架与返回 / Local library and navigation

- 系统书架加入真实本机 TXT/EPUB 图书，将内置示例单独分组；兼容 TF 根目录和 books 目录，分批读取清单，传输期间暂停扫描。
  Show actual local TXT/EPUB files in the system shelf, with built-in examples in a separate group. Support the TF root and books directory, scan in bounded batches and pause during transfer.
- 点击本机图书直接打开真实阅读引擎；产品页面跳转保留返回来源，普通返回和菜单操作不再误入工程测试页。
  Open selected local books through the actual reading engine. Preserve product navigation origins so ordinary Back/menu actions no longer enter engineering pages.

## 2026-10-17 · 0.16.0-hw8 TF 卡与统一传书 / TF card and file transfer

- 重做 TF 卡检测、挂载、错误提示与格式化；显式清卡重建单个 FAT32 分区，重新挂载后写入、同步并重新打开文件读回验证。普通挂载失败不会清卡。
  Rework card detection, mounting and formatting. Explicit erasure rebuilds one FAT32 partition, then remounts and verifies a synced file through a new read handle. Ordinary mount failures never format media.
- 统一同网、设备热点和 USB 三种传书方式；USB 将 TF 卡作为电脑磁盘，暂停设备读卡和无线传书，电脑弹出后重新挂载校验再恢复。
  Unify LAN, hotspot and USB transfer. USB exports only the TF card, suspends local/wireless access and verifies a remount after host eject before restoring readers.
- 手机网页增加连接与上传超时、取消中止及失败重试，配网期间避免重复操作。
  Add connection/upload timeouts, request cancellation and retries to the mobile page, and prevent overlapping setup operations.

## 2026-10-17 · 0.16.0-hw7 配网与传书 / WiFi and transfer

- 系统WiFi页改为真实扫描、选网、密码输入和连接状态，记忆最多8个网络，支持自动连接开关、详情与确认遗忘；日常联网独立于传书，离开设置不会断开WiFi。
  Replace fixture networks with real scan/selection, password entry and connection state. Remember up to eight networks with per-network auto-join, details and confirmed forgetting; everyday WiFi remains connected outside settings.
- 无线传书使用统一横竖屏页面，提供同网推荐与设备热点两种方式、连接/网页二维码、网址、容量和接收进度；结束并等待文件接收退出后打开真实文件书架，存储不可用可前往检查。
  Add unified portrait/landscape LAN and hotspot transfer with QR codes, URL, capacity and reception progress. Join reception before opening the file library; guide unavailable storage to inspection.
- 手机传书网页显示批量上传结果、等待保存阶段，重连保留选择，只重试失败项；密码沿用手机式英文/符号键盘，默认隐藏，离开清除输入缓存。
  Show per-file batch results and the saving stage on mobile, retain selection across reconnection and retry failures only. Reuse the phone-style English/symbol password keyboard with masking and cleared buffers on exit.


## 2026-10-17 · 0.16.0-hw6 输入与存储 / Input and storage

- 中文26键与9键增加离线词组补全、连续拼音分词和候选翻页；采用错行键盘、更高字母键与按键间隙，抬手确认，跨键滑动取消输入。
  Add offline phrase completion, continuous-pinyin segmentation and candidate paging to Chinese 26-key/T9 input. Use staggered rows, taller letter keys and gaps, commit on release and cancel cross-key movement.
- 设备设置增加“TF 卡与存储”，显示状态与容量，支持重新检测及两次确认后的格式化；普通挂载失败不会清卡，维护期间暂停文件消费者。
  Add TF status/capacity, redetection and formatting after two confirmations under Device settings. Ordinary mount failures never clear a card; maintenance pauses file consumers.
- 扇形快捷菜单收紧层间距，扩大圆心取消区及触摸范围，移除入口后继续自动收缩；修复编辑完成后同次抬手点击到上一页。
  Tighten shortcut arc spacing, enlarge the center cancellation zone and hit targets, retain automatic compaction after removal and prevent release navigation from tapping through into the prior page.

## 2026-10-16 · 0.16.0-hw5 显示方向与操作界面 / Display direction and controls

- 使用设备安装方向对应的横竖屏显示旋转，修复内容相对触控倒转180°；触控和实体键坐标不变。
  Match display rotation to the mounted panel, correcting upside-down content while retaining touch and key coordinates.
- 继续放大按钮和菜单，保留25/600正文；修复键盘与详情操作的底栏遮挡，并让正文大小与分页测量始终一致。
  Enlarge buttons/menus, retain the 25/600 reading default, keep keyboard/detail actions above navigation and match body rendering to pagination.
- 大面积背景按物理行批量填充，快捷面板跳过被完全遮挡页面的绘制；增加串口分段计时用于继续定位等待。
  Batch background fills in native rows, skip pages fully hidden by shortcut overlays and add serial timing to investigate remaining latency.

## 2026-10-16 · 0.16.0-hw4 可读性与方向修正 / Readability and orientation

- 修正实体键朝下时横竖判断颠倒，连续稳定采样后切换方向；保留方向锁定。
  Correct the reversed portrait/landscape detection when the keys face down; require stable samples and retain rotation lock.
- 放大加粗界面文字与边框，正文默认字号增大；兼容已有阅读进度和偏好，修复横屏书库及页码与底部导航的遮挡。
  Enlarge and embolden UI text, borders and the default reading font; retain existing progress/preferences and keep landscape shelves and page numbers clear of bottom navigation.
- 普通黑白操作使用快速刷新，书库封面和漫画保留灰阶；方向切换和定期清屏保持完整刷新。
  Use fast updates for ordinary monochrome interaction, retain grayscale for covers/comics, and keep full updates for rotation and periodic cleanup.

## 2026-10-16 · 0.16.0-hw3 原生系统预览 / Native system preview

- 按网页0.15组织阅读、家庭、设备入口，支持全局返回、镜像扇形快捷菜单、实体翻页键及可跳过的首次引导。
  Follow the web 0.15 Reading/Home/Device structure with global Back, mirrored corner shortcuts, physical page keys and skippable onboarding.
- 内置长文与预转换漫画、封面书库、按原文位置保存的阅读进度、独立书签/笔记/划线、三种真实字体、中文26键/9键和英语/符号输入；手写仅保留墨迹，不提供识别。
  Bundle prose and preconverted comics, covers, source-anchored progress, separate bookmarks/notes/highlights, three real fonts and Chinese 26-key/9-key plus English/symbol input. Handwriting stores ink without recognition.
- 原生家庭面板、房间、实体筛选与手动排序、网络记忆设置及打印监看沿用示例交互；Wi-Fi、HA及MQTT连接均明确标为示例。实际TXT/EPUB导入仍通过独立原生书库，尚无CBZ导入。
  Native home dashboards, rooms, entity filtering/order, saved-network settings and printer monitoring reproduce sample interactions. Wi-Fi, HA and MQTT remain labelled fixtures. Actual TXT/EPUB import still uses the separate native library; CBZ import is unavailable.
- 支持翻页钟动画与横竖屏划线摘录、9条缓存、定时更换、低电量提示和方向锁定；此预览待机保持CPU运行，未实现低功耗深睡。增加四种棋类和循序难度的模拟对手。
  Add animated flip-clock and portrait/landscape excerpts with a nine-item cache, timed rotation, low-battery notice and orientation lock. Standby keeps the CPU running; low-power sleep is not implemented. Add four board games with progressive opponents.
- 系统数据保存在自有目录，初始化失败不格式化或擦除。无损压缩开机/锁屏图以在原4MiB应用分区内加入字体与界面。
  Persist system data in an owned directory without formatting or erasing on initialization failure. Losslessly compress boot/lock assets to fit the fonts and UI within the existing 4MiB app slot.

## 2026-10-16 · 0.15.0-hw2 本地触摸诊断 / Local touch diagnostics

- 系统首页和阅读演示停留期间不再因空闲两分钟自动关机，便于实机演示；电源键和用户原有睡眠设置保留。
  Keep the system home and reading demo awake during idle demonstrations; retain the power key and the user's saved sleep setting.
- 临时记录触摸原始帧、坐标、触点和读数状态，变化日志最多每秒五次，空闲每五秒摘要；不改变触摸协议、解析或控制命令。此版本用于诊断，触摸问题根因仍待确认。
  Temporarily log raw touch frames, coordinates, contacts and read status at up to five change reports per second, with idle summaries every five seconds. Touch protocol, parsing and control commands remain unchanged; the reported touch issue is still under investigation.

## 2026-10-16 · 0.15.0-hw1 本地实机演示 / Local hardware demo

- 新首页内置 24 章长文、8 页预转换漫画与真实 TTF 字形样张，无 TF 卡也可试读；左右滑动或两侧实体键翻页，中键打开字号、字重、强刷与返回工具。
  A new home embeds 24 chapters, eight preconverted comic pages and actual TTF specimens without requiring a card. Swipe or use side keys to turn; the middle key opens size, weight, refresh and return tools.
- Home Assistant 目前仅展示未连接说明；时钟仅为外观预览，尚无新锁屏或翻页动画。CBZ 导入、划线笔记和浏览器预览的完整功能尚未移植。
  Home Assistant shows a disconnected explanation; the clock is appearance-only without new sleep behavior or flip animation. CBZ import, annotations and the complete browser feature set are not ported yet.
- 演示使用已连接设备的双 OTA 分区布局，只更新应用；初始化失败不自动擦除 NVS 或格式化原书库。
  The demo matches the connected device's dual-OTA layout and updates only the application; initialization failures never auto-erase NVS or format existing books.

## 2026-10-09

- 锁屏：未单独选择时默认关机（EN=0，长按约 1 秒开机）。已保存的浅睡或深睡不变。
  Power-key lock defaults to power-off when no mode is stored. A saved light or deep choice stays.
- 空闲 2 分钟自动锁屏，睡眠档仍按已选的浅睡、深睡或关机。传书页停留期间不计时。
  Lock after 2 minutes idle, using the saved light, deep, or power-off mode. The transfer page does not count idle time.

## 2026-09-28 · UNSaWEN

- 阅读：进入书架先确认是否续读；ZIP目录连续读取、字宽测量不生成位图，当前章先排起始/续读页，其余分页分批补齐。
  Confirm resume before opening the last book; read ZIP directories sequentially, measure advances without rasterizing, and paginate the initial/resume pages before completing the chapter incrementally.
- EPUB插图：默认占位，点击后单独预览本地JPEG/PNG，返回保持正文位置；标题前的章首图片及同资源单独标为标题图，其他重复图片提示本次已读章节中最早出现的位置，开章不再反复解码重复图片。
  Show image placeholders and load local JPEG/PNG previews only on tap, returning to the same text position; label opening title images and matching resources separately, retaining earliest-visited hints for other repeated illustrations and avoid decoding images on chapter opening.
- 手势：滑动翻页阈值由120缩短到64像素，保留抬手提交、轻点容差和短拖取消。
  Reduce swipe distance from 120 to 64 pixels while retaining release-to-commit, tap tolerance and short-drag cancellation.
- EPUB：ZIP 条目、资源项及章节上限提高到 32768，使用固定窗口扫描ZIP目录和紧凑索引查找，标题按实际长度保存，OPF/导航上限4 MiB；打开和排版显示等待提示，失败显示具体原因。
  Raise EPUB ZIP, manifest and spine limits to 32768, scan ZIP directories through a fixed window, use compact indexed lookup and actual-length titles, and allow 4 MiB OPF/navigation metadata; show parsing/layout wait hints and failure reasons.
- WiFi：TF 卡支持上传完整 TTF 字体，32 MiB 上限、同名确认、提交前结构校验及中断恢复；传书期间暂停卡上字体读取，离页恢复。
  Upload complete TTF fonts to TF over WiFi with a 32 MiB limit, confirmed replacement, pre-commit structural checks and interruption recovery; suspend card font reads during transfer and resume on exit.
- 部署：随仓库提供未修改的 ChillDuanSans VF v1.30、原 OFL-1.1 许可和来源校验；内置子集命名为 Read Pico UI，保留原版权与许可。
  Bundle unmodified ChillDuanSans VF v1.30, original OFL-1.1 terms and source verification; name the embedded subset Read Pico UI and retain attribution and licensing.

## 2026-09-27 · UNSaWEN

- EPUB 图片位置增加“[图片]”占位，避免纯图片章节显示为空白；暂不解码图片。
  Show “[图片]” placeholders at EPUB image positions so image-only chapters are visible; image decoding is not yet supported.

- 修复长篇 EPUB 因条目或章节超过 512 而无法打开的问题；上限提高到 2048，单资源解压上限仍为 2 MiB。
  Fix long EPUBs failing to open above 512 entries or chapters; raise the limit to 2048 while retaining the 2 MiB uncompressed resource limit.

- 图书阅读：支持 TF 卡或内置存储的 UTF-8/GBK TXT 与纯文本 EPUB，目录、逐书续读、字号、手势翻页及默认关闭的实验晃动翻页。
  Read UTF-8/GBK TXT and text-only EPUB from TF or internal storage, with a TOC, per-book resume, font sizes, gestures and optional experimental shake-to-turn.
- 图书管理：增加拼音/首字母/英文搜索、来源筛选、名称/最近阅读排序、详情，以及分别确认的单本/批量删除和清进度。
  Add pinyin/initials/English search, source filters, name/recent sorting, details and separately confirmed single/batch deletion or progress reset.
- WiFi 传书：支持设备热点和已有网络、触屏/网页配网、连接与网址二维码；网页管理当前存储，支持确认替换、取消和重试；停止后返回进入前的位置。
  Transfer over the device hotspot or an existing network, with touchscreen/web provisioning and connection/URL QR codes. Manage current storage in the browser, confirm replacements, cancel or retry uploads, and return to the entry location on stop.
- 可靠性：处理保存失败与部分成功重试、中文文件名和覆盖中断恢复；修复显示欠载后相位队列残留导致的死锁；检测 TF 失效后停止相关消费者并回退字体，插回后需显式重挂。
  Handle save failures, partial-operation retries, Chinese filenames and interrupted replacements. Fix a display queue deadlock after underrun; stop affected consumers and fall back fonts on TF loss, requiring explicit remount after reinsertion.

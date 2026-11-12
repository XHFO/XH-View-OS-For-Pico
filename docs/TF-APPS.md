# TF 卡应用包规范（PGA1 / ABI v1）

二态开关统一使用 `pico_app_switch.h` 的圆角轨道与左右滑块：左关右开，用灰阶和位置同时表达状态。系统设置使用 `ps_toggle`；独立应用复用 `pico_switch_draw`，不增加宿主 ABI。Home Link 开关捕获自身手势，松手只提交一次，等待 HA 确认后显示新状态；不连续驱动屏幕动画。多选目录仍使用复选框，操作按钮与多项样式选择保留各自语义。

家庭连接先选择房间范围，再以每批24项浏览完整目录并选择最多96个控制卡片。房间内实体总量不受64项限制；跨页保留勾选，第97项明确提示。日常只从HA投影选中实体的必要字段。取消或保存失败保留原选择；旧隐藏与排序记录迁移。范围与选择使用独立版本化记录，保留v3凭据布局。打印机配置不参与家庭双机共享。

Select rooms, then page the complete catalog in batches of 24 and choose up to 96 cards. Room totals have no 64-item gate; selections survive paging, and the 97th item is rejected explicitly. Daily requests project selected fields only. Cancel/failure retains active selections, legacy visibility/order migrates, and printer settings are excluded from home sharing.

打印机「添加 → 通过 Home Assistant 查找」列出未添加的打印状态实体，选择后自动推导实体前缀，再由用户确认名称并保存。只匹配 `_print_status` 或 `_printing_state`，不会将 SD 卡状态误识别为打印状态。手动添加作为高级入口保留；本机最多3台，发现列表最多8台。Bambu MQTT/云与 Formlabs 直接连接尚未迁入，既有凭据仍保留。

Printer setup selects a source, discovers complete `_print_status`/`_printing_state` entities, derives their prefixes and asks for confirmation before saving. Manual prefixes remain available. There are three saved slots and at most eight discovery candidates; direct Bambu/Formlabs protocols remain unavailable with existing credentials retained.

家庭与打印机分别由 `tools/home-link.pga`（kind 130）和 `tools/printer-link.pga`（kind 131）提供。家庭连接同步 HA 支持的实体，可保存本机显示选择与排序；隐藏不删除 HA 实体。打印机提供三槽位的 HA 实体前缀添加、改名与移除，不依赖家庭连接包安装。两者复用宿主 HA 服务、凭据与持久化事务，手机填写按入口分别保存。首次使用 HA 需先在设置的家庭连接页面配置服务。旧 Bambu LAN/云端及 Formlabs 直连配置保留；薄宿主目前尚未迁入直连协议，应用明确提示不可用，不伪造已连接状态。

Home and Printers use separate `tools/home-link.pga` (kind 130) and `tools/printer-link.pga` (kind 131) packages. Home supports local entity visibility and ordering without deleting HA entities. Printers supports three HA entity-prefix slots with rename/removal, independently of installing Home Link. Both share host HA credentials and persistence transactions; phone forms save separately by entry. Configure HA once through Home settings. Existing Bambu LAN/cloud and Formlabs direct credentials remain preserved; direct protocols are not yet available in the thin host and are explicitly marked unavailable.

本规范定义小纸 Pico 的可安装应用。六个首批游戏为五子棋、围棋、象棋、国际象棋、2048 和贪吃蛇；工具应用另有画板笔记。**游戏规则、AI、游戏页面以及画板编辑实现均编译进独立应用包，不静态链接进固件。** 固件仅包含启动器、包校验、加载器及公共绘图、输入、存档与任务接口。

The six first-party games are independent executable packages. Rules, AI and game UI live in the packages; firmware contains only the launcher, validator, loader and host services.

## 1. 用户安装与移除

- 安装目录固定为 `/sdcard/apps/games/<id>.pga`。可通过 USB 磁盘复制到此目录；安全弹出并结束设备 USB 传输后，在小游戏页刷新。
- 工具应用使用 `/sdcard/apps/tools/<id>.pga`；画板笔记为 `drawing-notes.pga`。刷新应用或重新开机后，在“设置 → 首页导航 → 添加入口”选择“画板笔记”，不用进入小游戏。隐藏首页入口不删除应用；删除包保留原稿，重装同 ID 后可继续。游戏与工具目录不允许同 ID 同时存在。
- 一包一游戏；有效包的文件名必须与内部 ID 一致。例如 `chess.pga` 的 ID 为 `chess`。四种棋各有本游戏的三档难度、自动进阶、开始新局、继续与规则页，不再进入另一层六游戏大厅。
- 小游戏页只显示已安装且支持当前平台的包。进入“设置 → 应用与服务 → 应用集”，长按 TF 应用卡片并确认可移除应用，**保留独立存档、最高分及难度成绩**。重新安装同 ID 包可继续。
- 无卡、未挂载、包损坏、ABI 不兼容或缺少当前平台代码时，不回退到固件内置游戏。刷新/打开失败会提示原因；不兼容文件计入不可用数量。
- 卡可容纳更多包，首版目录最多显示 24 个应用，每页 6 个；上下滑或页码按钮翻页，左右滑仍切换书架、家庭与小游戏主页面。
- 电脑原生预览另有“导入应用包”，使用同一 C 校验及安装事务，兼容原六款游戏与画板等工具。网页侧栏提供包下载、添加及工具移除；工具首页入口仍由用户在“首页导航”中选择。其 TF 是本机会话目录，和实体卡相互独立。新的私有 TF 可安装明确标记的开发样本包；重启既有会话不会自动补回用户移除的包。

USB exports the whole card. Copy packages while the device has relinquished local ownership, safely eject, end USB mode and rescan. Uninstall removes code only, retaining saves.

## 2. 包、目标与版本

`.pga` 是单文件二进制容器，不是 ZIP。它不包含文件系统路径，不需要解压目录树。所有整数均为小端无符号 32 位。文件必须恰好等于声明长度，不允许尾随数据；上限 4 MiB。

| 偏移 | 长度 | 字段 | 约束 |
| --- | --- | --- | --- |
| 0 | 8 | magic | ASCII `PICOAPP1` |
| 8 | 4 | format | `1` |
| 12 | 4 | header_bytes | `512` |
| 16 | 4 | package_bytes | 整个文件精确长度，512 B–4 MiB |
| 20 | 4 | abi | `1`，对应 `pico_app_sdk.h` |
| 24 | 4 | app_version | `major*10000 + minor*100 + patch`；六游戏 `10001`（1.0.1），画板 `10002`（1.0.2） |
| 28 | 4 | kind | 游戏0–5；工具128–255，画板128；类型不能替代稳定ID |
| 32 | 32 | id | 1–31 个小写 ASCII 字母、数字或 `-`；不以 `-` 开始/结束；NUL 结尾 |
| 64 | 64 | title | NUL 结尾 UTF-8，最多 63 字节 |
| 128 | 96 | description | NUL 结尾 UTF-8，最多 95 字节 |
| 224 | 4 | flags | v1 保留为 0 |
| 228 | 4 | payload_count | 1–4 |
| 232 | 4 | header_crc32 | 将本字段置零后，对完整 512 字节头算 CRC32/IEEE |
| 236 | 20 | reserved | 打包器写 0 |
| 256 | 128 | payloads | 最多 4 个、各 32 字节，见下表 |
| 384 | 64 | reserved | 打包器写 0 |
| 448 | 32 | source_sha256 | 同源应用源码、兼容层及 SDK 输入的组合摘要 |
| 480 | 32 | reserved | 打包器写 0 |

所有定长字符串必须是合法 UTF-8（Unicode 标量编码），首个 NUL 后的填充字节为 0。v1 接收端拒绝非零 flags、保留区、记录保留字及未使用记录。

每个 payload 记录如下，未使用记录填零：

| 记录内偏移 | 长度 | 字段 |
| --- | --- | --- |
| 0 | 16 | NUL 结尾目标名 |
| 16 | 4 | 文件内绝对偏移 |
| 20 | 4 | 代码长度，1 B–2 MiB |
| 24 | 4 | 对代码字节算 CRC32/IEEE |
| 28 | 4 | 保留，0 |

payload 紧接 512 字节头，按目录顺序连续排列，无间隙、重叠或重复目标。固件选择 `esp32s3-xtensa`；电脑预览选择当前主机，如 `macos-arm64`、`macos-x86_64`、`linux-arm64`、`linux-x86_64`。设备目标为 ESP32-S3 Xtensa ELF；电脑目标为对应系统动态库，两者由同一应用源码和 SDK 构建。

生产包应包含 `esp32s3-xtensa`。开发工具允许 `--native-only` 生成电脑专用候选包，这些包不能在设备运行。包含两种目标的同一个包可用于电脑预览和设备；主机机器码不会在设备执行。CRC 用于检测损坏，不提供发行者身份认证。

## 3. 运行时 ABI

唯一权威头文件是 [`pico_app_sdk.h`](../components/read_pico_apps/include/pico_app_sdk.h)。当前 ABI 为 1。结构必须报告准确的 `abi` 和 `bytes`；新增不兼容布局须提升 ABI。入口为 `pico_app_query`，返回 `pico_app_exports_t`；包头与导出表的 ID、kind、version 必须一致。

| 应用回调 | 契约 |
| --- | --- |
| `start(host)` | 一次实例初始化，恢复本应用存档；返回后才接收绘图和输入 |
| `render(framebuffer)` | 纯绘图，不读 TF、不存档、不创建任务，不推屏 |
| `event/key/action` | 固定事件、实体键与快捷动作；可请求重绘、导航或提交状态 |
| `tick` | 有界前台推进，接收已完成 AI 结果；不阻塞等待输入 |
| `status` | 休闲、蛇、暂停、忙碌和未保存等能力/状态位 |
| `save` | 提交当前应用状态，成功才报告 true |
| `stop` | 暂停实时逻辑、取消 AI 并等待任务停止，返回前配对回收任务；保留静态局面直到卸载 |
| `ink(framebuffer, rects)`（可选尾部） | 绘制待显示的新笔迹，不访问文件、不推屏；返回-1要求完整帧、0无变化，或1–4个360×640/640×360设计坐标损伤区域 |

ABI v1 采用可选尾部扩展。旧六游戏的导出表仍按 `PICO_APP_EXPORTS_V1_BYTES` 最小前缀读取；宿主检查 `bytes` 后才可访问 `ink`，因此无需强制重装旧游戏包。画板编辑时置 `PGA_STATUS_INK`；列表或确认弹窗不置此位。整帧绘制建立快照，撤销、清空、旋转、菜单覆盖恢复和丢失显示参考时重建；移动手指只提交新增笔迹，连续DU由固件统一管理。

宿主仅提供：360×640/640×360 逻辑绘图、固定宽度事件、重绘和提示、导航请求、命名空间存档、单调时钟、随机数、受管理工作任务及少量 C 字符串/格式化接口。不提供任意文件读写、网络、板级、显示时序、电源校准和固件内部结构访问。

应用是原生机器码，**不是安全沙箱**；发布端应只分发审核及测试过的可信包。CRC/摘要不能把任意原生程序变成受限程序。

## 4. 生命周期与资源上限

一次只载入一个应用。常规目录列举读取有界包头，不执行代码；恢复 `.pga.bak` 时额外流式校验完整包。打开时完整校验包和选中代码，再将代码载入内存。游戏运行不依赖持续打开的 TF 文件。

宿主完成一次模块调用后才执行返回/卸载请求，不允许模块正在 `event()` 中时因导航而卸载自身。普通离页、游戏切换、USB 接管、存储维护和包更新共用停止流程：

1. 停止输入及新 AI 提交。
2. 递增取消 generation，等待合作取消完成，配对删除 worker 和其外部栈，丢弃迟到结果；工作线程只做搜索，不绘图或读写 TF。
3. 任务已停止后保存局面，避免保存与 AI 同时读写状态。
4. 保存成功且没有工作线程或引用后才卸载代码。

每应用最多一项工作任务，棋类使用 20,480 B PSRAM 栈。原有三档 AI 搜索预算与规则保持。贪吃蛇离页暂停，不在后台自行推进。若保存失败，宿主保留当前模块和未保存局面于本次会话内，停止后台推进，阻止替换/移除它，直到保存成功或用户明确放弃本次未保存进度。同一介质 generation 仍可重新打开原游戏，继续本次会话并重试保存。保存失败时留在原应用，统一显示“继续保留／重试保存／放弃未存内容”；外点或返回只取消浮层，不将接触传给画板或游戏。“继续保留”在原介质仍可用时恢复编辑；“重试保存”成功后完成原先的离页或换应用请求；只有“放弃未存内容”会清除 RAM 中本次未保存内容，卡内已有存档不删除。失卡后可通过页底“未保存内容”或返回键再次处理。断电仍不能保证未保存状态。

TF 丢失时停止模块任务并停止存档，不继续访问卡；重新插入即使是同一张卡也属于新的介质 generation，旧会话不得自动把 RAM 局面写入它。可保留待处理状态，或确认放弃未保存进度后重新加载卡内存档。USB 独占、Wi-Fi 传输及存储维护期间不打开游戏或读写应用存档；取得介质前先保存、停止并卸载模块。设备加载器固定依赖 Espressif ELF loader 1.3.5，ESP32-S3 使用 PSRAM 执行映射与必要缓存同步，不改变板级波形、VCOM 或 Flash 时序。

## 5. 存档与兼容迁移

安装包和数据分离。宿主按稳定 ID 使用独立目录 `/sdcard/apps/data/<id>/<key>.dat`，例如 `/sdcard/apps/data/chess/games.dat`；这避免扁平键拼接的长度限制与歧义。`key` 同 ID 一样是 1–31 个小写 ASCII 字母、数字或 `-`，且不以 `-` 开始/结束。应用只能操作自己的键，每个序列化数据块上限 64 KiB；宿主生成路径，模块不提供文件路径。

存档文件包含 12 字节小端头：`0x31535058`、数据字节数、CRC32/IEEE，随后为精确长度的数据。读主 `.dat` 失败时尝试同名 `.bak`。写入先完成 `.tmp`，刷新、同步并读回校验；只有已通过完整校验的旧主档才可以轮换为 `.bak`，坏主档不得覆盖仍有效的备份，然后提升新主档。提交失败保留可恢复副本并向模块报告失败；断电后不会把未完成的 `.tmp` 当作已保存状态。删除 `.pga` 保留整个应用数据目录。

旧四棋 `games` 和双休闲 `arcade1` 保持只读迁移：四棋难度及成绩各归对应应用，唯一当前棋局仅归旧存档中的棋种；2048 与蛇分别恢复自己的局面。新档缺失时读取旧档，再写独立档；失败保留旧档，不能清空旧文件。升级应保留稳定 ID，且模块负责自己的数据版本兼容，不以应用显示名称生成存档键。

## 6. 安装与更新事务

管理器只接收 `/sdcard/apps/.staging/<安全文件名>.tmp` 中已完整接收的文件。顺序为：长度/头/ABI/所有 payload CRC 校验 → 当前平台检查 → 同 ID 替换确认 → 保存并停止正在运行的相同应用 → 恢复可用的旧包备份 → 仅将有效原包轮换为 `.pga.bak` → 改名新包 → 完整读回校验 → 删除备份 → 重扫。

提交失败尝试恢复原包；若介质故障导致改名也失败，保留的完整备份可供下次扫描恢复，界面报告失败。目录扫描忽略 `.tmp`。若断电发生在原包备份后，下次扫描在主包缺失、损坏或内部 ID 与文件名不一致，而备份完整有效时恢复备份；有效主包会清理遗留备份。备份保留的是应用包，不是固件。移除应用也先保存和停止任务，再清除包的备份与主文件，防止下次扫描复活旧包；失败不声称已移除，独立存档不删除。手动 USB 复制时应先完整复制再改为最终 `.pga` 文件名，不在设备本地挂载时同时写卡。

## 7. 构建与网站分发

```sh
# 设备 ELF，由固定 Xtensa 工具链构建（具体参数见 --help）
python3 tools/build_game_elf.py --output build/game-elf

# 同源本机库＋设备 ELF → 六个可在两端运行的独立包和网站目录
python3 tools/build_game_packages.py --elf-dir build/game-elf --output build/game-packages

# 只做电脑候选，不能复制到设备运行
python3 tools/build_game_packages.py --native-only --output build/game-packages-native

# 独立工具；默认仅电脑预览，--device增加同源ESP32-S3载荷
python3 tools/build_tool_packages.py --device --output build/tool-packages
```

打包目录包含六个 `.pga`、构建输入摘要、原生中间产物和 `catalog.json`。向用户分发 `.pga` 与目录清单即可，不需要把中间 `.elf`/`.dylib` 文件散放到 TF。

网站目录每项需包含稳定 `id`、标题、说明、版本、ABI、字节数、目标平台、SHA-256 和下载 URL；只发布与实际包一致的数据。网站未来可按 ID 对比用户安装清单，提供添加/更新/移除操作，设备侧仍走上述同一事务。**本改动提供格式、构建工具和本地包管理，不代表已部署官网应用商店或云端远程管理。**

## 画板笔记的数据边界

画板提供十二个固定原稿入口、三档画笔、橡皮、按笔画撤销、清空确认和保存。新纸张按竖屏可写区的比例建立；横屏工具移到左侧，纸面宽高随方向互换，整张纸等比旋转90度，触摸对应逆变换。不裁切或依赖平移查看原稿，旋转本身不落盘，也不改写笔迹坐标。旧矩形、正方形原稿保留原比例和存档字节，完整居中显示；有留白时不拉伸笔迹。两侧实体键改为撤销、保存，底部角手势仍保留系统快捷菜单的触发范围。

纸面提供稀疏小网点，橡皮恢复同位置的背景网点，连续绘画的局部结果与完整重绘一致。网点使用可通过当前墨水屏二值跟手模式的深色单像素，以稀疏排布降低视觉干扰，不切换显示波形或校准设置。

抬手并空闲约1.8秒后自动保存，触摸、手势菜单和其它独占交互期间延后文件写入；也可手动保存。保存失败保留本次内存原稿并提示，退出、换应用和USB交接均不能隐藏或卸载这份未保存内容；可继续保留、重试保存或明确放弃。不能把断电后的未保存内容称为已恢复。

New paper matches the portrait writing area. A landscape left toolbar allows the whole paper to rotate 90 degrees at the same aspect ratio, with inverse touch mapping. Stored points remain unchanged; legacy rectangular and square pages fit completely with any necessary margins. No pan controls are required; the outer hardware keys undo and save. Global corner gestures retain their trigger areas. Sparse dots remain visible through binary ink refresh, and erasing restores the same dotted background. Failed saves remain visible and retryable; outside dismissal never discards RAM or activates the underlying canvas.

每张原稿最多8192个向量采样点，当前序列化块49168字节，存在 `apps/data/drawing-notes/board01.dat` 至 `board12.dat`，目录元数据为 `index.dat`。原稿先提交，再更新目录；两者沿用宿主CRC、临时文件读回、双代备份事务。目录中十二个位置始终可打开，因此目录更新中断不会使已保存的原稿失去入口。已标记存在但读取失败的原稿不会被当空白覆盖。此应用保留可编辑向量，不承诺图片导出或文字识别；画板与书籍文字输入互相独立。

## 8. 必须验证的行为

- 包损坏、截断、超限、偏移溢出、目标重复、ABI 不符和非规范 ID 拒绝执行。
- 六游戏由真实卡包加载；移走包后固件中无可启动的静态替代游戏。
- 四棋规则、三难度 AI、重放、悔棋、升变、围棋终局与旧档迁移保持。
- 2048 和蛇双方向布局、控制、最高分、暂停/重开和重启恢复保持。
- AI 运行中离页、更新、移除、卡丢失及 USB 接管不执行已卸载代码，不泄漏工作任务。
- 删除/重装保留成绩；更新失败、接收中断及重启可恢复旧包，残留备份不得使移除的包复活。
- 主存档损坏时可读有效备份；坏主档不得覆盖好备份。保存失败后保留 RAM，显式放弃后不写入另一张卡。
- 电脑原生验证与设备 ELF 构建分别记录。机器码加载、实际 AI 耗时/堆栈及显示需要真机验收；不以电脑测试代替。

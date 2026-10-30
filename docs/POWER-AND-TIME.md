# 电源、锁屏与时间 / Power, lock and time

点按电源键锁屏并进入低功耗等待，再次点按电源键恢复。按下再松开只计一次，不需要双击或长按，也没有唤醒后的两秒按键忽略。长按电源键（由 PMU 已有 KEY_LONG 事件判定，默认约 1.5 秒）打开关机、重启、锁屏、取消菜单；锁屏中长按会先恢复再打开菜单。也可在“系统设置 → 电源与锁屏”选择状态。阅读正文和系统页面共用同一个入口；锁定期间不继续分发翻页、快捷菜单或游戏输入。

| 状态 | 行为 | 恢复方式 |
| --- | --- | --- |
| 锁屏 | 保存内容、暂停游戏与联网任务，保留当前应用和画面，进入可用的低功耗等待 | 电源键单次点按，或已开启的屏幕双击 |
| 休眠 | 同样保留应用，屏幕触摸不解除休眠 | 电源键 |
| 重启 | 保存内容后重启系统；操作前确认 | 等待重新启动 |
| 关机 | 保存内容后先强刷用户选择的锁屏画面，底部注明“已关机 · 长按电源键开机”，再通过 PMU 关机；刷新失败会取消关机 | 长按电源键开机 |

One complete power-key click locks and a second click restores the retained application, including reading position. Optional complete screen double taps unlock only the lock state; the switch defaults on. A PMU long-key event opens the four-action power menu, including from a locked state. Sleep and shutdown ignore touch. Shutdown first fully presents the selected face and the power-on notice; a failed refresh cancels shutdown. Failed persistence or an active transfer prevents entry instead of discarding work.

锁屏不插入“正在锁屏”提示或进度动画。联网任务先请求暂停，与内容保存并行，进入低功耗前仍检查全部保存和后台收束成功；失败保留内容并恢复联网。显式文件提交仍仅在当前主文件的格式、长度、CRC及每个字节均与待存内容一致时免写，不依赖时间戳或备份文件。家庭、打印与摘录设置在本次存储会话已成功提交、内容未变且存储仍可用时可略过重复提交；从主档或备份加载不算成功提交。USB回收、媒体变更、格式化和重新绑定会清除此状态，失败提交不会清除待保存内容。串口阶段耗时分别记录捕获、保存、网络收束、准备、推屏及硬件休眠准备，不以网页模拟耗时代表实机速度。

Locking adds no transitional status or progress animation. Network quiescence overlaps content saving, but low power still requires successful persistence and stopped workers; failure retains content and restores networking. An explicit file commit skips its write only when the current primary's format, length, CRC and every byte match the intended content, never from timestamps or backups. Unchanged home, printer and excerpt settings may skip a repeated commit only after a successful commit in the same available storage session. Loading either primary or backup does not establish that state. USB reclaim, media changes, formatting and rebinding invalidate it; failed commits remain pending. Serial timings separate capture, save, network, preparation, presentation and hardware sleep setup; browser timing is not a hardware speed measurement.

“系统设置 → 电源与锁屏 → 锁屏外观”提供六种锁屏画面：翻页时钟、随机划线摘录、书库封面、当前页面、自定义图片、灵犀画板。书库封面可在同一外观页选择全屏单封面、双封面或六本拼贴；无有效封面时显示书名占位。保留当前页会叠加锁屏提示。除保留当前页外，进入时完整清屏。保留当前页仅在已显示帧与保留帧一致、没有未完成驱动或浮层恢复时局刷底部锁屏标识；否则仍完整清屏。恢复原页面后才继续接收操作。

The single Power and Lock settings entry links to Lock Appearance. Only options relevant to the selected face are shown: flip clock, random highlights, library covers, the retained page, custom images and Lingxi Board. Covers offer single, double and six-book layouts. Restoration uses a full refresh. Entry fully cleans other appearances; the retained-page style updates only its lock notice when the retained frame matches the display baseline and no unfinished drive or overlay recovery remains, otherwise fully cleaning as well.

时钟和摘录使用既有定时刷新设置。摘录缓存九条，自动随机补充，不要求逐条筛选；再次锁屏保留当前有效摘录，首次没有有效内容才填充，实际轮换间隔到期才推进，不因每次锁屏增加一次保存。低电量暂停时不消费队列，最后画面仍注明低电量。锁屏沿用进入前方向。“双击屏幕唤醒”默认开启，只对锁屏有效。开启时保留触摸控制器 NORMAL 报点模式，ESP32 进入可用的 light sleep，由 TP INT 唤醒并判断两次完整短触；无触摸时不持续读取触控 I2C，单击、长按、拖动、多指和触发锁屏的那次接触不解锁。关闭开关或进入休眠/关机时将触控送入 DEEPSLEEP，恢复后复位触摸。NORMAL 的触控待机耗电高于 DEEPSLEEP；不使用未经该模组确认的 standby 手势命令。蓝牙服务仍运行时，会限制轻睡眠并显示提示；关闭后待真实后台线程与协议栈停止，锁屏会重新启用可用的轻睡眠并移除该提示。GPIO唤醒即使在CPU恢复时INT脉冲已回高，也会保留一次触点读取。这里不承诺未经测量的功耗或续航时长。

Highlights retain the nine-entry random queue and the low-battery final notice. Re-entering lock retains the valid current excerpt; an empty queue is filled on demand, and only a due rotation advances it. Lock entry alone does not add a queue commit, and low-battery pause does not consume pending excerpts. Optional double tap keeps the supported touch NORMAL mode with TP INT wake and CPU light sleep. Idle touch I2C is not polled; two complete short contacts are required. NORMAL uses more touch standby power than DEEPSLEEP. With the switch off, or during sleep/shutdown, touch enters DEEPSLEEP. No unknown standby command is used. Bluetooth activity can prevent light sleep. Host previews cannot establish current consumption or battery life.

设置首页的“保持唤醒”默认开启，供开发测试使用。它阻止自动空闲和充电横屏停靠锁屏；电源键和手动锁屏仍可使用。关闭此开关会恢复自动策略。电源与锁屏页面可选择自动锁屏时间：关闭、1、3、5、10、30 分钟，默认 3 分钟；选择会持久保存。设为关闭也暂停自动停靠锁屏。保持唤醒开启时会在时间选项旁注明“保持唤醒已开启，自动锁屏暂停”，不会悄悄关闭开发开关。

Keep-awake defaults on for development. It blocks automatic idle/dock entry, while manual power actions remain available. Persisted idle choices are off, 1, 3, 5, 10 and 30 minutes (default 3); off also disables automatic dock entry. The power page explicitly discloses the keep-awake override without changing it.

在“系统设置 → 日期与时间”可以离线设置日期、时分和时区。保存时验证日期、换算 UTC、写入 PMU RTC 并读回确认；成功后才更新系统与界面时间。启动和硬件轮询会将有效、已同步且本次读取成功的 RTC 桥接到系统时钟，避免界面时钟正常而安全连接仍使用错误时间。

Date/time settings validate the calendar, convert the chosen timezone to UTC, write and verify the PMU RTC, then update system and UI clocks. Hardware polling bridges a valid synchronized RTC only after a fresh successful read, so secure connections use the same clock as the interface.

连接 Wi-Fi 后自动后台授时，成功后每小时更新。单次等待 30 秒，失败后间隔 60 秒轮换服务器重试；主循环写回 RTC 并核对后才更新系统时钟，暂时无法提交时保留样本。手动保存会取消在途校时并将下次尝试延后一小时。断网依靠 RTC 继续走时。电脑预览不启动真实网络授时，仅修改该会话的模拟 RTC，不改变电脑时间。

Wi-Fi connectivity starts background time synchronization, repeated hourly after success. A request times out after 30 seconds; retries rotate servers after 60 seconds. The main loop commits and verifies the RTC before updating system time, retaining samples while a commit is unavailable. Manual saving cancels pending synchronization and defers the next attempt for an hour. Offline operation retains RTC time. The preview never starts real NTP or changes the computer clock; only its session RTC is simulated.


自定义图片锁屏与在线信息画布分开存放。设备与多屏灵犀均可选择第五种「自定义图片」，上传只保存，不立即锁屏；详细接口和TF路径见 [电脑管理协议](DESKTOP-MANAGEMENT.md#自定义图片锁屏--custom-image-lock-screen)。

Custom image locks are separate from online information canvases. Device and desktop may select the fifth appearance; upload only saves the image, without locking immediately. See the [desktop management protocol](DESKTOP-MANAGEMENT.md#自定义图片锁屏--custom-image-lock-screen) for API and TF paths.


选择「灵犀画板」后，PMU 确认正在充电或已充满仍连接外接电源时，锁屏显示最新已保存画板，并保留现有局域网与已配对管理服务接收更新。墨水屏和加速度计仍按统一锁屏路径停用，CPU 不进入会中断网络的手动浅睡；这属于外接电源下的联网显示状态，不宣称低功耗续航。电源键单击、长按菜单和可选双击屏幕唤醒沿用统一逻辑，恢复原阅读或画稿页面。未接电、供电状态未知、无有效画板、深度休眠或关机时，回退保留当前页；不改变用户选定样式。

With Lingxi Board selected, PMU-confirmed charging or full-but-externally-powered operation displays the latest saved board while retaining the existing LAN and paired management window. The common lock path still parks the panel and accelerometer; the CPU does not enter manual light sleep that would interrupt networking. This externally powered network-display mode makes no low-power battery-life claim. Existing power clicks, long-menu actions and optional screen double taps restore the retained reading/drawing page. Battery, unknown power, unavailable boards, deep sleep and shutdown fall back to the retained page without changing the preference.

阅读或画画期间，新画板只保存到 TF 并由空闲时的主循环采纳，不切页，不显示 USB 弹窗，不导出 TF。当前画板页或联网画板锁屏可见时才刷新新内容；没有新版本不重复刷新。拔掉电源后停止采纳更新，异步收束并等待网络任务结束，保留最后画面，然后重新启用既有轻睡眠与唤醒路径。管理仍遵守原十五分钟配对到期规则，不因锁屏延长密钥或绕过鉴权；预览只验证状态与像素，不代表实机功耗或响应耗时。

While reading or drawing, new boards are saved on TF and adopted when idle without navigation, a USB dialog or exporting TF. Only a visible board page or network board lock paints new content; unchanged revisions do not repaint. Unplugging stops frame adoption, joins asynchronous network shutdown, retains the last image and rearms the existing light-sleep/wake path. The original fifteen-minute pairing expiry remains enforced; locking never extends keys or bypasses authentication. Preview checks establish state/pixel behavior rather than physical consumption or response time.

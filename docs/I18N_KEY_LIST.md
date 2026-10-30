# Interface language keys / 界面语言键

Generated from `main/system/pico_i18n_table.inc`; the firmware stores both languages in a lossless catalog.
从同一份文案表生成；固件保留两种语言，以无损压缩节省 APP 空间。

```tsv
key	zh-Hans	en
common.cancel	取消	Cancel
common.done	完成	Done
common.confirm	确认	Confirm
common.back	返回	Back
common.prev	上一页	Prev
common.next	下一页	Next
settings.locale.chip	中文	EN
settings.locale.saved	界面语言已切换	Language updated
settings.locale.save_failed	界面语言未能保存	Couldn't save the language
nav.books	书架	Books
nav.home	家庭	Home
nav.games	小游戏	Games
nav.printers	打印机	Printers
nav.apps	应用集	Apps
settings.nav.title	首页导航	Home nav
settings.nav.hint	选择入口后调整；左右滑动按此顺序切换	Pick an item, then reorder. Swipes follow this order.
settings.nav.hint_settings	设置按钮始终保留，完成后才会保存	Settings stays. Changes save when you tap Done.
settings.nav.add_title	添加首页入口	Add home item
settings.nav.replace_title	替换首页入口	Replace home item
settings.nav.shown	已显示	Shown
settings.nav.missing	未安装应用	Not installed
settings.nav.up	上移	Up
settings.nav.down	下移	Down
settings.nav.replace	替换	Replace
settings.nav.hide	隐藏	Hide
settings.nav.add	添加入口	Add
settings.nav.reset	恢复默认	Defaults
settings.nav.keep_one	至少保留一个首页入口	Keep at least one home item
settings.nav.keep_usable	至少保留一个可用入口	Keep one usable item
settings.nav.already	该入口已在首页，可直接调整顺序	Already on Home. Reorder it there.
settings.nav.full	首页最多保留五个入口，请先隐藏或替换	Home holds five items. Hide or replace one.
settings.nav.none_left	所有可用入口都已显示	Every item is already on Home
settings.nav.save_failed	未能保存，调整内容已保留，请重试	Couldn't save. Your edits are kept.
launcher.title	应用集	Apps
launcher.refresh	刷新	Refresh
launcher.summary	系统与工具 %u · TF 游戏 %u	System %u · TF games %u
launcher.settings	系统设置	Settings
launcher.source.settings	设置	Settings
launcher.source.system	系统应用	System
launcher.source.tf_app	TF 应用	TF app
launcher.source.tf_game	TF 游戏	TF game
settings.power.title	电源与锁屏	Power & lock
settings.power.menu	电源菜单	Power menu
settings.power.off	关机	Power off
settings.power.restart	重启	Restart
settings.power.lock	锁屏	Lock
settings.power.lock_now	立即锁屏	Lock now
settings.power.sleep	深度休眠	Deep sleep
settings.power.confirm_off	确认关机？	Power off?
settings.power.confirm_restart	确认重启设备？	Restart device?
settings.power.auto	自动锁屏时间	Auto-lock
settings.power.auto.off	关闭	Off
settings.power.auto.m1	1 分钟	1 min
settings.power.auto.m3	3 分钟	3 min
settings.power.auto.m5	5 分钟	5 min
settings.power.auto.m10	10 分钟	10 min
settings.power.auto.m30	30 分钟	30 min
settings.power.awake	保持唤醒已开启，自动锁屏暂停	Stay-awake is on. Auto-lock is paused.
settings.power.doubletap.on	双击屏幕唤醒 · 开	Double-tap wake · On
settings.power.doubletap.off	双击屏幕唤醒 · 关	Double-tap wake · Off
settings.power.doubletap.hint	电源键单击锁屏或唤醒。深度休眠仅用电源键恢复。	Tap the power key to lock or wake. Deep sleep wakes only from the power key.
settings.power.appearance	锁屏外观 · %s	Lock look · %s
settings.power.rotation.on	方向锁定 · 开	Rotation lock · On
settings.power.rotation.off	方向锁定 · 关	Rotation lock · Off
settings.power.hint	关机前保留锁屏画面，并标注已关机。长按电源键打开电源菜单。	Shutdown keeps the lock screen and marks the device off. Hold the power key for the power menu.
settings.power.save_failed	电源设置保存失败	Couldn't save power settings
settings.power.cover_save_failed	封面布局保存失败	Couldn't save the cover layout
settings.power.style.clock	翻页时钟	Flip clock
settings.power.style.quotes	划线摘录	Highlights
settings.power.style.covers	藏书封面	Covers
settings.power.style.page	保持当前页面	Keep page
settings.power.style.image	自定义图片	Custom image
settings.power.style.board	灵犀画板	Lingxi board
settings.power.locked.doubletap	已锁屏 · 双击屏幕唤醒	Locked · double-tap to wake
settings.power.locked.key	已锁屏 · 点按电源键唤醒	Locked · tap the power key
settings.power.sleeping	已休眠 · 按电源键恢复	Sleeping · power key to wake
settings.power.off_state	已关机 · 长按电源键开机	Off · hold the power key
settings.power.low_power	低功耗待机受限 · 电源键可唤醒	Low-power standby limited · power key wakes
settings.power.restarting	正在重启	Restarting
settings.power.toast.storage	请先结束存储操作	Finish the storage task first
settings.power.toast.storage_transfer	请先结束存储操作或文件传输	Finish storage or the file transfer first
settings.power.toast.weread	请先结束微信读书同步	Finish the WeRead sync first
settings.power.toast.memory	锁屏内存不足，请稍后重试	Not enough memory to lock. Try again.
settings.power.toast.progress	阅读进度未能保存，请检查TF卡	Reading progress wasn't saved. Check the TF card.
settings.power.toast.unsaved	内容未保存，暂不进入电源状态	Content isn't saved, so power state stays as it is
settings.power.toast.home	家庭网络任务尚未结束，请稍后重试	Home network task is still running. Try again.
settings.power.toast.network	网络传输尚未结束，请稍后重试	A network transfer is still running. Try again.
settings.power.toast.off_failed	关机失败，请重试	Couldn't power off. Try again.
settings.power.toast.off_paint	关机画面刷新失败，已取消关机	Couldn't refresh the power-off screen. Shutdown cancelled.
settings.power.toast.lock_failed	无法进入锁屏，请重试	Couldn't lock. Try again.
settings.wifi.title	Wi-Fi	Wi-Fi
settings.wifi.on	已开启	On
settings.wifi.off	已关闭	Off
settings.wifi.nearby	附近网络	Nearby
settings.wifi.saved	已存网络	Saved
settings.wifi.scanning	扫描中	Scanning
settings.wifi.rescan	重新扫描	Rescan
settings.wifi.add	添加网络	Add network
settings.wifi.transfer	无线传书	Transfer
settings.wifi.detail	网络详情	Network
settings.wifi.connect	连接此网络	Connect
settings.wifi.connected	已连接	Connected
settings.wifi.password	重新输入密码	New password
settings.wifi.auto.on	自动连接：开启	Auto-join: On
settings.wifi.auto.off	自动连接：关闭	Auto-join: Off
settings.wifi.forget	忘记此网络	Forget
settings.wifi.forget_short	忘记网络	Forget
settings.wifi.security.pass	安全：需要密码	Security: Password
settings.wifi.security.open	安全：无密码	Security: Open
settings.wifi.state.preparing	正在准备网络	Preparing network
settings.wifi.state.disabled	Wi-Fi 已关闭	Wi-Fi is off
settings.wifi.state.connecting	正在连接	Connecting
settings.wifi.state.connected	已连接	Connected
settings.wifi.state.searching	正在查找网络	Searching
settings.wifi.state.failed	连接未成功	Couldn't connect
settings.wifi.state.idle	未连接网络	Not connected
settings.wifi.hint.on	选择下方网络，连接一次即可记住	Pick a network below. A successful join is remembered.
settings.wifi.hint.off	开启后可连接网络或无线传书	Turn on to join a network or transfer books.
settings.wifi.ip	地址 %s	Address %s
settings.wifi.ip_label	网络地址 %s	Address %s
settings.wifi.row.connected	已连接	Connected
settings.wifi.row.connecting	正在连接	Connecting
settings.wifi.row.saved	已保存	Saved
settings.wifi.row.unsupported	暂不支持	Unsupported
settings.wifi.row.secured	需要密码	Password
settings.wifi.row.open	开放网络	Open
settings.wifi.signal.sep	 · 信号	 · Signal
settings.wifi.signal.strong	强	Strong
settings.wifi.signal.good	良好	Good
settings.wifi.signal.fair	较弱	Fair
settings.wifi.signal.weak	弱	Weak
settings.wifi.saved_empty	尚未保存网络，可从附近网络选择或添加。	No saved networks. Pick one nearby or add it.
settings.wifi.unsupported_body	此网络认证方式暂不支持，请选择其他网络。	This security type isn't supported. Choose another network.
settings.wifi.detail.connected	已连接	Connected
settings.wifi.detail.saved	已保存	Saved
settings.wifi.detail.unsaved	尚未连接	Not connected
settings.wifi.detail.known	已记忆连接	Remembered
settings.wifi.detail.secured	加密网络	Secured
settings.wifi.detail.open	开放网络	Open
settings.wifi.scan.start	扫描启动未成功，请重新扫描	Scan didn't start. Scan again.
settings.wifi.scan.result	读取扫描结果失败，请重新扫描	Couldn't read scan results. Scan again.
settings.wifi.scan.timeout	扫描超时，请重新扫描	Scan timed out. Scan again.
settings.wifi.scan.cancelled	扫描已中断，请重新扫描	Scan was interrupted. Scan again.
settings.wifi.scan.incomplete	扫描未完成，请重新扫描	Scan didn't finish. Scan again.
settings.wifi.scan.searching	正在查找附近网络…	Looking for networks…
settings.wifi.scan.after_connect	连接结束后将自动扫描附近网络…	Nearby networks will be scanned after connecting…
settings.wifi.scan.preparing	正在准备扫描附近网络…	Preparing to scan nearby networks…
settings.wifi.scan.off_hint	开启或点重新扫描，即可查找网络；也可直接添加。	Turn Wi-Fi on or tap Rescan. You can also add a network.
settings.wifi.scan.empty	扫描完成，未发现附近网络；可重新扫描或手动添加。	Scan finished. No networks nearby. Rescan or add one.
settings.wifi.scan.never	尚未扫描，可点重新扫描或手动添加。	Not scanned yet. Tap Rescan or add a network.
settings.wifi.toast.unsupported	此网络认证方式暂不支持	This security type isn't supported
settings.wifi.toast.ssid	网络名称需要 1–32 字节	Network name must be 1–32 bytes
settings.wifi.toast.busy	正在处理网络，请稍候	Still working on the network
settings.wifi.toast.enable_fail	Wi-Fi 开启请求未提交，请重试	Couldn't submit Wi-Fi on. Try again.
settings.wifi.toast.scan_fail	扫描请求未提交，请重新扫描	Couldn't submit the scan. Scan again.
settings.wifi.editor.password	Wi-Fi 密码	Wi-Fi password
settings.wifi.editor.ssid	添加网络 · 名称	Add network · name
settings.wifi.label.off	Wi-Fi 已关闭	Wi-Fi off
settings.wifi.label.idle	Wi-Fi 未连接	Wi-Fi not connected
settings.wifi.toast.pending	操作暂未完成，请稍后重试	That didn't finish. Try again.
settings.wifi.toast.generic	网络操作未完成，请重试	Network action didn't finish. Try again.
settings.wifi.toast.wifi_on	Wi-Fi 开启未成功，请重试	Couldn't turn Wi-Fi on. Try again.
settings.wifi.toast.connect	连接未成功，请检查密码或重新尝试	Couldn't connect. Check the password and try again.
settings.wifi.toast.profile_saved	已连接，但网络未保存，请重试	Connected, but the network wasn't saved. Try again.
settings.wifi.toast.profile	网络记忆未更新，请重试	Saved networks weren't updated. Try again.
settings.wifi.toast.transfer	传书网络未准备完成，请重试	Transfer network isn't ready. Try again.
settings.wifi.toast.unavailable	网络暂不可用，请稍后重试	Network isn't available yet. Try again.
settings.wifi.toast.need_lan	先连接手机所在的 Wi-Fi，再开始传书	Join the phone's Wi-Fi before transferring.
settings.wifi.toast.storage	存储暂不可用，请检查存储卡后重试	Storage isn't available. Check the card and try again.
settings.wifi.toast.card_gone	存储卡已移除，传书已停止	Card removed. Transfer stopped.
dialog.wifi.forget.title	忘记此网络？	Forget this network?
dialog.wifi.forget.body	将移除已保存的连接信息，下次连接需要重新输入密码。	Saved sign-in info will be removed. You'll enter the password next time.
dialog.transfer.stop.title	结束本次传书？	Stop this transfer?
dialog.transfer.uploading	正在上传的文件会中断。已经完成的文件会保留。	The file being sent will stop. Finished files stay.
dialog.transfer.stop_body	结束后将关闭上传网页，已完成的文件会保留。	Stopping closes the upload page. Finished files stay.
dialog.transfer.stopping	正在安全结束，请稍候…	Stopping safely…
dialog.transfer.stop_failed	尚未结束，可重试，或取消后继续留在本页。	Not stopped yet. Retry, or cancel and stay on this page.
dialog.transfer.retry	重试结束	Retry stop
dialog.transfer.stop	结束传书	Stop transfer
settings.transfer.title	无线传书	Transfer
settings.transfer.hub	存储与传输	Storage
settings.transfer.same_wifi	同一 Wi-Fi	Same Wi-Fi
settings.transfer.hotspot	设备热点	Hotspot
settings.transfer.usb	USB U 盘	USB drive
settings.transfer.storage	TF 卡与笔记	TF card
settings.transfer.books	打开本机图书	On-device books
settings.transfer.open_shelf	打开书架	Open shelf
settings.transfer.done_shelf	完成，打开书架	Done, open shelf
settings.transfer.join	连接热点	Join hotspot
settings.transfer.open_page	打开网页	Open page
settings.transfer.reselect	重新选择方式	Change method
settings.transfer.card.missing	请先插入 TF 卡	Insert a TF card
settings.transfer.card.exported	TF 卡正在由电脑使用	TF card is in use by a computer
settings.transfer.card.verified	TF 卡已通过读写检查	TF card passed read/write checks
settings.transfer.card.mounted	TF 卡已挂载	TF card mounted
settings.transfer.card.bad	TF 卡暂不可用，请先检查	TF card isn't ready. Check it first.
settings.transfer.card.manage	挂载、格式化与检查都在存储页完成	Mount, format, and checks are on the Storage page
settings.transfer.hint.lan_ready	推荐 · 手机与阅读器在同一网络	Recommended · phone and reader on the same network
settings.transfer.hint.lan	连接家中 Wi-Fi 后扫码上传	Join home Wi-Fi, then scan to upload
settings.transfer.hint.ap	不需要路由器，手机直连设备热点	No router. The phone joins the device hotspot.
settings.transfer.hint.usb	数据线连接电脑，拖入文件	Connect a cable and drag files over
settings.transfer.state.preparing	正在准备传书…	Preparing transfer…
settings.transfer.state.not_started	传书未开始	Transfer didn't start
settings.transfer.state.receiving	正在接收文件	Receiving files
settings.transfer.state.waiting	等待上传	Waiting for upload
settings.transfer.state.incomplete	传书未完成	Transfer didn't finish
settings.transfer.state.connecting	正在连接网络…	Connecting…
settings.transfer.state.stopped	传书已停止	Transfer stopped
settings.transfer.mode.ap	设备热点	Hotspot
settings.transfer.mode.sta	同一 Wi-Fi	Same Wi-Fi
settings.transfer.qr.preparing	正在准备二维码	Preparing QR
settings.transfer.qr.pending	连接后显示二维码	QR appears after connecting
settings.transfer.qr.step	第1步 · 扫码连接阅读器热点	Step 1 · Scan to join the reader hotspot
settings.transfer.qr.browser	扫码后在浏览器选择文件	Scan, then pick files in the browser
settings.transfer.password	热点密码：%s	Hotspot password: %s
settings.transfer.same_net	手机与阅读器保持连接同一个 Wi-Fi。	Keep the phone and reader on the same Wi-Fi.
settings.transfer.url.reselect	请重新选择传书方式	Choose a transfer method again
settings.transfer.url.pending	正在取得上传网址…	Getting the upload address…
settings.transfer.capacity	%s · 剩余 %s%s	%s · %s free%s
settings.transfer.capacity.flash	内置存储	Internal storage
settings.transfer.capacity.card	存储卡	Card
settings.transfer.capacity.limit	 · 单文件≤1 MB	 · 1 MB per file
settings.transfer.progress.receiving	正在接收 %u%% · 已完成 %u 个	Receiving %u%% · %u done
settings.transfer.progress.files_retry	已完成 %u 个文件%s	%u files done%s
settings.transfer.progress.retry	 · 请在浏览器重试	 · retry in the browser
settings.transfer.progress.bytes	%s / %s · 已完成 %u 个	%s / %s · %u done
settings.transfer.progress.done	已完成 %u 个文件	%u files done
settings.manage.title	手机与电脑管理	Phone & computer
settings.manage.weread_title	微信读书 · 手机配置	WeRead · phone setup
settings.manage.refresh	刷新链接	Refresh link
settings.manage.stop	停止管理	Stop
settings.manage.start	开启无线管理	Start wireless setup
settings.manage.connect	连接 Wi-Fi	Connect Wi-Fi
settings.manage.qr_on	开启后扫码管理	Scan to manage after starting
settings.manage.qr_off	先连接 Wi-Fi	Connect Wi-Fi first
settings.manage.weread_head	手机扫码填写微信读书 API Key	Scan with your phone to enter the WeRead API key
settings.manage.active_head	手机扫码管理，电脑连接桌面软件	Scan with your phone, or connect desktop software
settings.manage.idle_head	在手机或电脑上管理小纸 Pico	Manage Read Pico from a phone or computer
settings.manage.weread_body	手机与 Pico 连接同一 Wi-Fi，扫码后填写 Key、选择书籍同步。配置仅保存在此设备，无需电脑。	Phone and Pico share Wi-Fi. Scan, enter the key, and choose books. Settings stay on this device.
settings.manage.active_body	手机与阅读器保持同一 Wi-Fi，可管理图书、应用、画板和连接配置。	Keep the phone and reader on the same Wi-Fi to manage books, apps, boards, and connections.
settings.manage.idle_body	先连接 Wi-Fi；也可通过 USB 连接桌面软件，在电脑完成首次配网。	Connect Wi-Fi first, or use USB desktop software for the first network setup.
settings.manage.ttl	约 %u 分钟有效 · 可离开此页	About %u min left · you can leave this page
settings.manage.weread_fail	暂时无法打开手机配置	Can't open phone setup right now
settings.home_link.title	家庭	Home
settings.home_link.empty	固件仅保留空状态入口。安装 TF 工具包 home-link 后可监看 Home Assistant 与打印机。	This firmware keeps an empty Home entry. Install the TF package home-link to watch Home Assistant and printers.
settings.home_link.open	打开家庭工具包	Open Home Link
settings.home_link.settings_title	家庭设置	Home settings
settings.home_link.settings_empty	连接配置随家庭工具包提供。未安装包时无法保存 HA 令牌或打印机凭据。	Connection setup comes with the Home Link package. Without it, HA tokens and printer credentials can't be saved.
settings.home_link.printers_title	打印机	Printers
settings.home_link.printers_settings	打印机设置	Printer settings
settings.home_link.printers_empty	打印机只读监看已迁至 TF 工具包。未安装时本页保持空状态，不影响阅读。	Printer monitoring lives in the TF package. This page stays empty until it is installed, and reading is unchanged.
settings.home_link.missing	请将 home-link.pga 复制到 TF apps/tools 后刷新	Copy home-link.pga to TF apps/tools, then refresh
settings.home_link.monitor	家庭连接	Home Link
settings.home_link.syncing	同步中…	Syncing…
settings.home_link.local	本地	Local
settings.home_link.external	外部	External
settings.home_link.host_old	当前固件宿主过旧，缺少 home_link 服务。请刷含 PGA 桥的固件后再打开本工具包。	This firmware host is too old for Home Link. Flash a build with the PGA bridge, then open this package.
settings.home_link.status	%s · 实体 %u/%u · Wi‑Fi %s	%s · entities %u/%u · Wi-Fi %s
settings.home_link.wifi_on	已连接	Connected
settings.home_link.wifi_off	未连接	Not connected
settings.home_link.access	Cloudflare Access 拦截：请在设置中填写 Service Token（非浏览器登录）。	Cloudflare Access blocked the request. Enter a Service Token in Settings (not a browser login).
settings.home_link.unconfigured	尚未配置。点「设置」填写本地/外部地址、HA 令牌与可选 Access。	Not set up. Tap Settings for local/external URLs, the HA token, and optional Access.
settings.home_link.off	关闭	Off
settings.home_link.on	开启	On
settings.home_link.settings	设置	Settings
settings.home_link.sync	同步	Sync
settings.home_link.printers	打印机	Printers
settings.home_link.setup_title	连接设置	Connection
settings.home_link.setup_sub	设备本地保存	Saved on device
settings.home_link.setup_body	手机 manage 在薄宿主仍返回 home_module_missing。此处配置仅存本机，供本工具包使用。	Phone manage still returns home_module_missing on the thin host. These settings stay on the device for this package.
settings.home_link.field.local	本地地址	Local URL
settings.home_link.field.external	外部 HTTPS	External HTTPS
settings.home_link.field.token	HA 令牌	HA token
settings.home_link.field.cf_id	CF Client ID	CF Client ID
settings.home_link.field.cf_secret	CF Client Secret	CF Client Secret
settings.home_link.field.wifi	家庭 Wi‑Fi	Home Wi-Fi
settings.home_link.field_line	%s：%s	%s: %s
settings.home_link.unset	（未设）	(not set)
settings.home_link.saved	已保存	Saved
settings.home_link.flags	启用 %s · 外部 %s · 外控 %s · Access %s	Enabled %s · External %s · Remote %s · Access %s
settings.home_link.flag.on	开	On
settings.home_link.flag.off	关	Off
settings.home_link.access_ready	已配置	Set
settings.home_link.access_none	无	None
settings.home_link.toggles	开关项	Toggles
settings.home_link.save	保存	Save
settings.home_link.saved_toast	已保存到设备	Saved on device
settings.home_link.save_failed	保存失败（检查地址/令牌）	Couldn't save (check URL or token)
settings.home_link.no_input	宿主不支持输入	This host can't edit text
settings.home_link.input_fail	无法打开输入	Couldn't open the editor
settings.home_link.printers_sub	只读监看	Read only
settings.home_link.printers_body	独立 Bambu MQTT/云端仍待完整迁入；此处显示宿主快照中的打印机槽位与 HA 导入状态。	Standalone Bambu MQTT/cloud is not fully moved yet. This page shows host snapshot slots and HA import state.
settings.home_link.host_down	宿主服务不可用	Host service unavailable
settings.home_link.slot	%u · %s	%u · %s
settings.home_link.empty_slot	空槽位	Empty slot
settings.home_link.no_state	无状态	No status
settings.home_link.progress	%s · %d%%	%s · %d%%
settings.home_link.no_data	无数据（LAN/云端 MQTT 未迁入 PGA）	No data (LAN/cloud MQTT is not in the PGA yet)
settings.home_link.back_monitor	返回监看	Back to monitor
settings.home_link.submitted	已提交	Sent
settings.home_link.control_failed	控制失败	Control failed
settings.home_link.input_title	家庭连接输入	Home Link input
settings.home_link.phone_fill	手机填写	Phone setup
settings.home_link.setup_body_portal	点「手机填写」在局域网扫码。电脑管理页可稍后读取同一份配置，两边不要同时保存。	Tap Phone setup and scan on the same LAN. The desktop can read the same settings later. Avoid saving from both at once.
```

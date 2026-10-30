# 小纸 Pico 蓝牙翻页器 / Read Pico Bluetooth page turner

ESP32-S3 使用 BLE HID over GATT（HOGP）主机连接一个外部翻页器。首次使用默认关闭；开启后扫描 8 秒，最多显示 8 个广播 `0x1812` HID 服务的设备。扫描列表中的无名称设备显示为「BLE HID」。用户需要先让翻页器进入配对模式，再从设备设置中选择。

ESP32-S3 connects one external page turner as a BLE HID-over-GATT (HOGP) central. Bluetooth is off on first use. A scan lasts eight seconds and shows at most eight peers advertising the `0x1812` HID service. Unnamed peers use “BLE HID”. Put the remote into pairing mode before selecting it in settings.

开关、按键方向和上次成功连接的设备会保留。开启后或连接意外断开后最多尝试 3 次自动重连；主动断开和扫描会暂停自动重连。忘记设备会删除本组件的配对记录。关闭后停止扫描和连接，已初始化的主机保留空闲状态，避免反复初始化；本组件不保证蓝牙从硬件深睡唤醒。

The switch, direction preference and last successful peer persist. Enabling or an unexpected disconnect permits up to three reconnect attempts. Manual disconnect and scanning suspend reconnect. Forget removes this component’s bond. Disabling stops scanning and disconnects while leaving an initialized host idle. Bluetooth wake from hardware deep sleep is not guaranteed.

支持标准键盘 Page Up / Page Down、左右与上下方向键、空格，以及媒体音量加减、上一曲和下一曲。按报告描述解析布局，不猜测媒体报告的字节意义；只有按下边沿产生一次事件，长按重复报告不会连续翻页。设置内可交换前后方向。标准 8 字节 Boot Keyboard 仅在报告描述不可用且存在 Protocol Mode 时作为后备。

Supported inputs are standard keyboard Page Up / Page Down, arrows and Space, plus consumer Volume Up / Down and Previous / Next Track. Report descriptors define the layout; consumer bytes are never guessed. A press creates one event until release. Settings can swap direction. Standard eight-byte Boot Keyboard is a fallback only when no usable report map exists and Protocol Mode is available.

仅支持无需输入 PIN 的 BLE HID 配对。经典蓝牙、只发送私有协议或相机快门而未广播 HID 服务的设备，以及夹在屏幕上的电容触控翻页器，不属于此驱动兼容范围。使用随机地址的设备重连还取决于对端是否正确保存配对与身份信息。

Only BLE HID pairing without PIN entry is supported. Classic Bluetooth, private-protocol/shutter devices without advertised HID service, and capacitive screen clips are outside this driver’s compatibility. Reconnect for randomized-address peers also depends on the peer retaining correct bonding and identity data.

组件不直接绘图或调用阅读器。UI 从线程安全快照获取状态，只在允许翻页的阅读状态消费事件；其他页面应丢弃事件。输入队列最多 8 条，超过 2 秒的旧事件会被丢弃，避免加载和刷新后补发过时翻页。HID 描述上限 1024 字节、8 个输入报告、48 个有效字段；超预算设备会报不支持，不无限分配。

The component neither draws nor calls the reader. UI uses thread-safe snapshots and consumes events only when reading permits page turns; other pages discard them. The input queue holds eight events and expires events older than two seconds. HID limits are 1024 descriptor bytes, eight input reports and 48 useful fields; over-budget peers are rejected without unbounded allocation.

复跑解析回归：`python3 tools/test_ble_pager.py --sanitize`。网页由独立平台适配模拟蓝牙设备及按键，不使用电脑蓝牙；真实配对、射频共存、功耗与翻页器兼容性必须在实体设备验收。

Run parser regression with `python3 tools/test_ble_pager.py --sanitize`. The browser platform adapter simulates peers and button input without using the computer’s Bluetooth. Physical pairing, coexistence, power and remote compatibility require device acceptance.

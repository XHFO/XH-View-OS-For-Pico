# 显示刷新契约 / Display refresh contract

小纸 Pico 的快速交互由刷新波形、扫描档位和页面调度共同决定。局部刷新不等于高刷新率；`MODE_DU` 本身也不表示连续 DU。
Fast interaction depends on the waveform, scan profile and page scheduler together. A local update does not imply a higher refresh rate, and `MODE_DU` alone does not mean continuous DU.

## 官方基线与来源 / Official baseline and sources

本契约对照官方仓库 `MindReset/read_pico_firmware` 的提交 `6a247cc94420b85c650b32a6b2c063a360070eee`。下列数字来自该提交中的表与代码；不表示已核对远端最新版本，也不是应用端到端延迟承诺。
This contract was checked against official commit `6a247cc94420b85c650b32a6b2c063a360070eee` in `MindReset/read_pico_firmware`. Counts below describe that revision's tables and code, rather than the latest remote revision or guaranteed application latency.

- [波形与帧数 / Waveforms and phase counts](https://github.com/MindReset/read_pico_firmware/blob/6a247cc94420b85c650b32a6b2c063a360070eee/components/e0470_epaper_waveform/include/e0470_epaper_waveform.h)
- [刷新出口与扫描档位 / Present helpers and scan profiles](https://github.com/MindReset/read_pico_firmware/blob/6a247cc94420b85c650b32a6b2c063a360070eee/main/display.c)
- [触摸跟手与抬手定稿 / Touch tracking and release settling](https://github.com/MindReset/read_pico_firmware/blob/6a247cc94420b85c650b32a6b2c063a360070eee/main/apps/app_touch.c)
- [连续 DU 状态机 / Continuous DU state machine](https://github.com/MindReset/read_pico_firmware/blob/6a247cc94420b85c650b32a6b2c063a360070eee/components/continuous_du/continuous_du.c)
- [扫描时序计算 / Scan timing calculation](https://github.com/MindReset/read_pico_firmware/blob/6a247cc94420b85c650b32a6b2c063a360070eee/components/read_pico/read_pico_epd_timing.c)
- [事件循环 / Event loop](https://github.com/MindReset/read_pico_firmware/blob/6a247cc94420b85c650b32a6b2c063a360070eee/main/app/app_loop.c) 与 / and [手势识别 / Gesture recognition](https://github.com/MindReset/read_pico_firmware/blob/6a247cc94420b85c650b32a6b2c063a360070eee/main/ui/ui_gesture.c)

## 模式选择 / Mode selection

| 路径 / Path | 相位与扫描 / Phases and scan | 适用内容 / Intended content |
| --- | --- | --- |
| `E0470_WAVEFORM`, `MODE_GL16` | 裁剪后 37 相，FULL 扫描 / 37 trimmed phases, FULL scan | 稳定页面、封面、插图、灰阶与抗锯齿文字；白底补推要求区域内全像素驱动 / Stable pages, covers, images, gray and antialiased text; the white-background pulse requires full-pixel drive within the area |
| `E0470_WAVEFORM`, `MODE_GC16` | 裁剪后 36 相，FULL 扫描 / 36 trimmed phases, FULL scan | 首帧、方向切换、明确清残影与锁屏定稿；区域内有明显清屏闪动 / Initial frame, rotation, explicit cleaning and final lock face; visibly flashes within its area |
| `E0470_WAVEFORM`, `MODE_DU` | 原厂 DU 20 相，FULL 扫描 / 20 vendor DU phases, FULL scan | 二值差分；目标灰阶按阈值映射到黑白，不能保留真实灰阶 / Binary differential updates; target gray is thresholded to black/white, losing real gray |
| `E0470_FOLLOW_WAVEFORM`, `MODE_DU` | 8 帧，FAST 扫描；最大黑推 7 帧、白推 8 帧 / 8 frames, FAST scan; up to 7 dark and 8 light pushes | 短暂的按键、候选词、数值、选择标记等快速反馈，结束后定稿 / Transient key, candidate, numeric and selection feedback, followed by settling |
| `continuous_du` | 每次调用只扫 1 相，FAST 扫描；每像素剩余相位由软件保留 / One phase per call, FAST scan; software retains each pixel's remaining phases | 持续移动的二值笔迹或光标；不是完整灰阶绘制器 / Continuously moving binary ink or cursors; not a grayscale renderer |

`E0470_FULL_WAVEFORM` 另保留 GC16/GL16 各 48 相作为参考；默认裁剪表不是该完整表。FOLLOW 是按灰阶差值分配推动次数的快速近似，不能把它当作稳定灰阶定稿；continuous DU 的差分编码只支持黑、白和保持。
`E0470_FULL_WAVEFORM` retains separate 48-phase GC16/GL16 reference tables; the default trimmed tables differ. FOLLOW apportions pushes by the gray difference as a fast approximation, rather than a final grayscale rendering. Continuous DU encodes only black, white and hold.

普通 DU 与 FOLLOW DU 使用同一个模式常量，但 `display.c` 只为 **FOLLOW 波形 + DU 模式**选择 FAST。单把页面从 GL16 改为普通 DU，仍要完成 20 相 FULL 扫描。
Vendor DU and FOLLOW DU share the mode constant, but `display.c` selects FAST only for **the FOLLOW waveform with DU mode**. Changing a page from GL16 to vendor DU still requires 20 FULL scans.

## 连续交互与定稿 / Interaction and settling

所有系统稳定画面使用原生4bit画布：每像素0–15，网页原尺寸像素对应0、17、…、255，图标边缘、文字、图片与灰色背景均保留量化后的层次。角落菜单复用16灰阶静态基底，只对光圈与固定提示的变化区域提交GL16，同一选项不重复刷新。网页画布关闭插值并用像素采样缩放，避免浏览器额外混出硬件不存在的灰阶。
Every stable system frame uses native packed 4-bit pixels: levels 0–15 map to 0, 17, …, 255 in the original-resolution browser image. Icons, type, images and surfaces retain quantized gray. The corner menu reuses its gray base and submits GL16 only for changed cursor/prompt damage, skipping unchanged selection. Browser sampling disables interpolation so scaling does not introduce unsupported grays.

输入和TF画板保留快速continuous DU：只二值化本次损伤区，不改整页或稳定工具栏。停止触摸、余相排空并静置900ms后，从原始文字和画稿重新绘制16灰阶，通过GL16差分定稿一次；相同稳定内容不重复定稿。连续输入不插入周期强刷或逐键灰阶定稿。游戏与时钟的临时DU仅二值化变化像素，静止工具和背景保持原灰阶，暂停或动画结束恢复GL16。
Input and TF drawing retain fast continuous DU: threshold only current damage, preserving the rest of the frame and static toolbars. After contact ends, phases drain and 900ms of inactivity elapses, redraw original text/ink in 16 grays and settle once with changed-area GL16. Unchanged stable content does not repeatedly settle. Continuous input never inserts periodic cleans or per-key gray updates. Transient game/clock DU thresholds changed pixels only, preserving static tools/background; pause or animation completion restores GL16.

1. 稳定页面用灰阶绘制；没有像素变化时不推屏。输入时只重绘文本、拼音、候选等实际变化部分，键盘框和未变化内容保持不动。
   Render stable content in grayscale and skip presents when pixels are unchanged. During input, repaint only changed text, composition and candidates while keeping keyboard outlines and unchanged content stable.
2. 键盘与画板的快速反馈使用 continuous DU，由同一个显示所有者逐 tick 扫一相，停止输入后继续补完余相；不能在一次事件里同步循环扫完全部相位。其它快速交互可使用局部 FOLLOW。
   Keyboard and drawing use a single owner to scan one continuous-DU phase per tick and drain after input stops. Never synchronously drain all phases in one event. Other fast interactions may use local FOLLOW.
3. 连续输入期间合并更新与清理请求；编辑器只在连续输入期间保留局部黑白目标，停止后用GL16恢复真实灰阶。需要清残影时再使用 GC16，不应每个字母或每个 MOVE 都强刷。
   Coalesce updates and cleanup during input. Keep only transient editor damage binary during continuous input, then restore actual gray with GL16. Use GC16 when cleanup is needed rather than for every letter or MOVE.
   文字编辑器用独立5ms采样和有界边沿队列捕获同步绘制/扫描期间的短按；逻辑输入不等推屏，松手且停顿85ms后再合并目标。队列未排空时先处理输入。工作器在非编辑器和电源交接前同步暂停，停用时等待通知而非周期唤醒。主机回归不能证明物理光学响应，实机用`editor_perf`分段核对输入、字形读取/栅格化与单相扫描耗时。
   Text editors use independent 5ms sampling and a bounded edge queue across synchronous paint/scan work. Logical input does not wait for display; a released contact and 85ms pause merge the target, with queued input handled first. Pause the worker outside editors and before power handoff; a disabled worker waits for notification without periodic wakes. Host regressions do not prove optical response; physical `editor_perf` logs separate input, glyph I/O/rasterization and single-phase scan cost.
4. 模式切换必须保持显示参考帧与实际已驱动像素一致。对临时二值区域阈值化时，同步其前后帧语义；退出、取消、切页、方向变化和失败恢复都要结束连续状态并定稿，不能留下未完成的驱动。
   Keep display reference frames consistent with pixels actually driven. Thresholding a transient binary region also requires coherent front/back semantics. Exit, cancellation, page changes, rotation and error recovery must end the continuous state and settle it without abandoned drive.

官方触摸演示的成功路径返回 `APP_REDRAW_DONE`，防止主循环重复推屏；连续路径分配失败时退回区域 FOLLOW，抬手再用区域 GC16 定稿。普通 `APP_REDRAW_AREA` 默认仍使用原厂 DU，要使用 FOLLOW 必须由自定义 `present` 或明确波形出口负责。`ui_gesture` 只产出事件，不选择波形；保留 MOVE 接触跟踪不等于启用了连续 DU。
The official touch demo returns `APP_REDRAW_DONE` after presenting to prevent a second loop present. If continuous allocation fails, it falls back to area FOLLOW, then area GC16 on release. Ordinary `APP_REDRAW_AREA` still defaults to vendor DU; FOLLOW requires a custom `present` or an explicit waveform helper. Gesture recognition produces events without choosing a waveform, so retained MOVE tracking does not enable continuous DU.

共享出口每 14 次普通软刷会把当前区域提升为 GC16；FOLLOW 不参与计数或自动提升。连续交互不要意外走普通软刷路径而把周期清理插进输入过程，也不要全局关闭必要清理。清理的时机与区域应由交互边界安排。
The shared helper promotes the current area to GC16 after 14 ordinary soft updates. FOLLOW neither counts nor promotes. Keep periodic cleaning out of active input by choosing the correct interaction path, while retaining necessary cleanup at an appropriate boundary and area.

## 局部更新、时钟与约束 / Regions, clock and constraints

局部矩形限制被驱动像素及清理范围；当前高层驱动仍逐相扫描整个面板，不保证刷新耗时按矩形面积缩小。两个相隔很远的变化若合成一个大矩形，会把中间未变区域纳入 GL16/GC16 全像素驱动；优先使用有界区域，但每追加一个独立推屏又会增加一轮波形扫描，必须合并真正相邻的更新。
An area bounds driven pixels and cleanup, while the current high-level path still scans the whole panel for each phase. Latency therefore does not scale directly with rectangle area. One bounding box around distant changes includes the unchanged gap in full-pixel GL16/GC16 drive. Prefer bounded regions, but combine adjacent changes because each extra independent present adds another waveform scan sequence.

FULL 档按约 11090 µs 的标定帧周期补足行时序。提高像素时钟会改变有效数据传输与行内余量，却不会自动减少相位数或这个目标帧周期；还可能使 DMA 供数更紧张。应先减少重绘、阻塞、波形相位和重复推屏，不能通过擅改像素时钟、厂家波形、面板电压或电源时序追求 UI 速度。连续操作沿用显示电源轨的 keepalive，空闲再下电。
FULL scan pads line timing to its calibrated frame period of about 11090 µs. A higher pixel clock changes transfer time and line slack without automatically reducing phase count or that frame target, and can tighten DMA supply margins. First reduce redraw work, blocking, waveform phases and redundant presents. Do not seek UI speed by changing pixel clocks, vendor waveforms, panel voltage or power timing. Retain rail keepalive during interaction bursts and power down when idle.

`ALL_DU` 只用于受控实验，不作为全局产品默认：它牺牲封面、插图、细字抗锯齿和稳定灰阶，而且普通 DU 仍有完整扫描成本。continuous DU 的每像素状态需要约 `epd_width() * epd_height()` 字节 PSRAM；必须处理分配失败，旋转坐标也必须覆盖产品所有方向。官方演示的逻辑坐标辅助函数针对单一方向，不能直接当作四向通用变换。
Keep `ALL_DU` as a controlled experiment, rather than the product default. It loses cover/image gray and text antialiasing, while ordinary DU retains full scan costs. Continuous DU needs about `epd_width() * epd_height()` PSRAM bytes for pixel state, so allocation failure and every supported rotation need explicit handling. The official demo's logical-coordinate helpers target one orientation and cannot be reused as general four-way transforms.

## 验证边界 / Verification boundary

主机测试与同源网页可核对区域、调度、无变化不推屏、重复点击与保存状态；实际闪烁、残影、键到屏延迟和欠载只能由实机确认。对比输入体验时分别记录输入处理、排版绘制、推屏和定稿耗时，不把面板单相扫描频率称为应用刷新率。
Host tests and the shared-source browser can verify areas, scheduling, no-op presents, repeat input and saved state. Real flicker, ghosting, key-to-screen latency and underruns require hardware checks. Measure input handling, layout/drawing, presentation and settling separately, and do not call the single-phase scan frequency the application's refresh rate.


画板逐段绘制笔迹，保持触摸坐标采样。扇形菜单多个变化区先独立缩放再合并，一次选择只提交一轮 GL16；静态图标保留原16灰阶，不用放大的视觉光圈进行命中判定。
Drawing retains coordinate sampling and paints incremental segments. Fan damage is scaled separately before union and submitted in one GL16 update; static icons retain all native grays, and visual halos do not participate in hit testing.

`tools/test_pico_ink.py` 将真实 continuous-DU 状态机与共享推屏器一起运行，覆盖四个方向、单相排空、半程反向、无变化、中断、错误和内存不足；网页明确分开统计 FOLLOW、普通和 CONTINUOUS 调用。网页计数只是调度证据，不模拟面板黑度、波形耗时或真实触摸延迟。
`tools/test_pico_ink.py` runs the actual continuous-DU state machine and shared presenter across four rotations, single-phase draining, reversal, no-op, interruptions, errors and OOM. Browser telemetry separates FOLLOW, default and CONTINUOUS calls. These counts prove scheduling only, without simulating panel darkness, waveform time or physical touch latency.

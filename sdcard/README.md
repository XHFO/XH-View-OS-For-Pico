# TF 卡部署 / TF card deployment

默认完整字库为官方原版 **MiSans VF**，随本阅读器软件作为组件部署。将 `fonts/` 目录整体复制到 TF 卡根目录，保留字体及其许可证；固件默认读取 `/sdcard/fonts/MiSansVF.ttf`，无需首次手动挑选。已有明确字体偏好会保留，可在字体页改选和调节字重。

The default complete font is the original **MiSans VF**, bundled as a component of this reader software. Copy the entire `fonts/` directory to the TF card root, retaining the font and licenses. Firmware defaults to `/sdcard/fonts/MiSansVF.ttf`; explicit saved choices are retained. The Fonts page offers selection and variable weight.

完整字体按需从 TF 卡读取，不占用有限的 APP 分区。没有卡、文件损坏或传输独占卡时，暂用内置界面子集；恢复本地读卡后重新尝试原选择。内置子集只覆盖界面和示例文字，不能替代完整书籍字库。WiFi 网页可上传 TTF（每文件最多32 MiB）；USB 传输时也可将字体及许可证放入 `fonts/`。

The full font is read on demand from the TF card, outside the limited APP partition. A missing/unusable card or transfer ownership temporarily uses the built-in UI subset; local access restores the preferred font. The subset cannot cover arbitrary books. The webpage accepts TTF files up to 32 MiB, and USB can copy fonts and licenses into `fonts/`.

## MiSans 来源与许可 / MiSans source and license

- 官方下载 / Official download: https://hyperos.mi.com/font/download
- 官方发行包 / Official archive: https://hyperos.mi.com/font-download/MiSans.zip
- 官方许可说明 / Official FAQ: https://hyperos.mi.com/font/faq
- 原版可变字体 / Original variable font: `fonts/MiSansVF.ttf`，20,093,424 bytes.
- SHA-256: `0ddef90648998900175cfdca9a6f087a2544c182f130b0ad4f7e94a03a115e79`.
- 原件未经修改、子集化、重命名或转成静态字重；可变字重在运行时处理。
  The font is unmodified: no subsetting, renaming or conversion to a static font. Weight is applied at runtime.
- 使用 MiSans 字体。官方允许免费商业使用和软件嵌入；本软件保留归属与许可。MiSans 不适用本仓库主体的 Apache-2.0 或后述 OFL。不可将字体作为独立产品销售或分发，不得改编字体；以 [官方协议原件](fonts/MiSans-License.pdf) 为准，文字副本见 [MiSans-LICENSE.txt](fonts/MiSans-LICENSE.txt)。
  Uses MiSans. The official terms permit free commercial and embedded use with attribution. MiSans is governed by its [original license](fonts/MiSans-License.pdf), not Apache-2.0 or the OFL below. Do not sell or redistribute it as a standalone font product, or modify it. A text copy is in [MiSans-LICENSE.txt](fonts/MiSans-LICENSE.txt).

## 内置回退字体来源与许可 / Built-in fallback source and license

- 字体 / Font: ChillDuanSans VF，寒蝉端黑体。
- 上游发行 / Upstream release: [Warren2060/ChillDuanSans v1.30](https://github.com/Warren2060/ChillDuanSans/releases/tag/v1.30).
- 发行包 / Archive: `ChillDuanSans.v1.30.zip`，原文件 / original entry: `ChillDuanSans v1.3/ChillDuanSansVF.ttf`.
- 文件大小 / File size: 16,320,196 bytes.
- SHA-256: `966286fc44907d4123528e1efb98c08e51fff013cd083140fdad1bf6b593555c`.
- 随附字体逐字节保留上游原文件，未经子集化、重命名或修改；版权声明和 SIL Open Font License 1.1 原文见 [fonts/OFL.txt](fonts/OFL.txt)。
  The bundled font is byte-identical to the upstream original, without subsetting, renaming or modification. Copyright notices and the SIL Open Font License 1.1 are preserved in [fonts/OFL.txt](fonts/OFL.txt).

字体适用 OFL-1.1，不适用本仓库主体的 Apache-2.0。允许随软件部署、嵌入和再分发，但须保留版权和许可，不得单独销售字体；修改版须遵守保留字体名称等条款。以随附许可原文为准。

The font is licensed under OFL-1.1, not the repository's Apache-2.0 license. It may be bundled, embedded and redistributed with software while retaining copyright and license notices; it must not be sold on its own. Modified versions must follow the reserved font name conditions and other terms. The bundled license text governs.

内置 `main/assets/builtin.ttf` 是此字体的界面子集，生成器将其字体名设为 `Read Pico UI`，保留原版权与许可字段，以区别于未修改的上游字体。

The embedded `main/assets/builtin.ttf` is a UI subset. Its generator uses the font name `Read Pico UI` and preserves original copyright and license fields, distinguishing it from the unmodified upstream font.

## TF 卡游戏应用 / TF game apps

游戏包放在 `apps/games/<id>.pga`。它们是包含实际规则、AI 和 UI 的可执行模块，固件没有六个游戏的静态替代版本。安装包和存档分离；长按小游戏卡片移除只删包，重新安装同 ID 可继续。USB 拖入后须安全弹出并在设备结束传输，再刷新小游戏页。

Build packages with `tools/build_game_elf.py` and `tools/build_game_packages.py`; distribute only the resulting `.pga` files, not intermediate libraries. See [PGA1 / ABI v1](../docs/TF-APPS.md). The local native preview uses a separate simulated TF card and requires a matching native target in the package.

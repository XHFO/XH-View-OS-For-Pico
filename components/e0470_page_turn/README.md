# E0470 错相揭页引擎 / Staggered page-turn engine

翻页波形与错相揭页实现源自 **Kiiko 厂长**维护的
[kiikoread / Pico Reader](https://github.com/wegooo-cell/read-pico-reader)。
MindReset 官方文档列有该社区系统及其固件入口：

- <https://dot.mindreset.tech/docs/read_0/start/kiikoread>
- <https://kiikoread.com/>
- <https://github.com/wegooo-cell/read-pico-reader/releases>

本仓库将该实现适配到 MindReset Read Pico 的 E0470 波形、显示刷新路径和
`epdiy` LCD 后端，并增加本项目的条带调度、逐相 LUT、11090 µs 快档及双机阅读接入。

The waveform and staggered page-turn implementation originate from the
[kiikoread / Pico Reader](https://github.com/wegooo-cell/read-pico-reader)
project maintained by Kiiko. This repository adapts it to the MindReset Read
Pico E0470 waveform and refresh path, `epdiy`'s LCD backend, and this project's
band scheduling, per-phase LUT selection, 11090 µs pacing and paired-reader
integration.

## 许可 / License

本目录沿用源文件所保留的历史 Apache-2.0 声明。面板波形数据来自
`components/e0470_epaper_waveform`，底层显示驱动来自
[epdiy](https://github.com/vroland/epdiy)（LGPL-3.0-or-later）。

Files in this directory retain their historical Apache-2.0 notices. Panel
waveform data comes from `components/e0470_epaper_waveform`; the lower-level
display driver comes from [epdiy](https://github.com/vroland/epdiy)
(LGPL-3.0-or-later).

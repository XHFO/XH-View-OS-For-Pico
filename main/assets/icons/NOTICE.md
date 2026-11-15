# 小纸 Pico / Read Pico interface icons

39 SVGs under `lucide/` are unmodified source assets from **Lucide 0.468.0**:
https://github.com/lucide-icons/lucide/tree/f12b0de177fbc2a6795e99be065887e72b237123/icons

Pinned upstream commit: `f12b0de177fbc2a6795e99be065887e72b237123`.

The upstream ISC license and attribution, including the notice for Feather-derived
portions, are retained in `lucide/LICENSE`. Source URLs and SHA-256 hashes are in
`lucide/source.json`. Include this license when distributing source or binaries
that contain these icons. No Apple SF Symbols assets are included.

`pico/sd-card.svg` is original artwork, copyright 2026 XH_Lab, licensed under the
repository's Apache-2.0 license. It depicts a removable memory card rather than a SIM.
`pico/speaker.svg` is original XH_Lab artwork under the same Apache-2.0 license.

`tools/generate_pico_icons.py` compiles these vectors to
`main/system/pico_icons_data.h`. The generated header is derived from both sources;
its geometry retains the source asset licenses. Runtime code does not download,
parse SVG, or allocate a framebuffer for icons.

39 个 Lucide SVG 保持上游原样，ISC 许可与 Feather 衍生部分声明随包保留。
SD 卡图形为 XH_Lab 原创，使用仓库 Apache-2.0 许可。网页原生预览和设备共用生成
数据与 C 绘图，不包含 Apple SF Symbols 素材。
扬声器图形同样为 XH_Lab 原创，使用 Apache-2.0 许可。

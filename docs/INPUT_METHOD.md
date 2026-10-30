# 笔记输入 / Note input

拼音 26 键和九键共用离线词库，支持连续拼音、末尾补全、分词符和可翻页候选。输入设置显示当前使用的是内置词库还是 TF 扩展词库。候选不足时可继续补全拼音，或使用“分词”指定音节边界。文字输入提供拼音26键、拼音9键、英文26键和数字符号四种方式；手写文字输入及识别模型已撤下。

Pinyin QWERTY and T9 share the offline lexicon, with continuous segmentation, tail completion, explicit separators and paged candidates. Input settings show the active built-in or TF dictionary. Text input provides pinyin QWERTY, pinyin T9, English QWERTY and numbers/symbols. Handwriting text input and its recognition model have been removed.

将 `assets/tf-pack/readpico/ime/` 整个目录复制到 TF 卡的 `/readpico/ime/`。标准包为 43,730 条：32,000 词组和 11,730 单字，约 1.05 MiB。这是常用离线词库，不承诺覆盖所有人名、专业词或口语表达。单字能否显示仍取决于所选字体；推荐保留 TF 卡中的完整中文字体。复制或替换词库后重新插卡或重启设备。

Copy all of `assets/tf-pack/readpico/ime/` to `/readpico/ime/` on TF. The standard 1.05 MiB package contains 32,000 phrases and 11,730 characters. It is a common-word lexicon, not a complete vocabulary of names or technical terms. Glyph coverage depends on the chosen font. Reinsert the card or restart after replacing the file.

词库只在进入输入设置或编辑器时按媒体代际检查一次，完整加载到 PSRAM。输入、候选选择及候选翻页只查询 RAM，不逐键打开文件。初次载入遇到无卡、USB 独占、文件损坏、格式不兼容或内存不足时，保留内置输入能力；已成功加载的词库在卡片临时断开或 USB 独占时仍可从 RAM 使用，不阻塞输入。不会修改或删除卡中的词库。

Load and validate once per media generation on entry to input settings or the editor. All subsequent keystroke and candidate queries use RAM. If initial loading fails because of missing media, exclusive USB access, corruption, unsupported format or allocation failure, retain built-in input. A previously loaded lexicon remains usable in RAM across temporary media loss or USB access. The dictionary on TF is never modified.

## 笔记保存与草稿 / Note persistence and drafts

本机支持最多1024条笔记、划线与书签，每条笔记文字最多2047个 UTF-8 字节；长汉字文本大约682字，实际以编辑器的字节计数为准。此限制不等于候选词库的词条数量。旧版笔记和草稿中的64点笔迹附件保持原有格式，可继续查看、随文字保存并在导出中保留附件说明。已存英文、符号输入偏好的编号不变；旧手写输入偏好自动回退到拼音。升级不会删除TF卡上的旧模型或用户笔迹。独立的画板笔记仍支持直接书写，与文字输入法无关。

Up to 1024 notes, highlights and bookmarks share the local record store. Each note holds at most 2047 UTF-8 bytes, approximately 682 Chinese characters; the editor's byte count is authoritative. This limit is independent of dictionary size. Legacy notes/drafts retain their 64-point ink attachments for viewing, saving alongside text and attachment descriptions in exports. Existing English/symbol preference IDs remain unchanged; a retired handwriting preference falls back to pinyin. Upgrading does not delete old TF models or user ink. The independent drawing-notes app continues to support direct drawing separately from text input.

“TF 卡与存储”显示实际保存目标。内置存储可用时沿用 `/flash/books/.pico-system`；内置不可用且 TF 已挂载时使用 `/sdcard/books/.pico-system`。本次会话不会在写入失败后静默换源，不会自动格式化。需要合并时，在“TF 卡与存储 → 笔记与划线”确认从另一处可用存储合并到当前库；完整相同的记录只保留一份，源库不删除，目标成功提交后才更新显示。未保存的输入、损坏库、容量不足或换卡会阻止合并。笔记不写入原 EPUB/TXT 文件。导出文本另存于所选图书根的 `.pico-reader/reading-records.txt`，并不代替记录库本身。

TF Card & Storage shows the actual destination. Usable internal storage uses `/flash/books/.pico-system`; otherwise mounted TF storage uses `/sdcard/books/.pico-system`. A failed write never silently switches stores or formats media. Use TF Card & Storage → Notes & Highlights to explicitly merge the other available store into the current one. Identical records are deduplicated and the source is retained; update the live library only after the target commits. Unsaved input, corrupt libraries, capacity failures or changed media stop the merge. Original EPUB/TXT files are never edited. Exported text is a separate `.pico-reader/reading-records.txt` under the selected book root, not the record database itself.

停止输入约6秒后，系统尝试保存有改动的草稿；拼音尚未选词、手指正在按键、全局手势交互中会延期。无修改的草稿不重复写入。手动保存会尝试保护草稿，但草稿键失败不阻断正式记录的提交；只有正式记录提交成功才显示“笔记已保存”。清理草稿失败时另行提示，并保留重试。正式保存失败时保留编辑器与本次会话内容，请在断电前检查原因并重试；尚未落盘的文字无法承诺断电恢复。

Changed drafts save after approximately six idle seconds, deferred while composing, holding a key or using global gestures. Unchanged drafts perform no writes. An explicit save attempts to protect the draft but can commit the note even if the draft key fails. Note saved appears only after the record commit succeeds. Draft cleanup failures have a separate message and retry. A failed record commit keeps the editor and session contents available; check the cause and retry before power-off. Text that never reached storage cannot survive power loss reliably.

阅读记录使用独立不可变对象与最终索引提交：新增记录只写新增对象和有界索引，不随总记录数重复重写所有正文。运行时按实际条数与正文长度分配PSRAM。升级兼容读取旧v1/v2记录与旧短草稿，首次成功提交时采用v3；旧记录键保留。对象或索引写入失败不切换已提交的库，未引用的对象不显示。旧版固件不保证读取新增的v3记录。

Records use immutable objects and a final bounded-index commit: additions write new objects and the index rather than every stored body. Live PSRAM allocation follows actual record count and text length. Upgrade reads legacy v1/v2 records and short drafts, adopting v3 only after a successful commit while retaining the old keys. Object or index failures preserve the committed library; unreferenced objects are not displayed. Older firmware cannot be assumed to read newly added v3 records.

## 生成与许可 / Generation and licenses

文字输入使用黑白高对比键盘。编辑器独立采样触摸，保留同步绘字/刷屏期间的按下、移动与抬手顺序；抬起立即修改文字与组合状态，停顿85ms后合并重画文字、拼音和候选变化区，静态字母区不重复绘制。候选按已显示内容选择。待消费边沿或按住期间不开始新的绘制；溢出或读取失败取消当前接触至松手，切页、转向、USB交接和电源状态交接清队列。其他页面保留原采样路径。变化交给逐相位连续DU；输入时不会触发每14次普通软刷的清残影，切页恢复灰阶，旋转或失败仍完整恢复。维护与测试见[显示刷新契约](DISPLAY-REFRESH.md)及 `tools/test_app_touch_buffer.py`、`tools/test_pico_editor_render.py`、`tools/test_native_preview_keyboard.py`。

Text input uses a high-contrast binary keyboard. Independent editor sampling retains press/move/release order across synchronous font/display work. Release immediately changes text/composition; an 85ms pause merges changed text and candidates while static letters remain untouched. Candidate selection uses painted content. Held contacts or queued edges postpone new paint; overflow/read errors cancel through release, and page, rotation, USB and power boundaries flush capture. Other pages retain their original sampling. Changes use one-phase continuous DU. Typing avoids ordinary periodic cleans; exit restores grayscale and rotation/errors retain full recovery. See the [display refresh contract](DISPLAY-REFRESH.md) and the sampling/editor/native-keyboard tests.

生成器为 `tools/build_pico_ime_dictionary.py`，固定依赖 `jieba==0.42.1` 和 `pypinyin==0.55.0`。将生成的 `.bin`、`.json` 与 `licenses/` 一起分发；两个数据来源的 MIT 许可保存在 `assets/tf-pack/readpico/ime/licenses/`。生成器不会访问设备。

The reproducible generator pins jieba 0.42.1 and pypinyin 0.55.0. Distribute the binary, manifest and retained MIT notices together. It never accesses a device.

```sh
python3 -m pip install jieba==0.42.1 pypinyin==0.55.0
python3 tools/build_pico_ime_dictionary.py
python3 tools/test_pico_ime.py --sanitize
```

## 文件格式 / File format

所有整数为 little-endian。32 字节头：8 字节 `PICOIME1`、4 字节词条数、4 字节负载大小、4 字节负载 FNV-1a 32 校验值、12 字节保留。词条数至多 65,535，负载至多 2 MiB。

All integers are little-endian. The 32-byte header contains the 8-byte magic, entry count, payload size, FNV-1a 32 payload checksum and 12 reserved bytes. Limits are 65,535 entries and a 2 MiB payload.

负载依次为两组 `count × uint32` 偏移表和词条数据；偏移相对数据区。第一组按完整拼音、词频、汉字排序，第二组按九键数字、词频、汉字排序。每条记录为 `uint8 拼音长度`、`uint8 UTF-8 文本长度`、`uint8 频率惩罚`、ASCII 拼音、零终止、UTF-8 文本、零终止。拼音最多 39 字节，文本最多 127 字节。载入时验证校验值、长度、字符、UTF-8、偏移和索引顺序；输入时二分检索，并限制每次匹配的同音候选数量。

The payload holds two count-sized uint32 offset indices followed by variable-length records. Offsets are relative to records. Indices sort by pinyin/frequency/text and T9/frequency/text. Records hold one-byte pinyin length, text length and frequency penalty, then NUL-terminated ASCII pinyin and NUL-terminated UTF-8 text. Limits are 39 and 127 bytes. Loading validates checksums, lengths, encoding, offsets and ordering; queries use bounded binary searches.

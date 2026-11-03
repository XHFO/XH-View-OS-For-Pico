# XH View OS For Pico · Duo

> **Private dual-reader preview · 0.40.0-dual46** — Complete firmware source for `experiment/dual-pico-reader`. Wi-Fi synchronizes books and the shared font; authenticated BLE synchronizes opening, paired pages and turns. See the [Duo guide](docs/DUAL-READER.md). This branch remains separate from the main system. The upstream hardware reference follows below.

**Languages:** [English](./README.md) | [简体中文](./README.zh-CN.md) | [日本語](./README.ja-JP.md)

[Contributing](CONTRIBUTING.md) · [Support](SUPPORT.md) · [Security](SECURITY.md) · [Code of Conduct](CODE_OF_CONDUCT.md)

[![License](https://img.shields.io/github/license/MindReset/read_pico_firmware?style=for-the-badge&logo=apache&logoColor=white)](LICENSE)
[![Build](https://img.shields.io/github/actions/workflow/status/MindReset/read_pico_firmware/build.yml?branch=main&style=for-the-badge&logo=githubactions&logoColor=white)](https://github.com/MindReset/read_pico_firmware/actions)
![ESP-IDF](https://img.shields.io/badge/ESP--IDF-v6.1-E7352C?style=for-the-badge&logo=espressif&logoColor=white)
![Target](https://img.shields.io/badge/target-ESP32--S3-E7352C?style=for-the-badge&logo=espressif&logoColor=white)

Read Pico is an ESP32-S3 development board in the Read series with a 4.7" monochrome e-paper panel,
made by Shenzhen MindReset Technology Co., Ltd. for developers building open-source
e-paper firmware. This repository contains the factory firmware shipped with the board.

The firmware provides separate demo and diagnostic pages for the display, touch,
accelerometer, power, keys, TF card, fonts, sleep and wake. Use them to check the
hardware and as reference implementations for your own firmware.

The board support, PMU protocol host and chip drivers are independent components
that can be reused. This is a hardware demo, not a finished reader product.

Agent-facing layout, `app_desc_t` contract, glossary and comment style are in
[AGENTS.md](AGENTS.md).

## Documentation & More Devices

- [Official Read Pico documentation](https://dot.mindreset.tech/docs/read_0)
- [Dot Open Platform](https://github.com/MindReset/dot_open_platform): explore other Dot devices and projects, including Quote/0 hardware resources and Rand/0 local display integration, with firmware examples, pin maps, and enclosure files.

## Hardware

| Item | Specification |
| --- | --- |
| MCU | ESP32-S3, 16 MB flash and 8 MB Octal PSRAM, both at 120 MHz |
| Display | 4.7" monochrome e-paper, 1216 × 684, 16 gray levels, 16-bit parallel interface driven by the LCD peripheral |
| EPD power | SY7636A; PGOOD read through the IO expander |
| PMU | CW32L010 with a custom I2C protocol for battery, charging/discharging, indicator LED, RTC, alarms and power control |
| Touch | CST836U, two touch points, interrupt and deep-sleep wake |
| Accelerometer | SC7A20H, tap, orientation, free-fall and FIFO |
| IO expander | FCA9555, EPD control pins and card detection |
| Storage | TF card over 1-bit SDMMC; fonts loaded from the card |
| Other | Buzzer and three capacitive key zones |

## Build & Flash

Requires ESP-IDF v6.1. `components/read_pico/read_pico_flash_hpm.c` depends on
`esp_flash_chips/spi_flash_override.h`, which is available only in v6.

```sh
idf.py set-target esp32s3
idf.py build
idf.py -p /dev/cu.usbmodem* flash monitor
```

The 120 MHz flash / PSRAM timing in `sdkconfig.defaults` depends on the flash part
installed on the board. CI uses `sdkconfig.ci` with default timing only to check
compilation; see [.github/workflows/build.yml](docs/ci/upstream-build.yml).

If the device is not recognized after waking from sleep during development or
flashing:

1. Try a USB Type-A data cable.
2. Put the device to sleep and wake it again.
3. Reboot the board and retry.

The panel VCOM is calibrated at the factory and stored in the PMU. The firmware
reads it once at boot to configure the driver; it is neither stored locally nor
user-editable.

## Pages

The function menu lists pages in the order defined in
[main/app/app_registry.c](main/app/app_registry.c).

| Page | Content |
| --- | --- |
| Overview | Boot I2C scan, IDs, battery and charging status, build time |
| EPD Refresh | Full-screen GC16, partial DU, 16-gray and fast 8-gray ladders, each with measured refresh time |
| Reading | Built-in text, DU / GL16 / GC16 page turns, font size adjustment in the header |
| Touch | Two-point tracking with continuous DU, full-page settle on lift, deep sleep and automatic wake |
| Accel | Live three-axis readings and tilt, tap count, orientation |
| IMU Lab | Sampling parameters and self-test |
| Power | Battery voltage, level and charging status; EPD rails, temperature, faults and read-only SY7636A configuration |
| PMU | Protocol status, events, configuration and commands |
| Power Key | `key_raw_events`, press/release levels and DOWN/UP/SHORT/LONG events |
| Sleep | Light sleep with key or pickup wake, deep sleep and power off |
| Storage | TF card capacity and mount status, remount, format and buzzer |
| Font | Select TTF files from the card, preview typesetting and cycle font weight |
| IOE | Port-0 levels and interrupts, touch-reset pulse from the bottom bar |
| Device | Device probes and command ACKs; power-cut items are recorded in the backend only |
| Books | UTF-8 / GBK TXT and EPUB from TF or internal storage, chapters, font size and per-book progress; swipe to turn, hold and drag text to select and hold a shelf row for details, progress reset or confirmed deletion; filter storage sources, sort by name or recent reading, and search by pinyin, initials or English; single-book actions use a popup, while management supports batch selection, progress reset/deletion and rescan; experimental shake-to-turn defaults off. EPUB navigation supports NCX and nav documents. |
| Transfer | Device hotspot or existing WiFi, with browser TXT/EPUB upload and complete TTF font uploads to TF; TF card preferred, internal storage limited to 1 MB per file. Scan the hotspot QR to join, or select a 2.4 GHz network and enter its password on the touchscreen. Web provisioning remains available. The browser lists and searches books in the current upload destination, confirms replacement or deletion, and supports upload cancellation and retry. Saved WiFi can be forgotten on the device. Leaving the page stops networking. |

In the original Books demo, KEY1 / KEY2 / KEY3 select previous page / toolbar / next page. The toolbar includes full refresh. Hold KEY2 for 500 ms to open the demo menu. Other demo pages retain KEY2 full GC16 refresh and KEY3 menu. Menu rows select on release; slide away to cancel.

## Repository Layout

```text
main/
  app_main.c        Boot wiring, then app_loop
  app/              App interface (app.h), registry and event loop
  apps/             One demo per file, exporting only app_desc_t
  ui/               ui_kit drawing primitives and layout constants; ui_menu two-level menu
  font/             stb_truetype glyph cache
  factory/          Device self-test and factory VCOM calibration
components/
  read_pico/        Board BSP: I2C, EPD definition/timing, TF card, buzzer, flash HPM
  read_pico_pmu/    CW32L010 protocol host
  epdiy/            E-paper renderer, trimmed to the LCD peripheral path
  continuous_du/    Continuous DU with phases accumulated across scans for finger tracking
  cst836u/ sc7a20h/ fca9555/ sy7636a/    Chip drivers
  e0470_epaper_waveform/                 Panel waveform tables and trimming functions
  pwm_audio/        LEDC PWM audio, one buzzer backend
assets/             Image sources for main/assets/*.bin
tools/              Font and image conversion scripts
```

To add a demo page, create a file in `main/apps/`, implement the `app_desc_t`
callbacks you need and add it to the menu table in `main/app/app_registry.c`.
The main loop stays untouched.

## Pinout

| Function | GPIO |
| --- | --- |
| I2C SCL / SDA | 40 / 39 (400 kHz) |
| EPD data D0–D15 | 4–18, 45 |
| EPD XLE / XSTL / XCL / SPV / CKV | 3 / 46 / 21 / 47 / 48 |
| FCA9555 INT# | 41 (also a light-sleep wake source) |
| CST836U INT# | 43 |
| SC7A20H INT1 | 1 |
| TF card CLK / CMD / D0 | 38 / 42 / 44 |
| Buzzer | 2 |

EPD power enable, XOE, MODE, VCOM_EN, touch reset and card detection are on FCA9555
Port-0; see the pin table in [main/apps/app_ioe.c](main/apps/app_ioe.c).

## Wi-Fi and file transfer in the native system

Lingxi Board USB updates use the debug connection directly when the reader advertises direct-transfer support; the TF card stays available to the reader. Automatic updates save in the background. A manual send can request immediate display after the current touch operation ends, while an active lock remains intact. File management still uses the separate, safely ejected TF-disk handoff described below.

The native system is **XH View OS For Pico 0.39.1-dev69**, an **early preview test build** (先行预览测试版). System Settings groups functions by purpose:

| Group | Functions |
| --- | --- |
| Storage & Transfer | Opens the transfer hub directly: Same Wi-Fi, Device Hotspot, USB Disk, TF Card & Notes and Open Local Books |
| Network & Connections | Wi-Fi, Phone & Computer Management, Bluetooth Pager |
| Reading & Input | Reading Layout, Note Input |
| Appearance & Controls | Home Navigation, Quick Menu, Sounds |
| Apps & Services | App Launcher, Home Assistant, 3D Printers, Board Games |
| System & Power | Power & Lock Screen, Date & Time, Keep Awake |

Tap the system information card at the top to open About, which shows the full system name, preview label and version, and contains the First-use Guide. Child settings return to their group, then the settings home and previous page.

The static startup page shows the same system name, version and preview label using the panel's 16-level grayscale.

System Settings → System & Power → Keep Awake defaults to on and remembers your choice. While enabled, it pauses idle standby and the automatic landscape/charging dock standby; manual sleep from the power key or shortcut remains available. Turn it off to restore automatic standby.

The four offline board-game opponents calculate in a background task with an external-memory stack. If resources are unavailable, choose Retry opponent to try again while keeping the current game and difficulty.

Choose covers, illustrated list, compact list, gallery or focus from the shelf filter bar; the choice is remembered, and compact mode does not decode covers. Cover view keeps continuation and six books on page one, then hides continuation and shows nine books in a 3×3 grid, with continuous pagination and matching hits in both orientations. Shelf paging compares frames and updates only the changed region; orientation changes, overlay recovery and explicit cleaning retain full refresh paths. Physical flashing and ghosting still require device testing.

Open System Settings → Network & Connections → Bluetooth Pager. Enable it, scan and select a standard BLE HID pager; pairing memory, reconnect, disconnect, forget and reverse controls are available. Keys operate reading pages, with forward on the opening cover entering the first text page; dialogs, selection, tools, transfers and standby remain guarded. Classic Bluetooth is unsupported. Actual pager key mappings, compatibility and power consumption require hardware testing.

Shelf refresh keeps the current contents visible until scanning and visible cover preparation finish, then publishes one result. Both corner menus share fourteen direct actions in 3/5/6 rings. Open the layout editor to drag actions between their actual positions, tap to replace or remove, and undo changes. Save explicitly to update both corners; Back or Cancel preserves the previous layout. Previously enabled actions that do not fit remain available to assign.

Open System Settings → Network & Connections → Wi-Fi, enable Wi-Fi, select a nearby 2.4 GHz network and enter its password. A successful connection is saved, with up to eight remembered networks. The arrow on a network row opens its details, automatic connection setting and confirmed forget action. Wi-Fi stays connected after leaving settings, and setup does not require a storage card. Enterprise authentication and captive-portal networks are not supported.

Open System Settings → Storage & Transfer and choose Same Wi-Fi, Device Hotspot or USB Disk. For the same-network option, connect the phone and reader to the same network, then scan the webpage QR code. For the hotspot, scan the connection code first, stay connected, then switch to the webpage code. The webpage requires no app and accepts multiple TXT/EPUB books or TTF fonts for the TF card, with a result for each file. After a network interruption or timeout, the current selection is retained and reconnecting retries only unfinished files. Replacing a file with the same name still requires confirmation.

USB requires a data cable and an available TF card. When enabled, the computer sees a TF-card disk: put TXT/EPUB books in `books/` and TTF fonts in `fonts/`. Reading, card fonts and wireless transfer pause while USB owns the card; the normal debug serial port also temporarily disappears. Eject the disk on the computer first. The device then stops USB, remounts and checks the card before restoring local access. If the computer sends no eject notification, choose Safe End USB Transfer on the device and confirm that the disk has been ejected on the computer. A failed stop keeps access paused and offers retry, without automatic formatting; unplugging the cable does not replace ejecting the disk. USB exposes only the TF card, not internal storage.

Open System Settings → Storage & Transfer → TF Card & Notes (TF 卡与笔记) to reach TF Card & Storage, which shows the card, filesystem, capacity and check results. It offers remounting or erasing after two confirmations. Formatting deletes every partition and file on the entire TF card, creates one FAT32 partition, remounts it and verifies actual file reads and writes. This firmware requires explicit formatting to FAT32 before using an exFAT card. It never clears the card automatically or erases internal storage. Keep the device powered and the card inserted during maintenance.

Finish, Open Library first asks to end transfer, waits for the receiver to exit, then returns to the native library. The top back button, global back and quick gestures also ask for confirmation before leaving an active transfer page. The native library reads TXT/EPUB files directly in `books/` and the TF-card root; the firmware contains no bundled sample books or comics. Other folders are not scanned recursively. When a book is opened from this library, the middle key goes back on a single press, opens the root switcher on a double press and returns to the shelf on hold; leaving reading saves progress; the outer keys turn pages. Transfer temporarily uses the built-in font and restores the selected card font afterward. If storage is unavailable, open TF Card & Storage to check it; the device does not automatically format it or switch to another storage source.

Real local books share the corner shortcut fans and global Back; horizontal body swipes still turn pages. Hold a character and drag in either direction to extend the selection; release to highlight or annotate, and drag the circular endpoint handles to adjust it. Tapping outside a selection, reading tool, page-entry or image-confirmation panel closes only the overlay, without turning pages or saving an annotation. Records separate notes, highlights and bookmarks; a book’s entry filters its own records and supports returning to text, removal and export. Saved highlights enter sleep excerpts automatically. Annotations reuse the pinyin QWERTY/T9, English, number/symbol and ink input methods. Anchors bind path, source size, chapter and parsed UTF-8 text ranges. Replacing a book with different content at the same path and size can still misidentify anchors.

TF Card & Storage shows the actual note destination: `/flash/books/.pico-system` when internal storage is available, otherwise `.pico-system` under the TF book root. Existing stores are never silently moved or merged. Up to 256 records are supported, with 383 UTF-8 bytes of text per note; the editor shows the limit. Compact two-generation chunked commits can read earlier fixed-size chunks and legacy records. A changed draft is saved after approximately six idle seconds, deferred during composition or touch; a draft write failure does not independently block an explicit note save. Only a Note saved message confirms persistence. On failure, editing contents remain in this session; check the message and retry before powering off. See [Note input](docs/INPUT_METHOD.md) for the optional expanded pinyin dictionary.

The unchanged-height reading header separates chapter title and page/percentage into two rows. Long titles use an ellipsis while numeric values remain complete, without reducing the body area. Tap anywhere in the top reading progress area, including the track, page label and blank space, to enter a whole-book page using a roomy numeric keypad. Hold this area to open reading tools for font size, text selection, highlights and notes. Holding and dragging the body text still selects text. Numeric entry stays idle until confirmation, then counts the pages needed for the target chapter by chapter for the current font, size and orientation; progress uses local updates and cancellation preserves the reading location. Layout changes require recalculation; annotations retain their source-byte anchors. A track tap never seeks directly to the touch position. Deliberate horizontal dragging previews a chapter and approximate source-byte percentage, then seeks once on release; the dedicated Return to previous position button restores the origin. The reading shortcut card and page dialog both open the contents, marking the current chapter and allowing chapter selection. Controls follow the product orientation; physical keys and horizontal page swipes remain available.

WiFi settings can enable and scan with no saved network. Scan requests survive radio startup or temporary busy states, with separate scan and connection errors. Add Network is also available while off: enter the name, password or open security, then connect to enable WiFi automatically. A network is remembered only after a successful connection.

The original hardware demo pages remain available. The transfer demo's stop-networking-on-exit behavior does not apply to the native system's everyday Wi-Fi connection.

## Transfer and limitations

AP and existing WiFi modes provide a QR code for the upload page. AP can switch between joining WiFi and opening the page. The device labels its build timestamp as UTC.

The transfer server runs only on its page and stops on exit. It uses local-network HTTP without separate login or TLS; peers on that network can manage books in the active upload storage, so use a trusted network. Saved WiFi credentials reside in device NVS and are not returned by public endpoints or logs. NVS/flash encryption is not enabled, so this does not provide physical-access protection.

Stopping transfer returns to the entry page or menu position. When a mounted TF card becomes unavailable, affected reading/transfer stops and fonts fall back; explicitly remount from the TF page after reinserting it. Removing a card during writes can damage the filesystem. EPUB automatically loads local JPEG/PNG illustrations before presenting the current page. Images fit the body proportionally and move whole across page breaks, without stretching or cropping. Probed dimensions keep pagination stable across decoding and cache eviction. A declared independent cover appears on first opening without saved progress; tap or turn forward to enter text. Resuming skips the cover, and turning backward at the first text page revisits it. The cover is outside body page numbering, preserving chapter and annotation byte anchors. Tapping an inline illustration asks before opening its full-screen preview. Continue reading keeps the original page; only explicit confirmation loads the larger image. Swiping across an inline image still turns text pages, and the confirmation dialog blocks paging and stale touches. Drawing and the background prepaint worker never read files or decode images. ZIP entries, manifest items and spine chapters are each limited to 32768; OPF/navigation metadata is limited to 4 MiB uncompressed and chapter/image reads to 2 MiB. The ZIP central directory has an 8 MiB limit; title storage has a 1 MiB budget, with numbered fallback titles beyond it. Entries above 4 MiB are rejected even when unused. These are independent memory/resource limits, so chapter count alone does not guarantee acceptance; ZIP64 and books over 32768 entries or chapters are unsupported. Externally replaced files or another card with the same path and file size may still match old reading progress. Pending retries after failed saves are not guaranteed to survive power loss.

Entering Books offers to resume the previous book; staying on the shelf does not open it (KEY1 cancels, KEY3 resumes). The reader parses metadata and the current chapter, then paginates the first two pages or through the saved position. Remaining pagination advances two pages at a time; other chapters load on demand. The page total shows “…” while incomplete; entering the previous chapter at its last page still requires paginating that chapter. Width measurement uses resident font tables without rasterizing a whole chapter. Opening and chapter loading retain waiting hints and visible failure reasons (memory, limits, unsupported format or invalid file). A selected local book first presents a static loading screen with its filename; font/file reads and parsing start on a later loop tick.

Swipe page turns commit on release at 64 pixels (previously 120); movement beyond the 24-pixel tap tolerance but below the swipe threshold cancels, and swiping over illustrations still turns pages without opening the separate image preview.

Illustration limits: baseline JPEG favors decode-time scaling with at most 2x enlargement to the display size, up to 16M source pixels and 8192 per side; PNG and progressive JPEG allow up to 1M source pixels and a 4 MiB decoder heap. Output fits 648x1000 grayscale pixels; transparent PNG uses a white background. Inline caching is limited to 1,296,000 bytes and 64 images, evicting off-page entries first and releasing them on chapter exit. A separate preview retains one viewed image for reopening. Missing, corrupt, oversized or unsupported images retain an unavailable placeholder in the text; tap to retry and see the reason, without blocking reading or changing established pagination. Earliest visited-section hints for repeated references appear only in the separate preview. Images leading directly into an opening heading use the title-image preview label instead. This history covers only chapters visited during the current book opening, resets on closing, and makes no claims about unread chapters or the first occurrence in the whole book. Identical content under different resource paths is not matched. Local JPEG/PNG references inside SVG wrappers work; pure SVG vectors, CSS backgrounds and remote images do not. Plain TXT files do not receive an invented cover. See [decoder sources and licenses](main/book/vendor/README.md).

The default complete font is the original MiSans variable font, provided as part of this software in `sdcard/fonts/MiSansVF.ttf`. Copy the package’s `fonts/` directory, including its license files, to the TF card root. The firmware reads it on demand from `/sdcard/fonts/MiSansVF.ttf`, supports variable weight, and selects it by default; an explicit saved font choice is retained. If the card or file is unavailable, the UI falls back to the built-in subset, which covers UI/demo text and cannot cover an arbitrary book. Other complete TrueType fonts can be selected on the Fonts page. The webpage accepts TTF files into `/sdcard/fonts`, up to 32 MiB each, with TrueType outlines (not OTF/CFF, TTC or WOFF). Capacity is checked, replacements require confirmation, and failed uploads retain the old file. Transfer temporarily uses the built-in font and restores the selected card font afterward. See [TF deployment and font licenses](sdcard/README.md).

Note input supports pinyin QWERTY/T9, English and numbers/symbols. Handwriting text input has been removed; existing ink attachments remain readable. The independent Drawing Notes app rotates the whole dotted paper with the screen; its landscape tools move to the left, preserving a large writing area without cropping or stretching existing originals. Erasing restores the dots, and the outer hardware keys undo and save.

See [Changelog](docs/CHANGELOG.md) for feature changes. Offline pinyin data comes from pypinyin under MIT; see the [component license and regeneration notes](components/read_pico_search/README.md).

## Acknowledgments & License

- Firmware: Apache-2.0, see [LICENSE](LICENSE).
- [epdiy](https://github.com/vroland/epdiy): e-paper timing and rendering. This is a
  trimmed fork for the board's LCD path, licensed under LGPL-3.0-or-later.
  Local changes are listed in [components/epdiy/LICENSE](components/epdiy/LICENSE).
- Uses MiSans. The default full TF-card font is the unmodified official MiSans variable font, bundled as a software component under the [MiSans license](licenses/misans/LICENSE.zh-CN.txt), with the [original agreement](licenses/misans/MiSans-License.pdf). See [deployment notes](sdcard/README.md).
- [stb_truetype](https://github.com/nothings/stb): glyph rasterization, public domain.
- [pwm_audio](https://github.com/espressif/esp-iot-solution/tree/master/components/audio/pwm_audio):
  Espressif LEDC PWM audio, trimmed copy, Apache-2.0. See
  [components/pwm_audio/LICENSE](components/pwm_audio/LICENSE).
- Panel waveform tables ship with the board as-is under Apache-2.0. See
  [components/e0470_epaper_waveform/LICENSE](components/e0470_epaper_waveform/LICENSE).
- The built-in font, `main/assets/builtin.ttf`, is a subset of the
  [ChillDuanSans](https://github.com/Warren2060/ChillDuanSans) variable font by
  Warren2060, generated by `tools/gen_builtin_font.py`. The unmodified full font
  and original license are bundled in `sdcard/fonts/`; the modified UI subset
  is named Read Pico UI. Both remain under SIL OFL-1.1. See [deployment notes](sdcard/README.md).

Thank you for your patience and support.

Shenzhen MindReset Technology Co., Ltd.

Enable “Clock with highlights” in Sleep & Orientation to show cached excerpt previews and sources alongside the flip clock. It defaults off and reuses the nine-item random queue, refresh interval and low-power pause. Ellipses mark long previews; complete text remains in excerpts and records. The original large clock layout remains when disabled.

Games are independent `.pga` applications on the TF card at `/sdcard/apps/games/`. Rules, AI and game UI load from the package; removing a package retains saves. See the [TF app package specification](docs/TF-APPS.md) for the ABI, builds, migration and distribution catalog.

Power actions, four lock faces and offline date/time setup are described in [Power, lock and time](docs/POWER-AND-TIME.md).

See [Home and printer connections](docs/HOME-LINK.md) for real Home Assistant, LAN/cloud printer setup and credentials. Unconfigured services stay empty; validate against your own accounts and devices.

Detailed HA, Bambu Lab and Formlabs Dashboard setup opens on the device's web page: connect to Wi-Fi, choose Phone Setup, scan from a phone on the same network, then save and verify. The same page supports standard HA entities, room assignments and controls. The timed management window closes when you leave it on the device; credentials stay outside the TF card. See the [management API](docs/HOME-LINK-WEB.md).

Bambu cloud uses one shared phone-management account card for regional password/code login and bound-printer selection across slots. Continue from a saved account without entering its password; the device revalidates saved credentials before showing printers. Fresh-login import updates same-account cloud-slot tokens in one transaction; reuse preserves other tokens. Passwords are never persisted, and the shared RAM list expires after five minutes or when management stops. Existing-token setup remains advanced; authenticator challenges can explicitly switch to code login. Website challenges/service blocks are reported without bypass; rejected tokens require reauthentication.

Secure cloud connections use the device's date/time. Wi-Fi automatically synchronizes time and saves it to the RTC; valid RTC time is restored after startup and manual setup; if unset, open Date & Time on the device before retrying. Device and web messages distinguish unset time from network and TLS/certificate failures. See [connection guidance](docs/HOME-LINK.md).

WeRead runs as an independent TF app with on-device QR authorization, online book/author/keyword search, metadata and covers, chapters, account progress, recommendations, reading statistics, popular highlights and public reviews. Personal shelf/highlight/thought sync remains available offline. A phone opens the Pico LAN setup page or scans an official book-reading link; no Mac is required. Full book bodies are not downloaded or read natively. See [TF-WEREAD](docs/TF-WEREAD.md).

# lingxi-readpico-private

Private companion **slices** for **小纸 Pico / 多屏灵犀** firmware.

This repository holds proprietary / unreleased source slices mirrored from a local dirty firmware tree that is based on the public upstream:

- **Public baseline:** [MindReset/read_pico_firmware](https://github.com/MindReset/read_pico_firmware) `@ 6a247cc` (`feat/native-demo-0.16`)
- **Local PROJECT_VER:** `0.39.0-dev64`

## Important

- These paths are **not** a full firmware tree. They are selected proprietary slices for private development.
- **Do not merge blindly** into the public `MindReset/read_pico_firmware` origin.
- Contact **XHFO** for access or coordination.
- Never commit secrets, API keys, tokens, Wi‑Fi passwords, `HANDOFF.local.md`, or `docs/local/`.

## Layout

```
read_pico_firmware_slices/   # mirrored relative paths from the local firmware tree
  components/
  main/system/
  apps/
  games/
  tools/
  docs/
  assets/tf-pack/
  licenses/
```

See [MANIFEST.md](./MANIFEST.md) / [SOURCE-MAP.md](./SOURCE-MAP.md) for the exact pin, copied paths, and deferred large files.

## Deferred (Git LFS later)

Large binaries / generated tables are **not** in this first batch:

| Path | Approx size | Reason |
|------|-------------|--------|
| `sdcard/fonts/MiSansVF.ttf` | ~20 MB | huge font; LFS later |
| `assets/tf-pack/**/pinyin*.bin` (>500 KB) | ~1.1 MB | skipped this batch |
| `components/read_pico_search/search_table.h` | ~864 KB | huge generated table |
| `main/system/pico_clock_digits.h` | ~648 KB | huge generated header |

## First batch contents (summary)

- **A)** Home Link / Bambu / HA — `components/read_pico_home_link/`, docs, license, related tools + `main/system` home/desktop UI
- **B)** Unreleased manage / connectivity — selected `components/read_pico_transfer/` manage & connectivity sources + tools
- **C)** TF apps / games / PGA — `apps/`, `games/`, `components/read_pico_apps/`, TF docs, build/test tools, small `assets/tf-pack/` files

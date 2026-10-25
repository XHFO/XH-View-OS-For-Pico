# MANIFEST / SOURCE-MAP

## Pin

- **Public upstream:** MindReset/read_pico_firmware
- **Baseline commit:** `6a247cc` (`feat/native-demo-0.16` — improve ISR handling for audio playback during screen refresh)
- **Local PROJECT_VER:** `0.39.0-dev66` (`CMakeLists.txt` `set(PROJECT_VER ...)`)
- **Source root (local, not pushed):** `.../work/read_pico_firmware`
- **Staging clone:** `.../work/lingxi-readpico-private-staging` → origin `XHFO/lingxi-readpico-private` (private only)
- **Import date (Asia/Shanghai):** 2026-10-26


## Stretch (dev66)

- Thin host + `pico_home_pga` bridge + `apps/tools/home-link.pga` **v10200**.
- `home_link_mode=pga` when that package validates; phone manage home/printer/bambu still 503 `home_module_missing`.
- On-device HA config, CF Access (external HTTPS only), entity control, and HA printer-prefix watch.
- Bambu LAN/cloud MQTT and account login stay `RP_HOME_UNSUPPORTED` (no fake printer online).
- See `read_pico_firmware_slices/docs/HOME-LINK-PGA-V1.md`.

## Copied paths (relative under `read_pico_firmware_slices/`)

- `CMakeLists.txt` (1346)
- `apps/include/weread_explore.h` (38850)
- `apps/include/weread_explore_json.h` (9888)
- `apps/include/weread_snapshot.h` (2280)
- `apps/src/drawing_notes.c` (17030)
- `apps/src/home_link.c` (21832)
- `apps/src/weread.c` (23829)
- `assets/tf-pack/readpico/ime/NOTICE.txt` (604)
- `assets/tf-pack/readpico/ime/licenses/LICENSE.jieba` (1075)
- `assets/tf-pack/readpico/ime/licenses/LICENSE.pypinyin` (1105)
- `assets/tf-pack/readpico/ime/pinyin-v1.json` (349)
- `build-artifacts/dev65-install.json` (1436)
- `build-artifacts/home-link-catalog.json` (857)
- `build-artifacts/home-link.pga` (35372)
- `components/read_pico_apps/CMakeLists.txt` (129)
- `components/read_pico_apps/include/pico_app_loader.h` (1391)
- `components/read_pico_apps/include/pico_app_package.h` (1427)
- `components/read_pico_apps/include/pico_app_sdk.h` (9792)
- `components/read_pico_apps/pico_app_loader.c` (18592)
- `components/read_pico_apps/pico_app_package.c` (7123)
- `components/read_pico_home_link/CMakeLists.txt` (1293)
- `components/read_pico_home_link/bambu_auth.c` (13422)
- `components/read_pico_home_link/bambu_auth_internal.h` (1050)
- `components/read_pico_home_link/home_link_json.c` (730)
- `components/read_pico_home_link/home_link_json.h` (4798)
- `components/read_pico_home_link/home_link_model.c` (46328)
- `components/read_pico_home_link/home_link_stubs.c` (3072)
- `components/read_pico_home_link/home_link_web.c` (28704)
- `components/read_pico_home_link/include/home_link_web.h` (3020)
- `components/read_pico_home_link/include/read_pico_home_link.h` (11557)
- `components/read_pico_home_link/read_pico_home_link.c` (46780)
- `components/read_pico_transfer/compress_management.py` (260)
- `components/read_pico_transfer/connectivity_profiles.h` (4138)
- `components/read_pico_transfer/include/read_pico_connectivity.h` (6648)
- `components/read_pico_transfer/include/read_pico_content.h` (1559)
- `components/read_pico_transfer/include/read_pico_management.h` (2721)
- `components/read_pico_transfer/manage.html` (124254)
- `components/read_pico_transfer/read_pico_connectivity.c` (36622)
- `components/read_pico_transfer/read_pico_content.c` (18321)
- `components/read_pico_transfer/read_pico_management.c` (19538)
- `components/read_pico_transfer/wifi_memory_config.h` (1623)
- `docs/HOME-LINK-CONTRACT.md` (7050)
- `docs/HOME-LINK-PGA-V1.md` (5523)
- `docs/HOME-LINK-WEB.md` (23512)
- `docs/HOME-LINK.md` (24329)
- `docs/LAUNCHER.md` (4668)
- `docs/TF-APPS.md` (16783)
- `docs/TF-WEREAD.md` (11224)
- `games/sdk/pico_game_module_compat.h` (3648)
- `games/src/builtin_games.c` (72059)
- `licenses/ha-bambulab-MIT.txt` (1081)
- `main/CMakeLists.txt` (8052)
- `main/system/pico_connectivity.c` (43249)
- `main/system/pico_connectivity.h` (2004)
- `main/system/pico_desktop.c` (33897)
- `main/system/pico_desktop.h` (5038)
- `main/system/pico_games.c` (41305)
- `main/system/pico_home.c` (76764)
- `main/system/pico_home_link_ui.inc` (30153)
- `main/system/pico_home_link_ui_thin.inc` (2684)
- `main/system/pico_home_pga.c` (20700)
- `main/system/pico_home_pga.h` (1205)
- `main/system/pico_launcher.c` (5327)
- `main/system/pico_launcher.h` (1188)
- `main/system/pico_navigation.c` (12543)
- `main/system/pico_navigation.h` (3458)
- `main/system/pico_power.c` (25725)
- `tools/bambu_auth_host_test.c` (12534)
- `tools/bambu_auth_runtime_test.c` (16445)
- `tools/build_game_elf.py` (11415)
- `tools/build_game_packages.py` (10947)
- `tools/build_pico_ime_dictionary.py` (3912)
- `tools/build_tool_packages.py` (6706)
- `tools/connectivity_host_test.c` (27934)
- `tools/connectivity_profiles_host_test.c` (16676)
- `tools/connectivity_stubs/connectivity_host_env.h` (4030)
- `tools/connectivity_stubs/esp_err.h` (210)
- `tools/connectivity_stubs/nvs.h` (441)
- `tools/home_link_host_test.c` (28892)
- `tools/home_link_thin_host_test.c` (2156)
- `tools/home_link_web_host_test.c` (23861)
- `tools/manage_web_frontend_test.js` (64104)
- `tools/manage_web_readback_test.js` (22204)
- `tools/manage_weread_explore_test.js` (20258)
- `tools/manage_weread_web_test.js` (16054)
- `tools/pico_app_package_host_test.c` (6672)
- `tools/pico_connectivity_host_test.c` (27198)
- `tools/pico_connectivity_render.c` (10322)
- `tools/pico_desktop_host_test.c` (55341)
- `tools/pico_fan_host_test.c` (5127)
- `tools/pico_fan_paint_host_test.c` (23086)
- `tools/pico_games_render.c` (3963)
- `tools/pico_home_host_test.c` (34153)
- `tools/pico_home_render.c` (3910)
- `tools/pico_launcher_host_test.c` (6344)
- `tools/pico_management_http_host_test.c` (30130)
- `tools/pico_navigation_host_test.c` (7416)
- `tools/pico_navigation_render.c` (1267)
- `tools/test_bambu_auth.py` (2511)
- `tools/test_connectivity.py` (1385)
- `tools/test_connectivity_profiles.py` (1740)
- `tools/test_game_packages.py` (9793)
- `tools/test_home_link.py` (8276)
- `tools/test_home_link_thin.py` (1141)
- `tools/test_home_link_tls.py` (7667)
- `tools/test_home_link_web.py` (976)
- `tools/test_native_management.py` (10021)
- `tools/test_pico_app_origin.py` (2853)
- `tools/test_pico_app_packages.py` (1287)
- `tools/test_pico_games.c` (7841)
- `tools/test_pico_games_ui.c` (23670)
- `tools/test_pico_launcher.py` (938)
- `tools/test_wifi_transfer_memory.py` (19741)

## Skipped / deferred

- SKIP-DEFERRED-HUGE: components/read_pico_search/search_table.h (884144 bytes) [deferred; LFS later]
- SKIP-DEFERRED-HUGE: main/system/pico_clock_digits.h (661191 bytes) [deferred; LFS later]
- SKIP-DEFERRED-HUGE: sdcard/fonts/MiSansVF.ttf (20093424 bytes) [not in this batch; LFS later]
- SKIP-TFPACK-GT500KB: assets/tf-pack/readpico/ime/pinyin-v1.bin (1096619 bytes)

## Notes

- Paths preserve the firmware tree layout under `read_pico_firmware_slices/`.
- Matching `main/system/pico_{home,desktop,connectivity,games,launcher,navigation}*` sources were included alongside tools because they implement the proprietary UI/features for batches A–C.
- `tools/pico_games_test/` (host test build dir) was not copied.
- No remotes were added to the public MindReset firmware checkout; this private repo is a separate clone.

## 2026-10-26 contract freeze

- Added `read_pico_firmware_slices/docs/HOME-LINK-CONTRACT.md` (thin-host manage 503 `home_module_missing`, CF Access JSON, `home_link_mode`, PGA kind 130).
- Cloudflare Access write-only `cf_access_client_id` / `cf_access_client_secret`; GET `has_*` only.
- PGA `home-link.pga` **v10200** is the on-device HA path (`home_link_mode=pga`). Phone manage stays 503. Independent MQTT is unsupported.

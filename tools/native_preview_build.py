#!/usr/bin/env python3
"""中文：将当前固件原生控制器与真实字体编译成本地主机预览，不构建或连接设备。
English: Build current native controllers and real variable fonts as a local host preview, without a device build or connection.
"""
from pathlib import Path
import argparse, json, hashlib, os, re, subprocess, sys
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
from render_pico_reader import prepare_native_renderer,extract_function
from build_game_packages import build_packages
from build_tool_packages import build_packages as build_tool_packages
from build_game_elf import build as build_game_elf

def filtered_header(source, remove=()):
    return '\n'.join(line for line in source.splitlines() if not any(line.strip()==f'#include "{name}"' for name in remove))+'\n'

def prepare(output, device_packages=False, native_only=False):
    if device_packages and native_only:
        raise ValueError('Choose either device-and-native packages or explicit native-only packages')
    output.mkdir(parents=True,exist_ok=True);stubs=output/'stubs';stubs.mkdir(exist_ok=True)
    subprocess.run([sys.executable,str(ROOT/'tools/build_home_portal_asset.py'),'--check'],cwd=ROOT,check=True)
    # 中文：游戏始终从TF包独立加载，不能静态链接规则实现充当预览。
    # English: Games load from independent TF packages, never statically linked rule substitutes.
    game_packages=output/'game-packages'
    tool_packages=output/'tool-packages'
    tool_report=build_tool_packages(tool_packages,device=device_packages)
    game_elf=output/'device-game-elf' if device_packages else ROOT/'build/game-elf'
    if device_packages:
        # 显式构建双平台分发包；编译不访问硬件。/ Explicit dual-platform distribution packages; compilation never accesses hardware.
        build_game_elf(game_elf)
    if native_only:
        game_report=build_packages(game_packages,native_only=True)
    elif (game_elf/'manifest.json').is_file():
        game_report=build_packages(game_packages,elf_dir=game_elf)
    elif any(game_elf.glob('*.elf')):
        raise ValueError('Device ELF files have no verified manifest; rebuild with tools/build_game_elf.py')
    else:
        game_report=build_packages(game_packages,native_only=True)
        print('游戏包仅供电脑预览，不能复制到阅读器；先运行 tools/build_game_elf.py 可生成双平台包。',file=sys.stderr)
    def put(name,text):
        p=stubs/name;p.parent.mkdir(parents=True,exist_ok=True);p.write_text(text)
    def cp(name,source,remove=()):put(name,filtered_header((ROOT/source).read_text(),remove))
    put('esp_err.h','#pragma once\ntypedef int esp_err_t;\n#define ESP_OK 0\n#define ESP_FAIL -1\n'+''.join(f'#define {name} {i+0x101}\n' for i,name in enumerate(['ESP_ERR_INVALID_CRC','ESP_ERR_NVS_NOT_FOUND','ESP_ERR_NVS_INVALID_LENGTH','ESP_ERR_NO_MEM','ESP_ERR_INVALID_ARG','ESP_ERR_INVALID_STATE','ESP_ERR_INVALID_SIZE','ESP_ERR_NOT_FOUND','ESP_ERR_NOT_SUPPORTED','ESP_ERR_TIMEOUT','ESP_ERR_NOT_FINISHED','ESP_ERR_INVALID_RESPONSE','ESP_ERR_CRC'])))
    for h in ['esp_heap_caps.h','esp_log.h','esp_timer.h','esp_random.h','freertos/FreeRTOS.h','freertos/task.h','freertos/idf_additions.h','freertos/semphr.h']:put(h,'#pragma once\n#include "native_preview_hal.h"\n')
    for h in ['esp_http_client.h','esp_http_server.h','esp_crt_bundle.h','esp_random.h']:
        put(h,'#pragma once\n#include "native_home_http_stub.h"\n')
    put('miniz.h',(ROOT/'tools/zip_host_stubs/miniz.h').read_text()+'\n#define MZ_CRC32_INIT 0\n#define mz_crc32(crc,data,bytes) crc32((crc),(const Bytef*)(data),(uInt)(bytes))\n')
    put('esp_log.h','#pragma once\n#include "native_preview_hal.h"\ntypedef int esp_log_level_t;\n#define ESP_LOG_WARN 2\nint esp_log_level_get(const char*);void esp_log_level_set(const char*,int);\n')
    cp('epdiy.h','tools/pico_reader_stubs/epdiy.h')
    actual_epd=(ROOT/'components/epdiy/src/epdiy.h').read_text()
    epd_stub=(stubs/'epdiy.h').read_text()
    epd_stub=re.sub(r'enum EpdFontFlags \{[^}]*\};',re.search(r'enum EpdFontFlags \{[^}]*\};',actual_epd,re.S).group(),epd_stub)
    (stubs/'epdiy.h').write_text(epd_stub)
    with (stubs/'epdiy.h').open('a') as f:f.write('''\n#include <stdbool.h>
enum EpdRotation {EPD_ROT_LANDSCAPE=0,EPD_ROT_PORTRAIT=1,EPD_ROT_INVERTED_LANDSCAPE=2,EPD_ROT_INVERTED_PORTRAIT=3};
enum EpdDrawError {EPD_DRAW_SUCCESS=0};
enum EpdDrawMode {MODE_INIT=0,MODE_DU=1,MODE_GC16=2,MODE_GL16=5};
typedef struct {uint8_t *front_fb,*back_fb;} EpdiyHighlevelState;
typedef struct {int unused;} EpdWaveform;
enum EpdRotation epd_get_rotation(void);void epd_set_rotation(enum EpdRotation rotation);
int epd_width(void);int epd_height(void);int epd_rotated_display_width(void);int epd_rotated_display_height(void);EpdRect epd_full_screen(void);void epd_hl_last_timing(int*,int*,int*);
void epd_poweron(void);void epd_fill_triangle(int,int,int,int,int,int,uint8_t,uint8_t*);void epd_fill_circle(int,int,int,uint8_t,uint8_t*);void epd_draw_rotated_image(EpdRect,const uint8_t*,uint8_t*);void epd_draw_rect(EpdRect,uint8_t,uint8_t*);void epd_hl_set_all_white(EpdiyHighlevelState*);
''')
    put('epd_highlevel.h','#pragma once\n#include "epdiy.h"\n')
    cp('continuous_du.h','components/continuous_du/include/continuous_du.h')
    put('app_config.h','#pragma once\n#define APP_PAGE_REFRESH_MODE MODE_GL16\n#define APP_PAGE_FORCE_FULL 1\n#define APP_DYNAMIC_REFRESH_MODE MODE_DU\n')
    put('cst836u.h','#pragma once\n#include <stdbool.h>\n#include <stdint.h>\ntypedef void *cst836u_handle_t;typedef struct {bool touched;uint8_t count;uint16_t x,y,x2,y2;} cst836u_touch_t;typedef struct {int unused;} cst836u_info_t;\n')
    put('sc7a20h_lab.h','#pragma once\ntypedef void *sc7a20h_handle_t;typedef struct {int x_mg,y_mg,z_mg;} sc7a20h_sample_t;\n')
    cp('app.h','main/app/app.h')
    cp('ui_kit.h','main/ui/ui_kit.h')
    cp('ui_menu.h','main/ui/ui_menu.h')
    cp('ui_gesture.h','main/ui/ui_gesture.h')
    cp('ttf_font.h','main/font/ttf_font.h')
    cp('settings.h','tools/pico_reader_stubs/settings.h')
    cp('read_pico_sd.h','components/read_pico/include/read_pico_sd.h')
    cp('sdmmc_cmd.h','tools/pico_reader_stubs/sdmmc_cmd.h')
    for name in ['read_pico_connectivity.h','read_pico_transfer.h','read_pico_transfer_network.h','read_pico_management.h','read_pico_content.h']:cp(name,'components/read_pico_transfer/include/'+name)
    put('psa/crypto.h','#include "native_preview_psa.h"\n')
    put('mbedtls/sha256.h','#pragma once\n#include <stddef.h>\nint mbedtls_sha256(const unsigned char*,size_t,unsigned char[32],int);\n')
    cp('read_pico_usb.h','components/read_pico_usb/include/read_pico_usb.h')
    cp('read_pico_ble_pager.h','components/read_pico_ble_pager/include/read_pico_ble_pager.h')
    cp('read_pico_dual_ble.h','components/read_pico_ble_pager/include/read_pico_dual_ble.h')
    cp('nvs.h','tools/book_storage_stubs/nvs.h')
    put('e0470_epaper_waveform.h','#pragma once\n#include "epdiy.h"\nextern const EpdWaveform E0470_FOLLOW_WAVEFORM,E0470_WAVEFORM;\n')
    put('e0470_page_turn.h','''#pragma once
#include "epdiy.h"
#define E0470_TURN_FAST_TICK_US 11090
enum e0470_page_turn_direction {E0470_TURN_RTL=1,E0470_TURN_LTR=-1};
void e0470_page_turn_set_tick_us(int us);
enum EpdDrawError e0470_page_turn(EpdiyHighlevelState *hl,EpdRect area,enum e0470_page_turn_direction direction);
''')
    put('read_pico_init.h','#pragma once\n#include "app.h"\nesp_err_t read_pico_sensor_wake(sc7a20h_handle_t);esp_err_t read_pico_sensor_sleep(sc7a20h_handle_t);esp_err_t read_pico_accel_read(sc7a20h_handle_t,sc7a20h_sample_t*);\n')
    cp('read_pico_pmu_protocol.h','components/read_pico_pmu/include/read_pico_pmu_protocol.h')
    put('read_pico_pmu.h','#pragma once\n#include <stdint.h>\n#include <stdbool.h>\n#include "read_pico_pmu_protocol.h"\ntypedef struct {bool present,status_ok,time_ok,time_synced;int last_err;uint32_t flags;uint16_t last_op_status;uint16_t soc_permille;int charge_state;uint32_t unix_sec;} pmu_snapshot_t;bool read_pico_pmu_ready(void);int read_pico_pmu_cmd(uint16_t,const uint8_t*,size_t);int read_pico_pmu_refresh(void);const pmu_snapshot_t *read_pico_pmu_get(void);\n')
    cp('display.h','main/display.h',('read_pico_epd_timing.h',))
    put('read_pico_epd_timing.h','#pragma once\n#define READ_PICO_EPD_PCLK_MIN_MHZ 12\n#define READ_PICO_EPD_PCLK_MAX_MHZ 24\n')
    # 中文：真实字库链接符号，字体文件保持只读。/ English: Link real font bytes without modifying the originals.
    section='.section __TEXT,__const' if sys.platform=='darwin' else '.section .rodata'
    assembly=[section]
    for stem in ['builtin','serif','kai']:
        source=ROOT/'main/assets'/f'{stem}.ttf';assembly += [f'.global _binary_{stem}_ttf_start',f'_binary_{stem}_ttf_start:',f'.incbin "{source}"',f'.global _binary_{stem}_ttf_end',f'_binary_{stem}_ttf_end:']
    if sys.platform.startswith('linux'):assembly.append('.section .note.GNU-stack,"",@progbits')
    (output/'fonts.S').write_text('\n'.join(assembly)+'\n')
    # 中文：复用同源几何；字体和像素边界由本运行器承担。/ English: Reuse native geometry; this runtime owns only font and pixel platform boundaries.
    prepare_native_renderer(output)
    geometry=(output/'native-geometry.c').read_text();geometry=geometry[:geometry.index('static int clamp_radius')] if 'static int clamp_radius' in geometry else geometry
    geometry += '\n'+extract_function((ROOT/'components/epdiy/src/epdiy.c').read_text(),'epd_fill_triangle')+'\n'
    (output/'native-geometry.c').write_text(geometry)
    # 中文：区域判定直接编译生产函数，网页只替换屏幕推送边界。
    # English: Compile the production changed-area function; the browser replaces only display I/O.
    display_region=output/'native-display-region.c'
    display_region.write_text('#include "display.h"\n#include "e0470_epaper_waveform.h"\n#include <string.h>\n'+extract_function((ROOT/'main/display.c').read_text(),'update_display_changed_mode')+'\n')
    flags=['-DPICO_NATIVE_PREVIEW=1','-DPICO_HOME_LINK_FULL=0','-DpdTRUE=1','-DpdFALSE=0','-std=gnu11','-O1','-g','-Wall','-Wextra','-Wno-unused-parameter','-Wno-unused-function','-Wno-unused-variable','-I',str(stubs),'-I',str(output),'-I',str(ROOT/'tools'),'-I',str(ROOT/'main/system'),'-I',str(ROOT/'main/book'),'-I',str(ROOT/'main/font'),'-I',str(ROOT/'main/ui'),'-I',str(ROOT/'components/read_pico_apps/include'),'-DPICO_NATIVE_GAME_PACKAGES='+json.dumps(str(game_packages)),'-include',str(ROOT/'tools/native_preview_hal.h')]
    flags+=['-DPICO_NATIVE_IME_PACKAGE='+json.dumps(str(ROOT/'assets/tf-pack/readpico/ime/pinyin-v1.bin'))]
    flags+=['-DPICO_NATIVE_TOOL_PACKAGES='+json.dumps(str(tool_packages))]
    sources=[ROOT/'tools/native_preview_runtime.c',ROOT/'tools/native_preview_framebuffer.c',ROOT/'main/system/pico_draw.c',ROOT/'main/apps/app_font_pick.c',ROOT/'main/apps/app_transfer.c',ROOT/'main/system/pico_reader.c',ROOT/'main/system/pico_home.c',ROOT/'main/system/pico_games.c',ROOT/'main/font/ttf_font.c',ROOT/'main/ui/ui_kit.c',ROOT/'main/ui/ui_menu.c',ROOT/'main/ui/datamatrix.c',ROOT/'main/ui/ui_gesture.c',output/'native-geometry.c',output/'fonts.S']
    sources.append(display_region)
    sources += [ROOT/'components/read_pico_apps'/name for name in ['pico_app_package.c','pico_app_loader.c']]
    sources.append(ROOT/'components/read_pico_transfer/read_pico_content.c')
    flags+=['-Wno-deprecated-declarations']
    # 中文：与实体默认配置一致，编译薄宿主、PGA/PSRAM桥和局域网页面；仅替换ESP HTTP收发边界。
    # English: Match the default thin firmware, PGA/PSRAM bridge and LAN portal; replace only ESP HTTP I/O.
    home_link=ROOT/'components/read_pico_home_link'
    cjson=ROOT/'managed_components/espressif__cjson/cJSON'
    flags+=['-I',str(home_link/'include'),'-I',str(home_link),'-I',str(cjson)]
    sources += [home_link/name for name in ['home_link_model.c','home_link_json.c','home_link_stubs.c','home_link_portal.c','home_link_discovery.c']]
    sources += [ROOT/'main/system'/name for name in ['pico_home_pga.c','pico_home_portal.c','pico_i18n.c']]
    sources += [ROOT/'tools/native_home_http_stub.c',ROOT/'main/ui/ui_packed_asset.c']
    for extra in ['native_preview_services.c','native_preview_ble_pager.c','native_dual_ble.c','native_preview_management.c']:
        if (ROOT/'tools'/extra).exists():sources.append(ROOT/'tools'/extra)
    for name in ['pico_connectivity.c','pico_storage.c','pico_usb.c','pico_bluetooth.c','pico_datetime.c','pico_power.c','pico_sound.c','pico_navigation.c','pico_lock_covers.c','pico_ink.c','pico_desktop.c','pico_launcher.c','pico_weread.c']:sources.append(ROOT/'main/system'/name)
    sources.append(ROOT/'main/system/pico_weread_cover.c')
    sources += [ROOT/'main/system/dual_link.c',ROOT/'main/system/pico_dual.c']
    sources += [ROOT/'main/system/pico_board_data.c',ROOT/'main/system/pico_board_render.c',ROOT/'tools/native_board_sha256.c']
    # Optional book adapter contributes source files and generated HAL declarations.
    additions=ROOT/'tools/native_preview_book_build.py'
    if additions.exists():
        import importlib.util
        spec=importlib.util.spec_from_file_location('native_preview_book_build',additions);module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
        more=module.prepare(output,stubs);sources += [Path(p) for p in more]
    flags+=['-pthread']
    if sys.platform=='darwin':flags+=['-D_DARWIN_C_SOURCE=1']
    if sys.platform.startswith('linux'):flags+=['-ldl','-lcrypto']
    qr=ROOT/'managed_components/espressif__qrcode'
    flags+=['-I',str(qr/'include'),'-I',str(qr)]
    sources += [ROOT/'main/ui/ui_wifi_qr.c']+[qr/p for p in ['esp_qrcode_main.c','esp_qrcode_wrapper.c','qrcodegen.c']]
    binary=output/'native-preview'
    subprocess.run([os.environ.get('CC','cc')]+flags+[str(p) for p in sources]+['-lm','-lz','-o',str(binary)],cwd=ROOT,check=True)
    bound=set(p for p in sources if p.is_file())
    bound.update(ROOT/'tools'/name for name in ['native_preview_build.py','native_preview_hal.h'])
    for folder in ['main/system','main/book','main/ui','main/font']:
        bound.update(p for p in (ROOT/folder).rglob('*') if p.suffix in {'.h','.c','.inc'})
    bound.update(p for p in (ROOT/'tools').glob('native_preview*') if p.is_file())
    bound.update(p for p in stubs.rglob('*') if p.is_file())
    # 中文：记录生成阅读页include的原始输入和产物，防止生产页面变化而宿主仍显示旧版。
    # English: Bind generated book includes to their actual originals and generated evidence.
    bound.update(ROOT/name for name in ['main/apps/app_book.c','main/apps/app_book.h','main/app/app_registry.h','main/settings.h','components/read_pico_search/include/read_pico_search.h'])
    bound.update(p for p in output.glob('native-preview-book-*') if p.is_file())
    bound.add(ROOT/'components/epdiy/src/epdiy.h')
    bound.add(ROOT/'components/epdiy/src/epdiy.c')
    bound.add(ROOT/'main/display.c')
    bound.add(ROOT/'main/system/pico_home_link_ui.inc')
    bound.add(ROOT/'main/system/pico_ime_dictionary.inc')
    bound.add(ROOT/'assets/tf-pack/readpico/ime/pinyin-v1.bin')
    bound.add(ROOT/'components/continuous_du/include/continuous_du.h')
    bound.update(ROOT/'tools'/name for name in ['native_home_http_stub.c','native_home_http_stub.h','zip_host_stubs/miniz.h'])
    bound.add(ROOT/'components/read_pico_ble_pager/include/read_pico_ble_pager.h')
    bound.add(ROOT/'components/read_pico_ble_pager/include/read_pico_dual_ble.h')
    bound.add(ROOT/'components/read_pico_pmu/include/read_pico_pmu_protocol.h')
    bound.update(p for p in (ROOT/'components/read_pico_apps').rglob('*') if p.suffix in {'.h','.c'})
    bound.update(p for p in home_link.rglob('*') if p.suffix in {'.h','.c','.inc','.html'})
    bound.add(ROOT/'tools/build_home_portal_asset.py')
    bound.update(cjson/name for name in ['cJSON.c','cJSON.h'])
    bound.update(p for p in (ROOT/'games').rglob('*') if p.suffix in {'.h','.c'})
    bound.add(ROOT/'tools/build_game_packages.py')
    bound.add(ROOT/'tools/build_game_elf.py')
    bound.add(ROOT/'tools/build_tool_packages.py')
    bound.update(p for p in (ROOT/'apps').rglob('*') if p.suffix in {'.h','.c'})
    bound.update(p for p in tool_packages.glob('*') if p.is_file())
    if not game_report['native_only']:
        bound.add(game_elf/'manifest.json')
        bound.update(game_elf.glob('*.elf'))
    bound.update(p for p in game_packages.glob('*') if p.is_file())
    bound.add(ROOT/'sdcard/fonts/MiSansVF.ttf')
    bound.add(ROOT/'components/read_pico_transfer/manage.html')
    bound.add(ROOT/'components/read_pico_transfer/include/read_pico_management.h')
    bound.update(ROOT/'main/assets'/f'{name}.ttf' for name in ['builtin','serif','kai'])
    version=re.search(r'#define PS_VERSION "([^"]+)"',(ROOT/'main/system/pico_system.c').read_text()).group(1)
    manifest={'schema':1,'kind':'native-host-preview','source_root':str(ROOT.resolve()),'version':version,'binary':str(binary),'binary_sha256':hashlib.sha256(binary.read_bytes()).hexdigest(),'game_packages':{p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in sorted(game_packages.glob('*.pga'))},'game_package_mode':'native-only' if game_report['native_only'] else 'device-and-native','game_execution':'independent-native-payloads-from-private-tf','sources':{str(p.relative_to(ROOT)) if p.is_relative_to(ROOT) else str(p):hashlib.sha256(p.read_bytes()).hexdigest() for p in sorted(bound)},'hardware_access':False}
    manifest['tool_packages']={p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in sorted(tool_packages.glob('*.pga'))}
    manifest['tool_package_mode']='device-and-native' if tool_report['device_ready'] else 'native-only'
    manifest['home_link_mode']='thin-pga-psram-production-bridge'
    manifest['home_http_client']='curl-stdin-tls-verified-offline-env-supported'
    manifest['home_http_server']='in-memory-production-handlers-binary-base64-no-socket-listener'
    (output/'native-preview-build.json').write_text(json.dumps(manifest,ensure_ascii=False,indent=2)+'\n')
    print(binary)
    return binary
if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--output',type=Path,default=ROOT/'build/native-web-preview')
    mode=p.add_mutually_exclusive_group();mode.add_argument('--device-packages',action='store_true',help='Build verified device/native TF packages using the Xtensa compiler; no device access');mode.add_argument('--native-only',action='store_true',help='Explicit host-only packages; do not consume an existing device ELF manifest')
    args=p.parse_args();prepare(args.output.resolve(),device_packages=args.device_packages,native_only=args.native_only)

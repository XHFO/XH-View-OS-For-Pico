#!/usr/bin/env python3
"""中文：设备真实设置API和原生适配API的跨进程持久化测试，不接触硬件。
English: Cross-process persistence for actual device settings APIs and native adapters, without hardware.
"""
from pathlib import Path
import argparse
import os
import subprocess
import tempfile
from native_preview_book_prepare import prepare_book

ROOT=Path(__file__).resolve().parents[1]

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--sanitize',action='store_true')
    args=parser.parse_args()
    with tempfile.TemporaryDirectory(prefix='pico-settings-test-') as directory:
        build=Path(directory);stubs=build/'stubs';stubs.mkdir()
        prepare_book(build)
        errors=['ESP_ERR_NVS_NOT_FOUND','ESP_ERR_NVS_INVALID_LENGTH','ESP_ERR_NO_MEM','ESP_ERR_INVALID_ARG','ESP_ERR_INVALID_STATE','ESP_ERR_INVALID_SIZE','ESP_ERR_NOT_FOUND','ESP_ERR_NOT_SUPPORTED','ESP_ERR_INVALID_RESPONSE']
        (stubs/'esp_err.h').write_text('#pragma once\ntypedef int esp_err_t;\n#define ESP_OK 0\n#define ESP_FAIL -1\n'+''.join(f'#define {key} {i+1}\n' for i,key in enumerate(errors)))
        (stubs/'esp_log.h').write_text('#pragma once\n#define ESP_LOGI(...) ((void)0)\n#define ESP_LOGW(...) ((void)0)\n')
        (stubs/'nvs.h').write_text((ROOT/'tools/book_storage_stubs/nvs.h').read_text()+'\nesp_err_t nvs_get_u8(nvs_handle_t,const char*,uint8_t*);\nesp_err_t nvs_set_u8(nvs_handle_t,const char*,uint8_t);\n')
        (stubs/'nvs_flash.h').write_text('#pragma once\n#include "esp_err.h"\nesp_err_t nvs_flash_init(void);\n')
        (stubs/'ttf_font.h').write_text('#pragma once\n#define TTF_FONT_DEFAULT_SD "/sdcard/fonts/MiSansVF.ttf"\n#define TTF_FONT_BUILTIN "builtin"\n')
        for name,source in [('settings.h','main/settings.h'),('read_pico_sd.h','components/read_pico/include/read_pico_sd.h'),('sdmmc_cmd.h','tools/pico_reader_stubs/sdmmc_cmd.h')]:
            (stubs/name).write_text((ROOT/source).read_text())
        flags=['-std=gnu11','-O1','-g','-Wall','-Wextra','-Werror','-Wno-unused-variable','-D_POSIX_C_SOURCE=200809L','-D_DARWIN_C_SOURCE']
        if args.sanitize:flags+=['-fsanitize=address,undefined','-fno-omit-frame-pointer']
        command=[os.environ.get('CC','cc')]+flags
        for path in [stubs,build,ROOT/'tools',ROOT/'main',ROOT/'main/book']:command+=['-I',str(path)]
        env=dict(os.environ);env['UBSAN_OPTIONS']='halt_on_error=1:print_stacktrace=1'
        for native in [False,True]:
            binary=build/('native' if native else 'device');state=build/(binary.name+'-state');state.mkdir()
            sources=[ROOT/'tools/settings_host_test.c']
            extra=[]
            if native:
                extra=['-DSETTINGS_NATIVE_TEST']
                sources += [ROOT/'tools/native_preview_book_storage.c',ROOT/'tools/native_preview_book_vfs.c',ROOT/'main/book/book_progress.c']
            else:sources += [ROOT/'main/settings.c']
            subprocess.run(command+extra+[str(p) for p in sources]+['-o',str(binary)],check=True)
            actions=['get-on','set-on','get-on','set-off','get-off','set-on','get-on']
            if not native:actions+=['init-fail','open-fail']
            actions+=['sound-get-on','sound-set-off','sound-get-off','sound-set-custom','sound-get-custom','sound-invalid']
            if not native:actions+=['sound-open-fail','sound-get-custom','sound-set-fail','sound-get-custom','sound-commit-fail','sound-get-custom']
            actions+=['sound-set-on','sound-get-on']
            for action in actions:subprocess.run([str(binary),str(state),action],check=True,env=env)
            key=state/('tf/.pico-system/nvs/read_pico_dev_awake' if native else 'settings.bin')
            assert key.read_bytes()==b'\x01'
            sound_key=state/('tf/.pico-system/nvs/read_pico_sound_v1' if native else 'sound.bin')
            assert sound_key.read_bytes()==b'\x0f'
            print(binary.name+': missing-key default, explicit on/off, process restart persistence'+(', NVS failure default' if not native else '')+' passed')
            print(binary.name+': independent sound mask/default, master/suboptions, invalid bits, restart persistence'+(', checked open/write/commit failures retain previous values' if not native else '')+' passed')

if __name__=='__main__':main()

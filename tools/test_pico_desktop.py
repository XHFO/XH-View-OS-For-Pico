#!/usr/bin/env python3
"""中文：生产离线画布的解析、四方向布局、纯绘制与故障存储回归；无硬件。
English: Exercise production offline-canvas parsing, four orientations, pure rendering and storage failures without hardware.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, default=ROOT / 'build/desktop-host-check/report.json')
    parser.add_argument('--consumer', type=Path, help='Retain the native inbox consumer for Swift packet integration tests')
    parser.add_argument('--sanitized-consumer', type=Path, help='Retain the ASan/UBSan receiver for serial integration tests')
    parser.add_argument('--desktop-source', type=Path, help='Validate a staged desktop transport before integrating it')
    args = parser.parse_args()
    inputs = ['main/system/pico_desktop.c', 'main/system/pico_desktop.h', 'main/system/pico_weread.h',
              'main/system/pico_board_data.h', 'main/system/pico_board_data.c',
              'components/read_pico_home_link/home_link_json.h', 'components/read_pico_home_link/home_link_json.c']
    def source(name):
        return args.desktop_source if args.desktop_source and name == 'main/system/pico_desktop.c' else ROOT / name
    hashes = {name: hashlib.sha256(source(name).read_bytes()).hexdigest() for name in inputs}
    stubs = {
        'pico_system.h': '#pragma once\n#include <stdbool.h>\n#include <stdint.h>\n#include <stddef.h>\ntypedef int ps_route_t;\nenum {PS_CANVAS=29,PS_CLOUDFLARE=32,PI_LAYOUT=1,PI_LINK=2};\nint ps_sleep_style(void);ps_route_t ps_route(void);bool ps_apply_sleep_style(int);bool ps_background_io_allowed(void);int64_t ps_now(void);bool ps_power_active(void);void ps_navigate(ps_route_t);void ps_toast(const char*);const char *ps_version(void);\n',
        'pico_navigation.h': '#pragma once\n#include "pico_system.h"\ntypedef struct {uint16_t id;ps_route_t route;int icon;const char *label;void(*launch)(uint16_t);}ps_navigation_entry_t;\nbool ps_navigation_register(const ps_navigation_entry_t*);\n',
        'pico_power.h': '#pragma once\n#include <stdbool.h>\nunsigned ps_power_auto_minutes(void);bool ps_power_doubletap(void);bool ps_power_configure(unsigned,bool);bool ps_power_board_visible(void);int ps_power_cover_layout(void);void ps_power_set_cover_layout(int);\n',
        'read_pico_home_link.h': '#pragma once\n',
        'pico_home_portal.h': '#pragma once\n#include <stddef.h>\nvoid ps_home_portal_register(void);void ps_home_portal_status_suffix(char*,size_t);int ps_home_portal_mac_handle(const char*,char*,size_t);\n',
        'mbedtls/sha256.h': '#pragma once\n#include <stddef.h>\nint mbedtls_sha256(const unsigned char*,size_t,unsigned char[32],int);\n',
        'read_pico_pmu.h': '#pragma once\n#include <stdbool.h>\n#include <stdint.h>\nenum {PMU_CHARGE_UNKNOWN=0,PMU_CHARGE_NOT_CHARGING=1,PMU_CHARGE_CHARGING=2,PMU_CHARGE_FULL_INFERRED=3,PMU_CHARGE_FAULT=4};\n#define PMU_STATUS_BATTERY_VALID 1u\ntypedef struct {bool present,status_ok;int last_err;unsigned flags,soc_permille,charge_state;}pmu_snapshot_t;\nconst pmu_snapshot_t *read_pico_pmu_get(void);\n',
        'read_pico_sd.h': '#pragma once\n#include <stdbool.h>\ntypedef struct {bool present,mounted,exported;}read_pico_sd_info_t;int read_pico_sd_get_info(read_pico_sd_info_t*);\n',
        'esp_heap_caps.h': '#pragma once\n#include <stddef.h>\nenum {MALLOC_CAP_SPIRAM=1,MALLOC_CAP_8BIT=2};void *heap_caps_malloc(size_t,unsigned);\n',
        'epdiy.h': '#pragma once\n#include <stdint.h>\ntypedef struct {int x,y,width,height;}EpdRect;int epd_width(void),epd_height(void),epd_rotated_display_width(void),epd_rotated_display_height(void);EpdRect epd_full_screen(void);void epd_draw_pixel(int,int,uint8_t,uint8_t*);void epd_fill_rect(EpdRect,uint8_t,uint8_t*);\n',
    }
    results = []
    with tempfile.TemporaryDirectory(prefix='pico-desktop-host-') as directory:
        output = Path(directory)
        for name in inputs:
            target = output / name
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(source(name).read_bytes())
        (output / 'read_pico_management.h').write_bytes((ROOT / 'components/read_pico_transfer/include/read_pico_management.h').read_bytes())
        for name, text in stubs.items():
            (output / name).parent.mkdir(parents=True, exist_ok=True)
            (output / name).write_text(text)
        (output / 'sha256.c').write_text('#include <CommonCrypto/CommonDigest.h>\n#include <stddef.h>\nint mbedtls_sha256(const unsigned char*p,size_t n,unsigned char out[32],int is224){return is224||!CC_SHA256(p,(CC_LONG)n,out);}\n')
        (output / 'board-under-test.c').write_text('#include <stdlib.h>\nvoid *test_board_allocate(size_t);\n#define malloc test_board_allocate\n#include "main/system/pico_board_data.c"\n')
        cjson = ROOT / 'managed_components/espressif__cjson/cJSON'
        for sanitized in (False, True):
            flags = ['-std=gnu11', '-O1', '-g', '-Wall', '-Wextra', '-Werror', '-Wno-deprecated-declarations', '-I', str(output), '-I', str(output / 'main/system'), '-I', str(cjson)]
            if sanitized:
                flags += ['-fsanitize=address,undefined', '-fno-omit-frame-pointer']
            executable = output / ('test-sanitized' if sanitized else 'test')
            subprocess.run([os.environ.get('CC', 'cc'), *flags, str(ROOT / 'tools/pico_desktop_host_test.c'), str(output / 'board-under-test.c'), str(output / 'sha256.c'), str(ROOT / 'components/read_pico_home_link/home_link_json.c'), '-o', str(executable)], check=True)
            if args.consumer and not sanitized:
                args.consumer.parent.mkdir(parents=True, exist_ok=True)
                shutil.copy2(executable, args.consumer)
            if args.sanitized_consumer and sanitized:
                args.sanitized_consumer.parent.mkdir(parents=True, exist_ok=True)
                shutil.copy2(executable, args.sanitized_consumer)
            storage = output / ('tf-sanitized' if sanitized else 'tf-normal')
            storage.mkdir()
            run = subprocess.run([str(executable), str(storage)], text=True, capture_output=True)
            print(run.stdout, end='')
            if run.returncode:
                print(run.stderr, end='')
                run.check_returncode()
            match = re.search(r'(\d+) checks PASS', run.stdout)
            assert match, run.stdout
            results.append({'sanitized': sanitized, 'checks': int(match.group(1))})
    assert hashes == {name: hashlib.sha256(source(name).read_bytes()).hexdigest() for name in inputs}, 'Production source changed during the test; rerun'
    report = {'result': 'PASS', 'hardware_access': False, 'source_sha256': hashes, 'runs': results,
              'boundary': 'actual pico_desktop C with host pixel/storage fault injection; management authorization tested separately'}
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, ensure_ascii=False, indent=2) + '\n')


if __name__ == '__main__':
    main()

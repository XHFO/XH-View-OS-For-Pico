#!/usr/bin/env python3
"""Exercise the unmodified native-preview BLE adapter; never access host Bluetooth or a device."""
from pathlib import Path
import argparse
import hashlib
import json
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / 'tools/native_preview_ble_pager.c'
HEADER = ROOT / 'components/read_pico_ble_pager/include/read_pico_ble_pager.h'
TEST = r'''
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "read_pico_ble_pager.h"
void native_preview_ble_tick(int64_t);
bool native_preview_ble_press(int);
static unsigned checks,stores;
#define CHECK(x) do { checks++;assert(x); } while(0)
bool ps_load(const char *name,void *out,size_t length){(void)out;CHECK(!strcmp(name,"ble-preview-v1")&&length==4);return false;}
bool ps_store(const char *name,const void *data,size_t length){CHECK(!strcmp(name,"ble-preview-v1")&&data&&length==4);stores++;return true;}
int main(void){
 read_pico_ble_pager_status_t s;
 CHECK(read_pico_ble_pager_init()==ESP_OK);read_pico_ble_pager_get_status(&s);
 CHECK(s.simulated&&!s.enabled&&!s.ready&&read_pico_ble_pager_light_sleep_safe());
 CHECK(read_pico_ble_pager_set_enabled(true)==ESP_OK&&!read_pico_ble_pager_light_sleep_safe());
 CHECK(read_pico_ble_pager_scan()==ESP_OK);native_preview_ble_tick(1000);
 read_pico_ble_pager_get_status(&s);CHECK(s.enabled&&s.ready&&s.device_count==1&&!s.scanning);
 CHECK(read_pico_ble_pager_connect(0)==ESP_OK);native_preview_ble_tick(1600);
 read_pico_ble_pager_get_status(&s);CHECK(s.connected&&s.remembered);
 CHECK(native_preview_ble_press(1)&&read_pico_ble_pager_poll_event()==1);
 CHECK(read_pico_ble_pager_set_reversed(true)==ESP_OK);
 CHECK(native_preview_ble_press(1)&&read_pico_ble_pager_poll_event()==-1);
 CHECK(native_preview_ble_press(1));
 CHECK(read_pico_ble_pager_set_enabled(false)==ESP_OK&&read_pico_ble_pager_light_sleep_safe());
 read_pico_ble_pager_get_status(&s);CHECK(!s.ready&&!s.enabled&&!s.connected&&!s.device_count&&s.remembered&&s.reversed);
 CHECK(!native_preview_ble_press(1)&&read_pico_ble_pager_poll_event()==0);
 CHECK(read_pico_ble_pager_set_enabled(true)==ESP_OK&&!read_pico_ble_pager_light_sleep_safe());
 read_pico_ble_pager_get_status(&s);CHECK(s.connected&&s.remembered&&s.reversed);
 CHECK(read_pico_ble_pager_disconnect()==ESP_OK&&!read_pico_ble_pager_light_sleep_safe());
 CHECK(read_pico_ble_pager_scan()==ESP_OK);CHECK(read_pico_ble_pager_set_enabled(false)==ESP_OK);
 native_preview_ble_tick(3000);read_pico_ble_pager_get_status(&s);
 CHECK(!s.enabled&&!s.device_count&&!s.scanning&&read_pico_ble_pager_light_sleep_safe());
 for(unsigned i=0;i<20;i++){
  CHECK(read_pico_ble_pager_set_enabled(true)==ESP_OK&&!read_pico_ble_pager_light_sleep_safe());
  CHECK(read_pico_ble_pager_set_enabled(false)==ESP_OK&&read_pico_ble_pager_light_sleep_safe());
 }
 CHECK(read_pico_ble_pager_forget()==ESP_OK);read_pico_ble_pager_get_status(&s);
 CHECK(!s.remembered&&s.reversed&&read_pico_ble_pager_light_sleep_safe()&&stores>40);
 printf("Native BLE lifecycle: %u checks PASS; simulated disable releases sleep guard, queued input/scans clear, re-enable and preferences survive\n",checks);
 return 0;
}
'''

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--sanitize', action='store_true')
parser.add_argument('--report', type=Path)
args = parser.parse_args()
with tempfile.TemporaryDirectory(prefix='native-ble-lifecycle-') as temp:
    output = Path(temp)
    (output / 'esp_err.h').write_text('#pragma once\ntypedef int esp_err_t;\n#define ESP_OK 0\n#define ESP_ERR_INVALID_STATE 100\n#define ESP_ERR_INVALID_ARG 101\n')
    (output / 'pico_system.h').write_text('#pragma once\n#include <stdbool.h>\n#include <stddef.h>\nbool ps_load(const char*,void*,size_t);bool ps_store(const char*,const void*,size_t);\n')
    (output / 'test.c').write_text(TEST)
    flags = ['-std=c11', '-Wall', '-Wextra', '-Werror', '-O1', '-g']
    if args.sanitize:
        flags += ['-fsanitize=address,undefined', '-fno-omit-frame-pointer']
    subprocess.run(['cc', *flags, '-I', str(output), '-I', str(HEADER.parent), str(SOURCE), str(output/'test.c'), '-o', str(output/'test')], check=True)
    result = subprocess.check_output([str(output/'test')], text=True)
    print(result, end='')
    if args.report:
        args.report.parent.mkdir(parents=True, exist_ok=True)
        args.report.write_text(json.dumps({'result':'PASS', 'output':result.strip(), 'sanitized':args.sanitize,
            'sources':{str(p.relative_to(ROOT)):hashlib.sha256(p.read_bytes()).hexdigest() for p in [SOURCE, HEADER, Path(__file__)]},
            'scope':'Unmodified production preview adapter with disposable persistence boundary; no host Bluetooth, user settings or physical device accessed.'}, indent=2)+'\n')

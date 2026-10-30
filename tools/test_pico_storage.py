#!/usr/bin/env python3
"""Test actual TF UI with card/task doubles; never access or format a physical card.
测试真实TF界面与任务替身，不访问或格式化真实卡。
"""
from pathlib import Path
import argparse
import os
import shlex
import subprocess
import tempfile
ROOT=Path(__file__).resolve().parents[1]
def prepare_stubs(build):
    (build/'freertos').mkdir()
    (build/'freertos/FreeRTOS.h').write_text('#pragma once\n#define pdPASS 1\n')
    (build/'freertos/task.h').write_text('#pragma once\nint xTaskCreate(void (*fn)(void*),const char*,unsigned,void*,int,void*);\nvoid vTaskDelete(void*);\n')
    (build/'esp_err.h').write_text('#pragma once\ntypedef int esp_err_t;\n#define ESP_OK 0\n#define ESP_FAIL -1\n#define ESP_ERR_NO_MEM -2\n#define ESP_ERR_NOT_FINISHED -3\n')
    (build/'sdmmc_cmd.h').write_text('#pragma once\ntypedef struct {int unused;} sdmmc_card_t;\n')
    (build/'app.h').write_text('#pragma once\n#include <stdbool.h>\n#include <stdint.h>\ntypedef struct {int unused;} app_ctx_t;\ntypedef struct {int unused;} app_desc_t;\ntypedef int app_redraw_t;\ntypedef struct {int x,y,width,height;} EpdRect;\n')
def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--sanitize',action='store_true');args=parser.parse_args()
    with tempfile.TemporaryDirectory(prefix='pico-storage-test-') as td:
        build=Path(td);prepare_stubs(build);binary=build/'test'
        flags=['-std=c11','-O1','-g','-Wall','-Wextra','-Werror']+shlex.split(os.environ.get('CFLAGS',''))
        if args.sanitize:flags+=['-fsanitize=address,undefined','-fno-omit-frame-pointer']
        command=[os.environ.get('CC','cc')]+flags
        for path in [build,ROOT/'main/system',ROOT/'components/read_pico/include']:command+=['-I',str(path)]
        subprocess.run(command+[str(ROOT/'tools/pico_storage_host_test.c'),'-o',str(binary)],check=True)
        subprocess.run([str(binary)],check=True)
if __name__=='__main__':main()

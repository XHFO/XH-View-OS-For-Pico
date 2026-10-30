#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Compile the actual SD lifecycle with deterministic boundary doubles / 编译实际 SD 生命周期及确定性边界替身。"""
from pathlib import Path
import argparse
import os
import shlex
import subprocess
import tempfile

parser = argparse.ArgumentParser()
parser.add_argument('--sanitize', action='store_true')
args = parser.parse_args()
root = Path(__file__).resolve().parents[1]
(root / 'build').mkdir(exist_ok=True)
with tempfile.TemporaryDirectory(prefix='sd-media-test-', dir=root / 'build') as scratch:
    out = Path(scratch)
    for name in ('driver/sdmmc_host.h', 'esp_heap_caps.h', 'esp_log.h', 'esp_vfs_fat.h',
                 'freertos/FreeRTOS.h', 'freertos/task.h', 'read_pico_board.h',
                 'sdmmc_cmd.h', 'esp_err.h', 'ff.h', 'diskio_sdmmc.h', 'diskio_impl.h'):
        p = out / name
        p.parent.mkdir(parents=True, exist_ok=True)
        p.write_text('#pragma once\n', encoding='utf-8')
    flags = ['-fsanitize=address,undefined', '-fno-omit-frame-pointer'] if args.sanitize else []
    subprocess.run([os.environ.get('CC', 'cc'), '-std=gnu11', '-Wall', '-Wextra', '-Werror',
                    '-Wno-unused-variable', '-g', '-pthread'] + flags + shlex.split(os.environ.get('CFLAGS', '')) + [
                    '-I' + str(out), '-I' + str(root / 'components/read_pico/include'),
                    str(root / 'tools/sd_media_host_test.c'), '-o', str(out / 'test')], check=True)
    subprocess.run([str(out / 'test')], check=True)
    board = (root / 'components/read_pico/read_pico_board.c').read_text()
    start = board.index('esp_err_t read_pico_sd_detect(')
    end = board.index('void read_pico_clear_ioe_int(', start)
    (out / 'board_sd_detect.inc').write_text(board[start:end])
    subprocess.run([os.environ.get('CC', 'cc'), '-std=gnu11', '-Wall', '-Wextra', '-Werror',
                    '-g'] + flags + ['-I' + str(out), str(root / 'tools/sd_detect_host_test.c'),
                                    '-o', str(out / 'detect')], check=True)
    subprocess.run([str(out / 'detect')], check=True)

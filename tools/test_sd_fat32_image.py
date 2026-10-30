#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 mindreset
# SPDX-License-Identifier: Apache-2.0
"""在 32/64/128 GiB 稀疏软件磁盘上运行真实 IDF FatFs，不访问硬件。
Run actual IDF FatFs on sparse 32/64/128 GiB software disks, without hardware.

临时主机配置只关闭长文件名与 RTOS 锁；格式化、扇区、分区、动态缓冲、TRIM 和文件系统支持保持产品配置。
Only long filename support and RTOS mutexes are disabled in the temporary host
configuration. Formatter, sector sizes, partitioning, dynamic buffers, TRIM,
FAT32 and exFAT settings remain those of the checkout.

冻结：不修改生产源文件或配置，不访问实体卡；TRIM 只记录并返回不支持，本测试不代表实体 SD/USB 验收。
Frozen: never modify production sources/configuration or access physical cards;
record and decline TRIM without simulating erase. This is not SD/USB acceptance.
"""
from pathlib import Path
import argparse
import hashlib
import os
import re
import shlex
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
IDF = ROOT.parent / 'esp-idf-v6.1'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--sanitize', action='store_true', help='enable AddressSanitizer and UndefinedBehaviorSanitizer')
    args = parser.parse_args()
    source = IDF / 'components/fatfs/src'
    if not (source / 'ff.c').is_file():
        parser.error(f'the checkout IDF FatFs source is missing: {source}')
    config = dict(re.findall(r'^(CONFIG_[A-Z0-9_]+)=(.+)$', (ROOT / 'sdkconfig').read_text(), re.MULTILINE))
    with tempfile.TemporaryDirectory(prefix='pico-fat32-image-') as directory:
        temporary = Path(directory)
        for name in ('ff.c', 'ff.h', 'ffconf.h', 'diskio.h'):
            shutil.copy2(source / name, temporary / name)
        ffconf = (temporary / 'ffconf.h').read_text()
        ffconf, count = re.subn(r'^(#define\s+FF_FS_REENTRANT\s+)1\s*$', r'\g<1>0', ffconf, flags=re.MULTILINE)
        if count != 1:
            raise RuntimeError('expected one reentrancy setting in the copied ffconf.h')
        (temporary / 'ffconf.h').write_text(ffconf)
        referenced = sorted(set(re.findall(r'\bCONFIG_[A-Z0-9_]+\b', ffconf)))
        lines = ['#pragma once', '#include <stddef.h>']
        for key in referenced:
            if key not in config or key in ('CONFIG_FATFS_LFN_HEAP', 'CONFIG_FATFS_LFN_STACK'):
                continue
            value = '1' if config[key] == 'y' else config[key]
            lines.append(f'#define {key} {value}')
        lines.append('#define CONFIG_FATFS_LFN_NONE 1')
        (temporary / 'sdkconfig.h').write_text('\n'.join(lines) + '\n')
        (temporary / 'freertos').mkdir()
        (temporary / 'freertos/FreeRTOS.h').write_text('#pragma once\n#define portTICK_PERIOD_MS 1\n')
        (temporary / 'freertos/semphr.h').write_text('#pragma once\n')
        compiler = shlex.split(os.environ.get('CC', 'cc'))
        flags = ['-std=gnu11', '-Wall', '-Wextra', '-g', '-I', str(temporary)]
        if args.sanitize:
            flags += ['-fsanitize=address,undefined', '-fno-omit-frame-pointer']
        fatfs_object = temporary / 'ff.o'
        executable = temporary / 'fat32-image-test'
        subprocess.run(compiler + flags + ['-c', str(temporary / 'ff.c'), '-o', str(fatfs_object)], cwd=ROOT, check=True)
        subprocess.run(compiler + flags + ['-Werror', str(ROOT / 'tools/sd_fat32_image_host_test.c'), str(fatfs_object), '-o', str(executable)], cwd=ROOT, check=True)
        digest = hashlib.sha256((source / 'ff.c').read_bytes()).hexdigest()
        print(f'Actual IDF ff.c SHA-256: {digest}', flush=True)
        print('Host-only configuration changes: LFN=0, reentrancy=0; sparse software disks only.', flush=True)
        subprocess.run([str(executable)], cwd=ROOT, check=True)


if __name__ == '__main__':
    main()

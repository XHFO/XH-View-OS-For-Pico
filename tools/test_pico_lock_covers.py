#!/usr/bin/env python3
"""中文：独立休眠封面实际源码、缓存和横竖边界回归，不接触设备。
English: Exact-source sleep-cover cache and orientation regression, without device access.
"""
import argparse
import os
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--sanitize', action='store_true')
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix='pico-lock-covers-') as folder:
        build = Path(folder)
        (build / 'esp_heap_caps.h').write_text('''#pragma once
#include <stddef.h>
#define MALLOC_CAP_SPIRAM 1
#define MALLOC_CAP_8BIT 2
void *lock_test_malloc(size_t);
#define heap_caps_malloc(n,caps) lock_test_malloc(n)
''')
        flags = ['-std=gnu11', '-Wall', '-Wextra', '-Werror', '-O1', '-g']
        if args.sanitize:
            flags += ['-fsanitize=address,undefined', '-fno-omit-frame-pointer']
        command = [os.environ.get('CC', 'cc')] + flags
        for path in [build, ROOT/'tools/pico_reader_stubs', ROOT/'tools/zip_host_stubs', ROOT/'main/book', ROOT/'main/system']:
            command += ['-I', str(path)]
        binary = build/'test'
        subprocess.run(command + [str(ROOT/'tools/pico_lock_covers_host_test.c'), '-o', str(binary)], check=True)
        subprocess.run([str(binary), str(build)], check=True)


if __name__ == '__main__':
    main()

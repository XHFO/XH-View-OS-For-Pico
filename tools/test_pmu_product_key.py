#!/usr/bin/env python3
"""Exercise exact PMU product-key source and its real public enums without hardware."""
from pathlib import Path
import argparse
import os
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--sanitize', action='store_true')
args = parser.parse_args()
source = (ROOT / 'components/read_pico_pmu/read_pico_pmu.c').read_text()
start = source.index('/* ---- 产品电源动作 / Product power-key actions ---- */')
end = source.index('/* ---- 唤醒事件兼容入口 / Legacy wake-event entry ---- */', start)
header = (ROOT / 'components/read_pico_pmu/include/read_pico_pmu.h').read_text()
header = '\n'.join(line for line in header.splitlines() if not line.lstrip().startswith('#include '))
prefix = '#include <stdbool.h>\n#include <stdint.h>\n#include "read_pico_pmu_protocol.h"\n'
prefix += 'typedef int esp_err_t; typedef void *i2c_master_bus_handle_t;\n#define ESP_OK 0\n'
flags = ['-std=c11', '-Wall', '-Wextra', '-Werror', '-g',
         '-I' + str(ROOT / 'components/read_pico_pmu/include')]
if args.sanitize:
    flags += ['-fsanitize=address,undefined', '-fno-omit-frame-pointer']
with tempfile.TemporaryDirectory(prefix='pico-pmu-key-') as folder:
    folder = Path(folder)
    (folder / 'pmu_api_under_test.h').write_text(prefix + header)
    (folder / 'pmu_product_key_under_test.c').write_text(source[start:end])
    binary = folder / 'test'
    subprocess.run(['cc', *flags, '-I' + str(folder),
                    str(ROOT / 'tools/pmu_product_key_host_test.c'), '-o', str(binary)], check=True)
    env = dict(os.environ)
    env['UBSAN_OPTIONS'] = 'halt_on_error=1:print_stacktrace=1'
    subprocess.run([str(binary)], check=True, env=env)

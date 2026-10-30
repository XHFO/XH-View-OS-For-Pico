#!/usr/bin/env python3
"""中文：对真实PMU命令/全量刷新注入RTC失败。 / English: Inject RTC failures into real PMU command/full-refresh source."""
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
command = source[source.index('esp_err_t read_pico_pmu_cmd('):source.index('esp_err_t read_pico_pmu_vcom_get(')]
refresh = source[source.index('static esp_err_t refresh_core('):source.index('esp_err_t read_pico_pmu_init(')]
header = (ROOT / 'components/read_pico_pmu/include/read_pico_pmu.h').read_text()
header = '\n'.join(line for line in header.splitlines() if not line.lstrip().startswith('#include '))
prefix = '#include <stdbool.h>\n#include <stdint.h>\n#include "read_pico_pmu_protocol.h"\n'
prefix += 'typedef int esp_err_t; typedef void *i2c_master_bus_handle_t;\n#define ESP_OK 0\n'
flags = ['-std=c11', '-Wall', '-Wextra', '-Werror', '-g',
         '-I' + str(ROOT / 'components/read_pico_pmu/include')]
if args.sanitize:
    flags += ['-fsanitize=address,undefined', '-fno-omit-frame-pointer']
with tempfile.TemporaryDirectory(prefix='pico-pmu-rtc-') as folder:
    folder = Path(folder)
    (folder / 'pmu_api_under_test.h').write_text(prefix + header)
    (folder / 'pmu_rtc_under_test.c').write_text(command + refresh)
    binary = folder / 'test'
    subprocess.run([os.environ.get('CC', 'cc'), *flags, '-I' + str(folder),
                    str(ROOT / 'tools/pmu_rtc_host_test.c'), '-o', str(binary)], check=True)
    env = dict(os.environ, UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1')
    subprocess.run([str(binary)], check=True, env=env)

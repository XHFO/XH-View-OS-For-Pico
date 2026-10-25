#!/usr/bin/env python3
"""运行实际连接worker的主机故障注入；不访问设备。 / Fault-inject the actual worker without hardware."""
from pathlib import Path
import argparse
import os
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--sanitize', action='store_true')
    args = parser.parse_args()
    cjson = ROOT / 'managed_components/espressif__cjson/cJSON'
    with tempfile.TemporaryDirectory(prefix='connectivity-test-') as directory:
        exe = Path(directory) / 'test'
        cmd = [os.environ.get('CC', 'cc'), '-std=gnu11', '-Wall', '-Wextra', '-Werror', '-Wno-unused-function', '-g']
        if args.sanitize:
            cmd += ['-fsanitize=address,undefined', '-fno-omit-frame-pointer']
        for include in [ROOT / 'tools/connectivity_stubs', ROOT / 'components/read_pico_transfer/include', cjson]:
            cmd += ['-I', str(include)]
        vendor = Path(directory) / 'cJSON.o'
        subprocess.run(cmd + ['-Wno-deprecated-declarations', '-c', str(cjson / 'cJSON.c'), '-o', str(vendor)], check=True)
        cmd += [str(ROOT / 'tools/connectivity_host_test.c'), str(vendor), '-o', str(exe)]
        subprocess.run(cmd, check=True)
        subprocess.run([str(exe)], check=True)


if __name__ == '__main__':
    main()

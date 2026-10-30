#!/usr/bin/env python3
"""真实多WiFi存储故障注入，不访问设备。 / Profile fault injection without device access."""
from pathlib import Path
import argparse
import os
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--sanitize', action='store_true', help='enable AddressSanitizer and UndefinedBehaviorSanitizer')
    args = parser.parse_args()
    cjson = ROOT / 'managed_components/espressif__cjson/cJSON'
    with tempfile.TemporaryDirectory(prefix='pico-profiles-') as directory:
        executable = Path(directory) / 'connectivity-profiles-test'
        command = shlex.split(os.environ.get('CC', 'cc')) + [
            '-std=gnu11', '-Wall', '-Wextra', '-Werror', '-Wno-unused-function', '-g',
        ]
        if args.sanitize:
            command += ['-fsanitize=address,undefined', '-fno-omit-frame-pointer']
        # 仅第三方源码豁免macOS弃用声明；测试与生产头仍按Werror编译。
        # Exempt only vendor deprecation declarations; keep Werror for the test and production headers.
        vendor_object = Path(directory) / 'cJSON.o'
        subprocess.run(command + ['-Wno-deprecated-declarations', '-I', str(cjson), '-c',
                                 str(cjson / 'cJSON.c'), '-o', str(vendor_object)], cwd=ROOT, check=True)
        command += ['-I', str(cjson), str(ROOT / 'tools/connectivity_profiles_host_test.c'),
                    str(vendor_object), '-lm', '-o', str(executable)]
        subprocess.run(command, cwd=ROOT, check=True)
        subprocess.run([str(executable)], cwd=ROOT, check=True)


if __name__ == '__main__':
    main()

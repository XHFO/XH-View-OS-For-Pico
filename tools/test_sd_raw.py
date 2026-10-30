#!/usr/bin/env python3
"""真实 raw helper 故障注入；无硬件访问。 / Fault-inject the real raw helper without hardware access."""
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
    with tempfile.TemporaryDirectory(prefix='pico-sd-raw-') as directory:
        temporary = Path(directory)
        # 最小 ABI 与假 API 在测试 C 文件集中声明；这里只替代目标机专用 include。
        # The C test declares the minimal ABI and fake APIs; replace only target-specific includes here.
        for name in ('driver/sdmmc_host.h', 'esp_err.h', 'sdmmc_cmd.h', 'ff.h', 'diskio_impl.h', 'diskio_sdmmc.h'):
            stub = temporary / name
            stub.parent.mkdir(parents=True, exist_ok=True)
            stub.write_text('/* Host test declarations precede the production header. */\n')
        executable = temporary / 'sd-raw-test'
        command = shlex.split(os.environ.get('CC', 'cc')) + ['-std=gnu11', '-Wall', '-Wextra', '-Werror', '-g']
        if args.sanitize:
            command += ['-fsanitize=address,undefined', '-fno-omit-frame-pointer']
        command += ['-I', str(temporary), str(ROOT / 'tools/sd_raw_host_test.c'), '-o', str(executable)]
        subprocess.run(command, cwd=ROOT, check=True)
        subprocess.run([str(executable)], cwd=ROOT, check=True)


if __name__ == '__main__':
    main()

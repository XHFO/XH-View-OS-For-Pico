#!/usr/bin/env python3
"""直接测试生产管理HTTP适配器；模拟HTTP，不绑定socket或访问设备。
Test the production management HTTP adapter with no sockets or device access.
"""
import argparse
import gzip
import hashlib
import json
from pathlib import Path
import os
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, default=ROOT / 'build/management-http-check/report.json')
    args = parser.parse_args()
    names = ['components/read_pico_transfer/read_pico_management.c',
             'components/read_pico_transfer/include/read_pico_management.h',
             'tools/pico_management_http_host_test.c',
             'tools/management_http_stubs/management_host_env.h']
    hashes = {name: hashlib.sha256((ROOT / name).read_bytes()).hexdigest() for name in names}
    runs = []
    with tempfile.TemporaryDirectory(prefix="pico-management-http-") as temp:
        work = Path(temp)
        for name in ["esp_http_server.h", "esp_heap_caps.h", "esp_random.h", "esp_timer.h",
                     "freertos/FreeRTOS.h", "freertos/semphr.h"]:
            wrapper = work / name
            wrapper.parent.mkdir(parents=True, exist_ok=True)
            wrapper.write_text('#include "management_host_env.h"\n')
        for sanitized in (False, True):
            command = shlex.split(os.environ.get("CC", "cc")) + ["-std=gnu11", "-Wall", "-Wextra", "-Werror", "-g"]
            if sanitized:
                command += ["-fsanitize=address,undefined", "-fno-omit-frame-pointer"]
            for folder in [work, ROOT / "tools/management_http_stubs", ROOT / "tools/connectivity_stubs",
                           ROOT / "components/read_pico_transfer/include", ROOT / "components/read_pico_home_link/include"]:
                command += ["-I", str(folder)]
            executable = work / ('management-http-sanitized' if sanitized else 'management-http')
            command += [str(ROOT / "tools/pico_management_http_host_test.c"), "-o", str(executable)]
            subprocess.run(command, cwd=ROOT, check=True)
            run = subprocess.run([str(executable)], cwd=ROOT, text=True, capture_output=True)
            print(run.stdout, end='')
            if run.returncode:
                print(run.stderr, end='')
                run.check_returncode()
            runs.append({'sanitized': sanitized, 'output': run.stdout.strip()})
            encoded = subprocess.check_output([str(executable), '--page-gzip'], cwd=ROOT)
            assert encoded == (ROOT / 'components/read_pico_transfer/assets/manage.html.gz').read_bytes()
            assert gzip.decompress(encoded) == (ROOT / 'components/read_pico_transfer/manage.html').read_bytes()
    assert hashes == {name: hashlib.sha256((ROOT / name).read_bytes()).hexdigest() for name in names}, 'Source changed during the test; rerun'
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps({'result': 'PASS', 'hardware_access': False, 'sockets': False,
        'source_sha256': hashes, 'runs': runs}, indent=2) + '\n')


if __name__ == '__main__':
    main()

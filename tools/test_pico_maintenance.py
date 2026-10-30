#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Test the actual maintenance parser using pipe stdin; no serial/hardware access.
使用管道 stdin 测试真实维护解析器，不访问串口或硬件。
"""
from pathlib import Path
import argparse
import json
import hashlib
import re
import os
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def check_wire_boundaries(output):
    # 实际观察到的 64 字节残缺日志；不放宽独立协议行的匹配规则。
    # Observed 64-byte truncated log; keep protocol matching restricted to standalone lines.
    stale_prefix = b'I (134532) cst836u: touch-diag idle result=ESP_OK latest=ESP_OK '
    assert len(stale_prefix) == 64 and b'\n' not in stale_prefix
    fixtures = {}
    for line in output.splitlines():
        if line.startswith('WIRE '):
            _, name, encoded = line.split()
            assert name not in fixtures
            fixtures[name] = bytes.fromhex(encoded)
    assert set(fixtures) == {'status', 'wifi', 'time', 'board', 'accepted', 'rejected', 'busy', 'background', 'wireless'}

    def strict_frames(wire, split):
        pending = b''
        frames = []
        for chunk in (wire[:split], wire[split:]):
            pending += chunk
            while b'\n' in pending:
                complete, pending = pending.split(b'\n', 1)
                if complete.endswith(b'\r'):
                    complete = complete[:-1]
                if complete.startswith(b'RPICO '):
                    frames.append(json.loads(complete[6:]))
                elif complete.startswith(b'PICO_WIFI1 '):
                    frames.append(json.loads(complete[11:]))
        assert pending == b''
        return frames

    checks = 0
    for name, actual in fixtures.items():
        prefix=b'\nPICO_WIFI1 ' if name=='wireless' else b'\nRPICO '
        assert actual.startswith(prefix) and actual.endswith(b'\n') and actual.count(b'\n') == 2
        expected = json.loads(actual[len(prefix):-1])
        # 主机管道保留 LF；这里只模拟设备控制台的 LF → CRLF 转换。
        # Host pipes preserve LF; explicitly simulate device-console LF-to-CRLF conversion here.
        wire = stale_prefix + actual.replace(b'\n', b'\r\n')
        for split in range(len(wire) + 1):
            assert strict_frames(wire, split) == [expected], (name, split)
            checks += 1
        # 去掉修复的首个换行后，严格解析必须拒绝被日志前缀污染的回包。
        # Without the fixed leading newline, strict parsing must reject the log-prefixed reply.
        unfixed_wire = stale_prefix + actual[1:].replace(b'\n', b'\r\n')
        assert strict_frames(unfixed_wire, len(stale_prefix)) == [], name
        checks += 1
    print(f'PASS maintenance wire: {checks} checks, {len(fixtures)} actual replies; stale 64-byte log, CRLF and every split boundary')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--sanitize', action='store_true')
    parser.add_argument('--output', type=Path, help='Source-bound proof for packaged firmware')
    args = parser.parse_args()
    names = ['main/system/pico_maintenance.c', 'main/system/pico_desktop.c', 'main/system/pico_desktop.h', 'main/system/pico_system.c', 'tools/pico_maintenance_host_test.c']
    hashes = {name: hashlib.sha256((ROOT / name).read_bytes()).hexdigest() for name in names}
    version = re.search(r'#define PS_VERSION \"([^\"]+)\"', (ROOT / 'main/system/pico_system.c').read_text()).group(1)
    with tempfile.TemporaryDirectory(prefix='pico-maintenance-test-') as scratch:
        build = Path(scratch)
        (build / 'app.h').write_text('#pragma once\n#include <stdbool.h>\n#include <stdint.h>\ntypedef enum {APP_REDRAW_NONE,APP_REDRAW_AREA,APP_REDRAW_PAGE,APP_REDRAW_FULL,APP_REDRAW_DONE} app_redraw_t;\ntypedef struct {int unused;} app_ctx_t;\ntypedef struct {int unused;} app_desc_t;\ntypedef struct {int x,y,width,height;} EpdRect;\n')
        (build / 'esp_err.h').write_text('#pragma once\ntypedef int esp_err_t;\n#define ESP_OK 0\n#define ESP_FAIL -1\n')
        (build / 'sdmmc_cmd.h').write_text('#pragma once\ntypedef struct {int unused;} sdmmc_card_t;\n')
        (build / 'sdkconfig.h').write_text('#pragma once\n#define CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG 1\n')
        (build / 'settings.h').write_text('#pragma once\n#include <stdbool.h>\nbool app_settings_test_awake(void);\n')
        (build / 'read_pico_pmu.h').write_text('#pragma once\n#include <stdbool.h>\n#include <stdint.h>\ntypedef struct {uint32_t unix_sec;bool time_ok;uint8_t time_synced;} pmu_snapshot_t;\nbool read_pico_pmu_ready(void);\nconst pmu_snapshot_t *read_pico_pmu_get(void);\n')
        (build / 'driver').mkdir()
        (build / 'driver/usb_serial_jtag.h').write_text('''#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
typedef struct {uint32_t tx_buffer_size,rx_buffer_size;} usb_serial_jtag_driver_config_t;
#define USB_SERIAL_JTAG_DRIVER_CONFIG_DEFAULT() ((usb_serial_jtag_driver_config_t){.tx_buffer_size=256,.rx_buffer_size=256})
bool usb_serial_jtag_is_driver_installed(void);
esp_err_t usb_serial_jtag_driver_install(usb_serial_jtag_driver_config_t *config);
int usb_serial_jtag_read_bytes(void *buffer,uint32_t length,uint32_t ticks_to_wait);
''')
        flags = ['-std=c11', '-D_POSIX_C_SOURCE=200809L', '-O1', '-g', '-Wall', '-Wextra', '-Werror'] + shlex.split(os.environ.get('CFLAGS', ''))
        if args.sanitize:
            flags += ['-fsanitize=address,undefined', '-fno-omit-frame-pointer']
        command = [os.environ.get('CC', 'cc')] + flags
        for path in (build, ROOT / 'main/system', ROOT / 'components/read_pico/include', ROOT / 'components/read_pico_usb/include', ROOT / 'components/read_pico_transfer/include'):
            command += ['-I', str(path)]
        binary = build / 'test'
        subprocess.run(command + [str(ROOT / 'tools/pico_maintenance_host_test.c'), '-o', str(binary)], check=True)
        result = subprocess.run([str(binary)], text=True, capture_output=True, timeout=10)
        if result.returncode:
            print(result.stdout, end='')
            print(result.stderr, end='')
            raise SystemExit(result.returncode)
        status_lines = [line for line in result.stdout.splitlines() if line.startswith('RPICO ')]
        assert len(status_lines) == 3
        status = json.loads(status_lines[0].removeprefix('RPICO '))
        assert status['device'] == 'read-pico' and status['version'] == 'maintenance-test'
        assert status['usb_background_supported'] is True and status['usb_background'] is False
        assert status['usb_debug_restored'] and not status['usb_active'] and not status['usb_held']
        assert not status['usb_connected'] and not status['usb_host_ejected'] and status['usb_error'] == 0
        assert status['mounted'] and status['read_verified'] and status['write_verified']
        assert status['keep_awake'] is True
        assert status['power'] == {'known': True, 'charging': True, 'external_power': True, 'battery_percent': 86, 'revision': 7}
        assert status['bytes'] == 64000000000 and status['free'] == 63000000000
        assert status['transfer_url'] == 'http://192.168.4.1' and status['done'] == 7
        wifi = json.loads(status_lines[1].removeprefix('RPICO '))
        assert wifi['wifi_status'] and wifi['enabled'] and wifi['scanning']
        assert wifi['network_count'] == 4 and wifi['scan_error'] == 0x3006
        assert wifi['scan_failure'] == 1 and wifi['result_seq'] == 9
        assert 'ssid' not in wifi
        clock = json.loads(status_lines[2].removeprefix('RPICO '))
        assert clock['time_status'] and clock['rtc_ready'] and clock['rtc_ok'] and clock['rtc_synced']
        assert isinstance(clock['system_utc'], int) and clock['system_utc'] > 1700000000
        assert clock['rtc_utc'] == 1792382400 and clock['source'] == 3 and clock['ntp_state'] == 4
        assert not clock['ntp_pending'] and clock['last_ntp_utc'] == 1792382400
        assert not any(word in result.stdout.lower() for word in ('password', 'credential', 'token', 'private_'))
        check_wire_boundaries(result.stdout)
        assert hashes == {name: hashlib.sha256((ROOT / name).read_bytes()).hexdigest() for name in names}, 'Production source changed during the test; rerun'
        if args.output:
            parsed = re.search(r'PASS maintenance: (\d+) checks, (\d+) scenarios', result.stdout)
            args.output.parent.mkdir(parents=True, exist_ok=True)
            args.output.write_text(json.dumps({'result': 'PASS', 'version': version, 'hardware_access': False,
                'source_sha256': hashes, 'runs': [{'sanitized': args.sanitize, 'checks': int(parsed[1]), 'scenarios': int(parsed[2]), 'wire_replies': 9}],
                'boundary': 'actual maintenance parser through pipe stdin; includes standalone BOARD STATUS framing, no hardware I/O'}, indent=2) + '\n')
        for line in result.stdout.splitlines():
            if line and not line.startswith(('RPICO ', 'WIRE ')):
                print(line)
if __name__ == '__main__':
    main()

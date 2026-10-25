#!/usr/bin/env python3
"""Bambu账号登录的离线协议/并发测试，不使用真实账户。
Offline Bambu protocol/concurrency tests, with no production accounts.
"""
from pathlib import Path
import os
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
COMPONENT = ROOT / 'components/read_pico_home_link'

with tempfile.TemporaryDirectory(prefix='pico-bambu-auth-') as temporary:
    private = Path(temporary)
    private.chmod(0o700)
    flags = [os.environ.get('CC', 'cc'), '-std=c11', '-Wall', '-Wextra', '-Werror', '-g',
             '-fsanitize=address,undefined', '-fno-omit-frame-pointer', '-I', str(COMPONENT),
             '-I', str(COMPONENT/'include'), '-I', str(ROOT/'managed_components/espressif__cjson/cJSON')]
    sources = [COMPONENT/'bambu_auth.c', COMPONENT/'home_link_json.c']
    binary = private/'protocol-test'
    subprocess.run([*flags, str(ROOT/'tools/bambu_auth_host_test.c'), *map(str, sources), '-lm', '-o', str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
    # 测试只替换生产工作器的网络传输函数；会话、排队、互斥、时限及持久化代码仍逐字编译。
    # Replace only the production worker's transport initializer; compile session/queue/locks/deadlines/storage verbatim.
    runtime = (COMPONENT/'read_pico_home_link.c').read_text()
    needle = 'rp_home_transport_t transport = {http_request, mqtt_report, NULL};'
    assert runtime.count(needle) == 2
    runtime = runtime.replace(needle, 'rp_home_transport_t transport = {fixture_http, mqtt_report, NULL};')
    (private/'bambu_runtime_test.inc').write_text(runtime)
    binary = private/'runtime-test'
    subprocess.run([*flags, '-I', str(private), str(ROOT/'tools/bambu_auth_runtime_test.c'), *map(str, sources),
                    str(COMPONENT/'home_link_model.c'), '-pthread', '-lm', '-o', str(binary)], check=True)
    environment = dict(os.environ, RPICO_HOME_LINK_OFFLINE='1', RPICO_HOME_LINK_CONFIG=str(private/'runtime-config.bin'))
    subprocess.run([str(binary)], env=environment, check=True, timeout=25)
    environment['RPICO_HOME_LINK_CONFIG'] = str(private/'account-restart.bin')
    subprocess.run([str(binary), 'persist-seed'], env=environment, check=True, timeout=10)
    persisted = Path(environment['RPICO_HOME_LINK_CONFIG']).read_bytes()
    subprocess.run([str(binary), 'persist-load'], env=environment, check=True, timeout=10)
    assert Path(environment['RPICO_HOME_LINK_CONFIG']).read_bytes() == persisted

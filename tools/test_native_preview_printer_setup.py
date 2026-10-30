#!/usr/bin/env python3
"""Frozen printer setup: raw-v1 runtime migration, real UI drafts and isolated HTTP.
Synthetic credentials only; bootstrap forces simulated Wi-Fi offline before any route/tick.
Never contact accounts, devices, browsers or existing preview services.
"""
from __future__ import annotations
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
from unittest.mock import patch

from test_native_preview_power_options import Case, HttpCase, Runtime, SERVER, sha, verify_frozen

ROOT = Path(__file__).resolve().parents[1]
FIELDS = {1: (0, 1, 10, 2, 11, 12), 2: (0, 1, 4, 2, 6, 7, 11, 12),
          3: (0, 1, 3, 2, 5, 6, 11, 12), 4: (0, 1, 8, 9, 2, 11, 12)}
SENTINELS = (b'SYNTHETIC_HA_SECRET', b'SYNTHETIC_FORM_SECRET', b'SYNTHETIC_CLOUD_TOKEN',
             b'SYNTHETIC_LAN_CODE', b'FOREIGN_MQTT_SECRET', b'FOREIGN_FORM_SECRET')


def helper_run(helper, mode, file):
    result = subprocess.run([str(helper), mode, str(file)], check=True, text=True, capture_output=True,
                            env={**os.environ, 'RPICO_HOME_LINK_CONFIG': str(file)})
    return result.stdout.strip()


def compile_helper(source, output):
    binary = output/'printer-config-host-test'
    files = [ROOT/'tools/printer_setup_runtime_host_test.c', *[source/'components/read_pico_home_link'/name
             for name in ('home_link_model.c', 'home_link_json.c', 'bambu_auth.c', 'read_pico_home_link.c')]]
    command = [os.environ.get('CC', 'cc'), '-std=c11', '-Wall', '-Wextra', '-Werror', '-g',
               '-fsanitize=address,undefined', '-fno-omit-frame-pointer',
               '-I', str(source/'components/read_pico_home_link/include'),
               '-I', str(source/'managed_components/espressif__cjson/cJSON'),
               *map(str, files), '-pthread', '-lm', '-o', str(binary)]
    subprocess.run(command, check=True, capture_output=True)
    return binary


class NativePrinter(Case):
    def __init__(self, source, build, output, helper):
        super().__init__(output)
        self.session = output/'session'
        (self.session/'tf/books').mkdir(parents=True)
        (self.session/'private').mkdir(mode=0o700)
        self.config = self.session/'private/home-link.dat'
        helper_run(helper, 'seed', self.config)
        self.runtime = Runtime(build/'native-preview', source/'sdcard/fonts/MiSansVF.ttf', self.session, output)

    def start(self):
        with patch.dict(os.environ, {'RPICO_HOME_LINK_CONFIG': str(self.config)}):
            self.remember('process-start', self.runtime.start())
        return self.command('scenario offline')

    def command(self, value):
        return self.remember(value, self.runtime.command(value))

    def frame_bytes(self):
        return self.runtime.frame.read_bytes()

    def restart(self):
        self.runtime.close()
        return self.start()

    def close(self):
        self.runtime.close()


class HttpPrinter(HttpCase):
    def __init__(self, source, build, output, helper):
        super().__init__(source, build, output)
        module = self.module
        OriginalSession = module.Session

        class OfflineSession(OriginalSession):
            def __init__(self, session_id=None):
                session_id = session_id or module.uuid.uuid4().hex
                private = module.BUILD/'sessions'/session_id/'private'
                private.mkdir(mode=0o700, parents=True, exist_ok=True)
                file = private/'home-link.dat'
                if not file.exists():
                    helper_run(helper, 'seed', file)
                # Session's initial status never ticks or queues home requests. The first step
                # is the supported native fixture scenario, before returning an HTTP session.
                super().__init__(session_id)
                self.state = self.command('scenario offline')

        module.Session = OfflineSession

    def start(self):
        state = self.request('/api/session', {})
        self.id = state['session']
        self.session = self.module.SESSIONS[self.id].root
        self.config = self.session/'private/home-link.dat'
        return self.remember('http-offline-fixture-session', state)


class PrinterUI:
    def __init__(self, case):
        self.case, self.page = case, 0

    def begin(self, slot=0, landscape=False):
        c = self.case
        c.command('key 1 long')
        c.command(f'rotate {int(landscape)}')
        c.command('route 8')
        c.tap(120, 118+slot*(53 if landscape else 84)+(21 if landscape else 34))
        self.page = 0

    def field(self, source, field):
        c = self.case
        rows = 2 if c.state['logical_width'] > c.state['logical_height'] else 4
        position = FIELDS[source].index(field)
        page = position//rows
        while self.page < page:
            c.tap(c.state['logical_width']-66, c.state['logical_height']-71)
            self.page += 1
        while self.page > page:
            c.tap(66, c.state['logical_height']-71)
            self.page -= 1
        c.tap(50, 155+(position % rows)*70)

    def source(self, source):
        c = self.case
        self.field(4, 0)  # source is the first field for every form
        landscape = c.state['logical_width'] > c.state['logical_height']
        i = source-1
        c.tap(60+(i % 2)*312 if landscape else 80, 153+(i//2)*80 if landscape else 153+i*88)
        self.page = 0

    def secret_x(self):
        c = self.case
        assert c.state['route'] == 19, c.state
        landscape = c.state['logical_width'] > c.state['logical_height']
        # Exact center of X in the shared phone-style EN QWERTY grid.
        c.tap(344 if landscape else 110, 242 if landscape else 508)
        c.tap(c.state['logical_width']-46, 47)
        assert c.state['route'] != 19, c.state


def exercise(case, helper):
    c = case
    c.start()
    assert c.state['services']['connected'] is False, c.state
    # Init has cleaned the v1 disk blob. Every later configuration operation stays offline.
    helper_run(helper, 'inspect', c.config)
    original = c.config.read_bytes()
    assert not any(secret in original for secret in (b'FOREIGN_MQTT_SECRET', b'FOREIGN_FORM_SECRET'))
    c.check('raw-v1-cleaned-into-v2-with-three-original-sources-ha-and-private-file-retained')
    ui = PrinterUI(c)
    for landscape in (False, True):
        for source in range(1, 5):
            ui.begin(0, landscape)
            ui.source(source)
            rows = 2 if landscape else 4
            for page in range((len(FIELDS[source])+rows-1)//rows):
                if page:
                    c.tap(c.state['logical_width']-66, c.state['logical_height']-71)
                    ui.page += 1
                c.capture(f'printer-source-{source}-{int(landscape)}-page-{page}')
            assert c.config.read_bytes() == original
            c.command('key 1 single')
            assert c.config.read_bytes() == original
        c.check('four-source-forms-and-source-switching-cancel-without-persistence-'+str(int(landscape)))
    c.command('rotate 0')
    ui.begin()
    ui.field(4, 9)
    assert c.state['route'] == 19
    c.capture('formlabs-secret-editor-starts-empty')
    c.command('key 1 single')
    assert c.state['route'] == 8 and c.config.read_bytes() == original
    c.check('secret-editor-cancel-does-not-replace-current-saved-secret')
    ui.field(4, 9)
    ui.secret_x()
    assert c.config.read_bytes() == original
    blocked = Path(str(c.config)+'.tmp')
    blocked.mkdir(mode=0o700)
    try:
        ui.field(4, 11)
        c.tick(1)
        assert c.state['route'] == 8 and c.config.read_bytes() == original
        c.capture('save-failure-keeps-draft-and-current-config')
    finally:
        blocked.rmdir()
    ui.field(4, 11)
    c.tick(1)
    assert c.config.read_bytes() != original
    helper_run(helper, 'inspect-x', c.config)
    c.check('failed-draft-save-retains-old-file-and-draft-retry-commits-only-current-slot')
    committed = c.config.read_bytes()
    assert c.config.stat().st_mode & 0o777 == 0o600
    ui.begin()
    ui.field(4, 9)
    c.command('route 0')
    c.command('route 8')
    c.tap(120, 152)
    ui.page = 0
    ui.field(4, 11)
    c.tick(1)
    assert c.config.read_bytes() == committed
    c.check('leaving-secret-editor-and-reopening-printer-never-saves-uncommitted-edit')
    c.restart()
    assert c.state['services']['connected'] is False
    assert c.config.read_bytes() == committed
    helper_run(helper, 'inspect-x', c.config)
    c.check('saved-source-and-short-secret-replacement-survive-process-restart')
    # Regression: a printer draft must not swallow HA field callbacks after global navigation.
    ui.begin()
    c.command('route 7')
    c.command('rotate 0')
    c.tap(40, 382)  # HA token: page 0, fourth row.
    assert c.state['route'] == 19, c.state
    ui.secret_x()
    c.tick(1)
    assert c.config.read_bytes() != committed and b'SYNTHETIC_HA_SECRET' not in c.config.read_bytes()
    assert b'SYNTHETIC_FORM_SECRET' not in c.config.read_bytes()
    c.check('global-navigation-printer-draft-to-ha-settings-persists-ha-callback')
    for file in c.session.rglob('*'):
        if not file.is_file() or file == c.config or file.suffix.lower() in ('.ttf', '.pga'):
            continue
        assert not any(secret in file.read_bytes() for secret in SENTINELS), 'Credential leaked outside private config: '+str(file)
    assert not any(secret.decode() in json.dumps(c.transcript) for secret in SENTINELS)
    c.check('synthetic-secrets-never-in-json-transcript-runtime-logs-or-virtual-tf')
    return c.checks


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source-root', type=Path, required=True)
    parser.add_argument('--build-dir', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--mode', choices=('native', 'http', 'both'), default='both')
    args = parser.parse_args()
    source, build, output = args.source_root.resolve(), args.build_dir.resolve(), args.output.resolve()
    assert not output.exists() or not any(output.iterdir()), 'Use a fresh evidence directory'
    output.mkdir(parents=True, exist_ok=True)
    manifest, manifest_hash = verify_frozen(source, build)
    baseline_hash = sha(SERVER.with_name('baseline.json'))
    helper = compile_helper(source, output)
    raw_dir = output/'raw-runtime'
    raw_dir.mkdir(mode=0o700)
    config = raw_dir/'settings.bin'
    raw_checks = []
    for mode in ('seed', 'load', 'save', 'reload', 'fail', 'seed', 'migrate-fail', 'load'):
        raw_checks.append(helper_run(helper, mode, config))
    runs = {}
    try:
        for name, cls in (('native', NativePrinter), ('http', HttpPrinter)):
            if args.mode not in (name, 'both'):
                continue
            case = cls(source, build, output/name, helper)
            try:
                runs[name] = {'result': 'PASS', 'checks': exercise(case, helper), 'commands': len(case.transcript)}
            finally:
                (output/name/'transcript.json').write_text(json.dumps(case.transcript, ensure_ascii=False, indent=2)+'\n')
                case.close()
        assert verify_frozen(source, build)[1] == manifest_hash
        assert sha(SERVER.with_name('baseline.json')) == baseline_hash
        report = {'result': 'PASS', 'version': manifest['version'], 'native_sha256': manifest['binary_sha256'],
                  'manifest_sha256': manifest_hash, 'source_files_verified': len(manifest['sources']),
                  'raw_runtime_sanitized_checks': raw_checks, 'runs': runs,
                  'synthetic_credentials_only': True, 'network_fixture_forced_offline': True,
                  'accounts_or_devices_accessed': False, 'browser_access': False,
                  'existing_services_accessed': False, 'baseline_unchanged': True}
        (output/'report.json').write_text(json.dumps(report, ensure_ascii=False, indent=2)+'\n')
        print(f'PASS: raw migration/storage runtime + {sum(len(run["checks"]) for run in runs.values())} native/HTTP printer setup scenarios; {output/"report.json"}')
    except BaseException as error:
        (output/'failure.json').write_text(json.dumps({'result': 'FAIL', 'error': repr(error), 'completed_runs': runs}, ensure_ascii=False, indent=2)+'\n')
        raise


if __name__ == '__main__':
    main()

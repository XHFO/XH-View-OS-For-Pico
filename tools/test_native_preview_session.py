#!/usr/bin/env python3
"""Exercise the real preview Session and native child without binding ports or touching existing services."""
from pathlib import Path
import argparse
import hashlib
import importlib.util
import json
import os
import stat
import tempfile

ROOT = Path(__file__).resolve().parents[1]
SERVER = ROOT.parents[1] / 'outputs/firmware-preview/server.py'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    binary, output = args.binary.resolve(), args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    spec = importlib.util.spec_from_file_location('private_session_server', SERVER)
    server = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(server)
    baseline = SERVER.with_name('baseline.json')
    baseline_hash = hashlib.sha256(baseline.read_bytes()).hexdigest()
    checks, environments = [], []
    original_popen = server.subprocess.Popen
    def spawn(*arguments, **kwargs):
        environments.append(kwargs['env'].copy())
        return original_popen(*arguments, **kwargs)
    server.subprocess.Popen = spawn
    session = None
    try:
        with tempfile.TemporaryDirectory(prefix='session-privacy-', dir=output) as temporary:
            server.BUILD = Path(temporary)
            (server.BUILD / 'native-preview').symlink_to(binary)
            session_id = '1' * 32
            root = server.BUILD / 'sessions' / session_id
            books = root / 'tf/books'
            books.mkdir(parents=True)
            book = books / 'preserved.txt'
            payload = '这是既有预览的合成图书，重启后必须保留。'.encode()
            book.write_bytes(payload)
            private_file = root / 'private/home-link.dat'
            for attempt in range(2):
                session = server.Session(session_id)
                assert session.id == session_id and session.root == root
                assert stat.S_IMODE(root.stat().st_mode) == 0o700
                assert stat.S_IMODE(session.private.stat().st_mode) == 0o700
                assert session.private.parent == root and session.private != root / 'tf'
                assert environments[-1]['RPICO_HOME_LINK_CONFIG'] == str(private_file)
                assert all(environments[-1][key] == value for key, value in os.environ.items() if key != 'RPICO_HOME_LINK_CONFIG')
                assert book.read_bytes() == payload
                for route in (25, 26):
                    command = server.command_text({'action': 'route', 'route': route})
                    state = session.command(command)
                    assert state['route'] == route, state
                assert '家庭服务未配置' in session.state['environment']
                session.close()
                session = None
                if attempt == 0:
                    # Deliberately invalid fixture: backend ignores it but Session must never delete private data.
                    descriptor = os.open(private_file, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
                    with os.fdopen(descriptor, 'wb') as stream:
                        stream.write(b'private-invalid-fixture')
                else:
                    assert private_file.read_bytes() == b'private-invalid-fixture'
                    assert stat.S_IMODE(private_file.stat().st_mode) == 0o600
            checks += ['real-native-child-inherits-session-specific-private-config-path', 'session-and-private-directories-0700', 'TF-book-and-private-file-preserved-on-restart', 'private-file-outside-TF-with-0600-preserved', 'date25-and-power26-routes-reachable', 'default-unconfigured-network-explanation']
            try:
                server.command_text({'action': 'route', 'route': 27})
            except ValueError:
                checks.append('out-of-range-route-rejected')
            else:
                raise AssertionError('route 27 accepted')
    finally:
        if session is not None:
            session.close()
        server.subprocess.Popen = original_popen
    assert hashlib.sha256(baseline.read_bytes()).hexdigest() == baseline_hash
    checks.append('installed-baseline-unchanged-no-server-port-or-browser')
    report = {'result': 'PASS', 'binary_sha256': hashlib.sha256(binary.read_bytes()).hexdigest(), 'checks': checks, 'hardware_access': False, 'server_port_bound': False}
    (output / 'report.json').write_text(json.dumps(report, ensure_ascii=False, indent=2) + '\n')
    print(json.dumps(report, ensure_ascii=False, indent=2))


if __name__ == '__main__':
    main()

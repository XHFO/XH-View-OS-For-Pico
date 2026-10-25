#!/usr/bin/env python3
"""中文：验证六个独立游戏包、原生导出和生产 PGA1 校验器，不访问设备。
English: Validate six game packages, native exports and the production PGA1 validator without hardware.
"""
from __future__ import annotations
import argparse
import ctypes
import hashlib
import json
import os
from pathlib import Path
import struct
import subprocess
import tempfile
import zlib

from build_game_packages import GAMES, ROOT, build_packages, native_target, package_bytes, source_digest, verify_elf_manifest
from build_game_elf import check_elf


class Exports(ctypes.Structure):
    _fields_ = [(name, ctypes.c_uint32) for name in ('abi', 'bytes', 'version', 'kind')] + [('id', ctypes.c_char_p)]


def recalc(data):
    data = bytearray(data)
    struct.pack_into('<I', data, 232, 0)
    struct.pack_into('<I', data, 232, zlib.crc32(data[:512]))
    return bytes(data)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--packages', type=Path)
    parser.add_argument('--output', type=Path, default=ROOT / 'build/game-package-tests')
    args = parser.parse_args()
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    packages = args.packages.resolve() if args.packages else output / 'packages'
    if args.packages is None:
        build_packages(packages, native_only=True)
    report = {'result': 'RUNNING', 'native_target': native_target(), 'hardware_access': False, 'packages': [], 'negative_cases': []}
    with tempfile.TemporaryDirectory(prefix='pico-game-packages-') as temporary:
        temporary = Path(temporary)
        harness = temporary / 'validator.c'
        harness.write_text('''#include "pico_app_package.h"
#include <stdio.h>
#include <stdlib.h>
int main(int argc,char **argv){if(argc!=3)return 2;pico_app_package_t package;char error[160];
int ok=pico_app_package_read(argv[1],&package,true,error,sizeof(error));
if(ok!=atoi(argv[2])){fprintf(stderr,"validator mismatch: %s: %s\\n",argv[1],error);return 1;}
if(ok&&!pico_app_package_payload(&package)){fprintf(stderr,"missing native payload\\n");return 1;}return 0;}
''')
        validator = temporary / 'validator'
        subprocess.run([os.environ.get('CC', 'cc'), '-std=c11', '-Wall', '-Wextra',
                        '-fsanitize=address,undefined', '-fno-omit-frame-pointer',
                        '-I', str(ROOT / 'components/read_pico_apps/include'), str(harness),
                        str(ROOT / 'components/read_pico_apps/pico_app_package.c'), '-o', str(validator)], check=True)
        def validate(path, accepted):
            subprocess.run([str(validator), str(path), '1' if accepted else '0'], check=True)
        catalog = json.loads((packages / 'catalog.json').read_text())
        assert catalog['format'] == 'PGA1' and catalog['abi'] == 1 and len(catalog['games']) == 6
        digest, sources = source_digest()
        libraries = []
        native_payload = None
        device_payloads = {}
        for kind, (game_id, title, description) in enumerate(GAMES):
            item = catalog['games'][kind]
            path = packages / (game_id + '.pga')
            data = path.read_bytes()
            assert item['id'] == game_id and item['kind'] == kind and item['title'] == title
            assert item['bytes'] == len(data) and item['sha256'] == hashlib.sha256(data).hexdigest()
            # 发行载荷不包含本机调试或动态库输出目录。/ Release payloads contain no local debug or dylib output paths.
            assert all(private not in data for private in (b'/' + b'Users/', b'/' + b'private/var/', b'/' + b'home/'))
            assert data[:8] == b'PICOAPP1'
            assert struct.unpack_from('<6I', data, 8) == (1, 512, len(data), 1, 10001, kind)
            for offset, length, text in ((32, 32, game_id), (64, 64, title), (128, 96, description)):
                encoded = text.encode()
                assert data[offset:offset + len(encoded)] == encoded
                assert data[offset + len(encoded):offset + length] == bytes(length - len(encoded))
            assert data[448:480] == digest
            header = bytearray(data[:512])
            recorded_crc = struct.unpack_from('<I', header, 232)[0]
            struct.pack_into('<I', header, 232, 0)
            assert zlib.crc32(header) == recorded_crc
            assert header[236:256] == bytes(20) and header[480:512] == bytes(32)
            count = struct.unpack_from('<I', header, 228)[0]
            assert count in (1, 2)
            assert header[256 + count * 32:448] == bytes(192 - count * 32)
            cursor = 512
            for index in range(count):
                offset = 256 + index * 32
                target = data[offset:offset + 16].split(b'\0')[0].decode()
                start, size, crc, reserved = struct.unpack_from('<4I', data, offset + 16)
                assert start == cursor and reserved == 0
                code = data[start:start + size]
                assert len(code) == size and zlib.crc32(code) == crc
                cursor += size
                if target == native_target():
                    native_payload = code
                    library_path = temporary / (game_id + ('.dylib' if os.sys.platform == 'darwin' else '.so'))
                    library_path.write_bytes(code)
                    library = ctypes.CDLL(str(library_path))
                    libraries.append(library)
                    query = library.pico_app_query
                    query.restype = ctypes.POINTER(Exports)
                    exports = query().contents
                    assert (exports.abi, exports.version, exports.kind, exports.id.decode()) == (1, 10001, kind, game_id)
                elif target == 'esp32s3-xtensa':
                    elf_path = temporary / (game_id + '.elf')
                    elf_path.write_bytes(code)
                    check_elf(elf_path)
                    device_payloads[game_id] = code
            assert cursor == len(data)
            validate(path, True)
            report['packages'].append({'id': game_id, 'bytes': len(data), 'sha256': item['sha256'], 'native_query': 'PASS'})
        assert native_payload
        original = (packages / 'gomoku.pga').read_bytes()
        cases = {'truncated-header': original[:256], 'truncated-payload': original[:-1],
                 'trailing-byte': original + b'x'}
        damaged = bytearray(original); damaged[511] ^= 1; cases['header-crc'] = damaged
        damaged = bytearray(original); damaged[-1] ^= 1; cases['payload-crc'] = damaged
        damaged = bytearray(original); struct.pack_into('<I', damaged, 20, 2); cases['incompatible-abi'] = recalc(damaged)
        damaged = bytearray(original); struct.pack_into('<I', damaged, 272, 513); cases['payload-gap'] = recalc(damaged)
        damaged = bytearray(original); struct.pack_into('<I', damaged, 228, 5); cases['too-many-payloads'] = recalc(damaged)
        for label, data in cases.items():
            path = temporary / (label + '.pga'); path.write_bytes(data); validate(path, False)
            report['negative_cases'].append(label)
        for label, kwargs in (
            ('unsafe-id', {'game_id': '../outside'}), ('empty-payload', {'payloads': [(native_target(), b'')]}),
            ('oversized-payload', {'payloads': [(native_target(), bytes(2 * 1024 * 1024 + 1))]}),
            ('duplicate-target', {'payloads': [(native_target(), b'a'), (native_target(), b'b')]}),
            ('oversized-package', {'payloads': [('macos-arm64', bytes(2 * 1024 * 1024)), ('esp32s3-xtensa', bytes(2 * 1024 * 1024))]}),
        ):
            values = dict(game_id='gomoku', title='五子棋', description='测试', kind=0,
                          payloads=[(native_target(), native_payload)], digest=digest)
            values.update(kwargs)
            try:
                package_bytes(**values)
            except ValueError:
                report['negative_cases'].append('builder-' + label)
            else:
                raise AssertionError('Builder accepted ' + label)
        report['sources'] = sources
        if device_payloads:
            assert len(device_payloads) == 6
            build_report = json.loads((packages / 'build-report.json').read_text())
            manifest = build_report['device_build']
            manifest_path = temporary / 'manifest.json'
            manifest_path.write_text(json.dumps(manifest))
            verify_elf_manifest(temporary, digest, sources)
            for label, modify in (
                ('stale-source', lambda value: value.update(source_sha256='0' * 64)),
                ('wrong-target', lambda value: value.update(target='macos-arm64')),
                ('missing-game', lambda value: value['games'].pop('snake')),
                ('wrong-elf-hash', lambda value: value['games']['gomoku'].update(sha256='0' * 64)),
            ):
                changed = json.loads(json.dumps(manifest)); modify(changed)
                manifest_path.write_text(json.dumps(changed))
                try:
                    verify_elf_manifest(temporary, digest, sources)
                except ValueError:
                    report['negative_cases'].append('elf-manifest-' + label)
                else:
                    raise AssertionError('Builder accepted stale manifest: ' + label)
            report['device_elf_payloads'] = {game_id: hashlib.sha256(data).hexdigest() for game_id, data in device_payloads.items()}
    report['result'] = 'PASS'
    (output / 'report.json').write_text(json.dumps(report, ensure_ascii=False, indent=2) + '\n')
    print(f"PASS: {len(report['packages'])} independent native packages, {len(report['negative_cases'])} rejection cases; {output / 'report.json'}")


if __name__ == '__main__':
    main()

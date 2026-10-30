#!/usr/bin/env python3
"""Verify the real boot-splash HTTP route with temporary selected source trees; no native runtime or device access."""
from __future__ import annotations

import argparse
import hashlib
from http.client import HTTPConnection
import importlib.util
import json
from pathlib import Path
import queue
import struct
import sys
import tempfile
import threading
import zlib


ROOT = Path(__file__).resolve().parents[1]
SERVER = ROOT.parents[1] / 'outputs/firmware-preview/server.py'
WIDTH, HEIGHT = 684, 1216


def decode_png(data):
    """Independent PNG reader checks CRC, dimensions, bit depth and each decoded scanline."""
    assert data[:8] == b'\x89PNG\r\n\x1a\n'
    at, compressed, dimensions = 8, bytearray(), None
    while at < len(data):
        size = struct.unpack_from('!I', data, at)[0]
        kind, body = data[at + 4:at + 8], data[at + 8:at + 8 + size]
        assert zlib.crc32(kind + body) & 0xffffffff == struct.unpack_from('!I', data, at + 8 + size)[0]
        if kind == b'IHDR':
            width, height, depth, color, compression, filtering, interlace = struct.unpack('!2I5B', body)
            assert (depth, color, compression, filtering, interlace) == (8, 0, 0, 0, 0)
            dimensions = width, height
        elif kind == b'IDAT':
            compressed.extend(body)
        at += size + 12
    assert at == len(data) and kind == b'IEND' and dimensions == (WIDTH, HEIGHT)
    decoded = zlib.decompress(compressed)
    assert len(decoded) == (WIDTH + 1) * HEIGHT
    pixels = bytearray()
    previous = bytearray(WIDTH)
    for row in range(HEIGHT):
        start = row * (WIDTH + 1)
        filtering = decoded[start]
        assert filtering in range(5)
        current = bytearray(decoded[start + 1:start + 1 + WIDTH])
        for column in range(WIDTH):
            left = current[column - 1] if column else 0
            above = previous[column]
            diagonal = previous[column - 1] if column else 0
            if filtering == 1:
                predictor = left
            elif filtering == 2:
                predictor = above
            elif filtering == 3:
                predictor = (left + above) // 2
            elif filtering == 4:
                estimate = left + above - diagonal
                distances = (abs(estimate - left), abs(estimate - above), abs(estimate - diagonal))
                predictor = (left, above, diagonal)[distances.index(min(distances))]
            else:
                predictor = 0
            current[column] = (current[column] + predictor) % 256
        pixels.extend(current)
        previous = current
    return bytes(pixels)


def source_fixture(directory, raw):
    source, build = directory / 'source', directory / 'build'
    (source / 'main/assets').mkdir(parents=True)
    (source / 'tools').mkdir()
    (source / 'tools/native_preview_build.py').write_text('# No builds are invoked in this fixture.\n')
    (source / 'main/assets/loading_4bpp.bin').write_bytes(raw)
    build.mkdir()
    binary = b'not executable; the splash must not start a native process'
    (build / 'native-preview').write_bytes(binary)
    (build / 'native-preview-build.json').write_text(json.dumps({
        'version': 'boot-source-fixture', 'source_root': str(source), 'sources': {},
        'binary_sha256': hashlib.sha256(binary).hexdigest(),
    }))
    # A misleading generated preview cannot be used in place of the selected source raster.
    (build / 'loading.png').write_bytes(b'not the source boot image')
    return source.resolve(), build.resolve()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    checks, source_results = [], []
    baseline = SERVER.with_name('baseline.json').read_bytes()
    with tempfile.TemporaryDirectory(prefix='pico-boot-http-') as folder:
        # Different roots prove --source-root affects the served raster, not only bootstrap metadata.
        for number, seed in enumerate((b'\x10\x32\x54\x76\x98\xba\xdc\xfe', b'\x0f\xf0\x5a\xa5')):
            fixture = Path(folder) / str(number)
            raw = (seed * (WIDTH * HEIGHT // (2 * len(seed)) + 1))[:WIDTH * HEIGHT // 2]
            source, build = source_fixture(fixture, raw)
            spec = importlib.util.spec_from_file_location('boot_splash_fixture_' + str(number), SERVER)
            module = importlib.util.module_from_spec(spec)
            spec.loader.exec_module(module)
            started, errors = queue.Queue(), []
            original_http = module.ThreadingHTTPServer
            class FixtureServer(original_http):
                def serve_forever(self, *arguments, **keywords):
                    started.put(self)
                    return super().serve_forever(poll_interval=.02)
            module.ThreadingHTTPServer = FixtureServer
            def no_session(*arguments, **keywords):
                raise AssertionError('Boot splash started a native runtime')
            module.Session = no_session
            previous = sys.argv
            sys.argv = [str(SERVER), '--candidate', '--source-root', str(source), '--build-dir', str(build), '--port', '0']
            def run():
                try:
                    module.main()
                except BaseException as error:
                    errors.append(error)
            worker = threading.Thread(target=run, daemon=True)
            worker.start()
            http = None
            try:
                http = started.get(timeout=10)
                def request(path, host=None):
                    headers = {'Host': host} if host else {}
                    connection = HTTPConnection('127.0.0.1', http.server_port, timeout=10)
                    try:
                        connection.request('GET', path, headers=headers)
                        response = connection.getresponse()
                        return response.status, response.read(), response.headers
                    finally:
                        connection.close()
                status, image, headers = request('/api/boot-splash')
                assert status == 200 and headers['Content-Type'] == 'image/png'
                assert headers['Cache-Control'] == 'no-store' and headers['X-Content-Type-Options'] == 'nosniff'
                pixels = decode_png(image)
                # Known alternating nibble colors check left/right and row ordering independently.
                expected_seed = bytes(gray * 17 for value in seed for gray in (value % 16, value // 16))
                expected = (expected_seed * (WIDTH * HEIGHT // len(expected_seed) + 1))[:WIDTH * HEIGHT]
                assert pixels == expected and all(value % 17 == 0 for value in pixels)
                assert module.REPO == source and not module.SESSIONS
                bootstrap = json.loads(request('/api/bootstrap')[1])
                assert bootstrap['source_root'] == str(source)
                assert request('/api/boot-splash?source_root=/untrusted/path')[1] == image
                assert request('/api/boot-splash', 'remote.invalid')[0] == 400
                asset = source / 'main/assets/loading_4bpp.bin'
                assert asset.read_bytes() == raw
                asset.write_bytes(raw[:-1])
                failure = request('/api/boot-splash')
                assert failure[0] == 400 and '启动位图数据不完整' in json.loads(failure[1])['error']
                asset.unlink()
                assert request('/api/boot-splash')[0] == 400
                assert not module.SESSIONS and (build / 'loading.png').read_bytes() == b'not the source boot image'
                (output / ('source-' + str(number) + '.png')).write_bytes(image)
                source_results.append({'source_index': number, 'pixel_sha256': hashlib.sha256(pixels).hexdigest(),
                                       'png_sha256': hashlib.sha256(image).hexdigest()})
            finally:
                if http:
                    http.shutdown()
                worker.join(timeout=10)
                sys.argv = previous
            assert not worker.is_alive() and not errors, errors
    checks += ['actual-source-root-cli-selects-bitmap', 'known-left-right-nibble-and-row-pixel-order',
               'physical-684x1216-16gray-png', 'crc-and-complete-scanlines', 'no-generated-png-dependency',
               'no-native-session-or-process', 'query-cannot-override-source', 'local-host-only',
               'no-store-and-nosniff', 'incomplete-source-controlled-error', 'missing-source-controlled-error']
    assert source_results[0]['pixel_sha256'] != source_results[1]['pixel_sha256']
    assert SERVER.with_name('baseline.json').read_bytes() == baseline
    report = {'result': 'PASS', 'checks': checks, 'sources': source_results, 'baseline_unchanged': True,
              'existing_service_accessed': False, 'hardware_access': False, 'native_runtime_started': False}
    (output / 'report.json').write_text(json.dumps(report, ensure_ascii=False, indent=2) + '\n')
    print('PASS: ' + str(len(checks)) + ' boot-splash HTTP/source checks; ' + str(output / 'report.json'))


if __name__ == '__main__':
    main()

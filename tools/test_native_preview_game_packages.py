#!/usr/bin/env python3
"""中文：通过真实TF游戏包验证六应用、AI、安装移除与重启；仅使用私有合成会话。
English: Exercise six actual TF game modules, AI, package lifecycle and restart in private synthetic sessions.
"""
from __future__ import annotations
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import struct
import time
import zlib

from build_game_packages import GAMES, ROOT, native_target, package_bytes
from test_native_preview_shelf_ble import Runtime


def saved(session, game_id, key):
    path = session / 'tf/apps/data' / game_id / (key + '.dat')
    assert path.is_file(), (game_id, key, path)
    data = path.read_bytes()
    magic, size, crc = struct.unpack_from('<III', data)
    payload = data[12:]
    assert magic == 0x31535058 and size == len(payload) and zlib.crc32(payload) == crc
    return path, payload


def board_point(kind, point, landscape):
    cols = 15 if kind == 0 else 8 if kind == 3 else 9
    rows = 10 if kind == 2 else cols
    x, y, width, pad = (20, 76, 244 * 8 // 9 if kind == 2 else 244, 12) if landscape else (16, 96, 328, 14)
    height = 244 if landscape else min(364, width * 9 // 8 if kind == 2 else width)
    if kind == 3:
        return x + (2 * (point % 8) + 1) * width // 16, y + (2 * (point // 8) + 1) * height // 16
    return x + pad + (point % cols) * (width - 2 * pad) // (cols - 1), y + pad + (point // cols) * (height - 2 * pad) // (rows - 1)


def open_game(runtime, index, landscape):
    runtime.command('route 13')
    cols, width, height = (3, 196, 82) if landscape else (2, 159, 132)
    state = runtime.tap(16 + (index % cols) * (width + 10) + width // 2,
                        143 + (index // cols) * (height + 10) + height // 2)
    assert state['route'] == (14 if index < 4 else 23) and not state['games_error'], state
    return state


def stage(runtime, package, replace=False, name='test-upload.tmp'):
    directory = runtime.session / 'tf/apps/.staging'
    directory.mkdir(parents=True, exist_ok=True)
    path = directory / name
    path.write_bytes(package)
    return path, f'game-install {1 if replace else 0} {name}'


def wait_moves(runtime, game_id, count):
    deadline = time.monotonic() + 8
    while time.monotonic() < deadline:
        state = runtime.tick(100)
        _, payload = saved(runtime.session, game_id, 'games')
        moves = struct.unpack_from('<H', payload, 6)[0]
        if moves >= count:
            return state, moves
        time.sleep(.04)
    raise AssertionError('AI did not respond: ' + game_id)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', type=Path, default=ROOT / 'build/native-preview-candidate/native-preview')
    parser.add_argument('--packages', type=Path)
    parser.add_argument('--output', type=Path, default=ROOT / 'build/native-game-package-integration')
    args = parser.parse_args()
    binary, output = args.binary.resolve(), args.output.resolve()
    packages = args.packages.resolve() if args.packages else binary.parent / 'game-packages'
    output.mkdir(parents=True, exist_ok=True)
    binary_sha = hashlib.sha256(binary.read_bytes()).hexdigest()
    report = {'result': 'RUNNING', 'binary_sha256': binary_sha, 'hardware_access': False,
              'actual_dynamic_modules': True,
              'game_packages': {game_id: hashlib.sha256((packages / (game_id + '.pga')).read_bytes()).hexdigest() for game_id, _, _ in GAMES},
              'ai': [], 'arcade': [], 'lifecycle': [], 'scenes': {}, 'commands': 0}
    for landscape in (False, True):
        label = 'landscape' if landscape else 'portrait'
        session = output / ('session-' + label)
        if session.exists():
            raise SystemExit('Use a fresh output directory: ' + str(session))
        session.mkdir()
        runtime = Runtime(binary, ROOT / 'sdcard/fonts/MiSansVF.ttf', session, output)
        try:
            state = runtime.start()
            assert state['games_count'] == 6 and state['version'] == '0.21.0-dev24', state
            assert (session / 'tf/apps/.defaults-initialized').exists()
            if args.packages:
                for game_id, _, _ in GAMES:
                    package = (packages / (game_id + '.pga')).read_bytes()
                    path, command = stage(runtime, package, True)
                    state = runtime.command(command)
                    assert not path.exists() and state['games_count'] == 6
                    assert (session / 'tf/apps/games' / (game_id + '.pga')).read_bytes() == package
            if landscape:
                runtime.command('rotate 1')
            state = runtime.command('route 13')
            runtime.capture('launcher-' + label, state)
            for kind, (game_id, _, _) in enumerate(GAMES):
                state = open_game(runtime, kind, landscape)
                runtime.capture(game_id + '-open-' + label, state)
                if kind < 4:
                    # 中文：两方向分别运行入门与挑战，规则层另覆盖全部三档。
                    # English: Run beginner/challenge across orientations; rule tests separately cover all levels.
                    level = 3 if landscape else 1
                    x, y, width = (42, 88, 240) if landscape else (24, 118, 312)
                    button_width = (width - 16) // 3
                    runtime.tap(x + (level - 1) * (button_width + 8) + button_width // 2, y + 51)
                    state = runtime.tap(450 if landscape else 180, 169 if landscape else 380)
                    _, payload = saved(session, game_id, 'games')
                    assert struct.unpack_from('<H', payload, 6)[0] == 0 and payload[8] == 1, (game_id, payload[:16])
                    assert payload[9] == kind and payload[10] == level
                    if kind in (0, 1):
                        runtime.tap(*board_point(kind, 112 if kind == 0 else 40, landscape))
                    else:
                        runtime.tap(*board_point(kind, 54 if kind == 2 else 52, landscape))
                        runtime.tap(*board_point(kind, 45 if kind == 2 else 36, landscape))
                    state, moves = wait_moves(runtime, game_id, 2)
                    runtime.capture(game_id + '-ai-' + label, state)
                    report['ai'].append({'id': game_id, 'orientation': label, 'level': level, 'saved_moves': moves})
                elif kind == 4:
                    runtime.command('key 0 click')
                    state = runtime.command('key 2 click')
                    _, payload = saved(session, game_id, 'arcade1')
                    moves = struct.unpack_from('<I', payload, 76)[0]
                    assert moves > 0
                    runtime.capture('2048-play-' + label, state)
                    report['arcade'].append({'id': game_id, 'orientation': label, 'saved_moves': moves})
                else:
                    runtime.tap(517 if landscape else 263, 286 if landscape else 576)
                    for _ in range(15):
                        time.sleep(.1)
                        runtime.tick(100)
                    state = runtime.tap(517 if landscape else 263, 286 if landscape else 576)
                    _, payload = saved(session, game_id, 'arcade1')
                    moves = struct.unpack_from('<I', payload, 88)[0]
                    assert moves > 0
                    runtime.capture('snake-paused-' + label, state)
                    report['arcade'].append({'id': game_id, 'orientation': label, 'saved_moves': moves})
                runtime.command('route 13')
            if not landscape:
                save_path, payload = saved(session, 'gomoku', 'games')
                backup_save = save_path.with_suffix('.bak')
                recovery_save = save_path.read_bytes()
                backup_save.write_bytes(recovery_save)
                save_path.write_bytes(b'broken-primary')
                open_game(runtime, 0, False)
                runtime.tap(180, 327)
                runtime.command('route 13')
                _, restored = saved(session, 'gomoku', 'games')
                assert struct.unpack_from('<H', restored, 6)[0] == 2
                assert backup_save.read_bytes() == recovery_save
                runtime.capture('save-recovered-from-backup', runtime.command('status'))
                installed_path = session / 'tf/apps/games/gomoku.pga'
                backup_package = installed_path.with_suffix('.pga.bak')
                original_package = installed_path.read_bytes()
                backup_package.write_bytes(original_package)
                installed_path.write_bytes(b'broken-package')
                state = runtime.command('games')
                assert state['games_count'] == 6 and installed_path.read_bytes() == original_package and not backup_package.exists()
                backup_package.write_bytes(original_package)
                installed_path.unlink()
                state = runtime.command('games')
                assert state['games_count'] == 6 and installed_path.read_bytes() == original_package and not backup_package.exists()
                before_pin = save_path.read_bytes()
                open_game(runtime, 0, False)
                runtime.tap(180, 327)
                state = runtime.command('scenario tf-absent')
                assert not state['services']['tfMounted']
                runtime.command('key 0 click')
                runtime.tap(*board_point(0, 113, False))
                runtime.tick(500)
                assert save_path.read_bytes() == before_pin
                runtime.command('route 13')
                runtime.command('scenario tf-ready')
                state = runtime.command('games')
                assert state['games_count'] == 6
                state = runtime.tap(95, 209)
                assert state['route'] == 13
                runtime.capture('pinned-discard-confirmation', state)
                state = runtime.tap(107, 401)
                assert state['route'] == 13 and save_path.read_bytes() == before_pin
                runtime.tap(95, 209)
                state = runtime.tap(253, 401)
                assert state['route'] == 14
                runtime.tap(180, 327)
                runtime.command('route 13')
                _, resumed_after_pin = saved(session, 'gomoku', 'games')
                assert struct.unpack_from('<H', resumed_after_pin, 6)[0] == 2
                runtime.capture('explicit-discard-reopens-saved-game', runtime.command('status'))
                before_save = save_path.read_bytes()
                backup_package.write_bytes(original_package)
                state = runtime.command('game-remove gomoku')
                assert state['games_count'] == 5 and save_path.read_bytes() == before_save
                assert not installed_path.exists() and not backup_package.exists()
                runtime.command('games')
                assert not installed_path.exists()
                runtime.capture('removed-preserved-save', state)
                runtime.close()
                state = runtime.start()
                assert state['games_count'] == 5 and not (session / 'tf/apps/games/gomoku.pga').exists()
                path, command = stage(runtime, (packages / 'gomoku.pga').read_bytes())
                state = runtime.command(command)
                assert state['games_count'] == 6 and not path.exists() and save_path.read_bytes() == before_save
                open_game(runtime, 0, False)
                state = runtime.tap(180, 327)
                _, resumed = saved(session, 'gomoku', 'games')
                assert struct.unpack_from('<H', resumed, 6)[0] == 2
                runtime.capture('reinstalled-resumed', state)
                runtime.command('route 13')
                before_package = (session / 'tf/apps/games/gomoku.pga').read_bytes()
                path, command = stage(runtime, before_package)
                state = runtime.command(command, ok=False)
                assert state['error'].endswith('已安装，请确认替换') and path.exists()
                path.unlink()
                corrupted = bytearray(before_package); corrupted[-1] ^= 1
                path, command = stage(runtime, corrupted, True)
                state = runtime.command(command, ok=False)
                assert (session / 'tf/apps/games/gomoku.pga').read_bytes() == before_package
                path.unlink()
                path, command = stage(runtime, before_package, True)
                state = runtime.command(command)
                assert not path.exists() and state['games_count'] == 6
                state = runtime.command('game-install 0 ../outside.tmp', ok=False)
                assert state['games_count'] == 6
                for game_id, _, _ in GAMES:
                    runtime.command('game-remove ' + game_id)
                runtime.close()
                state = runtime.start()
                assert state['games_count'] == 0
                assert save_path.read_bytes() == before_save
                runtime.command('route 13')
                runtime.capture('empty-after-restart', state)
                report['lifecycle'] = ['native-install', 'default-seed-once', 'remove-preserves-save',
                                       'corrupt-primary-save-recovers-backup', 'save-repair-preserves-good-backup',
                                       'corrupt-package-recovers-backup', 'missing-package-recovers-backup',
                                       'card-loss-blocks-input-and-writes', 'keep-pinned-game-retains-ram',
                                       'explicit-discard-reopens-saved-game', 'removed-backup-does-not-resurrect',
                                       'restart-keeps-removal', 'reinstall-resumes-game', 'replace-requires-confirmation',
                                       'bad-update-preserves-installed-package', 'confirmed-update',
                                       'unsafe-staging-rejected', 'empty-after-restart']
            report['commands'] += len(runtime.transcript)
            report['scenes'].update(runtime.scenes)
            (output / ('transcript-' + label + '.json')).write_text(json.dumps(runtime.transcript, ensure_ascii=False, indent=2) + '\n')
        finally:
            runtime.close()
    assert hashlib.sha256(binary.read_bytes()).hexdigest() == binary_sha, 'Native binary changed during integration'
    report['result'] = 'PASS'
    (output / 'report.json').write_text(json.dumps(report, ensure_ascii=False, indent=2) + '\n')
    print(f"PASS: {len(report['ai'])} real AI replies, {len(report['arcade'])} arcade runs, {len(report['lifecycle'])} lifecycle cases, {len(report['scenes'])} scenes; {output / 'report.json'}")


if __name__ == '__main__':
    main()

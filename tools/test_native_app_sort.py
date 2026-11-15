#!/usr/bin/env python3
"""中文：验证 TF 应用集五种排序、最近打开、重启持久化与删除保留存档。
English: Verify five TF app sort modes, recent opens, restart persistence and save-preserving removal.
"""
from __future__ import annotations
import argparse
import hashlib
import json
import os
from pathlib import Path
import struct
import tempfile
import zlib

from build_game_packages import GAMES, ROOT
from test_native_preview_shelf_ble import Runtime


MODES = ("默认", "名称", "最近", "安装", "版本")
TITLES = {game_id: title for game_id, title, _ in GAMES}


def set_mode(runtime, target):
    for _ in range(len(MODES)):
        state = runtime.command('status')
        if state['games_sort'] == target:
            return state
        runtime.command('game-sort')
    raise AssertionError('Unable to select sort mode: ' + target)


def versioned(package, version):
    data = bytearray(package)
    struct.pack_into('<I', data, 24, version)
    struct.pack_into('<I', data, 232, 0)
    struct.pack_into('<I', data, 232, zlib.crc32(data[:512]))
    return bytes(data)


def replace(runtime, package, version):
    staged = runtime.session / 'tf/apps/.staging/sort-update.tmp'
    staged.parent.mkdir(parents=True, exist_ok=True)
    staged.write_bytes(versioned(package, version))
    state = runtime.command('game-install 1 sort-update.tmp')
    assert state['games_count'] == len(GAMES)
    return state


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', type=Path, default=ROOT / 'build/dual-native-update83/native-preview')
    parser.add_argument('--output', type=Path, default=ROOT / 'build/dual-native-update83-app-sort')
    args = parser.parse_args()
    binary = args.binary.resolve()
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    packages = binary.parent / 'game-packages'
    font = ROOT / 'sdcard/fonts/MiSansVF.ttf'
    session = Path(tempfile.mkdtemp(prefix='session-', dir=output))
    runtime = Runtime(binary, font, session, output)
    report = {
        'result': 'RUNNING',
        'binary_sha256': hashlib.sha256(binary.read_bytes()).hexdigest(),
        'hardware_access': False,
        'modes': [],
        'save_preserved': False,
    }
    try:
        state = runtime.start()
        assert state['games_count'] == len(GAMES) and state['games_sort'] == '默认', state
        runtime.command('route 13')
        state = runtime.tap(190, 102)
        assert state['games_sort'] == '名称', state
        runtime.capture('sort-name-ui', state)

        set_mode(runtime, '默认')
        state = runtime.tap(87, 209)
        assert state['route'] == 14, state
        runtime.command('route 13')
        state = runtime.tap(240, 493)
        assert state['route'] == 23, state
        runtime.command('route 13')
        state = set_mode(runtime, '最近')
        assert state['games_order'][:2] == ['snake', 'gomoku'], state
        report['modes'].append({'name': '最近', 'order': state['games_order']})

        runtime.close()
        state = runtime.start()
        assert state['games_sort'] == '最近' and state['games_order'][:2] == ['snake', 'gomoku'], state
        report['restart_sort'] = state['games_sort']
        report['restart_order'] = state['games_order']

        versions = {'gomoku': 20010, 'go': 20005, 'xiangqi': 30000}
        for game_id, version in versions.items():
            replace(runtime, (packages / (game_id + '.pga')).read_bytes(), version)
        state = set_mode(runtime, '默认')
        assert state['games_order'] == [game_id for game_id, _, _ in GAMES], state
        report['modes'].append({'name': '默认', 'order': state['games_order']})

        state = set_mode(runtime, '名称')
        expected_name = sorted(TITLES, key=lambda game_id: (TITLES[game_id].casefold(), game_id))
        assert state['games_order'] == expected_name, (state['games_order'], expected_name)
        report['modes'].append({'name': '名称', 'order': state['games_order']})

        base = 1_700_000_000
        for index, (game_id, _, _) in enumerate(GAMES):
            os.utime(session / 'tf/apps/games' / (game_id + '.pga'), (base + index, base + index))
        state = runtime.command('games')
        state = set_mode(runtime, '安装')
        expected_installed = [game_id for game_id, _, _ in reversed(GAMES)]
        assert state['games_order'] == expected_installed, (state['games_order'], expected_installed)
        report['modes'].append({'name': '安装', 'order': state['games_order']})

        state = set_mode(runtime, '版本')
        expected_version = sorted(TITLES, key=lambda game_id: (
            -versions.get(game_id, 10001), game_id))
        assert state['games_order'] == expected_version, (state['games_order'], expected_version)
        report['modes'].append({'name': '版本', 'order': state['games_order']})

        state = set_mode(runtime, '默认')
        runtime.command('route 13')
        save = session / 'tf/apps/data/gomoku/games.dat'
        assert save.is_file()
        before = save.read_bytes()
        runtime.command('route 33')
        # 第二页前三个是工具应用，第四个才进入游戏目录。
        # The second page starts with three tools; its fourth card is the first game.
        runtime.tap(310, 590)
        state = runtime.hold(264, 354)
        assert state['route'] == 33
        state = runtime.tap(253, 381)
        assert state['games_count'] == len(GAMES) - 1, state
        assert not (session / 'tf/apps/games/gomoku.pga').exists()
        assert save.read_bytes() == before
        report['save_preserved'] = True
        runtime.capture('removed-package-preserved-save', state)

        assert hashlib.sha256(binary.read_bytes()).hexdigest() == report['binary_sha256']
        report['result'] = 'PASS'
        (output / 'report.json').write_text(json.dumps(report, ensure_ascii=False, indent=2) + '\n')
        print(f"PASS: five app sort modes, restart persistence and save-preserving removal; {output / 'report.json'}")
    finally:
        runtime.close()


if __name__ == '__main__':
    main()

#!/usr/bin/env python3
"""原生设置分组、品牌/测试版标识和真实触摸返回回归；仅私有合成 TF 会话。
Verify grouped native settings, branding, actual touch boundaries and Back navigation without hardware.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import tempfile

from test_native_preview_shelf_ble import ROOT, Runtime


OCR = r'''
import Foundation
import Vision
for path in CommandLine.arguments.dropFirst() {
    let request = VNRecognizeTextRequest()
    request.recognitionLevel = .accurate
    request.recognitionLanguages = ["zh-Hans", "en-US"]
    request.usesLanguageCorrection = false
    try VNImageRequestHandler(url: URL(fileURLWithPath: path)).perform([request])
    let lines: [[String: Any]] = (request.results ?? []).compactMap { observation in
        guard let best = observation.topCandidates(1).first else { return nil }
        let box = observation.boundingBox
        return ["text": best.string, "confidence": best.confidence,
                "box": [box.minX, box.minY, box.width, box.height]]
    }
    let data = try JSONSerialization.data(withJSONObject: ["file": path, "lines": lines], options: [.sortedKeys])
    print(String(data: data, encoding: .utf8)!)
}
'''

# This acceptance geometry is filled from the root implementation contract, never parsed from production code.
ROOT_LABELS = ('存储与传输', '网络与连接', '阅读与输入', '外观与操作', '应用与服务', '系统与电源')
GROUPS = {
    1: (('Wi-Fi', 6), ('手机与电脑管理', 30), ('蓝牙翻页器', 24)),
    2: (('阅读排版', 9), ('笔记输入', 10)),
    3: (('首页导航', 27), ('快捷菜单', 12), ('提示音', 31)),
    4: (('应用集', 33), ('Home Assistant', 7), ('3D 打印机', 8), ('棋类小游戏', 13)),
    5: (('电源与锁屏', 26), ('日期与时间', 25), ('保持唤醒', None)),
}


def root_rect(index, landscape):
    width = 640 if landscape else 360
    columns, rows, top, row_height = (2, 3, 146, 48) if landscape else (1, 6, 158, 60)
    cell_width = (width - 32 - (columns - 1) * 12) // columns
    return 16 + index // rows * (cell_width + 12), top + index % rows * row_height, cell_width, row_height


def child_rect(index, landscape):
    width = 640 if landscape else 360
    columns, rows, top, row_height = (2, 2, 112, 60) if landscape else (1, 4, 126, 76)
    cell_width = (width - 32 - (columns - 1) * 12) // columns
    return 16 + index // rows * (cell_width + 12), top + index % rows * row_height, cell_width, row_height


def about_rect(landscape):
    return 16, 82, (640 if landscape else 360) - 32, 56


def guide_rect(landscape):
    return 16, (360 if landscape else 640) - 110, (640 if landscape else 360) - 32, 44


def center(rect):
    x, y, width, height = rect
    return x + width // 2, y + height // 2


def digest(runtime):
    return hashlib.sha256(runtime.frame.read_bytes()).hexdigest()


def normalized(text):
    # Vision can choose the traditional form for an identical-looking short Chinese label.
    text = text.translate(str.maketrans({'喚': '唤', '閉': '闭', '開': '开'}))
    return ''.join(char.upper() for char in text if char.isalnum())


def keep_awake(session):
    path = session / 'tf/.pico-system/nvs/read_pico_dev_awake'
    value = path.read_bytes() if path.exists() else b'\1'
    assert value in (b'\0', b'\1'), value
    return bool(value[0])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', type=Path, required=True)
    parser.add_argument('--font', type=Path, default=ROOT / 'sdcard/fonts/MiSansVF.ttf')
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--version', help='Expected binary version; defaults to its bound native-preview-build.json manifest')
    args = parser.parse_args()
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    binary = args.binary.resolve()
    if not args.version:
        manifest_path = binary.with_name('native-preview-build.json')
        manifest = json.loads(manifest_path.read_text())
        assert manifest.get('binary_sha256') == hashlib.sha256(binary.read_bytes()).hexdigest(), 'Binary does not match its build manifest'
        args.version = manifest['version']
    os.environ.update(RPICO_HOME_LINK_OFFLINE='1', RPICO_WEREAD_OFFLINE='1')
    report = {'result': 'RUNNING', 'binary_sha256': hashlib.sha256(binary.read_bytes()).hexdigest(),
              'hardware_access': False, 'real_tf_access': False, 'network_access': False,
              'checks': [], 'scenes': {}, 'ocr': []}
    requirements = {}
    try:
        for landscape in (False, True):
            label = 'landscape' if landscape else 'portrait'
            session = Path(tempfile.mkdtemp(prefix=label + '-', dir=output))
            runtime = Runtime(binary, args.font.resolve(), session, output)
            try:
                state = runtime.start()
                assert state['version'] == args.version, state
                runtime.command('rotate ' + str(int(landscape)))
                runtime.command('route 0')
                runtime.settle(5)
                runtime.command('route 5')
                runtime.settle(35)

                def capture(name, texts=(), regions=()):
                    state = runtime.command('status')
                    scene = label + '-' + name
                    runtime.capture(scene, state)
                    report['scenes'][scene] = state
                    requirements[str(output / (scene + '.png'))] = {'texts': list(texts), 'regions': list(regions),
                        'width': state['width'], 'height': state['height']}
                    return digest(runtime)

                root = capture('root-six-groups', (*ROOT_LABELS, 'XH View OS For Pico', '先行预览测试版', args.version),
                               [(title, root_rect(index, landscape)) for index, title in enumerate(ROOT_LABELS)])

                def back(physical=False):
                    state = runtime.command('key 1 click') if physical else runtime.tap(23, 47)
                    runtime.settle(35)
                    return state

                def expect_settings(expected, message):
                    state = runtime.command('status')
                    assert state['route'] == 5 and state['active_app'] == 'system', (message, state)
                    assert digest(runtime) == expected, (message, 'wrong retained settings level', state)

                # The first group is a direct storage/transfer entry, with no redundant child page.
                rect = root_rect(0, landscape)
                for point in ((rect[0] - 3, rect[1] + rect[3] // 2), (rect[0] + rect[2] + 3, rect[1] + rect[3] // 2)):
                    runtime.tap(*point)
                    runtime.settle(3)
                    expect_settings(root, 'Outside direct-storage row activated navigation')
                state = runtime.tap(*center(root_rect(0, landscape)))
                assert state['route'] == 21 and state['active_app'] == 'system', state
                capture('storage-direct-transfer')
                back()
                expect_settings(root, 'Storage Back must restore root settings')
                report['checks'].append(label + ': storage group opens unified transfer directly and returns once')

                for group, children in GROUPS.items():
                    rect = root_rect(group, landscape)
                    # Leave three logical pixels for the host's physical/logical integer-coordinate conversion.
                    for point in ((rect[0] - 3, rect[1] + rect[3] // 2), (rect[0] + rect[2] + 3, rect[1] + rect[3] // 2)):
                        runtime.tap(*point)
                        runtime.settle(3)
                        expect_settings(root, 'Outside group row activated navigation')
                    state = runtime.tap(*center(rect))
                    assert state['route'] == 5 and digest(runtime) != root, (group, state)
                    group_frame = capture('group-' + str(group), [ROOT_LABELS[group], *(title for title, _ in children)],
                                          [(title, child_rect(index, landscape)) for index, (title, _) in enumerate(children)])
                    for index, (title, route) in enumerate(children):
                        rect = child_rect(index, landscape)
                        for point in ((rect[0] - 3, rect[1] + rect[3] // 2), (rect[0] + rect[2] + 3, rect[1] + rect[3] // 2)):
                            runtime.tap(*point)
                            runtime.settle(3)
                            expect_settings(group_frame, 'Outside child row activated ' + title)
                        if route is None:
                            before = keep_awake(session)
                            state = runtime.tap(*center(rect))
                            assert state['route'] == 5 and keep_awake(session) is not before, ('Awake toggle failed', label, state)
                            runtime.settle(35)
                            capture('keep-awake-toggled')
                            runtime.tap(*center(rect))
                            runtime.settle(35)
                            assert keep_awake(session) is before
                            expect_settings(group_frame, 'Awake toggle must preserve group and restore original value')
                            report['checks'].append(label + ': keep-awake row toggles persistent private setting without navigation')
                            continue
                        for physical in (False, True):
                            state = runtime.tap(*center(rect))
                            assert state['route'] == route and state['active_app'] == 'system', (label, group, title, state)
                            capture('group-' + str(group) + '-child-' + str(index) + ('-physical' if physical else '-heading'))
                            back(physical=physical)
                            expect_settings(group_frame, title + (' physical' if physical else ' heading') + ' Back must restore its parent group')
                        report['checks'].append(label + ': ' + title + ' opens expected route; heading and physical Back restore its exact group')
                    back(physical=group % 2 == 0)
                    expect_settings(root, 'Group Back must restore root without leaving settings')
                    # A visible row edge is part of the same full-width target, not just its label/icon.
                    rect = root_rect(group, landscape)
                    runtime.tap(rect[0] + 1, rect[1] + rect[3] // 2)
                    runtime.settle(35)
                    expect_settings(group_frame, 'Inside group edge missed the visible row')
                    back()
                    expect_settings(root, 'Inside-edge return must restore settings root')
                    report['checks'].append(label + ': group ' + str(group) + ' respects both outside bounds and full row touch target')

                state = runtime.tap(*center(about_rect(landscape)))
                assert state['route'] == 5 and digest(runtime) != root, state
                about = capture('about', ('XH View OS For Pico', '先行预览测试版', args.version, '首次使用引导'))
                state = runtime.tap(*center(guide_rect(landscape)))
                assert state['route'] == 15 and state['active_app'] == 'system', state
                capture('about-first-use-guide')
                back(physical=True)
                expect_settings(about, 'Guide Back must restore About inside settings')
                back()
                expect_settings(root, 'About Back must restore settings root')
                back(physical=True)
                state = runtime.command('status')
                assert state['route'] == 0, ('Root Back did not return to the original shelf', state)
                capture('root-back-to-shelf')
                report['checks'].append(label + ': branded About and first-use guide preserve each nested Back level')

                # Every settings level must remain usable after a real process restart with the same private data.
                runtime.command('route 5')
                runtime.tap(*center(root_rect(5, landscape)))
                runtime.tap(*center(child_rect(2, landscape)))
                runtime.settle(35)
                assert not keep_awake(session)
                capture('keep-awake-off-before-restart')
                runtime.close()
                runtime.start()
                runtime.command('rotate ' + str(int(landscape)))
                runtime.command('route 0')
                state = runtime.command('route 5')
                runtime.settle(35)
                assert state['route'] == 5 and not keep_awake(session), state
                capture('restart-root', (*ROOT_LABELS, 'XH View OS For Pico', '先行预览测试版', args.version))
                runtime.tap(*center(root_rect(5, landscape)))
                runtime.settle(35)
                capture('keep-awake-off-after-restart', ('系统与电源', '保持唤醒', '关闭'))
                runtime.tap(*center(child_rect(2, landscape)))
                runtime.settle(35)
                assert keep_awake(session)
                report['checks'].append(label + ': restart retains persistent awake preference and exposes clean settings root')
            finally:
                runtime.close()
                (output / ('transcript-' + label + '.json')).write_text(json.dumps(runtime.transcript, ensure_ascii=False, indent=2) + '\n')

        source, decoder = output / 'settings-ocr.swift', output / 'settings-ocr'
        source.write_text(OCR)
        subprocess.run(['swiftc', str(source), '-o', str(decoder)], check=True)
        # Only scenes with a text contract require Vision OCR; route-only child captures remain available for review.
        selected = {path: expected for path, expected in requirements.items() if expected['texts']}
        response = subprocess.run([str(decoder), *selected], capture_output=True, text=True, check=True)
        for line in response.stdout.splitlines():
            item = json.loads(line)
            expected = selected[item['file']]
            text = normalized(' '.join(value['text'] for value in item['lines']))
            for phrase in expected['texts']:
                assert normalized(phrase) in text, ('Visible settings text missing or clipped', item['file'], phrase, item['lines'])
            for phrase, rect in expected['regions']:
                matching = [value for value in item['lines'] if normalized(phrase) in normalized(value['text'])]
                assert matching, ('Group/row label missing', item['file'], phrase, item['lines'])
                x, y, width, height = rect
                found = False
                for value in matching:
                    bx, by, bw, bh = value['box']
                    logical_x = bx * expected['width'] / 1.9
                    logical_y = (1 - by - bh) * expected['height'] / 1.9
                    logical_right = (bx + bw) * expected['width'] / 1.9
                    logical_bottom = (1 - by) * expected['height'] / 1.9
                    if x - 2 <= logical_x and logical_right <= x + width + 2 and y - 2 <= logical_y and logical_bottom <= y + height + 2:
                        found = True
                        break
                assert found, ('Visible row text overlaps its promised touch bounds', item['file'], phrase, rect, matching)
            report['ocr'].append(item)
        assert len(report['ocr']) == len(selected)
        report['checks'].append('Vision OCR confirms system name, preview-test badge/version, six groups and every child label stay inside matching touch rows')
        report['result'] = 'PASS'
    except BaseException as error:
        report['result'], report['error'] = 'FAIL', repr(error)
        raise
    finally:
        (output / 'report.json').write_text(json.dumps(report, ensure_ascii=False, indent=2) + '\n')
    print('Native grouped settings PASS:', len(report['checks']), 'checks;', len(report['scenes']), 'scenes;', len(report['ocr']), 'OCR-verified pages')


if __name__ == '__main__':
    main()

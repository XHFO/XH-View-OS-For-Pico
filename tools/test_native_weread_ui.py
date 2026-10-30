#!/usr/bin/env python3
"""中文：真实TF微信读书包、手机二维码及返回路径回归；仅私有TF和模拟网络，不访问设备。
English: Test the real TF WeRead package, phone QR and Back routes using private TF/simulated networking, without devices.
"""
from pathlib import Path
import argparse
import hashlib
import json
import os
import shutil
import subprocess

from test_native_preview_cover_layouts import ManagementRuntime
from test_native_preview_shelf_ble import ROOT
from test_weread import fixtures


DECODER = r'''
import Foundation
import Vision
for path in CommandLine.arguments.dropFirst() {
    let request = VNDetectBarcodesRequest(); request.symbologies = [.qr]
    try VNImageRequestHandler(url: URL(fileURLWithPath: path)).perform([request])
    let values = request.results?.compactMap { $0.payloadStringValue } ?? []
    let data = try JSONSerialization.data(withJSONObject: ["file": path, "values": values], options: [.sortedKeys])
    print(String(data: data, encoding: .utf8)!)
}
'''


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', type=Path, required=True)
    parser.add_argument('--package', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    binary, package, output = args.binary.resolve(), args.package.resolve(), args.output.resolve()
    output.mkdir(parents=True, exist_ok=False)
    os.environ['RPICO_HOME_LINK_OFFLINE'] = '1'
    os.environ['RPICO_WEREAD_OFFLINE'] = '1'
    report = {'result': 'RUNNING', 'hardware_access': False, 'network_access': False, 'real_tf_module': True,
              'binary_sha256': hashlib.sha256(binary.read_bytes()).hexdigest(),
              'package_sha256': hashlib.sha256(package.read_bytes()).hexdigest(), 'checks': [], 'qr': []}
    snapshots = output/'fixtures'; fixtures(snapshots)
    expected = {}
    try:
        for landscape in (False, True):
            label = 'landscape' if landscape else 'portrait'
            session = output/('session-'+label); session.mkdir()
            runtime = ManagementRuntime(binary, ROOT/'sdcard/fonts/MiSansVF.ttf', session, output)
            width = 640 if landscape else 360
            try:
                runtime.start()
                tools = session/'tf/apps/tools'; tools.mkdir(parents=True, exist_ok=True)
                shutil.copyfile(package, tools/'weread.pga')
                runtime.command('rotate '+str(int(landscape)))
                state = runtime.command('app-open weread'); assert state['route'] == 28 and not state['games_error'], state
                runtime.capture('weread-empty-'+label, state)
                state = runtime.tap(width-140, 88); assert state['route'] == 28, state
                runtime.capture('weread-guide-'+label, state)
                state = runtime.tap(60, 260 if landscape else 284); assert state['route'] == 30 and not state['games_error'], state
                state = runtime.settle(3)
                runtime.capture('weread-qr-'+label, state)
                link = runtime.command('manage-status')['management']; assert link['active'], link
                expected[output/('weread-qr-'+label+'.png')] = link['url']+'&view=weread'
                # 配网子页必须返回相同二维码范围，随后返回仍为活动微信读书模块。
                # The Wi-Fi child must retain QR scope, then return to the same active WeRead module.
                state = runtime.tap(width-58, 47); assert state['route'] == 6, state
                state = runtime.tap(23, 47); assert state['route'] == 30, state
                state = runtime.settle(3); runtime.capture('weread-qr-return-'+label, state)
                link = runtime.command('manage-status')['management']; assert link['active'], link
                expected[output/('weread-qr-return-'+label+'.png')] = link['url']+'&view=weread'
                state = runtime.tap(23, 47); assert state['route'] == 28 and not state['games_error'], state
                runtime.capture('weread-guide-return-'+label, state)
                state = runtime.command('key 1 click'); assert state['route'] == 28, state
                # 原WRS1书架/位置可读取，不依赖电脑授权或网络内容。
                # Original WRS1 shelves/progress stay readable without a desktop authorization or network content.
                data = session/'tf/apps/data/weread'; data.mkdir(parents=True, exist_ok=True)
                for source in (snapshots/'valid').iterdir(): shutil.copyfile(source, data/source.name)
                state = runtime.tap(width-45, 88); assert state['route'] == 28, state
                runtime.capture('weread-shelf-'+label, state)
                state = runtime.tap(50, 130); assert state['route'] == 28 and not state['games_error'], state
                runtime.capture('weread-notes-'+label, state)
                state = runtime.tap(40, 35); assert state['route'] == 28, state
                state = runtime.tap(40, 35); assert state['route'] == 0, state
                # 普通手机管理必须回到通用页，不残留微信读书目标。
                # Ordinary phone management must keep its generic destination after leaving WeRead.
                state = runtime.command('route 30'); state = runtime.settle(3)
                runtime.capture('generic-qr-'+label, state)
                link = runtime.command('manage-status')['management']; assert link['active'], link
                expected[output/('generic-qr-'+label+'.png')] = link['url']
                report['checks'] += [label+'-actual-package-phone-guide', label+'-setup-child-return-keeps-module',
                                     label+'-legacy-wrs1-notes', label+'-ordinary-management-scope-restored']
            finally:
                runtime.close()
                (output/('transcript-'+label+'.json')).write_text(json.dumps(runtime.transcript, ensure_ascii=False, indent=2)+'\n')
        decoder_source = output/'decode.swift'; decoder_source.write_text(DECODER)
        decoder = output/'decode-qr'
        subprocess.run(['swiftc', str(decoder_source), '-o', str(decoder)], check=True)
        result = subprocess.run([str(decoder), *(str(path) for path in expected)], text=True, capture_output=True, check=True)
        for line in result.stdout.splitlines():
            decoded = json.loads(line); assert decoded['values'] == [expected[Path(decoded['file'])]], decoded
            report['qr'].append(decoded)
        assert len(report['qr']) == 6
        report['result'] = 'PASS'
    except Exception as error:
        report['error'] = str(error)
        raise
    finally:
        (output/'report.json').write_text(json.dumps(report, ensure_ascii=False, indent=2)+'\n')
    print('Native WeRead guide: 8 route checks, 6 rendered QR decodes PASS')


if __name__ == '__main__': main()

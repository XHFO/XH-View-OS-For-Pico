#!/usr/bin/env python3
"""中文：构建独立TF工具应用，输出同源原生预览及可选ESP32-S3载荷。
English: Build independent TF utility packages with matching native preview and optional ESP32-S3 code.
"""
from __future__ import annotations
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile

from build_game_packages import ROOT, native_target, package_bytes, write_atomic
from build_game_elf import compiler, check_elf, normalize_relocations

TOOLS = (
    ("drawing-notes", "画板笔记", "画笔、橡皮和撤销 · 保存原稿后继续绘画", 128, "apps/src/drawing_notes.c", 10002, ()),
    ("weread", "微信读书", "在线搜书、图书资料、推荐与阅读统计 · 离线划线与想法", 129, "apps/src/weread.c", 10200,
     ("apps/include/weread_snapshot.h", "apps/include/weread_explore.h", "apps/include/weread_explore_json.h")),
    ("home-link", "家庭连接", "Home Assistant 房间选择、设备管理与控制 · 可扫码填写", 130, "apps/src/home_link.c", 10111,
     ("main/system/pico_scroll.h", "components/read_pico_apps/include/pico_app_switch.h")),
    ("printer-link", "3D 打印机", "独立打印机发现、添加与管理 · 保留已有配置", 131, "apps/src/printer_link.c", 10002, ()),
)


def build_packages(output: Path, *, device: bool = False, cc: Path | None = None,
                   app_ids: tuple[str, ...] | None = None) -> dict:
    output = output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    reports = []
    sdk = ROOT / 'components/read_pico_apps/include/pico_app_sdk.h'
    known = {entry[0] for entry in TOOLS}
    if app_ids is not None and (not app_ids or set(app_ids) - known):
        raise ValueError('Select known utility IDs: ' + ', '.join(sorted(known)))
    for app_id, title, description, kind, source_name, version, extra_inputs in TOOLS:
        if app_ids is not None and app_id not in app_ids:
            continue
        source = ROOT / source_name
        inputs = {name: hashlib.sha256(path.read_bytes()).hexdigest() for name, path in
                  ((source_name, source), ('components/read_pico_apps/include/pico_app_sdk.h', sdk))}
        inputs.update({name: hashlib.sha256((ROOT / name).read_bytes()).hexdigest() for name in extra_inputs})
        digest = hashlib.sha256(b''.join(name.encode() + b'\0' + (ROOT / name).read_bytes() + b'\0' for name in inputs)).digest()
        payloads = []
        with tempfile.TemporaryDirectory(prefix='utility-', dir=output) as directory:
            temporary = Path(directory)
            native = temporary / ('app.dylib' if sys.platform == 'darwin' else 'app.so')
            command = [os.environ.get('CC', 'cc'), '-std=gnu11', '-O2', '-fPIC', '-fno-builtin',
                       '-fno-stack-protector', '-fvisibility=hidden', '-Wall', '-Wextra', '-Werror',
                       '-dynamiclib' if sys.platform == 'darwin' else '-shared',
                       '-I', str(sdk.parent), str(source), '-o', str(native)]
            command += ['-Wl,-install_name,@rpath/read-pico-' + app_id + '.dylib'] if sys.platform == 'darwin' else ['-Wl,-z,defs']
            subprocess.run(command, cwd=ROOT, check=True)
            payloads.append((native_target(), native.read_bytes()))
            if device:
                toolchain = (cc or compiler()).resolve()
                raw = temporary / 'app.elf'
                linker = temporary / 'align.ld'
                linker.write_text('SECTIONS { .rodata ALIGN(4) : { *(.rodata .rodata.*) } } INSERT AFTER .text;\n')
                exports = temporary / 'exports.map'
                exports.write_text('{ global: pico_app_query; local: *; };\n')
                subprocess.run([str(toolchain), '-std=gnu11', '-O2', '-fPIC', '-fvisibility=hidden',
                                '-ffunction-sections', '-fdata-sections', '-fno-builtin', '-fno-stack-protector',
                                '-fno-unwind-tables', '-fno-asynchronous-unwind-tables', '-Wall', '-Wextra', '-Werror',
                                '-I', str(sdk.parent), '-shared', '-nostdlib', '-nostartfiles', '-Wl,-e,pico_app_query',
                                '-Wl,--gc-sections', '-Wl,-Bsymbolic', '-Wl,--build-id=none', '-Wl,--no-undefined',
                                '-Wl,-T,' + str(linker), '-Wl,--version-script=' + str(exports), str(source),
                                '-lgcc', '-o', str(raw)], cwd=ROOT, check=True)
                elf = temporary / 'app-stripped.elf'
                strip = toolchain.with_name(toolchain.name.removesuffix('gcc') + 'strip')
                subprocess.run([str(strip), '--strip-unneeded', '--remove-section=.comment', '--remove-section=.got.loc',
                                '--remove-section=.got', '--remove-section=.xt.lit', '--remove-section=.xt.prop',
                                '--remove-section=.xtensa.info', '--remove-section=.eh_frame', '--remove-section=.eh_frame_hdr',
                                str(raw), '-o', str(elf)], check=True)
                normalize_relocations(elf)
                check_elf(elf)
                write_atomic(output / (app_id + '.elf'), elf.read_bytes())
                payloads.insert(0, ('esp32s3-xtensa', elf.read_bytes()))
            package = package_bytes(app_id, title, description, kind, payloads, digest, version=version)
            write_atomic(output / (app_id + '.pga'), package)
        if inputs != {name: hashlib.sha256((ROOT / name).read_bytes()).hexdigest() for name in inputs}:
            raise RuntimeError('Utility source changed during the build; rebuild the package')
        reports.append({'id': app_id, 'kind': kind, 'title': title, 'description': description,
                        'file': app_id + '.pga', 'version': version, 'abi': 1, 'directory': 'apps/tools',
                        'bytes': len(package), 'sha256': hashlib.sha256(package).hexdigest(),
                        'source_sha256': digest.hex(), 'inputs': inputs, 'targets': [name for name, _ in payloads]})
    report = {'schema': 1, 'device_ready': device, 'apps': reports}
    write_atomic(output / 'catalog.json', (json.dumps(report, ensure_ascii=False, indent=2) + '\n').encode())
    return report


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, default=ROOT / 'build/tool-packages')
    parser.add_argument('--device', action='store_true')
    parser.add_argument('--cc', type=Path)
    parser.add_argument('--app', action='append', choices=[entry[0] for entry in TOOLS],
                        help='Build only the selected utility; repeat to select several')
    args = parser.parse_args()
    print(json.dumps(build_packages(args.output, device=args.device, cc=args.cc,
                                    app_ids=tuple(args.app) if args.app else None), ensure_ascii=False, indent=2))

#!/usr/bin/env python3
"""共享原生键盘的真实触摸与刷新边界回归；不模拟物理墨水屏延迟。
Real shared-native keyboard touch/refresh regression, without physical panel latency claims.
"""
from pathlib import Path
import argparse
import hashlib
import json
import selectors
import statistics
import subprocess
import tempfile
import time

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', type=Path, required=True)
    parser.add_argument('--font', type=Path, default=ROOT / 'sdcard/fonts/MiSansVF.ttf')
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    binary = args.binary.resolve()
    manifest = json.loads(binary.with_name('native-preview-build.json').read_text())
    assert hashlib.sha256(binary.read_bytes()).hexdigest() == manifest['binary_sha256']
    session = Path(tempfile.mkdtemp(prefix='keyboard-', dir=output))
    frame = session / 'frame.pgm'
    log = (session / 'stderr.log').open('w')
    process = subprocess.Popen([str(binary), str(args.font.resolve()), str(session), str(frame)],
                               stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=log, text=True)
    state = {}
    checks = []
    timings = []
    events = []

    def command(text):
        nonlocal state
        started = time.perf_counter()
        process.stdin.write(text + '\n')
        process.stdin.flush()
        with selectors.DefaultSelector() as selector:
            selector.register(process.stdout, selectors.EVENT_READ)
            assert selector.select(30), ('response timeout', text)
            state = json.loads(process.stdout.readline())
        assert state['ok'], state
        events.append({'command': text, 'state': state.copy()})
        return (time.perf_counter() - started) * 1000

    def tap(x, y, fast=False):
        before = state.copy()
        command(f'touch down {x} {y} {state["now_ms"] + 90}')
        pressed = state.copy()
        assert pressed['refresh_default_count'] == before['refresh_default_count'], 'PRESS repainted keyboard'
        assert pressed['refresh_follow_count'] == before['refresh_follow_count'], 'PRESS synchronously drove a full FOLLOW batch'
        elapsed = command(f'touch up {x} {y} {state["now_ms"] + 55}')
        if fast:
            assert state['presents'] == pressed['presents'], ('logical input must not synchronously paint/scan per release', state)
            assert state['refresh_continuous_count'] == pressed['refresh_continuous_count']
            assert state['refresh_follow_count'] == before['refresh_follow_count']
            assert state['refresh_default_count'] == before['refresh_default_count']
            assert state['refresh_gc16_count'] == before['refresh_gc16_count']
            timings.append(elapsed)

    def flush_keyboard(fast=True):
        before = state.copy()
        command(f'tick {state["now_ms"] + 90}')
        if fast:
            assert state['presents'] == before['presents'] + 1, 'a burst must merge into one changed target'
            assert state['refresh_continuous_count'] == before['refresh_continuous_count'] + 1
            assert state['refresh_default_count'] == before['refresh_default_count']
            assert state['refresh_follow_count'] == before['refresh_follow_count']
            assert state['refresh_waveform'] == 'CONTINUOUS' and state['refresh_mode'] == 1
            assert state['refresh_partial'], 'typing used a full-page request'

    def key(letter, fast=True):
        landscape = state['logical_width'] > state['logical_height']
        width, x, top, pitch = (404, 224, 136, 44) if landscape else (344, 8, 390, 48)
        kp = width // 10
        for row, letters in enumerate(('qwertyuiop', 'asdfghjkl', 'zxcvbnm')):
            if letter in letters:
                tap(x + (width - len(letters) * kp) // 2 + letters.index(letter) * kp + (kp - 4) // 2,
                    top + row * pitch + (pitch - 4) // 2, fast)
                return
        raise AssertionError(letter)

    def editor(landscape):
        command('route 10')
        tap(320 if landscape else 180, 294 if landscape else 574)
        assert state['route'] == 19

    def save(name):
        (output / (name + '.pgm')).write_bytes(frame.read_bytes())

    try:
        command('status')
        assert state['version'] == manifest['version']
        for landscape in (False, True):
            command('rotate ' + str(int(landscape)))
            editor(landscape)
            # Forty real English taps exceed the default periodic-GC16 interval twice.
            # 四十次真实英文按键跨过两次默认GC16周期。
            tap(62 if landscape else 222, 251 if landscape else 293)
            flush_keyboard(False)
            before = state.copy()
            for letter in 'qwertyuiopasdfghjklzxcvbnmqwertyuiopasdfghj':
                key(letter)
            assert state['refresh_default_count'] == before['refresh_default_count']
            assert state['presents'] == before['presents'], 'a typing burst started display work before pausing'
            flush_keyboard()
            checks.append(('landscape' if landscape else 'portrait') + ':40 logical keys captured, one merged continuous target after pause, no periodic clean')
            save(('landscape' if landscape else 'portrait') + '-english')
            for _ in range(8):
                command(f'tick {state["now_ms"] + 10}')
            before = state['presents']
            tap(214 if landscape else 180, 315 if landscape else 240)
            assert state['presents'] == before, 'no-op tap presented'
            checks.append('no-op tap does not present')
            # Return through a normal page and exercise changing candidate geometry.
            # 经普通页面返回后验证候选布局变化。
            editor(landscape)
            tap(62 if landscape else 44, 211 if landscape else 293)
            flush_keyboard(False)
            for letter in 'woxihuanyuedu':
                key(letter)
            flush_keyboard()
            save(('landscape' if landscape else 'portrait') + '-pinyin')
            before = state.copy()
            tap(286 if landscape else 100, 106 if landscape else 362, True)
            flush_keyboard()
            assert state['changed'], 'candidate commit did not change the frame'
            save(('landscape' if landscape else 'portrait') + '-committed')
            checks.append('candidate commits combined damage in one continuous phase')
            # A full rotation re-establishes the reference before fast typing resumes.
            # 转向完整恢复参考帧后才恢复快速输入。
            before = state['refresh_gc16_count']
            command('rotate ' + str(int(not landscape)))
            assert state['refresh_waveform'] == 'DEFAULT'
            assert state['refresh_gc16_count'] > before
            key('n')
            flush_keyboard()
            checks.append('rotation uses full recovery; subsequent input uses continuous damage')
        checks.append('all keyboard input went through real shared touch dispatch')
        report = {'result': 'PASS', 'version': manifest['version'], 'checks': checks,
                  'binary_sha256': manifest['binary_sha256'], 'events': len(events),
                  'host_key_release_ms': {'samples': len(timings), 'median': statistics.median(timings), 'max': max(timings)},
                  'limit': 'Host command/render time only; no physical panel timing or cloud/device acceptance.'}
        (output / 'report.json').write_text(json.dumps(report, ensure_ascii=False, indent=2) + '\n')
        print(json.dumps({k: v for k, v in report.items() if k != 'checks'}, indent=2))
    finally:
        (output / 'events.json').write_text(json.dumps(events, ensure_ascii=False, indent=2) + '\n')
        if process.poll() is None:
            process.stdin.write('quit\n')
            process.stdin.flush()
            process.communicate(timeout=10)
        log.close()
        assert process.returncode == 0


if __name__ == '__main__':
    main()

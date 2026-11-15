#!/usr/bin/env python3
"""Check production two-process display ordering with an explicit simulated display delay.
No physical waveform is simulated; timestamps prove callback sequencing only.
"""
import argparse
import json
import os
from pathlib import Path
import select
import subprocess
import tempfile
import time
import threading

ROOT = Path(__file__).resolve().parents[1]
p = argparse.ArgumentParser()
p.add_argument('--binary', type=Path, required=True)
p.add_argument('--output', type=Path, required=True)
a = p.parse_args()
a.output.mkdir(parents=True, exist_ok=True)
run = Path(tempfile.mkdtemp(prefix='relay-', dir=a.output.resolve()))
devices = []
history = []
results = []

class Device:
    def __init__(self, name):
        self.name = name
        self.root = run / name
        self.books = self.root / 'tf/books'
        self.books.mkdir(parents=True)
        (self.books / 'relay.txt').write_text(''.join(
            f'Chapter {i}\n' + ('Both screens relay actual completion. Shared text anchors remain unchanged.\n' * 180)
            for i in range(1, 4)))
        self.trace = self.root / 'turns.jsonl'
        self.log = (self.root / 'runtime.log').open('w')
        env = dict(os.environ, PICO_NATIVE_BOOK_PREFETCH=os.getenv('PICO_RELAY_PREFETCH','1'), PICO_NATIVE_E0470_SUCCESS='1',
                   PICO_NATIVE_TURN_DELAY_MS=os.getenv('PICO_RELAY_TURN_MS','800'), PICO_NATIVE_TURN_TRACE=str(self.trace))
        self.p = subprocess.Popen([str(a.binary.resolve()), str(ROOT / 'main/assets/serif.ttf'),
                                   str(self.root), str(self.root / 'frame.pgm')], env=env,
                                  stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=self.log, text=True)
        devices.append(self)
        self.state = {}
        self.cmd('status')
        self.cmd('route 34')

    def cmd(self, line):
        self.p.stdin.write(line + '\n')
        self.p.stdin.flush()
        assert select.select([self.p.stdout], [], [], 30)[0], (self.name, line)
        raw = self.p.stdout.readline()
        assert raw, (self.name, line, self.log.name)
        self.state = json.loads(raw)
        assert self.state['ok'], (self.name, line, self.state)
        history.append(dict(device=self.name, command=line, state=self.state))
        return self.state

    def tick(self):
        return self.cmd(f"tick {self.state['now_ms'] + 20}")

    def events(self):
        return [json.loads(x) for x in self.trace.read_text().splitlines()] if self.trace.exists() else []

try:
    left, right = Device('left'), Device('right')
    def pump(n=1):
        for _ in range(n):
            left.tick()
            right.tick()
            time.sleep(.001)
    def until(predicate, label, limit=5000):
        for _ in range(limit):
            pump()
            if predicate():
                return
        raise AssertionError((label, left.state, right.state))
    def settled():
        return all(d.state.get('active_app') == 'book' and not d.state.get('dual_target')
                   and not d.state.get('book_animation_lead') and d.state.get('dual_turn_ready')
                   for d in devices) and left.state['dual_spread'] == right.state['dual_spread']
    def slices(indices):
        return {d.name: d.events()[indices[d.name]:] for d in devices}
    def turn(source, forward):
        start = left.state['dual_spread']
        indices = {d.name: len(d.events()) for d in devices}
        # 两端独立推进UI：前屏阻塞推屏时，后屏仍能收控制并多次tick，才能检出重叠动画。
        # Advance both UI loops independently, so a blocked display cannot serialize the test driver and conceal overlap.
        stop = threading.Event()
        errors = []
        def worker(device):
            try:
                if device is source:
                    device.cmd('key 2 short' if forward else 'key 0 short')
                while not stop.is_set():
                    device.tick()
                    time.sleep(.004)
            except BaseException as error:
                errors.append(error)
                stop.set()
        threads = [threading.Thread(target=worker, args=(d,)) for d in devices]
        for thread in threads:
            thread.start()
        deadline = time.monotonic() + 30
        try:
            while not (settled() and left.state['dual_spread'] == start + (2 if forward else -2)):
                if errors:
                    raise errors[0]
                assert time.monotonic() < deadline, ('concurrent turn timeout', left.state, right.state)
                time.sleep(.001)
        finally:
            stop.set()
            for thread in threads:
                thread.join(timeout=35)
                assert not thread.is_alive()
        if errors:
            raise errors[0]
        events = slices(indices)
        first, second = (right, left) if forward else (left, right)
        starts = {d.name: [e for e in events[d.name] if e['phase'] == 'begin'] for d in devices}
        ends = {d.name: [e for e in events[d.name] if e['phase'] == 'end'] for d in devices}
        assert all(len(starts[d.name]) == len(ends[d.name]) == 1 for d in devices), events
        assert ends[first.name][0]['result'] == ends[second.name][0]['result'] == 0, events
        release=next(e['time_us'] for e in events[first.name] if e['phase']=='relay')
        assert starts[first.name][0]['time_us'] < release <= starts[second.name][0]['time_us'] < ends[first.name][0]['time_us'], events
        advance=ends[first.name][0]['time_us']-starts[second.name][0]['time_us']
        assert 420000 <= advance <= 550000, ('approximately500ms overlap',advance,events)
        assert ends[first.name][0]['time_us'] - starts[first.name][0]['time_us'] >= 80000, events
        if os.getenv('PICO_NATIVE_COLD_DRAW_MS'):
            prepared=[e for e in events[second.name] if e['phase']=='draw-end']
            assert prepared and prepared[0]['time_us'] < ends[first.name][0]['time_us'], ('trailing draw must finish during lead animation',events)
            gap=starts[second.name][0]['time_us']-release
            assert gap < 100000, ('host handover must not repeat cold draw',gap,events)
        assert left.state['page_end'] == right.state['page_start'], (left.state, right.state)
        results.append(dict(source=source.name, direction='next' if forward else 'previous', events=events))

    left.cmd('dual 1 127.0.0.1 123456')
    right.cmd('dual 2 127.0.0.1 123456')
    until(lambda: all(d.state['dual_synced'] and d.state['route'] == 0 for d in devices), 'sync')
    left.cmd('book-select /sdcard/books/relay.txt')
    until(settled, 'initial open')
    for source, forward in ((left, True), (right, True), (left, False), (right, False)):
        turn(source, forward)
    # A failed right display must not start the waiting left animation.
    right.cmd('display-body-failure 1')
    initial_calls = {d.name: d.state['e0470_calls'] for d in devices}
    indices = {d.name: len(d.events()) for d in devices}
    old = left.state['dual_spread']
    left.cmd('key 2 short')
    until(lambda: right.state['e0470_calls'] > initial_calls['right'], 'right engine failure')
    pump(12)
    assert left.state['e0470_calls'] == initial_calls['left'], slices(indices)
    assert not left.state['dual_peer_presented'], left.state
    right.cmd('display-body-failure 0')
    until(lambda: settled() and left.state['dual_spread'] == old + 2, 'display recovery')
    events = slices(indices)
    recoveries = [e for e in events['right'] if e['phase'] == 'display-body' and e['result'] == 0]
    left_begin = next(e for e in events['left'] if e['phase'] == 'begin')
    assert recoveries and recoveries[0]['time_us'] <= left_begin['time_us'], events
    results.append(dict(case='right failure then full-display recovery', events=events))
    # The same failure rule applies to left-first previous.
    left.cmd('display-body-failure 1')
    initial_calls = right.state['e0470_calls']
    indices = {d.name: len(d.events()) for d in devices}
    right.cmd('key 0 short')
    until(lambda: any(e['phase'] == 'end' for e in slices(indices)['left']), 'left failure')
    pump(12)
    assert right.state['e0470_calls'] == initial_calls
    left.cmd('display-body-failure 0')
    until(settled, 'left recovery')
    events = slices(indices)
    left_done = next(e for e in events['left'] if e['phase'] == 'display-body' and e['result'] == 0)
    right_begin = next(e for e in events['right'] if e['phase'] == 'begin')
    assert left_done['time_us'] <= right_begin['time_us'], events
    results.append(dict(case='left failure then full-display recovery', events=events))
    # Disconnect while waiting must let the remaining screen finish and later rejoin.
    right.cmd('display-body-failure 1')
    initial_calls = right.state['e0470_calls']
    left.cmd('key 2 short')
    until(lambda: right.state['e0470_calls'] > initial_calls, 'right waiting disconnect')
    for d in devices:
        d.cmd('dual-radio 0')
    pump(100)
    assert not left.state.get('book_animation_lead') and not left.state.get('dual_target'), left.state
    right.cmd('display-body-failure 0')
    for d in devices:
        d.cmd('dual-radio 1')
    until(settled, 'rejoin after interrupted relay')
    results.append(dict(case='disconnect during relay; rejoin', spread=left.state['dual_spread']))
    (run / 'result.json').write_text(json.dumps(dict(passed=True, results=results,
        scope='Production callback sequencing with opt-in host display/render delays; not physical panel timing',
        display_delay_ms=int(os.getenv('PICO_RELAY_TURN_MS','800')),cold_draw_ms=int(os.getenv('PICO_NATIVE_COLD_DRAW_MS','0'))), indent=2))
    print('PASS: four source/direction combinations, approximately500ms early handover, both display-failure paths and disconnect/rejoin')
finally:
    (run / 'transcript.json').write_text(json.dumps(history, ensure_ascii=False, indent=2))
    for d in devices:
        try:
            d.cmd('quit')
            d.p.wait(timeout=5)
        except Exception:
            d.p.terminate()
            d.p.wait(timeout=5)
        d.log.close()
    print(run)

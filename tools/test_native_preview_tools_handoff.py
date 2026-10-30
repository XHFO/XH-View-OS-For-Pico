#!/usr/bin/env python3
"""Exercise USB app-catalog recovery and non-destructive modal dismissal in a private native TF.

Uses the real product scanner, USB ownership UI and home editor. USB/card hardware
is simulated; no running preview, serial port or host disk is accessed.
"""
from pathlib import Path
import argparse
import hashlib
import json
import os
import time

from test_native_preview_shelf_ble import ROOT, Runtime


def packages(session):
    return {str(p.relative_to(session / 'tf')): hashlib.sha256(p.read_bytes()).hexdigest()
            for p in (session / 'tf/apps').glob('*/*.pga')}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    os.environ['RPICO_HOME_LINK_OFFLINE'] = '1'
    binary, output = args.binary.resolve(), args.output.resolve()
    output.mkdir(parents=True, exist_ok=False)
    session = output / 'session'; session.mkdir()
    runtime = Runtime(binary, ROOT / 'sdcard/fonts/MiSansVF.ttf', session, output)
    report = {'result': 'RUNNING', 'hardware_access': False,
              'usb_and_card_hardware_simulated': True,
              'binary_sha256': hashlib.sha256(binary.read_bytes()).hexdigest()}
    try:
        state = runtime.start()
        assert state['games_count'] == 6, state
        originals = packages(session)
        assert len(originals) == 7 and 'apps/tools/drawing-notes.pga' in originals

        # Reproduce the ownership transition through actual UI and service states.
        runtime.command('route 22'); runtime.tap(180, 514)
        for _ in range(50):
            time.sleep(.01); state = runtime.tick(100)
            if state['services']['usbActive']:
                break
        assert state['services']['usbActive'], state
        runtime.command('scenario usb-writing')
        runtime.command('scenario usb-idle')
        runtime.command('scenario usb-eject')
        for _ in range(60):
            time.sleep(.01); state = runtime.tick(100)
            if not state['services']['usbActive'] and state['games_count'] == 6:
                break
        state = runtime.settle(4)
        assert state['services']['tfMounted'] and not state['services']['usbActive'], state
        assert state['games_count'] == 6 and not state['games_error'], state

        # The tool must be available immediately, without opening Games/rebooting.
        runtime.command('route 27'); runtime.tap(80, 520)
        runtime.capture('tool-picker-after-usb', runtime.command('status'))
        runtime.tap(130, 424)  # sixth entry after four built-ins and desktop
        runtime.tap(300, 590); runtime.command('route 0')
        state = runtime.tap(265, 48)
        assert state['route'] == 28 and not state['games_error'], state
        runtime.capture('drawing-open-after-usb', state)

        runtime.command('route 13')
        state = runtime.hold(90, 190)
        runtime.capture('remove-dialog', state)
        before = runtime.frame.read_bytes()
        state = runtime.tap(5, 190)
        assert state['route'] == 13 and runtime.frame.read_bytes() != before, state
        runtime.capture('remove-outside-dismissed', state)
        assert packages(session) == originals
        state = runtime.tap(90, 190)
        assert state['route'] == 14, state

        # A replacement card pins the old application; an outside tap must keep it.
        runtime.command('scenario tf-absent')
        runtime.command('route 13')
        runtime.command('scenario tf-ready')
        runtime.command('games')
        state = runtime.command("status")
        assert state['route'] == 14, state
        runtime.capture('discard-dialog', state)
        before = runtime.frame.read_bytes()
        state = runtime.tap(5, 190)
        assert state['route'] == 14 and runtime.frame.read_bytes() != before, state
        runtime.capture('discard-outside-dismissed', state)
        assert packages(session) == originals
        # Back reopens recovery in the retained game; outside dismissal never discards.
        state = runtime.command("key 1 single")
        assert state['route'] == 14, state
        runtime.capture('discard-still-required', state)

        report.update(result='PASS', package_hashes=originals, commands=len(runtime.transcript),
                      scenes=runtime.scenes, transcript=runtime.transcript,
                      contracts=['deferred catalog scan after USB ownership release on any page',
                                 'tool appears in Home editor and opens without Games or reboot',
                                 'outside remove is cancel and consumes the contact',
                                 'outside discard retains pinned state and consumes the contact',
                                 'all seven original package hashes preserved'])
    finally:
        runtime.close()
        (output / 'report.json').write_text(json.dumps(report, ensure_ascii=False, indent=2) + '\n')
    print(json.dumps({'result': report['result'], 'commands': report['commands'],
                      'report': str(output / 'report.json')}, ensure_ascii=False))


if __name__ == '__main__':
    main()

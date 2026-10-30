#!/usr/bin/env python3
"""Run the real app loop to reject a post-wake two-second power-button blackout."""
from pathlib import Path
import argparse
import json
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--sanitize', action='store_true')
    parser.add_argument('--prove-regression', action='store_true',
                        help='Also verify that reinstating the removed post-wake ignore makes this scenario fail')
    parser.add_argument('--report', type=Path)
    args = parser.parse_args()
    harness = '#include <stdlib.h>\n' + (ROOT / 'tools/app_loop_host_test.c').read_text()
    harness = harness.replace('#include "../main/system/pico_system.h"',
                              '#include "' + str(ROOT / 'main/system/pico_system.h') + '"')
    extra = '''
/* The first synthetic power return happens after boot suppression. A second
 * completed click arrives 600ms later, while the old renewed ignore was active.
 * Touch-release consumption gets its own loop before that second click. */
static app_redraw_t power_button_repress_tick(app_ctx_t *ctx) {
    (void)ctx;
    if(step==10)lock_due=true;
    return APP_REDRAW_NONE;
}
static void power_button_repress_case(void) {
    reset();first.product_shell=true;first.on_tick=power_button_repress_tick;
    time_step=300000;power_request_at=8;
    for(int i=0;i<13;i++)add(0,0,0,0);
    run();
    if(power_runs!=2||power_toggles!=1||power_page!=&first||exits||enters!=1||sleep_calls){
        fprintf(stderr,"power-button-regression: completed click within 600ms of wake was ignored; runs=%d toggles=%d\\n",power_runs,power_toggles);
        abort();
    }
}
'''
    assert harness.count('int main(void)') == 1
    harness = harness.replace('int main(void)', extra + '\nint main(void)')
    needle = '    product_power_cases();\n'
    assert harness.count(needle) == 1
    harness = harness.replace(needle, needle + '    power_button_repress_case();\n')
    flags = ['-std=c11', '-Wall', '-Wextra', '-Werror', '-g',
             '-I' + str(ROOT / 'tools/app_loop_stubs'), '-I' + str(ROOT / 'main/app'),
             '-I' + str(ROOT / 'main/ui'), '-I' + str(ROOT / 'tools/ui_gesture_stubs')]
    if args.sanitize:
        flags += ['-fsanitize=address,undefined', '-fno-omit-frame-pointer']
    report = {'result': 'PASS', 'device_access': False, 'browser_access': False,
              'real_loop_source': 'main/app/app_loop.c', 'sanitized': args.sanitize,
              'checks': ['actual-loop-second-completed-power-click-within-600ms-of-wake',
                         'wake-contact-release-consumed-before-next-power-click',
                         'same-product-instance-retained-through-repeated-power-actions']}
    with tempfile.TemporaryDirectory(prefix='pico-power-button-') as folder:
        folder = Path(folder)
        fixture = folder / 'fixture.c';fixture.write_text(harness)
        executable = folder / 'test'
        def build(loop, binary):
            subprocess.run(['cc', *flags, str(fixture), str(loop),
                            str(ROOT / 'main/ui/ui_gesture.c'), '-o', str(binary)], check=True)
        build(ROOT / 'main/app/app_loop.c', executable)
        subprocess.run([str(executable)], check=True)
        if args.prove_regression:
            old = (ROOT / 'main/app/app_loop.c').read_text()
            marker = 'last_input_ms = last_lock_poll_ms = last_font_retry_ms = ctx.now_ms;\n                last_media_poll_ms = 0;\n'
            assert old.count(marker) == 1
            old = old.replace(marker, marker + '                app_lock_ignore_for(APP_LOCK_IGNORE_BOOT_MS);\n')
            previous = folder / 'old_loop.c';previous.write_text(old)
            negative = folder / 'negative'
            build(previous, negative)
            result = subprocess.run([str(negative)], capture_output=True, text=True)
            assert result.returncode != 0 and 'power-button-regression:' in result.stderr, result
            report['checks'].append('negative-control-old-post-wake-ignore-rejected')
    if args.report:
        args.report.parent.mkdir(parents=True, exist_ok=True)
        args.report.write_text(json.dumps(report, ensure_ascii=False, indent=2) + '\n')
    print(json.dumps(report, ensure_ascii=False))


if __name__ == '__main__':
    main()

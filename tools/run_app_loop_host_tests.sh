#!/bin/sh
set -eu
out=$(mktemp -d)
trap 'rm -rf "$out"' EXIT
case "${1:-}" in
  "") sanitize="" ;;
  --sanitize) sanitize="-fsanitize=address,undefined" ;;
  *) echo "Usage: $0 [--sanitize]" >&2; exit 2 ;;
esac
# 检测到未定义行为即失败，不能只输出告警后继续显示通过。
# Sanitizer diagnostics must fail the run rather than print a misleading pass.
UBSAN_OPTIONS="${UBSAN_OPTIONS:+$UBSAN_OPTIONS:}halt_on_error=1:print_stacktrace=1"
export UBSAN_OPTIONS
cc -std=c11 -Wall -Wextra -Werror $sanitize -g \
  -Itools/app_loop_stubs -Imain/app -Imain/ui -Itools/ui_gesture_stubs \
  tools/app_loop_host_test.c main/app/app_loop.c main/ui/ui_gesture.c -o "$out/test"
"$out/test"
cc -std=c11 -Wall -Wextra -Werror $sanitize -g \
  -Itools/app_loop_stubs -Imain/app -Imain/ui -Itools/ui_gesture_stubs \
  tools/app_touch_queue_host_test.c -o "$out/touch-queue"
"$out/touch-queue"
cc -std=c11 -Wall -Wextra -Werror $sanitize -g \
  -Itools/app_loop_stubs -Imain/app -Imain/ui -Itools/ui_gesture_stubs \
  tools/ui_menu_host_test.c main/ui/ui_menu.c -o "$out/menu"
"$out/menu"

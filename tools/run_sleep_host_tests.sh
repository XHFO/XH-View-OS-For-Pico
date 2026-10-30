#!/usr/bin/env bash
# 中文：只替换真实sleep.c/.h的硬件头，执行未改写的函数体与公开枚举。
# English: Replace hardware includes only, executing the unchanged sleep.c bodies and public enums.
set -euo pipefail
cd "$(dirname "$0")/.."
task_out="$(mktemp -d "${TMPDIR:-/tmp}/pico-sleep-test.XXXXXX")"
trap 'rm -rf "$task_out"' EXIT
extra=()
case "${1:-}" in
  --sanitize) extra=(-fsanitize=address,undefined -fno-omit-frame-pointer) ;;
  "") ;;
  *) echo "Usage: $0 [--sanitize]" >&2; exit 2 ;;
esac
python3 - "$task_out" <<'PY'
from pathlib import Path
import sys
target = Path(sys.argv[1])
for source, name in [('main/sleep.c', 'sleep_under_test.c'), ('main/sleep.h', 'sleep_api_under_test.h')]:
    lines = Path(source).read_text().splitlines(keepends=True)
    target.joinpath(name).write_text(''.join(line for line in lines if not line.lstrip().startswith('#include ')))
PY
cc -std=gnu11 -Wall -Wextra -Werror -g ${extra[@]+"${extra[@]}"} -Itools -I"$task_out" tools/sleep_host_test.c -o "$task_out/test"
UBSAN_OPTIONS="${UBSAN_OPTIONS:+$UBSAN_OPTIONS:}halt_on_error=1:print_stacktrace=1" "$task_out/test"

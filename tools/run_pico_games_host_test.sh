#!/bin/sh
# 中文：编译设备共用规则核心并验证。/ English: Compile and test the device rule core.
set -eu
if [ "$#" -gt 1 ];then echo "usage: $0 [--sanitize]" >&2;exit 2;fi
case "${1:-}" in
    --sanitize) shift;set -- -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer ;;
    "") set -- ;;
    *) echo "usage: $0 [--sanitize]" >&2;exit 2 ;;
esac
task_root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
task_binary=$(mktemp "${TMPDIR:-/tmp}/pico-games-test.XXXXXX")
trap 'rm -f "$task_binary"' EXIT HUP INT TERM
"${CC:-cc}" -std=c11 -O2 -Wall -Wextra -Werror "$@" "$task_root/tools/test_pico_games.c" -o "$task_binary"
"$task_binary"
"${CC:-cc}" -std=c11 -O2 -Wall -Wextra -Werror "$@" -I"$task_root/tools/pico_games_test" "$task_root/tools/test_pico_games_ui.c" -o "$task_binary"
"$task_binary"

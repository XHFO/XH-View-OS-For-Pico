#!/usr/bin/env bash
# 中文：独立构建与加载六个真实TF模块。/ English: Independently build and load all six actual TF modules.
set -euo pipefail
cd "$(dirname "$0")/.."
task_tmp="$(mktemp -d "${TMPDIR:-/tmp}/pico-app-test.XXXXXX")"
trap 'rm -rf "$task_tmp"' EXIT
extra=()
if [[ "${1:-}" == --sanitize ]]; then extra=(-fsanitize=address,undefined -fno-omit-frame-pointer); fi
cc -std=c11 -O2 -Wall -Wextra -Werror ${extra[@]+"${extra[@]}"} tools/test_pico_game_module.c -o "$task_tmp/test" -pthread
names=(gomoku go xiangqi chess 2048 snake)
for kind in 0 1 2 3 4 5; do
    name="${names[$kind]}"
    cc -std=c11 -O2 -Wall -Wextra -Werror -fPIC -fno-builtin -fno-stack-protector -fvisibility=hidden ${extra[@]+"${extra[@]}"} \
        -DPICO_GAME_MODULE -DPICO_GAME_KIND="$kind" "-DPICO_GAME_ID=\"$name\"" \
        -dynamiclib games/src/builtin_games.c -o "$task_tmp/$name.dylib"
    "$task_tmp/test" "$task_tmp/$name.dylib" "$kind" 0
    if [[ "$kind" -lt 4 ]]; then
        "$task_tmp/test" "$task_tmp/$name.dylib" "$kind" 1
        "$task_tmp/test" "$task_tmp/$name.dylib" "$kind" 2
    fi
done

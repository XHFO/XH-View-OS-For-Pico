#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 mindreset
# SPDX-License-Identifier: Apache-2.0
# 真实C实现的宿主边界测试与独立展开参照。/ Host boundaries and an independent expansion oracle for the real C implementation.
import ctypes
import importlib.util
import itertools
import json
from pathlib import Path
import random
import subprocess
import sys
import zipfile

ROOT = Path(__file__).resolve().parents[1]
BUILD = ROOT / "build/search-tests"
BUILD.mkdir(parents=True, exist_ok=True)
source = ROOT / "components/read_pico_search/read_pico_search.c"
flags = ["gcc", "-std=c11", "-Wall", "-Wextra", "-Werror", "-O2",
         "-I" + str(ROOT / "components/read_pico_search/include")]
exe = BUILD / "search-host"
subprocess.run(flags + ["-g", "-fsanitize=address,undefined", str(ROOT / "tools/search_host_test.c"), str(source), "-o", str(exe)], check=True)
subprocess.run([str(exe)], check=True)
shared = BUILD / "search.so"
wrapper = BUILD / "search-wrapper.c"
wrapper.write_text('#include ' + json.dumps(str(source)) + '\nint search_test_lookup(uint32_t cp) { return lookup(cp); }\n')
subprocess.run(flags + ["-shared", "-fPIC", str(wrapper), "-o", str(shared)], check=True)
lib = ctypes.CDLL(str(shared))
match = lib.read_pico_search_match
match.argtypes = [ctypes.c_char_p, ctypes.c_char_p]
match.restype = ctypes.c_bool

# 测试时可选官方wheel，独立穷举短输入与状态机比较。/ Optional official wheel enables exhaustive short-input oracle comparisons.
if len(sys.argv) > 1:
    spec = importlib.util.spec_from_file_location("generator", ROOT / "tools/generate_search_table.py")
    generator = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(generator)
    with zipfile.ZipFile(sys.argv[1]) as wheel:
        dictionary = json.loads(wheel.read("pypinyin/pinyin_dict.json"))
    # 独立官方数据验证全部码点和空隙；也覆盖24位压缩表的字节顺序。 / Validate every codepoint and gap against independent official data, including packed-table byte order.
    pronunciations = {int(cp): tuple(sorted(set(generator.normalize(v) for v in values.split(","))))
                      for cp, values in dictionary.items()}
    groups = {group: index for index, group in enumerate(sorted(set(pronunciations.values())))}
    expected_lookup = {cp: groups[values] for cp, values in pronunciations.items()}
    lookup = lib.search_test_lookup
    lookup.argtypes = [ctypes.c_uint32]
    lookup.restype = ctypes.c_int
    for cp in range(0x110000):
        assert lookup(cp) == expected_lookup.get(cp, -1), f"codepoint lookup mismatch U+{cp:04X}"
    for cp in [0x110000, 0xffffff, 0x1000000, 0xffffffff]:
        assert lookup(cp) == -1
    print(f"search: {len(pronunciations)} official codepoints and all Unicode gaps PASS")
    rng = random.Random(20260927)
    for case in range(500):
        tokens = [rng.choice(["重", "长", "女", "绿", "行", "乐", "ABC", "Cat", "3"]) for _ in range(3)]
        alternatives = [sorted(set(generator.normalize(v) for v in dictionary[str(ord(token))].split(",")))
                        if len(token) == 1 and ord(token) > 127 else [token.lower()] for token in tokens]
        full = ["".join(parts) for parts in itertools.product(*alternatives)]
        initials = ["".join(part[0] for part in parts) for parts in itertools.product(*alternatives)]
        name = " ".join(tokens)
        sample = rng.choice(full + initials)
        a = rng.randrange(len(sample))
        query = sample[a:rng.randrange(a + 1, len(sample) + 1)]
        if case % 3 == 0:
            query += "xyz"
        expected = query in name.lower() or any(query in stream for stream in full + initials)
        actual = match((name + ".epub").encode(), query.encode())
        assert actual == expected, (name, query, expected, actual)
    print("search: 500 official-dictionary expansion-oracle comparisons PASS")

obj = BUILD / "search.o"
subprocess.run(flags + ["-fstack-usage", "-c", str(source), "-o", str(obj)], check=True)
subprocess.run(["size", str(obj)], check=True)
print(obj.with_suffix(".su").read_text(), end="")

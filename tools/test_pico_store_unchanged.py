#!/usr/bin/env python3
"""验证实际存储函数的完整相同免写路径，仅操作临时目录。
Exercise actual unchanged-store functions with real temporary files and faulted I/O.
"""
from pathlib import Path
import argparse
import hashlib
import json
import os
import random
import re
import shlex
import subprocess
import tempfile
import zlib

ROOT = Path(__file__).resolve().parents[1]


def function(source, name):
    match = re.search(r"(?:^|\n)((?:static\s+)?[\w\s*]+?\b" + re.escape(name) + r"\([^;]*?\)\s*\{)", source)
    if not match:
        raise RuntimeError(f"Cannot locate actual function: {name}")
    start, at, depth, quote = match.start(1), match.end() - 1, 0, None
    while at < len(source):
        char = source[at]
        if quote:
            if char == "\\":
                at += 2
                continue
            if char == quote:
                quote = None
        elif source.startswith("//", at):
            at = source.index("\n", at)
            continue
        elif source.startswith("/*", at):
            at = source.index("*/", at) + 2
            continue
        elif char in "\"'":
            quote = char
        elif char == "{":
            depth += 1
        elif char == "}":
            depth -= 1
            if depth == 0:
                return source[start:at + 1]
        at += 1
    raise RuntimeError(f"Unbalanced actual function: {name}")


def collision():
    # 确定性的真实CRC碰撞，不能用只改坏校验值代替内容比较。/ A real deterministic CRC collision tests full payload comparison.
    rng, seen = random.Random(49), {}
    for _ in range(400000):
        payload = rng.getrandbits(64).to_bytes(8, "little")
        crc = zlib.crc32(payload)
        if crc in seen and seen[crc] != payload:
            return seen[crc], payload
        seen[crc] = payload
    raise RuntimeError("No CRC collision found")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--sanitize", action="store_true")
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    source_path = ROOT / "main/system/pico_system.c"
    source = source_path.read_text()
    names = ["crc32_update", "crc32", "good_key", "valid_file", "load_file", "stored_file_matches", "store_file", "ps_store"]
    extracted = "\n\n".join(function(source, name) for name in names)
    a, b = collision()
    with tempfile.TemporaryDirectory(prefix="pico-store-unchanged-") as temporary:
        build = args.output.resolve() if args.output else Path(temporary)
        build.mkdir(parents=True, exist_ok=True)
        (build / "actual_store.inc").write_text(extracted + "\n")
        (build / "collision.inc").write_text("static const uint8_t collision_a[8]={" + ",".join(map(str, a)) + "};\nstatic const uint8_t collision_b[8]={" + ",".join(map(str, b)) + "};\n")
        binary = build / "test"
        flags = ["-std=c11", "-O1", "-g", "-Wall", "-Wextra", "-Werror"] + shlex.split(os.environ.get("CFLAGS", ""))
        if args.sanitize:
            flags += ["-fsanitize=address,undefined", "-fno-omit-frame-pointer"]
        command = [os.environ.get("CC", "cc")] + flags + ["-I", str(build), str(ROOT / "tools/pico_store_unchanged_host_test.c"), "-o", str(binary)]
        subprocess.run(command, check=True)
        result = subprocess.run([str(binary), str(Path(temporary) / "fixtures")], text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        (build / "stdout.log").write_text(result.stdout)
        (build / "stderr.log").write_text(result.stderr)
        if result.returncode:
            print(result.stdout)
            print(result.stderr)
            raise SystemExit(result.returncode)
        report = json.loads(result.stdout)
        report.update(source_sha256=hashlib.sha256(source_path.read_bytes()).hexdigest(), extracted_sha256=hashlib.sha256(extracted.encode()).hexdigest(), extracted_functions=names, sanitize=args.sanitize, command=command, physical_device_accessed=False, crc_collision={"a":a.hex(),"b":b.hex(),"crc32":zlib.crc32(a)})
        (build / "report.json").write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n")
        print(f"PASS unchanged store: {report['checks']} checks, {len(report['cases'])} cases, sanitize={args.sanitize}; {build / 'report.json'}")


if __name__ == "__main__":
    main()

#!/usr/bin/env python3
"""编译实际滑动辅助器并执行消毒器回归。/ Compile the actual scroll helper and run sanitizer regressions."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shlex
import shutil
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--header", type=Path, help="alternate helper for a regression counterexample")
    parser.add_argument("--report", type=Path, help="save compiler and test evidence as JSON")
    args = parser.parse_args()
    header = (args.header or ROOT / "main/system/pico_scroll.h").resolve()
    compiler = shlex.split(os.environ.get("CC") or ("clang" if shutil.which("clang") else "cc"))
    evidence = {
        "header": str(header),
        "header_sha256": hashlib.sha256(header.read_bytes()).hexdigest(),
        "sanitizers": ["address", "undefined"],
    }
    with tempfile.TemporaryDirectory(prefix="pico-scroll-") as temporary:
        executable = Path(temporary) / "scroll-test"
        command = compiler + [
            "-std=c11", "-Wall", "-Wextra", "-Werror", "-g", "-O1",
            "-fsanitize=address,undefined", "-fno-omit-frame-pointer",
            '-DPICO_SCROLL_HEADER="' + str(header) + '"',
            str(ROOT / "tools/pico_scroll_host_test.c"), "-o", str(executable),
        ]
        evidence["command"] = command
        compiled = subprocess.run(command, cwd=ROOT, text=True, capture_output=True)
        evidence["compile"] = {"returncode": compiled.returncode, "stdout": compiled.stdout, "stderr": compiled.stderr}
        result = compiled
        if not compiled.returncode:
            result = subprocess.run([str(executable)], cwd=ROOT, text=True, capture_output=True)
            evidence["run"] = {"returncode": result.returncode, "stdout": result.stdout, "stderr": result.stderr}
        evidence["passed"] = result.returncode == 0
    if args.report:
        args.report.parent.mkdir(parents=True, exist_ok=True)
        args.report.write_text(json.dumps(evidence, indent=2) + "\n", encoding="utf-8")
    for phase in (evidence["compile"], evidence.get("run", {})):
        print(phase.get("stdout", ""), end="")
        print(phase.get("stderr", ""), end="", file=sys.stderr)
    return 0 if evidence["passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())

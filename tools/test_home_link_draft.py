#!/usr/bin/env python3
"""Exercise actual Home Link draft/save ABI with ASan and UBSan; never access a device."""
import argparse
import json
import os
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, default=ROOT / "apps/src/home_link.c")
    parser.add_argument("--case", choices=("all", "sequential", "retry", "gates", "lifecycle"), default="all")
    parser.add_argument("--report", type=Path)
    args = parser.parse_args()
    source = args.source.resolve()
    sdk = ROOT / "components/read_pico_apps/include"
    cc = os.environ.get("CC", "cc")
    report = {"source": str(source), "case": args.case, "sanitizers": ["address", "undefined"], "passed": False}
    with tempfile.TemporaryDirectory(prefix="pico-home-draft-") as temporary:
        temp = Path(temporary)
        library, binary = temp / "home-link.so", temp / "home-link-draft"
        flags = [cc, "-std=gnu11", "-O1", "-g", "-fno-omit-frame-pointer", "-fsanitize=address,undefined",
                 "-Wall", "-Wextra", "-Werror", "-I", str(sdk)]
        commands = [
            flags + ["-fPIC", "-fno-builtin", "-fno-stack-protector", "-fvisibility=hidden", "-shared",
                     str(source), "-o", str(library)],
            flags + [str(ROOT / "tools/home_link_draft_host_test.c"), "-ldl", "-o", str(binary)],
            [str(binary), str(library), args.case],
        ]
        results = []
        for command in commands:
            result = subprocess.run(command, cwd=ROOT, text=True, capture_output=True)
            results.append({"phase": ["compile_module", "compile_host", "exercise"][len(results)],
                            "returncode": result.returncode, "stdout": result.stdout, "stderr": result.stderr})
            if result.stdout:
                print(result.stdout, end="")
            if result.stderr:
                print(result.stderr, end="")
            if result.returncode:
                break
        report["results"] = results
        report["passed"] = len(results) == 3 and all(r["returncode"] == 0 for r in results)
    if args.report:
        args.report.parent.mkdir(parents=True, exist_ok=True)
        args.report.write_text(json.dumps(report, indent=2) + "\n")
    if not report["passed"]:
        raise SystemExit(1)


if __name__ == "__main__":
    main()

#!/usr/bin/env python3
"""Compile the i18n catalog test and check docs/I18N_KEY_LIST.md stays aligned."""
import os
import re
import shlex
import shutil
import subprocess
import tempfile
from pathlib import Path

root = Path(__file__).resolve().parents[1]
inc_path = root / "main/system/pico_i18n_table.inc"
doc_path = root / "docs/I18N_KEY_LIST.md"
line_re = re.compile(r'^PS_I18N\("([^"]*)", "([^"]*)", "([^"]*)"\)\s*$')
call_re = re.compile(
    r'(?:ps_tr|hl)\(\s*"([^"]+)"|(?:\?|:)\s*"((?:common|nav|launcher|settings|dialog)\.[a-z0-9_.]+)"'
)
hl_re = re.compile(r'hl\(\s*"([^"]+)"\s*,\s*"([^"]*)"\s*\)')

def catalog():
    rows = []
    for lineno, line in enumerate(inc_path.read_text(encoding="utf-8").splitlines(), 1):
        if not line.startswith("PS_I18N("):
            continue
        match = line_re.match(line)
        if not match:
            raise SystemExit(f"{inc_path}:{lineno}: unparsable catalog line")
        rows.append(match.groups())
    if not rows:
        raise SystemExit("catalog is empty")
    return rows

def tsv_block(text):
    start = text.find("```tsv\n")
    end = text.find("\n```", start + 6)
    if start < 0 or end < 0:
        raise SystemExit("docs/I18N_KEY_LIST.md is missing a tsv fence")
    body = text[start + len("```tsv\n"):end]
    parsed = []
    lines = body.split("\n")
    if not lines or lines[0] != "key\tzh-Hans\ten":
        raise SystemExit("tsv header must be key, zh-Hans, en")
    for line in lines[1:]:
        if not line:
            continue
        parts = line.split("\t")
        if len(parts) != 3:
            raise SystemExit(f"tsv row does not have 3 columns: {line!r}")
        parsed.append(tuple(parts))
    return parsed

def main():
    rows = catalog()
    doc_rows = tsv_block(doc_path.read_text(encoding="utf-8"))
    if rows != doc_rows:
        raise SystemExit("docs/I18N_KEY_LIST.md tsv does not match pico_i18n_table.inc")
    keys = {key: (zh, en) for key, zh, en in rows}
    sources = [
        root / "main/system/pico_connectivity.c",
        root / "main/system/pico_navigation.c",
        root / "main/system/pico_launcher.c",
        root / "main/system/pico_power.c",
        root / "main/system/pico_home_link_ui_thin.inc",
        root / "main/system/pico_games.c",
        root / "apps/src/home_link.c",
    ]
    used = set()
    for path in sources:
        text = path.read_text(encoding="utf-8")
        for match in call_re.finditer(text):
            key = match.group(1) or match.group(2)
            if key not in keys:
                raise SystemExit(f"{path.name}: unknown key {key}")
            used.add(key)
        if path.name == "home_link.c":
            for key, zh in hl_re.findall(text):
                if keys[key][0] != zh:
                    raise SystemExit(f"home_link fallback for {key} does not match catalog zh")
    if len(used) < 40:
        raise SystemExit(f"expected migrated call sites, found {len(used)}")
    with tempfile.TemporaryDirectory(prefix="pico-i18n-") as tmp:
        executable = str(Path(tmp) / "i18n-test")
        compiler = os.environ.get("CC") or ("gcc" if shutil.which("gcc") else "cc")
        command = shlex.split(compiler) + [
            "-std=c11", "-Wall", "-Wextra", "-Werror", "-g",
            "-fsanitize=address,undefined", "-fno-omit-frame-pointer",
            str(root / "tools/test_pico_i18n.c"), "-o", executable,
        ]
        subprocess.run(command, cwd=root, check=True)
        subprocess.run([executable], cwd=root, check=True)
    print(f"i18n key list aligned: {len(rows)} keys, {len(used)} call-site keys")

if __name__ == "__main__":
    main()

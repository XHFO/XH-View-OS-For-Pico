#!/usr/bin/env python3
"""薄宿主路由及真实空状态UI契约。/ Thin-host routing and production empty-state UI contracts."""
from pathlib import Path
import argparse
import os
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def run():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--ui-source", type=Path, help="alternate production UI source for a regression counterexample")
    args = parser.parse_args()
    cjson = ROOT / "managed_components/espressif__cjson/cJSON"
    if not (cjson / "cJSON.c").is_file():
        raise SystemExit("Install the repository's locked cJSON component first; this test never downloads dependencies.")
    compiler = shlex.split(os.environ.get("CC", "cc"))
    flags = ["-std=c11", "-Wall", "-Wextra", "-Werror", "-g",
             "-fsanitize=address,undefined", "-fno-omit-frame-pointer"]
    with tempfile.TemporaryDirectory(prefix="pico-home-thin-") as temp:
        binary = Path(temp) / "home-link-thin"
        subprocess.run(
            compiler + flags + [
                "-I",
                str(ROOT / "components/read_pico_home_link/include"),
                "-I", str(cjson),
                str(ROOT / "tools/home_link_thin_host_test.c"),
                *[str(ROOT / "components/read_pico_home_link" / name) for name in
                  ("home_link_stubs.c", "home_link_portal.c", "home_link_model.c", "home_link_json.c")],
                "-lm", "-o", str(binary),
            ],
            check=True,
            cwd=ROOT,
        )
        subprocess.run([str(binary)], check=True, cwd=ROOT)
        ui_binary = Path(temp) / "home-link-thin-ui"
        (Path(temp) / "esp_log.h").write_text("#define ESP_LOGE(tag, ...) thin_test_log(tag, __VA_ARGS__)\n")
        alternate = []
        if args.ui_source:
            # 原字节复制避免另一checkout的pragma-once头重复定义；不改被测UI逻辑。
            # Copy bytes unchanged so another checkout's pragma-once headers cannot redefine types; preserve tested UI logic.
            ui_source = Path(temp) / "thin-ui-under-test.inc"
            ui_source.write_bytes(args.ui_source.read_bytes())
            alternate = [f'-DPICO_THIN_UI_SOURCE="{ui_source}"']
        includes = ["tools/preview_home_stubs", "tools/zip_host_stubs", "main/app", "main/ui", "main/system",
                    "components/read_pico_home_link/include"]
        subprocess.run(compiler + flags + ["-I", temp] +
                       [argument for path in includes for argument in ("-I", str(ROOT / path))] +
                       alternate +
                       [str(ROOT / "tools/home_link_thin_ui_host_test.c"),
                        str(ROOT / "main/system/pico_i18n.c"), "-lz", "-o", str(ui_binary)],
                       check=True, cwd=ROOT)
        subprocess.run([str(ui_binary)], check=True, cwd=ROOT)
        print("home-link thin routing and UI passed")


if __name__ == "__main__":
    run()

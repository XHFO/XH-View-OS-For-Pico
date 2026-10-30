#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 mindreset
# SPDX-License-Identifier: Apache-2.0
"""测试共享启动/锁屏解压器、资源一致性、错误原子性及CMake生成依赖；保留开机测试入口。
Test shared splash/lock inflation, exact assets, failure atomicity and CMake dependencies; retain the boot-test entry point.
"""
from pathlib import Path
import os
import shlex
import shutil
import struct
import subprocess
import sys
import tempfile
import zlib
from compress_boot_image import compress_boot_image, compress_packed_image


root = Path(__file__).resolve().parents[1]
source = (root / "main/app_main.c").read_text()
sleep_source = (root / "main/sleep.c").read_text()
assets = {name: (root / f"main/assets/{name}_4bpp.p4b").read_bytes() for name in ("loading", "lock")}
packed_assets = {name: compress_packed_image(image) for name, image in assets.items()}
for name, original in assets.items():
    compressed = packed_assets[name]
    assert compressed == compress_packed_image(original) == compress_boot_image(original)
    assert struct.unpack_from("<II", compressed, 8) == (len(original), zlib.crc32(original))
    assert zlib.decompress(compressed[16:], -15) == original
# 品牌启动页必须留在原先的6747字节预算内，同时仍逐字节验证真实资源和完整解压。
# Keep the branded splash within the original 6747-byte budget while retaining exact resource/inflate checks.
assert 16 < len(packed_assets["loading"]) <= 6747
assert len(packed_assets["lock"]) == 10542
assert "loading_4bpp_p4b_start" not in source
assert "lock_4bpp_p4b_start" not in source + sleep_source
assert source.index("free(loading);", source.index("update_display_from_white(&hl);")) < source.index("ttf_font_init();")
assert "ui_image_validate(loading, loading_size" in source
assert "loading,loading_size" in source[source.index("if (!ui_image_decode"):]
assert source.index("if (!lock_image_matches_panel())") < source.index("uint8_t* loading =")
assert "ui_packed_asset_unpack" in source and "ui_packed_asset_unpack" in sleep_source
lock_entry = sleep_source[sleep_source.index("void enter_lock_and_sleep("):]
assert lock_entry.index("free(image);", lock_entry.index("epd_hl_update_screen_from_white")) < lock_entry.index("app_lock_wait_key_idle(800)")
assert "ui/ui_packed_asset.c" in (root / "main/CMakeLists.txt").read_text()

with tempfile.TemporaryDirectory(prefix="pico-boot-deflate-") as temporary:
    temp = Path(temporary)
    (temp / "esp_heap_caps.h").write_text("#define MALLOC_CAP_SPIRAM 1u\n#define MALLOC_CAP_8BIT 2u\n")
    (temp / "esp_log.h").write_text("#define ESP_LOGE(tag, ...) test_log(tag, __VA_ARGS__)\n")
    executable = temp / "boot-test"
    command = shlex.split(os.environ.get("CC", "cc")) + [
        "-std=c11", "-O1", "-Wall", "-Wextra", "-Werror", "-fsanitize=address,undefined",
        "-fno-omit-frame-pointer", "-I", str(temp), "-I", str(root / "main/ui"),
        "-I", str(root / "tools/zip_host_stubs"), str(root / "tools/boot_splash_host_test.c"),
        str(root / "main/ui/ui_image.c"), "-lz", "-o", str(executable),
    ]
    subprocess.run(command, check=True)
    for name, original in assets.items():
        original_path = root / f"main/assets/{name}_4bpp.p4b"
        packed_path = temp / f"{name}_4bpp.p4b.deflate"
        subprocess.run([sys.executable, str(root / "tools/compress_boot_image.py"), str(original_path), str(packed_path)], check=True)
        assert packed_path.read_bytes() == packed_assets[name]
        corrupt = bytearray(original)
        corrupt[20] ^= 1
        corrupt_path = temp / f"{name}-invalid-p4b.deflate"
        corrupt_path.write_bytes(compress_boot_image(bytes(corrupt)))
        subprocess.run([str(executable), str(packed_path), str(original_path),
                        str(root / f"main/assets/{name}_4bpp.bin"), str(corrupt_path)], check=True)

    # 执行原入口函数体，保持ROM/屏幕替身与逻辑验证边界明确。
    # Execute the original entry bodies while keeping ROM/display substitution distinct from logic verification.
    validator = source[source.index("static bool lock_image_matches_panel("):source.index("void app_main(")]
    (temp / "packed_asset_integration_under_test.inc").write_text(validator + "\n" + lock_entry)
    integration = temp / "integration-test"
    integration_command = command.copy()
    integration_command[integration_command.index(str(root / "tools/boot_splash_host_test.c"))] = str(root / "tools/packed_asset_integration_host_test.c")
    integration_command[-1] = str(integration)
    subprocess.run(integration_command, check=True)
    subprocess.run([str(integration), str(temp / "lock_4bpp.p4b.deflate"), str(root / "main/assets/lock_4bpp.p4b"), str(temp / "lock-invalid-p4b.deflate")], check=True)

    # 用轻量IDF替身真实执行原CMake生成规则，既检查依赖又不整编固件。
    # Execute the original CMake generation rule with a small IDF stand-in, checking wiring without a firmware build.
    cmake = os.environ.get("CMAKE") or shutil.which("cmake")
    assert cmake, "cmake required for boot asset dependency test (or set CMAKE)"
    (temp / "dummy.c").write_text("int boot_test_dummy(void) { return 0; }\n")
    (temp / "emit_asset.py").write_text(
        "from pathlib import Path\nimport sys\n"
        "p=Path(sys.argv[1]); assert p.read_bytes()[:8] == b'PICO4DF1'\n"
        "Path(sys.argv[2]).write_text('const unsigned char boot_test_asset[] = {1};\\n')\n"
    )
    (temp / "CMakeLists.txt").write_text(f'''cmake_minimum_required(VERSION 3.16)
project(boot_asset_dependency C)
set(test_python "{sys.executable}")
set(test_root "{temp}")
function(idf_component_register)
    cmake_parse_arguments(BOOT "" "" "SRCS;REQUIRES;INCLUDE_DIRS;EMBED_FILES" ${{ARGN}})
    if("assets/loading_4bpp.p4b" IN_LIST BOOT_EMBED_FILES OR "assets/lock_4bpp.p4b" IN_LIST BOOT_EMBED_FILES)
        message(FATAL_ERROR "raw splash/lock resource remains embedded")
    endif()
    add_library(mock_main STATIC "${{test_root}}/dummy.c")
    set(COMPONENT_LIB mock_main PARENT_SCOPE)
endfunction()
function(idf_build_get_property output property)
    if(NOT property STREQUAL "PYTHON")
        message(FATAL_ERROR "unexpected build property")
    endif()
    set(${{output}} "${{test_python}}" PARENT_SCOPE)
endfunction()
function(idf_component_get_property output component property)
    set(${{output}} mock_main PARENT_SCOPE)
endfunction()
function(target_add_binary_data target path mode)
    if(NOT mode STREQUAL "BINARY")
        message(FATAL_ERROR "asset must be binary")
    endif()
    get_filename_component(asset_name "${{path}}" NAME)
    set(generated "${{CMAKE_CURRENT_BINARY_DIR}}/${{asset_name}}.c")
    add_custom_command(OUTPUT "${{generated}}"
        COMMAND "${{test_python}}" "${{test_root}}/emit_asset.py" "${{path}}" "${{generated}}"
        DEPENDS "${{path}}" "${{test_root}}/emit_asset.py" VERBATIM)
    target_sources(${{target}} PRIVATE "${{generated}}")
endfunction()
include("{root / 'main/CMakeLists.txt'}")
''')
    cmake_build = temp / "cmake-build"
    subprocess.run([cmake, "-S", str(temp), "-B", str(cmake_build)], check=True, stdout=subprocess.DEVNULL)
    subprocess.run([cmake, "--build", str(cmake_build), "--target", "mock_main", "--parallel", "4"], check=True, stdout=subprocess.DEVNULL)
    for name, original in assets.items():
        compressed = packed_assets[name]
        assert (cmake_build / f"{name}_4bpp.p4b.deflate").read_bytes() == compressed
        print(f"{name} asset: {len(original)} -> {len(compressed)} B; {len(original)-len(compressed)} B saved; deterministic generation/dependency wiring PASS")

#!/usr/bin/env python3
"""中文：将六个真实游戏编译为无固件/IDF导入的ESP32-S3独立ELF。
English: Compile six actual games into standalone ESP32-S3 ELFs without firmware/IDF imports.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import struct
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
GAME_IDS = ("gomoku", "go", "xiangqi", "chess", "2048", "snake")
MODULE_INPUTS = (
    "games/src/builtin_games.c",
    "games/sdk/pico_game_module_compat.h",
    "components/read_pico_apps/include/pico_app_sdk.h",
)


def source_digest() -> tuple[str, dict[str, str]]:
    combined = hashlib.sha256()
    inputs = {}
    for name in MODULE_INPUTS:
        data = (ROOT / name).read_bytes()
        inputs[name] = hashlib.sha256(data).hexdigest()
        combined.update(name.encode() + b"\0" + data + b"\0")
    return combined.hexdigest(), inputs


def compiler() -> Path:
    """中文：优先显式工具链，再找PATH和相邻的本地IDF工具目录。
    English: Prefer an explicit compiler, then PATH and the adjacent local IDF tools.
    """
    configured = os.environ.get("PICO_XTENSA_CC")
    if configured:
        return Path(configured).resolve()
    found = shutil.which("xtensa-esp32s3-elf-gcc")
    if found:
        return Path(found).resolve()
    candidates = sorted((ROOT.parent / "idf-tools/tools/xtensa-esp-elf").glob(
        "*/xtensa-esp-elf/bin/xtensa-esp32s3-elf-gcc"))
    if candidates:
        return candidates[-1]
    raise FileNotFoundError("Set PICO_XTENSA_CC or activate the ESP32-S3 IDF toolchain")


def check_elf(path: Path) -> dict:
    """中文：构建时拒绝外部导入和宿主不支持的重定位，避免打包不可运行文件。
    English: Reject imports and unsupported relocations before packaging non-runnable files.
    """
    data = path.read_bytes()
    if len(data) < 52 or data[:7] != b"\x7fELF\x01\x01\x01":
        raise ValueError(f"Not a little-endian ELF32: {path}")
    header = struct.unpack_from("<16sHHIIIIIHHHHHH", data)
    if header[1:4] != (3, 94, 1) or header[11] != 40:
        raise ValueError(f"Not an Xtensa ET_DYN module: {path}")
    sections = [struct.unpack_from("<10I", data, header[6] + index * 40)
                for index in range(header[12])]
    name_section = sections[header[13]]
    names = data[name_section[4]:name_section[4] + name_section[5]]
    def name(section: tuple) -> str:
        return names[section[0]:].split(b"\0", 1)[0].decode("ascii")
    loaded = []
    runtime_sections = {".text", ".rodata", ".data", ".data.rel.ro", ".bss"}
    metadata_sections = {".hash", ".dynsym", ".dynstr", ".rela.dyn", ".dynamic"}
    relocs = 0
    query = False
    for section in sections:
        label = name(section)
        if section[2] & 2 and section[5] and label not in runtime_sections | metadata_sections:
            raise ValueError(f"Unsupported allocated section {label}: {path}")
        if label in runtime_sections and section[5]:
            if section[3] & 3:
                raise ValueError(f"Unaligned runtime section {label}: {path}")
            loaded.append((section[3], section[5], label))
        if section[1] in (2, 11):
            strings = sections[section[6]]
            string_data = data[strings[4]:strings[4] + strings[5]]
            for offset in range(section[4], section[4] + section[5], 16):
                symbol = struct.unpack_from("<IIIBBH", data, offset)
                symbol_name = string_data[symbol[0]:].split(b"\0", 1)[0]
                if not symbol[5] and symbol_name:
                    raise ValueError(f"Module imports {symbol_name!r}: {path}")
                if symbol_name == b"pico_app_query":
                    query = symbol[1] == header[4] and symbol[3] & 15 == 2
        if section[1] == 4:
            previous = -1
            for offset in range(section[4], section[4] + section[5], 12):
                address, info, addend = struct.unpack_from("<IIi", data, offset)
                if info != 5 or addend or address & 3 or address <= previous:
                    raise ValueError(f"Unsupported or unsorted relocation: offset={address:#x} info={info:#x} addend={addend} previous={previous:#x}: {path}")
                previous = address
                relocs += 1
        if section[1] == 9:
            raise ValueError(f"Implicit relocations are unsupported: {path}")
    if not query:
        raise ValueError(f"Entry does not export pico_app_query: {path}")
    loaded.sort()
    for current, following in zip(loaded, loaded[1:]):
        size = (current[1] + 3) & ~3 if current[2] == ".text" else current[1]
        if current[0] + size > following[0]:
            raise ValueError(f"Runtime section overlap: {path}")
    return {"bytes": len(data), "relative_relocations": relocs,
            "mapped_bytes": sum(size for _, size, _ in loaded)}


def normalize_relocations(path: Path) -> None:
    """中文：删除链接器GC留下的NONE占位并排序，保留文件偏移与ELF动态表一致。
    English: Remove linker-GC NONE placeholders and sort patches without moving file offsets.
    """
    data = bytearray(path.read_bytes())
    header = struct.unpack_from("<16sHHIIIIIHHHHHH", data)
    sections = [struct.unpack_from("<10I", data, header[6] + index * 40)
                for index in range(header[12])]
    relative_count = 0
    relative_bytes = 0
    for index, section in enumerate(sections):
        if section[1] != 4:
            continue
        entries = []
        for offset in range(section[4], section[4] + section[5], 12):
            relocation = struct.unpack_from("<IIi", data, offset)
            if relocation == (0, 0, 0):
                continue
            if relocation[1:] != (5, 0):
                raise ValueError(f"Unsupported relocation {relocation}: {path}")
            entries.append(relocation)
        entries.sort()
        normalized = b"".join(struct.pack("<IIi", *entry) for entry in entries)
        data[section[4]:section[4] + section[5]] = normalized + bytes(section[5] - len(normalized))
        struct.pack_into("<I", data, header[6] + index * 40 + 20, len(normalized))
        relative_count += len(entries)
        relative_bytes += len(normalized)
    for section in sections:
        if section[1] == 6:
            for offset in range(section[4], section[4] + section[5], 8):
                tag, _ = struct.unpack_from("<II", data, offset)
                if tag == 8:  # DT_RELASZ
                    struct.pack_into("<I", data, offset + 4, relative_bytes)
                elif tag == 0x6ffffff9:  # DT_RELACOUNT
                    struct.pack_into("<I", data, offset + 4, relative_count)
    path.write_bytes(data)


def build(output: Path, *, cc: Path | None = None, source: Path | None = None) -> dict[str, Path]:
    output = output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    cc = (cc or compiler()).resolve()
    source = (source or ROOT / "games/src/builtin_games.c").resolve()
    # 替代源码仅用于工具链探针，不能冒充产品同源构建。
    # Alternate source is a toolchain probe and must not claim a first-party source digest.
    official_source = source == (ROOT / MODULE_INPUTS[0]).resolve()
    manifest = output / "manifest.json"
    manifest.unlink(missing_ok=True)
    digest_before, input_hashes = source_digest()
    script_hash = hashlib.sha256(Path(__file__).read_bytes()).hexdigest()
    strip = cc.with_name(cc.name.removesuffix("gcc") + "strip")
    paths = {}
    # text按四字节补齐后才开始rodata，匹配S3 ELF加载器的地址映射边界。
    # Start rodata after four-byte text padding, matching S3 ELF loader address mapping.
    with tempfile.TemporaryDirectory(prefix="game-elf-", dir=output) as directory:
        temporary = Path(directory)
        linker = temporary / "align.ld"
        linker.write_text("SECTIONS { .rodata ALIGN(4) : { *(.rodata .rodata.*) } } INSERT AFTER .text;\n")
        exports = temporary / "exports.map"
        exports.write_text("{ global: pico_app_query; local: *; };\n")
        for kind, game_id in enumerate(GAME_IDS):
            raw = temporary / (game_id + ".elf")
            command = [str(cc), "-std=gnu11", "-O2", "-fPIC", "-fvisibility=hidden",
                       "-ffunction-sections", "-fdata-sections", "-fno-builtin", "-fno-stack-protector",
                       "-fno-unwind-tables", "-fno-asynchronous-unwind-tables",
                       "-Wall", "-Wextra", "-Werror", "-Wno-unused-function", "-Wno-unused-parameter",
                       "-DPICO_GAME_MODULE", f"-DPICO_GAME_KIND={kind}", f'-DPICO_GAME_ID="{game_id}"',
                       "-I", str(ROOT / "components/read_pico_apps/include"),
                       "-shared", "-nostdlib", "-nostartfiles", "-Wl,-e,pico_app_query",
                       "-Wl,--gc-sections", "-Wl,-Bsymbolic", "-Wl,--build-id=none", "-Wl,--no-undefined",
                       "-Wl,-T," + str(linker), "-Wl,--version-script=" + str(exports),
                       str(source), "-lgcc", "-o", str(raw)]
            subprocess.run(command, cwd=ROOT, check=True)
            destination = output / (game_id + ".elf")
            staged = temporary / (game_id + "-stripped.elf")
            subprocess.run([str(strip), "--strip-unneeded", "--remove-section=.comment",
                            "--remove-section=.got.loc", "--remove-section=.got",
                            "--remove-section=.xt.lit", "--remove-section=.xt.prop",
                            "--remove-section=.xtensa.info", "--remove-section=.eh_frame",
                            "--remove-section=.eh_frame_hdr", str(raw), "-o", str(staged)], check=True)
            normalize_relocations(staged)
            check_elf(staged)
            staged.replace(destination)
            paths[game_id] = destination
    digest_after, _ = source_digest()
    if digest_before != digest_after:
        raise RuntimeError("Game source changed while compiling; rebuild all modules")
    if official_source:
        report = {
            "schema": 1,
            "target": "esp32s3-xtensa",
            "source_sha256": digest_before,
            "inputs": input_hashes,
            "build_script_sha256": script_hash,
            "compiler": subprocess.run([str(cc), "--version"], check=True, capture_output=True,
                                       text=True).stdout.splitlines()[0],
            "games": {game_id: {"file": path.name, "bytes": path.stat().st_size,
                                "sha256": hashlib.sha256(path.read_bytes()).hexdigest()}
                      for game_id, path in paths.items()},
        }
        staged_manifest = output / "manifest.json.tmp"
        staged_manifest.write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n")
        staged_manifest.replace(manifest)
    return paths


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, default=ROOT / "build/game-elf")
    parser.add_argument("--cc", type=Path)
    parser.add_argument("--source", type=Path)
    args = parser.parse_args()
    for game_id, path in build(args.output, cc=args.cc, source=args.source).items():
        print(game_id, path, check_elf(path))


if __name__ == "__main__":
    main()

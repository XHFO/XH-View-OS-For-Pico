#!/usr/bin/env python3
"""中文：构建独立小游戏动态模块并封装 PGA1，输出可供网站下载的目录。
English: Build standalone game modules and PGA1 packages with a website catalog.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import re
import struct
import subprocess
import sys
import zlib

ROOT = Path(__file__).resolve().parents[1]
HEADER_BYTES = 512
PAYLOAD_MAX = 2 * 1024 * 1024
PACKAGE_MAX = 4 * 1024 * 1024
GAMES = (
    ("gomoku", "五子棋", "五子连珠 · 离线 AI 对弈"),
    ("go", "围棋", "九路围棋 · 离线 AI 对弈"),
    ("xiangqi", "象棋", "中国象棋 · 离线 AI 对弈"),
    ("chess", "国际象棋", "国际象棋 · 离线 AI 对弈"),
    ("2048", "2048", "滑动合并 · 挑战高分"),
    ("snake", "贪吃蛇", "方向控制 · 三档速度"),
)
MODULE_INPUTS = (
    "games/src/builtin_games.c",
    "games/sdk/pico_game_module_compat.h",
    "components/read_pico_apps/include/pico_app_sdk.h",
)


def native_target() -> str:
    system = platform.system().lower()
    machine = platform.machine().lower()
    machine = {"amd64": "x86_64", "aarch64": "arm64"}.get(machine, machine)
    if system == "darwin" and machine in ("arm64", "x86_64"):
        return "macos-" + machine
    if system == "linux" and machine in ("arm64", "x86_64"):
        return "linux-" + machine
    raise ValueError(f"Unsupported native package target: {system}/{machine}")


def source_digest() -> tuple[bytes, dict[str, str]]:
    combined = hashlib.sha256()
    hashes = {}
    for name in MODULE_INPUTS:
        data = (ROOT / name).read_bytes()
        hashes[name] = hashlib.sha256(data).hexdigest()
        combined.update(name.encode() + b"\0" + data + b"\0")
    return combined.digest(), hashes


def field(text: str, maximum: int, label: str) -> bytes:
    data = text.encode("utf-8")
    if not data or b"\0" in data or len(data) >= maximum:
        raise ValueError(f"Invalid {label}")
    return data + bytes(maximum - len(data))


def package_bytes(game_id: str, title: str, description: str, kind: int,
                  payloads: list[tuple[str, bytes]], digest: bytes,
                  version: int = 10001) -> bytes:
    if not re.fullmatch(r"[a-z0-9][a-z0-9-]{0,30}", game_id) or game_id.endswith('-'):
        raise ValueError("Invalid game id")
    if not 0 <= kind <= 255 or len(digest) != 32 or not 0 < version <= 0xffffffff:
        raise ValueError("Invalid package metadata")
    if not 1 <= len(payloads) <= 4 or len({name for name, _ in payloads}) != len(payloads):
        raise ValueError("Packages need one to four unique payload targets")
    header = bytearray(HEADER_BYTES)
    header[:8] = b"PICOAPP1"
    total = HEADER_BYTES + sum(len(data) for _, data in payloads)
    if total > PACKAGE_MAX:
        raise ValueError("Package exceeds 4 MiB")
    struct.pack_into("<6I", header, 8, 1, HEADER_BYTES, total, 1, version, kind)
    header[32:64] = field(game_id, 32, "game id")
    header[64:128] = field(title, 64, "title")
    header[128:224] = field(description, 96, "description")
    struct.pack_into("<2I", header, 224, 0, len(payloads))
    offset = HEADER_BYTES
    for index, (target, data) in enumerate(payloads):
        if not re.fullmatch(r"[a-z0-9][a-z0-9_-]+", target):
            raise ValueError("Invalid payload target")
        if not 0 < len(data) <= PAYLOAD_MAX:
            raise ValueError("Payload must contain 1 byte to 2 MiB")
        start = 256 + index * 32
        header[start:start + 16] = field(target, 16, "payload target")
        struct.pack_into("<4I", header, start + 16, offset, len(data), zlib.crc32(data), 0)
        offset += len(data)
    header[448:480] = digest
    struct.pack_into("<I", header, 232, zlib.crc32(header))
    return bytes(header) + b"".join(data for _, data in payloads)


def write_atomic(path: Path, data: bytes) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_name(path.name + ".tmp")
    temporary.write_bytes(data)
    temporary.replace(path)


def build_native(output: Path, game_id: str, kind: int) -> Path:
    output.mkdir(parents=True, exist_ok=True)
    suffix = ".dylib" if sys.platform == "darwin" else ".so"
    destination = output / (game_id + suffix)
    temporary = destination.with_name(destination.name + ".tmp")
    command = [os.environ.get("CC", "cc"), "-std=gnu11", "-O2", "-g0", "-fPIC",
               "-ffile-prefix-map=" + str(ROOT) + "=.",
               "-fno-builtin", "-fno-stack-protector", "-fvisibility=hidden",
               "-dynamiclib" if sys.platform == "darwin" else "-shared",
               "-Wall", "-Wextra", "-Wno-unused-function", "-Wno-unused-parameter",
               "-DPICO_GAME_MODULE", f"-DPICO_GAME_KIND={kind}",
               '-DPICO_GAME_ID="' + game_id + '"',
               "-I", str(ROOT / "components/read_pico_apps/include"),
               "-I", str(ROOT / "games/sdk"),
               str(ROOT / "games/src/builtin_games.c"), "-lm", "-o", str(temporary)]
    if sys.platform.startswith("linux"):
        command.append("-Wl,-z,defs")
    elif sys.platform == "darwin":
        # 发布载荷不能保留本机输出目录或调试路径。/ Release payloads must not retain local output or debug paths.
        command.append("-Wl,-install_name,@rpath/read-pico-" + game_id + ".dylib")
    subprocess.run(command, cwd=ROOT, check=True)
    temporary.replace(destination)
    return destination


def verify_elf_manifest(directory: Path, digest: bytes, inputs: dict[str, str]) -> dict:
    from build_game_elf import check_elf
    manifest = json.loads((directory / "manifest.json").read_text())
    if (manifest.get("schema") != 1 or manifest.get("target") != "esp32s3-xtensa"
            or manifest.get("source_sha256") != digest.hex() or manifest.get("inputs") != inputs
            or manifest.get("build_script_sha256") != hashlib.sha256((ROOT / "tools/build_game_elf.py").read_bytes()).hexdigest()):
        raise ValueError("ELF build manifest is stale or does not match the current game sources")
    if set(manifest.get("games", {})) != {game_id for game_id, _, _ in GAMES}:
        raise ValueError("ELF build manifest must describe all six games")
    for game_id, _, _ in GAMES:
        item = manifest["games"][game_id]
        path = directory / (game_id + ".elf")
        code = path.read_bytes()
        if (item.get("file") != path.name or item.get("bytes") != len(code)
                or item.get("sha256") != hashlib.sha256(code).hexdigest()):
            raise ValueError("ELF payload differs from its build manifest: " + game_id)
        check_elf(path)
    return manifest


def build_packages(output: Path, *, native_only: bool = False,
                   elf_dir: Path | None = None, url_prefix: str = "./") -> dict:
    output = output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    if native_only and elf_dir is not None:
        raise ValueError("Choose --native-only or --elf-dir")
    if not native_only and elf_dir is None:
        raise ValueError("Device payloads require --elf-dir; use --native-only for computer preview")
    if elf_dir is not None:
        elf_dir = elf_dir.resolve()
    digest, inputs = source_digest()
    device_manifest = verify_elf_manifest(elf_dir, digest, inputs) if elf_dir is not None else None
    target = native_target()
    items = []
    for kind, (game_id, title, description) in enumerate(GAMES):
        native = build_native(output / "native", game_id, kind)
        payloads = [(target, native.read_bytes())]
        if elf_dir is not None:
            device = elf_dir / (game_id + ".elf")
            code = device.read_bytes()
            if not code.startswith(b"\x7fELF"):
                raise ValueError(f"Device payload is not ELF: {device}")
            payloads.insert(0, ("esp32s3-xtensa", code))
        package = package_bytes(game_id, title, description, kind, payloads, digest)
        filename = game_id + ".pga"
        write_atomic(output / filename, package)
        items.append({"id": game_id, "kind": kind, "title": title,
                      "description": description, "version": "1.0.1", "version_code": 10001,
                      "abi": 1, "file": filename, "url": url_prefix.rstrip("/") + "/" + filename,
                      "bytes": len(package), "sha256": hashlib.sha256(package).hexdigest(),
                      "source_sha256": digest.hex(), "targets": [name for name, _ in payloads],
                      "payloads": [{"target": name, "bytes": len(data),
                                    "sha256": hashlib.sha256(data).hexdigest()} for name, data in payloads]})
    if source_digest() != (digest, inputs):
        raise ValueError("Game sources changed during packaging; rebuild the complete package set")
    catalog = {"schema": 1, "format": "PGA1", "abi": 1,
               "device_compatible": not native_only,
               "targets": items[0]["targets"],
               "notice": "此处游戏包同时适用于阅读器与本机预览，可复制到 TF 卡的 apps/games 目录。" if not native_only else "此处游戏包仅适用于本机预览，不能复制到阅读器；尚未构建设备平台代码。",
               "games": items}
    write_atomic(output / "catalog.json", (json.dumps(catalog, ensure_ascii=False, indent=2) + "\n").encode())
    report = {"schema": 1, "kind": "standalone-game-packages", "native_target": target,
              "source_sha256": digest.hex(), "sources": inputs,
              "builder_sha256": hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
              "native_only": native_only, "catalog_sha256": hashlib.sha256((output / "catalog.json").read_bytes()).hexdigest(),
              "packages": {item["file"]: item["sha256"] for item in items}, "hardware_access": False}
    if device_manifest is not None:
        report["device_manifest_sha256"] = hashlib.sha256((elf_dir / "manifest.json").read_bytes()).hexdigest()
        report["device_build"] = device_manifest
    write_atomic(output / "build-report.json", (json.dumps(report, ensure_ascii=False, indent=2) + "\n").encode())
    return report


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, default=ROOT / "build/game-packages")
    parser.add_argument("--native-only", action="store_true")
    parser.add_argument("--elf-dir", type=Path)
    parser.add_argument("--url-prefix", default="./")
    args = parser.parse_args()
    try:
        build_packages(args.output, native_only=args.native_only, elf_dir=args.elf_dir,
                       url_prefix=args.url_prefix)
    except (ValueError, OSError, subprocess.CalledProcessError) as error:
        parser.exit(1, str(error) + "\n")
    print(args.output.resolve() / "catalog.json")


if __name__ == "__main__":
    main()

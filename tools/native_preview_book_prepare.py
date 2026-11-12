#!/usr/bin/env python3
"""Prepare actual book modules for the native web preview without changing firmware sources.
中文：仅生成宿主构建输入，保留生产页面和解析/分页/进度实现。
"""
from pathlib import Path
import hashlib
import json
import re
import shutil

ROOT = Path(__file__).resolve().parents[1]
BOOK_SOURCES = (
    "book_source.c", "book_txt.c", "gbk.c", "book_epub.c", "zip_reader.c",
    "html_text.c", "book_image.c", "vendor/tjpgd.c", "book_layout.c",
    "book_progress.c", "book_cover.c",
)


def prepare_book(build: Path):
    """Return original module paths; generate only the page's host storage adaptation."""
    build = Path(build)
    build.mkdir(parents=True, exist_ok=True)
    stubs = build / "stubs"
    stubs.mkdir(exist_ok=True)
    original = ROOT / "main/apps/app_book.c"
    source = original.read_text()
    # 所有绘制/事件函数和4位framebuffer长度原样保留。
    # Preserve all paint/event functions and the packed 4-bit framebuffer size.
    source = re.sub(r'^#include "[^"\n]+"\n', '', source, flags=re.M)
    # 包装绘制边界以注入冷绘制耗时；生产函数体保持原样。/ Wrap only the draw boundary for cold-render delay injection, preserving its production body.
    source=source.replace('static void draw_reader(uint8_t* fb, size_t page) {', 'static void actual_draw_reader(uint8_t* fb, size_t page) {')
    (build / "native-preview-book-source.inc").write_text(source)
    # 删除确认后的业务逻辑直接复用生产实现，包含传书覆盖备份的处理。
    # Reuse production deletion exactly, including replacement-backup recovery.
    store_original = ROOT / "main/book/book_store.c"
    store_source = store_original.read_text()
    ready = store_source[store_source.index("static bool sd_ready(void)"):store_source.index("static esp_err_t flash_root(")]
    delete = store_source[store_source.index("esp_err_t book_store_delete("):]
    (build / "native-preview-book-delete.inc").write_text(ready + delete)
    for relative in ("main/apps/app_book.h", "main/app/app_registry.h", "main/settings.h", "components/read_pico_search/include/read_pico_search.h"):
        shutil.copyfile(ROOT / relative, stubs / Path(relative).name)
    for name in ("nvs.h",):
        shutil.copyfile(ROOT / "tools/book_storage_stubs" / name, stubs / name)
    target = stubs / "miniz.h"
    if not target.exists():
        shutil.copyfile(ROOT / "tools/zip_host_stubs/miniz.h", target)
    report = {
        "book_page_source_sha256": hashlib.sha256(original.read_bytes()).hexdigest(),
        "book_delete_source_sha256": hashlib.sha256(store_original.read_bytes()).hexdigest(),
        "actual_book_delete_and_backup_recovery": True,
        "actual_parser_layout_progress": True,
        "host_only_adaptations": ["platform includes supplied by host", "draw boundary timestamps and opt-in cold-render delay injection", "foreground rendering without hardware page-preparation worker", "preview-only TF roots and file-backed NVS", "VFS maps unchanged firmware /sdcard paths to session-private tf directory"],
        "modules": {name: hashlib.sha256((ROOT / "main/book" / name).read_bytes()).hexdigest() for name in BOOK_SOURCES},
    }
    (build / "native-preview-book-source-evidence.json").write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n")
    return [ROOT / "tools/native_preview_book.c", ROOT / "tools/native_preview_book_storage.c", ROOT / "tools/native_preview_book_vfs.c", ROOT / "components/read_pico_search/read_pico_search.c"] + [ROOT / "main/book" / name for name in BOOK_SOURCES]


if __name__ == "__main__":
    import argparse
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("build", type=Path)
    args = parser.parse_args()
    print("\n".join(map(str, prepare_book(args.build))))

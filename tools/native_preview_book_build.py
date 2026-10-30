#!/usr/bin/env python3
"""Book build hook for the common native preview runtime. / 真实阅读器宿主构建入口。"""
from pathlib import Path
import importlib.util


def prepare(output, stubs=None):
    path = Path(__file__).with_name("native_preview_book_prepare.py")
    spec = importlib.util.spec_from_file_location("native_preview_book_prepare", path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    output = Path(output)
    if stubs is not None and Path(stubs) != output / "stubs":
        raise ValueError("Book host expects the common output/stubs directory")
    return module.prepare_book(output)

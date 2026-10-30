"""SPDX-License-Identifier: Apache-2.0
中文：真实计页算法与EPUB历史隔离的可复跑ASan/UBSan测试。
English: Reproducible ASan/UBSan checks for real pagination and EPUB history isolation.
"""
from pathlib import Path
import subprocess
import tempfile
import zipfile
root = Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory() as folder:
    tmp = Path(folder)
    epub = tmp / "pagination.epub"
    with zipfile.ZipFile(epub, "w", zipfile.ZIP_DEFLATED) as z:
        z.writestr("mimetype", "application/epub+zip")
        z.writestr("META-INF/container.xml", '<container><rootfiles><rootfile full-path="book.opf" media-type="application/oebps-package+xml"/></rootfiles></container>')
        z.writestr("book.opf", '<package><manifest><item id="a" href="a.xhtml" media-type="application/xhtml+xml"/><item id="b" href="b.xhtml" media-type="application/xhtml+xml"/></manifest><spine><itemref idref="a"/><itemref idref="b"/></spine></package>')
        z.writestr("a.xhtml", '<html><body><img src="shared.jpg"/><h1>Title</h1><p>甲乙丙丁 Wi words</p><img src="ordinary.jpg"/></body></html>')
        z.writestr("b.xhtml", '<html><body><h1>Second</h1><p>next 123 中文</p><img src="shared.jpg"/><img src="ordinary.jpg"/></body></html>')
        z.writestr("shared.jpg", b"not decoded")
        z.writestr("ordinary.jpg", b"not decoded either")
    text = tmp / "pagination.txt"
    text.write_text("第一章\n甲乙丙丁。 Wi sample\n" * 50, encoding="utf-8")
    sources = ["tools/book_pagination_host_test.c", "main/book/book_layout.c", "main/book/book_source.c", "main/book/book_txt.c", "main/book/gbk.c", "main/book/book_epub.c", "main/book/zip_reader.c", "main/book/html_text.c", "main/book/book_image.c", "main/book/vendor/tjpgd.c"]
    args = ["gcc", "-std=c11", "-D_DEFAULT_SOURCE", "-Wall", "-Wextra", "-Werror", "-g", "-fsanitize=address,undefined", "-fno-omit-frame-pointer"]
    for include in ["main/book", "tools/book_layout_job_stubs", "tools/book_layout_stubs", "tools/book_epub_stubs", "tools/zip_host_stubs"]:
        args += ["-I" + str(root / include)]
    subprocess.run(args + [str(root / s) for s in sources] + ["-lz", "-o", str(tmp / "test")], check=True)
    subprocess.run([str(tmp / "test"), str(epub), str(text)], check=True)

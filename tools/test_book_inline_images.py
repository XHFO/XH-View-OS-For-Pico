#!/usr/bin/env python3
"""Run actual EPUB/image/layout auto-load with original fixtures under ASan/UBSan."""
from pathlib import Path
import subprocess
import os
ROOT=Path(__file__).resolve().parents[1]
output=ROOT/'build/book-inline-image-tests';output.mkdir(parents=True,exist_ok=True)
subprocess.run(['python3',str(ROOT/'tools/gen_book_image_fixtures.py')],check=True)
includes=['main/book','tools/book_epub_stubs','tools/book_layout_stubs','tools/zip_host_stubs']
sources=['tools/book_inline_images_host_test.c','main/book/book_source.c','main/book/book_txt.c','main/book/gbk.c','main/book/book_epub.c','main/book/zip_reader.c','main/book/html_text.c','main/book/book_image.c','main/book/vendor/tjpgd.c','main/book/book_layout.c']
exe=output/'inline-images'
subprocess.run(['cc','-std=c11','-D_DEFAULT_SOURCE','-Wall','-Wextra','-Werror','-g','-fsanitize=address,undefined','-fno-omit-frame-pointer',*['-I'+str(ROOT/x) for x in includes],*[str(ROOT/x) for x in sources],'-lz','-o',str(exe)],check=True)
subprocess.run([str(exe),str(ROOT/'build/native-preview-image-fixtures/001-original-illustrated.epub')],check=True,env={**os.environ,'UBSAN_OPTIONS':'halt_on_error=1'})

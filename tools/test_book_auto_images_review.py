#!/usr/bin/env python3
"""Independent production layout review; no device or production-source mutation."""
from pathlib import Path
import hashlib
import json
import os
import re
import subprocess
import tempfile

ROOT=Path(__file__).resolve().parents[1]


def function(text,name):
    found=re.search(r"^(?:[A-Za-z_][A-Za-z0-9_]*[ \t*]+)+"+name+r"\([^\n]*\) \{",text,re.M)
    assert found,name
    start,at,depth,quote,escape=found.start(),found.end(),1,None,False
    while depth:
        char=text[at]
        if quote:
            if escape:escape=False
            elif char=="\\":escape=True
            elif char==quote:quote=None
        elif char in "\"'":quote=char
        elif char=="{":depth+=1
        elif char=="}":depth-=1
        at+=1
    return text[start:at]


def main():
    sources=["main/book/book_layout.c","main/book/book_layout.h","main/book/html_text.h","main/book/book_source.c",
             "main/book/book_source.h","main/book/book_epub.c","main/apps/app_book.c","tools/book_auto_images_review.c",
             "tools/book_auto_images_resources_review.c","tools/book_epub_dimensions_review.c"]
    before={name:hashlib.sha256((ROOT/name).read_bytes()).hexdigest() for name in sources}
    output=ROOT/"build/book-auto-images-independent-review"
    output.mkdir(parents=True,exist_ok=True)
    report=output/"report.json";report.unlink(missing_ok=True)
    app=(ROOT/"main/apps/app_book.c").read_text()
    render_helpers=("render","draw_reader","draw_image","prep_task")
    for name in render_helpers:
        assert not re.search(r"\b(?:prepare_page_images|open_cover|book_chapter_load\w*|book_epub_load\w*|book_image_decode|fopen|fread|zip_\w+)\s*\(",function(app,name)),"Book/resource IO in "+name
    with tempfile.TemporaryDirectory(prefix="pico-image-independent-") as folder:
        temporary=Path(folder)
        (temporary/"book-source-images-review.inc").write_text(function((ROOT/"main/book/book_source.c").read_text(),"book_chapter_prepare_images"))
        (temporary/"book-cover-lifecycle-review.inc").write_text("\n".join(function(app,name) for name in ("free_image","close_image","open_cover","turn_page")))
        (temporary/"book-epub-dimensions-review.inc").write_text(function((ROOT/"main/book/book_epub.c").read_text(),"load_chapter_blocks"))
        logs=[]
        for name in ("book_auto_images_review","book_auto_images_resources_review","book_epub_dimensions_review"):
            binary=temporary/name
            command=["cc","-std=c11","-D_DEFAULT_SOURCE","-Wall","-Wextra","-Werror","-O1","-g","-fsanitize=address,undefined","-fno-omit-frame-pointer",
                     "-I"+str(temporary),"-I"+str(ROOT/"tools/book_layout_stubs"),"-I"+str(ROOT/"tools/book_source_host_stubs"),"-I"+str(ROOT/"main/book"),
                     str(ROOT/"tools"/(name+".c"))]
            if name=="book_auto_images_review":command.append(str(ROOT/"main/book/book_layout.c"))
            subprocess.run(command+["-o",str(binary)],check=True)
            result=subprocess.run([str(binary)],stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True,
                                  env={**os.environ,"UBSAN_OPTIONS":"halt_on_error=1"})
            logs.append(result.stdout);(output/"validation.log").write_text("".join(logs))
            print(result.stdout,end="");result.check_returncode()
    assert before=={name:hashlib.sha256((ROOT/name).read_bytes()).hexdigest() for name in sources},"Sources changed during review; rerun stable source"
    report.write_text(json.dumps({"status":"PASS","production_layout":True,"sanitizers":["ASan","UBSan"],
                                 "device_access":False,"sources":before},indent=2)+"\n")


if __name__=="__main__":
    main()

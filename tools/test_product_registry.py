#!/usr/bin/env python3
"""产品与原生预览的入口一致，保留独立出厂恢复入口。 / Match product entries while retaining factory recovery."""
from pathlib import Path
import os
import re
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
source = (ROOT/'main/app/app_registry.c').read_text()
runtime = (ROOT/'tools/native_preview_runtime.c').read_text()
entries = re.findall(r'&app_\w+', source.split('s_apps[] = {', 1)[1].split('};', 1)[0])
native = re.findall(r'&app_\w+', runtime.split('const app_desc_t *list[]={', 1)[1].split('};', 1)[0])
assert entries == native == ['&app_pico_system', '&app_book', '&app_font_pick', '&app_transfer']
with tempfile.TemporaryDirectory(prefix='pico-registry-') as folder:
    root = Path(folder)
    (root/'app.h').write_text('#pragma once\n#include <stddef.h>\ntypedef struct { int id; } app_desc_t;\n')
    (root/'app_registry.h').write_text('#include "app.h"\nconst app_desc_t *app_at(int),*app_home_page(void),*app_selftest_page(void);int app_count(void),app_index_of(const app_desc_t*);\n')
    (root/'registry.c').write_text(source)
    (root/'test.c').write_text('''#include <assert.h>
#include "app_registry.h"
const app_desc_t app_pico_system={0},app_book={1},app_font_pick={2},app_transfer={3},app_selftest={4};
int main(void){
    assert(app_count()==4&&app_at(-1)==NULL&&app_at(4)==NULL);
    for(int i=0;i<4;i++)assert(app_index_of(app_at(i))==i);
    assert(app_home_page()==&app_pico_system&&app_selftest_page()==&app_selftest);
    assert(app_index_of(&app_selftest)==-1);
}
''')
    binary = root/'test'
    subprocess.run([os.environ.get('CC', 'cc'), '-std=c11', '-Wall', '-Wextra', '-Werror', '-I', folder,
                    str(root/'registry.c'), str(root/'test.c'), '-o', str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
print('PASS: four matching product/native entries; original demos excluded; factory boot-resume retained.')

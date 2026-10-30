#!/usr/bin/env python3
"""SPDX-License-Identifier: Apache-2.0
中文：真实封面优先级、路径隔离、解码和缓存回归。 / Real cover priority, path isolation, decoding and cache tests.
"""
from pathlib import Path
import struct
import subprocess
import tempfile
import zipfile
import zlib
ROOT=Path(__file__).resolve().parents[1]
def png(w,h,gray):
    def chunk(tag,data):return struct.pack('>I',len(data))+tag+data+struct.pack('>I',zlib.crc32(tag+data))
    return b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR',struct.pack('>IIBBBBB',w,h,8,0,0,0,0))+chunk(b'IDAT',zlib.compress((b'\0'+bytes([gray])*w)*h))+chunk(b'IEND',b'')
def main():
    with tempfile.TemporaryDirectory(prefix='pico-cover-') as folder:
        tmp=Path(folder)
        def epub(name,items,files,metadata='',guide='',prefix=''):
            pkg=f'<{prefix}package xmlns:opf="http://www.idpf.org/2007/opf"><{prefix}metadata>{metadata}</{prefix}metadata><{prefix}manifest>{items}</{prefix}manifest><{prefix}spine><{prefix}itemref idref="text"/></{prefix}spine><{prefix}guide>{guide}</{prefix}guide></{prefix}package>'
            with zipfile.ZipFile(tmp/name,'w',zipfile.ZIP_DEFLATED) as z:
                z.writestr('META-INF/container.xml','<container><rootfiles><rootfile full-path="OPS/book.opf" media-type="application/oebps-package+xml"/></rootfiles></container>')
                z.writestr('OPS/book.opf',pkg)
                z.writestr('OPS/chapter.xhtml','<html><body><p>Original text preserved</p></body></html>')
                for path,data in files.items():z.writestr(path,data)
        def item(id,href,media='image/png',props='',prefix=''):
            return f'<{prefix}item id="{id}" href="{href}" media-type="{media}" properties="{props}"/>'
        epub('epub3.epub',item('cover','wrong.png')+item('real','real.png',props='cover-image'),{'OPS/wrong.png':png(32,48,240),'OPS/real.png':png(240,360,32)},metadata='<meta name="description" content="'+'long '*100+'"/>')
        epub('epub2.epub',item('arbitrary-id','image.png'),{'OPS/image.png':png(48,72,64)},metadata='<meta name="cover" content="arbitrary-id"/>')
        epub('xhtml.epub',item('page','pages/front.xhtml','application/xhtml+xml'),{'OPS/pages/front.xhtml':'<html><body><img src="../art/front.png"/></body></html>','OPS/art/front.png':png(64,96,96)},guide='<reference type="cover" href="pages/front.xhtml"/>')
        epub('svg.epub',item('cover','front.svg','image/svg+xml'),{'OPS/front.svg':'<svg xmlns:xlink="http://www.w3.org/1999/xlink"><image xlink:href="art/pic.png"/></svg>','OPS/art/pic.png':png(60,90,128)})
        epub('namespace.epub',item('real','art/front%20cover.png',props='cover-image',prefix='opf:'),{'OPS/art/front cover.png':png(80,120,160)},prefix='opf:')
        epub('fallback.epub',item('cover-image','image.png'),{'OPS/image.png':png(32,48,192)})
        epub('replacement.epub',item('cover','image.png'),{'OPS/image.png':png(256,128,224)})
        epub('none.epub',item('ordinary','illustration.png'),{'OPS/illustration.png':png(32,48,16)})
        epub('remote.epub',item('cover','https://example.com/cover.png'),{})
        epub('traversal.epub',item('cover','../../outside.png'),{'outside.png':png(32,48,16)})
        epub('corrupt-image.epub',item('cover','image.png'),{'OPS/image.png':b'not an image'})
        for index,letter in enumerate('abcdefghi'):
            epub(f'lru-{letter}.epub',item('cover','image.png',props='cover-image'),{'OPS/image.png':png(24,36,(index+1)*17)})
        for index in range(7):
            epub(f'lru-x{index}.epub',item('cover','image.png',props='cover-image'),{'OPS/image.png':png(24,36,187+index*9)})
        (tmp/'broken.epub').write_bytes(b'bad ZIP')
        unchanged={p.name:p.read_bytes() for p in tmp.glob('*.epub') if p.name not in ('epub3.epub','replacement.epub')}
        flags=['-std=c11','-D_DEFAULT_SOURCE','-Wall','-Wextra','-Werror','-g','-O1','-fsanitize=address,undefined','-fno-omit-frame-pointer']
        for include in ['main/book','tools/book_layout_job_stubs','tools/book_layout_stubs','tools/book_epub_stubs','tools/zip_host_stubs']:flags+=['-I'+str(ROOT/include)]
        obj=tmp/'epub.o'
        subprocess.run(['cc']+flags+['-Dbook_epub_load_cover=real_book_epub_load_cover','-c',str(ROOT/'main/book/book_epub.c'),'-o',str(obj)],check=True)
        cover_obj=tmp/'cover.o'
        subprocess.run(['cc']+flags+['-Drename=cover_test_rename','-c',str(ROOT/'main/book/book_cover.c'),'-o',str(cover_obj)],check=True)
        sources=['tools/book_cover_host_test.c','main/book/zip_reader.c','main/book/html_text.c','main/book/book_image.c','main/book/vendor/tjpgd.c']
        exe=tmp/'test'
        subprocess.run(['cc']+flags+[str(ROOT/s) for s in sources]+[str(obj),str(cover_obj),'-lz','-o',str(exe)],check=True)
        subprocess.run([str(exe),str(tmp)],check=True)
        assert all((tmp/name).read_bytes()==data for name,data in unchanged.items()),'Cover extraction mutated original ebook'
        print('Original EPUB bytes unchanged; temporary fixture/cache only: PASS')
if __name__=='__main__':main()

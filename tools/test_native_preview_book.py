#!/usr/bin/env python3
"""中文：通过预览进程的真实触摸入口验证 app_book，而非旧 PS_READ 示例。
English: Exercise app_book through real preview touch events, not the old PS_READ demo.
"""
from pathlib import Path
import argparse
import json
import struct
import subprocess
import tempfile
import zlib
import zipfile

ROOT=Path(__file__).resolve().parents[1]


def png_from_pgm(pgm, png):
    signature, dimensions, maximum, pixels=pgm.read_bytes().split(b"\n",3)
    w,h=map(int,dimensions.split())
    assert signature==b"P5" and maximum==b"255" and len(pixels)==w*h
    def chunk(kind,data):
        return struct.pack(">I",len(data))+kind+data+struct.pack(">I",zlib.crc32(kind+data))
    rows=b"".join(b"\0"+pixels[y*w:(y+1)*w] for y in range(h))
    png.write_bytes(b"\x89PNG\r\n\x1a\n"+chunk(b"IHDR",struct.pack(">IIBBBBB",w,h,8,0,0,0,0))+chunk(b"IDAT",zlib.compress(rows,6))+chunk(b"IEND",b""))


def persisted_annotations(index_path):
    """Read the actual v2 store format, including the saved note body. / 校验真实 v2 记录正文。"""
    version,count,sequence,slot=struct.unpack_from("<IIII",index_path.read_bytes(),12)
    assert version==2 and count<=32 and slot in (0,1)
    data=index_path.with_name("reader-r2-"+("b" if slot else "a")+"-0.dat").read_bytes()
    assert struct.unpack_from("<IIII",data,12)==(2,sequence,0,count)
    # Compact chunks contain count records; accept earlier fixed 32-item native chunks too.
    # 紧凑块仅包含实际记录；兼容先前原生预览的32条定长块。
    record_bytes=(len(data)-28)//max(count,1)
    if record_bytes>2048:record_bytes=(len(data)-28)//32
    assert 1564<=record_bytes<=1576 and (len(data)-28)%record_bytes==0
    records=[]
    for i in range(count):
        record=data[28+i*record_bytes:28+(i+1)*record_bytes]
        records.append({"book":record[8],"type":record[9],"text":record[524:908].split(b"\0",1)[0].decode("utf-8")})
    return records,data


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary",type=Path,default=ROOT/"build/native-preview-local/native-preview")
    parser.add_argument("--font",type=Path,default=ROOT/"sdcard/fonts/MiSansVF.ttf")
    parser.add_argument("--output",type=Path,default=ROOT/"build/native-preview-book-integration")
    args=parser.parse_args();output=args.output.resolve();output.mkdir(parents=True,exist_ok=True)
    # A failed run must not leave a previous passing acceptance report behind.
    # 失败时不保留上次 PASS，避免把旧结果误认为本次通过。
    (output/"report.json").unlink(missing_ok=True)
    session=Path(tempfile.mkdtemp(prefix="session-",dir=output));books=session/"tf/books";books.mkdir(parents=True)
    text="".join(f"第{i}章 阅读测试\n"+("这是与设备采用相同阅读引擎的长文本，每一页都应该保持正确的分页。文字中可以长按拖选，保存划线与笔记。\n"*70) for i in range(1,5))
    (books/"001-long.txt").write_text(text)
    def cover_png():
        def chunk(kind,data):return struct.pack(">I",len(data))+kind+data+struct.pack(">I",zlib.crc32(kind+data))
        pixels=b"".join(b"\0"+bytes([45 if y<50 or y>145 else 205])*128 for y in range(192))
        return b"\x89PNG\r\n\x1a\n"+chunk(b"IHDR",struct.pack(">IIBBBBB",128,192,8,0,0,0,0))+chunk(b"IDAT",zlib.compress(pixels))+chunk(b"IEND",b"")
    with zipfile.ZipFile(books/"002-book.epub","w",zipfile.ZIP_DEFLATED) as epub:
        epub.writestr("META-INF/container.xml",'<container><rootfiles><rootfile full-path="OPS/book.opf" media-type="application/oebps-package+xml"/></rootfiles></container>')
        epub.writestr("OPS/book.opf",'<package xmlns="http://www.idpf.org/2007/opf" version="3.0"><metadata><title>真实封面图文测试</title></metadata><manifest><item id="nav" href="nav.xhtml" media-type="application/xhtml+xml" properties="nav"/><item id="c1" href="c1.xhtml" media-type="application/xhtml+xml"/><item id="c2" href="c2.xhtml" media-type="application/xhtml+xml"/><item id="cover" href="cover.png" media-type="image/png" properties="cover-image"/></manifest><spine><itemref idref="c1"/><itemref idref="c2"/></spine></package>')
        epub.writestr("OPS/nav.xhtml",'<html><body><nav epub:type="toc"><ol><li><a href="c1.xhtml">第一章 图文阅读</a></li><li><a href="c2.xhtml">第二章 阅读继续</a></li></ol></nav></body></html>')
        for i in (1,2):epub.writestr(f"OPS/c{i}.xhtml",f'<html><body><h1>第{i}章 图文阅读</h1>'+('<p>这本电子书由本地测试生成，使用真实 EPUB 解析器读取正文、目录和封面。长按文字可以选择并保存批注。</p>'*50)+'</body></html>')
        epub.writestr("OPS/cover.png",cover_png())
    log=(session/"runtime.log").open("w")
    process=subprocess.Popen([str(args.binary.resolve()),str(args.font.resolve()),str(session),str(session/"frame.pgm")],stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=log,text=True)
    now=10000;transcript=[];scenes={}
    def command(value):
        nonlocal now
        process.stdin.write(value+"\n");process.stdin.flush()
        line=process.stdout.readline()
        if not line: raise RuntimeError("Native runtime ended; see "+str(session/"runtime.log"))
        state=json.loads(line);assert state["ok"],state;now=state["now_ms"]
        transcript.append({"command":value,"state":state});return state
    def tick(delta=100):
        nonlocal now
        now+=delta;return command(f"tick {now}")
    def tap(x,y):
        nonlocal now
        now+=20;command(f"touch down {x} {y} {now}")
        now+=60;return command(f"touch up {x} {y} {now}")
    def hold(x,y,tx=None,ty=None):
        nonlocal now
        now+=20;command(f"touch down {x} {y} {now}");state=tick(700)
        if tx is not None:
            now+=40;state=command(f"touch move {tx} {ty} {now}")
        now+=40;return command(f"touch up {x if tx is None else tx} {y if ty is None else ty} {now}")
    def capture(name,state):
        png_from_pgm(session/"frame.pgm",output/(name+".png"));scenes[name]=state
    try:
        command("route 0")
        for _ in range(12):state=tick()
        capture("shelf",state)
        caches=list(books.glob(".pico-covers/*.pcv"));assert len(caches)==1 and caches[0].stat().st_size>128*192,"Real EPUB cover thumbnail cache missing"
        cache_digest=zlib.crc32(caches[0].read_bytes());cache_time=caches[0].stat().st_mtime_ns
        command("library")
        for _ in range(12):state=tick()
        assert caches[0].stat().st_mtime_ns==cache_time and zlib.crc32(caches[0].read_bytes())==cache_digest,"Shelf refresh rewrote the cached cover"
        state=tap(60,330);assert state["active_app"]=="book" and state["book_pages"]==0,state
        capture("book-loading",state)
        for _ in range(8):state=tick()
        assert state["active_app"]=="book" and state["book_kind"]=="txt" and state["book_pages"]>2 and not state["book_jump"],state
        capture("txt-reading",state)
        before=state["revision"]
        for _ in range(5):state=tick()
        assert state["revision"]==before,"Incremental pagination repeatedly repainted reading"
        state=command("key 2 single");assert state["book_page"]==2 and not state["book_tools"],state
        state=command("key 0 single");assert state["book_page"]==1 and not state["book_tools"],state
        state=tap(110,20);assert state["book_jump"] and state["book_page"]==1,state
        capture("exact-page-input",state)
        for _ in range(4):state=tick()
        assert state["book_jump"] and state["book_page"]==1,state
        tap(282,265);state=tap(282,452)
        for _ in range(40):
            if not state["book_jump"]:break
            state=tick()
        assert not state["book_jump"] and state["book_page"]==3,state
        capture("exact-page-three",state)
        state=tap(22,20);assert state["book_view"]=="toc",state;capture("contents",state)
        state=command("key 1 single")
        assert state["active_app"]=="book" and state["book_view"]=="reading" and state["book_page"]==3,state
        capture("contents-return-reading",state)
        state=tap(22,20);assert state["book_view"]=="toc",state
        state=tap(100,162);assert state["book_chapter"]==2 and state["book_page"]==1,state
        capture("contents-chapter-two",state)
        state=hold(110,20);assert state["book_tools"],state
        capture("reading-tools",state)
        state=command("key 1 single")
        # 中键逐层返回工具后可能回到书架；总是从产品书架重新打开真实书。
        # The global middle key backs through tools; reopen from the shelf if it returned there.
        if state["active_app"]!="book":
            command("route 0")
            for _ in range(8):state=tick()
            tap(60,330)
            for _ in range(5):state=tick()
        state=hold(38,115,210,151);assert state["selection"],state
        capture("drag-selection",state)
        state=tap(60,75);assert not state["selection"],state
        record_files=list(session.glob("tf/**/reader-r2-index.dat"))
        assert record_files,"Actual reader did not persist the selected highlight"
        assert struct.unpack_from("<I",record_files[0].read_bytes(),16)[0]==1
        capture("highlight-saved",state)
        state=hold(38,115,210,151);assert state["selection"],state
        state=tap(140,75);assert state["active_app"]=="system" and state["route"]==19,state
        capture("real-book-note-editor",state)
        tap(246,292) # English mode in the real phone-like keyboard. / 实际键盘的英文模式。
        tap(24,412) # q on the actual first row. / 实际首行 q。
        state=tap(313,47)
        for _ in range(3):state=tick()
        assert state["active_app"]=="book",state
        assert struct.unpack_from("<I",record_files[0].read_bytes(),16)[0]==2,"Real note did not persist beside the highlight"
        saved_records,saved_chunk=persisted_annotations(record_files[0])
        assert saved_records==[{"book":255,"type":1,"text":""},{"book":255,"type":0,"text":"q"}],saved_records
        capture("note-saved-returned",state)
        # 实际图书的休眠/唤醒必须恢复同章节同页。
        # Sleeping from an actual book must resume its exact chapter/page.
        book_position=(state["book_chapter"],state["book_page"])
        state=command("sleep");capture("real-book-sleep",state)
        # A successful power checkpoint may commit an identical payload under a
        # new sequence/slot. Compare every record byte, including quote/anchor,
        # rather than treating the generation header and its CRC as note data.
        # 休眠检查点会以新代提交相同记录；全部正文/类型/选文/锚点仍须逐字节保留。
        sleep_records,sleep_chunk=persisted_annotations(record_files[0])
        assert sleep_records==saved_records and sleep_chunk[28:]==saved_chunk[28:],"Sleep checkpoint changed saved highlight/note records"
        state=command("wake")
        for _ in range(3):state=tick()
        assert state["active_app"]=="book" and (state["book_chapter"],state["book_page"])==book_position,state
        capture("real-book-wake",state)
        # 两角菜单在真实书内可展开并取消，不通过旧示例页。
        # Open/cancel both global corners while actual app_book owns the page.
        for side,x,target in (("left",8,80),("right",352,280)):
            now+=20;command(f"touch down {x} 627 {now}");now+=80;state=command(f"touch move {target} 540 {now}")
            assert state["fan"] and state["active_app"]=="book",state
            capture("book-fan-"+side,state)
            now+=40;state=command(f"touch cancel {target} 540 {now}");assert not state["fan"],state
        command("key 1 long");command("route 0")
        for _ in range(8):state=tick()
        state=tap(100,420);assert state["active_app"]=="book",state
        for _ in range(8):state=tick()
        assert state["book_pages"]>0 and state["book_kind"]=="epub",state
        assert state["book_view"]=="cover", state
        capture("epub-opening-cover",state)
        state=tap(180,320);assert state["book_view"]=="reading",state
        capture("epub-reading",state)
        state=tap(22,20);assert state["book_view"]=="toc",state;capture("epub-contents",state)
        # 横屏从真实目录/正文几何重排，不打开旧 PS_READ 路由。
        # Landscape repaginates actual reader geometry without opening PS_READ.
        command("key 1 single");state=command("rotate 1")
        for _ in range(4):state=tick()
        assert state["active_app"]=="book" and state["width"]==1216,state
        capture("epub-landscape",state)
        # 结束整个进程后复用同一私有 TF：真实封面缓存、批注与进度不能仅存在于 RAM。
        # Restart with the same private TF; covers, annotations and position must survive RAM loss.
        command("quit");process.wait(timeout=10);log.close()
        persisted_index=record_files[0].read_bytes()
        checkpoint_records,checkpoint_chunk=persisted_annotations(record_files[0])
        assert checkpoint_records==saved_records and checkpoint_chunk[28:]==saved_chunk[28:],"A normal checkpoint changed saved highlight/note records"
        log=(session/"runtime-restart.log").open("w")
        process=subprocess.Popen([str(args.binary.resolve()),str(args.font.resolve()),str(session),str(session/"frame.pgm")],stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=log,text=True)
        now=10000;command("route 0")
        for _ in range(12):state=tick()
        assert caches[0].stat().st_mtime_ns==cache_time and zlib.crc32(caches[0].read_bytes())==cache_digest,"Process restart rewrote the cached cover"
        assert record_files[0].read_bytes()==persisted_index,"Process restart changed the annotation index"
        assert persisted_annotations(record_files[0])==(checkpoint_records,checkpoint_chunk),"Process restart changed the actual saved highlight/note payload"
        capture("restart-shelf-cached-cover",state)
        state=command("route 2");capture("restart-persisted-records",state)
        command("route 0")
        for _ in range(8):state=tick()
        # The fixture persisted landscape; restore the actual portrait geometry before tapping.
        if state["width"]==1216:state=command("rotate 0")
        # Recently opened EPUB sorts ahead of TXT in the actual shelf.
        # 实际书架按最近阅读排序，刚打开的 EPUB 在前，TXT 在第二行。
        state=tap(100,420)
        for _ in range(8):state=tick()
        assert state["active_app"]=="book" and state["book_kind"]=="txt" and (state["book_chapter"],state["book_page"])==book_position,state
        capture("restart-reading-progress-highlight",state)
        state=command("key 1 single")
        assert state["active_app"]=="system" and state["route"]==0,state
        capture("reading-middle-back-shelf",state)
        report={"result":"PASS","actual_app_book":True,"process_restart":True,"cover_cache_reused":True,"persistent_annotation_count":2,"session":str(session),"scenes":scenes,"commands":len(transcript),"physical_acceptance":False}
        (output/"report.json").write_text(json.dumps(report,ensure_ascii=False,indent=2)+"\n")
        print(json.dumps({"result":"PASS","scenes":list(scenes),"commands":len(transcript),"output":str(output)},ensure_ascii=False))
    finally:
        (session/"transcript.json").write_text(json.dumps(transcript,ensure_ascii=False,indent=2)+"\n")
        if process.poll() is None:
            command("quit");process.wait(timeout=10)
        log.close()


if __name__=="__main__":main()

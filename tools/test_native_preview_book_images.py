#!/usr/bin/env python3
"""离线独立native进程回归实际app_book图文、封面及页码；不接触浏览器或设备。
Offline fresh native processes verify actual app_book illustrations, cover navigation and pages.
"""
from pathlib import Path
import argparse
import hashlib
import json
import tempfile
from gen_book_image_fixtures import make_fixture
from test_native_preview_shelf_ble import Runtime, page_position, read_pgm

ROOT=Path(__file__).resolve().parents[1]


def image_confirm_case(runtime,capture,point,landscape=False,label='portrait'):
    """Use actual touch PRESS/release and production confirmation controls, never direct image calls."""
    origin=runtime.command('status');position=page_position(origin)
    assert origin['book_view']=='reading' and not origin['book_tools'],origin
    width,height,pixels=read_pgm(runtime.frame)
    original_body=pixels[96*width:]
    cancel=(222,227) if landscape else (103,367)
    confirm=(418,227) if landscape else (257,367)

    def reading(state):
        assert state['active_app']=='book' and state['book_view']=='reading' and page_position(state)==position,state
        assert not state['book_jump'] and not state['book_tools'] and not state['selection'],state
        actual_width,actual_height,actual_pixels=read_pgm(runtime.frame)
        assert (actual_width,actual_height)==(width,height)
        assert actual_pixels[96*width:]==original_body,'Dismissing image confirmation changed the underlying page'

    def open_confirmation():
        x,y=point
        state=runtime.command(f'touch down {x} {y} {runtime.now+20}')
        assert state['book_view']=='reading' and page_position(state)==position,'Image PRESS opened an overlay before release'
        state=runtime.command(f'touch up {x} {y} {runtime.now+60}')
        assert state['book_view']=='image-confirm' and page_position(state)==position,state
        assert not state['book_tools'] and not state['selection'],state
        state=runtime.settle(3)
        assert state['book_view']=='image-confirm' and page_position(state)==position,'Opening release leaked through the confirmation'
        return state

    state=open_confirmation();capture('image-confirm-'+label,state)
    state=runtime.command('pager 1')
    assert state['book_view']=='image-confirm' and page_position(state)==position,'BLE paged behind image confirmation'
    x,y=cancel
    state=runtime.command(f'touch down {x} {y} {runtime.now+20}')
    assert state['book_view']=='image-confirm',state
    state=runtime.command(f'touch up {x} {y} {runtime.now+60}');reading(state)
    state=runtime.settle(3);reading(state)
    state=runtime.command(f'touch up {x} {y} {runtime.now+60}');reading(state)
    capture('image-confirm-cancel-'+label,state)

    open_confirmation()
    state=runtime.command('key 1 single');reading(state)
    capture('image-confirm-back-'+label,state)

    open_confirmation()
    x,y=confirm
    state=runtime.command(f'touch down {x} {y} {runtime.now+20}')
    assert state['book_view']=='image-confirm' and page_position(state)==position,'Preview opened on button PRESS'
    state=runtime.command(f'touch cancel {x} {y} {runtime.now+40}')
    assert state['book_view']=='image-confirm' and page_position(state)==position,state
    state=runtime.command(f'touch up {x} {y} {runtime.now+40}')
    assert state['book_view']=='image-confirm' and page_position(state)==position,'Cancelled button contact accepted the preview'
    state=runtime.tap(*confirm)
    assert state['book_view']=='image' and page_position(state)==position,state
    state=runtime.settle(3)
    assert state['book_view']=='image' and page_position(state)==position,'Confirming release closed the image preview'
    capture('image-preview-confirmed-'+label,state)
    state=runtime.tap(320 if landscape else 180,180 if landscape else 320);reading(state)
    state=runtime.settle(3);reading(state)
    capture('image-preview-closed-'+label,state)


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary',type=Path,default=ROOT/'build/native-preview-candidate/native-preview')
    parser.add_argument('--font',type=Path,default=ROOT/'sdcard/fonts/MiSansVF.ttf')
    parser.add_argument('--output',type=Path,default=ROOT/'build/native-preview-book-images')
    args=parser.parse_args();output=args.output.resolve();output.mkdir(parents=True,exist_ok=True)
    (output/'report.json').unlink(missing_ok=True)
    binary_sha256=hashlib.sha256(args.binary.resolve().read_bytes()).hexdigest()
    reports=[]
    for in_spine in (False,True):
        folder=output/('spine' if in_spine else 'standalone');folder.mkdir(exist_ok=True)
        session=Path(tempfile.mkdtemp(prefix='session-',dir=folder));books=session/'tf/books';books.mkdir(parents=True)
        make_fixture(books/'001-original.epub',in_spine)
        runtime=Runtime(args.binary.resolve(),args.font.resolve(),session,folder)
        def capture(name,state):
            assert state.get('book_image_bytes',0)<=1_296_000 and state.get('book_images',0)<=64,state
            runtime.capture(name,state)
        def swipe_left():
            runtime.command(f'touch down 250 320 {runtime.now+20}')
            runtime.command(f'touch move 100 320 {runtime.now+100}')
            return runtime.command(f'touch up 100 320 {runtime.now+60}')
        def image_swipe(right=False):
            # In this original fixture page one has a wide image; page two has a portrait illustration.
            # Both gestures begin inside the actual image, using the production gesture recognizer.
            x,y,finish=(180,210,280) if right else (180,335,80)
            runtime.command(f'touch down {x} {y} {runtime.now+20}')
            runtime.command(f'touch move {finish} {y} {runtime.now+100}')
            return runtime.command(f'touch up {finish} {y} {runtime.now+60}')
        try:
            runtime.start();runtime.command('route 0');runtime.settle(12)
            # Pair the explicit host-only BLE double before opening the book. / 使用明确标记模拟的BLE后端。
            runtime.command('route 24');runtime.tap(100,100);state=runtime.settle(12)
            assert state['ble_devices']==1,state
            runtime.tap(90,396);state=runtime.settle(8);assert state['ble_connected'],state
            runtime.command('route 0');runtime.settle(12);runtime.tap(70,350);state=runtime.settle(8)
            assert state['active_app']=='book' and state['book_kind']=='epub',state
            if in_spine:
                assert state['book_view']=='reading' and state['book_images']==1,state
                capture('spine-cover-auto',state)
                image_confirm_case(runtime,capture,(180,200),label='spine-portrait')
                state=runtime.command('rotate 1');state=runtime.settle(5)
                assert state['width']==1216 and state['book_view']=='reading',state
                image_confirm_case(runtime,capture,(320,180),True,label='spine-landscape')
                runtime.command('rotate 0');state=runtime.settle(5)
                before=page_position(state);state=runtime.command('key 2 single')
                assert page_position(state)!=before and state['book_view']=='reading',state
                capture('spine-cover-next-chapter',state)
            else:
                assert state['book_view']=='cover' and page_position(state)==(1,1),state
                capture('declared-cover-first-open',state)
                state=runtime.command('pager 1');assert state['book_view']=='reading' and page_position(state)==(1,1),state
                assert state['book_images']>=1,state
                capture('first-page-auto-landscape-image',state)
                image_confirm_case(runtime,capture,(180,335),label='inline-portrait')
                state=image_swipe();assert state['book_view']=='reading' and page_position(state)==(1,2),state
                capture('image-swipe-next-without-confirmation',state)
                state=image_swipe(True);assert state['book_view']=='reading' and page_position(state)==(1,1),state
                capture('image-swipe-previous-without-confirmation',state)
                state=runtime.command('pager -1');assert state['book_view']=='cover',state
                state=runtime.tap(180,320);assert state['book_view']=='reading' and page_position(state)==(1,1),state
                state=runtime.command('key 0 single');assert state['book_view']=='cover',state
                state=swipe_left();assert state['book_view']=='reading' and page_position(state)==(1,1),state
                state=runtime.command('key 0 single');assert state['book_view']=='cover',state
                # Global back closes the innermost cover, then normal reading exits once. / 返回逐层关闭封面和阅读页。
                state=runtime.command('key 1 single');assert state['active_app']=='book' and state['book_view']=='reading',state
                runtime.command('key 1 long');runtime.command('route 0');runtime.settle(10)
                runtime.tap(70,350);state=runtime.settle(8)
                assert state['book_view']=='reading' and page_position(state)==(1,1),state
                capture('resume-skips-cover',state)
                # Every displayed image page must have decoded pixels before its frame appears. / 每个图页首次显示前已解码。
                for turn in range(20):
                    if state['book_chapter']>1: break
                    state=runtime.command('key 2 single')
                    if state['book_chapter']==1:
                        assert state['book_images']>0,state
                        capture('auto-illustration-'+str(turn+2),state)
                assert state['book_chapter']==2,state
                capture('gallery-auto',state)
                for _ in range(8):state=runtime.command('key 2 single')
                assert state['book_images']>1 and state['book_image_bytes']>0,state
                capture('gallery-many-images',state)
                state=runtime.command('rotate 1');state=runtime.settle(5)
                assert state['width']==1216 and state['book_view']=='reading' and state['book_images']>0,state
                capture('gallery-landscape',state)
                runtime.command('key 1 long');runtime.command('route 0');runtime.settle(8)
                state=runtime.command('rotate 0');runtime.settle(3)
                runtime.tap(70,350);state=runtime.settle(8)
                assert state['book_view']=='reading' and state['book_chapter']>=2,state
                capture('gallery-resume',state)
                state=runtime.tap(22,20);assert state['book_view']=='toc',state
                state=runtime.tap(100,218);assert state['book_chapter']==3 and state['book_view']=='reading',state
                capture('failed-images-fallback-and-flat-image',state)
                state=runtime.command('key 2 single');assert state['book_chapter']==3 and state['book_images']>=1,state
                capture('thin-illustration-auto',state)
            reports.append({'cover_in_spine':in_spine,'scenes':runtime.scenes,'session':str(session),'commands':len(runtime.transcript)})
        finally:
            (session/'transcript.json').write_text(json.dumps(runtime.transcript,ensure_ascii=False,indent=2)+'\n')
            runtime.close()
    assert hashlib.sha256(args.binary.resolve().read_bytes()).hexdigest()==binary_sha256,'Native binary changed during image integration checks'
    report={'result':'PASS','binary_sha256':binary_sha256,
            'actual_app_book_source_layout_decode':True,'image_confirmation_touch_release':True,
            'image_swipes_preserve_page_turns':True,'fresh_native_process_only':True,'browser_access':False,'device_access':False,'cases':reports}
    (output/'report.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n')
    print(json.dumps({'result':'PASS','output':str(output),'scenes':sum(len(r['scenes']) for r in reports)},ensure_ascii=False))

if __name__=='__main__':main()

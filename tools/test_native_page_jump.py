#!/usr/bin/env python3
"""四向真实页码弹窗与像素回归，不连接设备。 / Four-orientation page dialog behavior and pixel checks, without hardware."""
from dataclasses import dataclass
from pathlib import Path
import argparse
import hashlib
import json
import tempfile

from test_native_preview_shelf_ble import Runtime, read_pgm, page_position

ROOT = Path(__file__).resolve().parents[1]


@dataclass(frozen=True)
class Rect:
    x: int
    y: int
    width: int
    height: int

    def center(self, state):
        return (round((self.x + self.width / 2) * state['logical_width'] / state['width']),
                round((self.y + self.height / 2) * state['logical_height'] / state['height']))


def crop(frame, rect):
    width, height, pixels = frame
    assert 0 <= rect.x < rect.x + rect.width <= width
    assert 0 <= rect.y < rect.y + rect.height <= height
    return b''.join(pixels[(rect.y+y)*width+rect.x:(rect.y+y)*width+rect.x+rect.width]
                    for y in range(rect.height))


class Dialog:
    def __init__(self, runtime, state):
        self.r, self.state = runtime, state
        width, height = state['width'], state['height']
        landscape = width > height
        pw, ph = (1000, 500) if landscape else (width-64, 680)
        self.panel = Rect((width-pw)//2, (height-84-ph)//2, pw, ph)
        left = self.panel.x + (pw//2+12 if landscape else 24)
        top = self.panel.y + (108 if landscape else 240)
        kw, kh = (pw//2-36, 64) if landscape else (pw-48, 76)
        cell = (kw-20)//3
        self.keys = [Rect(left+(i%3)*(cell+10), top+(i//3)*(kh+10), cell, kh) for i in range(12)]
        self.input = Rect(self.panel.x+24, self.panel.y+(140 if landscape else 124),
                          pw//2-48 if landscape else pw-48, 64)
        bw = (pw-72)//3
        self.cancel = Rect(self.panel.x+24, self.panel.y+ph-76, bw, 56)
        self.confirm = Rect(self.panel.x+24+bw+12, self.panel.y+ph-76, bw*2+12, 56)

    def frame(self):
        return read_pgm(self.r.frame)

    def tap(self, rect):
        self.state = self.r.tap(*rect.center(self.state))
        return self.state

    def digit(self, value):
        return self.tap(self.keys[10 if value == 0 else value-1])

    def text(self, value):
        for digit in str(value):
            self.digit(int(digit))
        return self.state

    def clear(self):
        return self.tap(self.keys[11])

    def open(self):
        self.state = self.r.tap(110, 20)
        assert self.state['book_jump'] and self.state['active_app'] == 'book', self.state
        return self.state

    def settle(self, predicate, label, limit=1000):
        for _ in range(limit):
            self.state = self.r.tick(100)
            if predicate(self.state):
                return self.state
        raise AssertionError((label, self.state))

    def stationary_pixels(self, expected):
        current = self.frame()
        for rect in self.keys + [self.cancel, self.confirm]:
            assert crop(current, rect) == crop(expected, rect), (rect, 'released controls left stale pixels')

    def stable_redraw(self):
        # 同一状态重新绘制不能出现重复控件或残留按压轮廓。/ A full redraw of the same state must not reveal duplicate controls or stale pressed outlines.
        before = self.frame()
        x, y = self.input.center(self.state)
        self.state = self.r.command(f'touch cancel {x} {y} {self.r.now+20}')
        assert crop(self.frame(),self.panel) == crop(before,self.panel), 'partial dialog frame differs from a clean redraw'


def run_case(args, output, label, rotation):
    session = Path(tempfile.mkdtemp(prefix=label+'-', dir=output))
    books = session/'tf/books'
    books.mkdir(parents=True)
    text = ''.join(f'第{i}章 自动测试\n' +
                   ('清晨的窗前放着一本书。这是自动生成的图书，用于验证页码输入、翻页和画面刷新，不包含私人内容。\n'*240)
                   for i in range(1, 9))
    source = books/'001-page-jump.txt'
    source.write_text(text)
    digest = hashlib.sha256(source.read_bytes()).hexdigest()
    r = Runtime(args.binary.resolve(), args.font.resolve(), session, output)
    checks = []
    try:
        r.start();r.command('route 0');r.settle(15)
        r.tap(60,330);state = r.settle(20)
        assert state['active_app']=='book' and state['book_page']==1 and not state['dual_role'],state
        r.command('rotation-lock 0')
        state = r.command(f'rotation {rotation}');state = r.settle(5)
        assert state['rotation']==rotation,state
        assert state['width']==(1216 if rotation in (0,2) else 684),state
        d = Dialog(r,state);d.open();empty = d.frame()
        r.capture(label+'-empty',d.state)
        primary = crop(empty,d.confirm);secondary = crop(empty,d.cancel)
        assert sum(v<=51 for v in primary)/len(primary)>.70, 'jump needs a clearly emphasized dark fill'
        assert sum(v>=204 for v in secondary)/len(secondary)>.75, 'cancel competes with the primary action'
        assert sum(v>=204 for v in primary)>80, 'primary action needs legible light text'
        checks.append('two footer actions with high-contrast primary')
        # 空输入退格是无操作，按下黑底、抬手复原应逐像素一致。/ Empty backspace is a no-op: black on press, pixel-identical after release.
        x,y=d.keys[9].center(d.state)
        d.state=r.command(f'touch down {x} {y} {r.now+20}')
        pressed=crop(d.frame(),d.keys[9])
        assert sum(v<=51 for v in pressed)/len(pressed)>.70,'pressed key lacks a clear fill'
        r.capture(label+'-pressed',d.state)
        d.state=r.command(f'touch up {x} {y} {r.now+60}')
        assert crop(d.frame(),d.panel)==crop(empty,d.panel),'no-op release left an extra border or duplicate key'
        d.stable_redraw()
        # 数字反馈只变输入区，键盘和底部按钮抬手后保持稳定。/ Digits change entry text; released keypad/footer controls remain stable.
        for digit in (1,2,3,4,5,6,7,8,9,0):
            d.digit(digit);d.stationary_pixels(empty);d.stable_redraw()
        full_input=crop(d.frame(),d.input)
        d.digit(1)
        assert crop(d.frame(),d.input)==full_input,'eleventh digit must not exceed the bounded input'
        d.clear();d.text('12');twelve=crop(d.frame(),d.input)
        d.digit(3);d.tap(d.keys[9])
        assert crop(d.frame(),d.input)==twelve,'backspace did not restore the previous input'
        d.clear();assert crop(d.frame(),d.panel)==crop(empty,d.panel),'clear did not restore the clean dialog'
        # 从键上拖出不能输入，也不能在抬手后留按压框。/ Dragging off a key must neither enter a digit nor leave a pressed frame.
        x,y=d.keys[0].center(d.state)
        d.state=r.command(f'touch down {x} {y} {r.now+20}')
        outside=(8,100)
        r.command(f'touch move {outside[0]} {outside[1]} {r.now+80}')
        d.state=r.command(f'touch up {outside[0]} {outside[1]} {r.now+60}')
        assert d.state['book_jump'] and crop(d.frame(),d.panel)==crop(empty,d.panel)
        checks.append('digits, bounded length, backspace, clear, canceled press and exact redraw')
        # 确认空值/零/整数溢出均保留原阅读位置且可继续编辑。/ Empty, zero and overflowing values preserve the original position and remain editable.
        original=page_position(d.state)
        for value in ('','0','4294967296'):
            d.clear();d.text(value);d.tap(d.confirm);d.state=r.settle(3)
            assert d.state['book_jump'] and page_position(d.state)==original,(value,d.state)
            d.stationary_pixels(empty);d.stable_redraw()
        r.capture(label+'-invalid',d.state)
        checks.append('empty, zero and uint32 overflow remain in the dialog')
        # 定位跨章范围时重复确认不能跳入目录、重复提交或留按压像素。/ Repeated confirmation during indexing must not open contents, resubmit or leave a pressed state.
        d.clear();d.text('99999');d.tap(d.confirm)
        assert d.state['book_jump'],d.state
        waiting=crop(d.frame(),d.confirm)
        r.capture(label+'-waiting',d.state)
        x,y=d.confirm.center(d.state)
        for _ in range(3):
            d.state=r.command(f'touch down {x} {y} {r.now+20}')
            assert crop(d.frame(),d.confirm)==waiting,'waiting confirmation must not paint a pressed state'
            d.state=r.command(f'touch up {x} {y} {r.now+60}')
            assert d.state['book_jump'] and d.state['book_view']=='page-jump',d.state
            assert page_position(d.state)==original
        # 定位完成后主操作恢复为跳转；按真实状态等待，不假定每种方向相同的分页速度。/ Await the restored action after indexing, rather than assuming equal pagination speed in every orientation.
        d.settle(lambda s:crop(d.frame(),d.confirm)==crop(empty,d.confirm),'out-of-book validation finished',1500)
        assert d.state['book_jump'] and page_position(d.state)==original,d.state
        d.stationary_pixels(empty);d.stable_redraw()
        checks.append('out-of-book target and repeated confirmation during indexing')
        d.clear();d.text('3')
        # 原来目录按钮的区域现在属于唯一跳转动作。/ The former contents-button area now belongs to the single jump action.
        old_middle=Rect(d.confirm.x,d.confirm.y,(d.confirm.width-12)//2,d.confirm.height)
        d.tap(old_middle);d.settle(lambda s:not s['book_jump'],'valid page three')
        assert d.state['book_chapter']==1 and d.state['book_page']==3,d.state
        assert d.state['book_view']=='reading' and not d.state['book_tools'],d.state
        r.capture(label+'-page-three',d.state)
        checks.append('valid target, former contents hit area now jumps')
        target=page_position(d.state)
        for dismiss in ('cancel','outside'):
            d.open();d.text('4')
            if dismiss=='cancel':d.tap(d.cancel)
            else:d.state=r.tap(8,100)
            d.state=r.settle(3)
            assert not d.state['book_jump'] and page_position(d.state)==target,d.state
            assert d.state['book_view']=='reading' and not d.state['selection'],d.state
        checks.append('cancel and outside dismissal never leak into reading actions')
        assert hashlib.sha256(source.read_bytes()).hexdigest()==digest
        return {'orientation':label,'rotation':rotation,'passed':True,'checks':checks,'commands':len(r.transcript),'session':str(session)}
    finally:
        r.close()
        (session/'transcript.json').write_text(json.dumps(r.transcript,ensure_ascii=False,indent=2)+'\n')


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--binary',type=Path,required=True)
    p.add_argument('--font',type=Path,default=ROOT/'sdcard/fonts/MiSansVF.ttf')
    p.add_argument('--output',type=Path,required=True)
    args=p.parse_args();output=args.output.resolve();output.mkdir(parents=True,exist_ok=True)
    report=output/'report.json';report.unlink(missing_ok=True)
    cases=[run_case(args,output,label,rotation) for label,rotation in
           [('portrait',3),('portrait-reverse',1),('landscape',2),('landscape-reverse',0)]]
    report.write_text(json.dumps({'passed':True,'binary_sha256':hashlib.sha256(args.binary.read_bytes()).hexdigest(),
        'physical_panel_tested':False,'hardware_connected':False,'cases':cases},ensure_ascii=False,indent=2)+'\n')
    print('PASS: four actual orientations, modal behavior, primary contrast and clean press/release redraws')


if __name__=='__main__':
    main()

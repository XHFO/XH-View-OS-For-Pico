#!/usr/bin/env python3
"""Two production C readers and independent TFs; real Wi-Fi sockets, simulated BLE transport."""
from pathlib import Path
import argparse, json, subprocess, tempfile, time, select, hashlib, shutil, struct, zlib
from test_native_preview_book import png_from_pgm
ROOT=Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser();p.add_argument('--binary',type=Path,default=ROOT/'build/dual-native-v2/native-preview');p.add_argument('--primary-binary',type=Path);p.add_argument('--output',type=Path);args=p.parse_args()
out=args.output or ROOT/'build/dual-native-tests';out.mkdir(parents=True,exist_ok=True)
run=Path(tempfile.mkdtemp(prefix='pair-',dir=out));processes=[];history=[]
class Device:
    def __init__(self,name,font):
        self.name=name;self.root=run/name;self.books=self.root/'tf/books';self.books.mkdir(parents=True);self.now=10000
        (self.books/'same.txt').write_text(''.join(f'第{i}章 Chapter {i}\n'+(('Primary text. Continuous pagination and shared fonts.\n' if name=='primary' else 'Secondary text. A different book with the same name.\n')*120) for i in range(1,5)))
        self.log=(self.root/'runtime.log').open('w');binary=args.primary_binary if name=='primary' and args.primary_binary else args.binary;self.p=subprocess.Popen([str(binary.resolve()),str(font),str(self.root),str(self.root/'frame.pgm')],stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=self.log,text=True);processes.append(self)
        self.state={};self.cmd('status');self.cmd('route 34')
    def cmd(self,line):
        self.p.stdin.write(line+'\n');self.p.stdin.flush()
        if not select.select([self.p.stdout],[],[],30)[0]:raise RuntimeError(f'{self.name} timed out: {line}')
        result=self.p.stdout.readline()
        if not result:raise RuntimeError(f'{self.name} exited: {self.log.name}')
        self.state=json.loads(result);assert self.state['ok'],(line,self.state)
        self.now=self.state['now_ms'];history.append({'device':self.name,'command':line,'state':self.state});return self.state
    def tick(self):self.now+=20;return self.cmd(f'tick {self.now}')
    def tap(self,x,y):
        self.cmd(f'touch down {x} {y} {self.now+20}')
        return self.cmd(f'touch up {x} {y} {self.now+60}')
    def capture(self,name):png_from_pgm(self.root/'frame.pgm',run/f'{name}-{self.name}.png')
try:
    a=Device('primary',ROOT/'main/assets/serif.ttf');b=Device('secondary',ROOT/'main/assets/kai.ttf')
    # 双机投影不得覆盖日常方向和锁定偏好。/ Paired projection must preserve ordinary direction and lock preferences.
    def global_preferences(d):
        raw=next(d.root.glob('tf/**/system_v1.dat')).read_bytes()
        magic,size,crc=struct.unpack_from('<III',raw)
        assert magic==0x31535058 and size==len(raw)-12 and zlib.crc32(raw[12:])==crc
        assert struct.unpack_from('<I',raw,12)[0]==1
        return tuple(raw[18:20])
    a.cmd('rotate 1');a.cmd('rotation-lock 1')
    b.cmd('rotate 0');b.cmd('rotation-lock 1')
    preferences=(global_preferences(a),global_preferences(b))
    assert preferences==((1,1),(1,0)),preferences
    a.cmd('dual 1 127.0.0.1 123456')
    # The deployment command must be idempotent and preserve the existing pairing secret.
    config=a.books/'.pico-system/dualconfig1.dat';saved=config.read_bytes()
    a.cmd('dual-primary');assert a.state['dual_role']==1 and config.read_bytes()==saved
    b.cmd('dual 2 127.0.0.1 123456')
    def pump(count=1):
        for _ in range(count):a.tick();b.tick();time.sleep(.001)
    def until(predicate,label,limit=5000):
        for i in range(limit):
            pump()
            if predicate():return
            if i%500==0:print(label,i,a.state.get('dual_books'),b.state.get('dual_books'),a.state.get('dual_scanning'),b.state.get('dual_scanning'),flush=True)
        for d in (a,b):d.cmd('dual-usb RPICO DUAL STATUS')
        raise AssertionError((label,a.state,b.state))
    pump(3)
    assert not b.state['dual_synced']
    b.cmd('route 0')
    b.cmd('dual-open /sdcard/books/same.txt')
    b.capture('queued-open')
    until(lambda:a.state['dual_synced'] and b.state['dual_synced'],'initial library and font sync')
    until(lambda:a.state.get('active_app')==b.state.get('active_app')=='book' and a.state.get('dual_page')==1 and b.state.get('dual_page')==2 and a.state.get('dual_target')==b.state.get('dual_target')==0,'queued open completes after font sync')
    assert a.state['page_end']==b.state['page_start']
    pump(800)
    for d in (a,b):assert any(d.books.glob('.pico-system/b_*.dat')),'TF progress missing: '+d.name
    a.capture('paired');b.capture('paired')
    assert (a.books/'same.txt').read_bytes()!=(b.books/'same.txt').read_bytes()
    def contents(d):return sorted(hashlib.sha256(f.read_bytes()).hexdigest() for f in d.books.iterdir() if f.suffix in ('.txt','.epub'))
    assert contents(a)==contents(b) and len(contents(a))==2
    # macOS indexing may update ctime after a received file is published. Refresh
    # that legitimately invalidated cache before asserting a truly unchanged scan.
    def metadata(d):
        return {str(p): (s.st_ino,s.st_size,s.st_mtime_ns,s.st_ctime_ns)
                for root in (d.books,d.root/'tf/fonts') for p in root.iterdir()
                if p.suffix.lower() in ('.txt','.epub','.ttf') for s in (p.stat(),)}
    for d in (a,b):
        previous=metadata(d);stable=0
        for _ in range(50):
            pump();time.sleep(.1);current=metadata(d)
            stable=stable+1 if current==previous else 0;previous=current
            if stable>=10:break
        assert stable>=10,'fixture metadata did not settle'
        d.cmd('dual-usb RPICO DUAL RESCAN')
        until(lambda:a.state['dual_synced'] and b.state['dual_synced'],'refresh cache after fixture metadata settles')
        before=metadata(d)
        d.cmd('dual-usb RPICO DUAL RESCAN')
        until(lambda:a.state['dual_synced'] and b.state['dual_synced'],'unchanged manifest reuses verified entries')
        assert metadata(d)==before,'external metadata changed during unchanged-file test'
        assert d.state['dual_verified_bytes']==0 and d.state['dual_reused']==d.state['dual_books']
        assert d.state['dual_fast_verified'] and d.state['dual_peer_id']
        # Device USB diagnostics must remain complete JSON inside the production 1024-byte reply.
        for line in Path(d.log.name).read_text().splitlines():
            if line.startswith('DUALUSB '):json.loads(line[8:])
    # A peer in ordinary settings must still follow an explicit book open; edit screens stay protected.
    b.cmd('route 0');pump(10);assert b.state['dual_secondary_shelf'] and b.state['dual_synced']
    b.capture('dual-secondary-progress-shelf')
    for style in range(2):
        b.cmd(f'status-style {style}');pump(3);b.capture(f'status-connected-secondary-{style}')
    b.cmd('status-style 0')
    a.cmd('route 5');pump(3)
    for style in range(2):
        a.cmd(f'status-style {style}');pump(3);a.capture(f'status-connected-primary-{style}')
    a.cmd('status-style 0')
    b.cmd('dual-open /sdcard/books/same.txt')
    until(lambda:a.state.get('active_app')==b.state.get('active_app')=='book' and a.state.get('book_page')==1 and b.state.get('book_page')==2,'open from secondary')
    until(lambda:a.state.get('dual_target')==b.state.get('dual_target')==0,'first spread settled');pump(20);a.capture('spread-1');b.capture('spread-1')
    assert a.state['page_end']==b.state['page_start'],(a.state,b.state)
    sounds=(a.state['sound_played'],b.state['sound_played'])
    offset=len(history)
    b.cmd('powerkey')
    until(lambda:a.state.get('book_page')==3 and b.state.get('book_page')==4 and a.state.get('book_painted_page')==3 and b.state.get('book_painted_page')==4,'secondary short power next')
    secondary_lead=next(i for i,h in enumerate(history[offset:],offset) if h['device']=='secondary' and h['state'].get('book_painted_page')==4)
    primary_follow=next(i for i,h in enumerate(history[offset:],offset) if h['device']=='primary' and h['state'].get('book_painted_page')==3)
    assert secondary_lead<primary_follow,(secondary_lead,primary_follow,a.state,b.state)
    # 接力需当前修订的提前通知或真实完成；真正完成仍独立约束下一次翻页。/ Relay requires a current early signal or completion; actual completion still gates another turn.
    assert history[primary_follow]['state']['dual_relay_ready'] or history[primary_follow]['state']['dual_peer_presented'],(secondary_lead,primary_follow,a.state,b.state)
    assert a.state['book_turn_dir']==b.state['book_turn_dir']==1
    assert (a.state['sound_played'],b.state['sound_played'])==(sounds[0],sounds[1]+1)
    assert b.state['sound_last']==1
    assert a.state['power_state']==b.state['power_state']==0
    until(lambda:a.state['dual_turn_ready'] and b.state['dual_turn_ready'],'next completed before previous input')
    offset=len(history)
    a.cmd('powerkey')
    until(lambda:a.state.get('book_page')==1 and b.state.get('book_page')==2 and a.state.get('book_painted_page')==1 and b.state.get('book_painted_page')==2,'primary short power previous')
    primary_lead=next(i for i,h in enumerate(history[offset:],offset) if h['device']=='primary' and h['state'].get('book_painted_page')==1)
    secondary_follow=next(i for i,h in enumerate(history[offset:],offset) if h['device']=='secondary' and h['state'].get('book_painted_page')==2)
    assert primary_lead<secondary_follow,(primary_lead,secondary_follow,a.state,b.state)
    assert a.state['book_turn_dir']==b.state['book_turn_dir']==-1
    assert (a.state['sound_played'],b.state['sound_played'])==(sounds[0]+1,sounds[1]+1)
    # 静音不阻挡翻页；接收端始终不因同步重复发声。/ Muting never blocks turns; synchronized peers do not echo cues.
    for flags in (13,14):
        b.cmd(f'sound-options {flags}');pump(20);sounds=(a.state['sound_played'],b.state['sound_played'])
        until(lambda:a.state['dual_turn_ready'] and b.state['dual_turn_ready'],'completed before muted next')
        b.cmd('powerkey');until(lambda:a.state.get('book_page')==3 and b.state.get('book_page')==4,'muted secondary next')
        assert (a.state['sound_played'],b.state['sound_played'])==sounds
        until(lambda:a.state['dual_turn_ready'] and b.state['dual_turn_ready'],'completed before restore')
        a.cmd('powerkey');until(lambda:a.state.get('book_page')==1 and b.state.get('book_page')==2,'restore first spread')
        assert (a.state['sound_played'],b.state['sound_played'])==(sounds[0]+1,sounds[1])
        pump(8)
    b.cmd('sound-options 15')

    # 主机基准同步两个屏幕；角色偏移独立，既有设置不变。/ Primary base is shared; role offsets are independent and ordinary settings stay unchanged.
    def pose(d, x, y=0, z=0):
        d.cmd(f'pose {x} {y} {z}')
        for _ in range(3):d.cmd(f'tick {d.now+600}')
        pump(5)
        until(lambda:b.state['dual_base_rotation']==a.state['dual_base_rotation'],'pose state broadcast')
    def page_identity(d):return (d.state['dual_page'],d.state['book_chapter'],d.state['page_start'],d.state['page_end'])
    def tap(d,x,y):
        d.cmd(f'touch down {round(x)} {round(y)} {d.now+80}')
        d.cmd(f'touch up {round(x)} {round(y)} {d.now+60}')
    def orientation_is(base,primary,secondary):
        return all((d.state['dual_base_rotation'],d.state['dual_primary_flip'],d.state['dual_secondary_flip'])==(base,primary,secondary) for d in (a,b))
    def orientation_preferences_preserved():
        assert (global_preferences(a),global_preferences(b))==preferences
        assert a.state['rotation_locked'] and b.state['rotation_locked']
    def rotation_switch(d,role,enabled):
        before=(page_identity(a),page_identity(b));base=a.state['dual_base_rotation']
        expected=[a.state['dual_primary_flip'],a.state['dual_secondary_flip']];expected[role-1]=enabled
        d.cmd(f'dual-flip {role} {int(enabled)}')
        until(lambda:orientation_is(base,*expected),'menu role rotation synchronizes')
        after=(page_identity(a),page_identity(b));assert after==before,(before,after,a.state,b.state)
        orientation_preferences_preserved()
    until(lambda:orientation_is(3,False,False),'default shared orientation')
    assert (a.state['rotation'],b.state['rotation'])==(3,3)
    # 贴合防误触必须两端同步：主机/从机任一端切换，另一端都跟随。
    # The face-to-face guard must be synchronized: toggling either role updates both devices.
    a.cmd('dual-usb RPICO DUAL GUARD OFF');pump(20)
    assert not a.state['dual_guard'] and not b.state['dual_guard']
    b.cmd('dual-usb RPICO DUAL GUARD ON');pump(20)
    assert a.state['dual_guard'] and b.state['dual_guard']
    identities=(page_identity(a),page_identity(b));normal_hash=a.state['physical_hash']
    pose(a,-980)
    assert orientation_is(1,False,False) and (a.state['rotation'],b.state['rotation'])==(1,1)
    assert a.state['physical_hash']!=normal_hash
    assert (page_identity(a),page_identity(b))==identities
    inverted_hash=a.state['physical_hash'];pose(a,0,-980);pose(a,0,980);pose(a,0,0,980)
    assert a.state['rotation']==1 and a.state['physical_hash']==inverted_hash
    assert a.state['width']==b.state['width']==684 and a.state['height']==b.state['height']==1216
    pose(b,980);pose(b,-980)
    assert orientation_is(1,False,False) and (a.state['rotation'],b.state['rotation'])==(1,1)
    # 双机会话锁定停止自动换向；系统锁定偏好不被改写，手动翻转仍可用。
    # Paired-session lock stops auto turns without rewriting the system lock preference; manual flips stay available.
    a.cmd('dual-usb RPICO DUAL LOCK ON');assert a.state['dual_locked']
    b.cmd('dual-usb RPICO DUAL LOCK ON');assert b.state['dual_locked']
    pose(a,980)
    assert orientation_is(1,False,False) and (a.state['rotation'],b.state['rotation'])==(1,1)
    a.cmd('dual-usb RPICO DUAL LOCK OFF')
    b.cmd('dual-usb RPICO DUAL LOCK OFF')
    pose(a,980)
    assert orientation_is(3,False,False) and (a.state['rotation'],b.state['rotation'])==(3,3)
    pose(a,-980)
    assert orientation_is(1,False,False) and (a.state['rotation'],b.state['rotation'])==(1,1)
    # 从机锁定只冻结自己的显示，不阻止主机继续自动换向；解锁后重新跟随。
    # Locking the secondary freezes only its display; the primary keeps auto-turning, and unlock follows again.
    b.cmd('dual-usb RPICO DUAL LOCK ON')
    a.cmd('dual-usb RPICO DUAL LOCK OFF')
    pose(a,980)
    assert a.state['rotation']==3 and b.state['rotation']==1
    b.cmd('dual-usb RPICO DUAL LOCK OFF')
    pump(30)
    assert b.state['rotation']==3
    pose(a,-980)
    assert orientation_is(1,False,False) and (a.state['rotation'],b.state['rotation'])==(1,1)
    rotation_switch(a,1,True);assert (a.state['rotation'],b.state['rotation'])==(3,1)
    # 主机翻转后传感器候选方向也必须补偿：旋转设备仍能换到相反显示方向。
    # A flipped primary compensates the sensor candidate, so turning the device still flips the display.
    pose(a,980)
    assert (a.state['rotation'],b.state['rotation'])==(1,3)
    pose(a,-980)
    assert (a.state['rotation'],b.state['rotation'])==(3,1)
    rotation_switch(b,2,True);assert (a.state['rotation'],b.state['rotation'])==(3,3)
    rotation_switch(b,1,False);assert (a.state['rotation'],b.state['rotation'])==(1,3)
    rotation_switch(a,2,False);assert (a.state['rotation'],b.state['rotation'])==(1,1)
    # 快捷旋转只切换本机角色偏移。/ The rotation shortcut toggles only its local role offset.
    a.cmd('flip');until(lambda:orientation_is(1,True,False),'primary shortcut offset')
    b.cmd('flip');until(lambda:orientation_is(1,True,True),'secondary shortcut offset')
    a.cmd('flip');until(lambda:orientation_is(1,False,True),'primary shortcut restore')
    b.cmd('flip');until(lambda:orientation_is(1,False,False),'secondary shortcut restore')
    assert (page_identity(a),page_identity(b))==identities
    orientation_preferences_preserved()
    # 阅读弹窗关闭后才应用新方向；反向屏幕触摸映射仍然有效。/ Defer a new orientation until the reading dialog closes, including inverted touch mapping.
    def jump_open(d):
        tap(d,180,22);assert d.state['book_jump'],d.state
    def digit(d,number):
        key=10 if number==0 else number-1
        tap(d,(56+(key%3)*194+92)*360/684,(466+(key//3)*86+38)*640/1216)
    def confirm(d):tap(d,536*360/684,858*640/1216)
    # 从机编辑中的弹窗也不能被主机的方向更新打断。/ A primary orientation update must not interrupt a secondary's active dialog.
    jump_open(b);digit(b,5);pose(a,980)
    assert orientation_is(3,False,False) and a.state['rotation']==3
    assert b.state['book_jump'] and b.state['rotation']==1,b.state
    tap(b,8,100);pump(10);assert not b.state['book_jump'] and b.state['rotation']==3
    pose(a,-980);assert orientation_is(1,False,False)
    jump_open(a);pose(a,980);assert a.state['rotation']==1 and orientation_is(1,False,False)
    tap(a,8,100);assert not a.state['book_jump'];pose(a,980)
    assert orientation_is(3,False,False) and (a.state['rotation'],b.state['rotation'])==(3,3)
    rotation_switch(a,2,True);assert (a.state['rotation'],b.state['rotation'])==(3,1)
    pose(a,0,0,980);pose(b,0,0,980)
    # 真正关闭双机恢复日常偏好；重新启用保留角色偏移和正文位置。/ Disabling restores ordinary preferences; re-enabling retains role offsets and the reading position.
    before=(page_identity(a)[:4],page_identity(b)[:4])
    for d,rotation in ((a,2),(b,3)):
        d.cmd('route 34');pump(4);tap(d,70,124)
        assert d.state['dual_role']==0 and d.state['rotation']==rotation,d.state
    orientation_preferences_preserved()
    a.cmd('dual 1 127.0.0.1 123456');b.cmd('dual 2 127.0.0.1 123456')
    a.cmd('route 0');b.cmd('route 0')
    until(lambda:a.state['dual_synced'] and b.state['dual_synced'] and a.state.get('active_app')==b.state.get('active_app')=='book' and a.state.get('dual_target')==b.state.get('dual_target')==0,'re-enable retains reading and role offsets')
    assert orientation_is(3,False,True) and (a.state['rotation'],b.state['rotation'])==(3,1)
    assert (page_identity(a)[:4],page_identity(b)[:4])==before
    orientation_preferences_preserved()
    # Secondary-origin turns wait while the primary has a page dialog open.
    jump_open(a);before=a.state['sound_played'];a.cmd('powerkey');assert a.state['sound_played']==before
    b.cmd('powerkey');pump(40)
    assert (a.state['dual_page'],b.state['dual_page'])==(1,2),(a.state,b.state)
    offset=len(history);tap(a,8,100)
    pump(20)
    if (a.state.get('dual_page'),b.state.get('dual_page'))!=(3,4):b.cmd('powerkey')
    until(lambda:a.state.get('dual_page')==3 and b.state.get('dual_page')==4 and a.state.get('book_painted_page')==3 and b.state.get('book_painted_page')==4,'direction lead before follow')
    transitions=[(i,h['device'],h['state'].get('book_painted_page')) for i,h in enumerate(history[offset:])]
    assert next(i for i,d,page in transitions if d=='secondary' and page==4)<next(i for i,d,page in transitions if d=='primary' and page==3)
    until(lambda:a.state['dual_turn_ready'] and b.state['dual_turn_ready'],'both completions acknowledged before failure case')
    # Simulated panel body failure without animation: header-only updates cannot release the right page.
    a.cmd('dual-anim 0');a.cmd('display-body-failure 1');b.cmd('powerkey');pump(15)
    assert a.state['dual_page']==5 and not a.state['dual_left_ready'] and b.state['dual_page']==4,(a.state,b.state)
    a.cmd(f'touch down 140 22 {a.now+80}')
    a.cmd(f'touch move 230 22 {a.now+80}')
    pump(5)
    assert not a.state['dual_left_ready'] and b.state['dual_page']==4
    a.cmd(f'touch cancel 230 22 {a.now+80}');pump(5)
    assert not a.state['dual_left_ready'] and b.state['dual_page']==4
    a.cmd('display-body-failure 0')
    until(lambda:a.state['dual_left_ready'] and b.state['dual_page']==6,'body retry releases secondary only after success')
    a.cmd('dual-anim 1')
    # Both numeric dialogs validate then submit one shared spread, never a local-only jump.
    jump_open(b);digit(b,1);digit(b,0);confirm(b)
    until(lambda:a.state.get('dual_page')==9 and b.state.get('dual_page')==10 and not b.state['book_jump'],'secondary numeric even-page jump')
    assert a.state['page_end']==b.state['page_start']
    assert (a.state['rotation'],b.state['rotation'])==(3,1)
    jump_open(a);digit(a,5);confirm(a)
    until(lambda:a.state.get('dual_page')==5 and b.state.get('dual_page')==6 and not a.state['book_jump'],'primary numeric odd-page jump')
    # Zero and beyond-book inputs keep the original spread, cancellation remains usable.
    jump_open(a);digit(a,0);confirm(a);pump(20)
    assert a.state['book_jump'] and (a.state['dual_page'],b.state['dual_page'])==(5,6)
    tap(a,536*360/684,762*640/1216)
    for number in (9,9,9,9,9):digit(a,number)
    confirm(a);pump(180)
    assert a.state['book_jump'] and (a.state['dual_page'],b.state['dual_page'])==(5,6)
    tap(a,8,100);assert not a.state['book_jump']
    jump_open(a);digit(a,1);confirm(a)
    until(lambda:a.state.get('dual_page')==1 and b.state.get('dual_page')==2,'numeric return to first spread')
    a.capture('rotated-primary-normal');b.capture('rotated-secondary-inverted')
    # Real toolbar touch changes the shared size and anchors the original text.
    def tools_open(d):
        d.cmd(f'touch down 180 22 {d.now+80}')
        d.cmd(f'tick {d.now+700}')
        d.cmd(f'touch up 180 22 {d.now+30}')
        assert d.state['book_tools'],d.state
    def settled():
        return (a.state.get('active_app')==b.state.get('active_app')=='book' and
            a.state.get('dual_target')==b.state.get('dual_target')==0 and
            not a.state['dual_reflow'] and not b.state['dual_reflow'] and
            a.state['dual_revision']==b.state['dual_revision'] and a.state['dual_left_ready'] and b.state['dual_left_ready'] and
            a.state['dual_turn_ready'] and b.state['dual_turn_ready'])
    jump_open(a);digit(a,9);confirm(a)
    until(lambda:settled() and a.state['dual_page']==9,'position before shared resize')
    anchor=(a.state['book_chapter'],a.state['page_start'])
    tools_open(b);tap(b,288,452)
    until(lambda:settled() and a.state['book_px']==b.state['book_px']==52,'secondary shared size increase')
    assert a.state['dual_font_px']==b.state['dual_font_px']==52
    assert a.state['dual_page']+1==b.state['dual_page']
    assert (a.state['book_chapter'],a.state['page_start'])<=anchor<(b.state['book_chapter'],b.state['page_end'])
    assert a.state['page_end']==b.state['page_start']
    a.capture('size52');b.capture('size52')
    tools_open(a);tap(a,180,452)
    until(lambda:settled() and a.state['book_px']==b.state['book_px']==48,'primary shared size decrease')
    # Ordinary settings and records Back retain book, spread and size.
    for d in (a,b):
        for route in (5,2):
            identity=(a.state['dual_spread'],a.state['dual_revision'])
            d.cmd(f'route {route}');pump(4);tap(d,23,47)
            until(settled,'settings or records back resumes shared book')
            assert a.state['dual_spread']==identity[0]
        page=a.state['dual_spread'];path=d.state['book_path']
        d.cmd('key 1 short');pump(4);assert d.state['active_app']!='book'
        d.cmd('dual-open '+path)
        until(settled,'same-book shelf resume')
        assert a.state['dual_spread']==page
    # Contents works on either device and submits the containing spread.
    tap(b,20,22);assert b.state['book_view']=='toc',b.state
    tap(b,100,199)
    until(lambda:settled() and b.state['book_chapter']>=3,'secondary contents chapter navigation')
    assert a.state['dual_page']+1==b.state['dual_page']
    # Scrub and return use the same shared anchor resolver.
    original=(a.state['book_chapter'],a.state['page_start'])
    a.cmd(f'touch down 140 22 {a.now+80}')
    a.cmd(f'touch move 245 22 {a.now+100}')
    a.cmd(f'touch up 245 22 {a.now+80}')
    revision=a.state['dual_revision']
    until(lambda:settled() and a.state['dual_revision']>revision,'shared progress scrub')
    tap(a,330,22)
    until(lambda:settled() and (a.state['book_chapter'],a.state['page_start'])==original,'scrub return to original spread')
    # Bookmarks retain stable anchors and return to the correct paired spread.
    bookmarked=(a.state['dual_page'],b.state['dual_page'])
    tools_open(b);tap(b,180,553)
    record_index=next(b.books.glob('.pico-system/reader-r3-index.dat'))
    record_bytes=record_index.read_bytes();assert struct.unpack_from('<I',record_bytes,16)[0]==1
    tap(b,8,100)
    jump_open(a);digit(a,1);confirm(a)
    until(lambda:settled() and a.state['dual_page']==1,'move away from saved bookmark')
    tools_open(b);tap(b,288,553);pump(5)
    assert b.state['route']==2,b.state
    tap(b,288,137);tap(b,100,210);pump(3)
    tap(b,95,574)
    tap(b,180,568);pump(3)
    b.cmd('route 2');tap(b,288,137);tap(b,100,210);tap(b,95,574)
    until(lambda:settled() and (a.state['dual_page'],b.state['dual_page'])==bookmarked,'bookmark returns to paired source')
    assert record_index.read_bytes()==record_bytes,'navigation must not change persisted bookmark'
    assert a.state['dual_page']+1==b.state['dual_page']
    jump_open(a);digit(a,1);confirm(a)
    until(lambda:settled() and a.state['dual_page']==1,'return to first spread after basic checks')
    # Exercise actual ps_load migration and durable reflow recovery using private TF files.
    def restart_primary():
        a.p=subprocess.Popen([str((args.primary_binary or args.binary).resolve()),str(ROOT/'main/assets/serif.ttf'),str(a.root),str(a.root/'frame.pgm')],stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=a.log,text=True)
        a.cmd('status')
    jump_open(a);digit(a,9);confirm(a)
    until(lambda:settled() and a.state['dual_page']==9,'migration checkpoint');pump(80)
    saved_path=a.state['book_path'];saved_spread=a.state['dual_spread']
    store=a.books/'.pico-system';durable=(store/'dualreading2.dat').read_bytes()
    a.cmd('quit');a.p.wait(timeout=5)
    legacy=durable[12:12+76]
    (store/'dualreading1.dat').write_bytes(struct.pack('<III',0x31535058,len(legacy),zlib.crc32(legacy))+legacy)
    for suffix in ('dat','bak'):(store/('dualreading2.'+suffix)).unlink(missing_ok=True)
    restart_primary()
    until(lambda:settled() and a.state.get('book_px')==b.state.get('book_px')==48,'legacy dual12 record migration')
    assert a.state['book_path']==saved_path and a.state['dual_spread']==saved_spread
    assert len((store/'dualreading2.dat').read_bytes())==92
    assert orientation_is(3,False,True) and (a.state['rotation'],b.state['rotation'])==(3,1)
    orientation_preferences_preserved()
    tools_open(a);tap(a,288,452)
    until(lambda:settled() and a.state['book_px']==b.state['book_px']==52,'durable size before interrupted reflow');pump(80)
    durable=(store/'dualreading2.dat').read_bytes();saved_spread=a.state['dual_spread']
    a.cmd('dual-usb RPICO DUAL FONT 56')
    assert a.state['dual_reflow'] and (store/'dualreading2.dat').read_bytes()==durable
    a.p.terminate();a.p.wait(timeout=5)
    restart_primary()
    until(lambda:settled() and a.state.get('book_px')==b.state.get('book_px')==52,'interrupted reflow restores durable size')
    assert a.state['dual_spread']==saved_spread and a.state['book_path']==saved_path
    tools_open(b);tap(b,180,452)
    until(lambda:settled() and a.state['book_px']==b.state['book_px']==48,'restore standard size')
    jump_open(a);digit(a,1);confirm(a)
    until(lambda:settled() and a.state['dual_page']==1,'first spread after persistence checks')
    # Wi-Fi file IO and simulated BLE control must fail independently.
    a.cmd('dual-wifi 0');b.cmd('dual-wifi 0');pump(40)
    assert not a.state['dual_wifi_connected'] and not b.state['dual_wifi_connected']
    assert a.state['dual_connected'] and b.state['dual_connected']
    b.cmd('powerkey')
    until(lambda:a.state.get('book_page')==3 and b.state.get('book_page')==4,'BLE next with Wi-Fi offline')
    until(lambda:a.state['dual_turn_ready'] and b.state['dual_turn_ready'],'BLE next display and ACK completed')
    a.cmd('powerkey')
    until(lambda:a.state.get('book_page')==1 and b.state.get('book_page')==2,'BLE previous with Wi-Fi offline')
    a.cmd('dual-radio 0');b.cmd('dual-radio 0');pump(30)
    assert not a.state['dual_connected'] and not b.state['dual_connected']
    saved=(a.state['dual_spread'],b.state['dual_spread'])
    b.cmd('powerkey');a.cmd('powerkey');pump(30)
    assert saved==(a.state['dual_spread'],b.state['dual_spread'])
    assert a.state['power_state']==b.state['power_state']==0
    a.cmd('dual-radio 1');b.cmd('dual-radio 1')
    until(lambda:a.state['dual_synced'] and b.state['dual_synced'] and a.state['dual_left_ready'] and b.state['dual_left_ready'] and a.state.get('dual_target')==b.state.get('dual_target')==0,'BLE reconnect while Wi-Fi offline')
    # Reconnect now adopts the furthest historical anchor from the earlier jump tests.
    # Settle that reconciliation, then verify relative book-wide turns, including chapter boundaries.
    pump(160)
    until(settled,'reconnect reconciliation settled')
    rejoined=(a.state['dual_page'],b.state['dual_page'])
    assert rejoined[0]+1==rejoined[1]
    b.cmd('powerkey')
    until(lambda:settled() and (a.state['dual_page'],b.state['dual_page'])==(rejoined[0]+2,rejoined[1]+2),'BLE next after reconnect')
    a.cmd('powerkey')
    until(lambda:settled() and (a.state['dual_page'],b.state['dual_page'])==rejoined,'BLE previous after reconnect')
    a.cmd('dual-wifi 1');b.cmd('dual-wifi 1')
    until(lambda:a.state['dual_wifi_connected'] and b.state['dual_wifi_connected'],'Wi-Fi reconnect')
    # Starting again on the primary selects its original content, not the same-named secondary book.
    old_revision=a.state['dual_revision'];a.cmd('route 0');pump(5);a.cmd('dual-open /sdcard/books/same.txt')
    until(lambda:a.state.get('active_app')==b.state.get('active_app')=='book' and a.state.get('book_page')==1 and b.state.get('book_page')==2 and a.state['dual_revision']>old_revision and a.state.get('dual_target')==b.state.get('dual_target')==0,'open from primary')
    # Repeated key events cross actual TXT chapter boundaries and never lock the reader.
    for i in range(24):
        pump(12);b.cmd('powerkey');pump(30)
        until(lambda:a.state.get('dual_target')==b.state.get('dual_target')==0 and a.state['dual_spread']==b.state['dual_spread'],'spread settled')
        assert a.state['dual_page']+1==b.state['dual_page'],(a.state,b.state)
        assert a.state['power_state']==b.state['power_state']==0
        if a.state['book_chapter']==b.state['book_chapter']:assert a.state['page_end']==b.state['page_start'],(a.state,b.state)
        else:assert a.state['book_chapter']+1==b.state['book_chapter'] and b.state['book_page']==1,(a.state,b.state)
    assert a.state['book_chapter']>1,'must cross a real chapter boundary'
    a.capture('later-spread');b.capture('later-spread')
    # A newly added one-page book exercises rescan, EOF pairing and automatic import.
    (a.books/'one-page.txt').write_text('One page. The secondary must show the end, not repeat this text.\n')
    a.cmd('route 0');pump(4);a.cmd('dual-open /sdcard/books/one-page.txt')
    until(lambda:a.state.get('book_path','').endswith('/one-page.txt') and b.state.get('book_path','').endswith('/one-page.txt') and a.state.get('dual_target')==b.state.get('dual_target')==0 and b.state.get('dual_blank'),'new book and odd final page')
    assert a.state['dual_page']==1 and not a.state['dual_blank']
    a.capture('odd-end');b.capture('odd-end')
    b.cmd('powerkey');pump(100)
    until(lambda:a.state.get('dual_target')==b.state.get('dual_target')==0 and a.state['dual_spread']==b.state['dual_spread']==0,'end clamp')
    assert b.state['dual_blank'] and a.state['dual_page']==1
    # Both links drop on process exit; restarting restores the primary's reading state.
    b.cmd('quit');b.p.wait(timeout=5)
    for _ in range(10):a.tick()
    assert not a.state['dual_connected']
    before=a.state['dual_spread'];sounds=a.state['sound_played'];a.cmd('powerkey');assert a.state['dual_spread']==before and a.state['power_state']==0 and a.state['sound_played']==sounds
    b.p=subprocess.Popen([str(args.binary.resolve()),str(ROOT/'main/assets/kai.ttf'),str(b.root),str(b.root/'frame.pgm')],stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=b.log,text=True)
    b.cmd('status')
    until(lambda:a.state['dual_synced'] and b.state['dual_synced'] and b.state.get('dual_blank') and b.state.get('dual_target')==0,'secondary restart resumes latest spread')
    assert len(contents(a))==len(contents(b))==3 and contents(a)==contents(b)
    assert orientation_is(3,False,True) and (a.state['rotation'],b.state['rotation'])==(3,1)
    orientation_preferences_preserved()
    sounds=a.state['sound_played'];a.cmd('powerlong');assert a.state['power_state']==0 and a.state['sound_played']==sounds
    assert a.state['dual_role']==1 and b.state['dual_role']==2
    print('PASS: two C readers, independent TF, Wi-Fi file/font transfer, simulated BLE-only turns, link loss/reconnect, both-side opening, power controls',flush=True)
    (run/'report.json').write_text(json.dumps({'passed':True,'physical_BLE_tested':False,'checks':['independent TF roots','bidirectional library union','same-name preservation','primary font transferred','open from secondary','open from primary','power primary previous / secondary next','local key cue, peer silence, mute, overlay and long-key isolation','cross-chapter navigation','no short-key lock','Wi-Fi offline BLE turns','BLE disconnect prevents turns','BLE reconnect restores state','Wi-Fi reconnect','primary-only auto direction shared across both screens','independent role switches from both menus','local role rotation shortcuts','global orientation and lock preferences preserved','disable and restart retain role offsets','landscape ignored without repagination','reading overlay defers rotation','primary displays before secondary','body-failure and header-only presentation gate','both-side numeric page jump','invalid page keeps original spread','shared size from both toolbars','anchored typography reflow','settings and record Back preserve spread','same-book shelf resume','shared contents and scrub','bookmark source navigation without modifying saved record','legacy record migration via ps_load','interrupted reflow restores durable size and anchor'],'last_primary':a.state,'last_secondary':b.state},ensure_ascii=False,indent=2)+'\n')
finally:
    (run/'transcript.json').write_text(json.dumps(history,ensure_ascii=False,indent=2)+'\n')
    for d in processes:
        if d.p.poll() is None:
            try:d.cmd('quit');d.p.wait(timeout=5)
            except Exception:d.p.terminate();d.p.wait(timeout=5)
        d.log.close()
    print(run,flush=True)

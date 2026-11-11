#!/usr/bin/env python3
"""Build three real, open-licensed product font subsets; source paths are explicit.
生成三种真实开源字体子集；不读取或再分发电脑的商业系统字体。
"""
import argparse
import hashlib
import json
import shutil
import tempfile
import difflib
from pathlib import Path
import gen_builtin_font as gen

ROOT=Path(__file__).resolve().parents[1]
UI_HEADER_EXCLUDES={'pico_reader_data.h','pico_ime_phrases.h','pico_clock_digits.h','fallback.h','stb_truetype.h','gbk_table.h'}
# 保留先前发布UI已覆盖但当前文案移除的字。/ Retain glyphs covered by previously shipped UI wording.
LEGACY_UI_COVERAGE="底渐箭"

def ui_sources():
    # 自动补齐产品路由中的UI；工程诊断仍由全覆盖内置字体负责。
    # Follow all product-route UI; the full builtin font retains engineering diagnostics.
    files={p for p in (ROOT/'main/system').iterdir() if p.suffix in {'.c','.h','.inc'} and p.name not in UI_HEADER_EXCLUDES}
    for folder,names in [('apps',['app_book','app_font_pick','app_transfer']),('ui',['ui_kit','ui_menu','ui_image'])]:
        for name in names:
            files.update(p for suffix in ['.c','.h'] if (p:=ROOT/'main'/folder/(name+suffix)).is_file())
    files.update(p for folder in ('games/src','apps/src') for p in (ROOT/folder).rglob('*') if p.suffix in {'.c','.h'})
    return sorted(files)

def source_strings(path):
    return ''.join(gen.unescape(m.group(0)) for m in gen.STRING_RE.finditer(path.read_text()))

def digest(path):return hashlib.sha256(path.read_bytes()).hexdigest()
def retitle(path,family,postscript):
    from fontTools.ttLib import TTFont
    font=TTFont(path)
    values={1:family,3:family+' 1.0',4:family,6:postscript,16:family,18:family,21:family,25:postscript}
    for record in list(font['name'].names):
        if record.nameID in values:font['name'].setName(values[record.nameID],record.nameID,record.platformID,record.platEncID,record.langID)
    font.save(path);font.close()
def fill_missing(path,donor_paths,charset):
    # 仅补源字体真实提供的缺字；按字体em比例转换轮廓，不伪造系统回退。/ Copy genuine missing donor outlines, scaled to the target em.
    from fontTools.ttLib import TTFont
    from fontTools.pens.recordingPen import DecomposingRecordingPen
    from fontTools.pens.ttGlyphPen import TTGlyphPen
    from fontTools.pens.transformPen import TransformPen
    target=TTFont(path);supported=set(target.getBestCmap());wanted={ord(ch) for ch in charset if ord(ch) not in supported and not ch.isspace()}
    if not wanted:target.close();return []
    added=[]
    if not any(t.format==12 for t in target['cmap'].tables):
        from fontTools.ttLib.tables._c_m_a_p import CmapSubtable
        full=CmapSubtable.newSubtable(12);full.platformID=3;full.platEncID=10;full.language=0;full.cmap=dict(target.getBestCmap());target['cmap'].tables.append(full)
    for donor_path in donor_paths:
        if not wanted:break
        donor=TTFont(donor_path)
        if 'fvar' in donor and any(axis.axisTag=='wght' for axis in donor['fvar'].axes):
            # 与既有静态Regular主体一致；源VF默认可能是200或300，不能直接取默认轮廓。
            # Match the shipped Regular subset; source VF defaults may be 200/300 rather than Regular.
            from fontTools.varLib.instancer import instantiateVariableFont
            donor=instantiateVariableFont(donor,{'wght':400},inplace=True)
        cmap=donor.getBestCmap();glyphs=donor.getGlyphSet();scale=target['head'].unitsPerEm/donor['head'].unitsPerEm
        for cp in sorted(wanted & cmap.keys()):
            name=f'fallback{cp:06X}';recording=DecomposingRecordingPen(glyphs);glyphs[cmap[cp]].draw(recording)
            pen=TTGlyphPen(None);recording.replay(TransformPen(pen,(scale,0,0,scale,0,0)))
            target['glyf'][name]=pen.glyph();advance,lsb=donor['hmtx'][cmap[cp]];target['hmtx'][name]=(round(advance*scale),round(lsb*scale))
            if name not in target.getGlyphOrder():target.setGlyphOrder(target.getGlyphOrder()+[name])
            for table in target['cmap'].tables:
                if table.isUnicode() and (table.format in (12,13) or cp<=0xffff):table.cmap[cp]=name
            supported.add(cp);wanted.remove(cp)
            added.append(cp)
        donor.close()
    if added:target.save(path)
    target.close()
    return sorted(added)

def compact_redundant_cmap(path):
    # 设备已优先读取format12；仅移除它完全覆盖且映射相同的重复BMP表，不重排字形。
    # The device already prefers format12; remove only identical covered BMP maps, without reordering glyphs.
    from fontTools.ttLib import TTFont
    font=TTFont(path);before=dict(font.getBestCmap());covering=[table for table in font['cmap'].tables if table.isUnicode() and table.format==12]
    removed=[];retained=[];size_before=path.stat().st_size
    for table in font['cmap'].tables:
        duplicate=table.isUnicode() and table.format==4 and any(all(full.cmap.get(cp)==glyph for cp,glyph in table.cmap.items()) for full in covering)
        if duplicate:removed.append([table.platformID,table.platEncID,table.format])
        else:retained.append(table)
    if removed:
        font['cmap'].tables=retained
        assert font.getBestCmap()==before,'Cmap compaction changed codepoint mappings'
        font.save(path)
    font.close()
    return {'removed_duplicate_tables':removed,'saved_bytes':size_before-path.stat().st_size}

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--sans',type=Path,default=gen.default_src());p.add_argument('--serif',type=Path,required=True);p.add_argument('--kai',type=Path,required=True)
    p.add_argument('--fill-missing',action='store_true',help='Preserve shipped subsets and append only genuinely missing UI glyphs / 保留现有子集，仅补UI缺字')
    p.add_argument('--ui-delta-from',type=Path,help='With --fill-missing, add only strings changed from this source tree / 仅补相对基线新增文案')
    p.add_argument('--compact-cmap',action='store_true',help='Remove redundant BMP maps covered by identical format12 mappings / 移除format12完全覆盖的重复BMP映射')
    p.add_argument('--report',type=Path,default=ROOT/'build/product-fonts.json');args=p.parse_args()
    raster_regression="龘𰻞𠮷麤"
    text=gen.unique_text(gen.ascii_printable(),gen.load_preset(gen.CHARSET),gen.scan_sources(),raster_regression,LEGACY_UI_COVERAGE)
    parts=[]
    for fn in ['main/system/pico_reader.c','main/system/pico_system.c','main/system/pico_storage.c','main/system/pico_connectivity.c','main/system/pico_usb.c','main/system/pico_draw.c','main/system/pico_reader_data.h','main/assets/preview_riverside.txt']:
        path=ROOT/fn;s=path.read_text().split('/* 拼音数据源')[0]
        parts.append(s if path.suffix=='.txt' else ''.join(gen.unescape(m.group(0)) for m in gen.STRING_RE.finditer(s)))
    ui_files=ui_sources()
    prose=gen.unique_text(gen.ascii_printable(),*parts,*(source_strings(path) for path in ui_files),raster_regression,LEGACY_UI_COVERAGE)
    if args.ui_delta_from:
        assert args.fill_missing,'Delta coverage requires preserving shipped glyphs'
        delta=[]
        for path in ui_files:
            before=args.ui_delta_from/path.relative_to(ROOT)
            lines=difflib.unified_diff(before.read_text().splitlines() if before.is_file() else [],path.read_text().splitlines())
            added='\n'.join(line[1:] for line in lines if line.startswith('+') and not line.startswith('+++') and not line.lstrip('+ ').startswith(('//','*')))
            delta.extend(gen.unescape(m.group(0)) for m in gen.STRING_RE.finditer(added))
        text=prose=gen.unique_text(gen.ascii_printable(),*delta)
    report={}
    from fontTools.ttLib import TTFont
    with tempfile.TemporaryDirectory(prefix='pico-font-subsets-') as directory:
        generated=Path(directory)
        for kind,source,charset in [('builtin',args.sans,text),('serif',args.serif,prose),('kai',args.kai,prose)]:
            out=generated/f'{kind}.ttf'
            existing=ROOT/'main/assets'/f'{kind}.ttf'
            previous=set()
            if args.fill_missing:
                font=TTFont(existing);previous=set(font.getBestCmap());font.close()
                shutil.copyfile(existing,out)
            else:gen.subset_regular(source,charset,out)
            added=fill_missing(out,[source,args.kai,args.sans,args.serif],charset)
            if not args.fill_missing:retitle(out,'Read Pico '+{'builtin':'Sans','serif':'Serif','kai':'Kai'}[kind],'ReadPico'+kind.title())
            compaction=compact_redundant_cmap(out) if args.compact_cmap else {'removed_duplicate_tables':[],'saved_bytes':0}
            font=TTFont(out);supported=set(font.getBestCmap());font.close()
            assert previous<=supported,'Previously shipped glyph coverage was removed'
            missing=[ch for ch in charset if ord(ch) not in supported and not ch.isspace()]
            report[kind]={'source_sha256':digest(source),'bytes':out.stat().st_size,'sha256':digest(out),'requested':len(charset),'missing':['U+%04X'%ord(ch) for ch in missing],
                          'mode':'fill-missing' if args.fill_missing else 'rebuild','retained':len(previous),'added':['U+%04X'%cp for cp in added],
                          'donor_weight':400,
                          'cmap_compaction':compaction,
                          'ui_sources':{str(path.relative_to(ROOT)):digest(path) for path in ui_files}}
            print(kind,out.stat().st_size,'bytes; missing',report[kind]['missing'])
        args.report.parent.mkdir(parents=True,exist_ok=True);args.report.write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n')
        # 为新增产品文案保留小幅补字预算，最终仍须验证4MiB固件上限。
        # Allow bounded product-copy glyph growth; the final firmware must still fit its 4 MiB partition.
        assert sum(x['bytes'] for x in report.values())<1728*1024,'Product font budget exceeded'
        assert not any(x['missing'] for x in report.values()),'Product font coverage incomplete'
        # 三字体均通过预算及覆盖检查后才发布，失败不留下半更新资源。
        # Publish only after all three fonts pass budget and coverage checks.
        for kind in report:shutil.copyfile(generated/f'{kind}.ttf',ROOT/'main/assets'/f'{kind}.ttf')
if __name__=='__main__':main()

#!/usr/bin/env python3
"""中文：语义画板严格解析与原生排版回归，无硬件。English: Semantic board parser/native-layout regression, without hardware."""
import argparse
import base64
import copy
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import tempfile
import struct
import zlib

ROOT = Path(__file__).resolve().parents[1]
KINDS = 'clock date cpu memory network now_playing pomodoro uptime text image qwen_quota codex oracle_text sspai disk home_assistant bambu_lab formlabs weather reading_excerpt cloudflare grok_bot'.split()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, default=ROOT/'build/board-data-host/report.json')
    parser.add_argument('--fixture', type=Path, help='Validate an independently generated client snapshot')
    args = parser.parse_args()
    sources = ['main/system/pico_board_data.c', 'main/system/pico_board_data.h', 'main/system/pico_board_render.c',
               'components/read_pico_home_link/home_link_json.c', 'components/read_pico_home_link/home_link_json.h', 'tools/pico_board_host_test.c', 'tools/pico_board_alloc_test.c']
    hashes = {name: hashlib.sha256((ROOT/name).read_bytes()).hexdigest() for name in sources}
    world_map=(ROOT/'resources/cloudflare-world-dots.bwm').read_bytes()
    world_text=base64.b64encode(world_map).decode()
    with tempfile.TemporaryDirectory(prefix='pico-board-data-') as directory:
        temp = Path(directory)
        (temp/'mbedtls').mkdir()
        (temp/'mbedtls/sha256.h').write_text('#pragma once\n#include <stddef.h>\nint mbedtls_sha256(const unsigned char*,size_t,unsigned char[32],int);\n')
        (temp/'sha.c').write_text('#include <CommonCrypto/CommonDigest.h>\n#include "mbedtls/sha256.h"\nint mbedtls_sha256(const unsigned char*p,size_t n,unsigned char out[32],int is224){return is224||!CC_SHA256(p,(CC_LONG)n,out);}\n')
        raw = bytes([0x01,0x23,0x45,0x67,0x89,0xab,0xcd,0xef])*32
        asset = {'id': hashlib.sha256(raw).hexdigest(), 'width':32, 'height':16, 'format':'gray4', 'data':base64.b64encode(raw).decode()}
        base = {'schema':1,'board_id':'test-board','generated_at':1793116800,'columns':2,'dark':False,'modules':[{'kind':'text','id':'first','title':'中文与 English','body':'正常显示，可离线保留。'}]}
        corpus = []
        def case(value, valid=True):
            data = value if isinstance(value,bytes) else json.dumps(value,ensure_ascii=False,separators=(',',':')).encode()
            path = temp/f'case-{len(corpus):04}.json'; path.write_bytes(data)
            corpus.append(('V' if valid else 'I', path))
        case(base)
        if args.fixture: case(args.fixture.read_bytes())
        for kind in KINDS:
            value = copy.deepcopy(base); m=value['modules'][0]; m['kind']=kind
            m.update(subtitle='信息来源与当前状态',body='中文 English '+('长段落需要在卡片内自动换行。'*80),footer='TF / Offline',
                     metrics=[{'label':'Requests','value':'123.4','unit':'k'},{'label':'流量','value':'8.2','unit':'GB'}],
                     rows=[{'label':'条目 '+str(i),'value':'在线 / available'} for i in range(24)],series=[0,5,1,15,7,22,4,18],
                     progress=50,duration=600,timestamp=1793116800,running=kind in ('now_playing','pomodoro'))
            if kind in ('image','now_playing','bambu_lab','formlabs','sspai'):
                value['assets']=[asset];m['asset']=asset['id']
            if kind=='cloudflare':
                value['world_map']=world_text;m['regions']=[{'code':'CN','value':150},{'code':'US','value':100},{'code':'UNKNOWN','value':15}]
            case(value)
        cloud=copy.deepcopy(base);cloud['world_map']=world_text;cloud['modules'][0].update(kind='cloudflare',regions=[{'code':'CN','value':150},{'code':'US','value':100},{'code':'UNKNOWN','value':15}])
        for scale in (2,3,4):
            for presentation in ('worldMap','ranking'):
                value=copy.deepcopy(cloud);value['text_scale']=scale;value['modules'][0]['presentation']=presentation
                value['modules'][0]['rank_metric']='traffic' if scale%2 else 'requests'
                value['modules'][0]['span']=2
                if presentation=='ranking':value.pop('world_map')
                case(value)
        for layout in ('horizontal','vertical'):
            value=copy.deepcopy(base);value['assets']=[asset];value['modules'][0].update(kind='now_playing',asset=asset['id'],media_layout=layout,body='Artist / Album');case(value)
        for count in (1,12):
            value=copy.deepcopy(base);value['modules']=[dict(base['modules'][0],id=str(i),span=2 if i%2 else 1) for i in range(count)]
            for scale in (2,3,4):value['text_scale']=scale;case(value)
        for scale in (2,3,4):
            value=copy.deepcopy(base);value['text_scale']=scale
            value['modules']=[dict(base['modules'][0],id=str(i),span=2) for i in range(12)];case(value)
        value=copy.deepcopy(cloud);value['modules'][0]['regions']=[{'code':'CODE'+str(i),'value':i} for i in range(250)];case(value)
        for count in range(1,13):
            for columns in (1,2):
                value=copy.deepcopy(base);value['columns']=columns;value['dark']=bool(count%2)
                value['modules']=[dict(base['modules'][0],id=str(i),metrics=[{'label':'Temperature','value':'21.8','unit':'°C'}]) for i in range(count)]
                case(value)
        for field,v in [('schema',0),('schema',2),('schema',1.5),('columns',0),('columns',3),('columns',True),('dark',1),('board_id',''),('board_id','x'*65),('generated_at',-1),('generated_at',1e50),('modules',[]),('modules',{}),('unknown',1)]:
            value=copy.deepcopy(base);value[field]=v;case(value,False)
        for field,v in [('kind','bad'),('id',''),('id','x'*65),('body','好'*2731),('title','x'*193),('timestamp',-1),('duration',1e30),('progress',101),('progress',float('nan')),('running',1),('series',[float('inf')]),('series',[0]*61),('rows',[{}]*25),('metrics',[{}]*9),('asset','0'*64),('path','/sdcard/test')]:
            value=copy.deepcopy(base);value['modules'][0][field]=v;case(value,False)
        value=copy.deepcopy(base);value['modules']*=13;case(value,False)
        value=copy.deepcopy(base);value['modules']*=2;case(value,False)
        value=copy.deepcopy(base);value['modules'][0]['kind']='image';case(value,False)
        value=copy.deepcopy(base);value['modules'][0]['body']='好'*2730;case(value)
        with_image=copy.deepcopy(base);with_image['modules'][0].update(kind='image',asset=asset['id']);with_image['assets']=[asset]
        for field,v in [('width',0),('height',193),('width',1.5),('format','bmp'),('id','0'*64),('data',asset['data'][:-4]),('data',asset['data']+'AAAA')]:
            value=copy.deepcopy(with_image);value['assets'][0][field]=v;case(value,False)
        value=copy.deepcopy(with_image);value['assets']*=2;case(value,False)
        for field,v in [('span',0),('span',3),('span',1.5),('media_layout','horizontal'),('presentation','ranking'),('rank_metric','traffic'),('regions',[])]:
            value=copy.deepcopy(base);value['modules'][0][field]=v;case(value,False)
        for field,v in [('presentation','bad'),('rank_metric','bytes'),('regions',[{'code':'CN','value':-1}]),('regions',[{'code':'中国','value':1}]),('regions',[{'code':'A'*33,'value':1}]),('regions',[{'code':'CN','value':1}]*2),('regions',[{'code':str(i),'value':i} for i in range(251)])]:
            value=copy.deepcopy(cloud);value['modules'][0][field]=v;case(value,False)
        for scale in (1,5,2.5,True):value=copy.deepcopy(base);value['text_scale']=scale;case(value,False)
        value=copy.deepcopy(base);value['world_map']=world_text;case(value,False)
        value=copy.deepcopy(cloud);value.pop('world_map');case(value,False)
        for bad in [b'',b'A'*8193,world_map[:-1],bytes([world_map[0]^1])+world_map[1:]]:
            value=copy.deepcopy(cloud);value['world_map']=base64.b64encode(bad).decode();case(value,False)
        for offset,replacement in [(8,struct.pack('<H',119)),(12,struct.pack('<H',251)),(24,b'xx'),(26,struct.pack('<H',1)),(24+147*6,struct.pack('<H',6240))]:
            damaged=bytearray(world_map);damaged[offset:offset+len(replacement)]=replacement;struct.pack_into('<I',damaged,20,zlib.crc32(damaged[24:]))
            value=copy.deepcopy(cloud);value['world_map']=base64.b64encode(damaged).decode();case(value,False)
        for broken in [b'',b'{}',b'[]',b'{"schema":1,"schema":1}',b'{"schema":1,"board_id":"\\u0000hidden"}',b'['*40+b']'*40,
                       b'{"x":"\xff"}',b'{"x":"\xc0\x80"}',b'{"x":"\xed\xa0\x80"}',b'{"x":"\xf4\x90\x80\x80"}',b'{"x":"\xf0\x9f"}',
                       json.dumps(base).encode()+b'junk',json.dumps(base).encode()+b'\0',b' '*65537]:case(broken,False)
        normal=json.dumps(base,ensure_ascii=False,separators=(',',':')).encode()
        for i in range(len(normal)): case(normal[:i],False)
        for a,b in [(b'"schema":1',b'"schema":1,"schema":1'),(b'"title":',b'"title":"duplicate","title":'),(b'"body":',b'"body":"\\u0000","extra":')]:case(normal.replace(a,b),False)
        # 中文：无效Unicode及同内容不同采样时间也走实际解析器。/ Invalid Unicode and sampling-only variants use the real parser.
        value=copy.deepcopy(base);value['generated_at']+=1;case(value)
        cases=temp/'cases.txt';cases.write_text(''.join(f'{kind} {path}\n' for kind,path in corpus))
        # 中文：仅替换底层分配调用，私有解析器及地图校验保持实际实现。
        # English: Replace only allocation calls; retain the production private parser and map validation.
        alloc_header=temp/'allocation.h';alloc_header.write_text('#include <stddef.h>\nvoid *pico_test_allocate(size_t);\n')
        alloc_data=temp/'alloc-data.c';alloc_json=temp/'alloc-json.c'
        alloc_data.write_text((ROOT/'main/system/pico_board_data.c').read_text().replace('#define BOARD_ALLOC(n) malloc(n)','#define BOARD_ALLOC(n) pico_test_allocate(n)').replace('../../components/read_pico_home_link/home_link_json.h',str(ROOT/'components/read_pico_home_link/home_link_json.h')))
        alloc_json.write_text((ROOT/'components/read_pico_home_link/home_link_json.c').read_text().replace('void *p=malloc(n)','void *p=pico_test_allocate(n)'))
        alloc_fixture=temp/'allocation-model.json';alloc_fixture.write_text(json.dumps(cloud))
        runs=[]
        for sanitized in (False,True):
            exe=temp/('sanitized' if sanitized else 'normal')
            flags=['-std=gnu11','-O1','-g','-Wall','-Wextra','-Werror','-Wno-deprecated-declarations','-DPICO_BOARD_PORTABLE',
                   '-I',str(temp),'-I',str(ROOT/'main/system'),'-I',str(ROOT/'managed_components/espressif__cjson/cJSON')]
            if sanitized:flags+=['-fsanitize=address,undefined','-fno-omit-frame-pointer']
            cmd=[os.environ.get('CC','cc'),*flags,str(ROOT/'tools/pico_board_host_test.c'),str(ROOT/'main/system/pico_board_data.c'),
                 str(ROOT/'main/system/pico_board_render.c'),str(ROOT/'components/read_pico_home_link/home_link_json.c'),str(temp/'sha.c'),'-o',str(exe)]
            subprocess.run(cmd,check=True)
            result=subprocess.run([str(exe),str(cases)],capture_output=True,text=True)
            print(result.stdout,end='')
            if result.returncode:print(result.stderr,end='');result.check_returncode()
            allocation_exe=temp/('allocation-sanitized' if sanitized else 'allocation-normal')
            subprocess.run([os.environ.get('CC','cc'),*flags,'-include',str(alloc_header),'-I',str(ROOT/'components/read_pico_home_link'),
                            str(ROOT/'tools/pico_board_alloc_test.c'),str(alloc_data),str(alloc_json),str(temp/'sha.c'),'-o',str(allocation_exe)],check=True)
            allocation_result=subprocess.run([str(allocation_exe),str(alloc_fixture)],capture_output=True,text=True)
            print(allocation_result.stdout,end='')
            if allocation_result.returncode:print(allocation_result.stderr,end='');allocation_result.check_returncode()
            runs.append({'sanitized':sanitized,'checks':int(re.search(r'(\d+) checks PASS',result.stdout)[1]),
                         'allocation_checks':int(re.search(r'(\d+) allocation checks PASS',allocation_result.stdout)[1])})
        assert hashes=={name:hashlib.sha256((ROOT/name).read_bytes()).hexdigest() for name in sources}
        args.output.parent.mkdir(parents=True,exist_ok=True)
        args.output.write_text(json.dumps({'result':'PASS','hardware_access':False,'valid':sum(k=='V' for k,p in corpus),'rejected':sum(k=='I' for k,p in corpus),'runs':runs,'source_sha256':hashes},indent=2)+'\n')


if __name__=='__main__':main()

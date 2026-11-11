#!/usr/bin/env python3
"""中文：由已校验 PGA 目录生成向后兼容的更新清单。/ English: Generate a backward-compatible update feed from validated PGA catalogs."""
import argparse,hashlib,json,re,struct,zlib
from pathlib import Path

def feed(catalogs):
    entries={}
    for path in catalogs:
        data=json.loads(path.read_text())
        for item in data.get('apps',data.get('games',[])):
            ident=item['id'];version=item.get('version_code',item['version'])
            if not re.fullmatch(r'[a-z0-9](?:[a-z0-9-]{0,29}[a-z0-9])?',ident) or ident in entries:
                raise ValueError('Invalid or duplicate ID: '+ident)
            if not isinstance(version,int) or not 0<version<=0xffffffff:raise ValueError('Invalid version: '+ident)
            name=item['file']
            if Path(name).name!=name:raise ValueError('Package must be beside catalog')
            package=(path.parent/name).read_bytes();h=bytearray(package[:512])
            if len(h)!=512 or h[:8]!=b'PICOAPP1':raise ValueError('Invalid header: '+ident)
            crc=struct.unpack_from('<I',h,232)[0];h[232:236]=b'\0'*4
            actual_id=h[32:64].split(b'\0',1)[0].decode('ascii')
            if zlib.crc32(h)!=crc or actual_id!=ident or struct.unpack_from('<I',h,24)[0]!=version:
                raise ValueError('Header differs from catalog: '+ident)
            if len(package)!=item['bytes'] or hashlib.sha256(package).hexdigest()!=item['sha256']:
                raise ValueError('Package differs from catalog: '+ident)
            kind=struct.unpack_from('<I',h,28)[0]
            if kind!=item['kind']:raise ValueError('Kind differs from catalog: '+ident)
            count=struct.unpack_from('<I',h,228)[0]
            targets=[h[256+i*32:272+i*32].split(b'\0',1)[0] for i in range(min(count,4))]
            if count<1 or count>4 or b'esp32s3-xtensa' not in targets:
                raise ValueError('Device payload missing: '+ident)
            entries[ident]=('tool' if kind>=128 else 'game',version)
    if not entries or len(entries)>12:raise ValueError('Feed requires 1..12 entries')
    text='xhv=1\n'+''.join(f'{kind}={ident}\napp_version={ident},{version}\n' for ident,(kind,version) in entries.items())
    if len(text.encode())>2048:raise ValueError('Feed exceeds device response limit')
    return text

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('catalogs',type=Path,nargs='+');p.add_argument('--output',type=Path,required=True);a=p.parse_args()
    result=feed(a.catalogs);a.output.parent.mkdir(parents=True,exist_ok=True);a.output.write_text(result)
    print('Generated versioned app feed:',a.output)

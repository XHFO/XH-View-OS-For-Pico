#!/usr/bin/env python3
"""中文：同源设备微信读书服务的离线回归，不访问硬件、真实密钥或账户。
English: Offline regression for the production device WeRead service, without hardware, real credentials or accounts.
"""
from pathlib import Path
import argparse
import hashlib
import json
import os
import re
import subprocess
import tempfile
ROOT=Path(__file__).resolve().parents[1]
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--output',type=Path,default=ROOT/'build/weread-device-service-tests')
args=parser.parse_args()
output=args.output.resolve();output.mkdir(parents=True,exist_ok=True)
with tempfile.TemporaryDirectory(prefix='pico-weread-') as tmp:
    private=Path(tmp);private.chmod(0o700)
    binary=private/'firmware-weread-tests'
    validator=private/'module-validator.c'
    validator.write_text('#include <stdio.h>\n#include <string.h>\n#include "'+str(ROOT/'apps/src/weread.c')+'"\n'+r'''
bool wr_module_validate(const char *directory){
    static pico_app_host_t validation_host={.compare=memcmp};host=&validation_host;
    char path[512];snprintf(path,sizeof(path),"%s/index.dat",directory);FILE *f=fopen(path,"rb");
    if(!f)return false;bool ok=fseek(f,12,SEEK_SET)==0&&fread(index_data,1,WRS_INDEX_BYTES,f)==WRS_INDEX_BYTES;fclose(f);
    if(!ok||!valid_index())return false;
    for(unsigned slot=0;slot<u32(index_data+WRS_I_BOOKS);slot++)for(unsigned part=0;part<chunks(slot);part++){
        snprintf(path,sizeof(path),"%s/g%08x-b%02x-c%03x.dat",directory,u32(index_data+WRS_I_GENERATION),slot,part);f=fopen(path,"rb");
        if(!f)return false;ok=fseek(f,12,SEEK_SET)==0&&fread(chunk_data,1,WRS_CHUNK_BYTES,f)==WRS_CHUNK_BYTES;fclose(f);
        if(!ok||!valid_chunk(slot,part))return false;
    }
    return true;
}
''')
    subprocess.run([os.environ.get('CC','cc'),'-std=c11','-Wall','-Wextra','-Werror','-g','-fsanitize=address,undefined','-fno-omit-frame-pointer','-I',str(ROOT/'managed_components/espressif__cjson/cJSON'),'-I',str(ROOT/'components/read_pico_apps/include'),str(validator),str(ROOT/'tools/pico_weread_host_test.c'),str(ROOT/'components/read_pico_home_link/home_link_json.c'),'-pthread','-lm','-o',str(binary)],check=True)
    result=subprocess.run([str(binary),str(private)],timeout=45,text=True,capture_output=True)
    (output/'test-output.log').write_text(result.stdout+result.stderr,encoding='utf-8')
    if result.returncode:
        print(result.stderr,end='')
        result.check_returncode()
    print(result.stdout,end='')
    report={'schema':1,'kind':'device-weread-service-offline-regression','checks':int(re.search(r'checks: (\d+)',result.stdout).group(1)),
            'passed':True,'sanitizers':['address','undefined'],'official_account_tested':False,'hardware_access':False,
            'snapshot_validation':'actual apps/src/weread.c index and chunk validators',
            'cover_result_lifecycle':['information readable with ready=true and busy=true',
                                      'optional cover failure preserves information',
                                      'completion increments query revision',
                                      'cancellation immediately revokes in-flight ready result',
                                      'completed result survives a later cancel'],
            'limits':{'official_response_buffer_bytes':524288,'official_response_payload_max_bytes':524287,
                      'query_json_cache_max_bytes':524287,'query_http_chunk_max_bytes':8192,'query_readonly_operations':9,
                      'large_directory_fixture_chapters':2000,'shelf_books':64,
                      'selected_books':64,'total_normalized_note_text_bytes':33554432,'chunks_per_book':256,'utf8_bytes_per_chunk':32704},
            'stdout':result.stdout.strip(),'sources':{str(p.relative_to(ROOT)):hashlib.sha256(p.read_bytes()).hexdigest() for p in
                [ROOT/'main/system/pico_weread.c',ROOT/'main/system/pico_weread.h',ROOT/'main/system/pico_weread_cover.h',ROOT/'tools/pico_weread_host_test.c',ROOT/'tools/test_pico_weread.py',ROOT/'apps/src/weread.c']}}
    (output/'report.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n')

#!/usr/bin/env python3
"""Run production paired-content file streams in isolated temporary directories."""
import hashlib,json,os,re,subprocess,tempfile
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
names=['components/read_pico_transfer/read_pico_content.c','components/read_pico_transfer/include/read_pico_content.h','components/read_pico_transfer/transfer_font.h','components/read_pico_apps/pico_app_package.c','tools/pico_content_host_test.c']
hashes={n:hashlib.sha256((ROOT/n).read_bytes()).hexdigest() for n in names}
with tempfile.TemporaryDirectory(prefix='pico-content-') as temp:
    work=Path(temp);(work/'psa').mkdir()
    (work/'read_pico_sd.h').write_text('#pragma once\n#include <stdbool.h>\n#include <stdint.h>\ntypedef struct{bool present,mounted,exported;uint64_t free_bytes;uint32_t media_generation;}read_pico_sd_info_t;int read_pico_sd_get_info(read_pico_sd_info_t*);\n')
    (work/'psa/crypto.h').write_text('''#pragma once
#include <CommonCrypto/CommonDigest.h>
#include <stddef.h>
#define PSA_SUCCESS 0
#define PSA_ALG_SHA_256 1
typedef CC_SHA256_CTX psa_hash_operation_t;
#define PSA_HASH_OPERATION_INIT {0}
static inline int psa_crypto_init(void){return 0;}
static inline int psa_hash_setup(psa_hash_operation_t *h,int a){(void)a;return CC_SHA256_Init(h)?0:-1;}
static inline int psa_hash_update(psa_hash_operation_t *h,const void *p,size_t n){return CC_SHA256_Update(h,p,(CC_LONG)n)?0:-1;}
static inline int psa_hash_finish(psa_hash_operation_t *h,void *p,size_t n,size_t *out){(void)n;*out=32;return CC_SHA256_Final(p,h)?0:-1;}
static inline int psa_hash_abort(psa_hash_operation_t *h){(void)h;return 0;}
''')
    runs=[]
    for sanitized in (False,True):
        exe=work/('sanitized' if sanitized else 'normal');card=work/('card-sanitized' if sanitized else 'card-normal');card.mkdir()
        cmd=[os.environ.get('CC','cc'),'-std=gnu11','-Wall','-Wextra','-Werror','-Wno-deprecated-declarations','-g','-O1']
        if sanitized:cmd+=['-fsanitize=address,undefined','-fno-omit-frame-pointer']
        for d in [work,ROOT/'components/read_pico_transfer/include',ROOT/'components/read_pico_apps/include',ROOT/'managed_components/espressif__cjson/cJSON']:cmd+=['-I',str(d)]
        cmd += [str(ROOT/'tools/pico_content_host_test.c'),str(ROOT/'components/read_pico_apps/pico_app_package.c'),str(ROOT/'components/read_pico_home_link/home_link_json.c'),'-o',str(exe)]
        subprocess.run(cmd,check=True);run=subprocess.run([str(exe),str(card),str(ROOT/'main/assets/builtin.ttf')],capture_output=True,text=True);print(run.stdout,end='');print(run.stderr,end='');run.check_returncode();runs.append({'sanitized':sanitized,'output':run.stdout.strip()})
assert hashes=={n:hashlib.sha256((ROOT/n).read_bytes()).hexdigest() for n in names}
out=ROOT/'build/content-http-check/report.json';out.parent.mkdir(parents=True,exist_ok=True);out.write_text(json.dumps({'result':'PASS','hardware_access':False,'source_sha256':hashes,'runs':runs},indent=2)+'\n')

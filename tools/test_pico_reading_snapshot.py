#!/usr/bin/env python3
"""生产双缓冲阅读快照与USB交接顺序回归，仅操作合成构建夹具。
Production reading snapshots and USB handoff ordering using synthetic build fixtures only.
"""
from pathlib import Path
import argparse
import os
import shlex
import subprocess
import tempfile
from test_pico_transfer_bridge import function
ROOT = Path(__file__).resolve().parents[1]
PRELUDE = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#define ESP_OK 0
static bool s_external_active,s_transfer_prepared,s_store_lost,s_store_is_sd,s_network_resume_pending,s_external_was_lost;
static uint32_t s_external_generation;
static bool busy,fail_quiesce,fail_save,fail_snapshot,fail_prepare,bulk;
static unsigned stage,probes,resumes;
typedef struct{bool mounted,exported;uint32_t media_generation;}read_pico_sd_info_t;
static bool ps_storage_busy(void){return busy;}
static int read_pico_sd_get_info(read_pico_sd_info_t *out){probes++;*out=(read_pico_sd_info_t){true,false,7};return ESP_OK;}
static bool ps_connectivity_shutdown(void){assert(stage==0);stage=1;return !fail_quiesce;}
static bool ps_reader_power_save(void){assert(stage==1&&!s_external_active);stage=2;return !fail_save;}
static bool reading_snapshot_prepare(void){assert(stage==2&&!s_external_active);stage=3;return !fail_snapshot;}
static int read_pico_connectivity_resume(void){resumes++;return ESP_OK;}
static bool ps_storage_prepare(void){assert(stage==3&&!s_external_active);stage=4;return !fail_prepare;}
static void ps_storage_resume(void){resumes++;}
static void display_set_bulk_io(bool value){assert(stage==4);bulk=value;}
static void ps_toast(const char *message){(void)message;}
'''
CHECKS = r'''
static void reset(void){s_external_active=s_transfer_prepared=s_store_lost=s_store_is_sd=s_network_resume_pending=s_external_was_lost=false;busy=fail_quiesce=fail_save=fail_snapshot=fail_prepare=bulk=false;stage=probes=resumes=0;}
int main(void){
reset();assert(ps_external_prepare()&&stage==4&&s_external_active&&bulk&&probes==2&&s_external_generation==7);
reset();fail_snapshot=true;assert(!ps_external_prepare()&&stage==3&&!s_external_active&&!bulk&&resumes==1&&probes==1);
reset();fail_save=true;assert(!ps_external_prepare()&&stage==2&&!s_external_active&&!bulk&&resumes==1);
reset();fail_quiesce=true;assert(!ps_external_prepare()&&stage==1&&!s_external_active&&!bulk&&s_network_resume_pending);
reset();busy=true;assert(!ps_external_prepare()&&!stage&&!probes);
reset();s_external_active=true;assert(!ps_external_prepare()&&!stage&&!probes);
reset();s_transfer_prepared=true;assert(!ps_external_prepare()&&!stage&&!probes);
puts("PASS: actual USB handoff saves then snapshots before media release; save/snapshot/quiesce failure never exports stale content or grants USB ownership");return 0;}
'''
def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--sanitize',action='store_true');args=parser.parse_args()
    flags=shlex.split(os.environ.get('CFLAGS',''))
    if args.sanitize: flags+=['-fsanitize=address,undefined','-fno-omit-frame-pointer']
    compiler=[os.environ.get('CC','cc'),'-std=gnu11','-Wall','-Wextra','-Werror','-Wno-unused-function','-Wno-unused-variable']+flags
    with tempfile.TemporaryDirectory(prefix='pico-snapshot-') as temporary:
        exe=Path(temporary)/'snapshot'
        includes=sum((['-I',str(ROOT/path)] for path in ['tools/pico_reader_stubs','tools/preview_reading_stubs','main/system','main/book']),[])
        subprocess.run(compiler+includes+[str(ROOT/'tools/pico_reading_snapshot_host_test.c'),'-o',str(exe)],check=True)
        subprocess.run([str(exe)],cwd=ROOT,check=True)
        source=Path(temporary)/'handoff.c';source.write_text(PRELUDE+function((ROOT/'main/system/pico_system.c').read_text(),'ps_external_prepare')+CHECKS)
        subprocess.run(compiler+[str(source),'-o',str(exe)],check=True);subprocess.run([str(exe)],check=True)
if __name__=='__main__':main()

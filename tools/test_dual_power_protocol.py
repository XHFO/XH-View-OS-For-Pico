#!/usr/bin/env python3
"""Run lock arbitration with loss, duplication, conflicts and stale frames; no hardware."""
from pathlib import Path
import subprocess,tempfile
root=Path(__file__).resolve().parents[1]
source=r'''
#include "pico_dual_power.h"
#include <assert.h>
#include <stdio.h>
int main(void){
 ps_dual_power_t a={0},b={0};uint8_t p[9],old[9];
 pdp_set(&a,1,true);assert(pdp_take(&a,1,1,old)==9);
 // 丢请求后重传。/ Retransmit a lost request.
 assert(!pdp_take(&a,1,100,p));assert(pdp_take(&a,1,401,p)==9);
 assert(pdp_receive(&b,2,p,9)==1&&b.locked);
 assert(pdp_take(&b,2,402,p)==9&&p[1]==2);
 // 丢确认后重复请求仍确认；不会反复触发锁屏。/ Lost ACK re-acks duplicates without relocking.
 assert(pdp_take(&a,1,801,p)==9);assert(pdp_receive(&b,2,p,9)==0);
 assert(pdp_take(&b,2,802,p)==9);assert(pdp_receive(&a,1,p,9)==0);
 assert(!pdp_take(&a,1,2000,p)&&!pdp_take(&b,2,2000,p));
 pdp_set(&b,2,false);assert(pdp_take(&b,2,2001,p)==9);
 assert(pdp_receive(&a,1,p,9)==1&&!a.locked);
 assert(pdp_receive(&a,1,old,9)==-1); // 错误发送者。/ Wrong sender.
 old[2]=2;old[1]=2;assert(pdp_receive(&a,1,old,9)==0&&!a.locked);
 // 同时请求主机优先；下一次用户操作总是递增。/ Primary wins simultaneous requests; next action increments.
 a=(ps_dual_power_t){0};b=a;pdp_set(&a,1,true);pdp_set(&b,2,false);
 assert(pdp_take(&a,1,3000,p)==9);assert(pdp_receive(&b,2,p,9)==1&&b.locked);
 pdp_set(&b,2,false);assert(pdp_take(&b,2,3001,p)==9);assert(pdp_receive(&a,1,p,9)==1&&!a.locked);
 p[8]=0;assert(pdp_receive(&a,1,p,9)==-1);p[8]=2;
 pdp_put(p+4,0);assert(pdp_receive(&a,1,p,9)==-1);pdp_put(p+4,b.clock);
 p[3]=3;assert(pdp_receive(&a,1,p,9)==-1);assert(pdp_receive(&a,1,p,8)==-1);
 puts("PASS: linked power loss, ACK loss, duplicates, stale state, simultaneous actions, wake and malformed frames");
}
'''
with tempfile.TemporaryDirectory() as d:
 p=Path(d);(p/'test.c').write_text(source)
 subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-I',str(root/'main/system'),str(p/'test.c'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)

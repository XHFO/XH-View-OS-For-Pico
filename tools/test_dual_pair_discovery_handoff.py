#!/usr/bin/env python3
"""Run production discovery completion and BLE connect gating; no physical radio.

Exercise UDP-first, BLE-first, retry and cancellation with boundary doubles.
"""
import argparse
from pathlib import Path
import re
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def extract(source, name):
    match = re.search(r'(?:^|\n)((?:static\s+)?[\w\s*]+?\b' + re.escape(name) +
                      r'\([^;]*?\)\s*\{)', source)
    if not match:
        raise RuntimeError(name)
    start, at, depth, quote = match.start(1), match.end() - 1, 0, None
    while at < len(source):
        c = source[at]
        if quote:
            if c == '\\':
                at += 2
                continue
            if c == quote:
                quote = None
        elif source.startswith('//', at):
            at = source.index('\n', at)
            continue
        elif source.startswith('/*', at):
            at = source.index('*/', at) + 2
            continue
        elif c in '\"\'':
            quote = c
        elif c == '{':
            depth += 1
        elif c == '}':
            depth -= 1
            if not depth:
                return source[start:at + 1]
        at += 1
    raise RuntimeError(name)


prelude = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#define portENTER_CRITICAL(x) ((void)(x))
#define portEXIT_CRITICAL(x) ((void)(x))
#define ESP_OK 0
#define NONE 65535
#define PICO_NATIVE_PREVIEW 1
typedef int esp_err_t;
typedef struct {bool found;char ip[48],ap_ssid[33];bool direct;} pico_dual_ble_peer_info_t;
typedef struct {bool online;char ssid[33];} read_pico_connectivity_link_t;
typedef struct {uint8_t type,val[6];} ble_addr_t;
struct ble_gap_disc_desc {const uint8_t *data;unsigned length_data;ble_addr_t addr;};
struct uuid {int u;};
struct ble_hs_adv_fields {const uint8_t *mfg_data;unsigned mfg_data_len,num_uuids128;struct uuid *uuids128;};
struct ble_gap_conn_params {int scan_itvl,scan_window,itvl_min,itvl_max,latency,supervision_timeout,min_ce_len,max_ce_len;};
static struct uuid service_uuid={7};
static int guard,pair_sock=-1;
static struct {int role;char code[7];}settings;
static struct {bool synced;}status;
static bool pair_scanning,pair_found,pair_failed,pair_editing,scan_only;
static bool data_finished,hotspot_active,draft_short,connecting;
static char pair_ip[48],pair_code[7],pair_ap_ssid[33],draft_host[48],draft_ap_ssid[33];
static char primary_ip[48],hotspot_ssid[33],auto_ap_ssid[33];
static int64_t pair_started,last_ip,pair_last_tx,auto_ap_retry_at,setup_deadline,now;
static uint16_t connection=NONE;
static uint8_t address_type;
static ble_addr_t attempted,rejected[4];
static unsigned rejected_count,closed,stops,connects,prompts,checks;
static int64_t reject_until;
static pico_dual_ble_peer_info_t peer_info;
static char udp[96];
static struct ble_hs_adv_fields advertisement;
#define CHECK(x) do {checks++;if(!(x)){fprintf(stderr,"line %d: %s\n",__LINE__,#x);abort();}}while(0)
static void load(void){}
static int64_t ps_now(void){return now;}
static int64_t esp_timer_get_time(void){return now*1000;}
static bool local_address(char *ip){strcpy(ip,"192.168.50.9");return true;}
static void pico_dual_ble_set_pairing_info(const char *ip,const char *ssid){(void)ip;(void)ssid;}
static void pico_dual_ble_peer_info(pico_dual_ble_peer_info_t *out){*out=peer_info;}
static void read_pico_connectivity_get_link(read_pico_connectivity_link_t *out){memset(out,0,sizeof(*out));out->online=true;}
static int read_pico_connectivity_connect(const char *ssid,const char *pw){(void)ssid;(void)pw;return 0;}
static void direct_password(const char *code,const char *ssid,char *pw){(void)code;(void)ssid;strcpy(pw,"test");}
static void ps_toast(const char *s){(void)s;}
static void ps_dirty(void){}
static void edit_code(const char *s){CHECK(!*s);prompts++;}
static void pico_dual_ble_stop(void){stops++;}
static int fake_close(int fd){CHECK(fd==42);closed++;return 0;}
static ssize_t fake_recvfrom(int fd,void *buf,size_t n,int flags,struct sockaddr *from,socklen_t *len){
 (void)flags;(void)from;(void)len;CHECK(fd==42);
 if(!udp[0])return -1;size_t size=strlen(udp);CHECK(size<=n);memcpy(buf,udp,size);udp[0]=0;return (ssize_t)size;
}
#define close fake_close
#define recvfrom fake_recvfrom
static int ble_hs_adv_parse_fields(struct ble_hs_adv_fields *out,const uint8_t *p,unsigned n){(void)p;(void)n;*out=advertisement;return 0;}
static int ble_uuid_cmp(const int *a,const int *b){return *a-*b;}
static int ble_gap_disc_cancel(void){return 0;}
static int gap_event(void *ev,void *arg){(void)ev;(void)arg;return 0;}
static int ble_gap_connect(uint8_t type,const ble_addr_t *addr,int timeout,const struct ble_gap_conn_params *p,int(*cb)(void*,void*),void *arg){
 (void)type;(void)addr;(void)p;(void)cb;(void)arg;CHECK(timeout==10000);connects++;return 0;
}
static void reject(int rc){CHECK(rc==0);}
'''

tests = r'''
static void reset(void){
 settings.role=2;strcpy(settings.code,"123456");status.synced=false;
 now=1000;pair_started=now;pair_sock=42;pair_scanning=pair_editing=scan_only=true;
 pair_found=pair_failed=data_finished=connecting=false;connection=NONE;
 peer_info=(pico_dual_ble_peer_info_t){0};closed=stops=connects=prompts=0;
 udp[0]=0;draft_host[0]=draft_ap_ssid[0]=0;reject_until=0;rejected_count=0;
}
static void try_connect(void){
 static uint8_t mfr[]={0xfe,0xfe,'X','P',1,1,192,168,50,7,0};
 advertisement=(struct ble_hs_adv_fields){.mfg_data=mfr,.mfg_data_len=sizeof(mfr)};
 struct ble_gap_disc_desc disc={0};discover_peer(&disc);
}
static void udp_first(void){
 for(unsigned edit=0;edit<2;edit++)for(unsigned repeat=0;repeat<3;repeat++){
  reset();pair_editing=edit;strcpy(udp,"PICOPAIR2 192.168.50.7 -");
  pair_scan_update();
  CHECK(!pair_scanning&&!scan_only&&pair_sock==-1&&closed==1&&stops==1);
  CHECK(prompts==edit&&!strcmp(pair_ip,"192.168.50.7")&&!pair_ap_ssid[0]);
  if(edit)CHECK(!strcmp(draft_host,"192.168.50.7")&&!draft_ap_ssid[0]);
  try_connect();CHECK(connects==1&&connecting);
  pair_scan_update();CHECK(closed==1&&stops==1&&prompts==edit);
 }
}
static void ble_first(void){
 for(unsigned direct=0;direct<2;direct++){
  reset();peer_info.found=true;strcpy(peer_info.ip,direct?"192.168.4.1":"192.168.50.7");
  if(direct){strcpy(peer_info.ap_ssid,"PicoDual-ABCD");peer_info.direct=true;}
  pair_scan_update();CHECK(!scan_only&&!pair_scanning&&pair_sock==-1&&stops==1&&closed==1&&prompts==1);
  CHECK(!strcmp(draft_host,direct?"192.168.4.1":"192.168.50.7"));
  CHECK(!strcmp(draft_ap_ssid,direct?"PicoDual-ABCD":""));
  try_connect();CHECK(connects==1);
 }
}
static void guards(void){
 reset();try_connect();CHECK(!connects&&scan_only);
 reset();pair_scan_update();CHECK(scan_only&&pair_scanning&&!stops&&!closed&&!prompts);
 reset();strcpy(udp,"unrelated");pair_scan_update();CHECK(scan_only&&pair_scanning&&!prompts);
 reset();now+=30001;pair_scan_update();CHECK(!scan_only&&!pair_scanning&&pair_failed&&closed==1&&stops==1);
 reset();pair_stop();pair_stop();CHECK(!scan_only&&!pair_scanning&&closed==1&&stops==1);
}
int main(void){udp_first();ble_first();guards();printf("Discovery handoff: %u checks PASS (production UDP/BLE completion and connect gate; no physical radio)\n",checks);}
'''

p = argparse.ArgumentParser()
p.add_argument('--sanitize', action='store_true')
p.add_argument('--pair-source', type=Path, default=ROOT/'main/system/pico_dual.c')
a = p.parse_args()
pair = a.pair_source.read_text()
ble = (ROOT/'components/read_pico_ble_pager/read_pico_dual_ble.c').read_text()
functions = [extract(ble, 'pico_dual_ble_scan_stop'), extract(ble, 'discover_peer'),
             extract(pair, 'pair_stop'), extract(pair, 'pair_scan_update')]
with tempfile.TemporaryDirectory(prefix='dual-discovery-handoff-') as folder:
    path = Path(folder)
    (path/'test.c').write_text(prelude + '\n' + '\n'.join(functions) + '\n' + tests)
    flags = ['-std=c11', '-Wall', '-Wextra', '-Werror', '-Wno-unused-function', '-g', '-O1']
    if a.sanitize:
        flags += ['-fsanitize=address,undefined', '-fno-omit-frame-pointer']
    subprocess.run(['cc', *flags, str(path/'test.c'), '-o', str(path/'test')], check=True)
    subprocess.run([str(path/'test')], check=True)

#!/usr/bin/env python3
"""真实UI/HTTP租约函数的并发与取消回归；不访问设备或用户文件。
Exercise actual UI/HTTP lease functions concurrently, without devices or user files.
"""
from pathlib import Path
import argparse
import os
import re
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
SOURCE = (ROOT / "main/system/pico_system.c").read_text()


def function(name):
    match = re.search(r"(?:^|\n)((?:static\s+)?[\w\s*]+?\b" + re.escape(name) + r"\([^;]*?\)\s*\{)", SOURCE)
    assert match, name
    start, at, depth, quote = match.start(1), match.end() - 1, 0, None
    while at < len(SOURCE):
        char = SOURCE[at]
        if quote:
            if char == "\\":
                at += 2
                continue
            if char == quote:
                quote = None
        elif SOURCE.startswith("//", at):
            at = SOURCE.index("\n", at)
            continue
        elif SOURCE.startswith("/*", at):
            at = SOURCE.index("*/", at) + 2
            continue
        elif char in "\"'":
            quote = char
        elif char == "{":
            depth += 1
        elif char == "}":
            depth -= 1
            if not depth:
                return SOURCE[start:at + 1]
        at += 1
    raise AssertionError(name)


PRELUDE = r'''
#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdatomic.h>
#include <string.h>
#include <time.h>
#define ESP_OK 0
#define PICO_STORE_COLD
#define pdMS_TO_TICKS(ms) (ms)
typedef int esp_err_t;
typedef struct {bool mounted,present,exported;uint32_t media_generation;} read_pico_sd_info_t;
static read_pico_sd_info_t card;
static bool s_external_active,s_external_was_lost,s_store_lost,s_store_is_sd,s_transfer_prepared;
static uint32_t s_external_generation;
static char s_store_root[160],s_toast[160];
static atomic_uint s_content_state,delays;
static bool s_content_owned,s_content_refresh;
enum {CONTENT_IDLE,CONTENT_REQUEST,CONTENT_GRANTED,CONTENT_RELEASED,CONTENT_CHANGED};
static const int app_book=1;
static const int *s_global_app;
static pthread_t ui_thread;
static unsigned checks,saves,books,games,suspends,resumes,lost,notifications,rescans,config_saves,probes;
static bool input_free,usb_held,storage_busy,fail_reader,fail_book,fail_game,fail_font;
static bool change_during_prepare,late_grant,cancel_during_prepare,cancel_during_bulk,export_during_bulk;
static unsigned weread_leases;
static void content_cancel(void);
#define CHECK(x) do{checks++;if(!(x)){fprintf(stderr,"content lease failure line %d: %s\n",__LINE__,#x);assert(x);}}while(0)
static void ui_only(void){CHECK(pthread_equal(pthread_self(),ui_thread));}
static bool ps_weread_pause(void){ui_only();weread_leases++;return true;}
static void ps_weread_resume(void){ui_only();CHECK(weread_leases>0);weread_leases--;}
static bool ps_storage_busy(void){return storage_busy;}
static bool ps_usb_held(void){return usb_held;}
static bool ps_background_io_allowed(void){return input_free&&!s_external_active&&!usb_held&&!storage_busy&&!s_transfer_prepared;}
static bool ps_reader_power_save(void){ui_only();CHECK(!s_external_active);saves++;return !fail_reader;}
static bool app_book_power_prepare(void){ui_only();CHECK(!s_external_active);books++;return !fail_book;}
static bool ps_games_prepare_storage(void){ui_only();CHECK(!s_external_active);games++;return !fail_game;}
static int ttf_font_suspend_sd(bool suspend){ui_only();suspends+=suspend;if(change_during_prepare&&suspend)card.media_generation++;if(cancel_during_prepare&&suspend)content_cancel();return suspend&&fail_font?-1:0;}
static int read_pico_sd_get_info(read_pico_sd_info_t *out){ui_only();probes++;*out=card;return 0;}
static void display_set_bulk_io(bool active){ui_only();if(active&&cancel_during_bulk)content_cancel();if(active&&export_during_bulk)card.exported=true;}
static void ps_reader_media_lost(void){ui_only();lost++;}
static void ps_storage_resume(void){ui_only();resumes++;ps_weread_resume();}
static void book_store_notify_changed(void){ui_only();notifications++;}
static void ps_games_rescan(void){ui_only();rescans++;}
static void ps_desktop_usb_reclaimed(void){ui_only();}
static bool ps_storage_rebind(bool format){ui_only();(void)format;return true;}
static void save_config(void){ui_only();CHECK(!s_external_active);config_saves++;}
static void vTaskDelay(unsigned ms){
    assert(ms==10);unsigned n=atomic_fetch_add(&delays,1)+1;
    if(late_grant&&n==300)atomic_store(&s_content_state,CONTENT_GRANTED);
    struct timespec pause={0,500000};nanosleep(&pause,NULL);
}
'''

CHECKS = r'''
static void fixture(void){
    CHECK(!weread_leases);
    ui_thread=pthread_self();card=(read_pico_sd_info_t){true,true,false,7};
    s_external_active=s_external_was_lost=s_store_lost=s_transfer_prepared=s_content_owned=s_content_refresh=false;
    s_store_is_sd=input_free=true;s_external_generation=0;strcpy(s_store_root,"private-test-only");s_toast[0]=0;
    s_global_app=NULL;usb_held=storage_busy=fail_reader=fail_book=fail_game=fail_font=change_during_prepare=late_grant=false;
    cancel_during_prepare=cancel_during_bulk=export_during_bulk=false;
    saves=books=games=suspends=resumes=lost=notifications=rescans=config_saves=probes=0;
    atomic_store(&s_content_state,CONTENT_IDLE);atomic_store(&delays,0);
}
static void *acquire_worker(void *out){*(bool *)out=content_acquire();return NULL;}
static pthread_t acquire(bool *out){pthread_t t;*out=false;CHECK(!pthread_create(&t,NULL,acquire_worker,out));return t;}
static void pending(void){
    for(unsigned i=0;i<1000&&atomic_load(&s_content_state)!=CONTENT_REQUEST;i++){struct timespec p={0,100000};nanosleep(&p,NULL);}
    CHECK(atomic_load(&s_content_state)==CONTENT_REQUEST);
}
static void join(pthread_t thread){CHECK(!pthread_join(thread,NULL));}
static void lifecycle(bool changed){
    fixture();s_global_app=&app_book;bool result;pthread_t t=acquire(&result);pending();
    input_free=false;CHECK(!content_tick()&&!s_external_active&&!s_content_owned&&!saves&&!books&&!games&&!probes);
    input_free=true;CHECK(!content_tick());join(t);
    CHECK(result&&ps_content_held()&&s_external_active&&atomic_load(&s_content_state)==CONTENT_GRANTED);
    CHECK(saves==1&&books==1&&games==1&&suspends==1&&!resumes&&!lost&&!notifications&&!rescans);
    CHECK(!ps_storage_save_allowed());
    // 取消仅撤回尚未授予请求，已授权HTTP结束之前不得恢复本机文件访问。
    // Cancellation revokes pending requests only; an active HTTP handler retains ownership until release.
    content_cancel();CHECK(s_external_active&&ps_content_held()&&atomic_load(&s_content_state)==CONTENT_GRANTED);
    CHECK(!content_tick()&&!resumes);content_release(changed);
    CHECK(s_external_active&&ps_content_held());bool refresh=content_tick();
    CHECK(refresh==changed&&!s_external_active&&!ps_content_held()&&ps_storage_save_allowed());
    CHECK(resumes==1&&lost==(unsigned)changed&&notifications==(unsigned)changed&&rescans==(unsigned)changed&&config_saves==1);
    CHECK(!weread_leases);
    CHECK(atomic_load(&s_content_state)==CONTENT_IDLE&&!content_tick());
}
static void cancellation(void){
    fixture();bool result;pthread_t t=acquire(&result);pending();content_cancel();join(t);
    CHECK(!result&&!content_tick()&&!s_external_active&&!ps_content_held()&&!saves&&!games&&!probes);
    // 等候中的HTTP被UI停服务取消后，join可以结束，不需要UI再授予一次。
    // Cancelling before server join lets the waiting HTTP return without another UI grant.
    t=acquire(&result);pending();content_cancel();join(t);CHECK(!result&&!s_external_active);
    t=acquire(&result);pending();content_tick();join(t);CHECK(result);content_release(false);content_tick();
    // UI准备期间取消不能成为迟到授权，且归还前仍保持存储门禁。
    // Cancellation during UI preparation cannot turn into a late grant or bypass the storage gate.
    fixture();cancel_during_prepare=true;t=acquire(&result);pending();CHECK(!content_tick());join(t);
    CHECK(!result&&!ps_content_held()&&!s_external_active&&resumes==1&&config_saves==1&&!notifications);
    CHECK(atomic_load(&s_content_state)==CONTENT_IDLE&&ps_storage_save_allowed());
    fixture();cancel_during_bulk=export_during_bulk=true;t=acquire(&result);pending();CHECK(!content_tick());join(t);
    CHECK(!result&&ps_content_held()&&s_external_active&&!resumes&&!ps_storage_save_allowed());
    CHECK(atomic_load(&s_content_state)==CONTENT_RELEASED&&!content_tick());
    card.exported=false;CHECK(!content_tick()&&!ps_content_held()&&!s_external_active&&resumes==1&&config_saves==1);
    CHECK(atomic_load(&s_content_state)==CONTENT_IDLE&&ps_storage_save_allowed());
}
static void failures(void){
    for(int failure=0;failure<7;failure++){
        fixture();bool result;s_global_app=&app_book;
        if(failure==0)card.mounted=false;if(failure==1)fail_book=true;if(failure==2)fail_reader=true;
        if(failure==3)fail_game=true;if(failure==4)fail_font=true;if(failure==5)change_during_prepare=true;
        if(failure==6)card.present=false;
        pthread_t t=acquire(&result);pending();CHECK(!content_tick());join(t);
        CHECK(!result&&!s_external_active&&!ps_content_held()&&!resumes&&atomic_load(&s_content_state)==CONTENT_IDLE);
        if(failure==5)CHECK(s_store_lost&&!ps_storage_save_allowed());
    }
    fixture();bool result;input_free=false;pthread_t t=acquire(&result);pending();join(t);
    CHECK(!result&&atomic_load(&delays)==300&&atomic_load(&s_content_state)==CONTENT_IDLE&&!saves&&!probes);
    fixture();s_external_active=true;t=acquire(&result);pending();CHECK(!content_tick());join(t);
    CHECK(!result&&s_external_active&&!saves&&!probes);
    fixture();late_grant=true;CHECK(content_acquire());CHECK(atomic_load(&delays)==300&&atomic_load(&s_content_state)==CONTENT_GRANTED);
}
static void media_and_visuals(void){
    fixture();bool result;pthread_t t=acquire(&result);pending();content_tick();join(t);CHECK(result);
    s_content_refresh=true;content_release(false);CHECK(content_tick()&&!s_external_active&&!ps_content_held()&&!notifications);
    fixture();t=acquire(&result);pending();content_tick();join(t);CHECK(result);
    card.media_generation++;content_release(true);CHECK(content_tick());
    CHECK(s_store_lost&&!config_saves&&notifications==1&&rescans==1&&!s_external_active);
    fixture();t=acquire(&result);pending();content_tick();join(t);CHECK(result);
    card.exported=true;content_release(false);CHECK(!content_tick()&&s_external_active&&ps_content_held()&&!resumes);
    card.exported=false;CHECK(!content_tick()&&!s_external_active&&!ps_content_held()&&resumes==1);
    fixture();t=acquire(&result);pending();content_tick();join(t);CHECK(result);
    card.present=card.mounted=false;content_release(true);CHECK(content_tick()&&!s_external_active&&!ps_content_held()&&s_store_lost&&!config_saves);
}
int main(void){lifecycle(false);lifecycle(true);cancellation();failures();media_and_visuals();printf("content UI handoff: %u checks PASS; real atomic acquire/grant/release/cancel, UI-only file transitions, bounded HTTP wait, save failures, timeout/grant race, unchanged reads, visual debt, changed/replaced/exported/absent cards; no hardware or user files.\n",checks);return 0;}
'''


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--sanitize', action='store_true')
    args = parser.parse_args()
    names = ['ps_storage_save_allowed', 'ps_external_finished', 'ps_content_held', 'content_acquire', 'content_release', 'content_cancel', 'content_tick']
    with tempfile.TemporaryDirectory(prefix='pico-content-lease-') as directory:
        source, binary = Path(directory) / 'handoff.c', Path(directory) / 'handoff'
        source.write_text(PRELUDE + '\n'.join(function(name) for name in names) + CHECKS)
        flags = ['-std=c11', '-O1', '-g', '-Wall', '-Wextra', '-Werror', '-pthread']
        if args.sanitize:
            flags += ['-fsanitize=address,undefined', '-fno-omit-frame-pointer']
        subprocess.run([os.environ.get('CC', 'cc'), *flags, *shlex.split(os.environ.get('CFLAGS', '')), str(source), '-o', str(binary)], check=True)
        subprocess.run([str(binary)], check=True)


if __name__ == '__main__':
    main()

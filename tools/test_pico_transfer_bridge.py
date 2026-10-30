#!/usr/bin/env python3
"""Exercise source-extracted transfer/storage ownership and routing; no hardware or file I/O.
提取真实传书/存储所有权与导航函数测试；不连接硬件，不操作文件或卡片。
"""
from pathlib import Path
import os
import re
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def function(source, name):
    match = re.search(r"(?:^|\n)((?:static\s+)?[\w\s*]+?\b" + re.escape(name) + r"\([^;]*?\)\s*\{)", source)
    if not match:
        raise RuntimeError("Missing production function: " + name)
    start, at, depth, quote = match.start(1), match.end() - 1, 0, None
    while at < len(source):
        char = source[at]
        if quote:
            if char == "\\":
                at += 2
                continue
            if char == quote:
                quote = None
        elif source.startswith("//", at):
            at = source.index("\n", at)
            continue
        elif source.startswith("/*", at):
            at = source.index("*/", at) + 2
            continue
        elif char in "\"'":
            quote = char
        elif char == "{":
            depth += 1
        elif char == "}":
            depth -= 1
            if depth == 0:
                return source[start:at + 1]
        at += 1
    raise RuntimeError("Unbalanced production function: " + name)


PRELUDE = r'''
#define _POSIX_C_SOURCE 200809L
#include "pico_connectivity.h"
#include "read_pico_connectivity.h"
#include "read_pico_sd.h"
#include "book_store.h"
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#define PICO_STORE_COLD
#define ARRAY_COUNT(a) ((int)(sizeof(a)/sizeof((a)[0])))
#define ESP_LOGW(...) ((void)0)
enum { EPD_ROT_INVERTED_PORTRAIT=3 };
enum { CC_CONFIRM_NONE, CC_CONFIRM_FORGET, CC_CONFIRM_LEAVE };
enum { CC_LEAVE_BACK, CC_LEAVE_ROUTE, CC_LEAVE_BOOKS };
enum { CC_LIST, CC_DETAIL };
enum { FONT_ON=1, FONT_OFF, ROOT, FREE, LIMIT, BULK_ON, BULK_OFF, NOTIFY, CONFIG,
       QUIESCE, QR_CLEAR, READER_LOST, NETWORK_RESUME, UI_MEDIA_LOST,
       READER_EXIT, HOME_EXIT, GAMES_EXIT, SENSOR_SLEEP, ROTATION, ENTER,
       SD_INFO, REBIND, STORAGE_IO, READER_SAVE, READING_SNAPSHOT };
static int events[512], event_count, checks;
#define CHECK(x) do { checks++; if(!(x)){fprintf(stderr,"FAIL line%d: %s\n",__LINE__,#x);return 1;} } while(0)
static void event(int e) { assert(event_count < ARRAY_COUNT(events)); events[event_count++]=e; }
static int first(int e) { for(int i=0;i<event_count;i++)if(events[i]==e)return i;return -1; }
static int occurrences(int e) { int n=0;for(int i=0;i<event_count;i++)if(events[i]==e)n++;return n; }
static bool s_store_is_sd, s_store_lost, s_transfer_prepared, s_tracking, s_fan, s_fan_candidate, s_dirty;
static bool s_network_resume_pending,s_external_active,s_external_was_lost;
static uint32_t s_external_generation;
static bool s_ready,s_store_media_known,test_usb_held;
static uint32_t s_store_media_generation;
static char s_toast[128],s_store_root[160];
static const app_desc_t *s_legacy,*s_global_app;
static app_ctx_t *s_ctx;
static bool s_book_return,s_real_book_preferred;
static int s_book_return_depth,s_global_redraw,s_quick_mode;
static struct {bool active;} s_quick_edit;
enum {BOOK_GLOBAL_FONT,BOOK_GLOBAL_BACK};
static unsigned weread_pauses,weread_resumes,weread_cancels,weread_leases;
static bool weread_busy,weread_blocked;
static bool snapshot_ok,reader_save_ok,games_prepare_ok;
static bool ps_weread_pause(void){weread_pauses++;if(weread_busy)return false;weread_leases++;weread_blocked=true;return true;}
static void ps_weread_resume(void){weread_resumes++;assert(weread_leases);weread_leases--;weread_blocked=weread_leases>0;}
static void ps_weread_cancel(void){weread_cancels++;}
bool ps_games_prepare_storage(void){return games_prepare_ok;}
void ps_games_rescan(void){}
void ps_games_media_lost(void){}
bool ps_reader_power_save(void){event(READER_SAVE);return reader_save_ok;}
static bool reading_snapshot_prepare(void){event(READING_SNAPSHOT);return snapshot_ok;}
static void ps_desktop_usb_reclaimed(void){}
static void checkpoint_invalidate(void){}
static void ps_sound_error(void){}
void ps_global_cancel_input(void){s_fan_candidate=false;}
static void quick_edit_contact_cancel(void){}
static void global_depart(void){}
static bool resume_real_book(void){return false;}
static bool ps_power_back(void){return false;}
static bool ps_navigation_back(void){return false;}
static void ps_power_preview(int value){(void)value;}
static int app_book_global_action(app_ctx_t *ctx,int action){(void)ctx;(void)action;return 0;}
bool ps_usb_background(void){return false;}
bool ps_usb_held(void){return test_usb_held;}
bool ps_usb_local_route(ps_route_t route){return route==PS_DEVICE;}
static bool local_hold(void){return false;}
static bool local_before_leave(ps_route_t route){return ps_usb_before_leave(route);}
static ps_route_t s_route, s_history[10], s_workspace[2], s_sleep_origin;
static int s_depth, s_section, s_panel, s_group, s_device_page;
static struct { unsigned onboard_step; } s_config;
static book_store_root_t s_transfer_root;
static uint64_t s_transfer_free;
static size_t s_transfer_limit;
static read_pico_connectivity_status_t cc_status, next_status;
static bool cc_stopping, cc_session, cc_stop_failed;
static unsigned cc_changed;
static int cc_confirm, cc_leave_kind, cc_view;
static ps_route_t cc_destination, cc_page;
static bool storage_busy, font_suspended, bulk, joined, root_flash, quiesce_keeps_active;
static int font_error, root_error, quiesce_failures, resume_error;
static unsigned config_writes;
static uint64_t fixture_free;
static size_t fixture_limit;
static read_pico_sd_info_t sd_infos[2];
static esp_err_t sd_errors[2];
static int sd_reads,ram_migrations;
const app_desc_t app_transfer={0}, app_book={0};
bool ps_storage_busy(void) { return storage_busy; }
bool ps_usb_before_leave(ps_route_t r) { (void)r;return s_external_active; }
bool ps_usb_back(void) { return s_external_active; }
esp_err_t ttf_font_suspend_sd(bool on) { event(on?FONT_ON:FONT_OFF);font_suspended=on;return on?font_error:ESP_OK; }
esp_err_t book_store_upload_root(book_store_root_t *out) {
    event(ROOT);if(root_error)return root_error;
    *out=(book_store_root_t){.is_flash=root_flash};
    snprintf(out->path,sizeof(out->path),"%s",root_flash?"/flash/books":"/sdcard/books");return ESP_OK;
}
uint64_t book_store_free_bytes(const book_store_root_t *root) { assert(root==&s_transfer_root);event(FREE);return fixture_free; }
size_t book_store_file_limit(const book_store_root_t *root) { assert(root==&s_transfer_root);event(LIMIT);return fixture_limit; }
esp_err_t book_progress_forget(const char *path) { (void)path;return ESP_OK; }
void display_set_bulk_io(bool on) { if(!on)assert(joined);bulk=on;event(on?BULK_ON:BULK_OFF); }
void book_store_notify_changed(void) { assert(joined);event(NOTIFY); }
static void save_config(void) { event(CONFIG);if(!s_store_lost&&!storage_busy&&!s_transfer_prepared&&!s_external_active){assert(joined);config_writes++;} }
esp_err_t read_pico_sd_get_info(read_pico_sd_info_t *out) {
    event(SD_INFO);int index=sd_reads++?1:0;*out=sd_infos[index];return sd_errors[index];
}
bool ps_storage_rebind(bool formatted) {
    assert(!formatted&&!s_external_active&&joined);event(REBIND);ram_migrations++;return true;
}
typedef int nvs_handle_t;
enum { NVS_READONLY,NVS_READWRITE };
static esp_err_t nvs_open(const char *space,int mode,nvs_handle_t *out) { (void)space;(void)mode;(void)out;event(STORAGE_IO);return ESP_FAIL; }
static esp_err_t nvs_get_blob(nvs_handle_t h,const char *key,void *out,size_t *size) { (void)h;(void)key;(void)out;(void)size;event(STORAGE_IO);return ESP_FAIL; }
static esp_err_t nvs_set_blob(nvs_handle_t h,const char *key,const void *in,size_t size) { (void)h;(void)key;(void)in;(void)size;event(STORAGE_IO);return ESP_FAIL; }
static esp_err_t nvs_commit(nvs_handle_t h) { (void)h;event(STORAGE_IO);return ESP_FAIL; }
static void nvs_close(nvs_handle_t h) { (void)h;event(STORAGE_IO); }
static bool store_file(const char *root,const char *key,const void *data,size_t size){(void)root;(void)key;(void)data;(void)size;event(STORAGE_IO);return false;}
static bool load_file(const char *path,void *out,size_t size) { (void)path;(void)out;(void)size;event(STORAGE_IO);return false; }
void read_pico_connectivity_get_status(read_pico_connectivity_status_t *out) { *out=next_status; }
esp_err_t read_pico_connectivity_quiesce(uint32_t timeout) {
    assert(timeout==15000);event(QUIESCE);
    if(quiesce_failures){quiesce_failures--;return ESP_ERR_TIMEOUT;}
    if(!quiesce_keeps_active){next_status.transfer_active=false;next_status.busy=false;next_status.suspended=true;joined=true;}
    return ESP_OK;
}
esp_err_t read_pico_connectivity_resume(void) { event(NETWORK_RESUME);if(!resume_error)next_status.suspended=false;return resume_error; }
static void cc_clear_qr(void) { event(QR_CLEAR); }
void ps_reader_media_lost(void) { assert(!s_transfer_prepared);event(READER_LOST); }
void ps_connectivity_media_lost(void) { assert(joined);event(UI_MEDIA_LOST); }
void ps_reader_enter(ps_route_t route) { assert(route==PS_ROUTE_COUNT&&joined);event(READER_EXIT); }
void ps_home_enter(ps_route_t route) { assert(route==PS_ROUTE_COUNT&&joined);event(HOME_EXIT); }
void ps_games_enter(ps_route_t route) { assert(route==PS_ROUTE_COUNT&&joined);event(GAMES_EXIT); }
void read_pico_sensor_sleep(void *sensor) { (void)sensor;assert(joined);event(SENSOR_SLEEP); }
void epd_set_rotation(int rotation) { assert(rotation==EPD_ROT_INVERTED_PORTRAIT);event(ROTATION); }
ps_route_t ps_route(void) { return s_route; }
static void enter(ps_route_t route) { event(ENTER);s_route=route; }
void ps_toast(const char *message) { snprintf(s_toast,sizeof(s_toast),"%s",message); }
void ps_wake(void) { enter(s_sleep_origin); }
static bool reader_route(ps_route_t route) { return route==PS_READ; }
static bool home_route(ps_route_t route) { return route==PS_HA; }
bool ps_reader_key(int key) { (void)key;return false; }
bool ps_home_key(int key) { (void)key;return false; }
bool ps_games_key(int key) { (void)key;return false; }
bool ps_storage_back(void) { return false; }
'''

TESTS = r'''
static void reset(void) {
    s_ready=true;s_store_media_known=false;s_store_media_generation=0;test_usb_held=true;
    s_store_is_sd=s_store_lost=s_transfer_prepared=s_tracking=s_fan=s_fan_candidate=s_dirty=s_network_resume_pending=false;
    s_external_active=s_external_was_lost=false;s_external_generation=0;s_store_root[0]=0;
    s_route=PS_SHELF;s_workspace[0]=PS_SHELF;s_workspace[1]=PS_HA;
    s_depth=s_section=s_panel=s_group=0;s_config.onboard_step=0;s_legacy=NULL;s_toast[0]=0;
    memset(s_history,0,sizeof(s_history));memset(&s_transfer_root,0,sizeof(s_transfer_root));
    s_transfer_free=s_transfer_limit=0;storage_busy=font_suspended=bulk=joined=root_flash=quiesce_keeps_active=false;
    font_error=root_error=quiesce_failures=resume_error=0;config_writes=0;fixture_free=12345678;fixture_limit=SIZE_MAX;
    memset(&cc_status,0,sizeof(cc_status));memset(&next_status,0,sizeof(next_status));
    cc_stopping=cc_session=cc_stop_failed=false;cc_changed=0;cc_confirm=CC_CONFIRM_NONE;cc_view=CC_LIST;cc_page=PS_TRANSFER;
    sd_infos[0]=sd_infos[1]=(read_pico_sd_info_t){.present=true,.mounted=true,.media_generation=11};
    sd_errors[0]=sd_errors[1]=ESP_OK;sd_reads=ram_migrations=0;
    event_count=0;weread_pauses=weread_resumes=weread_cancels=weread_leases=0;weread_busy=weread_blocked=false;
    snapshot_ok=reader_save_ok=games_prepare_ok=true;s_global_app=NULL;s_ctx=NULL;
    s_book_return=s_real_book_preferred=s_quick_edit.active=false;s_book_return_depth=s_global_redraw=s_quick_mode=0;
}
static void active_transfer(void) {
    s_route=PS_TRANSFER;s_store_is_sd=true;
    cc_status.transfer_active=next_status.transfer_active=true;
    cc_status.busy=next_status.busy=true;
    next_status.transfer.changed_count=3;joined=false;
}
int main(void) {
    read_pico_transfer_cfg_t cfg={0}, sentinel;
    reset();CHECK(ps_transfer_prepare(NULL)==ESP_ERR_INVALID_ARG);CHECK(event_count==0);
    memset(&sentinel,0x5a,sizeof(sentinel));cfg=sentinel;
    storage_busy=true;CHECK(ps_transfer_prepare(&cfg)==ESP_ERR_INVALID_STATE);CHECK(!memcmp(&cfg,&sentinel,sizeof(cfg)));CHECK(event_count==0);
    storage_busy=false;s_transfer_prepared=true;CHECK(ps_transfer_prepare(&cfg)==ESP_ERR_INVALID_STATE);CHECK(event_count==0);
    reset();weread_busy=true;cfg=sentinel;
    CHECK(ps_transfer_prepare(&cfg)==ESP_ERR_INVALID_STATE);CHECK(!memcmp(&cfg,&sentinel,sizeof(cfg))&&event_count==0&&!weread_blocked);
    CHECK(!ps_storage_prepare()&&event_count==0&&!weread_blocked);
    CHECK(!ps_external_prepare()&&occurrences(SD_INFO)==1&&occurrences(QUIESCE)==0&&!weread_blocked);
    reset();games_prepare_ok=false;CHECK(ps_transfer_prepare(&cfg)==ESP_ERR_INVALID_STATE&&!weread_blocked&&event_count==0);
    CHECK(!ps_storage_prepare()&&!weread_blocked&&event_count==0);
    reset();font_error=ESP_FAIL;CHECK(ps_transfer_prepare(&cfg)==ESP_FAIL);CHECK(!ps_transfer_prepared()&&!font_suspended&&!bulk);CHECK(occurrences(ROOT)==0);CHECK(first(FONT_ON)<first(FONT_OFF));CHECK(!weread_blocked);
    reset();root_error=ESP_ERR_NOT_FOUND;CHECK(ps_transfer_prepare(&cfg)==ESP_ERR_NOT_FOUND);CHECK(!ps_transfer_prepared()&&!font_suspended&&!bulk);CHECK(occurrences(FREE)==0&&!weread_blocked);
    reset();CHECK(ps_transfer_prepare(&cfg)==ESP_OK);CHECK(ps_transfer_prepared()&&font_suspended&&bulk&&weread_blocked);
    CHECK(first(FONT_ON)<first(ROOT)&&first(ROOT)<first(FREE)&&first(FREE)<first(BULK_ON));
    CHECK(!strcmp(cfg.root_dir,"/sdcard/books")&&!strcmp(cfg.font_dir,"/sdcard/fonts")&&!cfg.is_flash);
    CHECK(cfg.free_bytes_ctx==&s_transfer_root&&cfg.file_changed_cb==book_progress_forget);
    uint64_t bytes=0;size_t limit=0;bool flash=true;int free_reads=occurrences(FREE);
    fixture_free=555;ps_transfer_capacity(&bytes,&flash,&limit);CHECK(bytes==12345678&&!flash&&limit==SIZE_MAX);CHECK(occurrences(FREE)==free_reads);
    CHECK(cfg.free_bytes_cb(cfg.free_bytes_ctx)==555);CHECK(occurrences(FREE)==free_reads+1);
    active_transfer();event_count=0;quiesce_failures=1;
    CHECK(!ps_connectivity_shutdown());CHECK(ps_transfer_prepared()&&font_suspended&&bulk);
    CHECK(!strcmp(s_transfer_root.path,"/sdcard/books"));CHECK(occurrences(FONT_OFF)==0&&occurrences(CONFIG)==0&&occurrences(NOTIFY)==0);
    quiesce_keeps_active=true;CHECK(!ps_connectivity_shutdown());CHECK(ps_transfer_prepared()&&font_suspended&&bulk);
    quiesce_keeps_active=false;event_count=0;CHECK(ps_connectivity_shutdown());CHECK(!ps_transfer_prepared()&&!font_suspended&&!bulk&&joined);
    CHECK(first(QUIESCE)<first(BULK_OFF)&&first(BULK_OFF)<first(FONT_OFF)&&first(FONT_OFF)<first(NOTIFY));
    CHECK(config_writes==1&&occurrences(NOTIFY)==1&&occurrences(CONFIG)==1&&!weread_blocked);CHECK(!s_transfer_root.path[0]);
    ps_transfer_capacity(&bytes,&flash,&limit);CHECK(bytes==0&&!flash&&limit==0);
    event_count=0;ps_transfer_finished(8);CHECK(event_count==0);
    reset();root_flash=true;fixture_limit=1048576;fixture_free=0;
    CHECK(ps_transfer_prepare(&cfg)==ESP_OK);CHECK(cfg.is_flash&&!cfg.font_dir&&cfg.file_limit==1048576);
    CHECK(ps_transfer_prepared()&&font_suspended&&bulk); // 满卡仍可开启管理并删除图书。/ A full mounted card can still serve file management.
    active_transfer();storage_busy=true;quiesce_failures=1;event_count=0;
    CHECK(!ps_storage_prepare());CHECK(s_network_resume_pending);CHECK(ps_transfer_prepared()&&font_suspended&&bulk);CHECK(occurrences(READER_LOST)==0&&occurrences(FONT_OFF)==0);
    event_count=0;CHECK(ps_storage_prepare());CHECK(!s_network_resume_pending);CHECK(joined&&!ps_transfer_prepared()&&font_suspended&&!bulk&&s_store_lost);
    CHECK(first(QUIESCE)<first(READER_LOST)&&first(READER_LOST)<first(FONT_ON));CHECK(config_writes==0);
    s_network_resume_pending=true;ps_storage_resume();CHECK(!s_network_resume_pending);CHECK(!font_suspended&&first(FONT_OFF)<first(NETWORK_RESUME));
    reset();s_store_is_sd=true;font_error=ESP_FAIL;CHECK(!ps_storage_prepare());CHECK(!font_suspended&&!s_store_lost);CHECK(occurrences(NETWORK_RESUME)==1);
    reset();s_store_is_sd=true;s_store_lost=true;font_error=ESP_FAIL;CHECK(!ps_storage_prepare());CHECK(s_store_lost);
    reset();weread_busy=true;joined=true;app_ctx_t busy_context={.sensor_ready=true};system_exit(&busy_context);
    CHECK(occurrences(QUIESCE)==0&&!weread_blocked&&weread_resumes==0&&occurrences(READER_EXIT)==1&&occurrences(SENSOR_SLEEP)==1);
    reset();CHECK(ps_transfer_prepare(&cfg)==ESP_OK);active_transfer();event_count=0;quiesce_failures=2;
    app_ctx_t context={.sensor_ready=true};s_network_resume_pending=true;system_exit(&context);CHECK(!s_network_resume_pending);
    CHECK(occurrences(QUIESCE)==3&&first(QUIESCE)<first(FONT_OFF)&&first(FONT_OFF)<first(READER_EXIT));CHECK(!weread_leases);
    CHECK(first(READER_EXIT)<first(HOME_EXIT)&&first(HOME_EXIT)<first(GAMES_EXIT)&&first(GAMES_EXIT)<first(SENSOR_SLEEP));
    reset();CHECK(ps_transfer_prepare(&cfg)==ESP_OK);active_transfer();event_count=0;quiesce_failures=1;
    s_network_resume_pending=true;system_media_lost(&context);CHECK(!s_network_resume_pending);CHECK(s_store_lost&&!ps_transfer_prepared()&&!font_suspended&&!bulk);CHECK(config_writes==0);
    CHECK(occurrences(QUIESCE)==2&&first(QUIESCE)<first(UI_MEDIA_LOST)&&first(UI_MEDIA_LOST)<first(READER_LOST));
    CHECK(first(READER_LOST)<first(NETWORK_RESUME)&&!next_status.suspended&&!next_status.transfer_active);
    CHECK(occurrences(BULK_ON)==0); // 日常联网恢复不重新开放传书。/ Everyday networking must not reopen transfer.
    reset();CHECK(ps_transfer_prepare(&cfg)==ESP_OK);active_transfer();event_count=0;
    s_depth=1;s_history[0]=PS_DEVICE;ps_navigate(PS_STORAGE);
    CHECK(s_route==PS_TRANSFER&&s_depth==1&&cc_confirm==CC_CONFIRM_LEAVE&&cc_destination==PS_STORAGE&&occurrences(ENTER)==0);
    cc_confirm=CC_CONFIRM_NONE;ps_switch(2);CHECK(s_route==PS_TRANSFER&&s_section==0&&s_depth==1&&cc_confirm==CC_CONFIRM_LEAVE);
    cc_confirm=CC_CONFIRM_NONE;ps_back();CHECK(s_route==PS_TRANSFER&&s_depth==1&&cc_confirm==CC_CONFIRM_LEAVE);
    cc_stopping=true;ps_navigate(PS_SHELF);CHECK(s_route==PS_TRANSFER&&s_depth==1&&occurrences(ENTER)==0);
    ps_legacy(&app_book);CHECK(!s_legacy&&s_toast[0]);
    CHECK(ps_connectivity_shutdown());event_count=0;ps_navigate(PS_STORAGE);CHECK(s_route==PS_STORAGE&&s_depth==2&&occurrences(ENTER)==1);
    reset();ps_legacy(&app_transfer);CHECK(s_route==PS_TRANSFER&&!s_legacy);ps_legacy(&app_book);CHECK(s_legacy==&app_book);
    reset();s_network_resume_pending=true;next_status.suspended=true;resume_error=ESP_ERR_INVALID_STATE;
    tick_resume(true);CHECK(s_network_resume_pending&&next_status.suspended&&occurrences(NETWORK_RESUME)==0);
    tick_resume(false);CHECK(s_network_resume_pending&&next_status.suspended&&occurrences(NETWORK_RESUME)==1);
    tick_resume(false);CHECK(s_network_resume_pending&&next_status.suspended&&occurrences(NETWORK_RESUME)==2);
    resume_error=ESP_OK;tick_resume(false);CHECK(!s_network_resume_pending&&!next_status.suspended&&occurrences(NETWORK_RESUME)==3);
    tick_resume(false);CHECK(occurrences(NETWORK_RESUME)==3);
    // External prepare cannot overlap a transfer, format/verify task or existing lease.
    reset();s_external_active=true;CHECK(!ps_external_prepare()&&event_count==0);
    reset();storage_busy=true;CHECK(!ps_external_prepare()&&event_count==0);
    reset();s_transfer_prepared=true;CHECK(!ps_external_prepare()&&event_count==0);
    reset();sd_errors[0]=ESP_ERR_NOT_FOUND;CHECK(!ps_external_prepare());CHECK(occurrences(QUIESCE)==0);
    reset();sd_infos[0].mounted=false;CHECK(!ps_external_prepare());CHECK(occurrences(FONT_ON)==0);
    reset();sd_infos[0].exported=true;CHECK(!ps_external_prepare());CHECK(occurrences(QUIESCE)==0);
    reset();s_store_is_sd=true;quiesce_failures=1;
    CHECK(!ps_external_prepare());CHECK(!s_external_active&&!font_suspended&&!bulk&&!s_store_lost&&s_network_resume_pending);
    CHECK(occurrences(READER_LOST)==0&&occurrences(FONT_ON)==0&&occurrences(BULK_ON)==0);
    reset();reader_save_ok=false;CHECK(!ps_external_prepare());
    CHECK(!s_external_active&&!weread_blocked&&!font_suspended&&occurrences(NETWORK_RESUME)==1&&occurrences(READING_SNAPSHOT)==0);
    reset();snapshot_ok=false;CHECK(!ps_external_prepare());
    CHECK(!s_external_active&&!weread_blocked&&!font_suspended&&first(READER_SAVE)<first(READING_SNAPSHOT)&&occurrences(NETWORK_RESUME)==1);
    reset();s_store_is_sd=true;font_error=ESP_FAIL;
    CHECK(!ps_external_prepare());CHECK(!s_external_active&&!font_suspended&&!bulk&&!s_store_lost);
    CHECK(first(QUIESCE)<first(READER_LOST)&&first(READER_LOST)<first(FONT_ON)&&first(FONT_ON)<first(FONT_OFF)&&first(FONT_OFF)<first(NETWORK_RESUME));
    reset();s_store_is_sd=true;font_error=ESP_FAIL;resume_error=ESP_ERR_INVALID_STATE;
    CHECK(!ps_external_prepare()&&s_network_resume_pending&&!s_external_active);
    resume_error=ESP_OK;tick_resume(false);CHECK(!s_network_resume_pending);
    reset();s_store_is_sd=true;sd_infos[1].mounted=false;
    CHECK(!ps_external_prepare());CHECK(!s_external_active&&!font_suspended&&!bulk&&s_store_lost);
    CHECK(occurrences(SD_INFO)==2&&first(FONT_ON)<first(FONT_OFF)&&first(FONT_OFF)<first(NETWORK_RESUME)&&occurrences(BULK_ON)==0);
    reset();s_store_is_sd=true;sd_infos[1].media_generation++;
    CHECK(!ps_external_prepare()&&s_store_lost&&!s_external_active&&!font_suspended&&!bulk);
    reset();s_store_is_sd=false;sd_infos[1].exported=true;resume_error=ESP_ERR_INVALID_STATE;
    CHECK(!ps_external_prepare()&&!s_store_lost&&s_network_resume_pending);
    CHECK(!s_external_active&&!font_suspended&&!bulk&&ram_migrations==0);
    resume_error=ESP_OK;tick_resume(false);CHECK(!s_network_resume_pending);
    reset();s_store_is_sd=true;snprintf(s_store_root,sizeof(s_store_root),"/sdcard/.pico-system");
    CHECK(ps_external_prepare());CHECK(s_external_active&&font_suspended&&bulk&&s_store_lost&&joined);
    CHECK(s_external_generation==11&&!s_external_was_lost&&!s_network_resume_pending);CHECK(weread_leases==1&&weread_blocked);
    CHECK(first(QUIESCE)<first(READER_LOST)&&first(READER_LOST)<first(FONT_ON)&&first(FONT_ON)<first(BULK_ON));
    CHECK(occurrences(SD_INFO)==2&&occurrences(FONT_OFF)==0&&occurrences(NETWORK_RESUME)==0&&config_writes==0);
    // Even failed exported snapshots retain every barrier and do not touch local persistence.
    sd_infos[1].exported=true;sd_errors[1]=ESP_FAIL;event_count=0;
    CHECK(!ps_external_finished(true));CHECK(s_external_active&&font_suspended&&bulk&&s_store_lost&&weread_leases==1);
    CHECK(event_count==1&&events[0]==SD_INFO&&ram_migrations==0&&config_writes==0);
    // While external ownership remains, WiFi file transfer and format preparation are rejected.
    event_count=0;CHECK(ps_transfer_prepare(&cfg)==ESP_ERR_INVALID_STATE);CHECK(!ps_storage_prepare());CHECK(event_count==0);
    char value=0;CHECK(!ps_store("reader-draft-v1",&value,1)&&!ps_load("reader-draft-v1",&value,1));CHECK(occurrences(STORAGE_IO)==0);
    s_route=PS_USB_TRANSFER;s_depth=1;s_history[0]=PS_DEVICE;
    ps_navigate(PS_WIFI);CHECK(s_route==PS_USB_TRANSFER&&s_depth==1&&occurrences(ENTER)==0);
    ps_navigate(PS_STORAGE);CHECK(s_route==PS_USB_TRANSFER&&s_depth==1&&occurrences(ENTER)==0);
    ps_switch(0);CHECK(s_route==PS_USB_TRANSFER&&s_depth==1&&s_section==0);
    ps_back();CHECK(s_route==PS_USB_TRANSFER&&s_depth==1&&s_dirty);
    ps_legacy(&app_book);CHECK(!s_legacy&&s_toast[0]);
    ps_legacy(&app_transfer);CHECK(s_route==PS_USB_TRANSFER&&s_depth==1&&!s_legacy);
    event_count=0;system_media_lost(&context);CHECK(s_external_active&&event_count==0&&bulk&&font_suspended);
    sd_infos[1].exported=false;sd_errors[1]=ESP_OK;event_count=0;
    CHECK(ps_external_finished(true));CHECK(!s_external_active&&!font_suspended&&!bulk&&!s_store_lost&&!weread_leases);
    CHECK(first(SD_INFO)<first(BULK_OFF)&&first(BULK_OFF)<first(FONT_OFF)&&first(FONT_OFF)<first(NETWORK_RESUME)&&first(NETWORK_RESUME)<first(NOTIFY)&&first(NOTIFY)<first(CONFIG));
    CHECK(config_writes==1&&ram_migrations==0&&occurrences(NOTIFY)==1);
    event_count=0;CHECK(!ps_external_finished(true)&&event_count==0);
    // Swapped cards never receive old RAM notes, whether or not a root existed before.
    reset();s_store_is_sd=true;CHECK(ps_external_prepare());sd_infos[1].media_generation++;event_count=0;
    CHECK(ps_external_finished(true));CHECK(!s_external_active&&s_store_lost&&!font_suspended&&!bulk);
    CHECK(ram_migrations==0&&config_writes==0&&occurrences(REBIND)==0&&occurrences(CONFIG)==0&&occurrences(NOTIFY)==1);
    reset();s_store_is_sd=true;snprintf(s_store_root,sizeof(s_store_root),"/sdcard/.pico-system");
    CHECK(ps_external_prepare());sd_infos[1].media_generation++;event_count=0;
    CHECK(ps_external_finished(false)&&s_store_lost&&occurrences(NOTIFY)==1&&ram_migrations==0&&config_writes==0);
    // Same card with no prior root can bind once, only after the lease and font barriers release.
    reset();s_store_is_sd=true;CHECK(ps_external_prepare());event_count=0;
    CHECK(ps_external_finished(false));CHECK(ram_migrations==1&&first(FONT_OFF)<first(REBIND)&&first(NETWORK_RESUME)<first(REBIND));
    CHECK(occurrences(NOTIFY)==0&&config_writes==1);
    // An unavailable card releases USB ownership but keeps old SD persistence unavailable.
    reset();s_store_is_sd=true;CHECK(ps_external_prepare());sd_infos[1].mounted=false;sd_errors[1]=ESP_ERR_NOT_FOUND;event_count=0;
    CHECK(!ps_external_finished(true));CHECK(!s_external_active&&s_store_lost&&!bulk&&!font_suspended);
    CHECK(occurrences(NOTIFY)==1&&ram_migrations==0&&config_writes==0&&occurrences(CONFIG)==0);
    // A pre-existing lost session remains lost; a flash session is never marked as SD-lost.
    reset();s_store_is_sd=s_store_lost=true;snprintf(s_store_root,sizeof(s_store_root),"/sdcard/.pico-system");
    CHECK(ps_external_prepare()&&s_external_was_lost);CHECK(ps_external_finished(false)&&s_store_lost&&config_writes==0);
    reset();snprintf(s_store_root,sizeof(s_store_root),"/flash/books/.pico-system");CHECK(ps_external_prepare());
    sd_infos[1].media_generation++;CHECK(ps_external_finished(false)&&!s_store_lost&&config_writes==0&&ram_migrations==0);
    // Resume errors remain scheduled for the actual tick retry, including normal finish.
    reset();s_store_is_sd=true;snprintf(s_store_root,sizeof(s_store_root),"/sdcard/.pico-system");
    CHECK(ps_external_prepare());resume_error=ESP_ERR_INVALID_STATE;
    CHECK(ps_external_finished(false)&&!s_external_active&&s_network_resume_pending);
    tick_resume(false);CHECK(s_network_resume_pending);resume_error=ESP_OK;tick_resume(false);CHECK(!s_network_resume_pending);
    // USB UI仍持有租约时不消费恢复；归还后用同一生产入口恢复目录，且换卡仍禁写。
    // Restoration waits for the USB UI lease; the same production callback resumes catalogs afterward without writing a replacement card.
    reset();s_store_is_sd=s_store_media_known=true;s_store_media_generation=11;
    snprintf(s_store_root,sizeof(s_store_root),"/sdcard/.pico-system");
    CHECK(ps_external_prepare());event_count=0;
    ps_media_restored();CHECK(event_count==0&&s_store_lost&&font_suspended);
    CHECK(ps_external_finished(false));event_count=0;
    ps_media_restored();CHECK(event_count==0);
    test_usb_held=false;ps_media_restored();
    CHECK(!s_store_lost&&!font_suspended&&occurrences(NOTIFY)==1&&occurrences(CONFIG)==0);
    s_store_lost=true;sd_infos[1].media_generation++;event_count=0;
    ps_media_restored();CHECK(s_store_lost&&occurrences(NOTIFY)==1&&occurrences(CONFIG)==0&&ram_migrations==0);
    puts("transfer bridge: source-derived external/transfer leases, generation isolation, failed joins, font/network rollback, local-I/O and navigation barriers passed");
    printf("%d assertions; no serial, network worker, card or persistence access\n",checks);
    return 0;
}
'''


def main():
    shell = (ROOT / "main/system/pico_system.c").read_text()
    ui = (ROOT / "main/system/pico_connectivity.c").read_text()
    names = ["good_key", "ps_load", "ps_store", "transfer_free_bytes", "ps_transfer_prepared", "ps_transfer_prepare", "ps_transfer_capacity", "ps_transfer_finished", "ps_media_restored", "ps_storage_prepare", "ps_storage_resume", "ps_external_prepare", "ps_external_finished", "system_exit", "ps_media_lost", "system_media_lost", "ps_navigate", "ps_switch", "ps_back", "ps_legacy"]
    prototypes = "\n".join(function(shell, name).split("{", 1)[0] + ";" for name in names)
    prototypes += "\nstatic void cc_release_finished(void);\n"
    bodies = "\n\n".join(function(shell, name) for name in names)
    # 仅提取tick内的待恢复重试片段，避免将手势、传感器和屏幕任务替身带入此所有权测试。
    # Extract only the actual resume-retry prefix, keeping unrelated tick hardware outside this harness.
    tick = function(shell, "on_tick")
    resume_prefix = re.search(r"    if \(!input_live && s_network_resume_pending[^\n]*", tick).group()
    bodies += "\nstatic void tick_resume(bool input_live) {\n" + resume_prefix + "\n}\n"
    bodies += "\n\n" + "\n\n".join(function(ui, name) for name in ["cc_holds_transfer", "cc_release_finished", "ps_connectivity_back", "ps_connectivity_before_leave", "ps_connectivity_shutdown"])
    with tempfile.TemporaryDirectory(prefix="pico-transfer-bridge-") as td:
        build = Path(td)
        (build / "app.h").write_text("#pragma once\n#include <stdbool.h>\n#include <stdint.h>\ntypedef enum{APP_REDRAW_NONE,APP_REDRAW_PAGE,APP_REDRAW_FULL,APP_REDRAW_DONE}app_redraw_t;\ntypedef struct{int unused;}app_desc_t;\ntypedef struct{bool sensor_ready;void*acc;const app_desc_t*request_app;bool request_return;}app_ctx_t;\ntypedef struct{int x,y,width,height;}EpdRect;\n")
        (build / "esp_err.h").write_text("#pragma once\ntypedef int esp_err_t;\n#define ESP_OK 0\n#define ESP_FAIL -1\n#define ESP_ERR_INVALID_ARG -2\n#define ESP_ERR_INVALID_STATE -3\n#define ESP_ERR_NO_MEM -4\n#define ESP_ERR_NOT_FOUND -5\n#define ESP_ERR_TIMEOUT -6\n#define ESP_ERR_INVALID_SIZE -7\n")
        (build / "sdmmc_cmd.h").write_text("#pragma once\ntypedef struct sdmmc_card sdmmc_card_t;\n")
        source = build / "test.c"
        source.write_text(PRELUDE + "\n" + prototypes + "\n" + bodies + "\n" + TESTS)
        exe = build / "test"
        command = [os.environ.get("CC", "cc"), "-std=c11", "-O1", "-g", "-Wall", "-Wextra", "-Werror"]
        command += shlex.split(os.environ.get("CFLAGS", ""))
        for directory in [build, ROOT / "main/system", ROOT / "main/book", ROOT / "components/read_pico_transfer/include", ROOT / "components/read_pico/include"]:
            command += ["-I", str(directory)]
        subprocess.run(command + [str(source), "-o", str(exe)], check=True)
        subprocess.run([str(exe)], check=True)


if __name__ == "__main__":
    main()

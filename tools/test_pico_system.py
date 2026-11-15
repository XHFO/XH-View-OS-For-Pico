#!/usr/bin/env python3
"""中文：提取设备同源壳函数，验证存档故障、导航与触摸；不连接硬件。
English: Exercise exact shell functions for storage faults, navigation and touch without hardware.
"""
from pathlib import Path
import argparse
import os
import re
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--sanitize', action='store_true')
ARGS = parser.parse_args()
SOURCE = (ROOT / "main/system/pico_system.c").read_text()
HEADER = (ROOT / "main/system/pico_system.h").read_text()
NAV_HEADER = (ROOT / "main/system/pico_navigation.h").read_text()
READER = (ROOT / "main/system/pico_reader.c").read_text()
FONT_HEADER = (ROOT / "main/font/ttf_font.h").read_text()
EPD_HEADER = (ROOT / "components/epdiy/src/epdiy.h").read_text()
BOOK_STORE_HEADER = (ROOT / "main/book/book_store.h").read_text()


def function(name, source=SOURCE):
    match = re.search(r"(?:^|\n)((?:static\s+)?[\w\s*]+?\b" + re.escape(name) + r"\([^;]*?\)\s*\{)", source)
    if not match:
        raise RuntimeError(f"Cannot locate actual shell function: {name}")
    start = match.start(1)
    at = match.end() - 1
    depth = 0
    quote = None
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
    raise RuntimeError(f"Unbalanced source function: {name}")


types = "\n".join(re.findall(r"typedef enum\s*\{.*?\}\s*\w+;", HEADER, re.S))
types += "\n" + "\n".join(re.findall(r"typedef struct\s*\{[^}]*\}\s*ps_(?:event|view)_t;", HEADER, re.S))
types += "\n" + "\n".join(re.findall(r"enum\s*\{[^}]*\};", NAV_HEADER, re.S))
types += "\n" + re.search(r"typedef struct\s*\{[^}]*\}\s*ps_navigation_entry_t;", NAV_HEADER, re.S).group()
# 复用真实记录类型，避免小样本掩盖存档容量限制。/ Reuse actual record types so tiny blobs cannot hide persistence limits.
reader_types = '\n'.join(re.findall(r'^#define PS_READER_(?:RECORD_CAPACITY|NOTE_MAX_BYTES)[^\n]+', (ROOT/'main/system/pico_reader.h').read_text(), re.M)) + '\n'
reader_types += re.search(r"enum\s*\{[^}]*\bBOOK_COUNT\s*=[^}]*\};", READER).group()
reader_types += "\n" + re.search(r"#define TTF_FONT_PATH_MAX[^\n]+", FONT_HEADER).group()
for name in ("reader_progress_t", "reader_prefs_t", "reader_ink_t", "reader_record_v2_t", "reader_record_t", "reader_records_t", "reader_record_index_t", "reader_record_chunk_t", "reader_draft_t"):
    reader_types += "\n" + re.search(r"typedef struct\s*\{[^}]*\}\s*" + name + r";", READER).group()
types += "\n" + reader_types
types += "\n" + re.search(r"enum EpdRotation\s*\{[^}]*\};", EPD_HEADER).group()
types += "\n" + re.search(r"typedef struct\s*\{[^}]*\}\s*book_store_root_t;", BOOK_STORE_HEADER).group()
types = re.search(r"#define BOOK_STORE_PATH_MAX[^\n]+", BOOK_STORE_HEADER).group() + "\n" + types
types += "\n" + re.search(r"typedef enum\s*\{[^}]*\}\s*app_book_global_action_t;", (ROOT / "main/apps/app_book.h").read_text()).group()
prefix = SOURCE[SOURCE.index('#define PS_VERSION'):SOURCE.index('// ROM与主机实现使用同一标准CRC32')]
fan_type = re.search(r"typedef struct\s*\{[^}]*\}\s*fan_item_t;", SOURCE).group()
names = ["crc32_update", "crc32", "good_key", "valid_file", "load_file", "ps_load", "stored_file_matches", "store_file", "ps_store", "ps_storage_save_allowed", "checkpoint_invalidate", "record_root_allowed", "ps_storage_record_roots", "ps_load_at", "ps_store_at", "ps_store_exists_at", "ps_storage_prepare", "ps_storage_resume", "ps_storage_rebind", "ps_storage_target", "save_config", "quick_canonical", "quick_catalog", "quick_enabled", "quick_slot_of", "quick_placed", "quick_seed_slots", "quick_import", "quick_defaults", "quick_adopt", "quick_load", "quick_save_slots", "quick_edit_contact_cancel", "quick_edit_begin", "quick_edit_snapshot", "quick_edit_assign", "ps_background_io_allowed", "ps_content_held", "local_hold", "local_before_leave", "ps_view", "ps_route", "ps_now", "ps_dirty", "ps_toast", "reader_route", "home_route", "setting_route", "enter", "resume_real_book", "ps_global_book_return", "ps_global_prefer_real_book", "global_depart", "ps_navigate", "ps_switch", "ps_back", "ps_sleep", "ps_wake", "apply_rotation", "ps_rotate", "sample_rotation", "ps_rotation_lock", "enabled", "quick_unavailable", "open_panel", "quick_action", "fan_editor_active", "fan_frame_active", "fan_frame_selected", "fan_frame_side", "fan_scale", "fan_layout_slots", "fan_layout", "fan_prompt_x", "quick_catalog_row_height", "quick_catalog_rows", "quick_catalog_rect", "fan_pick", "fan_move", "fan_release", "map_touch", "touch_tick", "ps_global_cancel_input", "ps_power_partial_safe", "ps_global_open_settings", "center_input", "ps_global_enable", "ps_global_page", "ps_global_input", "on_enter", "system_exit", "on_tick"]
names[names.index("ps_storage_prepare"):names.index("ps_storage_prepare")] = ["store_media_bind", "ps_media_restored"]
names[names.index("ps_switch"):names.index("ps_switch")] = ["launcher_switch", "launcher_root"]
names.insert(names.index("ps_background_io_allowed"), "ps_background_io_blockers")
names.insert(names.index("ps_rotation_lock"), "rotation_consider")
names.insert(names.index("rotation_consider"), "rotation_flip180")
names.insert(names.index("ps_rotation_lock") + 1, "ps_rotation_locked")
names.insert(names.index("ps_rotation_locked") + 1, "ps_touch_guard_enabled")
names.insert(names.index("ps_touch_guard_enabled") + 1, "ps_touch_guard_set")
functions = function("ps_usb_local_route", (ROOT / "main/system/pico_usb.c").read_text()) + "\n" + "\n\n".join(function(n) for n in names)
# 同源BSP转换搭配独立四姿态fixture，避免只向壳注入假定设备轴。/ Couple the actual BSP transform with independent four-pose fixtures rather than presumed device axes.
functions += "\n" + function("read_pico_accel_to_device", (ROOT / "components/read_pico/read_pico_init.c").read_text()).replace("sc7a20h_sample_t", "test_bsp_sample_t")
functions += "\n" + function("quick_settings_event") + "\n" + function("group_items") + "\n" + function("panel_group_rect") + "\n" + function("panel_reading_rect") + "\n" + function("panel_event")
functions += "\n" + re.sub(r"\bdispatch\(", "global_dispatch(", function("dispatch"), count=1)

prelude = r'''
#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>
#include <setjmp.h>
#include <stdatomic.h>

typedef struct { bool touched;int count,x,y; } cst836u_touch_t;
typedef struct { bool product_shell; } app_desc_t;
typedef struct { int x,y,width,height; } EpdRect;
typedef struct { const cst836u_touch_t *touch;bool consumed,pressed,released,sensor_ready,request_return,input_pending;int64_t now_ms,touch_sample_ms;void *acc;const app_desc_t *request_app; } app_ctx_t;
typedef struct { int x_mg,y_mg,z_mg; } sc7a20h_sample_t;
typedef struct { int16_t x_mg,y_mg,z_mg,x_raw,y_raw,z_raw;uint8_t status; } test_bsp_sample_t;
typedef enum { APP_REDRAW_NONE,APP_REDRAW_PAGE,APP_REDRAW_FULL,APP_REDRAW_DONE } app_redraw_t;
#define APP_AUTO_SLEEP_MS (2*60*1000)
typedef int nvs_handle_t;
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_ERR_INVALID_SIZE -3
#define NVS_READONLY 1
#define NVS_READWRITE 2
#define ESP_LOGI(...) ((void)0)
#define ESP_LOGW(...) ((void)0)
static uint8_t test_nvs[1024];static size_t test_nvs_size;static char test_nvs_key[32];static unsigned test_nvs_opens,test_file_opens;
static int nvs_open(const char *ns,int mode,nvs_handle_t *h){(void)ns;(void)mode;test_nvs_opens++;*h=1;return ESP_OK;}
static int nvs_get_blob(nvs_handle_t h,const char *key,void *out,size_t *bytes){(void)h;if(strcmp(key,test_nvs_key)||!test_nvs_size)return -1;if(out){if(*bytes<test_nvs_size)return -1;memcpy(out,test_nvs,test_nvs_size);}*bytes=test_nvs_size;return 0;}
static int nvs_set_blob(nvs_handle_t h,const char *key,const void *data,size_t bytes){(void)h;if(bytes>sizeof(test_nvs))return -1;memcpy(test_nvs,data,bytes);test_nvs_size=bytes;snprintf(test_nvs_key,sizeof(test_nvs_key),"%s",key);return 0;}
static int nvs_commit(nvs_handle_t h){(void)h;return 0;}
static void nvs_close(nvs_handle_t h){(void)h;}
static bool test_fail_tmp;static int test_fault_after,test_fs_step;static jmp_buf test_crash;
static void test_fault_point(void){if(test_fault_after&&++test_fs_step==test_fault_after)longjmp(test_crash,1);}
static FILE *test_fopen(const char *path,const char *mode){test_file_opens++;if(test_fail_tmp&&strstr(path,".tmp")&&mode[0]=='w'){errno=ENOSPC;return NULL;}return fopen(path,mode);}
static int test_rename(const char *a,const char *b){int r=rename(a,b);if(!r)test_fault_point();return r;}
static int test_unlink(const char *a){int r=unlink(a);if(!r)test_fault_point();return r;}
static bool test_stat_eio,test_mkdir_race;
static int test_stat(const char *p,struct stat *s){if(test_stat_eio){errno=EIO;return -1;}return stat(p,s);}
static int test_mkdir(const char *p,mode_t m){if(test_mkdir_race&&strstr(p,".pico-system")){(void)mkdir(p,m);errno=EEXIST;return -1;}return mkdir(p,m);}
'''

stubs = r'''
static bool ps_rotation_locked(void);
static int ps_dual_rotation(void){return EPD_ROT_INVERTED_PORTRAIT;}
static int ps_dual_base_rotation(void){return EPD_ROT_INVERTED_PORTRAIT;}
static int ps_dual_role(void){return 1;}
static bool ps_dual_orient(int rotation){(void)rotation;return false;}
static bool ps_dual_rotate(void){return true;}
static bool ps_dual_rotation_locked(void){return false;}
static bool ps_dual_lock_set(bool locked){(void)locked;return true;}
static bool ps_dual_take_manual_rotation(void){return false;}
static bool ps_dual_guard_enabled(void){return true;}
static bool ps_dual_guard_set(bool enabled){(void)enabled;return true;}
static void ps_dual_rotation_changed(void){}
static bool ps_dual_enabled(void){return false;}
static void ps_dual_suspend(void){}
static void ps_dual_enter(void){}
static bool ps_dual_event(const ps_event_t *e){(void)e;return false;}
static bool ps_updates_tick(void){return false;}
static bool ps_animation_event(const ps_event_t *e){(void)e;return false;}
static bool dual_resume_visible(void){return false;}
static EpdRect dual_resume_rect(void){return (EpdRect){0,0,0,0};}
static bool ps_dual_service(app_ctx_t *ctx,const app_desc_t *app,app_redraw_t *redraw){(void)ctx;(void)app;(void)redraw;return false;}
static ps_navigation_entry_t test_nav_items[PS_NAV_MAX_VISIBLE];
static int test_navigation_count=3,test_navigation_enters,test_navigation_events;
static bool test_navigation_picker;
static uint16_t test_navigation_launched;
static void test_navigation_launch(uint16_t id){test_navigation_launched=id;}
static void ps_navigation_init(void){
    const ps_navigation_entry_t defaults[]={
        {PS_NAV_BOOKS,PS_SHELF,PI_BOOK,"书架",NULL},
        {PS_NAV_HOME,PS_HA,PI_HOME,"家庭",NULL},
        {PS_NAV_GAMES,PS_GAMES,PI_GAME,"小游戏",NULL}
    };
    memcpy(test_nav_items,defaults,sizeof(defaults));test_navigation_count=3;test_navigation_picker=false;
}
static int ps_navigation_count(void){return test_navigation_count;}
static const ps_navigation_entry_t *ps_navigation_item(int index){return index>=0&&index<test_navigation_count?&test_nav_items[index]:NULL;}
static int ps_navigation_index(ps_route_t route){for(int i=0;i<test_navigation_count;i++)if(test_nav_items[i].route==route)return i;return -1;}
static void ps_navigation_enter(void){test_navigation_enters++;}
static bool ps_navigation_back(void){if(!test_navigation_picker)return false;test_navigation_picker=false;return true;}
static bool ps_navigation_event(const ps_event_t *e){(void)e;test_navigation_events++;return true;}
static bool test_keep_awake;
static bool app_settings_test_awake(void){return test_keep_awake;}
static void app_settings_set_test_awake(bool on){test_keep_awake=on;}
static bool test_reader_layer,test_home_layer,test_game_layer;
static bool ps_home_launcher_visible(void){return (s_route==PS_HA||s_route==PS_PRINTERS)&&!test_home_layer;}
static const app_desc_t app_book={0},app_pico_system={.product_shell=true};
static int test_book_action=-1,test_book_prepares,test_book_resumes;static bool test_book_can_resume;
static void app_book_global_prepare(void){test_book_prepares++;}
static bool app_book_resume_last(void){test_book_resumes++;return test_book_can_resume;}
static app_redraw_t app_book_global_action(app_ctx_t *c,app_book_global_action_t a){(void)c;test_book_action=a;return APP_REDRAW_PAGE;}
static void ps_legacy(const app_desc_t *app){s_legacy=app;}
static bool test_local_network,test_accel_ok;static sc7a20h_sample_t test_accel;
static int test_rotation=-1;
static unsigned test_poll_calls,test_maintenance_calls,test_accel_calls,test_network_ticks,test_file_consumer_ticks;
static void poll_hardware(void){test_poll_calls++;}
static bool test_ink_mode,test_ink_active,test_apps_ink_mode,test_ink_pending,test_ink_fail;
static unsigned test_ink_steps,test_ink_cancels,test_apps_invalidations;
static void ps_apps_register_navigation(void){}
static int test_desktop_app=-1;static unsigned test_launcher_enters;
static void ps_launcher_enter(void){test_launcher_enters++;}
static bool ps_launcher_event(const ps_event_t *e){(void)e;return true;}
static bool ps_desktop_prepare_app(bool app){test_desktop_app=app;return true;}
static bool ps_desktop_app_available(bool app){(void)app;return false;}
static void ps_desktop_init(void){}
static void ps_desktop_usb_reclaimed(void){}
static bool ps_desktop_tick(void){test_file_consumer_ticks++;return false;}
static bool ps_reader_editor_ink_mode(void){return s_route==PS_EDITOR&&test_ink_mode;}
static bool ps_reader_editor_ink_active(void){return ps_reader_editor_ink_mode()&&test_ink_active;}
static void ps_reader_editor_ink_cancel(void){test_ink_active=false;}
static bool ps_apps_ink_mode(void){return s_route==PS_APPLICATION&&test_apps_ink_mode;}
static bool ps_ink_pending(void){return test_ink_pending;}
static bool ps_ink_step(app_ctx_t *ctx){(void)ctx;test_ink_steps++;if(test_ink_fail)return false;test_ink_pending=false;return true;}
static bool ps_ink_cancel(void){test_ink_cancels++;bool pending=test_ink_pending;test_ink_pending=false;return pending;}
static void ps_apps_ink_invalidate(void){test_apps_invalidations++;}
static void ps_apps_ink_cancel(void){}
typedef struct { bool present,mounted,exported;uint32_t media_generation; } read_pico_sd_info_t;
static read_pico_sd_info_t test_card;
static int test_card_error,test_media_scans,test_media_revisions;
static int read_pico_sd_get_info(read_pico_sd_info_t *out){*out=test_card;return test_card_error;}
static void ps_games_rescan(void){test_media_scans++;}
static void book_store_notify_changed(void){test_media_revisions++;}
static bool test_storage_busy;
static bool test_font_suspend_fail,test_font_suspended;static int test_media_lost;
static int ttf_font_suspend_sd(bool suspend){test_font_suspended=suspend;return suspend&&test_font_suspend_fail?-1:ESP_OK;}
static unsigned test_reader_invalidations,test_home_invalidations;
static void ps_reader_checkpoint_invalidate(void){test_reader_invalidations++;}
static void ps_home_storage_invalidate(void){test_home_invalidations++;}
static void ps_reader_media_lost(void){test_media_lost++;}
static bool ps_storage_busy(void){return test_storage_busy;}
static bool ps_storage_tick(void){return false;}
static bool test_weread_busy,test_weread_blocked;
static unsigned test_weread_pauses,test_weread_resumes,test_weread_initializations,test_weread_leases;
static void ps_weread_init(void){test_weread_initializations++;}
static bool ps_weread_pause(void){test_weread_pauses++;if(test_weread_busy)return false;test_weread_leases++;test_weread_blocked=true;return true;}
static void ps_weread_resume(void){test_weread_resumes++;assert(test_weread_leases);test_weread_leases--;test_weread_blocked=test_weread_leases>0;}
static bool test_connectivity_shutdown_fail,test_connectivity_intercept;
static int test_connectivity_shutdowns,test_connectivity_resume;
static bool ps_connectivity_shutdown(void){test_connectivity_shutdowns++;return !test_connectivity_shutdown_fail;}
static int read_pico_connectivity_resume(void){test_connectivity_resume++;return 0;}
static bool test_usb_held,test_usb_busy,test_usb_background;
static unsigned test_usb_ticks;
static bool ps_usb_background(void){return test_usb_background;}
static void ps_usb_enter(void){}
static bool ps_usb_local_route(ps_route_t r);
static bool ps_usb_before_leave(ps_route_t r){return test_usb_held&&(!test_usb_background||test_usb_busy||!ps_usb_local_route(r));}
static bool ps_usb_back(void){return test_usb_held&&(!test_usb_background||test_usb_busy);}
static void ps_usb_init(void){}
static void ps_bluetooth_init(void){}
static void ps_bluetooth_enter(void){}
static bool ps_bluetooth_tick(void){test_network_ticks++;return false;}
static unsigned test_bluetooth_events,test_unsafe_events;
static bool ps_bluetooth_event(const ps_event_t *e){(void)e;test_bluetooth_events++;return true;}
static bool ps_bluetooth_input(app_ctx_t *ctx,const app_desc_t *app,bool blocked,app_redraw_t *redraw){(void)ctx;(void)app;(void)blocked;(void)redraw;return false;}
static bool content_tick(void){return false;}
static bool content_acquire(void){return false;}
static void content_release(bool changed){(void)changed;}
static void content_cancel(void){}
static void read_pico_content_set_storage_hooks(bool(*a)(void),void(*r)(bool),void(*c)(void)){(void)a;(void)r;(void)c;}
static void ps_maintenance_tick(void){test_maintenance_calls++;}
static bool ps_usb_tick(void){test_usb_ticks++;return false;}
static bool ps_usb_busy(void){return test_usb_busy;}
static bool ps_usb_held(void){return test_usb_held;}
static bool ps_hit(int x,int y,int rx,int ry,int rw,int rh){return x>=rx&&x<rx+rw&&y>=ry&&y<ry+rh;}
static bool ps_reader_event(const ps_event_t *e){(void)e;test_unsafe_events++;return false;}
static bool ps_home_event(const ps_event_t *e){(void)e;test_unsafe_events++;return false;}
static bool ps_games_event(const ps_event_t *e){(void)e;test_unsafe_events++;return false;}
static bool device_event(const ps_event_t *e){(void)e;return false;}
static bool ps_connectivity_event(const ps_event_t *e){(void)e;test_unsafe_events++;return false;}
static bool ps_storage_event(const ps_event_t *e){(void)e;test_unsafe_events++;return false;}
static bool ps_usb_event(const ps_event_t *e){(void)e;return false;}
static bool quick_settings_event(const ps_event_t *e);
static bool onboarding_event(const ps_event_t *e){(void)e;return false;}
static int test_reader_tools,test_reader_toc;
static bool app_book_global_is_open(void){return true;}
static bool test_selection;
static bool app_book_global_has_selection(void){return test_selection;}
static bool ps_reader_is_open(void){return true;}
static bool ps_demo_home_online(void){return false;}
static const char *ps_network_label(void){return "network";}
static void ps_reader_action(int a){(void)a;}
static void ps_reader_records_tab(int tab){(void)tab;}
static void ps_games_action(int a){(void)a;}
static bool ps_games_is_arcade(void){return s_route==PS_ARCADEGAME;}
static bool ps_games_is_snake(void){return false;}
static void ps_reader_tools(void){test_reader_tools++;test_reader_layer=true;}
static void ps_reader_toc(void){test_reader_toc++;test_reader_layer=true;}
static bool global_dispatch(const ps_event_t *e);
static void ps_connectivity_init(void){}
static void ps_connectivity_enter(ps_route_t r){(void)r;}
static bool ps_connectivity_before_leave(ps_route_t r){(void)r;return test_connectivity_intercept;}
static bool ps_connectivity_back(void){return false;}
static bool ps_connectivity_tick(void){test_network_ticks++;return false;}
static void ps_storage_enter(void){}
static bool ps_storage_back(void){return false;}
static bool test_roots_available;static char test_books_root[160];static int test_module_saves;
static int book_store_roots(book_store_root_t out[2],int *count){*count=0;if(!test_roots_available)return -1;memset(&out[0],0,sizeof(out[0]));snprintf(out[0].path,sizeof(out[0].path),"%s",test_books_root);*count=1;return ESP_OK;}
static bool ps_reader_storage_save(void){test_module_saves++;return true;}
static bool ps_home_storage_save(void){test_module_saves++;return true;}
static bool ps_games_storage_save(void){test_module_saves++;return true;}
static bool ps_navigation_storage_save(void){test_module_saves++;return true;}
static bool ps_games_prepare_storage(void){return true;}
static int test_power_action=-1,test_power_preview_style=-1;static unsigned test_power_wakes;static int64_t test_power_activity_ms;
enum {PS_POWER_LOCK=0};
static void ps_power_request(int action){test_power_action=action;}
static void ps_power_preview(int style){test_power_preview_style=style;}
static void ps_power_wake(void){test_power_wakes++;}
static void ps_power_activity(int64_t now){test_power_activity_ms=now;}
static void ps_datetime_enter(void){}
static void ps_datetime_init(void){}
static void ps_sound_init(void){}
static void ps_sound_error(void){}
static bool ps_sound_event(const ps_event_t *e){(void)e;return false;}
static bool ps_datetime_sync_from_rtc(uint32_t utc,bool valid,bool synced){(void)utc;(void)valid;(void)synced;return true;}
static bool ps_datetime_tick(void){return false;}
static bool test_power_confirmation;
static void ps_power_enter(void){test_power_confirmation=false;}
static bool ps_power_back(void){bool dismiss=test_power_confirmation;test_power_confirmation=false;return dismiss;}
static bool ps_datetime_event(const ps_event_t *e){(void)e;return false;}
static bool ps_power_event(const ps_event_t *e){(void)e;return false;}
static void ps_reader_init(void){}
static void ps_home_init(void){}
static void ps_games_init(void){}
static void read_pico_sensor_wake(void *acc){(void)acc;}
static void read_pico_sensor_sleep(void *acc){(void)acc;}
static bool ps_reader_tick(void){test_file_consumer_ticks++;return false;}
static unsigned test_editor_invalidations;
static void ps_reader_editor_invalidate(void){test_editor_invalidations++;}
static bool ps_home_tick(void){test_network_ticks++;return false;}
static bool ps_games_tick(void){test_file_consumer_ticks++;return false;}
static bool ps_home_local_network(void){return test_local_network;}
static const char *ps_home_connection_label(void){return "家庭未连接";}
static void epd_set_rotation(enum EpdRotation direction){test_rotation=direction;}
static enum EpdRotation epd_get_rotation(void){return test_rotation<0?EPD_ROT_INVERTED_PORTRAIT:(enum EpdRotation)test_rotation;}
static int read_pico_accel_read(void *acc,sc7a20h_sample_t *sample){(void)acc;test_accel_calls++;if(!test_accel_ok)return -1;*sample=test_accel;return ESP_OK;}
static int test_action=-1,test_event_count,test_release_transition;static ps_event_type_t test_events[64];
static void ps_reader_enter(ps_route_t r){(void)r;}
static void ps_home_enter(ps_route_t r){(void)r;}
static void ps_games_enter(ps_route_t r){(void)r;}
static bool ps_reader_key(int k){if(k==-1&&test_reader_layer){test_reader_layer=false;return true;}return false;}
static bool ps_home_key(int k){if(k==-1&&test_home_layer){test_home_layer=false;return true;}return false;}
static bool ps_games_key(int k){if(k==-1&&test_game_layer){test_game_layer=false;return true;}return false;}
void ps_navigate(ps_route_t r);void ps_switch(int section);void ps_back(void);void ps_wake(void);
void ps_toast(const char *text);
void ps_global_cancel_input(void);void ps_global_open_settings(app_ctx_t *ctx);
#define UI_KEY_2 1
static int ui_key_hit_test(int x,int y){return y<1300?-1:x/160<3?x/160:-1;}
static void ps_reader_open(int b){(void)b;ps_navigate(PS_READ);}
static void ps_home_action(int a){ps_navigate(a==2?PS_HA_SETTINGS:PS_HA);}
static bool test_dispatch_real;
static bool dispatch(const ps_event_t *e){if(s_global_app||test_dispatch_real)return global_dispatch(e);if(test_event_count<64)test_events[test_event_count++]=e->type;if(e->type==PS_RELEASE){if(test_release_transition==1)s_route=PS_SHELF;if(test_release_transition==2)s_panel=1;}return false;}
'''

checks = r'''
#undef fopen
#undef rename
#undef unlink
#undef stat
#undef mkdir
static unsigned test_failures,test_checks;
#define CHECK(expr,label) do{test_checks++;if(!(expr)){fprintf(stderr,"FAIL %s (line %d)\n",label,__LINE__);test_failures++;}}while(0)
static void reset_shell(void){
    ps_navigation_init();test_navigation_enters=test_navigation_events=0;test_desktop_app=-1;test_launcher_enters=0;
    test_keep_awake=false;test_weread_busy=test_weread_blocked=false;test_weread_pauses=test_weread_resumes=test_weread_leases=0;
    s_ctx=NULL;quick_defaults();ps_global_cancel_input();s_force_full=s_overlay_restore=false;s_quick_mode=0;s_quick_scroll=0;test_selection=false;
    test_storage_busy=test_roots_available=false;s_store_lost=s_store_is_sd=s_store_media_known=false;s_store_media_generation=0;test_card=(read_pico_sd_info_t){0};test_card_error=-1;test_media_scans=test_media_revisions=0;test_module_saves=0;
    s_ready=false;test_rotation=-1;
    memset(&s_config,0,sizeof(s_config));s_config.version=1;s_config.interval=15;
    s_depth=s_section=s_panel=s_group=s_focus=0;s_route=PS_SHELF;s_workspace[0]=PS_SHELF;s_workspace[1]=PS_HA;
    s_fan=s_fan_candidate=s_tracking=s_dirty=s_fast=s_long=false;memset(&s_quick_edit,0,sizeof(s_quick_edit));s_down_ms=s_move_ms=s_last_input=0;test_reader_layer=test_home_layer=test_game_layer=false;
    test_power_action=test_power_preview_style=-1;test_power_wakes=0;test_power_activity_ms=0;
    s_motion_ms=s_sleep_ms=s_acc_ms=0;s_sample_valid=false;s_charging=false;
    s_orientation_candidate=-1;s_orientation_matches=0;s_display_rotation=-1;
    test_local_network=test_accel_ok=false;s_legacy=NULL;s_toast[0]=0;
    s_global_enabled=s_global_claimed=s_book_return=s_real_book_preferred=false;s_global_app=NULL;
    test_book_action=-1;test_book_prepares=test_book_resumes=0;test_book_can_resume=false;
    test_reader_tools=test_reader_toc=0;
    test_editor_invalidations=0;
    test_ink_mode=test_ink_active=test_apps_ink_mode=test_ink_pending=test_ink_fail=false;
    test_ink_steps=test_ink_cancels=test_apps_invalidations=test_poll_calls=test_maintenance_calls=test_accel_calls=test_network_ticks=0;
    test_event_count=0;test_action=-1;test_release_transition=0;s_store_root[0]=0;test_nvs_size=0;test_dispatch_real=false;test_usb_held=test_usb_busy=test_usb_background=s_content_owned=s_content_refresh=false;atomic_store(&s_content_state,CONTENT_IDLE);test_bluetooth_events=test_unsafe_events=0;s_external_active=false;
}
static void test_navigation(void){
    reset_shell();enter(PS_READ);quick_action(12);ps_switch(0);CHECK(s_route==PS_SHELF,"printing shortcut returns to bookshelf root");
    reset_shell();ps_switch(1);quick_action(1);ps_switch(1);CHECK(s_route==PS_HA,"continue-reading shortcut preserves home workspace");
    reset_shell();ps_switch(1);quick_action(2);ps_switch(1);CHECK(s_route==PS_HA,"records shortcut preserves home workspace");
    reset_shell();enter(PS_READ);quick_action(11);ps_switch(1);ps_switch(0);CHECK(s_route==PS_SHELF,"games tool returns to bookshelf root");
    reset_shell();ps_navigate(PS_READ);ps_navigate(PS_RECORDS);ps_navigate(PS_READING_SETTINGS);ps_navigate(PS_READ);
    CHECK(s_depth==1&&s_route==PS_READ,"history cycle compacts stack");ps_back();CHECK(s_route==PS_SHELF,"history returns to previous origin");
    reset_shell();ps_navigate(PS_PLAYGAME);s_panel=2;s_fan=true;test_game_layer=true;ps_back();CHECK(!s_fan&&s_panel==2,"Back cancels fan first");
    ps_back();CHECK(s_panel==1,"Back pops one panel layer");ps_back();CHECK(!s_panel,"Back closes root panel");
    ps_back();CHECK(!test_game_layer&&s_route==PS_PLAYGAME,"Back closes game modal before page");ps_back();CHECK(s_route==PS_SHELF,"Back pops page after overlays");
    reset_shell();enter(PS_READ);ps_sleep();CHECK(test_power_action==PS_POWER_LOCK&&s_route==PS_READ,"sleep delegates without replacing page");ps_wake();CHECK(test_power_wakes==1&&s_route==PS_READ,"wake delegates while retaining origin");
    for(int landscape=0;landscape<2;landscape++){
        reset_shell();s_config.landscape=landscape;
        test_nav_items[3]=test_nav_items[2];test_nav_items[2]=test_nav_items[1];test_nav_items[1]=test_nav_items[0];
        test_nav_items[0]=(ps_navigation_entry_t){PS_NAV_PRINTERS,PS_PRINTERS,PI_PRINTER,"打印机",NULL};test_navigation_count=4;
        launcher_switch(0);CHECK(s_route==PS_PRINTERS&&launcher_root(),"printer list is a customizable launcher root");
        ps_event_t swipe={.type=PS_SWIPE_L};global_dispatch(&swipe);CHECK(s_route==PS_SHELF,"swipe follows custom printer/books order");
        global_dispatch(&swipe);CHECK(s_route==PS_HA,"swipe follows custom books/home order");
        swipe.type=PS_SWIPE_R;global_dispatch(&swipe);CHECK(s_route==PS_SHELF,"reverse swipe follows custom order");
        int w=(ps_view().w-72-(test_navigation_count-1)*4)/test_navigation_count;
        ps_event_t tap={.type=PS_TAP,.x=12+3*(w+4)+w/2,.y=48};global_dispatch(&tap);
        CHECK(s_route==PS_GAMES,"top tabs and swipe use same custom positions");
        test_home_layer=true;enter(PS_PRINTERS);CHECK(!launcher_root(),"printer modal does not expose root swipe");test_home_layer=false;
        ps_switch(0);CHECK(s_route==PS_SHELF,"fixed bookshelf shortcut ignores custom order");
        ps_switch(1);CHECK(s_route==PS_HA,"fixed home shortcut ignores custom order");
        ps_navigate(PS_NAV_SETTINGS);CHECK(test_navigation_enters==1,"navigation settings creates isolated editor");
        test_navigation_picker=true;ps_back();CHECK(s_route==PS_NAV_SETTINGS&&!test_navigation_picker,"Back dismisses navigation picker first");
        ps_back();CHECK(s_route==PS_HA,"Back returns navigation settings to exact origin");
        test_nav_items[4]=(ps_navigation_entry_t){256,PS_ROUTE_COUNT,PI_GRID,"自定义应用",test_navigation_launch};
        test_navigation_count=5;w=(ps_view().w-72-(test_navigation_count-1)*4)/test_navigation_count;
        CHECK(w>=44,"five home destinations retain a usable minimum touch width");
        for(int i=0;i<5;i++)for(int edge=0;edge<2;edge++){
            enter(PS_SHELF);test_navigation_launched=0;
            tap=(ps_event_t){.type=PS_TAP,.x=12+i*(w+4)+(edge?w-1:1),.y=edge?67:28};global_dispatch(&tap);
            CHECK(i==4?test_navigation_launched==256:s_route==test_nav_items[i].route,"each five-slot top cell uses its full visual touch bounds");
        }
    }
}
static void test_app_routes(void){
    for(int landscape=0;landscape<2;landscape++){
        reset_shell();s_config.landscape=landscape;
        ps_navigate(PS_APPS);CHECK(test_launcher_enters==1&&s_route==PS_APPS,"Apps enters the common catalog");
        CHECK(!launcher_root(),"Apps hidden from saved Home has a heading Back");
        ps_navigate(PS_PLAYGAME);ps_back();CHECK(s_route==PS_APPS,"game shell returns to Apps origin");
        ps_navigate(PS_CLOUDFLARE);CHECK(test_desktop_app==1&&s_route==PS_CLOUDFLARE,"Cloudflare prepares its independent channel");
        ps_event_t back={.type=PS_TAP,.x=23,.y=47};global_dispatch(&back);CHECK(s_route==PS_APPS,"Cloudflare empty-state Back returns to Apps");
        ps_navigate(PS_CANVAS);CHECK(test_desktop_app==0&&s_route==PS_CANVAS,"ordinary board prepares only its own channel");ps_back();
        CHECK(s_route==PS_APPS,"ordinary board returns to Apps origin");
        ps_back();CHECK(s_route==PS_SHELF,"Apps returns to its exact origin");
        test_nav_items[3]=(ps_navigation_entry_t){PS_NAV_APPS,PS_APPS,PI_GRID,"应用集",NULL};test_navigation_count=4;
        ps_navigate(PS_APPS);CHECK(launcher_root(),"visible Apps participates in Home navigation");
        ps_event_t left={.type=PS_SWIPE_R};global_dispatch(&left);CHECK(s_route==PS_GAMES,"Apps swipe follows saved Home order");
    }
}
static void test_shortcut_mask(unsigned mask){s_shortcuts.count=0;for(int i=0;i<13&&s_shortcuts.count<QUICK_MAX;i++)if(mask&(1u<<i))s_shortcuts.ids[s_shortcuts.count++]=i;}
static void test_fan(void){
    reset_shell();
    for(int land=0;land<2;land++) {
        s_config.landscape=land;ps_view_t v=ps_view();
        fan_item_t left[QUICK_COUNT],right[QUICK_COUNT];s_fan_side=0;int n=fan_layout(left);s_fan_side=1;int nr=fan_layout(right);
        CHECK(n==14&&nr==14,"three rings contain 3/5/6 direct positions");
        for(int i=0;i<n;i++) {
            CHECK(left[i].id==s_quick_defaults[i],"default slots match approved direct function order");
            CHECK(left[i].id==right[i].id&&abs(left[i].x+right[i].x-v.w)<=1&&left[i].y==right[i].y,"fan mirrors exact content");
            CHECK(left[i].x-left[i].r>=0&&left[i].x+left[i].r<v.w&&left[i].y-left[i].r>=24&&left[i].y+left[i].r<v.h,"fan circle in viewport");
        }
    }
}
static void touch_sample(app_ctx_t *ctx,cst836u_touch_t *t,int x,int y,bool down,bool press,bool release){
    ps_view_t v=ps_view();if(s_config.landscape){t->x=y*684/360;t->y=1215-x*1216/640;}else{t->x=x*684/360;t->y=y*1216/640;}
    (void)v;t->touched=down;t->count=down?1:0;ctx->touch=t;ctx->pressed=press;ctx->released=release;ctx->consumed=false;ctx->now_ms+=100;touch_tick(ctx);
}
static bool saw(ps_event_type_t event){for(int i=0;i<test_event_count;i++)if(test_events[i]==event)return true;return false;}
static int count_event(ps_event_type_t event){int count=0;for(int i=0;i<test_event_count;i++)count+=test_events[i]==event;return count;}
static void precise_touch_sample(app_ctx_t *ctx,cst836u_touch_t *touch,int x,int y,int64_t time,bool press){
    if(s_config.landscape){touch->x=(y*684+359)/360;touch->y=1215-(x*1216+639)/640;}
    else{touch->x=(x*684+359)/360;touch->y=(y*1216+639)/640;}
    touch->touched=true;touch->count=1;ctx->touch=touch;ctx->pressed=press;ctx->released=false;ctx->now_ms=time;touch_tick(ctx);
}
static void test_ink_touch_sampling(void){
    for(int land=0;land<2;land++)for(int application=0;application<2;application++){
        reset_shell();s_config.landscape=land;s_route=application?PS_APPLICATION:PS_EDITOR;
        epd_set_rotation(land?EPD_ROT_INVERTED_LANDSCAPE:EPD_ROT_INVERTED_PORTRAIT);
        test_ink_mode=!application;test_apps_ink_mode=application;
        app_ctx_t ctx={.now_ms=1000};s_ctx=&ctx;cst836u_touch_t touch={0};
        precise_touch_sample(&ctx,&touch,110,150,1000,true);
        for(int point=1;point<=24;point++){
            precise_touch_sample(&ctx,&touch,110+point,150,1000+point*8,false);
            CHECK(count_event(PS_MOVE)==point,"each new one-pixel pen coordinate is dispatched at 8ms without 70ms control throttling");
            precise_touch_sample(&ctx,&touch,110+point,150,1002+point*8,false);
            CHECK(count_event(PS_MOVE)==point,"identical pen samples never duplicate a point");
        }
        CHECK(count_event(PS_PRESS)==1&&count_event(PS_MOVE)==24,"note handwriting and TF drawing retain every sampled movement in both orientations");
    }
    reset_shell();app_ctx_t ctx={.now_ms=1000};s_ctx=&ctx;cst836u_touch_t touch={0};
    precise_touch_sample(&ctx,&touch,110,150,1000,true);s_move_ms=1000;
    precise_touch_sample(&ctx,&touch,112,150,1008,false);
    precise_touch_sample(&ctx,&touch,118,150,1016,false);
    CHECK(!count_event(PS_MOVE),"ordinary controls keep the 70ms throttle despite changed coordinates");
    precise_touch_sample(&ctx,&touch,120,150,1080,false);
    CHECK(count_event(PS_MOVE)==1,"ordinary controls dispatch once movement and 70ms requirements are met");
}
static void test_touch(void){
    for(int land=0;land<2;land++){
        reset_shell();s_config.landscape=land;app_ctx_t ctx={.now_ms=1000};s_ctx=&ctx;cst836u_touch_t t={0};ps_view_t v=ps_view();
        touch_sample(&ctx,&t,v.w-80,120,true,true,false);touch_sample(&ctx,&t,80,120,true,false,false);touch_sample(&ctx,&t,80,120,false,false,true);
        CHECK(saw(PS_PRESS)&&saw(PS_MOVE)&&saw(PS_RELEASE)&&saw(PS_SWIPE_L),"raw touch yields reading swipe");
        test_event_count=0;touch_sample(&ctx,&t,110,100,true,true,false);touch_sample(&ctx,&t,110,100,false,false,true);CHECK(saw(PS_TAP)&&!saw(PS_SWIPE_L),"raw tap is distinct from swipe");
        test_event_count=0;touch_sample(&ctx,&t,25,v.h-20,true,true,false);touch_sample(&ctx,&t,120,v.h-120,true,false,false);
        CHECK(s_fan&&!saw(PS_PRESS)&&!saw(PS_SWIPE_L),"corner fan exclusively owns gesture");s_dirty=false;t.count=2;ctx.pressed=ctx.released=false;touch_tick(&ctx);
        CHECK(!s_fan&&!s_tracking&&s_dirty,"multitouch cancellation redraws closed fan");
        test_event_count=0;touch_sample(&ctx,&t,110,100,true,true,false);ctx.now_ms+=600;ctx.pressed=false;touch_tick(&ctx);touch_sample(&ctx,&t,110,100,false,false,true);
        CHECK(saw(PS_LONG)&&!saw(PS_TAP),"long press suppresses tap");
        test_event_count=0;touch_sample(&ctx,&t,110,100,true,true,false);ctx.consumed=true;ctx.pressed=false;touch_tick(&ctx);CHECK(!s_tracking&&saw(PS_CANCEL),"consumed input cancels page drag");
        for(int change=1;change<=2;change++){
            s_route=PS_EDITOR;s_panel=0;test_release_transition=change;test_event_count=0;
            touch_sample(&ctx,&t,110,100,true,true,false);touch_sample(&ctx,&t,110,100,false,false,true);
            CHECK(saw(PS_RELEASE)&&!saw(PS_TAP)&&!saw(PS_SWIPE_L),"release navigation cannot tap through into a new page or panel");
        }
        test_release_transition=0;
    }
}
static void test_fan_release_and_migration(void){
    for(int land=0;land<2;land++)for(int side=0;side<2;side++){
        reset_shell();s_config.landscape=land;s_fan_side=side;s_fan=true;s_route=PS_HA;
        fan_item_t a[QUICK_COUNT];fan_layout(a);fan_move(a[0].x,a[0].y);
        fan_release(side?ps_view().w-1:0,ps_view().h-1);
        CHECK(!s_fan&&s_route==PS_HA,"release in safe area cannot execute prior position");
        s_fan=true;fan_layout(a);fan_move(a[0].x,a[0].y);fan_release(a[2].x,a[2].y);
        CHECK(!s_fan&&s_route==PS_GAMES,"release re-picks its coordinate instead of old MOVE");
        s_fan=true;fan_layout(a);fan_release(a[10].x,a[10].y);
        CHECK(!s_fan&&s_route==PS_WIFI,"outer position executes its direct action without categories");
        s_fan=true;app_ctx_t ctx={.now_ms=1000};cst836u_touch_t t={0};fan_layout(a);
        touch_sample(&ctx,&t,a[8].x,a[8].y,true,true,false);CHECK(s_fan_side==side,"tap keeps existing corner origin");
        touch_sample(&ctx,&t,side?ps_view().w-1:1,ps_view().h-1,false,false,true);
        s_fan=true;s_fan_side=side;int editx=fan_prompt_x()+30;
        touch_sample(&ctx,&t,editx,45,true,true,false);touch_sample(&ctx,&t,editx,45,false,false,true);
        CHECK(s_route==PS_QUICK_SETTINGS&&s_quick_edit.active&&s_quick_edit.side==side&&!s_fan,"runtime edit button opens same-side visual arrangement directly");
    }
    reset_shell();s_store_root[0]=0;test_nvs_size=0;test_nvs_key[0]=0;
    struct {uint32_t version;uint8_t count,ids[12];} old={.version=2,.count=4,.ids={14,0,7,9}};
    CHECK(ps_store("quick_v2",&old,sizeof(old)),"write migration fixture");quick_load();
    const uint8_t migrated[]={0,4,11,14,7,9};
    CHECK(!memcmp(s_quick_slots,migrated,sizeof(migrated)),"legacy roots first then old enabled ordering");
    CHECK(s_shortcuts.count==6&&quick_enabled(4)&&quick_enabled(11),"legacy visible roots retained");
    old.count=0;CHECK(ps_store("quick_v2",&old,sizeof(old)),"write empty legacy fixture");quick_load();CHECK(quick_placed(s_quick_slots)==3,"legacy empty keeps formerly fixed roots");
    old.count=5;uint8_t defaults[]={0,13,7,9,14};memcpy(old.ids,defaults,5);
    CHECK(ps_store("quick_v2",&old,sizeof(old)),"write old default fixture");quick_load();CHECK(!memcmp(s_quick_slots,s_quick_defaults,FAN_SLOTS),"untouched old default upgrades to approved 14 direct actions");
    quick_saved_t bad={.version=5,.count=6,.ids={37,37,255,0,38,17}};memset(bad.slots,255,sizeof(bad.slots));bad.slots[0]=38;bad.slots[1]=17;bad.slots[5]=37;
    CHECK(ps_store("quick_v5",&bad,sizeof(bad)),"write malformed v5 fixture");quick_load();
    CHECK(s_shortcuts.count==3&&s_quick_slots[0]==17&&s_quick_slots[1]==QUICK_EMPTY&&s_quick_slots[5]==37,"v5 canonicalizes duplicate functions and preserves explicit empty slots");
    memset(s_quick_slots,QUICK_EMPTY,sizeof(s_quick_slots));CHECK(quick_save_slots(s_quick_slots),"save empty v5");quick_load();CHECK(!quick_placed(s_quick_slots),"v5 empty never resurrects fixed roots");
    test_nvs_size=0;test_nvs_key[0]=0;
}
static void test_v3_projection_and_gate(void){
    reset_shell();s_store_root[0]=0;test_nvs_size=0;test_nvs_key[0]=0;
    struct {uint32_t version;uint8_t count,ids[40];} old={.version=3,.count=5,.ids={18,38,15,17,39}};
    CHECK(ps_store("quick_v3",&old,sizeof(old)),"write genuine v3 fixture size");quick_load();
    const uint8_t expected[]={0,4,11,18,17,15};
    CHECK(!memcmp(s_quick_slots,expected,sizeof(expected))&&s_shortcuts.count==6,"legacy aliases deduplicate and retain saved encounter order");
    old.count=40;for(int i=0;i<40;i++)old.ids[i]=i;CHECK(ps_store("quick_v3",&old,sizeof(old)),"write all-functions legacy fixture");quick_load();
    CHECK(s_shortcuts.count==38&&quick_placed(s_quick_slots)==14,"all legacy enabled functions retained beyond slot capacity");
    for(int id=0;id<38;id++)CHECK(quick_enabled(id),"unplaced legacy functions remain available");
    uint8_t slots[FAN_SLOTS];memcpy(slots,s_quick_slots,FAN_SLOTS);CHECK(quick_save_slots(s_quick_slots),"write migrated v5");memset(&s_shortcuts,0,sizeof(s_shortcuts));quick_load();CHECK(s_shortcuts.count==38&&!memcmp(slots,s_quick_slots,FAN_SLOTS),"v5 reload preserves both arrangement and unplaced alternatives");
    memset(&s_shortcuts,0,sizeof(s_shortcuts));s_shortcuts.version=4;
    const uint8_t v4ids[]={38,17,39,18,37,0,14,9,8,7,6,5,4,3,2,1,11};s_shortcuts.count=sizeof(v4ids);memcpy(s_shortcuts.ids,v4ids,sizeof(v4ids));
    CHECK(ps_store("quick_v4",&s_shortcuts,sizeof(s_shortcuts)),"write exact v4 grouped-layout fixture");unsigned before_load=test_nvs_opens;quick_load();
    const uint8_t v4slots[]={0,4,11,17,18,37,14,9,8,7,6,5,3,2};
    CHECK(s_shortcuts.count==15&&!memcmp(s_quick_slots,v4slots,FAN_SLOTS)&&quick_enabled(1),"v4 migration uses saved order, deduplicates aliases and retains overflow as unplaced");
    CHECK(!strcmp(test_nvs_key,"quick_v4")&&test_nvs_opens>=before_load,"migration reads old preferences without replacing them until Done");
    reset_shell();app_ctx_t ctx={0};cst836u_touch_t touch={0};s_ctx=&ctx;ctx.touch=&touch;
    CHECK(ps_background_io_allowed(),"idle permits optional background I/O");
    bool *blocks[]={&s_fan,&s_fan_candidate,&s_tracking,&s_center_down,&s_center_pending,&s_quick_edit.active};
    for(int i=0;i<6;i++){*blocks[i]=true;CHECK(!ps_background_io_allowed(),"each gesture and editing state pauses I/O");*blocks[i]=false;}
    s_panel=3;CHECK(!ps_background_io_allowed(),"page switcher pauses I/O");s_panel=0;
    touch.touched=true;CHECK(!ps_background_io_allowed(),"held touch pauses I/O");touch.touched=false;
    ctx.request_app=&app_book;CHECK(!ps_background_io_allowed(),"pending page switch pauses I/O");ctx.request_app=NULL;
    CHECK(ps_background_io_allowed(),"settled state resumes optional I/O");
    s_external_active=true;CHECK(!ps_background_io_allowed(),"external USB owner pauses all optional I/O");s_external_active=false;
    test_usb_held=true;CHECK(!ps_background_io_allowed(),"USB held blocks I/O between worker jobs");
    unsigned network=test_network_ticks,consumers=test_file_consumer_ticks;unsigned maintenance=test_maintenance_calls;
    ctx.now_ms=1000;s_dirty=false;on_tick(&ctx);CHECK(test_file_consumer_ticks==consumers&&test_network_ticks==network&&test_maintenance_calls==maintenance+1,"held USB freezes connectivity, time and reader workers");
    test_usb_background=true;s_global_enabled=true;s_route=PS_CANVAS;app_desc_t app={.product_shell=true};app_redraw_t redraw=APP_REDRAW_FULL;unsigned usb_ticks=test_usb_ticks;
    CHECK(ps_global_input(&ctx,&app,&redraw)&&test_usb_ticks==usb_ticks+1&&s_route==PS_CANVAS,"background global input services only USB without leaving canvas");
    CHECK(redraw==APP_REDRAW_NONE&&test_file_consumer_ticks==consumers&&test_network_ticks==network+1,"quiet background input polls BLE snapshots without redraw or TF consumers");
    ps_back();CHECK(s_route==PS_DEVICE,"background Back opens settings without releasing USB-owned card");
    test_usb_held=test_usb_background=false;CHECK(ps_background_io_allowed(),"reclaim resumes optional consumers");s_ctx=NULL;
}
static void test_fan_paging_touch(void){
    for(int land=0;land<2;land++)for(int side=0;side<2;side++)for(int position=0;position<FAN_SLOTS;position++){
        reset_shell();s_config.landscape=land;s_fan=true;s_fan_side=side;
        for(int i=0;i<FAN_SLOTS;i++)s_quick_slots[i]=QUICK_EMPTY;s_quick_slots[position]=14;
        fan_item_t a[QUICK_COUNT];fan_layout(a);app_ctx_t ctx={.now_ms=1000};cst836u_touch_t t={0};
        touch_sample(&ctx,&t,a[position].x,a[position].y,true,true,false);touch_sample(&ctx,&t,a[position].x,a[position].y,false,false,true);
        CHECK(!s_fan&&s_route==PS_DEVICE,"every inner/middle/outer position directly executes the visible function");
    }
}
static void tick_at(app_ctx_t *ctx,int64_t now){ctx->now_ms=now;(void)on_tick(ctx);}
// 自动休眠/唤醒策略由真实power控制器专项验证；壳只验证唯一入口转发。
// Actual power-controller tests own auto-sleep/wake policy; the shell verifies single-entry delegation.
static void test_power_navigation(void){
    for(int style=0;style<4;style++){
        reset_shell();app_ctx_t ctx={.now_ms=1000};s_ctx=&ctx;enter(PS_READ);
        s_config.sleep_style=style;test_keep_awake=true;
        ps_sleep();CHECK(test_power_action==PS_POWER_LOCK&&s_route==PS_READ&&test_keep_awake,"manual lock delegates without replacing page or test switch");
        ps_wake();CHECK(test_power_wakes==1&&s_route==PS_READ,"wake delegates to same retained page");
        ps_navigate(PS_CLOCK);CHECK(test_power_preview_style==0&&s_route==PS_READ,"clock preview uses unified power controller");
        ps_navigate(PS_QUOTES);CHECK(test_power_preview_style==1&&s_route==PS_READ,"excerpt preview uses unified power controller");
    }
}
static void orientation_setup(app_ctx_t *ctx,int landscape){
    reset_shell();*ctx=(app_ctx_t){.sensor_ready=true};s_config.landscape=landscape;test_accel_ok=true;
    epd_set_rotation(landscape?EPD_ROT_INVERTED_LANDSCAPE:EPD_ROT_INVERTED_PORTRAIT);
}
static void test_display_orientation(void){
    // 实屏原生缓冲1216x684相对触摸板倒置；只翻显示，保留触摸和按键设备轴。
    // The 1216x684 native framebuffer is inverted relative to touch; flip display only.
    app_ctx_t ctx={.now_ms=1000};
    reset_shell();on_enter(&ctx);
    CHECK(test_rotation==EPD_ROT_INVERTED_PORTRAIT,"first boot preserves BSP keys-down portrait display orientation");
    for(int landscape=0;landscape<2;landscape++){
        reset_shell();s_ready=true;s_config.landscape=landscape;on_enter(&ctx);
        CHECK(test_rotation==(landscape?EPD_ROT_INVERTED_LANDSCAPE:EPD_ROT_INVERTED_PORTRAIT),"enter uses board-mounted display rotation");
        ps_rotate();
        CHECK(s_config.landscape==!landscape&&test_rotation==(landscape?EPD_ROT_INVERTED_PORTRAIT:EPD_ROT_INVERTED_LANDSCAPE),"manual rotation changes display while keeping device-axis touch convention");
        system_exit(&ctx);CHECK(test_rotation==EPD_ROT_INVERTED_PORTRAIT,"exit restores BSP default display rotation");
    }
    const int samples[][7]={
        {0,0,0,0,0,360,640},{0,683,1215,359,639,360,640},{0,342,608,180,320,360,640},
        {1,0,1215,0,0,640,360},{1,683,0,639,359,640,360},{1,342,608,319,180,640,360}
    };
    for(size_t i=0;i<sizeof(samples)/sizeof(samples[0]);i++){
        s_config.landscape=samples[i][0];epd_set_rotation(s_config.landscape?EPD_ROT_INVERTED_LANDSCAPE:EPD_ROT_INVERTED_PORTRAIT);int x=-1,y=-1;map_touch(samples[i][1],samples[i][2],&x,&y);ps_view_t v=ps_view();
        CHECK(x==samples[i][3]&&y==samples[i][4],"display correction keeps raw touch corner and center mapping unchanged");
        CHECK(v.w==samples[i][5]&&v.h==samples[i][6],"180-degree correction preserves logical display dimensions");
    }
}
static void test_orientation_bsp_pipeline(void){
    // 四条芯片轴±1g输入经过真实BSP，期望方向来自固定fixture；正负轴的实物安装仍需实屏验收。
    // Four chip-axis ±1g inputs pass through the actual BSP against fixed fixtures; physical mounting polarity still needs device acceptance.
    const struct {int chip_x,chip_y,device_x,device_y,landscape;enum EpdRotation rotation;} poses[]={
        {0,-1000,1000,0,0,EPD_ROT_INVERTED_PORTRAIT},
        {1000,0,0,-1000,1,EPD_ROT_LANDSCAPE},
        {0,1000,-1000,0,0,EPD_ROT_PORTRAIT},
        {-1000,0,0,1000,1,EPD_ROT_INVERTED_LANDSCAPE}
    };
    read_pico_accel_to_device(NULL);
    for(size_t i=0;i<sizeof(poses)/sizeof(poses[0]);i++)for(int locked=0;locked<2;locked++){
        app_ctx_t ctx;orientation_setup(&ctx,!poses[i].landscape);s_config.locked=locked;
        enum EpdRotation before=epd_get_rotation();
        test_bsp_sample_t raw={.x_mg=poses[i].chip_x,.y_mg=poses[i].chip_y,.z_mg=100,
            .x_raw=poses[i].chip_x/2,.y_raw=poses[i].chip_y/2,.z_raw=50,.status=0xa5};
        read_pico_accel_to_device(&raw);
        CHECK(raw.x_mg==poses[i].device_x&&raw.y_mg==poses[i].device_y&&raw.z_mg==-100,"actual BSP maps four signed chip gravity fixtures");
        CHECK(raw.x_raw==poses[i].device_x/2&&raw.y_raw==poses[i].device_y/2&&raw.z_raw==-50&&raw.status==0xa5,"BSP keeps raw and mg transforms aligned and preserves status");
        test_accel=(sc7a20h_sample_t){raw.x_mg,raw.y_mg,raw.z_mg};
        CHECK(sample_rotation(&test_accel)==(int)poses[i].rotation,"four signed device poses select four distinct display targets");
        tick_at(&ctx,500);CHECK(epd_get_rotation()==before,"first signed pose sample never rotates");
        tick_at(&ctx,1000);
        CHECK(epd_get_rotation()==(locked?before:poses[i].rotation),"two samples commit signed target unless orientation is locked");
        CHECK(s_config.landscape==(locked?!poses[i].landscape:poses[i].landscape),"four-way target retains existing boolean layout preference");
        tick_at(&ctx,1500);CHECK(epd_get_rotation()==(locked?before:poses[i].rotation),"stable signed pose never alternates directions");
        if(!locked){
            s_ready=true;system_exit(&ctx);CHECK(epd_get_rotation()==EPD_ROT_INVERTED_PORTRAIT,"engineering exit retains accepted canonical display");
            on_enter(&ctx);CHECK(epd_get_rotation()==poses[i].rotation,"return to system restores complete runtime direction");
            ps_sleep();tick_at(&ctx,2000);CHECK(epd_get_rotation()==poses[i].rotation,"standby keeps exact signed orientation");
            ps_wake();CHECK(epd_get_rotation()==poses[i].rotation,"wake preserves exact signed orientation");
            ps_rotate();CHECK(epd_get_rotation()==(poses[i].landscape?EPD_ROT_INVERTED_PORTRAIT:EPD_ROT_INVERTED_LANDSCAPE),"manual toggle retains its accepted canonical pair");
        }
    }
    app_ctx_t ctx;orientation_setup(&ctx,1);
    const int signed_y[]={-1000,1000,-1000,1000};
    for(size_t i=0;i<sizeof(signed_y)/sizeof(signed_y[0]);i++){
        test_accel=(sc7a20h_sample_t){0,signed_y[i],0};
        enum EpdRotation wanted=signed_y[i]<0?EPD_ROT_LANDSCAPE:EPD_ROT_INVERTED_LANDSCAPE;
        tick_at(&ctx,500+i*1000);tick_at(&ctx,1000+i*1000);
        CHECK(s_config.landscape&&epd_get_rotation()==wanted,"opposite landscape poses can rotate 180 degrees without a layout toggle");
    }
    orientation_setup(&ctx,0);
    const struct {int x,y;enum EpdRotation rotation;} left_turns[]={
        {0,-1000,EPD_ROT_LANDSCAPE},{-1000,0,EPD_ROT_PORTRAIT},
        {0,1000,EPD_ROT_INVERTED_LANDSCAPE},{1000,0,EPD_ROT_INVERTED_PORTRAIT}
    };
    for(size_t i=0;i<sizeof(left_turns)/sizeof(left_turns[0]);i++){
        test_accel=(sc7a20h_sample_t){left_turns[i].x,left_turns[i].y,0};
        tick_at(&ctx,500+i*1000);tick_at(&ctx,1000+i*1000);
        CHECK(epd_get_rotation()==left_turns[i].rotation,"four successive signed left turns return to canonical portrait without reversing");
    }
    orientation_setup(&ctx,1);test_accel=(sc7a20h_sample_t){0,-1000,0};
    tick_at(&ctx,500);s_config.locked=1;tick_at(&ctx,1000);
    CHECK(epd_get_rotation()==EPD_ROT_INVERTED_LANDSCAPE,"lock blocks 180-degree signed reversal as well as layout changes");
    s_config.locked=0;tick_at(&ctx,1500);
    CHECK(epd_get_rotation()==EPD_ROT_INVERTED_LANDSCAPE,"unlock cannot reuse a pre-lock signed pose sample");
    tick_at(&ctx,2000);CHECK(epd_get_rotation()==EPD_ROT_LANDSCAPE,"two fresh unlocked samples commit left-facing target");
    const struct {enum EpdRotation rotation;int rawx,rawy,x,y;} corners[]={
        {EPD_ROT_INVERTED_PORTRAIT,0,0,0,0},{EPD_ROT_INVERTED_PORTRAIT,683,1215,359,639},
        {EPD_ROT_PORTRAIT,683,1215,0,0},{EPD_ROT_PORTRAIT,0,0,359,639},
        {EPD_ROT_INVERTED_LANDSCAPE,0,1215,0,0},{EPD_ROT_INVERTED_LANDSCAPE,683,0,639,359},
        {EPD_ROT_LANDSCAPE,683,0,0,0},{EPD_ROT_LANDSCAPE,0,1215,639,359}
    };
    for(size_t i=0;i<sizeof(corners)/sizeof(corners[0]);i++){
        epd_set_rotation(corners[i].rotation);
        s_config.landscape=corners[i].rotation==EPD_ROT_LANDSCAPE||corners[i].rotation==EPD_ROT_INVERTED_LANDSCAPE;
        int x=-1,y=-1;map_touch(corners[i].rawx,corners[i].rawy,&x,&y);
        CHECK(x==corners[i].x&&y==corners[i].y,"four-way display and raw touch corner fixtures stay aligned");
    }
    for(int landscape=0;landscape<2;landscape++){
        int x0,y0,x1,y1;s_config.landscape=landscape;
        epd_set_rotation(landscape?EPD_ROT_INVERTED_LANDSCAPE:EPD_ROT_INVERTED_PORTRAIT);map_touch(100,1250,&x0,&y0);
        epd_set_rotation(landscape?EPD_ROT_LANDSCAPE:EPD_ROT_PORTRAIT);map_touch(100,1250,&x1,&y1);
        CHECK(x0==x1&&y0==y1,"physical key row never enters new screen coordinate normalization");
    }
}
static void test_orientation(void){
    app_ctx_t ctx;
    // 此处输入已是BSP设备轴，而非SC7A20H芯片轴。/ Samples here are BSP device axes, not SC7A20H chip axes.
    for(int landscape=0;landscape<2;landscape++)for(int sign=-1;sign<=1;sign+=2){
        orientation_setup(&ctx,!landscape);
        test_accel=landscape?(sc7a20h_sample_t){100,sign*950,100}:(sc7a20h_sample_t){sign*950,100,100};
        tick_at(&ctx,500);CHECK(s_config.landscape==!landscape,"orientation needs two samples in either gravity direction");
        tick_at(&ctx,999);CHECK(s_config.landscape==!landscape,"rapid ticks cannot reuse the same sensor sample");
        tick_at(&ctx,1000);CHECK(s_config.landscape==landscape,"keys-down X gravity is portrait; Y gravity is landscape");
        tick_at(&ctx,1500);CHECK(s_config.landscape==landscape,"stable pose never toggles orientation repeatedly");
    }
    const sc7a20h_sample_t uncertain[]={
        {650,650,0},{0,0,1000},{0,0,-1000},{900,100,900},{100,900,-900},
        {100,700,100},{500,900,100},{100,900,700},{0,0,0}
    };
    for(size_t i=0;i<sizeof(uncertain)/sizeof(uncertain[0]);i++){
        orientation_setup(&ctx,0);test_accel=(sc7a20h_sample_t){100,950,100};tick_at(&ctx,500);
        test_accel=uncertain[i];tick_at(&ctx,1000);CHECK(!s_config.landscape,"flat diagonal and threshold poses preserve orientation");
        test_accel=(sc7a20h_sample_t){100,950,100};tick_at(&ctx,1500);CHECK(!s_config.landscape,"uncertain pose resets prior confirmation");
        tick_at(&ctx,2000);CHECK(s_config.landscape,"new stable pose confirms after uncertainty");
    }
    orientation_setup(&ctx,0);test_accel=(sc7a20h_sample_t){100,950,100};tick_at(&ctx,500);
    test_accel_ok=false;tick_at(&ctx,1000);test_accel_ok=true;tick_at(&ctx,1500);
    CHECK(!s_config.landscape,"read failure breaks orientation confirmation");tick_at(&ctx,2000);CHECK(s_config.landscape,"fresh samples recover after read failure");
    for(int blocked=0;blocked<5;blocked++){
        orientation_setup(&ctx,0);test_accel=(sc7a20h_sample_t){100,950,100};tick_at(&ctx,500);
        cst836u_touch_t held={.touched=true,.count=1,.x=200,.y=200};
        if(blocked==0)s_config.locked=1;
        if(blocked==1)s_panel=1;
        if(blocked==2)ctx.touch=&held;
        if(blocked==3)ctx.consumed=true;
        if(blocked==4){ctx.touch=&held;s_tracking=true;}
        tick_at(&ctx,1000);CHECK(!s_config.landscape,"lock panel touch and consumed frames block rotation");
        s_config.locked=0;s_panel=0;ctx.touch=NULL;ctx.consumed=false;s_tracking=false;
        tick_at(&ctx,1500);CHECK(!s_config.landscape,"interaction interruption clears orientation candidate");
        tick_at(&ctx,2000);CHECK(s_config.landscape,"two fresh unblocked samples rotate");
    }
    for(int quote=0;quote<2;quote++){
        orientation_setup(&ctx,0);s_route=quote?PS_QUOTES:PS_CLOCK;test_accel=(sc7a20h_sample_t){100,950,100};
        tick_at(&ctx,500);tick_at(&ctx,1000);CHECK(!s_config.landscape,"standby preserves its inherited orientation");
    }
    orientation_setup(&ctx,0);
    for(int i=0;i<8;i++){
        test_accel=i%2?(sc7a20h_sample_t){950,100,100}:(sc7a20h_sample_t){100,950,100};
        tick_at(&ctx,500*(i+1));CHECK(!s_config.landscape,"turning back and forth does not confirm an unstable orientation");
    }
}
static void clean_store(void){
    char p[256];const char *suffix[]={"dat","bak","tmp"};for(int i=0;i<3;i++){snprintf(p,sizeof(p),"%s/cfg.%s",s_store_root,suffix[i]);unlink(p);}test_nvs_size=0;test_fail_tmp=false;test_fault_after=0;
}
static void test_storage(void){
    reset_shell();const char *dir=getenv("PICO_SYSTEM_TEST_ROOT");CHECK(dir&&strlen(dir)<sizeof(s_store_root),"private test directory");if(!dir)return;snprintf(s_store_root,sizeof(s_store_root),"%s",dir);
    const uint32_t old=123,new_value=456;
    for(int corrupt=0;corrupt<2;corrupt++)for(int phase=1;phase<=3;phase++){
        clean_store();CHECK(ps_store("cfg",&old,sizeof(old)),"initial store");
        // 独立准备有效备份，不依赖重复保存相同数据制造写入。/ Seed recovery independently of duplicate no-op saves.
        char primary[256],backup[256];snprintf(primary,sizeof(primary),"%s/cfg.dat",s_store_root);snprintf(backup,sizeof(backup),"%s/cfg.bak",s_store_root);
        CHECK(rename(primary,backup)==0,"seed valid backup");CHECK(ps_store("cfg",&old,sizeof(old)),"restore primary beside backup");
        if(corrupt){char p[256];snprintf(p,sizeof(p),"%s/cfg.dat",s_store_root);FILE *f=fopen(p,"wb");CHECK(f!=NULL,"create corrupt primary");if(f){fputs("bad crc",f);fclose(f);}}
        test_fs_step=0;test_fault_after=phase;if(!setjmp(test_crash))(void)ps_store("cfg",&new_value,sizeof(new_value));test_fault_after=0;
        uint32_t value=0;bool loaded=ps_load("cfg",&value,sizeof(value));CHECK(loaded&&(value==old||value==new_value),corrupt?"CRC backup survives update power cut":"atomic store survives update power cut");
    }
    clean_store();CHECK(ps_store("cfg",&old,sizeof(old)),"save old primary");test_fail_tmp=true;bool stored=ps_store("cfg",&new_value,sizeof(new_value));test_fail_tmp=false;
    uint32_t value=0;bool loaded=ps_load("cfg",&value,sizeof(value));CHECK(!stored||(loaded&&value==new_value),"successful NVS fallback cannot reload stale file");
    CHECK(!ps_store("../bad",&old,sizeof(old)),"reject traversal key");clean_store();
}
static void clear_key(const char *key){
    const char *suffix[]={"dat","bak","tmp"};char path[256];
    for(int i=0;i<3;i++){snprintf(path,sizeof(path),"%s/%s.%s",s_store_root,key,suffix[i]);unlink(path);}
}
static void test_reader_storage_keys(void){
    reset_shell();s_store_lost=false;test_fail_tmp=false;test_fault_after=0;
    const char *dir=getenv("PICO_SYSTEM_TEST_ROOT");CHECK(dir!=NULL,"reader private store");if(!dir)return;
    const char *keys[]={"reader-prefs-v1","reader-r2-a-0","reader-r2-index","reader-draft-v1"};
    const size_t sizes[]={sizeof(reader_prefs_t),sizeof(reader_record_chunk_t),sizeof(reader_record_index_t),sizeof(reader_draft_t)};
    const uint32_t marker=0x12345678;
    printf("persistent sizes: system=%zu prefs=%zu records-chunk=%zu index=%zu draft=%zu bytes\n",sizeof(s_config),sizes[0],sizes[1],sizes[2],sizes[3]);
    size_t allocation=sizeof(reader_record_chunk_t)>sizeof(reader_records_t)?sizeof(reader_record_chunk_t):sizeof(reader_records_t);
    uint8_t *saved=malloc(allocation),*loaded=malloc(allocation);
    CHECK(saved&&loaded,"allocate actual reader record size");if(!saved||!loaded){free(saved);free(loaded);return;}
    for(size_t i=0;i<allocation;i++)saved[i]=(uint8_t)(i*37+19);
    for(unsigned i=0;i<sizeof(keys)/sizeof(keys[0]);i++){
        snprintf(s_store_root,sizeof(s_store_root),"%s",dir);clear_key(keys[i]);
        CHECK(ps_store(keys[i],saved,sizes[i]),"actual reader key saves full payload to file");
        memset(loaded,0,sizes[i]);CHECK(ps_load(keys[i],loaded,sizes[i])&&!memcmp(saved,loaded,sizes[i]),"actual reader key loads exact full payload");
        CHECK(!ps_load(keys[i],loaded,sizes[i]-1),"reader payload size mismatch rejected");
        clear_key(keys[i]);s_store_root[0]=0;test_nvs_size=0;
        const bool supported=sizes[i]<=1024&&strlen(keys[i])<=15;
        s_toast[0]=0;CHECK(ps_store(keys[i],saved,sizes[i])==supported,"actual reader key obeys NVS byte and key limits");
        memset(loaded,0,sizes[i]);bool ok=ps_load(keys[i],loaded,sizes[i]);
        CHECK(ok==supported&&(!ok||!memcmp(saved,loaded,sizes[i])),"NVS fallback reads only supported reader payload");
        if(!supported)CHECK(s_toast[0]!=0,"unsupported record storage reports failure");
    }
    const char *invalid[]={NULL,"","../reader-prefs-v1","reader/prefs","reader\\prefs","..","reader.prefs","reader prefs","reader:prefs","\nreader","中文","12345678901234567890123456789012345678901"};
    for(int file=0;file<2;file++){
        snprintf(s_store_root,sizeof(s_store_root),"%s",file?dir:"");
        for(size_t i=0;i<sizeof(invalid)/sizeof(invalid[0]);i++){
            unsigned fs=test_file_opens,nvs=test_nvs_opens;uint32_t value=marker;
            CHECK(!ps_store(invalid[i],&marker,sizeof(marker)),"invalid key cannot write");
            CHECK(!ps_load(invalid[i],&value,sizeof(value))&&value==marker,"invalid key cannot read or mutate destination");
            CHECK(test_file_opens==fs&&test_nvs_opens==nvs,"invalid key rejected before storage I/O");
        }
    }
    s_store_root[0]=0;memset(saved,0x5a,1025);
    CHECK(ps_store("123456789012345",saved,1024),"NVS permits exact 1024-byte and 15-char boundary");
    memset(loaded,0,1024);CHECK(ps_load("123456789012345",loaded,1024)&&!memcmp(saved,loaded,1024),"NVS boundary round-trip");
    unsigned nvs=test_nvs_opens;
    CHECK(!ps_store("123456789012345",saved,1025)&&!ps_load("123456789012345",loaded,1025),"NVS rejects over 1024 bytes");
    CHECK(!ps_store("1234567890123456",saved,4)&&!ps_load("1234567890123456",loaded,4),"NVS rejects over 15 key chars");
    CHECK(test_nvs_opens==nvs,"oversize NVS requests rejected before I/O");
    free(saved);free(loaded);
}
static void test_rooted_records(void){
    reset_shell();const char *base=getenv("PICO_SYSTEM_TEST_ROOT");CHECK(base&&chdir(base)==0,"rooted store private fixture");
    snprintf(test_books_root,sizeof(test_books_root),"tf");mkdir(test_books_root,0700);mkdir("tf/.pico-system",0700);test_roots_available=true;
    snprintf(s_store_root,sizeof(s_store_root),"tf/.pico-system");s_store_is_sd=true;
    uint32_t value=73,loaded=0;bool exists=true;char current[160],other[160];bool is_sd=false;
    CHECK(ps_storage_record_roots(current,other,sizeof(current),&is_sd)&&is_sd&&!strcmp(current,s_store_root)&&!other[0],"only actual available roots are returned");
    CHECK(ps_store_exists_at(current,"rooted-new",&exists)&&!exists,"missing keys are distinguished from read failures");
    CHECK(ps_store_at(current,"rooted-new",&value,sizeof(value)),"explicit canonical root writes a CRC-protected record");
    CHECK(ps_store_exists_at(current,"rooted-new",&exists)&&exists&&ps_load_at(current,"rooted-new",&loaded,sizeof(loaded))&&loaded==value,"rooted round trip");
    CHECK(!ps_store_at("tf/../escape","rooted-new",&value,sizeof(value))&&!ps_load_at("tf/../escape","rooted-new",&loaded,sizeof(loaded)),"unlisted and traversal roots rejected");
    CHECK(!ps_store_at(current,"../escape",&value,sizeof(value)),"rooted operations reject traversal keys");
    unsigned fs=test_file_opens;test_storage_busy=true;
    CHECK(!ps_store_at(current,"rooted-new",&value,sizeof(value))&&!ps_load_at(current,"rooted-new",&loaded,sizeof(loaded))&&!ps_store_exists_at(current,"rooted-new",&exists)&&test_file_opens==fs,"rooted IO cannot bypass maintenance ownership");
    test_storage_busy=false;s_external_active=true;
    CHECK(!ps_store_at(current,"rooted-new",&value,sizeof(value))&&!ps_load_at(current,"rooted-new",&loaded,sizeof(loaded)),"rooted IO cannot bypass USB ownership");s_external_active=false;
    test_roots_available=false;CHECK(!ps_load_at(current,"rooted-new",&loaded,sizeof(loaded)),"lost media rejects a formerly available root");
    test_roots_available=true;clear_key("rooted-new");
}
static void test_checkpoint_gates(void){
    reset_shell();
    for(unsigned flags=0;flags<16;flags++){
        test_storage_busy=flags&1;s_transfer_prepared=flags&2;s_external_active=flags&4;s_store_lost=flags&8;
        unsigned files=test_file_opens,nvs=test_nvs_opens;
        CHECK(ps_storage_save_allowed()==!flags,"all checkpoint gates reject unavailable ownership or media");
        CHECK(files==test_file_opens&&nvs==test_nvs_opens,"checkpoint state gate never opens storage");
    }
    test_storage_busy=s_transfer_prepared=s_external_active=s_store_lost=false;
    unsigned reader=test_reader_invalidations,home=test_home_invalidations;checkpoint_invalidate();
    CHECK(test_reader_invalidations==reader+1&&test_home_invalidations==home+1,"storage boundary invalidates both successful checkpoint caches");
    reader=test_reader_invalidations;home=test_home_invalidations;CHECK(ps_weread_pause(),"seed storage lease for resume boundary");ps_storage_resume();
    CHECK(test_reader_invalidations==reader+1&&test_home_invalidations==home+1,"storage return invalidates caches even for the same card");
}
static void test_storage_adoption(void){
    reset_shell();
    const char *base=getenv("PICO_SYSTEM_TEST_ROOT");
    CHECK(chdir(base)==0,"enter short-path isolated TF fixture");snprintf(test_books_root,sizeof(test_books_root),"tf");mkdir(test_books_root,0700);
    test_roots_available=true;
    test_stat_eio=true;int first_saves=test_module_saves;
    CHECK(!ps_storage_rebind(false)&&test_module_saves==first_saves&&!s_store_root[0],"stat I/O failure cannot be treated as an empty new card");test_stat_eio=false;
    test_mkdir_race=true;
    CHECK(!ps_storage_rebind(false)&&test_module_saves==first_saves&&!s_store_root[0],"concurrent directory creation cannot be overwritten");test_mkdir_race=false;rmdir("tf/.pico-system");
    CHECK(ps_storage_rebind(false),"fresh TF directory adopts current session without restart");
    CHECK(s_store_is_sd&&!s_store_lost&&test_module_saves==4,"adoption saves all module data after binding TF root");
    uint32_t n=0x12345678,m=0;CHECK(ps_store("sentinel",&n,sizeof(n)),"old card sentinel saved");
    s_store_lost=true;int saves=test_module_saves;
    CHECK(!ps_storage_rebind(false)&&test_module_saves==saves&&s_store_lost,"existing card directory never overwritten by current RAM defaults");
    s_store_lost=false;CHECK(ps_load("sentinel",&m,sizeof(m))&&m==n,"old card data survives declined adoption");
    unsigned fs=test_file_opens,nvs=test_nvs_opens;
    s_transfer_prepared=true;m=0;
    CHECK(!ps_store("sentinel",&n,sizeof(n))&&!ps_load("sentinel",&m,sizeof(m))&&m==0,"upload ownership blocks persistence reads and writes");
    CHECK(test_file_opens==fs&&test_nvs_opens==nvs,"upload guard prevents all filesystem and NVS access");s_transfer_prepared=false;
    test_storage_busy=true;m=0;
    CHECK(!ps_store("sentinel",&n,sizeof(n))&&!ps_load("sentinel",&m,sizeof(m))&&m==0,"maintenance blocks persistence reads and writes");
    CHECK(test_file_opens==fs&&test_nvs_opens==nvs,"busy guard prevents all filesystem and NVS access");test_storage_busy=false;
    s_store_lost=true;CHECK(ps_storage_rebind(true)&&!s_store_lost&&test_module_saves==saves+4,"explicitly formatted store allows migration and restores saves");
    s_store_is_sd=false;test_roots_available=false;saves=test_module_saves;
    CHECK(ps_storage_rebind(false)&&test_module_saves==saves,"usable internal file store retained when TF is mounted");
    test_weread_busy=true;int prior_shutdowns=test_connectivity_shutdowns,prior_media=test_media_lost;
    CHECK(!ps_storage_prepare()&&!test_weread_blocked&&prior_shutdowns==test_connectivity_shutdowns&&prior_media==test_media_lost,"active WeRead sync blocks maintenance before network or media detach");test_weread_busy=false;
    test_connectivity_shutdown_fail=true;s_store_lost=false;int prior_lost=test_media_lost;
    CHECK(!ps_storage_prepare()&&!s_store_lost&&test_media_lost==prior_lost,"failed HTTP quiescence blocks all storage maintenance");CHECK(!test_weread_blocked,"failed network shutdown releases WeRead admission barrier");test_connectivity_shutdown_fail=false;
    test_font_suspend_fail=true;s_store_is_sd=true;s_store_lost=false;
    CHECK(!ps_storage_prepare()&&!test_font_suspended&&!s_store_lost,"font detach failure stops maintenance and restores previous storage state");CHECK(!test_weread_blocked,"font failure releases WeRead admission barrier");
    test_font_suspend_fail=false;CHECK(ps_storage_prepare()&&test_font_suspended&&s_store_lost,"successful prepare detaches card consumers");
    CHECK(test_weread_blocked,"successful maintenance keeps WeRead admission blocked");ps_storage_resume();CHECK(!test_font_suspended&&!test_weread_blocked,"maintenance resumes font access and WeRead admission after completion");
}
static bool global_sample(app_ctx_t *ctx,cst836u_touch_t *t,int x,int y,bool down,bool pressed,bool released){
    ps_view_t view=ps_view();
    *t=(cst836u_touch_t){.touched=down,.count=down?1:0};
    if(view.landscape){t->x=y*684/360;t->y=1215-x*1216/640;}
    else{t->x=x*684/360;t->y=y*1216/640;}
    enum EpdRotation rotation=epd_get_rotation();
    if(rotation==EPD_ROT_PORTRAIT||rotation==EPD_ROT_LANDSCAPE){t->x=683-t->x;t->y=1215-t->y;}
    ctx->touch=t;ctx->pressed=pressed;ctx->released=released;ctx->consumed=false;ctx->now_ms+=100;
    app_redraw_t redraw=APP_REDRAW_NONE;return ps_global_input(ctx,&app_book,&redraw);
}
static void test_global_reader(void){
    for(int rotation=0;rotation<4;rotation++)for(int side=0;side<2;side++){
        reset_shell();app_ctx_t ctx={.now_ms=1000};cst836u_touch_t t={0};s_ctx=&ctx;
        ps_global_enable(true);ps_global_page(&ctx,&app_book);epd_set_rotation(rotation);ps_view_t view=ps_view();
        global_sample(&ctx,&t,side?view.w-20:20,view.h-20,true,true,false);
        CHECK(global_sample(&ctx,&t,side?view.w-120:120,view.h-140,true,false,false)&&s_fan,"real-book corner gesture in every orientation");
        ps_back();s_global_claimed=false;test_book_action=-1;
        CHECK(!global_sample(&ctx,&t,view.w/2,view.h-20,true,true,false),"bottom center belongs to book, never virtual Back");
        CHECK(test_book_action==-1,"no hidden center Back action");
        const int ids[]={3,31,32,38,39};
        const app_book_global_action_t actions[]={BOOK_GLOBAL_FONT,BOOK_GLOBAL_PAGE,BOOK_GLOBAL_BOOKMARKS,BOOK_GLOBAL_HIGHLIGHTS,BOOK_GLOBAL_NOTES};
        for(int i=0;i<5;i++){test_book_action=-1;quick_action(ids[i]);CHECK(test_book_action==(int)actions[i],"each reading leaf reaches its distinct real-book action");}
        quick_action(35);CHECK(s_global_redraw==APP_REDRAW_FULL,"real-book screen refresh requests full refresh");
        test_book_can_resume=true;quick_action(7);CHECK(test_power_action==PS_POWER_LOCK&&ctx.request_app==NULL&&!s_book_return,"real book lock retains exact page instance via unified power controller");
        ps_wake();CHECK(test_power_wakes==1&&s_global_app==&app_book&&!s_book_return,"wake keeps same real book instance");
        ps_global_page(&ctx,&app_book);ctx.request_app=NULL;ps_global_open_settings(&ctx);CHECK(s_route==PS_DEVICE&&s_book_return&&ctx.request_app==&app_pico_system,"book settings has independent origin");
        ps_global_page(&ctx,&app_pico_system);ctx.request_app=NULL;ps_back();CHECK(s_legacy==&app_book,"settings returns to real book");
        s_legacy=NULL;ps_global_page(&ctx,&app_book);ctx.request_app=NULL;ps_global_open_settings(&ctx);CHECK(s_route==PS_DEVICE&&ctx.request_app==&app_pico_system,"reopening settings from real book works with stale shell route");
        ps_global_page(&ctx,&app_pico_system);ctx.request_app=NULL;ps_navigate(PS_READING_SETTINGS);CHECK(s_route==PS_DEVICE&&strstr(s_toast,"请返回图书"),"real-book settings never silently edit sample-only typography");
        ps_global_page(&ctx,&app_pico_system);ctx.request_app=NULL;ps_back();s_legacy=NULL;ps_global_page(&ctx,&app_book);ctx.request_app=NULL;s_route=PS_RECORDS;ps_navigate(PS_RECORDS);CHECK(ctx.request_app==&app_pico_system,"real-book records can reopen same shell route");
    }
}
static bool center_sample(app_ctx_t *ctx,cst836u_touch_t *t,int64_t now,bool down,bool press,bool release){
    *t=(cst836u_touch_t){.x=240,.y=1500,.touched=down,.count=down?1:0};ctx->touch=t;ctx->now_ms=now;ctx->pressed=press;ctx->released=release;ctx->consumed=false;app_redraw_t redraw;return ps_global_input(ctx,s_global_app?s_global_app:&app_pico_system,&redraw);
}
static void test_background_usb_input(void){
    for(int real=0;real<2;real++)for(int land=0;land<2;land++){
        reset_shell();s_config.landscape=land;app_ctx_t ctx={0};cst836u_touch_t t={0};
        ps_global_enable(true);ps_global_page(&ctx,real?&app_book:&app_pico_system);s_route=PS_READ;
        test_usb_held=test_usb_background=s_external_active=true;test_dispatch_real=true;
        unsigned file_opens=test_file_opens,consumers=test_file_consumer_ticks;
        center_sample(&ctx,&t,1000,true,true,false);center_sample(&ctx,&t,1060,false,false,true);
        center_sample(&ctx,&t,1360,false,false,false);
        CHECK(s_route==PS_DEVICE&&test_book_action==-1&&!test_book_prepares,"USB background center Back opens settings without invoking closed book callbacks");
        CHECK(!real||ctx.request_app==&app_pico_system,"USB background leaves legacy book through normal shell handoff");
        ctx.request_app=NULL;ps_global_page(&ctx,&app_pico_system);
        ps_navigate(PS_BLUETOOTH);CHECK(s_route==PS_BLUETOOTH&&test_usb_held,"Bluetooth route stays available while host retains exclusive TF ownership");
        ps_event_t tap={.type=PS_TAP,.x=70,.y=100};global_dispatch(&tap);
        CHECK(test_bluetooth_events==1,"Bluetooth controls receive touches during background USB");
        ps_back();CHECK(s_route==PS_DEVICE&&test_usb_held,"Back returns from safe settings without ending USB");
        const ps_route_t unsafe[]={PS_SHELF,PS_READ,PS_RECORDS,PS_GAMES,PS_APPLICATION,PS_INPUT_SETTINGS,PS_SLEEP_SETTINGS,PS_STORAGE,PS_TRANSFER,PS_EDITOR,PS_POWER,PS_DATETIME};
        for(unsigned i=0;i<sizeof(unsafe)/sizeof(unsafe[0]);i++){
            ps_navigate(unsafe[i]);CHECK(s_route==PS_DEVICE&&test_usb_held,"TF-dependent or persisting routes remain closed until reclaim");
            s_route=unsafe[i];global_dispatch(&tap);CHECK(!test_unsafe_events,"background USB never dispatches touches to file consumers");s_route=PS_DEVICE;
        }
        test_nav_items[0]=(ps_navigation_entry_t){.id=256,.route=PS_APPLICATION,.launch=test_navigation_launch};test_navigation_launched=0;
        launcher_switch(0);CHECK(!test_navigation_launched&&s_route==PS_DEVICE,"custom launcher cannot bypass exported-TF guard");
        s_global_app=real?&app_book:NULL;ctx.request_app=NULL;
        for(int id=0;id<QUICK_COUNT;id++)if(id!=14&&id!=34&&id!=35&&id!=36&&id!=37){
            s_route=PS_DEVICE;quick_action(id);CHECK(s_route==PS_DEVICE&&test_book_action==-1&&!test_reader_tools&&!test_reader_toc&&!s_legacy,"background shortcuts cannot reopen files through direct module actions");
        }
        s_global_app=NULL;s_route=PS_DEVICE;ctx.request_app=NULL;
        center_sample(&ctx,&t,2000,true,true,false);center_sample(&ctx,&t,2040,false,false,true);
        center_sample(&ctx,&t,2140,true,true,false);center_sample(&ctx,&t,2200,false,false,true);
        CHECK(s_panel==3,"double-center still opens page switcher while TF is exported");
        ps_back();CHECK(!s_panel&&s_route==PS_DEVICE,"Back dismisses page switcher without eject confirmation");
        ctx.request_app=NULL;ps_view_t v=ps_view();
        touch_sample(&ctx,&t,24,v.h-20,true,true,false);app_redraw_t redraw;
        ps_global_input(&ctx,&app_pico_system,&redraw);
        touch_sample(&ctx,&t,110,v.h-110,true,false,false);ps_global_input(&ctx,&app_pico_system,&redraw);
        CHECK(s_fan,"corner fan continues to open during background USB");
        ps_back();CHECK(!s_fan&&s_route==PS_DEVICE,"Back closes background fan without touching storage");
        uint8_t value=1;CHECK(!ps_store("usb-test",&value,1)&&!ps_load("usb-test",&value,1),"background UI does not grant storage persistence or reads");
        CHECK(test_file_opens==file_opens&&test_file_consumer_ticks==consumers&&test_usb_held,"nonmodal interactions keep all file consumers stopped and TF exclusively exported");
        test_usb_busy=true;ps_navigate(PS_BLUETOOTH);CHECK(s_route==PS_DEVICE,"worker transition still blocks route entry");
        test_usb_held=test_usb_background=test_usb_busy=s_external_active=false;s_global_app=NULL;
        ps_navigate(PS_SHELF);CHECK(s_route==PS_SHELF,"normal navigation resumes after ownership returns");
    }
}
static void test_launcher_controls(void){
    for(int real=0;real<2;real++){
        reset_shell();app_ctx_t ctx={0};cst836u_touch_t t={0};ps_global_enable(true);ps_global_page(&ctx,real?&app_book:&app_pico_system);s_route=PS_READ;
        center_sample(&ctx,&t,1000,true,true,false);center_sample(&ctx,&t,1060,false,false,true);CHECK(test_book_action==-1&&s_route==PS_READ,"single release waits for double-tap window");
        center_sample(&ctx,&t,1359,false,false,false);CHECK(test_book_action==-1&&s_route==PS_READ,"single does not fire early");
        center_sample(&ctx,&t,1360,false,false,false);CHECK(real?test_book_action==BOOK_GLOBAL_BACK:s_route==PS_SHELF,"single center invokes global Back");
        test_book_action=-1;ctx.request_app=NULL;s_route=PS_READ;
        center_sample(&ctx,&t,2000,true,true,false);center_sample(&ctx,&t,2040,false,false,true);center_sample(&ctx,&t,2140,true,true,false);center_sample(&ctx,&t,2200,false,false,true);
        CHECK(s_panel==3&&test_book_action==-1,"double opens exactly three-page switcher");
        s_panel=0;center_sample(&ctx,&t,2300,true,true,false);center_sample(&ctx,&t,2340,false,false,true);center_sample(&ctx,&t,2600,true,true,false);center_sample(&ctx,&t,2700,false,false,true);CHECK(s_panel==3&&test_book_action==-1,"double qualifies on second down even when release crosses deadline");
        center_sample(&ctx,&t,3000,true,true,false);center_sample(&ctx,&t,3650,true,false,false);CHECK(s_route==PS_SHELF&&!s_panel&&s_last_input==3000,"hold returns to bookshelf and renews idle timer without delayed Back");
        ps_global_page(&ctx,real?&app_book:&app_pico_system);ctx.request_app=NULL;s_route=PS_READ;
        center_sample(&ctx,&t,4000,true,true,false);center_sample(&ctx,&t,4040,false,false,true);ps_global_cancel_input();center_sample(&ctx,&t,4500,false,false,false);CHECK(s_route==PS_READ,"read-error cancellation removes pending action");
        center_sample(&ctx,&t,5000,true,true,false);t.count=2;ctx.pressed=false;ctx.now_ms=5200;app_redraw_t r;ps_global_input(&ctx,real?&app_book:&app_pico_system,&r);center_sample(&ctx,&t,5800,false,false,false);CHECK(s_route==PS_READ&&!s_center_pending,"multitouch cancels center action");
        t=(cst836u_touch_t){.x=240,.y=1500,.touched=true,.count=2};ctx.touch=&t;ctx.now_ms=5900;ctx.pressed=true;ctx.released=false;test_book_action=-1;CHECK(ps_global_input(&ctx,real?&app_book:&app_pico_system,&r)&&!s_center_down&&!s_center_pending&&test_book_action==-1,"initial multi-finger center is consumed before legacy key handler");
        center_sample(&ctx,&t,6000,true,true,false);center_sample(&ctx,&t,6040,false,false,true);apply_rotation(EPD_ROT_INVERTED_LANDSCAPE);center_sample(&ctx,&t,6400,false,false,false);CHECK(s_route==PS_READ&&!s_center_pending,"rotation cancels delayed center action");
        center_sample(&ctx,&t,7000,true,true,false);center_sample(&ctx,&t,7040,false,false,true);system_exit(&ctx);center_sample(&ctx,&t,7400,false,false,false);CHECK(s_route==PS_READ&&!s_center_pending,"leaving shell cancels delayed center action");
        s_fan=true;ps_global_cancel_input();ctx.touch=&t;ctx.pressed=ctx.released=false;ctx.now_ms=7600;app_redraw_t restore=APP_REDRAW_NONE;CHECK(ps_global_input(&ctx,real?&app_book:&app_pico_system,&restore)&&restore==APP_REDRAW_PAGE&&!s_overlay_restore,"read-error cancellation restores page behind fan");
    }
    reset_shell();CHECK(s_shortcuts.count==14&&!memcmp(s_quick_slots,s_quick_defaults,FAN_SLOTS),"approved direct slot defaults");
    s_shortcuts.count=3;s_shortcuts.ids[0]=14;s_shortcuts.ids[1]=0;s_shortcuts.ids[2]=7;int ids[QUICK_COUNT];CHECK(enabled(ids)==3&&ids[0]==14&&ids[1]==0&&ids[2]==7,"menu honors stored order");
    CHECK(quick_unavailable(19)!=NULL,"selection actions explain unavailable context");
    for(int root=0;root<3;root++){ps_switch(root);ps_route_t origin=s_route;ps_global_open_settings(NULL);ps_back();CHECK(s_route==origin,"settings returns to exact root");}
    ps_switch(0);ps_event_t e={.type=PS_SWIPE_L};global_dispatch(&e);CHECK(s_route==PS_HA,"horizontal swipe bookshelf to HA");global_dispatch(&e);CHECK(s_route==PS_GAMES,"horizontal swipe HA to games");global_dispatch(&e);CHECK(s_route==PS_GAMES,"root swipe stops at final page");
}
static void test_visible_back(void){
    const ps_route_t headings[]={PS_DEVICE,PS_WIFI,PS_WIRELESS_MANAGE,PS_HA_SETTINGS,PS_PRINTER_SETTINGS,PS_READING_SETTINGS,PS_INPUT_SETTINGS,PS_SLEEP_SETTINGS,PS_STORAGE,PS_TRANSFER,PS_USB_TRANSFER,PS_BLUETOOTH,PS_DATETIME,PS_POWER,PS_NAV_SETTINGS,PS_RECORDS,PS_EDITOR,PS_IMPORT,PS_PLAYGAME,PS_ARCADEGAME,PS_APPLICATION};
    const int points[][2]={{6,30},{23,47},{39,63}};
    for(int land=0;land<2;land++)for(unsigned i=0;i<sizeof(headings)/sizeof(headings[0]);i++)for(unsigned p=0;p<sizeof(points)/sizeof(points[0]);p++){
        reset_shell();s_config.landscape=land;ps_navigate(headings[i]);ps_event_t e={.type=PS_TAP,.x=points[p][0],.y=points[p][1]};
        CHECK(global_dispatch(&e)&&s_route==PS_SHELF&&s_depth==0,"every visible heading Back returns to its exact origin across the whole arrow");
    }
    for(int land=0;land<2;land++)for(int printer=0;printer<2;printer++){
        reset_shell();s_config.landscape=land;s_route=printer?PS_PRINTERS:PS_HA;test_home_layer=true;
        ps_event_t e={.type=PS_TAP,.x=23,.y=47};CHECK(global_dispatch(&e)&&!test_home_layer&&s_route==(printer?PS_PRINTERS:PS_HA),"home and printer heading Back close child state without leaving root");
    }
    reset_shell();ps_navigate(PS_RECORDS);test_reader_layer=true;ps_event_t e={.type=PS_TAP,.x=23,.y=47};
    CHECK(global_dispatch(&e)&&!test_reader_layer&&s_route==PS_RECORDS,"record heading Back dismisses the selected record first");
    CHECK(global_dispatch(&e)&&s_route==PS_SHELF,"second record Back returns to the page origin");
    for(int land=0;land<2;land++){
        reset_shell();s_config.landscape=land;ps_navigate(PS_POWER);test_power_confirmation=true;
        CHECK(global_dispatch(&e)&&!test_power_confirmation&&s_route==PS_POWER,"power heading Back dismisses confirmation before leaving settings");
        CHECK(global_dispatch(&e)&&s_route==PS_SHELF,"second power Back returns to exact origin");
    }
    reset_shell();s_route=PS_READ;CHECK(!global_dispatch(&e)&&s_route==PS_READ,"plain text has no invisible heading Back target");
    reset_shell();ps_navigate(PS_RECORDS);e.x=-1;CHECK(!global_dispatch(&e)&&s_route==PS_RECORDS,"offscreen contact never triggers Back");
    reset_shell();ps_navigate(PS_EDITOR);test_storage_busy=true;e.x=23;
    CHECK(global_dispatch(&e)&&s_route==PS_EDITOR,"storage maintenance preserves its existing navigation guard");
    reset_shell();s_route=PS_SHELF;test_usb_held=test_usb_background=s_external_active=true;
    CHECK(global_dispatch(&e)&&s_route==PS_DEVICE&&test_usb_held,"USB frozen heading Back reaches safe settings without releasing host-owned TF");
}
static void test_child_wireless_maintenance(void){
    reset_shell();cst836u_touch_t t={0};app_ctx_t ctx={.touch=&t,.now_ms=1000};app_redraw_t redraw=APP_REDRAW_NONE;
    ps_global_enable(true);ps_global_page(&ctx,&app_book);s_route=PS_READ;
    ps_global_input(&ctx,&app_book,&redraw);CHECK(test_maintenance_calls==1&&!ctx.request_app,"idle real book services wired setup without leaving the page");
    t.touched=true;t.count=1;t.x=300;t.y=300;ctx.pressed=true;ps_global_input(&ctx,&app_book,&redraw);
    CHECK(test_maintenance_calls==1,"held contact defers optional setup polling");
    t.touched=false;ctx.pressed=false;s_panel=1;ps_global_input(&ctx,&app_book,&redraw);CHECK(test_maintenance_calls==1,"open overlay defers optional setup polling");
    s_panel=0;test_ink_pending=true;ps_global_input(&ctx,&app_book,&redraw);CHECK(test_maintenance_calls==1,"unfinished display phase defers optional setup polling");
    test_ink_pending=false;ps_global_page(&ctx,&app_pico_system);ps_global_input(&ctx,&app_pico_system,&redraw);
    CHECK(test_maintenance_calls==1,"product shell keeps its single existing on-tick maintenance owner");
}
static void editor_event(ps_event_type_t type,int x,int y){ps_event_t e={.type=type,.x=x,.y=y};(void)quick_settings_event(&e);}
static void editor_tap(int x,int y){editor_event(PS_PRESS,x,y);editor_event(PS_RELEASE,x,y);editor_event(PS_TAP,x,y);}
static void test_quick_editor(void){
    for(int land=0;land<2;land++)for(int side=0;side<2;side++){
        reset_shell();s_config.landscape=land;s_store_root[0]=0;test_nvs_size=0;test_nvs_key[0]=0;
        uint8_t system_before[sizeof(s_config)];memcpy(system_before,&s_config,sizeof(s_config));
        ps_navigate(PS_QUICK_SETTINGS);s_quick_edit.side=side;
        CHECK(s_quick_edit.active&&fan_frame_active(),"enter editor starts isolated draft and cache frame");
        uint8_t original[FAN_SLOTS];memcpy(original,s_quick_slots,FAN_SLOTS);fan_item_t a[QUICK_COUNT];fan_layout(a);
        unsigned writes=test_nvs_opens;
        for(int from=0;from<FAN_SLOTS;from++)for(int to=0;to<FAN_SLOTS;to++) {
            memcpy(s_quick_edit.slots,original,FAN_SLOTS);s_quick_edit.selected=-1;
            editor_event(PS_PRESS,a[from].x,a[from].y);editor_event(PS_LONG,a[from].x,a[from].y);
            editor_event(PS_MOVE,a[(to+1)%FAN_SLOTS].x,a[(to+1)%FAN_SLOTS].y);
            CHECK(!memcmp(s_quick_slots,original,FAN_SLOTS)&&test_nvs_opens==writes,"hover changes no live slots or persistent storage");
            editor_event(PS_RELEASE,a[to].x,a[to].y);
            CHECK(s_quick_edit.slots[to]==original[from]&&s_quick_edit.slots[from]==original[to],"UP recalculates target and swaps every ring position");
            CHECK(!s_quick_edit.dragging,"release ends drag");
        }
        memcpy(s_quick_edit.slots,original,FAN_SLOTS);
        editor_event(PS_PRESS,a[0].x,a[0].y);editor_event(PS_LONG,a[0].x,a[0].y);editor_event(PS_MOVE,a[9].x,a[9].y);
        editor_event(PS_RELEASE,side?ps_view().w-1:0,ps_view().h-1);CHECK(!memcmp(s_quick_edit.slots,original,FAN_SLOTS),"UP in safe area cancels stale hover swap");
        editor_event(PS_PRESS,a[0].x,a[0].y);editor_event(PS_LONG,a[0].x,a[0].y);editor_event(PS_MOVE,a[9].x,a[9].y);editor_event(PS_CANCEL,0,0);editor_event(PS_RELEASE,a[9].x,a[9].y);
        CHECK(!memcmp(s_quick_edit.slots,original,FAN_SLOTS),"contact cancel cannot commit swap");
        editor_tap(a[0].x,a[0].y);CHECK(s_quick_mode==1&&s_quick_edit.selected==0,"tap opens replacement catalog for actual inner position");
        quick_edit_assign(7);CHECK(s_quick_edit.slots[0]==7&&s_quick_edit.slots[9]==0,"selecting occupied action swaps rather than duplicates");
        ps_back();CHECK(s_route==PS_QUICK_SETTINGS&&!s_quick_mode&&s_quick_edit.active&&s_quick_edit.slots[0]==7,"physical Back from catalog returns one level and retains draft");
        int ix=fan_prompt_x(),bw=land?80:76;
        editor_tap(ix+bw+9,190);CHECK(!memcmp(s_quick_edit.slots,original,FAN_SLOTS),"Undo restores previous arrangement");
        s_quick_edit.selected=0;editor_tap(ix+2*(bw+8)+2,230);CHECK(s_quick_edit.slots[0]==QUICK_EMPTY,"remove leaves editable empty position");
        fan_layout(a);CHECK(fan_pick(a,FAN_SLOTS,a[0].x,a[0].y,-1)==0,"empty position remains selectable in editor");
        editor_tap(a[0].x,a[0].y);quick_edit_assign(36);ps_back();CHECK(s_quick_edit.slots[0]==36,"any catalog function replaces empty inner position");
        editor_tap(a[0].x,a[0].y);s_quick_scroll=7;editor_tap(100,188);
        CHECK(s_quick_mode==0&&s_quick_edit.slots[0]==7,"catalog row tap assigns selected function then returns to diagram");
        CHECK(test_nvs_opens==writes,"draft replacement/remove/undo performs no writes");
        ps_back();CHECK(!s_quick_edit.active&&s_route==PS_SHELF&&!memcmp(s_quick_slots,original,FAN_SLOTS)&&test_nvs_opens==writes,"Back from editor discards draft without save");
        ps_navigate(PS_QUICK_SETTINGS);s_quick_edit.side=side;s_quick_edit.selected=0;quick_edit_assign(36);
        ix=fan_prompt_x();editor_tap(ix+2*(bw+8)+2,190);CHECK(!s_quick_edit.active&&s_route==PS_SHELF&&s_quick_slots[0]==36,"Done persists edited positions then returns");
        memset(s_quick_slots,QUICK_EMPTY,FAN_SLOTS);quick_load();CHECK(s_quick_slots[0]==36&&quick_enabled(0),"reload restores layout and retains replaced function as unplaced");
        CHECK(!memcmp(system_before,&s_config,sizeof(s_config)),"editor leaves system and WiFi blob unchanged");
        int catalog[QUICK_COUNT],total=quick_catalog(catalog),rows=quick_catalog_rows();
        for(int page=0;page<(total+rows-1)/rows;page++)for(int row=0;row<rows&&page*rows+row<total;row++)for(int bottom=0;bottom<2;bottom++) {
            ps_navigate(PS_QUICK_SETTINGS);s_quick_edit.side=side;s_quick_edit.selected=0;s_quick_mode=1;s_quick_scroll=page*rows;
            EpdRect box=quick_catalog_rect(row);editor_tap(box.x+4,bottom?box.y+box.height-1:box.y);
            CHECK(s_quick_mode==0&&s_quick_edit.slots[0]==catalog[page*rows+row],"every displayed catalog row top/bottom selects same action on every page");
            ps_back();
        }
        ps_navigate(PS_QUICK_SETTINGS);s_quick_edit.selected=0;s_quick_mode=1;s_quick_scroll=((total-1)/rows)*rows;
        editor_tap(ps_view().w-50,ps_view().h-62);CHECK(s_quick_scroll==0,"last catalog page advances to first");
        editor_tap(20,ps_view().h-62);CHECK(s_quick_scroll==((total-1)/rows)*rows,"first catalog page wraps back to last");
        EpdRect gap=quick_catalog_rect(0);editor_tap(gap.x+4,gap.y+gap.height+1);CHECK(s_quick_mode==1,"gap outside visible card cannot select adjacent action");ps_back();ps_back();
        ps_navigate(PS_QUICK_SETTINGS);s_quick_edit.side=side;s_quick_edit.selected=0;quick_edit_assign(37);
        char unavailable[180];snprintf(unavailable,sizeof(unavailable),"/private/tmp/pico-edit-fail-%d-%d",land,side);snprintf(s_store_root,sizeof(s_store_root),"%s/missing",unavailable);s_store_is_sd=true;s_store_lost=false;
        ix=fan_prompt_x();editor_tap(ix+2*(bw+8)+2,190);
        CHECK(s_quick_edit.active&&s_quick_edit.save_failed&&s_quick_slots[0]==36&&s_quick_edit.slots[0]==37,"save failure retains draft and live configuration");
        s_store_root[0]=0;s_store_is_sd=false;editor_tap(ix+2*(bw+8)+2,190);CHECK(!s_quick_edit.active&&s_quick_slots[0]==37,"failed save may retry successfully");
    }
}

static void test_editor_toast_expiry(void){
    const ps_route_t routes[]={PS_EDITOR,PS_SHELF};
    for(unsigned i=0;i<sizeof(routes)/sizeof(routes[0]);i++){
        reset_shell();cst836u_touch_t touch={0};app_ctx_t ctx={.now_ms=1000,.touch=&touch};
        s_ctx=&ctx;s_route=routes[i];ps_toast("保存状态");
        CHECK(s_toast[0]&&s_toast_until==4000,"toast records a definite expiry without immediate editor invalidation");
        s_dirty=false;ctx.now_ms=3999;
        CHECK(on_tick(&ctx)==APP_REDRAW_NONE&&s_toast[0]&&!test_editor_invalidations,
              "live toast and editor reference stay stable before expiry");
        ctx.now_ms=4000;
        CHECK(on_tick(&ctx)==APP_REDRAW_PAGE&&!s_toast[0]&&test_editor_invalidations==1,
              "exact toast expiry invalidates editor damage reference and requests one clearing redraw");
        ctx.now_ms=4005;
        CHECK(on_tick(&ctx)==APP_REDRAW_NONE&&test_editor_invalidations==1,
              "expired toast cannot invalidate or redraw repeatedly on later idle ticks");
    }
}
static void test_ink_tick_boundaries(void){
    const enum EpdRotation rotations[]={EPD_ROT_PORTRAIT,EPD_ROT_INVERTED_PORTRAIT,EPD_ROT_LANDSCAPE,EPD_ROT_INVERTED_LANDSCAPE};
    for(unsigned rotation=0;rotation<4;rotation++)for(int application=0;application<2;application++){
        reset_shell();cst836u_touch_t touch={.touched=true,.count=1,.x=200,.y=300};app_ctx_t ctx={.now_ms=1000,.touch=&touch,.sensor_ready=true};s_ctx=&ctx;
        s_route=application?PS_APPLICATION:PS_EDITOR;test_ink_mode=!application;test_apps_ink_mode=application;test_accel_ok=true;test_accel=(sc7a20h_sample_t){0,0,1000};epd_set_rotation(rotations[rotation]);
        test_ink_active=false;on_tick(&ctx);
        CHECK(!test_poll_calls&&!test_maintenance_calls&&!test_accel_calls,"first raw handwriting contact suppresses synchronous optional I2C before reader capture");
        touch.touched=false;touch.count=0;ctx.now_ms=1600;test_ink_active=!application;on_tick(&ctx);
        if(!application)CHECK(!test_poll_calls&&!test_maintenance_calls&&!test_accel_calls,"reader capture survives missing raw contact without polling during an unfinished stroke");
        test_ink_active=false;ctx.now_ms=2200;s_tracking=false;test_ink_pending=true;s_dirty=false;
        test_poll_calls=test_maintenance_calls=test_accel_calls=test_network_ticks=0;
        CHECK(on_tick(&ctx)==APP_REDRAW_DONE&&test_ink_steps==1&&!test_ink_pending,"idle tick drains exactly one phase and reports already presented");
        CHECK(!test_poll_calls&&!test_maintenance_calls&&!test_accel_calls&&!test_network_ticks,"remaining phases drain before optional hardware, network and maintenance polling");
        ctx.now_ms=2250;on_tick(&ctx);
        CHECK(test_poll_calls&&test_maintenance_calls&&test_accel_calls&&test_network_ticks,"fully drained pen-up idle restores deferred optional polling");
        test_ink_pending=true;test_ink_fail=true;ctx.now_ms=2300;
        CHECK(on_tick(&ctx)==APP_REDRAW_PAGE&&s_force_full&&!test_ink_pending,"drain failure cancels debt and requests strong page recovery");
        test_ink_fail=false;s_force_full=false;test_ink_pending=true;ps_global_cancel_input();
        CHECK(s_force_full&&!test_ink_pending&&test_apps_invalidations,"global input interruption cancels unfinished drive and invalidates app canvas");
        CHECK(!ps_power_partial_safe(),"unfinished drive blocks retained-page partial lock");
        s_force_full=false;s_overlay_restore=true;CHECK(!ps_power_partial_safe(),"cancelled overlay blocks retained-page partial lock");
        s_overlay_restore=false;test_ink_pending=true;CHECK(!ps_power_partial_safe(),"live drive blocks retained-page partial lock");
        test_ink_pending=false;s_dirty=false;CHECK(ps_power_partial_safe(),"clean retained page allows partial lock");
        s_dirty=true;CHECK(!ps_power_partial_safe(),"unsubmitted page state blocks frame reuse");s_dirty=false;
        s_quick_edit.active=true;s_quick_edit.source=2;s_quick_edit.target=3;s_quick_edit.dragging=true;
        ps_global_cancel_input();CHECK(s_overlay_restore&&!s_quick_edit.dragging&&!ps_power_partial_safe(),"cancelled quick drag requires redraw before capture");
        s_overlay_restore=false;s_quick_edit.active=false;
        s_force_full=false;test_ink_pending=true;apply_rotation(rotations[(rotation+2)%4]);
        CHECK(s_force_full&&!test_ink_pending,"all rotation interruptions force cleanup when a phase remains");
    }
}
static void test_media_restoration(void){
    reset_shell();s_ready=true;s_store_is_sd=true;
    test_card=(read_pico_sd_info_t){.present=true,.mounted=true,.media_generation=17};test_card_error=ESP_OK;
    store_media_bind();CHECK(s_store_media_known&&s_store_media_generation==17,"bind only verified locally owned media");
    s_store_lost=true;test_font_suspended=true;
    int writes=test_module_saves;unsigned opens=test_file_opens,nvs=test_nvs_opens;
    ps_media_restored();
    CHECK(!s_store_lost&&!test_font_suspended&&test_media_scans==1&&test_media_revisions==1,"same media restores saves, fonts, library and apps");
    CHECK(test_module_saves==writes&&test_file_opens==opens&&test_nvs_opens==nvs,"restore never overwrites stored settings or RAM notes");
    s_store_lost=true;test_card.media_generation=19;ps_media_restored();
    CHECK(s_store_lost&&test_media_scans==2,"replacement media can be browsed without inheriting old session writes");
    for(int gate=0;gate<6;gate++){
        int scans=test_media_scans;
        s_external_active=gate==0;s_transfer_prepared=gate==1;test_storage_busy=gate==2;test_usb_held=gate==3;test_card.exported=gate==4;test_card_error=gate==5?-1:ESP_OK;
        ps_media_restored();CHECK(scans==test_media_scans&&s_store_lost,"busy, exported or failed media cannot restore consumers");
    }
    s_external_active=s_transfer_prepared=test_storage_busy=test_usb_held=test_card.exported=false;test_card_error=ESP_OK;
    test_card.media_generation=17;snprintf(s_store_root,sizeof(s_store_root),"/existing/.pico-system");
    CHECK(ps_storage_rebind(false)&&!s_store_lost&&test_module_saves==writes,"same generation rebind keeps existing directory without migration or overwrite");
    s_store_lost=true;test_card.media_generation=20;test_roots_available=false;
    CHECK(!ps_storage_rebind(false)&&s_store_lost,"another card cannot regain writes through rebind");
    const char *base=getenv("PICO_SYSTEM_TEST_ROOT");
    CHECK(base&&mkdir(base,0700)==0,"create isolated replacement-media root");
    CHECK(chdir(base)==0,"enter replacement-media fixture");
    snprintf(test_books_root,sizeof(test_books_root),"replacement-tf");mkdir(test_books_root,0700);test_roots_available=true;
    struct stat st;
    CHECK(stat("replacement-tf/.pico-system",&st)!=0&&errno==ENOENT,"replacement starts without a system directory");
    opens=test_file_opens;
    CHECK(!ps_storage_rebind(false)&&s_store_lost,"empty replacement card cannot adopt the previous card's RAM");
    CHECK(test_module_saves==writes&&test_file_opens==opens&&stat("replacement-tf/.pico-system",&st)!=0,"replacement rebind creates no directory or files");
}
int main(void){test_media_restoration();test_app_routes();test_navigation();test_fan();test_touch();test_ink_touch_sampling();test_fan_release_and_migration();test_v3_projection_and_gate();test_fan_paging_touch();test_power_navigation();test_display_orientation();test_orientation_bsp_pipeline();test_orientation();test_storage();test_reader_storage_keys();test_rooted_records();test_storage_adoption();test_checkpoint_gates();test_global_reader();test_background_usb_input();test_launcher_controls();test_visible_back();test_child_wireless_maintenance();test_quick_editor();test_editor_toast_expiry();test_ink_tick_boundaries();printf("shell host: %u checks; %u failures\n",test_checks,test_failures);return test_failures?1:0;}
'''

with tempfile.TemporaryDirectory(prefix="pico-shell-host-") as work:
    work = Path(work)
    source = work / "shell.c"
    source.write_text(prelude + types + "\n" + prefix + fan_type + stubs + "\n#define fopen test_fopen\n#define rename test_rename\n#define unlink test_unlink\n#define stat(p,s) test_stat(p,s)\n#define mkdir test_mkdir\n" + functions + checks)
    binary = work / "shell"
    sanitizer_flags = ['-fsanitize=address,undefined', '-fno-omit-frame-pointer'] if ARGS.sanitize else []
    subprocess.run([os.environ.get("CC", "cc"), "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror", "-Wno-unused-function", "-Wno-unused-variable", "-Wno-unused-parameter"] + sanitizer_flags + shlex.split(os.environ.get("CFLAGS", "")) + [str(source), "-lm", "-o", str(binary)], check=True)
    env = os.environ.copy()
    env["PICO_SYSTEM_TEST_ROOT"] = str(work / "private-store")
    result = subprocess.run([str(binary)], env=env)
    raise SystemExit(result.returncode)

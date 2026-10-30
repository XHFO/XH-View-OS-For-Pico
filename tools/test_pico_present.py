#!/usr/bin/env python3
"""Exercise the actual product present/picture-policy functions without hardware.

Display I/O, editor damage rendering, and the separately tested cached fan
painter are stubbed. The real shell/book dispatchers, editor presenter and grayscale policy are extracted
without rewriting their bodies. No serial ports are opened. Set
CFLAGS='-fsanitize=address,undefined -fno-omit-frame-pointer' for sanitizer QA.
"""
from pathlib import Path
import os
import re
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
SYSTEM = (ROOT / "main/system/pico_system.c").read_text()
READER = (ROOT / "main/system/pico_reader.c").read_text()
DRAW = (ROOT / "main/system/pico_draw.c").read_text()
HEADER = (ROOT / "main/system/pico_system.h").read_text()
APP_HEADER = (ROOT / "main/app/app.h").read_text()
EPD_HEADER = (ROOT / "components/epdiy/src/epdiy.h").read_text()


def function(source, name):
    # Same comment/string-aware extraction as test_pico_system.py; importing
    # that executable test would run its unrelated shell cases as a side effect.
    match = re.search(
        r"(?:^|\n)((?:static\s+)?[\w\s*]+?\b"
        + re.escape(name) + r"\([^;]*?\)\s*\{)", source
    )
    if not match:
        raise RuntimeError(f"Cannot locate actual function: {name}")
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
    raise RuntimeError(f"Unbalanced actual function: {name}")


def enum(source, name):
    return re.search(
        r"typedef enum\s*\{[^}]*\}\s*" + re.escape(name) + r";", source
    ).group()


prelude = r'''
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>

typedef struct { uint8_t *back_fb; } EpdiyHighlevelState;
typedef struct { int x,y,width,height; } EpdRect;
typedef struct { uint8_t *fb; EpdiyHighlevelState *hl; int64_t now_ms; } app_ctx_t;
enum EpdDrawError { EPD_DRAW_SUCCESS = 0, EPD_DRAW_EMPTY_LINE_QUEUE = 1 };
enum EpdDrawMode { MODE_DU = 1, MODE_GC16 = 2, MODE_GL16 = 5 };
enum { WIDTH = 1216, HEIGHT = 684, FRAME_BYTES = WIDTH * HEIGHT / 2, GUARD_BYTES = 32 };
static struct { uint8_t before[GUARD_BYTES], fb[FRAME_BYTES], after[GUARD_BYTES]; } guarded;
static uint8_t original[FRAME_BYTES], binary[FRAME_BYTES], back[FRAME_BYTES], selective[FRAME_BYTES];
static struct { bool landscape; } s_config;
typedef struct {int w,h;bool landscape;} ps_view_t;
static ps_view_t ps_view(void){return s_config.landscape?(ps_view_t){640,360,true}:(ps_view_t){360,640,false};}
static enum EpdRotation test_rotation = EPD_ROT_INVERTED_PORTRAIT;
static enum EpdRotation epd_get_rotation(void) { return test_rotation; }
static bool s_fast, s_fan, s_force_full, test_editor_active;
static bool s_ink_gray_settle,s_dirty;static int64_t s_ink_gray_due;
static bool test_animating=true,test_snake,test_paused;
static bool ps_home_animating(void){return test_animating;}
static bool ps_games_is_snake(void){return test_snake;}
static bool ps_games_paused(void){return test_paused;}
#define fan_frame_active() (s_fan || test_editor_active)
typedef struct {int unused;}app_desc_t;
static const app_desc_t fan_app={0}, other_app={0};
static bool test_usb_background;
static bool ps_usb_background(void){return test_usb_background;}
static bool local_hold(void){return ps_usb_background();}
static void ps_usb_background_render(uint8_t *fb){fb[FRAME_BYTES-1]=0x22;}
static bool s_global_enabled;static const app_desc_t *s_global_app;
static int s_panel;
static struct { bool comic; } rb[6];
static int active_book;
static ps_route_t route;
#define s_route route
static unsigned checks, failures, renders, modes, fulls, calls, changed_areas;
static unsigned fan_present_calls, fan_reset_calls;
static bool fan_present_result = true;
static unsigned fail_remaining;
static int64_t fake_now_us;
static unsigned timer_reads, timing_reads, logs;
static char log_text[768];
static enum EpdDrawMode last_mode;
static int E0470_FOLLOW_WAVEFORM,E0470_WAVEFORM;
static unsigned follow_updates;
static char s_toast[96];
static bool editor_case,test_partial_ready,test_layout_change;
static bool test_ink_mode,test_apps_ink,test_apps_ready,test_ink_pending,test_ink_fail;
static unsigned ink_presents,ink_steps,ink_cancels,ink_remaining,full_thresholds,apps_invalidations;
static unsigned editor_partial_calls,editor_invalidations,editor_thresholds,guard_calls;
static int test_damage_count;
static EpdRect test_damage[4],submitted_damage[4],expected_union[5];
static unsigned submitted_count;
static enum EpdDrawError guarded_error;
static char order[8];
static const uint8_t *expected;
static EpdiyHighlevelState hl={.back_fb=back};
static app_ctx_t ctx = { .fb = guarded.fb, .hl = &hl };
#define CHECK(expr,label) do { checks++; if (!(expr)) { \
    fprintf(stderr,"FAIL %s (line %d)\n",label,__LINE__); failures++; } } while (0)
static bool fan_present(app_ctx_t *c) {
    CHECK(c == &ctx && c->fb == guarded.fb && c->hl == &hl,
          "cached fan receives original context, framebuffer, and reference");
    fan_present_calls++;
    s_fast = false;
    return fan_present_result;
}
static void fan_present_reset(void) { fan_reset_calls++; }
static int epd_width(void) { return WIDTH; }
static int epd_height(void) { return HEIGHT; }
static ps_route_t ps_route(void) { return route; }
static int64_t esp_timer_get_time(void) {
    if(editor_case){timer_reads++;int64_t value=fake_now_us;fake_now_us+=7;return value;}
    CHECK(timer_reads < 4, "bounded four-read timing overhead per custom present");
    if (timer_reads == 0) CHECK(!renders && !calls, "timing starts before render");
    if (timer_reads == 1) {
        CHECK(renders == 1 && !calls, "render timestamp precedes threshold and display");
        CHECK(!memcmp(guarded.fb, original, FRAME_BYTES), "render timestamp sees unquantized pixels");
    }
    if (timer_reads == 2) {
        CHECK(renders == 1 && !calls, "threshold timestamp precedes display");
        CHECK(!memcmp(guarded.fb, expected, FRAME_BYTES), "threshold timestamp sees complete intended pixels");
    }
    if (timer_reads == 3) CHECK(calls > 0, "refresh timestamp follows all display calls");
    timer_reads++;
    int64_t result = fake_now_us;
    fake_now_us += 7;
    return result;
}
static void epd_hl_last_timing(int *diff_ms, int *draw_ms, int *copy_ms) {
    CHECK((editor_case||timer_reads == 4) && calls > 0, "last highlevel timing retrieved after completed refresh");
    timing_reads++;
    *diff_ms = 4; *draw_ms = 225; *copy_ms = 8;
}
static void capture_log(const char *tag, const char *format, ...) {
    CHECK(!strcmp(tag, "pico_system"), "performance log uses system tag");
    CHECK(editor_case||(timer_reads == 4 && timing_reads == 1), "one complete timing snapshot before logging");
    va_list args;
    va_start(args, format);
    int length = vsnprintf(log_text, sizeof(log_text), format, args);
    va_end(args);
    CHECK(length > 0 && (size_t)length < sizeof(log_text), "performance log bounded and untruncated");
    logs++;
}
#define ESP_LOGI(tag,...) capture_log(tag,__VA_ARGS__)
static void render(app_ctx_t *c, uint8_t *fb) {
    CHECK(c == &ctx && fb == guarded.fb, "render receives original context/framebuffer");
    CHECK(!s_fast, "fast request consumed before render");
    renders++;
    memcpy(fb, original, FRAME_BYTES);
    if(route==PS_EDITOR){test_partial_ready=true;test_layout_change=false;}
    if(route==PS_APPLICATION){test_apps_ready=true;test_layout_change=false;}
    fake_now_us += 11000;
}
// Actual editor drawing has independent framebuffer tests; this seam exposes
// whether the dispatcher requests a partial frame, commits zero damage, or falls back.
static bool ps_reader_editor_render_partial(uint8_t *fb,EpdRect areas[4],int *count){
    CHECK(fb==guarded.fb&&areas&&count,"partial renderer receives owned framebuffer and area outputs");
    editor_partial_calls++;*count=0;
    if(!test_partial_ready||test_layout_change)return false;
    CHECK(test_damage_count>=0&&test_damage_count<=4,"fixture respects public four-region capacity");
    for(int i=0;i<test_damage_count;i++)areas[i]=test_damage[i];
    *count=test_damage_count;return true;
}
static void ps_reader_editor_invalidate(void){editor_invalidations++;test_partial_ready=false;}
static bool ps_reader_editor_ink_mode(void){return route==PS_EDITOR&&test_ink_mode;}
static void ps_reader_editor_ink_cancel(void){}
static bool ps_apps_ink_mode(void){return route==PS_APPLICATION&&test_apps_ink;}
static void ps_apps_ink_cancel(void){}
static void ps_apps_ink_invalidate(void){apps_invalidations++;test_apps_ready=false;}
static bool ps_apps_ink_render_partial(uint8_t *fb,EpdRect areas[4],int *count){
    CHECK(fb==guarded.fb&&areas&&count,"app partial renderer receives owned buffers");editor_partial_calls++;*count=0;
    if(!test_apps_ready||test_layout_change)return false;
    for(int i=0;i<test_damage_count;i++)areas[i]=test_damage[i];*count=test_damage_count;return true;
}
static unsigned desktop_acks;static bool desktop_ok;
static void ps_desktop_presented(bool ok){desktop_acks++;desktop_ok=ok;}
static bool ps_ink_pending(void){return test_ink_pending;}
static bool ps_ink_cancel(void){ink_cancels++;bool pending=ps_ink_pending();test_ink_pending=false;ink_remaining=0;return pending;}
static bool ps_ink_step(app_ctx_t *c){
    CHECK(c==&ctx,"continuous drain preserves context");ink_steps++;
    if(test_ink_fail)return false;
    if(!test_ink_pending){ink_steps--;return true;}
    if(ink_remaining&&!--ink_remaining)test_ink_pending=false;
    return true;
}
static void fan_threshold(uint8_t *fb){
    CHECK(fb==guarded.fb,"full binary baseline uses owned framebuffer");full_thresholds++;
    for(unsigned i=0;i<FRAME_BYTES;i++){unsigned value=fb[i];fb[i]=(value&0x80?0xf0:0)|(value&8?15:0);}
}
static void fan_area_pixels(uint8_t *fb,const uint8_t *source,EpdRect area,bool threshold){
    CHECK(fb==guarded.fb&&!source&&threshold,"editor thresholds only its rendered damage");
    CHECK(editor_thresholds<(unsigned)test_damage_count,"damage threshold count remains bounded");
    if(editor_thresholds<(unsigned)test_damage_count)
        CHECK(!memcmp(&area,&test_damage[editor_thresholds],sizeof(area)),"threshold receives exact product-logical damage");
    editor_thresholds++;
}
EpdRect ps_rect(int x,int y,int width,int height);
static enum EpdDrawError update_result(EpdiyHighlevelState *h, char kind) {
    CHECK(h == &hl, "display receives original highlevel state");
    CHECK(calls < sizeof(order), "bounded display call sequence");
    if (calls < sizeof(order)) order[calls] = kind;
    calls++;
    CHECK(memcmp(guarded.fb, expected, FRAME_BYTES) == 0,
          "display sees complete intended framebuffer before pushing");
    fake_now_us += 22000;
    if (fail_remaining) { fail_remaining--; return EPD_DRAW_EMPTY_LINE_QUEUE; }
    memcpy(h->back_fb,guarded.fb,FRAME_BYTES);
    return EPD_DRAW_SUCCESS;
}
static enum EpdDrawError update_display_mode(EpdiyHighlevelState *h, enum EpdDrawMode mode) {
    modes++; last_mode = mode;
    return update_result(h, 'M');
}
static enum EpdDrawError update_display_changed_mode(EpdiyHighlevelState *h, enum EpdDrawMode mode) {
    changed_areas++;modes++;last_mode=mode;
    return update_result(h, 'M');
}
static enum EpdDrawError update_display_with(EpdiyHighlevelState *h,const int *waveform,enum EpdDrawMode mode) {
    CHECK(waveform==&E0470_FOLLOW_WAVEFORM&&mode==MODE_DU,"transient frame uses existing FOLLOW DU exclusively");
    follow_updates++;modes++;last_mode=mode;return update_result(h,'M');
}
static enum EpdDrawError update_display_area_with(EpdiyHighlevelState *h,const int *waveform,enum EpdDrawMode mode,EpdRect area){
    if(test_usb_background){CHECK(waveform==&E0470_WAVEFORM&&mode==MODE_GL16,"retained USB status uses only standard grayscale regional refresh");CHECK(area.height>0&&area.width>0,"USB status refresh region is bounded");return update_result(h,'A');}
    CHECK(waveform==&E0470_FOLLOW_WAVEFORM&&mode==MODE_DU,"editor damage exclusively uses fast FOLLOW DU");
    CHECK(submitted_count<4,"bounded submitted editor damage count");
    if(submitted_count<4)submitted_damage[submitted_count++]=area;
    follow_updates++;last_mode=mode;return update_result(h,'A');
}
static bool ps_ink_present(app_ctx_t *c,EpdRect damage){
    CHECK(c==&ctx,"continuous ink receives original context");ink_presents++;ink_steps++;
    CHECK(submitted_count<4,"ink damage fits observation buffer");if(submitted_count<4)submitted_damage[submitted_count++]=damage;
    if(test_ink_fail)return false;ink_remaining=5;test_ink_pending=true;return true;
}
static void guard_draw_result(EpdiyHighlevelState *h,enum EpdDrawError error){CHECK(h==&hl&&(editor_case||error==EPD_DRAW_SUCCESS),"global display preserves error guard");guard_calls++;guarded_error=error;}
static enum EpdDrawError update_display_full(EpdiyHighlevelState *h) {
    fulls++;
    return update_result(h, 'F');
}
'''

tests = r'''
static void clear_observation(void) {
    renders = modes = fulls = calls = fail_remaining = follow_updates = changed_areas = 0;
    fan_present_calls = fan_reset_calls = 0;
    fan_present_result = true;
    editor_case=false;editor_partial_calls=editor_invalidations=editor_thresholds=guard_calls=submitted_count=0;
    ink_presents=ink_steps=ink_cancels=full_thresholds=apps_invalidations=0;test_ink_fail=false;
    guarded_error=EPD_DRAW_SUCCESS;
    timer_reads = timing_reads = logs = 0;
    fake_now_us = 1000000;
    ctx.now_ms = 990;
    log_text[0] = '\0';
    memset(order, 0, sizeof(order));
    memset(guarded.before, 0xa5, sizeof(guarded.before));
    memset(guarded.after, 0x5a, sizeof(guarded.after));
}
static void check_guards(void) {
    for (unsigned i = 0; i < GUARD_BYTES; i++) {
        CHECK(guarded.before[i] == 0xa5, "framebuffer prefix guard intact");
        CHECK(guarded.after[i] == 0x5a, "framebuffer suffix guard intact");
    }
}
static void check_log(const char *wanted_mode, bool fast, bool monochrome, int error, int fallback) {
    int r, panel, fan, fast_log, landscape, err_log, fallback_log, diff, draw, copy;
    char mode[24];
    long long render_us, threshold_us, refresh_us, since_ctx_ms;
    CHECK(logs == 1 && timer_reads == 4 && timing_reads == 1, "single bounded performance record");
    int fields = sscanf(log_text,
        "present route=%d panel=%d fan=%d fast=%d landscape=%d req=%23s "
        "render_us=%lld threshold_us=%lld refresh_us=%lld total_since_ctx_now_ms=%lld "
        "epd_err=%d fallback_err=%d last_hl_diff_ms=%d last_hl_draw_ms=%d last_hl_copy_ms=%d",
        &r, &panel, &fan, &fast_log, &landscape, mode, &render_us, &threshold_us,
        &refresh_us, &since_ctx_ms, &err_log, &fallback_log, &diff, &draw, &copy);
    CHECK(fields == 15, "all documented timing/log fields are parseable");
    if (fields != 15) return;
    CHECK(r == (int)route && panel == s_panel && fan == s_fan && fast_log == fast &&
          landscape == s_config.landscape, "route/panel/fan/orientation and consumed fast state logged");
    CHECK(!strcmp(mode, wanted_mode), "requested DU/GL16/rotationGC16 mode logged");
    CHECK(render_us == 11007, "render duration brackets actual render");
    CHECK(threshold_us == (monochrome ? 7 : 0), "threshold duration is zero for preserved grayscale");
    CHECK(refresh_us == (long long)calls * 22000 + 7, "refresh duration includes fallback when needed");
    CHECK(since_ctx_ms == (long long)(1011021 + calls * 22000) / 1000 - ctx.now_ms,
          "total context age includes upstream elapsed time and every refresh");
    CHECK(err_log == error && fallback_log == fallback, "original and fallback errors both retained");
    CHECK(diff == 4 && draw == 225 && copy == 8, "last highlevel timing reported verbatim");
}
static void prime(bool landscape) {
    if(route==PS_CANVAS||route==PS_CLOUDFLARE)route=PS_HA;
    s_force_full=false;s_fan=false;test_editor_active=false;s_ink_gray_due=0;s_ink_gray_settle=s_dirty=false;memset(back,255,FRAME_BYTES);
    test_ink_mode=test_apps_ink=test_apps_ready=test_ink_pending=false;ink_remaining=0;
    s_toast[0]=0;test_partial_ready=false;test_layout_change=false;test_damage_count=0;
    s_config.landscape = landscape;
    test_rotation = landscape ? EPD_ROT_INVERTED_LANDSCAPE : EPD_ROT_INVERTED_PORTRAIT;
    clear_observation();
    s_fast = true;
    CHECK(!present(&ctx, APP_REDRAW_FULL), "full redraw delegates to shared original path");
    CHECK(!s_fast && !renders && !modes && !fulls, "delegated full redraw has no custom I/O");
    CHECK(!timer_reads && !timing_reads && !logs, "delegation has no performance instrumentation work");
    clear_observation();
}
static void desktop_full_confirmation(void){
    const ps_route_t app_routes[]={PS_CANVAS,PS_CLOUDFLARE};
    for(unsigned i=0;i<2;i++){
        prime(false);route=app_routes[i];expected=original;unsigned before=desktop_acks;
        CHECK(present(&ctx,APP_REDRAW_FULL),"each independent desktop app owns its confirmation boundary");
        CHECK(fulls==1&&desktop_acks==before+1&&desktop_ok,"each desktop app confirms one successful full presentation");
    }
}
static void first_page(void) {
    // Run before any other present call: no valid panel reference exists yet.
    clear_observation();
    route = PS_HA; s_config.landscape = false; s_fast = true; expected = original;
    CHECK(present(&ctx, APP_REDRAW_PAGE), "initial unprimed page handled");
    CHECK(renders == 1 && !modes && fulls == 1, "unprimed page establishes full reference");
    CHECK(!s_fast && !memcmp(guarded.fb, original, FRAME_BYTES), "initial full frame preserves grayscale");
    check_log("rotationGC16", true, false, EPD_DRAW_SUCCESS, -1);
    check_guards();
}
static void quantization(void) {
    prime(false); route = PS_CLOCK; s_fast=true; expected = binary;
    CHECK(present(&ctx, APP_REDRAW_PAGE), "ordinary page handled");
    CHECK(modes == 1 && last_mode == MODE_DU && !fulls, "ordinary interaction uses one DU");
    for (unsigned i = 0; i < FRAME_BYTES; i++)
        CHECK(guarded.fb[i] == binary[i], "all framebuffer bytes match independent nibble threshold");
    for (unsigned value = 0; value < 256; value++) {
        uint8_t want = (value / 16 >= 8 ? 0xf0 : 0) | (value % 16 >= 8 ? 0x0f : 0);
        CHECK(guarded.fb[value] == want, "each of all 256 input bytes quantized exactly");
    }
    CHECK(renders == 1 && !s_fast, "DU renders once and consumes fast flag");
    check_log("DU", true, true, EPD_DRAW_SUCCESS, -1);
    check_guards();
}
static void motion_preserves_unchanged_gray(void) {
    for(int snake=0;snake<2;snake++){
        prime(false);route=snake?PS_ARCADEGAME:PS_CLOCK;test_snake=snake;test_paused=false;test_animating=true;s_fast=true;
        for(unsigned i=0;i<FRAME_BYTES;i++){
            // Keep one gray nibble stationary and change its neighbour; verify both packing orders.
            back[i]=(i&1)?(original[i]&0xf0)|15:(original[i]&15)|240;
            selective[i]=(i&1)?(original[i]&0xf0)|(binary[i]&15):(original[i]&15)|(binary[i]&240);
        }
        expected=selective;
        CHECK(present(&ctx,APP_REDRAW_PAGE)&&last_mode==MODE_DU,"motion uses DU with a mixed gray/binary target");
        CHECK(!memcmp(guarded.fb,selective,FRAME_BYTES),"stationary gray nibbles survive beside thresholded changed pixels");
        CHECK(!memcmp(back,selective,FRAME_BYTES),"submitted reference matches exact selective target");
        check_guards();
    }
    test_snake=test_paused=false;
}
static void policy_matrix(void) {
    for (int landscape = 0; landscape < 2; landscape++)
    for (int r = 0; r < PS_ROUTE_COUNT; r++)
    for (int book = 0; book < 6; book++)
    for (int comic = 0; comic < 2; comic++)
    for (int fast = 0; fast < 2; fast++) {
        if(r==PS_EDITOR)continue; // Dedicated editor cases exercise its partial-render seam below.
        prime(landscape); route = (ps_route_t)r; active_book = book; rb[book].comic = comic;
        bool gray = r == PS_SHELF || (r == PS_READ && comic);
        CHECK(ps_reader_prefers_grayscale() == gray, "actual reader grayscale policy for every route/book");
        bool monochrome = fast && r==PS_CLOCK;
        expected = monochrome ? binary : original; s_fast = fast;
        CHECK(present(&ctx, APP_REDRAW_PAGE), "all page routes handled");
        CHECK(renders == 1 && modes == 1 && !fulls, "exactly one ordinary page render and push");
        CHECK(fan_reset_calls == 1 && !fan_present_calls, "ordinary page invalidates cached fan before repainting");
        CHECK(last_mode == (monochrome ? MODE_DU : MODE_GL16), "correct grayscale/fast-fan branch");
        CHECK(!memcmp(guarded.fb, expected, FRAME_BYTES), "image frames preserved, DU frames binary");
        CHECK(!s_fast, "fast flag consumed for every route");
        check_log(monochrome ? "DU" : "GL16", fast, monochrome, EPD_DRAW_SUCCESS, -1);
        check_guards();
    }
}
static void delegated_redraws(void) {
    const app_redraw_t other[] = { APP_REDRAW_NONE, APP_REDRAW_AREA, APP_REDRAW_FULL, APP_REDRAW_DONE };
    for (unsigned i = 0; i < sizeof(other)/sizeof(other[0]); i++) {
        prime(false); route = PS_HA; s_fast = true;
        memcpy(guarded.fb, original, FRAME_BYTES);
        CHECK(!present(&ctx, other[i]), "non-page redraw delegates unchanged");
        CHECK(!renders && !modes && !fulls && !s_fast, "delegation neither renders nor pushes");
        CHECK(fan_reset_calls == 1 && !fan_present_calls, "non-fan delegation invalidates cached diagram");
        CHECK(!timer_reads && !timing_reads && !logs, "non-page delegation emits no misleading timing");
        CHECK(!memcmp(guarded.fb, original, FRAME_BYTES), "delegation never quantizes framebuffer");
        check_guards();
    }
}
static void orientation_changes(void) {
    for (int before = 0; before < 2; before++)
    for (int gray = 0; gray < 2; gray++)
    for (int fast = 0; fast < 2; fast++) {
        prime(before); route = gray ? PS_SHELF : PS_HA; s_fast = fast;
        s_config.landscape = !before; expected = original;
        CHECK(present(&ctx, APP_REDRAW_PAGE), "rotated page handled");
        CHECK(renders == 1 && fulls == 1 && !modes, "rotation uses GC16 even when fast fan requested");
        CHECK(!memcmp(guarded.fb, original, FRAME_BYTES), "rotation full redraw preserves grayscale");
        check_log("rotationGC16", fast, false, EPD_DRAW_SUCCESS, -1);
        clear_observation(); expected = original;
        CHECK(present(&ctx, APP_REDRAW_PAGE), "post-rotation page handled");
        CHECK(modes == 1 && !fulls && last_mode == MODE_GL16,
              "committed orientation resumes ordinary mode on following page");
        check_log("GL16", false, false, EPD_DRAW_SUCCESS, -1);
        check_guards();
    }
}
static void orientation_reversals(void) {
    const enum EpdRotation before[]={EPD_ROT_INVERTED_PORTRAIT,EPD_ROT_PORTRAIT,EPD_ROT_INVERTED_LANDSCAPE,EPD_ROT_LANDSCAPE};
    const enum EpdRotation after[]={EPD_ROT_PORTRAIT,EPD_ROT_INVERTED_PORTRAIT,EPD_ROT_LANDSCAPE,EPD_ROT_INVERTED_LANDSCAPE};
    for(unsigned i=0;i<4;i++) {
        bool landscape=i>=2;
        prime(landscape);test_rotation=before[i];
        CHECK(!present(&ctx,APP_REDRAW_FULL),"full delegation commits actual signed orientation reference");
        clear_observation();route=PS_HA;test_rotation=after[i];s_fast=true;expected=original;
        CHECK(present(&ctx,APP_REDRAW_PAGE),"same-layout 180 degree reversal handled");
        CHECK(s_config.landscape==landscape&&renders==1&&fulls==1&&!modes,"signed reversal uses GC16 without changing layout preference");
        check_log("rotationGC16",true,false,EPD_DRAW_SUCCESS,-1);check_guards();
        clear_observation();expected=original;
        CHECK(present(&ctx,APP_REDRAW_PAGE)&&modes==1&&!fulls&&last_mode==MODE_GL16,"stable signed orientation returns to ordinary refresh");
        check_log("GL16",false,false,EPD_DRAW_SUCCESS,-1);check_guards();
    }
}
static void explicit_full_refresh(void) {
    prime(false);route=PS_HA;s_fast=true;s_force_full=true;expected=original;
    CHECK(present(&ctx,APP_REDRAW_PAGE),"explicit screen refresh handled");
    CHECK(renders==1&&fulls==1&&!modes&&!s_force_full,"screen refresh consumes force flag and uses one full waveform");
    CHECK(!memcmp(guarded.fb,original,FRAME_BYTES),"forced refresh preserves grayscale");check_guards();
}
static void check_cached_dispatch(void) {
    CHECK(fan_present_calls == 1 && !fan_reset_calls,
          "active cached frame uses one shared painter without invalidation");
    CHECK(!renders && !calls && !modes && !fulls && !follow_updates,
          "dispatcher does not duplicate cached painter rendering or display I/O");
    CHECK(!timer_reads && !timing_reads && !logs,
          "cached dispatch bypasses ordinary whole-page timing and logs");
    CHECK(!s_fast, "cached painter consumes pending fast request");
    CHECK(!memcmp(guarded.fb, original, FRAME_BYTES),
          "dispatcher does not threshold or change the cached painter framebuffer");
    check_guards();
}
static void live_fan_policy(void) {
    const enum EpdRotation rotations[] = {
        EPD_ROT_INVERTED_PORTRAIT, EPD_ROT_PORTRAIT,
        EPD_ROT_INVERTED_LANDSCAPE, EPD_ROT_LANDSCAPE
    };
    for (unsigned orientation = 0; orientation < sizeof(rotations)/sizeof(rotations[0]); orientation++) {
        prime(orientation >= 2);
        test_rotation = rotations[orientation];
        CHECK(!present(&ctx, APP_REDRAW_FULL), "full delegation primes signed fan orientation");
        route = PS_SHELF; s_fan = true;
        for (int frame = 0; frame < 200; frame++) {
            clear_observation(); s_fast = (frame % 2) == 0;
            memcpy(guarded.fb, original, FRAME_BYTES);
            CHECK(present(&ctx, APP_REDRAW_PAGE), "live shell fan delegates to shared cached painter");
            check_cached_dispatch();
        }
        s_global_enabled = true; s_global_app = &fan_app;
        for (int frame = 0; frame < 200; frame++) {
            clear_observation(); s_fast = (frame % 2) == 0;
            memcpy(guarded.fb, original, FRAME_BYTES);
            CHECK(ps_global_present(&ctx, &fan_app, APP_REDRAW_PAGE),
                  "real-book global fan delegates to same cached painter");
            check_cached_dispatch();
        }
        clear_observation(); fan_present_result = false;
        CHECK(!present(&ctx, APP_REDRAW_PAGE) && fan_present_calls == 1,
              "shell propagates cached painter handled result without second path");
        CHECK(!renders && !calls && !fan_reset_calls, "false cached result does not render twice");
        clear_observation(); fan_present_result = false;
        CHECK(!ps_global_present(&ctx, &fan_app, APP_REDRAW_PAGE) && fan_present_calls == 1,
              "book overlay propagates cached painter handled result");
        CHECK(!renders && !calls && !fan_reset_calls, "false overlay result does not render twice");
        s_fan = false; s_global_enabled = false; s_global_app = NULL;
        clear_observation(); expected = original;
        CHECK(present(&ctx, APP_REDRAW_PAGE) && fan_reset_calls == 1 && !fan_present_calls &&
              !follow_updates && last_mode == MODE_GL16,
              "closing fan invalidates cache and resumes ordinary grayscale policy");
        check_log("GL16", false, false, EPD_DRAW_SUCCESS, -1);
    }
}
static void fan_dispatch_boundaries(void) {
    const app_redraw_t ignored[] = { APP_REDRAW_NONE, APP_REDRAW_DONE };
    prime(false); route = PS_READ; s_fan = true;
    s_global_enabled = true; s_global_app = &fan_app;
    for (unsigned i = 0; i < sizeof(ignored)/sizeof(ignored[0]); i++) {
        clear_observation();
        CHECK(ps_global_present(&ctx, &fan_app, ignored[i]), "claimed overlay ignores no-draw/already-done frames");
        CHECK(!fan_present_calls && !fan_reset_calls && !renders && !calls,
              "ignored overlay frames retain cache with no painter or display work");
    }
    clear_observation();
    CHECK(!ps_global_present(&ctx, &other_app, APP_REDRAW_PAGE), "unrelated app never claims fan overlay");
    CHECK(!fan_present_calls && !renders && !calls, "unrelated app has no fan display side effect");
    clear_observation(); s_global_enabled = false;
    CHECK(!ps_global_present(&ctx, &fan_app, APP_REDRAW_PAGE), "disabled global overlay declines dispatch");
    CHECK(!fan_present_calls && !renders && !calls, "disabled overlay has no fan display side effect");
    s_global_enabled = true;

    clear_observation(); s_force_full = true; s_fast = true; expected = original;
    CHECK(present(&ctx, APP_REDRAW_PAGE), "explicit full refresh supersedes active cached fan");
    CHECK(!fan_present_calls && fan_reset_calls == 1 && renders == 1 && fulls == 1 && !modes,
          "forced fan frame invalidates cache and preserves full-refresh path");
    CHECK(!s_force_full, "forced fan refresh request consumed");
    check_log("standbyGC16", true, false, EPD_DRAW_SUCCESS, -1);

    clear_observation(); test_rotation = EPD_ROT_PORTRAIT; expected = original; s_fast = true;
    CHECK(present(&ctx, APP_REDRAW_PAGE), "rotation supersedes active cached fan");
    CHECK(!fan_present_calls && fan_reset_calls == 1 && renders == 1 && fulls == 1 && !modes,
          "rotated fan frame uses full reference instead of stale cache");
    check_log("rotationGC16", true, false, EPD_DRAW_SUCCESS, -1);

    clear_observation();
    CHECK(!present(&ctx, APP_REDRAW_FULL), "explicit full redraw delegates while fan is open");
    CHECK(fan_reset_calls == 1 && !fan_present_calls && !renders && !calls,
          "delegated full redraw invalidates fan cache without duplicate work");
    clear_observation(); s_fan = false; s_panel = 0;
    CHECK(!ps_global_present(&ctx, &fan_app, APP_REDRAW_PAGE), "closed overlay declines book frame");
    CHECK(fan_reset_calls == 1 && !fan_present_calls && !renders && !calls,
          "closed global overlay invalidates cached fan");
    s_global_enabled = false; s_global_app = NULL;
}
static void editor_cached_policy(void) {
#if HAVE_FAN_FRAME_ACTIVE
    for (int landscape = 0; landscape < 2; landscape++) {
        prime(landscape); route = PS_QUICK_SETTINGS; test_editor_active = true;
        clear_observation(); s_fast = true;
        memcpy(guarded.fb, original, FRAME_BYTES);
        CHECK(present(&ctx, APP_REDRAW_PAGE), "visual editor delegates cached diagram to shared painter");
        check_cached_dispatch();
        s_global_enabled = true; s_global_app = &fan_app;
        clear_observation(); s_fast = true;
        memcpy(guarded.fb, original, FRAME_BYTES);
        (void)ps_global_present(&ctx, &fan_app, APP_REDRAW_PAGE);
        CHECK(!fan_reset_calls, "global dispatch does not discard active editor diagram cache");
        test_editor_active = false; s_global_enabled = false; s_global_app = NULL;
    }
#endif
}
static void fan_is_one_shot(void) {
    prime(false); route = PS_CLOCK; s_fast = true; expected = binary;
    CHECK(present(&ctx, APP_REDRAW_PAGE) && last_mode == MODE_DU, "fan overrides underlying image route");
    check_log("DU", true, true, EPD_DRAW_SUCCESS, -1);
    clear_observation(); expected = original;
    CHECK(present(&ctx, APP_REDRAW_PAGE) && last_mode == MODE_GL16, "next underlying image frame regains grayscale");
    CHECK(renders == 1 && modes == 1 && !fulls && !s_fast, "fast override does not leak to next frame");
    check_log("GL16", false, false, EPD_DRAW_SUCCESS, -1);
    check_guards();
}
static void display_failures(void) {
    for (int gray = 0; gray < 2; gray++) {
        prime(false); route = gray ? PS_SHELF : PS_CLOCK; s_fast=!gray; expected = gray ? original : binary;
        fail_remaining = 1;
        CHECK(present(&ctx, APP_REDRAW_PAGE), "failed normal push still handled");
        CHECK(modes == 1 && fulls == 1 && calls == 2 && order[0] == 'M' && order[1] == 'F',
              "failed DU/GL16 immediately falls back once to GC16");
        CHECK(last_mode == (gray ? MODE_GL16 : MODE_DU), "failure test covers each normal mode");
        CHECK(renders == 1 && !memcmp(guarded.fb, expected, FRAME_BYTES), "fallback reuses the same correct frame");
        check_log(gray ? "GL16" : "DU", !gray, !gray, EPD_DRAW_EMPTY_LINE_QUEUE, EPD_DRAW_SUCCESS);
        check_guards();
    }
    prime(false); route = PS_HA; s_config.landscape = true; expected = original; fail_remaining = 1;
    CHECK(present(&ctx, APP_REDRAW_PAGE), "failed orientation push handled");
    CHECK(!modes && fulls == 2 && renders == 1 && order[0] == 'F' && order[1] == 'F',
          "failed orientation full push retries full without a duplicate render");
    check_log("rotationGC16", false, false, EPD_DRAW_EMPTY_LINE_QUEUE, EPD_DRAW_SUCCESS);
    check_guards();
}
static void diagnostic_context_and_double_failure(void) {
    prime(false); route = PS_HA; s_panel = 3; s_fast = true; expected = original;
    ctx.now_ms = 900; fail_remaining = 2;
    CHECK(present(&ctx, APP_REDRAW_PAGE), "double display failure still follows existing handled contract");
    CHECK(calls == 2 && modes == 1 && fulls == 1, "only one GC16 fallback attempted even if both fail");
    check_log("GL16", true, false, EPD_DRAW_EMPTY_LINE_QUEUE, EPD_DRAW_EMPTY_LINE_QUEUE);
    s_panel = 0;
    check_guards();
}
static void shelf_region_policy(void) {
    route=PS_SHELF;s_panel=0;expected=original;prime(false);
    CHECK(present(&ctx,APP_REDRAW_PAGE)&&changed_areas==1&&!fulls,
          "stable shelf after delegated full entry uses changed area");
    clear_observation();
    CHECK(!present(&ctx,APP_REDRAW_NONE)&&!present(&ctx,APP_REDRAW_DONE),
          "no-draw polls retain the existing shelf reference");
    CHECK(present(&ctx,APP_REDRAW_PAGE)&&changed_areas==1,
          "shelf paging remains partial after idle polling");
    clear_observation();s_panel=3;
    CHECK(present(&ctx,APP_REDRAW_PAGE)&&!changed_areas,
          "shelf overlay keeps complete overlay presentation");
    clear_observation();s_panel=0;
    CHECK(present(&ctx,APP_REDRAW_PAGE)&&!changed_areas,
          "overlay close restores complete shelf before reusing its reference");
    clear_observation();
    CHECK(present(&ctx,APP_REDRAW_PAGE)&&changed_areas==1,
          "restored shelf returns to bounded region presentation");
    clear_observation();s_force_full=true;
    CHECK(present(&ctx,APP_REDRAW_PAGE)&&!changed_areas&&fulls==1,
          "explicit shelf refresh remains a full cleanup");
    clear_observation();fail_remaining=2;
    CHECK(present(&ctx,APP_REDRAW_PAGE)&&changed_areas==1&&fulls==1,
          "changed shelf failure receives the existing full recovery");
    clear_observation();
    CHECK(present(&ctx,APP_REDRAW_PAGE)&&!changed_areas,
          "failed full recovery invalidates the shelf region reference");
    clear_observation();route=PS_HA;expected=original;
    CHECK(present(&ctx,APP_REDRAW_PAGE)&&!changed_areas,
          "navigation to another page invalidates the content reference");
    clear_observation();route=PS_SHELF;expected=original;
    CHECK(present(&ctx,APP_REDRAW_PAGE)&&!changed_areas,
          "navigation back to shelf restores full content once");
    check_guards();
}
static void motion_settles_gray(void) {
    prime(false);route=PS_CLOCK;s_fast=true;test_animating=true;expected=binary;
    CHECK(present(&ctx,APP_REDRAW_PAGE)&&last_mode==MODE_DU,"moving clock uses fast binary");
    clear_observation();s_fast=true;test_animating=false;expected=original;
    CHECK(present(&ctx,APP_REDRAW_PAGE)&&last_mode==MODE_GL16&&changed_areas==1,"clock final frame restores grayscale using changed region");
    test_animating=true;
    for(int paused=0;paused<2;paused++) {
        prime(false);route=PS_ARCADEGAME;test_snake=true;test_paused=paused;expected=paused?original:binary;
        CHECK(present(&ctx,APP_REDRAW_PAGE)&&last_mode==(paused?MODE_GL16:MODE_DU),"snake updates fast while running and settles grayscale when paused");
        check_guards();
    }
    test_snake=test_paused=false;
    for(int r=0;r<PS_ROUTE_COUNT;r++) {
        if(r==PS_EDITOR)continue;
        route=(ps_route_t)r;prime(false);expected=original;
        CHECK(present(&ctx,APP_REDRAW_PAGE)&&last_mode==MODE_GL16&&changed_areas==1,"every stable route keeps grayscale and uses bounded differences");
    }
}
static void editor_observe(void){clear_observation();editor_case=true;expected=original;}
static void shared_fast_damage_union(void){
    // Independent golden bounds include the rounding trap: logical union then
    // scale would lose the final pixel of the second separately rendered box.
    const EpdRect rounding[]={{0,0,1,1},{3,3,3,3}};
    const EpdRect disjoint[]={{14,80,196,112},{222,34,408,96},{292,271,192,38}};
    const EpdRect nested[]={{8,9,24,33},{10,12,2,3},{8,9,24,33},{17,18,1,1}};
    const EpdRect wanted[]={{0,0,12,12},{27,65,1170,522},{15,17,46,63}};
    const EpdRect *sets[]={rounding,disjoint,nested};
    const int counts[]={2,3,4};
    const enum EpdRotation rotations[]={EPD_ROT_PORTRAIT,EPD_ROT_INVERTED_PORTRAIT,
        EPD_ROT_LANDSCAPE,EPD_ROT_INVERTED_LANDSCAPE};
    for(unsigned rotation=0;rotation<4;rotation++){
        test_rotation=rotations[rotation];
        for(unsigned fixture=0;fixture<3;fixture++){
            EpdRect before[4];memcpy(before,sets[fixture],sizeof(EpdRect)*counts[fixture]);
            EpdRect merged=fast_damage_pixels(sets[fixture],counts[fixture]);
            CHECK(!memcmp(&merged,&wanted[fixture],sizeof(merged)),
                  "shared fast helper unions separately scaled boxes without rounding gaps");
            CHECK(!memcmp(before,sets[fixture],sizeof(EpdRect)*counts[fixture]),
                  "merging fan/editor damage leaves caller-owned logical regions unchanged");
            for(int area=0;area<counts[fixture];area++){
                EpdRect r=ps_rect(sets[fixture][area].x,sets[fixture][area].y,
                                 sets[fixture][area].width,sets[fixture][area].height);
                CHECK(merged.x<=r.x&&merged.y<=r.y&&
                      merged.x+merged.width>=r.x+r.width&&merged.y+merged.height>=r.y+r.height,
                      "one submission contains every independently rasterized field");
            }
        }
        EpdRect one=fast_damage_pixels(disjoint,1),first=ps_rect(14,80,196,112);
        CHECK(!memcmp(&one,&first,sizeof(one)),"single field keeps its exact existing update bounds");
    }
}
static void editor_begin(bool landscape){
    route=PS_HA;s_panel=0;prime(landscape);route=PS_EDITOR;editor_observe();
    CHECK(present(&ctx,APP_REDRAW_PAGE),"editor entry handled by actual product dispatcher");
    CHECK(renders==1&&modes==1&&last_mode==MODE_GL16&&!follow_updates&&!fulls&&!full_thresholds,
          "first keyboard frame establishes native gray without whole-frame thresholding");
    CHECK(!editor_partial_calls&&test_partial_ready,"first editor render establishes partial-render reference");
    test_damage[0]=(EpdRect){14,80,196,112};
    test_damage[1]=(EpdRect){222,34,408,96};
    test_damage[2]=(EpdRect){292,271,192,38};
    test_damage[3]=(EpdRect){0,0,640,20};
    // Independent expected pixel bounds for the 1.9x product conversion;
    // real ps_rect/px are extracted instead of copying the union algorithm.
    expected_union[1]=(EpdRect){27,152,372,213};
    expected_union[2]=(EpdRect){27,65,1170,300};
    expected_union[3]=(EpdRect){27,65,1170,522};
    expected_union[4]=(EpdRect){0,0,1216,587};
    if(!landscape){
        test_damage[0]=(EpdRect){14,80,332,188};
        test_damage[1]=(EpdRect){6,312,348,74};
        test_damage[2]=(EpdRect){65,551,180,38};
        test_damage[3]=(EpdRect){0,0,360,24};
        expected_union[1]=(EpdRect){27,152,631,357};
        expected_union[2]=(EpdRect){11,152,661,582};
        expected_union[3]=(EpdRect){11,152,661,967};
        expected_union[4]=(EpdRect){0,0,684,1119};
    }
    test_damage_count=1;
}
static void editor_input_damage_policy(void){
    for(int landscape=0;landscape<2;landscape++){
        editor_begin(landscape);unsigned total=0;
        for(unsigned key=0;key<40;key++){
            editor_observe();test_damage_count=1+(int)(key%4);s_fast=(key&1)!=0;
            CHECK(present(&ctx,APP_REDRAW_PAGE),"continuous keyboard input handled");
            CHECK(editor_partial_calls==1&&!renders,"each key repaints only changed fields");
            CHECK(ink_presents==1&&ink_steps==1&&submitted_count==1&&ps_ink_pending(),
                  "each keyboard target submits exactly one phase and retains remaining drive");
            CHECK(s_ink_gray_due==ctx.now_ms+900&&!s_ink_gray_settle,"new local input schedules one deferred gray settlement");
            CHECK(!ink_cancels,"ordinary keyboard frames never cancel outstanding phases");
            CHECK(editor_thresholds==(unsigned)test_damage_count,"only reported damage is thresholded");
            CHECK(!modes&&!changed_areas&&!fulls&&!follow_updates&&!editor_invalidations,
                  "forty consecutive keys avoid full FOLLOW batches, ordinary updates and periodic GC16");
            CHECK(!guard_calls&&!s_fast&&!logs,"successful one-phase input avoids blocking display guards and per-key log output");
            CHECK(!memcmp(&submitted_damage[0],&expected_union[test_damage_count],sizeof(EpdRect)),
                  "continuous input uses independently expected scaled union bounds");
            total+=ink_presents;check_guards();
        }
        CHECK(total==40,"every keyboard target is submitted once beyond the default cleanup interval");
        test_damage_count=0;
        unsigned drained=0;
        while(ps_ink_pending()){
            editor_observe();CHECK(present(&ctx,APP_REDRAW_PAGE),"zero-damage keyboard turn completes unfinished drive");
            CHECK(ink_steps==1&&!ink_presents&&!calls&&!renders&&!editor_thresholds&&!ink_cancels,
                  "each unchanged keyboard turn drains one phase without redraw or cancellation");
            CHECK(++drained<=6,"keyboard draining is bounded by existing phase count");
        }
        editor_observe();uint8_t before[FRAME_BYTES];memcpy(before,guarded.fb,FRAME_BYTES);
        CHECK(present(&ctx,APP_REDRAW_PAGE)&&editor_partial_calls==1,"drained unchanged editor is still owned by partial renderer");
        CHECK(!calls&&!renders&&!ink_steps&&!ink_presents&&!editor_thresholds&&!guard_calls&&!logs,
              "zero damage after drain performs no panel work");
        CHECK(!memcmp(before,guarded.fb,FRAME_BYTES),"no-change editor leaves every pixel unchanged");
        for(int ignored=0;ignored<2;ignored++){
            editor_observe();
            CHECK(!present(&ctx,ignored?APP_REDRAW_DONE:APP_REDRAW_NONE),"idle editor poll delegates no-draw result");
            CHECK(!editor_partial_calls&&!editor_invalidations&&!calls&&!renders&&!ink_cancels,
                  "idle polls preserve keyboard reference and pending-drive ownership");
        }
        editor_observe();test_damage_count=1;
        CHECK(present(&ctx,APP_REDRAW_PAGE)&&ink_presents==1&&!renders,"input stays incremental after fully drained idle polls");
    }
    editor_case=false;
}
static void editor_layout_toast_and_full_policy(void){
    for(int interruption=0;interruption<8;interruption++){
        editor_begin(false);editor_observe();
        CHECK(present(&ctx,APP_REDRAW_PAGE)&&ps_ink_pending(),"keyboard interruption starts with real outstanding phase debt");
        editor_observe();
        if(interruption==0)test_layout_change=true;
        if(interruption==1)snprintf(s_toast,sizeof(s_toast),"保存失败");
        if(interruption==2)s_force_full=true;
        if(interruption==3){s_config.landscape=true;test_rotation=EPD_ROT_INVERTED_LANDSCAPE;}
        if(interruption==4){route=PS_HA;expected=original;}
        if(interruption==5){s_panel=3;expected=original;}
        if(interruption==6){s_fan=true;expected=original;}
        CHECK(present(&ctx,interruption==7?APP_REDRAW_FULL:APP_REDRAW_PAGE),"keyboard interruption is presented by actual dispatcher");
        CHECK(fulls==1&&renders==1&&!ps_ink_pending()&&!ink_presents&&!follow_updates,
              "keyboard layout/toast/cleanup/rotation/route/overlay/full changes clean once before reuse");
        if(interruption!=4&&interruption!=5&&interruption!=6)
            CHECK(!full_thresholds&&!memcmp(guarded.fb,original,FRAME_BYTES),"keyboard recovery rebuilds native gray target");
        check_guards();
    }
    editor_case=false;
}
static void continuous_ink_observe(void){clear_observation();editor_case=true;expected=original;}
static void continuous_ink_begin(enum EpdRotation rotation,bool application){
    route=PS_HA;s_panel=0;prime(rotation==EPD_ROT_LANDSCAPE||rotation==EPD_ROT_INVERTED_LANDSCAPE);
    test_rotation=rotation;route=application?PS_APPLICATION:PS_EDITOR;test_apps_ink=application;test_ink_mode=!application;
    continuous_ink_observe();CHECK(present(&ctx,APP_REDRAW_PAGE),"ink entry establishes baseline through actual dispatcher");
    CHECK(renders==1&&!full_thresholds&&!memcmp(guarded.fb,original,FRAME_BYTES),"all ink modes establish native gray baseline before local binary drive");
    CHECK(!ink_presents&&!ps_ink_pending(),"baseline uses complete display presentation rather than starting partial phases");
    test_damage[0]=(EpdRect){24,200,40,24};test_damage_count=1;
}
static void continuous_ink_dispatch_policy(void){
    const enum EpdRotation rotations[]={EPD_ROT_PORTRAIT,EPD_ROT_INVERTED_PORTRAIT,EPD_ROT_LANDSCAPE,EPD_ROT_INVERTED_LANDSCAPE};
    for(unsigned orientation=0;orientation<4;orientation++)for(int application=0;application<2;application++){
        continuous_ink_begin(rotations[orientation],application);continuous_ink_observe();
        CHECK(present(&ctx,APP_REDRAW_PAGE)&&editor_partial_calls==1&&!renders,"ink paints incremental damage without redrawing static controls");
        CHECK(ink_presents==1&&ink_steps==1&&submitted_count==1&&ps_ink_pending(),"new ink submits exactly one continuous phase and retains backlog");
        CHECK(!follow_updates&&!fulls&&!modes,"live strokes avoid blocking FOLLOW batches and full cleanup");
        test_damage_count=0;
        while(ps_ink_pending()){
            continuous_ink_observe();CHECK(present(&ctx,APP_REDRAW_PAGE),"zero-damage ink still drains remaining phases");
            CHECK(ink_steps==1&&!ink_presents&&!renders&&!calls&&!fulls,"each empty damage turn drains one phase without repainting");
        }
        continuous_ink_observe();CHECK(present(&ctx,APP_REDRAW_PAGE)&&!ink_presents&&!renders&&!calls,"already drained empty ink performs no panel I/O");
        for(int interruption=0;interruption<6;interruption++){
            continuous_ink_begin(rotations[orientation],application);continuous_ink_observe();CHECK(present(&ctx,APP_REDRAW_PAGE)&&ps_ink_pending(),"interrupt fixture has unfinished phase debt");continuous_ink_observe();
            if(interruption==0){s_panel=3;expected=original;}
            if(interruption==1){s_fan=true;expected=original;}
            if(interruption==2){route=PS_HA;expected=original;}
            if(interruption==3){test_rotation=rotations[(orientation+2)%4];s_config.landscape=test_rotation==EPD_ROT_LANDSCAPE||test_rotation==EPD_ROT_INVERTED_LANDSCAPE;}
            if(interruption==4){test_layout_change=true;}
            if(interruption==5){snprintf(s_toast,sizeof(s_toast),"模型载入失败");}
            CHECK(present(&ctx,APP_REDRAW_PAGE)&&!ps_ink_pending(),"interrupt abandons phase backlog");
            CHECK(fulls==1&&renders==1&&!ink_presents&&!follow_updates,"panel/route/rotation/layout/toast interruptions force one clean complete recovery");check_guards();
        }
        continuous_ink_begin(rotations[orientation],application);continuous_ink_observe();CHECK(present(&ctx,APP_REDRAW_PAGE)&&ps_ink_pending(),"forced full fixture starts with pending ink");continuous_ink_observe();
        CHECK(present(&ctx,APP_REDRAW_FULL)&&fulls==1&&renders==1&&!full_thresholds&&!ps_ink_pending()&&!ink_presents,
              "owned strong ink refresh cancels debt and establishes native gray baseline");
        CHECK(!memcmp(guarded.fb,original,FRAME_BYTES),"forced full editor/app baseline retains gray in all rotations");
        continuous_ink_observe();CHECK(present(&ctx,APP_REDRAW_PAGE)&&ink_presents==1&&!fulls&&!renders,"continuous reuse starts only after owned binary strong cleanup");
        continuous_ink_begin(rotations[orientation],application);continuous_ink_observe();test_ink_fail=true;
        CHECK(present(&ctx,APP_REDRAW_PAGE)&&ink_presents==1&&fulls==1&&!ps_ink_pending(),"continuous presenter failure performs one full recovery");
        CHECK(guard_calls==1&&guarded_error==EPD_DRAW_SUCCESS,"continuous failure recovery reaches shared result guard");
    }
    test_ink_mode=test_apps_ink=false;test_ink_pending=false;editor_case=false;s_toast[0]=0;s_panel=0;s_fan=false;
}
static void editor_partial_display_failures(void){
    for(int recovery_fails=0;recovery_fails<2;recovery_fails++){
        editor_begin(false);editor_observe();test_damage_count=3;test_ink_fail=true;fail_remaining=recovery_fails;
        CHECK(present(&ctx,APP_REDRAW_PAGE),"failed keyboard continuous present remains owned");
        CHECK(submitted_count==1&&ink_presents==1&&!follow_updates&&fulls==1&&calls==1&&order[0]=='F'&&!ps_ink_pending(),
              "failed single-phase target attempts one full recovery without another FOLLOW batch");
        CHECK(editor_thresholds==3&&editor_invalidations==1&&!test_partial_ready,
              "recovery invalidates partial reference and preserves prepared local target");
        CHECK(guard_calls==1&&guarded_error==(recovery_fails?EPD_DRAW_EMPTY_LINE_QUEUE:EPD_DRAW_SUCCESS),
              "full recovery result reaches shared guard");
        CHECK(s_force_full==(bool)recovery_fails,"failed full recovery retains a strong-rebuild requirement");
        editor_observe();test_damage_count=1;
        CHECK(present(&ctx,APP_REDRAW_PAGE)&&renders==1&&!submitted_count&&!ink_presents,
              "next keyboard frame rebuilds its complete baseline before accepting incremental drive");
        CHECK(test_partial_ready&&!full_thresholds&&!memcmp(guarded.fb,original,FRAME_BYTES),"recovery rebuild restores complete native gray");
        if(recovery_fails)CHECK(fulls==1&&!follow_updates&&!s_force_full,"failed cleanup cannot be followed by a partial-only recovery");
        editor_observe();
        CHECK(present(&ctx,APP_REDRAW_PAGE)&&submitted_count==1&&ink_presents==1&&!renders,
              "subsequent input resumes continuous drive after baseline recovery");
        check_guards();
    }
    editor_case=false;
}
static void editor_gray_settlement_policy(void){
    // The real runtime idle scheduler has its own integration test; exercise the
    // actual dispatcher once that scheduler requests the final gray frame.
    for(int application=0;application<2;application++)for(int landscape=0;landscape<2;landscape++){
        continuous_ink_begin(landscape?EPD_ROT_INVERTED_LANDSCAPE:EPD_ROT_INVERTED_PORTRAIT,application);
        continuous_ink_observe();CHECK(present(&ctx,APP_REDRAW_PAGE)&&ink_presents==1,"settlement fixture submits local binary damage");
        int64_t due=s_ink_gray_due;CHECK(due==ctx.now_ms+900,"local damage arms the 900 ms gray deadline");
        test_damage_count=0;
        while(ps_ink_pending()){
            continuous_ink_observe();CHECK(present(&ctx,APP_REDRAW_PAGE),"settlement waits while existing phases drain");
            CHECK(s_ink_gray_due==due&&!s_ink_gray_settle&&!renders&&!calls,"phase draining does not postpone or render the gray target");
        }
        continuous_ink_observe();ctx.now_ms=due;s_ink_gray_settle=true;
        CHECK(present(&ctx,APP_REDRAW_PAGE),"idle settlement owns one complete native-gray render");
        CHECK(renders==1&&modes==1&&last_mode==MODE_GL16&&changed_areas==1&&!fulls&&!ink_presents&&!full_thresholds,
              "settlement bypasses binary partial path and submits one bounded GL16 update");
        CHECK(!s_ink_gray_due&&!s_ink_gray_settle&&!ps_ink_pending()&&!memcmp(guarded.fb,original,FRAME_BYTES),
              "gray settlement clears its timer and retains full native-gray pixels");
        for(int idle=0;idle<5;idle++){
            continuous_ink_observe();ctx.now_ms=due+1000+idle*1000;
            CHECK(present(&ctx,APP_REDRAW_PAGE),"settled no-change input remains owned");
            CHECK(!renders&&!calls&&!ink_presents&&!ink_steps&&!s_ink_gray_due&&!s_ink_gray_settle,
                  "settled idle turns issue no repeated full, region, phase or gray refresh");
        }
        check_guards();
    }
    test_apps_ink=test_ink_mode=false;editor_case=false;
}
static void usb_background_frame(void){
    clear_observation();test_usb_background=true;memset(guarded.fb,0x77,FRAME_BYTES);memset(original,0x77,FRAME_BYTES);original[FRAME_BYTES-1]=0x22;expected=original;
    CHECK(ps_global_present(&ctx,&other_app,APP_REDRAW_NONE),"background consumes quiet frame without rendering base");
    CHECK(guarded.fb[FRAME_BYTES-1]==0x77,"quiet background does not paint");
    CHECK(ps_global_present(&ctx,&other_app,APP_REDRAW_PAGE),"background owns memory-only presentation");
    for(size_t i=0;i<FRAME_BYTES-1;i++)CHECK(guarded.fb[i]==0x77,"background preserves previous page pixels");
    CHECK(guarded.fb[FRAME_BYTES-1]==0x22&&renders==1&&modes==1&&last_mode==MODE_GL16,
          "background renders a requested memory-only page once; quiet ticks never repaint");test_usb_background=false;
}
int main(void) {
    (void)ps_view;(void)ps_usb_background_render;
    usb_background_frame();
    // 兼容生产出口移除旧全屏扇形支路；缓存画屏仍由独立测试覆盖。
    // Keep this stub available if production removes its former whole-frame fan branch.
    (void)update_display_with;(void)update_display_area_with;(void)ps_reader_editor_ink_mode;(void)fan_threshold;
    for (unsigned i = 0; i < FRAME_BYTES; i++) {
        unsigned value = i % 256;
        original[i] = (uint8_t)value;
        binary[i] = (value / 16 >= 8 ? 0xf0 : 0) | (value % 16 >= 8 ? 0x0f : 0);
    }
    first_page(); motion_settles_gray(); quantization(); motion_preserves_unchanged_gray(); policy_matrix(); delegated_redraws();
    orientation_changes(); orientation_reversals(); explicit_full_refresh(); live_fan_policy();
    fan_dispatch_boundaries(); editor_cached_policy(); fan_is_one_shot(); display_failures(); diagnostic_context_and_double_failure();shelf_region_policy();
    desktop_full_confirmation();shared_fast_damage_union();editor_input_damage_policy();editor_layout_toast_and_full_policy();editor_partial_display_failures();continuous_ink_dispatch_policy();editor_gray_settlement_policy();
    printf("present host (actual product functions): %u checks; %u failures\n", checks, failures);
    return failures ? 1 : 0;
}
'''

dispatchers = function(SYSTEM, "fast_damage_pixels") + "\n" + function(SYSTEM, "editor_present") + "\n" + function(SYSTEM, "present") + "\n" + function(SYSTEM, "ps_global_present")
has_editor_cache = bool(re.search(r"\bfan_frame_active\s*\(", dispatchers))

with tempfile.TemporaryDirectory(prefix="pico-present-host-") as work:
    work = Path(work)
    source = work / "present.c"
    source.write_text(
        enum(HEADER, "ps_route_t") + "\n" + enum(APP_HEADER, "app_redraw_t")
        + "\n" + re.search(r"enum EpdRotation\s*\{[^}]*\};", EPD_HEADER).group()
        + f"\n#define HAVE_FAN_FRAME_ACTIVE {int(has_editor_cache)}\n"
        + "\n" + prelude + "\n" + function(DRAW,"px") + "\n" + function(DRAW,"ps_rect")
        + "\n" + function(READER, "ps_reader_prefers_grayscale")
        + "\n" + dispatchers + "\n" + tests
    )
    binary_path = work / "present"
    subprocess.run(
        [os.environ.get("CC", "cc"), "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror"]
        + shlex.split(os.environ.get("CFLAGS", ""))
        + [str(source), "-o", str(binary_path)], check=True
    )
    raise SystemExit(subprocess.run([str(binary_path)], env=os.environ.copy()).returncode)

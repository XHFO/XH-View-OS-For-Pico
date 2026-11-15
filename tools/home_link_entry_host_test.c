/* Host-side check: home-link starts on the page armed by PGA_HOME_ENTRY. */
#include "pico_app_sdk.h"
#include <assert.h>
#include <dlfcn.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

static char heading[64];
static int entry_page = -1; /* -1: no deep link. 0..2: armed page. */
static bool configured, saw_empty;
static bool connected = true, busy;
static int dirties;
static char last_toast[96];
static int navigated, controls, status_reads, config_reads, syncs, inputs, rendered_entities;
static unsigned entity_count;
static uint32_t entity_revision=1;
static unsigned entity_shift;
static unsigned hidden_writes;
static bool hidden_saved,hidden_fail;
static unsigned char saved_hidden[264];
static bool drawn[128];
static pico_app_view_t viewport = {360, 640, false};
static bool legacy_status;  /* Old host: ENTRY returns a normal status without ENTRY_OK. */

static pico_app_view_t view(void) { return viewport; }
static int32_t page(void) { return PGA_PAGE_APP; }
static void navigate(int32_t p) { navigated = p; }
static void dirty(void) { dirties++; }
static void toast(const char *text) { snprintf(last_toast, sizeof(last_toast), "%s", text); }
static bool load(const char *key, void *out, size_t bytes) {
    if(strcmp(key,"homehidden1")||!hidden_saved)return false;
    assert(bytes==sizeof(saved_hidden));memcpy(out,saved_hidden,bytes);return true;
}
static bool store(const char *key, const void *data, size_t bytes) {
    if(strcmp(key,"homehidden1"))return false;
    hidden_writes++;assert(bytes==sizeof(saved_hidden));if(hidden_fail)return false;
    memcpy(saved_hidden,data,bytes);hidden_saved=true;return true;
}
static bool legacy_load(const char *key, void *out, size_t bytes) { (void)key; (void)out; (void)bytes; return false; }
static int64_t time_us(void) { return 0; }
static uint32_t random_u32(void) { return 1; }
static void fill(uint8_t *fb, int x, int y, int w, int h, int gray) { (void)fb;
    assert(x >= 0 && y >= 0 && w > 0 && h > 0 && x + w <= viewport.w && y + h <= viewport.h && gray >= 0 && gray <= 15);
    if (h >= 60) { assert(y + h <= viewport.h); rendered_entities++; }
}
static void line(uint8_t *fb, int x0, int y0, int x1, int y1, int gray) { (void)fb; (void)x0; (void)y0; (void)x1; (void)y1; (void)gray; }
static void box(uint8_t *fb, int x, int y, int w, int h, int radius, int gray, bool filled) {
    (void)fb; (void)x; (void)y; (void)w; (void)h; (void)radius; (void)gray; (void)filled;
}
static void text(uint8_t *fb, int x, int y, int size, const char *s, int gray) { (void)fb; (void)x; (void)y; (void)size; (void)s; (void)gray; }
static void center(uint8_t *fb, int x, int y, int size, const char *s, int gray) { (void)fb; (void)x; (void)y; (void)size; (void)s; (void)gray; }
static void wrap(uint8_t *fb, int x, int y, int w, int size, int lines, const char *s, int gray) {
    (void)fb; (void)x; (void)y; (void)w; (void)size; (void)lines; (void)gray;
    if (strstr(s, "尚未配置")) saw_empty = true;
    unsigned id;if(sscanf(s,"Device %u",&id)==1&&id<128)drawn[id]=true;
}
static void button(uint8_t *fb, int x, int y, int w, int h, const char *label, bool selected) {
    (void)fb; (void)label; (void)selected;
    assert(x >= 0 && y >= 0 && w > 0 && h > 0 && x + w <= viewport.w && y + h <= viewport.h);
    if (y == 148 || y == 184 || y == 220 || y == 256 || y == 292 || y == 328)
        assert(y + h <= viewport.h - 68);
}
static void heading_fn(uint8_t *fb, const char *title, const char *subtitle) {
    (void)fb; (void)subtitle;
    snprintf(heading, sizeof(heading), "%s", title ? title : "");
}
static int format(char *out, size_t n, const char *fmt, ...) {
    va_list ap; va_start(ap, fmt); int wrote = vsnprintf(out, n, fmt, ap); va_end(ap); return wrote;
}
static bool home_link(uint32_t command, pico_app_home_status_t *out) {
    if (!out || out->bytes < sizeof(*out)) return false;
    if (command == PGA_HOME_ENTRY) {
        if (legacy_status) {
            memset(out, 0, sizeof(*out));
            out->bytes = sizeof(*out);
            out->result = entry_page > 0 ? (uint8_t)entry_page : 0;
            return true;
        }
        if (entry_page < 0 || entry_page > 2) return false;
        memset(out, 0, sizeof(*out));
        out->bytes = sizeof(*out);
        out->result = (uint8_t)entry_page;
        out->flags = PGA_HOME_ENTRY_OK;
        entry_page = -1;
        return true;
    }
    if (command == PGA_HOME_STATUS || command == PGA_HOME_SYNC) {
        memset(out, 0, sizeof(*out));
        out->bytes = sizeof(*out);
        out->flags = (configured ? PGA_HOME_CFG : 0) | (connected ? PGA_HOME_WIFI : 0);
        out->busy = busy;
        out->entity_count = out->entity_total = (uint16_t)entity_count;
        out->revision = entity_revision;
        status_reads++;
        syncs += command == PGA_HOME_SYNC;
        return true;
    }
    if (command == PGA_HOME_OPEN_SETUP) {
        snprintf(out->error, sizeof(out->error), "请先关闭管理网页");
        return false;
    }
    return false;
}
static bool home_config(uint32_t command, pico_app_home_config_t *cfg) {
    if (!cfg || cfg->bytes < sizeof(*cfg) || command != PGA_HOME_CONFIG_GET) return false;
    config_reads++;
    memset(cfg, 0, sizeof(*cfg));
    cfg->bytes = sizeof(*cfg);
    cfg->enabled = cfg->has_token = configured;
    if (configured) snprintf(cfg->local, sizeof(cfg->local), "http://ha.local:8123");
    return true;
}
static bool home_entity(uint32_t index, pico_app_home_entity_t *out) {
    if (index >= entity_count) return false;
    index=(index+entity_shift)%entity_count;
    memset(out, 0, sizeof(*out)); out->bytes = sizeof(*out);
    snprintf(out->name, sizeof(out->name), "Device %u", index);
    snprintf(out->id, sizeof(out->id), "demo.room_%u", index);
    out->kind = (uint8_t)(index % 6u);
    out->on = (uint8_t)(index & 1u);
    out->available = 1;
    out->number = (float)(22 + (int)(index % 8u));
    snprintf(out->state, sizeof(out->state), "%s", out->on ? "on" : "off");
    snprintf(out->unit, sizeof(out->unit), "%s", out->kind == 4 ? "°C" : out->kind == 5 ? "%" : "");
    snprintf(out->room, sizeof(out->room), "Room %u", index % 4u);
    return true;
}
static bool home_printer(uint32_t slot, pico_app_home_printer_t *out) {
    if (slot >= 3) return false;
    memset(out, 0, sizeof(*out)); out->bytes = sizeof(*out);
    return true;
}
static bool home_control(const char *id, const char *action, float value) { (void)id; (void)action; (void)value; controls++; return true; }
static bool home_input(uint32_t command, char *value, size_t capacity) { (void)value; (void)capacity; inputs += command == PGA_HOME_INPUT_OPEN; return false; }

static const pico_app_host_t host = {
    .abi = PICO_APP_ABI, .bytes = sizeof(pico_app_host_t), .view = view, .page = page, .navigate = navigate,
    .dirty = dirty, .toast = toast, .load = load, .store = store, .legacy_load = legacy_load, .time_us = time_us,
    .random = random_u32, .fill = fill, .line = line, .box = box, .text = text, .center = center, .wrap = wrap,
    .button = button, .heading = heading_fn, .format = format, .copy = memcpy, .move = memmove, .set = memset,
    .compare = memcmp, .home_link = home_link, .home_link_config = home_config, .home_link_entity = home_entity,
    .home_link_printer = home_printer, .home_link_control = home_control, .home_link_input = home_input,
};

static void expect(const pico_app_exports_t *app, int armed, bool legacy, const char *title) {
    entry_page = armed;
    legacy_status = legacy;
    heading[0] = 0;
    assert(app->start(&host));
    app->render(NULL);
    assert(strcmp(heading, title) == 0);
    heading[0] = 0;
    app->render(NULL);
    assert(strcmp(heading, title) == 0);
    app->stop();
}


static bool tap(const pico_app_exports_t *app, int x, int y) {
    return app->event(&(pico_app_event_t){.type = PGA_TAP, .x0 = x, .y0 = y, .x = x, .y = y});
}
static void render_pure(const pico_app_exports_t *app, const char *expected) {
    int reads = status_reads, configs = config_reads, synced = syncs;
    app->render(NULL);
    assert(!strcmp(heading, expected));
    assert(reads == status_reads && configs == config_reads && synced == syncs);
}
static void navigation_contract(const pico_app_exports_t *app) {
    configured = false; entity_count = 0; entry_page = -1; legacy_status = false;
    navigated = -1; saw_empty = false;
    assert(app->start(&host));
    render_pure(app, "家庭连接"); assert(saw_empty);
    assert(tap(app, 50, 600)); render_pure(app, "连接设置");
    assert(tap(app, 300, 600)); render_pure(app, "家庭连接");
    assert(tap(app, 300, 40) && navigated == PGA_PAGE_LAUNCHER);
    app->stop();
    configured = true; entity_count = 96;
    for (int landscape = 0; landscape < 2; landscape++) {
        viewport = landscape ? (pico_app_view_t){640, 360, true} : (pico_app_view_t){360, 640, false};
        entry_page = -1; navigated = -1; rendered_entities = controls = 0;
        assert(app->start(&host));
        render_pure(app, "家庭连接");
        assert(rendered_entities > 0 && !syncs);
        (void)app->tick(); assert(syncs > 0);
        int old_controls = controls;
        assert(tap(app, viewport.w - 50, viewport.h - 32));
        assert(controls == old_controls); render_pure(app, "添加与管理设备");
        assert(tap(app, 120, 180)); render_pure(app, "添加与管理设备");
        assert(app->key(-1)); render_pure(app, "家庭连接");
        assert(tap(app, 50, viewport.h - 32)); render_pure(app, "连接设置");
        int old_inputs = inputs;
        if (landscape) assert(tap(app, 50, 160));
        else assert(tap(app, 50, 232));
        assert(inputs == old_inputs + 1);
        assert(app->key(-1)); render_pure(app, "家庭连接");
        assert(app->key(-1) && navigated == PGA_PAGE_LAUNCHER);
        app->stop(); syncs = 0;
    }
    viewport = (pico_app_view_t){360, 640, false};
    pico_app_host_t old_host = host;
    old_host.home_link = NULL;
    assert(app->start(&old_host));
    render_pure(app, "家庭连接");
    assert(tap(app, 50, 600) && navigated == PGA_PAGE_LAUNCHER);
    app->stop();
    puts("PASS: empty/configured monitor, explicit setup/back, printer blank taps, bounded portrait/landscape controls, pure rendering, unavailable host exit");
}

static void network_contract(const pico_app_exports_t *app) {
    configured = true; connected = false; busy = false; entity_count = 0;
    entry_page = -1; legacy_status = false; syncs = 0;
    assert(app->start(&host));
    (void)app->tick();
    assert(!syncs);
    int painted = dirties;
    connected = true;
    (void)app->tick();
    assert(dirties > painted && syncs == 1);
    painted = dirties;
    (void)app->tick();
    assert(dirties == painted && syncs == 1);
    connected = false;
    (void)app->tick();
    assert(dirties == painted + 1 && syncs == 1);
    connected = true;
    (void)app->tick();
    assert(syncs == 2);
    painted = dirties; busy = true;
    (void)app->tick();
    assert(dirties == painted + 1);
    painted = dirties; busy = false;
    (void)app->tick();
    assert(dirties == painted + 1);
    assert(tap(app, 50, 600));
    assert(tap(app, 50, 600));
    assert(!strcmp(last_toast, "请先关闭管理网页"));
    app->stop();
    puts("PASS: unchanged HA revision still repaints WiFi/busy transitions, syncs once on reconnect, and shows the actual portal failure");
}


static bool raw_event(const pico_app_exports_t *app, int type, int x, int y) {
    return app->event(&(pico_app_event_t){.type = type, .x0 = x, .y0 = y, .x = x, .y = y});
}

static void drag_contract(const pico_app_exports_t *app) {
    configured = true; entity_count = 6; entry_page = -1; legacy_status = false;
    viewport = (pico_app_view_t){360, 640, false};
    assert(app->start(&host));
    app->render(NULL);
    last_toast[0] = 0;
    // 2 columns x 3 rows: first card center 94,226; second 265,226.
    assert(raw_event(app, PGA_LONG, 94, 226));
    assert(raw_event(app, PGA_MOVE, 265, 226));
    assert(raw_event(app, PGA_RELEASE, 265, 226));
    assert(!strcmp(last_toast, "卡片顺序已保存"));
    int old_controls = controls;
    assert(raw_event(app, PGA_TAP, 94, 226));
    assert(controls == old_controls);
    (void)raw_event(app, PGA_PRESS, 94, 226);
    app->stop();
    puts("PASS: HA card long-press drag, order commit and suppressed synthetic tap");
}

static void management_contract(const pico_app_exports_t *app) {
    for(int landscape=0;landscape<2;landscape++) {
        viewport=landscape?(pico_app_view_t){640,360,true}:(pico_app_view_t){360,640,false};
        configured=true;entity_count=16;entity_shift=0;entity_revision++;hidden_saved=hidden_fail=false;hidden_writes=0;
        assert(app->start(&host));int prior=controls;
        assert(tap(app,viewport.w-40,viewport.h-32));
        int row_y=landscape?130:178;assert(tap(app,40,row_y));
        assert(tap(app,40,viewport.h-32));assert(!hidden_writes&&controls==prior);
        assert(tap(app,viewport.w-40,viewport.h-32));
        memset(drawn,0,sizeof(drawn));app->render(NULL);assert(drawn[0]);
        assert(tap(app,40,row_y));
        assert(raw_event(app,PGA_SWIPE_L,180,180));assert(tap(app,40,row_y));
        hidden_fail=true;assert(tap(app,viewport.w-40,viewport.h-32));render_pure(app,"添加与管理设备");assert(!hidden_saved);
        hidden_fail=false;assert(tap(app,viewport.w-40,viewport.h-32));render_pure(app,"家庭连接");assert(hidden_saved&&hidden_writes==2&&controls==prior);
        memset(drawn,0,sizeof(drawn));app->render(NULL);assert(!drawn[0]);
        app->stop();assert(app->start(&host));memset(drawn,0,sizeof(drawn));app->render(NULL);assert(!drawn[0]);
        entity_shift=8;entity_revision++;app->tick();memset(drawn,0,sizeof(drawn));app->render(NULL);assert(!drawn[0]);
        assert(tap(app,viewport.w-40,viewport.h-32));assert(raw_event(app,PGA_SWIPE_L,180,180));
        memset(drawn,0,sizeof(drawn));app->render(NULL);assert(drawn[0]);
        assert(tap(app,40,row_y+(landscape?46:0)));assert(tap(app,viewport.w-40,viewport.h-32));
        entity_shift=0;entity_revision++;app->tick();memset(drawn,0,sizeof(drawn));app->render(NULL);assert(drawn[0]);
        app->stop();hidden_saved=false;entity_shift=0;
    }
    puts("PASS: HA device visibility cancel, failed save/retry, cross-page selection, restart persistence, entity reorder and no real-device control");
}

int main(int argc, char **argv) {
    assert(argc == 2);
    void *library = dlopen(argv[1], RTLD_NOW);
    assert(library);
    pico_app_query_t query = (pico_app_query_t)dlsym(library, "pico_app_query");
    assert(query);
    const pico_app_exports_t *app = query();
    assert(app && app->version == 10109 && app->kind == 130 && !strcmp(app->id, "home-link"));
    expect(app, -1, false, "家庭连接");
    expect(app, 1, false, "连接设置");
    expect(app, 2, false, "家庭连接");
    expect(app, 1, true, "家庭连接");
    puts("PASS: home-link entry opens setup/printers once and ignores a host without ENTRY_OK");
    navigation_contract(app);
    network_contract(app);
    drag_contract(app);
    management_contract(app);
    return 0;
}

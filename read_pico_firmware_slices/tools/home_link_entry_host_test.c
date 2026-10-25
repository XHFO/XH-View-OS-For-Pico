/* Host-side check: home-link starts on the page armed by PGA_HOME_ENTRY. */
#include "pico_app_sdk.h"
#include <assert.h>
#include <dlfcn.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

static char heading[64];
static int entry_page = -1; /* -1: no deep link. 0..2: armed page. */
static bool legacy_status;  /* Old host: ENTRY returns a normal status without ENTRY_OK. */

static pico_app_view_t view(void) { return (pico_app_view_t){360, 640, false}; }
static int32_t page(void) { return PGA_PAGE_APP; }
static void navigate(int32_t p) { (void)p; }
static void dirty(void) {}
static void toast(const char *text) { (void)text; }
static bool load(const char *key, void *out, size_t bytes) { (void)key; (void)out; (void)bytes; return false; }
static bool store(const char *key, const void *data, size_t bytes) { (void)key; (void)data; (void)bytes; return false; }
static bool legacy_load(const char *key, void *out, size_t bytes) { (void)key; (void)out; (void)bytes; return false; }
static int64_t time_us(void) { return 0; }
static uint32_t random_u32(void) { return 1; }
static void fill(uint8_t *fb, int x, int y, int w, int h, int gray) { (void)fb; (void)x; (void)y; (void)w; (void)h; (void)gray; }
static void line(uint8_t *fb, int x0, int y0, int x1, int y1, int gray) { (void)fb; (void)x0; (void)y0; (void)x1; (void)y1; (void)gray; }
static void box(uint8_t *fb, int x, int y, int w, int h, int radius, int gray, bool filled) {
    (void)fb; (void)x; (void)y; (void)w; (void)h; (void)radius; (void)gray; (void)filled;
}
static void text(uint8_t *fb, int x, int y, int size, const char *s, int gray) { (void)fb; (void)x; (void)y; (void)size; (void)s; (void)gray; }
static void center(uint8_t *fb, int x, int y, int size, const char *s, int gray) { (void)fb; (void)x; (void)y; (void)size; (void)s; (void)gray; }
static void wrap(uint8_t *fb, int x, int y, int w, int size, int lines, const char *s, int gray) {
    (void)fb; (void)x; (void)y; (void)w; (void)size; (void)lines; (void)s; (void)gray;
}
static void button(uint8_t *fb, int x, int y, int w, int h, const char *label, bool selected) {
    (void)fb; (void)x; (void)y; (void)w; (void)h; (void)label; (void)selected;
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
        return true;
    }
    return false;
}
static bool home_config(uint32_t command, pico_app_home_config_t *cfg) {
    if (!cfg || cfg->bytes < sizeof(*cfg) || command != PGA_HOME_CONFIG_GET) return false;
    memset(cfg, 0, sizeof(*cfg));
    cfg->bytes = sizeof(*cfg);
    return true;
}
static bool home_entity(uint32_t index, pico_app_home_entity_t *out) { (void)index; (void)out; return false; }
static bool home_printer(uint32_t slot, pico_app_home_printer_t *out) { (void)slot; (void)out; return false; }
static bool home_control(const char *id, const char *action, float value) { (void)id; (void)action; (void)value; return false; }
static bool home_input(uint32_t command, char *value, size_t capacity) { (void)command; (void)value; (void)capacity; return false; }

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

int main(int argc, char **argv) {
    assert(argc == 2);
    void *library = dlopen(argv[1], RTLD_NOW);
    assert(library);
    pico_app_query_t query = (pico_app_query_t)dlsym(library, "pico_app_query");
    assert(query);
    const pico_app_exports_t *app = query();
    assert(app && app->version == 10101 && app->kind == 130 && !strcmp(app->id, "home-link"));
    expect(app, -1, false, "家庭连接");
    expect(app, 1, false, "连接设置");
    expect(app, 2, false, "打印机");
    expect(app, 1, true, "家庭连接");
    puts("PASS: home-link entry opens setup/printers once and ignores a host without ENTRY_OK");
    return 0;
}

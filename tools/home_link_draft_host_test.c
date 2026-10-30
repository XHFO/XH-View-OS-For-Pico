/* SPDX-License-Identifier: Apache-2.0
 * Exercise the actual TF module through its exported ABI, including a host that
 * wipes the SET argument on failure exactly as the firmware host does.
 */
#include "pico_app_sdk.h"
#include <assert.h>
#include <dlfcn.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *const token = "draft-ha-token-do-not-render";
static const char *const client_id = "draft-cf-id-do-not-render";
static const char *const client_secret = "draft-cf-secret-do-not-render";
static pico_app_home_config_t saved, submitted;
static const pico_app_exports_t *app;
static uint32_t status_flags;
static int entry_page, config_get_count, config_set_count, syncs, cancels;
static int token_wipes, id_wipes, secret_wipes;
static bool save_ok, input_open, input_ready;
static char input_value[256], input_seed[256];

static void safe_text(const char *s) {
    if (!s) return;
    assert(!strstr(s, token));
    assert(!strstr(s, client_id));
    assert(!strstr(s, client_secret));
}
static pico_app_view_t view(void) { return (pico_app_view_t){360, 640, false}; }
static int32_t page(void) { return PGA_PAGE_APP; }
static void navigate(int32_t p) { (void)p; }
static void dirty(void) {}
static void toast(const char *s) { safe_text(s); }
static void fill(uint8_t *fb, int x, int y, int w, int h, int gray) {
    (void)fb; (void)x; (void)y; (void)w; (void)h; (void)gray;
}
static void text(uint8_t *fb, int x, int y, int size, const char *s, int gray) {
    (void)fb; (void)x; (void)y; (void)size; (void)gray; safe_text(s);
}
static void wrap(uint8_t *fb, int x, int y, int w, int size, int lines, const char *s, int gray) {
    (void)w; (void)lines; text(fb, x, y, size, s, gray);
}
static void button(uint8_t *fb, int x, int y, int w, int h, const char *s, bool selected) {
    (void)w; (void)h; (void)selected; text(fb, x, y, 0, s, 0);
}
static void heading(uint8_t *fb, const char *title, const char *subtitle) {
    (void)fb; safe_text(title); safe_text(subtitle);
}
static int format(char *out, size_t n, const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    int result = vsnprintf(out, n, fmt, args);
    va_end(args);
    return result;
}
static bool contains(const void *data, size_t n, const char *needle) {
    size_t size = strlen(needle);
    if (n < size) return false;
    for (size_t i = 0; i <= n - size; i++)
        if (!memcmp((const char *)data + i, needle, size)) return true;
    return false;
}
static void *set(void *out, int value, size_t n) {
    if (!value) {
        token_wipes += contains(out, n, token);
        id_wipes += contains(out, n, client_id);
        secret_wipes += contains(out, n, client_secret);
    }
    return memset(out, value, n);
}
static bool home_link(uint32_t command, pico_app_home_status_t *out) {
    assert(out && out->bytes >= sizeof(*out));
    memset(out, 0, sizeof(*out));
    out->bytes = sizeof(*out);
    if (command == PGA_HOME_ENTRY) {
        if (entry_page < 0) return false;
        out->result = (uint8_t)entry_page;
        out->flags = PGA_HOME_ENTRY_OK;
        entry_page = -1;
        return true;
    }
    out->flags = status_flags;
    if (command == PGA_HOME_SYNC) syncs++;
    return command == PGA_HOME_STATUS || command == PGA_HOME_SYNC;
}
static void wipe_credentials(pico_app_home_config_t *cfg) {
    memset(cfg->ha_token, 0, sizeof(cfg->ha_token));
    memset(cfg->cf_access_client_id, 0, sizeof(cfg->cf_access_client_id));
    memset(cfg->cf_access_client_secret, 0, sizeof(cfg->cf_access_client_secret));
}
static bool home_config(uint32_t command, pico_app_home_config_t *cfg) {
    assert(cfg && cfg->bytes >= sizeof(*cfg));
    if (command == PGA_HOME_CONFIG_GET) {
        config_get_count++;
        memcpy(cfg, &saved, sizeof(*cfg));
        cfg->bytes = sizeof(*cfg);
        cfg->has_token = saved.ha_token[0] != 0;
        cfg->has_cf_id = saved.cf_access_client_id[0] != 0;
        cfg->has_cf_secret = saved.cf_access_client_secret[0] != 0;
        wipe_credentials(cfg);
        return true;
    }
    assert(command == PGA_HOME_CONFIG_SET);
    config_set_count++;
    memcpy(&submitted, cfg, sizeof(submitted));
    if (save_ok) {
        pico_app_home_config_t next = *cfg;
        if (!next.ha_token[0]) snprintf(next.ha_token, sizeof(next.ha_token), "%s", saved.ha_token);
        if (!next.cf_access_client_id[0])
            snprintf(next.cf_access_client_id, sizeof(next.cf_access_client_id), "%s", saved.cf_access_client_id);
        if (!next.cf_access_client_secret[0])
            snprintf(next.cf_access_client_secret, sizeof(next.cf_access_client_secret), "%s", saved.cf_access_client_secret);
        saved = next;
    }
    /* Production SET deliberately mutates this argument even when persistence fails. */
    wipe_credentials(cfg);
    return save_ok;
}
static bool home_entity(uint32_t index, pico_app_home_entity_t *out) { (void)index; (void)out; return false; }
static bool home_printer(uint32_t slot, pico_app_home_printer_t *out) { (void)slot; (void)out; return false; }
static bool home_control(const char *id, const char *action, float value) {
    (void)id; (void)action; (void)value; return false;
}
static bool home_input(uint32_t command, char *value, size_t capacity) {
    if (command == PGA_HOME_INPUT_OPEN) {
        assert(!input_open);
        snprintf(input_seed, sizeof(input_seed), "%s", value);
        input_open = true;
        input_ready = false;
        return true;
    }
    if (command == PGA_HOME_INPUT_CANCEL) {
        cancels++;
        input_open = input_ready = false;
        memset(input_value, 0, sizeof(input_value));
        return true;
    }
    assert(command == PGA_HOME_INPUT_TAKE);
    if (!input_open || !input_ready) return false;
    snprintf(value, capacity, "%s", input_value);
    input_open = input_ready = false;
    memset(input_value, 0, sizeof(input_value));
    return true;
}
static const pico_app_host_t host = {
    .abi = PICO_APP_ABI, .bytes = sizeof(pico_app_host_t), .view = view, .page = page,
    .navigate = navigate, .dirty = dirty, .toast = toast, .fill = fill, .text = text,
    .wrap = wrap, .button = button, .heading = heading, .format = format,
    .copy = memcpy, .set = set, .home_link = home_link, .home_link_config = home_config,
    .home_link_entity = home_entity, .home_link_printer = home_printer,
    .home_link_control = home_control, .home_link_input = home_input,
};

static void reset(void) {
    memset(&saved, 0, sizeof(saved));
    memset(&submitted, 0, sizeof(submitted));
    saved.bytes = sizeof(saved);
    snprintf(saved.local, sizeof(saved.local), "http://original.local:8123");
    snprintf(saved.external, sizeof(saved.external), "https://original.example");
    snprintf(saved.home_wifi, sizeof(saved.home_wifi), "Original WiFi");
    status_flags = 0;
    entry_page = 1;
    config_get_count = config_set_count = syncs = cancels = 0;
    token_wipes = id_wipes = secret_wipes = 0;
    save_ok = false;
    input_open = input_ready = false;
    memset(input_value, 0, sizeof(input_value));
    memset(input_seed, 0, sizeof(input_seed));
    assert(app->start(&host));
    app->render(NULL);
}
static void tap(int x, int y) {
    pico_app_event_t e = {.type = PGA_TAP, .x0 = x, .y0 = y, .x = x, .y = y};
    assert(app->event(&e));
}
static void edit(int field, const char *value) {
    tap(80, 160 + field * 36);
    assert(input_open);
    if (field >= 2 && field <= 4) assert(!input_seed[0]);
    snprintf(input_value, sizeof(input_value), "%s", value);
    input_ready = true;
    (void)app->tick();
    assert(!input_open);
    app->render(NULL);
}
static void press_save(void) { tap(170, 600); app->render(NULL); }
static void fill_all(void) {
    edit(0, "http://edited.local:8123");
    edit(1, "https://edited.example");
    edit(2, token);
    edit(3, client_id);
    edit(4, client_secret);
    edit(5, "Edited WiFi");
}
static void assert_all(void) {
    assert(!strcmp(submitted.local, "http://edited.local:8123"));
    assert(!strcmp(submitted.external, "https://edited.example"));
    assert(!strcmp(submitted.ha_token, token));
    assert(!strcmp(submitted.cf_access_client_id, client_id));
    assert(!strcmp(submitted.cf_access_client_secret, client_secret));
    assert(!strcmp(submitted.home_wifi, "Edited WiFi"));
    assert(submitted.enabled);
}
static void sequential(void) {
    reset();
    int initial_config_get_count = config_get_count;
    fill_all();
    assert(config_get_count == initial_config_get_count); /* No GET can overwrite preceding unsaved fields. */
    press_save();
    assert(config_set_count == 1);
    assert_all();
    app->stop();
    puts("PASS: sequential field edits stay in one draft and credentials never render");
}
static void retry(void) {
    reset();
    edit(2, token);
    press_save();
    assert(config_set_count == 1 && !strcmp(submitted.ha_token, token));
    press_save();
    assert(config_set_count == 2 && !strcmp(submitted.ha_token, token));
    app->stop();
    puts("PASS: failed host SET may wipe its argument without destroying the retry draft");
}
static void gates(void) {
    reset();
    fill_all();
    status_flags = PGA_HOME_PORTAL;
    press_save();
    assert(config_set_count == 0);
    status_flags = PGA_HOME_MANAGE;
    press_save();
    assert(config_set_count == 0);
    status_flags = 0;
    press_save();
    assert(config_set_count == 1);
    assert_all();
    save_ok = true;
    press_save();
    assert(config_set_count == 2 && syncs == 1);
    assert_all();
    press_save();
    assert(config_set_count == 3 && syncs == 2);
    assert(!submitted.ha_token[0] && !submitted.cf_access_client_id[0] && !submitted.cf_access_client_secret[0]);
    app->stop();
    puts("PASS: portal/manage gates retain draft; successful save reloads a redacted config");
}
static void lifecycle(void) {
    reset();
    edit(2, token);
    edit(3, client_id);
    edit(4, client_secret);
    int previous_token = token_wipes, previous_id = id_wipes, previous_secret = secret_wipes;
    tap(80, 160); /* Exit while a further input session is open. */
    assert(input_open);
    app->stop();
    assert(cancels == 1 && !input_open);
    assert(token_wipes > previous_token && id_wipes > previous_id && secret_wipes > previous_secret);
    entry_page = 1;
    assert(app->start(&host));
    press_save();
    assert(!submitted.ha_token[0] && !submitted.cf_access_client_id[0] && !submitted.cf_access_client_secret[0]);
    app->stop();
    puts("PASS: stop cancels input, wipes pending credentials, and restart cannot resubmit them");
}
int main(int argc, char **argv) {
    assert(argc == 3);
    void *library = dlopen(argv[1], RTLD_NOW);
    if (!library) { fprintf(stderr, "dlopen: %s\n", dlerror()); return 1; }
    pico_app_query_t query = (pico_app_query_t)dlsym(library, "pico_app_query");
    assert(query);
    app = query();
    assert(app && app->kind == 130 && !strcmp(app->id, "home-link"));
    if (!strcmp(argv[2], "all") || !strcmp(argv[2], "sequential")) sequential();
    if (!strcmp(argv[2], "all") || !strcmp(argv[2], "retry")) retry();
    if (!strcmp(argv[2], "all") || !strcmp(argv[2], "gates")) gates();
    if (!strcmp(argv[2], "all") || !strcmp(argv[2], "lifecycle")) lifecycle();
    printf("PASS: actual Home Link %u draft contract\n", app->version);
    dlclose(library);
    return 0;
}

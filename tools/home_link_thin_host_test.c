/* Thin-host manage contract: 503 home_module_missing, no persistence, no Bambu session. */
#include "home_link_web.h"
#include "read_pico_home_link.h"
#include "home_link_portal.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static rp_home_config_t saved;
static unsigned commits, reads;
static bool portal_active;
static rp_portal_public_t public_state;
static bool load(rp_home_config_t *config, rp_home_snapshot_t *snapshot) {
    reads++;
    *config = saved;
    memset(snapshot, 0, sizeof(*snapshot));
    return true;
}
static bool commit(const rp_home_config_t *config, bool *queued) {
    commits++;
    saved = *config;
    *queued = true;
    return true;
}
static bool active(void) { return portal_active; }
static void get_public(rp_portal_public_t *out) { *out = public_state; }
static void set_public(const rp_portal_public_t *in) { public_state = *in; }

static void bridge_contract(void) {
    char out[8192];
    rp_home_web_response_t response = {0};
    rp_home_web_request_t request = {.method = "GET", .path = "/api/manage/home"};
    saved.version = RP_HOME_CONFIG_VERSION;
    snprintf(saved.local, sizeof(saved.local), "http://ha.local:8123");
    snprintf(saved.ha_token, sizeof(saved.ha_token), "thin-test-private-token");
    rp_home_store_bridge_t bridge = {load, commit, active, get_public, set_public};
    rp_home_set_store_bridge(&bridge);
    assert(rp_home_store_bridge_ready());
    assert(rp_home_web_handle(&request, &response, out, sizeof(out)));
    assert(response.http_status == 200 && response.body_bytes == strlen(out) && reads == 1 && commits == 0);
    assert(strstr(out, "http://ha.local:8123") && strstr(out, "\"has_token\":true"));
    assert(!strstr(out, "thin-test-private-token"));

    request.method = "PUT";
    request.body = "{\"enabled\":true}";
    request.body_bytes = strlen(request.body);
    assert(rp_home_web_handle(&request, &response, out, sizeof(out)));
    assert(response.http_status == 200 && commits == 1 && saved.enabled);
    assert(!strcmp(saved.ha_token, "thin-test-private-token"));

    // 注册桥后才允许配置；手机门户持有写权限时不能从manage抢写。
    // Configuration is enabled only by the registered bridge; manage cannot steal the phone portal's write ownership.
    portal_active = true;
    unsigned old_reads = reads;
    request.body = "{\"enabled\":false}";
    request.body_bytes = strlen(request.body);
    assert(rp_home_web_handle(&request, &response, out, sizeof(out)));
    assert(response.http_status == 409 && strstr(out, "home_portal_active"));
    assert(commits == 1 && saved.enabled && reads == old_reads);
    request.path = "/api/manage/printers";
    request.method = "DELETE";
    request.query = "slot=0";
    request.body = NULL;
    request.body_bytes = 0;
    assert(rp_home_web_handle(&request, &response, out, sizeof(out)));
    assert(response.http_status == 409 && commits == 1 && reads == old_reads);
    request.method = "GET";
    assert(rp_home_web_handle(&request, &response, out, sizeof(out)) && response.http_status == 200);
    assert(strstr(out, "\"printers\":[") && commits == 1);

    request.path = "/api/manage/bambu";
    assert(rp_home_web_handle(&request, &response, out, sizeof(out)) && response.http_status == 503);
    assert(strstr(out, "home_module_missing") && commits == 1);
    rp_home_set_store_bridge(NULL);
    request.path = "/api/manage/home";
    assert(!rp_home_store_bridge_ready());
    assert(rp_home_web_handle(&request, &response, out, sizeof(out)) && response.http_status == 503);
    assert(commits == 1);
}

int main(void) {
    static const char expect[] =
        "{\"ok\":false,\"error\":\"home_module_missing\",\"message\":\"Install TF tools/home-link.pga\"}";
    static const char *paths[] = {"/api/manage/home", "/api/manage/home/extra", "/api/manage/printers",
                                  "/api/manage/printer?slot=0", "/api/manage/bambu", "/api/manage/bambu/auth"};
    static const char *methods[] = {"GET", "PUT", "POST", "DELETE"};
    char out[256];
    rp_home_web_response_t response;
    for (unsigned p = 0; p < sizeof(paths) / sizeof(paths[0]); p++) {
        for (unsigned m = 0; m < sizeof(methods) / sizeof(methods[0]); m++) {
            rp_home_web_request_t request = {.method = methods[m], .path = paths[p], .body = "{}", .body_bytes = 2};
            memset(out, 0x5a, sizeof(out));
            assert(rp_home_web_handle(&request, &response, out, sizeof(out)));
            assert(response.http_status == 503);
            assert(response.body_bytes == sizeof(expect) - 1);
            assert(!strcmp(out, expect));
        }
    }
    rp_home_web_request_t other = {.method = "GET", .path = "/api/manage/ha/entities"};
    assert(!rp_home_web_handle(&other, &response, out, sizeof(out)));
    rp_home_config_t config = {.version = RP_HOME_CONFIG_VERSION, .enabled = 1};
    assert(!read_pico_home_link_configure(&config));
    assert(!read_pico_home_link_request(true, true));
    assert(!read_pico_home_link_control(NULL, "turn_on", 0, true));
    assert(read_pico_home_link_suspend(0));
    rp_home_bambu_input_t input;
    memset(&input, 0, sizeof(input));
    assert(!read_pico_bambu_auth_submit(&input, true));
    rp_home_bambu_state_t state;
    read_pico_bambu_auth_get(&state);
    assert(state.phase == RP_BAMBU_IDLE);
    assert(!read_pico_bambu_auth_prepare_import(1, 0, &config.printers[0]));
    bridge_contract();
    printf("thin home-link: missing-module 503, real bridge routing, portal write exclusion and private-token redaction passed\n");
    return 0;
}

/* SPDX-License-Identifier: Apache-2.0
 * 局域网填写与 Mac 回读的字段契约。不监听套接字，不保存真实密钥。
 * Field contract for the LAN form and Mac readback. No sockets and no real secrets.
 */
#include "home_link_json.h"
#include "home_link_portal.h"
#include "home_link_web.h"
#include <assert.h>
#include <ctype.h>
#include <stdio.h>
#include <string.h>

static rp_home_config_t store;
static rp_home_snapshot_t snap;
static rp_portal_public_t pub;
static int commits;
static bool portal_on, commit_ok = true;
static const char *saved_ssid = "Home";

static bool load_cb(rp_home_config_t *config, rp_home_snapshot_t *snapshot) {
    *config = store;
    *snapshot = snap;
    return true;
}
static bool commit_cb(const rp_home_config_t *config, bool *queued) {
    if (!commit_ok) return false;
    store = *config;
    commits++;
    if (queued) *queued = true;
    return true;
}
static bool active_cb(void) { return portal_on; }
static void get_cb(rp_portal_public_t *out) { *out = pub; }
static void set_cb(const rp_portal_public_t *in) {
    pub.phase = in->phase;
    pub.revision = in->revision;
}

static void install(void) {
    rp_home_store_bridge_t bridge = {load_cb, commit_cb, active_cb, get_cb, set_cb};
    rp_home_set_store_bridge(&bridge);
}

static void seed(void) {
    memset(&store, 0, sizeof(store));
    memset(&snap, 0, sizeof(snap));
    memset(&pub, 0, sizeof(pub));
    commits = 0;
    portal_on = false;
    commit_ok = true;
    store.version = RP_HOME_CONFIG_VERSION;
    store.enabled = 1;
    snprintf(store.local, sizeof(store.local), "http://ha.local:8123");
    snprintf(store.ha_token, sizeof(store.ha_token), "secret-token-value");
    snprintf(store.cf_access_client_id, sizeof(store.cf_access_client_id), "cf-id-value");
    snprintf(store.cf_access_client_secret, sizeof(store.cf_access_client_secret), "cf-secret-value");
    store.room_count = 1;
    snprintf(store.rooms[0].entity, sizeof(store.rooms[0].entity), "light.desk");
    snprintf(store.rooms[0].room, sizeof(store.rooms[0].room), "书房");
    store.printers[0].source = RP_HOME_BAMBU_CLOUD;
    store.printers[0].region = 1;
    snprintf(store.printers[0].name, sizeof(store.printers[0].name), "Cloud");
    snprintf(store.printers[0].serial, sizeof(store.printers[0].serial), "01PTEST001");
    snprintf(store.printers[0].username, sizeof(store.printers[0].username), "u_123456");
    snprintf(store.printers[0].secret, sizeof(store.printers[0].secret), "bambu-secret-value");
    snap.result = 2;
    snap.revision = 9;
    pub.phase = RP_PORTAL_OPEN;
    pub.revision = 4;
    pub.active = true;
    pub.seconds_left = 42;
    install();
}

static int call(const char *method, const char *path, const char *query, const char *body, bool manage, char *out,
                size_t cap) {
    const char *wifi[] = {saved_ssid};
    rp_home_web_request_t request = {.method = method,
                                     .path = path,
                                     .query = query,
                                     .body = body,
                                     .body_bytes = body ? strlen(body) : 0,
                                     .network_online = true,
                                     .current_wifi = "Home",
                                     .saved_wifi = wifi,
                                     .saved_wifi_count = 1};
    rp_home_web_response_t response = {0};
    assert(rp_portal_serve(&request, &response, out, cap, manage));
    assert(response.body_bytes == strlen(out));
    return response.http_status;
}

static cJSON *parse(const char *json) {
    const char *end = NULL;
    cJSON *j = cJSON_ParseWithOpts(json, &end, false);
    assert(j && end && !*end);
    return j;
}
static bool flag(const cJSON *j, const char *key) {
    cJSON *v = cJSON_GetObjectItemCaseSensitive(j, key);
    assert(cJSON_IsBool(v));
    return cJSON_IsTrue(v);
}
static void no_secrets(const char *json) {
    assert(!strstr(json, "secret-token-value"));
    assert(!strstr(json, "cf-id-value"));
    assert(!strstr(json, "cf-secret-value"));
    assert(!strstr(json, "bambu-secret-value"));
    assert(!strstr(json, "ha_token\":\""));
    assert(!strstr(json, "\"ha_token\""));
    assert(!strstr(json, "cf_access_client_secret\":\""));
}

int main(void) {
    char out[8192];
    seed();
    assert(!strcmp(rp_portal_phase_name(RP_PORTAL_IDLE), "idle"));
    assert(!strcmp(rp_portal_phase_name(RP_PORTAL_OPEN), "open"));
    assert(!strcmp(rp_portal_phase_name(RP_PORTAL_SAVED), "saved"));
    assert(!strcmp(rp_portal_phase_name(RP_PORTAL_OFFLINE), "offline"));
    assert(!strcmp(rp_portal_phase_name(RP_PORTAL_ERROR), "error"));

    assert(call("GET", "/api/lan/home", NULL, NULL, false, out, sizeof(out)) == 200);
    no_secrets(out);
    cJSON *home = parse(out);
    cJSON *config = cJSON_GetObjectItemCaseSensitive(home, "config");
    cJSON *status = cJSON_GetObjectItemCaseSensitive(home, "status");
    cJSON *portal = cJSON_GetObjectItemCaseSensitive(home, "portal");
    assert(flag(config, "enabled") && flag(config, "has_token") && flag(config, "has_cf_access_client_id") &&
           flag(config, "has_cf_access_client_secret"));
    assert(!strcmp(cJSON_GetObjectItemCaseSensitive(config, "local")->valuestring, "http://ha.local:8123"));
    assert(cJSON_GetArraySize(cJSON_GetObjectItemCaseSensitive(config, "rooms")) == 1);
    assert(cJSON_GetObjectItemCaseSensitive(status, "result")->valuedouble == 2);
    assert(cJSON_GetObjectItemCaseSensitive(status, "revision")->valuedouble == 9);
    assert(!strcmp(cJSON_GetObjectItemCaseSensitive(portal, "phase")->valuestring, "open"));
    assert(cJSON_GetObjectItemCaseSensitive(portal, "revision")->valuedouble == 4);
    assert(flag(portal, "active") && !strcmp(cJSON_GetObjectItemCaseSensitive(portal, "writer")->valuestring, "device"));
    assert(cJSON_GetArraySize(cJSON_GetObjectItemCaseSensitive(home, "saved_wifi")) == 1);
    cJSON_Delete(home);

    assert(call("PUT", "/api/lan/home", NULL, "{\"local\":\"http://ha.local:8123\"}", false, out, sizeof(out)) == 200);
    assert(commits == 1 && !strcmp(store.ha_token, "secret-token-value") && store.room_count == 1);
    assert(pub.phase == RP_PORTAL_SAVED && pub.revision == 5 && pub.active && pub.seconds_left == 42);
    no_secrets(out);
    home = parse(out);
    assert(flag(home, "ok") && flag(home, "queued"));
    assert(!strcmp(cJSON_GetObjectItemCaseSensitive(cJSON_GetObjectItemCaseSensitive(home, "portal"), "phase")->valuestring,
                   "saved"));
    cJSON_Delete(home);

    assert(call("PUT", "/api/lan/home", NULL, "{\"ha_token\":\"\"}", false, out, sizeof(out)) == 200);
    assert(!strcmp(store.ha_token, "secret-token-value"));
    assert(call("PUT", "/api/lan/home", NULL, "{\"clear\":[\"ha_token\"]}", false, out, sizeof(out)) == 200);
    assert(!store.ha_token[0] && !strcmp(store.cf_access_client_id, "cf-id-value"));
    assert(call("PUT", "/api/lan/home", NULL, "{\"clear\":[\"cf_access_client_secret\"]}", false, out, sizeof(out)) == 400);
    assert(strstr(out, "invalid_configuration") && !strcmp(store.cf_access_client_id, "cf-id-value") &&
           !strcmp(store.cf_access_client_secret, "cf-secret-value"));
    int before = commits;
    assert(call("PUT", "/api/lan/home", NULL, "{\"extra\":1}", false, out, sizeof(out)) == 400);
    assert(commits == before && !strcmp(store.local, "http://ha.local:8123"));
    assert(call("PUT", "/api/lan/home", NULL, "{", false, out, sizeof(out)) == 400 && strstr(out, "bad_json"));
    assert(call("PUT", "/api/lan/home", NULL, "{\"home_wifi\":\"Other\"}", false, out, sizeof(out)) == 400);
    assert(!store.home_wifi[0]);
    assert(call("PUT", "/api/lan/home", NULL,
                "{\"home_wifi\":\"Home\",\"rooms\":[{\"entity\":\"switch.lamp\",\"room\":\"客厅\"}]}", false, out,
                sizeof(out)) == 200);
    assert(store.room_count == 1 && !strcmp(store.rooms[0].entity, "switch.lamp") && !strcmp(store.home_wifi, "Home"));
    assert(call("PUT", "/api/lan/home", NULL, "{\"enabled\":false}", false, out, sizeof(out)) == 200);
    assert(store.room_count == 1 && !strcmp(store.rooms[0].entity, "switch.lamp"));

    assert(call("GET", "/api/lan/printers", NULL, NULL, false, out, sizeof(out)) == 200);
    no_secrets(out);
    cJSON *printers = parse(out);
    assert(cJSON_GetArraySize(cJSON_GetObjectItemCaseSensitive(printers, "imports")) == 0);
    cJSON *slot = cJSON_GetArrayItem(cJSON_GetObjectItemCaseSensitive(printers, "printers"), 0);
    assert(cJSON_GetObjectItemCaseSensitive(slot, "source")->valuedouble == RP_HOME_BAMBU_CLOUD);
    assert(flag(slot, "has_secret") && !flag(slot, "has_client_secret"));
    cJSON_Delete(printers);
    before = commits;
    assert(call("PUT", "/api/lan/printers", "slot=0", "{\"source\":2,\"name\":\"Lan\",\"serial\":\"01PTEST001\"}", false,
                out, sizeof(out)) == 400);
    assert(strstr(out, "unsupported_source") && commits == before && store.printers[0].source == RP_HOME_BAMBU_CLOUD);
    assert(!strcmp(store.printers[0].secret, "bambu-secret-value"));
    assert(call("PUT", "/api/lan/printers", "slot=0", "{\"name\":\"Nope\"}", false, out, sizeof(out)) == 400);
    assert(store.printers[0].source == RP_HOME_BAMBU_CLOUD);
    assert(call("PUT", "/api/lan/printers", "slot=0",
                "{\"source\":1,\"name\":\"HA\",\"serial\":\"sensor.printer\"}", false, out, sizeof(out)) == 200);
    assert(store.printers[0].source == RP_HOME_HA && !store.printers[0].secret[0] && store.enabled == 1);
    assert(!strcmp(store.printers[0].serial, "sensor.printer"));
    assert(call("PUT", "/api/lan/printers", "slot=0", "{\"source\":0}", false, out, sizeof(out)) == 200);
    assert(store.printers[0].source == RP_HOME_NONE && !store.printers[0].name[0]);
    assert(call("POST", "/api/lan/home", NULL, NULL, false, out, sizeof(out)) == 405);
    char big[RP_PORTAL_BODY_LIMIT + 8];
    memset(big, 'a', sizeof(big));
    big[sizeof(big) - 1] = 0;
    rp_home_web_request_t huge = {.method = "PUT",
                                  .path = "/api/lan/home",
                                  .body = big,
                                  .body_bytes = RP_PORTAL_BODY_LIMIT + 1};
    rp_home_web_response_t response = {0};
    assert(rp_portal_serve(&huge, &response, out, sizeof(out), false) && response.http_status == 413);

    portal_on = true;
    before = commits;
    assert(call("PUT", "/api/manage/home", NULL, "{\"enabled\":true}", true, out, sizeof(out)) == 409);
    assert(strstr(out, "home_portal_active") && commits == before);
    assert(call("DELETE", "/api/manage/printers", "slot=1", NULL, true, out, sizeof(out)) == 409);
    portal_on = false;
    assert(call("GET", "/api/manage/home", NULL, NULL, true, out, sizeof(out)) == 200);
    no_secrets(out);
    assert(strstr(out, "\"saved_wifi\"") && strstr(out, "\"portal\""));
    assert(call("DELETE", "/api/manage/printers", "slot=0", NULL, true, out, sizeof(out)) == 200);
    assert(store.printers[0].source == RP_HOME_NONE);

    commit_ok = false;
    before = commits;
    uint32_t revision = pub.revision;
    snprintf(store.local, sizeof(store.local), "http://ha.local:8123");
    assert(call("PUT", "/api/manage/home", NULL, "{\"local\":\"http://other.local\"}", true, out, sizeof(out)) == 507);
    assert(strstr(out, "save_failed") && commits == before && !strcmp(store.local, "http://ha.local:8123"));
    assert(pub.revision == revision && pub.phase == RP_PORTAL_SAVED);
    commit_ok = true;

    char mac[8192];
    size_t used = 0;
    const char *wifi[] = {"Home"};
    rp_home_web_request_t request = {.method = "GET",
                                     .path = "/api/manage/pico/home-link",
                                     .network_online = true,
                                     .current_wifi = "Home",
                                     .saved_wifi = wifi,
                                     .saved_wifi_count = 1};
    assert(rp_portal_write_mac(&request, &store, &snap, &pub, "pga", mac, sizeof(mac), &used) && used == strlen(mac));
    no_secrets(mac);
    home = parse(mac);
    assert(flag(home, "ok") && !strcmp(cJSON_GetObjectItemCaseSensitive(home, "home_link_mode")->valuestring, "pga"));
    assert(!strcmp(cJSON_GetObjectItemCaseSensitive(home, "writer")->valuestring, "device"));
    assert(cJSON_GetObjectItemCaseSensitive(home, "config") && cJSON_GetObjectItemCaseSensitive(home, "status"));
    assert(cJSON_GetObjectItemCaseSensitive(home, "printers") && !cJSON_GetObjectItemCaseSensitive(home, "saved_wifi"));
    assert(!cJSON_GetObjectItemCaseSensitive(home, "imports"));
    cJSON_Delete(home);

    rp_home_set_store_bridge(NULL);
    assert(!rp_home_store_bridge_ready());
    assert(!rp_portal_serve(&request, &response, out, sizeof(out), false));
    request.path = "/api/manage/home";
    request.method = "PUT";
    assert(rp_home_web_handle(&request, &response, out, sizeof(out)));
    assert(response.http_status == 503 && strstr(out, "home_module_missing"));
    install();
    request.path = "/api/manage/bambu/auth";
    request.method = "GET";
    assert(rp_home_web_handle(&request, &response, out, sizeof(out)));
    assert(response.http_status == 503 && strstr(out, "home_module_missing"));
    request.path = "/api/manage/home/extra";
    assert(rp_home_web_handle(&request, &response, out, sizeof(out)) && response.http_status == 503);

    size_t html_bytes = 0;
    const char *html = rp_portal_html(&html_bytes);
    assert(html && html_bytes == strlen(html));
    assert(strstr(html, "X-Pico-Portal-Key") && strstr(html, "/api/lan/home") && strstr(html, "ha_token"));
    assert(strstr(html, "cf_access_client_id") && (strstr(html, "酒店") || strstr(html, "隔离")));
    assert(strstr(html, "Bambu"));
    for (const char *p = html; *p; p++) {
        int n = 0;
        while (isxdigit((unsigned char)p[n])) n++;
        assert(n < 32);
        if (n) p += n - 1;
    }
    printf("home_link_portal: mask, preserve, clear, rooms, printer source, 409, mac readback passed\n");
    return 0;
}

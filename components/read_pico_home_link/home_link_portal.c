/*
 * SPDX-FileCopyrightText: 2026 XH Lab
 * SPDX-License-Identifier: Apache-2.0
 * 家庭/打印机填写的共享 JSON。手机门户与 Mac 管理页使用同一套字段名。
 * Shared HA/printer JSON. The phone portal and the Mac manage page use the same field names.
 */
#include "home_link_portal.h"
#include "home_link_json.h"
#include <ctype.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef ESP_PLATFORM
#include "esp_heap_caps.h"
#endif

static rp_home_store_bridge_t bridge;
static rp_home_discovery_bridge_t discovery_bridge;
void rp_home_set_discovery_bridge(rp_home_discovery_bridge_t cb) { discovery_bridge = cb; }

void rp_home_set_store_bridge(const rp_home_store_bridge_t *next) {
    if (!next || !next->load || !next->commit || !next->portal_active || !next->get_public || !next->set_public)
        memset(&bridge, 0, sizeof(bridge));
    else
        bridge = *next;
}
bool rp_home_store_bridge_ready(void) { return bridge.load && bridge.commit && bridge.get_public && bridge.set_public; }

const char *rp_portal_phase_name(uint8_t phase) {
    switch (phase) {
    case RP_PORTAL_OPEN: return "open";
    case RP_PORTAL_SAVED: return "saved";
    case RP_PORTAL_OFFLINE: return "offline";
    case RP_PORTAL_ERROR: return "error";
    default: return "idle";
    }
}

static void *portal_alloc(size_t bytes) {
#ifdef ESP_PLATFORM
    return heap_caps_calloc(1, bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
#else
    return calloc(1, bytes);
#endif
}
static void wipe(void *p, size_t n) {
    volatile unsigned char *v = p;
    while (n--) *v++ = 0;
}

typedef struct { char *out; size_t capacity, used; bool ok; } writer_t;
static void writef(writer_t *w, const char *format, ...) {
    if (!w->ok) return;
    va_list args;
    va_start(args, format);
    int n = vsnprintf(w->out + w->used, w->capacity - w->used, format, args);
    va_end(args);
    if (n < 0 || (size_t)n >= w->capacity - w->used) w->ok = false;
    else w->used += (size_t)n;
}
static void quoted(writer_t *w, const char *s) {
    writef(w, "\"");
    for (const unsigned char *p = (const unsigned char *)(s ? s : ""); *p && w->ok; p++) {
        if (*p == '"' || *p == '\\') writef(w, "\\%c", *p);
        else if (*p < 32) writef(w, "\\u%04x", *p);
        else writef(w, "%c", *p);
    }
    writef(w, "\"");
}
static const char *boolean(bool v) { return v ? "true" : "false"; }
static cJSON *item(const cJSON *j, const char *key) { return cJSON_GetObjectItemCaseSensitive(j, key); }
static void json_wipe(cJSON *j) {
    for (cJSON *p = j; p; p = p->next) {
        if (p->child) json_wipe(p->child);
        if (p->valuestring) wipe(p->valuestring, strlen(p->valuestring));
    }
}
static cJSON *body_json(const char *body, size_t n) {
    if (!body || !n || n > RP_PORTAL_BODY_LIMIT || memchr(body, 0, n)) return NULL;
    for (size_t i = 0; i + 6 <= n; i++)
        if (!memcmp(body + i, "\\u0000", 6)) return NULL;
    const char *end = NULL;
    cJSON *j = cJSON_ParseWithLengthOpts(body, n, &end, false);
    if (j)
        while (end < body + n && isspace((unsigned char)*end)) end++;
    if (!cJSON_IsObject(j) || end != body + n) {
        json_wipe(j);
        cJSON_Delete(j);
        return NULL;
    }
    return j;
}
static bool keys(const cJSON *j, const char *allowed) {
    for (const cJSON *p = j->child; p; p = p->next) {
        if (!p->string || !*p->string) return false;
        for (const cJSON *q = j->child; q != p; q = q->next)
            if (!strcmp(q->string, p->string)) return false;
        const char *at = allowed;
        bool found = false;
        size_t n = strlen(p->string);
        while (*at) {
            const char *space = strchr(at, ' ');
            size_t count = space ? (size_t)(space - at) : strlen(at);
            if (n == count && !memcmp(at, p->string, n)) found = true;
            if (!space) break;
            at = space + 1;
        }
        if (!found) return false;
    }
    return true;
}
static bool text(cJSON *j, const char *key, char *dest, size_t size, bool secret) {
    cJSON *v = item(j, key);
    if (!v) return true;
    if (!cJSON_IsString(v) || strlen(v->valuestring) >= size) return false;
    if (!secret || *v->valuestring) {
        memset(dest, 0, size);
        memcpy(dest, v->valuestring, strlen(v->valuestring));
    }
    return true;
}
static bool flag(cJSON *j, const char *key, uint8_t *dest) {
    cJSON *v = item(j, key);
    if (!v) return true;
    if (!cJSON_IsBool(v)) return false;
    *dest = cJSON_IsTrue(v);
    return true;
}
static bool clears(cJSON *j, const char *key, char *dest, size_t size) {
    cJSON *array = item(j, "clear");
    if (!array) return true;
    if (!cJSON_IsArray(array)) return false;
    static const char *allowed[] = {"ha_token", "cf_access_client_id", "cf_access_client_secret"};
    for (cJSON *v = array->child; v; v = v->next) {
        if (!cJSON_IsString(v)) return false;
        bool found = false;
        for (size_t i = 0; i < 3; i++)
            if (!strcmp(v->valuestring, allowed[i])) found = true;
        if (!found) return false;
        if (!strcmp(v->valuestring, key)) memset(dest, 0, size);
    }
    return true;
}
static bool saved_wifi(const rp_home_web_request_t *r, const char *ssid) {
    if (!ssid || !*ssid || !r->saved_wifi) return true;
    if (r->network_online && r->current_wifi && !strcmp(ssid, r->current_wifi)) return true;
    for (size_t i = 0; i < r->saved_wifi_count; i++)
        if (r->saved_wifi[i] && !strcmp(ssid, r->saved_wifi[i])) return true;
    return false;
}
static bool at_home(const rp_home_web_request_t *r, const rp_home_config_t *c) {
    return r->current_wifi ? r->network_online && *c->home_wifi && !strcmp(r->current_wifi, c->home_wifi) : r->at_home;
}
static int hex(unsigned char c) {
    if (c >= '0' && c <= '9') return c - '0';
    c = (unsigned char)tolower(c);
    return c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1;
}
static int query(const char *raw, const char *key, char *out, size_t size) {
    int found = 0;
    out[0] = 0;
    if (!raw) return 0;
    if (strlen(raw) > RP_HOME_WEB_QUERY_LIMIT) return -1;
    for (const char *p = raw; *p;) {
        const char *amp = strchr(p, '&');
        const char *end = amp ? amp : p + strlen(p);
        const char *equal = memchr(p, '=', (size_t)(end - p));
        if (equal && (size_t)(equal - p) == strlen(key) && !memcmp(p, key, strlen(key))) {
            if (found++) return -1;
            size_t n = 0;
            for (const char *v = equal + 1; v < end; v++) {
                unsigned char c = (unsigned char)*v;
                if (c == '%') {
                    if (end - v < 3 || hex((unsigned char)v[1]) < 0 || hex((unsigned char)v[2]) < 0) return -1;
                    c = (unsigned char)(hex((unsigned char)v[1]) * 16 + hex((unsigned char)v[2]));
                    v += 2;
                } else if (c == '+') c = ' ';
                if (!c || c < 32 || n + 1 >= size) return -1;
                out[n++] = (char)c;
            }
            out[n] = 0;
        }
        if (!amp) break;
        p = amp + 1;
    }
    return found;
}
static bool query_slot(const char *q, int *slot) {
    char b[8];
    int found = query(q, "slot", b, sizeof(b));
    if (found <= 0 || !b[0] || strspn(b, "0123456789") != strlen(b)) return false;
    int n = 0;
    for (const char *p = b; *p; p++) n = n * 10 + *p - '0';
    if (n >= RP_HOME_PRINTERS) return false;
    *slot = n;
    return true;
}

static void portal_json(writer_t *w, const rp_portal_public_t *p) {
    rp_portal_public_t empty = {0};
    if (!p) p = &empty;
    writef(w, "{\"active\":%s,\"phase\":\"%s\",\"revision\":%u,\"seconds_left\":%u,\"writer\":\"device\"}",
           boolean(p->active), rp_portal_phase_name(p->phase), (unsigned)p->revision, (unsigned)p->seconds_left);
}
static void status_json(writer_t *w, const rp_home_snapshot_t *s) {
    writef(w, "{\"result\":%d,\"http_status\":%d,\"revision\":%u,\"busy\":%s,\"external\":%s,\"sampled_ms\":%lld,\"entity_count\":%u,\"entity_total\":%u,\"truncated\":%s}",
           s->result, s->http_status, (unsigned)s->revision, boolean(s->busy), boolean(s->external),
           (long long)s->sampled_ms, (unsigned)s->entity_count, (unsigned)s->entity_total, boolean(s->truncated));
}
static void config_json(writer_t *w, const rp_home_config_t *c) {
    writef(w, "{\"enabled\":%s,\"local\":", boolean(c->enabled));
    quoted(w, c->local);
    writef(w, ",\"external\":");
    quoted(w, c->external);
    writef(w, ",\"home_wifi\":");
    quoted(w, c->home_wifi);
    writef(w,
           ",\"external_enabled\":%s,\"external_control\":%s,\"has_token\":%s,\"has_cf_access_client_id\":%s,"
           "\"has_cf_access_client_secret\":%s,\"rooms\":[",
           boolean(c->external_enabled), boolean(c->external_control), boolean(*c->ha_token),
           boolean(*c->cf_access_client_id), boolean(*c->cf_access_client_secret));
    for (unsigned i = 0; i < c->room_count && i < RP_HOME_ROOMS; i++) {
        if (i) writef(w, ",");
        writef(w, "{\"entity\":");
        quoted(w, c->rooms[i].entity);
        writef(w, ",\"room\":");
        quoted(w, c->rooms[i].room);
        writef(w, "}");
    }
    writef(w, "]}");
}
static void printer_json(writer_t *w, unsigned slot, const rp_home_printer_config_t *p, const rp_home_printer_t *v) {
    writef(w, "{\"slot\":%u,\"source\":%u,\"name\":", slot, p->source);
    quoted(w, p->name);
    writef(w, ",\"serial\":");
    quoted(w, p->serial);
    writef(w, ",\"host\":");
    quoted(w, p->host);
    writef(w,
           ",\"port\":%u,\"region\":%u,\"has_username\":%s,\"has_secret\":%s,\"has_client_id\":%s,"
           "\"has_client_secret\":%s,\"has_certificate\":%s,\"status\":{\"result\":%d,\"http_status\":%d,"
           "\"connected\":%s,\"has_data\":%s,\"percent\":%d,\"remaining_minutes\":%d,\"layer\":%d,\"layers\":%d,"
           "\"state\":",
           p->port, p->region, boolean(*p->username), boolean(*p->secret), boolean(*p->client_id),
           boolean(*p->client_secret), boolean(*p->certificate), v->result, v->http_status, boolean(v->connected),
           boolean(v->has_data), v->percent, v->remaining_minutes, v->layer, v->layers);
    quoted(w, v->state);
    writef(w, ",\"task\":");
    quoted(w, v->task);
    writef(w, ",\"material\":");
    quoted(w, v->material);
    writef(w, "}}");
}
static void finish_error(writer_t *w, int *status, const char *error) {
    if (!w->ok && !error) {
        *status = 507;
        error = "output_limit";
    }
    if (!error) return;
    w->used = 0;
    w->ok = true;
    writef(w, "{\"error\":\"%s\"}", error);
    if (!w->ok) {
        *status = 507;
        w->used = 0;
        w->ok = true;
        writef(w, "{\"error\":\"output_limit\"}");
    }
}

static bool apply_home(const rp_home_web_request_t *r, cJSON *j, rp_home_config_t *c) {
    if (!keys(j, "enabled local external home_wifi external_enabled external_control ha_token cf_access_client_id "
                 "cf_access_client_secret rooms clear") ||
        !flag(j, "enabled", &c->enabled) || !flag(j, "external_enabled", &c->external_enabled) ||
        !flag(j, "external_control", &c->external_control) || !text(j, "local", c->local, sizeof(c->local), false) ||
        !text(j, "external", c->external, sizeof(c->external), false) ||
        !text(j, "home_wifi", c->home_wifi, sizeof(c->home_wifi), false) ||
        !text(j, "ha_token", c->ha_token, sizeof(c->ha_token), true) ||
        !text(j, "cf_access_client_id", c->cf_access_client_id, sizeof(c->cf_access_client_id), true) ||
        !text(j, "cf_access_client_secret", c->cf_access_client_secret, sizeof(c->cf_access_client_secret), true) ||
        !clears(j, "ha_token", c->ha_token, sizeof(c->ha_token)) ||
        !clears(j, "cf_access_client_id", c->cf_access_client_id, sizeof(c->cf_access_client_id)) ||
        !clears(j, "cf_access_client_secret", c->cf_access_client_secret, sizeof(c->cf_access_client_secret)))
        return false;
    if (item(j, "home_wifi") && !saved_wifi(r, c->home_wifi)) return false;
    cJSON *rooms = item(j, "rooms");
    if (!rooms) return true;
    if (!cJSON_IsArray(rooms) || cJSON_GetArraySize(rooms) > RP_HOME_ROOMS) return false;
    c->room_count = 0;
    memset(c->rooms, 0, sizeof(c->rooms));
    for (cJSON *v = rooms->child; v; v = v->next) {
        rp_home_room_t *p = &c->rooms[c->room_count++];
        if (!cJSON_IsObject(v) || !keys(v, "entity room") || !item(v, "entity") || !item(v, "room") ||
            !text(v, "entity", p->entity, sizeof(p->entity), false) || !text(v, "room", p->room, sizeof(p->room), false))
            return false;
        for (unsigned i = 0; i + 1 < c->room_count; i++)
            if (!strcmp(c->rooms[i].entity, p->entity)) return false;
    }
    return true;
}
static const char *apply_printer(cJSON *j, rp_home_printer_config_t *p) {
    cJSON *source_item = item(j, "source");
    int source = p->source;
    if (source_item) {
        if (!cJSON_IsNumber(source_item) || !isfinite(source_item->valuedouble) || source_item->valuedouble < 0 ||
            source_item->valuedouble > RP_HOME_FORMLABS || (int)source_item->valuedouble != source_item->valuedouble)
            return "invalid_configuration";
        source = (int)source_item->valuedouble;
        if (source > RP_HOME_HA) return "unsupported_source";
    } else if (p->source > RP_HOME_HA) {
        return "unsupported_source";
    }
    if (source == RP_HOME_NONE) {
        if (!keys(j, "source")) return "invalid_configuration";
        memset(p, 0, sizeof(*p));
        return NULL;
    }
    if (!keys(j, "source name serial") || !text(j, "name", p->name, sizeof(p->name), false) ||
        !text(j, "serial", p->serial, sizeof(p->serial), false))
        return "invalid_configuration";
    if (source != p->source) {
        char name[sizeof(p->name)];
        memcpy(name, p->name, sizeof(name));
        memset(p, 0, sizeof(*p));
        memcpy(p->name, name, sizeof(name));
        if (item(j, "name")) text(j, "name", p->name, sizeof(p->name), false);
        if (item(j, "serial")) text(j, "serial", p->serial, sizeof(p->serial), false);
        p->source = (uint8_t)source;
    }
    return NULL;
}

static void write_home(writer_t *w, const rp_home_web_request_t *r, const rp_home_config_t *c, const rp_home_snapshot_t *s,
                       const rp_portal_public_t *portal) {
    writef(w, "{\"config\":");
    config_json(w, c);
    writef(w, ",\"status\":");
    status_json(w, s);
    writef(w, ",\"current_wifi\":");
    quoted(w, r->network_online && r->current_wifi ? r->current_wifi : "");
    writef(w, ",\"network_online\":%s,\"at_home\":%s,\"saved_wifi\":[", boolean(r->network_online), boolean(at_home(r, c)));
    size_t count = r->saved_wifi ? r->saved_wifi_count : 0;
    for (size_t i = 0; i < count && i < RP_HOME_WEB_WIFI_LIMIT; i++) {
        if (i) writef(w, ",");
        quoted(w, r->saved_wifi[i] ? r->saved_wifi[i] : "");
    }
    writef(w, "],\"wifi_truncated\":%s,\"portal\":", boolean(count > RP_HOME_WEB_WIFI_LIMIT));
    portal_json(w, portal);
    writef(w, "}");
}
static void write_printers(writer_t *w, const rp_home_config_t *c, const rp_home_snapshot_t *s,
                           const rp_portal_public_t *portal) {
    writef(w, "{\"printers\":[");
    for (unsigned i = 0; i < RP_HOME_PRINTERS; i++) {
        if (i) writef(w, ",");
        printer_json(w, i, &c->printers[i], &s->printers[i]);
    }
    writef(w, "],\"imports\":[],\"portal\":");
    portal_json(w, portal);
    writef(w, "}");
}

bool rp_portal_write_mac(const rp_home_web_request_t *request, const rp_home_config_t *config,
                         const rp_home_snapshot_t *snapshot, const rp_portal_public_t *portal, const char *mode,
                         char *output, size_t capacity, size_t *used) {
    if (!request || !config || !snapshot || !output || capacity < 64) return false;
    writer_t w = {output, capacity, 0, true};
    writef(&w, "{\"ok\":true,\"home_link_mode\":");
    quoted(&w, mode && *mode ? mode : "pga");
    writef(&w, ",\"writer\":\"device\",\"portal\":");
    portal_json(&w, portal);
    writef(&w, ",\"config\":");
    config_json(&w, config);
    writef(&w, ",\"status\":");
    status_json(&w, snapshot);
    writef(&w, ",\"network_online\":%s,\"at_home\":%s,\"printers\":[", boolean(request->network_online),
           boolean(at_home(request, config)));
    for (unsigned i = 0; i < RP_HOME_PRINTERS; i++) {
        if (i) writef(&w, ",");
        printer_json(&w, i, &config->printers[i], &snapshot->printers[i]);
    }
    writef(&w, "]}");
    if (used) *used = w.ok ? w.used : 0;
    return w.ok;
}

bool rp_portal_serve(const rp_home_web_request_t *request, rp_home_web_response_t *response, char *output,
                     size_t capacity, bool manage_route) {
    if (!request || !request->path || !request->method || !response || !output || capacity < 32 ||
        !rp_home_store_bridge_ready())
        return false;
    bool home = !strcmp(request->path, manage_route ? "/api/manage/home" : "/api/lan/home");
    bool printers = !strcmp(request->path, manage_route ? "/api/manage/printers" : "/api/lan/printers");
    bool discovery = !strcmp(request->path, manage_route ? "/api/manage/home/discover" : "/api/lan/home/discover");
    if (!home && !printers && !discovery) return false;
    writer_t w = {output, capacity > 8192 ? 8192 : capacity, 0, true};
    int status = 200;
    const char *error = NULL;
    bool get = !strcmp(request->method, "GET");
    bool put = !strcmp(request->method, "PUT");
    bool del = !strcmp(request->method, "DELETE");
    rp_home_config_t *cfg = NULL;
    rp_home_snapshot_t *snap = NULL;
    cJSON *j = NULL;
    rp_portal_public_t portal = {0};
    bridge.get_public(&portal);
    if (request->body_bytes > RP_PORTAL_BODY_LIMIT) {
        status = 413;
        error = "too_large";
        goto done;
    }
    if (request->query && strlen(request->query) > RP_HOME_WEB_QUERY_LIMIT) {
        status = 400;
        error = "invalid_query";
        goto done;
    }
    if (discovery) {
        bool start = !strcmp(request->method, "POST"), busy = false, failed = false;
        if ((!get && !start) || request->body_bytes || (request->query && *request->query)) {
            status = !get && !start ? 405 : 400; error = "method"; goto done;
        }
        if (!request->network_online) { status = 409; error = "offline"; goto done; }
        rp_home_discovery_t *found = portal_alloc(sizeof(*found));
        if (!found || !discovery_bridge || !discovery_bridge(start, found, &busy, &failed)) {
            free(found); status = 503; error = "discovery_unavailable"; goto done;
        }
        writef(&w, "{\"busy\":%s,\"failed\":%s,\"truncated\":%s,\"servers\":[", boolean(busy), boolean(failed), boolean(found->truncated));
        for (unsigned i = 0; i < found->count && i < RP_HOME_DISCOVERY_MAX; i++) {
            if (i) writef(&w, ",");
            writef(&w, "{\"name\":"); quoted(&w, found->items[i].name);
            writef(&w, ",\"url\":"); quoted(&w, found->items[i].url); writef(&w, "}");
        }
        writef(&w, "]}"); free(found); goto done;
    }
    if (!((home && (get || put)) || (printers && (get || put || del)))) {
        status = 405;
        error = "method";
        goto done;
    }
    if (manage_route && !get && bridge.portal_active && bridge.portal_active()) {
        status = 409;
        error = "home_portal_active";
        goto done;
    }
    cfg = portal_alloc(sizeof(*cfg));
    snap = portal_alloc(sizeof(*snap));
    if (!cfg || !snap || !bridge.load(cfg, snap)) {
        status = 503;
        error = "memory";
        goto done;
    }
    if (get) {
        if (home) write_home(&w, request, cfg, snap, &portal);
        else write_printers(&w, cfg, snap, &portal);
        goto done;
    }
    if (put) {
        j = body_json(request->body, request->body_bytes);
        if (!j) {
            status = 400;
            error = "bad_json";
            goto done;
        }
    }
    if (home) {
        if (!apply_home(request, j, cfg) || !rp_home_config_valid(cfg)) {
            status = 400;
            error = "invalid_configuration";
            goto done;
        }
    } else {
        int slot = -1;
        if (!query_slot(request->query, &slot)) {
            status = 400;
            error = "invalid_query";
            goto done;
        }
        if (del) memset(&cfg->printers[slot], 0, sizeof(cfg->printers[slot]));
        else {
            const char *printer_error = apply_printer(j, &cfg->printers[slot]);
            if (printer_error) {
                status = 400;
                error = printer_error;
                goto done;
            }
            if (cfg->printers[slot].source) cfg->enabled = 1;
        }
        if (!rp_home_config_valid(cfg)) {
            status = 400;
            error = "invalid_configuration";
            goto done;
        }
    }
    bool queued = false;
    if (!bridge.commit(cfg, &queued)) {
        status = 507;
        error = "save_failed";
        goto done;
    }
    portal.phase = RP_PORTAL_SAVED;
    portal.revision++;
    bridge.set_public(&portal);
    bridge.get_public(&portal);
    writef(&w, "{\"ok\":true,\"queued\":%s,\"portal\":", boolean(queued));
    portal_json(&w, &portal);
    writef(&w, "}");
done:
    if (j) {
        json_wipe(j);
        cJSON_Delete(j);
    }
    if (cfg) {
        wipe(cfg, sizeof(*cfg));
        free(cfg);
    }
    if (snap) {
        wipe(snap, sizeof(*snap));
        free(snap);
    }
    finish_error(&w, &status, error);
    response->http_status = status;
    response->body_bytes = w.ok ? w.used : 0;
    return true;
}

#include "home_link_portal_html_gzip.inc"

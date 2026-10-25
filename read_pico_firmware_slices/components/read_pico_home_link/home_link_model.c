/*
 * SPDX-FileCopyrightText: 2026 XH Lab
 * SPDX-License-Identifier: Apache-2.0
 * 真实HA、Formlabs OAuth与Bambu报告的共享业务模型；不包含界面或硬件。
 * Shared real HA, Formlabs OAuth and Bambu report logic without UI or hardware.
 * 冻结：解析失败保留上次有效数据；只允许六种标准实体；打印机只有只读入口。
 * Frozen: Parse failures retain the last valid data; only six standard entity kinds and read-only printer APIs.
 */
#include "home_link_json.h"
#include "read_pico_home_link.h"
#include <ctype.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#ifdef ESP_PLATFORM
#include "esp_heap_caps.h"
#endif

static void *model_memory(size_t n) {
#ifdef ESP_PLATFORM
    return heap_caps_calloc(1, n, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
#else
    return calloc(1, n);
#endif
}
static uint32_t config_crc(const unsigned char *p, size_t bytes) {
    uint32_t crc = ~0u;
    for (size_t i = 0; i < bytes; i++) {
        crc ^= p[i];
        for (unsigned b = 0; b < 8; b++)
            crc = (crc >> 1) ^ ((0u - (crc & 1u)) & 0xedb88320u);
    }
    return ~crc;
}
size_t rp_home_config_pack(const rp_home_config_t *c, void *output, size_t capacity) {
    if (!c || !output || capacity < 12 || !rp_home_config_valid(c))
        return 0;
    unsigned char *out = output;
    const unsigned char *raw = (const unsigned char *)c;
    uint32_t header[] = {0x48504b31u, (uint32_t)sizeof(*c), config_crc(raw, sizeof(*c))};
    memcpy(out, header, 12);
    size_t used = 12, at = 0;
    while (at < sizeof(*c)) {
        size_t run = 0;
        while (at + run < sizeof(*c) && !raw[at + run] && run < 128)
            run++;
        if (run >= 3) {
            if (used == capacity)
                return 0;
            out[used++] = (unsigned char)(0x80u | (run - 1));
            at += run;
            continue;
        }
        size_t start = at;
        do {
            at++;
            if (at == sizeof(*c) || at - start == 128)
                break;
            run = 0;
            while (at + run < sizeof(*c) && !raw[at + run] && run < 3)
                run++;
        } while (run < 3);
        size_t n = at - start;
        if (used + n + 1 > capacity)
            return 0;
        out[used++] = (unsigned char)(n - 1);
        memcpy(out + used, raw + start, n);
        used += n;
    }
    return used;
}
bool rp_home_config_unpack_version(const void *input, size_t bytes, rp_home_config_t *c, uint32_t *stored_version) {
    if (!input || !c || bytes < 12 || bytes > RP_HOME_CONFIG_PACK_LIMIT)
        return false;
    const unsigned char *in = input;
    uint32_t header[3];
    memcpy(header, in, 12);
    uint32_t stored_size = header[1];
    if (header[0] != 0x48504b31u ||
        (stored_size != sizeof(*c) && stored_size != RP_HOME_CONFIG_SIZE_V2))
        return false;
    unsigned char temp[sizeof(rp_home_config_t)];
    memset(temp, 0, sizeof(temp));
    size_t at = 12, used = 0;
    while (at < bytes) {
        unsigned control = in[at++];
        size_t n = (control & 127u) + 1;
        if (used + n > stored_size)
            goto bad;
        if (!(control & 128u)) {
            if (at + n > bytes)
                goto bad;
            memcpy(temp + used, in + at, n);
            at += n;
        }
        used += n;
    }
    if (used != stored_size || config_crc(temp, stored_size) != header[2])
        goto bad;
    memset(c, 0, sizeof(*c));
    memcpy(c, temp, stored_size <= sizeof(*c) ? stored_size : sizeof(*c));
    if (stored_size == RP_HOME_CONFIG_SIZE_V2 && stored_size < sizeof(*c)) {
        /* v2 layout ended at ha_token; CF fields and trailing rooms/printers follow at new offsets.
         * Copy rooms/printers from old offsets: after ha_token at 192+192+48+1024 + 4+4 = ...
         * Safer path: rebuild from known v2 field offsets. */
        typedef struct {
            uint32_t version;
            uint8_t enabled, external_enabled, external_control, room_count;
            char local[192], external[192], home_wifi[48], ha_token[1024];
            rp_home_room_t rooms[RP_HOME_ROOMS];
            rp_home_printer_config_t printers[RP_HOME_PRINTERS];
        } rp_home_config_v2_t;
        _Static_assert(sizeof(rp_home_config_v2_t) == RP_HOME_CONFIG_SIZE_V2, "v2 config size");
        const rp_home_config_v2_t *old = (const rp_home_config_v2_t *)temp;
        memset(c, 0, sizeof(*c));
        c->version = old->version;
        c->enabled = old->enabled;
        c->external_enabled = old->external_enabled;
        c->external_control = old->external_control;
        c->room_count = old->room_count;
        memcpy(c->local, old->local, sizeof(c->local));
        memcpy(c->external, old->external, sizeof(c->external));
        memcpy(c->home_wifi, old->home_wifi, sizeof(c->home_wifi));
        memcpy(c->ha_token, old->ha_token, sizeof(c->ha_token));
        memcpy(c->rooms, old->rooms, sizeof(c->rooms));
        memcpy(c->printers, old->printers, sizeof(c->printers));
    }
    {
        uint32_t previous = c->version;
        if (rp_home_config_upgrade(c)) {
            if (stored_version) *stored_version = previous;
            return true;
        }
    }
bad:
    memset(c, 0, sizeof(*c));
    return false;
}
bool rp_home_config_unpack(const void *input, size_t bytes, rp_home_config_t *c) {
    return rp_home_config_unpack_version(input, bytes, c, NULL);
}

static cJSON *get(const cJSON *j, const char *key) { return cJSON_GetObjectItemCaseSensitive(j, key); }
static const char *str(const cJSON *j, const char *key) {
    cJSON *v = get(j, key);
    return cJSON_IsString(v) && v->valuestring ? v->valuestring : "";
}
static const char *first(const cJSON *j, const char *a, const char *b) {
    const char *s = str(j, a);
    return *s ? s : str(j, b);
}
static void copy(char *out, size_t capacity, const char *s) {
    if (capacity)
        snprintf(out, capacity, "%s", s ? s : "");
}
static double number(const cJSON *j, const char *key) {
    cJSON *v = get(j, key);
    if (cJSON_IsNumber(v))
        return isfinite(v->valuedouble) ? v->valuedouble : NAN;
    if (cJSON_IsString(v)) {
        char *end;
        double n = strtod(v->valuestring, &end);
        if (end != v->valuestring && !*end && isfinite(n))
            return n;
    }
    return NAN;
}
static int integer(double n, int low, int high) { return isfinite(n) && n >= low && n <= high ? (int)n : -1; }
static bool atom(const char *s) {
    return s && *s && strspn(s, "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_.-") == strlen(s);
}
static bool header(const char *s) {
    if (!s)
        return false;
    for (; *s; s++)
        if ((unsigned char)*s < 32 || (unsigned char)*s == 127)
            return false;
    return true;
}
static bool terminated(const char *s, size_t n) { return memchr(s, 0, n) != NULL; }
static bool empty_bytes(const void *value, size_t bytes) {
    const unsigned char *p = value;
    for (size_t i = 0; i < bytes; i++) if (p[i]) return false;
    return true;
}
static bool url(const char *s, bool tls) {
    if (!s || !header(s))
        return false;
    const char *p = !strncmp(s, "https://", 8) ? s + 8 : !tls && !strncmp(s, "http://", 7) ? s + 7 : NULL;
    if (!p || !*p || strchr(p, '@') || strchr(p, '?') || strchr(p, '#') || strchr(p, ' '))
        return false;
    const char *slash = strchr(p, '/');
    size_t n = slash ? (size_t)(slash - p) : strlen(p);
    return n && strspn(p, "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789.-:[]") >= n;
}
bool rp_home_config_valid(const rp_home_config_t *c) {
    if (!c || c->version != RP_HOME_CONFIG_VERSION || c->room_count > RP_HOME_ROOMS || c->enabled > 1 ||
        c->external_enabled > 1 || c->external_control > 1)
        return false;
    if (!terminated(c->local, sizeof(c->local)) || !terminated(c->external, sizeof(c->external)) ||
        !terminated(c->home_wifi, sizeof(c->home_wifi)) || !terminated(c->ha_token, sizeof(c->ha_token)) ||
        !terminated(c->cf_access_client_id, sizeof(c->cf_access_client_id)) ||
        !terminated(c->cf_access_client_secret, sizeof(c->cf_access_client_secret)))
        return false;
    if ((*c->local && !url(c->local, false)) || (*c->external && !url(c->external, true)) || !header(c->ha_token) ||
        !header(c->cf_access_client_id) || !header(c->cf_access_client_secret))
        return false;
    if ((*c->cf_access_client_id != 0) != (*c->cf_access_client_secret != 0))
        return false;
    for (unsigned i = 0; i < c->room_count; i++)
        if (!terminated(c->rooms[i].entity, sizeof(c->rooms[i].entity)) || !atom(c->rooms[i].entity) ||
            !terminated(c->rooms[i].room, sizeof(c->rooms[i].room)))
            return false;
    for (unsigned i = 0; i < RP_HOME_PRINTERS; i++) {
        const rp_home_printer_config_t *p = &c->printers[i];
        if (p->source > RP_HOME_FORMLABS || p->region > 1 || !terminated(p->name, sizeof(p->name)) ||
            !terminated(p->serial, sizeof(p->serial)) || !terminated(p->host, sizeof(p->host)) ||
            !terminated(p->username, sizeof(p->username)) || !terminated(p->secret, sizeof(p->secret)) ||
            !terminated(p->client_id, sizeof(p->client_id)) ||
            !terminated(p->client_secret, sizeof(p->client_secret)) ||
            !terminated(p->certificate, sizeof(p->certificate)))
            return false;
        if (p->source && !atom(p->serial))
            return false;
        const bool mqtt = p->source == RP_HOME_BAMBU_LAN || p->source == RP_HOME_BAMBU_CLOUD;
        if ((!mqtt && (!empty_bytes(p->username, sizeof(p->username)) || !empty_bytes(p->secret, sizeof(p->secret)))) ||
            (p->source != RP_HOME_FORMLABS && (!empty_bytes(p->client_id, sizeof(p->client_id)) || !empty_bytes(p->client_secret, sizeof(p->client_secret)))) ||
            (p->source != RP_HOME_BAMBU_LAN && (!empty_bytes(p->host, sizeof(p->host)) || !empty_bytes(p->certificate, sizeof(p->certificate)) || p->port)) ||
            (p->source != RP_HOME_BAMBU_CLOUD && p->region))
            return false;
        if (p->source == RP_HOME_NONE && (!empty_bytes(p->name, sizeof(p->name)) || !empty_bytes(p->serial, sizeof(p->serial))))
            return false;
        if ((p->source == RP_HOME_BAMBU_LAN || p->source == RP_HOME_BAMBU_CLOUD) &&
            (!header(p->username) || !header(p->secret)))
            return false;
        if (p->source == RP_HOME_BAMBU_LAN &&
            (!atom(p->host) || !*p->secret || !*p->certificate || !empty_bytes(p->username, sizeof(p->username))))
            return false;
        if (p->source == RP_HOME_BAMBU_CLOUD && (!atom(p->username) || strncmp(p->username, "u_", 2) || !*p->secret))
            return false;
        if (p->source == RP_HOME_FORMLABS &&
            (!*p->client_id || !*p->client_secret || !header(p->client_id) || !header(p->client_secret)))
            return false;
    }
    return true;
}
bool rp_home_config_upgrade(rp_home_config_t *c) {
    if (!c || (c->version != 1u && c->version != 2u && c->version != RP_HOME_CONFIG_VERSION))
        return false;
    if (c->version == 1u) {
        for (unsigned i = 0; i < RP_HOME_PRINTERS; i++) {
            rp_home_printer_config_t *p = &c->printers[i];
            if (p->source == RP_HOME_NONE) {
                memset(p, 0, sizeof(*p));
                continue;
            }
            if (p->source != RP_HOME_BAMBU_CLOUD) p->region = 0;
            if (p->source != RP_HOME_BAMBU_LAN) {
                p->port = 0;
                memset(p->host, 0, sizeof(p->host));
                memset(p->certificate, 0, sizeof(p->certificate));
            }
            if (p->source != RP_HOME_BAMBU_CLOUD) memset(p->username, 0, sizeof(p->username));
            if (p->source != RP_HOME_BAMBU_LAN && p->source != RP_HOME_BAMBU_CLOUD)
                memset(p->secret, 0, sizeof(p->secret));
            if (p->source != RP_HOME_FORMLABS) {
                memset(p->client_id, 0, sizeof(p->client_id));
                memset(p->client_secret, 0, sizeof(p->client_secret));
            }
        }
        c->version = RP_HOME_CONFIG_VERSION;
    }
    if (c->version == 2u) {
        memset(c->cf_access_client_id, 0, sizeof(c->cf_access_client_id));
        memset(c->cf_access_client_secret, 0, sizeof(c->cf_access_client_secret));
        c->version = RP_HOME_CONFIG_VERSION;
    }
    return rp_home_config_valid(c);
}
static void copy_field(char *dst, size_t n, const char *src) {
    if (!dst || !n)
        return;
    if (!src)
        src = "";
    snprintf(dst, n, "%s", src);
}
bool rp_home_apply_link_update(rp_home_config_t *dst, const rp_home_link_update_t *in) {
    if (!dst || !in || in->enabled > 1 || in->external_enabled > 1 || in->external_control > 1 || in->clear_token > 1 ||
        in->clear_cf > 1 || in->has_printers > 1)
        return false;
    rp_home_config_t *next = model_memory(sizeof(*next));
    if (!next)
        return false;
    *next = *dst;
    next->version = RP_HOME_CONFIG_VERSION;
    next->enabled = in->enabled;
    next->external_enabled = in->external_enabled;
    next->external_control = in->external_control;
    if (in->local)
        copy_field(next->local, sizeof(next->local), in->local);
    if (in->external)
        copy_field(next->external, sizeof(next->external), in->external);
    if (in->home_wifi)
        copy_field(next->home_wifi, sizeof(next->home_wifi), in->home_wifi);
    if (in->clear_token)
        memset(next->ha_token, 0, sizeof(next->ha_token));
    else if (in->ha_token && *in->ha_token)
        copy_field(next->ha_token, sizeof(next->ha_token), in->ha_token);
    if (in->clear_cf) {
        memset(next->cf_access_client_id, 0, sizeof(next->cf_access_client_id));
        memset(next->cf_access_client_secret, 0, sizeof(next->cf_access_client_secret));
    } else {
        if (in->cf_id && *in->cf_id)
            copy_field(next->cf_access_client_id, sizeof(next->cf_access_client_id), in->cf_id);
        if (in->cf_secret && *in->cf_secret)
            copy_field(next->cf_access_client_secret, sizeof(next->cf_access_client_secret), in->cf_secret);
    }
    if (in->has_printers) {
        for (unsigned i = 0; i < RP_HOME_PRINTERS; i++) {
            uint8_t source = in->printer_source[i];
            if (source != RP_HOME_NONE && source != RP_HOME_HA) {
                memset(next, 0, sizeof(*next));
                free(next);
                return false;
            }
            memset(&next->printers[i], 0, sizeof(next->printers[i]));
            if (source == RP_HOME_NONE)
                continue;
            next->printers[i].source = RP_HOME_HA;
            copy_field(next->printers[i].name, sizeof(next->printers[i].name), in->printer_name[i]);
            copy_field(next->printers[i].serial, sizeof(next->printers[i].serial), in->printer_serial[i]);
        }
    }
    bool ok = rp_home_config_valid(next);
    if (ok)
        *dst = *next;
    memset(next, 0, sizeof(*next));
    free(next);
    return ok;
}
static cJSON *parse(const char *json, size_t bytes) {
    if (!json || !bytes || bytes > RP_HOME_HTTP_LIMIT || memchr(json, 0, bytes))
        return NULL;
    const char *end = NULL;
    cJSON *j = cJSON_ParseWithLengthOpts(json, bytes, &end, false);
    if (j) {
        while (end < json + bytes && isspace((unsigned char)*end))
            end++;
        if (end != json + bytes) {
            cJSON_Delete(j);
            j = NULL;
        }
    }
    return j;
}
static int kind(const char *id, const cJSON *attrs) {
    if (!strncmp(id, "light.", 6))
        return RP_HOME_LIGHT;
    if (!strncmp(id, "switch.", 7))
        return RP_HOME_SWITCH;
    if (!strncmp(id, "climate.", 8))
        return RP_HOME_CLIMATE;
    if (!strncmp(id, "fan.", 4))
        return RP_HOME_FAN;
    if (!strncmp(id, "sensor.", 7)) {
        const char *s = str(attrs, "device_class");
        if (!strcmp(s, "temperature"))
            return RP_HOME_TEMPERATURE;
        if (!strcmp(s, "humidity"))
            return RP_HOME_HUMIDITY;
    }
    return -1;
}
static void printer_clear(rp_home_printer_t *p) {
    memset(p, 0, sizeof(*p));
    p->percent = p->remaining_minutes = p->layer = p->layers = p->nozzle = p->bed = -1;
}
static bool suffix(const char *s, const char *end) {
    size_t a = strlen(s), b = strlen(end);
    return a > b && !strcmp(s + a - b, end);
}
static const char *room_for(const rp_home_config_t *c, const char *id, const cJSON *attrs) {
    for (unsigned i = 0; i < c->room_count; i++)
        if (!strcmp(c->rooms[i].entity, id))
            return c->rooms[i].room;
    const char *r = first(attrs, "area_name", "room");
    return *r ? r : "未分组";
}
static void import_candidate(const cJSON *e, rp_home_snapshot_t *s) {
    const char *id = str(e, "entity_id");
    const char *tail = suffix(id, "_print_status")     ? "_print_status"
                       : suffix(id, "_printing_state") ? "_printing_state"
                                                       : NULL;
    if (!tail)
        return;
    size_t len = strlen(id) - strlen(tail);
    if (len >= sizeof(s->imports[0].serial) || !len)
        return;
    for (unsigned i = 0; i < s->import_count; i++)
        if (strlen(s->imports[i].serial) == len && !strncmp(s->imports[i].serial, id, len))
            return;
    if (s->import_count >= RP_HOME_IMPORTS) {
        s->truncated = 1;
        return;
    }
    rp_home_printer_t *p = &s->imports[s->import_count++];
    printer_clear(p);
    memcpy(p->serial, id, len);
    p->serial[len] = 0;
    copy(p->name, sizeof(p->name), str(get(e, "attributes"), "friendly_name"));
    if (!*p->name)
        copy(p->name, sizeof(p->name), p->serial);
    p->source = RP_HOME_HA;
}
static void ha_printer_value(rp_home_printer_t *p, const cJSON *e) {
    const char *id = str(e, "entity_id"), *s = str(e, "state");
    double n = number(e, "state");
    if (suffix(id, "_print_status") || suffix(id, "_printing_state")) {
        copy(p->state, sizeof(p->state), s);
        p->connected = strcmp(s, "unavailable") && strcmp(s, "unknown");
        p->has_data = 1;
    } else if (suffix(id, "_print_progress") || suffix(id, "_progress"))
        p->percent = integer(n, 0, 100);
    else if (suffix(id, "_remaining_time"))
        p->remaining_minutes = integer(n, 0, 525600);
    else if (suffix(id, "_current_layer"))
        p->layer = integer(n, 0, INT_MAX);
    else if (suffix(id, "_total_layer_count") || suffix(id, "_total_layers"))
        p->layers = integer(n, 0, INT_MAX);
    else if (suffix(id, "_nozzle_temperature"))
        p->nozzle = integer(n, -100, 1000);
    else if (suffix(id, "_bed_temperature"))
        p->bed = integer(n, -100, 1000);
    else if (suffix(id, "_task_name") || suffix(id, "_subtask_name"))
        copy(p->task, sizeof(p->task), s);
}
// 逐实体解析，避免大型HA响应建立整棵对象树；提交前仍验证整个数组。
// Parse one entity at a time instead of building a large HA object tree; validate the full array before committing.
static const char *item_end(const char *p, const char *end) {
    char stack[32];
    unsigned depth = 0;
    bool quoted = false, escape = false;
    for (; p < end; p++) {
        char c = *p;
        if (quoted) {
            if (escape) {
                escape = false;
                continue;
            }
            if (c == '\\') {
                escape = true;
                continue;
            }
            if (c == '"')
                quoted = false;
            continue;
        }
        if (c == '"') {
            quoted = true;
            continue;
        }
        if (c == '{' || c == '[') {
            if (depth == sizeof(stack))
                return NULL;
            stack[depth++] = c;
        } else if (c == '}' || c == ']') {
            if (!depth || stack[depth - 1] != (c == '}' ? '{' : '['))
                return NULL;
            if (!--depth)
                return p + 1;
        }
    }
    return NULL;
}
static const char *space(const char *p, const char *end) {
    while (p < end && isspace((unsigned char)*p))
        p++;
    return p;
}
rp_home_result_t rp_home_parse_ha(const char *json, size_t bytes, const rp_home_config_t *c, rp_home_snapshot_t *s,
                                  int64_t now) {
    if (!json || !bytes || bytes > RP_HOME_HTTP_LIMIT || memchr(json, 0, bytes))
        return bytes > RP_HOME_HTTP_LIMIT ? RP_HOME_TOO_LARGE : RP_HOME_INVALID;
    const char *end = json + bytes, *p = space(json, end);
    if (p == end || *p++ != '[')
        return RP_HOME_INVALID;
    rp_home_snapshot_t *next = model_memory(sizeof(*next));
    if (!next)
        return RP_HOME_NOMEM;
    *next = *s;
    unsigned count = 0, total = 0;
    next->import_count = 0;
    next->truncated = 0;
    rp_home_result_t result = RP_HOME_INVALID;
    for (unsigned i = 0; i < RP_HOME_PRINTERS; i++)
        if (c->printers[i].source == RP_HOME_HA) {
            printer_clear(&next->printers[i]);
            next->printers[i].source = RP_HOME_HA;
            copy(next->printers[i].name, sizeof(next->printers[i].name), c->printers[i].name);
            copy(next->printers[i].serial, sizeof(next->printers[i].serial), c->printers[i].serial);
        }
    p = space(p, end);
    if (p < end && *p == ']') {
        p++;
        goto complete;
    }
    for (;;) {
        if (p == end || *p != '{')
            goto done;
        const char *finish = item_end(p, end);
        if (!finish)
            goto done;
        size_t item_bytes = (size_t)(finish - p);
        if (item_bytes > 16384) {
            result = RP_HOME_TOO_LARGE;
            goto done;
        }
        cJSON *e = parse(p, item_bytes);
        if (!cJSON_IsObject(e)) {
            cJSON_Delete(e);
            goto done;
        }
        const char *id = str(e, "entity_id"), *state_ = str(e, "state");
        cJSON *attrs = get(e, "attributes");
        if (atom(id) && strlen(id) < sizeof(next->entities[0].id) && *state_) {
            import_candidate(e, next);
            for (unsigned i = 0; i < RP_HOME_PRINTERS; i++) {
                const char *prefix = c->printers[i].serial;
                size_t len = strlen(prefix);
                if (c->printers[i].source == RP_HOME_HA && len && !strncmp(id, prefix, len) && id[len] == '_')
                    ha_printer_value(&next->printers[i], e);
            }
            int k = kind(id, attrs);
            if (k >= 0) {
                total++;
                if (count < RP_HOME_ENTITIES) {
                    rp_home_entity_t *v = &next->entities[count++];
                    memset(v, 0, sizeof(*v));
                    copy(v->id, sizeof(v->id), id);
                    copy(v->name, sizeof(v->name), str(attrs, "friendly_name"));
                    if (!*v->name)
                        copy(v->name, sizeof(v->name), id);
                    copy(v->state, sizeof(v->state), state_);
                    copy(v->unit, sizeof(v->unit), str(attrs, "unit_of_measurement"));
                    copy(v->room, sizeof(v->room), room_for(c, id, attrs));
                    v->kind = (uint8_t)k;
                    v->available = strcmp(state_, "unavailable") && strcmp(state_, "unknown");
                    v->on = !strcmp(state_, "on") || (k == RP_HOME_CLIMATE && strcmp(state_, "off") && v->available);
                    v->number = (float)number(e, "state");
                    if (k == RP_HOME_CLIMATE)
                        v->number = (float)number(attrs, "temperature");
                }
            }
        }
        cJSON_Delete(e);
        p = space(finish, end);
        if (p < end && *p == ']') {
            p++;
            break;
        }
        if (p == end || *p++ != ',')
            goto done;
        p = space(p, end);
    }
complete:
    if (space(p, end) != end)
        goto done;
    next->entity_count = (uint16_t)count;
    next->entity_total = (uint16_t)(total > UINT16_MAX ? UINT16_MAX : total);
    next->truncated |= total > count;
    next->sampled_ms = now;
    for (unsigned i = 0; i < RP_HOME_PRINTERS; i++)
        if (c->printers[i].source == RP_HOME_HA) {
            rp_home_printer_t *v = &next->printers[i];
            v->result = v->has_data ? RP_HOME_OK : RP_HOME_EMPTY;
            if (v->has_data)
                v->sampled_ms = now;
        }
    *s = *next;
    result = RP_HOME_OK;
done:
    free(next);
    return result;
}

static bool offline(const char *s) {
    return !strcasecmp(s, "offline") || !strcasecmp(s, "off") || !strcasecmp(s, "disconnected") ||
           !strcasecmp(s, "unavailable");
}
rp_home_result_t rp_home_parse_formlabs(const char *json, size_t bytes, const char *serial, rp_home_printer_t *out,
                                        int64_t now) {
    cJSON *j = parse(json, bytes);
    if (!cJSON_IsObject(j)) {
        cJSON_Delete(j);
        return RP_HOME_INVALID;
    }
    cJSON *p = get(j, "printer");
    if (!cJSON_IsObject(p))
        p = j;
    const char *reported = first(p, "serial", "printer_serial");
    if (*reported && strcmp(reported, serial)) {
        cJSON_Delete(j);
        return RP_HOME_INVALID;
    }
    cJSON *status = get(p, "printer_status");
    if (!cJSON_IsObject(status))
        status = get(p, "status");
    if (!cJSON_IsObject(status))
        status = p;
    const char *state = first(status, "status", "state");
    if (!*state)
        state = str(p, "status");
    cJSON *run = get(status, "current_print_run");
    if (!cJSON_IsObject(run))
        run = get(p, "current_print_run");
    if (!*state && !cJSON_HasObjectItem(status, "current_print_run") && !cJSON_HasObjectItem(p, "current_print_run")) {
        cJSON_Delete(j);
        return RP_HOME_INVALID;
    }
    rp_home_printer_t n = *out;
    copy(n.serial, sizeof(n.serial), serial);
    if (!*n.name) copy(n.name, sizeof(n.name), str(p, "alias"));
    copy(n.state, sizeof(n.state), state);
    cJSON *on = get(p, "is_connected");
    n.connected = cJSON_IsBool(on) ? cJSON_IsTrue(on) : !offline(state);
    if (cJSON_IsObject(run)) {
        const char *name = first(run, "name", "print_name");
        if (*name && strcmp(n.task, name)) {
            n.percent = n.remaining_minutes = n.layer = n.layers = -1;
            n.material[0] = 0;
        }
        copy(n.task, sizeof(n.task), name);
        copy(n.state, sizeof(n.state), *str(run, "status") ? str(run, "status") : state);
        double layer = number(run, "currently_printing_layer"), layers = number(run, "layer_count");
        n.layer = integer(layer, 0, INT_MAX);
        n.layers = integer(layers, 1, INT_MAX);
        double elapsed = number(run, "elapsed_duration_ms"), duration = number(run, "estimated_duration_ms");
        double progress = layer >= 0 && layers > 0       ? 100 * layer / layers
                          : elapsed >= 0 && duration > 0 ? 100 * elapsed / duration
                                                         : NAN;
        n.percent = isfinite(progress) ? (int)fmax(0, fmin(100, progress)) : -1;
        double remaining = number(run, "estimated_time_remaining_ms");
        if (!isfinite(remaining))
            remaining = number(p, "estimated_print_time_remaining_ms");
        if (!isfinite(remaining) && duration >= elapsed && elapsed >= 0)
            remaining = duration - elapsed;
        n.remaining_minutes = integer(ceil(remaining / 60000), 0, 525600);
        copy(n.material, sizeof(n.material), first(run, "material_name", "material"));
    } else {
        // Dashboard明确无当前任务时，不能继续显示上一次任务的数据。
        // A null current_print_run must not keep the preceding job on an idle printer.
        n.task[0] = n.material[0] = 0;
        n.percent = n.remaining_minutes = n.layer = n.layers = -1;
    }
    n.nozzle = n.bed = -1;
    n.has_data = 1;
    n.source = RP_HOME_FORMLABS;
    n.result = RP_HOME_OK;
    n.sampled_ms = now;
    *out = n;
    cJSON_Delete(j);
    return RP_HOME_OK;
}
rp_home_result_t rp_home_parse_bambu(const char *json, size_t bytes, rp_home_printer_t *out, int64_t now) {
    cJSON *j = parse(json, bytes), *p = get(j, "print");
    if (!cJSON_IsObject(p)) {
        cJSON_Delete(j);
        return RP_HOME_INVALID;
    }
    if (!cJSON_HasObjectItem(p, "gcode_state") && !cJSON_HasObjectItem(p, "mc_percent") &&
        !cJSON_HasObjectItem(p, "nozzle_temper") && !cJSON_HasObjectItem(p, "layer_num")) {
        cJSON_Delete(j);
        return RP_HOME_INVALID;
    }
    rp_home_printer_t n = *out;
    const char *task = first(p, "subtask_name", "gcode_file");
    if (*task) {
        if (strcmp(task, n.task)) {
            n.percent = n.remaining_minutes = n.layer = n.layers = -1;
        }
        copy(n.task, sizeof(n.task), task);
    }
    const char *state = str(p, "gcode_state");
    if (*state)
        copy(n.state, sizeof(n.state), state);
    const char *keys[] = {"mc_percent",      "mc_remaining_time", "layer_num",
                          "total_layer_num", "nozzle_temper",     "bed_temper"};
    int *fields[] = {&n.percent, &n.remaining_minutes, &n.layer, &n.layers, &n.nozzle, &n.bed};
    for (unsigned i = 0; i < 6; i++)
        if (cJSON_HasObjectItem(p, keys[i]))
            *fields[i] = integer(number(p, keys[i]), i >= 4 ? -100 : 0, i == 0 ? 100 : i >= 4 ? 1000 : INT_MAX);
    n.has_data = n.connected = 1;
    n.result = RP_HOME_OK;
    n.sampled_ms = now;
    *out = n;
    cJSON_Delete(j);
    return RP_HOME_OK;
}
static bool encode(const char *s, char *out, size_t capacity, size_t *used) {
    static const char hex[] = "0123456789ABCDEF";
    for (const unsigned char *p = (const unsigned char *)s; *p; p++) {
        bool plain = isalnum(*p) || strchr("-._~", *p);
        size_t n = plain ? 1 : 3;
        if (*used + n >= capacity)
            return false;
        if (plain)
            out[(*used)++] = (char)*p;
        else {
            out[(*used)++] = '%';
            out[(*used)++] = hex[*p >> 4];
            out[(*used)++] = hex[*p & 15];
        }
    }
    out[*used] = 0;
    return true;
}
bool rp_home_form_body(const char *id, const char *secret, char *out, size_t capacity) {
    const char *head = "grant_type=client_credentials&client_id=";
    size_t used = strlen(head);
    if (!id || !*id || !secret || !*secret || !out || used >= capacity)
        return false;
    memcpy(out, head, used + 1);
    if (!encode(id, out, capacity, &used) || used + 15 >= capacity)
        return false;
    memcpy(out + used, "&client_secret=", 15);
    used += 15;
    return encode(secret, out, capacity, &used);
}
static uint32_t auth_hash(const rp_home_printer_config_t *p) {
    uint32_t h = 2166136261u;
    const char *v[] = {p->client_id, p->client_secret};
    for (unsigned i = 0; i < 2; i++) {
        for (const unsigned char *s = (const unsigned char *)v[i]; *s; s++)
            h = (h ^ *s) * 16777619u;
        h = (h ^ 0) * 16777619u;
    }
    return h;
}
static bool find_bytes(const char *hay, size_t n, const char *needle) {
    size_t m = strlen(needle);
    if (!hay || !m || n < m)
        return false;
    for (size_t i = 0; i + m <= n; i++)
        if (!memcmp(hay + i, needle, m))
            return true;
    return false;
}
static bool access_challenge(int status, const char *body, size_t n) {
    if (status == 302 || status == 401 || status == 403) {
        if (find_bytes(body, n, "cloudflareaccess.com") || find_bytes(body, n, "CF_AppSession") ||
            find_bytes(body, n, "Cloudflare Access") || find_bytes(body, n, "access.cloudflare") ||
            find_bytes(body, n, "cf-access") || find_bytes(body, n, "CF_Authorization"))
            return true;
        /* Redirect/forbidden with HTML login and no HA JSON object. */
        if (n && body[0] == '<' &&
            (find_bytes(body, n, "cloudflare") || find_bytes(body, n, "Access") || find_bytes(body, n, "login")))
            return true;
    }
    if (status >= 200 && status < 400 && n > 32 && body[0] == '<' &&
        (find_bytes(body, n, "cloudflareaccess.com") || find_bytes(body, n, "Cloudflare Access") ||
         find_bytes(body, n, "CF_AppSession")))
        return true;
    return false;
}
static rp_home_result_t request(const rp_home_transport_t *t, const char *method, const char *url_, const char *bearer,
                                const char *body, char *response, size_t capacity, size_t *bytes, int *status,
                                const char *cf_id, const char *cf_secret) {
    *bytes = 0;
    *status = 0;
    if (!t || !t->http)
        return RP_HOME_UNSUPPORTED;
    rp_home_http_request_t r = {method, url_, bearer,
                                body ? (*body == '{' ? "application/json" : "application/x-www-form-urlencoded") : NULL,
                                body, cf_id && *cf_id ? cf_id : NULL, cf_secret && *cf_secret ? cf_secret : NULL};
    rp_home_result_t result = t->http(t->context, &r, response, capacity, bytes, status);
    if (result != RP_HOME_OK)
        return result;
    if (*bytes >= capacity)
        return RP_HOME_TOO_LARGE;
    if (access_challenge(*status, response, *bytes))
        return RP_HOME_ACCESS;
    return *status == 401 || *status == 403  ? RP_HOME_AUTH
           : *status >= 200 && *status < 300 ? RP_HOME_OK
                                             : RP_HOME_HTTP;
}
static bool endpoint(const rp_home_config_t *c, bool at_home, char *out, size_t n, bool *external) {
    *external = !at_home;
    const char *base = at_home ? c->local : c->external;
    if (!at_home && (!c->external_enabled || !*base))
        return false;
    if (!*base || !url(base, !at_home))
        return false;
    int used = snprintf(out, n, "%s", base);
    while (used > 0 && out[used - 1] == '/')
        out[--used] = 0;
    return used > 0 && (size_t)used < n;
}
/* HA模板通过正式area_name读取注册表，既不猜名称也不下载完整设备注册表。
 * Official area_name resolves registry rooms without guessing names or downloading the full device registry. */
static void fetch_rooms(const rp_home_config_t *c, rp_home_snapshot_t *s, const rp_home_transport_t *t,
                        const char *base, char *response, bool external) {
    s->rooms_synced = 0;
    if (!s->entity_count)
        return;
    cJSON *payload = cJSON_CreateObject(), *variables = cJSON_CreateObject(), *entities = cJSON_CreateArray();
    if (!payload || !variables || !entities) {
        cJSON_Delete(payload);
        cJSON_Delete(variables);
        cJSON_Delete(entities);
        return;
    }
    cJSON_AddItemToObject(payload, "variables", variables);
    cJSON_AddItemToObject(variables, "entities", entities);
    cJSON_AddStringToObject(payload, "template",
                            "{% set ns=namespace(pairs=[]) %}{% for e in entities %}{% set a=area_name(e) %}{% if a "
                            "%}{% set ns.pairs=ns.pairs+[(e,a)] %}{% endif %}{% endfor %}{{dict(ns.pairs)|to_json}}");
    for (int i = 0; i < s->entity_count; i++)
        cJSON_AddItemToArray(entities, cJSON_CreateString(s->entities[i].id));
    char *body = cJSON_PrintUnformatted(payload);
    cJSON_Delete(payload);
    if (!body)
        return;
    char target[224];
    snprintf(target, sizeof(target), "%s/api/template", base);
    size_t bytes = 0;
    int status = 0;
    const char *cf_id = external && *c->cf_access_client_id ? c->cf_access_client_id : NULL;
    const char *cf_sec = external && *c->cf_access_client_secret ? c->cf_access_client_secret : NULL;
    rp_home_result_t result =
        request(t, "POST", target, c->ha_token, body, response, RP_HOME_HTTP_LIMIT + 1, &bytes, &status, cf_id, cf_sec);
    free(body);
    if (result != RP_HOME_OK)
        return;
    cJSON *mapping = parse(response, bytes);
    if (!cJSON_IsObject(mapping)) {
        cJSON_Delete(mapping);
        return;
    }
    s->rooms_synced = 1;
    for (int i = 0; i < s->entity_count; i++) {
        rp_home_entity_t *e = &s->entities[i];
        bool manual = false;
        for (unsigned r = 0; r < c->room_count; r++)
            if (!strcmp(c->rooms[r].entity, e->id))
                manual = true;
        const char *area = str(mapping, e->id);
        if (!manual && *area && strlen(area) < sizeof(e->room))
            copy(e->room, sizeof(e->room), area);
    }
    cJSON_Delete(mapping);
}
rp_home_result_t rp_home_fetch(const rp_home_config_t *c, rp_home_snapshot_t *s,
                               rp_home_token_t tokens[RP_HOME_PRINTERS], const rp_home_transport_t *t, bool online,
                               bool at_home, int64_t now) {
    if (!c || !s || !tokens || !rp_home_config_valid(c))
        return RP_HOME_INVALID;
    if (!c->enabled)
        return s->result = RP_HOME_EMPTY;
    if (!online)
        return s->result = RP_HOME_OFFLINE;
    char *response = model_memory(RP_HOME_HTTP_LIMIT + 1);
    if (!response)
        return s->result = RP_HOME_NOMEM;
    size_t bytes = 0;
    int status = 0;
    char base[192], target[320];
    bool external = false;
    s->result = RP_HOME_EMPTY;
    if (*c->ha_token) {
        rp_home_result_t result = RP_HOME_OFFLINE;
        if (endpoint(c, at_home, base, sizeof(base), &external)) {
            snprintf(target, sizeof(target), "%s/api/states", base);
            result = request(t, "GET", target, c->ha_token, NULL, response, RP_HOME_HTTP_LIMIT + 1, &bytes, &status,
                             external && *c->cf_access_client_id ? c->cf_access_client_id : NULL,
                             external && *c->cf_access_client_secret ? c->cf_access_client_secret : NULL);
        }
        if (result != RP_HOME_OK && at_home && c->external_enabled && *c->external && result != RP_HOME_AUTH &&
            endpoint(c, false, base, sizeof(base), &external)) {
            snprintf(target, sizeof(target), "%s/api/states", base);
            result = request(t, "GET", target, c->ha_token, NULL, response, RP_HOME_HTTP_LIMIT + 1, &bytes, &status,
                             *c->cf_access_client_id ? c->cf_access_client_id : NULL,
                             *c->cf_access_client_secret ? c->cf_access_client_secret : NULL);
        }
        if (result == RP_HOME_OK)
            result = rp_home_parse_ha(response, bytes, c, s, now);
        if (result == RP_HOME_OK)
            fetch_rooms(c, s, t, base, response, external);
        s->result = result;
        s->http_status = status;
        s->external = external;
    } else if (at_home && *c->local) {
        // 无凭据只探测标准API响应，401说明HA存在，不表示已认证。
        // An unauthenticated standard API probe treats 401 as presence, never authentication.
        if (endpoint(c, true, base, sizeof(base), &external)) {
            snprintf(target, sizeof(target), "%s/api/", base);
            rp_home_result_t found =
                request(t, "GET", target, NULL, NULL, response, RP_HOME_HTTP_LIMIT + 1, &bytes, &status, NULL, NULL);
            s->result = found == RP_HOME_OK || status == 401 ? RP_HOME_OK : found;
            s->http_status = status;
            s->external = 0;
        }
    }
    for (unsigned i = 0; i < RP_HOME_PRINTERS; i++) {
        const rp_home_printer_config_t *p = &c->printers[i];
        rp_home_printer_t *v = &s->printers[i];
        rp_home_result_t result = RP_HOME_EMPTY;
        if (p->source == RP_HOME_NONE)
            continue;
        if (p->source == RP_HOME_HA) {
            if (s->result != RP_HOME_OK)
                v->result = s->result;
            continue;
        }
        v->source = p->source;
        copy(v->name, sizeof(v->name), p->name);
        copy(v->serial, sizeof(v->serial), p->serial);
        status = 0;
        if (p->source == RP_HOME_FORMLABS) {
            rp_home_token_t *token = &tokens[i];
            uint32_t key = auth_hash(p);
            if (token->credential_hash != key) {
                memset(token, 0, sizeof(*token));
                token->credential_hash = key;
            }
            for (unsigned attempt = 0; attempt < 2; attempt++) {
                if (!*token->value || token->expires_ms <= now + 120000) {
                    char form[1280];
                    result = rp_home_form_body(p->client_id, p->client_secret, form, sizeof(form))
                                 ? request(t, "POST", "https://api.formlabs.com/developer/v1/o/token/", NULL, form,
                                           response, RP_HOME_HTTP_LIMIT + 1, &bytes, &status, NULL, NULL)
                                 : RP_HOME_INVALID;
                    memset(form, 0, sizeof(form));
                    if (result != RP_HOME_OK)
                        break;
                    cJSON *j = parse(response, bytes);
                    const char *value = str(j, "access_token");
                    double lifetime = number(j, "expires_in");
                    if (!isfinite(lifetime))
                        lifetime = 86400;
                    if (!*value || strlen(value) >= sizeof(token->value) || !header(value) || lifetime <= 0 ||
                        lifetime > 604800) {
                        cJSON_Delete(j);
                        result = RP_HOME_INVALID;
                        break;
                    }
                    copy(token->value, sizeof(token->value), value);
                    token->expires_ms = now + (int64_t)(lifetime * 1000);
                    cJSON_Delete(j);
                }
                snprintf(target, sizeof(target), "https://api.formlabs.com/developer/v1/printers/%s/", p->serial);
                result =
                    request(t, "GET", target, token->value, NULL, response, RP_HOME_HTTP_LIMIT + 1, &bytes, &status, NULL, NULL);
                if (result == RP_HOME_AUTH && attempt == 0) {
                    memset(token->value, 0, sizeof(token->value));
                    continue;
                }
                if (result == RP_HOME_OK)
                    result = rp_home_parse_formlabs(response, bytes, p->serial, v, now);
                break;
            }
        } else if (t && t->mqtt) {
            result = t->mqtt(t->context, p, response, RP_HOME_MQTT_LIMIT + 1, &bytes);
            if (result == RP_HOME_OK)
                result = bytes <= RP_HOME_MQTT_LIMIT ? rp_home_parse_bambu(response, bytes, v, now) : RP_HOME_TOO_LARGE;
        } else
            result = RP_HOME_UNSUPPORTED;
        /* 传输明确不支持时清掉上次遥测，避免槽位看起来仍在线。瞬时离线仍保留上次成功数据。
         * An explicit unsupported transport clears stale telemetry so the slot cannot look live.
         * A transient offline result still keeps the last successful sample. */
        if (result == RP_HOME_UNSUPPORTED) {
            printer_clear(v);
            v->source = p->source;
            copy(v->name, sizeof(v->name), p->name);
            copy(v->serial, sizeof(v->serial), p->serial);
        }
        v->result = result;
        v->http_status = status;
        if (s->result == RP_HOME_EMPTY || (!*c->ha_token && result != RP_HOME_OK))
            s->result = result;
    }
    memset(response, 0, RP_HOME_HTTP_LIMIT + 1);
    free(response);
    s->revision++;
    return s->result;
}
rp_home_result_t rp_home_control(const rp_home_config_t *c, const rp_home_entity_t *e, const char *action, float value,
                                 const rp_home_transport_t *t, bool at_home, int *status) {
    if (!c || !e || !action || !status || !rp_home_config_valid(c) || !c->enabled || !*c->ha_token || !e->available ||
        e->kind > RP_HOME_FAN || !atom(e->id))
        return RP_HOME_INVALID;
    if (!at_home && !c->external_control)
        return RP_HOME_AUTH;
    const char *domains[] = {"light", "switch", "climate", "fan"};
    const char *domain = domains[e->kind];
    size_t prefix = strlen(domain);
    if (strncmp(e->id, domain, prefix) || e->id[prefix] != '.')
        return RP_HOME_INVALID;
    bool temperature = e->kind == RP_HOME_CLIMATE && !strcmp(action, "set_temperature");
    if (strcmp(action, "turn_on") && strcmp(action, "turn_off") && !temperature)
        return RP_HOME_INVALID;
    if (temperature && (!isfinite(value) || value < 5 || value > 40))
        return RP_HOME_INVALID;
    char base[192], target[288], body[192], response[2048];
    bool external;
    if (!endpoint(c, at_home, base, sizeof(base), &external))
        return RP_HOME_OFFLINE;
    snprintf(target, sizeof(target), "%s/api/services/%s/%s", base, domain, action);
    if (temperature)
        snprintf(body, sizeof(body), "{\"entity_id\":\"%s\",\"temperature\":%.1f}", e->id, (double)value);
    else
        snprintf(body, sizeof(body), "{\"entity_id\":\"%s\"}", e->id);
    if (!t || !t->http)
        return RP_HOME_UNSUPPORTED;
    size_t bytes = 0;
    rp_home_http_request_t r = {"POST", target, c->ha_token, "application/json", body,
                                external && *c->cf_access_client_id ? c->cf_access_client_id : NULL,
                                external && *c->cf_access_client_secret ? c->cf_access_client_secret : NULL};
    rp_home_result_t result = t->http(t->context, &r, response, sizeof(response), &bytes, status);
    if (result == RP_HOME_OK && access_challenge(*status, response, bytes))
        result = RP_HOME_ACCESS;
    memset(response, 0, sizeof(response));
    return result != RP_HOME_OK               ? result
           : *status == 401 || *status == 403 ? RP_HOME_AUTH
           : *status >= 200 && *status < 300  ? RP_HOME_OK
                                              : RP_HOME_HTTP;
}
const char *rp_home_result_text(rp_home_result_t r) {
    static const char *labels[] = {
        "尚未配置",          "正在连接",       "已更新",           "网络不可用", "HA令牌无效或权限不足", "服务器返回错误",
        "TLS安全连接或证书校验失败", "响应或设置无效", "响应超过容量限制", "内存不足",   "当前传输不支持",
        "设备时间未校准，请设置日期与时间",
        "Cloudflare Access拦截（需Service Token，非浏览器登录）"};
    return (unsigned)r < sizeof(labels) / sizeof(labels[0]) ? labels[r] : "未知错误";
}

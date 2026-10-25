/*
 * SPDX-FileCopyrightText: 2026 XH Lab
 * SPDX-License-Identifier: Apache-2.0
 * Bambu账户登录的有界协议模型，参考ha-bambulab 0e027ff135a6的请求契约，独立实现。
 * Bounded Bambu account protocol, independently implemented from ha-bambulab 0e027ff135a6 request contracts.
 * 冻结：社区接口不是官方OAuth；密码只在一次任务存活，不模拟浏览器或绕过挑战。
 * Frozen: Community endpoints are not official OAuth; passwords live for one job, with no browser imitation or challenge bypass.
 */
#include "bambu_auth_internal.h"
#include "home_link_json.h"
#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef ESP_PLATFORM
#include "esp_heap_caps.h"
#endif

#define BAMBU_RESPONSE_LIMIT (64u * 1024u)
void rp_home_bambu_wipe(void *p, size_t n) { volatile unsigned char *v = p; while (n--) *v++ = 0; }
static void *allocate(size_t n) {
#ifdef ESP_PLATFORM
    return heap_caps_calloc(1, n, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
#else
    return calloc(1, n);
#endif
}
static bool bounded(const char *s, size_t n) { return memchr(s, 0, n) != NULL; }
static bool clean(const char *s) { for (; *s; ++s) if ((unsigned char)*s < 32 || (unsigned char)*s == 127) return false; return true; }
bool rp_home_bambu_input_valid(const rp_home_bambu_input_t *in) {
    if (!in || in->op > RP_BAMBU_REUSE || in->region > 1 || !bounded(in->account, sizeof(in->account)) ||
        !bounded(in->password, sizeof(in->password)) || !bounded(in->code, sizeof(in->code))) return false;
    if (in->op == RP_BAMBU_LOGIN) return *in->account && *in->password && !*in->code && clean(in->account) &&
        clean(in->password) && !strchr(in->account, ' ');
    if (*in->account || *in->password) return false;
    if (in->op == RP_BAMBU_REUSE) return !in->region && !*in->code && in->saved_slot < RP_HOME_PRINTERS;
    if (in->op == RP_BAMBU_RESEND) return !*in->code;
    size_t n = strlen(in->code);
    return n >= 4 && n <= 12 && strspn(in->code, "0123456789") == n;
}
static void scrub_json(cJSON *j) {
    for (cJSON *p = j; p; p = p->next) { if (p->child) scrub_json(p->child); if (p->valuestring) rp_home_bambu_wipe(p->valuestring, strlen(p->valuestring)); }
}
static void release(cJSON *j) { scrub_json(j); cJSON_Delete(j); }
static cJSON *member(const cJSON *j, const char *key) { return cJSON_GetObjectItemCaseSensitive(j, key); }
static const char *string(const cJSON *j, const char *key) { cJSON *v = member(j, key); return cJSON_IsString(v) ? v->valuestring : ""; }
static bool unique(const cJSON *j) {
    for (const cJSON *p = j->child; p; p = p->next) for (const cJSON *q = j->child; q != p; q = q->next)
        if (p->string && q->string && !strcmp(p->string, q->string)) return false;
    return true;
}
static cJSON *parse(const char *raw, size_t n) {
    if (!n || n > BAMBU_RESPONSE_LIMIT || memchr(raw, 0, n)) return NULL;
    for (size_t i = 0; i + 6 <= n; ++i) if (!memcmp(raw + i, "\\u0000", 6)) return NULL;
    const char *end = NULL;
    cJSON *j = cJSON_ParseWithLengthOpts(raw, n, &end, false);
    if (j) while (end < raw + n && isspace((unsigned char)*end)) ++end;
    if (!cJSON_IsObject(j) || end != raw + n || !unique(j)) { release(j); return NULL; }
    return j;
}
static bool atom(const char *s) { return *s && strspn(s, "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_.-") == strlen(s); }
// 整个字符串须为合法UTF-8；只在完整码点边界截短设备显示名，不截断序列号或凭据。
// Validate all UTF-8 and shorten display names only at code-point boundaries, never serials or credentials.
static bool display_text(char *out, size_t cap, const char *s) {
    size_t used = 0; bool full = false;
    const unsigned char *p = (const unsigned char *)s;
    while (*p) {
        unsigned n = *p < 0x80 ? 1 : *p >= 0xc2 && *p <= 0xdf ? 2 : *p >= 0xe0 && *p <= 0xef ? 3 : *p >= 0xf0 && *p <= 0xf4 ? 4 : 0;
        if (!n || *p < 32 || *p == 127) return false;
        uint32_t cp = *p & (n == 1 ? 0x7fu : (1u << (7u - n)) - 1u);
        for (unsigned k = 1; k < n; ++k) { if (!p[k] || (p[k] & 0xc0u) != 0x80u) return false; cp = (cp << 6) | (p[k] & 63u); }
        if ((n == 2 && cp < 128) || (n == 3 && cp < 2048) || (n == 4 && cp < 65536) || cp > 0x10ffff || (cp >= 0xd800 && cp <= 0xdfff)) return false;
        if (!full && used + n < cap) { memcpy(out + used, p, n); used += n; } else full = true;
        p += n;
    }
    out[used] = 0;
    return true;
}
static rp_home_result_t call(const rp_home_transport_t *t, unsigned region, const char *path, const char *token,
                             cJSON *payload, char *out, size_t *bytes, int *status) {
    *bytes = 0; *status = 0;
    if (!t || !t->http) return RP_HOME_UNSUPPORTED;
    char url[160];
    if (!strcmp(path, "sms")) snprintf(url, sizeof(url), "https://bambulab.cn/api/v1/user-service/user/sendsmscode");
    else snprintf(url, sizeof(url), "https://api.bambulab.%s%s", region ? "cn" : "com", path);
    char *body = payload ? cJSON_PrintUnformatted(payload) : NULL;
    if (payload && !body) return RP_HOME_NOMEM;
    rp_home_http_request_t r = {payload ? "POST" : "GET", url, token, payload ? "application/json" : NULL, body};
    rp_home_result_t result = t->http(t->context, &r, out, BAMBU_RESPONSE_LIMIT + 1, bytes, status);
    if (body) { rp_home_bambu_wipe(body, strlen(body)); free(body); }
    if (result != RP_HOME_OK) return result;
    if (*bytes > BAMBU_RESPONSE_LIMIT) return RP_HOME_TOO_LARGE;
    if (*status == 400 || *status == 401 || *status == 403) return RP_HOME_AUTH;
    return *status >= 200 && *status < 300 ? RP_HOME_OK : RP_HOME_HTTP;
}
static unsigned error_for(rp_home_result_t r, int status) {
    return status == 429 ? RP_BAMBU_ERROR_RATE_LIMIT : status == 403 ? RP_BAMBU_ERROR_BLOCKED :
        r == RP_HOME_AUTH ? RP_BAMBU_ERROR_CREDENTIALS : r == RP_HOME_INVALID ? RP_BAMBU_ERROR_RESPONSE : RP_BAMBU_ERROR_NONE;
}
static void failure(rp_home_bambu_session_t *s, unsigned phase, rp_home_result_t r, int status, unsigned error) {
    s->public.phase = (uint8_t)phase; s->public.result = r; s->public.http_status = status; s->public.error = (uint8_t)error;
    rp_home_bambu_wipe(s->token, sizeof(s->token)); rp_home_bambu_wipe(s->username, sizeof(s->username));
    s->public.count = s->public.truncated = 0; memset(s->public.devices, 0, sizeof(s->public.devices));
}
static bool identity(const cJSON *j, char *out, size_t capacity) {
    cJSON *v = member(j, "uid"); char numeric[32]; const char *uid = cJSON_IsString(v) ? v->valuestring : "";
    if (cJSON_IsNumber(v) && isfinite(v->valuedouble) && v->valuedouble >= 0 && v->valuedouble <= 9007199254740991.0 && floor(v->valuedouble) == v->valuedouble) {
        snprintf(numeric, sizeof(numeric), "%.0f", v->valuedouble); uid = numeric;
    }
    size_t n = strlen(uid);
    if (!n || n + 3 > capacity || strspn(uid, "0123456789") != n) return false;
    snprintf(out, capacity, "u_%s", uid); return true;
}
static bool devices(const cJSON *j, rp_home_bambu_state_t *state) {
    cJSON *array = member(j, "devices");
    if (!cJSON_IsArray(array) || cJSON_GetArraySize(array) > 256) return false;
    state->count = state->truncated = 0; memset(state->devices, 0, sizeof(state->devices));
    for (cJSON *v = array->child; v; v = v->next) {
        const char *serial = string(v, "dev_id");
        if (!cJSON_IsObject(v) || !unique(v) || !atom(serial) || strlen(serial) >= sizeof(state->devices[0].serial)) return false;
        for (cJSON *p = array->child; p != v; p = p->next) if (!strcmp(serial, string(p, "dev_id"))) return false;
        rp_home_bambu_device_t candidate = {0};
        memcpy(candidate.serial, serial, strlen(serial));
        const char *name = string(v, "name"), *model = string(v, "dev_product_name");
        if (!*model) model = string(v, "dev_model_name");
        if (!display_text(candidate.name, sizeof(candidate.name), *name ? name : serial) || !display_text(candidate.model, sizeof(candidate.model), model)) return false;
        if (state->count < RP_HOME_BAMBU_DEVICES) state->devices[state->count++] = candidate;
        else state->truncated = 1;
    }
    return true;
}
void rp_home_bambu_execute(const rp_home_bambu_input_t *in, rp_home_bambu_session_t *s, const rp_home_transport_t *t) {
    bool reuse = in->op == RP_BAMBU_REUSE;
    unsigned fallback = in->op == RP_BAMBU_LOGIN || reuse ? RP_BAMBU_ERROR : s->public.phase;
    if (!rp_home_bambu_input_valid(in)) { failure(s, fallback, RP_HOME_INVALID, 0, RP_BAMBU_ERROR_RESPONSE); return; }
    if (in->op == RP_BAMBU_TFA_CODE) { failure(s, RP_BAMBU_TFA, RP_HOME_UNSUPPORTED, 0, RP_BAMBU_ERROR_TFA_UNSUPPORTED); return; }
    char *response = allocate(BAMBU_RESPONSE_LIMIT + 1);
    cJSON *body = reuse ? NULL : cJSON_CreateObject(), *j = NULL;
    size_t bytes = 0; int status = 0; rp_home_result_t result = RP_HOME_OK;
    if (!response || (!reuse && !body)) { failure(s, fallback, RP_HOME_NOMEM, 0, RP_BAMBU_ERROR_NONE); goto done; }
    if (reuse) {
        if (!bounded(s->token, sizeof(s->token)) || !*s->token || !clean(s->token) ||
            !bounded(s->username, sizeof(s->username)) || strncmp(s->username, "u_", 2) || !atom(s->username) || s->public.region > 1) {
            failure(s, fallback, RP_HOME_INVALID, 0, RP_BAMBU_ERROR_RESPONSE); goto done;
        }
        goto account;
    }
    const char *path = "/v1/user-service/user/login";
    if (in->op == RP_BAMBU_LOGIN) {
        memcpy(s->account, in->account, sizeof(s->account)); s->public.region = in->region;
        if (!cJSON_AddStringToObject(body, "account", s->account) || !cJSON_AddStringToObject(body, "password", in->password) || !cJSON_AddStringToObject(body, "apiError", "")) goto memory;
    } else if (in->op == RP_BAMBU_RESEND) {
        bool email = strchr(s->account, '@') != NULL;
        if (!cJSON_AddStringToObject(body, email ? "email" : "phone", s->account) || !cJSON_AddStringToObject(body, "type", "codeLogin")) goto memory;
        path = email ? "/v1/user-service/user/sendemail/code" : "sms";
    } else if (!cJSON_AddStringToObject(body, "account", s->account) || !cJSON_AddStringToObject(body, "code", in->code)) goto memory;
    result = call(t, s->public.region, path, NULL, body, response, &bytes, &status);
    release(body); body = NULL;
    if (result != RP_HOME_OK) {
        unsigned error = error_for(result, status);
        if (in->op == RP_BAMBU_EMAIL_CODE && status == 400) {
            j = parse(response, bytes); cJSON *code = member(j, "code");
            if (cJSON_IsNumber(code)) error = code->valuedouble == 1 ? RP_BAMBU_ERROR_CODE_EXPIRED : code->valuedouble == 2 ? RP_BAMBU_ERROR_CODE_INCORRECT : error;
        }
        failure(s, fallback, result, status, error); goto done;
    }
    if (in->op == RP_BAMBU_RESEND) { s->public.phase = RP_BAMBU_EMAIL; s->public.result = RP_HOME_OK; s->public.http_status = status; s->public.error = RP_BAMBU_ERROR_NONE; goto done; }
    j = parse(response, bytes);
    if (!j) { failure(s, fallback, RP_HOME_INVALID, status, RP_BAMBU_ERROR_RESPONSE); goto done; }
    const char *token = string(j, "accessToken"), *type = string(j, "loginType");
    if (!*token && in->op == RP_BAMBU_LOGIN && (!strcmp(type, "verifyCode") || !strcmp(type, "tfa"))) {
        s->public.phase = !strcmp(type, "tfa") ? RP_BAMBU_TFA : RP_BAMBU_EMAIL;
        s->public.result = s->public.phase == RP_BAMBU_TFA ? RP_HOME_UNSUPPORTED : RP_HOME_OK;
        s->public.error = s->public.phase == RP_BAMBU_TFA ? RP_BAMBU_ERROR_TFA_UNSUPPORTED : RP_BAMBU_ERROR_NONE;
        s->public.http_status = status; goto done;
    }
    if (!*token || strlen(token) >= sizeof(s->token) || !clean(token)) { failure(s, fallback, RP_HOME_INVALID, status, RP_BAMBU_ERROR_RESPONSE); goto done; }
    memcpy(s->token, token, strlen(token) + 1); release(j); j = NULL;
account:
    rp_home_bambu_wipe(response, BAMBU_RESPONSE_LIMIT + 1);
    result = call(t, s->public.region, "/v1/design-user-service/my/preference", s->token, NULL, response, &bytes, &status);
    if (result == RP_HOME_OK) {
        char verified[sizeof(s->username)]; j = parse(response, bytes);
        if (!j || !identity(j, verified, sizeof(verified))) result = RP_HOME_INVALID;
        else if (reuse && strcmp(verified, s->username)) result = RP_HOME_AUTH;
        else memcpy(s->username, verified, strlen(verified) + 1);
        rp_home_bambu_wipe(verified, sizeof(verified));
    }
    release(j); j = NULL;
    if (result != RP_HOME_OK) { failure(s, RP_BAMBU_ERROR, result, status, error_for(result, status)); goto done; }
    rp_home_bambu_wipe(response, BAMBU_RESPONSE_LIMIT + 1);
    result = call(t, s->public.region, "/v1/iot-service/api/user/bind", s->token, NULL, response, &bytes, &status);
    if (result == RP_HOME_OK) { j = parse(response, bytes); if (!j || !devices(j, &s->public)) result = RP_HOME_INVALID; }
    if (result != RP_HOME_OK) { failure(s, RP_BAMBU_ERROR, result, status, error_for(result, status)); goto done; }
    s->public.phase = RP_BAMBU_READY; s->public.result = RP_HOME_OK; s->public.http_status = status; s->public.error = RP_BAMBU_ERROR_NONE;
    goto done;
memory:
    failure(s, fallback, RP_HOME_NOMEM, 0, RP_BAMBU_ERROR_NONE);
done:
    release(body); release(j);
    if (response) { rp_home_bambu_wipe(response, BAMBU_RESPONSE_LIMIT + 1); free(response); }
}

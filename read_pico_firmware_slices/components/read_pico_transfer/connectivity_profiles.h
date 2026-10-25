/* SPDX-License-Identifier: Apache-2.0
 * 有界WiFi记忆与原单凭据迁移；原子提交后才替换内存状态。
 * Bounded WiFi profiles and legacy migration; replace live state only after atomic commit.
 */
#pragma once
#include "transfer_credentials_store.h"

enum { CONNECTIVITY_PROFILE_MAX = 8 };
typedef struct {
    uint32_t version;
    uint8_t count, enabled, reserved[2];
    struct { transfer_credentials_t credentials; uint8_t auto_join; } items[CONNECTIVITY_PROFILE_MAX];
} connectivity_profiles_t;

static bool profiles_valid(const connectivity_profiles_t *p) {
    if (p->version != 1 || p->count > CONNECTIVITY_PROFILE_MAX || p->enabled > 1) return false;
    for (unsigned i = 0; i < p->count; ++i) {
        if (!credentials_valid(&p->items[i].credentials) || p->items[i].auto_join > 1) return false;
        for (unsigned j = 0; j < i; ++j)
            if (!strcmp(p->items[j].credentials.ssid, p->items[i].credentials.ssid)) return false;
    }
    return true;
}

static int profiles_find(const connectivity_profiles_t *p, const char *ssid) {
    for (unsigned i = 0; i < p->count; ++i)
        if (!strcmp(p->items[i].credentials.ssid, ssid)) return (int)i;
    return -1;
}

static esp_err_t profiles_store(const connectivity_profiles_t *next) {
    if (!profiles_valid(next)) return ESP_ERR_INVALID_ARG;
    nvs_handle_t handle;
    esp_err_t err = nvs_open("rp_connect", NVS_READWRITE, &handle);
    if (err != ESP_OK) return err;
    err = nvs_set_blob(handle, "profiles", next, sizeof(*next));
    if (err == ESP_OK) err = nvs_commit(handle);
    nvs_close(handle);
    return err;
}

static esp_err_t profiles_load(connectivity_profiles_t *out) {
    memset(out, 0, sizeof(*out)); out->version = 1;
    nvs_handle_t handle;
    esp_err_t err = nvs_open("rp_connect", NVS_READONLY, &handle);
    if (err == ESP_OK) {
        size_t length = sizeof(*out);
        err = nvs_get_blob(handle, "profiles", out, &length);
        nvs_close(handle);
        if (err == ESP_OK) {
            if (length == sizeof(*out) && profiles_valid(out)) return ESP_OK;
            clear_secret(out, sizeof(*out)); out->version = 1;
            return ESP_ERR_INVALID_STATE;
        }
    }
    if (err != ESP_ERR_NVS_NOT_FOUND) return err;
    // 只迁移一次，不删除原blob；迁移写失败仍保留原始凭据可重试。
    // Migrate once without deleting the legacy blob; failed commits retain the original for retry.
    transfer_credentials_t legacy;
    err = load_credentials(&legacy);
    if (err == ESP_OK && legacy.version == 1) {
        out->count = 1; out->items[0].credentials = legacy; out->items[0].auto_join = 1;
    }
    clear_secret(&legacy, sizeof(legacy));
    if (err != ESP_OK) return err;
    return profiles_store(out);
}

static esp_err_t profiles_remember(connectivity_profiles_t *live, const transfer_credentials_t *credentials) {
    if (!credentials_valid(credentials)) return ESP_ERR_INVALID_ARG;
    int at = profiles_find(live, credentials->ssid);
    if (at < 0 && live->count == CONNECTIVITY_PROFILE_MAX) return ESP_ERR_NO_MEM;
    if (at == 0 && !memcmp(&live->items[0].credentials, credentials, sizeof(*credentials))) return ESP_OK;
    bool auto_join = at < 0 || live->items[at].auto_join;
    connectivity_profiles_t next = *live;
    if (at < 0) at = next.count++;
    for (int i = at; i > 0; --i) next.items[i] = next.items[i - 1];
    next.items[0].credentials = *credentials;
    next.items[0].auto_join = auto_join;
    esp_err_t err = profiles_store(&next);
    if (err == ESP_OK) *live = next;
    clear_secret(&next, sizeof(next));
    return err;
}

static esp_err_t profiles_forget(connectivity_profiles_t *live, const char *ssid) {
    int at = profiles_find(live, ssid);
    if (at < 0) return ESP_ERR_NOT_FOUND;
    connectivity_profiles_t next = *live;
    for (unsigned i = (unsigned)at; i + 1 < next.count; ++i) next.items[i] = next.items[i + 1];
    clear_secret(&next.items[--next.count], sizeof(next.items[0]));
    esp_err_t err = profiles_store(&next);
    if (err == ESP_OK) *live = next;
    clear_secret(&next, sizeof(next));
    return err;
}

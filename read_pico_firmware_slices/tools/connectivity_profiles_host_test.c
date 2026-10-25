/*
 * SPDX-License-Identifier: Apache-2.0
 * 多网络真实存储逻辑的NVS故障注入；不访问设备或用户凭据。
 * Inject NVS faults into production profile helpers without device or user credentials.
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../components/read_pico_transfer/transfer_policy.h"

#define READ_PICO_TRANSFER_NVS_TEST
typedef int esp_err_t;
typedef unsigned nvs_handle_t;
enum {
    ESP_OK, ESP_ERR_NVS_NOT_FOUND, ESP_ERR_INVALID_STATE, ESP_ERR_INVALID_ARG,
    ESP_FAIL, ESP_ERR_NO_MEM, ESP_ERR_NOT_FOUND, ESP_ERR_NVS_INVALID_LENGTH,
};
enum { NVS_READONLY, NVS_READWRITE };
enum { LEGACY, PROFILES, STORE_COUNT, BLOB_CAPACITY = 4096, HANDLE_COUNT = 8 };
enum { OP_OPEN = 1, OP_READ, OP_WRITE, OP_COMMIT };

static unsigned checks, scenarios;
#define CHECK(condition) do { ++checks; if (!(condition)) { \
    fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); exit(1); \
} } while (0)

typedef struct {
    bool namespace_present, present;
    size_t length;
    unsigned char bytes[BLOB_CAPACITY];
} test_blob_t;
typedef struct {
    bool active;
    unsigned store;
    int mode;
    test_blob_t staged;
} test_handle_t;
static test_blob_t committed[STORE_COUNT];
static test_handle_t handles[HANDLE_COUNT];
static unsigned open_handles, operations[STORE_COUNT][OP_COMMIT + 1];
static int fault_store = -1, fault_operation;

static bool injected(unsigned store, int operation) {
    ++operations[store][operation];
    if (fault_store != (int)store || fault_operation != operation) return false;
    fault_operation = 0;
    return true;
}
static unsigned store_for(const char *name) {
    if (!strcmp(name, "rp_wifi")) return LEGACY;
    CHECK(!strcmp(name, "rp_connect")); return PROFILES;
}
static test_handle_t *handle_for(nvs_handle_t handle) {
    CHECK(handle && handle <= HANDLE_COUNT && handles[handle - 1].active);
    return &handles[handle - 1];
}
static void check_key(unsigned store, const char *key) {
    CHECK(!strcmp(key, store == LEGACY ? "credentials" : "profiles"));
}
static esp_err_t nvs_open(const char *name, int mode, nvs_handle_t *handle) {
    unsigned store = store_for(name);
    if (injected(store, OP_OPEN)) return ESP_FAIL;
    if (mode == NVS_READONLY && !committed[store].namespace_present) return ESP_ERR_NVS_NOT_FOUND;
    for (unsigned i = 0; i < HANDLE_COUNT; ++i) if (!handles[i].active) {
        handles[i] = (test_handle_t){.active = true, .store = store, .mode = mode, .staged = committed[store]};
        handles[i].staged.namespace_present = true;
        *handle = i + 1; ++open_handles; return ESP_OK;
    }
    return ESP_ERR_NO_MEM;
}
static esp_err_t nvs_get_blob(nvs_handle_t handle, const char *key, void *out, size_t *length) {
    test_handle_t *h = handle_for(handle); check_key(h->store, key);
    if (injected(h->store, OP_READ)) return ESP_FAIL;
    const test_blob_t *blob = &committed[h->store];
    if (!blob->present) return ESP_ERR_NVS_NOT_FOUND;
    if (!out) { *length = blob->length; return ESP_OK; }
    if (*length < blob->length) { *length = blob->length; return ESP_ERR_NVS_INVALID_LENGTH; }
    memcpy(out, blob->bytes, blob->length); *length = blob->length; return ESP_OK;
}
static esp_err_t nvs_set_blob(nvs_handle_t handle, const char *key, const void *data, size_t length) {
    test_handle_t *h = handle_for(handle); check_key(h->store, key);
    CHECK(h->mode == NVS_READWRITE && length <= BLOB_CAPACITY);
    if (injected(h->store, OP_WRITE)) return ESP_FAIL;
    memset(h->staged.bytes, 0, sizeof(h->staged.bytes));
    memcpy(h->staged.bytes, data, length); h->staged.length = length; h->staged.present = true;
    return ESP_OK;
}
static esp_err_t nvs_erase_key(nvs_handle_t handle, const char *key) {
    test_handle_t *h = handle_for(handle); check_key(h->store, key); CHECK(h->mode == NVS_READWRITE);
    if (injected(h->store, OP_WRITE)) return ESP_FAIL;
    bool present = h->staged.present;
    h->staged.present = false; h->staged.length = 0; memset(h->staged.bytes, 0, sizeof(h->staged.bytes));
    return present ? ESP_OK : ESP_ERR_NVS_NOT_FOUND;
}
static esp_err_t nvs_commit(nvs_handle_t handle) {
    test_handle_t *h = handle_for(handle); CHECK(h->mode == NVS_READWRITE);
    if (injected(h->store, OP_COMMIT)) return ESP_FAIL;
    committed[h->store] = h->staged; return ESP_OK;
}
static void nvs_close(nvs_handle_t handle) {
    test_handle_t *h = handle_for(handle); clear_secret(h, sizeof(*h)); CHECK(open_handles); --open_handles;
}

#include "../components/read_pico_transfer/connectivity_profiles.h"

static void reset(void) {
    CHECK(!open_handles);
    clear_secret(committed, sizeof(committed)); clear_secret(handles, sizeof(handles));
    memset(operations, 0, sizeof(operations)); fault_store = -1; fault_operation = 0; ++scenarios;
}
static void fail_next(unsigned store, int operation) { fault_store = (int)store; fault_operation = operation; }
static void seed(unsigned store, const void *data, size_t length) {
    CHECK(length <= BLOB_CAPACITY);
    committed[store] = (test_blob_t){.namespace_present = true, .present = true, .length = length};
    memcpy(committed[store].bytes, data, length);
}
static transfer_credentials_t credentials(unsigned number, unsigned edition) {
    char ssid[33], password[65]; transfer_credentials_t result;
    snprintf(ssid, sizeof(ssid), "test-network-%u", number);
    snprintf(password, sizeof(password), "test-pass-%u-%u", number, edition);
    CHECK(credentials_from_text(ssid, password, &result)); return result;
}
static connectivity_profiles_t empty_profiles(void) {
    return (connectivity_profiles_t){.version = 1};
}
static connectivity_profiles_t loaded(void) {
    connectivity_profiles_t result; CHECK(profiles_load(&result) == ESP_OK); CHECK(!open_handles);
    CHECK(profiles_valid(&result)); return result;
}
static void same(const connectivity_profiles_t *a, const connectivity_profiles_t *b) {
    CHECK(!memcmp(a, b, sizeof(*a)));
}
static unsigned writes(void) { return operations[PROFILES][OP_WRITE]; }
static void check_order(const connectivity_profiles_t *profiles, const unsigned *numbers, size_t count) {
    CHECK(profiles->count == count); CHECK(profiles_valid(profiles));
    for (size_t i = 0; i < count; ++i) {
        transfer_credentials_t item = credentials(numbers[i], 0);
        CHECK(!strcmp(profiles->items[i].credentials.ssid, item.ssid));
        CHECK(profiles_find(profiles, item.ssid) == (int)i);
    }
}

static void test_migration(void) {
    reset(); CHECK(sizeof(transfer_credentials_t) == 99);
    transfer_credentials_t old = credentials(12, 0); seed(LEGACY, &old, sizeof(old));
    test_blob_t legacy_before = committed[LEGACY];
    connectivity_profiles_t profiles = loaded();
    CHECK(profiles.count == 1 && profiles.items[0].auto_join == 1);
    CHECK(!memcmp(&profiles.items[0].credentials, &old, sizeof(old)));
    CHECK(!memcmp(&committed[LEGACY], &legacy_before, sizeof(legacy_before)));
    CHECK(committed[PROFILES].present && committed[PROFILES].length == sizeof(profiles));
    unsigned legacy_reads = operations[LEGACY][OP_READ];
    transfer_credentials_t changed = credentials(13, 1); seed(LEGACY, &changed, sizeof(changed));
    connectivity_profiles_t reboot = loaded(); same(&profiles, &reboot);
    CHECK(operations[LEGACY][OP_READ] == legacy_reads);

    reset(); committed[PROFILES].namespace_present = true; seed(LEGACY, &old, sizeof(old));
    profiles = loaded(); CHECK(profiles.count == 1); // 缺key也迁移。/ A missing key also migrates.

    reset(); profiles = loaded(); CHECK(profiles.count == 0 && committed[PROFILES].present);
    seed(LEGACY, &old, sizeof(old)); legacy_reads = operations[LEGACY][OP_READ];
    reboot = loaded(); CHECK(reboot.count == 0); CHECK(operations[LEGACY][OP_READ] == legacy_reads);

    reset(); profiles = empty_profiles(); seed(PROFILES, &profiles, sizeof(profiles)); seed(LEGACY, &old, sizeof(old));
    reboot = loaded(); CHECK(reboot.count == 0 && operations[LEGACY][OP_OPEN] == 0 && !writes());

    reset(); seed(LEGACY, &old, sizeof(old)); profiles = loaded();
    CHECK(profiles_forget(&profiles, old.ssid) == ESP_OK && profiles.count == 0);
    reboot = loaded(); CHECK(reboot.count == 0 && committed[LEGACY].present);

    // NVS错误必须终止，不能当作不存在再迁移。/ I/O errors must not be treated as a missing profile store.
    for (int operation = OP_OPEN; operation <= OP_READ; ++operation) {
        reset(); profiles = empty_profiles(); seed(PROFILES, &profiles, sizeof(profiles)); seed(LEGACY, &old, sizeof(old));
        fail_next(PROFILES, operation); CHECK(profiles_load(&reboot) == ESP_FAIL);
        CHECK(!open_handles && !writes() && operations[LEGACY][OP_OPEN] == 0);
    }
    for (int operation = OP_OPEN; operation <= OP_READ; ++operation) {
        reset(); seed(LEGACY, &old, sizeof(old)); fail_next(LEGACY, operation);
        CHECK(profiles_load(&reboot) == ESP_FAIL && !open_handles && !writes());
    }
    for (int operation = OP_WRITE; operation <= OP_COMMIT; ++operation) {
        reset(); seed(LEGACY, &old, sizeof(old)); legacy_before = committed[LEGACY]; fail_next(PROFILES, operation);
        CHECK(profiles_load(&reboot) == ESP_FAIL && !open_handles && !committed[PROFILES].present);
        CHECK(!memcmp(&committed[LEGACY], &legacy_before, sizeof(legacy_before)));
        reboot = loaded(); CHECK(reboot.count == 1 && profiles_find(&reboot, old.ssid) == 0);
    }
}

static void test_remember_lru_capacity(void) {
    reset(); connectivity_profiles_t profiles = loaded(); profiles.enabled = 1; CHECK(profiles_store(&profiles) == ESP_OK);
    for (unsigned i = 0; i < CONNECTIVITY_PROFILE_MAX; ++i) {
        transfer_credentials_t item = credentials(i, 0); CHECK(profiles_remember(&profiles, &item) == ESP_OK);
        CHECK(profiles.count == i + 1 && profiles.enabled == 1);
        connectivity_profiles_t reboot = loaded(); same(&profiles, &reboot);
    }
    const unsigned initial[] = {7, 6, 5, 4, 3, 2, 1, 0}; check_order(&profiles, initial, 8);
    unsigned before_writes = writes(); transfer_credentials_t item = credentials(7, 0);
    CHECK(profiles_remember(&profiles, &item) == ESP_OK && writes() == before_writes);
    connectivity_profiles_t before = profiles; test_blob_t disk_before = committed[PROFILES]; item = credentials(8, 0);
    CHECK(profiles_remember(&profiles, &item) == ESP_ERR_NO_MEM); same(&profiles, &before);
    CHECK(!memcmp(&committed[PROFILES], &disk_before, sizeof(disk_before)) && writes() == before_writes);
    profiles.items[4].auto_join = 0; CHECK(profiles_store(&profiles) == ESP_OK);
    item = credentials(3, 0); CHECK(profiles_remember(&profiles, &item) == ESP_OK);
    const unsigned reordered[] = {3, 7, 6, 5, 4, 2, 1, 0}; check_order(&profiles, reordered, 8);
    CHECK(profiles.items[0].auto_join == 0);
    item = credentials(3, 1); CHECK(profiles_remember(&profiles, &item) == ESP_OK);
    CHECK(!strcmp(profiles.items[0].credentials.password, item.password) && !profiles.items[0].auto_join);
    CHECK(profiles.count == 8); connectivity_profiles_t reboot = loaded(); same(&profiles, &reboot);
    transfer_credentials_t invalid = item; invalid.password[0] = 1; before = profiles; before_writes = writes();
    CHECK(profiles_remember(&profiles, &invalid) == ESP_ERR_INVALID_ARG); same(&profiles, &before); CHECK(writes() == before_writes);
}

static void test_forget_and_atomic_failures(void) {
    for (unsigned mutation = 0; mutation < 3; ++mutation) for (int operation = OP_OPEN; operation <= OP_COMMIT; ++operation) {
        if (operation == OP_READ) continue;
        reset(); connectivity_profiles_t profiles = loaded();
        for (unsigned i = 0; i < 4; ++i) { transfer_credentials_t item = credentials(i, 0); CHECK(profiles_remember(&profiles, &item) == ESP_OK); }
        connectivity_profiles_t before = profiles; test_blob_t disk_before = committed[PROFILES];
        transfer_credentials_t item = credentials(mutation == 0 ? 8 : mutation == 1 ? 1 : 2, 1);
        fail_next(PROFILES, operation);
        esp_err_t err = mutation == 2 ? profiles_forget(&profiles, item.ssid) : profiles_remember(&profiles, &item);
        CHECK(err == ESP_FAIL && !open_handles); same(&profiles, &before);
        CHECK(!memcmp(&committed[PROFILES], &disk_before, sizeof(disk_before)));
        connectivity_profiles_t reboot = loaded(); same(&profiles, &reboot);
        err = mutation == 2 ? profiles_forget(&profiles, item.ssid) : profiles_remember(&profiles, &item);
        CHECK(err == ESP_OK && !open_handles); reboot = loaded(); same(&profiles, &reboot);
    }
    reset(); connectivity_profiles_t profiles = loaded();
    for (unsigned i = 0; i < 8; ++i) { transfer_credentials_t item = credentials(i, 0); CHECK(profiles_remember(&profiles, &item) == ESP_OK); }
    const unsigned deletion[] = {7, 3, 0, 4, 6, 1, 5, 2};
    for (unsigned i = 0; i < 8; ++i) {
        transfer_credentials_t item = credentials(deletion[i], 0);
        CHECK(profiles_forget(&profiles, item.ssid) == ESP_OK && profiles.count == 7 - i);
        CHECK(profiles_find(&profiles, item.ssid) == -1);
        const unsigned char *tail = (const unsigned char *)&profiles.items[profiles.count];
        for (size_t j = 0; j < sizeof(profiles.items[0]); ++j) CHECK(tail[j] == 0);
        connectivity_profiles_t reboot = loaded(); same(&profiles, &reboot);
        unsigned before_writes = writes(); CHECK(profiles_forget(&profiles, item.ssid) == ESP_ERR_NOT_FOUND && writes() == before_writes);
    }
}

static void reject_blob(const connectivity_profiles_t *profiles, size_t size) {
    reset(); transfer_credentials_t old = credentials(99, 0); seed(LEGACY, &old, sizeof(old)); seed(PROFILES, profiles, size);
    test_blob_t before = committed[PROFILES]; connectivity_profiles_t result;
    CHECK(profiles_load(&result) != ESP_OK && !open_handles && !writes());
    CHECK(!memcmp(&committed[PROFILES], &before, sizeof(before)) && operations[LEGACY][OP_OPEN] == 0);
}
static void test_corruption(void) {
    connectivity_profiles_t profiles = empty_profiles();
    reject_blob(&profiles, 0); reject_blob(&profiles, sizeof(profiles) - 1);
    unsigned char oversized[sizeof(profiles) + 1]; memset(oversized, 0, sizeof(oversized)); memcpy(oversized, &profiles, sizeof(profiles));
    reset(); transfer_credentials_t old = credentials(99, 0); seed(LEGACY, &old, sizeof(old)); seed(PROFILES, oversized, sizeof(oversized));
    connectivity_profiles_t result; CHECK(profiles_load(&result) == ESP_ERR_NVS_INVALID_LENGTH);
    CHECK(!writes() && !open_handles && operations[LEGACY][OP_OPEN] == 0);
    profiles.version = 0; reject_blob(&profiles, sizeof(profiles)); profiles.version = 2; reject_blob(&profiles, sizeof(profiles));
    profiles = empty_profiles(); profiles.count = 9; reject_blob(&profiles, sizeof(profiles));
    profiles = empty_profiles(); profiles.enabled = 2; reject_blob(&profiles, sizeof(profiles));
    profiles = empty_profiles(); profiles.count = 1; profiles.items[0].credentials = old; profiles.items[0].auto_join = 2; reject_blob(&profiles, sizeof(profiles));
    profiles.items[0].auto_join = 1; profiles.items[0].credentials.version = 9; reject_blob(&profiles, sizeof(profiles));
    profiles.items[0].credentials = old; memset(profiles.items[0].credentials.ssid, 'a', sizeof(old.ssid)); reject_blob(&profiles, sizeof(profiles));
    profiles.items[0].credentials = old; memset(profiles.items[0].credentials.password, 'a', sizeof(old.password)); reject_blob(&profiles, sizeof(profiles));
    profiles.items[0].credentials = old; strcpy(profiles.items[0].credentials.password, "short"); reject_blob(&profiles, sizeof(profiles));
    profiles.items[0].credentials = old; profiles.count = 2; profiles.items[1] = profiles.items[0]; reject_blob(&profiles, sizeof(profiles));
    reset(); profiles.version = 2; unsigned before_writes = writes(); CHECK(profiles_store(&profiles) == ESP_ERR_INVALID_ARG && writes() == before_writes);
    reset(); old.version = 0; seed(LEGACY, &old, sizeof(old)); CHECK(profiles_load(&result) == ESP_ERR_INVALID_STATE && !writes() && !open_handles);
    reset(); old = credentials(99, 0); seed(LEGACY, &old, sizeof(old) - 1); CHECK(profiles_load(&result) == ESP_ERR_INVALID_STATE && !writes() && !open_handles);
}

int main(void) {
    test_migration(); test_remember_lru_capacity(); test_forget_and_atomic_failures(); test_corruption();
    CHECK(!open_handles);
    printf("connectivity profiles PASS: %u checks, %u scenarios; migration, empty tombstone, dedup/8-cap/LRU, atomic failure/reload, corruption\n", checks, scenarios);
    return 0;
}

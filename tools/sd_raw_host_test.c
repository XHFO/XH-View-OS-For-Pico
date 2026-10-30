/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 *
 * 同源 raw helper 的生命周期、故障清理及格式化调用边界测试；不是物理卡验收。
 * Lifecycle, fault cleanup and format-call boundary tests of the real raw helper; not physical-card acceptance.
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned checks, scenarios;
#define CHECK(condition) do { ++checks; if (!(condition)) { \
    fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); exit(1); \
} } while (0)

// 仅声明 helper 使用的 ABI；所有副作用由下方故障替身记录。/ Declare only the ABI used by the helper; fault doubles record all side effects.
typedef int esp_err_t;
enum { ESP_OK = 0, ESP_FAIL = -1, ESP_ERR_NO_MEM = 0x101, ESP_ERR_INVALID_ARG,
       ESP_ERR_INVALID_STATE, ESP_ERR_NOT_SUPPORTED, ESP_ERR_NOT_FOUND, ESP_ERR_TIMEOUT };
enum { SDMMC_HOST_FLAG_SPI = 1U << 3, SDMMC_HOST_FLAG_DEINIT_ARG = 1U << 5,
       SDMMC_HOST_FLAG_ALLOC_ALIGNED_BUF = 1U << 6 };
typedef struct { unsigned opcode; } sdmmc_command_t;
typedef struct {
    unsigned flags;
    int slot;
    int max_freq_khz;
    esp_err_t (*init)(void);
    esp_err_t (*do_transaction)(int, sdmmc_command_t*);
    union { esp_err_t (*deinit)(void); esp_err_t (*deinit_p)(int); };
} sdmmc_host_t;
typedef struct { int width, clk, cmd, d0; unsigned flags; } sdmmc_slot_config_t;
typedef struct { sdmmc_host_t host; struct { int capacity, sector_size; } csd; } sdmmc_card_t;
typedef uint8_t BYTE;
typedef unsigned int UINT;
typedef int FRESULT;
#define FR_OK 0
#define FF_MULTI_PARTITION 1
#define FF_VOLUMES 2
#define FF_DRV_NOT_USED 0xff
#define FM_FAT32 2
#define FM_SFD 8
typedef struct { BYTE pd, pt; } PARTITION;
typedef struct { BYTE fmt, n_fat; UINT align, n_root; uint32_t au_size; } MKFS_PARM;
static PARTITION VolToPart[FF_VOLUMES];

static esp_err_t sdmmc_host_init_slot(int slot, const sdmmc_slot_config_t* config);
static esp_err_t sdmmc_host_deinit(void);
static esp_err_t sdmmc_card_init(const sdmmc_host_t* host, sdmmc_card_t* card);
static esp_err_t ff_diskio_get_drive(BYTE* drive);
static void ff_diskio_register_sdmmc(BYTE drive, sdmmc_card_t* card);
static void ff_diskio_unregister(BYTE drive);
static FRESULT f_mkfs(const char* path, const MKFS_PARM* options, void* work, UINT bytes);
static void* fake_malloc(size_t bytes);
static void* fake_calloc(size_t count, size_t bytes);
static void fake_free(void* pointer);

#define malloc fake_malloc
#define calloc fake_calloc
#define free fake_free
#include "../components/read_pico/read_pico_sd_raw.h"
#undef malloc
#undef calloc
#undef free

static esp_err_t init_result, slot_result, card_result, deinit_result, drive_result, media_result;
static FRESULT mkfs_result;
static unsigned allocation_calls, fail_allocation, init_calls, slot_calls, card_calls;
static unsigned deinit_calls, slot_deinit_calls, global_deinit_calls, drive_calls;
static unsigned register_calls, unregister_calls, media_calls, mark_calls, mkfs_calls;
static unsigned transaction_attempts, transaction_calls, invalidate_after_transaction;
static esp_err_t transaction_results[8];
static sdmmc_command_t* expected_command;
static void* allocations[8];
static size_t allocation_sizes[8];
static BYTE chosen_drive;
static bool registered[FF_VOLUMES];
static sdmmc_card_t* registration_card;
static sdmmc_card_t* expected_raw;
static sdmmc_host_t expected_host;
static sdmmc_slot_config_t expected_slot;
static char events[128];
static unsigned event_count;
static esp_err_t fake_transaction(int slot, sdmmc_command_t* command);

static void event(char value) { CHECK(event_count + 1 < sizeof(events)); events[event_count++] = value; events[event_count] = 0; }
static void* fake_malloc(size_t bytes) {
    ++allocation_calls;
    event('a');
    if (allocation_calls == fail_allocation) return NULL;
    void* result = malloc(bytes);
    CHECK(result != NULL);
    for (unsigned i = 0; i < 8; ++i) {
        if (!allocations[i]) { allocations[i] = result; allocation_sizes[i] = bytes; return result; }
    }
    CHECK(false);
    return NULL;
}
static void* fake_calloc(size_t count, size_t bytes) {
    void* result = fake_malloc(count * bytes);
    if (result) memset(result, 0, count * bytes);
    return result;
}
static void fake_free(void* pointer) {
    if (!pointer) return;
    event('x');
    for (unsigned i = 0; i < 8; ++i) {
        if (allocations[i] == pointer) { allocations[i] = NULL; allocation_sizes[i] = 0; free(pointer); return; }
    }
    CHECK(false);
}
static void reset(void) {
    ++scenarios;
    for (unsigned i = 0; i < 8; ++i) CHECK(!allocations[i]);
    for (unsigned i = 0; i < FF_VOLUMES; ++i) CHECK(!registered[i]);
    init_result = slot_result = card_result = deinit_result = drive_result = media_result = ESP_OK;
    mkfs_result = FR_OK;
    allocation_calls = fail_allocation = init_calls = slot_calls = card_calls = 0;
    deinit_calls = slot_deinit_calls = global_deinit_calls = drive_calls = 0;
    register_calls = unregister_calls = media_calls = mark_calls = mkfs_calls = 0;
    transaction_attempts = transaction_calls = invalidate_after_transaction = 0;
    chosen_drive = 1;
    registration_card = expected_raw = NULL;
    event_count = 0; events[0] = 0;
    VolToPart[0] = (PARTITION){1, 3};
    VolToPart[1] = (PARTITION){0, 4};
}
static esp_err_t fake_host_init(void) { ++init_calls; event('h'); return init_result; }
static esp_err_t fake_host_deinit(void) { ++deinit_calls; event('c'); return deinit_result; }
static esp_err_t fake_slot_deinit(int slot) { CHECK(slot == expected_host.slot); ++slot_deinit_calls; event('q'); return deinit_result; }
static esp_err_t sdmmc_host_deinit(void) { ++global_deinit_calls; event('g'); return deinit_result; }
static esp_err_t sdmmc_host_init_slot(int slot, const sdmmc_slot_config_t* config) {
    CHECK(slot == expected_host.slot && !memcmp(config, &expected_slot, sizeof(*config)));
    ++slot_calls; event('s'); return slot_result;
}
static esp_err_t sdmmc_card_init(const sdmmc_host_t* host, sdmmc_card_t* card) {
    CHECK(host->flags == expected_host.flags && host->slot == expected_host.slot);
    CHECK(host->max_freq_khz == expected_host.max_freq_khz);
    ++card_calls; event('i');
    if (card_result == ESP_OK) { card->host = *host; card->csd.capacity = 134217728; card->csd.sector_size = 512; }
    return card_result;
}
static esp_err_t ff_diskio_get_drive(BYTE* drive) {
    ++drive_calls; event('d');
    if (drive_result == ESP_OK) *drive = chosen_drive;
    return drive_result;
}
static void ff_diskio_register_sdmmc(BYTE drive, sdmmc_card_t* card) {
    CHECK(drive == chosen_drive && drive < FF_VOLUMES && !registered[drive]);
    CHECK(card && card != expected_raw);
    CHECK(card->csd.capacity == expected_raw->csd.capacity && card->csd.sector_size == 512);
    registered[drive] = true; registration_card = card; ++register_calls; event('r');
}
static void ff_diskio_unregister(BYTE drive) {
    CHECK(drive == chosen_drive && registered[drive]);
    registered[drive] = false; ++unregister_calls; event('u');
}
static esp_err_t operation_media_status(void) {
    CHECK(register_calls == 1 && (mark_calls == 0 ? mkfs_calls == 0 : mkfs_calls == 1));
    ++media_calls; event('v'); return media_result;
}
static void mark_format_started(void) {
    CHECK(media_calls == 1 && media_result == ESP_OK && mkfs_calls == 0 && mark_calls == 0);
    ++mark_calls; event('b');
}
static FRESULT f_mkfs(const char* path, const MKFS_PARM* options, void* work, UINT bytes) {
    CHECK(mark_calls == 1 && media_calls == 1 && media_result == ESP_OK);
    CHECK(event_count >= 2 && events[event_count - 1] == 'b' && events[event_count - 2] == 'v');
    CHECK(path[0] == (char)('0' + chosen_drive) && path[1] == ':' && path[2] == 0);
    CHECK(options->fmt == FM_FAT32 && !(options->fmt & FM_SFD));
    CHECK(options->n_fat == 2 && options->au_size == 0 && options->align == 0 && options->n_root == 0);
    CHECK(VolToPart[chosen_drive].pd == chosen_drive && VolToPart[chosen_drive].pt == 0);
    CHECK(work && bytes == 4096 && allocations[0] == work && allocation_sizes[0] == 4096);
    CHECK(registered[chosen_drive] && registration_card != expected_raw);
    ++mkfs_calls; event('f');
    CHECK(transaction_attempts <= sizeof(transaction_results) / sizeof(*transaction_results));
    for (unsigned i = 0; i < transaction_attempts; ++i) {
        sdmmc_command_t command = {.opcode = i};
        expected_command = &command;
        transaction_results[i] = registration_card->host.do_transaction(registration_card->host.slot, &command);
    }
    return mkfs_result;
}
static esp_err_t fake_transaction(int slot, sdmmc_command_t* command) {
    CHECK(expected_raw && slot == expected_raw->host.slot && command == expected_command);
    ++transaction_calls; event('t');
    if (transaction_calls == invalidate_after_transaction) media_result = ESP_ERR_NOT_FOUND;
    return ESP_OK;
}
static sdmmc_host_t host_config(bool slot_deinit) {
    sdmmc_host_t config = {.flags = 1, .slot = 1, .max_freq_khz = 40000, .init = fake_host_init,
                          .do_transaction = fake_transaction};
    if (slot_deinit) { config.flags |= SDMMC_HOST_FLAG_DEINIT_ARG; config.deinit_p = fake_slot_deinit; }
    else config.deinit = fake_host_deinit;
    expected_host = config;
    expected_slot = (sdmmc_slot_config_t){.width = 1, .clk = 38, .cmd = 42, .d0 = 44, .flags = 3};
    return config;
}
static void check_format_cleanup(void) {
    CHECK(register_calls == 1 && unregister_calls == 1);
    CHECK(VolToPart[0].pd == 1 && VolToPart[0].pt == 3);
    CHECK(VolToPart[1].pd == 0 && VolToPart[1].pt == 4);
    CHECK(!allocations[0] && !registered[0] && !registered[1]);
    CHECK(sd_raw_format_transaction == NULL);
}

int main(void) {
    sdmmc_card_t* owned = NULL;
    sdmmc_card_t raw = {.host = {.slot = 1, .max_freq_khz = 40000, .do_transaction = fake_transaction},
                        .csd = {.capacity = 134217728, .sector_size = 512}};
    for (unsigned slot_mode = 0; slot_mode < 2; ++slot_mode) {
        reset(); sdmmc_host_t host = host_config(slot_mode);
        CHECK(sd_raw_open(NULL, &expected_slot, &owned) == ESP_ERR_INVALID_ARG);
        CHECK(sd_raw_open(&host, NULL, &owned) == ESP_ERR_INVALID_ARG);
        CHECK(sd_raw_open(&host, &expected_slot, NULL) == ESP_ERR_INVALID_ARG);
        sdmmc_host_t bad = host; bad.init = NULL;
        CHECK(sd_raw_open(&bad, &expected_slot, &owned) == ESP_ERR_INVALID_ARG);
        bad = host; bad.deinit = NULL;
        CHECK(sd_raw_open(&bad, &expected_slot, &owned) == ESP_ERR_INVALID_ARG);
        CHECK(!owned && !allocation_calls);
        for (unsigned flag = SDMMC_HOST_FLAG_SPI; flag <= SDMMC_HOST_FLAG_ALLOC_ALIGNED_BUF; flag *= 8) {
            bad = host; bad.flags |= flag;
            CHECK(sd_raw_open(&bad, &expected_slot, &owned) == ESP_ERR_NOT_SUPPORTED);
        }
        CHECK(!allocation_calls);

        reset(); host = host_config(slot_mode); fail_allocation = 1;
        CHECK(sd_raw_open(&host, &expected_slot, &owned) == ESP_ERR_NO_MEM);
        CHECK(!owned && !init_calls && !slot_calls && !card_calls);

        reset(); host = host_config(slot_mode); init_result = ESP_ERR_TIMEOUT;
        CHECK(sd_raw_open(&host, &expected_slot, &owned) == ESP_ERR_TIMEOUT);
        CHECK(!owned && init_calls == 1 && !slot_calls && !deinit_calls && !slot_deinit_calls && !global_deinit_calls);

        reset(); host = host_config(slot_mode); slot_result = ESP_ERR_TIMEOUT; deinit_result = ESP_FAIL;
        CHECK(sd_raw_open(&host, &expected_slot, &owned) == ESP_ERR_TIMEOUT);
        CHECK(!owned && init_calls == 1 && slot_calls == 1 && !card_calls);
        CHECK(slot_mode ? global_deinit_calls == 1 && !slot_deinit_calls : deinit_calls == 1 && !global_deinit_calls);

        reset(); host = host_config(slot_mode); card_result = ESP_ERR_TIMEOUT; deinit_result = ESP_FAIL;
        CHECK(sd_raw_open(&host, &expected_slot, &owned) == ESP_ERR_TIMEOUT);
        CHECK(!owned && init_calls == 1 && slot_calls == 1 && card_calls == 1);
        CHECK(slot_mode ? slot_deinit_calls == 1 : deinit_calls == 1);
        CHECK(!global_deinit_calls);

        for (unsigned close_fails = 0; close_fails < 2; ++close_fails) {
            reset(); host = host_config(slot_mode);
            CHECK(sd_raw_open(&host, &expected_slot, &owned) == ESP_OK && owned);
            sdmmc_card_t* original = owned;
            CHECK(sd_raw_open(&host, &expected_slot, &owned) == ESP_ERR_INVALID_STATE && owned == original);
            CHECK(init_calls == 1 && card_calls == 1 && allocation_calls == 1);
            deinit_result = close_fails ? ESP_FAIL : ESP_OK;
            CHECK(sd_raw_close(&owned) == deinit_result && !owned);
            CHECK(slot_mode ? slot_deinit_calls == 1 : deinit_calls == 1);
            CHECK(sd_raw_close(&owned) == ESP_OK);
            CHECK(sd_raw_close(NULL) == ESP_ERR_INVALID_ARG);
            CHECK(!allocations[0]);
        }
    }

    reset(); CHECK(sd_raw_format_fat32(NULL) == ESP_ERR_INVALID_ARG);
    for (int capacity = -1; capacity <= 0; ++capacity) {
        reset(); raw.csd.capacity = capacity;
        CHECK(sd_raw_format_fat32(&raw) == ESP_ERR_NOT_SUPPORTED && !allocation_calls && !mark_calls);
    }
    raw.csd.capacity = 134217728;
    reset(); raw.csd.sector_size = 4096;
    CHECK(sd_raw_format_fat32(&raw) == ESP_ERR_NOT_SUPPORTED && !allocation_calls);
    raw.csd.sector_size = 512;
    reset(); raw.host.do_transaction = NULL;
    CHECK(sd_raw_format_fat32(&raw) == ESP_ERR_INVALID_STATE && !allocation_calls && !mark_calls && !mkfs_calls);
    raw.host.do_transaction = fake_transaction;

    reset(); expected_raw = &raw; fail_allocation = 1;
    CHECK(sd_raw_format_fat32(&raw) == ESP_ERR_NO_MEM && !drive_calls && !mark_calls);
    reset(); expected_raw = &raw; drive_result = ESP_ERR_NOT_FOUND;
    CHECK(sd_raw_format_fat32(&raw) == ESP_ERR_NOT_FOUND && !register_calls && !mark_calls && !allocations[0]);
    for (unsigned drive = FF_VOLUMES; drive <= 255; drive += 253) {
        reset(); expected_raw = &raw; chosen_drive = drive;
        CHECK(sd_raw_format_fat32(&raw) == ESP_ERR_INVALID_STATE && !register_calls && !mark_calls && !allocations[0]);
    }
    reset(); expected_raw = &raw; media_result = ESP_ERR_NOT_FOUND;
    CHECK(sd_raw_format_fat32(&raw) == ESP_ERR_NOT_FOUND && !mark_calls && !mkfs_calls);
    CHECK(!strcmp(events, "adrvux")); check_format_cleanup();

    reset(); expected_raw = &raw; transaction_attempts = 4;
    CHECK(sd_raw_format_fat32(&raw) == ESP_OK);
    CHECK(transaction_calls == 4 && media_calls == 5);
    for (unsigned i = 0; i < transaction_attempts; ++i) CHECK(transaction_results[i] == ESP_OK);
    CHECK(raw.host.do_transaction == fake_transaction && raw.host.max_freq_khz == 40000);
    check_format_cleanup();

    reset(); expected_raw = &raw; transaction_attempts = 4; invalidate_after_transaction = 1;
    // 即便假 FatFs 忽略事务错误返回 OK，helper 也必须返回第一次媒体错误。
    // Even if fake FatFs ignores transaction errors and returns OK, preserve the first media error.
    CHECK(sd_raw_format_fat32(&raw) == ESP_ERR_NOT_FOUND);
    CHECK(transaction_calls == 1 && media_calls == 3 && mark_calls == 1 && mkfs_calls == 1);
    CHECK(transaction_results[0] == ESP_OK);
    for (unsigned i = 1; i < transaction_attempts; ++i) CHECK(transaction_results[i] == ESP_ERR_NOT_FOUND);
    CHECK(raw.host.do_transaction == fake_transaction && raw.host.max_freq_khz == 40000);
    check_format_cleanup();
    sdmmc_command_t after = {0};
    CHECK(sd_raw_format_guarded_transaction(raw.host.slot, &after) == ESP_ERR_INVALID_STATE);
    CHECK(transaction_calls == 1 && media_calls == 3);

    // 穷举 FatFs 返回码与两个 drive，重复调用检查清理及稳定地址。/ Enumerate FatFs results and both drives; repeated calls check cleanup and stable addresses.
    sdmmc_card_t* stable = NULL;
    for (unsigned repetition = 0; repetition < 5; ++repetition) {
        for (unsigned drive = 0; drive < FF_VOLUMES; ++drive) {
            for (int result = 0; result < 20; ++result) {
                reset(); expected_raw = &raw; chosen_drive = drive; mkfs_result = result;
                CHECK(sd_raw_format_fat32(&raw) == (result == FR_OK ? ESP_OK : ESP_FAIL));
                CHECK(media_calls == 1 && mark_calls == 1 && mkfs_calls == 1);
                CHECK(!strcmp(events, "adrvbfux")); check_format_cleanup();
                if (stable) CHECK(registration_card == stable);
                stable = registration_card;
                CHECK(raw.csd.capacity == 134217728 && raw.csd.sector_size == 512);
            }
        }
    }
    reset();
    printf("PASS: %u checks, %u scenarios; real raw helper, fake SDMMC/FatFs APIs, no device I/O\n", checks, scenarios);
    return 0;
}

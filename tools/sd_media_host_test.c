/*
 * SPDX-License-Identifier: Apache-2.0
 * SD 生命周期、只读识别、显式格式化与 USB 租约的故障注入；不访问真实卡。
 * Fault injection for SD lifecycle, read-only identification, explicit formatting and USB leases; no physical card I/O.
 */
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

typedef int esp_err_t;
enum {
    ESP_OK = 0,
    ESP_FAIL = -1,
    ESP_ERR_INVALID_ARG = 1,
    ESP_ERR_INVALID_STATE,
    ESP_ERR_TIMEOUT,
    ESP_ERR_NOT_FOUND,
    ESP_ERR_NOT_FINISHED,
    ESP_ERR_NO_MEM,
    ESP_ERR_NOT_SUPPORTED
};
#define GPIO_NUM_38 38
#define GPIO_NUM_42 42
#define GPIO_NUM_44 44
#define GPIO_NUM_NC -1
#define SDMMC_FREQ_HIGHSPEED 40000
#define SDMMC_SLOT_FLAG_INTERNAL_PULLUP 1
#define SDMMC_HOST_FLAG_SPI 8
#define SDMMC_HOST_FLAG_DEINIT_ARG 32
#define SDMMC_HOST_FLAG_ALLOC_ALIGNED_BUF 64
static int fake_host_init(void), fake_host_deinit(void);
typedef struct {
    int unused;
} sdmmc_command_t;
static int fake_transaction(int slot, sdmmc_command_t* command) {
    (void)slot;
    (void)command;
    return ESP_OK;
}
typedef struct {
    unsigned flags;
    int slot, max_freq_khz;
    int (*init)(void);
    int (*do_transaction)(int, sdmmc_command_t*);
    union {
        int (*deinit)(void);
        int (*deinit_p)(int);
    };
} sdmmc_host_t;
#define SDMMC_HOST_DEFAULT() \
    ((sdmmc_host_t){         \
        .init = fake_host_init, .deinit = fake_host_deinit, .do_transaction = fake_transaction })
#define SDMMC_SLOT_CONFIG_DEFAULT() ((sdmmc_slot_config_t){ 0 })
typedef struct {
    int width, clk, cmd, d0, d1, d2, d3, cd, wp;
    unsigned flags;
} sdmmc_slot_config_t;
typedef struct {
    bool format_if_mount_failed;
    int max_files, allocation_unit_size;
} esp_vfs_fat_sdmmc_mount_config_t;
typedef struct {
    sdmmc_host_t host;
    struct {
        char name[8];
    } cid;
    struct {
        int capacity, sector_size;
    } csd;
} sdmmc_card_t;
typedef uint8_t BYTE;
typedef unsigned UINT;
typedef int FRESULT;
typedef struct {
    BYTE fmt, n_fat;
    UINT align, n_root, au_size;
} MKFS_PARM;
typedef struct {
    BYTE pd, pt;
} PARTITION;
#define FR_OK 0
#define FR_DISK_ERR 1
#define FM_FAT32 2
#define FF_MULTI_PARTITION 1
#define FF_VOLUMES 2
#define FF_DRV_NOT_USED 255
static PARTITION VolToPart[FF_VOLUMES];
typedef pthread_mutex_t portMUX_TYPE;
#define portMUX_INITIALIZER_UNLOCKED PTHREAD_MUTEX_INITIALIZER
#define portENTER_CRITICAL(p) assert(!pthread_mutex_lock(p))
#define portEXIT_CRITICAL(p) assert(!pthread_mutex_unlock(p))
typedef int BaseType_t;
#define pdPASS 1
#define pdMS_TO_TICKS(x) (x)
#define ESP_LOGW(...) ((void)0)
#define ESP_LOGI(...) ((void)0)
#define MALLOC_CAP_DMA 1

static bool present = true, driver_live, fat_mounted, filesystem_valid = true, confirmation;
static bool drop_mount, drop_format, drop_raw, drop_write, fail_malloc, fail_calloc;
static bool detached_for_close, drop_on_collision, drop_release_commit, drop_acquire_commit;
static int cd_after_raw, info_calls;
static int detect_result;
static int mounts, unmounts, formats, raw_opens, raw_closes, mkdirs, reads, writes, unlinks, opens,
    collisions;
static int mount_result, unmount_result, info_result, mkfs_result, raw_result, read_sector_result,
    create_result;
static uint64_t fat_total = 1048576, fat_free = 524288;
static sdmmc_card_t mock_card = { .cid = { "MOCK" }, .csd = { 2048, 512 } };
static sdmmc_card_t* registered;
static bool fd_owned, path_created, file_readonly;
static uint8_t file_data[256];
static enum {
    FILE_OK,
    FILE_OPEN,
    FILE_WRITE,
    FILE_SYNC,
    FILE_REOPEN,
    FILE_SEEK,
    FILE_READ,
    FILE_MISMATCH,
    FILE_CLOSE,
    FILE_UNLINK
} file_failure;
static enum { BOOT_UNKNOWN, BOOT_EXFAT, BOOT_FAT32, BOOT_MBR_EXFAT, BOOT_BAD_MBR } boot_kind;
static void (*pending)(void*);
#include "read_pico_sd.h"
static void assert_busy_snapshot(void) {
    read_pico_sd_info_t info;
    int err = read_pico_sd_get_info(&info);
    assert(err == ESP_ERR_NOT_FINISHED || err == ESP_ERR_NOT_FOUND || err == ESP_ERR_INVALID_STATE);
    assert(!info.mounted && !info.capacity_bytes && !info.free_bytes && !info.needs_format);
    assert(read_pico_sd_format() != ESP_OK);
    assert(read_pico_sd_remount() != ESP_OK);
}
static int read_pico_sd_detect(bool* detected) {
    if (detect_result) return detect_result;
    if (drop_acquire_commit && cd_after_raw && ++cd_after_raw == 4) present = false;
    *detected = present;
    return ESP_OK;
}
static void vTaskDelay(int n) {
    (void)n;
}
static void vTaskDelete(void* p) {
    (void)p;
}
static int xTaskCreate(void (*f)(void*), const char* n, int z, void* a, int pr, void* h) {
    (void)n;
    (void)z;
    (void)a;
    (void)pr;
    (void)h;
    assert(!pending);
    if (create_result) return 0;
    pending = f;
    return pdPASS;
}
static int esp_vfs_fat_info(const char* p, uint64_t* total, uint64_t* freeb) {
    assert(!strcmp(p, "/sdcard") && driver_live && fat_mounted);
    assert_busy_snapshot();
    *total = fat_total;
    *freeb = fat_free;
    if (drop_release_commit && ++info_calls == 2) present = false;
    return info_result;
}
static int esp_vfs_fat_sdmmc_mount(
    const char* p, const sdmmc_host_t* h, const sdmmc_slot_config_t* s,
    const esp_vfs_fat_sdmmc_mount_config_t* c, sdmmc_card_t** out) {
    assert(!strcmp(p, "/sdcard") && !driver_live && !*out);
    assert(
        h->max_freq_khz == 40000 && s->width == 1 && s->clk == 38 && s->cmd == 42 && s->d0 == 44);
    assert(!c->format_if_mount_failed);
    assert_busy_snapshot();
    mounts++;
    if (mount_result) return mount_result;
    if (!filesystem_valid) return ESP_FAIL;
    *out = &mock_card;
    driver_live = fat_mounted = true;
    if (drop_mount) present = false;
    return ESP_OK;
}
static int esp_vfs_fat_sdcard_unmount(const char* p, sdmmc_card_t* c) {
    assert(!strcmp(p, "/sdcard") && c == &mock_card && driver_live);
    assert_busy_snapshot();
    unmounts++;
    driver_live = fat_mounted = false;
    return unmount_result;
}
static int fake_host_init(void) {
    assert(!driver_live);
    assert_busy_snapshot();
    raw_opens++;
    if (raw_result) return raw_result;
    driver_live = true;
    return ESP_OK;
}
static int fake_host_deinit(void) {
    assert(driver_live && !fat_mounted);
    raw_closes++;
    driver_live = false;
    return ESP_OK;
}
static int sdmmc_host_deinit(void) {
    return fake_host_deinit();
}
static int sdmmc_host_init_slot(int slot, const sdmmc_slot_config_t* s) {
    (void)slot;
    assert(driver_live && s->width == 1);
    return ESP_OK;
}
static int sdmmc_card_init(const sdmmc_host_t* h, sdmmc_card_t* c) {
    *c = mock_card;
    c->host = *h;
    if (drop_raw) present = false;
    if (drop_acquire_commit) cd_after_raw = 1;
    return ESP_OK;
}
static int sdmmc_read_sectors(sdmmc_card_t* c, void* b, size_t start, size_t count) {
    assert(c && driver_live && !fat_mounted && count == 1);
    reads++;
    if (read_sector_result) return read_sector_result;
    uint8_t* p = b;
    memset(p, 0, 512);
    p[510] = 0x55;
    p[511] = 0xaa;
    if (boot_kind == BOOT_EXFAT || (boot_kind == BOOT_MBR_EXFAT && start == 8))
        memcpy(p + 3, "EXFAT   ", 8);
    if (boot_kind == BOOT_FAT32) memcpy(p + 82, "FAT32   ", 8);
    if ((boot_kind == BOOT_MBR_EXFAT || boot_kind == BOOT_BAD_MBR) && !start) {
        p[450] = 7;
        p[454] = boot_kind == BOOT_BAD_MBR ? 0xff : 8;
        p[455] = boot_kind == BOOT_BAD_MBR ? 0xff : 0;
        p[456] = boot_kind == BOOT_BAD_MBR ? 0xff : 0;
        p[457] = boot_kind == BOOT_BAD_MBR ? 0xff : 0;
        p[458] = 1;
    }
    return ESP_OK;
}
static int ff_diskio_get_drive(BYTE* out) {
    *out = 0;
    return ESP_OK;
}
static void ff_diskio_register_sdmmc(BYTE d, sdmmc_card_t* c) {
    assert(!d && !registered && driver_live && !fat_mounted);
    registered = c;
}
static void ff_diskio_unregister(BYTE d) {
    assert(!d && registered);
    registered = NULL;
}
static BYTE ff_diskio_get_pdrv_card(const sdmmc_card_t* c) {
    assert(c == &mock_card && driver_live);
    return 0;
}
static FRESULT f_mount(void* fs, const char* path, int option) {
    assert(!fs && !strcmp(path, "0:") && !option);
    detached_for_close = true;
    fat_mounted = false;
    return FR_OK;
}
static FRESULT f_mkfs(const char* path, const MKFS_PARM* o, void* work, UINT length) {
    assert(
        !strcmp(path, "0:") && work && length == 4096 && confirmation && driver_live &&
        !fat_mounted && registered);
    assert(
        o->fmt == FM_FAT32 && o->n_fat == 2 && !o->au_size && !VolToPart[0].pd && !VolToPart[0].pt);
    assert_busy_snapshot();
    formats++;
    if (drop_format) present = false;
    if (!mkfs_result) filesystem_valid = true;
    return mkfs_result;
}
static void* fake_malloc(size_t n) {
    return fail_malloc ? NULL : malloc(n);
}
static void* fake_calloc(size_t n, size_t s) {
    return fail_calloc ? NULL : calloc(n, s);
}
static void* heap_caps_malloc(size_t n, int caps) {
    assert(caps == MALLOC_CAP_DMA);
    return fake_malloc(n);
}
static int fake_mkdir(const char* p, int mode) {
    (void)p;
    (void)mode;
    assert(driver_live && fat_mounted);
    mkdirs++;
    return 0;
}
static int fake_open(const char* p, int flags, ...) {
    assert(!strncmp(p, "/sdcard/.pico-check-", 20) && driver_live && fat_mounted && !fd_owned);
    assert_busy_snapshot();
    if (flags == O_RDONLY) {
        assert(path_created);
        if (file_failure == FILE_REOPEN) return -1;
        fd_owned = file_readonly = true;
        return 42;
    }
    assert((flags & O_EXCL) && (flags & O_CREAT));
    opens++;
    if (collisions) {
        collisions--;
        errno = EEXIST;
        if (drop_on_collision) present = false;
        return -1;
    }
    if (file_failure == FILE_OPEN) {
        errno = EROFS;
        return -1;
    }
    fd_owned = path_created = true;
    file_readonly = false;
    return 42;
}
static ssize_t fake_write(int fd, const void* p, size_t n) {
    assert(fd == 42 && fd_owned && !file_readonly && n == 256);
    writes++;
    if (drop_write) present = false;
    if (file_failure == FILE_WRITE) return -1;
    memcpy(file_data, p, n);
    return n;
}
static int fake_fsync(int fd) {
    assert(fd == 42 && fd_owned);
    return file_failure == FILE_SYNC ? -1 : 0;
}
static off_t fake_lseek(int fd, off_t offset, int mode) {
    assert(fd == 42 && fd_owned && !offset && mode == SEEK_SET);
    return file_failure == FILE_SEEK ? -1 : 0;
}
static ssize_t fake_read(int fd, void* p, size_t n) {
    assert(fd == 42 && fd_owned && file_readonly && n == 256);
    if (file_failure == FILE_READ) return -1;
    memcpy(p, file_data, n);
    if (file_failure == FILE_MISMATCH) ((uint8_t*)p)[3] ^= 1;
    return n;
}
static int fake_close(int fd) {
    assert(fd == 42 && fd_owned);
    if (!present) assert(detached_for_close);
    fd_owned = false;
    return file_failure == FILE_CLOSE ? -1 : 0;
}
static int fake_unlink(const char* p) {
    assert(!fd_owned && present && !strncmp(p, "/sdcard/.pico-check-", 20));
    unlinks++;
    path_created = false;
    return file_failure == FILE_UNLINK ? -1 : 0;
}
#define malloc fake_malloc
#define calloc fake_calloc
#define mkdir fake_mkdir
#define open fake_open
#define write fake_write
#define fsync fake_fsync
#define lseek fake_lseek
#define read fake_read
#define close fake_close
#define unlink fake_unlink
#include "../components/read_pico/read_pico_sd.c"
#undef malloc
#undef calloc
static void finish_probe(void) {
    assert(pending);
    void (*f)(void*) = pending;
    pending = NULL;
    f(NULL);
}
static void reset_fixture(void) {
    assert(!pending && !fd_owned && !registered);
    if (card || exported_card) {
        assert(driver_live);
        driver_live = false;
    }
    if (exported_card) free(exported_card);
    card = exported_card = NULL;
    exported = false;
    probe_state = 0;
    media_invalidated = false;
    cd_observed = false;
    last_cd = false;
    media_generation = 0;
    memset(&cached_info, 0, sizeof(cached_info));
    present = true;
    drop_mount = drop_raw = drop_format = drop_write = false;
    driver_live = fat_mounted = false;
    filesystem_valid = true;
    confirmation = false;
    fail_malloc = fail_calloc = false;
    mounts = unmounts = formats = raw_opens = raw_closes = mkdirs = reads = writes = unlinks =
        opens = collisions = 0;
    mount_result = unmount_result = info_result = mkfs_result = raw_result = read_sector_result =
        create_result = 0;
    fat_total = 1048576;
    fat_free = 524288;
    file_failure = FILE_OK;
    boot_kind = BOOT_UNKNOWN;
    detached_for_close = drop_on_collision = drop_release_commit = drop_acquire_commit = false;
    cd_after_raw = info_calls = 0;
    detect_result = ESP_OK;
    path_created = file_readonly = false;
}
static void mount_fixture(void) {
    assert(read_pico_sd_start_probe() == ESP_ERR_NOT_FINISHED);
    finish_probe();
    read_pico_sd_info_t i;
    assert(read_pico_sd_get_info(&i) == ESP_OK && i.mounted);
}
static void* snapshot_reader(void* arg) {
    (void)arg;
    for (int n = 0; n < 20000; n++) {
        read_pico_sd_info_t i;
        int e = read_pico_sd_get_info(&i);
        if (!e) assert(i.mounted && i.capacity_bytes == 1048576 && i.free_bytes == 524288);
    }
    return NULL;
}
static void detector_error_tests(void) {
    reset_fixture();
    read_pico_sd_info_t before, after;
    detect_result = ESP_ERR_TIMEOUT;
    assert(read_pico_sd_get_info(&after) == ESP_ERR_NOT_FINISHED && !after.mounted);
    assert(read_pico_sd_start_probe() == ESP_ERR_TIMEOUT && !pending && !mounts);
    detect_result = ESP_OK;
    mount_fixture();
    assert(read_pico_sd_get_info(&before) == ESP_OK);
    detect_result = ESP_ERR_TIMEOUT;
    for (int n = 0; n < 1000; n++) {
        assert(read_pico_sd_get_info(&after) == ESP_OK);
        assert(after.mounted && after.present && after.media_generation == before.media_generation);
        assert(after.capacity_bytes == before.capacity_bytes && !media_invalidated);
    }
    sdmmc_card_t* lease = NULL;
    assert(read_pico_sd_remount() == ESP_ERR_TIMEOUT && !unmounts);
    assert(read_pico_sd_format() == ESP_ERR_TIMEOUT && !formats);
    assert(read_pico_sd_verify() == ESP_ERR_TIMEOUT && !writes);
    assert(read_pico_sd_raw_acquire(&lease) == ESP_ERR_TIMEOUT && !lease && !exported);
    detect_result = ESP_OK;
    assert(read_pico_sd_get_info(&after) == ESP_OK && after.mounted);
    assert(after.media_generation == before.media_generation && mounts == 1);
    present = false;
    assert(read_pico_sd_get_info(&after) == ESP_ERR_NOT_FOUND && !after.mounted && media_invalidated);
    detect_result = ESP_ERR_TIMEOUT;
    assert(read_pico_sd_get_info(&after) == ESP_ERR_NOT_FOUND && !after.mounted);
    detect_result = ESP_OK;
    present = true;
    assert(read_pico_sd_get_info(&after) == ESP_ERR_INVALID_STATE && !after.mounted);
    assert(read_pico_sd_remount() == ESP_ERR_NOT_FINISHED);
    finish_probe();
    assert(read_pico_sd_get_info(&after) == ESP_OK && after.mounted);
    assert(after.media_generation == before.media_generation + 2);
    assert(read_pico_sd_raw_acquire(&lease) == ESP_OK && lease && exported);
    detect_result = ESP_ERR_TIMEOUT;
    assert(read_pico_sd_get_info(&after) == ESP_OK && after.exported && !after.mounted);
    assert(read_pico_sd_raw_release() == ESP_ERR_TIMEOUT && exported && !driver_live);
    assert(read_pico_sd_get_info(&after) == ESP_ERR_TIMEOUT && after.exported && !after.mounted);
    detect_result = ESP_OK;
    assert(read_pico_sd_raw_release() == ESP_OK);
    assert(read_pico_sd_get_info(&after) == ESP_OK && after.mounted && !after.exported);
    puts("PASS CD communication errors: stable mounted snapshot, blocked mutations, real removal retained, safe remount");
}
static void lifecycle_tests(void) {
    reset_fixture();
    read_pico_sd_info_t i;
    assert(read_pico_sd_get_info(NULL) == ESP_ERR_INVALID_ARG);
    mount_fixture();
    assert(
        read_pico_sd_get_info(&i) == ESP_OK && !i.read_verified && !i.write_verified &&
        i.state == READ_PICO_SD_STATE_READY);
    uint32_t gen = i.media_generation;
    present = false;
    assert(read_pico_sd_get_info(&i) == ESP_ERR_NOT_FOUND && i.media_generation > gen);
    assert(!i.mounted && !i.capacity_bytes && !i.name[0] && !i.needs_format && !unmounts && card);
    present = true;
    assert(
        read_pico_sd_get_info(&i) == ESP_ERR_INVALID_STATE &&
        i.state == READ_PICO_SD_STATE_NEEDS_REMOUNT);
    assert(read_pico_sd_format() == ESP_ERR_INVALID_STATE && !formats);
    assert(read_pico_sd_remount() == ESP_ERR_NOT_FINISHED);
    finish_probe();
    assert(read_pico_sd_get_info(&i) == ESP_OK);
    assert(read_pico_sd_remount() == ESP_ERR_NOT_FINISHED);
    present = false;
    read_pico_sd_get_info(&i);
    present = true;
    finish_probe();
    assert(read_pico_sd_get_info(&i) == ESP_ERR_INVALID_STATE);
    assert(read_pico_sd_remount() == ESP_ERR_NOT_FINISHED);
    finish_probe();
    pthread_t reader;
    assert(!pthread_create(&reader, NULL, snapshot_reader, NULL));
    for (int n = 0; n < 100; n++) {
        assert(read_pico_sd_remount() == ESP_ERR_NOT_FINISHED);
        finish_probe();
    }
    assert(!pthread_join(reader, NULL));
    assert(!formats && !writes);
    assert(read_pico_sd_sync() == ESP_OK);
    assert(read_pico_sd_get_info(&i) == ESP_ERR_INVALID_STATE && !i.mounted);
    reset_fixture();
    create_result = 1;
    assert(read_pico_sd_start_probe() == ESP_ERR_NO_MEM);
    assert(read_pico_sd_get_info(&i) == ESP_ERR_NO_MEM && i.stage == READ_PICO_SD_STAGE_FAILED);
    puts(
        "PASS lifecycle: live CD, generation, stale reinsertion, busy exclusion, concurrency, no "
        "automatic writes/format");
}
static void diagnosis_tests(void) {
    const int errors[] = { ESP_FAIL,          ESP_ERR_INVALID_STATE, ESP_ERR_TIMEOUT,
                           ESP_ERR_NOT_FOUND, ESP_ERR_NO_MEM,        99 };
    read_pico_sd_info_t i;
    for (unsigned n = 0; n < sizeof(errors) / sizeof(errors[0]); n++) {
        reset_fixture();
        mount_result = errors[n];
        assert(read_pico_sd_start_probe() == ESP_ERR_NOT_FINISHED);
        finish_probe();
        assert(read_pico_sd_get_info(&i) == errors[n]);
        assert(!i.mounted && !i.needs_format && !formats && !writes && !driver_live);
    }
    for (int n = BOOT_UNKNOWN; n <= BOOT_BAD_MBR; n++) {
        reset_fixture();
        filesystem_valid = false;
        boot_kind = n;
        assert(read_pico_sd_start_probe() == ESP_ERR_NOT_FINISHED);
        finish_probe();
        assert(read_pico_sd_get_info(&i) == ESP_FAIL);
        assert(
            i.physical_bytes == 1048576 && !i.capacity_bytes && !i.needs_format && !formats &&
            !writes);
        if (n == BOOT_EXFAT || n == BOOT_MBR_EXFAT)
            assert(
                i.filesystem == READ_PICO_SD_FS_EXFAT &&
                i.state == READ_PICO_SD_STATE_UNSUPPORTED_FILESYSTEM);
        else
            assert(i.state == READ_PICO_SD_STATE_FILESYSTEM_ERROR);
        if (n == BOOT_FAT32) assert(i.filesystem == READ_PICO_SD_FS_FAT);
        if (n == BOOT_BAD_MBR) assert(reads == 1);
    }
    reset_fixture();
    filesystem_valid = false;
    read_sector_result = ESP_FAIL;
    assert(read_pico_sd_start_probe() == ESP_ERR_NOT_FINISHED);
    finish_probe();
    read_pico_sd_get_info(&i);
    assert(i.state == READ_PICO_SD_STATE_IO_ERROR);
    puts(
        "PASS diagnosis: mount errors do not suggest erase; raw exFAT/FAT/MBR recognition, range "
        "guards, read errors");
}
static void verify_tests(void) {
    read_pico_sd_info_t i;
    reset_fixture();
    mount_fixture();
    assert(read_pico_sd_verify() == ESP_OK);
    assert(
        read_pico_sd_get_info(&i) == ESP_OK && i.read_verified && i.write_verified && writes == 1 &&
        unlinks == 1);
    for (int n = FILE_OPEN; n <= FILE_UNLINK; n++) {
        reset_fixture();
        mount_fixture();
        file_failure = n;
        assert(read_pico_sd_verify() == ESP_FAIL);
        assert(
            read_pico_sd_get_info(&i) == ESP_FAIL && i.state == READ_PICO_SD_STATE_CHECK_FAILED &&
            !i.mounted && !i.write_verified && !driver_live);
        assert(unlinks == (n == FILE_OPEN ? 0 : 1) && !formats);
    }
    reset_fixture();
    mount_fixture();
    collisions = 32;
    assert(read_pico_sd_verify() == ESP_FAIL && opens == 32 && !unlinks && !writes);
    reset_fixture();
    mount_fixture();
    collisions = 2;
    assert(read_pico_sd_verify() == ESP_OK && opens == 3 && writes == 1 && unlinks == 1);
    reset_fixture();
    mount_fixture();
    drop_write = true;
    assert(read_pico_sd_verify() == ESP_ERR_NOT_FOUND && !unlinks && !driver_live);
    reset_fixture();
    mount_fixture();
    collisions = 3;
    drop_on_collision = true;
    assert(read_pico_sd_verify() == ESP_ERR_NOT_FOUND && opens == 1 && !unlinks && !writes);
    puts(
        "PASS verification: exclusive create, sync/read-back, mismatch and I/O failures, "
        "collisions, owned cleanup, removal guard");
}
static void format_tests(void) {
    read_pico_sd_info_t i;
    for (int n = 0; n < 3; n++) {
        reset_fixture();
        if (n < 2) mount_fixture();
        if (n == 1) assert(read_pico_sd_sync() == ESP_OK);
        if (n == 2) {
            filesystem_valid = false;
            boot_kind = BOOT_EXFAT;
        }
        confirmation = true;
        assert(read_pico_sd_format() == ESP_OK);
        assert(
            read_pico_sd_get_info(&i) == ESP_OK && i.mounted && i.write_verified &&
            i.read_verified && i.format_started);
        assert(
            formats == 1 && mkdirs == 3 && writes == 1 && raw_opens == 1 && raw_closes == 1 &&
            !registered);
    }
    reset_fixture();
    mount_fixture();
    confirmation = true;
    mkfs_result = FR_DISK_ERR;
    assert(read_pico_sd_format() == ESP_FAIL && !card && !driver_live && formats == 1);
    read_pico_sd_get_info(&i);
    assert(i.format_started && !i.needs_format);
    assert(read_pico_sd_start_probe() == ESP_FAIL && formats == 1);
    reset_fixture();
    mount_fixture();
    confirmation = true;
    mount_result = ESP_FAIL;
    assert(read_pico_sd_format() == ESP_FAIL && formats == 1 && !driver_live);
    read_pico_sd_get_info(&i);
    assert(i.format_started);
    reset_fixture();
    mount_fixture();
    confirmation = true;
    fail_malloc = true;
    assert(read_pico_sd_format() == ESP_ERR_NO_MEM && !formats && !driver_live);
    read_pico_sd_get_info(&i);
    assert(!i.format_started);
    reset_fixture();
    mount_fixture();
    confirmation = true;
    fail_calloc = true;
    assert(read_pico_sd_format() == ESP_ERR_NO_MEM && !formats && !driver_live);
    reset_fixture();
    mount_fixture();
    confirmation = true;
    drop_format = true;
    assert(read_pico_sd_format() == ESP_ERR_NOT_FOUND && formats == 1 && !driver_live);
    read_pico_sd_get_info(&i);
    assert(i.format_started && i.state == READ_PICO_SD_STATE_ABSENT);
    reset_fixture();
    mount_fixture();
    confirmation = true;
    drop_raw = true;
    assert(read_pico_sd_format() == ESP_ERR_NOT_FOUND && !formats && !driver_live);
    reset_fixture();
    mount_fixture();
    confirmation = true;
    unmount_result = ESP_FAIL;
    assert(read_pico_sd_format() == ESP_FAIL && !formats && !driver_live);
    for (int n = 0; n < 4; n++) {
        reset_fixture();
        mount_fixture();
        confirmation = true;
        if (!n) info_result = ESP_FAIL;
        if (n == 1) fat_total = 0;
        if (n == 2) fat_total = 1048577;
        if (n == 3) fat_free = 1048577;
        assert(read_pico_sd_format() == ESP_FAIL && formats == 1 && !driver_live);
    }
    puts(
        "PASS format: single explicit FAT32 rebuild, nonformat remount, capacity plus read/write "
        "validation, no retry after failure");
}
static void usb_tests(void) {
    read_pico_sd_info_t i;
    sdmmc_card_t* lease = NULL;
    reset_fixture();
    mount_fixture();
    assert(read_pico_sd_raw_acquire(NULL) == ESP_ERR_INVALID_ARG);
    assert(read_pico_sd_raw_acquire(&lease) == ESP_OK && lease && driver_live && !fat_mounted);
    assert(
        read_pico_sd_get_info(&i) == ESP_OK && i.exported && !i.mounted &&
        i.state == READ_PICO_SD_STATE_EXPORTED && i.physical_bytes == 1048576);
    assert(
        read_pico_sd_start_probe() == ESP_ERR_INVALID_STATE &&
        read_pico_sd_remount() == ESP_ERR_INVALID_STATE &&
        read_pico_sd_sync() == ESP_ERR_INVALID_STATE &&
        read_pico_sd_format() == ESP_ERR_INVALID_STATE &&
        read_pico_sd_verify() == ESP_ERR_INVALID_STATE);
    sdmmc_card_t* second = (void*)1;
    assert(read_pico_sd_raw_acquire(&second) == ESP_ERR_INVALID_STATE && !second);
    mount_result = ESP_FAIL;
    assert(read_pico_sd_raw_release() == ESP_FAIL && !driver_live);
    assert(read_pico_sd_get_info(&i) == ESP_FAIL && i.exported && !i.mounted);
    assert(read_pico_sd_remount() == ESP_ERR_INVALID_STATE);
    mount_result = ESP_OK;
    assert(read_pico_sd_raw_release() == ESP_OK);
    assert(read_pico_sd_get_info(&i) == ESP_OK && !i.exported && i.mounted && i.write_verified);
    assert(raw_closes == 1 && !formats);
    assert(read_pico_sd_raw_release() == ESP_ERR_INVALID_STATE);
    reset_fixture();
    mount_fixture();
    assert(read_pico_sd_raw_acquire(&lease) == ESP_OK);
    file_failure = FILE_SYNC;
    assert(read_pico_sd_raw_release() == ESP_FAIL);
    read_pico_sd_get_info(&i);
    assert(i.exported && !i.mounted);
    file_failure = FILE_OK;
    assert(read_pico_sd_raw_release() == ESP_OK);
    reset_fixture();
    mount_fixture();
    assert(read_pico_sd_raw_acquire(&lease) == ESP_OK);
    present = false;
    assert(read_pico_sd_get_info(&i) == ESP_ERR_NOT_FOUND && i.exported);
    assert(read_pico_sd_raw_release() == ESP_ERR_NOT_FOUND);
    present = true;
    assert(read_pico_sd_raw_release() == ESP_OK && !formats);
    reset_fixture();
    mount_fixture();
    raw_result = ESP_FAIL;
    assert(read_pico_sd_raw_acquire(&lease) == ESP_FAIL && !lease && !driver_live);
    read_pico_sd_get_info(&i);
    assert(!i.exported);
    reset_fixture();
    mount_fixture();
    drop_acquire_commit = true;
    assert(read_pico_sd_raw_acquire(&lease) == ESP_ERR_NOT_FOUND && !lease && !driver_live);
    read_pico_sd_get_info(&i);
    assert(!i.exported && i.state == READ_PICO_SD_STATE_ABSENT);
    reset_fixture();
    mount_fixture();
    assert(read_pico_sd_raw_acquire(&lease) == ESP_OK);
    drop_release_commit = true;
    assert(read_pico_sd_raw_release() == ESP_ERR_NOT_FOUND && !driver_live);
    read_pico_sd_get_info(&i);
    assert(i.exported && !i.mounted);
    drop_release_commit = false;
    present = true;
    assert(read_pico_sd_raw_release() == ESP_OK);
    read_pico_sd_get_info(&i);
    assert(!i.exported && i.mounted);
    puts(
        "PASS USB: exclusive raw ownership, blocked local access, failed release retains lease, "
        "retry/removal recovery, verified return");
}
int main(void) {
    detector_error_tests();
    lifecycle_tests();
    diagnosis_tests();
    verify_tests();
    format_tests();
    usb_tests();
    reset_fixture();
    puts("PASS actual SD implementation host fault injection (no physical media)");
}

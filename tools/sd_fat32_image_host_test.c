/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 *
 * 在临时稀疏软件磁盘上运行真实 ESP-IDF FatFs，验证 FAT32 布局及文件持久性。
 * Run real ESP-IDF FatFs on temporary sparse software disks to check FAT32 layout and file persistence.
 *
 * 冻结：不得访问实体卡或硬件；TRIM 只记录并返回不支持，不模拟物理擦除。
 * Frozen: never access physical cards or hardware; record and decline TRIM without simulating erase.
 */
#define _POSIX_C_SOURCE 200809L
#define _FILE_OFFSET_BITS 64
#include <errno.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "ff.h"
#include "diskio.h"

_Static_assert(FF_FS_EXFAT == 0 && FF_MULTI_PARTITION == 1 && FF_LBA64 == 0,
               "Keep the product's filesystem and partition support");
_Static_assert(FF_SS_SDCARD == 512 && FF_SS_WL == 4096 && FF_USE_DYN_BUFFER == 1,
               "Keep the product's variable sector and dynamic-buffer settings");
_Static_assert(FF_USE_MKFS == 1 && FF_USE_TRIM == 1 && FF_VOLUMES == 2,
               "Keep the product's formatter, TRIM, and volume settings");
_Static_assert(FF_USE_LFN == 0 && FF_FS_REENTRANT == 0,
               "Only host-only LFN and mutex adaptations are expected");
_Static_assert(sizeof(off_t) >= 8, "Sparse disks require 64-bit file offsets");

PARTITION VolToPart[FF_VOLUMES] = {{0, 0}, {1, 0}};
static FILE *image;
static BYTE active_drive;
static DWORD sectors;
static unsigned checks, scenarios, allocations;
static unsigned trim_requests, sync_requests;
static LBA_t trim_first, trim_last;
static uint64_t read_bytes, written_bytes;

#define CHECK(condition) do { \
    ++checks; \
    if (!(condition)) { \
        fprintf(stderr, "FAIL line %d: %s (errno=%d)\n", __LINE__, #condition, errno); \
        exit(EXIT_FAILURE); \
    } \
} while (0)

static void expect_result(FRESULT actual, FRESULT expected, int line) {
    ++checks;
    if (actual != expected) {
        fprintf(stderr, "FAIL line %d: FatFs result %d, expected %d\n", line, actual, expected);
        exit(EXIT_FAILURE);
    }
}
#define FAT_OK(call) expect_result((call), FR_OK, __LINE__)

void *ff_memalloc(size_t size) {
    void *result = malloc(size);
    if (result) ++allocations;
    return result;
}
void ff_memfree(void *pointer) {
    if (pointer) {
        CHECK(allocations != 0);
        --allocations;
        free(pointer);
    }
}
DWORD get_fattime(void) {
    return ((DWORD)(2026 - 1980) << 25) | ((DWORD)10 << 21) | ((DWORD)17 << 16);
}
DSTATUS disk_initialize(BYTE drive) {
    return image && drive == active_drive ? 0 : STA_NOINIT;
}
DSTATUS disk_status(BYTE drive) { return disk_initialize(drive); }

static DRESULT transfer(BYTE drive, void *buffer, LBA_t sector, UINT count, int writing) {
    if (!image || drive != active_drive || !buffer || !count ||
        (uint64_t)sector + count > sectors) return RES_PARERR;
    size_t length = (size_t)count * 512;
    size_t done = 0;
    while (done < length) {
        off_t at = (off_t)((uint64_t)sector * 512 + done);
        ssize_t result = writing ?
            pwrite(fileno(image), (const uint8_t *)buffer + done, length - done, at) :
            pread(fileno(image), (uint8_t *)buffer + done, length - done, at);
        if (result < 0 && errno == EINTR) continue;
        if (result <= 0) return RES_ERROR;
        done += (size_t)result;
    }
    if (writing) written_bytes += length;
    else read_bytes += length;
    return RES_OK;
}
DRESULT disk_read(BYTE drive, BYTE *buffer, LBA_t sector, UINT count) {
    return transfer(drive, buffer, sector, count, 0);
}
DRESULT disk_write(BYTE drive, const BYTE *buffer, LBA_t sector, UINT count) {
    return transfer(drive, (void *)buffer, sector, count, 1);
}
DRESULT disk_ioctl(BYTE drive, BYTE command, void *buffer) {
    if (!image || drive != active_drive) return RES_NOTRDY;
    switch (command) {
    case CTRL_SYNC:
        ++sync_requests;
        return fsync(fileno(image)) == 0 ? RES_OK : RES_ERROR;
    case GET_SECTOR_COUNT:
        if (!buffer) return RES_PARERR;
        *(DWORD *)buffer = sectors;
        return RES_OK;
    case GET_SECTOR_SIZE:
        if (!buffer) return RES_PARERR;
        *(WORD *)buffer = 512;
        return RES_OK;
    case GET_BLOCK_SIZE:
        // 与 SDMMC 适配器相同，不支持查询擦除块大小。/ Like the SDMMC adapter, decline erase-block-size queries.
        return RES_ERROR;
    case CTRL_TRIM:
        if (!buffer) return RES_PARERR;
        ++trim_requests;
        trim_first = ((LBA_t *)buffer)[0];
        trim_last = ((LBA_t *)buffer)[1];
        CHECK(trim_first <= trim_last && trim_last < sectors);
        // 不模拟擦除，对应不支持 TRIM 的卡。/ Do not simulate erase; model a card without TRIM support.
        return RES_PARERR;
    default:
        return RES_PARERR;
    }
}

static uint16_t le16(const BYTE *p) { return (uint16_t)(p[0] | p[1] << 8); }
static uint32_t le32(const BYTE *p) {
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}
static void store32(BYTE *p, uint32_t value) {
    for (unsigned i = 0; i < 4; ++i) p[i] = (BYTE)(value >> (8 * i));
}

static void seed_old_layout(unsigned seed) {
    BYTE sector[512] = {0};
    sector[510] = 0x55; sector[511] = 0xaa;
    if (seed == 0) {
        // 预置两个旧 MBR 分区，新布局不能保留它们。/ Seed two old MBR entries that the new layout must replace.
        sector[446 + 4] = 0x07;
        store32(sector + 446 + 8, 2048);
        store32(sector + 446 + 12, sectors / 4);
        sector[462 + 4] = 0x83;
        store32(sector + 462 + 8, sectors / 2);
        store32(sector + 462 + 12, sectors / 4);
        CHECK(disk_write(active_drive, sector, 0, 1) == RES_OK);
        memset(sector, 0, sizeof(sector));
        memcpy(sector + 3, "EXFAT   ", 8);
        sector[510] = 0x55; sector[511] = 0xaa;
        CHECK(disk_write(active_drive, sector, 2048, 1) == RES_OK);
    } else {
        // 仅预置不支持的 exFAT VBR 签名，不创建完整 exFAT 卷。/ Seed an unsupported exFAT VBR signature, not a complete exFAT volume.
        memcpy(sector + 3, "EXFAT   ", 8);
        CHECK(disk_write(active_drive, sector, 0, 1) == RES_OK);
    }
}

static DWORD check_layout(void) {
    BYTE mbr[512], boot[512], backup[512], fat_a[512], fat_b[512];
    CHECK(disk_read(active_drive, mbr, 0, 1) == RES_OK);
    CHECK(le16(mbr + 510) == 0xaa55);
    CHECK(mbr[446 + 4] == 0x0c);
    DWORD start = le32(mbr + 446 + 8), length = le32(mbr + 446 + 12);
    CHECK(start == 63);
    // 除 MBR 前导区域外，覆盖整个介质。/ Cover the whole medium apart from the MBR lead-in.
    CHECK((uint64_t)start + length == sectors);
    for (unsigned i = 462; i < 510; ++i) CHECK(mbr[i] == 0);
    CHECK(disk_read(active_drive, boot, start, 1) == RES_OK);
    CHECK(le16(boot + 510) == 0xaa55 && !memcmp(boot + 82, "FAT32   ", 8));
    CHECK(le16(boot + 11) == 512 && boot[16] == 2);
    CHECK(boot[13] != 0 && (boot[13] & (boot[13] - 1)) == 0);
    CHECK(le16(boot + 17) == 0 && le16(boot + 22) == 0);
    CHECK(le32(boot + 28) == start && le32(boot + 32) == length);
    CHECK(le32(boot + 36) != 0 && le32(boot + 44) == 2);
    CHECK(disk_read(active_drive, backup, start + le16(boot + 50), 1) == RES_OK);
    CHECK(!memcmp(boot, backup, sizeof(boot)));
    DWORD fat_start = start + le16(boot + 14);
    CHECK(disk_read(active_drive, fat_a, fat_start, 1) == RES_OK);
    CHECK(disk_read(active_drive, fat_b, fat_start + le32(boot + 36), 1) == RES_OK);
    CHECK(!memcmp(fat_a, fat_b, sizeof(fat_a)) && le32(fat_a + 8) == 0x0fffffff);
    CHECK(trim_requests == 1 && trim_first == start && trim_last == sectors - 1);
    return (DWORD)boot[13] * 512;
}

static void read_payload(const char *path, const BYTE *expected, size_t length) {
    FIL file = {0};
    BYTE actual[8193]; UINT got = 0;
    CHECK(length == sizeof(actual));
    FAT_OK(f_open(&file, path, FA_READ));
    CHECK(f_size(&file) == length);
    FAT_OK(f_read(&file, actual, sizeof(actual), &got));
    CHECK(got == sizeof(actual) && !memcmp(actual, expected, sizeof(actual)));
    FAT_OK(f_close(&file));
}

static void run_scenario(unsigned gib, unsigned seed) {
    ++scenarios;
    active_drive = (BYTE)((scenarios - 1) % FF_VOLUMES);
    sectors = (DWORD)(((uint64_t)gib << 30) / 512);
    trim_requests = sync_requests = 0;
    read_bytes = written_bytes = 0;
    image = tmpfile();
    CHECK(image != NULL);
    CHECK(ftruncate(fileno(image), (off_t)((uint64_t)sectors * 512)) == 0);
    seed_old_layout(seed);
    char drive[] = {(char)('0' + active_drive), ':', 0};
    char path[] = {(char)('0' + active_drive), ':', '/', 'C', 'H', 'E', 'C', 'K', '.', 'B', 'I', 'N', 0};
    VolToPart[active_drive] = (PARTITION){active_drive, 0};
    FATFS filesystem = {0};
    expect_result(f_mount(&filesystem, drive, 1), FR_NO_FILESYSTEM, __LINE__);
    FAT_OK(f_mount(NULL, drive, 0));
    CHECK(allocations == 0);

    BYTE work[4096];
    const MKFS_PARM options = {.fmt = FM_FAT32, .n_fat = 2, .au_size = 0};
    FAT_OK(f_mkfs(drive, &options, work, sizeof(work)));
    CHECK(sync_requests != 0);
    DWORD cluster_bytes = check_layout();
    FAT_OK(f_mount(&filesystem, drive, 1));
    CHECK(filesystem.fs_type == FS_FAT32 && filesystem.n_fats == 2);
    CHECK(filesystem.volbase == 63 && filesystem.pdrv == active_drive);
    DWORD free_before, free_after;
    FATFS *volume = NULL;
    FAT_OK(f_getfree(drive, &free_before, &volume));
    CHECK(volume == &filesystem && free_before > 0);
    CHECK((uint64_t)(filesystem.n_fatent - 2) * cluster_bytes < (uint64_t)sectors * 512);

    BYTE expected[8193];
    for (size_t i = 0; i < sizeof(expected); ++i) expected[i] = (BYTE)(i * 29 + gib + seed);
    FIL file = {0}; UINT wrote = 0;
    FAT_OK(f_open(&file, path, FA_CREATE_NEW | FA_WRITE));
    FAT_OK(f_write(&file, expected, sizeof(expected), &wrote));
    CHECK(wrote == sizeof(expected));
    unsigned sync_before = sync_requests;
    // FatFs f_sync 经 CTRL_SYNC 调用主机 fsync。/ FatFs f_sync reaches host fsync through CTRL_SYNC.
    FAT_OK(f_sync(&file));
    CHECK(sync_requests > sync_before);
    FAT_OK(f_close(&file));
    read_payload(path, expected, sizeof(expected));
    FAT_OK(f_mount(NULL, drive, 0));
    CHECK(allocations == 0);
    memset(&filesystem, 0, sizeof(filesystem));
    FAT_OK(f_mount(&filesystem, drive, 1));
    read_payload(path, expected, sizeof(expected));
    FAT_OK(f_unlink(path));
    FAT_OK(f_getfree(drive, &free_after, &volume));
    CHECK(free_after == free_before);
    FAT_OK(f_mount(NULL, drive, 0));
    memset(&filesystem, 0, sizeof(filesystem));
    FAT_OK(f_mount(&filesystem, drive, 1));
    FILINFO entry;
    expect_result(f_stat(path, &entry), FR_NO_FILE, __LINE__);
    FAT_OK(f_getfree(drive, &free_after, &volume));
    CHECK(free_after == free_before);
    // 丢卡清理须先使仍存活的 FATFS 对象失效，再关闭脏 FIL；这里只验证 FatFs 顺序，不覆盖 VFS FILE 包装。
    // Media-loss cleanup invalidates the still-live FATFS before closing a dirty FIL; check FatFs ordering, not the VFS FILE wrapper.
    FIL dirty = {0}; UINT dirty_written = 0;
    FAT_OK(f_open(&dirty, path, FA_CREATE_NEW | FA_WRITE));
    FAT_OK(f_write(&dirty, expected, 123, &dirty_written));
    CHECK(dirty_written == 123 && dirty.buf != NULL);
    uint64_t writes_before_invalidation = written_bytes;
    FAT_OK(f_mount(NULL, drive, 0));
    CHECK(written_bytes == writes_before_invalidation);
    expect_result(f_close(&dirty), FR_INVALID_OBJECT, __LINE__);
    CHECK(written_bytes == writes_before_invalidation);
    CHECK(allocations == 0);
    struct stat storage;
    CHECK(fstat(fileno(image), &storage) == 0);
    CHECK((uint64_t)storage.st_size == (uint64_t)sectors * 512);
    CHECK((uint64_t)storage.st_blocks * 512 < (uint64_t)storage.st_size / 8);
    CHECK(fclose(image) == 0);
    image = NULL;
    printf("PASS %u GiB %s: single MBR FAT32, %" PRIu32 "-byte clusters; "
           "write/sync/read/unlink/remount; I/O %.1f MiB written, %.1f MiB read\n",
           gib, seed == 0 ? "old-two-partition" : "exFAT-signature", cluster_bytes,
           (double)written_bytes / 1048576, (double)read_bytes / 1048576);
}

int main(void) {
    const unsigned sizes[] = {32, 64, 128};
    for (unsigned i = 0; i < sizeof(sizes) / sizeof(sizes[0]); ++i)
        for (unsigned seed = 0; seed < 2; ++seed) run_scenario(sizes[i], seed);
    printf("PASS: %u checks, %u software-disk scenarios; actual IDF FatFs. "
           "TRIM recorded but unsupported; no physical SD/USB acceptance.\n", checks, scenarios);
    return 0;
}

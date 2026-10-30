/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 *
 * SDMMC 探测、显式 FAT32 重建与 USB 独占租约；不自动清卡。
 * SDMMC probing, explicit FAT32 rebuilding and an exclusive USB lease; never autoformats.
 */

#include "read_pico_sd.h"

#include <errno.h>
#include <fcntl.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "ff.h"

#include "diskio_sdmmc.h"
#include "driver/sdmmc_host.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_vfs_fat.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "read_pico_board.h"
#include "sdmmc_cmd.h"

#define SD_MOUNT_POINT "/sdcard"
#define SD_PIN_CLK GPIO_NUM_38
#define SD_PIN_CMD GPIO_NUM_42
#define SD_PIN_D0 GPIO_NUM_44

static const char* TAG = "sd_card";
static sdmmc_card_t* card;
static sdmmc_card_t* exported_card;
static bool exported;
static int probe_state;
static portMUX_TYPE state_lock = portMUX_INITIALIZER_UNLOCKED;
static bool media_invalidated;
static bool cd_observed;
static bool last_cd;
static uint32_t media_generation;
static uint32_t check_sequence;
static read_pico_sd_info_t cached_info;
static esp_err_t operation_media_status(void);
static void mark_format_started(void);

#include "read_pico_sd_raw.h"

// CD 失效只影响快照；消费者释放句柄前不得卸载卡。
// CD invalidation changes snapshots only; consumers must close handles before unmounting.
static void observe_media_locked(bool present) {
    if (!cd_observed || present != last_cd) {
        ++media_generation;
        cd_observed = true;
        last_cd = present;
    }
    if (!present && (cached_info.mounted || probe_state == 1 || exported)) media_invalidated = true;
    cached_info.present = present;
    cached_info.media_generation = media_generation;
    cached_info.exported = exported;
    if (!present || media_invalidated) {
        bool erased = cached_info.format_started;
        memset(&cached_info, 0, sizeof(cached_info));
        cached_info.present = present;
        cached_info.media_generation = media_generation;
        cached_info.format_started = erased;
        cached_info.exported = exported;
        cached_info.state = present ? READ_PICO_SD_STATE_NEEDS_REMOUNT : READ_PICO_SD_STATE_ABSENT;
        cached_info.stage = READ_PICO_SD_STAGE_FAILED;
        cached_info.error = present ? ESP_ERR_INVALID_STATE : ESP_ERR_NOT_FOUND;
    }
}

static void set_stage(read_pico_sd_stage_t stage) {
    portENTER_CRITICAL(&state_lock);
    cached_info.stage = stage;
    portEXIT_CRITICAL(&state_lock);
}

static void mark_format_started(void) {
    portENTER_CRITICAL(&state_lock);
    cached_info.format_started = true;
    portEXIT_CRITICAL(&state_lock);
}

static void publish_info(read_pico_sd_info_t* info) {
    bool present = false;
    esp_err_t detected = read_pico_sd_detect(&present);
    portENTER_CRITICAL(&state_lock);
    info->format_started |= cached_info.format_started;
    cached_info = *info;
    cached_info.media_generation = media_generation;
    cached_info.exported = exported;
    if (detected == ESP_OK) observe_media_locked(present);
    probe_state = 2;
    portEXIT_CRITICAL(&state_lock);
}

static uint64_t physical_capacity(const sdmmc_card_t* value) {
    return value && value->csd.capacity > 0 && value->csd.sector_size > 0
               ? (uint64_t)value->csd.capacity * value->csd.sector_size
               : 0;
}

static void fill_info(read_pico_sd_info_t* info, esp_err_t err) {
    memset(info, 0, sizeof(*info));
    portENTER_CRITICAL(&state_lock);
    info->present = cached_info.present;
    portEXIT_CRITICAL(&state_lock);
    esp_err_t detected = read_pico_sd_detect(&info->present);
    info->error = err;
    info->state = READ_PICO_SD_STATE_IO_ERROR;
    info->stage = err == ESP_OK ? READ_PICO_SD_STAGE_COMPLETE : READ_PICO_SD_STAGE_FAILED;
    if (detected != ESP_OK) {
        info->error = detected;
        info->stage = READ_PICO_SD_STAGE_FAILED;
        return;
    }
    if (!info->present) {
        info->error = ESP_ERR_NOT_FOUND;
        info->state = READ_PICO_SD_STATE_ABSENT;
        return;
    }
    if (err != ESP_OK) return;
    if (!card) {
        info->error = ESP_ERR_INVALID_STATE;
        return;
    }
    strlcpy(info->name, card->cid.name, sizeof(info->name));
    info->physical_bytes = physical_capacity(card);
    info->capacity_bytes = info->physical_bytes;
    uint64_t total = 0;
    info->error = esp_vfs_fat_info(SD_MOUNT_POINT, &total, &info->free_bytes);
    if (info->error == ESP_OK && (!total || !info->physical_bytes || total > info->physical_bytes ||
                                  info->free_bytes > total))
        info->error = ESP_FAIL;
    info->mounted = info->error == ESP_OK;
    if (info->mounted) {
        info->filesystem = READ_PICO_SD_FS_FAT;
        info->state = READ_PICO_SD_STATE_READY;
    } else {
        info->stage = READ_PICO_SD_STAGE_FAILED;
        info->capacity_bytes = info->free_bytes = 0;
        info->name[0] = 0;
    }
}

// 共用既有引脚与时序；维护流程不修改硬件协商参数。
// Reuse existing pins and timings; maintenance never changes hardware negotiation parameters.
static void host_configuration(sdmmc_host_t* host, sdmmc_slot_config_t* slot) {
    *host = (sdmmc_host_t)SDMMC_HOST_DEFAULT();
    host->max_freq_khz = SDMMC_FREQ_HIGHSPEED;
    *slot = (sdmmc_slot_config_t)SDMMC_SLOT_CONFIG_DEFAULT();
    slot->width = 1;
    slot->clk = SD_PIN_CLK;
    slot->cmd = SD_PIN_CMD;
    slot->d0 = SD_PIN_D0;
    slot->d1 = slot->d2 = slot->d3 = GPIO_NUM_NC;
    slot->cd = slot->wp = GPIO_NUM_NC;
    slot->flags |= SDMMC_SLOT_FLAG_INTERNAL_PULLUP;
}

static esp_err_t mount_card(void) {
    if (card) return ESP_OK;
    sdmmc_host_t host;
    sdmmc_slot_config_t slot;
    host_configuration(&host, &slot);
    const esp_vfs_fat_sdmmc_mount_config_t config = {
        .format_if_mount_failed = false,
        .max_files = 8,
        .allocation_unit_size = 16 * 1024,
    };
    card = NULL;
    esp_err_t err = esp_vfs_fat_sdmmc_mount(SD_MOUNT_POINT, &host, &slot, &config, &card);
    // 保留既有的首次超时重试，不在检测或重挂中格式化。
    // Preserve the existing initial-timeout retry; probing and remounting never format.
    if (err == ESP_ERR_TIMEOUT && operation_media_status() == ESP_OK) {
        card = NULL;
        vTaskDelay(pdMS_TO_TICKS(200));
        err = operation_media_status();
        if (err == ESP_OK)
            err = esp_vfs_fat_sdmmc_mount(SD_MOUNT_POINT, &host, &slot, &config, &card);
    }
    if (err != ESP_OK) {
        card = NULL;
        ESP_LOGW(TAG, "Mount failed: %s", esp_err_to_name(err));
    }
    return err;
}

static esp_err_t close_card(void) {
    if (!card) return ESP_OK;
    esp_err_t err = esp_vfs_fat_sdcard_unmount(SD_MOUNT_POINT, card);
    // IDF 无论返回值都会释放 card，旧指针不可再用。
    // IDF frees card regardless of its return value; the old pointer must not be reused.
    card = NULL;
    return err;
}

static esp_err_t operation_media_status(void) {
    bool present = false;
    esp_err_t detected = read_pico_sd_detect(&present);
    if (detected != ESP_OK) return detected;
    portENTER_CRITICAL(&state_lock);
    observe_media_locked(present);
    esp_err_t err = !present            ? ESP_ERR_NOT_FOUND
                    : media_invalidated ? ESP_ERR_INVALID_STATE
                                        : ESP_OK;
    portEXIT_CRITICAL(&state_lock);
    return err;
}

static read_pico_sd_filesystem_t boot_filesystem(const uint8_t* data) {
    if (data[510] != 0x55 || data[511] != 0xaa) return READ_PICO_SD_FS_UNKNOWN;
    if (!memcmp(data + 3, "EXFAT   ", 8)) return READ_PICO_SD_FS_EXFAT;
    if (!memcmp(data + 54, "FAT12   ", 8) || !memcmp(data + 54, "FAT16   ", 8) ||
        !memcmp(data + 82, "FAT32   ", 8))
        return READ_PICO_SD_FS_FAT;
    return READ_PICO_SD_FS_UNKNOWN;
}

static uint32_t little_u32(const uint8_t* p) {
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

// 挂载失败后仅原始读取引导扇区；FR_NO_FILESYSTEM 不等于物理故障或空卡。
// After a mount failure, only read raw boot sectors; FR_NO_FILESYSTEM does not mean a bad or empty card.
static void diagnose_filesystem(read_pico_sd_info_t* info) {
    sdmmc_host_t host;
    sdmmc_slot_config_t slot;
    host_configuration(&host, &slot);
    sdmmc_card_t* raw = NULL;
    esp_err_t err = operation_media_status();
    if (err == ESP_OK) err = sd_raw_open(&host, &slot, &raw);
    if (err != ESP_OK) return;
    info->physical_bytes = physical_capacity(raw);
    if (raw->csd.sector_size != 512 || raw->csd.capacity <= 0) {
        (void)sd_raw_close(&raw);
        return;
    }
    uint8_t* sector = heap_caps_malloc(512, MALLOC_CAP_DMA);
    if (!sector) {
        (void)sd_raw_close(&raw);
        return;
    }
    err = operation_media_status();
    if (err == ESP_OK) err = sdmmc_read_sectors(raw, sector, 0, 1);
    if (err == ESP_OK) {
        info->state = READ_PICO_SD_STATE_FILESYSTEM_ERROR;
        info->filesystem = boot_filesystem(sector);
        if (info->filesystem == READ_PICO_SD_FS_UNKNOWN && sector[510] == 0x55 &&
            sector[511] == 0xaa) {
            uint32_t starts[4] = { 0 };
            for (unsigned i = 0; i < 4; ++i) {
                const uint8_t* part = sector + 446 + i * 16;
                if (part[4] && little_u32(part + 12)) starts[i] = little_u32(part + 8);
            }
            for (unsigned i = 0; i < 4 && info->filesystem == READ_PICO_SD_FS_UNKNOWN; ++i) {
                if (!starts[i] || raw->csd.capacity <= 0 ||
                    starts[i] >= (uint32_t)raw->csd.capacity)
                    continue;
                err = operation_media_status();
                if (err == ESP_OK) err = sdmmc_read_sectors(raw, sector, starts[i], 1);
                if (err != ESP_OK) break;
                info->filesystem = boot_filesystem(sector);
            }
        }
        if (info->filesystem == READ_PICO_SD_FS_EXFAT)
            info->state = READ_PICO_SD_STATE_UNSUPPORTED_FILESYSTEM;
    }
    if (err != ESP_OK) info->state = READ_PICO_SD_STATE_IO_ERROR;
    free(sector);
    if (sd_raw_close(&raw) != ESP_OK) info->state = READ_PICO_SD_STATE_IO_ERROR;
}

static esp_err_t begin_operation(read_pico_sd_stage_t stage) {
    bool present = false;
    esp_err_t detected = read_pico_sd_detect(&present);
    if (detected != ESP_OK) return detected;
    portENTER_CRITICAL(&state_lock);
    esp_err_t err = exported           ? ESP_ERR_INVALID_STATE
                    : probe_state == 1 ? ESP_ERR_NOT_FINISHED
                                       : ESP_OK;
    if (err == ESP_OK) {
        probe_state = 1;
        memset(&cached_info, 0, sizeof(cached_info));
        cached_info.state = READ_PICO_SD_STATE_DETECTING;
        cached_info.stage = stage;
        cached_info.error = ESP_ERR_NOT_FINISHED;
    }
    portEXIT_CRITICAL(&state_lock);
    return err;
}

static void probe_task(void* arg) {
    (void)arg;
    set_stage(READ_PICO_SD_STAGE_MOUNTING);
    esp_err_t err = operation_media_status();
    if (err == ESP_OK) err = mount_card();
    read_pico_sd_info_t info;
    fill_info(&info, err);
    if (err != ESP_OK && info.present && operation_media_status() == ESP_OK)
        diagnose_filesystem(&info);
    publish_info(&info);
    vTaskDelete(NULL);
}

esp_err_t read_pico_sd_start_probe(void) {
    bool present = false;
    esp_err_t detected = read_pico_sd_detect(&present);
    if (detected != ESP_OK) return detected;
    portENTER_CRITICAL(&state_lock);
    observe_media_locked(present);
    esp_err_t err = ESP_OK;
    if (exported)
        err = ESP_ERR_INVALID_STATE;
    else if (probe_state == 1)
        err = ESP_ERR_NOT_FINISHED;
    else if (media_invalidated)
        err = cached_info.error;
    else if (probe_state == 2)
        err = cached_info.mounted           ? ESP_OK
              : cached_info.error != ESP_OK ? cached_info.error
                                            : ESP_FAIL;
    else if (!present) {
        probe_state = 2;
        err = ESP_ERR_NOT_FOUND;
    } else {
        memset(&cached_info, 0, sizeof(cached_info));
        cached_info.present = true;
        cached_info.error = ESP_ERR_NOT_FINISHED;
        cached_info.state = READ_PICO_SD_STATE_DETECTING;
        cached_info.stage = READ_PICO_SD_STAGE_DETECTING;
        probe_state = 1;
    }
    bool start = err == ESP_OK && probe_state == 1;
    portEXIT_CRITICAL(&state_lock);
    if (!start) return err;
    if (xTaskCreate(probe_task, "sd_probe", 4096, NULL, 5, NULL) != pdPASS) {
        read_pico_sd_info_t info;
        fill_info(&info, ESP_ERR_NO_MEM);
        publish_info(&info);
        return ESP_ERR_NO_MEM;
    }
    return ESP_ERR_NOT_FINISHED;
}

esp_err_t read_pico_sd_get_info(read_pico_sd_info_t* info) {
    if (!info) return ESP_ERR_INVALID_ARG;
    bool present = false;
    esp_err_t detected = read_pico_sd_detect(&present);
    portENTER_CRITICAL(&state_lock);
    if (detected == ESP_OK) observe_media_locked(present);
    *info = cached_info;
    // 未取得新样本时保留已确认的媒体快照；真正的维护和写入另行要求实时检测成功。
    // Without a new sample preserve the confirmed snapshot; maintenance and writes separately require a successful live detection.
    esp_err_t err = !cd_observed                  ? ESP_ERR_NOT_FINISHED
                    : !info->present || media_invalidated ? info->error
                    : probe_state == 0            ? ESP_ERR_INVALID_STATE
                    : probe_state == 1            ? ESP_ERR_NOT_FINISHED
                                                  : info->error;
    portEXIT_CRITICAL(&state_lock);
    return err;
}

esp_err_t read_pico_sd_remount(void) {
    esp_err_t err = begin_operation(READ_PICO_SD_STAGE_DETECTING);
    if (err != ESP_OK) return err;
    err = close_card();
    if (err != ESP_OK) {
        read_pico_sd_info_t info;
        fill_info(&info, err);
        publish_info(&info);
        return err;
    }
    portENTER_CRITICAL(&state_lock);
    media_invalidated = false;
    probe_state = 0;
    portEXIT_CRITICAL(&state_lock);
    return read_pico_sd_start_probe();
}

esp_err_t read_pico_sd_sync(void) {
    esp_err_t err = begin_operation(READ_PICO_SD_STAGE_IDLE);
    if (err != ESP_OK) return err;
    err = close_card();
    read_pico_sd_info_t info;
    fill_info(&info, err == ESP_OK ? ESP_ERR_INVALID_STATE : err);
    info.state = READ_PICO_SD_STATE_NEEDS_REMOUNT;
    publish_info(&info);
    return err;
}

// 失效时先注销旧卷，再释放描述符；f_close 不能向换入的介质隐式补写。
// Invalidate an old volume before releasing its descriptor; f_close must not implicitly write to replacement media.
static esp_err_t close_check_file(int fd, esp_err_t err) {
    esp_err_t media = operation_media_status();
    if (media != ESP_OK) {
        BYTE drive = ff_diskio_get_pdrv_card(card);
        if (drive < FF_VOLUMES && drive < 10) {
            char volume[] = { (char)('0' + drive), ':', 0 };
            (void)f_mount(NULL, volume, 0);
        }
        err = media;
    }
    if (close(fd) != 0 && err == ESP_OK) err = ESP_FAIL;
    return err;
}

// 独占创建临时文件，只删除本次拥有的路径；换卡后不继续写入或删除。
// Create an exclusive temporary file and remove only our own path; never write or unlink after a media change.
static esp_err_t verify_io(read_pico_sd_info_t* info) {
    uint8_t expected[256], actual[256];
    for (unsigned i = 0; i < sizeof(expected); ++i) expected[i] = (uint8_t)(i * 29 + 7);
    char path[64];
    int fd = -1;
    bool owned_path = false;
    esp_err_t err = operation_media_status();
    set_stage(READ_PICO_SD_STAGE_CHECKING_WRITE);
    for (unsigned n = 0; err == ESP_OK && fd < 0 && n < 32; ++n) {
        err = operation_media_status();
        if (err != ESP_OK) break;
        snprintf(
            path, sizeof(path), SD_MOUNT_POINT "/.pico-check-%08lx.tmp",
            (unsigned long)++check_sequence);
        fd = open(path, O_CREAT | O_EXCL | O_RDWR, 0600);
        if (fd < 0 && errno != EEXIST) err = ESP_FAIL;
    }
    if (fd < 0 && err == ESP_OK) err = ESP_FAIL;
    owned_path = fd >= 0;
    if (err == ESP_OK) err = operation_media_status();
    if (err == ESP_OK && write(fd, expected, sizeof(expected)) != sizeof(expected)) err = ESP_FAIL;
    if (err == ESP_OK) err = operation_media_status();
    if (err == ESP_OK && fsync(fd) != 0) err = ESP_FAIL;
    if (fd >= 0) {
        err = close_check_file(fd, err);
        fd = -1;
    }
    // 重开只读句柄，避免同一 FIL 的数据缓存让假读回通过。
    // Reopen read-only so the same FIL data cache cannot make a false read-back pass.
    set_stage(READ_PICO_SD_STAGE_CHECKING_READ);
    if (err == ESP_OK) err = operation_media_status();
    if (err == ESP_OK) {
        fd = open(path, O_RDONLY);
        if (fd < 0) err = ESP_FAIL;
    }
    if (err == ESP_OK) err = operation_media_status();
    if (err == ESP_OK && lseek(fd, 0, SEEK_SET) != 0) err = ESP_FAIL;
    if (err == ESP_OK && read(fd, actual, sizeof(actual)) != sizeof(actual)) err = ESP_FAIL;
    if (err == ESP_OK && memcmp(expected, actual, sizeof(expected))) err = ESP_FAIL;
    if (fd >= 0) err = close_check_file(fd, err);
    if (owned_path) {
        esp_err_t media = operation_media_status();
        if (media == ESP_OK) {
            if (unlink(path) != 0 && err == ESP_OK) err = ESP_FAIL;
        } else
            err = media;
    }
    if (err == ESP_OK) info->read_verified = info->write_verified = true;
    return err;
}

static esp_err_t complete_verified_mount(read_pico_sd_info_t* info) {
    fill_info(info, ESP_OK);
    esp_err_t err = info->error;
    if (err == ESP_OK) err = verify_io(info);
    if (err == ESP_OK) {
        // 写入临时文件可能分配簇，删除后重新读取真实剩余空间。
        // The temporary file can allocate clusters; refresh actual free space after removal.
        fill_info(info, ESP_OK);
        err = info->error;
        info->read_verified = info->write_verified = err == ESP_OK;
    }
    if (err != ESP_OK) {
        uint64_t physical = info->physical_bytes;
        (void)close_card();
        fill_info(info, err);
        info->physical_bytes = physical;
        info->state = READ_PICO_SD_STATE_CHECK_FAILED;
    }
    return err;
}

esp_err_t read_pico_sd_verify(void) {
    read_pico_sd_info_t current;
    esp_err_t err = read_pico_sd_get_info(&current);
    if (err != ESP_OK || !current.mounted) return err == ESP_OK ? ESP_ERR_INVALID_STATE : err;
    err = begin_operation(READ_PICO_SD_STAGE_CHECKING_WRITE);
    if (err != ESP_OK) return err;
    read_pico_sd_info_t info;
    err = complete_verified_mount(&info);
    publish_info(&info);
    return err == ESP_OK ? read_pico_sd_get_info(&info) : err;
}

static esp_err_t ensure_font_dirs(void) {
    const char* paths[] = { SD_MOUNT_POINT "/assets", SD_MOUNT_POINT "/assets/fonts",
                            SD_MOUNT_POINT "/fonts" };
    for (unsigned i = 0; i < sizeof(paths) / sizeof(paths[0]); ++i) {
        esp_err_t err = operation_media_status();
        if (err != ESP_OK) return err;
        if (mkdir(paths[i], 0777) != 0 && errno != EEXIST) return ESP_FAIL;
    }
    return ESP_OK;
}

esp_err_t read_pico_sd_format(void) {
    read_pico_sd_info_t current;
    (void)read_pico_sd_get_info(&current);
    portENTER_CRITICAL(&state_lock);
    bool invalid = media_invalidated;
    portEXIT_CRITICAL(&state_lock);
    if (invalid) return current.present ? ESP_ERR_INVALID_STATE : ESP_ERR_NOT_FOUND;
    esp_err_t err = begin_operation(READ_PICO_SD_STAGE_DETECTING);
    if (err != ESP_OK) return err;
    err = operation_media_status();
    if (err == ESP_OK) err = close_card();
    sdmmc_card_t* raw = NULL;
    sdmmc_host_t host;
    sdmmc_slot_config_t slot;
    host_configuration(&host, &slot);
    if (err == ESP_OK) err = sd_raw_open(&host, &slot, &raw);
    if (err == ESP_OK) {
        set_stage(READ_PICO_SD_STAGE_FORMATTING);
        err = sd_raw_format_fat32(raw);
    }
    esp_err_t cleanup = sd_raw_close(&raw);
    if (err == ESP_OK) err = cleanup;
    if (err == ESP_OK) err = operation_media_status();
    if (err == ESP_OK) {
        set_stage(READ_PICO_SD_STAGE_REMOUNTING);
        err = mount_card();
    }
    if (err == ESP_OK) err = ensure_font_dirs();
    read_pico_sd_info_t info;
    if (err == ESP_OK)
        err = complete_verified_mount(&info);
    else {
        (void)close_card();
        fill_info(&info, err);
    }
    publish_info(&info);
    return err == ESP_OK ? read_pico_sd_get_info(&info) : err;
}

esp_err_t read_pico_sd_raw_acquire(sdmmc_card_t** out_card) {
    if (!out_card) return ESP_ERR_INVALID_ARG;
    *out_card = NULL;
    read_pico_sd_info_t current;
    (void)read_pico_sd_get_info(&current);
    portENTER_CRITICAL(&state_lock);
    bool invalid = media_invalidated;
    portEXIT_CRITICAL(&state_lock);
    if (invalid) return current.present ? ESP_ERR_INVALID_STATE : ESP_ERR_NOT_FOUND;
    esp_err_t err = begin_operation(READ_PICO_SD_STAGE_DETECTING);
    if (err != ESP_OK) return err;
    portENTER_CRITICAL(&state_lock);
    exported = true;
    cached_info.exported = true;
    portEXIT_CRITICAL(&state_lock);
    err = operation_media_status();
    if (err == ESP_OK) err = close_card();
    sdmmc_host_t host;
    sdmmc_slot_config_t slot;
    host_configuration(&host, &slot);
    if (err == ESP_OK) err = sd_raw_open(&host, &slot, &exported_card);
    if (err == ESP_OK &&
        (!physical_capacity(exported_card) || exported_card->csd.sector_size != 512)) {
        err = ESP_ERR_NOT_SUPPORTED;
    }
    if (err == ESP_OK) err = operation_media_status();
    read_pico_sd_info_t info;
    fill_info(&info, err == ESP_OK ? ESP_ERR_INVALID_STATE : err);
    if (err == ESP_OK) {
        info.error = ESP_OK;
        info.state = READ_PICO_SD_STATE_EXPORTED;
        info.stage = READ_PICO_SD_STAGE_COMPLETE;
        info.physical_bytes = physical_capacity(exported_card);
        bool present = false;
        esp_err_t detected = read_pico_sd_detect(&present);
        // 媒体确认与租约交付在同一临界区提交，不能把已失效的 raw 指针交给 USB。
        // Commit media validation and lease delivery together; never hand USB an already-invalid raw pointer.
        portENTER_CRITICAL(&state_lock);
        if (detected == ESP_OK) observe_media_locked(present);
        err = detected != ESP_OK ? detected : !present ? ESP_ERR_NOT_FOUND : media_invalidated ? ESP_ERR_INVALID_STATE : ESP_OK;
        if (err == ESP_OK) {
            cached_info = info;
            observe_media_locked(present);
            probe_state = 2;
            *out_card = exported_card;
        }
        portEXIT_CRITICAL(&state_lock);
        if (err == ESP_OK) return ESP_OK;
    }
    {
        (void)sd_raw_close(&exported_card);
        portENTER_CRITICAL(&state_lock);
        exported = false;
        portEXIT_CRITICAL(&state_lock);
        fill_info(&info, err);
    }
    publish_info(&info);
    return err;
}

esp_err_t read_pico_sd_raw_release(void) {
    portENTER_CRITICAL(&state_lock);
    esp_err_t err = !exported          ? ESP_ERR_INVALID_STATE
                    : probe_state == 1 ? ESP_ERR_NOT_FINISHED
                                       : ESP_OK;
    if (err == ESP_OK) {
        probe_state = 1;
        cached_info.stage = READ_PICO_SD_STAGE_REMOUNTING;
        cached_info.error = ESP_ERR_NOT_FINISHED;
    }
    portEXIT_CRITICAL(&state_lock);
    if (err != ESP_OK) return err;
    err = sd_raw_close(&exported_card);
    if (err == ESP_OK) {
        // 完全停止 USB 后的显式归还允许重新检测新插入的卡。
        // Explicit return after USB fully stops permits detection of a newly inserted card.
        bool present = false;
        err = read_pico_sd_detect(&present);
        portENTER_CRITICAL(&state_lock);
        if (err == ESP_OK) {
            observe_media_locked(present);
            media_invalidated = false;
        }
        portEXIT_CRITICAL(&state_lock);
    }
    if (err == ESP_OK) err = operation_media_status();
    if (err == ESP_OK) err = mount_card();
    read_pico_sd_info_t info;
    if (err == ESP_OK)
        err = complete_verified_mount(&info);
    else
        fill_info(&info, err);
    if (err == ESP_OK) {
        bool present = false;
        esp_err_t detected = read_pico_sd_detect(&present);
        portENTER_CRITICAL(&state_lock);
        if (detected == ESP_OK) observe_media_locked(present);
        err = detected != ESP_OK ? detected : !present ? ESP_ERR_NOT_FOUND : media_invalidated ? ESP_ERR_INVALID_STATE : ESP_OK;
        if (err == ESP_OK) {
            exported = false;
            cached_info = info;
            observe_media_locked(present);
            probe_state = 2;
        }
        portEXIT_CRITICAL(&state_lock);
        if (err == ESP_OK) return ESP_OK;
        (void)close_card();
        fill_info(&info, err);
    }
    publish_info(&info);
    return err;
}

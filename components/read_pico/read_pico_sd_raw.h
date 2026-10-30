/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 *
 * 内部独占 SDMMC 生命周期与显式 FAT32 重建；不注册 VFS，不决定用户授权。
 * Internal exclusive SDMMC lifecycle and explicit FAT32 rebuild; no VFS or user-authorization policy.
 *
 * 冻结：调用方须串行化并排空所有消费者；不得自动调用格式化或更改 host/slot 时序。
 * Frozen: the caller serializes and drains all consumers; never autoformat or alter host/slot timing.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

#include "driver/sdmmc_host.h"
#include "esp_err.h"
#include "sdmmc_cmd.h"
#include "ff.h"
#include "diskio_impl.h"
#include "diskio_sdmmc.h"

// 由包含此头的主模块提供，检查本次操作的卡与确认状态。/ Supplied by the owner to check this operation's media and confirmation.
static esp_err_t operation_media_status(void);
// 仅在即将进入可能写卡的 FatFs 调用前标记。/ Mark only immediately before entering the potentially destructive FatFs call.
static void mark_format_started(void);

static esp_err_t sd_raw_host_deinit(const sdmmc_host_t* host) {
    if (host->flags & SDMMC_HOST_FLAG_DEINIT_ARG) return host->deinit_p(host->slot);
    return host->deinit();
}

// out 必须初始化为空；现有所有权不能被第二次 open 覆盖。/ Initialize out to NULL; a second open must not overwrite existing ownership.
static esp_err_t sd_raw_open(const sdmmc_host_t* host, const sdmmc_slot_config_t* slot,
                             sdmmc_card_t** out) {
    if (!out || !host || !slot || !host->init ||
        ((host->flags & SDMMC_HOST_FLAG_DEINIT_ARG) ? !host->deinit_p : !host->deinit)) {
        return ESP_ERR_INVALID_ARG;
    }
    if (*out) return ESP_ERR_INVALID_STATE;
    // 本 helper 仅接管普通 SDMMC，不接管 SPI 或 card_init 内部新增 DMA 缓冲的所有权。
    // This helper owns ordinary SDMMC, not SPI or DMA buffers allocated internally by card_init.
    if (host->flags & (SDMMC_HOST_FLAG_SPI | SDMMC_HOST_FLAG_ALLOC_ALIGNED_BUF)) {
        return ESP_ERR_NOT_SUPPORTED;
    }

    sdmmc_card_t* opened = calloc(1, sizeof(*opened));
    if (!opened) return ESP_ERR_NO_MEM;
    esp_err_t err = host->init();
    if (err != ESP_OK) {
        free(opened);
        return err;
    }
    err = sdmmc_host_init_slot(host->slot, slot);
    if (err != ESP_OK) {
        // IDF 的 deinit_slot 无法回收尚未建立 slot 的 controller；此处拥有整个独占 host。
        // IDF deinit_slot cannot reclaim a controller whose slot never initialized; this path owns the exclusive host.
        if (host->flags & SDMMC_HOST_FLAG_DEINIT_ARG) (void)sdmmc_host_deinit();
        else (void)sd_raw_host_deinit(host);
        free(opened);
        return err;
    }
    err = sdmmc_card_init(host, opened);
    if (err != ESP_OK) {
        // init 的原始错误优先；回收失败也不能保留半初始化的 card。/ Preserve the init error; failed cleanup must not retain a partial card.
        (void)sd_raw_host_deinit(host);
        free(opened);
        return err;
    }
    *out = opened;
    return ESP_OK;
}

static esp_err_t sd_raw_close(sdmmc_card_t** owned) {
    if (!owned) return ESP_ERR_INVALID_ARG;
    if (!*owned) return ESP_OK;
    sdmmc_card_t* closing = *owned;
    *owned = NULL;
    esp_err_t err = sd_raw_host_deinit(&closing->host);
    free(closing);
    return err;
}

// 仅同步、串行的格式化调用持有；不修改调用方 raw card 的 host。
// Owned only during synchronous serialized formatting; the caller's raw-card host stays unchanged.
static esp_err_t (*sd_raw_format_transaction)(int, sdmmc_command_t*);
static esp_err_t sd_raw_format_media_error;

static esp_err_t sd_raw_format_guarded_transaction(int slot, sdmmc_command_t* command) {
    if (!sd_raw_format_transaction) return ESP_ERR_INVALID_STATE;
    if (sd_raw_format_media_error != ESP_OK) return sd_raw_format_media_error;
    esp_err_t err = operation_media_status();
    if (err != ESP_OK) {
        // 第一次失效锁存到格式化结束，后续命令不能因重新插入而继续。
        // Latch the first invalidation until formatting ends; reinsertion must not resume later commands.
        sd_raw_format_media_error = err;
        return err;
    }
    return sd_raw_format_transaction(slot, command);
}

static esp_err_t sd_raw_format_fat32(sdmmc_card_t* raw) {
    if (!raw) return ESP_ERR_INVALID_ARG;
    // 当前 IDF 的容量是有符号扇区数；只接受可表示的 512-byte SD 介质。
    // This IDF uses a signed sector count; accept only representable 512-byte SD media.
    if (raw->csd.capacity <= 0 || raw->csd.sector_size != 512) return ESP_ERR_NOT_SUPPORTED;
    if (!raw->host.do_transaction) return ESP_ERR_INVALID_STATE;

    void* work = malloc(4096);
    if (!work) return ESP_ERR_NO_MEM;
    BYTE drive = FF_DRV_NOT_USED;
    esp_err_t err = ff_diskio_get_drive(&drive);
    if (err != ESP_OK) {
        free(work);
        return err;
    }
    if (drive >= FF_VOLUMES || drive > 9) {
        free(work);
        return ESP_ERR_INVALID_STATE;
    }

    // IDF 注销 diskio 不清 s_cards。稳定副本避免留下可复用的 heap card 悬空地址。
    // IDF diskio unregister does not clear s_cards. A stable copy avoids a dangling reusable heap-card address.
    // 公共注册 API 内部分配失败会 assert；它没有可返回的错误码。
    // The public registration API asserts on allocation failure; it has no recoverable error result.
    static sdmmc_card_t format_card;
    format_card = *raw;
    sd_raw_format_transaction = raw->host.do_transaction;
    sd_raw_format_media_error = ESP_OK;
    format_card.host.do_transaction = sd_raw_format_guarded_transaction;
    ff_diskio_register_sdmmc(drive, &format_card);
#if FF_MULTI_PARTITION
    PARTITION previous = VolToPart[drive];
    VolToPart[drive].pd = drive;
    VolToPart[drive].pt = 0;
#else
#error "The explicit single-MBR format path requires FF_MULTI_PARTITION"
#endif
    char path[] = {(char)('0' + drive), ':', '\0'};
    MKFS_PARM options = {.fmt = FM_FAT32, .n_fat = 2, .au_size = 0};
    err = operation_media_status();
    if (err == ESP_OK) {
        mark_format_started();
        // pt=0 且不设 FM_SFD：由 FatFs 一次重建单 MBR 分区及 FAT32，不依赖旧卷。
        // pt=0 without FM_SFD lets FatFs rebuild one MBR partition and FAT32 in one pass, independent of the old volume.
        FRESULT result = f_mkfs(path, &options, work, 4096);
        err = sd_raw_format_media_error != ESP_OK ? sd_raw_format_media_error :
            result == FR_OK ? ESP_OK : ESP_FAIL;
    }
    ff_diskio_unregister(drive);
    sd_raw_format_transaction = NULL;
#if FF_MULTI_PARTITION
    VolToPart[drive] = previous;
#endif
    free(work);
    return err;
}

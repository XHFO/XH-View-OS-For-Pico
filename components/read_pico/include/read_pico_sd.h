/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 *
 * 小纸 Pico SDMMC 探测、挂载、格式化。
 *
 * Read Pico SDMMC probe, mount, and format.
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "sdmmc_cmd.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    READ_PICO_SD_STATE_ABSENT,
    READ_PICO_SD_STATE_DETECTING,
    READ_PICO_SD_STATE_NEEDS_REMOUNT,
    READ_PICO_SD_STATE_READY,
    READ_PICO_SD_STATE_UNSUPPORTED_FILESYSTEM,
    READ_PICO_SD_STATE_FILESYSTEM_ERROR,
    READ_PICO_SD_STATE_IO_ERROR,
    READ_PICO_SD_STATE_CHECK_FAILED,
    READ_PICO_SD_STATE_EXPORTED,
} read_pico_sd_state_t;

typedef enum {
    READ_PICO_SD_STAGE_IDLE,
    READ_PICO_SD_STAGE_DETECTING,
    READ_PICO_SD_STAGE_MOUNTING,
    READ_PICO_SD_STAGE_FORMATTING,
    READ_PICO_SD_STAGE_REMOUNTING,
    READ_PICO_SD_STAGE_CHECKING_READ,
    READ_PICO_SD_STAGE_CHECKING_WRITE,
    READ_PICO_SD_STAGE_COMPLETE,
    READ_PICO_SD_STAGE_FAILED,
} read_pico_sd_stage_t;

typedef enum {
    READ_PICO_SD_FS_UNKNOWN,
    READ_PICO_SD_FS_FAT,
    READ_PICO_SD_FS_EXFAT,
} read_pico_sd_filesystem_t;

typedef struct {
    bool present;
    bool mounted;
    /// 兼容字段；一般挂载/读卡故障不能据此判定需要格式化。
    /// Compatibility field; generic mount/I/O errors never imply formatting is required.
    bool needs_format;
    char name[8];
    uint64_t capacity_bytes;
    uint64_t free_bytes;
    esp_err_t error;
    /// 操作状态、实际阶段与只读识别的卷类型；不显示伪造进度。
    /// Operation state, actual stage and read-only volume identification; no invented progress.
    read_pico_sd_state_t state;
    read_pico_sd_stage_t stage;
    read_pico_sd_filesystem_t filesystem;
    /// 每次观察到 CD 改变时递增；不能识别未观察到的快速换卡。
    /// Increments on each observed CD transition; cannot detect an unobserved fast swap.
    uint32_t media_generation;
    /// 即使不支持文件系统，也可显示已读取的物理容量。
    /// Physical capacity may be available even for an unsupported filesystem.
    uint64_t physical_bytes;
    /// 仅显式校验中的独占临时文件写入、同步与读回成功后置位。
    /// Set only after the explicit exclusive temporary-file write, sync and read-back check.
    bool read_verified;
    bool write_verified;
    /// 破坏性重建已经开始；其后失败也不能自动重试。
    /// Destructive rebuilding has started; a later failure must not trigger an automatic retry.
    bool format_started;
    /// USB 租约已保留（含进入、退出与退出失败待重试），本地消费者不可访问。
    /// USB lease reserved, including entry, exit and failed exit pending retry; local consumers must stay paused.
    bool exported;
} read_pico_sd_info_t;

esp_err_t read_pico_sd_start_probe(void);
/// 查询实时 CD 并锁存失效，不卸载卡；拔卡后容量清零，重新插入也须显式 remount。
/// / Sample live CD and latch invalidation without unmounting; removal clears capacity and reinsertion requires explicit remount.
/// CD 通信失败保留上次已确认快照和返回状态；尚无样本时返回 NOT_FINISHED，不能凭此执行维护写入。
/// A CD communication failure preserves the last confirmed snapshot/status; no prior sample returns NOT_FINISHED and never authorizes maintenance writes.
esp_err_t read_pico_sd_get_info(read_pico_sd_info_t* info);
/// 调用前关闭所有字体、阅读及上传句柄；这是拔卡失效后恢复挂载的唯一入口。
/// / Close all font, reader and upload handles first; this is the only recovery entry after removal invalidation.
/// 排空并暂停所有 SD I/O；返回 NOT_FINISHED 后查询 info，忙时 mounted 为 false，绝不格式化。
/// Drain and pause all SD I/O; after NOT_FINISHED poll info, whose mounted flag stays false while busy. Never formats.
esp_err_t read_pico_sd_remount(void);
/// 仅显式用户确认后调用；失效挂载必须先 remount，绝不自动建议格式化拔出的卡。
/// / Explicit user confirmation only; an invalidated mount needs remount first, never a removal-triggered format suggestion.
/// 同步阻塞；调用前排空并暂停字体、阅读、上传和其它 SD I/O。成功表示非格式化重挂及容量验证通过。
/// Blocking; first drain/pause font, reader, upload and all other SD I/O. Success requires fresh nonformat remount and capacity validation.
/// 重建全卡为一个 MBR/FAT32 分区，再执行容量及临时文件读写校验。
/// Rebuild the entire card as one MBR/FAT32 partition, then verify capacity and temporary-file read/write.
/// 失败时卡上内容可能已被清除；调用方不可自动重试格式化，也不可把错误当作仍需清卡的依据。
/// Failure may follow actual erasure; callers must not automatically retry formatting or infer another erase is needed.
esp_err_t read_pico_sd_format(void);
/// 同步读写校验；须先排空其它 SD I/O，不覆盖现有文件，失败不自动格式化。
/// Blocking read/write check; first drain other SD I/O. Never overwrites existing files or autoformats.
esp_err_t read_pico_sd_verify(void);
/// USB MSC 独占租约：调用前排空所有消费者，成功后仅租约持有者可做块 I/O。
/// Exclusive USB MSC lease: drain all consumers first; only the lease owner may perform block I/O on success.
esp_err_t read_pico_sd_raw_acquire(sdmmc_card_t** out_card);
/// USB 完全停止后归还；同步执行非格式化挂载与读写校验，不自动格式化。
/// Return after USB has fully stopped; synchronously mount without formatting and verify read/write.
/// 失败保留租约与 exported=true，可再次调用；成功才允许本地访问。
/// Failure retains the lease with exported=true and allows retry; only success permits local access.
/// 一旦调用，不论结果，旧 raw 指针均不可再用于块访问。
/// Once called, the old raw pointer must never be used for block access, regardless of the result.
esp_err_t read_pico_sd_raw_release(void);
/// 已挂载则卸载落盘。探测未完成返回 ESP_ERR_NOT_FINISHED。
/// / Unmount if mounted so writes land. ESP_ERR_NOT_FINISHED while probing.
esp_err_t read_pico_sd_sync(void);

#ifdef __cplusplus
}
#endif

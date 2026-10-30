/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 * 中文：启动和锁屏P4B资源共用的有界无损外层解压，调用方仍验证尺寸并按原方向绘制。
 * English: Bounded lossless outer unpacking shared by splash/lock P4B assets; callers retain dimensions and rotation validation.
 * 冻结：为云端登录腾出APP空间，只压缩封装、不改原始资源字节；不操作屏幕或持久缓存。
 * Frozen: Make APP room for cloud login through packaging only; preserve original asset bytes, with no display access or persistent cache.
 */
#pragma once
#include <stddef.h>
#include <stdint.h>

#define UI_PACKED_ASSET_MAX_BYTES (64u * 1024u)

/// 完整校验raw DEFLATE、输入/输出长度和CRC后返回PSRAM；失败置长度0，调用方free并继续验证内部P4B。
/// Return PSRAM only after complete raw DEFLATE, input/output lengths and CRC validation; failure sets length zero. Caller frees and validates the inner P4B.
uint8_t* ui_packed_asset_unpack(const uint8_t* packed, size_t packed_bytes, size_t* image_bytes);

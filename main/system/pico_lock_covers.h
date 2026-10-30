/*
 * SPDX-FileCopyrightText: 2026 XH Lab
 * SPDX-License-Identifier: Apache-2.0
 * 中文：休眠封面的准备、纯内存绘制与释放；不控制电源或阅读进度。
 * English: Prepare, paint and release lock-screen covers, without power or reading-progress ownership.
 * 冻结：所有文件访问只在 prepare；render 仅使用自有 RAM，最多六本。
 * Frozen: File access belongs to prepare only; render uses owned RAM and at most six books.
 */
#pragma once
#include <stdint.h>

/// 准备封面：0 单本等比全屏，1 双本，2 六本拼贴，3 留白海报，4 全屏裁切书墙；优先最近阅读。
/// Prepare covers: 0 full-screen aspect fit, 1 two books, 2 six-book collage, 3 poster, 4 cropped wall; prioritize the last read.
void ps_lock_covers_prepare(int layout);
/// 按当前横竖方向绘制已准备封面；没有封面时显示书名占位，不访问文件。
/// Paint prepared covers in the current orientation; missing art uses title placeholders, with no file access.
void ps_lock_covers_render(uint8_t *fb);
/// 释放所有封面内存；可重复调用；不删除可丢弃的 TF 缓存。
/// Release all cover memory; safe to repeat and never deletes disposable TF caches.
void ps_lock_covers_release(void);

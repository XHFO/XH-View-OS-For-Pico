/*
 * SPDX-License-Identifier: Apache-2.0
 * 中文：系统编辑器与TF画板共用的连续DU推屏边界。
 * English: Shared continuous-DU presenter for the editor and TF drawing apps.
 * 冻结：每次仅扫一个相位；目标参考与未完成驱动分开管理，打断必须强刷恢复。
 * Frozen: Scan one phase per call; track target references separately from unfinished drive and clean on interruption.
 */
#pragma once
#include "app.h"
/// 输入为旋转后的屏幕像素区域；只将该区域变更目标二值化，停止后由调用方灰阶定稿。/ Rotated pixel damage; binary targets only here, followed by caller-owned gray settling.
bool ps_ink_present(app_ctx_t *ctx,EpdRect logical_pixels);
/// 无新笔迹时继续补一相；false要求强刷恢复。/ Drain one phase without new ink; false requests a full recovery.
bool ps_ink_step(app_ctx_t *ctx);
/// 是否仍有驱动未完成。/ Whether panel drive is still unfinished.
bool ps_ink_pending(void);
/// 放弃余相，返回是否必须强刷；调用方负责恢复。/ Abandon phases, returning whether a full clean is required; caller owns recovery.
bool ps_ink_cancel(void);

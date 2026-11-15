/*
 * SPDX-License-Identifier: Apache-2.0
 * 中文：灵犀画板有界语义快照；网络只交付数据，横竖排版由设备决定。
 * English: Bounded semantic board snapshots; the device owns layout in every orientation.
 * 冻结：不接受绘图指令、路径、脚本或凭据；解析失败不改变当前画面。
 * Frozen: No drawing commands, paths, scripts or credentials; failed parsing never changes the active frame.
 */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "../../components/read_pico_apps/include/pico_app_board.h"

/// 严格解析；失败返回NULL且不产生I/O。/ Strict parsing; NULL on failure, with no I/O.
ps_board_model_t *ps_board_model_parse(const void *data, size_t bytes);
/// 可选固定错误码，不回显内容；线程间不共享错误状态。/ Optional fixed error code, never content; no shared error state.
ps_board_model_t *ps_board_model_parse_ex(const void *data, size_t bytes, const char **error);
void ps_board_model_free(ps_board_model_t *model);
/// 只按可见的时间变化请求重画，不进行显示、网络或文件操作。/ Request a repaint for visible time changes only; no display, network or file operations.
bool ps_board_model_tick(ps_board_model_t *model, int64_t monotonic_ms);
/// 用可注入墙钟测试；负数表示RTC尚不可用。/ Injectable wall-clock tick; negative means RTC unavailable.
bool ps_board_model_tick_at(ps_board_model_t *model, int64_t monotonic_ms, double utc_seconds);
/// 缓存时区，纯绘制不读取设置；返回是否改变。/ Cache timezone so painting never reads settings; return whether it changed.
bool ps_board_model_set_timezone(ps_board_model_t *model, int minutes);
/// 固定格式底图随语义快照持久保存，不编入APP，也不是界面位图。/ Fixed-format basemap data persists with the semantic snapshot; it is neither APP data nor a rendered UI bitmap.
bool ps_board_model_needs_world_map(const ps_board_model_t *model);
/// 解析时验证并复制；失败保留旧资源。纯绘制不读取TF。/ Validate and copy while parsing; failure retains old resources. Painting never reads TF.
bool ps_board_model_set_world_map(ps_board_model_t *model, const void *data, size_t bytes);
/// 纯绘制当前快照；不分配、联网、读盘或导航。/ Paint the snapshot without allocating, networking, reading files or navigating.
void ps_board_model_render(uint8_t *fb, const ps_board_model_t *model);
/// 非绘制阶段载入；失卡/卸载/升级释放代码，快照不删除。/ Load outside paint; media loss/removal/update releases code without deleting snapshots.
bool ps_board_renderer_prepare(bool app);
void ps_board_renderer_release(void);
bool ps_board_renderer_available(bool app);
void ps_board_model_paint(const ps_board_model_t *model, const ps_board_painter_t *painter);
/// 协议kind名称列表与索引。/ Protocol kind names and index.
const char *ps_board_kind_name(ps_board_kind_t kind);

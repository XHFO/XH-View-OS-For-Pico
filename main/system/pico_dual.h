/* SPDX-License-Identifier: Apache-2.0
 * 双机实验产品入口，所有副作用由UI线程触发。/ Dual-reader product adapter; all effects run on the UI thread.
 */
#pragma once
#include "dual_link.h"
#include "pico_system.h"
bool ps_dual_enabled(void);
int ps_dual_role(void);
/// 主机自动方向加各角色独立翻转，不覆盖系统设置。/ Primary auto orientation plus role offsets, without overwriting system settings.
int ps_dual_base_rotation(void);
int ps_dual_rotation(void);
bool ps_dual_orient(int rotation);
bool ps_dual_flip(int role, bool enabled);
bool ps_dual_rotate(void);
/// 双机会话内的方向锁定；独立于系统方向锁定，停止自动换向但保留手动翻转。
/// Paired-session direction lock; independent of the system lock, stops auto turns and keeps manual flips.
bool ps_dual_rotation_locked(void);
bool ps_dual_lock_set(bool locked);
/// 双机贴合防误触开关；两端通过控制通道同步。/ Paired face-to-face guard, synchronized over the control channel.
bool ps_dual_guard_enabled(void);
bool ps_dual_guard_set(bool enabled);
/// 当前两机是否处于屏幕相对（折叠贴合）姿态。
/// Whether both screens currently face each other (folded contact pose).
bool ps_dual_contact_likely(void);
/// 桌面展开校准；主机采样并广播，从机请求主机执行。
/// Desk-unfolded calibration; the primary samples and broadcasts, a secondary requests it.
bool ps_dual_calibrate(void);
bool ps_dual_calibrated(void);
int ps_dual_peer_face(void);
/// 主机是否已确认当前版本推屏成功；从机据此才允许真正显示。
/// Whether the primary confirmed this revision's presentation; the secondary may then paint.
bool ps_dual_left_ready(void);
/// 取一次“手动翻转待应用”标记，避免从机锁定阻止手动翻转。
/// Take the one-shot manual-flip marker so a locked secondary still applies its own flip.
bool ps_dual_take_manual_rotation(void);
/// 同步或双机连接期间暂缓自动休眠，显式电源操作仍有效。/ Defer idle sleep during sync or pairing; explicit power actions remain available.
bool ps_dual_holds_awake(void);
bool ps_dual_configure(int role, const char* host, const char* code);
void ps_dual_enter(void);
void ps_dual_render(uint8_t* fb);
bool ps_dual_event(const ps_event_t* event);
bool ps_dual_service(app_ctx_t* ctx, const app_desc_t* app, app_redraw_t* redraw);
bool ps_dual_open(const char* path);
bool ps_dual_open_at(const char* path, uint32_t page);
/// 仅返回本次请求是否被接收；失败不代表已翻页。/ Report admission of this request, never success for a rejected turn.
bool ps_dual_turn(int direction);
bool ps_dual_seek(uint32_t page);
bool ps_dual_resize(uint8_t font_px);
bool ps_dual_reflow_resolved(uint32_t revision, uint32_t spread);
bool ps_dual_reflow_failed(uint32_t revision);
/// 恢复本端共享正文，不发开书请求或改变双机位置。/ Resume this device's shared reader without reopening or changing the paired position.
bool ps_dual_resume(void);
/// 仅在双机正文目标页实际推屏成功后调用。/ Call only after successful physical presentation of the paired target page.
void ps_dual_presented(uint32_t page);
void ps_dual_total(uint32_t total);
const char* ps_dual_font(void);
const char* ps_dual_path(void);
void ps_dual_suspend(void);

void ps_dual_status(dl_status_t* out);
bool ps_dual_usb_command(const char* line, char* out, size_t capacity);

#ifdef PICO_NATIVE_PREVIEW
void ps_dual_native_wifi(bool online);
void pico_dual_ble_test_enabled(bool enabled);
#endif

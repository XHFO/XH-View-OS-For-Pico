/* SPDX-License-Identifier: Apache-2.0
 * 中文：产品统一电源状态，保留原页面实例并拦截锁屏输入。
 * English: Product power states preserving the current page instance and gating locked input.
 * 冻结：完整短按切换锁屏、长按开菜单；可选双击唤醒以额外触控功耗换便捷，保持唤醒只抑制自动入口。
 * Frozen: Complete clicks toggle lock and long presses open a menu; optional double tap trades touch standby power for convenience, and keep-awake suppresses automatic entry only.
 */
#pragma once
#include "pico_system.h"
typedef enum { PS_POWER_LOCK, PS_POWER_SLEEP, PS_POWER_RESTART, PS_POWER_OFF } ps_power_action_t;
/// 只提交请求，下一轮输入前统一执行。/ Queue a request for processing before the next input dispatch.
void ps_power_request(int action);
void ps_power_preview(int style);
void ps_power_poll_hardware(app_ctx_t *ctx);
/// 产品根与子页共用一只活动时钟；仅实际输入/阻塞操作重置。/ One activity clock across product roots and children; reset only on input or blocked operations.
void ps_power_activity(int64_t now_ms);
/// 非阻塞策略/恢复轮询；主循环在取消输入和执行状态切换之前调用。/ Nonblocking policy/recovery poll before the loop cancels input and executes transitions.
void ps_power_poll(app_ctx_t *ctx,const app_desc_t *current);
/// 系统提供停靠条件；空闲时长仍由电源控制器统一判定。/ The system supplies dock eligibility; the power controller owns elapsed idle time.
bool ps_power_dock_ready(void);
/// 取消并等待游戏任务，保留已载入模块和局面。/ Cancel and join game workers while retaining the loaded module and position.
bool ps_games_power_prepare(void);
void ps_games_power_resume(void);
bool ps_power_pending(void);
bool ps_power_active(void);
/// 当前锁屏实际显示灵犀画板，不以锁屏前的路由代替。/ Whether the lock face actually displays the canvas, independent of its resume route.
bool ps_power_board_visible(void);
bool ps_power_toggle(app_ctx_t *ctx,const app_desc_t *current);
/// PMU长按或预览长按打开菜单；锁屏中先恢复，保留原页面返回路径。/ PMU/native long press opens the menu, restoring a locked page and retaining its return path.
bool ps_power_key_long(app_ctx_t *ctx,const app_desc_t *current);
/// 自动空闲分钟数；0关闭，保持唤醒仍优先，仅接受六个固定档位。/ Idle minutes; zero disables, keep-awake wins, and only six fixed choices are accepted.
unsigned ps_power_auto_minutes(void);
bool ps_power_set_auto_minutes(unsigned minutes);
/// 可选锁屏双击；休眠/关机不接受触摸，NORMAL触控会增加待机耗电。/ Optional lock double tap; sleep/off ignore touch, and NORMAL touch increases standby consumption.
bool ps_power_doubletap(void);
bool ps_power_set_doubletap(bool enabled);
/// 同一次原子保存配置自动休眠与触摸唤醒；供配对电脑配置。/ Atomically save idle sleep and touch-wake settings for paired desktop configuration.
bool ps_power_configure(unsigned minutes,bool doubletap);
/// 返回true时调用方不得再分发页面输入或tick。/ A true return suppresses page input and ticks.
bool ps_power_run(app_ctx_t *ctx,const app_desc_t *current);
void ps_power_wake(void);
int ps_power_state(void);
void ps_power_enter(void);
/// 仅关闭最内层关机/重启确认；true时保留当前页面且不提交电源操作。/ Dismiss only shutdown/restart confirmation; true retains this page without requesting power actions.
bool ps_power_back(void);
void ps_power_render(uint8_t *fb);
bool ps_power_event(const ps_event_t *event);
int ps_power_cover_layout(void);
void ps_power_set_cover_layout(int layout);
/// 统一锁屏时钟/摘录绘制，不改变原页面路由。/ Standby drawing without changing the original page route.
void ps_home_standby_begin(int style);
bool ps_home_standby_tick(int style);
void ps_home_standby_render(uint8_t *fb,int style);

/* SPDX-License-Identifier: Apache-2.0
 * 中文：手动日期/时间与本地时区；UTC写入PMU RTC后核对读回。
 * English: Manual date/time and local timezone; write UTC to the PMU RTC and verify it.
 * 冻结：有效已同步RTC可经硬件轮询桥接ESP系统钟，5秒内误差不重设；预览不修改电脑时间。
 * Frozen: Hardware polling may bridge valid synchronized RTC to ESP time, leaving differences within five seconds unchanged; preview never changes host time.
 * 冻结：绘制/时间页tick不校时，不触及电源或VCOM。
 * Frozen: Rendering/datetime ticks never set clocks, power or VCOM.
 * 冻结：Wi-Fi授时回调只缓存样本，由主循环在RTC写回并读回校验后更新系统钟；预览无真实NTP。
 * Frozen: Wi-Fi sync callbacks only cache samples; the main loop verifies RTC writes before system time updates, with no real NTP in preview.
 */
#pragma once
#include "pico_system.h"
#include <stdint.h>

/// 初始化/读取已保存时区；缺省为UTC+08:00。/ Load the saved timezone, defaulting to UTC+08:00.
void ps_datetime_init(void);
typedef enum {PS_TIME_NONE,PS_TIME_RTC,PS_TIME_MANUAL,PS_TIME_NETWORK} ps_datetime_source_t;
typedef enum {PS_NTP_OFFLINE,PS_NTP_WAITING,PS_NTP_SYNCING,PS_NTP_SAVING,PS_NTP_SYNCED,PS_NTP_RETRY} ps_datetime_ntp_state_t;
typedef struct {uint8_t source,ntp_state;bool ntp_pending;uint32_t last_ntp_utc;} ps_datetime_status_t;
/// 全局主循环限频处理自动授时；网络回调不得写RTC，独占PMU时可延后提交。
/// Rate-limited automatic sync on the global main loop; network callbacks never write RTC and PMU ownership can defer commits.
void ps_datetime_service_tick(bool allow_commit);
/// 只读来源与自动授时状态，不含网络名称或凭据。/ Read-only clock provenance/sync status, without network names or credentials.
void ps_datetime_get_status(ps_datetime_status_t *out);
void ps_datetime_enter(void);
void ps_datetime_render(uint8_t *framebuffer);
bool ps_datetime_event(const ps_event_t *event);
bool ps_datetime_key(int key);
bool ps_datetime_tick(void);
/// UTC偏移分钟数，范围-12:00至+14:00，15分钟步进。/ UTC offset in minutes, -12:00 to +14:00 in 15-minute steps.
int ps_datetime_timezone_minutes(void);
/// 真实RTC校时并读回校验；仅成功读回后更新界面时钟锚点。
/// Set and read back the real RTC; update the UI clock anchor only after verified readback.
bool ps_datetime_apply_utc(uint32_t utc);
/// 将有效已同步RTC桥接到系统钟及界面；无效RTC不改钟，失败可在下次轮询重试。
/// Bridge valid synchronized RTC to system/UI clocks; invalid RTC changes nothing and failures can retry on the next poll.
bool ps_datetime_sync_from_rtc(uint32_t utc,bool valid,bool synced);
/// 最近一次校时结果；失败也区分已写入但读回失败的情况。
/// Last clock-setting result, including a successful write followed by failed readback.
const char *ps_datetime_last_result(void);
/// 纯日期校验与UTC换算，不依赖进程全局TZ，也不执行任何存储或硬件操作。
/// Pure date validation and UTC conversion, independent of process TZ, storage and hardware.
bool ps_datetime_local_to_utc(int year,int month,int day,int hour,int minute,int offset_minutes,uint32_t *utc);
/// 主系统提供：在RTC或网络校时后更新UTC单调锚点。/ Core callback: anchor UTC after RTC/network synchronization.
void ps_clock_set_utc(uint32_t utc);

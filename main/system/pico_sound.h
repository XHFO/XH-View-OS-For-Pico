/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 *
 * 中文：产品提示音策略；GPIO仅由独立工作器通过板级蜂鸣器驱动访问。
 * English: Product feedback policy; only its worker accesses GPIO through the board buzzer driver.
 * 冻结：提示音不刷屏、不延长活动时钟；电源边界先停收并确认工作器空闲。
 * Frozen: Feedback never redraws or extends activity; power transitions stop admission and require an idle worker.
 */
#pragma once
#include "pico_system.h"

enum {
    PS_SOUND_ENABLED = 1, PS_SOUND_TOUCH = 2, PS_SOUND_CHARGING = 4,
    PS_SOUND_ERROR = 8, PS_SOUND_ALL = 15
};
typedef enum { PS_SOUND_NONE, PS_SOUND_TAP, PS_SOUND_CHARGE, PS_SOUND_FAILURE } ps_sound_event_t;
typedef struct {
    uint8_t options, last_event;
    bool available, simulated, suspended, busy;
    uint32_t played;
} ps_sound_snapshot_t;

/// 启动时创建单一工作器，输入路径不分配任务或等待声音。/ Create one worker at startup; input never allocates a task or waits for audio.
void ps_sound_init(void);
uint8_t ps_sound_options(void);
/// 同一独立NVS键原子保存；失败保留旧选项和全部已有设置。/ Atomically save one separate NVS key; failure retains old options and all existing settings.
bool ps_sound_configure(bool enabled,bool touch,bool charging,bool error);
/// 仅有效接触上升沿发声；无效读数不伪造抬手，多点和移动不连响。/ Sound only valid contact rises; read errors never invent a release, and multitouch/motion never repeat.
void ps_sound_contact(bool valid,bool down,unsigned count,int64_t now_ms);
/// 已接受的实体按键操作短音；沿用操作音开关，不伪造触摸边沿。/ Cue an accepted physical-key action using the interaction preference, without changing touch edges.
void ps_sound_key(void);
/// 充电状态1未接入、2充电、3推定充满；首次有效值只建基线。/ Charge states: 1 absent, 2 charging, 3 inferred full; the first valid value only establishes a baseline.
void ps_sound_charge_sample(bool valid,unsigned charge_state,int64_t now_ms);
void ps_sound_error(void);
/// 非阻塞停收/清队列；quiet前不得进入睡眠，以免短音任务额外唤醒CPU。/ Stop admission/clear pending feedback without waiting; sleep requires quiet to avoid audio task wakeups.
void ps_sound_pause(void);
bool ps_sound_quiet(void);
void ps_sound_resume(void);
void ps_sound_snapshot(ps_sound_snapshot_t *out);
void ps_sound_render(uint8_t *fb);
bool ps_sound_event(const ps_event_t *event);

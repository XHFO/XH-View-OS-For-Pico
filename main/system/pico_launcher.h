/*
 * SPDX-License-Identifier: Apache-2.0
 * 中文：应用集聚合系统入口与已安装TF应用，复用原有导航和载入器。
 * English: App collection combines system destinations and installed TF apps using the existing router and loader.
 * 冻结：扫描仅在进入/刷新时执行；绘制纯查询；不改首页顺序、不绕过应用保存与介质独占。
 * Frozen: Scan only on entry/refresh; rendering is pure; preserve Home order and app-save/media-ownership guards.
 */
#pragma once
#include "pico_system.h"
/// 进入或显式刷新时扫描应用目录。/ Scan app directories on entry or explicit refresh.
void ps_launcher_enter(void);
/// 纯绘制应用下载入口、系统入口与TF应用网格。/ Pure rendering of the app-download entry, system destinations and TF app grid.
void ps_launcher_render(uint8_t *fb);
/// 应用下载入口、网格点击、刷新和上下分页；左右滑动由系统首页导航拥有。/ App download, grid activation, refresh and vertical paging; Home navigation owns horizontal swipes.
bool ps_launcher_event(const ps_event_t *event);
/// 左右实体键按页翻动；返回仍由系统壳负责。/ Side keys page the grid; the shell retains Back ownership.
bool ps_launcher_key(int key);

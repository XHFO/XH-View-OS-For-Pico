/*
 * SPDX-FileCopyrightText: 2026 XH Lab
 * SPDX-License-Identifier: Apache-2.0
 *
 * 中文：首页入口注册表、用户排序与编排页；不负责页面切换或应用加载。
 * English: Home destination registry, saved ordering and editor; routing/loading belong to the shell.
 * 冻结：稳定ID独立于路由枚举；设置始终由系统壳单独提供；至少保留一个可用入口，完成才保存。
 * Frozen: Stable IDs are independent of route enums; Settings remains shell-owned; retain one usable destination and save only on Done.
 */
#pragma once
#include "pico_system.h"

enum { PS_NAV_MAX_VISIBLE = 5, PS_NAV_MAX_ENTRIES = 16 };
enum { PS_NAV_BOOKS = 1, PS_NAV_HOME = 2, PS_NAV_GAMES = 3, PS_NAV_PRINTERS = 4,
       PS_NAV_CUSTOM_FIRST = 256, PS_NAV_APPS = 513 };

typedef struct {
    uint16_t id;                 ///< 持久稳定标识；自定义应用从256开始。/ Stable persisted ID; custom apps start at 256.
    ps_route_t route;            ///< 内置页面，外部应用可使用PS_ROUTE_COUNT。/ Built-in route, or PS_ROUTE_COUNT for external apps.
    ps_icon_t icon;              ///< 系统图标。/ System icon.
    const char *label;           ///< 生命周期覆盖注册表的名称。/ Label must outlive the registry.
    void (*launch)(uint16_t id); ///< 可选自定义启动回调，系统壳负责调用。/ Optional app launcher, invoked by the shell.
} ps_navigation_entry_t;

/// 初始化内置入口并恢复顺序；随后注册已安装的自定义应用。/ Restore built-ins/order, then register installed custom apps.
void ps_navigation_init(void);
/// 自定义应用必须用稳定ID；不自动添加到首页；名称与回调由调用方持有。/ Register a stable custom ID without adding it; caller owns label/callback lifetime.
bool ps_navigation_register(const ps_navigation_entry_t *entry);
/// 移除已卸载应用的运行注册，保留用户排序中的稳定ID供重装恢复。/ Unregister a removed app while retaining its saved stable ID for reinstall.
void ps_navigation_unregister(uint16_t id);
/// 纯查询可见入口；暂无可用注册时回退书架，不触碰存储。/ Pure visible lookup, falling back to Books when no registered item is available.
int ps_navigation_count(void);
const ps_navigation_entry_t *ps_navigation_item(int index);
/// 纯查询所有已注册入口，包含未放在首页的应用；不改变用户排序。/ Pure catalog lookup includes apps hidden from Home without changing saved order.
int ps_navigation_catalog_count(void);
const ps_navigation_entry_t *ps_navigation_catalog_item(int index);
/// 精确根页匹配；不存在返回-1。/ Match a root route exactly, or return -1.
int ps_navigation_index(ps_route_t route);
/// 存储维护迁移时保存正在使用的入口，不提交未完成草稿。/ Persist live destinations for storage migration, without committing a draft.
bool ps_navigation_storage_save(void);
/// 编排使用草稿；取消不保存，完成事务保存失败仍保留草稿。/ Edit a draft; cancel does not persist and failed Done retains the draft.
void ps_navigation_enter(void);
void ps_navigation_render(uint8_t *fb);
bool ps_navigation_event(const ps_event_t *event);
/// 关闭选择浮层后返回true；否则丢弃草稿并由系统壳返回来源页。/ Return true after closing picker; otherwise discard draft and let shell navigate back.
bool ps_navigation_back(void);

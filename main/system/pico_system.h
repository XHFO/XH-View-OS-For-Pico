/*
 * 小纸 Pico 产品界面的共享契约，按网页 demo 的 360×640 设计坐标绘制。
 * Shared product UI contract, using the web demo's 360x640 design coordinates.
 * 冻结：保留真实硬件与演示边界；只写自有目录，普通UI格式化须两次确认；可信维护仅明确授权后可发起。
 * Frozen: Distinguish hardware from demo data; write owned files. UI formatting requires two confirmations; trusted maintenance requires explicit authorization.
 */
#pragma once
#include "app.h"
#include <stddef.h>
#include <time.h>

typedef enum {
    PS_SHELF, PS_READ, PS_RECORDS, PS_HA, PS_PRINTERS, PS_DEVICE,
    PS_WIFI, PS_HA_SETTINGS, PS_PRINTER_SETTINGS, PS_READING_SETTINGS,
    PS_INPUT_SETTINGS, PS_SLEEP_SETTINGS, PS_QUICK_SETTINGS, PS_GAMES,
    PS_PLAYGAME, PS_ONBOARDING, PS_CLOCK, PS_QUOTES, PS_IMPORT, PS_EDITOR,
    PS_STORAGE, PS_TRANSFER, PS_USB_TRANSFER, PS_ARCADEGAME, PS_BLUETOOTH, PS_DATETIME, PS_POWER, PS_NAV_SETTINGS, PS_APPLICATION, PS_CANVAS, PS_WIRELESS_MANAGE, PS_SOUND, PS_CLOUDFLARE, PS_APPS, PS_DUAL, PS_ROUTE_COUNT
} ps_route_t;
typedef enum { PS_PRESS, PS_MOVE, PS_RELEASE, PS_TAP, PS_LONG, PS_SWIPE_L,
    PS_SWIPE_R, PS_SWIPE_U, PS_SWIPE_D, PS_CANCEL } ps_event_type_t;
typedef struct { ps_event_type_t type; int x0,y0,x,y; int64_t ms; } ps_event_t;
typedef struct { int w,h; bool landscape; } ps_view_t;
typedef enum { PI_BOOK, PI_HOME, PI_SETTINGS, PI_BACK, PI_CLOSE, PI_SEARCH,
    PI_IMPORT, PI_NOTE, PI_MARK, PI_LAYOUT, PI_CLOCK, PI_ROTATE, PI_WIFI,
    PI_LIGHT, PI_SWITCH, PI_FAN, PI_THERMOMETER, PI_CLIMATE, PI_PRINTER,
    PI_GAME, PI_CHECK, PI_PLUS, PI_MINUS, PI_REFRESH, PI_LOCK, PI_MORE, PI_BLUETOOTH,
    PI_HIGHLIGHT, PI_LIST, PI_HEART, PI_BATTERY, PI_USB, PI_SD, PI_SLIDERS,
    PI_PAUSE, PI_PLAY, PI_POWER, PI_GRID, PI_LINK, PI_DROPLET, PI_COUNT } ps_icon_t;

ps_view_t ps_view(void);
ps_route_t ps_route(void);
/// 只读上一层来源；顶层无历史时返回书架。/ Read the prior route, or the shelf for a top-level entry.
ps_route_t ps_previous_route(void);
const char *ps_version(void);
int64_t ps_now(void);
void ps_navigate(ps_route_t route);
void ps_switch(int section);
void ps_back(void);
void ps_sleep(void);
void ps_wake(void);
void ps_rotate(void);
/// 应用双机临时方向或恢复系统方向，不修改两者的偏好。/ Apply paired display direction or restore the system direction without changing preferences.
void ps_dual_rotation_changed(void);
void ps_rotation_lock(bool locked);
bool ps_rotation_locked(void);
/// 双机正文继承本机竖屏方向并在空闲时采样180度翻转。/ Paired text retains local portrait orientation and samples 180-degree flips while idle.
void ps_book_rotation_enter(app_ctx_t* ctx);
bool ps_book_rotation_tick(app_ctx_t* ctx, bool idle);
void ps_toast(const char *text);
void ps_dirty(void);
/// 手势/覆盖层/快捷编排/按住/切页期间暂停非必要后台文件读取。/ Pause optional file reads during input, overlays, quick editing or navigation.
bool ps_background_io_allowed(void);
/// 只读后台工作暂停原因位图，供可信维护诊断。/ Read-only background pause flags for trusted diagnostics.
unsigned ps_background_io_blockers(void);
bool ps_load(const char *key,void *out,size_t bytes);
bool ps_store(const char *key,const void *data,size_t bytes);
/// 检查点仍须遵守文件独占与介质失效状态；查询不访问文件。/ Checkpoints retain ownership and media-loss gates without file access.
bool ps_storage_save_allowed(void);
/// 仅访问当前可用图书存储的系统目录，用于显式复制阅读记录；不切换活动库。/ Access only available book-store system roots for explicit record copying, without switching the active store.
bool ps_load_at(const char *root,const char *key,void *out,size_t bytes);
bool ps_store_at(const char *root,const char *key,const void *data,size_t bytes);
bool ps_store_exists_at(const char *root,const char *key,bool *exists);
bool ps_storage_record_roots(char *current,char *other,size_t capacity,bool *current_is_sd);
/// 仅写TF上固定的阅读快照目录；不改变源记录或活动存储。/ Write only fixed TF reading snapshots, without changing source records or the active store.
bool ps_reading_snapshot_store(const char *root,const char *key,const void *data,size_t bytes,uint32_t *crc);

/// 存储维护独占文件访问，完成后只向空的新目录迁移本次数据。/ Storage maintenance owns file access and adopts only fresh directories.
bool ps_storage_busy(void);
bool ps_storage_prepare(void);
void ps_storage_resume(void);
/// 挂载恢复后重建媒体消费者；换卡不继承原会话写入资格。
/// Restore media consumers without allowing a replacement card to inherit session writes.
void ps_media_restored(void);
/// 产品阅读子页也须停止共享媒体消费者并保护未保存内容。
/// Product reading subpages must also stop shared consumers and protect unsaved state.
void ps_media_lost(void);
bool ps_storage_rebind(bool formatted);
const char *ps_storage_target(void);
void ps_storage_enter(void);
void ps_storage_render(uint8_t *fb);
bool ps_storage_event(const ps_event_t *event);
bool ps_storage_tick(void);
bool ps_storage_back(void);
/// 仅主循环明确授权的可信维护请求；普通触屏仍须两次确认。/ Main-loop authorized maintenance only; ordinary touch still requires two confirmations.
bool ps_storage_request_format(void);
/// 外部块设备独占，返回后仍须等待USB停止和挂载校验。/ External block ownership; stop USB and verify remount before release.
bool ps_external_prepare(void);
bool ps_external_finished(bool changed);
/// UI已授予内容HTTP的独占文件租约；回调线程不访问本地UI状态。/ UI-granted exclusive content-HTTP lease; callback threads never operate local UI state.
bool ps_content_held(void);
void ps_usb_init(void);
void ps_usb_enter(void);
void ps_usb_render(uint8_t *fb);
bool ps_usb_event(const ps_event_t *event);
bool ps_usb_tick(void);
bool ps_usb_held(void);
bool ps_usb_busy(void);
bool ps_usb_request_start(void);
/// 后台独占TF且保留无文件操作，仍须主机弹出后接回。/ Export TF in the background with memory-only UI until safe host eject/reclaim.
bool ps_usb_request_background(void);
bool ps_usb_background(void);
/// 后台交接可用的无TF文件路由；不授权文件消费者。/ Memory-only routes during background handoff; never grants file-consumer access.
bool ps_usb_local_route(ps_route_t route);
void ps_usb_background_render(uint8_t *fb);
bool ps_usb_before_leave(ps_route_t target);
bool ps_usb_back(void);
void ps_maintenance_tick(void);
/// 本机书架只读缓存快照，无文件访问。/ Read-only local shelf cache, without file access.
void ps_reader_library_status(unsigned *count, bool *loading, bool *limited);
bool ps_reader_storage_save(void);
/// 电源保存不释放当前显示所需的封面缓存。/ Persist for power states without releasing displayed cover caches.
bool ps_reader_power_save(void);
/// 存储交接后不得复用上次成功检查点。/ Invalidate successful checkpoints after storage ownership changes.
void ps_reader_checkpoint_invalidate(void);
bool ps_home_storage_save(void);
void ps_home_storage_invalidate(void);
/// 时钟翻动期间的纯状态查询，用于快速反馈与灰阶定稿。/ Pure clock-motion state for fast feedback and grayscale settling.
bool ps_home_animating(void);
bool ps_games_storage_save(void);
void ps_open_legacy(app_ctx_t *ctx,const app_desc_t *app);
void ps_legacy(const app_desc_t *app);
/// 产品来源子页共享两角菜单与全局返回；工程入口保持独立。/ Product child pages share global shortcuts/back; engineering entry remains independent.
void ps_global_enable(bool enabled);
void ps_global_cancel_input(void);
/// 仅文字编辑器允许独立采样；覆盖层、USB/存储和其他页面保留原路径。/ Independent capture is text-editor only; overlays, USB/storage and other pages retain the original path.
bool ps_keyboard_capture_active(void);
/// 原画面无待绘制、未完成驱动或浮层恢复债务才可捕获复用或局刷。/ Capture/reuse or partially lock only without pending paint, drive or overlay recovery.
bool ps_power_partial_safe(void);
void ps_global_open_settings(app_ctx_t *ctx);
void ps_global_page(app_ctx_t *ctx,const app_desc_t *app);
bool ps_global_input(app_ctx_t *ctx,const app_desc_t *app,app_redraw_t *redraw);
bool ps_global_present(app_ctx_t *ctx,const app_desc_t *app,app_redraw_t redraw);
void ps_global_draw_controls(const app_desc_t *app,uint8_t *fb);
void ps_global_book_return(bool enabled);
void ps_global_prefer_real_book(bool enabled);
void ps_edit(const char *title,const char *initial,size_t max_bytes,void (*done)(const char*));
/// 大数字键盘，decimal允许地址分隔点；步骤提交不关闭下一编辑器。/ Large digit pad; decimal permits address dots and submitting preserves the next editor.
void ps_edit_numeric(const char *title,const char *initial,size_t max_bytes,bool decimal,
                     const char *submit,const char *hint,void (*done)(const char*));
/// 密码使用英文/完整ASCII符号键盘，离开即清除输入缓存。/ Password editor uses English/full ASCII symbols and clears its buffer on exit.
void ps_edit_password(const char *title,size_t max_bytes,void (*done)(const char*));
/// 通用令牌不使用Wi-Fi长度规则，同样遮蔽并在离页清除。/ Generic tokens are masked and wiped on exit without Wi-Fi length constraints.
void ps_edit_secret(const char *title,size_t max_bytes,void (*done)(const char*));
void ps_connectivity_init(void);
void ps_connectivity_enter(ps_route_t route);
void ps_connectivity_render(uint8_t *fb);
bool ps_connectivity_event(const ps_event_t *event);
bool ps_connectivity_tick(void);
bool ps_connectivity_back(void);
bool ps_connectivity_before_leave(ps_route_t target);
void ps_connectivity_media_lost(void);
bool ps_connectivity_shutdown(void);
const char *ps_network_label(void);
int ps_wifi_count(void);
const char *ps_wifi_name(int index);
int ps_wifi_connected_index(void);
bool ps_demo_home_online(void);
void ps_demo_connection(bool wifi,bool ha);
int ps_sleep_style(void);
int ps_excerpt_interval_minutes(void);
void ps_set_sleep_style(int style);
/// 仅在持久保存成功后应用锁屏样式。/ Apply a lock style only after successful persistent storage.
bool ps_apply_sleep_style(int style);
void ps_set_excerpt_interval(int minutes);
bool ps_wall_time(struct tm *out);
void ps_clock_set_utc(uint32_t utc);
int ps_reader_cover_sources(char paths[][288],int capacity);
int ps_battery_percent(void);
bool ps_charging(void);
/// PMU缓存确认外接电源，包括满电仍供电；未知/故障不视为外电。/ Cached PMU-confirmed external power, including full charge; unknown/fault never proves external power.
bool ps_external_power(void);

// 绘制与命中共用逻辑坐标，文字按行顶定位。/ Shared drawing/hit coordinates; text uses its top edge.
EpdRect ps_rect(int x,int y,int w,int h);
bool ps_hit(int x,int y,int rx,int ry,int rw,int rh);
void ps_fill(uint8_t *fb,int x,int y,int w,int h,int gray);
void ps_line(uint8_t *fb,int x0,int y0,int x1,int y1,int gray);
void ps_box(uint8_t *fb,int x,int y,int w,int h,int radius,int gray,bool filled);
void ps_text(uint8_t *fb,int x,int y,int size,const char *text,int gray);
/// 固定字号的提示文字；超宽仅截短，不缩字。/ Fixed-size prompts truncate rather than resize.
void ps_text_fixed(uint8_t *fb,int x,int y,int size,const char *text,int gray);
/// 只画光圈轮廓，避开静态圆形按钮，不重画其底色或图标。/ Draw only a cursor outline, excluding static circular buttons.
void ps_cursor_ring(uint8_t *fb,int x,int y,int radius,const EpdRect *buttons,int count);
void ps_body_text(uint8_t *fb,int x,int y,int size,const char *text,int gray);
void ps_center(uint8_t *fb,int x,int y,int size,const char *text,int gray);
void ps_wrap(uint8_t *fb,int x,int y,int w,int size,int lines,const char *text,int gray);
void ps_icon(uint8_t *fb,int x,int y,int size,ps_icon_t icon,int gray);
void ps_button(uint8_t *fb,int x,int y,int w,int h,const char *label,bool selected);
void ps_heading(uint8_t *fb,const char *title,const char *subtitle);
void ps_tabs(uint8_t *fb,int selected);
void ps_corner_controls(uint8_t *fb);

void ps_reader_media_lost(void);
void ps_reader_init(void);
void ps_reader_enter(ps_route_t route);
void ps_reader_render(uint8_t *fb);
/// 编辑器只清绘变更区，坐标为系统逻辑尺寸；false要求完整绘制，零区表示无需推屏。
/// Paint editor damage in system logical coordinates; false requests a full render, zero areas need no present.
bool ps_reader_editor_render_partial(uint8_t *fb,EpdRect areas[4],int *count);
/// 覆盖层、转向或强刷后放弃编辑器局部帧基准。/ Invalidate editor damage state after overlays, rotation or forced refresh.
void ps_reader_editor_invalidate(void);
/// 旧输入接口兼容：文字编辑器不再采集笔迹，查询恒为false。/ Legacy input compatibility: text editors no longer capture ink; queries return false.
bool ps_reader_editor_ink_mode(void);
bool ps_reader_editor_ink_active(void);
/// 保留旧全局中断调用契约。/ Preserve the legacy global-interruption call contract.
void ps_reader_editor_ink_cancel(void);
/// TF应用与画板局部绘制。/ TF application and drawing damage interface.
bool ps_apps_open(const char *id);
void ps_apps_register_navigation(void);
bool ps_apps_ink_mode(void);
bool ps_apps_ink_render_partial(uint8_t *fb,EpdRect areas[4],int *count);
void ps_apps_ink_invalidate(void);
void ps_apps_ink_cancel(void);
bool ps_reader_event(const ps_event_t *event);
bool ps_reader_key(int key);
bool ps_reader_tick(void);
void ps_reader_open(int book);
bool ps_reader_is_open(void);
bool ps_reader_prefers_grayscale(void);
void ps_reader_tools(void);
/// 在当前示例书中打开目录；不切换到真实图书引擎。/ Open the current sample's contents without switching book engines.
void ps_reader_toc(void);
void ps_reader_action(int action);
void ps_reader_records_tab(int tab);
bool ps_reader_file_favorite(const char *path,bool toggle);
void ps_reader_real_records_tab(const char *path,uint64_t bytes,int tab);
int ps_reader_excerpt_count(void);
bool ps_reader_excerpt(int index,char *quote,size_t bytes,char *source,size_t source_bytes);

void ps_home_action(int action);
bool ps_home_local_network(void);
const char *ps_home_connection_label(void);
bool ps_home_service_configured(void);
void ps_home_init(void);
void ps_home_enter(ps_route_t route);
/// 家庭/打印机子视图不接管启动器滑动。/ Home/printer child views keep their own gestures.
bool ps_home_launcher_visible(void);
void ps_home_render(uint8_t *fb);
bool ps_home_event(const ps_event_t *event);
bool ps_home_key(int key);
bool ps_home_tick(void);
/// TF应用目录与事务；删除包保留独立存档。/ TF app catalog and transactions; removal retains namespaced saves.
void ps_games_rescan(void);
unsigned ps_games_count(void);
/// 只读已扫描TF游戏目录；名称指针仅在下一次扫描前有效。/ Read the scanned TF-game catalog; labels remain valid until the next scan.
bool ps_games_item(unsigned index,const char **id,const char **title);
/// 按稳定包ID启动游戏，复用保存、介质检查与单模块载入流程。/ Open a stable game package ID through existing save, media and single-module guards.
bool ps_games_open_id(const char *id);
bool ps_games_install(const char *staged_path,bool replace);
bool ps_games_remove(const char *id);
const char *ps_games_last_error(void);
void ps_games_media_lost(void);
bool ps_games_prepare_storage(void);
void ps_games_action(int action);
bool ps_games_is_arcade(void);
bool ps_games_is_snake(void);
bool ps_games_paused(void);
void ps_games_init(void);
void ps_games_enter(ps_route_t route);
void ps_games_render(uint8_t *fb);
bool ps_games_event(const ps_event_t *event);
bool ps_games_key(int key);
bool ps_games_tick(void);

extern const app_desc_t app_pico_system;

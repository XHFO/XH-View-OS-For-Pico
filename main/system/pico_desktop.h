/*
 * SPDX-License-Identifier: Apache-2.0
 * 中文：配对电脑的语义画板和图片接收与本地显示；不接管网络或文件传输。
 * English: Paired semantic-board/media reception and local rendering without owning networking or transfers.
 * 冻结：网络请求必须鉴权；USB可分块接收有界画板而不导出TF，原子保存自有文件；仅UI空闲时应用明确的展示意图。
 * Frozen: Authenticate network requests; bounded chunked USB canvases never export TF, owned files are persisted atomically, and explicit presentation intent is applied only by an idle UI.
 */
#pragma once
#include "pico_system.h"
/// 注册管理扩展和可选首页入口。/ Register management extension and optional home destination.
void ps_desktop_init(void);
/// 事件循环接收已提交画布；忙时延后，保留当前页面，仅画板可见时请求重绘。/ Adopt committed canvases when idle, retaining the current route and requesting paint only for a visible board.
bool ps_desktop_tick(void);
/// 准备最新已保存画板；不切页，不占用或导出TF卡。/ Prepare the latest saved board without navigation or exporting TF.
bool ps_desktop_prepare(void);
/// 准备独立应用频道；不复用其他应用内容。/ Load the selected app channel without borrowing another app's image.
bool ps_desktop_prepare_app(bool cloudflare);
/// 只读当前频道是否已准备。/ Query loaded-channel availability without storage I/O.
bool ps_desktop_app_available(bool cloudflare);
/// 已采纳画板的绘制版本，包含本地时间变化；独立于HTTP/USB数据回执版本。/ Adopted paint revision including local time changes, separate from HTTP/USB data receipt revisions.
unsigned ps_desktop_revision(void);
/// 只绘制已载入的画布，不读文件。/ Paint an already loaded canvas without file I/O.
void ps_desktop_render(uint8_t *fb);
/// 只绘制匹配的已载入频道，不读盘。/ Render only the matching loaded channel without I/O.
void ps_desktop_render_app(uint8_t *fb,bool cloudflare);
/// 显示出口完成后记录成功/失败，不等同用户验收。/ Record display-boundary success/failure, not physical user acceptance.
void ps_desktop_presented(bool success);
/// 已经由管理层鉴权的共享请求；返回HTTP状态。/ Shared request after management authorization; returns HTTP status.
int ps_desktop_handle(const char *method,const char *path,const char *type,const void *body,size_t bytes,char *out,size_t capacity);

/// 从专有TF目录准备兼容1位或原生16灰4位BMP；失败不改样式。/ Prepare compatible one-bit or native 16-gray four-bit BMPs from the owned TF directory without changing the style on failure.
bool ps_lock_image_prepare(void);
/// 返回最近准备或上传确认的图片可用性；不读卡。/ Return last prepared or adopted image availability without storage I/O.
bool ps_lock_image_available(void);
/// 等比绘制已准备锁屏，不访问文件。/ Aspect-fit a prepared lock image without file access.
void ps_lock_image_render(uint8_t *fb);
/// 唤醒后释放图片RAM，保留TF文件。/ Release lock-image RAM after wake, retaining TF files.
void ps_lock_image_release(void);
/// 鉴权后读取专有锁屏BMP；成功时调用者释放data。/ Read the owned BMP after authorization; caller frees data on success.
int ps_desktop_image_read(const char *path,uint8_t **data,size_t *bytes);

/// USB安全交还后检查专有画板提交；不在MSC占用卡时读文件。/ Check owned board commits after safe USB reclamation, never while MSC owns the card.
void ps_desktop_usb_reclaimed(void);
/// 只读USB画板能力与任务回执JSON。/ Read-only USB board capability and task-receipt JSON.
void ps_desktop_usb_status(char *out,size_t capacity);
/// 有界USB画板控制；CHUNK成功时返回所需二进制长度，完成后才应答，不接收路径。/ Bounded USB board control; accepted CHUNK returns its exact binary length and replies after payload, never accepting paths.
bool ps_desktop_usb_direct_command(const char *line,char *out,size_t capacity,size_t *binary_bytes);
/// 提交一块完整的二进制载荷；长度或CRC失败保留之前的画板。/ Submit one complete binary chunk, preserving the prior canvas on length or CRC failure.
void ps_desktop_usb_direct_payload(const void *data,size_t bytes,char *out,size_t capacity);
/// 清理超时或TF交接期间的半成品；不挂载或导出磁盘。/ Discard timed-out or storage-handoff staging without mounting or exporting a volume.
void ps_desktop_usb_direct_tick(void);
/// USB接收任务仍有效；主循环据此丢弃超时的部分载荷。/ Whether a USB reception is live, allowing the parser to discard an expired partial payload.
bool ps_desktop_usb_direct_active(void);

/// PMU缓存供电遥测JSON；未知不表示电池或外接供电，不触发I2C。/ Cached PMU power telemetry JSON; unknown proves neither battery nor external power and never triggers I2C.
void ps_desktop_power_status(char *out,size_t capacity);

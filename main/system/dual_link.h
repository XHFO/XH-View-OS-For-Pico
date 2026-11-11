/* SPDX-License-Identifier: Apache-2.0
 * 双机实验分支的有界局域网书库与阅读状态协议。/ Bounded LAN library and reading protocol for the dual-reader branch.
 * 冻结：单线程增量执行；只追加校验成功的图书，永不覆盖或删除用户文件。
 * Frozen: Incremental single-thread execution; append verified books, never overwrite or delete user files.
 */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#define DL_PATH 288
#define DL_BOOKS 512
#define DL_FILE_MAX (128u * 1024u * 1024u)
#define DL_PORT 18771
#define DL_CONTROL_MAX 96
#define DL_FONT_DEFAULT 48
#define DL_FONT_MIN 36
#define DL_FONT_MAX 72
#define DL_FONT_STEP 4
typedef struct dual_link dual_link_t;
typedef struct {
    bool started; ///< 是否已有主设备进度。/ Whether primary progress exists.
    uint8_t pct; ///< 全书百分比。/ Whole-book percentage.
    uint32_t recent; ///< 主设备阅读顺序。/ Primary reading order.
} dl_book_progress_t;
typedef bool (*dl_progress_get_t)(const char* path, uint32_t bytes, dl_book_progress_t* out, void* context);
typedef struct {
    int role;  ///< 1主机，2从机。/ 1 primary, 2 secondary.
    uint8_t device_id[8]; ///< 本机稳定身份；用于证明重连仍是同一设备。/ Stable local identity for same-peer fast validation.
    char host[48], code[7];
    unsigned port;
    char roots[3][DL_PATH], destination[DL_PATH], font[DL_PATH];
    unsigned root_count;
    dl_progress_get_t progress_get; ///< 主设备读取本机进度；从设备可省略。/ Primary progress lookup; optional on secondary.
    void* progress_context; ///< 回调查询上下文。/ Progress lookup context.
} dl_config_t;
typedef struct {
    uint8_t book[32], font[32];
    uint32_t revision, spread, total;
    uint8_t font_px; ///< 两端共用正文字号。/ Shared body font size.
    bool reflow_pending; ///< 主机按原正文锚点重新分页，副机继续等候。/ Primary reflows its text anchor while the secondary waits.
    uint8_t reflow_from_px; ///< 重排前字号，用于未打开正文时恢复旧页锚点。/ Previous size for recovering the old-page anchor when the reader is closed.
} dl_reading_t;
typedef struct {
    bool inverted, primary_flip, secondary_flip; ///< 主机自动基准与两个独立翻转开关。/ Primary automatic base and two independent flips.
} dl_orientation_t;
typedef struct {
    bool connected, data_connected, scanning, synced, busy;
    bool peer_id_valid; ///< 已收到对端稳定设备身份。/ Peer stable identity received.
    bool fast_verified; ///< 仅凭BLE身份与目录摘要完成校验，未进入TCP清单交换。/ Verified from BLE identity and catalog without TCP manifest exchange.
    uint8_t peer_device_id[8]; ///< 对端稳定身份。/ Peer stable identity.
    uint8_t catalog[32]; ///< 双方一致时的书库目录摘要。/ Matching library catalog digest.
    bool request_pending; ///< 从机请求已排队。/ Secondary request is queued.
    char host[48]; ///< 已确认的数据通道端点。/ Confirmed data-plane endpoint.
    bool left_ready; ///< 主机已完成此版本的左页推屏，从机才能显示。/ Primary presented this revision's left page; secondary may now display.
    unsigned books, peer_books, imported;
    uint32_t received, expected, content_revision;
    uint32_t sent;
    unsigned reused; ///< 本轮重扫复用的已校验文件。/ Verified files reused in this scan.
    uint32_t verified_bytes, resumed; ///< 本轮哈希字节及最近续传偏移。/ Scan bytes hashed and last resumed offset.
    bool listed, peer_listed, done, peer_done, receiving, sending, catalog_valid, peer_catalog_valid;
    const char* error;
    dl_reading_t reading;
    dl_orientation_t orientation;
    bool guard; ///< 两端同步的贴合防误触开关。/ Face-to-face guard flag synchronized across both devices.
    int8_t face, peer_face; ///< 屏幕朝向：+1 朝上，-1 朝下，0 未知。/ Screen face: +1 up, -1 down, 0 unknown.
    bool base_valid; ///< 是否收到校准基准。/ Whether a calibration baseline is available.
    int16_t base_x, base_y, base_z; ///< 桌面展开参考重力向量。/ Desk-unfolded reference gravity vector.
    uint32_t base_seq; ///< 基准版本，用于只应用一次。/ Baseline revision, applied once.
    bool calibration_request; ///< 从机请求主机重新校准。/ Secondary asks the primary to calibrate.
    bool peer_reading; ///< 对端当前是否仍在阅读页。/ Whether the peer is still on the reading page.
    bool annotation_valid; ///< 是否收到划词/批注事件。/ Whether a selection/annotation event is available.
    uint32_t annotation_seq, annotation_chapter, annotation_start, annotation_end;
    uint8_t annotation_type;
    char annotation_text[80];
} dl_status_t;
/// 所有接口只从UI线程使用，tick每次至多一个文件块。/ UI-thread only; tick processes at most one file block.
dual_link_t* dl_create(const dl_config_t* config);
void dl_destroy(dual_link_t* link);
void dl_tick(dual_link_t* link, int64_t now_ms, bool available);
void dl_status(const dual_link_t* link, dl_status_t* out);
/// 明确开书会重新发布版本；同书保留页组，允许重试失败推屏。/ Explicit opening republishes a revision; reopening retains its spread and can retry failed presentation.
bool dl_open_path(dual_link_t* link, const char* path);
/// 原子打开目标书的目标页组；同书等同跳页。/ Atomically open a book at its target spread; the same book seeks instead.
bool dl_open_at(dual_link_t* link, const char* path, uint32_t page);
/// 显式请求对端进入当前共享书；同书也发送 OPEN，而非仅发页码。
/// Explicitly ask the peer to enter the current shared book; a same-book request sends OPEN rather than only a page seek.
bool dl_activate_path(dual_link_t* link, const char* path);
bool dl_turn(dual_link_t* link, int direction);
/// 请求包含目标页的双页组；两端均须连接，页码从 1 开始。/ Request the spread containing a one-based page; both devices must be connected.
bool dl_seek(dual_link_t* link, uint32_t page);
/// 任意端请求共享字号，主机重排后才发布新页码。/ Either device requests a shared size; primary reflow determines the new spread.
bool dl_resize(dual_link_t* link, uint8_t font_px);
/// 仅主机可完成当前重排；spread为零基偶数，完成后仍须实际推屏。/ Primary resolves the current reflow to a zero-based even spread; presentation is still required.
bool dl_reflow_resolved(dual_link_t* link, uint32_t revision, uint32_t spread);
/// 主机重排失败，恢复旧字号与页组，仍须重新推屏。/ Failed primary reflow restores its old size/spread and requires presentation again.
bool dl_reflow_failed(dual_link_t* link, uint32_t revision);
/// UI 确认主机该版本实际推屏成功，旧版本确认不会放行。/ Confirm successful primary presentation; stale revisions cannot release the secondary.
bool dl_presented(dual_link_t* link, uint32_t revision);
void dl_total(dual_link_t* link, uint32_t total);
const char* dl_reading_path(const dual_link_t* link);
const char* dl_font_path(const dual_link_t* link);
/// 只读已校验目录条目；序号不接受外部路径。/ Read a verified catalog entry by index, never an external path.
const char* dl_entry(const dual_link_t* link, unsigned index, uint8_t id[32], uint32_t* bytes);
/// 仅同一可信存储会话复用元数据未变的摘要；换卡、USB/外部写入前须 destroy。
/// Reuse unchanged verified entries only within one trusted storage session; destroy before media/USB/external writes.
void dl_rescan(dual_link_t* link);
void dl_restore(dual_link_t* link, const dl_reading_t* reading);
/// 只读主设备发布的书籍进度；从设备不会借此写入本地进度。
/// Read primary-published book progress; the secondary never persists it locally.
bool dl_book_progress(const dual_link_t* link, const char* path, uint32_t bytes, dl_book_progress_t* out);
/// 主设备进度表更新序号，用于书架按需重绘。/ Primary progress-table revision for on-demand shelf redraws.
uint32_t dl_progress_revision(const dual_link_t* link);
/// 取一次远端显式开书请求，用来解除本机“已离开阅读”锁存。
/// Take a one-shot peer explicit-open request to clear this device's local-away latch.
bool dl_remote_open_take(dual_link_t* link);
bool dl_remote_open_pending(const dual_link_t* link);
/// 主设备发布新的 IPv4 数据端点；从设备收到后自动重连 TCP。
/// Primary publishes a new IPv4 data endpoint; the secondary reconnects TCP automatically.
void dl_endpoint(dual_link_t* link, const char* host);
/// 恢复独立于阅读页码的方向偏好；默认倒置竖屏且两端均不翻转。
/// Restore orientation independently of reading; default inverted portrait with neither device flipped.
void dl_orientation_restore(dual_link_t* link, const dl_orientation_t* orientation);
/// 仅主机更新自动基准，离线也有效，重连后广播。/ Only the primary updates the automatic base, including offline; reconnect broadcasts it.
bool dl_orient(dual_link_t* link, bool inverted);
/// 设置角色1/2的独立180度开关；主机可离线设置，从机须连接且等待确认。
/// Set role 1/2's independent 180-degree switch; primary supports offline updates, secondary requires connection and confirmation.
bool dl_flip(dual_link_t* link, int role, bool enabled);
/// 同步贴合防误触开关；从机发请求，主机确认后广播。
/// Synchronize the face-to-face guard; the secondary requests and the primary broadcasts.
bool dl_guard(dual_link_t* link, bool enabled);
/// 交换低频率屏幕朝向，用于区分折叠贴合与展开使用。
/// Exchange the low-rate screen face to distinguish folded contact from unfolded use.
bool dl_face(dual_link_t* link, int face);
/// 主机广播桌面展开基准；从机请求重新校准。
/// Primary broadcasts the desk-unfolded baseline; secondary requests recalibration.
bool dl_baseline(dual_link_t* link, int16_t x, int16_t y, int16_t z);
bool dl_calibration_request(dual_link_t* link);
void dl_calibration_clear(dual_link_t* link);
/// 通知对端本机是否仍在阅读页；离开时对端冻结当前页，不做偶数翻页。
/// Tell the peer whether this unit is still reading; the peer freezes its page when not.
bool dl_reading_active(dual_link_t* link, bool active);
/// 同步已提交的划词/批注；文本会截断到控制帧上限。
/// Sync a committed selection/annotation; text is truncated to the control-frame limit.
bool dl_annotation(dual_link_t* link, uint8_t type, uint32_t chapter, uint32_t start, uint32_t end, const char* text);

/// 双机游戏手柄输入：手柄机提交，屏幕机读取；BLE 控制通道承载。
/// Dual-game controller input: controller submits, screen consumes; carried by BLE control.
bool dl_game_input(dual_link_t* link, uint8_t input);
bool dl_game_take(dual_link_t* link, uint8_t* input);
/// 主机请求从机同步打开同一游戏。 / Primary asks the secondary to open the same game.
bool dl_game_open(dual_link_t* link, uint8_t kind);
bool dl_game_open_take(dual_link_t* link, uint8_t* kind);
/// 棋类吃子状态小包；主发从收。 / Captured-state packet; primary to secondary.
bool dl_game_packet(dual_link_t* link, const void* data, size_t bytes);
size_t dl_game_packet_take(dual_link_t* link, void* data, size_t capacity);
/// 任一端退出游戏时通知对端一起退出。 / Either side can request the peer to leave the game.
bool dl_game_close(dual_link_t* link);
bool dl_game_close_take(dual_link_t* link);

/// BLE 已加密且认证后才交付控制帧；这些接口只从 UI 线程调用。
/// Deliver control frames only after BLE encryption/authentication; UI-thread only.
void dl_control_connected(dual_link_t* link, bool connected);
size_t dl_control_take(dual_link_t* link, uint8_t* packet, size_t capacity);
bool dl_control_receive(dual_link_t* link, const uint8_t* packet, size_t bytes);

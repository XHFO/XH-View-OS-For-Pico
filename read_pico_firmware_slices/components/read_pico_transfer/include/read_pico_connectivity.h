/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 * 日常WiFi与传书的异步单任务所有权；公开快照不含密码。
 * One asynchronous owner for everyday WiFi and transfer; public snapshots contain no passwords.
 * 冻结：日常联网不挂载存储、不自动启动HTTP；用户可显式开启限时手机配置，传书不格式化介质。
 * Frozen: Everyday WiFi mounts no storage or automatic HTTP; explicit timed phone setup is allowed, and transfer never formats media.
 */
#pragma once
#include "read_pico_transfer.h"

#define READ_PICO_CONNECTIVITY_SAVED_MAX 8

typedef enum {
    READ_PICO_CONNECTIVITY_DISABLED,
    READ_PICO_CONNECTIVITY_IDLE,
    READ_PICO_CONNECTIVITY_SCANNING,
    READ_PICO_CONNECTIVITY_CONNECTING,
    READ_PICO_CONNECTIVITY_CONNECTED,
    READ_PICO_CONNECTIVITY_STOPPING,
    READ_PICO_CONNECTIVITY_ERROR,
} read_pico_connectivity_state_t;

typedef enum {
    READ_PICO_CONNECTIVITY_OP_SYSTEM,
    READ_PICO_CONNECTIVITY_OP_WIFI,
    READ_PICO_CONNECTIVITY_OP_SCAN,
    READ_PICO_CONNECTIVITY_OP_CONNECT,
    READ_PICO_CONNECTIVITY_OP_PROFILE,
    READ_PICO_CONNECTIVITY_OP_TRANSFER,
} read_pico_connectivity_operation_t;

typedef enum {
    READ_PICO_CONNECTIVITY_SCAN_NONE,
    READ_PICO_CONNECTIVITY_SCAN_START,
    READ_PICO_CONNECTIVITY_SCAN_RESULT,
    READ_PICO_CONNECTIVITY_SCAN_TIMEOUT,
    READ_PICO_CONNECTIVITY_SCAN_CANCELLED,
} read_pico_connectivity_scan_failure_t;

typedef struct {
    char ssid[33]; ///< 已记忆网络，不含密码 / Saved network without its password
    bool auto_join; ///< 允许本轮有界自动连接 / Eligible for bounded automatic joining
} read_pico_connectivity_saved_t;

typedef struct {
    bool ready; ///< 凭据加载完成 / Credentials have finished loading
    bool suspended; ///< 已静默交出网络所有权 / Quiesced and released network ownership
    bool busy; ///< 命令排队或正在执行 / A command is queued or executing
    bool wifi_enabled; ///< WiFi已开启，含传书热点 / WiFi enabled, including transfer AP
    bool connected; ///< STA已经取得IP / STA acquired an IP address
    bool connecting; ///< STA正在有界连接 / Bounded STA connection in progress
    bool scanning; ///< 扫描尚未完成 / Scan is still pending
    bool transfer_active; ///< 传书资源存在，false且busy=false后可恢复存储读取 / Transfer owns resources; restore storage reads only after false and not busy
    read_pico_connectivity_state_t state; ///< 网络状态 / Network state
    char ssid[33]; ///< 当前或正在连接的SSID / Current or connecting SSID
    char ip[48]; ///< STA IPv4，未连接为空 / STA IPv4; empty while offline
    int8_t rssi; ///< 当前信号，未连接为0 / Current RSSI, zero while offline
    size_t network_count; ///< 最近扫描数量 / Latest scan count
    read_pico_transfer_network_t networks[READ_PICO_TRANSFER_SCAN_MAX];
    size_t saved_count; ///< 按最近成功顺序排列 / Most recently successful first
    read_pico_connectivity_saved_t saved[READ_PICO_CONNECTIVITY_SAVED_MAX];
    read_pico_transfer_status_t transfer; ///< 既有安全传书快照 / Existing safe-transfer snapshot
    uint32_t submitted_seq; ///< 已接受命令序号 / Last accepted command sequence
    uint32_t completed_seq; ///< 已执行命令序号，连接进展另见connecting / Last executed sequence; connecting reports later network progress
    uint32_t result_seq; ///< 命令及异步结果序号 / Command and asynchronous result sequence
    read_pico_connectivity_operation_t last_operation; ///< 最近结果所属操作 / Operation producing the latest result
    uint32_t scan_completed_seq; ///< 扫描终态序号，接受命令不算完成 / Scan terminal-result sequence; acceptance is not completion
    esp_err_t scan_error; ///< 最近扫描的SDK错误，和连接错误分开 / Latest scan SDK error, separate from connection errors
    read_pico_connectivity_scan_failure_t scan_failure; ///< 扫描失败阶段 / Scan failure phase
    esp_err_t last_error; ///< 最近命令或网络错误 / Latest command or network error
} read_pico_connectivity_status_t;

/// 一次创建worker，异步加载NVS；不接触存储卡。/ Create the worker once; asynchronously load NVS without touching removable storage.
esp_err_t read_pico_connectivity_init(void);
/// 跨任务复制快照，不执行网络/NVS操作。/ Copy a snapshot across tasks without network or NVS operations.
void read_pico_connectivity_get_status(read_pico_connectivity_status_t *out);
/// 小型只读STA联网状态，不复制扫描列表。/ Compact read-only STA online state without copying scan lists.
bool read_pico_connectivity_online(void);
/// 小型电源生命周期快照，避免在UI栈上分配完整扫描列表。/ Compact power-lifecycle snapshot without allocating the full scan list on the UI stack.
typedef struct {
    bool suspended, busy;
    uint32_t submitted_seq, completed_seq;
    esp_err_t last_error;
} read_pico_connectivity_resume_status_t;
void read_pico_connectivity_get_resume_status(read_pico_connectivity_resume_status_t *out);
/// 下列命令复制参数并立即返回；队列满或静默中返回错误，不代表连接已完成。/ Commands copy arguments and return immediately; full queues/quiescence fail, and acceptance does not mean connected.
esp_err_t read_pico_connectivity_set_enabled(bool enabled);
esp_err_t read_pico_connectivity_scan(void);
esp_err_t read_pico_connectivity_connect(const char *ssid, const char *password);
esp_err_t read_pico_connectivity_connect_saved(const char *ssid);
esp_err_t read_pico_connectivity_forget(const char *ssid);
esp_err_t read_pico_connectivity_set_auto_join(const char *ssid, bool enabled);
/// cfg字符串立即复制；回调上下文须保持有效直至传书完全停止。/ Copy cfg strings immediately; callback context must live until transfer fully stops.
esp_err_t read_pico_connectivity_start_transfer(const read_pico_transfer_cfg_t *cfg);
esp_err_t read_pico_connectivity_stop_transfer(void);
/// 清空待执行命令、停止并等待HTTP退出、释放WiFi；超时后仍须等待成功才能接触介质。/ Drain commands, stop and join HTTP, release WiFi; after timeout callers must still wait for success before touching media.
esp_err_t read_pico_connectivity_quiesce(uint32_t timeout_ms);
/// root重入后恢复之前的WiFi偏好；legacy页持有网络时不得调用。/ Resume the saved WiFi preference on root reentry; never call while a legacy page owns networking.
esp_err_t read_pico_connectivity_resume(void);

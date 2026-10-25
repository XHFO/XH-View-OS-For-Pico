/*
 * SPDX-FileCopyrightText: 2026 XH Lab
 * SPDX-License-Identifier: Apache-2.0
 * 家庭/打印服务的有界共享模型，网络由独立适配层提供。
 * Bounded shared home/printer model with a separate transport adapter.
 * 冻结：打印机只读；不伪造登录/状态，不记录凭据，不绕过TLS校验。
 * Frozen: Printers are read-only; never synthesize login/state, log credentials, or bypass TLS validation.
 */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define RP_HOME_ENTITIES 96
#define RP_HOME_PRINTERS 3
#define RP_HOME_IMPORTS 8
#define RP_HOME_ROOMS 24
#define RP_HOME_HTTP_LIMIT (192u*1024u)
#define RP_HOME_MQTT_LIMIT (64u*1024u)
#define RP_HOME_CONFIG_VERSION 3u
#define RP_HOME_CONFIG_PACK_LIMIT 16800u
#define RP_HOME_CONFIG_SIZE_V2 16116u
/// v2 packed sizeof before Cloudflare Access token fields. / Prior packed size before CF Access fields.


typedef enum {RP_HOME_NONE,RP_HOME_HA,RP_HOME_BAMBU_LAN,RP_HOME_BAMBU_CLOUD,RP_HOME_FORMLABS} rp_home_source_t;
typedef enum {RP_HOME_EMPTY,RP_HOME_BUSY,RP_HOME_OK,RP_HOME_OFFLINE,RP_HOME_AUTH,RP_HOME_HTTP,
    RP_HOME_TLS,RP_HOME_INVALID,RP_HOME_TOO_LARGE,RP_HOME_NOMEM,RP_HOME_UNSUPPORTED,RP_HOME_TIME=11,
    RP_HOME_ACCESS=12} rp_home_result_t;
typedef enum {RP_HOME_LIGHT,RP_HOME_SWITCH,RP_HOME_CLIMATE,RP_HOME_FAN,RP_HOME_TEMPERATURE,RP_HOME_HUMIDITY} rp_home_kind_t;

typedef struct {
    char entity[96],room[40];
} rp_home_room_t;
typedef struct {
    /// source是认证边界；Bambu MQTT与Formlabs OAuth字段不得跨来源共存。
    /// / Source is an authentication boundary; Bambu MQTT and Formlabs OAuth fields never coexist across sources.
    uint8_t source,region;
    uint16_t port;
    char name[48],serial[64],host[128],username[96],secret[1024];
    char client_id[128],client_secret[256],certificate[2048];
} rp_home_printer_config_t;
typedef struct {
    uint32_t version;
    uint8_t enabled,external_enabled,external_control,room_count;
    char local[192],external[192],home_wifi[48],ha_token[1024];
    /// Cloudflare Access Service Token；仅外部/远程 HA 请求附带，本地家庭 Wi‑Fi 路径不加。
    /// Cloudflare Access Service Token; attached only on remote/external HA requests, never on local home Wi‑Fi.
    char cf_access_client_id[96],cf_access_client_secret[64];
    rp_home_room_t rooms[RP_HOME_ROOMS];
    rp_home_printer_config_t printers[RP_HOME_PRINTERS];
} rp_home_config_t;
typedef struct {
    char id[96],name[80],state[64],unit[16],room[40];
    uint8_t kind,available,on;
    float number;
} rp_home_entity_t;
typedef struct {
    char name[48],serial[96],state[40],task[128],material[64];
    int percent,remaining_minutes,layer,layers,nozzle,bed;
    uint8_t source,connected,has_data;
    rp_home_result_t result;
    int http_status;
    int64_t sampled_ms;
} rp_home_printer_t;
typedef struct {
    uint32_t revision;
    uint16_t entity_count,entity_total;
    uint8_t import_count,truncated,external,busy,rooms_synced;
    rp_home_result_t result;
    int http_status;
    int64_t sampled_ms;
    rp_home_entity_t entities[RP_HOME_ENTITIES];
    rp_home_printer_t printers[RP_HOME_PRINTERS],imports[RP_HOME_IMPORTS];
} rp_home_snapshot_t;
typedef struct {
    const char *method,*url,*bearer,*content_type,*body;
    /// 可选；非空时附加 CF-Access-Client-Id/Secret（外部 Access）。/ Optional CF Access service-token headers.
    const char *cf_access_client_id,*cf_access_client_secret;
} rp_home_http_request_t;
typedef struct {
    /// 限额内返回完整响应，必须校验证书并禁止跨源重定向。/ Return a complete bounded response, verify TLS, and reject cross-origin redirects.
    rp_home_result_t (*http)(void *,const rp_home_http_request_t *,char *,size_t,size_t *,int *);
    /// 只发MQTT CONNECT/SUBSCRIBE/ACK/PING，禁止任何打印控制PUBLISH。/ Send only CONNECT/SUBSCRIBE/ACK/PING, never printer-control PUBLISH.
    rp_home_result_t (*mqtt)(void *,const rp_home_printer_config_t *,char *,size_t,size_t *);
    void *context;
} rp_home_transport_t;
typedef struct {
    char value[1024];
    int64_t expires_ms;
    uint32_t credential_hash;
} rp_home_token_t;

/// PGA/设备侧配置事务。空凭据保留已存值；clear_* 显式清除。has_printers 为 0 时不改槽位。
/// 只接受空槽或 HA 实体前缀；Bambu/Formlabs 来源被拒绝且不改目标。校验失败不写入。
/// On-device config transaction. Empty credentials keep stored values; clear_* wipes them.
/// has_printers 0 leaves slots unchanged. Only empty or HA-prefix slots are accepted;
/// Bambu/Formlabs sources are rejected and the destination is left unchanged.
typedef struct {
    uint8_t enabled, external_enabled, external_control, clear_token, clear_cf, has_printers;
    const char *local, *external, *home_wifi, *ha_token, *cf_id, *cf_secret;
    uint8_t printer_source[RP_HOME_PRINTERS];
    char printer_name[RP_HOME_PRINTERS][48];
    char printer_serial[RP_HOME_PRINTERS][64];
} rp_home_link_update_t;
bool rp_home_apply_link_update(rp_home_config_t *dst, const rp_home_link_update_t *update);

/// 严格验证地址/主题/凭据范围；不访问网络。/ Validate endpoints/topics/credential bounds without networking.
bool rp_home_config_valid(const rp_home_config_t *config);
/// 旧v1布局按已选source去除异源遗留字段；保留有效设备、HA与其它槽配置。
/// / Upgrade v1 in place by removing foreign-source leftovers while retaining valid printers, HA and other slots.
bool rp_home_config_upgrade(rp_home_config_t *config);
/// 返回原存储版本供迁移写回使用；失败不输出版本。/ Return the original stored version for migration writeback.
bool rp_home_config_unpack_version(const void *,size_t,rp_home_config_t *,uint32_t *stored_version);
/// 有界零游程编码与CRC，压缩空字段；损坏数据不恢复为有效配置。/ Bounded zero-run coding with CRC; never accept corrupt configuration.
size_t rp_home_config_pack(const rp_home_config_t *,void *,size_t);
bool rp_home_config_unpack(const void *,size_t,rp_home_config_t *);
/// 共享解析器；成功才整体替换目标，超额实体明确标记。/ Shared parser atomically replaces successful state and reports truncated entities.
rp_home_result_t rp_home_parse_ha(const char *json,size_t bytes,const rp_home_config_t *,rp_home_snapshot_t *,int64_t now_ms);
rp_home_result_t rp_home_parse_formlabs(const char *json,size_t bytes,const char *serial,rp_home_printer_t *,int64_t now_ms);
rp_home_result_t rp_home_parse_bambu(const char *json,size_t bytes,rp_home_printer_t *,int64_t now_ms);
/// 用户已有OAuth凭据，不收集账户密码，不实现未证实的登录流程。/ Use existing OAuth credentials, never collect account passwords or invent login flows.
bool rp_home_form_body(const char *id,const char *secret,char *body,size_t capacity);
/// 阻塞式工作器入口；UI只排队，不在绘制/触摸线程调用。/ Blocking worker entry; UI queues requests and never invokes it while drawing/handling touch.
rp_home_result_t rp_home_fetch(const rp_home_config_t *,rp_home_snapshot_t *,rp_home_token_t tokens[RP_HOME_PRINTERS],
    const rp_home_transport_t *,bool network_online,bool at_home,int64_t now_ms);
/// 仅允许支持的HA实体与明确动作，打印机没有控制入口。/ Only allow supported HA entities and explicit actions; printers have no control API.
rp_home_result_t rp_home_control(const rp_home_config_t *,const rp_home_entity_t *,const char *action,float value,
    const rp_home_transport_t *,bool at_home,int *http_status);
const char *rp_home_result_text(rp_home_result_t result);

/// 运行时接口：独立懒启动工作器，NVS持久化且不写TF凭据。/ Runtime uses a lazy worker and NVS persistence, never TF credentials.
void read_pico_home_link_init(void);
void read_pico_home_link_get_config(rp_home_config_t *out);
bool read_pico_home_link_configure(const rp_home_config_t *config);
bool read_pico_home_link_request(bool network_online,bool at_home);
uint32_t read_pico_home_link_revision(void);
void read_pico_home_link_get_snapshot(rp_home_snapshot_t *out);
bool read_pico_home_link_control(const rp_home_entity_t *,const char *action,float value,bool at_home);
bool read_pico_home_link_suspend(uint32_t timeout_ms);
void read_pico_home_link_resume(void);

#define RP_HOME_BAMBU_DEVICES 8u
typedef enum { RP_BAMBU_LOGIN, RP_BAMBU_EMAIL_CODE, RP_BAMBU_TFA_CODE, RP_BAMBU_RESEND, RP_BAMBU_REUSE } rp_home_bambu_op_t;
typedef enum { RP_BAMBU_IDLE, RP_BAMBU_BUSY, RP_BAMBU_EMAIL, RP_BAMBU_TFA,
    RP_BAMBU_READY, RP_BAMBU_ERROR, RP_BAMBU_EXPIRED } rp_home_bambu_phase_t;
typedef enum { RP_BAMBU_ERROR_NONE, RP_BAMBU_ERROR_CREDENTIALS, RP_BAMBU_ERROR_CODE_EXPIRED,
    RP_BAMBU_ERROR_CODE_INCORRECT, RP_BAMBU_ERROR_RATE_LIMIT, RP_BAMBU_ERROR_BLOCKED,
    RP_BAMBU_ERROR_RESPONSE, RP_BAMBU_ERROR_TFA_UNSUPPORTED, RP_BAMBU_ERROR_ACCOUNT_CHANGED } rp_home_bambu_error_t;
typedef struct {
    /// 每次提交须匹配GET会话；复用还须匹配配置版本及公开代表槽位；region仅登录时使用。
    /// Match the GET session; reuse also matches the config revision/public representative slot; region is login-only.
    uint32_t session, config_revision;
    uint8_t op, region, saved_slot;
    char account[128], password[256], code[16];
} rp_home_bambu_input_t;
typedef struct { char serial[64], name[48], model[48]; } rp_home_bambu_device_t;
typedef struct {
    /// 已保存账户的公开代表槽位与打印机计数；不暴露账号标识或凭据。
    /// Public representative slot and printer count for a saved account, never identity or credentials.
    uint8_t slot, region, printer_count;
    char name[48];
} rp_home_bambu_saved_account_t;
typedef struct {
    /// 只公开阶段与设备；不包含账户、密码、访问令牌或验证挑战秘密。
    /// Public phases/devices only, never account, password, token or secret challenge data.
    uint32_t session, revision, seconds_left, retry_after, config_revision;
    uint8_t phase, region, count, error, truncated, saved_count;
    /// 复用旧凭据的会话为1，避免导入时反向覆盖其它槽位较新的token。
    /// Reused credentials mark the session as 1 so imports never roll newer sibling-slot tokens backward.
    uint8_t reused;
    rp_home_result_t result;
    int http_status;
    rp_home_bambu_device_t devices[RP_HOME_BAMBU_DEVICES];
    rp_home_bambu_saved_account_t saved_accounts[RP_HOME_PRINTERS];
} rp_home_bambu_state_t;
/// 手机API只排队；经验证的社区云协议由现有后台TLS工作器执行。
/// Phone APIs only queue work; the existing TLS worker executes the researched community protocol.
bool read_pico_bambu_auth_submit(const rp_home_bambu_input_t *, bool online);
void read_pico_bambu_auth_get(rp_home_bambu_state_t *);
/// HTTP取消必须匹配会话；生命周期取消无条件撤销并清除秘密。
/// HTTP cancellation matches the session; lifecycle cancellation unconditionally revokes and wipes secrets.
bool read_pico_bambu_auth_cancel_session(uint32_t session);
void read_pico_bambu_auth_cancel(void);
/// 只复制当前会话已验证列表中的一台设备；调用者事务保存后须擦除输出，不得回传JSON。
/// Copy one retained current-session device; the caller persists transactionally then wipes it, never returns it as JSON.
bool read_pico_bambu_auth_prepare_import(uint32_t session, unsigned index, rp_home_printer_config_t *out);

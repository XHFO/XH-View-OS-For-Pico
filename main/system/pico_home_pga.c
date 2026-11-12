/*
 * SPDX-FileCopyrightText: 2026 XH Lab
 * SPDX-License-Identifier: Apache-2.0
 * 中文：PGA 家庭桥——NVS/HTTP/TLS/快照；门户与管理接口共用配置，所有网络操作在工作任务执行。
 * English: PGA home bridge — shared NVS/HTTP/TLS snapshots for portal/manage; all network operations run in the worker.
 * 修复：配置+快照+令牌约 52KiB，禁止放内部 BSS 或小栈。dev66 静态放入内部 DRAM 后，
 * USB MSC 的 DMA 分配失败，RPICO STATUS 报 usb_error 257（0x101 ESP_ERR_NO_MEM，不是 262 NOT_SUPPORTED）。
 * 大对象只从 PSRAM 分配；HTTP 工作任务保留小的内部栈（TLS 会关 cache，栈不能在 PSRAM）。
 * Fix: config+snapshot+tokens are ~52KiB and must not sit in internal BSS or on a small stack.
 * dev66's internal DRAM copies made USB MSC DMA allocation fail (usb_error 257 / ESP_ERR_NO_MEM).
 * Large objects come only from PSRAM. The HTTP worker keeps a small internal stack because TLS disables cache.
 */
#include "pico_home_pga.h"
#include "read_pico_home_link.h"
#include "read_pico_management.h"
#include "read_pico_connectivity.h"
#include "pico_system.h"
#include "pico_datetime.h"
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "nvs.h"
#include "pico_app_package.h"
#include <stdio.h>
#include <string.h>

/* dev66 常驻内部 BSS：config 16276 + snap 33880 + 3*token 1040 = 53276。工作函数再在 8192 栈上复制这两份。 */
/* dev66 internal BSS: config 16276 + snap 33880 + 3*token 1040 = 53276, then copied again on an 8192-byte stack. */
#define HOME_HA_BYTES offsetof(rp_home_config_t,printers)
#define HOME_SHARE_LIMIT 32768u
typedef struct {
    uint32_t magic,revision,base;
    uint8_t owner[8],ha[HOME_HA_BYTES];
    rp_home_area_scope_t scope;
    rp_home_selection_t selection;
    uint32_t checksum;
} home_shared_t;
typedef struct {
    rp_home_discovery_t discovery, discovery_job;
    char discovery_ssid[33];
    rp_home_config_t config;
    rp_home_snapshot_t snap;
    rp_home_config_t job_config;
    rp_home_snapshot_t job_snap;
    rp_home_token_t tokens[RP_HOME_PRINTERS];
    rp_home_config_t edit;
    uint8_t packed[RP_HOME_CONFIG_PACK_LIMIT];
    rp_home_entity_t control_entity;
    char control_action[24];
    float control_value;
    struct { uint32_t binding; rp_home_area_scope_t scope; uint32_t checksum; } room_record,room_edit;
    rp_home_area_scope_t job_scope;
    rp_home_areas_t areas,job_areas;
    struct { uint32_t binding,revision;rp_home_selection_t value;uint32_t checksum; } selection,selection_edit;
    rp_home_selection_t job_selection;
    pico_app_home_catalog_t catalog;
    home_shared_t shared,shared_edit,shared_draft,shared_in;
    uint8_t share_tx[HOME_SHARE_LIMIT],share_rx[HOME_SHARE_LIMIT];

} pga_store_t;

static SemaphoreHandle_t s_lock;
static TaskHandle_t s_worker;
static pga_store_t *s_store;
static bool s_ready, s_queued, s_busy, s_online, s_at_home, s_stop, s_portal_owner;
static bool s_control_pending, s_running, s_suspended;
static uint32_t s_generation;
static bool s_discover_queued, s_discover_running, s_discover_failed;
static bool s_areas_queued,s_areas_running;
static uint32_t s_areas_revision=1,s_areas_generation;
static rp_home_result_t s_areas_result;
static bool s_catalog_queued,s_catalog_running;
static int s_share_role;
static bool s_draft_conflict;
static bool s_share_deferred,s_draft_pending,s_share_job,s_share_link,s_peer_home,s_home_reading;
static int s_share_result;
static bool s_share_invalidate,s_home_local_reading,s_peer_home_reading;
static uint8_t s_share_owner[8];
static int64_t s_home_visible_until,s_home_poll_due;
static bool shared_save_locked(const rp_home_config_t *,const rp_home_area_scope_t *,const rp_home_selection_t *);
static void shared_load(void);
static void run_share(void);
static uint32_t s_catalog_offset;

#define s_config (s_store->config)
#define s_snap (s_store->snap)
#define s_tokens (s_store->tokens)

_Static_assert(sizeof(rp_home_config_t) + sizeof(rp_home_snapshot_t) + 3 * sizeof(rp_home_token_t) > 50000,
               "home PGA state must stay out of internal DRAM");
static char s_input[256];
static bool s_input_ready, s_input_open;
static int s_nav_edit; /* set by games when opening edit — unused here; games drives IME */

static int64_t now_ms(void) { return esp_timer_get_time() / 1000; }

static void lock(void) { if (s_lock) xSemaphoreTake(s_lock, portMAX_DELAY); }
static void unlock(void) { if (s_lock) xSemaphoreGive(s_lock); }

static bool tls_clock_ok(void) {
    ps_datetime_status_t st;
    ps_datetime_get_status(&st);
    return st.source == PS_TIME_NETWORK || st.source == PS_TIME_MANUAL || st.source == PS_TIME_RTC;
}

static esp_err_t nvs_blob(const char *space, bool writing, void *data, size_t *bytes) {
    nvs_handle_t h;
    esp_err_t err = nvs_open(space, writing ? NVS_READWRITE : NVS_READONLY, &h);
    if (err != ESP_OK) return err;
    err = writing ? nvs_set_blob(h, "settings", data, *bytes)
                            : nvs_get_blob(h, "settings", data, bytes);
    if (writing && err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
    return err;
}

static bool save_config_locked(const rp_home_config_t *value) {
    s_share_deferred=false;
    if(memcmp(value,&s_config,HOME_HA_BYTES))return shared_save_locked(value,NULL,NULL);
    size_t n = rp_home_config_pack(value, s_store->packed, sizeof(s_store->packed));
    bool ok = false;
    if (n) {
        size_t bytes = n;
        ok = nvs_blob("pico_home_pga", true, s_store->packed, &bytes) == ESP_OK;
    }
    memset(s_store->packed, 0, sizeof(s_store->packed));
    return ok;
}

// 房间选择绑定HA地址与认证；更换服务后旧选择不复活，不改变v3凭据布局。
// Bind room selection to HA endpoints/authentication; service changes never revive an old scope or alter the v3 credential layout.
static uint32_t room_hash(const void *data,size_t bytes){
    const uint8_t *p=data;uint32_t h=2166136261u;for(size_t i=0;i<bytes;i++)h=(h^p[i])*16777619u;return h;
}
static uint32_t room_binding(void){
    return room_hash(s_config.local,offsetof(rp_home_config_t,rooms)-offsetof(rp_home_config_t,local));
}
static bool scope_ready(void){
    return s_store->room_record.binding==room_binding()&&s_store->room_record.scope.version==1;
}
static void load_rooms(void){
    size_t n=sizeof(s_store->room_record);
    if(nvs_blob("pico_home_area",false,&s_store->room_record,&n)!=ESP_OK||n!=sizeof(s_store->room_record)||
       s_store->room_record.checksum!=room_hash(&s_store->room_record,offsetof(__typeof__(s_store->room_record),checksum))||
       s_store->room_record.scope.count>RP_HOME_AREA_MAX||!scope_ready()){
        memset(&s_store->room_record,0,sizeof(s_store->room_record));return;
    }
    for(unsigned i=0;i<s_store->room_record.scope.count;i++)
        if(!memchr(s_store->room_record.scope.ids[i],0,64)){memset(&s_store->room_record,0,sizeof(s_store->room_record));break;}
}

static void load_config(void) {
    size_t bytes = sizeof(s_store->packed);
    memset(&s_config, 0, sizeof(s_config));
    s_config.version = RP_HOME_CONFIG_VERSION;
    esp_err_t result = nvs_blob("pico_home_pga", false, s_store->packed, &bytes);
    // 仅新命名空间确实没有记录时读取旧完整栈；已有空配置、损坏或读取失败不复活旧凭据。
    // Read the full-host store only when the new record is absent; never revive old credentials over an existing/failed record.
    if (result == ESP_ERR_NVS_NOT_FOUND) {
        bytes = sizeof(s_store->packed);
        result = nvs_blob("pico_home_link", false, s_store->packed, &bytes);
    }
    if (result != ESP_OK || !bytes) {
        memset(s_store->packed, 0, sizeof(s_store->packed));
        return;
    }
    if (rp_home_config_unpack(s_store->packed, bytes, &s_store->edit) && rp_home_config_valid(&s_store->edit))
        s_config = s_store->edit;
    memset(s_store->packed, 0, sizeof(s_store->packed));
    memset(&s_store->edit, 0, sizeof(s_store->edit));
}

static bool find_bytes(const char *hay, size_t n, const char *needle) {
    size_t m = strlen(needle);
    if (!hay || !m || n < m) return false;
    for (size_t i = 0; i + m <= n; i++)
        if (!memcmp(hay + i, needle, m)) return true;
    return false;
}

static bool access_challenge(int status, const char *body, size_t n) {
    if ((status == 302 || status == 401 || status == 403) &&
        (find_bytes(body, n, "cloudflareaccess.com") || find_bytes(body, n, "Cloudflare Access") ||
         find_bytes(body, n, "CF_AppSession") || find_bytes(body, n, "access.cloudflare") ||
         (n && body[0] == '<' && (find_bytes(body, n, "cloudflare") || find_bytes(body, n, "login")))))
        return true;
    if (status >= 200 && status < 400 && n > 32 && body[0] == '<' &&
        (find_bytes(body, n, "cloudflareaccess.com") || find_bytes(body, n, "Cloudflare Access")))
        return true;
    return false;
}

static rp_home_result_t http_request(void *unused, const rp_home_http_request_t *r, char *response, size_t capacity,
                                     size_t *used, int *status) {
    (void)unused;
    *used = 0;
    *status = 0;
    if (!r || !r->url || !response || capacity < 2) return RP_HOME_INVALID;
    lock();
    bool suspended = s_suspended;
    unlock();
    if (suspended) return RP_HOME_OFFLINE;
    if (!strncmp(r->url, "https://", 8) && !tls_clock_ok()) return RP_HOME_TIME;
    esp_http_client_config_t cfg = {.url = r->url,
                                    .timeout_ms = 8000,
                                    .buffer_size = 1024,
                                    .crt_bundle_attach = esp_crt_bundle_attach,
                                    .disable_auto_redirect = true,
                                    .method = r->method && !strcmp(r->method, "POST") ? HTTP_METHOD_POST : HTTP_METHOD_GET};
    esp_http_client_handle_t h = esp_http_client_init(&cfg);
    if (!h) return RP_HOME_NOMEM;
    char auth[sizeof(s_config.ha_token) + 8];
    if (r->bearer && *r->bearer) {
        snprintf(auth, sizeof(auth), "Bearer %s", r->bearer);
        esp_http_client_set_header(h, "Authorization", auth);
        memset(auth, 0, sizeof(auth));
    }
    if (r->content_type) esp_http_client_set_header(h, "Content-Type", r->content_type);
    if (r->cf_access_client_id && *r->cf_access_client_id && r->cf_access_client_secret && *r->cf_access_client_secret) {
        esp_http_client_set_header(h, "CF-Access-Client-Id", r->cf_access_client_id);
        esp_http_client_set_header(h, "CF-Access-Client-Secret", r->cf_access_client_secret);
    }
    size_t length = r->body ? strlen(r->body) : 0;
    rp_home_result_t result = RP_HOME_OFFLINE;
    if (esp_http_client_open(h, (int)length) == ESP_OK) {
        size_t sent = 0;
        while (sent < length) {
            int n = esp_http_client_write(h, r->body + sent, (int)(length - sent));
            if (n <= 0) break;
            sent += (size_t)n;
        }
        if (sent == length) {
            int64_t declared = esp_http_client_fetch_headers(h);
            *status = esp_http_client_get_status_code(h);
            if (declared >= (int64_t)capacity) result = RP_HOME_TOO_LARGE;
            else {
                int n = 0;
                while (*used < capacity - 1 &&
                       (n = esp_http_client_read(h, response + *used, (int)(capacity - 1 - *used))) > 0)
                    *used += (size_t)n;
                response[*used] = 0;
                result = esp_http_client_is_complete_data_received(h) ? RP_HOME_OK
                         : *used == capacity - 1                      ? RP_HOME_TOO_LARGE
                                                                      : RP_HOME_OFFLINE;
            }
        }
        esp_http_client_close(h);
    }
    esp_http_client_cleanup(h);
    if (result == RP_HOME_OK && access_challenge(*status, response, *used)) return RP_HOME_ACCESS;
    return result;
}

static rp_home_result_t mqtt_unsupported(void *c, const rp_home_printer_config_t *p, char *b, size_t n, size_t *u) {
    (void)c; (void)p; (void)b; (void)n;
    if (u) *u = 0;
    return RP_HOME_UNSUPPORTED;
}

static void fill_status(pico_app_home_status_t *st) {
    memset(st, 0, sizeof(*st));
    st->bytes = sizeof(*st);
    st->revision = s_snap.revision;
    st->entity_count = s_snap.entity_count;
    st->entity_total = s_snap.entity_total;
    st->printer_count = 0;
    for (unsigned i = 0; i < RP_HOME_PRINTERS; i++)
        if (s_config.printers[i].source != RP_HOME_NONE || s_snap.printers[i].has_data) st->printer_count++;
    st->result = (uint8_t)s_snap.result;
    st->busy = s_busy || s_snap.busy;
    st->external = s_snap.external;
    read_pico_connectivity_link_t net;
    read_pico_connectivity_get_link(&net);
    st->at_home = net.online && *s_config.home_wifi && !strcmp(s_config.home_wifi, net.ssid);
    st->enabled = s_config.enabled;
    st->has_token = *s_config.ha_token != 0;
    st->has_cf = *s_config.cf_access_client_id && *s_config.cf_access_client_secret;
    st->http_status = s_snap.http_status;
    if (net.online) st->flags |= PGA_HOME_WIFI;
    if (st->has_token || *s_config.local || *s_config.external) st->flags |= PGA_HOME_CFG;
    if (st->busy) st->flags |= PGA_HOME_BUSY;
    if (st->external) st->flags |= PGA_HOME_EXTERNAL;
    if (st->has_cf) st->flags |= PGA_HOME_CF;
    if (s_draft_pending) st->flags |= PGA_HOME_PENDING;
    if (s_portal_owner) st->flags |= PGA_HOME_PORTAL;
    if (read_pico_management_active()) st->flags |= PGA_HOME_MANAGE;
    const char *text = rp_home_result_text((rp_home_result_t)st->result);
    snprintf(st->error, sizeof(st->error), "%s", text ? text : "");
}

typedef struct {
    uint32_t generation;
    char ssid[33];
} discovery_context_t;

static bool discovery_cancelled(void *context) {
    const discovery_context_t *job = context;
    lock();
    bool cancel = s_suspended || s_generation != job->generation;
    unlock();
    read_pico_connectivity_link_t net;
    read_pico_connectivity_get_link(&net);
    return cancel || !net.online || strcmp(net.ssid, job->ssid);
}

static void run_discovery(void) {
    lock();
    bool run = s_discover_queued && !s_suspended;
    s_discover_queued = false;
    s_discover_running = run;
    discovery_context_t job = {.generation = s_generation};
    memcpy(job.ssid, s_store->discovery_ssid, sizeof(job.ssid));
    unlock();
    if (!run) return;
    bool ok = rp_home_discover(&s_store->discovery_job, 3000, discovery_cancelled, &job);
    bool valid = !discovery_cancelled(&job);
    lock();
    s_discover_running = false;
    if (valid && !s_suspended && s_generation == job.generation) {
        s_discover_failed = !ok;
        s_store->discovery = s_store->discovery_job;
    } else {
        s_discover_failed = true;
        memset(&s_store->discovery, 0, sizeof(s_store->discovery));
    }
    unlock();
}

bool ps_home_pga_discover(bool start, rp_home_discovery_t *out, bool *busy, bool *failed) {
    ps_home_pga_init();
    if (!s_ready || !out || !busy || !failed) return false;
    read_pico_connectivity_link_t net;
    read_pico_connectivity_get_link(&net);
    lock();
    if (!net.online || strcmp(net.ssid, s_store->discovery_ssid)) {
        memset(&s_store->discovery, 0, sizeof(s_store->discovery));
        s_discover_failed = true;
    }
    bool queue = start && net.online && !s_suspended && s_worker && !s_discover_queued && !s_discover_running;
    if (queue) {
        memset(&s_store->discovery, 0, sizeof(s_store->discovery));
        memcpy(s_store->discovery_ssid, net.ssid, sizeof(s_store->discovery_ssid));
        s_discover_queued = true;
        s_discover_failed = false;
    }
    *out = s_store->discovery;
    *busy = s_discover_queued || s_discover_running;
    *failed = s_discover_failed;
    bool ok = !start || *busy;
    unlock();
    if (queue) xTaskNotifyGive(s_worker);
    return ok;
}

static void run_areas(void){
    lock();bool run=s_areas_queued&&!s_suspended;s_areas_queued=false;s_areas_running=run;
    uint32_t generation=s_generation;
    if(run)s_store->job_config=s_config;
    unlock();if(!run)return;
    read_pico_connectivity_link_t net;read_pico_connectivity_get_link(&net);
    bool home=net.online&&*s_store->job_config.home_wifi&&!strcmp(net.ssid,s_store->job_config.home_wifi);
    rp_home_transport_t transport={http_request,mqtt_unsupported,NULL};int status=0;
    rp_home_result_t result=net.online?rp_home_fetch_areas(&s_store->job_config,&s_store->job_areas,&transport,home,&status):RP_HOME_OFFLINE;
    lock();s_areas_running=false;
    if(!s_suspended&&generation==s_generation){
        s_areas_result=result;s_areas_revision++;
        if(result==RP_HOME_OK){s_store->areas=s_store->job_areas;s_areas_generation=generation;}
    }
    unlock();memset(&s_store->job_config,0,sizeof(s_store->job_config));
}

bool ps_home_pga_rooms(uint32_t command,pico_app_home_rooms_t *out){
    ps_home_pga_init();if(!s_ready||!out||out->bytes<sizeof(*out))return false;
    lock();bool ok=true,notify=false;
    uint32_t chosen=out->selected,revision=out->revision;
    out->error[0]=0;
    if(command==PGA_ROOMS_SCAN){
        if(s_suspended||!s_worker||s_areas_queued||s_areas_running)ok=false;
        else {s_areas_queued=notify=true;s_areas_result=RP_HOME_BUSY;}
    }else if(command==PGA_ROOMS_SET){
        unsigned count=s_store->areas.count;
        if(s_portal_owner||read_pico_management_active()||s_suspended){ok=false;snprintf(out->error,sizeof(out->error),"请先关闭手机或电脑管理会话");}
        else if(s_areas_running||s_areas_queued||s_areas_result!=RP_HOME_OK||s_areas_generation!=s_generation||revision!=s_areas_revision||
            (count<32&&(chosen>>count))){ok=false;snprintf(out->error,sizeof(out->error),"房间列表已变化，请重新获取");}
        if(ok){
            memset(&s_store->room_edit,0,sizeof(s_store->room_edit));s_store->room_edit.binding=room_binding();
            s_store->room_edit.scope.version=1;
            for(unsigned i=0;i<count;i++)if(chosen&(1u<<i)){
                unsigned j=s_store->room_edit.scope.count++;memcpy(s_store->room_edit.scope.ids[j],s_store->areas.items[i].id,64);
            }
            s_store->room_edit.checksum=room_hash(&s_store->room_edit,offsetof(__typeof__(s_store->room_edit),checksum));
            if(!shared_save_locked(&s_config,&s_store->room_edit.scope,NULL)){ok=false;snprintf(out->error,sizeof(out->error),"保存失败，原房间范围已保留");}
            else if(!s_share_deferred){
                s_store->room_record=s_store->room_edit;s_generation++;s_areas_generation=s_generation;s_areas_revision++;
                s_queued=s_control_pending=false;s_busy=s_running;
                uint32_t rev=s_snap.revision+1;memset(&s_snap,0,sizeof(s_snap));s_snap.revision=rev;
            }
        }
    }else if(command!=PGA_ROOMS_GET)ok=false;
    out->bytes=sizeof(*out);out->revision=s_areas_revision;out->count=s_store->areas.count;out->total=s_store->areas.total;
    out->configured=scope_ready();out->busy=s_areas_queued||s_areas_running;out->truncated=s_store->areas.truncated;out->result=s_areas_result;out->selected=0;
    for(unsigned i=0;i<out->count;i++){
        memcpy(out->items[i].id,s_store->areas.items[i].id,64);memcpy(out->items[i].name,s_store->areas.items[i].name,64);
        out->items[i].devices=s_store->areas.items[i].devices;
        if(out->configured)for(unsigned j=0;j<s_store->room_record.scope.count;j++)
            if(!strcmp(out->items[i].id,s_store->room_record.scope.ids[j]))out->selected|=1u<<i;
    }
    if(!out->error[0]&&s_areas_result!=RP_HOME_OK&&s_areas_result!=RP_HOME_BUSY)
        snprintf(out->error,sizeof(out->error),"%s",rp_home_result_text(s_areas_result));
    unlock();if(notify)xTaskNotifyGive(s_worker);return ok;
}

bool ps_home_pga_printer_candidate(uint32_t index,pico_app_printer_config_t *out){
    if(!s_ready||!out||out->bytes<sizeof(*out))return false;
    lock();bool ok=index<s_snap.import_count;
    if(ok){
        const rp_home_printer_t *p=&s_snap.imports[index];
        memset(out,0,sizeof(*out));out->bytes=sizeof(*out);out->revision=s_generation;out->source=RP_HOME_HA;
        snprintf(out->name,sizeof(out->name),"%s",p->name);snprintf(out->serial,sizeof(out->serial),"%s",p->serial);
    }
    unlock();return ok;
}

static bool run_pending(void) {
    lock();
    bool run = s_queued && !s_suspended && (!s_home_reading||s_control_pending);
    bool online = s_online, at_home = s_at_home;
    bool control = s_control_pending;
    uint32_t generation = s_generation;
    if (run) {
        s_store->job_config = s_config;
        s_store->job_snap = s_snap;
        memset(&s_store->job_scope,0,sizeof(s_store->job_scope));
        if(scope_ready())s_store->job_scope=s_store->room_record.scope;
        s_store->job_selection=s_store->selection.value;
        if(s_store->selection.binding!=room_binding())memset(&s_store->job_selection,0,sizeof(s_store->job_selection));
        // 保留旧配置供兼容回读，但薄宿主不能隐式启用尚未迁移的独立打印机协议。
        // Preserve legacy settings for readback without implicitly enabling unsupported printer protocols.
        for (unsigned i = 0; i < RP_HOME_PRINTERS; i++)
            if (s_store->job_config.printers[i].source > RP_HOME_HA)
                memset(&s_store->job_config.printers[i], 0, sizeof(s_store->job_config.printers[i]));
        s_running = s_busy = true;
    }
    s_queued = s_control_pending = false;
    if(!run)s_busy=s_running;
    unlock();
    if (!run) return false;
    rp_home_transport_t transport = {http_request, mqtt_unsupported, NULL};
    int http_status = 0;
    rp_home_result_t result = control
        ? rp_home_control(&s_store->job_config, &s_store->control_entity, s_store->control_action,
                          s_store->control_value, &transport, at_home, &http_status) : RP_HOME_OK;
    /* rp_home_fetch allocates its own PSRAM response (RP_HOME_HTTP_LIMIT). Do not malloc a second one from internal RAM. */
    if (result == RP_HOME_OK&&!(control&&s_share_role==2&&s_share_link))
        (void)rp_home_fetch_selected(&s_store->job_config, &s_store->job_snap, s_tokens, &transport, online, at_home, now_ms(),&s_store->job_scope,
            s_store->job_selection.version==1?&s_store->job_selection:NULL);
    else {
        s_store->job_snap.result = result;
        s_store->job_snap.http_status = http_status;
    }
    lock();
    if (!s_suspended && generation == s_generation) {
        for (unsigned i = 0; i < RP_HOME_PRINTERS; i++) {
            const rp_home_printer_config_t *config = &s_config.printers[i];
            if (config->source <= RP_HOME_HA) continue;
            rp_home_printer_t *printer = &s_store->job_snap.printers[i];
            memset(printer, 0, sizeof(*printer));
            printer->source = config->source;
            printer->percent = printer->remaining_minutes = -1;
            printer->result = RP_HOME_UNSUPPORTED;
            memmove(printer->name, config->name, sizeof(printer->name));
            if (s_store->job_snap.result == RP_HOME_EMPTY) s_store->job_snap.result = RP_HOME_UNSUPPORTED;
        }
        uint32_t rev = s_snap.revision + 1;
        s_snap = s_store->job_snap;
        s_snap.revision = rev;
    }
    if(control&&s_share_role==2&&s_share_link)s_share_invalidate=true;
    s_running = false;
    s_busy = s_queued;
    unlock();
    memset(&s_store->job_config, 0, sizeof(s_store->job_config));
    memset(&s_store->job_snap, 0, sizeof(s_store->job_snap));
    return true;
}

static void entity_export(const rp_home_entity_t *,uint32_t,pico_app_home_entity_t *);
static void run_catalog(void){
    lock();bool run=s_catalog_queued&&!s_suspended&&!s_home_local_reading;s_catalog_queued=false;s_catalog_running=run;
    uint32_t generation=s_generation,offset=s_catalog_offset;
    if(run){s_store->job_config=s_config;memset(&s_store->job_snap,0,sizeof(s_store->job_snap));s_store->job_scope=s_store->room_record.scope;}
    unlock();if(!run)return;
    read_pico_connectivity_link_t net={0};read_pico_connectivity_get_link(&net);
    bool home=net.online&&*s_store->job_config.home_wifi&&!strcmp(net.ssid,s_store->job_config.home_wifi);
    rp_home_transport_t transport={http_request,mqtt_unsupported,NULL};
    rp_home_result_t result=rp_home_fetch_catalog(&s_store->job_config,&s_store->job_snap,s_tokens,&transport,net.online,home,now_ms(),&s_store->job_scope,offset);
    lock();s_catalog_running=false;
    if(generation==s_generation&&!s_suspended){
        pico_app_home_catalog_t *out=&s_store->catalog;
        memset(out,0,sizeof(*out));out->bytes=sizeof(*out);out->revision=generation;out->offset=offset;out->result=result;
        out->total=s_store->job_snap.entity_total;out->count=s_store->job_snap.entity_count;
        if(out->count>24)out->count=24;
        for(unsigned i=0;i<out->count;i++){out->items[i].bytes=sizeof(out->items[i]);entity_export(&s_store->job_snap.entities[i],offset+i,&out->items[i]);}
        snprintf(out->error,sizeof(out->error),"%s",result==RP_HOME_OK?"":rp_home_result_text(result));
    }
    unlock();memset(&s_store->job_config,0,sizeof(s_store->job_config));
}
bool ps_home_pga_catalog(uint32_t command,pico_app_home_catalog_t *out){
    ps_home_pga_init();if(!s_ready||!out||out->bytes<sizeof(*out))return false;
    lock();bool ok=true,notify=false;
    if(command==PGA_CATALOG_SCAN){
        if(s_suspended||s_catalog_queued||s_catalog_running||!scope_ready())ok=false;
        else {s_catalog_offset=out->offset;s_catalog_queued=notify=true;}
    }else if(command!=PGA_CATALOG_GET)ok=false;
    *out=s_store->catalog;out->bytes=sizeof(*out);out->busy=s_catalog_queued||s_catalog_running;
    unlock();if(notify)xTaskNotifyGive(s_worker);return ok;
}
bool ps_home_pga_selection(uint32_t command,pico_app_home_selection_t *out){
    ps_home_pga_init();if(!s_ready||!out||out->bytes<sizeof(*out))return false;
    lock();bool ok=true;out->error[0]=0;
    if(command==PGA_SELECTION_SET){
        if(s_suspended||s_portal_owner||read_pico_management_active()||out->count>96||out->revision!=s_store->selection.revision)ok=false;
        for(unsigned i=0;ok&&i<out->count;i++){
            if(!memchr(out->ids[i],0,96)||!strchr(out->ids[i],'.'))ok=false;
            for(unsigned j=0;j<i;j++)if(!strcmp(out->ids[i],out->ids[j]))ok=false;
        }
        if(ok){
            memset(&s_store->selection_edit,0,sizeof(s_store->selection_edit));
            s_store->selection_edit.binding=room_binding();s_store->selection_edit.revision=s_store->selection.revision+1;
            s_store->selection_edit.value.version=1;s_store->selection_edit.value.count=out->count;
            memcpy(s_store->selection_edit.value.ids,out->ids,out->count*96);
            s_store->selection_edit.checksum=room_hash(&s_store->selection_edit,offsetof(__typeof__(s_store->selection_edit),checksum));
            ok=shared_save_locked(&s_config,NULL,&s_store->selection_edit.value);
            if(ok&&!s_share_deferred){s_generation++;s_snap.revision++;}
        }
        if(!ok)snprintf(out->error,sizeof(out->error),"保存失败或配置已变化，请重新编辑");
    }else if(command!=PGA_SELECTION_GET)ok=false;
    if(!ok){unlock();return false;}
    out->bytes=sizeof(*out);out->pending=s_draft_pending;out->revision=s_store->selection.revision;out->configured=s_store->selection.value.version==1&&s_store->selection.binding==room_binding();
    out->count=out->configured?s_store->selection.value.count:0;
    memcpy(out->ids,s_store->selection.value.ids,sizeof(out->ids));
    unlock();return ok;
}
static void worker(void *unused) {
    (void)unused;
    for (;;) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        if (s_stop) break;
        run_discovery();
        run_areas();
        run_share();
        run_catalog();
        (void)run_pending();
    }
    s_worker = NULL;
    vTaskDelete(NULL);
}

void ps_home_pga_init(void) {
    if (s_ready && s_worker) return;
    if (!s_lock) s_lock = xSemaphoreCreateMutex();
    if (!s_lock) return;
    if (!s_store) {
        /* No internal-RAM fallback: a failed PSRAM alloc must not steal the DMA heap USB MSC needs. */
        s_store = heap_caps_calloc(1, sizeof(*s_store), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!s_store) return;
        load_config();
        load_rooms();
        size_t selection_n=sizeof(s_store->selection);
        if(nvs_blob("pico_home_sel",false,&s_store->selection,&selection_n)!=ESP_OK||selection_n!=sizeof(s_store->selection)||
           s_store->selection.checksum!=room_hash(&s_store->selection,offsetof(__typeof__(s_store->selection),checksum))||
           s_store->selection.value.count>RP_HOME_DEVICE_LIMIT||s_store->selection.binding!=room_binding())memset(&s_store->selection,0,sizeof(s_store->selection));
        shared_load();
        memset(&s_snap, 0, sizeof(s_snap));
        s_snap.revision = 1;
        s_snap.result = RP_HOME_EMPTY;
        s_ready = true;
    }
    if (!s_worker && xTaskCreate(worker, "home_pga", 8192, NULL, 3, &s_worker) != pdPASS)
        s_worker = NULL;
}

bool ps_home_pga_active(void) { return s_ready; }

bool ps_home_pga_peek_status(pico_app_home_status_t *status) {
    if (!s_ready || !status || status->bytes < sizeof(*status)) return false;
    lock();
    fill_status(status);
    unlock();
    return true;
}

bool ps_home_link_package_installed(void) {
    pico_app_package_t pkg;
    char reason[80];
    if (!pico_app_package_read(PICO_APP_TOOLS_DIRECTORY "/home-link.pga", &pkg, false, reason, sizeof(reason)))
        return false;
    return pkg.abi == PICO_APP_ABI && pkg.kind == PGA_HOME_LINK && !strcmp(pkg.id, "home-link");
}


bool ps_home_pga_command(uint32_t command, pico_app_home_status_t *status) {
    ps_home_pga_init();
    if (!s_ready || !status || status->bytes < sizeof(*status)) {
        if (status) status->bytes = sizeof(*status);
        return false;
    }
    read_pico_connectivity_link_t net;
    read_pico_connectivity_get_link(&net);
    bool online = net.online;
    lock();
    bool at_home = online && *s_config.home_wifi && !strcmp(s_config.home_wifi, net.ssid);
    if(command==PGA_HOME_STATUS)s_home_visible_until=now_ms()+2000;
    bool sync=command==PGA_HOME_SYNC||command==PS_HOME_PRINTER_SYNC;
    if (!sync && command != PGA_HOME_STATUS) { unlock(); return false; }
    bool ok = !sync || (s_worker && !s_suspended&&(scope_ready()||command==PS_HOME_PRINTER_SYNC));
    if(sync&&ok&&command==PGA_HOME_SYNC&&s_share_role==2&&s_share_link){s_share_invalidate=true;fill_status(status);unlock();return true;}
    if (sync && ok && !s_home_reading) {
        s_online = online;
        s_at_home = at_home;
        s_queued = true;
        s_busy = true;s_home_poll_due=now_ms()+30000;
    }
    fill_status(status);
    if(!ok&&command==PGA_HOME_SYNC&&!scope_ready())snprintf(status->error,sizeof(status->error),"请先选择房间");
    unlock();
    if (sync && ok) xTaskNotifyGive(s_worker);
    return ok;
}

static void config_changed_locked(void) {
    s_generation++;
    s_queued = s_control_pending = false;
    s_busy = s_running;
    uint32_t revision = s_snap.revision + 1;
    memset(&s_snap, 0, sizeof(s_snap));
    s_snap.revision = revision;
    s_areas_queued=false;s_catalog_queued=false;s_areas_revision++;s_areas_generation=0;s_areas_result=RP_HOME_EMPTY;
    memset(&s_store->areas,0,sizeof(s_store->areas));
}

bool ps_home_pga_config(uint32_t command, pico_app_home_config_t *config) {
    ps_home_pga_init();
    if (!s_ready || !config || config->bytes < sizeof(*config)) {
        if (config) config->bytes = sizeof(*config);
        return false;
    }
    if (command == PGA_HOME_CONFIG_GET) {
        lock();
        memset(config, 0, sizeof(*config));
        config->bytes = sizeof(*config);
        config->enabled = s_config.enabled;
        config->external_enabled = s_config.external_enabled;
        config->external_control = s_config.external_control;
        config->has_token = *s_config.ha_token != 0;
        config->has_cf_id = *s_config.cf_access_client_id != 0;
        config->has_cf_secret = *s_config.cf_access_client_secret != 0;
        memcpy(config->local, s_config.local, sizeof(config->local));
        memcpy(config->external, s_config.external, sizeof(config->external));
        memcpy(config->home_wifi, s_config.home_wifi, sizeof(config->home_wifi));
        unlock();
        return true;
    }
    if (command != PGA_HOME_CONFIG_SET) return false;
    lock();
    if (s_portal_owner || read_pico_management_active()) {
        unlock();
        memset(config->ha_token, 0, sizeof(config->ha_token));
        memset(config->cf_access_client_id, 0, sizeof(config->cf_access_client_id));
        memset(config->cf_access_client_secret, 0, sizeof(config->cf_access_client_secret));
        return false;
    }
    rp_home_config_t *next = &s_store->edit;
    *next = s_config;
    next->version = RP_HOME_CONFIG_VERSION;
    next->enabled = config->enabled;
    next->external_enabled = config->external_enabled;
    next->external_control = config->external_control;
    snprintf(next->local, sizeof(next->local), "%s", config->local);
    snprintf(next->external, sizeof(next->external), "%s", config->external);
    snprintf(next->home_wifi, sizeof(next->home_wifi), "%s", config->home_wifi);
    if (config->clear_token) memset(next->ha_token, 0, sizeof(next->ha_token));
    else if (config->ha_token[0]) {
        memset(next->ha_token, 0, sizeof(next->ha_token));
        snprintf(next->ha_token, sizeof(next->ha_token), "%s", config->ha_token);
    }
    if (config->clear_cf) {
        memset(next->cf_access_client_id, 0, sizeof(next->cf_access_client_id));
        memset(next->cf_access_client_secret, 0, sizeof(next->cf_access_client_secret));
    } else {
        if (config->cf_access_client_id[0])
            snprintf(next->cf_access_client_id, sizeof(next->cf_access_client_id), "%s", config->cf_access_client_id);
        if (config->cf_access_client_secret[0])
            snprintf(next->cf_access_client_secret, sizeof(next->cf_access_client_secret), "%s",
                     config->cf_access_client_secret);
    }
    if (!rp_home_config_valid(next)) {
        memset(next, 0, sizeof(*next));
        unlock();
        return false;
    }
    /* NVS 事务成功后才发布；失败时内存与已提交配置仍一致。 */
    /* Publish only after NVS commits, keeping the live config unchanged on failure. */
    bool ok = save_config_locked(next);
    if (ok && !s_share_deferred) { s_config = *next; config_changed_locked(); }
    memset(next, 0, sizeof(*next));
    unlock();
    memset(config->ha_token, 0, sizeof(config->ha_token));
    memset(config->cf_access_client_id, 0, sizeof(config->cf_access_client_id));
    memset(config->cf_access_client_secret, 0, sizeof(config->cf_access_client_secret));
    return ok;
}

static void entity_export(const rp_home_entity_t *e,uint32_t index,pico_app_home_entity_t *out){
    uint32_t capacity=out->bytes;
    pico_app_home_entity_t next={.bytes=sizeof(next),.index=index,.kind=e->kind,.available=e->available,.on=e->on,.number=e->number,
        .brightness=e->brightness,.dimmable=e->dimmable,.hvac_modes=e->hvac_modes};
    snprintf(next.id,sizeof(next.id),"%.95s",e->id);snprintf(next.name,sizeof(next.name),"%.63s",e->name);
    snprintf(next.state,sizeof(next.state),"%.47s",e->state);snprintf(next.unit,sizeof(next.unit),"%.11s",e->unit);snprintf(next.room,sizeof(next.room),"%.31s",e->room);
    memcpy(out,&next,capacity<sizeof(next)?capacity:sizeof(next));
}
bool ps_home_pga_entity(uint32_t index,pico_app_home_entity_t *out){
    if(!s_ready||!out||out->bytes<PICO_HOME_ENTITY_V1_BYTES)return false;
    lock();bool ok=index<s_snap.entity_count;
    if(ok)entity_export(&s_snap.entities[index],index,out);
    unlock();return ok;
}

bool ps_home_pga_printer(uint32_t slot, pico_app_home_printer_t *out) {
    if (!s_ready || !out || out->bytes < sizeof(*out) || slot >= RP_HOME_PRINTERS) {
        if (out) out->bytes = sizeof(*out);
        return false;
    }
    lock();
    const rp_home_printer_t *p = &s_snap.printers[slot];
    memset(out, 0, sizeof(*out));
    out->bytes = sizeof(*out);
    out->slot = slot;
    snprintf(out->name, sizeof(out->name), "%s", p->name[0] ? p->name : s_config.printers[slot].name);
    snprintf(out->state, sizeof(out->state), "%s", p->state);
    snprintf(out->task, sizeof(out->task), "%s", p->task);
    out->percent = p->percent;
    out->remaining_minutes = p->remaining_minutes;
    out->source = p->source ? p->source : s_config.printers[slot].source;
    out->connected = p->connected;
    out->has_data = p->has_data;
    out->result = (uint8_t)p->result;
    out->http_status = p->http_status;
    unlock();
    return true;
}

bool ps_home_pga_printer_config(uint32_t command,pico_app_printer_config_t *edit) {
    ps_home_pga_init();
    if(!s_ready||!edit||edit->bytes<sizeof(*edit)||edit->slot>=RP_HOME_PRINTERS)return false;
    lock();
    rp_home_printer_config_t *prior=&s_config.printers[edit->slot];
    bool ok=false;
    edit->error[0]=0;
    if(command==PGA_PRINTER_GET){
        edit->revision=s_generation;edit->source=prior->source;
        snprintf(edit->name,sizeof(edit->name),"%s",prior->name);
        snprintf(edit->serial,sizeof(edit->serial),"%s",prior->serial);
        ok=true;
    }else if(command!=PGA_PRINTER_SET&&command!=PGA_PRINTER_REMOVE){
        snprintf(edit->error,sizeof(edit->error),"不支持的操作");
    }else if(s_portal_owner||read_pico_management_active()){
        snprintf(edit->error,sizeof(edit->error),"请先关闭手机或电脑管理页面");
    }else if(edit->revision!=s_generation){
        snprintf(edit->error,sizeof(edit->error),"配置已变化，请返回后重新编辑");
    }else{
        rp_home_config_t *next=&s_store->edit;*next=s_config;
        rp_home_printer_config_t *target=&next->printers[edit->slot];
        bool valid=true;
        if(command==PGA_PRINTER_REMOVE)memset(target,0,sizeof(*target));
        else if(!memchr(edit->name,0,sizeof(edit->name))||!memchr(edit->serial,0,sizeof(edit->serial))||!edit->name[0]){
            valid=false;
        }else if(edit->source==RP_HOME_HA){
            // 同源更新保留其余槽；新增打印机不复用异源凭据。/ Keep other slots and never reuse foreign-source credentials for a new HA printer.
            memset(target,0,sizeof(*target));target->source=RP_HOME_HA;
            snprintf(target->name,sizeof(target->name),"%s",edit->name);
            snprintf(target->serial,sizeof(target->serial),"%s",edit->serial);
            for(unsigned i=0;i<RP_HOME_PRINTERS;i++)if(i!=edit->slot&&next->printers[i].source==RP_HOME_HA&&
                !strcmp(next->printers[i].serial,edit->serial))valid=false;
        }else if(prior->source>RP_HOME_HA&&edit->source==prior->source&&!strcmp(edit->serial,prior->serial)){
            // 未迁移的旧连接只允许改名或明确移除，所有秘密字段原样保留。/ Legacy unsupported connections allow renaming or explicit removal while preserving every secret.
            snprintf(target->name,sizeof(target->name),"%s",edit->name);
        }else valid=false;
        valid=valid&&rp_home_config_valid(next);
        if(!valid)snprintf(edit->error,sizeof(edit->error),"检查名称与实体前缀，避免重复添加");
        else if(!(ok=save_config_locked(next)))snprintf(edit->error,sizeof(edit->error),"保存失败，原配置已保留");
        if(ok){s_config=*next;config_changed_locked();edit->revision=s_generation;}
        memset(next,0,sizeof(*next));
    }
    unlock();return ok;
}

bool ps_home_pga_control(const char *entity_id, const char *action, float value) {
    ps_home_pga_init();
    if (!s_ready || !s_worker || !entity_id || !action || !*entity_id ||
        strlen(action) >= sizeof(s_store->control_action)) return false;
    lock();
    if (s_suspended || s_busy || s_queued || !s_config.enabled || !*s_config.ha_token) {
        unlock();
        return false;
    }
    bool found = false;
    for (unsigned i = 0; i < s_snap.entity_count; i++) {
        if (!strcmp(s_snap.entities[i].id, entity_id)) {
            if (s_snap.entities[i].available && s_snap.entities[i].kind <= RP_HOME_FAN) {
                s_store->control_entity = s_snap.entities[i];
                found = true;
            }
            break;
        }
    }
    if (found) {
        snprintf(s_store->control_action, sizeof(s_store->control_action), "%s", action);
        s_store->control_value = value;
        read_pico_connectivity_link_t net;
        read_pico_connectivity_get_link(&net);
        s_online = net.online;
        s_at_home = net.online && *s_config.home_wifi && !strcmp(s_config.home_wifi, net.ssid);
        s_queued = s_busy = s_control_pending = true;
    }
    unlock();
    if (found) xTaskNotifyGive(s_worker);
    return found;
}

bool ps_home_pga_input(uint32_t command, char *value, size_t capacity) {
    if (command == PGA_HOME_INPUT_CANCEL) {
        s_input_ready = s_input_open = false;
        memset(s_input, 0, sizeof(s_input));
        return true;
    }
    if (!value || !capacity) return false;
    if (command == PGA_HOME_INPUT_OPEN) {
        size_t n = strnlen(value, capacity < sizeof(s_input) ? capacity : sizeof(s_input));
        if (n >= capacity || n >= sizeof(s_input) || s_input_open) return false;
        memcpy(s_input, value, n + 1);
        s_input_ready = false;
        s_input_open = true;
        return true;
    }
    if (command != PGA_HOME_INPUT_TAKE || !s_input_ready || strlen(s_input) >= capacity) return false;
    memcpy(value, s_input, strlen(s_input) + 1);
    s_input_ready = s_input_open = false;
    memset(s_input, 0, sizeof(s_input));
    return true;
}

void ps_home_pga_input_done(const char *text) {
    if (!s_input_open) return;
    snprintf(s_input, sizeof(s_input), "%s", text ? text : "");
    s_input_ready = true;
}

bool ps_home_pga_portal_load(rp_home_config_t *config, rp_home_snapshot_t *snapshot) {
    ps_home_pga_init();
    if (!s_ready || !config || !snapshot) return false;
    lock();
    *config = s_config;
    *snapshot = s_snap;
    unlock();
    return true;
}

bool ps_home_pga_portal_commit(const rp_home_config_t *config, bool *queued) {
    if (queued) *queued = false;
    ps_home_pga_init();
    if (!s_ready || !config || !rp_home_config_valid(config)) return false;
    read_pico_connectivity_link_t net;
    read_pico_connectivity_get_link(&net);
    bool online = net.online;
    bool home = online && config->home_wifi[0] && !strcmp(config->home_wifi, net.ssid);
    lock();
    bool ok = save_config_locked(config);
    if (ok && !s_share_deferred) { s_config = *config; config_changed_locked(); }
    bool queue = ok && s_worker && online && s_config.enabled && !s_suspended && scope_ready();
    if (queue) {
        s_online = online;
        s_at_home = home;
        s_queued = true;
        s_busy = true;s_home_poll_due=now_ms()+30000;
    }
    unlock();
    if (queue && s_worker) xTaskNotifyGive(s_worker);
    if (queued) *queued = queue;
    return ok;
}

void ps_home_pga_portal_set_owner(bool portal_owns_writes) {
    ps_home_pga_init();
    if (!s_lock) return;
    lock();
    s_portal_owner = portal_owns_writes;
    unlock();
}

bool ps_home_pga_suspend(uint32_t timeout_ms) {
    lock();
    if (!s_suspended) s_generation++;
    s_suspended = true;
    s_discover_queued = false;
    s_areas_queued=false;
    s_discover_failed = true;
    s_queued = s_control_pending = false;
    s_busy = s_running;
    unlock();
    int64_t deadline = now_ms() + timeout_ms;
    while (now_ms() < deadline) {
        lock();
        bool busy = s_busy || s_queued || s_discover_running || s_areas_running || s_catalog_running;
        unlock();
        if (!busy) return true;
        vTaskDelay(pdMS_TO_TICKS(50));
    }
    lock();
    bool busy = s_busy || s_queued || s_discover_running || s_areas_running || s_catalog_running;
    unlock();
    return !busy;
}

void ps_home_pga_resume(void) {
    lock();
    s_suspended = false;
    unlock();
}

#include "pico_home_share.inc"

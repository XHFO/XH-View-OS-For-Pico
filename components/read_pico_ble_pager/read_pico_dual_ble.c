/* SPDX-License-Identifier: Apache-2.0
 * 双机控制器独占 BLE：主机 GATT 服务、从机客户端；书籍只走 Wi-Fi。
 * Exclusive dual-reader BLE: primary GATT server, secondary client; books use Wi-Fi only.
 * 冻结：回调不绘图/不访问TF；不清除HID配对或用户配置；拒绝未认证控制帧。
 * Frozen: no drawing/TF in callbacks, no HID bond/settings erasure, no unauthenticated control.
 */
#include "read_pico_dual_ble.h"
#include "read_pico_ble_pager.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/ble_gap.h"
#include "host/ble_gatt.h"
#include "host/ble_hs_adv.h"
#include "host/ble_sm.h"
#include "host/util/util.h"
#include "os/os_mbuf.h"

#define NONE BLE_HS_CONN_HANDLE_NONE
#define MTU_MIN (PICO_DUAL_BLE_PACKET_MAX + 3)
#define QUEUE_SIZE 8
static const ble_uuid128_t service_uuid = BLE_UUID128_INIT(0x8e,0xb1,0x76,0x9d,0x60,0x54,0x4a,0x86,0x91,0x6f,0x18,0x67,0x01,0x02,0x48,0x58);
static const ble_uuid128_t rx_uuid = BLE_UUID128_INIT(0x8e,0xb1,0x76,0x9d,0x60,0x54,0x4a,0x86,0x91,0x6f,0x18,0x67,0x02,0x02,0x48,0x58);
static const ble_uuid128_t tx_uuid = BLE_UUID128_INIT(0x8e,0xb1,0x76,0x9d,0x60,0x54,0x4a,0x86,0x91,0x6f,0x18,0x67,0x03,0x02,0x48,0x58);
typedef struct { uint32_t generation; uint8_t size, bytes[PICO_DUAL_BLE_PACKET_MAX]; } packet_t;
typedef struct { int role; char code[7]; uint32_t epoch; } config_t;
static portMUX_TYPE guard = portMUX_INITIALIZER_UNLOCKED;
static config_t wanted, current;
static pico_dual_ble_status_t snapshot;
static bool worker, failed;
static uint32_t config_epoch = 1;
static QueueHandle_t incoming, outgoing;
static uint16_t connection = NONE, local_tx, remote_rx, remote_tx, service_start, service_end;
static uint8_t address_type;
static bool synced, subscribed, connecting, discovering, writing;
static uint32_t epoch;
static int64_t retry_at, setup_deadline, tx_retry_at;
static bool scan_only;
static char adv_ip[48], adv_ap_ssid[33];
static bool adv_info_dirty;
static pico_dual_ble_peer_info_t peer_info;
static struct ble_npl_callout ticker;
static ble_addr_t attempted, rejected[4];
static unsigned rejected_at, rejected_count;
static int64_t reject_until;
static int gap_event(struct ble_gap_event*, void*);

static void error_set(int error) {
    portENTER_CRITICAL(&guard); snapshot.error = error; portEXIT_CRITICAL(&guard);
}
static bool secure(uint16_t conn) {
    struct ble_gap_conn_desc desc;
    return conn != NONE && conn == connection && !ble_gap_conn_find(conn, &desc) &&
           desc.sec_state.encrypted && desc.sec_state.authenticated && desc.sec_state.key_size == 16;
}
static void ready_update(void) {
    bool ready = subscribed && secure(connection) && ble_att_mtu(connection) >= MTU_MIN;
    portENTER_CRITICAL(&guard); snapshot.connected = ready;
    if (ready) { snapshot.error = 0; connecting = false; rejected_count = 0; }
    portEXIT_CRITICAL(&guard);
}
static void clear_link(int error) {
    connection = NONE; subscribed = connecting = discovering = writing = false;
    remote_rx = remote_tx = service_start = service_end = 0;
    ++epoch; xQueueReset(incoming); xQueueReset(outgoing);
    portENTER_CRITICAL(&guard); snapshot.connected = false; snapshot.generation = epoch;
    if (error) snapshot.error = error;
    portEXIT_CRITICAL(&guard);
    retry_at = esp_timer_get_time() + 2000000;
    tx_retry_at = 0;
}
static void reject(int error) {
    error_set(error);
    if (current.role == 2 && connecting && error == BLE_HS_EAUTHEN) {
        rejected[rejected_at++ % 4] = attempted;
        if (rejected_count < 4) ++rejected_count;
        reject_until = esp_timer_get_time() + 30000000;
    }
    if (connection != NONE) ble_gap_terminate(connection, BLE_ERR_REM_USER_CONN_TERM);
    else { ble_gap_conn_cancel(); clear_link(error); }
}
static bool receive_packet(uint16_t conn, const struct os_mbuf* om) {
    uint16_t n = OS_MBUF_PKTLEN(om);
    if (!secure(conn) || !n || n > PICO_DUAL_BLE_PACKET_MAX) return false;
    packet_t p = {.generation = epoch, .size = (uint8_t)n};
    return !os_mbuf_copydata(om, 0, n, p.bytes) && xQueueSend(incoming, &p, 0) == pdTRUE;
}
static int access_gatt(uint16_t conn, uint16_t attr, struct ble_gatt_access_ctxt* ctx, void* arg) {
    (void)attr;
    if (!secure(conn)) return BLE_ATT_ERR_INSUFFICIENT_AUTHEN;
    if (arg && ctx->op == BLE_GATT_ACCESS_OP_WRITE_CHR)
        return receive_packet(conn, ctx->om) ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
    if (!arg && ctx->op == BLE_GATT_ACCESS_OP_READ_CHR) return 0;
    return BLE_ATT_ERR_UNLIKELY;
}
static const struct ble_gatt_svc_def services[] = {{
    .type = BLE_GATT_SVC_TYPE_PRIMARY, .uuid = &service_uuid.u,
    .characteristics = (const struct ble_gatt_chr_def[]) {
        {.uuid=&rx_uuid.u, .access_cb=access_gatt, .arg=(void*)1,
         .flags=BLE_GATT_CHR_F_WRITE|BLE_GATT_CHR_F_WRITE_ENC|BLE_GATT_CHR_F_WRITE_AUTHEN, .min_key_size=16},
        {.uuid=&tx_uuid.u, .access_cb=access_gatt, .val_handle=&local_tx,
         .flags=BLE_GATT_CHR_F_NOTIFY|BLE_GATT_CHR_F_READ|BLE_GATT_CHR_F_READ_ENC|BLE_GATT_CHR_F_READ_AUTHEN, .min_key_size=16},
        {0}
    }
}, {0}};
static bool callback_current(uint16_t conn, void* arg) {
    return conn == connection && (uint32_t)(uintptr_t)arg == epoch && secure(conn);
}
static int subscribed_cb(uint16_t conn, const struct ble_gatt_error* err, struct ble_gatt_attr* attr, void* arg) {
    (void)attr;
    if (!callback_current(conn, arg)) return 0;
    if (err->status) reject(err->status);
    else { subscribed = true; discovering = false; ready_update(); }
    return 0;
}
static int descriptor_cb(uint16_t conn, const struct ble_gatt_error* err, uint16_t value,
                         const struct ble_gatt_dsc* dsc, void* arg) {
    (void)value;
    if (!callback_current(conn, arg)) return 0;
    if (!err->status && ble_uuid_u16(&dsc->uuid.u) == 0x2902) {
        uint8_t enable[2]={1,0};
        int rc=ble_gattc_write_flat(conn,dsc->handle,enable,2,subscribed_cb,arg);
        if (rc) reject(rc);
    } else if (err->status && err->status != BLE_HS_EDONE) reject(err->status);
    return 0;
}
static int characteristic_cb(uint16_t conn, const struct ble_gatt_error* err,
                             const struct ble_gatt_chr* chr, void* arg) {
    if (!callback_current(conn, arg)) return 0;
    if (!err->status) {
        if (!ble_uuid_cmp(&chr->uuid.u,&rx_uuid.u)) remote_rx=chr->val_handle;
        if (!ble_uuid_cmp(&chr->uuid.u,&tx_uuid.u)) remote_tx=chr->val_handle;
    } else if (err->status == BLE_HS_EDONE && remote_rx && remote_tx && remote_tx < service_end) {
        int rc=ble_gattc_disc_all_dscs(conn,remote_tx,service_end,descriptor_cb,arg);
        if(rc)reject(rc);
    } else reject(err->status ? err->status : BLE_HS_ENOENT);
    return 0;
}
static int service_cb(uint16_t conn, const struct ble_gatt_error* err,
                      const struct ble_gatt_svc* svc, void* arg) {
    if (!callback_current(conn, arg)) return 0;
    if (!err->status) { service_start=svc->start_handle; service_end=svc->end_handle; }
    else if (err->status == BLE_HS_EDONE && service_start) {
        int rc=ble_gattc_disc_all_chrs(conn,service_start,service_end,characteristic_cb,arg);
        if(rc)reject(rc);
    } else reject(err->status ? err->status : BLE_HS_ENOENT);
    return 0;
}
static int mtu_cb(uint16_t conn, const struct ble_gatt_error* err, uint16_t mtu, void* arg) {
    if (!callback_current(conn,arg)) return 0;
    if(err->status || mtu < MTU_MIN) { reject(err->status ? err->status : BLE_HS_EMSGSIZE); return 0; }
    int rc=ble_gattc_disc_svc_by_uuid(conn,&service_uuid.u,service_cb,arg);
    if(rc)reject(rc);
    return 0;
}
static int write_cb(uint16_t conn, const struct ble_gatt_error* err, struct ble_gatt_attr* attr, void* arg) {
    (void)attr;
    if(!callback_current(conn,arg))return 0;
    writing=false;
    if(err->status==BLE_HS_ATT_ERR(BLE_ATT_ERR_INSUFFICIENT_RES) ||
       err->status==BLE_HS_ENOMEM || err->status==BLE_HS_EBUSY){
        // 对端忙只保留队首稍后重试，不把绘制期间的背压当作断联。
        // Retain the head on peer backpressure; rendering stalls must not tear down the link.
        tx_retry_at=esp_timer_get_time()+100000;
        portENTER_CRITICAL(&guard);++snapshot.tx_retries;portEXIT_CRITICAL(&guard);
        return 0;
    }
    if(err->status){reject(err->status);return 0;}
    packet_t p; xQueueReceive(outgoing,&p,0);
    return 0;
}
static void notification_received(uint16_t conn, uint16_t attr, const struct os_mbuf* om) {
    // 通知不能回压；溢出由协议重试恢复，不能主动断开已认证会话。
    // Notifications cannot exert backpressure; protocol retries recover overflow without disconnecting.
    if(current.role==2 && attr==remote_tx && !receive_packet(conn,om)) {
        portENTER_CRITICAL(&guard);++snapshot.rx_dropped;portEXIT_CRITICAL(&guard);
    }
}
static void discover_peer(const struct ble_gap_disc_desc* disc) {
    if(connecting || connection != NONE)return;
    struct ble_hs_adv_fields fields;
    if(ble_hs_adv_parse_fields(&fields,disc->data,disc->length_data))return;
    bool ours=false;
    if(fields.mfg_data && fields.mfg_data_len >= 8 && fields.mfg_data[0] == 0xfe && fields.mfg_data[1] == 0xfe) {
        unsigned a=0,b=0,c=0,d=0,mode=0,ssid_len=0;
        if(fields.mfg_data_len>=11&&fields.mfg_data[2]=='X'&&fields.mfg_data[3]=='P'){
            a=fields.mfg_data[6];b=fields.mfg_data[7];c=fields.mfg_data[8];d=fields.mfg_data[9];
            mode=fields.mfg_data[5];ssid_len=fields.mfg_data[10];
            ours=fields.mfg_data[4]==1&&fields.mfg_data_len>=11+ssid_len;
        }else if(fields.mfg_data_len>=8){
            // 兼容首次广播格式；两端升级前仍可发现。/ Accept the first wire format while both readers upgrade.
            a=fields.mfg_data[2];b=fields.mfg_data[3];c=fields.mfg_data[4];d=fields.mfg_data[5];
            mode=fields.mfg_data[6]&1?2:1;ssid_len=fields.mfg_data[7];
            ours=fields.mfg_data_len>=8+ssid_len;
        }
        if (ours&&ssid_len<=sizeof(peer_info.ap_ssid)-1) {
            portENTER_CRITICAL(&guard);
            snprintf(peer_info.ip,sizeof(peer_info.ip),"%u.%u.%u.%u",a,b,c,d);
            const uint8_t* ssid=fields.mfg_data+(fields.mfg_data[2]=='X'?11:8);
            memcpy(peer_info.ap_ssid,ssid,ssid_len);
            peer_info.ap_ssid[ssid_len]=0;
            peer_info.direct=mode==2;
            peer_info.found=a||b||c||d;
            portEXIT_CRITICAL(&guard);
        }
    }
    for(unsigned i=0;i<fields.num_uuids128;i++)if(!ble_uuid_cmp(&fields.uuids128[i].u,&service_uuid.u))ours=true;
    if(!ours)return;
    if(scan_only)return;
    // 自动重连先取得扫描响应中的端点/热点名，避免连接后才发现子机不在主机热点上。
    // Auto-reconnect waits for the scan response endpoint/AP name so the secondary can join the primary hotspot.
    portENTER_CRITICAL(&guard);bool endpoint_known=peer_info.found;portEXIT_CRITICAL(&guard);
    if(!endpoint_known)return;
    if(esp_timer_get_time()<reject_until)
        for(unsigned i=0;i<rejected_count;i++)if(!memcmp(&rejected[i],&disc->addr,sizeof(ble_addr_t)))return;
    attempted=disc->addr;connecting=true;setup_deadline=esp_timer_get_time()+25000000;
    ble_gap_disc_cancel();
    struct ble_gap_conn_params params={.scan_itvl=0x20,.scan_window=0x20,.itvl_min=12,.itvl_max=24,
        .latency=0,.supervision_timeout=300,.min_ce_len=0,.max_ce_len=0};
    int rc=ble_gap_connect(address_type,&attempted,10000,&params,gap_event,NULL);
    if(rc)reject(rc);
}
static int gap_event(struct ble_gap_event* event, void* arg) {
    (void)arg;
    switch(event->type){
    case BLE_GAP_EVENT_DISC: discover_peer(&event->disc); break;
    case BLE_GAP_EVENT_CONNECT:
        if(event->connect.status){reject(event->connect.status);break;}
        connection=event->connect.conn_handle;connecting=true;setup_deadline=esp_timer_get_time()+25000000;
        if(current.role==2){int rc=ble_gap_security_initiate(connection);if(rc)reject(rc);}
        break;
    case BLE_GAP_EVENT_DISCONNECT: clear_link(event->disconnect.reason);break;
    case BLE_GAP_EVENT_PASSKEY_ACTION: {
        struct ble_sm_io io={.action=event->passkey.params.action};
        if(io.action!=BLE_SM_IOACT_DISP && io.action!=BLE_SM_IOACT_INPUT){reject(BLE_HS_EAUTHEN);break;}
        io.passkey=(uint32_t)strtoul(current.code,NULL,10);
        int rc=ble_sm_inject_io(event->passkey.conn_handle,&io);if(rc)reject(rc);
        break;
    }
    case BLE_GAP_EVENT_ENC_CHANGE:
        if(event->enc_change.status || !secure(event->enc_change.conn_handle)){reject(BLE_HS_EAUTHEN);break;}
        if(current.role==2 && !discovering){
            discovering=true;
            int rc=ble_gattc_exchange_mtu(connection,mtu_cb,(void*)(uintptr_t)epoch);if(rc)reject(rc);
        }
        ready_update();break;
    case BLE_GAP_EVENT_MTU: ready_update();break;
    case BLE_GAP_EVENT_SUBSCRIBE:
        if(current.role==1 && event->subscribe.attr_handle==local_tx){
            subscribed=event->subscribe.cur_notify;ready_update();
        }break;
    case BLE_GAP_EVENT_NOTIFY_RX:
        notification_received(event->notify_rx.conn_handle,event->notify_rx.attr_handle,event->notify_rx.om);
        break;
    case BLE_GAP_EVENT_REPEAT_PAIRING: return BLE_GAP_REPEAT_PAIRING_IGNORE;
    default: break;
    }
    return 0;
}
static void host_sync(void) {
    int rc=ble_hs_util_ensure_addr(0);if(!rc)rc=ble_hs_id_infer_auto(0,&address_type);
    synced=rc==0;if(rc)error_set(rc);
}
static void host_reset(int reason) { synced=false;clear_link(reason); }
static void host_run(void* arg) { (void)arg;nimble_port_run();vTaskSuspend(NULL); }
static void host_tick(struct ble_npl_event* ev) {
    (void)ev;
    portENTER_CRITICAL(&guard);bool enabled=wanted.role==current.role&&wanted.epoch==current.epoch&&!memcmp(wanted.code,current.code,7);portEXIT_CRITICAL(&guard);
    if(!enabled)return;
    int64_t now=esp_timer_get_time();
    if(connecting && now>setup_deadline)reject(BLE_HS_ETIMEOUT);
    if(synced && connection==NONE && !connecting && now>=retry_at){
        if(current.role==1 && adv_info_dirty && ble_gap_adv_active()) {
            // 扫描响应只能在重新广播时更新；IP 到达后重启一次广播，带出配对信息。
            // Scan response can only change on restart; restart advertising once the IP is ready.
            adv_info_dirty = false;
            ble_gap_adv_stop();
        }
        if(current.role==1 && !ble_gap_adv_active()){
            static uint8_t mfr[27];
            unsigned a=0,b=0,c=0,d=0;char tail[4]={0};
            if(adv_ip[0]) {
                if(sscanf(adv_ip,"%u.%u.%u.%u%1s",&a,&b,&c,&d,tail)!=4||tail[0])a=b=c=d=0;
            }
            size_t ssid_len=strnlen(adv_ap_ssid,sizeof(adv_ap_ssid)-1);
            if(ssid_len>16)ssid_len=0;
            mfr[0]=0xfe;mfr[1]=0xfe;mfr[2]='X';mfr[3]='P';mfr[4]=1;
            mfr[5]=ssid_len?2:1;mfr[6]=(uint8_t)a;mfr[7]=(uint8_t)b;mfr[8]=(uint8_t)c;mfr[9]=(uint8_t)d;
            mfr[10]=(uint8_t)ssid_len;memcpy(mfr+11,adv_ap_ssid,ssid_len);
            struct ble_hs_adv_fields fields={.flags=BLE_HS_ADV_F_DISC_GEN|BLE_HS_ADV_F_BREDR_UNSUP,
                .mfg_data=mfr,.mfg_data_len=(uint8_t)(11+ssid_len)};
            int rc=ble_gap_adv_set_fields(&fields);
            struct ble_hs_adv_fields rsp={.uuids128=(ble_uuid128_t*)&service_uuid,
                .num_uuids128=1,.uuids128_is_complete=1};
            int rsp_rc=ble_gap_adv_rsp_set_fields(&rsp);
            if(!rc&&rsp_rc)rc=rsp_rc;
            struct ble_gap_adv_params params={.conn_mode=BLE_GAP_CONN_MODE_UND,.disc_mode=BLE_GAP_DISC_MODE_GEN,
                .itvl_min=160,.itvl_max=240};
            if(!rc)rc=ble_gap_adv_start(address_type,NULL,BLE_HS_FOREVER,&params,gap_event,NULL);
            if(rc){error_set(rc);retry_at=now+2000000;}
            else {error_set(0);adv_info_dirty = false;}
        }else if(current.role==2 && !ble_gap_disc_active()){
            struct ble_gap_disc_params params={.itvl=0x50,.window=0x40,.passive=0,.filter_duplicates=0};
            int rc=ble_gap_disc(address_type,5000,&params,gap_event,NULL);
            retry_at=now+6000000;if(rc)error_set(rc);else error_set(0);
        }
    }
    pico_dual_ble_status_t status;pico_dual_ble_status(&status);
    packet_t p;
    if(status.connected && !writing && now>=tx_retry_at && xQueuePeek(outgoing,&p,0)==pdTRUE){
        if(p.generation!=epoch)xQueueReceive(outgoing,&p,0);
        else if(current.role==1){
            struct os_mbuf* om=ble_hs_mbuf_from_flat(p.bytes,p.size);
            if(om){int rc=ble_gatts_notify_custom(connection,local_tx,om);if(!rc)xQueueReceive(outgoing,&p,0);}
        }else{
            int rc=ble_gattc_write_flat(connection,remote_rx,p.bytes,p.size,write_cb,(void*)(uintptr_t)epoch);
            if(!rc)writing=true;else if(rc!=BLE_HS_ENOMEM && rc!=BLE_HS_EBUSY)reject(rc);
        }
    }
    ble_npl_callout_reset(&ticker,ble_npl_time_ms_to_ticks32(20));
}
static void supervisor(void* unused) {
    (void)unused;
    for(;;){
        portENTER_CRITICAL(&guard);config_t next=wanted;portEXIT_CRITICAL(&guard);
        if(!next.role)break;
        int rc=read_pico_ble_pager_reserve(true);
        if(rc || !read_pico_ble_pager_reservation_ready()){
            if(rc && rc!=ESP_ERR_INVALID_STATE)error_set(rc);
            vTaskDelay(pdMS_TO_TICKS(50));continue;
        }
        current=next;synced=false;rejected_count=0;reject_until=0;clear_link(0);
        rc=nimble_port_init();
        if(rc){error_set(rc);vTaskDelay(pdMS_TO_TICKS(1000));continue;}
        ble_hs_cfg.sync_cb=host_sync;ble_hs_cfg.reset_cb=host_reset;
        ble_hs_cfg.sm_io_cap=current.role==1?BLE_HS_IO_DISPLAY_ONLY:BLE_HS_IO_KEYBOARD_ONLY;
        ble_hs_cfg.sm_bonding=0;ble_hs_cfg.sm_mitm=1;ble_hs_cfg.sm_sc=1;ble_hs_cfg.sm_sc_only=1;
        ble_hs_cfg.sm_our_key_dist=ble_hs_cfg.sm_their_key_dist=0;
        ble_att_set_preferred_mtu(128);
        if(current.role==1){rc=ble_gatts_count_cfg(services);if(!rc)rc=ble_gatts_add_svcs(services);}
        if(rc){
            error_set(rc);
            int stopped=nimble_port_deinit();
            if(stopped){
                error_set(stopped);portENTER_CRITICAL(&guard);failed=true;portEXIT_CRITICAL(&guard);
                vTaskDelete(NULL);return;
            }
            vTaskDelay(pdMS_TO_TICKS(1000));continue;
        }
        ble_npl_callout_init(&ticker,nimble_port_get_dflt_eventq(),host_tick,NULL);
        nimble_port_freertos_init(host_run);
        ble_npl_callout_reset(&ticker,ble_npl_time_ms_to_ticks32(20));
        for(;;){
            portENTER_CRITICAL(&guard);bool same=wanted.role==current.role&&wanted.epoch==current.epoch&&!memcmp(wanted.code,current.code,7);portEXIT_CRITICAL(&guard);
            if(!same)break;
            vTaskDelay(pdMS_TO_TICKS(50));
        }
        rc=nimble_port_stop();
        if(!rc){nimble_port_freertos_deinit();ble_npl_callout_deinit(&ticker);rc=nimble_port_deinit();}
        if(rc){error_set(rc);portENTER_CRITICAL(&guard);failed=true;portEXIT_CRITICAL(&guard);vTaskDelete(NULL);return;}
        clear_link(0);synced=false;
    }
    read_pico_ble_pager_reserve(false);
    portENTER_CRITICAL(&guard);snapshot.active=false;snapshot.connected=false;worker=false;portEXIT_CRITICAL(&guard);
    vTaskDelete(NULL);
}
void pico_dual_ble_set_pairing_info(const char* ip, const char* ap_ssid) {
    char next_ip[48], next_ssid[33];
    snprintf(next_ip, sizeof(next_ip), "%s", ip ? ip : "");
    snprintf(next_ssid, sizeof(next_ssid), "%s", ap_ssid ? ap_ssid : "");
    portENTER_CRITICAL(&guard);
    if (strcmp(adv_ip, next_ip) || strcmp(adv_ap_ssid, next_ssid)) {
        memcpy(adv_ip, next_ip, sizeof(adv_ip));
        memcpy(adv_ap_ssid, next_ssid, sizeof(adv_ap_ssid));
        adv_info_dirty = true;
    }
    portEXIT_CRITICAL(&guard);
}
esp_err_t pico_dual_ble_scan_start(void) {
    portENTER_CRITICAL(&guard); scan_only = true; peer_info.found = false; portEXIT_CRITICAL(&guard);
    esp_err_t err = pico_dual_ble_configure(2, "000000");
    if (err != ESP_OK) portENTER_CRITICAL(&guard), scan_only = false, portEXIT_CRITICAL(&guard);
    return err;
}
void pico_dual_ble_scan_stop(void) {
    portENTER_CRITICAL(&guard); scan_only = false; portEXIT_CRITICAL(&guard);
    pico_dual_ble_stop();
}
void pico_dual_ble_peer_info(pico_dual_ble_peer_info_t* out) {
    if (!out) return;
    portENTER_CRITICAL(&guard); *out = peer_info; portEXIT_CRITICAL(&guard);
}
esp_err_t pico_dual_ble_configure(int role,const char* code) {
    if(role!=1 && role!=2)return ESP_ERR_INVALID_ARG;
    if(!code || strlen(code)!=6)return ESP_ERR_INVALID_ARG;
    for(unsigned i=0;i<6;i++)if(code[i]<'0'||code[i]>'9')return ESP_ERR_INVALID_ARG;
    // UI线程是唯一配置提交方，队列保留以免异步收发持有释放后的句柄。
    // UI is the sole configuration producer; retain queues to avoid stale asynchronous handles.
    if(!incoming)incoming=xQueueCreate(QUEUE_SIZE,sizeof(packet_t));
    if(!outgoing)outgoing=xQueueCreate(QUEUE_SIZE,sizeof(packet_t));
    if(!incoming || !outgoing)return ESP_ERR_NO_MEM;
    portENTER_CRITICAL(&guard);
    if(failed){portEXIT_CRITICAL(&guard);return ESP_ERR_INVALID_STATE;}
    bool changed=wanted.role!=role||memcmp(wanted.code,code,7);
    if(changed && ++config_epoch == 0) ++config_epoch;
    wanted.role=role;memcpy(wanted.code,code,7);wanted.epoch=config_epoch;
    if(changed&&role==2)memset(&peer_info,0,sizeof(peer_info));
    if(worker){portEXIT_CRITICAL(&guard);return ESP_OK;}
    worker=true;snapshot.active=true;portEXIT_CRITICAL(&guard);
    if(xTaskCreate(supervisor,"dual_ble",6144,NULL,3,NULL)!=pdPASS){
        portENTER_CRITICAL(&guard);worker=false;snapshot.active=false;portEXIT_CRITICAL(&guard);return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}
void pico_dual_ble_request_restart(void) {
    portENTER_CRITICAL(&guard);
    if(++config_epoch == 0) ++config_epoch;
    if(wanted.role) wanted.epoch=config_epoch;
    portEXIT_CRITICAL(&guard);
}
void pico_dual_ble_stop(void) { portENTER_CRITICAL(&guard);wanted.role=0;portEXIT_CRITICAL(&guard); }
void pico_dual_ble_status(pico_dual_ble_status_t* out) { portENTER_CRITICAL(&guard);*out=snapshot;portEXIT_CRITICAL(&guard); }
bool pico_dual_ble_can_send(void) {
    pico_dual_ble_status_t s;pico_dual_ble_status(&s);
    return s.connected && outgoing && uxQueueSpacesAvailable(outgoing)>0;
}
bool pico_dual_ble_send(const uint8_t* data,size_t n) {
    if(!data || !n || n>PICO_DUAL_BLE_PACKET_MAX || !pico_dual_ble_can_send())return false;
    pico_dual_ble_status_t s;pico_dual_ble_status(&s);
    packet_t p={.generation=s.generation,.size=(uint8_t)n};memcpy(p.bytes,data,n);
    if(xQueueSend(outgoing,&p,0)!=pdTRUE)return false;
    // 控制包入队即唤醒现有主机循环，不等待20ms轮询；背压仍遵守重试期限。
    // Wake the existing host loop on enqueue instead of waiting for its 20ms poll; preserve the backpressure deadline.
    ble_npl_callout_reset(&ticker,1);
    return true;
}
size_t pico_dual_ble_receive(uint8_t* data,size_t capacity) {
    if(!incoming || !data || capacity<PICO_DUAL_BLE_PACKET_MAX)return 0;
    pico_dual_ble_status_t s;pico_dual_ble_status(&s);packet_t p;
    while(xQueueReceive(incoming,&p,0)==pdTRUE){
        if(s.connected && p.generation==s.generation){memcpy(data,p.bytes,p.size);return p.size;}
    }
    return 0;
}

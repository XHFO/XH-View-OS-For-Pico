/* SPDX-License-Identifier: Apache-2.0
 * 原生网页预览的 Wi-Fi、TF 和 USB 可控替身；界面与事件仍由生产控制器处理。
 * Controllable Wi-Fi, TF and USB doubles for the native web preview; production controllers retain UI/events.
 * 冻结：这些硬件替身不操作主机外设；家庭服务另经用户手动配置后使用真实主机网络，凭据不写虚拟TF。
 * Frozen: Hardware doubles never access host peripherals; separately configured Home services use real host networking and keep credentials outside virtual TF.
 */
#include "native_preview_services.h"
#include "native_home_http_stub.h"
#include "read_pico_connectivity.h"
#include "read_pico_sd.h"
#include "read_pico_usb.h"
#include "pico_system.h"
#include <pthread.h>
#include <string.h>

/* Real controller workers may query service snapshots while the UI ticks. */
static pthread_once_t np_mutex_once=PTHREAD_ONCE_INIT;
static pthread_mutex_t np_mutex;
static void np_mutex_init(void){pthread_mutexattr_t a;pthread_mutexattr_init(&a);pthread_mutexattr_settype(&a,PTHREAD_MUTEX_RECURSIVE);pthread_mutex_init(&np_mutex,&a);pthread_mutexattr_destroy(&a);}
static void np_unlock(int *held){if(*held)pthread_mutex_unlock(&np_mutex);}
#define NP_GUARD int np_held __attribute__((cleanup(np_unlock)))=0;pthread_once(&np_mutex_once,np_mutex_init);pthread_mutex_lock(&np_mutex);np_held=1

enum { NP_NONE, NP_ENABLED, NP_SCAN, NP_CONNECT, NP_FORGET, NP_AUTO, NP_TRANSFER_START, NP_TRANSFER_STOP };
static read_pico_connectivity_status_t np_net;
static read_pico_sd_info_t np_sd;
static read_pico_usb_status_t np_usb;
static int np_job;
static bool np_flag, np_initialized, np_scan_fail, np_connect_fail, np_format_fail, np_usb_stop_fail;
static bool np_resume_enabled;
static char np_ssid[33];
static read_pico_transfer_mode_t np_transfer_mode;
static int64_t np_time, np_due, np_mount_due;

static void np_copy(char *out, size_t bytes, const char *in) {
    if(bytes)snprintf(out,bytes,"%s",in?in:"");
}
static void np_networks(void) {
    static const char *names[]={"Home","Office","XH Lab","Guest Wi-Fi","Reading Room","Mobile Hotspot"};
    np_net.network_count=sizeof(names)/sizeof(names[0]);
    for(size_t i=0;i<np_net.network_count;i++) {
        read_pico_transfer_network_t *n=&np_net.networks[i];memset(n,0,sizeof(*n));
        np_copy(n->ssid,sizeof(n->ssid),names[i]);n->rssi=(int8_t)(-38-(int)i*7);
        n->supported=true;n->requires_password=i!=3;n->authmode=n->requires_password?3:0;
    }
}
static void np_card_ready(bool verified) {
    np_sd.present=np_sd.mounted=true;np_sd.exported=np_sd.needs_format=false;
    np_sd.capacity_bytes=np_sd.physical_bytes=32000000000ULL;np_sd.free_bytes=31700000000ULL;
    np_sd.error=ESP_OK;np_sd.state=READ_PICO_SD_STATE_READY;np_sd.stage=READ_PICO_SD_STAGE_COMPLETE;
    np_sd.filesystem=READ_PICO_SD_FS_FAT;np_sd.read_verified=np_sd.write_verified=verified;
    np_copy(np_sd.name,sizeof(np_sd.name),"PREVIEW");
}
void native_preview_services_init(void) {NP_GUARD;
    memset(&np_net,0,sizeof(np_net));memset(&np_sd,0,sizeof(np_sd));memset(&np_usb,0,sizeof(np_usb));
    np_job=NP_NONE;np_flag=np_scan_fail=np_connect_fail=np_format_fail=np_usb_stop_fail=false;
    np_due=np_mount_due=0;np_ssid[0]=0;np_resume_enabled=true;
    np_net.ready=np_net.wifi_enabled=np_net.connected=true;np_net.state=READ_PICO_CONNECTIVITY_CONNECTED;
    np_copy(np_net.ssid,sizeof(np_net.ssid),"Home");np_copy(np_net.ip,sizeof(np_net.ip),"192.168.50.123");np_net.rssi=-43;
    np_networks();np_net.saved_count=3;
    for(size_t i=0;i<np_net.saved_count;i++){np_copy(np_net.saved[i].ssid,sizeof(np_net.saved[i].ssid),np_net.networks[i].ssid);np_net.saved[i].auto_join=true;}
    np_sd.media_generation=1;np_card_ready(false);np_usb.debug_restored=true;np_initialized=true;
}
static void np_ensure(void){if(!np_initialized)native_preview_services_init();}
static esp_err_t np_queue(int job) {
    np_ensure();if(np_net.busy||np_net.suspended)return ESP_ERR_INVALID_STATE;
    np_job=job;np_net.busy=true;np_net.submitted_seq++;np_due=np_time+500;
    np_net.last_error=ESP_OK;return ESP_OK;
}
static int np_saved(const char *ssid) {
    for(size_t i=0;i<np_net.saved_count;i++)if(!strcmp(np_net.saved[i].ssid,ssid))return (int)i;
    return -1;
}
static void np_remember(const char *ssid) {
    int old=np_saved(ssid);read_pico_connectivity_saved_t entry={.auto_join=true};
    np_copy(entry.ssid,sizeof(entry.ssid),ssid);
    if(old>=0){entry.auto_join=np_net.saved[old].auto_join;memmove(np_net.saved+1,np_net.saved,(size_t)old*sizeof(np_net.saved[0]));}
    else {size_t n=np_net.saved_count<READ_PICO_CONNECTIVITY_SAVED_MAX?np_net.saved_count++:READ_PICO_CONNECTIVITY_SAVED_MAX-1;memmove(np_net.saved+1,np_net.saved,n*sizeof(np_net.saved[0]));}
    np_net.saved[0]=entry;
}
void native_preview_services_tick(int64_t now_ms) {NP_GUARD;
    np_ensure();np_time=now_ms;
    if(np_mount_due&&np_time>=np_mount_due){np_mount_due=0;if(np_sd.present)np_card_ready(false);}
    if(!np_job||np_time<np_due)return;
    int job=np_job;np_job=NP_NONE;np_net.busy=false;np_net.completed_seq=np_net.submitted_seq;
    np_net.result_seq++;np_net.last_error=ESP_OK;
    np_net.last_operation=job==NP_ENABLED?READ_PICO_CONNECTIVITY_OP_WIFI:job==NP_SCAN?READ_PICO_CONNECTIVITY_OP_SCAN:
        job==NP_CONNECT?READ_PICO_CONNECTIVITY_OP_CONNECT:job==NP_FORGET||job==NP_AUTO?READ_PICO_CONNECTIVITY_OP_PROFILE:READ_PICO_CONNECTIVITY_OP_TRANSFER;
    if(job==NP_ENABLED){
        np_net.wifi_enabled=np_flag;np_resume_enabled=np_flag;np_net.connected=np_net.connecting=false;
        np_net.ssid[0]=np_net.ip[0]=0;np_net.state=np_flag?READ_PICO_CONNECTIVITY_IDLE:READ_PICO_CONNECTIVITY_DISABLED;
        if(!np_flag)np_net.network_count=0;
    } else if(job==NP_SCAN){
        np_net.scanning=false;np_net.scan_completed_seq++;
        np_net.scan_error=np_scan_fail?ESP_FAIL:ESP_OK;
        np_net.scan_failure=np_scan_fail?READ_PICO_CONNECTIVITY_SCAN_TIMEOUT:READ_PICO_CONNECTIVITY_SCAN_NONE;
        np_net.last_error=np_net.scan_error;
        if(!np_scan_fail)np_networks();
        np_net.state=np_net.connected?READ_PICO_CONNECTIVITY_CONNECTED:READ_PICO_CONNECTIVITY_IDLE;
    } else if(job==NP_CONNECT){
        np_net.connecting=false;np_net.wifi_enabled=true;np_resume_enabled=true;
        np_net.connected=!np_connect_fail;
        if(np_connect_fail){np_net.last_error=ESP_FAIL;np_net.state=READ_PICO_CONNECTIVITY_ERROR;np_net.ip[0]=0;}
        else{np_copy(np_net.ssid,sizeof(np_net.ssid),np_ssid);np_copy(np_net.ip,sizeof(np_net.ip),"192.168.50.123");np_net.rssi=-43;np_net.state=READ_PICO_CONNECTIVITY_CONNECTED;np_remember(np_ssid);}
    } else if(job==NP_FORGET){
        int at=np_saved(np_ssid);if(at>=0){memmove(np_net.saved+at,np_net.saved+at+1,(np_net.saved_count-(size_t)at-1)*sizeof(np_net.saved[0]));np_net.saved_count--;}
    } else if(job==NP_AUTO){int at=np_saved(np_ssid);if(at>=0)np_net.saved[at].auto_join=np_flag;
    } else if(job==NP_TRANSFER_START){
        np_net.transfer_active=true;np_net.wifi_enabled=true;
        np_net.transfer.mode=np_transfer_mode;np_net.transfer.state=READ_PICO_TRANSFER_READY;np_net.transfer.network_ready=true;
        np_net.transfer.sta_count=1;
        np_copy(np_net.transfer.url,sizeof(np_net.transfer.url),np_transfer_mode==READ_PICO_TRANSFER_MODE_AP?"http://192.168.4.1":"http://192.168.50.123");
        np_copy(np_net.transfer.ssid,sizeof(np_net.transfer.ssid),"Read-Pico-Preview");
        np_copy(np_net.transfer.wifi_ssid,sizeof(np_net.transfer.wifi_ssid),np_net.ssid);
        np_net.transfer.wifi_configured=np_net.saved_count!=0;
    } else if(job==NP_TRANSFER_STOP){np_net.transfer_active=false;np_net.transfer.network_ready=false;np_net.transfer.state=READ_PICO_TRANSFER_STOPPED;}
}

esp_err_t read_pico_connectivity_init(void){NP_GUARD;np_ensure();return ESP_OK;}
void read_pico_connectivity_get_status(read_pico_connectivity_status_t *out){NP_GUARD;np_ensure();if(out)*out=np_net;}
esp_err_t read_pico_connectivity_set_enabled(bool enabled){NP_GUARD;esp_err_t e=np_queue(NP_ENABLED);if(e==ESP_OK)np_flag=enabled;return e;}
esp_err_t read_pico_connectivity_scan(void){NP_GUARD;np_ensure();if(!np_net.wifi_enabled)return ESP_ERR_INVALID_STATE;esp_err_t e=np_queue(NP_SCAN);if(e==ESP_OK){np_net.scanning=true;np_net.state=READ_PICO_CONNECTIVITY_SCANNING;}return e;}
esp_err_t read_pico_connectivity_connect(const char *ssid,const char *password){NP_GUARD;
    (void)password;if(!ssid||!*ssid||strlen(ssid)>32)return ESP_ERR_INVALID_ARG;
    esp_err_t e=np_queue(NP_CONNECT);if(e==ESP_OK){np_copy(np_ssid,sizeof(np_ssid),ssid);np_net.connecting=true;np_net.state=READ_PICO_CONNECTIVITY_CONNECTING;np_copy(np_net.ssid,sizeof(np_net.ssid),ssid);}return e;
}
esp_err_t read_pico_connectivity_connect_saved(const char *ssid){NP_GUARD;np_ensure();if(!ssid||np_saved(ssid)<0)return ESP_ERR_NOT_FOUND;return read_pico_connectivity_connect(ssid,NULL);}
esp_err_t read_pico_connectivity_forget(const char *ssid){NP_GUARD;if(!ssid)return ESP_ERR_INVALID_ARG;esp_err_t e=np_queue(NP_FORGET);if(e==ESP_OK)np_copy(np_ssid,sizeof(np_ssid),ssid);return e;}
esp_err_t read_pico_connectivity_set_auto_join(const char *ssid,bool enabled){NP_GUARD;if(!ssid)return ESP_ERR_INVALID_ARG;esp_err_t e=np_queue(NP_AUTO);if(e==ESP_OK){np_copy(np_ssid,sizeof(np_ssid),ssid);np_flag=enabled;}return e;}
esp_err_t read_pico_connectivity_start_transfer(const read_pico_transfer_cfg_t *cfg){NP_GUARD;
    if(!cfg||!cfg->root_dir)return ESP_ERR_INVALID_ARG;np_ensure();
    if(np_net.transfer_active||np_usb.active||(cfg->mode==READ_PICO_TRANSFER_MODE_STA&&!np_net.connected))return ESP_ERR_INVALID_STATE;
    esp_err_t e=np_queue(NP_TRANSFER_START);if(e==ESP_OK){np_transfer_mode=cfg->mode;np_net.transfer.state=READ_PICO_TRANSFER_STARTING;}return e;
}
esp_err_t read_pico_connectivity_stop_transfer(void){NP_GUARD;return np_queue(NP_TRANSFER_STOP);}
esp_err_t read_pico_connectivity_quiesce(uint32_t timeout_ms){NP_GUARD;
    (void)timeout_ms;np_ensure();np_resume_enabled=np_net.wifi_enabled;np_job=NP_NONE;
    np_net.suspended=true;np_net.busy=np_net.scanning=np_net.connecting=np_net.transfer_active=false;
    np_net.transfer.network_ready=false;np_net.transfer.state=READ_PICO_TRANSFER_STOPPED;return ESP_OK;
}
esp_err_t read_pico_connectivity_resume(void){NP_GUARD;np_ensure();if(np_net.suspended){np_net.suspended=false;np_net.wifi_enabled=np_resume_enabled;}np_net.last_error=ESP_OK;np_net.completed_seq=++np_net.submitted_seq;return ESP_OK;}
void read_pico_connectivity_get_resume_status(read_pico_connectivity_resume_status_t *out){NP_GUARD;
    np_ensure();if(out)*out=(read_pico_connectivity_resume_status_t){np_net.suspended,np_net.busy,np_net.submitted_seq,np_net.completed_seq,np_net.last_error};
}
bool read_pico_connectivity_online(void){NP_GUARD;np_ensure();return np_net.connected&&!np_net.suspended;}
void read_pico_connectivity_get_link(read_pico_connectivity_link_t *out){NP_GUARD;if(!out)return;np_ensure();out->online=np_net.connected&&!np_net.suspended;snprintf(out->ssid,sizeof(out->ssid),"%s",np_net.ssid);}

/* Legacy file-transfer page shares the same virtual backend, never an HTTP server. */
esp_err_t read_pico_transfer_start(const read_pico_transfer_cfg_t *cfg){NP_GUARD;return read_pico_connectivity_start_transfer(cfg);}
void read_pico_transfer_stop(void){NP_GUARD;(void)read_pico_transfer_stop_checked();}
esp_err_t read_pico_transfer_stop_checked(void){NP_GUARD;np_ensure();if(np_net.transfer.state==READ_PICO_TRANSFER_UPLOADING)return ESP_ERR_INVALID_STATE;np_job=NP_NONE;np_net.busy=np_net.transfer_active=false;np_net.transfer.network_ready=false;np_net.transfer.state=READ_PICO_TRANSFER_STOPPED;return ESP_OK;}
bool read_pico_transfer_try_stop_if_idle(void){NP_GUARD;return read_pico_transfer_stop_checked()==ESP_OK;}
void read_pico_transfer_get_status(read_pico_transfer_status_t *out){NP_GUARD;np_ensure();np_net.transfer.wifi_configured=np_net.saved_count>0;np_copy(np_net.transfer.wifi_ssid,sizeof(np_net.transfer.wifi_ssid),np_net.saved_count?np_net.saved[0].ssid:"");if(out)*out=np_net.transfer;}
void read_pico_transfer_service_poll(void){NP_GUARD;np_ensure();}
esp_err_t read_pico_transfer_get_saved_wifi(char ssid[33],bool *configured){NP_GUARD;np_ensure();if(!ssid||!configured)return ESP_ERR_INVALID_ARG;*configured=np_net.saved_count>0;np_copy(ssid,33,*configured?np_net.saved[0].ssid:"");return ESP_OK;}
esp_err_t read_pico_transfer_forget_wifi(void){NP_GUARD;np_ensure();if(np_net.transfer_active&&np_net.transfer.mode!=READ_PICO_TRANSFER_MODE_AP)return ESP_ERR_INVALID_STATE;if(np_net.transfer.state==READ_PICO_TRANSFER_UPLOADING)return ESP_ERR_INVALID_STATE;if(np_net.saved_count){memmove(np_net.saved,np_net.saved+1,(np_net.saved_count-1)*sizeof(np_net.saved[0]));np_net.saved_count--;}return ESP_OK;}
esp_err_t read_pico_transfer_scan_wifi(read_pico_transfer_network_t out[READ_PICO_TRANSFER_SCAN_MAX],size_t *count){NP_GUARD;np_ensure();if(!out||!count)return ESP_ERR_INVALID_ARG;*count=0;if(np_net.transfer_active||np_usb.active)return ESP_ERR_INVALID_STATE;if(np_scan_fail)return ESP_FAIL;np_networks();*count=np_net.network_count;memcpy(out,np_net.networks,*count*sizeof(*out));return ESP_OK;}
esp_err_t read_pico_transfer_save_wifi(const char *ssid,const char *password){NP_GUARD;np_ensure();if(!ssid||!*ssid||strlen(ssid)>32||!password||(strlen(password)>0&&(strlen(password)<8||strlen(password)>63)))return ESP_ERR_INVALID_ARG;if(np_net.transfer_active||np_usb.active)return ESP_ERR_INVALID_STATE;np_remember(ssid);return ESP_OK;}

esp_err_t read_pico_sd_start_probe(void){NP_GUARD;np_ensure();return ESP_OK;}
esp_err_t read_pico_sd_get_info(read_pico_sd_info_t *out){NP_GUARD;np_ensure();if(out)*out=np_sd;return np_sd.error;}
esp_err_t read_pico_sd_remount(void){NP_GUARD;
    np_ensure();if(!np_sd.present||np_sd.exported)return ESP_ERR_INVALID_STATE;
    np_sd.mounted=np_sd.read_verified=np_sd.write_verified=false;np_sd.state=READ_PICO_SD_STATE_DETECTING;
    np_sd.stage=READ_PICO_SD_STAGE_DETECTING;np_sd.error=ESP_ERR_NOT_FINISHED;np_mount_due=np_time+500;return ESP_ERR_NOT_FINISHED;
}
esp_err_t read_pico_sd_verify(void){NP_GUARD;np_ensure();if(!np_sd.present||!np_sd.mounted||np_sd.exported)return ESP_ERR_INVALID_STATE;np_sd.read_verified=np_sd.write_verified=true;np_sd.state=READ_PICO_SD_STATE_READY;np_sd.stage=READ_PICO_SD_STAGE_COMPLETE;np_sd.error=ESP_OK;return ESP_OK;}
esp_err_t read_pico_sd_format(void){NP_GUARD;
    np_ensure();if(!np_sd.present||np_sd.exported)return ESP_ERR_INVALID_STATE;np_sd.format_started=true;
    if(np_format_fail){np_sd.mounted=false;np_sd.state=READ_PICO_SD_STATE_IO_ERROR;np_sd.stage=READ_PICO_SD_STAGE_FAILED;np_sd.error=ESP_FAIL;return ESP_FAIL;}
    // 只重设虚拟卡快照，绝不清除私有书库或电脑文件。/ Reset the virtual card snapshot only; never erase the private library or host files.
    np_card_ready(true);return ESP_OK;
}
esp_err_t read_pico_sd_sync(void){NP_GUARD;np_ensure();return np_sd.mounted&&!np_sd.exported?ESP_OK:ESP_ERR_INVALID_STATE;}
esp_err_t read_pico_sd_raw_acquire(sdmmc_card_t **out_card){NP_GUARD;if(out_card)*out_card=NULL;return ESP_ERR_NOT_SUPPORTED;}
esp_err_t read_pico_sd_raw_release(void){NP_GUARD;return ESP_ERR_NOT_SUPPORTED;}

esp_err_t read_pico_usb_init(void){NP_GUARD;np_ensure();return ESP_OK;}
void read_pico_usb_get_status(read_pico_usb_status_t *out){NP_GUARD;np_ensure();if(out)*out=np_usb;}
esp_err_t read_pico_usb_start(void){NP_GUARD;
    np_ensure();if(np_usb.active||!np_sd.present||!np_sd.mounted||np_sd.exported)return ESP_ERR_INVALID_STATE;
    memset(&np_usb,0,sizeof(np_usb));np_usb.state=READ_PICO_USB_EXPORTED;np_usb.active=np_usb.connected=np_usb.card_exported=true;
    np_sd.exported=true;np_sd.mounted=false;np_sd.state=READ_PICO_SD_STATE_EXPORTED;return ESP_OK;
}
esp_err_t read_pico_usb_stop(void){NP_GUARD;
    np_ensure();if(np_usb_stop_fail){np_usb.last_error=ESP_FAIL;np_usb.state=READ_PICO_USB_ERROR;return ESP_FAIL;}
    np_usb.active=np_usb.connected=np_usb.card_exported=np_usb.writing=false;np_usb.debug_restored=true;
    np_usb.state=READ_PICO_USB_IDLE;np_usb.last_error=ESP_OK;np_card_ready(true);return ESP_OK;
}

bool native_preview_service_scenario(const char *name){NP_GUARD;
    np_ensure();if(!name)return false;
    if(!strncmp(name,"power-",6)){extern bool native_preview_power_scenario(const char*);return native_preview_power_scenario(name);}
    if(!strcmp(name,"default")){native_preview_services_init();return true;}
    if(!strcmp(name,"first-use")){native_preview_services_init();np_net.connected=np_net.wifi_enabled=false;np_resume_enabled=false;np_net.state=READ_PICO_CONNECTIVITY_DISABLED;np_net.saved_count=np_net.network_count=0;np_net.ssid[0]=np_net.ip[0]=0;return true;}
    if(!strcmp(name,"offline")){np_job=NP_NONE;np_net.busy=np_net.connected=np_net.connecting=np_net.scanning=false;np_net.ssid[0]=np_net.ip[0]=0;np_net.state=np_net.wifi_enabled?READ_PICO_CONNECTIVITY_IDLE:READ_PICO_CONNECTIVITY_DISABLED;return true;}
    if(!strcmp(name,"scan-failed")){np_scan_fail=true;return true;}
    if(!strcmp(name,"scan-ready")){np_scan_fail=false;return true;}
    if(!strcmp(name,"connect-failed")){np_connect_fail=true;return true;}
    if(!strcmp(name,"connect-ready")){np_connect_fail=false;return true;}
    if(!strcmp(name,"tf-absent")){np_sd.present=np_sd.mounted=np_sd.exported=false;np_sd.media_generation++;np_sd.state=READ_PICO_SD_STATE_ABSENT;np_sd.error=ESP_FAIL;np_sd.capacity_bytes=np_sd.free_bytes=0;return true;}
    if(!strcmp(name,"tf-ready")){np_sd.media_generation++;np_card_ready(false);return true;}
    if(!strcmp(name,"tf-remount")){np_card_ready(false);return true;}
    if(!strcmp(name,"tf-unformatted")){np_card_ready(false);np_sd.mounted=false;np_sd.filesystem=READ_PICO_SD_FS_UNKNOWN;np_sd.state=READ_PICO_SD_STATE_UNSUPPORTED_FILESYSTEM;np_sd.error=ESP_FAIL;return true;}
    if(!strcmp(name,"tf-full")){np_card_ready(true);np_sd.free_bytes=0;return true;}
    if(!strcmp(name,"format-failed")){np_format_fail=true;return true;}
    if(!strcmp(name,"format-ready")){np_format_fail=false;return true;}
    if(!strcmp(name,"usb-writing")){if(!np_usb.active)return false;np_usb.writing=true;np_usb.written_blocks+=32;return true;}
    if(!strcmp(name,"usb-idle")){np_usb.writing=false;return true;}
    if(!strcmp(name,"usb-eject")){if(!np_usb.active)return false;np_usb.writing=false;np_usb.host_ejected=true;return true;}
    if(!strcmp(name,"usb-stop-failed")){np_usb_stop_fail=true;return true;}
    if(!strcmp(name,"usb-stop-ready")){np_usb_stop_fail=false;return true;}
    if(!strcmp(name,"transfer-upload")){if(!np_net.transfer_active)return false;np_net.transfer.state=READ_PICO_TRANSFER_UPLOADING;np_net.transfer.cur_total=1048576;np_net.transfer.cur_bytes=524288;np_copy(np_net.transfer.cur_name,sizeof(np_net.transfer.cur_name),"预览图书.epub");return true;}
    if(!strcmp(name,"transfer-finished")){if(!np_net.transfer_active)return false;np_net.transfer.state=READ_PICO_TRANSFER_READY;np_net.transfer.cur_bytes=np_net.transfer.cur_total;np_net.transfer.done_count++;np_net.transfer.changed_count++;return true;}
    return false;
}
void native_preview_service_status_json(FILE *out){NP_GUARD;
    np_ensure();if(!out)return;
    bool home_configured=ps_home_service_configured();
    fprintf(out,"{\"simulated\":true,\"wifiSimulated\":true,\"bluetoothSimulated\":true,\"usbSimulated\":true,\"batterySimulated\":true,\"sensorsSimulated\":true,\"tfCapacitySimulated\":true,\"homeSimulated\":false,\"homePortalTransportSimulated\":true,\"homeConfigured\":%s,\"hostNetworkAccess\":%s,\"hostNetworkOptIn\":true,\"hostUsbAccess\":false,\"hostBlockAccess\":false,\"wifiEnabled\":%s,\"connected\":%s,\"networkCount\":%zu,\"savedNetworkCount\":%zu,\"busy\":%s,\"tfPresent\":%s,\"tfMounted\":%s,\"usbActive\":%s,\"transferActive\":%s}",home_configured?"true":"false",home_configured&&native_preview_home_network_enabled()?"true":"false",np_net.wifi_enabled?"true":"false",np_net.connected?"true":"false",np_net.network_count,np_net.saved_count,np_net.busy?"true":"false",np_sd.present?"true":"false",np_sd.mounted?"true":"false",np_usb.active?"true":"false",np_net.transfer_active?"true":"false");
}

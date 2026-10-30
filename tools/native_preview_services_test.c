/* SPDX-License-Identifier: Apache-2.0
 * 预览服务状态交接检查，只使用进程内快照。/ Verify preview-service handoffs using in-process snapshots only.
 */
#include "native_preview_services.h"
#include "read_pico_connectivity.h"
#include "read_pico_sd.h"
#include "read_pico_usb.h"
#include <assert.h>
#include <string.h>

static bool home_configured;
bool native_preview_power_scenario(const char *name){(void)name;return false;}
bool ps_home_service_configured(void){return home_configured;}
static void check_service_scope(bool configured){
    home_configured=configured;FILE *f=tmpfile();assert(f);native_preview_service_status_json(f);
    assert(!fseek(f,0,SEEK_SET));char text[2048]={0};size_t n=fread(text,1,sizeof(text)-1,f);assert(n>0&&!ferror(f));fclose(f);
    assert(strstr(text,"\"homeSimulated\":false"));
    assert(strstr(text,configured?"\"homeConfigured\":true":"\"homeConfigured\":false"));
    assert(strstr(text,configured?"\"hostNetworkAccess\":true":"\"hostNetworkAccess\":false"));
    assert(strstr(text,"\"hostNetworkOptIn\":true"));
    const char *hardware[]={"wifiSimulated","bluetoothSimulated","usbSimulated","batterySimulated","sensorsSimulated","tfCapacitySimulated"};
    for(unsigned i=0;i<sizeof(hardware)/sizeof(*hardware);i++){char key[80];snprintf(key,sizeof(key),"\"%s\":true",hardware[i]);assert(strstr(text,key));}
    assert(strstr(text,"\"hostUsbAccess\":false")&&strstr(text,"\"hostBlockAccess\":false"));
    assert(!strstr(text,"private-value-never-persisted"));
}

int main(void) {
    read_pico_connectivity_status_t net;read_pico_sd_info_t sd;read_pico_usb_status_t usb;
    native_preview_services_init();read_pico_connectivity_get_status(&net);
    assert(net.connected&&net.saved_count==3&&net.network_count==6);
    assert(native_preview_service_scenario("first-use"));read_pico_connectivity_get_status(&net);
    assert(!net.wifi_enabled&&!net.connected&&!net.saved_count&&!net.network_count);
    assert(read_pico_connectivity_scan()==ESP_ERR_INVALID_STATE);
    assert(read_pico_connectivity_set_enabled(true)==ESP_OK);native_preview_services_tick(499);
    read_pico_connectivity_get_status(&net);assert(net.busy&&!net.wifi_enabled);
    native_preview_services_tick(500);read_pico_connectivity_get_status(&net);assert(!net.busy&&net.wifi_enabled);
    assert(read_pico_connectivity_scan()==ESP_OK);native_preview_services_tick(1000);
    read_pico_connectivity_get_status(&net);assert(net.network_count==6&&net.scan_completed_seq==1);
    assert(read_pico_connectivity_connect("Home","private-value-never-persisted")==ESP_OK);
    native_preview_services_tick(1500);read_pico_connectivity_get_status(&net);
    assert(net.connected&&net.saved_count==1&&!strcmp(net.saved[0].ssid,"Home"));
    assert(native_preview_service_scenario("scan-failed"));assert(read_pico_connectivity_scan()==ESP_OK);
    native_preview_services_tick(2000);read_pico_connectivity_get_status(&net);
    assert(net.scan_failure==READ_PICO_CONNECTIVITY_SCAN_TIMEOUT&&net.last_operation==READ_PICO_CONNECTIVITY_OP_SCAN);
    assert(native_preview_service_scenario("scan-ready"));assert(read_pico_connectivity_scan()==ESP_OK);
    native_preview_services_tick(2500);read_pico_connectivity_get_status(&net);assert(net.scan_error==ESP_OK);
    assert(read_pico_connectivity_forget("Home")==ESP_OK);native_preview_services_tick(3000);
    read_pico_connectivity_get_status(&net);assert(net.saved_count==0);
    assert(read_pico_connectivity_quiesce(15000)==ESP_OK);
    assert(read_pico_connectivity_scan()==ESP_ERR_INVALID_STATE);assert(read_pico_connectivity_resume()==ESP_OK);
    read_pico_transfer_cfg_t cfg={.root_dir="private-fixture-only",.mode=READ_PICO_TRANSFER_MODE_STA};
    assert(read_pico_connectivity_start_transfer(&cfg)==ESP_OK);native_preview_services_tick(3500);
    assert(native_preview_service_scenario("transfer-upload"));read_pico_connectivity_get_status(&net);
    assert(net.transfer_active&&net.transfer.state==READ_PICO_TRANSFER_UPLOADING);
    assert(native_preview_service_scenario("transfer-finished"));assert(read_pico_connectivity_stop_transfer()==ESP_OK);
    native_preview_services_tick(4000);read_pico_connectivity_get_status(&net);assert(!net.transfer_active);
    assert(native_preview_service_scenario("tf-unformatted"));read_pico_sd_get_info(&sd);
    assert(sd.present&&!sd.mounted&&sd.state==READ_PICO_SD_STATE_UNSUPPORTED_FILESYSTEM);
    assert(read_pico_sd_format()==ESP_OK);read_pico_sd_get_info(&sd);assert(sd.mounted&&sd.read_verified&&sd.write_verified);
    assert(read_pico_sd_remount()==ESP_ERR_NOT_FINISHED);read_pico_sd_get_info(&sd);assert(!sd.mounted);
    native_preview_services_tick(4500);read_pico_sd_get_info(&sd);assert(sd.mounted&&!sd.read_verified);
    assert(read_pico_sd_verify()==ESP_OK);assert(read_pico_usb_start()==ESP_OK);
    read_pico_sd_get_info(&sd);assert(sd.exported&&!sd.mounted);
    assert(native_preview_service_scenario("usb-writing"));read_pico_usb_get_status(&usb);assert(usb.writing&&usb.written_blocks==32);
    assert(native_preview_service_scenario("usb-eject"));read_pico_usb_get_status(&usb);assert(usb.host_ejected&&!usb.writing);
    assert(native_preview_service_scenario("usb-stop-failed"));assert(read_pico_usb_stop()==ESP_FAIL);
    read_pico_sd_get_info(&sd);assert(sd.exported);
    assert(native_preview_service_scenario("usb-stop-ready"));assert(read_pico_usb_stop()==ESP_OK);
    read_pico_sd_get_info(&sd);assert(sd.mounted&&!sd.exported);
    assert(native_preview_service_scenario("tf-absent"));assert(read_pico_sd_format()==ESP_ERR_INVALID_STATE);
    assert(!native_preview_service_scenario("unknown"));
    /* Legacy native transfer UI shares virtual profiles and lifecycle. */
    assert(native_preview_service_scenario("first-use"));
    char saved[33];bool configured;read_pico_transfer_network_t networks[READ_PICO_TRANSFER_SCAN_MAX];size_t count;
    assert(read_pico_transfer_get_saved_wifi(saved,&configured)==ESP_OK&&!configured&&!saved[0]);
    assert(read_pico_transfer_scan_wifi(networks,&count)==ESP_OK&&count==6);
    assert(read_pico_transfer_save_wifi("Home","short")==ESP_ERR_INVALID_ARG);
    assert(read_pico_transfer_save_wifi("Home","preview-only")==ESP_OK);
    assert(read_pico_transfer_get_saved_wifi(saved,&configured)==ESP_OK&&configured&&!strcmp(saved,"Home"));
    cfg.mode=READ_PICO_TRANSFER_MODE_AP;assert(read_pico_transfer_start(&cfg)==ESP_OK);
    native_preview_services_tick(5000);read_pico_transfer_status_t transfer;read_pico_transfer_get_status(&transfer);
    assert(transfer.state==READ_PICO_TRANSFER_READY&&transfer.network_ready);
    assert(native_preview_service_scenario("transfer-upload"));assert(!read_pico_transfer_try_stop_if_idle());
    assert(read_pico_transfer_forget_wifi()==ESP_ERR_INVALID_STATE);
    assert(native_preview_service_scenario("transfer-finished"));assert(read_pico_transfer_try_stop_if_idle());
    assert(read_pico_transfer_forget_wifi()==ESP_OK);
    assert(read_pico_transfer_get_saved_wifi(saved,&configured)==ESP_OK&&!configured);
    check_service_scope(false);check_service_scope(true);home_configured=false;
    native_preview_service_status_json(stdout);puts("\nNative preview services PASS: queued Wi-Fi, zero-profile first use, TF/USB recovery, hardware simulation and explicit Home networking opt-in; no host hardware access.");
    return 0;
}

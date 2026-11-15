/*
 * SPDX-License-Identifier: Apache-2.0
 * 本地USB维护命令，由主循环执行与触屏相同的受控流程。
 * Local USB maintenance commands, dispatched by the main loop through the touch UI lifecycle.
 * 冻结：仅可信有线会话可用版本化无线引导；身份匹配后才配网或发临时密钥，不提供任意路径/寄存器访问。
 * 用户修订：电脑可经USB首次配网并取得限时管理入口；清卡仍必须带完整明确确认词。
 * Frozen: Only trusted wired sessions use versioned wireless setup; identity matching precedes credentials or temporary keys, with no arbitrary path/register access.
 * User revision: USB may provision first-time Wi-Fi and obtain timed management; erasure still requires the complete confirmation command.
 * 冻结：TIME只输出系统与缓存RTC时间状态，不校时、不访问网络或输出秘密。
 * Frozen: TIME reports only system/cached RTC clock state; it never sets time, accesses networks or emits secrets.
 * 用户修订：画板分块经调试USB直接传入，停等确认和CRC校验完成前不写盘，不接管TF或改变阅读页面。
 * User revision: Stop-and-wait USB board chunks write only after complete CRC validation, without taking TF or changing the reading page.
 */
#include "pico_system.h"
#include "pico_connectivity.h"
#include "pico_datetime.h"
#include "pico_desktop.h"
#include "pico_updates.h"
#ifdef PICO_DUAL_READER
#include "pico_dual.h"
#endif
#include "read_pico_connectivity.h"
#include "read_pico_sd.h"
#include "read_pico_usb.h"
#include "read_pico_pmu.h"
#include "settings.h"
#include "sdkconfig.h"
#ifdef ESP_PLATFORM
#include "esp_heap_caps.h"
#endif
#if CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG
#include "driver/usb_serial_jtag.h"
#endif
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

// USB 日志可能留下无换行的 64 字节片段；每条回应先补帧边界。
// USB console logs may leave a 64-byte unterminated fragment; begin each reply on a new line.
static char pm_line[PS_WIRELESS_REQUEST_MAX+1];
static size_t pm_used;
static bool pm_ready,pm_overflow,pm_binary_skip_lf;
static uint8_t pm_binary[1024];
static size_t pm_binary_size,pm_binary_used;
static void pm_clear(void *data,size_t size){volatile unsigned char *p=data;while(size--)*p++=0;}

static void pm_time_status(void){
    const pmu_snapshot_t *rtc=read_pico_pmu_get();
    ps_datetime_status_t state;ps_datetime_get_status(&state);
    printf("\nRPICO {\"time_status\":true,\"system_utc\":%lld,\"rtc_ready\":%s,\"rtc_utc\":%u,\"rtc_ok\":%s,\"rtc_synced\":%s,\"source\":%u,\"ntp_state\":%u,\"ntp_pending\":%s,\"last_ntp_utc\":%u}\n",
        (long long)time(NULL),read_pico_pmu_ready()?"true":"false",rtc?(unsigned)rtc->unix_sec:0,
        rtc&&rtc->time_ok?"true":"false",rtc&&rtc->time_synced?"true":"false",
        state.source,state.ntp_state,state.ntp_pending?"true":"false",(unsigned)state.last_ntp_utc);
}
static void pm_status(void){
    char power[144];ps_desktop_power_status(power,sizeof(power));
    read_pico_sd_info_t sd={0};read_pico_sd_get_info(&sd);
    read_pico_usb_status_t usb={0};read_pico_usb_get_status(&usb);
    read_pico_connectivity_status_t wifi;read_pico_connectivity_get_status(&wifi);
    char update_latest[32], update_message[96];bool update_newer=false;unsigned update_apps=0;
    (void)ps_updates_status(update_latest,sizeof(update_latest),&update_newer,&update_apps,update_message,sizeof(update_message));
    unsigned books=0;bool loading=false,limited=false;ps_reader_library_status(&books,&loading,&limited);
    printf("\nRPICO {\"device\":\"read-pico\",\"version\":\"%s\",\"usb_held\":%s,\"usb_background_supported\":true,\"usb_background\":%s,\"usb_active\":%s,\"usb_connected\":%s,\"usb_host_ejected\":%s,\"usb_debug_restored\":%s,\"usb_error\":%d,\"route\":%d,\"storage_busy\":%s,\"present\":%s,\"mounted\":%s,\"state\":%d,\"stage\":%d,\"read_verified\":%s,\"write_verified\":%s,\"bytes\":%llu,\"free\":%llu,\"error\":%d,\"wifi_connected\":%s,\"transfer_active\":%s,\"transfer_url\":\"%s\",\"done\":%u,\"books_count\":%u,\"books_loading\":%s,\"books_limited\":%s,\"keep_awake\":%s,\"update_latest\":\"%s\",\"update_newer\":%s,\"update_apps\":%u,\"power\":%s}\n",
        ps_version(),ps_usb_held()?"true":"false",ps_usb_background()?"true":"false",usb.active?"true":"false",usb.connected?"true":"false",usb.host_ejected?"true":"false",usb.debug_restored?"true":"false",usb.last_error,
        ps_route(),ps_storage_busy()?"true":"false",sd.present?"true":"false",sd.mounted?"true":"false",sd.state,sd.stage,
        sd.read_verified?"true":"false",sd.write_verified?"true":"false",(unsigned long long)sd.capacity_bytes,
        (unsigned long long)sd.free_bytes,sd.error,wifi.connected?"true":"false",wifi.transfer_active?"true":"false",
        wifi.transfer.url,wifi.transfer.done_count,books,loading?"true":"false",limited?"true":"false",app_settings_test_awake()?"true":"false",
        update_latest,update_newer?"true":"false",update_apps,power);
}
// 只输出状态与错误阶段，不输出网络名称或凭据。/ Emit only state/error phases, never network names or credentials.
static void pm_wifi_status(void){
    read_pico_connectivity_status_t wifi;read_pico_connectivity_get_status(&wifi);
    printf("\nRPICO {\"wifi_status\":true,\"ready\":%s,\"enabled\":%s,\"state\":%d,\"busy\":%s,\"connected\":%s,\"connecting\":%s,\"scanning\":%s,\"network_count\":%u,\"saved_count\":%u,\"submitted_seq\":%u,\"completed_seq\":%u,\"result_seq\":%u,\"last_operation\":%d,\"last_error\":%d,\"scan_completed_seq\":%u,\"scan_error\":%d,\"scan_failure\":%d}\n",
        wifi.ready?"true":"false",wifi.wifi_enabled?"true":"false",wifi.state,wifi.busy?"true":"false",
        wifi.connected?"true":"false",wifi.connecting?"true":"false",wifi.scanning?"true":"false",
        (unsigned)wifi.network_count,(unsigned)wifi.saved_count,(unsigned)wifi.submitted_seq,(unsigned)wifi.completed_seq,
        (unsigned)wifi.result_seq,wifi.last_operation,wifi.last_error,(unsigned)wifi.scan_completed_seq,wifi.scan_error,wifi.scan_failure);
}
static void pm_update_status(void){
    char latest[32],message[96];bool newer=false,firmware_ready=ps_updates_firmware_ready();unsigned apps=0;
    (void)ps_updates_status(latest,sizeof(latest),&newer,&apps,message,sizeof(message));
    printf("\nRPICO {\"update_status\":true,\"latest\":\"%s\",\"newer\":%s,\"apps\":%u,\"firmware_ready\":%s,\"message\":\"%s\",\"online\":%s}\n",
        latest,newer?"true":"false",apps,firmware_ready?"true":"false",message,
        read_pico_connectivity_online()?"true":"false");
}
static void pm_command(void){
    bool accepted=false,background=false;
    if(!strncmp(pm_line,PS_WIRELESS_PREFIX,sizeof(PS_WIRELESS_PREFIX)-1)){
        char *out=malloc(PS_WIRELESS_RESPONSE_MAX);
        if(out){ps_connectivity_wireless_request(pm_line+sizeof(PS_WIRELESS_PREFIX)-1,out,PS_WIRELESS_RESPONSE_MAX);printf("\n%s%s\n",PS_WIRELESS_PREFIX,out);pm_clear(out,PS_WIRELESS_RESPONSE_MAX);free(out);}
        fflush(stdout);return;
    }
    if(!strcmp(pm_line,"RPICO STATUS")){pm_status();return;}
    if(!strcmp(pm_line,"RPICO TIME")){pm_time_status();return;}
    if(!strcmp(pm_line,"RPICO UPDATE STATUS")){pm_update_status();return;}
    if(!strcmp(pm_line,"RPICO UPDATE CHECK")){
        bool ok=ps_updates_check();
        printf("\nRPICO {\"accepted\":%s,\"update_check\":true}\n",ok?"true":"false");
        fflush(stdout);return;
    }
    if(!strcmp(pm_line,"RPICO UPDATE FIRMWARE")){
        bool ok=ps_updates_download_firmware();
        printf("\nRPICO {\"accepted\":%s,\"firmware_download\":true}\n",ok?"true":"false");
        fflush(stdout);return;
    }
    if(!strcmp(pm_line,"RPICO UPDATE INSTALL CONFIRM_REBOOT")){
        bool ok=ps_updates_arm_firmware();
        printf("\nRPICO {\"accepted\":%s,\"firmware_install\":true}\n",ok?"true":"false");
        fflush(stdout);return;
    }
#ifdef PICO_DUAL_READER
    if(!strncmp(pm_line,"RPICO DUAL ",11)){
        char out[1024];
        if(ps_dual_usb_command(pm_line,out,sizeof(out))){printf("\nRPICO %s\n",out);fflush(stdout);return;}
    }
#endif
    if(!strcmp(pm_line,"RPICO BOARD STATUS")){char out[512];ps_desktop_usb_status(out,sizeof(out));printf("\nRPICO %s\n",out);return;}
    if(!strncmp(pm_line,"RPICO BOARD ",12)){
        if(ps_storage_busy()||ps_usb_held()){printf("\nRPICO {\"board_direct\":true,\"accepted\":false,\"busy\":true}\n");return;}
        char out[256];size_t binary=0;
        if(ps_desktop_usb_direct_command(pm_line,out,sizeof(out),&binary)){
            if(binary<=sizeof(pm_binary)){pm_binary_size=binary;pm_binary_used=0;}
            if(*out)printf("\nRPICO %s\n",out);
            fflush(stdout);return;
        }
    }
#ifdef ESP_PLATFORM
    if(!strcmp(pm_line,"RPICO HEAP")){
        printf("\nRPICO {\"heap\":true,\"internal_free\":%u,\"internal_largest\":%u,\"dma_free\":%u,\"dma_largest\":%u,\"psram_free\":%u,\"psram_largest\":%u}\n",(unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),(unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL),(unsigned)heap_caps_get_free_size(MALLOC_CAP_DMA),(unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_DMA),(unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM),(unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM));return;
    }
#endif
    if(!strcmp(pm_line,"RPICO WIFI STATUS")){pm_wifi_status();return;}
    if(ps_storage_busy()||ps_usb_held()){printf("\nRPICO {\"accepted\":false,\"busy\":true}\n");return;}
    if(!strcmp(pm_line,"RPICO FORMAT TF CONFIRM_ERASE")){
        ps_navigate(PS_STORAGE);accepted=ps_route()==PS_STORAGE&&ps_storage_request_format();
    }else if(!strcmp(pm_line,"RPICO USB START BACKGROUND")){
        background=true;accepted=ps_usb_request_background();
    }else if(!strcmp(pm_line,"RPICO USB START")){
        ps_navigate(PS_USB_TRANSFER);accepted=ps_route()==PS_USB_TRANSFER&&ps_usb_request_start();
    }else if(!strcmp(pm_line,"RPICO WIFI AP")){
        ps_navigate(PS_TRANSFER);accepted=ps_route()==PS_TRANSFER&&ps_connectivity_request_transfer(READ_PICO_TRANSFER_MODE_AP);
    }else if(!strcmp(pm_line,"RPICO WIFI SCAN")){
        ps_navigate(PS_WIFI);accepted=ps_route()==PS_WIFI&&ps_connectivity_request_scan();
    }else if(!strcmp(pm_line,"RPICO TRANSFER STOP")){
        accepted=ps_connectivity_shutdown();
        if(accepted){read_pico_connectivity_resume();ps_navigate(PS_TRANSFER);}
    }
    // 交接PHY前先提交完整确认行；电脑收到确认后关闭串口并等待TF磁盘。
    // Flush the whole acknowledgment before PHY handoff; the desktop closes serial and waits for the TF volume.
    printf("\nRPICO {\"accepted\":%s%s}\n",accepted?"true":"false",background?(accepted?",\"usb_background\":true":",\"usb_background\":false"):"");fflush(stdout);ps_dirty();
}
void ps_maintenance_tick(void){
    ps_desktop_usb_direct_tick();
    if(pm_binary_size&&!ps_desktop_usb_direct_active()){pm_clear(pm_binary,sizeof(pm_binary));pm_binary_size=pm_binary_used=0;pm_binary_skip_lf=false;pm_clear(pm_line,sizeof(pm_line));pm_used=0;pm_overflow=false;}
    if(ps_usb_held()){pm_clear(pm_line,sizeof(pm_line));pm_used=0;pm_overflow=false;return;}
    ps_connectivity_wireless_tick();
    char input[1152];ssize_t n;
    size_t input_capacity=ps_desktop_usb_direct_active()?sizeof(input):96;
#if CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG
    /* Read the driver's IRQ-fed RX ring directly, without switching VFS stdout
     * to its queued/possibly waiting TX path. A zero timeout never waits for RX.
     * Install outside MSC only: installation sets the PHY defaults and must
     * never take it from OTG.
     * Keep this separate-controller driver alive while MSC owns the PHY; the
     * guard above forbids all reads/reconfiguration until USB safely restores it.
     */
    if(!usb_serial_jtag_is_driver_installed()){
        pm_ready=false;
        usb_serial_jtag_driver_config_t config=USB_SERIAL_JTAG_DRIVER_CONFIG_DEFAULT();
        config.rx_buffer_size=4096;
        if(usb_serial_jtag_driver_install(&config)!=ESP_OK)return;
    }
    pm_ready=true;
    n=usb_serial_jtag_read_bytes(input,input_capacity,0);
#else
    if(!pm_ready){
        int flags=fcntl(STDIN_FILENO,F_GETFL);
        if(flags<0||fcntl(STDIN_FILENO,F_SETFL,flags|O_NONBLOCK)<0)return;
        pm_ready=true;
    }
    n=read(STDIN_FILENO,input,input_capacity);
#endif
    for(ssize_t i=0;i<n;i++){
        unsigned char c=(unsigned char)input[i];
        // 中文：兼容CRLF控制行，末尾LF不能成为首个图像字节。/ English: A CRLF control-line terminator must never become the first image byte.
        if(pm_binary_skip_lf){pm_binary_skip_lf=false;if(c=='\n')continue;}
        if(pm_binary_size){
            pm_binary[pm_binary_used++]=c;
            if(pm_binary_used==pm_binary_size){
                char out[256];ps_desktop_usb_direct_payload(pm_binary,pm_binary_used,out,sizeof(out));
                pm_clear(pm_binary,sizeof(pm_binary));pm_binary_size=pm_binary_used=0;
                printf("\nRPICO %s\n",out);fflush(stdout);
            }
            continue;
        }
        if(c=='\n'||c=='\r'){
            if(pm_used&&!pm_overflow){pm_line[pm_used]=0;pm_command();}
            if(c=='\r'&&pm_binary_size)pm_binary_skip_lf=true;
            pm_clear(pm_line,sizeof(pm_line));pm_used=0;pm_overflow=false;continue;
        }
        if(c<32||c>126){pm_overflow=true;continue;}
        if(pm_used>=95&&strncmp(pm_line,PS_WIRELESS_PREFIX,sizeof(PS_WIRELESS_PREFIX)-1)){pm_overflow=true;continue;}
        if(pm_used+1>=sizeof(pm_line)){pm_overflow=true;continue;}
        pm_line[pm_used++]=(char)c;
    }
    pm_clear(input,sizeof(input));
}

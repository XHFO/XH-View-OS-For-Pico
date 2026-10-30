/*
 * SPDX-License-Identifier: Apache-2.0
 * 实际维护命令解析器的管道测试；仅替换硬件与页面生命周期，不读取真实终端。
 * Pipe tests for the actual maintenance parser; only hardware and page lifecycles are doubled, never a real terminal.
 */
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>
#include "pico_connectivity.h"
#include "pico_system.h"
#include "pico_datetime.h"
#include "read_pico_pmu.h"
#include "read_pico_connectivity.h"
#include "read_pico_sd.h"
#include "read_pico_usb.h"
#include "driver/usb_serial_jtag.h"

void ps_desktop_power_status(char *out,size_t n){snprintf(out,n,"{\"known\":true,\"charging\":true,\"external_power\":true,\"battery_percent\":86,\"revision\":7}");}
void ps_desktop_usb_status(char *out,size_t n){snprintf(out,n,"{\"usb_board\":true,\"protocol\":1,\"gray_levels\":16,\"max_bytes\":420000,\"task\":\"\",\"state\":\"idle\"}");}
static unsigned checks, scenarios;
#define CHECK(x)                                                         \
    do {                                                                 \
        ++checks;                                                        \
        if (!(x)) {                                                      \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #x); \
            exit(1);                                                     \
        }                                                                \
    } while (0)
static bool test_direct_active;
static unsigned direct_commands,direct_payloads;
static uint8_t direct_payload[1024];
void ps_desktop_usb_direct_tick(void){}
bool ps_desktop_usb_direct_active(void){return test_direct_active;}
bool ps_desktop_usb_direct_command(const char *line,char *out,size_t capacity,size_t *binary){
    direct_commands++;*binary=0;out[0]=0;
    if(!strcmp(line,"RPICO BOARD BEGIN fixture")){test_direct_active=true;snprintf(out,capacity,"{\"board_direct\":true,\"accepted\":true}");return true;}
    if(!strcmp(line,"RPICO BOARD CHUNK fixture")){*binary=32;return true;}
    snprintf(out,capacity,"{\"board_direct\":true,\"accepted\":false}");return true;
}
void ps_desktop_usb_direct_payload(const void *data,size_t bytes,char *out,size_t capacity){
    CHECK(bytes==32);memcpy(direct_payload,data,bytes);direct_payloads++;snprintf(out,capacity,"{\"board_direct\":true,\"accepted\":true}");
}
static bool storage_busy, usb_held, route_allowed, request_success, hold_on_usb,
    invalidate_input_on_tick;
static bool test_keep_awake,background_mode;
static unsigned background_calls;
bool ps_usb_background(void){return background_mode;}
bool ps_usb_request_background(void){background_calls++;background_mode=request_success;if(request_success&&hold_on_usb)usb_held=true;return request_success;}
bool app_settings_test_awake(void){return test_keep_awake;}
static ps_route_t route_now, route_requests[16];
static unsigned route_count, format_calls, usb_calls, ap_calls, stop_calls, resume_calls,
    dirty_calls, sd_calls, wifi_calls, scan_calls;
static bool usj_installed,usj_install_fail;
static unsigned usj_install_calls,usj_rx_calls,usj_probe_calls;
static int input_write = -1, original_input = -1;
static char response[16384];
static unsigned wireless_calls,wireless_ticks;
static char wireless_request[513];
void ps_connectivity_wireless_request(const char *request,char *out,size_t capacity){wireless_calls++;snprintf(wireless_request,sizeof(wireless_request),"%s",request);snprintf(out,capacity,"{\"v\":1,\"id\":7,\"device\":\"read-pico\",\"usb_serial\":\"020000000001\",\"ok\":true,\"status\":200,\"op\":\"status\",\"phase\":\"done\"}");}
void ps_connectivity_wireless_tick(void){wireless_ticks++;}
static read_pico_sd_info_t sd_status;
static read_pico_usb_status_t usb_status;
const char *ps_version(void){return "maintenance-test";}
void read_pico_usb_get_status(read_pico_usb_status_t *out){*out=usb_status;}
static read_pico_connectivity_status_t wifi_status;
static bool rtc_ready,rtc_null;
static pmu_snapshot_t rtc_snapshot;
static ps_datetime_status_t clock_status;
bool read_pico_pmu_ready(void){return rtc_ready;}
const pmu_snapshot_t *read_pico_pmu_get(void){return rtc_null?NULL:&rtc_snapshot;}
void ps_datetime_get_status(ps_datetime_status_t *out){*out=clock_status;}

ps_route_t ps_route(void) {
    return route_now;
}
void ps_reader_library_status(unsigned *count,bool *loading,bool *limited){
    if(count)*count=17;if(loading)*loading=false;if(limited)*limited=true;
}
bool ps_storage_busy(void) {
    return storage_busy;
}
bool ps_usb_held(void) {
    return usb_held;
}
void ps_navigate(ps_route_t route) {
    CHECK(route_count < 16);
    route_requests[route_count++] = route;
    if (route_allowed) route_now = route;
}
bool ps_storage_request_format(void) {
    format_calls++;
    CHECK(route_now == PS_STORAGE);
    return request_success;
}
bool ps_usb_request_start(void) {
    usb_calls++;
    CHECK(route_now == PS_USB_TRANSFER);
    if (request_success && hold_on_usb) usb_held = true;
    return request_success;
}
bool ps_connectivity_request_transfer(read_pico_transfer_mode_t mode) {
    CHECK(mode == READ_PICO_TRANSFER_MODE_AP && route_now == PS_TRANSFER);
    ap_calls++;
    return request_success;
}
bool ps_connectivity_request_scan(void) {
    CHECK(route_now == PS_WIFI);
    scan_calls++;
    return request_success;
}
bool ps_connectivity_shutdown(void) {
    stop_calls++;
    return request_success;
}
esp_err_t read_pico_connectivity_resume(void) {
    resume_calls++;
    return ESP_OK;
}
void ps_dirty(void) {
    dirty_calls++;
}
esp_err_t read_pico_sd_get_info(read_pico_sd_info_t* out) {
    sd_calls++;
    *out = sd_status;
    return sd_status.error;
}
void read_pico_connectivity_get_status(read_pico_connectivity_status_t* out) {
    wifi_calls++;
    *out = wifi_status;
}
bool usb_serial_jtag_is_driver_installed(void){CHECK(!usb_held);usj_probe_calls++;return usj_installed;}
esp_err_t usb_serial_jtag_driver_install(usb_serial_jtag_driver_config_t *config){
    CHECK(!usb_held&&!usj_installed&&config->rx_buffer_size>64&&config->tx_buffer_size>0);
    usj_install_calls++;if(usj_install_fail)return ESP_FAIL;usj_installed=true;return ESP_OK;
}
int usb_serial_jtag_read_bytes(void *buffer,uint32_t length,uint32_t ticks_to_wait){
    /* Only the hardware RX boundary maps to a host pipe. Production never
     * switches the logging VFS or waits for a byte. */
    CHECK(!usb_held&&usj_installed&&ticks_to_wait==0&&length==(test_direct_active?1152u:96u));
    usj_rx_calls++;ssize_t n=read(STDIN_FILENO,buffer,length);return n>0?(int)n:0;
}
#include "../main/system/pico_maintenance.c"

static int pending_bytes(void) {
    int bytes = -1;
    CHECK(!ioctl(STDIN_FILENO, FIONREAD, &bytes));
    return bytes;
}
static void capture_tick(void) {
    int output[2];
    CHECK(!pipe(output));
    fflush(stdout);
    int saved = dup(STDOUT_FILENO);
    CHECK(saved >= 0);
    CHECK(dup2(output[1], STDOUT_FILENO) >= 0);
    close(output[1]);
    if (invalidate_input_on_tick) close(STDIN_FILENO);
    ps_maintenance_tick();
    fflush(stdout);
    CHECK(dup2(saved, STDOUT_FILENO) >= 0);
    close(saved);
    size_t used = 0;
    ssize_t count;
    while ((count = read(output[0], response + used, sizeof(response) - 1 - used)) > 0) {
        used += (size_t)count;
        CHECK(used + 1 < sizeof(response));
    }
    CHECK(count == 0);
    response[used] = 0;
    close(output[0]);
}
static void feed(const void* bytes, size_t length) {
    CHECK(write(input_write, bytes, length) == (ssize_t)length);
    capture_tick();
}
static void line(const char* text) {
    feed(text, strlen(text));
}
static void fixture(void) {
    ++scenarios;
    test_direct_active=false;direct_commands=direct_payloads=0;pm_binary_size=pm_binary_used=0;pm_binary_skip_lf=false;
    if (input_write >= 0) close(input_write);
    int input[2];
    CHECK(!pipe(input));
    CHECK(dup2(input[0], STDIN_FILENO) >= 0);
    if(input[0]!=STDIN_FILENO)close(input[0]);
    int pipe_flags=fcntl(STDIN_FILENO,F_GETFL);
    CHECK(pipe_flags>=0&&fcntl(STDIN_FILENO,F_SETFL,pipe_flags|O_NONBLOCK)==0);
    input_write = input[1];
    storage_busy = usb_held = hold_on_usb = false;
    test_keep_awake = background_mode = false;background_calls=0;
    route_allowed = request_success = true;
    route_now = PS_READ;
    invalidate_input_on_tick = false;
    route_count = format_calls = usb_calls = ap_calls = stop_calls = resume_calls = dirty_calls =
        sd_calls = wifi_calls = scan_calls = 0;
    pm_used = 0;
    pm_ready = pm_overflow = false;
    usj_installed=usj_install_fail=false;
    usj_install_calls=usj_rx_calls=usj_probe_calls=0;
    wireless_calls=wireless_ticks=0;wireless_request[0]=0;
    memset(pm_line, 0, sizeof(pm_line));
    response[0] = 0;
    rtc_ready=true;rtc_null=false;
    rtc_snapshot=(pmu_snapshot_t){.unix_sec=1792382400u,.time_ok=true,.time_synced=1};
    clock_status=(ps_datetime_status_t){.source=PS_TIME_NETWORK,.ntp_state=PS_NTP_SYNCED,.last_ntp_utc=1792382400u};
    sd_status = (read_pico_sd_info_t){ .present = true,
                                       .mounted = true,
                                       .capacity_bytes = 64000000000ULL,
                                       .free_bytes = 63000000000ULL,
                                       .state = READ_PICO_SD_STATE_READY,
                                       .stage = READ_PICO_SD_STAGE_COMPLETE,
                                       .read_verified = true,
                                       .write_verified = true };
    usb_status=(read_pico_usb_status_t){.debug_restored=true};
    memset(&wifi_status, 0, sizeof(wifi_status));
    wifi_status.connected = true;
    wifi_status.transfer_active = true;
    wifi_status.transfer.done_count = 7;
    strcpy(wifi_status.transfer.url, "http://192.168.4.1");
    strcpy(wifi_status.ssid, "PRIVATE_SSID_DO_NOT_PRINT");
    strcpy(wifi_status.transfer.ssid, "PRIVATE_AP_DO_NOT_PRINT");
    strcpy(wifi_status.transfer.wifi_ssid, "PRIVATE_SAVED_DO_NOT_PRINT");
    strcpy(wifi_status.transfer.cur_name, "PRIVATE_FILE_DO_NOT_PRINT");
}
static void no_effects(void) {
    CHECK(!background_calls && !route_count && !format_calls && !usb_calls && !ap_calls && !stop_calls && !resume_calls && !scan_calls);
}
static void accepted(bool value) {
    CHECK(
        !strcmp(response, value ? "\nRPICO {\"accepted\":true}\n" : "\nRPICO {\"accepted\":false}\n"));
}
static void wireless_framing(void){
    const char *frame="PICO_WIFI1 {\"v\":1,\"id\":7,\"op\":\"status\"}\n";
    for(size_t split=0;split<strlen(frame);split++){
        fixture();feed(frame,split);CHECK(!response[0]&&!wireless_calls);no_effects();
        feed(frame+split,strlen(frame)-split);
        CHECK(wireless_calls==1&&!strcmp(wireless_request,"{\"v\":1,\"id\":7,\"op\":\"status\"}"));
        CHECK(!strncmp(response,"\nPICO_WIFI1 {",13)&&strstr(response,"\"id\":7")&&!dirty_calls);no_effects();
        for(size_t i=0;i<sizeof(pm_line);i++)CHECK(!pm_line[i]);
    }
    char large[515];
    for(size_t length=511;length<=513;length++){
        fixture();memset(large,' ',length);memcpy(large,"PICO_WIFI1 {}",13);large[length]='\n';
        feed(large,length+1);while(pending_bytes())capture_tick();
        CHECK(wireless_calls==(length<=512));CHECK((response[0]!=0)==(length<=512));no_effects();
    }
    fixture();line("PICO_WIFI1 {\"v\":1");CHECK(pm_used&&!wireless_calls);
    unsigned prior_ticks=wireless_ticks;usb_held=true;capture_tick();CHECK(!pm_used&&wireless_ticks==prior_ticks);for(size_t i=0;i<sizeof(pm_line);i++)CHECK(!pm_line[i]);
    usb_held=false;line(",\"id\":7,\"op\":\"status\"}\n");CHECK(!wireless_calls);accepted(false);no_effects();
}
static const char* commands[] = { "RPICO FORMAT TF CONFIRM_ERASE\n", "RPICO USB START\n",
                                  "RPICO WIFI AP\n", "RPICO WIFI SCAN\n", "RPICO TRANSFER STOP\n" };
static const ps_route_t targets[] = { PS_STORAGE, PS_USB_TRANSFER, PS_TRANSFER, PS_WIFI,
                                      PS_TRANSFER };
static void dispatch(void) {
    for (unsigned n = 0; n < 5; n++) {
        fixture();
        line(commands[n]);
        accepted(true);
        CHECK(
            route_count == 1 && route_requests[0] == targets[n] && route_now == targets[n] &&
            dirty_calls == 1);
        CHECK(
            format_calls == (n == 0) && usb_calls == (n == 1) && ap_calls == (n == 2) &&
            stop_calls == (n == 4) && resume_calls == (n == 4) && scan_calls == (n == 3));
        fixture();
        request_success = false;
        line(commands[n]);
        accepted(false);
        CHECK(resume_calls == 0);
        fixture();
        route_allowed = false;
        line(commands[n]);
        accepted(n == 4);
        CHECK(!format_calls && !usb_calls && !ap_calls && !scan_calls);
        CHECK(route_now == PS_READ);
    }
    fixture();
    line("RPICO WIFI SCAN\r\nRPICO WIFI AP\r\n");
    CHECK(
        route_count == 2 && route_requests[0] == PS_WIFI && route_requests[1] == PS_TRANSFER &&
        ap_calls == 1 && dirty_calls == 2);
    CHECK(!strcmp(response, "\nRPICO {\"accepted\":true}\n\nRPICO {\"accepted\":true}\n"));
}
static void background_start(void){
    const char *command="RPICO USB START BACKGROUND\n";
    for(size_t split=0;split<strlen(command);split++){
        fixture();feed(command,split);CHECK(!response[0]);no_effects();
        feed(command+split,strlen(command)-split);
        CHECK(!strcmp(response,"\nRPICO {\"accepted\":true,\"usb_background\":true}\n"));
        CHECK(background_calls==1&&background_mode&&route_now==PS_READ&&!route_count&&!usb_calls&&dirty_calls==1);
    }
    fixture();request_success=false;line(command);CHECK(!strcmp(response,"\nRPICO {\"accepted\":false,\"usb_background\":false}\n"));CHECK(!background_mode&&!route_count&&background_calls==1);
    fixture();storage_busy=true;line(command);CHECK(strstr(response,"\"busy\":true"));no_effects();
    fixture();hold_on_usb=true;line("RPICO USB START BACKGROUND\nRPICO WIFI SCAN\n");CHECK(usb_held&&background_mode&&background_calls==1&&!route_count&&!scan_calls);CHECK(strstr(response,"\"busy\":true"));
    const char *invalid[]={"RPICO USB START BACKGROUND extra\n","RPICO USB START BACKGROUN\n","RPICO USB START BACKGROUND \n"};
    for(unsigned i=0;i<sizeof(invalid)/sizeof(*invalid);i++){fixture();line(invalid[i]);accepted(false);no_effects();}
}
static void framing(void) {
    for (unsigned c = 0; c < 5; c++)
        for (size_t split = 0; split < strlen(commands[c]); split++) {
            fixture();
            feed(commands[c], split);
            CHECK(!response[0]);
            no_effects();
            feed(commands[c] + split, strlen(commands[c]) - split);
            accepted(true);
        }
    fixture();
    line("\r\n\n\r");
    CHECK(!response[0]);
    no_effects();
    fixture();
    line("RPICO FORMAT TF CONFIRM_ERASE");
    CHECK(!response[0]);
    no_effects();
    line("\r");
    accepted(true);
    CHECK(format_calls == 1);
    line("\n");
    CHECK(!response[0] && format_calls == 1);
    const char* rejected[] = { "RPICO FORMAT TF\n",
                               "RPICO FORMAT TF CONFIRM\n",
                               "RPICO FORMAT TF CONFIRM_ERASE extra\n",
                               " RPICO FORMAT TF CONFIRM_ERASE\n",
                               "RPICO FORMAT TF CONFIRM_ERASE \n",
                               "rpico FORMAT TF CONFIRM_ERASE\n",
                               "RPICO FORMAT TF CONFIRM_ERAS\n",
                               "RPICO FORMAT TF CONFIRM_ERASEE\n",
                               "RPICO FORMAT TF CONFIRM_ERASE;RPICO STATUS\n",
                               "RPICO\n" };
    for (unsigned n = 0; n < sizeof(rejected) / sizeof(rejected[0]); n++) {
        fixture();
        line(rejected[n]);
        accepted(false);
        no_effects();
    }
}
static void poisoned_lines(void) {
    char input[400];
    for (size_t size = 94; size <= 97; size++) {
        fixture();
        memset(input, 'X', size);
        input[size] = '\n';
        feed(input, size + 1);
        while (pending_bytes()) capture_tick();
        no_effects();
        CHECK(dirty_calls == (size <= 95));
        line("RPICO WIFI SCAN\n");
        accepted(true);
    }
    fixture();
    memset(input, 'X', 240);
    memcpy(input + 240, "\nRPICO WIFI SCAN\n", 17);
    feed(input, 257);
    CHECK(!response[0] && !dirty_calls);
    capture_tick();
    CHECK(!response[0] && !dirty_calls);
    capture_tick();
    accepted(true);
    CHECK(route_count == 1);
    const unsigned char poison[] = { 0, 1, 8, 9, 11, 12, 27, 31, 127, 128, 0xc3, 0xe4, 0xff };
    for (unsigned n = 0; n < sizeof(poison); n++) {
        fixture();
        const char* command = "RPICO FORMAT TF CONFIRM_ERASE";
        size_t length = strlen(command);
        memcpy(input, command, length);
        input[length] = (char)poison[n];
        input[length + 1] = '\n';
        feed(input, length + 2);
        CHECK(!response[0] && !dirty_calls);
        no_effects();
        line("RPICO STATUS\n");
        CHECK(strstr(response, "\"mounted\":true"));
        CHECK(!format_calls);
    }
}
static void busy_and_usb(void) {
    for (unsigned n = 0; n < 5; n++) {
        fixture();
        storage_busy = true;
        line(commands[n]);
        CHECK(!strcmp(response, "\nRPICO {\"accepted\":false,\"busy\":true}\n"));
        no_effects();
        CHECK(!dirty_calls);
        fixture();
        usb_held = true;
        line(commands[n]);
        CHECK(!response[0] && !pm_ready && pending_bytes() == (int)strlen(commands[n]));
        no_effects();
        usb_held = false;
        capture_tick();
        accepted(true);
    }
    fixture();
    line("RPICO FORMAT TF CONFIRM_");
    no_effects();
    usb_held = true;
    line("ERASE\n");
    CHECK(!response[0] && pending_bytes() == 6);
    no_effects();
    usb_held = false;
    capture_tick();
    accepted(false);
    CHECK(format_calls == 0);
    fixture();
    hold_on_usb = true;
    line("RPICO USB START\nRPICO FORMAT TF CONFIRM_ERASE\n");
    CHECK(usb_calls == 1 && !format_calls && usb_held && route_count == 1);
    CHECK(strstr(response, "\"accepted\":true") && strstr(response, "\"busy\":true"));
    int prior = pending_bytes();
    line("RPICO STATUS\n");
    CHECK(!response[0] && pending_bytes() == prior + 13);
}
static void status_and_nonblocking(void) {
    fixture();
    line("RPICO STATUS\n");
    CHECK(strstr(response,"\"keep_awake\":false")&&!dirty_calls);
    no_effects();
    fixture();
    test_keep_awake = true;
    storage_busy = true;
    line("RPICO STATUS\n");
    CHECK(sd_calls == 1 && wifi_calls == 1 && !dirty_calls);
    no_effects();
    CHECK(
        strstr(response, "\"storage_busy\":true") && strstr(response, "\"route\":1") &&
        strstr(response, "\"read_verified\":true") && strstr(response, "\"done\":7"));
    CHECK(strstr(response,"\"keep_awake\":true"));
    CHECK(
        !strstr(response, "PRIVATE_") && !strstr(response, "password") &&
        !strstr(response, "token") && !strstr(response, "credential"));
    fputs(response, stdout);
    fixture();
    storage_busy=true;
    wifi_status.ready=true;wifi_status.wifi_enabled=true;wifi_status.scanning=true;
    wifi_status.network_count=4;wifi_status.saved_count=2;
    wifi_status.result_seq=9;wifi_status.scan_completed_seq=3;
    wifi_status.scan_error=0x3006;wifi_status.scan_failure=READ_PICO_CONNECTIVITY_SCAN_START;
    wifi_status.last_operation=READ_PICO_CONNECTIVITY_OP_SCAN;
    line("RPICO WIFI STATUS\n");
    CHECK(wifi_calls==1&&!sd_calls&&!dirty_calls);
    CHECK(strstr(response,"\"wifi_status\":true")&&strstr(response,"\"network_count\":4")&&
          strstr(response,"\"scanning\":true")&&strstr(response,"\"scan_error\":12294")&&
          strstr(response,"\"result_seq\":9")&&strstr(response,"\"scan_completed_seq\":3"));
    CHECK(!strstr(response,"PRIVATE_")&&!strstr(response,"ssid")&&!strstr(response,"password"));
    no_effects();fputs(response,stdout);
    fixture();storage_busy=true;line("RPICO TIME\n");
    CHECK(strstr(response,"\"time_status\":true")&&strstr(response,"\"rtc_utc\":1792382400")&&strstr(response,"\"source\":3")&&strstr(response,"\"ntp_state\":4"));
    CHECK(!sd_calls&&!wifi_calls&&!dirty_calls);no_effects();fputs(response,stdout);
    fixture();rtc_ready=false;rtc_null=true;clock_status=(ps_datetime_status_t){0};line("RPICO TIME\n");
    CHECK(strstr(response,"\"rtc_ready\":false")&&strstr(response,"\"rtc_utc\":0")&&strstr(response,"\"rtc_ok\":false")&&strstr(response,"\"rtc_synced\":false"));
    CHECK(!sd_calls&&!wifi_calls&&!dirty_calls);no_effects();
    fixture();rtc_snapshot.time_ok=false;rtc_snapshot.time_synced=0;clock_status.ntp_pending=true;clock_status.ntp_state=PS_NTP_SAVING;line("RPICO TIME\n");
    CHECK(strstr(response,"\"rtc_ok\":false")&&strstr(response,"\"rtc_synced\":false")&&strstr(response,"\"ntp_pending\":true")&&strstr(response,"\"ntp_state\":3"));
    CHECK(!sd_calls&&!wifi_calls&&!dirty_calls);no_effects();
    fixture();
    capture_tick();
    CHECK(pm_ready && (fcntl(STDIN_FILENO, F_GETFL) & O_NONBLOCK) && !response[0]);
    struct timespec start, end;
    CHECK(!clock_gettime(CLOCK_MONOTONIC, &start));
    for (int n = 0; n < 1000; n++) ps_maintenance_tick();
    CHECK(!clock_gettime(CLOCK_MONOTONIC, &end));
    CHECK((end.tv_sec - start.tv_sec) * 1000000000LL + end.tv_nsec - start.tv_nsec < 1000000000LL);
    no_effects();
    close(input_write);
    input_write = -1;
    capture_tick();
    CHECK(!response[0]);
    no_effects();
    fixture();
    invalidate_input_on_tick = true;
    capture_tick();
    CHECK(pm_ready && !response[0]);
    no_effects();
}
static void usj_driver_lifetime(void){
    fixture();usj_install_fail=true;line("RPICO STATUS\n");
    CHECK(!response[0]&&!pm_ready&&!sd_calls&&!wifi_calls);
    CHECK(usj_install_calls==1&&!usj_rx_calls&&!usj_installed&&pending_bytes()==13);
    usj_install_fail=false;capture_tick();
    CHECK(strstr(response,"\"mounted\":true")&&pm_ready&&usj_installed);
    CHECK(usj_install_calls==2&&usj_rx_calls==1&&!pending_bytes());
    capture_tick();CHECK(usj_install_calls==2&&usj_rx_calls==2&&!response[0]);
    fixture();usj_installed=true;line("RPICO WIFI SCAN\n");accepted(true);
    CHECK(!usj_install_calls&&usj_rx_calls==1&&pm_ready);
    fixture();usb_held=true;line("RPICO STATUS\n");
    CHECK(!usj_probe_calls&&!usj_install_calls&&!usj_rx_calls&&!pm_ready&&pending_bytes()==13);
    usb_held=false;capture_tick();CHECK(usj_install_calls==1&&usj_rx_calls==1&&sd_calls==1);
    unsigned probes=usj_probe_calls;usb_held=true;
    /* MSC temporarily owns the PHY. Maintenance must not inspect/reconfigure
     * the driver or consume buffered commands while it is held. */
    line("RPICO STATUS\n");CHECK(!response[0]&&usj_probe_calls==probes&&usj_install_calls==1&&usj_rx_calls==1&&pending_bytes()==13);
    usb_held=false;capture_tick();CHECK(usj_install_calls==1&&usj_rx_calls==2&&sd_calls==2);
    /* A driver explicitly removed elsewhere can be recreated after the MSC
     * handoff returns; failed setup keeps stdin untouched until a retry. */
    usj_installed=false;usj_install_fail=true;line("RPICO STATUS\n");
    CHECK(!response[0]&&!pm_ready&&pending_bytes()==13&&usj_install_calls==2);
    usb_held=true;probes=usj_probe_calls;capture_tick();CHECK(usj_probe_calls==probes&&usj_install_calls==2);
    usb_held=false;usj_install_fail=false;capture_tick();
    CHECK(usj_install_calls==3&&usj_rx_calls==3&&pm_ready&&sd_calls==3);
}
// 导出真实回包字节，Python 再模拟 USB 的 CRLF 和残缺日志前缀。
// Export actual reply bytes; Python simulates USB CRLF and the truncated log prefix.
static void wire_fixture(const char *name) {
    CHECK(!strncmp(response, "\nRPICO ", 7)||!strncmp(response,"\nPICO_WIFI1 ",12));
    CHECK(response[strlen(response) - 1] == '\n');
    printf("WIRE %s ", name);
    for (const unsigned char *p = (const unsigned char *)response; *p; p++) printf("%02x", *p);
    putchar('\n');
}
static void response_boundaries(void) {
    fixture();line("PICO_WIFI1 {\"v\":1,\"id\":7,\"op\":\"status\"}\n");no_effects();wire_fixture("wireless");
    fixture();line("RPICO STATUS\n");no_effects();wire_fixture("status");
    fixture();line("RPICO WIFI STATUS\n");no_effects();wire_fixture("wifi");
    fixture();line("RPICO TIME\n");no_effects();wire_fixture("time");
    fixture();storage_busy=true;line("RPICO BOARD STATUS\n");no_effects();
    CHECK(strstr(response,"\"usb_board\":true")&&strstr(response,"\"state\":\"idle\"")&&!sd_calls&&!wifi_calls&&!dirty_calls);wire_fixture("board");
    const char *board="RPICO BOARD STATUS\n";
    for(size_t split=0;split<strlen(board);split++){
        fixture();feed(board,split);CHECK(!response[0]);no_effects();
        feed(board+split,strlen(board)-split);CHECK(strstr(response,"\"usb_board\":true")&&!dirty_calls);no_effects();
    }
    fixture();line("RPICO WIFI SCAN\n");accepted(true);wire_fixture("accepted");
    fixture();line("RPICO USB START BACKGROUND\n");wire_fixture("background");
    fixture();line("RPICO UNKNOWN\n");accepted(false);no_effects();wire_fixture("rejected");
    fixture();storage_busy=true;line("RPICO WIFI SCAN\n");no_effects();wire_fixture("busy");
}

static void binary_board_framing(void){
    const uint8_t payload[32]="\nRPICO FORMAT TF CONFIRM_ERASE\n";
    for(unsigned split=0;split<=32;split++){
        fixture();line("RPICO BOARD BEGIN fixture\n");CHECK(test_direct_active&&direct_commands==1&&!direct_payloads);no_effects();
        line("RPICO BOARD CHUNK fixture\n");CHECK(pm_binary_size==32&&!response[0]);
        feed(payload,split);CHECK(direct_payloads==(split==32));no_effects();
        if(split<32)feed(payload+split,32-split);
        CHECK(direct_payloads==1&&!memcmp(payload,direct_payload,32)&&!pm_binary_size);no_effects();
        line("RPICO STATUS\n");CHECK(strstr(response,"\"mounted\":true"));
    }
    fixture();line("RPICO BOARD BEGIN fixture\nRPICO BOARD CHUNK fixture\n");CHECK(pm_binary_size==32);feed(payload,4);CHECK(pm_binary_used==4);test_direct_active=false;capture_tick();CHECK(!pm_binary_size&&!pm_binary_used);no_effects();
    for(unsigned split=0;split<=2;split++){
        const char terminator[]="\r\n";
        fixture();line("RPICO BOARD BEGIN fixture\r\nRPICO BOARD CHUNK fixture");feed(terminator,split);feed(terminator+split,2-split);
        CHECK(pm_binary_size==32&&!pm_binary_used&&!pm_binary_skip_lf);feed(payload,32);CHECK(direct_payloads==1&&!memcmp(payload,direct_payload,32));no_effects();
    }
}

int main(void) {
    original_input = dup(STDIN_FILENO);
    CHECK(original_input >= 0);
    binary_board_framing();
    dispatch();
    background_start();
    framing();
    poisoned_lines();
    busy_and_usb();
    wireless_framing();
    status_and_nonblocking();
    usj_driver_lifetime();
    response_boundaries();
    CHECK(dup2(original_input, STDIN_FILENO) >= 0);
    close(original_input);
    if (input_write >= 0) close(input_write);
    printf(
        "PASS maintenance: %u checks, %u scenarios; real parser with pipe stdin, no serial or "
        "hardware I/O\n",
        checks, scenarios);
    return 0;
}

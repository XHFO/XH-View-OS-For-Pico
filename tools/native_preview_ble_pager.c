/* SPDX-License-Identifier: Apache-2.0
 * 网页BLE翻页器模拟，绝不访问主机蓝牙。/ Simulated BLE pager; never accesses host Bluetooth.
 */
#include "read_pico_ble_pager.h"
#include "pico_system.h"
#include <stdio.h>
#include <string.h>

static read_pico_ble_pager_status_t status;
static read_pico_ble_pager_device_t device;
static bool initialized;
static bool service_started;
static int pending;
static int64_t due, now;
static struct {uint8_t version, enabled, remembered, reversed;} saved;
static void change(void) { status.revision++; }
static void save(void) {
    saved.version=1; saved.enabled=status.enabled; saved.remembered=status.remembered; saved.reversed=status.reversed;
    (void)ps_store("ble-preview-v1", &saved, sizeof(saved));
}
esp_err_t read_pico_ble_pager_init(void) {
    if(initialized) return ESP_OK;
    initialized=true; status.simulated=true; status.battery_percent=255;
    if(ps_load("ble-preview-v1", &saved, sizeof(saved)) && saved.version==1) {
        status.enabled=!!saved.enabled; status.ready=status.enabled;service_started=status.enabled;
        status.remembered=!!saved.remembered; status.reversed=!!saved.reversed;
        status.connected=status.enabled&&status.remembered;
        if(status.remembered) snprintf(status.name,sizeof(status.name),"示例 BLE 翻页器");
    }
    change(); return ESP_OK;
}
esp_err_t read_pico_ble_pager_set_enabled(bool enabled) {
    read_pico_ble_pager_init(); pending=0; due=0;
    service_started=enabled;
    status.enabled=status.ready=enabled; status.scanning=status.connecting=false;
    status.connected=enabled&&status.remembered; status.error=0;
    if(!enabled) status.device_count=0;
    save(); change(); return ESP_OK;
}
esp_err_t read_pico_ble_pager_scan(void) {
    if(!status.enabled||!status.ready||status.connecting) return ESP_ERR_INVALID_STATE;
    status.scanning=true; status.device_count=0; status.error=0; due=now+800; change(); return ESP_OK;
}
esp_err_t read_pico_ble_pager_connect(size_t index) {
    if(!status.enabled||index>=status.device_count||status.connecting) return ESP_ERR_INVALID_ARG;
    status.scanning=false; status.connected=false; status.connecting=true; pending=0; due=now+500;
    snprintf(status.name,sizeof(status.name),"%s",device.name); change(); return ESP_OK;
}
esp_err_t read_pico_ble_pager_disconnect(void) {
    status.connected=status.connecting=false; pending=0; due=0; change(); return ESP_OK;
}
esp_err_t read_pico_ble_pager_forget(void) {
    read_pico_ble_pager_disconnect(); status.remembered=false; status.name[0]=0; save(); change(); return ESP_OK;
}
esp_err_t read_pico_ble_pager_set_reversed(bool reversed) {
    status.reversed=reversed; pending=0; save(); change(); return ESP_OK;
}
void read_pico_ble_pager_get_status(read_pico_ble_pager_status_t *out) { read_pico_ble_pager_init(); if(out)*out=status; }
size_t read_pico_ble_pager_get_devices(read_pico_ble_pager_device_t *out,size_t capacity) {
    if(!out||!capacity||!status.device_count) return 0; *out=device; return 1;
}
int read_pico_ble_pager_poll_event(void) { int value=pending; pending=0; return value; }
bool read_pico_ble_pager_light_sleep_safe(void) { return !service_started&&!status.enabled; }
void native_preview_ble_tick(int64_t ms) {
    now=ms;
    if(!due||ms<due) return; due=0;
    if(status.scanning) {
        status.scanning=false; status.device_count=1; device.rssi=-48;
        snprintf(device.name,sizeof(device.name),"示例 BLE 翻页器");
    } else if(status.connecting) { status.connecting=false; status.connected=status.remembered=true; save(); }
    change();
}
bool native_preview_ble_press(int direction) {
    if(!status.connected||(direction!=-1&&direction!=1)) return false;
    pending=status.reversed?-direction:direction; return true;
}

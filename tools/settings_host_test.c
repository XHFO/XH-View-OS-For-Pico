/* SPDX-License-Identifier: Apache-2.0
 * 中文：调用真实设置API；设备NVS替身仅替换存储边界，原生分支使用真实私有文件NVS。
 * English: Call actual settings APIs; device doubles replace storage only, while the native branch uses actual private file NVS.
 */
#include "settings.h"
#include "nvs.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#ifdef SETTINGS_NATIVE_TEST
#include "native_preview_hal.h"
#include "read_pico_sd.h"
#endif

static const char *rootdir;
#ifdef SETTINGS_NATIVE_TEST
const char *native_preview_root(void) { return rootdir; }
void native_preview_log(const char *tag,const char *fmt,...) { (void)tag;(void)fmt; }
esp_err_t read_pico_sd_get_info(read_pico_sd_info_t *out) {
    if(out)*out=(read_pico_sd_info_t){.present=true,.mounted=true};
    return ESP_OK;
}
#else
static bool fail_init,fail_open,pending,fail_sound_set,fail_sound_commit,pending_sound;
static uint8_t pending_value;
static uint8_t pending_sound_value;
static FILE *state_file(const char *mode) {
    char path[1024];assert(snprintf(path,sizeof(path),"%s/settings.bin",rootdir)<(int)sizeof(path));
    return fopen(path,mode);
}
static FILE *sound_file(const char *mode) {
    char path[1024];assert(snprintf(path,sizeof(path),"%s/sound.bin",rootdir)<(int)sizeof(path));return fopen(path,mode);
}
esp_err_t nvs_flash_init(void) { return fail_init?ESP_FAIL:ESP_OK; }
esp_err_t nvs_open(const char *ns,int mode,nvs_handle_t *out) {
    assert(!strcmp(ns,"read_pico"));assert(mode==NVS_READONLY||mode==NVS_READWRITE);
    if(fail_open)return ESP_FAIL;
    *out=1;return ESP_OK;
}
void nvs_close(nvs_handle_t h) { assert(h==1);pending_sound=false; }
esp_err_t nvs_get_u8(nvs_handle_t h,const char *key,uint8_t *out) {
    assert(h==1);
    bool sound=!strcmp(key,"sound_v1");
    if(strcmp(key,"dev_awake")&&!sound)return ESP_ERR_NVS_NOT_FOUND;
    FILE *file=sound?sound_file("rb"):state_file("rb");if(!file)return ESP_ERR_NVS_NOT_FOUND;
    assert(fread(out,1,1,file)==1);assert(!fclose(file));return ESP_OK;
}
esp_err_t nvs_set_u8(nvs_handle_t h,const char *key,uint8_t value) {
    assert(h==1);
    if(!strcmp(key,"dev_awake")){pending=true;pending_value=value;}
    if(!strcmp(key,"sound_v1")){if(fail_sound_set)return ESP_FAIL;pending_sound=true;pending_sound_value=value;}
    return ESP_OK;
}
esp_err_t nvs_get_str(nvs_handle_t h,const char *key,char *out,size_t *size) {
    (void)h;(void)key;(void)out;(void)size;return ESP_ERR_NVS_NOT_FOUND;
}
esp_err_t nvs_set_str(nvs_handle_t h,const char *key,const char *text) {
    (void)key;(void)text;assert(h==1);return ESP_OK;
}
esp_err_t nvs_commit(nvs_handle_t h) {
    assert(h==1);
    if(pending_sound&&fail_sound_commit)return ESP_FAIL;
    if(pending_sound){FILE *file=sound_file("wb");assert(file);assert(fwrite(&pending_sound_value,1,1,file)==1);assert(!fclose(file));pending_sound=false;}
    if(pending){FILE *file=state_file("wb");assert(file);assert(fwrite(&pending_value,1,1,file)==1);assert(!fclose(file));pending=false;}
    return ESP_OK;
}
#endif

int main(int argc,char **argv) {
    assert(argc==3);rootdir=argv[1];const char *action=argv[2];
#ifndef SETTINGS_NATIVE_TEST
    fail_init=!strcmp(action,"init-fail");fail_open=!strcmp(action,"open-fail")||!strcmp(action,"sound-open-fail");
    fail_sound_set=!strcmp(action,"sound-set-fail");fail_sound_commit=!strcmp(action,"sound-commit-fail");
    app_settings_init();
#endif
    if(!strcmp(action,"set-off"))app_settings_set_test_awake(false);
    if(!strcmp(action,"set-on"))app_settings_set_test_awake(true);
    bool expected=strcmp(action,"set-off")&&strcmp(action,"get-off");
    assert(app_settings_test_awake()==expected);
    if(!strcmp(action,"sound-get-on"))assert(app_settings_sound_flags()==15);
    if(!strcmp(action,"sound-set-off"))assert(app_settings_set_sound_flags(14)&&app_settings_sound_flags()==14);
    if(!strcmp(action,"sound-get-off"))assert(app_settings_sound_flags()==14);
    if(!strcmp(action,"sound-set-custom"))assert(app_settings_set_sound_flags(7)&&app_settings_sound_flags()==7);
    if(!strcmp(action,"sound-get-custom"))assert(app_settings_sound_flags()==7);
    if(!strcmp(action,"sound-set-on"))assert(app_settings_set_sound_flags(15)&&app_settings_sound_flags()==15);
    if(!strcmp(action,"sound-invalid")||!strcmp(action,"sound-open-fail")||!strcmp(action,"sound-set-fail")||!strcmp(action,"sound-commit-fail")) {
        uint8_t before=app_settings_sound_flags();assert(!app_settings_set_sound_flags(!strcmp(action,"sound-invalid")?16:0));assert(app_settings_sound_flags()==before);
    }
    return 0;
}

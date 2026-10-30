/* SPDX-License-Identifier: Apache-2.0
 * 中文：私有预览 TF 目录与 NVS 文件适配；进度的记录格式和校验仍运行实际 book_progress.c。
 * English: Private preview TF/NVS file adaptation; actual book_progress.c owns progress format and validation.
 */
#include "native_preview_hal.h"
#include "book_store.h"
#include "book_progress.h"
#include "settings.h"
#include "nvs.h"
#include "read_pico_sd.h"
#include <errno.h>
#include <limits.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <unistd.h>

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif
static unsigned host_book_revision;
static char host_namespaces[16][40];
static bool host_namespace_used[16];

static bool owned_name(const char *text) {
    if (!text || !*text || strlen(text) >= 40) return false;
    for (; *text; ++text) if (!((*text >= 'a' && *text <= 'z') || (*text >= 'A' && *text <= 'Z') ||
        (*text >= '0' && *text <= '9') || *text == '_')) return false;
    return true;
}
static bool make_dir(const char *path) {
    struct stat st;
    return mkdir(path, 0700) == 0 || (errno == EEXIST && stat(path, &st) == 0 && S_ISDIR(st.st_mode));
}
static bool storage_dirs(void) {
    char path[PATH_MAX];
    const char *parts[] = {"tf", "tf/books", "tf/.pico-system", "tf/.pico-system/nvs", "private", "private/nvs"};
    for (unsigned i = 0; i < sizeof(parts)/sizeof(parts[0]); ++i) {
        if (snprintf(path, sizeof(path), "%s/%s", native_preview_root(), parts[i]) >= (int)sizeof(path) || !make_dir(path)) return false;
        if (i >= 4 && chmod(path, 0700)) return false;
    }
    return true;
}
static bool nvs_path(nvs_handle_t h, const char *key, char path[PATH_MAX]) {
    if (!(h > 0 && h <= 16 && host_namespace_used[h - 1] && owned_name(key))) return false;
    /* Firmware NVS is flash, never an exported TF directory. Keep Home credentials private. */
    const char *directory = !strcmp(host_namespaces[h - 1], "pico_home_pga") ? "private/nvs" : "tf/.pico-system/nvs";
    return snprintf(path, PATH_MAX, "%s/%s/%s_%s", native_preview_root(), directory, host_namespaces[h - 1], key) < PATH_MAX;
}
esp_err_t nvs_open(const char *name, int mode, nvs_handle_t *out) {
    (void)mode;
    if (!out || !owned_name(name)) return ESP_ERR_INVALID_ARG;
    if (!storage_dirs()) return ESP_FAIL;
    for (int i = 0; i < 16; ++i) if (!host_namespace_used[i]) {
        snprintf(host_namespaces[i], sizeof(host_namespaces[i]), "%s", name);
        host_namespace_used[i] = true; *out = i + 1; return ESP_OK;
    }
    return ESP_ERR_NO_MEM;
}
void nvs_close(nvs_handle_t h) { if (h > 0 && h <= 16) host_namespace_used[h - 1] = false; }
esp_err_t nvs_get_blob(nvs_handle_t h, const char *key, void *out, size_t *size) {
    char path[PATH_MAX];
    if (!size || !nvs_path(h, key, path)) return ESP_ERR_INVALID_ARG;
    FILE *f = fopen(path, "rb");
    if (!f) return errno == ENOENT ? ESP_ERR_NVS_NOT_FOUND : ESP_FAIL;
    if (fseek(f, 0, SEEK_END)) { fclose(f); return ESP_FAIL; }
    long length = ftell(f);
    if (length < 0 || fseek(f, 0, SEEK_SET)) { fclose(f); return ESP_FAIL; }
    if (!out) { *size = (size_t)length; fclose(f); return ESP_OK; }
    if (*size < (size_t)length) { *size = (size_t)length; fclose(f); return ESP_ERR_NVS_INVALID_LENGTH; }
    bool ok = fread(out, 1, (size_t)length, f) == (size_t)length;
    *size = (size_t)length;
    if (fclose(f)) ok = false;
    return ok ? ESP_OK : ESP_FAIL;
}
esp_err_t nvs_set_blob(nvs_handle_t h, const char *key, const void *data, size_t size) {
    char path[PATH_MAX], temporary[PATH_MAX];
    if (!data || !nvs_path(h, key, path)) return ESP_ERR_INVALID_ARG;
    if (snprintf(temporary, sizeof(temporary), "%s.tmp", path) >= (int)sizeof(temporary)) return ESP_ERR_INVALID_SIZE;
    FILE *f = fopen(temporary, "wb");
    if (!f) return ESP_FAIL;
    if (!strcmp(host_namespaces[h - 1], "pico_home_pga") && fchmod(fileno(f), 0600)) { fclose(f); unlink(temporary); return ESP_FAIL; }
    bool ok = fwrite(data, 1, size, f) == size;
    if (fflush(f)) ok = false;
    if (fclose(f)) ok = false;
    if (ok && rename(temporary, path) == 0) return ESP_OK;
    unlink(temporary); return ESP_FAIL;
}
esp_err_t nvs_get_str(nvs_handle_t h, const char *key, char *out, size_t *size) {
    esp_err_t err = nvs_get_blob(h, key, out, size);
    if (err == ESP_OK && out && (!*size || out[*size - 1] != 0)) return ESP_ERR_INVALID_RESPONSE;
    return err;
}
esp_err_t nvs_set_str(nvs_handle_t h, const char *key, const char *text) { return text ? nvs_set_blob(h,key,text,strlen(text)+1) : ESP_ERR_INVALID_ARG; }
esp_err_t nvs_erase_key(nvs_handle_t h, const char *key) {
    char path[PATH_MAX];
    if (!nvs_path(h,key,path)) return ESP_ERR_INVALID_ARG;
    return !unlink(path) ? ESP_OK : errno == ENOENT ? ESP_ERR_NVS_NOT_FOUND : ESP_FAIL;
}
esp_err_t nvs_commit(nvs_handle_t h) { return h > 0 && h <= 16 && host_namespace_used[h-1] ? ESP_OK : ESP_ERR_INVALID_ARG; }
esp_err_t nvs_get_u32(nvs_handle_t h,const char *key,uint32_t *value) {
    size_t size = sizeof(*value); esp_err_t err = nvs_get_blob(h,key,value,&size);
    return err == ESP_OK && size != sizeof(*value) ? ESP_ERR_INVALID_SIZE : err;
}
esp_err_t nvs_set_u32(nvs_handle_t h,const char *key,uint32_t value) { return nvs_set_blob(h,key,&value,sizeof(value)); }

static uint8_t host_setting_u8(const char *key,uint8_t fallback) {
    nvs_handle_t h;uint8_t value=fallback;size_t size=1;
    if (nvs_open("read_pico",NVS_READONLY,&h)==ESP_OK) {
        if (nvs_get_blob(h,key,&value,&size)!=ESP_OK || size!=1) value=fallback;
        nvs_close(h);
    }
    return value;
}
static void host_setting_save_u8(const char *key,uint8_t value) {
    nvs_handle_t h;
    if (nvs_open("read_pico",NVS_READWRITE,&h)==ESP_OK) {
        if (nvs_set_blob(h,key,&value,1)==ESP_OK) nvs_commit(h);
        nvs_close(h);
    }
}
static uint8_t valid_book_size(uint8_t value) { return value>=36 && value<=72 && (value-36)%4==0 ? value : 48; }
uint8_t app_settings_book_px(void) { return valid_book_size(host_setting_u8("bk_px",48)); }
void app_settings_set_book_px(uint8_t px) { host_setting_save_u8("bk_px",valid_book_size(px)); }
bool app_settings_book_shake(void) { return host_setting_u8("bk_shake",0)!=0; }
void app_settings_set_book_shake(bool on) { host_setting_save_u8("bk_shake",on?1:0); }
bool app_settings_test_awake(void) { return host_setting_u8("dev_awake",1)!=0; }
void app_settings_set_test_awake(bool on) { host_setting_save_u8("dev_awake",on?1:0); }
uint8_t app_settings_sound_flags(void) { uint8_t value=host_setting_u8("sound_v1",15);return value&~15u?15:value; }
bool app_settings_set_sound_flags(uint8_t flags) {
    if(flags&~15u)return false;
    nvs_handle_t h;if(nvs_open("read_pico",NVS_READWRITE,&h)!=ESP_OK)return false;
    esp_err_t error=nvs_set_blob(h,"sound_v1",&flags,1);
    if(error==ESP_OK)error=nvs_commit(h);
    nvs_close(h);return error==ESP_OK;
}

esp_err_t book_store_roots(book_store_root_t out[2], int *n) {
    if (!out || !n) return ESP_ERR_INVALID_ARG;
    memset(out,0,2*sizeof(*out)); *n = 0;
    if (!storage_dirs()) return ESP_FAIL;
    snprintf(out[0].path,sizeof(out[0].path),"/sdcard/books"); *n = 1; return ESP_OK;
}
bool book_store_roots_degraded(void) { return false; }
bool book_store_flash_ready(void) { return false; }
esp_err_t book_store_upload_root(book_store_root_t *out) {
    book_store_root_t roots[2];int count=0;esp_err_t err=book_store_roots(roots,&count);
    if (err == ESP_OK && out) *out=roots[0];
    return !out ? ESP_ERR_INVALID_ARG : err;
}
size_t book_store_file_limit(const book_store_root_t *root) { return root ? SIZE_MAX : 0; }
uint64_t book_store_free_bytes(const book_store_root_t *root) {
    struct statvfs fs;
    if (!root || statvfs(native_preview_root(),&fs)) return 0;
    return (uint64_t)fs.f_bavail * fs.f_frsize;
}
void book_store_notify_changed(void) { ++host_book_revision; }
unsigned book_store_revision(void) { return host_book_revision; }
// 原样生成自生产 book_store.c；仅文件系统落在预览私有 TF。
// Generated verbatim from production book_store.c; only the filesystem is private preview TF.
#include "native-preview-book-delete.inc"

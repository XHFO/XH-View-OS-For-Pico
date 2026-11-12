/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 *
 * TF进度事务、单机/双机记录与旧NVS保留源迁移，以完整路径校验碰撞。
 * TF progress transactions, solo/paired records and source-preserving NVS migration with full-path collision checks.
 *
 * 冻结：独立 rp_books 命名空间；不擦除分区、不覆盖碰撞书籍。为可靠最近阅读排序，
 * 用户修订：v3在TF分别保存单机/双机及最远正文锚点；NVS仅保留旧源和排序计数。
 * Frozen: use rp_books; never erase partitions or overwrite collisions. Reliable recent ordering uses
 * persistent sequences. User revision: v3 TF stores solo/paired and furthest text anchors; retain legacy NVS and sequence only.
 */
#include "book_progress.h"
#include "book_store.h"

#include <inttypes.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "esp_log.h"
#include "nvs.h"

// 复用带CRC、备份和原子提升的TF事务存储。/ Reuse the CRC/backup/atomic TF store.
extern bool ps_storage_record_roots(char*, char*, size_t, bool*);
extern bool ps_load_at(const char*, const char*, void*, size_t);
extern bool ps_store_at(const char*, const char*, const void*, size_t);
extern bool ps_store_exists_at(const char*, const char*, bool*);

#define PATH_CAP BOOK_STORE_PATH_MAX
#define HEADER_SIZE 24
#define RECORD_CAP (HEADER_SIZE + PATH_CAP)
static const char* TAG = "book_progress";
static const char* NS = "rp_books";

// 列表拓扑仅在 HTTP 停止并 join 后修改；HTTP 只原子标记已有节点。
// Mutate list topology only after HTTP stops and joins; HTTP only atomically marks existing nodes.
struct book_progress_watch {
    char path[PATH_CAP];
    atomic_bool invalidated;
    struct book_progress_watch* next;
};
static book_progress_watch_t* s_watches;

static bool path_valid(const char* path) {
    return path != NULL && path[0] != '\0' && strnlen(path, PATH_CAP) < PATH_CAP;
}

book_progress_watch_t* book_progress_watch_create(const char* path) {
    if (!path_valid(path)) return NULL;
    book_progress_watch_t* watch = malloc(sizeof(*watch));
    if (!watch) return NULL;
    strcpy(watch->path, path);
    atomic_init(&watch->invalidated, false);
    watch->next = s_watches;
    s_watches = watch;
    return watch;
}

bool book_progress_watch_invalidated(const book_progress_watch_t* watch) {
    return watch && atomic_load(&watch->invalidated);
}

void book_progress_watch_destroy(book_progress_watch_t* watch) {
    book_progress_watch_t** p = &s_watches;
    while (*p && *p != watch) p = &(*p)->next;
    if (*p) { *p = watch->next; free(watch); }
}

static void make_key(const char* path, char key[11]) {
    uint32_t hash = UINT32_C(2166136261);
    for (const unsigned char* p = (const unsigned char*)path; *p; ++p) {
        hash = (hash ^ *p) * UINT32_C(16777619);
    }
    snprintf(key, 11, "b_%08" PRIx32, hash);
}

static uint32_t get32(const uint8_t* p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}


static bool record_valid(const uint8_t* data, size_t len) {
    if (len < HEADER_SIZE + 2 || len > RECORD_CAP || memcmp(data, "RPB", 3) != 0 ||
        (data[3] != 1 && data[3] != 2)) return false;
    size_t path_len = len - HEADER_SIZE;
    if (data[3] == 1 && (path_len > 160 || data[23] != 0)) return false;
    return ((size_t)data[22] | ((size_t)data[23] << 8)) == path_len &&
        strnlen((const char*)data + HEADER_SIZE, path_len) == path_len - 1;
}

static esp_err_t warn_error(esp_err_t err) {
    if (err != ESP_OK) ESP_LOGW(TAG, "progress persistence: %s", esp_err_to_name(err));
    return err;
}

static bool legacy_load(const char* path, uint32_t size, book_progress_t* out) {
    if (!path_valid(path) || out == NULL) return false;
    nvs_handle_t h;
    if (nvs_open(NS, NVS_READONLY, &h) != ESP_OK) return false;
    char key[11];
    make_key(path, key);
    uint8_t data[RECORD_CAP];
    size_t len = sizeof(data);
    esp_err_t err = nvs_get_blob(h, key, data, &len);
    nvs_close(h);
    if (err != ESP_OK || !record_valid(data, len) ||
        strcmp((const char*)data + HEADER_SIZE, path) != 0 || get32(data + 4) != size ||
        data[14] < 36 || data[14] > 72 || (data[14] - 36) % 4 != 0 || data[15] > 100) return false;
    *out = (book_progress_t){
        .file_size = get32(data + 4), .chapter = (uint16_t)(data[8] | (data[9] << 8)),
        .byte_off = get32(data + 10), .px = data[14], .pct = data[15],
        .last_open_s = data[3] == 2 ? get32(data + 16) : 0,
    };
    return true;
}


static esp_err_t legacy_owner(const char* path){
    nvs_handle_t h;esp_err_t err=nvs_open(NS,NVS_READONLY,&h);
    if(err==ESP_ERR_NVS_NOT_FOUND)return ESP_OK;
    if(err!=ESP_OK)return err;
    char key[11];make_key(path,key);uint8_t data[RECORD_CAP];size_t len=sizeof(data);
    err=nvs_get_blob(h,key,data,&len);nvs_close(h);
    if(err==ESP_ERR_NVS_NOT_FOUND)return ESP_OK;
    if(err!=ESP_OK)return err;
    return record_valid(data,len)&&!strcmp((char*)data+HEADER_SIZE,path)?ESP_OK:ESP_ERR_INVALID_STATE;
}
typedef struct {
    uint32_t version, present;
    char path[PATH_CAP];
    book_progress_t single, paired, merged;
} card_progress_t;
static bool card_root(char root[160]) {
    char other[160]; bool sd=false;
    return ps_storage_record_roots(root,other,160,&sd)&&sd;
}
static int card_read(const char* path, card_progress_t* card, char root[160], char key[11]) {
    if(!card_root(root))return -1;
    make_key(path,key);bool exists=false;
    if(!ps_store_exists_at(root,key,&exists))return -1;
    memset(card,0,sizeof(*card));
    if(!exists){card->version=3;strcpy(card->path,path);return 0;}
    if(!ps_load_at(root,key,card,sizeof(*card))||card->version!=3||card->present>3||
       !memchr(card->path,0,sizeof(card->path))||strcmp(card->path,path))return -1;
    if(card->present&&(card->merged.px<36||card->merged.px>72||(card->merged.px-36)%4||card->merged.pct>100))return -1;
    return 1;
}
bool book_progress_ahead(const book_progress_t* a,const book_progress_t* b) {
    return a->chapter>b->chapter||(a->chapter==b->chapter&&a->byte_off>b->byte_off);
}
bool book_progress_load(const char* path,uint32_t size,book_progress_t* out) {
    if(!path_valid(path)||!out)return false;
    card_progress_t card;char root[160],key[11];int found=card_read(path,&card,root,key);
    if(found<0)return false;
    if(!found){
        if(legacy_owner(path)!=ESP_OK)return false;
        book_progress_t old;
        if(!legacy_load(path,size,&old))return false;
        card.present=1;card.single=card.merged=old;
        // 导入失败保留并恢复旧锚点；后续保存重试，不能打开书首覆盖体验。/ Restore the retained legacy anchor if import fails; retry later instead of opening at the start.
        if(!ps_store_at(root,key,&card,sizeof(card)))warn_error(ESP_FAIL);
    }
    if(!card.present||card.merged.file_size!=size)return false;
    *out=card.merged;return true;
}
esp_err_t book_progress_save_mode(const char* path,const book_progress_t* progress,bool paired) {
    if(!path_valid(path)||!progress||progress->px<36||progress->px>72||
       (progress->px-36)%4||progress->pct>100)return ESP_ERR_INVALID_ARG;
    card_progress_t card;char root[160],key[11];int found=card_read(path,&card,root,key);
    if(found<0)return warn_error(ESP_ERR_INVALID_STATE);
    if(!found){
        esp_err_t owner=legacy_owner(path);if(owner!=ESP_OK)return warn_error(owner);
        book_progress_t old;
        if(legacy_load(path,progress->file_size,&old)){card.present=1;card.single=card.merged=old;}
    }
    if(card.present&&card.merged.file_size!=progress->file_size)card.present=0;
    book_progress_t saved=*progress;
    nvs_handle_t h;uint32_t sequence=0;
    esp_err_t err=nvs_open(NS,NVS_READWRITE,&h);
    if(err!=ESP_OK)return warn_error(err);
    err=nvs_get_u32(h,"seq",&sequence);
    if(err==ESP_ERR_NVS_NOT_FOUND)err=ESP_OK;
    if(card.present&&card.merged.last_open_s>sequence)sequence=card.merged.last_open_s;
    if(err==ESP_OK&&sequence==UINT32_MAX)err=ESP_ERR_INVALID_STATE;
    if(err==ESP_OK)err=nvs_set_u32(h,"seq",++sequence);
    if(err==ESP_OK)err=nvs_commit(h);
    nvs_close(h);if(err!=ESP_OK)return warn_error(err);
    saved.last_open_s=sequence;
    if(!card.present||book_progress_ahead(&saved,&card.merged))card.merged=saved;
    card.merged.last_open_s=sequence;
    if(paired)card.paired=saved;else card.single=saved;
    card.present|=paired?2:1;
    return warn_error(ps_store_at(root,key,&card,sizeof(card))?ESP_OK:ESP_FAIL);
}
esp_err_t book_progress_save(const char* path,const book_progress_t* progress) {
    return book_progress_save_mode(path,progress,false);
}
esp_err_t book_progress_clear(const char* path) {
    if(!path_valid(path))return ESP_ERR_INVALID_ARG;
    card_progress_t card;char root[160],key[11];
    if(card_read(path,&card,root,key)<0)return warn_error(ESP_ERR_INVALID_STATE);
    // 墓碑阻止旧NVS和备份记录再次复活。/ A tombstone prevents legacy NVS progress resurrection.
    esp_err_t owner=legacy_owner(path);if(owner!=ESP_OK)return warn_error(owner);
    card.present=0;
    return warn_error(ps_store_at(root,key,&card,sizeof(card))?ESP_OK:ESP_FAIL);
}
esp_err_t book_progress_forget(const char* path) {
    if(!path_valid(path))return ESP_ERR_INVALID_ARG;
    for(book_progress_watch_t* w=s_watches;w;w=w->next)
        if(!strcmp(w->path,path))atomic_store(&w->invalidated,true);
    char last[PATH_CAP];
    if(book_progress_last_path(last,sizeof(last))&&!strcmp(last,path)){
        esp_err_t err=book_progress_set_last_path("");if(err!=ESP_OK)return err;
    }
    return book_progress_clear(path);
}
bool book_progress_last_path(char* out,size_t cap) {
    if(!out||!cap)return false;
    out[0]=0;
    char root[160],saved[PATH_CAP]={0};bool exists=false;
    if(!card_root(root)||!ps_store_exists_at(root,"progress-last",&exists))return false;
    if(exists){
        if(!ps_load_at(root,"progress-last",saved,sizeof(saved)))return false;
    }else{
        nvs_handle_t h;size_t len=sizeof(saved);
        if(nvs_open(NS,NVS_READONLY,&h)!=ESP_OK)return false;
        esp_err_t err=nvs_get_str(h,"last",saved,&len);nvs_close(h);
        if(err!=ESP_OK||!path_valid(saved)||!ps_store_at(root,"progress-last",saved,sizeof(saved)))return false;
    }
    if(!path_valid(saved)||strlen(saved)>=cap)return false;
    strcpy(out,saved);return true;
}
esp_err_t book_progress_set_last_path(const char* path) {
    if(!path||strnlen(path,PATH_CAP)>=PATH_CAP)return ESP_ERR_INVALID_ARG;
    char root[160],saved[PATH_CAP]={0};if(!card_root(root))return ESP_ERR_INVALID_STATE;
    strcpy(saved,path);
    return warn_error(ps_store_at(root,"progress-last",saved,sizeof(saved))?ESP_OK:ESP_FAIL);
}

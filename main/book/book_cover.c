/*
 * SPDX-FileCopyrightText: 2026 XH Lab
 * SPDX-License-Identifier: Apache-2.0
 * 中文：书架封面的逐本准备、九项内存缓存和带校验的 TF 缩略图。
 * English: Incremental shelf-cover preparation, nine memory entries and validated TF thumbnails.
 * 冻结：查询纯内存；仅 prepare 读取本地 EPUB；只在书目录 .pico-covers 写可丢弃缓存，不改图书/进度。
 * Frozen: Lookups use memory only; preparation reads local EPUBs and writes disposable .pico-covers cache only; books/progress stay untouched.
 */
#include "book_cover.h"
#include "book_epub.h"
#include "esp_heap_caps.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>

#define COVER_ENTRIES 9
#define COVER_PATH_MAX 288
#define COVER_WIDTH 128
#define COVER_HEIGHT 192
#define COVER_MAGIC 0x31564350u

typedef struct {
    char path[COVER_PATH_MAX];
    uint8_t *pixels;
    uint32_t age;
    uint16_t width, height;
} cover_entry_t;
typedef struct {
    uint32_t magic, bytes;
    uint64_t path_hash, source_hash;
    uint32_t pixel_hash;
    uint16_t width, height;
} cover_header_t;
static cover_entry_t entries[COVER_ENTRIES];
static uint32_t age;

static uint64_t hash64(uint64_t hash, const void *data, size_t n) {
    const unsigned char *p = data;
    while (n--) hash = (hash ^ *p++) * UINT64_C(1099511628211);
    return hash;
}
static uint32_t hash_pixels(const uint8_t *data, size_t n) {
    uint32_t hash = 2166136261u;
    while (n--) hash = (hash ^ *data++) * 16777619u;
    return hash;
}
static cover_entry_t *find(const char *path) {
    if (!path) return NULL;
    for (int i = 0; i < COVER_ENTRIES; i++) if (entries[i].path[0] && !strcmp(entries[i].path, path)) return &entries[i];
    return NULL;
}
bool book_cover_known(const char *path) { return find(path) != NULL; }
bool book_cover_get(const char *path, book_cover_view_t *out) {
    if (!out) return false;
    memset(out, 0, sizeof(*out));
    const cover_entry_t *entry = find(path);
    if (!entry || !entry->pixels) return false;
    out->pixels = entry->pixels; out->width = entry->width; out->height = entry->height;
    return true;
}
void book_cover_clear(void) {
    for (int i = 0; i < COVER_ENTRIES; i++) { free(entries[i].pixels); memset(&entries[i], 0, sizeof(entries[i])); }
    age = 0;
}

// 文件大小、修改时间及首尾小样共同识别同名换书；不为缓存全读整本书。
// Size, modification time and small end samples identify replacements without rereading the entire book for cache lookup.
static bool source_hash(const char *path, uint64_t *out) {
    struct stat st;
    if (stat(path, &st) || !S_ISREG(st.st_mode) || st.st_size < 0) return false;
    FILE *file = fopen(path, "rb");
    if (!file) return false;
    uint64_t size = (uint64_t)st.st_size, modified = (uint64_t)st.st_mtime;
    uint64_t hash = hash64(UINT64_C(14695981039346656037), &size, sizeof(size));
    hash = hash64(hash, &modified, sizeof(modified));
    uint8_t bytes[512];
    bool ok = true;
    for (int pass = 0; pass < 2 && ok; pass++) {
        uint64_t offset = pass && size > 2048 ? size - 2048 : 0;
        if (pass && size <= 2048) break;
        if (fseek(file, (long)offset, SEEK_SET)) { ok = false; break; }
        size_t left = size - offset > 2048 ? 2048 : (size_t)(size - offset);
        while (left) {
            size_t n = left < sizeof(bytes) ? left : sizeof(bytes);
            if (fread(bytes, 1, n, file) != n) { ok = false; break; }
            hash = hash64(hash, bytes, n); left -= n;
        }
    }
    if (fclose(file)) ok = false;
    if (ok) *out = hash;
    return ok;
}
static bool cache_path(const char *book, uint64_t key, char dir[COVER_PATH_MAX + 32], char file[COVER_PATH_MAX + 64]) {
    const char *slash = strrchr(book, '/');
    if (!slash || slash == book || (size_t)(slash - book) >= COVER_PATH_MAX) return false;
    int n = snprintf(dir, COVER_PATH_MAX + 32, "%.*s/.pico-covers", (int)(slash - book), book);
    if (n < 0 || n >= COVER_PATH_MAX + 32) return false;
    n = snprintf(file, COVER_PATH_MAX + 64, "%s/%016llx.pcv", dir, (unsigned long long)key);
    return n > 0 && n < COVER_PATH_MAX + 64;
}
static bool cache_read(const char *path, uint64_t key, uint64_t source, cover_entry_t *entry) {
    FILE *file = fopen(path, "rb");
    if (!file) return false;
    cover_header_t h;
    bool ok = fread(&h, 1, sizeof(h), file) == sizeof(h) && h.magic == COVER_MAGIC && h.path_hash == key && h.source_hash == source &&
        h.width <= COVER_WIDTH && h.height <= COVER_HEIGHT && h.bytes == (uint32_t)h.width * h.height &&
        ((h.width && h.height) || (!h.width && !h.height));
    uint8_t *pixels = NULL;
    if (ok && h.bytes) {
        pixels = heap_caps_malloc(h.bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        ok = pixels && fread(pixels, 1, h.bytes, file) == h.bytes && hash_pixels(pixels, h.bytes) == h.pixel_hash;
    }
    if (ok && !h.bytes) ok = h.pixel_hash == hash_pixels(NULL, 0);
    if (ok) ok = fgetc(file) == EOF && !ferror(file);
    if (fclose(file)) ok = false;
    if (!ok) { free(pixels); return false; }
    entry->pixels = pixels; entry->width = h.width; entry->height = h.height;
    return true;
}
static void cache_write(const char *dir, const char *path, uint64_t key, uint64_t source, const cover_entry_t *entry) {
    struct stat st;
    if (mkdir(dir, 0700) && (errno != EEXIST || stat(dir, &st) || !S_ISDIR(st.st_mode))) return;
    char temporary[COVER_PATH_MAX + 72];
    int n = snprintf(temporary, sizeof(temporary), "%s.tmp", path);
    if (n < 0 || (size_t)n >= sizeof(temporary)) return;
    FILE *file = fopen(temporary, "wb");
    if (!file) return;
    cover_header_t h = {0};
    h.magic = COVER_MAGIC; h.path_hash = key; h.source_hash = source;
    h.width = entry->width; h.height = entry->height; h.bytes = (uint32_t)h.width * h.height;
    h.pixel_hash = hash_pixels(entry->pixels, h.bytes);
    bool ok = fwrite(&h, 1, sizeof(h), file) == sizeof(h) && (!h.bytes || fwrite(entry->pixels, 1, h.bytes, file) == h.bytes);
    if (ok) ok = fflush(file) == 0;
    if (ok) ok = fsync(fileno(file)) == 0;
    if (fclose(file)) ok = false;
    // FatFs 的 rename 不覆盖现有目标；缓存可丢弃，完整临时文件就绪后才移除旧缓存。
    // FatFs cannot rename over an existing destination; remove only the disposable old cache after the new file is complete.
    if (ok && unlink(path) && errno != ENOENT) ok = false;
    if (ok) ok = rename(temporary, path) == 0;
    if (!ok) (void)unlink(temporary);
}
bool book_cover_prepare(const char *path) {
    if (!path || path[0] != '/' || strlen(path) >= COVER_PATH_MAX) return false;
    // 已有封面只更新驻留顺序；调用方在填充本页缺项前保护本页已有项，不触发文件读取。
    // Existing covers only refresh residency; protect page entries before filling missing ones without file I/O.
    cover_entry_t *known = find(path);
    if (known) { known->age = ++age; return false; }
    cover_entry_t *entry = &entries[0];
    for (int i = 0; i < COVER_ENTRIES; i++) {
        if (!entries[i].path[0]) { entry = &entries[i]; break; }
        if (entries[i].age < entry->age) entry = &entries[i];
    }
    free(entry->pixels); memset(entry, 0, sizeof(*entry));
    strcpy(entry->path, path); entry->age = ++age;
    const char *extension = strrchr(path, '.');
    if (!extension || strcasecmp(extension, ".epub")) return true;
    uint64_t source;
    if (!source_hash(path, &source)) return true;
    uint64_t key = hash64(UINT64_C(14695981039346656037), path, strlen(path));
    char dir[COVER_PATH_MAX + 32], cached[COVER_PATH_MAX + 64];
    bool disk = cache_path(path, key, dir, cached);
    if (disk && cache_read(cached, key, source, entry)) return true;
    uint8_t *pixels = NULL;
    uint16_t width = 0, height = 0;
    esp_err_t result = book_epub_load_cover(path, &pixels, &width, &height);
    if (result == ESP_OK && width && height) {
        unsigned w = width, h = height;
        if (w > COVER_WIDTH) { h = (h * COVER_WIDTH + w / 2) / w; w = COVER_WIDTH; }
        if (h > COVER_HEIGHT) { w = (w * COVER_HEIGHT + h / 2) / h; h = COVER_HEIGHT; }
        if (!w) w = 1;
        if (!h) h = 1;
        entry->pixels = heap_caps_malloc(w * h, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (entry->pixels) {
            for (unsigned y = 0; y < h; y++) for (unsigned x = 0; x < w; x++)
                entry->pixels[y * w + x] = pixels[((size_t)y * height / h) * width + (size_t)x * width / w];
            entry->width = (uint16_t)w; entry->height = (uint16_t)h;
        }
    }
    free(pixels);
    // 内存压力或介质读取失败不永久缓存为“无封面”。
    // Never persist memory-pressure or I/O failures as a permanent missing cover.
    // 解码器的“不支持”也可能含内部内存不足，因此只在 RAM 保留该 fallback。
    // Decoder unsupported may include internal allocation failure; keep that fallback in RAM only.
    bool stable = (result == ESP_OK && entry->pixels) || result == ESP_ERR_NOT_FOUND;
    if (disk && stable) cache_write(dir, cached, key, source, entry);
    return true;
}

/* SPDX-License-Identifier: Apache-2.0
 * 双机书库增量传输、配对与主机仲裁；文件与socket均有界。
 * Incremental library exchange, pairing and primary arbitration; bounded files and sockets.
 * 冻结：不导出TF、不删除源文件；网络输入不成为文件路径；断流不发布临时文件。
 * Frozen: Never export TF or delete sources; network input is never a path; never publish interrupted files.
 */
#include "dual_link.h"
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>
#ifdef ESP_PLATFORM
#include "esp_heap_caps.h"
#include "esp_random.h"
#include "lwip/sockets.h"
#include "lwip/tcp.h"
#include "psa/crypto.h"
typedef psa_hash_operation_t hash_t;
static bool hash_begin(hash_t* h) {
    *h = psa_hash_operation_init();
    return psa_hash_setup(h, PSA_ALG_SHA_256) == PSA_SUCCESS;
}
static bool hash_add(hash_t* h, const void* p, size_t n) {
    return psa_hash_update(h, p, n) == PSA_SUCCESS;
}
static bool hash_end(hash_t* h, uint8_t out[32]) {
    size_t n = 0;
    return psa_hash_finish(h, out, 32, &n) == PSA_SUCCESS && n == 32;
}
static void hash_abort(hash_t* h) {
    psa_hash_abort(h);
}
static void random_bytes(void* p, size_t n) {
    esp_fill_random(p, n);
}
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#ifdef __APPLE__
#include <CommonCrypto/CommonDigest.h>
typedef CC_SHA256_CTX hash_t;
static bool hash_begin(hash_t* h) {
    return CC_SHA256_Init(h) == 1;
}
static bool hash_add(hash_t* h, const void* p, size_t n) {
    return CC_SHA256_Update(h, p, (CC_LONG)n) == 1;
}
static bool hash_end(hash_t* h, uint8_t out[32]) {
    return CC_SHA256_Final(out, h) == 1;
}
#else
#include <openssl/sha.h>
typedef SHA256_CTX hash_t;
static bool hash_begin(hash_t* h) {
    return SHA256_Init(h) == 1;
}
static bool hash_add(hash_t* h, const void* p, size_t n) {
    return SHA256_Update(h, p, n) == 1;
}
static bool hash_end(hash_t* h, uint8_t out[32]) {
    return SHA256_Final(out, h) == 1;
}
#endif
static void hash_abort(hash_t* h) {
    memset(h, 0, sizeof(*h));
}
static void random_bytes(void* p, size_t n) {
    arc4random_buf(p, n);
}
#ifdef PICO_NATIVE_PREVIEW
#include "native_preview_book_vfs.h"
#endif
#endif
// 中文：大块减少 ESP32 每轮固定开销；缓冲区随会话放在 PSRAM，不占 UI 栈。
// English: Larger chunks reduce ESP32 per-tick overhead; session buffers live in PSRAM, not the UI stack.
#define CHUNK 16384u
#define WIRE_MAX (CHUNK + 64u)
#define PROTOCOL 3
#define CONTROL_VERSION 6
#define ANNOTATION_OUT_MAX 4
#define ACK_OUT_MAX 8
enum {
    CHALLENGE = 1,
    PROOF,
    ACCEPT,
    ITEM,
    LIST_END,
    GET,
    FILE_BEGIN,
    DATA,
    FILE_END,
    DONE,
    STATE,
    OPEN,
    TURN,
    PING,
    PROGRESS = 30,
    ACK = 31,
    ENDPOINT = 32
};
typedef struct {
    uint64_t inode;
    int64_t modified, changed;
#ifndef ESP_PLATFORM
    uint32_t nanos[2];
#endif
} stamp_t;
typedef struct {
    stamp_t stamp;
    uint8_t id[32];
    uint32_t bytes;
    char path[DL_PATH];
    uint32_t progress_recent;
    uint8_t progress_pct;
    bool seen, progress_started, progress_loaded;
} local_t;
typedef struct {
    uint8_t id[32];
    uint32_t bytes;
    char name[240];
} remote_t;
typedef struct {
    uint8_t id[32];
    uint32_t recent;
    uint8_t pct;
    bool started, valid;
} remote_progress_t;
typedef struct {
    uint8_t type;
    uint32_t seq, chapter, start, end;
    char text[80];
} annotation_event_t;
typedef struct {
    uint8_t type;
    uint32_t seq;
} ack_event_t;
struct dual_link {
    dl_config_t cfg;
    local_t local[DL_BOOKS];
    remote_t remote[DL_BOOKS];
    remote_progress_t progress[DL_BOOKS];
    unsigned count, remote_count, list_at, list_limit, root_at, imports;
    bool scan, font_scanned, listed, peer_listed, done, peer_done, authenticated, connecting,
        auth_sent, accept_pending, network_used, fast_verified, peer_id_valid;
    int listener, fd, request_index;
    DIR* dir;
    FILE *hash_file, *sending, *receiving;
    char hash_path[DL_PATH], temp[DL_PATH], final[DL_PATH], pending_open[DL_PATH];
    uint32_t hash_bytes, hash_read, sent, received, expected, content_revision;
    uint32_t resume_read, resumed, verified_bytes;
    unsigned reused;
    bool resume_hashing, incoming_ready;
    stamp_t hash_stamp;
    hash_t scan_hash, receive_hash;
    uint8_t incoming_id[32], sending_id[32], font_id[32], peer_font_id[32], nonce[16];
    uint8_t tx[WIRE_MAX + 8], rx[WIRE_MAX + 8];
    uint8_t scan_chunk[CHUNK], produce_chunk[CHUNK];
    size_t tx_len, tx_at, rx_len;
    bool send_begin, send_end, state_dirty, request_pending, left_ready;
    int request_direction;
    uint8_t request_book[32], request_px;
    uint32_t request_revision, request_page, pending_open_page;
    int64_t now, retry_at, last_rx, last_tx, request_at, catalog_ready_at;
    const char* error;
    dl_reading_t reading;
    dl_orientation_t orientation;
    bool guard, guard_dirty;
    int8_t face, peer_face;
    bool face_dirty, face_sent;
    bool base_dirty, base_valid;
    int16_t base_x, base_y, base_z;
    uint32_t base_seq;
    bool calibration_request;
    bool reading_active, peer_reading, reading_dirty;
    bool annotation_dirty, annotation_valid, annotation_inflight;
    uint32_t annotation_seq;
    uint8_t annotation_type;
    uint32_t annotation_chapter, annotation_start, annotation_end;
    char annotation_text[80];
    uint32_t orientation_revision, orientation_sequence;
    bool orientation_dirty, orientation_seen, control_compatible, orientation_last;
    uint8_t orientation_request;
    uint8_t game_input;
    bool game_pending, game_ready, game_open_pending, game_open_ready;
    uint8_t game_open_kind;
    int64_t game_open_sent, game_open_until;
    uint8_t game_payload[32], game_payload_n;
    bool game_payload_pending, game_payload_ready, game_payload_inflight;
    bool game_payload_rx_seen;
    uint32_t game_payload_seq;
    uint32_t game_payload_rx_seq;
    int64_t annotation_sent_at, game_payload_sent_at;
    annotation_event_t annotations[ANNOTATION_OUT_MAX];
    ack_event_t acks[ACK_OUT_MAX];
    uint8_t annotation_head, annotation_count, ack_head, ack_count;
    bool game_close_pending, game_close_ready;
    bool control_seen, control_connected, catalog_valid, catalog_dirty, peer_catalog_valid;
    bool remote_open_pending;
    bool endpoint_dirty;
    uint8_t catalog[32], peer_catalog[32];
    uint8_t peer_device_id[8];
    unsigned catalog_count;
    unsigned progress_at;
    bool progress_dirty, current_progress_dirty;
    uint32_t progress_revision;
    uint32_t request_sequence, peer_sequence;
    int64_t control_sent, request_started;
};
static void cache_load(dual_link_t* d);
static void cache_save(dual_link_t* d);
static uint32_t get32(const uint8_t* p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}
static void put32(uint8_t* p, uint32_t n) {
    p[0] = n >> 24;
    p[1] = n >> 16;
    p[2] = n >> 8;
    p[3] = n;
}
static int16_t get16(const uint8_t* p) { return (int16_t)(((uint16_t)p[0] << 8) | p[1]); }
static void put16(uint8_t* p, int16_t n) { p[0] = (uint8_t)((uint16_t)n >> 8); p[1] = (uint8_t)n; }
static bool nonzero(const uint8_t id[32]) {
    uint8_t n = 0;
    for (int i = 0; i < 32; i++) n |= id[i];
    return n != 0;
}
static bool same(const uint8_t* a, const uint8_t* b, size_t n) {
    unsigned d = 0;
    for (size_t i = 0; i < n; i++) d |= a[i] ^ b[i];
    return !d;
}
static const char* base(const char* p) {
    const char* s = strrchr(p, '/');
    return s ? s + 1 : p;
}
static bool book_name(const char* s) {
    size_t n = strlen(s);
    if (n < 5 || n >= 240 || s[0] == '.') return false;
    for (size_t i = 0; i < n; i++)
        if ((unsigned char)s[i] < 32 || s[i] == '/' || s[i] == '\\' || s[i] == ':') return false;
    const char* ext = strrchr(s, '.');
    return ext &&
           (!strcasecmp(ext, ".txt") || !strcasecmp(ext, ".epub") || !strcasecmp(ext, ".ttf"));
}
static int find_id(const dual_link_t* d, const uint8_t id[32]) {
    for (unsigned i = 0; i < d->count; i++)
        if (d->local[i].seen && same(d->local[i].id, id, 32)) return (int)i;
    return -1;
}
static bool font_file_name(const char* name) {
    const char* ext = name ? strrchr(name, '.') : NULL;
    return ext && !strcasecmp(ext, ".ttf");
}
static bool preferred_font_name(const dual_link_t* d, const char* name) {
    const char* expected = base(d->cfg.font);
    return expected && *expected && name && !strcasecmp(name, expected);
}
static void choose_local_font(dual_link_t* d) {
    if (nonzero(d->font_id)) return;
    int fallback = -1, preferred = -1;
    for (unsigned i = 0; i < d->count; ++i) {
        if (!d->local[i].seen || !font_file_name(d->local[i].path)) continue;
        if (fallback < 0) fallback = (int)i;
        if (preferred_font_name(d, base(d->local[i].path))) {
            preferred = (int)i;
            break;
        }
    }
    int picked = preferred >= 0 ? preferred : fallback;
    if (picked >= 0) memcpy(d->font_id, d->local[picked].id, 32);
}
static void remember_peer_font(dual_link_t* d, const uint8_t id[32], const char* name) {
    if (d->cfg.role != 1 || !font_file_name(name)) return;
    if (!nonzero(d->peer_font_id) || preferred_font_name(d, name))
        memcpy(d->peer_font_id, id, 32);
}
static void close_file(FILE** f) {
    if (*f) {
        fclose(*f);
        *f = NULL;
    }
}
static void discard_partial(dual_link_t* d) {
    close_file(&d->receiving);
    hash_abort(&d->receive_hash);
    if (d->temp[0]) remove(d->temp);
    d->temp[0] = 0;
    d->expected = d->received = 0;
    d->resume_hashing = d->incoming_ready = false;
}
static void drop(dual_link_t* d, const char* error) {
    if (d->fd >= 0) close(d->fd);
    d->fd = -1;
    d->connecting = false;
    d->authenticated = false;
    d->tx_len = d->tx_at = d->rx_len = 0;
    d->listed = d->peer_listed = d->done = d->peer_done = false;
    d->list_at = d->remote_count = 0;
    d->list_limit = d->count;
    d->auth_sent = d->accept_pending = false;
    d->send_begin = d->send_end = false;
    d->request_index = -1;
    memset(d->peer_font_id, 0, sizeof(d->peer_font_id));
    close_file(&d->sending);
    // 仅本会话拥有的半文件可重试；重新读取前缀后才续传。/ Retry only this session's partial, rehashing its prefix first.
    if (d->receiving) {
        bool ok = fflush(d->receiving) == 0;
        if (fclose(d->receiving)) ok = false;
        d->receiving = NULL;
        if (!ok) discard_partial(d);
    }
    hash_abort(&d->receive_hash);
    d->resume_hashing = d->incoming_ready = false;
    d->retry_at = d->now + 2000;
    d->error = error;
}
static bool queue(dual_link_t* d, unsigned kind, const void* p, size_t n) {
    if (d->tx_len || n > WIRE_MAX) return false;
    d->tx[0] = 'D';
    d->tx[1] = 'P';
    d->tx[2] = PROTOCOL;
    d->tx[3] = (uint8_t)kind;
    put32(d->tx + 4, (uint32_t)n);
    if (n) memcpy(d->tx + 8, p, n);
    d->tx_at = 0;
    d->tx_len = n + 8;
    return true;
}
static remote_progress_t* progress_find(dual_link_t* d, const uint8_t id[32], bool create) {
    for (unsigned i = 0; i < DL_BOOKS; ++i)
        if (d->progress[i].valid && same(d->progress[i].id, id, 32)) return &d->progress[i];
    if (!create) return NULL;
    for (unsigned i = 0; i < DL_BOOKS; ++i) if (!d->progress[i].valid) {
        memset(&d->progress[i], 0, sizeof(d->progress[i]));
        memcpy(d->progress[i].id, id, 32);
        d->progress[i].valid = true;
        ++d->progress_revision;
        return &d->progress[i];
    }
    return NULL;
}
static bool ack_queue(dual_link_t* d, uint8_t type, uint32_t seq) {
    if (!seq || d->ack_count >= ACK_OUT_MAX) return false;
    d->acks[(d->ack_head + d->ack_count) % ACK_OUT_MAX] = (ack_event_t){type, seq};
    ++d->ack_count;
    return true;
}
static bool ack_take(dual_link_t* d, ack_event_t* out) {
    if (!d->ack_count) return false;
    *out = d->acks[d->ack_head];
    d->ack_head = (uint8_t)((d->ack_head + 1) % ACK_OUT_MAX);
    --d->ack_count;
    return true;
}
static bool ack_consume(dual_link_t* d, const uint8_t* p, size_t n) {
    if (n != 6 || p[0] != ACK || !get32(p + 2)) return false;
    uint32_t seq = get32(p + 2);
    if (p[1] == 15 && d->annotation_count && d->annotations[d->annotation_head].seq == seq) {
        d->annotation_head = (uint8_t)((d->annotation_head + 1) % ANNOTATION_OUT_MAX);
        --d->annotation_count;
        d->annotation_inflight = false;
        d->annotation_dirty = d->annotation_count != 0;
        return true;
    }
    if (p[1] == 22 && d->game_payload_inflight && d->game_payload_seq == seq) {
        d->game_payload_inflight = d->game_payload_pending = false;
        return true;
    }
    if (p[1] == 21 || p[1] == 23) return true;
    return false;
}
static bool proof(dual_link_t* d, const char* role, uint8_t out[32]) {
    hash_t h;
    bool ok = hash_begin(&h) && hash_add(&h, "XH-DUAL-3-layout1", 17) &&
              hash_add(&h, d->cfg.code, 6) && hash_add(&h, d->nonce, 16) &&
              hash_add(&h, role, strlen(role)) && hash_end(&h, out);
    hash_abort(&h);
    return ok;
}
static int file_stat(const char* p, struct stat* s) {
#ifdef ESP_PLATFORM
    // FAT 无符号链接；ESP VFS 只实现 stat。/ FAT has no symlinks; ESP VFS implements stat only.
    return stat(p, s);
#else
    // 原生测试与预览拒绝符号链接。/ Reject symlinks in native tests and preview.
    return lstat(p, s);
#endif
}
static bool regular_file(const char* p, struct stat* s) {
    return !file_stat(p, s) && S_ISREG(s->st_mode) && s->st_size > 0 &&
           (uint64_t)s->st_size <= DL_FILE_MAX;
}
static stamp_t file_stamp(const struct stat* s) {
    stamp_t v = { .inode = s->st_ino, .modified = s->st_mtime, .changed = s->st_ctime };
#if !defined(ESP_PLATFORM) && defined(__APPLE__)
    v.nanos[0] = s->st_mtimespec.tv_nsec; v.nanos[1] = s->st_ctimespec.tv_nsec;
#elif !defined(ESP_PLATFORM)
    v.nanos[0] = s->st_mtim.tv_nsec; v.nanos[1] = s->st_ctim.tv_nsec;
#endif
    return v;
}
static bool content_unchanged(const stamp_t* before, const struct stat* st) {
    stamp_t after = file_stamp(st);
    // ctime 也受权限及扩展属性影响，只用于缓存失效。/ Permission/xattr changes affect ctime; use it only for cache invalidation.
    return before->inode == after.inode && before->modified == after.modified
#ifndef ESP_PLATFORM
           && before->nanos[0] == after.nanos[0]
#endif
        ;
}
static bool unchanged(const local_t* b, const struct stat* s) {
    stamp_t v = file_stamp(s);
    return b->bytes == (uint32_t)s->st_size && !memcmp(&b->stamp, &v, sizeof(v));
}
static void local_progress_load(dual_link_t* d, local_t* item) {
    if (item->progress_loaded) return;
    dl_book_progress_t progress = {0};
    if (d->cfg.progress_get)
        (void)d->cfg.progress_get(item->path, item->bytes, &progress, d->cfg.progress_context);
    item->progress_started = progress.started;
    item->progress_pct = progress.pct;
    item->progress_recent = progress.recent;
    item->progress_loaded = true;
}
static dl_book_progress_t current_progress(const dual_link_t* d) {
    dl_book_progress_t saved = {0};
    int at = find_id(d, d->reading.book);
    if (at >= 0 && d->cfg.progress_get)
        (void)d->cfg.progress_get(d->local[at].path, d->local[at].bytes, &saved, d->cfg.progress_context);
    // 新开书时全书页数尚未统计，不能把已有进度短暂发布成 0%。
    // The total is briefly unknown after opening; never publish an existing book as 0% during that window.
    if (!d->reading.total) {
        if (saved.started) return saved;
        return (dl_book_progress_t){.started = true};
    }
    dl_book_progress_t result = {
        .started = true,
        .pct = (uint8_t)(((uint64_t)(d->reading.spread + 1) * 100u + d->reading.total - 1u) /
                         d->reading.total),
        .recent = saved.recent,
    };
    if (result.pct > 100) result.pct = 100;
    return result;
}
static bool scan_start(dual_link_t* d, const struct stat* st) {
    for (unsigned i = 0; i < d->count; i++) {
        local_t* b = &d->local[i];
        if (!strcmp(b->path, d->hash_path)) {
            if (!unchanged(b, st)) { b->seen = false; continue; }
            bool duplicate = false;
            for (unsigned j = 0; j < d->count; j++)
                if (i != j && d->local[j].seen && same(b->id, d->local[j].id, 32)) duplicate = true;
            if (!b->seen && !duplicate) d->reused++;
            b->seen = !duplicate;
            b->progress_loaded = false;
            local_progress_load(d, b);
            if (!strcmp(d->hash_path, d->cfg.font)) memcpy(d->font_id, b->id, 32);
            return true;
        }
    }
    d->hash_bytes = (uint32_t)st->st_size;
    d->hash_stamp = file_stamp(st);
    d->hash_read = 0;
    d->hash_file = fopen(d->hash_path, "rb");
    return d->hash_file && hash_begin(&d->scan_hash);
}
static void scan_step(dual_link_t* d) {
    if (d->hash_file) {
        size_t n = fread(d->scan_chunk, 1, sizeof(d->scan_chunk), d->hash_file);
        if (n && !hash_add(&d->scan_hash, d->scan_chunk, n)) {
            d->error = "hash failed";
            goto failed;
        }
        d->hash_read += (uint32_t)n;
        d->verified_bytes += (uint32_t)n;
        if (n == sizeof(d->scan_chunk)) return;
        struct stat st;
        if (ferror(d->hash_file) || d->hash_read != d->hash_bytes ||
            !regular_file(d->hash_path, &st) || (uint32_t)st.st_size != d->hash_bytes ||
            !content_unchanged(&d->hash_stamp, &st)) {
            d->error = "book changed while scanning";
            goto failed;
        }
        d->hash_stamp = file_stamp(&st);
        uint8_t id[32];
        if (!hash_end(&d->scan_hash, id)) {
            d->error = "hash failed";
            goto failed;
        }
        close_file(&d->hash_file);
        if (d->cfg.font[0] && !strcmp(d->hash_path, d->cfg.font)) memcpy(d->font_id, id, 32);
        int at = -1;
        for (unsigned i = 0; i < d->count; i++) {
            if (!strcmp(d->local[i].path, d->hash_path)) at = (int)i;
            if (d->local[i].seen && same(d->local[i].id, id, 32)) return;
        }
        if (at < 0) {
            if (d->count < DL_BOOKS) at = (int)d->count++;
            else for (unsigned i = 0; i < d->count; i++)
                if (!d->local[i].seen) { at = (int)i; break; }
            if (at < 0) {
                d->error = "library limit (512 books)";
                goto failed;
            }
        }
        local_t* b = &d->local[at];
        memcpy(b->id, id, 32);
        b->bytes = d->hash_bytes; b->stamp = d->hash_stamp; b->seen = true;
        b->progress_loaded = false;
        local_progress_load(d, b);
        snprintf(b->path, sizeof(b->path), "%s", d->hash_path);
        return;
    }
    if (!d->dir) {
        if (d->root_at >= d->cfg.root_count) {
            if (!d->font_scanned && d->cfg.font[0]) {
                d->font_scanned = true;
                struct stat st;
                if (regular_file(d->cfg.font, &st)) {
                    snprintf(d->hash_path, sizeof(d->hash_path), "%s", d->cfg.font);
                    if (!scan_start(d, &st)) {
                        d->error = "cannot read shared font";
                        goto failed;
                    }
                    return;
                }
            }
            // 字体允许由对端补入；空主机仍须发现对端并交换文件清单。
            // The peer may supply the font; an empty primary must still discover it and exchange files.
            choose_local_font(d);
            unsigned used = 0;
            for (unsigned i = 0; i < d->count; i++)
                if (d->local[i].seen) d->local[used++] = d->local[i];
            d->count = used;
            d->scan = false;
            d->list_limit = d->count;
            cache_save(d);
            return;
        }
        d->dir = opendir(d->cfg.roots[d->root_at]);
        if (!d->dir) {
            d->error = "book directory unavailable";
            goto failed;
        }
    }
    struct dirent* entry = readdir(d->dir);
    if (!entry) {
        closedir(d->dir);
        d->dir = NULL;
        d->root_at++;
        return;
    }
    if (!book_name(entry->d_name)) return;
    if (snprintf(
            d->hash_path, sizeof(d->hash_path), "%s/%s", d->cfg.roots[d->root_at], entry->d_name) >=
        (int)sizeof(d->hash_path)) {
        d->error = "book path too long";
        goto failed;
    }
    struct stat st;
    if (!regular_file(d->hash_path, &st)) return;
    if (!scan_start(d, &st)) {
        d->error = "cannot read book";
        goto failed;
    }
    return;
failed:
    close_file(&d->hash_file);
    hash_abort(&d->scan_hash);
    if (d->dir) {
        closedir(d->dir);
        d->dir = NULL;
    }
    d->scan = false;
    discard_partial(d);
    drop(d, d->error);
    d->retry_at = INT64_MAX;
}
static void publish(dual_link_t* d, const uint8_t* id, int direction) {
    if (id) {
        memcpy(d->reading.book, id, 32);
        memcpy(d->reading.font, d->font_id, 32);
        d->reading.spread = 0;
        d->reading.total = 0;
        d->reading.reflow_pending = false;
    } else if (direction < 0) {
        if (d->reading.spread >= 2) d->reading.spread -= 2;
    } else if (direction > 0 && d->reading.spread <= UINT32_MAX - 4) {
        uint32_t next = d->reading.spread + 2;
        if (!d->reading.total || next < d->reading.total) d->reading.spread = next;
    }
    if (++d->reading.revision == 0) d->reading.revision = 1;
    d->left_ready = false;
    d->state_dirty = true;
    d->current_progress_dirty = true;
}
static bool valid_px(uint8_t px) {
    return px >= DL_FONT_MIN && px <= DL_FONT_MAX && (px - DL_FONT_MIN) % DL_FONT_STEP == 0;
}
static void resize(dual_link_t* d, uint8_t px) {
    d->reading.reflow_from_px = d->reading.font_px;
    d->reading.font_px = px;
    d->reading.total = 0;
    d->reading.reflow_pending = true;
    publish(d, NULL, 0);
}
static bool incoming_begin(dual_link_t* d, const uint8_t* p, size_t n) {
    if (n != 40 || d->request_index < 0 || d->incoming_ready || d->resume_hashing) return false;
    remote_t* b = &d->remote[d->request_index];
    uint32_t offset = get32(p + 36);
    if (!same(b->id, p, 32) || b->bytes != get32(p + 32) || offset != d->received) return false;
    if (d->receiving) {
        if (!same(d->incoming_id, p, 32) || d->expected != b->bytes) return false;
        d->incoming_ready = true;
        return true;
    }
    if (offset) return false;
    char hex[65];
    for (int i = 0; i < 32; i++) snprintf(hex + 2 * i, 3, "%02x", p[i]);
    const char* ext = strrchr(b->name, '.');
    // 优先保留原书名；同名不同书增加摘要，绝不覆盖。/ Preserve names; suffix collisions without replacing originals.
    if (snprintf(d->final, sizeof(d->final), "%s/%s", d->cfg.destination, b->name) >=
        (int)sizeof(d->final))
        return false;
    struct stat st;
    if (!file_stat(d->final, &st)) {
        if (snprintf(d->final, sizeof(d->final), "%s/dual-%.64s%s", d->cfg.destination, hex, ext) >=
                (int)sizeof(d->final) ||
            !file_stat(d->final, &st))
            return false;
    }
    if (snprintf(d->temp, sizeof(d->temp), "%s/.dual-%.64s.part", d->cfg.destination, hex) >=
        (int)sizeof(d->temp))
        return false;
    // 临时文件仅属于本协议；拒绝符号链接。/ Only this protocol owns temporary files; reject symlinks.
    if (!file_stat(d->temp, &st) && !S_ISREG(st.st_mode)) return false;
    d->receiving = fopen(d->temp, "wb");
    if (!d->receiving || !hash_begin(&d->receive_hash)) return false;
    memcpy(d->incoming_id, p, 32);
    d->expected = b->bytes;
    d->received = 0;
    d->incoming_ready = true;
    return true;
}
static bool incoming_end(dual_link_t* d) {
    uint8_t id[32];
    if (!d->receiving || !d->incoming_ready || d->received != d->expected ||
        !hash_end(&d->receive_hash, id) || !same(id, d->incoming_id, 32)) {
        discard_partial(d);
        return false;
    }
    bool ok = fflush(d->receiving) == 0;
    if (ok && fsync(fileno(d->receiving)) != 0) ok = false;
    if (fclose(d->receiving) != 0) ok = false;
    d->receiving = NULL;
    struct stat st;
    if (!ok || !file_stat(d->final, &st) || d->count >= DL_BOOKS ||
        !regular_file(d->temp, &st) || (uint32_t)st.st_size != d->expected ||
        rename(d->temp, d->final) != 0) {
        discard_partial(d);
        return false;
    }
    d->temp[0] = 0;
    local_t* b = &d->local[d->count++];
    memcpy(b->id, id, 32);
    b->bytes = d->expected;
    b->stamp = file_stamp(&st); b->seen = true;
    // rename 可能更新 ctime；缓存发布后的元数据。/ Rename can update ctime; cache the published metadata.
    if (!file_stat(d->final, &st)) b->stamp = file_stamp(&st);
    snprintf(b->path, sizeof(b->path), "%s", d->final);
    if (d->cfg.role == 1 && same(id, d->peer_font_id, 32)) {
        memcpy(d->font_id, id, 32);
        if (!nonzero(d->reading.font) || find_id(d, d->reading.font) < 0) {
            memcpy(d->reading.font, id, 32);
            d->state_dirty = true;
        }
    }
    d->imports++;
    d->content_revision++;
    d->request_index = -1;
    d->expected = d->received = 0;
    d->incoming_ready = false;
    return true;
}
static bool message(dual_link_t* d, unsigned type, const uint8_t* p, size_t n) {
    if (!d->authenticated) {
        uint8_t expected[32];
        if (d->cfg.role == 2 && type == CHALLENGE && n == 16 && !d->auth_sent) {
            memcpy(d->nonce, p, 16);
            d->auth_sent = true;
            return true;
        }
        if (d->cfg.role == 1 && type == PROOF && n == 32 && proof(d, "secondary", expected) &&
            same(expected, p, 32)) {
            d->accept_pending = true;
            return true;
        }
        if (d->cfg.role == 2 && type == ACCEPT && n == 32 && proof(d, "primary", expected) &&
            same(expected, p, 32)) {
            d->authenticated = true;
            d->error = NULL;
            return true;
        }
        d->error = "pairing code or protocol mismatch";
        return false;
    }
    switch (type) {
        case ITEM: {
            if (d->peer_listed || n < 38 || n > 36 + 239 || d->remote_count >= DL_BOOKS ||
                p[n - 1] != 0 || strlen((const char*)p + 36) != n - 37 ||
                !book_name((const char*)p + 36) || !get32(p + 32) || get32(p + 32) > DL_FILE_MAX)
                return false;
            for (unsigned i = 0; i < d->remote_count; i++)
                if (same(d->remote[i].id, p, 32)) return false;
            remember_peer_font(d, p, (const char*)p + 36);
            remote_t* b = &d->remote[d->remote_count++];
            memcpy(b->id, p, 32);
            b->bytes = get32(p + 32);
            memcpy(b->name, p + 36, n - 36);
            return true;
        }
        case LIST_END:
            if (n || d->peer_listed) return false;
            d->peer_listed = true;
            // 优先恢复原摘要；远端清单重排不会丢掉已有进度。/ Resume the same digest first despite manifest reordering.
            for (unsigned i = 1; d->temp[0] && i < d->remote_count; i++)
                if (same(d->remote[i].id, d->incoming_id, 32)) {
                    remote_t first = d->remote[0]; d->remote[0] = d->remote[i]; d->remote[i] = first;
                    break;
                }
            return true;
        case GET: {
            if (n != 36 || d->sending || d->send_end) return false;
            int i = find_id(d, p);
            if (i < 0) return false;
            struct stat st;
            uint32_t offset = get32(p + 32);
            if (!regular_file(d->local[i].path, &st) || (uint32_t)st.st_size != d->local[i].bytes ||
                !content_unchanged(&d->local[i].stamp, &st) ||
                offset > d->local[i].bytes)
                return false;
            d->sending = fopen(d->local[i].path, "rb");
            if (!d->sending || fseek(d->sending, (long)offset, SEEK_SET)) return false;
            d->send_begin = true;
            d->sent = offset;
            memcpy(d->sending_id, p, 32);
            return true;
        }
        case FILE_BEGIN: return incoming_begin(d, p, n);
        case DATA:
            if (!d->receiving || !d->incoming_ready || !n || n > CHUNK || n > d->expected - d->received ||
                fwrite(p, 1, n, d->receiving) != n || !hash_add(&d->receive_hash, p, n)) {
                discard_partial(d); return false;
            }
            d->received += (uint32_t)n;
            return true;
        case FILE_END: return !n && incoming_end(d);
        case DONE:
            if (n || !d->peer_listed) return false;
            d->peer_done = true;
            return true;
        // 翻页与开书只接受独立 BLE 通道；Wi-Fi 不承载阅读指令。
        // Reading commands belong exclusively to the separate BLE channel.
        case STATE: case OPEN: case TURN: return false;
        case PING: return n == 0;
        case PROGRESS: return false;
        default: return false;
    }
}
static void network(dual_link_t* d) {
    if (d->fd < 0) {
        if (d->now < d->retry_at) return;
        if (d->cfg.role == 1) {
            if (d->listener < 0) {
                int s = socket(AF_INET, SOCK_STREAM, 0);
                if (s < 0) return;
                int yes = 1;
                setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
                struct sockaddr_in a = { .sin_family = AF_INET,
                                         .sin_port = htons(d->cfg.port),
                                         .sin_addr.s_addr = htonl(INADDR_ANY) };
                if (bind(s, (struct sockaddr*)&a, sizeof(a)) || listen(s, 1) ||
                    fcntl(s, F_SETFL, O_NONBLOCK)) {
                    close(s);
                    d->retry_at = d->now + 2000;
                    d->error = "listen failed";
                    return;
                }
                d->listener = s;
            }
            d->fd = accept(d->listener, NULL, NULL);
            if (d->fd < 0) return;
            d->network_used = true;
            fcntl(d->fd, F_SETFL, O_NONBLOCK);
            random_bytes(d->nonce, sizeof(d->nonce));
            queue(d, CHALLENGE, d->nonce, 16);
        } else {
            struct sockaddr_in a = { .sin_family = AF_INET, .sin_port = htons(d->cfg.port) };
            if (inet_pton(AF_INET, d->cfg.host, &a.sin_addr) != 1) {
                d->error = "invalid primary IPv4 address";
                d->retry_at = INT64_MAX;
                return;
            }
            d->fd = socket(AF_INET, SOCK_STREAM, 0);
            if (d->fd < 0) return;
            d->network_used = true;
            fcntl(d->fd, F_SETFL, O_NONBLOCK);
            if (connect(d->fd, (struct sockaddr*)&a, sizeof(a)) && errno != EINPROGRESS) {
                drop(d, "primary unavailable");
                return;
            }
            d->connecting = true;
        }
#ifdef SO_NOSIGPIPE
        int yes = 1;
        setsockopt(d->fd, SOL_SOCKET, SO_NOSIGPIPE, &yes, sizeof(yes));
#endif
        int no_delay = 1;
        setsockopt(d->fd, IPPROTO_TCP, TCP_NODELAY, &no_delay, sizeof(no_delay));
        d->last_rx = d->last_tx = d->now;
    }
    if (d->connecting) {
        fd_set wr;
        FD_ZERO(&wr);
        FD_SET(d->fd, &wr);
        struct timeval wait = { 0 };
        if (select(d->fd + 1, NULL, &wr, NULL, &wait) <= 0) return;
        int error = 0;
        socklen_t len = sizeof(error);
        if (getsockopt(d->fd, SOL_SOCKET, SO_ERROR, &error, &len) || error) {
            drop(d, "primary unavailable");
            return;
        }
        d->connecting = false;
    }
    if (d->tx_len) {
        ssize_t n = send(d->fd, d->tx + d->tx_at, d->tx_len - d->tx_at, 0);
        if (n > 0) {
            d->tx_at += (size_t)n;
            d->last_tx = d->now;
            if (d->tx_at == d->tx_len) d->tx_at = d->tx_len = 0;
        } else if (n == 0 || (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR)) {
            drop(d, "connection lost");
            return;
        }
    }
    // 一次只收一个完整帧，避免大量文件数据占满UI一轮。/ Receive one frame per tick to bound UI work.
    size_t want = d->rx_len < 8 ? 8 : 8 + get32(d->rx + 4);
    if (want > sizeof(d->rx)) {
        discard_partial(d);
        drop(d, "oversize frame");
        return;
    }
    if (d->rx_len < want) {
        ssize_t n = recv(d->fd, d->rx + d->rx_len, want - d->rx_len, 0);
        if (n > 0) {
            d->rx_len += (size_t)n;
            d->last_rx = d->now;
        } else if (n == 0 || (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR)) {
            drop(d, "connection lost");
            return;
        }
    }
    if (d->rx_len >= 8) {
        if (d->rx[0] != 'D' || d->rx[1] != 'P' || d->rx[2] != PROTOCOL || get32(d->rx + 4) > WIRE_MAX) {
            discard_partial(d);
            drop(d, "invalid protocol");
            return;
        }
        size_t n = get32(d->rx + 4);
        if (d->rx_len == 8 + n) {
            if (!message(d, d->rx[3], d->rx + 8, n)) {
                const char* error = d->error ? d->error : "invalid peer data or storage error";
                discard_partial(d);
                drop(d, error);
                d->retry_at = d->now + 10000;
                return;
            }
            d->rx_len = 0;
        }
    }
    if (d->now - d->last_rx > (d->authenticated ? 30000 : 5000)) {
        drop(d, "peer timeout");
        return;
    }
}
static void produce(dual_link_t* d) {
    if (d->fd < 0 || d->connecting || d->tx_len) return;
    uint8_t* p = d->produce_chunk;
    if (d->accept_pending) {
        if (!proof(d, "primary", p)) {
            discard_partial(d);
            drop(d, "hash failed");
            return;
        }
        queue(d, ACCEPT, p, 32);
        d->accept_pending = false;
        d->authenticated = true;
        d->error = NULL;
        return;
    }
    if (!d->authenticated) {
        if (d->cfg.role == 2 && d->auth_sent) {
            if (!proof(d, "secondary", p)) {
                discard_partial(d);
                drop(d, "hash failed");
                return;
            }
            queue(d, PROOF, p, 32);
            d->auth_sent = false;
        }
        return;
    }
    if (d->sending) {
        if (d->send_begin) {
            // 通过打开句柄重新定位内容条目，不复用发送缓冲中的临时值。/ Locate the source by an explicit current path, never scratch wire bytes.
            int fd = fileno(d->sending);
            struct stat st;
            if (fstat(fd, &st) || st.st_size <= 0 || (uint64_t)st.st_size > DL_FILE_MAX) {
                discard_partial(d);
                drop(d, "source changed");
                return;
            }
            memcpy(p, d->sending_id, 32);
            put32(p + 32, (uint32_t)st.st_size);
            put32(p + 36, d->sent);
            queue(d, FILE_BEGIN, p, 40);
            d->send_begin = false;
            return;
        }
        size_t n = fread(p, 1, CHUNK, d->sending);
        if (n) {
            d->sent += (uint32_t)n;
            queue(d, DATA, p, n);
            return;
        }
        if (ferror(d->sending)) {
            discard_partial(d);
            drop(d, "source read failed");
            return;
        }
        close_file(&d->sending);
        d->send_end = true;
    }
    if (d->send_end) {
        queue(d, FILE_END, NULL, 0);
        d->send_end = false;
        return;
    }
    if (d->resume_hashing) {
        size_t n = d->received - d->resume_read;
        if (n > CHUNK) n = CHUNK;
        if (fread(p, 1, n, d->receiving) != n || !hash_add(&d->receive_hash, p, n)) {
            discard_partial(d); drop(d, "source read failed"); return;
        }
        d->resume_read += (uint32_t)n;
        if (d->resume_read < d->received) {
            // 慢 TF 重读前缀也须保活，避免等待 GET 时被对端超时。/ Keep alive during slow prefix reads while the peer awaits GET.
            if (d->now - d->last_tx > 2000) queue(d, PING, NULL, 0);
            return;
        }
        // C 的更新流由读转写前须定位。/ Position an update stream before switching from reading to writing.
        if (fseek(d->receiving, (long)d->received, SEEK_SET)) {
            discard_partial(d); drop(d, "source read failed"); return;
        }
        d->resume_hashing = false;
        d->resumed = d->received;
        memcpy(p, d->incoming_id, 32); put32(p + 32, d->received);
        queue(d, GET, p, 36);
        return;
    }
    if (!d->scan && !d->listed) {
        if (d->list_at < d->list_limit) {
            local_t* b = &d->local[d->list_at++];
            memcpy(p, b->id, 32);
            put32(p + 32, b->bytes);
            const char* name = base(b->path);
            size_t n = strlen(name) + 1;
            memcpy(p + 36, name, n);
            queue(d, ITEM, p, 36 + n);
            return;
        }
        queue(d, LIST_END, NULL, 0);
        d->listed = true;
        return;
    }
    if (d->peer_listed && !d->scan && d->request_index < 0 && !d->done) {
        for (unsigned i = 0; i < d->remote_count; i++)
            if (find_id(d, d->remote[i].id) < 0) {
                d->request_index = (int)i;
                remote_t* b = &d->remote[i];
                struct stat st;
                if (d->temp[0] && d->received && d->expected == b->bytes &&
                    same(b->id, d->incoming_id, 32) && regular_file(d->temp, &st) &&
                    (uint32_t)st.st_size == d->received) {
                    d->receiving = fopen(d->temp, "r+b");
                    if (d->receiving && hash_begin(&d->receive_hash)) {
                        d->resume_hashing = true; d->resume_read = 0; return;
                    }
                }
                discard_partial(d);
                memcpy(p, b->id, 32); put32(p + 32, 0);
                queue(d, GET, p, 36);
                return;
            }
        discard_partial(d);
        queue(d, DONE, NULL, 0);
        d->done = true;
        return;
    }
    if (d->now - d->last_tx > 2000) queue(d, PING, NULL, 0);
}
#ifdef ESP_PLATFORM
#define DUAL_CACHE_MAGIC 0x32434c44u
static void cache_path(dual_link_t* d, char* out, size_t n) {
    snprintf(out, n, "%s/.dual-hash-cache", d->cfg.destination);
}
static void cache_load(dual_link_t* d) {
    char path[DL_PATH + 32];
    cache_path(d, path, sizeof(path));
    FILE* f = fopen(path, "rb");
    if (!f) return;
    uint32_t magic = 0, count = 0;
    if (fread(&magic, sizeof(magic), 1, f) != 1 || magic != DUAL_CACHE_MAGIC ||
        fread(&count, sizeof(count), 1, f) != 1 || count > DL_BOOKS) {
        fclose(f); return;
    }
    for (uint32_t i = 0; i < count; ++i) {
        local_t b;
        if (fread(&b, sizeof(b), 1, f) != 1 || !b.path[0] || b.path[sizeof(b.path)-1] ||
            !b.bytes || !memchr(b.path, 0, sizeof(b.path))) break;
        if (d->count < DL_BOOKS) {
            b.seen = false;
            b.progress_loaded = false;
            d->local[d->count++] = b;
        }
    }
    fclose(f);
}
static void cache_save(dual_link_t* d) {
    char path[DL_PATH + 32];
    cache_path(d, path, sizeof(path));
    char temp[DL_PATH + 40];
    snprintf(temp, sizeof(temp), "%s.tmp", path);
    FILE* f = fopen(temp, "wb");
    if (!f) return;
    uint32_t magic = DUAL_CACHE_MAGIC, count = d->count;
    bool ok = fwrite(&magic, sizeof(magic), 1, f) == 1 && fwrite(&count, sizeof(count), 1, f) == 1;
    for (unsigned i = 0; ok && i < d->count; ++i) ok = fwrite(&d->local[i], sizeof(d->local[i]), 1, f) == 1;
    if (fflush(f) != 0) ok = false;
    fclose(f);
    if (ok) rename(temp, path); else remove(temp);
}
#else
static void cache_load(dual_link_t* d) { (void)d; }
static void cache_save(dual_link_t* d) { (void)d; }
#endif
dual_link_t* dl_create(const dl_config_t* cfg) {
    if (!cfg || (cfg->role != 1 && cfg->role != 2) || strlen(cfg->code) != 6 || !cfg->root_count ||
        cfg->root_count > 3 || !cfg->destination[0] || !cfg->port || cfg->port > 65535)
        return NULL;
    for (int i = 0; i < 6; i++)
        if (cfg->code[i] < '0' || cfg->code[i] > '9') return NULL;
#ifdef ESP_PLATFORM
    if (psa_crypto_init() != PSA_SUCCESS) return NULL;
    dual_link_t* d = heap_caps_calloc(1, sizeof(*d), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
#else
    dual_link_t* d = calloc(1, sizeof(*d));
#endif
    if (!d) return NULL;
    d->cfg = *cfg;
    d->listener = d->fd = d->request_index = -1;
    d->scan = true;
    d->reading.revision = 1;
    d->reading.font_px = DL_FONT_DEFAULT;
    d->orientation.inverted = true;
    d->guard = true;
    d->guard_dirty = false;
    d->face = 0;
    d->peer_face = 0;
    d->face_dirty = false;
    d->face_sent = false;
    d->base_dirty = false;
    d->base_valid = false;
    d->base_seq = 0;
    d->calibration_request = false;
    d->reading_active = true;
    d->peer_reading = true;
    d->reading_dirty = false;
    d->annotation_dirty = false;
    d->annotation_valid = false;
    d->annotation_seq = 0;
    d->orientation_revision = 1;
    cache_load(d);
    return d;
}
void dl_destroy(dual_link_t* d) {
    if (!d) return;
    drop(d, NULL);
    discard_partial(d);
    if (d->listener >= 0) close(d->listener);
    close_file(&d->hash_file);
    hash_abort(&d->scan_hash);
    if (d->dir) closedir(d->dir);
    cache_save(d);
    free(d);
}
static int catalog_compare(const void* a, const void* b) {
    return memcmp((*(const local_t* const*)a)->id, (*(const local_t* const*)b)->id, 32);
}
static void catalog_update(dual_link_t* d) {
    if (d->scan || d->receiving || d->retry_at == INT64_MAX) {
        if (d->catalog_valid) d->catalog_dirty = true;
        d->catalog_valid = false;
        d->fast_verified = false;
        return;
    }
    if (d->catalog_valid && d->catalog_count == d->count) return;
    const local_t* sorted[DL_BOOKS];
    for (unsigned i = 0; i < d->count; ++i) sorted[i] = &d->local[i];
    qsort(sorted, d->count, sizeof(sorted[0]), catalog_compare);
    hash_t h; bool ok = hash_begin(&h);
    for (unsigned i = 0; ok && i < d->count; ++i) ok = hash_add(&h, sorted[i]->id, 32);
    if (ok) ok = hash_end(&h, d->catalog);
    hash_abort(&h);
    bool became_valid = ok && !d->catalog_valid;
    d->catalog_valid = ok; d->catalog_dirty = true; d->catalog_count = d->count;
    if (became_valid) d->catalog_ready_at = d->now;
}
static bool control_ready(const dual_link_t* d) {
    return d->control_connected && d->catalog_valid && d->peer_catalog_valid &&
           same(d->catalog, d->peer_catalog, 32);
}
void dl_tick(dual_link_t* d, int64_t now, bool available) {
    if (!d) return;
    d->now = now;
    // TF 可用时本地校验独立于 Wi-Fi；已同步的书籍可离线通过 BLE 翻页。
    // Local verification is independent of Wi-Fi; matching libraries can turn over BLE offline.
    if (d->scan) scan_step(d);
    if (available && d->retry_at != INT64_MAX) { network(d); produce(d); }
    else {
        if (d->fd >= 0) drop(d, NULL);
        if (d->listener >= 0) { close(d->listener); d->listener = -1; }
    }
    catalog_update(d);
    d->fast_verified = control_ready(d) && !d->receiving && !d->sending;
    if (d->pending_open[0] && control_ready(d)) {
        char path[DL_PATH]; snprintf(path, sizeof(path), "%s", d->pending_open);
        d->pending_open[0] = 0;
        bool ok = d->pending_open_page ? dl_open_at(d, path, d->pending_open_page) : dl_open_path(d, path);
        if (!ok) d->error = "book unavailable in synchronized library";
    }
    if (d->request_pending && d->request_started && now - d->request_started > 10000) {
        d->request_pending = false; d->error = "Bluetooth reading request timed out";
    }
}
void dl_control_connected(dual_link_t* d, bool connected) {
    if (!d || d->control_connected == connected) return;
    d->control_connected = connected; d->peer_catalog_valid = false; d->control_seen = false;
    d->peer_id_valid = false;
    memset(d->peer_device_id, 0, sizeof(d->peer_device_id));
    d->network_used = false;
    d->fast_verified = false;
    d->catalog_ready_at = connected ? d->now : 0;
    d->control_compatible = d->orientation_seen = false;
    d->orientation_sequence = d->orientation_request = 0;
    d->orientation_dirty = true; d->orientation_last = false;
    d->request_pending = false; d->request_at = 0;
    d->game_pending = d->game_ready = false;
    d->game_open_pending = d->game_open_ready = false; d->game_open_sent = d->game_open_until = 0;
    if (!connected) d->game_payload_inflight = false;
    d->game_payload_ready = false;
    d->game_close_pending = d->game_close_ready = false;
    d->remote_open_pending = false;
    d->annotation_inflight = false;
    d->ack_head = d->ack_count = 0;
    d->left_ready = false;
    d->request_sequence = d->peer_sequence = 0;
    d->progress_at = 0;
    d->progress_dirty = connected;
    d->current_progress_dirty = connected;
    d->endpoint_dirty = connected && d->cfg.role == 1 && d->cfg.host[0];
    d->face_sent = false;
    d->reading_dirty = !d->reading_active;
    d->catalog_dirty = d->state_dirty = true; d->control_sent = 0;
}
size_t dl_control_take(dual_link_t* d, uint8_t* p, size_t capacity) {
    if (!d || !p || capacity < DL_CONTROL_MAX || !d->control_connected) return 0;
    bool reading_pending = control_ready(d) && (d->cfg.role == 1 ? d->state_dirty && nonzero(d->reading.book) :
        d->request_pending && (!d->request_at || d->now - d->request_at >= 500));
    bool orientation_pending = d->control_compatible && d->orientation_dirty &&
        (d->cfg.role == 1 || d->orientation_request);
    // 先排空待发状态再发周期心跳，慢速tick不能被心跳反复插队。
    // Drain queued updates before periodic heartbeats so slow ticks cannot starve them.
    ack_event_t ack;
    if (ack_take(d, &ack)) {
        p[0] = ACK; p[1] = ack.type; put32(p + 2, ack.seq);
        return 6;
    }
    if (d->game_close_pending && d->control_connected) {
        d->game_open_pending = false;
        p[0] = 23; d->game_close_pending = false; return 1;
    }
    if (d->game_open_pending && d->control_connected &&
        (!d->game_open_sent || d->now - d->game_open_sent >= 500)) {
        p[0] = 21; p[1] = d->game_open_kind;
        d->game_open_sent = d->now;
        if (d->now >= d->game_open_until) d->game_open_pending = false;
        return 2;
    }
    if (d->game_payload_pending && d->control_connected &&
        (!d->game_payload_inflight || d->now - d->game_payload_sent_at >= 750)) {
        p[0] = 22; p[1] = d->game_payload_n; memcpy(p + 2, d->game_payload, d->game_payload_n);
        put32(p + 2 + d->game_payload_n, d->game_payload_seq);
        d->game_payload_inflight = true; d->game_payload_sent_at = d->now;
        return 6 + d->game_payload_n;
    }
    if (d->cfg.role == 2 && d->game_pending && d->control_connected) {
        p[0] = 20; p[1] = d->game_input; d->game_pending = false;
        return 2;
    }
    if (d->catalog_dirty || (d->now - d->control_sent >= 1500 && !reading_pending && !orientation_pending)) {
        p[0] = 1; p[1] = d->catalog_valid;
        memcpy(p + 2, d->catalog, 32); p[34] = CONTROL_VERSION;
        memcpy(p + 35, d->cfg.device_id, 8);
        d->catalog_dirty = false; d->control_sent = d->now;
        // 周期校验后重发最终状态，覆盖通知丢失，重复帧不重复翻页。
        // Refresh authoritative state after the heartbeat; duplicates never advance twice.
        d->state_dirty = d->orientation_dirty = true;
        return 43;
    }
    if (orientation_pending && (!reading_pending || !d->orientation_last)) {
        d->orientation_last = true;
        if (d->cfg.role == 1) {
            p[0] = 7; put32(p + 1, d->orientation_revision); put32(p + 5, d->orientation_sequence);
            p[9] = d->orientation.inverted | (d->orientation.primary_flip << 1) | (d->orientation.secondary_flip << 2);
            d->orientation_dirty = false; return 10;
        }
        if (d->orientation_request) {
            p[0] = 8; put32(p + 1, d->orientation_sequence); p[5] = d->orientation_request;
            d->orientation_dirty = false; return 6;
        }
    }
    if (d->annotation_count && d->control_compatible &&
        (!d->annotation_inflight || d->now - d->annotation_sent_at >= 750)) {
        const annotation_event_t* event = &d->annotations[d->annotation_head];
        size_t len = strnlen(event->text, sizeof(event->text) - 1);
        p[0] = 15; p[1] = event->type;
        put32(p + 2, event->seq); put32(p + 6, event->chapter);
        put32(p + 10, event->start); put32(p + 14, event->end);
        p[18] = (uint8_t)len; memcpy(p + 19, event->text, len);
        d->annotation_inflight = true; d->annotation_sent_at = d->now;
        return 19 + len;
    }
    if (d->cfg.role == 1 && d->endpoint_dirty && d->control_compatible) {
        size_t len = strnlen(d->cfg.host, sizeof(d->cfg.host) - 1);
        p[0] = ENDPOINT; p[1] = (uint8_t)len;
        memcpy(p + 2, d->cfg.host, len);
        d->endpoint_dirty = false;
        return 2 + len;
    }
    if (!control_ready(d)) return 0;
    if (d->cfg.role == 1 && d->state_dirty && nonzero(d->reading.book)) {
        p[0] = 2; memcpy(p + 1, d->reading.book, 32);
        put32(p + 33, d->reading.revision); put32(p + 37, d->reading.spread);
        put32(p + 41, d->reading.total); memcpy(p + 45, d->reading.font, 32);
        put32(p + 77, d->peer_sequence); p[81] = d->left_ready;
        p[82] = d->reading.font_px; p[83] = d->reading.reflow_pending; p[84] = d->reading.reflow_from_px;
        d->state_dirty = false; d->orientation_last = false; return 85;
    }
    if (d->guard_dirty && d->control_compatible) {
        p[0] = d->cfg.role == 1 ? 9 : 10;
        p[1] = d->guard ? 1 : 0;
        d->guard_dirty = false;
        return 2;
    }
    if (d->face_dirty && d->control_compatible) {
        p[0] = 11;
        p[1] = (uint8_t)(d->face + 1);
        d->face_dirty = false;
        d->face_sent = true;
        return 2;
    }
    if (d->base_dirty && d->control_compatible) {
        p[0] = 12; put32(p + 1, d->base_seq);
        put16(p + 5, d->base_x); put16(p + 7, d->base_y); put16(p + 9, d->base_z);
        d->base_dirty = false;
        return 11;
    }
    if (d->calibration_request && d->control_compatible) {
        p[0] = 13; put32(p + 1, ++d->base_seq);
        d->calibration_request = false;
        return 5;
    }
    if (d->reading_dirty && d->control_compatible) {
        p[0] = 14; p[1] = d->reading_active ? 1 : 0;
        d->reading_dirty = false;
        return 2;
    }
    if (d->cfg.role == 2 && d->request_pending &&
        (!d->request_at || d->now - d->request_at >= 500)) {
        p[0] = nonzero(d->request_book) ? 3 : d->request_page ? 5 : d->request_px ? 6 : 4;
        put32(p + 1, d->request_revision); put32(p + 5, d->request_sequence);
        d->request_at = d->now; d->orientation_last = false;
        if (p[0] == 3) { memcpy(p + 9, d->request_book, 32); put32(p + 41, d->request_page); return 45; }
        if (p[0] == 5) { put32(p + 9, d->request_page); return 13; }
        if (p[0] == 6) { p[9] = d->request_px; return 10; }
        p[9] = d->request_direction > 0 ? 1 : 255; return 10;
    }
    if (d->cfg.role == 1 && d->control_compatible && control_ready(d) &&
        (d->current_progress_dirty || d->progress_dirty || d->progress_at < d->count)) {
        uint8_t id[32] = {0};
        dl_book_progress_t progress = {0};
        if (d->current_progress_dirty && nonzero(d->reading.book)) {
            memcpy(id, d->reading.book, 32);
            progress = current_progress(d);
            d->current_progress_dirty = false;
        } else {
            while (d->progress_at < d->count) {
                local_t* item = &d->local[d->progress_at++];
                local_progress_load(d, item);
                if (!item->progress_loaded || !item->progress_started) continue;
                progress = (dl_book_progress_t){true, item->progress_pct, item->progress_recent};
                memcpy(id, item->id, 32);
                break;
            }
            if (!nonzero(id)) {
                d->progress_dirty = false;
                if (d->progress_at >= d->count) d->progress_at = 0;
            }
        }
        if (nonzero(id)) {
            p[0] = PROGRESS; memcpy(p + 1, id, 32);
            p[33] = progress.started ? 1 : 0; p[34] = progress.pct; put32(p + 35, progress.recent);
            if (d->progress_at >= d->count) d->progress_dirty = false;
            return 38;
        }
    }
    return 0;
}
bool dl_control_receive(dual_link_t* d, const uint8_t* p, size_t n) {
    if (!d || !p || !n || !d->control_connected || n > DL_CONTROL_MAX) return false;
    if (p[0] == ACK) return ack_consume(d, p, n);
    if (p[0] == 1) {
        if (n != 43 || p[1] > 1 || p[34] != CONTROL_VERSION) return false;
        d->control_compatible = true;
        d->peer_catalog_valid = p[1]; memcpy(d->peer_catalog, p + 2, 32);
        memcpy(d->peer_device_id, p + 35, 8); d->peer_id_valid = true;
        d->fast_verified = d->catalog_valid && d->peer_catalog_valid && !d->network_used &&
            same(d->catalog, d->peer_catalog, 32);
        return true;
    }
    if (p[0] == PROGRESS) {
        if (n != 38 || d->cfg.role != 2 || p[33] > 1 || p[34] > 100) return false;
        remote_progress_t* progress = progress_find(d, p + 1, true);
        if (!progress) return false;
        progress->started = p[33] != 0;
        progress->pct = p[34];
        progress->recent = get32(p + 35);
        ++d->progress_revision;
        return true;
    }
    if (p[0] == ENDPOINT) {
        if (d->cfg.role != 2 || n < 3 || n > 49 || p[1] != n - 2) return false;
        char host[48];
        memcpy(host, p + 2, p[1]); host[p[1]] = 0;
        struct in_addr addr;
        if (inet_pton(AF_INET, host, &addr) != 1) return false;
        if (strcmp(host, d->cfg.host)) {
            snprintf(d->cfg.host, sizeof(d->cfg.host), "%s", host);
            if (d->fd >= 0) drop(d, NULL);
            d->retry_at = d->now;
        }
        return true;
    }
    if (p[0] == 15) {
        if (n < 19 || p[18] > n - 19 || p[18] > sizeof(d->annotation_text) - 1) return false;
        d->annotation_seq = get32(p + 2);
        if (!d->annotation_seq || !ack_queue(d, 15, d->annotation_seq)) return false;
        d->annotation_type = p[1];
        d->annotation_chapter = get32(p + 6);
        d->annotation_start = get32(p + 10);
        d->annotation_end = get32(p + 14);
        memcpy(d->annotation_text, p + 19, p[18]);
        d->annotation_text[p[18]] = 0;
        d->annotation_valid = true;
        return true;
    }
    if (p[0] == 14) {
        if (n != 2 || p[1] > 1) return false;
        d->peer_reading = p[1] != 0;
        return true;
    }
    if (p[0] == 12) {
        if (n != 11 || d->cfg.role != 2) return false;
        uint32_t seq = get32(p + 1);
        if (!seq || (d->base_valid && (int32_t)(seq - d->base_seq) < 0)) return true;
        d->base_seq = seq; d->base_valid = true;
        d->base_x = get16(p + 5); d->base_y = get16(p + 7); d->base_z = get16(p + 9);
        return true;
    }
    if (p[0] == 13) {
        if (n != 5 || d->cfg.role != 1 || !get32(p + 1)) return false;
        d->calibration_request = true;
        return true;
    }
    if (p[0] == 11) {
        if (n != 2 || p[1] > 2) return false;
        d->peer_face = (int8_t)p[1] - 1;
        return true;
    }
    if (p[0] == 9 || p[0] == 10) {
        if (n != 2 || p[1] > 1) return false;
        if (p[0] == 9 && d->cfg.role != 2) return false;
        if (p[0] == 10 && d->cfg.role != 1) return false;
        d->guard = p[1] != 0;
        d->guard_dirty = p[0] == 10;
        return true;
    }
    if (p[0] == 7 || p[0] == 8) {
        if (n != (p[0] == 7 ? 10u : 6u) || !d->control_compatible || !get32(p + 1)) return false;
        uint32_t sequence = get32(p + 1);
        if (p[0] == 7) {
            if (d->cfg.role != 2 || p[9] > 7) return false;
            if (!d->orientation_seen || (int32_t)(sequence - d->orientation_revision) >= 0) {
                d->orientation = (dl_orientation_t){p[9] & 1, p[9] & 2, p[9] & 4};
                d->orientation_revision = sequence; d->orientation_seen = true;
            }
            if ((int32_t)(get32(p + 5) - d->orientation_sequence) >= 0) d->orientation_request = 0;
        } else {
            if (d->cfg.role != 1 || p[5] > 6 || !(p[5] & 3) || (p[5] & 3) == 3) return false;
            if ((int32_t)(sequence - d->orientation_sequence) > 0) {
                d->orientation_sequence = sequence;
                (void)dl_flip(d, p[5] & 3, (p[5] & 4) != 0);
            }
            d->orientation_dirty = true;
        }
        return true;
    }
    if (p[0] == 23) {
        if (n != 1) return false;
        d->game_close_ready = true;
        (void)ack_queue(d, 23, 1);
        return true;
    }
    if (p[0] == 22) {
        if (d->cfg.role != 2 || n < 7 || p[1] > n - 6 || p[1] > sizeof(d->game_payload) ||
            n != 6 + p[1] || !get32(p + 2 + p[1])) return false;
        uint32_t seq = get32(p + 2 + p[1]);
        if (!d->game_payload_rx_seen || seq != d->game_payload_rx_seq) {
            memcpy(d->game_payload, p + 2, p[1]);
            d->game_payload_n = p[1];
            d->game_payload_ready = true;
            d->game_payload_rx_seen = true;
            d->game_payload_rx_seq = seq;
        }
        (void)ack_queue(d, 22, seq);
        return true;
    }
    if (p[0] == 21 && n == 2) {
        if (d->cfg.role != 2) return false;
        d->game_open_kind = p[1]; d->game_open_ready = true;
        (void)ack_queue(d, 21, 1);
        return true;
    }
    if (p[0] == 20 && n == 2) {
        if (d->cfg.role != 1) return false;
        d->game_input = p[1]; d->game_ready = true;
        return true;
    }
    if (!control_ready(d)) return false;
    if (p[0] == 2 && d->cfg.role == 2) {
        if (n != 85 || p[81] > 1 || !valid_px(p[82]) || p[83] > 1 || (p[81] && p[83]) ||
            (p[83] && !valid_px(p[84])) ||
            !get32(p + 33) || get32(p + 37) % 2 ||
            find_id(d, p + 1) < 0 || (nonzero(p + 45) && find_id(d, p + 45) < 0)) return false;
        uint32_t revision = get32(p + 33);
        // 主机恢复也发送当前版本；同连接中的过期状态不能回滚。
        // Reject stale state within a connection; reconnect accepts the primary's restored revision.
        if (d->control_seen && (int32_t)(revision - d->reading.revision) < 0) return true;
        // 同版本的迟到准备帧不能撤销已收到的推屏确认。/ A delayed prepare cannot revoke the same revision's display confirmation.
        d->left_ready = p[81] || (d->control_seen && revision == d->reading.revision && d->left_ready);
        d->control_seen = true;
        memcpy(d->reading.book, p + 1, 32); memcpy(d->reading.font, p + 45, 32);
        d->reading.revision = revision; d->reading.spread = get32(p + 37);
        d->reading.total = get32(p + 41);
        d->reading.font_px = p[82]; d->reading.reflow_pending = p[83]; d->reading.reflow_from_px = p[84];
        if (d->request_pending) d->remote_open_pending = true;
        if (get32(p + 77) >= d->request_sequence) d->request_pending = false;
        return true;
    }
    if (d->cfg.role != 1 || (p[0] != 3 && p[0] != 4 && p[0] != 5 && p[0] != 6) ||
        n != (p[0] == 3 ? 45u : p[0] == 5 ? 13u : 10u) || !get32(p + 5)) return false;
    uint32_t seq = get32(p + 5);
    if (seq <= d->peer_sequence) { d->state_dirty = true; return true; }
    if (p[0] == 3 && (find_id(d, p + 9) < 0 || !get32(p + 41) || get32(p + 41) == UINT32_MAX)) return false;
    if (p[0] == 4 && p[9] != 1 && p[9] != 255) return false;
    if (p[0] == 5 && (!get32(p + 9) || get32(p + 9) == UINT32_MAX ||
        (d->reading.total && get32(p + 9) > d->reading.total))) return false;
    if (p[0] == 6 && !valid_px(p[9])) return false;
    d->peer_sequence = seq;
    if (get32(p + 1) == d->reading.revision) {
        if (p[0] == 3) {
            bool current = same(d->reading.book, p + 9, 32);
            uint32_t spread = (get32(p + 41) - 1) & ~1u;
            if (!current || (!d->reading.reflow_pending && !d->sending && !d->receiving &&
                (spread == d->reading.spread || d->left_ready))) {
                publish(d, current ? NULL : p + 9, 0); d->reading.spread = spread;
                d->remote_open_pending = true;
            }
        }
        else if (nonzero(d->reading.book) && (d->left_ready || !d->reading_active)) {
            if (p[0] == 5) { d->reading.spread = (get32(p + 9) - 1) & ~1u; publish(d, NULL, 0); }
            else if (p[0] == 6) { if (p[9] != d->reading.font_px) resize(d, p[9]); }
            else publish(d, NULL, p[9] == 1 ? 1 : -1);
        }
    }
    // 主机离架后由仍在阅读的从机接力；权威进度直接接受为该版本已呈现。
    // When the primary is away, accept the reading secondary's relayed page as the authoritative presented revision.
    if (d->cfg.role == 1 && !d->reading_active && !d->receiving && !d->sending)
        d->left_ready = true;
    d->state_dirty = true;
    return true;
}
void dl_status(const dual_link_t* d, dl_status_t* out) {
    memset(out, 0, sizeof(*out));
    if (!d) return;
    out->connected = d->control_connected;
    out->data_connected = d->authenticated;
    out->peer_id_valid = d->peer_id_valid;
    out->fast_verified = d->fast_verified;
    memcpy(out->peer_device_id, d->peer_device_id, sizeof(out->peer_device_id));
    memcpy(out->catalog, d->catalog, sizeof(out->catalog));
    out->request_pending = d->request_pending;
    memcpy(out->host, d->cfg.host, sizeof(out->host));
    out->scanning = d->scan;
    out->synced = control_ready(d);
    out->busy = d->scan || d->sending || d->receiving || d->request_pending ||
                (nonzero(d->reading.book) && !d->left_ready);
    out->left_ready = d->left_ready;
    out->books = d->count;
    out->peer_books = d->remote_count;
    out->imported = d->imports;
    out->received = d->received;
    out->expected = d->expected;
    out->content_revision = d->content_revision;
    out->sent = d->sent;
    out->reused = d->reused; out->verified_bytes = d->verified_bytes; out->resumed = d->resumed;
    out->listed = d->listed; out->peer_listed = d->peer_listed;
    out->done = d->done; out->peer_done = d->peer_done;
    out->receiving = d->receiving != NULL; out->sending = d->sending != NULL;
    out->catalog_valid = d->catalog_valid; out->peer_catalog_valid = d->peer_catalog_valid;
    out->error = d->error;
    out->reading = d->reading;
    out->orientation = d->orientation;
    out->guard = d->guard;
    out->face = d->face;
    out->peer_face = d->peer_face;
    out->base_valid = d->base_valid;
    out->base_x = d->base_x; out->base_y = d->base_y; out->base_z = d->base_z;
    out->base_seq = d->base_seq;
    out->calibration_request = d->calibration_request;
    out->peer_reading = d->peer_reading;
    out->annotation_valid = d->annotation_valid;
    out->annotation_seq = d->annotation_seq;
    out->annotation_type = d->annotation_type;
    out->annotation_chapter = d->annotation_chapter;
    out->annotation_start = d->annotation_start;
    out->annotation_end = d->annotation_end;
    memcpy(out->annotation_text, d->annotation_text, sizeof(out->annotation_text));
}
static bool open_request(dual_link_t* d, const char* path, uint32_t page, bool reopen, bool activate) {
    if (!d || !path || !page || page == UINT32_MAX || strlen(path) >= DL_PATH) return false;
    const char* extension = strrchr(path, '.');
    if (!extension || (strcasecmp(extension, ".txt") && strcasecmp(extension, ".epub")))
        return false;
    const char* current = dl_reading_path(d);
    if (current && !strcmp(current, path)) {
        if (!reopen && !activate) return dl_seek(d, page);
        if (!control_ready(d)) return false;
    }
    if (!control_ready(d)) {
        snprintf(d->pending_open, sizeof(d->pending_open), "%s", path);
        d->pending_open_page = reopen ? 0 : page;
        return true;
    }
    int at = -1;
    for (unsigned i = 0; i < d->count; i++)
        if (!strcmp(d->local[i].path, path)) {
            at = (int)i;
            break;
        }
    if (at < 0) {
        dl_rescan(d);
        snprintf(d->pending_open, sizeof(d->pending_open), "%s", path);
        d->pending_open_page = reopen ? 0 : page;
        return true;
    }
    bool same_book = same(d->reading.book, d->local[at].id, 32);
    if (same_book) {
        if (!reopen && !activate) return dl_seek(d, page);
        if (d->reading.reflow_pending || d->request_pending || d->sending || d->receiving) return false;
        if (reopen || activate) page = d->reading.spread + 1;
    }
    if (d->cfg.role == 1) {
        publish(d, same_book ? NULL : d->local[at].id, 0);
        d->reading.spread = (page - 1) & ~1u;
    } else {
        // 用户从书架换书必须覆盖尚未完成的旧请求；保留旧请求会让新书永远无法打开。
        // A user shelf switch supersedes an unfinished request; retaining it would make the new book impossible to open.
        d->request_pending = false;
        memcpy(d->request_book, d->local[at].id, 32);
        d->request_page = page;
        d->request_revision = d->reading.revision;
        ++d->request_sequence;
        d->request_pending = true;
        d->request_started = d->now;
        d->request_at = 0;
    }
    return true;
}
bool dl_open_path(dual_link_t* d, const char* path) {
    return open_request(d, path, 1, true, false);
}
bool dl_open_at(dual_link_t* d, const char* path, uint32_t page) {
    return open_request(d, path, page, false, false);
}
bool dl_activate_path(dual_link_t* d, const char* path) {
    return open_request(d, path, 1, false, true);
}
bool dl_game_close(dual_link_t* d) {
    if (!d || !d->control_connected) return false;
    d->game_close_pending = true; return true;
}
bool dl_game_close_take(dual_link_t* d) {
    if (!d || !d->game_close_ready) return false;
    d->game_close_ready = false; return true;
}
bool dl_game_packet(dual_link_t* d, const void* data, size_t bytes) {
    if (!d || !data || !bytes || bytes > sizeof(d->game_payload) || d->cfg.role != 1 || !d->control_connected ||
        d->game_payload_pending) return false;
    memcpy(d->game_payload, data, bytes); d->game_payload_n = (uint8_t)bytes; d->game_payload_pending = true;
    ++d->game_payload_seq;
    if (!d->game_payload_seq) d->game_payload_seq = 1;
    d->game_payload_inflight = false;
    return true;
}
size_t dl_game_packet_take(dual_link_t* d, void* data, size_t capacity) {
    if (!d || !data || d->cfg.role != 2 || !d->game_payload_ready || d->game_payload_n > capacity) return 0;
    memcpy(data, d->game_payload, d->game_payload_n); d->game_payload_ready = false;
    return d->game_payload_n;
}
bool dl_game_input(dual_link_t* d, uint8_t input) {
    if (!d || d->cfg.role != 2 || !d->control_connected) return false;
    d->game_input = input; d->game_pending = true;
    return true;
}
bool dl_game_take(dual_link_t* d, uint8_t* input) {
    if (!d || !input || d->cfg.role != 1 || !d->game_ready) return false;
    *input = d->game_input; d->game_ready = false;
    return true;
}
bool dl_game_open(dual_link_t* d, uint8_t kind) {
    if (!d || !d->control_connected) return false;
    d->game_open_kind = kind; d->game_open_pending = true;
    d->game_open_sent = 0; d->game_open_until = d->now + 3000;
    return true;
}
bool dl_game_open_take(dual_link_t* d, uint8_t* kind) {
    if (!d || !kind || !d->game_open_ready) return false;
    *kind = d->game_open_kind; d->game_open_ready = false;
    return true;
}
bool dl_turn(dual_link_t* d, int direction) {
    // 旧请求在章末/丢包时可能长期占住 request_pending；超过 1.5 秒允许新翻页重试。
    // A stale request at a chapter boundary or lost packet otherwise blocks every turn
    // for the full timeout; let a new turn retry after 1.5 seconds.
    if (d && d->request_pending && d->request_started && d->now - d->request_started > 1500)
        d->request_pending = false;
    if (!d || (direction != -1 && direction != 1) || !control_ready(d) ||
        !nonzero(d->reading.book) || d->request_pending || !d->left_ready || !d->peer_reading)
        return false;
    if (d->cfg.role == 1)
        publish(d, NULL, direction);
    else {
        memset(d->request_book, 0, 32);
        d->request_page = 0;
        d->request_px = 0;
        d->request_direction = direction;
        d->request_revision = d->reading.revision;
        ++d->request_sequence;
        d->request_pending = true;
        d->request_started = d->now;
        d->request_at = 0;
    }
    return true;
}
bool dl_seek(dual_link_t* d, uint32_t page) {
    // 旧请求在章末/丢包时可能长期占住 request_pending；超过 1.5 秒允许新翻页重试。
    // A stale request at a chapter boundary or lost packet otherwise blocks every turn
    // for the full timeout; let a new turn retry after 1.5 seconds.
    if (d && d->request_pending && d->request_started && d->now - d->request_started > 1500)
        d->request_pending = false;
    if (!d || !page || page == UINT32_MAX || !control_ready(d) ||
        (d->peer_reading && !d->left_ready) ||
        !nonzero(d->reading.book) || d->request_pending || (d->reading.total && page > d->reading.total))
        return false;
    if (d->cfg.role == 1) {
        d->reading.spread = (page - 1) & ~1u;
        publish(d, NULL, 0);
    } else {
        memset(d->request_book, 0, 32);
        d->request_page = page;
        d->request_px = 0;
        d->request_revision = d->reading.revision;
        ++d->request_sequence;
        d->request_pending = true; d->request_started = d->now; d->request_at = 0;
    }
    return true;
}
bool dl_resize(dual_link_t* d, uint8_t px) {
    if (!d || !valid_px(px) || px == d->reading.font_px || !control_ready(d) || !d->left_ready || !d->peer_reading ||
        !nonzero(d->reading.book) || d->request_pending) return false;
    if (d->cfg.role == 1) resize(d, px);
    else {
        memset(d->request_book, 0, 32); d->request_page = 0; d->request_px = px;
        d->request_revision = d->reading.revision; ++d->request_sequence;
        d->request_pending = true; d->request_started = d->now; d->request_at = 0;
    }
    return true;
}
bool dl_reflow_resolved(dual_link_t* d, uint32_t revision, uint32_t spread) {
    if (!d || d->cfg.role != 1 || !d->reading.reflow_pending || revision != d->reading.revision ||
        (spread & 1u) || spread > UINT32_MAX - 3) return false;
    d->reading.spread = spread; d->reading.reflow_pending = false;
    publish(d, NULL, 0);
    return true;
}
bool dl_reflow_failed(dual_link_t* d, uint32_t revision) {
    if (!d || !dl_reflow_resolved(d, revision, d->reading.spread)) return false;
    d->reading.font_px = d->reading.reflow_from_px;
    return true;
}
bool dl_presented(dual_link_t* d, uint32_t revision) {
    if (!d || d->cfg.role != 1 || !nonzero(d->reading.book) || d->reading.reflow_pending || revision != d->reading.revision) return false;
    if (!d->left_ready) { d->left_ready = true; d->state_dirty = true; }
    return true;
}
void dl_total(dual_link_t* d, uint32_t total) {
    if (!d || d->cfg.role != 1 || !total || d->reading.reflow_pending || d->reading.total == total) return;
    d->reading.total = total;
    if (d->reading.spread >= total) d->reading.spread = (total - 1) & ~1u;
    publish(d, NULL, 0);
    d->current_progress_dirty = true;
}
const char* dl_reading_path(const dual_link_t* d) {
    if (!d) return NULL;
    int i = find_id(d, d->reading.book);
    return i < 0 ? NULL : d->local[i].path;
}
const char* dl_font_path(const dual_link_t* d) {
    if (!d) return NULL;
    int i = find_id(d, d->reading.font);
    return i < 0 ? NULL : d->local[i].path;
}
bool dl_book_progress(const dual_link_t* d, const char* path, uint32_t bytes, dl_book_progress_t* out) {
    if (!d || !path || !out) return false;
    int local = -1;
    for (unsigned i = 0; i < DL_BOOKS; ++i)
        if (d->local[i].bytes == bytes && !strcmp(d->local[i].path, path)) { local = (int)i; break; }
    if (local < 0) return false;
    // 主机当前共享书优先使用权威页组；其余条目继续读取主机本地进度。
    // The primary's current shared book uses its authoritative spread; other rows keep primary-local progress.
    if (d->cfg.role == 1 && same(d->local[local].id, d->reading.book, 32)) {
        *out = current_progress(d);
        return true;
    }
    if (d->cfg.role == 1 && d->cfg.progress_get)
        return d->cfg.progress_get(path, bytes, out, d->cfg.progress_context);
    for (unsigned i = 0; i < DL_BOOKS; ++i)
        if (d->progress[i].valid && same(d->progress[i].id, d->local[local].id, 32)) {
            *out = (dl_book_progress_t){d->progress[i].started, d->progress[i].pct, d->progress[i].recent};
            return true;
        }
    return false;
}
uint32_t dl_progress_revision(const dual_link_t* d) {
    return d ? d->progress_revision : 0;
}
bool dl_remote_open_take(dual_link_t* d) {
    if (!d || !d->remote_open_pending) return false;
    d->remote_open_pending = false;
    return true;
}
bool dl_remote_open_pending(const dual_link_t* d) {
    return d && d->remote_open_pending;
}
void dl_endpoint(dual_link_t* d, const char* host) {
    if (!d || d->cfg.role != 1 || !host || !host[0] || strlen(host) >= sizeof(d->cfg.host)) return;
    struct in_addr addr;
    if (inet_pton(AF_INET, host, &addr) != 1 || !strcmp(host, d->cfg.host)) return;
    snprintf(d->cfg.host, sizeof(d->cfg.host), "%s", host);
    d->endpoint_dirty = true;
}
const char* dl_entry(const dual_link_t* d, unsigned index, uint8_t id[32], uint32_t* bytes) {
    if (!d || d->scan || d->retry_at == INT64_MAX || index >= d->count) return NULL;
    if (id) memcpy(id, d->local[index].id, 32);
    if (bytes) *bytes = d->local[index].bytes;
    return d->local[index].path;
}
void dl_rescan(dual_link_t* d) {
    if (!d) return;
    drop(d, NULL);
    close_file(&d->hash_file);
    hash_abort(&d->scan_hash);
    if (d->dir) {
        closedir(d->dir);
        d->dir = NULL;
    }
    d->catalog_valid = false; d->catalog_dirty = true; d->request_pending = false;
    d->network_used = false; d->fast_verified = false; d->catalog_ready_at = 0;
    for (unsigned i = 0; i < d->count; i++) d->local[i].seen = false;
    d->root_at = d->reused = d->verified_bytes = 0;
    memset(d->font_id, 0, sizeof(d->font_id));
    memset(d->peer_font_id, 0, sizeof(d->peer_font_id));
    d->font_scanned = false;
    d->scan = true;
    d->retry_at = 0;
}
void dl_restore(dual_link_t* d, const dl_reading_t* s) {
    if (d && s && d->cfg.role == 1 && s->revision && s->spread % 2 == 0 && !s->reflow_pending) {
        d->reading = *s;
        if (!valid_px(d->reading.font_px)) d->reading.font_px = DL_FONT_DEFAULT;
        d->reading.reflow_pending = false;
        d->left_ready = false;
        d->state_dirty = true;
    }
}
void dl_orientation_restore(dual_link_t* d, const dl_orientation_t* orientation) {
    if (!d || !orientation || !memcmp(&d->orientation, orientation, sizeof(*orientation))) return;
    d->orientation = *orientation;
    if (++d->orientation_revision == 0) d->orientation_revision = 1;
    d->orientation_dirty = true;
}
bool dl_orient(dual_link_t* d, bool inverted) {
    if (!d || d->cfg.role != 1) return false;
    dl_orientation_t next = d->orientation; next.inverted = inverted;
    dl_orientation_restore(d, &next); return true;
}
bool dl_face(dual_link_t* d, int face) {
    if (!d || face < -1 || face > 1) return false;
    if (d->face != face || !d->face_sent) {
        d->face = (int8_t)face;
        d->face_dirty = true;
    }
    return true;
}
bool dl_baseline(dual_link_t* d, int16_t x, int16_t y, int16_t z) {
    if (!d || d->cfg.role != 1) return false;
    d->base_x = x; d->base_y = y; d->base_z = z;
    d->base_seq = d->base_seq == UINT32_MAX ? 1 : d->base_seq + 1;
    d->base_valid = true; d->base_dirty = true;
    return true;
}
bool dl_calibration_request(dual_link_t* d) {
    if (!d || d->cfg.role != 2) return false;
    d->calibration_request = true;
    return true;
}
void dl_calibration_clear(dual_link_t* d) {
    if (d) d->calibration_request = false;
}
bool dl_annotation(dual_link_t* d, uint8_t type, uint32_t chapter, uint32_t start, uint32_t end, const char* text) {
    if (!d || type > 2 || !text || d->annotation_count >= ANNOTATION_OUT_MAX) return false;
    annotation_event_t* event = &d->annotations[(d->annotation_head + d->annotation_count) % ANNOTATION_OUT_MAX];
    memset(event, 0, sizeof(*event));
    event->type = type;
    event->chapter = chapter;
    event->start = start;
    event->end = end;
    size_t len = strlen(text);
    if (len > DL_CONTROL_MAX - 19) {
        len = DL_CONTROL_MAX - 19;
        while (len && ((unsigned char)text[len] & 0xc0) == 0x80) --len;
    }
    memcpy(event->text, text, len);
    event->text[len] = 0;
    if (++d->annotation_seq == 0) d->annotation_seq = 1;
    event->seq = d->annotation_seq;
    ++d->annotation_count;
    d->annotation_dirty = true;
    return true;
}
bool dl_reading_active(dual_link_t* d, bool active) {
    if (!d) return false;
    if (d->reading_active != active) {
        d->reading_active = active;
        d->reading_dirty = true;
    }
    return true;
}
bool dl_guard(dual_link_t* d, bool enabled) {
    if (!d) return false;
    if (d->guard == enabled) return true;
    d->guard = enabled;
    d->guard_dirty = true;
    return true;
}
bool dl_flip(dual_link_t* d, int role, bool enabled) {
    if (!d || (role != 1 && role != 2) || (d->cfg.role == 2 &&
        (!d->control_connected || !d->control_compatible || !d->orientation_seen))) return false;
    uint8_t request = role | (enabled ? 4 : 0);
    if (d->orientation_request) return d->orientation_request == request;
    dl_orientation_t next = d->orientation;
    bool* value = role == 1 ? &next.primary_flip : &next.secondary_flip;
    if (*value == enabled) return true;
    if (d->cfg.role == 1) { *value = enabled; dl_orientation_restore(d, &next); }
    else {
        d->orientation_request = request;
        if (++d->orientation_sequence == 0) d->orientation_sequence = 1;
        d->orientation_dirty = true;
    }
    return true;
}

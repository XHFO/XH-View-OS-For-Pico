/*
 * SPDX-FileCopyrightText: 2026 XH Lab
 * SPDX-License-Identifier: Apache-2.0
 * 中文：家庭连接 TF 工具包（kind 130）；经宿主 ABI 监看 HA/打印机，含 Cloudflare Access。
 * English: Home-link TF package (kind 130); monitors HA/printers via host ABI, including Cloudflare Access.
 * 手机扫码写入与 Mac 管理页共用 PGA 存储。门户开着时本机与 Mac 都不保存，避免抢写。
 * The phone portal and the Mac manage page share the PGA store. While the portal is open, neither the device keyboard nor Mac saves.
 */
#include "pico_app_sdk.h"
#include "../../main/system/pico_scroll.h"

enum { HOME_LINK_KIND = 130, HOME_LINK_VERSION = 10106 };
enum { PAGE_MONITOR, PAGE_SETUP, PAGE_PRINTERS };

static const pico_app_host_t *host;
static unsigned page, entity_page, setup_field, last_revision;
_Static_assert(PGA_PRESS == 0 && PGA_MOVE == 1 && PGA_RELEASE == 2 && PGA_TAP == 3 && PGA_LONG == 4 &&
                   PGA_SWIPE_L == 5 && PGA_SWIPE_R == 6 && PGA_SWIPE_U == 7 && PGA_SWIPE_D == 8 && PGA_CANCEL == 9,
               "pico_scroll.h event numbers");
static ps_scroll_track_t entity_scroll;
static bool waiting_input, need_sync;
static pico_app_home_status_t status;
static pico_app_home_config_t config;
static char draft[256];
static int draft_kind; /* 0 local 1 external 2 token 3 cf_id 4 cf_secret 5 wifi */

/* 卡片面板：类型筛选与拖拽顺序按实体 id 哈希保存，同步后索引变化不会打乱。
 * Card panel: type filter and drag order persist by entity-id hash, so sync reindexing is safe. */
#define HLC_MAX 64u
#define HLC_UI_MAGIC 0x484C4331u
typedef struct {
    uint32_t magic, version;
    uint8_t type_filter;
    uint8_t order_count;
    uint16_t reserved;
    uint32_t order[HLC_MAX];
} hlc_ui_t;
typedef struct {
    uint32_t hash;
    char id[96], name[64], state[48], unit[12], room[32];
    uint8_t kind, available, on;
    float number;
} hlc_ent_t;
static hlc_ui_t cards;
static hlc_ent_t cache[HLC_MAX];
static unsigned cache_count;
static uint32_t cache_revision;
static int drag_from = -1, drag_to = -1;
static bool drag_suppress_tap;

__attribute__((visibility("hidden"))) void *memcpy(void *dst, const void *src, size_t n) {
    return host->copy(dst, src, n);
}
__attribute__((visibility("hidden"))) void *memset(void *dst, int value, size_t n) {
    return host->set(dst, value, n);
}

static bool has_home(void) {
    return host->bytes >= (uint32_t)(offsetof(pico_app_host_t, home_link_input) + sizeof(void *)) &&
           host->home_link && host->home_link_config && host->home_link_entity && host->home_link_printer &&
           host->home_link_control && host->home_link_input;
}


static bool hl_same(const char *a, const char *b);
static const char *hl(const char *key, const char *zh);

static uint32_t card_hash(const char *s) {
    uint32_t h = 2166136261u;
    while (s && *s) { h ^= (uint8_t)*s++; h *= 16777619u; }
    return h ? h : 1u;
}

static void cards_load(void) {
    host->set(&cards, 0, sizeof(cards));
    cards.magic = HLC_UI_MAGIC;
    cards.version = 1;
    cards.type_filter = 0;
    if (host->load && host->load("homecards1", &cards, sizeof(cards)) &&
        cards.magic == HLC_UI_MAGIC && cards.version == 1) {
        cards.type_filter &= 0x3fu;
        if (cards.order_count > HLC_MAX) cards.order_count = (uint8_t)HLC_MAX;
    } else {
        host->set(&cards, 0, sizeof(cards));
        cards.magic = HLC_UI_MAGIC;
        cards.version = 1;
    }
}

static void cards_save(void) {
    if (host->store) (void)host->store("homecards1", &cards, sizeof(cards));
}

// 实体缓存只在状态修订号变化时读取；render 不调用宿主服务。
// The entity cache is filled only when the status revision changes; render calls no host service.
static void refresh_cache(void) {
    unsigned count = 0;
    if (has_home()) {
        unsigned total = status.entity_count;
        if (total > HLC_MAX) total = HLC_MAX;
        for (unsigned i = 0; i < total; i++) {
            pico_app_home_entity_t e = {.bytes = sizeof(e)};
            if (!host->home_link_entity(i, &e)) break;
            hlc_ent_t *d = &cache[count];
            host->set(d, 0, sizeof(*d));
            d->hash = card_hash(e.id);
            host->copy(d->id, e.id, sizeof(d->id));
            host->copy(d->name, e.name, sizeof(d->name));
            host->copy(d->state, e.state, sizeof(d->state));
            host->copy(d->unit, e.unit, sizeof(d->unit));
            host->copy(d->room, e.room, sizeof(d->room));
            d->id[sizeof(d->id) - 1] = 0;
            d->name[sizeof(d->name) - 1] = 0;
            d->state[sizeof(d->state) - 1] = 0;
            d->unit[sizeof(d->unit) - 1] = 0;
            d->room[sizeof(d->room) - 1] = 0;
            d->kind = e.kind;
            d->available = e.available;
            d->on = e.on;
            d->number = e.number;
            count++;
        }
    }
    cache_count = count;
    cache_revision = status.revision;
}

static int order_rank(uint32_t hash) {
    for (unsigned i = 0; i < cards.order_count; i++)
        if (cards.order[i] == hash) return (int)i;
    return -1;
}

// 已知顺序在前，未知（新同步实体）按宿主索引排在后面，首次拖拽时才写入完整顺序。
// Known order first; new entities keep host order at the end until the first drag writes the full list.
static void sorted_all(int *out, int *count) {
    int n = (int)cache_count;
    for (int i = 0; i < n; i++) out[i] = i;
    for (int i = 1; i < n; i++) {
        int key = out[i];
        int key_rank = order_rank(cache[key].hash);
        if (key_rank < 0) key_rank = (int)HLC_MAX;
        int j = i - 1;
        while (j >= 0) {
            int prior_rank = order_rank(cache[out[j]].hash);
            if (prior_rank < 0) prior_rank = (int)HLC_MAX;
            bool after = prior_rank > key_rank || (prior_rank == key_rank && out[j] > key);
            if (!after) break;
            out[j + 1] = out[j];
            j--;
        }
        out[j + 1] = key;
    }
    *count = n;
}

static void build_display(int *out, int *count, bool all) {
    int all_idx[HLC_MAX], n = 0;
    sorted_all(all_idx, &n);
    int k = 0;
    for (int i = 0; i < n; i++) {
        int idx = all_idx[i];
        if (!all && cards.type_filter && !(cards.type_filter & (1u << cache[idx].kind))) continue;
        out[k++] = idx;
    }
    *count = k;
}

static const char *state_label(const char *state) {
    if (!state || !*state) return "";
    if (hl_same(state, "on")) return "开启";
    if (hl_same(state, "off")) return "关闭";
    if (hl_same(state, "cool")) return "制冷";
    if (hl_same(state, "heat")) return "制热";
    if (hl_same(state, "auto")) return "自动";
    if (hl_same(state, "dry")) return "除湿";
    if (hl_same(state, "fan") || hl_same(state, "fan_only")) return "送风";
    if (hl_same(state, "idle")) return "待机";
    if (hl_same(state, "unavailable")) return "不可用";
    return state;
}

static const char *const card_type_names[] = {"灯光", "开关", "空调", "风扇", "温度", "湿度"};
static const char *const card_glyphs[] = {"灯", "开", "空", "风", "温", "湿"};
static const struct { const char *label; uint8_t mask; } card_chips[] = {
    {"全部", 0}, {"灯光", 1u << 0}, {"空调", 1u << 2}, {"风扇", 1u << 3},
    {"开关", 1u << 1}, {"传感", (1u << 4) | (1u << 5)},
};
#define CARD_CHIP_COUNT ((int)(sizeof(card_chips) / sizeof(card_chips[0])))

static int card_columns(pico_app_view_t v) { return v.landscape ? 3 : 2; }
static int card_rows(pico_app_view_t v) { return v.landscape ? 2 : 3; }
static int cards_per_page(pico_app_view_t v) { return card_columns(v) * card_rows(v); }
static int card_top(pico_app_view_t v) { return v.landscape ? 150 : 160; }

static void card_rect(pico_app_view_t v, int index, int *x, int *y, int *w, int *h) {
    int cols = card_columns(v), rows = card_rows(v), margin = 14, gap = 10, bottom = 64;
    int width = (v.w - 2 * margin - gap * (cols - 1)) / cols;
    int height = (v.h - card_top(v) - bottom - gap * (rows - 1)) / rows;
    *x = margin + (index % cols) * (width + gap);
    *y = card_top(v) + (index / cols) * (height + gap);
    *w = width;
    *h = height;
}

static void chip_rect(pico_app_view_t v, int index, int *x, int *y, int *w, int *h) {
    int gap = 6, width = (v.w - 28 - gap * (CARD_CHIP_COUNT - 1)) / CARD_CHIP_COUNT;
    *x = 14 + index * (width + gap);
    *y = v.landscape ? 108 : 120;
    *w = width;
    *h = 30;
}

static bool chip_selected(int i) {
    uint8_t mask = card_chips[i].mask;
    if (!mask) return cards.type_filter == 0;
    return (cards.type_filter & mask) == mask;
}

static void draw_header(uint8_t *fb, int visible, int pages) {
    pico_app_view_t v = host->view();
    char line[192];
    const char *sub = status.busy ? "同步中…" :
                      status.at_home ? "本地" : status.external ? "外部" : "";
    host->heading(fb, hl("settings.home_link.monitor", "家庭连接"), sub);
    host->button(fb, v.w - 88, 24, 72, 36, "返回", false);
    host->format(line, sizeof(line), "%s实体 %u/%u · Wi-Fi %s · %d/%d 页 · 长按卡片拖动排序",
                 status.error[0] ? status.error : "", (unsigned)visible, (unsigned)status.entity_count,
                 (status.flags & PGA_HOME_WIFI) ? "已连接" : "未连接",
                 pages > 0 ? (entity_page + 1) : 0, pages);
    host->wrap(fb, 14, 92, v.w - 28, 12, 2, line, 5);
    for (int i = 0; i < CARD_CHIP_COUNT; i++) {
        int x, y, w, h;
        chip_rect(v, i, &x, &y, &w, &h);
        host->button(fb, x, y, w, h, card_chips[i].label, chip_selected(i));
    }
}

static void draw_card(uint8_t *fb, int x, int y, int w, int h, const hlc_ent_t *e, int display_index) {
    bool avail = e->available;
    bool on = avail && e->on && e->kind < 4;
    bool sensor = e->kind >= 4;
    int kind = e->kind < 6 ? e->kind : 0;
    int bs = h / 5;
    if (bs > 54) bs = 54;
    if (bs < 38) bs = 38;
    int bx = x + 14, by = y + 14;

    host->fill(fb, x + 3, y + 4, w, h, 12);
    host->box(fb, x, y, w, h, 12, avail ? (on ? 13 : 15) : 14, true);
    host->box(fb, x, y, w, h, 12, avail ? 0 : 9, false);
    if (on) host->fill(fb, x + 12, y + 1, w - 24, 4, 2);

    // 灯光/风扇开启时用同心浅灰光环表现“正在工作”，关闭时只留深色徽章。
    // Lights/fans get a concentric glow when on; off keeps the plain dark badge.
    if (on && (kind == 0 || kind == 3)) {
        host->box(fb, bx - 8, by - 8, bs + 16, bs + 16, (bs + 16) / 2, 15, true);
        host->box(fb, bx - 4, by - 4, bs + 8, bs + 8, (bs + 8) / 2, 14, true);
    }
    host->box(fb, bx, by, bs, bs, bs / 2, on ? 1 : 13, true);
    host->box(fb, bx, by, bs, bs, bs / 2, on ? 0 : 11, false);
    host->center(fb, bx + bs / 2, by + bs / 2 - 11, bs / 2 - 4, card_glyphs[kind], on ? 15 : 2);
    int tx = bx + bs + 8;
    if (tx > x + w - 76) tx = x + 14;
    host->text(fb, tx, by + 1, 12, card_type_names[kind], 5);
    host->wrap(fb, tx, by + 18, x + w - tx - 14, 15, 2, e->name[0] ? e->name : e->id, 0);

    char big[48] = "--", small[96] = "";
    if (!avail) {
        host->format(big, sizeof(big), "不可用");
    } else if (kind == 0) {
        host->format(big, sizeof(big), "%s", on ? "开灯" : "关灯");
        host->format(small, sizeof(small), "%s", state_label(e->state));
    } else if (kind == 1) {
        host->format(big, sizeof(big), "%s", on ? "开启" : "关闭");
        host->format(small, sizeof(small), "%s", state_label(e->state));
    } else if (kind == 2) {
        if (e->number > 0) host->format(big, sizeof(big), "%.0f°", e->number);
        else host->format(big, sizeof(big), "--°");
        host->format(small, sizeof(small), "%s · 设定温度", state_label(e->state));
    } else if (kind == 3) {
        host->format(big, sizeof(big), "%s", on ? "送风" : "停止");
        if (e->number > 0 && e->number <= 100)
            host->format(small, sizeof(small), "%s · %.0f%%", state_label(e->state), e->number);
        else
            host->format(small, sizeof(small), "%s", state_label(e->state));
    } else {
        if (e->number != 0) host->format(big, sizeof(big), "%.1f", e->number);
        else host->format(big, sizeof(big), "%s", e->state[0] ? e->state : "--");
        host->format(small, sizeof(small), "%s%s", e->unit[0] ? e->unit : "", sensor ? " · 只读" : "");
    }
    int big_y = y + h * 54 / 100;
    host->center(fb, x + w / 2, big_y, 36, big, avail ? 0 : 5);
    host->center(fb, x + w / 2, big_y + 43, 12, small, 5);

    int room_x = (kind == 2 && avail) ? x + 94 : x + 14;
    host->text(fb, room_x, y + h - 27, 11, e->room[0] ? e->room : "未分组", 5);
    if (avail && !sensor)
        host->button(fb, x + w - 94, y + h - 38, 80, 30, on ? "关闭" : "开启", on);
    else if (!avail)
        host->text(fb, x + w - 72, y + h - 30, 11, "不可用", 5);

    if (kind == 2 && avail) {
        host->button(fb, x + 14, y + h - 40, 34, 30, "-", false);
        host->button(fb, x + 52, y + h - 40, 34, 30, "+", false);
    }

    if (display_index == drag_from) {
        host->box(fb, x - 3, y - 3, w + 6, h + 6, 14, 2, false);
        host->box(fb, x - 7, y - 7, w + 14, h + 14, 16, 8, false);
    } else if (display_index == drag_to) {
        host->box(fb, x - 3, y - 3, w + 6, h + 6, 14, 1, false);
        host->box(fb, x - 7, y - 7, w + 14, h + 14, 16, 6, false);
    }
}

static int card_at(pico_app_view_t v, int px, int py) {
    int disp[HLC_MAX], n = 0;
    build_display(disp, &n, false);
    int per = cards_per_page(v), start = entity_page * per;
    for (int i = 0; i < per && start + i < n; i++) {
        int x, y, w, h;
        card_rect(v, i, &x, &y, &w, &h);
        if (px >= x && px < x + w && py >= y && py < y + h) return start + i;
    }
    return -1;
}

static void move_hash(uint32_t from_hash, uint32_t to_hash) {
    int all_idx[HLC_MAX], n = 0;
    sorted_all(all_idx, &n);
    int from = -1, to = -1;
    for (int i = 0; i < n; i++) {
        if (cache[all_idx[i]].hash == from_hash) from = i;
        if (cache[all_idx[i]].hash == to_hash) to = i;
    }
    if (from < 0 || to < 0 || from == to) return;
    int moved = all_idx[from];
    if (from < to)
        for (int i = from; i < to; i++) all_idx[i] = all_idx[i + 1];
    else
        for (int i = from; i > to; i--) all_idx[i] = all_idx[i - 1];
    all_idx[to] = moved;
    cards.order_count = (uint8_t)n;
    for (int i = 0; i < n; i++) cards.order[i] = cache[all_idx[i]].hash;
    cards_save();
}

static void commit_drag(void) {
    int from = drag_from, to = drag_to;
    drag_from = drag_to = -1;
    drag_suppress_tap = true;
    if (from >= 0 && to >= 0 && from != to) {
        int disp[HLC_MAX], n = 0;
        build_display(disp, &n, false);
        if (from < n && to < n) {
            move_hash(cache[disp[from]].hash, cache[disp[to]].hash);
            host->toast("卡片顺序已保存");
        }
    }
    host->dirty();
}

// 长按卡片开始拖拽；目标卡片实时加粗，松手才提交，避免墨水屏每帧全刷。
// Long-press a card to drag; the drop target outlines live and the order commits on release.
static bool monitor_drag_event(const pico_app_event_t *e) {
    if (page != PAGE_MONITOR || !has_home()) return false;
    if (e->type == PGA_LONG) {
        if (drag_from >= 0) return true;
        int slot = card_at(host->view(), e->x, e->y);
        if (slot < 0) return false;
        drag_from = drag_to = slot;
        drag_suppress_tap = false;
        host->toast("拖动到目标卡片后松手");
        host->dirty();
        return true;
    }
    if (drag_from < 0) return false;
    if (e->type == PGA_MOVE) {
        int slot = card_at(host->view(), e->x, e->y);
        if (slot >= 0 && slot != drag_to) {
            drag_to = slot;
            host->dirty();
        }
        return true;
    }
    if (e->type == PGA_RELEASE) {
        commit_drag();
        return true;
    }
    if (e->type == PGA_CANCEL) {
        drag_from = drag_to = -1;
        drag_suppress_tap = true;
        host->dirty();
        return true;
    }
    return false;
}

static bool hl_same(const char *a, const char *b) {
    if (!a || !b) return a == b;
    while (*a && *a == *b) { a++; b++; }
    return *a == *b;
}

static const char *hl(const char *key, const char *zh) {
    if (host && host->bytes >= (uint32_t)(offsetof(pico_app_host_t, tr) + sizeof(host->tr)) && host->tr) {
        const char *s = host->tr(key);
        if (s && !hl_same(s, key)) return s;
    }
    return zh;
}

static void refresh_status(void) {
    if (!has_home()) return;
    unsigned flags = status.flags, busy = status.busy, at_home = status.at_home, result = status.result;
    status.bytes = sizeof(status);
    (void)host->home_link(PGA_HOME_STATUS, &status);
    if (!(flags & PGA_HOME_WIFI) && (status.flags & PGA_HOME_WIFI) && (status.flags & PGA_HOME_CFG))
        need_sync = true;
    if (status.revision != last_revision || status.flags != flags || status.busy != busy ||
        status.at_home != at_home || status.result != result) {
        last_revision = status.revision;
        refresh_cache();
        host->dirty();
    }
}

static void load_config(void) {
    if (!has_home()) return;
    config.bytes = sizeof(config);
    (void)host->home_link_config(PGA_HOME_CONFIG_GET, &config);
}

static void request_sync(void) {
    if (!has_home()) return;
    status.bytes = sizeof(status);
    (void)host->home_link(PGA_HOME_SYNC, &status);
    need_sync = false;
    host->dirty();
}

/* Host arms this once via PGA_HOME_ENTRY. Old hosts return status without ENTRY_OK; ignore them. */
static void take_entry(void) {
    if (!has_home()) return;
    pico_app_home_status_t entry;
    host->set(&entry, 0, sizeof(entry));
    entry.bytes = sizeof(entry);
    if (!host->home_link(PGA_HOME_ENTRY, &entry) || !(entry.flags & PGA_HOME_ENTRY_OK) || entry.result > PAGE_PRINTERS)
        return;
    if (page == entry.result) return;
    page = entry.result;
    if (page == PAGE_SETUP) load_config();
    host->dirty();
}

static bool save_config(void) {
    if (!has_home()) return false;
    status.bytes = sizeof(status);
    (void)host->home_link(PGA_HOME_STATUS, &status);
    if (status.flags & PGA_HOME_PORTAL) {
        host->toast("手机正在填写，本机暂不保存");
        return false;
    }
    if (status.flags & PGA_HOME_MANAGE) {
        host->toast("请先关闭电脑管理会话");
        return false;
    }
    // 宿主会清空提交参数中的凭据；只提交副本，失败后仍可用当前草稿重试。
    // The host wipes submitted credentials, including on failure. Keep the editing draft for retries.
    pico_app_home_config_t submitted;
    host->copy(&submitted, &config, sizeof(submitted));
    submitted.bytes = sizeof(submitted);
    bool ok = host->home_link_config(PGA_HOME_CONFIG_SET, &submitted);
    host->set(&submitted, 0, sizeof(submitted));
    if (ok) {
        host->toast(hl("settings.home_link.saved_toast", "已保存到设备"));
        need_sync = true;
        load_config();
        request_sync();
    } else host->toast(hl("settings.home_link.save_failed", "保存失败（检查地址/令牌）"));
    return ok;
}

static void open_input(int kind, const char *seed, const char *title_unused) {
    (void)title_unused;
    if (!has_home() || !host->home_link_input) {
        host->toast(hl("settings.home_link.no_input", "宿主不支持输入"));
        return;
    }
    draft_kind = kind;
    host->format(draft, sizeof(draft), "%s", seed ? seed : "");
    waiting_input = host->home_link_input(PGA_HOME_INPUT_OPEN, draft, sizeof(draft));
    if (!waiting_input) host->toast(hl("settings.home_link.input_fail", "无法打开输入"));
}

static void take_input(void) {
    if (!waiting_input || !has_home()) return;
    if (!host->home_link_input(PGA_HOME_INPUT_TAKE, draft, sizeof(draft))) return;
    waiting_input = false;
    // 同一编辑会话可依次填写多个字段，不要用已保存的脱敏配置覆盖前一个草稿。
    // Preserve every field edited in this session instead of reloading the redacted stored config.
    if (draft_kind == 0) host->format(config.local, sizeof(config.local), "%s", draft);
    else if (draft_kind == 1) host->format(config.external, sizeof(config.external), "%s", draft);
    else if (draft_kind == 2) host->format(config.ha_token, sizeof(config.ha_token), "%s", draft);
    else if (draft_kind == 3) host->format(config.cf_access_client_id, sizeof(config.cf_access_client_id), "%s", draft);
    else if (draft_kind == 4)
        host->format(config.cf_access_client_secret, sizeof(config.cf_access_client_secret), "%s", draft);
    else if (draft_kind == 5) host->format(config.home_wifi, sizeof(config.home_wifi), "%s", draft);
    host->set(draft, 0, sizeof(draft));
    host->dirty();
}

static bool start(const pico_app_host_t *h) {
    if (!h || h->abi != PICO_APP_ABI || h->bytes < PICO_APP_HOST_V1_BYTES || !h->view || !h->text || !h->wrap ||
        !h->heading || !h->button || !h->fill || !h->toast || !h->navigate || !h->copy || !h->set || !h->format)
        return false;
    host = h;
    page = PAGE_MONITOR;
    entity_page = setup_field = 0;
    host->set(&entity_scroll, 0, sizeof(entity_scroll));
    waiting_input = need_sync = false;
    last_revision = 0;
    host->set(&status, 0, sizeof(status));
    host->set(&config, 0, sizeof(config));
    cards_load();
    drag_from = drag_to = -1;
    drag_suppress_tap = false;
    cache_count = 0;
    cache_revision = 0;
    if (has_home()) {
        load_config();
        refresh_status();
        refresh_cache();
        take_entry();
        if (status.flags & PGA_HOME_CFG) need_sync = true;
    }
    return true;
}

static void render_monitor(uint8_t *fb) {
    pico_app_view_t v = host->view();
    host->fill(fb, 0, 0, v.w, v.h, 15);
    if (!has_home()) {
        host->heading(fb, hl("settings.home_link.monitor", "家庭连接"), NULL);
        host->button(fb, v.w - 88, 24, 72, 36, "返回", false);
        host->wrap(fb, 24, 120, v.w - 48, 14, 6,
                   "当前固件宿主过旧，缺少 home_link 服务。请刷含 PGA 桥的固件后再打开本工具包。", 5);
        host->button(fb, 16, v.h - 56, v.w - 32, 40, "返回", false);
        return;
    }

    int disp[HLC_MAX], n = 0;
    build_display(disp, &n, false);
    int per = cards_per_page(v);
    int pages = n ? (n + per - 1) / per : 1;
    if (entity_page >= (unsigned)pages) entity_page = (unsigned)(pages - 1);
    draw_header(fb, n, pages);

    int start = (int)(entity_page * (unsigned)per);
    for (int i = 0; i < per && start + i < n; i++) {
        int x, y, w, h;
        card_rect(v, i, &x, &y, &w, &h);
        draw_card(fb, x, y, w, h, &cache[disp[start + i]], start + i);
    }
    if (!(status.flags & PGA_HOME_CFG)) {
        host->wrap(fb, 24, 210, v.w - 48, 14, 3,
                   "尚未配置。点「设置」填写本地/外部地址、HA 令牌与可选 Access。", 5);
    } else if (!n) {
        if (cards.type_filter)
            host->wrap(fb, 24, 210, v.w - 48, 14, 3, "当前筛选没有设备。点上方「全部」恢复显示。", 5);
        else
            host->wrap(fb, 24, 210, v.w - 48, 14, 3, "尚未同步到可显示的设备。请检查 HA 连接后点「同步」。", 5);
    }

    int bw = (v.w - 48) / 3;
    host->button(fb, 16, v.h - 56, bw, 40, "设置", false);
    host->button(fb, 24 + bw, v.h - 56, bw, 40, status.busy ? "…" : "同步", status.busy);
    host->button(fb, 32 + 2 * bw, v.h - 56, bw, 40, "打印机", false);
}

static void render_setup(uint8_t *fb) {
    pico_app_view_t v = host->view();
    host->fill(fb, 0, 0, v.w, v.h, 15);
    host->heading(fb, hl("settings.home_link.setup_title", "连接设置"), hl("settings.home_link.setup_sub", "设备本地保存"));
    host->wrap(fb, 24, 100, v.w - 48, 12, 3,
               hl("settings.home_link.setup_body_portal", "点「手机填写」在局域网扫码。电脑管理页可稍后读取同一份配置，两边不要同时保存。"), 5);
    char line[192];
    int columns = v.landscape ? 2 : 1;
    int field_width = (v.w - 32 - (columns - 1) * 8) / columns;
    const char *labels[] = {
        hl("settings.home_link.field.local", "本地地址"), hl("settings.home_link.field.external", "外部 HTTPS"),
        hl("settings.home_link.field.token", "HA 令牌"), hl("settings.home_link.field.cf_id", "CF Client ID"),
        hl("settings.home_link.field.cf_secret", "CF Client Secret"), hl("settings.home_link.field.wifi", "家庭 Wi‑Fi")};
    const char *unset = hl("settings.home_link.unset", "（未设）");
    const char *saved = hl("settings.home_link.saved", "已保存");
    for (int i = 0; i < 6; i++) {
        host->format(line, sizeof(line), hl("settings.home_link.field_line", "%s：%s"), labels[i],
                     i == 0   ? (config.local[0] ? config.local : unset)
                     : i == 1 ? (config.external[0] ? config.external : unset)
                     : i == 2 ? (config.has_token ? saved : unset)
                     : i == 3 ? (config.has_cf_id ? saved : unset)
                     : i == 4 ? (config.has_cf_secret ? saved : unset)
                              : (config.home_wifi[0] ? config.home_wifi : unset));
        host->button(fb, 16 + (i % columns) * (field_width + 8), 148 + (i / columns) * 36,
                     field_width, 32, line, setup_field == (unsigned)i);
    }
    int y = 148 + (6 / columns) * 36;
    host->format(line, sizeof(line), hl("settings.home_link.flags", "启用 %s · 外部 %s · 外控 %s · Access %s"),
                 hl(config.enabled ? "settings.home_link.flag.on" : "settings.home_link.flag.off", config.enabled ? "开" : "关"),
                 hl(config.external_enabled ? "settings.home_link.flag.on" : "settings.home_link.flag.off", config.external_enabled ? "开" : "关"),
                 hl(config.external_control ? "settings.home_link.flag.on" : "settings.home_link.flag.off", config.external_control ? "开" : "关"),
                 hl((config.has_cf_id && config.has_cf_secret) ? "settings.home_link.access_ready" : "settings.home_link.access_none",
                    (config.has_cf_id && config.has_cf_secret) ? "已配置" : "无"));
    host->text(fb, 24, y + 8, 13, line, 5);
    int bw = (v.w - 48) / 3;
    host->button(fb, 16, v.h - 56, bw, 40, hl("settings.home_link.phone_fill", "手机填写"), false);
    host->button(fb, 24 + bw, v.h - 56, bw, 40, hl("settings.home_link.save", "保存"), false);
    host->button(fb, 32 + 2 * bw, v.h - 56, bw, 40, hl("common.back", "返回"), false);
}

static void render_printers(uint8_t *fb) {
    pico_app_view_t v = host->view();
    host->fill(fb, 0, 0, v.w, v.h, 15);
    host->heading(fb, hl("settings.home_link.printers", "打印机"), hl("settings.home_link.printers_sub", "只读监看"));
    host->wrap(fb, 24, 110, v.w - 48, 12, 3,
               hl("settings.home_link.printers_body", "独立 Bambu MQTT/云端仍待完整迁入；此处显示宿主快照中的打印机槽位与 HA 导入状态。"), 5);
    char line[160];
    int y = v.landscape ? 156 : 170;
    if (!has_home()) {
        host->text(fb, 24, y, 16, hl("settings.home_link.host_down", "宿主服务不可用"), 0);
    } else {
        for (unsigned slot = 0; slot < 3; slot++) {
            pico_app_home_printer_t p = {.bytes = sizeof(p)};
            if (!host->home_link_printer(slot, &p)) continue;
            int card_width = v.landscape ? (v.w - 48) / 3 : v.w - 32;
            int x = v.landscape ? 16 + (int)slot * (card_width + 8) : 16;
            host->fill(fb, x, y, card_width, 64, 14);
            host->format(line, sizeof(line), hl("settings.home_link.slot", "%u · %s"), slot + 1,
                         p.name[0] ? p.name : hl("settings.home_link.empty_slot", "空槽位"));
            host->wrap(fb, x + 12, y + 10, card_width - 24, 16, 1, line, 0);
            host->format(line, sizeof(line), "%s%s%s", p.state,
                         p.percent >= 0 ? " · " : "", p.percent >= 0 ? "" : "");
            if (p.has_data && p.percent >= 0)
                host->format(line, sizeof(line), hl("settings.home_link.progress", "%s · %d%%"),
                             p.state[0] ? p.state : hl("settings.home_link.no_state", "无状态"), p.percent);
            else
                host->format(line, sizeof(line), "%s",
                             p.has_data ? p.state : hl("settings.home_link.no_data", "无数据（LAN/云端 MQTT 未迁入 PGA）"));
            host->wrap(fb, x + 12, y + 36, card_width - 24, 13, 1, line, 5);
            if (!v.landscape) y += 72;
        }
    }
    int bw = (v.w - 40) / 2;
    host->button(fb, 16, v.h - 56, bw, 40, hl("settings.home_link.phone_fill", "手机填写"), false);
    host->button(fb, 24 + bw, v.h - 56, bw, 40, hl("settings.home_link.back_monitor", "返回监看"), false);
}

static void render(uint8_t *fb) {
    if (page == PAGE_SETUP) render_setup(fb);
    else if (page == PAGE_PRINTERS) render_printers(fb);
    else render_monitor(fb);
}

static bool hit_bottom3(const pico_app_event_t *e, int which) {
    pico_app_view_t v = host->view();
    int bw = (v.w - 48) / 3;
    int x0 = 16 + which * (bw + 8);
    return e->x >= x0 && e->x < x0 + bw && e->y >= v.h - 56 && e->y < v.h - 16;
}

static bool event_monitor(const pico_app_event_t *e) {
    pico_app_view_t v = host->view();
    if (drag_suppress_tap) {
        // 长按拖拽结束后的合成点击不再触发开关。
        // The synthesized tap after a drag must not toggle a device.
        drag_suppress_tap = false;
        return true;
    }
    if (e->type == PGA_TAP && e->x >= v.w - 88 && e->x < v.w - 16 && e->y >= 24 && e->y < 60) {
        host->navigate(PGA_PAGE_LAUNCHER);
        return true;
    }
    if (!has_home()) {
        if (e->type == PGA_TAP && e->y >= v.h - 56 && e->y < v.h - 16) {
            host->navigate(PGA_PAGE_LAUNCHER);
            return true;
        }
        return false;
    }
    if (e->type != PGA_TAP) return false;

    for (int i = 0; i < CARD_CHIP_COUNT; i++) {
        int x, y, w, h;
        chip_rect(v, i, &x, &y, &w, &h);
        if (e->x >= x && e->x < x + w && e->y >= y && e->y < y + h) {
            uint8_t mask = card_chips[i].mask;
            cards.type_filter = !mask || chip_selected(i) ? 0 : mask;
            entity_page = 0;
            cards_save();
            host->dirty();
            return true;
        }
    }

    int per = cards_per_page(v);
    int slot = card_at(v, e->x, e->y);
    if (slot >= 0) {
        int disp[HLC_MAX], n = 0;
        build_display(disp, &n, false);
        if (slot >= n) return true;
        hlc_ent_t *ent = &cache[disp[slot]];
        int page_slot = slot - (int)(entity_page * (unsigned)per);
        int x, y, w, h;
        card_rect(v, page_slot, &x, &y, &w, &h);
        if (ent->kind == 2 && ent->available) {
            if (e->x >= x + 14 && e->x < x + 48 && e->y >= y + h - 40 && e->y < y + h - 10) {
                float value = ent->number > 0 ? ent->number - 1 : 24;
                if (value < 16) value = 16;
                bool ok = host->home_link_control(ent->id, "set_temperature", value);
                host->toast(ok ? "已提交温度" : "控制失败，请稍后重试");
                return true;
            }
            if (e->x >= x + 52 && e->x < x + 86 && e->y >= y + h - 40 && e->y < y + h - 10) {
                float value = ent->number > 0 ? ent->number + 1 : 24;
                if (value > 30) value = 30;
                bool ok = host->home_link_control(ent->id, "set_temperature", value);
                host->toast(ok ? "已提交温度" : "控制失败，请稍后重试");
                return true;
            }
        }
        if (ent->kind < 4) {
            if (!ent->available) {
                host->toast("设备当前不可用");
                return true;
            }
            bool ok = host->home_link_control(ent->id, ent->on ? "turn_off" : "turn_on", 0);
            host->toast(ok ? (ent->on ? "已提交关闭" : "已提交开启") : "控制失败，请稍后重试");
            return true;
        }
        host->toast("温湿度传感器只读");
        return true;
    }

    if (hit_bottom3(e, 0)) {
        page = PAGE_SETUP;
        load_config();
        host->dirty();
        return true;
    }
    if (hit_bottom3(e, 1)) {
        request_sync();
        return true;
    }
    if (hit_bottom3(e, 2)) {
        page = PAGE_PRINTERS;
        host->dirty();
        return true;
    }
    return false;
}

static bool event_setup(const pico_app_event_t *e) {
    if (e->type == PGA_TAP && hit_bottom3(e, 0)) {
        status.bytes = sizeof(status);
        if (!host->home_link(PGA_HOME_OPEN_SETUP, &status)) host->toast(status.error[0] ? status.error : "手机配置暂时不可用");
        return true;
    }
    if (e->type == PGA_TAP && hit_bottom3(e, 1)) {
        config.enabled = 1;
        save_config();
        return true;
    }
    if ((e->type == PGA_TAP && hit_bottom3(e, 2)) || e->type == PGA_SWIPE_R) {
        page = PAGE_MONITOR;
        host->dirty();
        return true;
    }
    if (e->type != PGA_TAP) return false;
    pico_app_view_t v = host->view();
    int columns = v.landscape ? 2 : 1;
    int field_width = (v.w - 32 - (columns - 1) * 8) / columns;
    for (int i = 0; i < 6; i++) {
        int x = 16 + (i % columns) * (field_width + 8), y = 148 + (i / columns) * 36;
        if (e->x >= x && e->x < x + field_width && e->y >= y && e->y < y + 32) {
            setup_field = (unsigned)i;
            if (i == 0) open_input(0, config.local, "本地");
            else if (i == 1) open_input(1, config.external, "外部");
            else if (i == 2) open_input(2, "", "令牌");
            else if (i == 3) open_input(3, "", "CF ID");
            else if (i == 4) open_input(4, "", "CF Secret");
            else open_input(5, config.home_wifi, "WiFi");
            return true;
        }
    }
    int y = 148 + (6 / columns) * 36;
    if (e->y >= y && e->y < y + 28) {
        if (!config.enabled) config.enabled = 1;
        else if (!config.external_enabled) config.external_enabled = 1;
        else if (!config.external_control) config.external_control = 1;
        else {
            config.enabled = config.external_enabled = config.external_control = 0;
        }
        host->dirty();
        return true;
    }
    return false;
}

static bool monitor_scroll(const pico_app_event_t *e) {
    if (page != PAGE_MONITOR || !has_home()) return false;
    if (drag_from >= 0) return true;
    bool consume = false;
    int d = ps_scroll_step(&entity_scroll, e->type, e->x0, e->y0, e->x, e->y, PS_SCROLL_AXIS_X, &consume);
    if (!d && !consume) return false;
    if (!d) return true;
    int disp[HLC_MAX], n = 0;
    build_display(disp, &n, false);
    int per = cards_per_page(host->view());
    unsigned pages = n ? (unsigned)((n + per - 1) / per) : 1;
    if (d > 0) {
        if (entity_page + 1 < pages) {
            entity_page++;
            host->dirty();
        }
        return true;
    }
    if (entity_page) {
        entity_page--;
        host->dirty();
    } else
        host->navigate(PGA_PAGE_LAUNCHER);
    return true;
}

static bool event(const pico_app_event_t *e) {
    if (!e) return false;
    if (e->type == PGA_PRESS) drag_suppress_tap = false;
    // 先处理卡片拖拽；拖拽中的移动不会再被横向翻页吃掉。
    // Handle card dragging first so its moves are never consumed by horizontal paging.
    if (monitor_drag_event(e)) return true;
    // 实体列表在拖动达到阈值时翻页，松手合成事件不重复翻。/ The entity list pages when the drag crosses the threshold; the synthesized lift event does not page again.
    if (monitor_scroll(e)) return true;
    if (e->type == PGA_TAP || e->type == PGA_SWIPE_L || e->type == PGA_SWIPE_R) {
        if (page == PAGE_SETUP) return event_setup(e);
        if (page == PAGE_PRINTERS) {
            pico_app_view_t v = host->view();
            int bw = (v.w - 40) / 2;
            if (e->type == PGA_TAP && e->x >= 16 && e->x < 16 + bw && e->y >= v.h - 56) {
                status.bytes = sizeof(status);
                if (!host->home_link(PGA_HOME_OPEN_SETUP, &status)) host->toast(status.error[0] ? status.error : "手机配置暂时不可用");
                return true;
            }
            if ((e->type == PGA_TAP && e->x >= 24 + bw && e->x < v.w - 16 &&
                 e->y >= v.h - 56 && e->y < v.h - 16) || e->type == PGA_SWIPE_R) {
                page = PAGE_MONITOR;
                host->dirty();
                return true;
            }
            return false;
        }
        return event_monitor(e);
    }
    return false;
}

static bool key(int k) {
    if (k == -1) {
        if (page != PAGE_MONITOR) {
            page = PAGE_MONITOR;
            host->dirty();
            return true;
        }
        host->navigate(PGA_PAGE_LAUNCHER);
        return true;
    }
    return false;
}

static bool tick(void) {
    if (waiting_input) take_input();
    take_entry();
    refresh_status();
    if (cache_revision != status.revision || (cache_count == 0 && status.entity_count > 0)) refresh_cache();
    int disp[HLC_MAX], n = 0;
    build_display(disp, &n, false);
    int per = cards_per_page(host->view());
    unsigned pages = n ? (unsigned)((n + per - 1) / per) : 1;
    unsigned last_page = pages - 1;
    if (entity_page > last_page) { entity_page = last_page; host->dirty(); }
    if (need_sync && has_home() && (status.flags & PGA_HOME_WIFI) && !status.busy) request_sync();
    return status.busy;
}

static void action(int a) { (void)a; }
static uint32_t status_bits(void) { return status.busy ? PGA_STATUS_BUSY : 0; }
static bool save(void) { return true; }
static void stop(void) {
    if (has_home() && host->home_link_input) (void)host->home_link_input(PGA_HOME_INPUT_CANCEL, draft, sizeof(draft));
    waiting_input = false;
    drag_from = drag_to = -1;
    drag_suppress_tap = false;
    host->set(draft, 0, sizeof(draft));
    host->set(config.ha_token, 0, sizeof(config.ha_token));
    host->set(config.cf_access_client_id, 0, sizeof(config.cf_access_client_id));
    host->set(config.cf_access_client_secret, 0, sizeof(config.cf_access_client_secret));
}

static const pico_app_exports_t exports = {
    .abi = PICO_APP_ABI,
    .bytes = PICO_APP_EXPORTS_V1_BYTES,
    .version = HOME_LINK_VERSION,
    .kind = HOME_LINK_KIND,
    .id = "home-link",
    .start = start,
    .render = render,
    .event = event,
    .key = key,
    .tick = tick,
    .action = action,
    .status = status_bits,
    .save = save,
    .stop = stop,
};

__attribute__((visibility("default"))) const pico_app_exports_t *pico_app_query(void) { return &exports; }

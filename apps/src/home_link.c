/*
 * SPDX-FileCopyrightText: 2026 XH Lab
 * SPDX-License-Identifier: Apache-2.0
 * 中文：家庭连接 TF 工具包（kind 130）；经宿主 ABI 管理 HA 设备，含 Cloudflare Access；打印机为独立应用。
 * English: Home-link TF package (kind 130); manages HA devices via host ABI, including Cloudflare Access; printers live in a separate app.
 * 手机扫码写入与 Mac 管理页共用 PGA 存储。门户开着时本机与 Mac 都不保存，避免抢写。
 * The phone portal and the Mac manage page share the PGA store. While the portal is open, neither the device keyboard nor Mac saves.
 */
#include "pico_app_sdk.h"
#include "pico_app_switch.h"
#include "../../main/system/pico_scroll.h"

enum { HOME_LINK_KIND = 130, HOME_LINK_VERSION = 10111 };
enum { PAGE_MONITOR, PAGE_SETUP, PAGE_DEVICES, PAGE_ROOMS };

static const pico_app_host_t *host;
static unsigned page, entity_page, setup_field, last_revision;
_Static_assert(PGA_PRESS == 0 && PGA_MOVE == 1 && PGA_RELEASE == 2 && PGA_TAP == 3 && PGA_LONG == 4 &&
                   PGA_SWIPE_L == 5 && PGA_SWIPE_R == 6 && PGA_SWIPE_U == 7 && PGA_SWIPE_D == 8 && PGA_CANCEL == 9,
               "pico_scroll.h event numbers");
static ps_scroll_track_t entity_scroll;
static bool waiting_input, need_sync, setup_advanced;
static pico_app_home_status_t status;
static pico_app_home_config_t config;
static char draft[256];
static int draft_kind; /* 0 local 1 external 2 token 3 cf_id 4 cf_secret 5 wifi */

/* 卡片面板：类型筛选与拖拽顺序按实体 id 哈希保存，同步后索引变化不会打乱。
 * Card panel: type filter and drag order persist by entity-id hash, so sync reindexing is safe. */
#define HLC_MAX 96u
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
    uint8_t kind, available, on,brightness,dimmable,hvac_modes;
    float number;
} hlc_ent_t;
static hlc_ui_t cards;
static hlc_ent_t cache[HLC_MAX];
typedef struct { uint32_t magic,count,hashes[HLC_MAX]; } hidden_t;
static hidden_t hidden, hidden_draft;
static unsigned manage_page;
static pico_app_home_rooms_t rooms,room_submit;
static uint32_t room_selection;
static unsigned room_page,room_return;
static char room_message[96];
static int64_t room_poll_us;
static pico_app_home_catalog_t catalog;
static pico_app_home_selection_t selection,selection_submit;
static int brightness_slot=-1,brightness_value,mode_slot=-1;
static int switch_slot=-1,switch_x,switch_y,switch_left,switch_top;
static bool switch_on,switch_suppress;
static char switch_id[96];
static bool control_suppress,selection_migrated;
static int64_t catalog_poll_us,sync_due_us;
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
           host->home_link && host->home_link_config && host->home_link_entity &&
           host->home_link_control && host->home_link_input;
}
static bool has_rooms(void){
    return host->bytes>=offsetof(pico_app_host_t,home_rooms)+sizeof(host->home_rooms)&&host->home_rooms;
}
static bool hl_same(const char *,const char *);
static bool has_selection(void){return host->bytes>=offsetof(pico_app_host_t,home_selection)+sizeof(host->home_selection)&&host->home_selection&&host->home_catalog;}
static void catalog_scan(unsigned offset){
    if(!has_selection())return;
    catalog.bytes=sizeof(catalog);catalog.offset=offset;
    if(!host->home_catalog(PGA_CATALOG_SCAN,&catalog))host->toast("目录正在获取，请稍候");
}
static void selection_get(void){selection.bytes=sizeof(selection);if(has_selection())(void)host->home_selection(PGA_SELECTION_GET,&selection);}
static int selected_at(const char *id){for(unsigned i=0;i<selection.count;i++)if(hl_same(selection.ids[i],id))return (int)i;return -1;}
static void rooms_refresh(void){
    if(!has_rooms())return;
    uint32_t revision=rooms.revision;bool busy=rooms.busy;
    rooms.bytes=sizeof(rooms);if(!host->home_rooms(PGA_ROOMS_GET,&rooms))return;
    if(revision!=rooms.revision||busy!=rooms.busy){room_selection=rooms.selected;host->dirty();}
}
static void rooms_begin(void){
    room_return=page==PAGE_DEVICES?PAGE_DEVICES:PAGE_MONITOR;page=PAGE_ROOMS;room_page=0;room_message[0]=0;
    rooms_refresh();room_selection=rooms.selected;
    if(has_rooms()&&!rooms.busy){rooms.bytes=sizeof(rooms);(void)host->home_rooms(PGA_ROOMS_SCAN,&rooms);}
    host->dirty();
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
        // 旧64项布局按原长度读取，保留筛选和顺序。/ Read the legacy 64-item record at its original size.
        struct {uint32_t magic,version;uint8_t type_filter,order_count;uint16_t reserved;uint32_t order[64];} old;
        host->set(&old,0,sizeof(old));
        if(host->load&&host->load("homecards1",&old,sizeof(old))&&old.magic==HLC_UI_MAGIC&&old.version==1&&old.order_count<=64){
            host->set(&cards,0,sizeof(cards));host->copy(&cards,&old,sizeof(old));cards.type_filter&=0x3fu;return;
        }
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
static void migrate_selection(void);
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
            d->number = e.number;d->brightness=e.brightness;d->dimmable=e.dimmable;d->hvac_modes=e.hvac_modes;
            count++;
        }
    }
    cache_count = count;
    cache_revision = status.revision;
    if(has_selection()&&page!=PAGE_DEVICES){
        selection_get();
        if(!selection.configured&&!selection_migrated&&status.result==2&&cache_count)migrate_selection();
        if(selection.configured){cards.order_count=(uint8_t)selection.count;hidden.count=0;
            for(unsigned i=0;i<selection.count;i++)cards.order[i]=card_hash(selection.ids[i]);}
    }
}

// 只保存本机显示选择，不删除 HA 实体；草稿失败或取消时保留当前配置。
// Persist local visibility only, never delete HA entities; failed saves and cancel retain the active selection.
static bool excluded(const hidden_t *prefs,uint32_t hash){
    for(unsigned i=0;i<prefs->count;i++)if(prefs->hashes[i]==hash)return true;
    return false;
}
static void hidden_load(void){
    host->set(&hidden,0,sizeof(hidden));
    if(!host->load||!host->load("homehidden1",&hidden,sizeof(hidden))||hidden.magic!=0x48484431u||hidden.count>HLC_MAX){
        struct {uint32_t magic,count,hashes[64];} old;host->set(&old,0,sizeof(old));host->set(&hidden,0,sizeof(hidden));
        if(host->load&&host->load("homehidden1",&old,sizeof(old))&&old.magic==0x48484431u&&old.count<=64)host->copy(&hidden,&old,sizeof(old));
    }
    hidden.magic=0x48484431u;
}
static void manage_begin(void){
    hidden_draft=hidden;manage_page=0;page=PAGE_DEVICES;
    if(has_selection()){selection_get();host->set(&catalog,0,sizeof(catalog));catalog_scan(0);}
    host->dirty();
}
static void visibility_toggle(uint32_t hash){
    for(unsigned i=0;i<hidden_draft.count;i++)if(hidden_draft.hashes[i]==hash){
        for(unsigned j=i+1;j<hidden_draft.count;j++)hidden_draft.hashes[j-1]=hidden_draft.hashes[j];
        hidden_draft.count--;return;
    }
    if(hidden_draft.count<HLC_MAX)hidden_draft.hashes[hidden_draft.count++]=hash;
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
        if (!all && excluded(&hidden,cache[idx].hash)) continue;
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

static const struct { const char *label; uint8_t mask; } card_chips[] = {
    {"全部", 0}, {"灯光", 1u << 0}, {"空调", 1u << 2}, {"风扇", 1u << 3},
    {"开关", 1u << 1}, {"传感", (1u << 4) | (1u << 5)},
};
#define CARD_CHIP_COUNT ((int)(sizeof(card_chips) / sizeof(card_chips[0])))

static int card_columns(pico_app_view_t v) { (void)v;return 2; }
static int card_rows(pico_app_view_t v) { return v.landscape ? 1 : 3; }
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
    host->heading(fb, hl("settings.home_link.monitor", "家庭连接"), NULL);
    host->button(fb, v.w - 88, 24, 72, 36, "返回", false);
    host->format(line,sizeof(line),"%s%u 个设备 · %d/%d 页 · %s",sub[0]?(status.busy?"同步中 · ":status.at_home?"本地 · ":"外部 · "):"",(unsigned)visible,entity_page+1,pages,
                 status.error[0]?status.error:((status.flags&PGA_HOME_WIFI)?"Wi-Fi 已连接":"Wi-Fi 未连接"));
    if(status.entity_total>HLC_MAX)host->format(line,sizeof(line),"显示 %u / %u 项 · 请减少同步房间",(unsigned)cache_count,(unsigned)status.entity_total);
    host->wrap(fb,14,v.landscape?84:92,v.w-28,11,1,line,5);
    for (int i = 0; i < CARD_CHIP_COUNT; i++) {
        int x, y, w, h;
        chip_rect(v, i, &x, &y, &w, &h);
        host->button(fb, x, y, w, h, card_chips[i].label, chip_selected(i));
    }
}

// 首次迁移只提交当前已显示实体，复用旧隐藏与排序。/ Migrate visible entities once, preserving legacy visibility and order.
static void migrate_selection(void){
    int idx[HLC_MAX],n=0;sorted_all(idx,&n);selection_submit=selection;selection_submit.count=0;
    for(int i=0;i<n;i++)if(!excluded(&hidden,cache[idx[i]].hash))host->copy(selection_submit.ids[selection_submit.count++],cache[idx[i]].id,96);
    if(host->home_selection(PGA_SELECTION_SET,&selection_submit)){selection_migrated=true;selection=selection_submit;need_sync=true;}
}

static int action_y(int y,int h){return y+h-(h<100?34:54);}
static void draw_card(uint8_t *fb,int x,int y,int w,int h,const hlc_ent_t *e,int display_index){
    bool small=h<100,avail=e->available,sensor=e->kind>=4;
    host->box(fb,x,y,w,h,10,avail?(e->on?14:15):14,true);
    host->box(fb,x,y,w,h,10,avail?3:10,false);
    host->wrap(fb,x+10,y+8,w-20,13,1,e->name[0]?e->name:e->id,1);
    char value[64];
    if(!avail)host->format(value,sizeof(value),"不可用");
    else if(e->kind>=4)host->format(value,sizeof(value),"%s %s",e->state[0]?e->state:"--",e->unit);
    else if(e->kind==2)host->format(value,sizeof(value),"%.0f°",e->number);
    else host->format(value,sizeof(value),"%s",state_label(e->state));
    if(!(e->kind==0&&e->dimmable)){
        if(!small)host->text(fb,x+10,y+46,22,value,avail?1:5);
        else if(sensor)host->wrap(fb,x+10,y+32,w-20,15,1,value,avail?1:5);
        else host->wrap(fb,x+10,y+32,e->kind==2?36:w-80,13,1,value,avail?1:5);
    }
    int cy=action_y(y,h);
    if(avail&&!sensor){
        if(e->kind==2){
            host->button(fb,x+(small?w-136:10),cy,30,28,"-",false);
            host->button(fb,x+(small?w-100:46),cy,30,28,"+",false);
        }
    }
    if(!sensor)pico_switch_draw(fb,x+w-60,cy+1,48,26,e->on,avail,host->box);
    if(e->kind==0&&e->dimmable){
        int value=brightness_slot==display_index?brightness_value:(int)e->brightness*100/255;
        int sy=y+h-78;char label[24];host->format(label,sizeof(label),"亮度 %d%%",value);
        host->text(fb,x+10,sy-14,10,label,avail?3:8);
        host->box(fb,x+12,sy+7,w-24,4,2,11,true);
        if(value)host->box(fb,x+12,sy+7,(w-24)*value/100,4,2,3,true);
        host->box(fb,x+8+(w-24)*value/100,sy+3,8,12,4,avail?2:8,true);
    }
    if(e->kind==2&&e->hvac_modes)host->button(fb,x+w-62,y+43,52,28,"模式",false);
    if(!small)host->wrap(fb,x+10,y+h-20,w-20,10,1,e->room[0]?e->room:(sensor?"只读传感器":"长按拖动排序"),5);
    if(display_index==drag_from||display_index==drag_to)host->box(fb,x-2,y-2,w+4,h+4,11,1,false);
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

static bool move_hash(uint32_t from_hash, uint32_t to_hash) {
    int all_idx[HLC_MAX], n = 0;
    sorted_all(all_idx, &n);
    int from = -1, to = -1;
    for (int i = 0; i < n; i++) {
        if (cache[all_idx[i]].hash == from_hash) from = i;
        if (cache[all_idx[i]].hash == to_hash) to = i;
    }
    if (from < 0 || to < 0 || from == to) return false;
    int moved = all_idx[from];
    if (from < to)
        for (int i = from; i < to; i++) all_idx[i] = all_idx[i + 1];
    else
        for (int i = from; i > to; i--) all_idx[i] = all_idx[i - 1];
    all_idx[to] = moved;
    if(has_selection()){
        selection_get();selection_submit=selection;selection_submit.count=(uint32_t)n;
        for(int i=0;i<n;i++)host->copy(selection_submit.ids[i],cache[all_idx[i]].id,96);
        if(!host->home_selection(PGA_SELECTION_SET,&selection_submit)){host->toast("顺序保存失败，请重试");return false;}
        if(selection_submit.pending)return true;
    }
    cards.order_count = (uint8_t)n;
    for (int i = 0; i < n; i++) cards.order[i] = cache[all_idx[i]].hash;
    cards_save();return true;
}

static void commit_drag(void) {
    int from = drag_from, to = drag_to;
    drag_from = drag_to = -1;
    drag_suppress_tap = true;
    if (from >= 0 && to >= 0 && from != to) {
        int disp[HLC_MAX], n = 0;
        build_display(disp, &n, false);
        if (from < n && to < n) {
            if(move_hash(cache[disp[from]].hash,cache[disp[to]].hash))host->toast(selection_submit.pending?"顺序草稿已保存，等待主机确认":"卡片顺序已保存");
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
        int x,y,w,h;card_rect(host->view(),slot-(int)(entity_page*cards_per_page(host->view())),&x,&y,&w,&h);
        if(e->y>=y+40)return true;
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
    if(has_rooms()){
        rooms_refresh();
        if(!rooms.configured){need_sync=false;rooms_begin();return;}
    }
    status.bytes = sizeof(status);
    if(!host->home_link(PGA_HOME_SYNC, &status))host->toast(status.error[0]?status.error:"暂时无法同步");
    need_sync = false;sync_due_us=(host->time_us?host->time_us():0)+30000000;
    host->dirty();
}

/* Host arms this once via PGA_HOME_ENTRY. Old hosts return status without ENTRY_OK; ignore them. */
static void take_entry(void) {
    if (!has_home()) return;
    pico_app_home_status_t entry;
    host->set(&entry, 0, sizeof(entry));
    entry.bytes = sizeof(entry);
    if (!host->home_link(PGA_HOME_ENTRY, &entry) || !(entry.flags & PGA_HOME_ENTRY_OK) || entry.result > PAGE_SETUP)
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
        refresh_status();host->toast(status.flags&PGA_HOME_PENDING?"已保存草稿，等待主机确认":hl("settings.home_link.saved_toast", "已保存到设备"));
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
    host = h;brightness_slot=mode_slot=switch_slot=-1;switch_suppress=false;control_suppress=selection_migrated=false;sync_due_us=0;
    page = PAGE_MONITOR;
    entity_page = setup_field = 0;
    host->set(&entity_scroll, 0, sizeof(entity_scroll));
    waiting_input = need_sync = false;
    last_revision = 0;
    host->set(&status, 0, sizeof(status));
    host->set(&config, 0, sizeof(config));
    cards_load();hidden_load();setup_advanced=false;
    host->set(&rooms,0,sizeof(rooms));room_selection=room_page=0;room_message[0]=0;
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
        if(cache_count&&hidden.count)
            host->wrap(fb,24,210,v.w-48,14,3,"设备已隐藏。点「管理设备」重新勾选显示。",5);
        else if (cards.type_filter)
            host->wrap(fb, 24, 210, v.w - 48, 14, 3, "当前筛选没有设备。点上方「全部」恢复显示。", 5);
        else
            host->wrap(fb, 24, 210, v.w - 48, 14, 3, "尚未同步到可显示的设备。请检查 HA 连接后点「同步」。", 5);
    }

    int bw = (v.w - 48) / 3;
    host->button(fb, 16, v.h - 56, bw, 40, "设置", false);
    host->button(fb, 24 + bw, v.h - 56, bw, 40, status.busy ? "…" : "同步", status.busy);
    host->button(fb, 32 + 2 * bw, v.h - 56, bw, 40, "管理设备", false);
}

// 高级认证按需展开，编辑仍写入同一份草稿。/ Reveal advanced authentication without replacing the draft.
static const unsigned setup_fields[]={0,2,5,1,3,4};
static unsigned setup_count(void){return setup_advanced?6:4;}
static void setup_geometry(pico_app_view_t v,unsigned slot,int *x,int *y,int *w,int *h){
    int cols=v.landscape?2:1;*w=(v.w-32-(cols-1)*8)/cols;*h=40;
    *x=16+(int)(slot%cols)*(*w+8);*y=(v.landscape?140:212)+(int)(slot/cols)*44;
}
static int setup_flags_y(pico_app_view_t v){
    int cols=v.landscape?2:1;
    return (v.landscape?140:212)+(int)((setup_count()+cols-1)/cols)*44+2;
}
static void render_setup(uint8_t *fb) {
    pico_app_view_t v = host->view();
    host->fill(fb, 0, 0, v.w, v.h, 15);
    host->heading(fb, hl("settings.home_link.setup_title", "连接设置"), hl("settings.home_link.setup_sub", "设备本地保存"));
    host->wrap(fb, 24, v.landscape?108:112, v.landscape?v.w-228:v.w-48, 11, v.landscape?1:2,
               "手机填写或点选编辑，修改后点保存。", 5);
    host->button(fb,v.landscape?v.w-188:16,v.landscape?88:164,v.landscape?172:v.w-32,40,
                 setup_advanced?"收起高级认证":"展开高级认证",false);
    char line[192];
    const char *labels[] = {
        hl("settings.home_link.field.local", "本地地址"), hl("settings.home_link.field.external", "外部 HTTPS"),
        hl("settings.home_link.field.token", "HA 令牌"), hl("settings.home_link.field.cf_id", "CF Client ID"),
        hl("settings.home_link.field.cf_secret", "CF Client Secret"), hl("settings.home_link.field.wifi", "家庭 Wi-Fi")};
    const char *unset = hl("settings.home_link.unset", "（未设）");
    const char *saved = hl("settings.home_link.saved", "已保存");
    for (unsigned slot=0;slot<setup_count();slot++) {
        unsigned i=setup_fields[slot];int x,y,w,h;setup_geometry(v,slot,&x,&y,&w,&h);
        host->format(line, sizeof(line), hl("settings.home_link.field_line", "%s：%s"), labels[i],
                     i == 0   ? (config.local[0] ? config.local : unset)
                     : i == 1 ? (config.external[0] ? config.external : unset)
                     : i == 2 ? (config.ha_token[0]?"待保存":config.has_token ? saved : unset)
                     : i == 3 ? (config.cf_access_client_id[0]?"待保存":config.has_cf_id ? saved : unset)
                     : i == 4 ? (config.cf_access_client_secret[0]?"待保存":config.has_cf_secret ? saved : unset)
                              : (config.home_wifi[0] ? config.home_wifi : unset));
        host->button(fb,x,y,w,h,line,false);
    }
    int y=setup_flags_y(v);
    int bw = (v.w - 48) / 3;
    const char *flags[]={"启用","外部","外控"};
    for(unsigned i=0;i<3;i++){
        bool on=i==0?config.enabled:i==1?config.external_enabled:config.external_control;
        int sx=16+(int)i*(bw+8),sh=v.landscape?26:40;
        host->box(fb,sx,y,bw,sh,8,15,true);
        host->box(fb,sx,y,bw,sh,8,12,false);
        host->text(fb,sx+6,y+(sh-16)/2,10,flags[i],1);
        pico_switch_draw(fb,sx+bw-40,y+(sh-20)/2,34,20,on,true,host->box);
    }
    host->button(fb, 16, v.h - 56, bw, 40, hl("settings.home_link.phone_fill", "手机填写"), false);
    host->button(fb, 24 + bw, v.h - 56, bw, 40, hl("settings.home_link.save", "保存"), false);
    host->button(fb, 32 + 2 * bw, v.h - 56, bw, 40, hl("common.back", "返回"), false);
}

static unsigned manage_per(pico_app_view_t v){return v.landscape?6:8;}
static void manage_rect(pico_app_view_t v,unsigned i,int *x,int *y,int *w){
    *w=v.landscape?(v.w-40)/2:v.w-32;
    *x=16+(v.landscape?(int)(i%2)*(*w+8):0);
    *y=(v.landscape?118:164)+(int)(v.landscape?i/2:i)*46;
}
static const pico_app_home_entity_t *catalog_item(unsigned index){
    return !catalog.busy&&index>=catalog.offset&&index-catalog.offset<catalog.count?&catalog.items[index-catalog.offset]:NULL;
}
static void render_catalog(uint8_t *fb){
    pico_app_view_t v=host->view();host->fill(fb,0,0,v.w,v.h,15);
    host->heading(fb,"添加与管理设备",v.landscape?NULL:"分页浏览全部目录，仅缓存选中的控制卡片");
    unsigned per=manage_per(v),pages=catalog.total?(catalog.total+per-1)/per:1;
    char text[128];host->format(text,sizeof(text),"已选 %u/96 · 目录 %u 项 · %u/%u 页",selection.count,catalog.total,manage_page+1,pages);
    host->text(fb,20,v.landscape?88:124,11,text,5);
    if(catalog.busy||!catalog.total)host->wrap(fb,24,v.landscape?140:224,v.w-48,13,3,catalog.busy?"正在获取本页设备…":catalog.error[0]?catalog.error:"该范围没有可添加的实体，请选择房间",5);
    for(unsigned i=0;i<per;i++){
        const pico_app_home_entity_t *e=catalog_item(manage_page*per+i);if(!e)continue;
        bool checked=selected_at(e->id)>=0;int x,y,w;manage_rect(v,i,&x,&y,&w);
        host->box(fb,x,y,w,40,7,checked?14:15,true);host->box(fb,x,y,w,40,7,10,false);
        host->box(fb,x+10,y+12,16,16,3,checked?2:10,checked);
        if(checked){host->line(fb,x+13,y+20,x+17,y+24,15);host->line(fb,x+17,y+24,x+23,y+16,15);}
        host->wrap(fb,x+36,y+8,w-48,12,1,e->name[0]?e->name:e->id,1);
        host->wrap(fb,x+36,y+25,w-48,9,1,e->room[0]?e->room:e->id,5);
    }
    int bw=(v.w-48)/3;host->button(fb,16,v.h-56,bw,40,"取消",false);
    host->button(fb,24+bw,v.h-56,bw,40,"选择房间",false);
    host->button(fb,32+2*bw,v.h-56,bw,40,"保存选择",false);
}
static void render_devices(uint8_t *fb){
    if(has_selection()){render_catalog(fb);return;}
    pico_app_view_t v=host->view();host->fill(fb,0,0,v.w,v.h,15);
    host->heading(fb,"添加与管理设备",v.landscape?NULL:"勾选在本机显示的设备，不删除 HA 中的实体");
    unsigned per=manage_per(v),pages=cache_count?(cache_count+per-1)/per:1;
    if(manage_page>=pages)manage_page=pages-1;
    char line[96];host->format(line,sizeof(line),"%u 项 · %u/%u 页 · 仅控制本机显示 · 左右滑动翻页",cache_count,manage_page+1,pages);
    host->text(fb,20,v.landscape?88:124,11,line,5);
    if(!cache_count)host->wrap(fb,24,v.landscape?140:224,v.w-48,13,3,
        "先在 HA 添加设备，再同步到本机。支持灯光、开关、空调、风扇和温湿度传感器。",5);
    for(unsigned i=0;i<per&&manage_page*per+i<cache_count;i++){
        const hlc_ent_t *e=&cache[manage_page*per+i];bool shown=!excluded(&hidden_draft,e->hash);
        int x,y,w;manage_rect(v,i,&x,&y,&w);
        host->box(fb,x,y,w,40,7,shown?14:15,true);host->box(fb,x,y,w,40,7,10,false);
        host->box(fb,x+10,y+12,16,16,3,shown?2:10,shown);
        if(shown&&host->line){host->line(fb,x+13,y+20,x+17,y+24,15);host->line(fb,x+17,y+24,x+23,y+16,15);}
        host->wrap(fb,x+36,y+8,w-48,12,1,e->name[0]?e->name:e->id,1);
        host->wrap(fb,x+36,y+25,w-48,9,1,e->id,5);
    }
    int bw=(v.w-48)/3;
    host->button(fb,16,v.h-56,bw,40,"取消",false);
    host->button(fb,24+bw,v.h-56,bw,40,"选择房间",false);
    host->button(fb,32+2*bw,v.h-56,bw,40,"保存显示选择",true);
}

static unsigned rooms_total(void){unsigned n=0;for(unsigned i=0;i<rooms.count;i++)if(room_selection&(1u<<i))n+=rooms.items[i].devices;return n;}
static void render_rooms(uint8_t *fb){
    pico_app_view_t v=host->view();host->fill(fb,0,0,v.w,v.h,15);
    host->heading(fb,"选择同步房间",v.landscape?NULL:"只同步勾选房间的设备，取消保留原范围");
    unsigned per=manage_per(v),pages=rooms.count?(rooms.count+per-1)/per:1;
    if(room_page>=pages)room_page=pages-1;
    char line[128];host->format(line,sizeof(line),"范围 %u 项 · %u/%u 页 · 下一步选择控制卡片",rooms_total(),room_page+1,pages);
    host->text(fb,20,v.landscape?88:124,11,line,5);
    if(!rooms.count)host->wrap(fb,24,v.landscape?140:224,v.w-48,13,3,
        rooms.busy?"正在获取房间与设备数量…":rooms.error[0]?rooms.error:"没有可同步的房间，请检查 HA 连接和设备所属房间。",5);
    for(unsigned i=0;i<per&&room_page*per+i<rooms.count;i++){
        unsigned idx=room_page*per+i;bool selected=(room_selection&(1u<<idx))!=0;
        int x,y,w;manage_rect(v,i,&x,&y,&w);
        host->box(fb,x,y,w,40,7,selected?14:15,true);host->box(fb,x,y,w,40,7,10,false);
        host->box(fb,x+10,y+12,16,16,3,selected?2:10,selected);
        if(selected&&host->line){host->line(fb,x+13,y+20,x+17,y+24,15);host->line(fb,x+17,y+24,x+23,y+16,15);}
        host->format(line,sizeof(line),"%s · %u 项",rooms.items[idx].name,rooms.items[idx].devices);
        host->wrap(fb,x+36,y+12,w-48,12,1,line,1);
    }
    const char *note=room_message[0]?room_message:rooms.truncated?"仅列出前32个房间，请在 HA 中合并房间后重新获取。":"";
    if(*note)host->wrap(fb,20,v.h-90,v.w-40,10,2,note,5);
    int bw=(v.w-48)/3;
    host->button(fb,16,v.h-56,bw,40,"取消",false);
    host->button(fb,24+bw,v.h-56,bw,40,rooms.busy?"获取中":"重新获取",false);
    host->button(fb,32+2*bw,v.h-56,bw,40,"保存并同步",!rooms.busy&&rooms.result==2);
}

static void render_mode(uint8_t *fb);
static void render(uint8_t *fb) {
    if (page == PAGE_SETUP) render_setup(fb);
    else if (page == PAGE_DEVICES) render_devices(fb);
    else if (page == PAGE_ROOMS) render_rooms(fb);
    else render_monitor(fb);
    render_mode(fb);
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
            if (e->x >= x + (h<100?w-136:10) && e->x < x + (h<100?w-106:40) && e->y >= action_y(y,h) && e->y < action_y(y,h)+28) {
                float value = ent->number > 0 ? ent->number - 1 : 24;
                if (value < 16) value = 16;
                bool ok = host->home_link_control(ent->id, "set_temperature", value);
                host->toast(ok ? "已提交温度" : "控制失败，请稍后重试");
                return true;
            }
            if (e->x >= x + (h<100?w-100:46) && e->x < x + (h<100?w-70:76) && e->y >= action_y(y,h) && e->y < action_y(y,h)+28) {
                float value = ent->number > 0 ? ent->number + 1 : 24;
                if (value > 30) value = 30;
                bool ok = host->home_link_control(ent->id, "set_temperature", value);
                host->toast(ok ? "已提交温度" : "控制失败，请稍后重试");
                return true;
            }
        }
        if (ent->kind < 4) {
            if(e->x<x+w-64||e->x>=x+w-10||e->y<action_y(y,h)||e->y>=action_y(y,h)+28)return true;
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
        manage_begin();
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
    int ax=v.landscape?v.w-188:16,ay=v.landscape?88:164,aw=v.landscape?172:v.w-32;
    if(e->x>=ax&&e->x<ax+aw&&e->y>=ay&&e->y<ay+40){setup_advanced=!setup_advanced;host->dirty();return true;}
    for (unsigned slot=0;slot<setup_count();slot++) {
        unsigned i=setup_fields[slot];int x,y,w,h;setup_geometry(v,slot,&x,&y,&w,&h);
        if (e->x >= x && e->x < x + w && e->y >= y && e->y < y + h) {
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
    int y = setup_flags_y(v);
    int bw=(v.w-48)/3;
    for(unsigned i=0;i<3;i++)if(e->x>=16+(int)i*(bw+8)&&e->x<16+(int)i*(bw+8)+bw&&e->y>=y&&e->y<y+(v.landscape?26:40)){
        if(i==0)config.enabled=!config.enabled;
        else if(i==1)config.external_enabled=!config.external_enabled;
        else config.external_control=!config.external_control;
        host->dirty();return true;
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

static bool catalog_event(const pico_app_event_t *e){
    pico_app_view_t v=host->view();unsigned per=manage_per(v),pages=catalog.total?(catalog.total+per-1)/per:1;
    if(e->type==PGA_SWIPE_L||e->type==PGA_SWIPE_R){
        if(catalog.busy)return true;
        if(e->type==PGA_SWIPE_L&&manage_page+1<pages)manage_page++;
        else if(e->type==PGA_SWIPE_R&&manage_page)manage_page--;
        unsigned offset=(manage_page*per/24)*24;
        if(offset!=catalog.offset)catalog_scan(offset);
        host->dirty();return true;
    }
    if(e->type!=PGA_TAP)return false;
    if(hit_bottom3(e,0)){page=PAGE_MONITOR;host->dirty();return true;}
    if(hit_bottom3(e,1)){rooms_begin();return true;}
    if(hit_bottom3(e,2)){
        selection_submit=selection;
        if(host->home_selection(PGA_SELECTION_SET,&selection_submit)){
            hidden.count=0;page=PAGE_MONITOR;entity_page=0;request_sync();host->dirty();
            host->toast(selection_submit.pending?"已保存草稿，等待主机确认":"已保存控制卡片");
        }else host->toast(selection_submit.error[0]?selection_submit.error:"保存失败，选择草稿已保留");
        return true;
    }
    for(unsigned i=0;i<per;i++){
        const pico_app_home_entity_t *ent=catalog_item(manage_page*per+i);if(!ent)continue;
        int x,y,w;manage_rect(v,i,&x,&y,&w);
        if(e->x>=x&&e->x<x+w&&e->y>=y&&e->y<y+40){
            int at=selected_at(ent->id);
            if(at>=0){for(unsigned j=(unsigned)at+1;j<selection.count;j++)host->copy(selection.ids[j-1],selection.ids[j],96);selection.count--;}
            else if(selection.count>=96)host->toast("最多选择96项，请先取消其他卡片");
            else host->copy(selection.ids[selection.count++],ent->id,96);
            host->dirty();return true;
        }
    }
    return false;
}
static const char *mode_names[]={"off","heat","cool","heat_cool","auto","dry","fan_only"};
static const char *mode_labels[]={"关闭","制热","制冷","冷热自动","自动","除湿","送风"};
static void render_mode(uint8_t *fb){
    if(mode_slot<0||(unsigned)mode_slot>=cache_count)return;
    pico_app_view_t v=host->view();hlc_ent_t *ent=&cache[mode_slot];
    int x=(v.w-280)/2,y=v.landscape?24:130,h=v.landscape?312:390;
    host->fill(fb,0,0,v.w,v.h,13);host->box(fb,x,y,280,h,12,15,true);host->box(fb,x,y,280,h,12,3,false);
    host->text(fb,x+16,y+16,17,"空调模式",1);
    int row=0;for(unsigned i=0;i<7;i++)if(ent->hvac_modes&(1u<<i)){
        int rh=v.landscape?25:38;host->button(fb,x+16,y+48+row*(rh+3),248,rh,mode_labels[i],hl_same(ent->state,mode_names[i]));row++;
    }
    host->button(fb,x+16,y+h-44,248,32,"取消",false);
}
// 滑杆接触先于排序与翻页处理；抬手才发送一次服务请求。/ Sliders precede sorting/paging and submit only once on lift.
static bool control_event(const pico_app_event_t *e){
    if(page!=PAGE_MONITOR)return false;
    if(mode_slot>=0){
        if(e->type==PGA_CANCEL){mode_slot=-1;host->dirty();return true;}
        if(e->type!=PGA_TAP)return true;
        pico_app_view_t v=host->view();int x=(v.w-280)/2,y=v.landscape?24:130,row=0,selected=-1;
        hlc_ent_t *ent=&cache[mode_slot];
        for(unsigned i=0;i<7;i++)if(ent->hvac_modes&(1u<<i)){
            int rh=v.landscape?25:38,ry=y+48+row*(rh+3);
            if(e->x>=x+16&&e->x<x+264&&e->y>=ry&&e->y<ry+rh)selected=(int)i;
            row++;
        }
        if(selected>=0){bool ok=host->home_link_control(ent->id,"set_hvac_mode",(float)selected);host->toast(ok?"已提交模式，等待设备确认":"模式提交失败");}
        mode_slot=-1;host->dirty();return true;
    }
    if(control_suppress){if(e->type==PGA_PRESS)control_suppress=false;else return true;}
    int disp[HLC_MAX],n=0;build_display(disp,&n,false);
    int slot=brightness_slot>=0?brightness_slot:card_at(host->view(),e->x,e->y);
    if(slot<0||slot>=n)return false;
    int index=disp[slot],x,y,w,h;hlc_ent_t *ent=&cache[index];
    card_rect(host->view(),slot-(int)(entity_page*cards_per_page(host->view())),&x,&y,&w,&h);
    if(ent->kind==2&&ent->hvac_modes&&e->type==PGA_TAP&&e->x>=x+w-62&&e->y>=y+43&&e->y<y+71){mode_slot=index;host->dirty();return true;}
    bool slider=ent->kind==0&&ent->dimmable&&ent->available&&e->x>=x+6&&e->x<x+w-6&&e->y>=y+h-92&&e->y<y+h-56;
    if(brightness_slot<0&&(!slider||(e->type!=PGA_PRESS&&e->type!=PGA_TAP)))return false;
    if(e->type==PGA_CANCEL){brightness_slot=-1;control_suppress=true;host->dirty();return true;}
    if(e->type==PGA_PRESS||e->type==PGA_MOVE||e->type==PGA_RELEASE||e->type==PGA_TAP){
        brightness_slot=slot;int value=(e->x-x-12)*100/(w-24);if(value<0)value=0;if(value>100)value=100;
        if(value!=brightness_value){brightness_value=value;host->dirty();}
        if(e->type==PGA_RELEASE||e->type==PGA_TAP){
            bool ok=host->home_link_control(ent->id,"set_brightness",(float)value);
            brightness_slot=-1;control_suppress=true;host->toast(ok?"已提交亮度，等待设备确认":"亮度提交失败");host->dirty();
        }
        return true;
    }
    return true;
}
// Capture only the switch itself; never let its drag page or reorder cards.
// Commit once on release, without optimistic HA state changes or animated EPD redraws.
static bool switch_event(const pico_app_event_t *e){
    if(e->type==PGA_PRESS)switch_suppress=false;
    else if(switch_suppress)return true;
    if(switch_slot>=0){
        if(e->type==PGA_RELEASE){
            int dx=e->x-switch_x,dy=e->y-switch_y;bool next=dx>=8?true:dx<=-8?false:!switch_on;
            bool valid=(dy<0?-dy:dy)<=24&&(dx>=8||dx<=-8||(e->x>=switch_left&&e->x<switch_left+54&&e->y>=switch_top&&e->y<switch_top+28));
            if(valid){
                if(switch_slot>=100){
                    if(switch_slot==100)config.enabled=next;
                    else if(switch_slot==101)config.external_enabled=next;
                    else config.external_control=next;
                    host->dirty();
                }else if(next!=switch_on){
                    bool ok=host->home_link_control(switch_id,next?"turn_on":"turn_off",0);
                    host->toast(ok?"已提交，等待设备确认":"控制失败，请稍后重试");
                }
            }
            switch_slot=-1;switch_suppress=true;return true;
        }
        if(e->type==PGA_CANCEL||e->type==PGA_LONG){switch_slot=-1;switch_suppress=true;return true;}
        return true;
    }
    if(e->type!=PGA_PRESS||mode_slot>=0||brightness_slot>=0)return false;
    pico_app_view_t v=host->view();int left=0,top=0,slot=-1;bool on=false;
    if(page==PAGE_SETUP){
        int bw=(v.w-48)/3,fy=setup_flags_y(v),fh=v.landscape?26:40;
        for(int i=0;i<3;i++){
            int x=16+i*(bw+8)+bw-44;
            if(e->x>=x&&e->x<x+44&&e->y>=fy&&e->y<fy+fh){slot=100+i;left=x;top=fy+(fh-28)/2;on=i==0?config.enabled:i==1?config.external_enabled:config.external_control;break;}
        }
    }else if(page==PAGE_MONITOR&&has_home()&&drag_from<0){
        int display=card_at(v,e->x,e->y),disp[HLC_MAX],n=0;build_display(disp,&n,false);
        if(display>=0&&display<n){
            int x,y,w,h;card_rect(v,display-(int)(entity_page*cards_per_page(v)),&x,&y,&w,&h);
            hlc_ent_t *ent=&cache[disp[display]];left=x+w-64;top=action_y(y,h);
            if(ent->kind<4&&ent->available&&e->x>=left&&e->x<left+54&&e->y>=top&&e->y<top+28){slot=disp[display];on=ent->on;host->copy(switch_id,ent->id,sizeof(switch_id));}
        }
    }
    if(slot<0)return false;
    switch_slot=slot;switch_on=on;switch_x=e->x;switch_y=e->y;switch_left=left;switch_top=top;return true;
}
static bool event(const pico_app_event_t *e) {
    if (!e) return false;
    if(switch_event(e))return true;
    if(control_event(e))return true;
    if(page==PAGE_DEVICES&&has_selection())return catalog_event(e);
    if (e->type == PGA_PRESS) drag_suppress_tap = false;
    // 先处理卡片拖拽；拖拽中的移动不会再被横向翻页吃掉。
    // Handle card dragging first so its moves are never consumed by horizontal paging.
    if (monitor_drag_event(e)) return true;
    // 实体列表在拖动达到阈值时翻页，松手合成事件不重复翻。/ The entity list pages when the drag crosses the threshold; the synthesized lift event does not page again.
    if (monitor_scroll(e)) return true;
    if (e->type == PGA_TAP || e->type == PGA_SWIPE_L || e->type == PGA_SWIPE_R) {
        if (page == PAGE_SETUP) return event_setup(e);
        if(page==PAGE_ROOMS){
            pico_app_view_t v=host->view();unsigned per=manage_per(v),pages=rooms.count?(rooms.count+per-1)/per:1;
            if(e->type==PGA_SWIPE_L||e->type==PGA_SWIPE_R){
                if(e->type==PGA_SWIPE_L&&room_page+1<pages)room_page++;
                else if(e->type==PGA_SWIPE_R&&room_page)room_page--;
                host->dirty();return true;
            }
            if(hit_bottom3(e,0)){page=room_return;host->dirty();return true;}
            if(hit_bottom3(e,1)){
                if(!rooms.busy){rooms.bytes=sizeof(rooms);(void)host->home_rooms(PGA_ROOMS_SCAN,&rooms);room_message[0]=0;host->dirty();}return true;
            }
            if(hit_bottom3(e,2)){
                if(rooms.busy||rooms.result!=2){host->toast("请先获取房间列表");return true;}
                room_submit=rooms;room_submit.selected=room_selection;
                if(host->home_rooms(PGA_ROOMS_SET,&room_submit)){
                    refresh_status();
                    if(status.flags&PGA_HOME_PENDING){page=PAGE_MONITOR;host->toast("房间草稿已保存，等待主机确认后选择卡片");}
                    else {rooms=room_submit;manage_begin();entity_page=0;if(!has_selection())request_sync();}
                }else host->format(room_message,sizeof(room_message),"%s",room_submit.error[0]?room_submit.error:"保存失败，请重新获取房间");
                host->dirty();return true;
            }
            if(!rooms.busy)for(unsigned i=0;i<per&&room_page*per+i<rooms.count;i++){
                int x,y,w;manage_rect(v,i,&x,&y,&w);
                if(e->x>=x&&e->x<x+w&&e->y>=y&&e->y<y+40){room_selection^=1u<<(room_page*per+i);room_message[0]=0;host->dirty();return true;}
            }
            return true;
        }
        if(page==PAGE_DEVICES){
            pico_app_view_t v=host->view();unsigned per=manage_per(v),pages=cache_count?(cache_count+per-1)/per:1;
            if(e->type==PGA_SWIPE_L||e->type==PGA_SWIPE_R){
                if(e->type==PGA_SWIPE_L&&manage_page+1<pages)manage_page++;
                else if(e->type==PGA_SWIPE_R&&manage_page)manage_page--;
                host->dirty();return true;
            }
            if(hit_bottom3(e,0)){page=PAGE_MONITOR;host->dirty();return true;}
            if(hit_bottom3(e,1)){if(has_rooms())rooms_begin();else host->toast("请更新系统以选择房间");return true;}
            if(hit_bottom3(e,2)){
                if(host->store&&host->store("homehidden1",&hidden_draft,sizeof(hidden_draft))){
                    hidden=hidden_draft;page=PAGE_MONITOR;entity_page=0;host->dirty();
                }else host->toast("保存失败，显示选择尚未变更");
                return true;
            }
            for(unsigned i=0;i<per&&manage_page*per+i<cache_count;i++){
                int x,y,w;manage_rect(v,i,&x,&y,&w);
                if(e->x>=x&&e->x<x+w&&e->y>=y&&e->y<y+40){visibility_toggle(cache[manage_page*per+i].hash);host->dirty();return true;}
            }
            return true;
        }
        return event_monitor(e);
    }
    return false;
}

static bool key(int k) {
    if (k == -1) {
        if(mode_slot>=0){mode_slot=-1;host->dirty();return true;}
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
    int64_t clock=host->time_us?host->time_us():0;
    if(page==PAGE_DEVICES&&has_selection()&&catalog.busy&&clock-catalog_poll_us>=250000){
        catalog_poll_us=clock;catalog.bytes=sizeof(catalog);(void)host->home_catalog(PGA_CATALOG_GET,&catalog);host->dirty();
    }
    if(page==PAGE_MONITOR&&clock>=sync_due_us&&!status.busy&&(status.flags&PGA_HOME_CFG))need_sync=true;
    if (cache_revision != status.revision || (cache_count == 0 && status.entity_count > 0)) refresh_cache();
    int disp[HLC_MAX], n = 0;
    build_display(disp, &n, false);
    int per = cards_per_page(host->view());
    unsigned pages = n ? (unsigned)((n + per - 1) / per) : 1;
    unsigned last_page = pages - 1;
    if (entity_page > last_page) { entity_page = last_page; host->dirty(); }
    if(page==PAGE_ROOMS&&rooms.busy){
        int64_t now=host->time_us?host->time_us():0;
        if(now<=0||now-room_poll_us>=250000){room_poll_us=now;rooms_refresh();}
    }
    if (need_sync && page==PAGE_MONITOR && has_home() && (status.flags & PGA_HOME_WIFI) && !status.busy) request_sync();
    return status.busy||rooms.busy;
}

static void action(int a) { (void)a; }
static uint32_t status_bits(void) { return status.busy||rooms.busy ? PGA_STATUS_BUSY : 0; }
static bool save(void) { return true; }
static void stop(void) {
    if (has_home() && host->home_link_input) (void)host->home_link_input(PGA_HOME_INPUT_CANCEL, draft, sizeof(draft));
    waiting_input = false;brightness_slot=mode_slot=switch_slot=-1;switch_suppress=false;control_suppress=false;
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

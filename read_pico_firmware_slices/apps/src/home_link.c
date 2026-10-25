/*
 * SPDX-FileCopyrightText: 2026 XH Lab
 * SPDX-License-Identifier: Apache-2.0
 * 中文：家庭连接 TF 工具包（kind 130）；经宿主 ABI 监看 HA/打印机，含 Cloudflare Access。
 * English: Home-link TF package (kind 130); monitors HA/printers via host ABI, including Cloudflare Access.
 * 薄宿主 manage 仍为 home_module_missing；本包用设备侧配置+宿主 HTTP，不声称手机 manage 已接通。
 * Thin-host manage stays home_module_missing; this package uses on-device config + host HTTP.
 */
#include "pico_app_sdk.h"
#include "../../main/system/pico_scroll.h"

enum { HOME_LINK_KIND = 130, HOME_LINK_VERSION = 10101 };
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

static void refresh_status(void) {
    if (!has_home()) return;
    status.bytes = sizeof(status);
    (void)host->home_link(PGA_HOME_STATUS, &status);
    if (status.revision != last_revision) {
        last_revision = status.revision;
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

static bool save_config(void) {
    if (!has_home()) return false;
    config.bytes = sizeof(config);
    bool ok = host->home_link_config(PGA_HOME_CONFIG_SET, &config);
    host->set(config.ha_token, 0, sizeof(config.ha_token));
    host->set(config.cf_access_client_id, 0, sizeof(config.cf_access_client_id));
    host->set(config.cf_access_client_secret, 0, sizeof(config.cf_access_client_secret));
    if (ok) {
        host->toast("已保存到设备");
        need_sync = true;
        load_config();
        request_sync();
    } else host->toast("保存失败（检查地址/令牌）");
    return ok;
}

static void open_input(int kind, const char *seed, const char *title_unused) {
    (void)title_unused;
    if (!has_home() || !host->home_link_input) {
        host->toast("宿主不支持输入");
        return;
    }
    draft_kind = kind;
    host->format(draft, sizeof(draft), "%s", seed ? seed : "");
    waiting_input = host->home_link_input(PGA_HOME_INPUT_OPEN, draft, sizeof(draft));
    if (!waiting_input) host->toast("无法打开输入");
}

static void take_input(void) {
    if (!waiting_input || !has_home()) return;
    if (!host->home_link_input(PGA_HOME_INPUT_TAKE, draft, sizeof(draft))) return;
    waiting_input = false;
    load_config();
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
    waiting_input = need_sync = false;
    last_revision = 0;
    host->set(&status, 0, sizeof(status));
    host->set(&config, 0, sizeof(config));
    if (has_home()) {
        load_config();
        refresh_status();
        if ((status.flags & PGA_HOME_CFG) && (status.flags & PGA_HOME_WIFI)) need_sync = true;
    }
    return true;
}

static int entities_per_page(pico_app_view_t v) {
    int rows = (v.h - 200) / 52;
    return rows > 1 ? rows : 1;
}

static void render_monitor(uint8_t *fb) {
    pico_app_view_t v = host->view();
    host->fill(fb, 0, 0, v.w, v.h, 15);
    host->heading(fb, "家庭连接", status.busy ? "同步中…" : (status.at_home ? "本地" : (status.external ? "外部" : "")));
    char line[160];
    if (!has_home()) {
        host->wrap(fb, 24, 120, v.w - 48, 14, 6,
                   "当前固件宿主过旧，缺少 home_link 服务。请刷含 PGA 桥的固件后再打开本工具包。", 5);
        host->button(fb, 16, v.h - 56, v.w - 32, 40, "返回", false);
        return;
    }
    host->format(line, sizeof(line), "%s · 实体 %u/%u · Wi‑Fi %s", status.error,
                 (unsigned)status.entity_count, (unsigned)status.entity_total,
                 (status.flags & PGA_HOME_WIFI) ? "已连接" : "未连接");
    host->wrap(fb, 24, 108, v.w - 48, 13, 3, line, 5);
    if (status.result == 12 /* RP_HOME_ACCESS */)
        host->wrap(fb, 24, 150, v.w - 48, 13, 3,
                   "Cloudflare Access 拦截：请在设置中填写 Service Token（非浏览器登录）。", 0);
    else if (!(status.flags & PGA_HOME_CFG))
        host->wrap(fb, 24, 150, v.w - 48, 13, 3, "尚未配置。点「设置」填写本地/外部地址、HA 令牌与可选 Access。", 5);

    int per = entities_per_page(v);
    int y = 190;
    for (int i = 0; i < per; i++) {
        unsigned idx = (unsigned)(entity_page * per + i);
        if (idx >= status.entity_count) break;
        pico_app_home_entity_t e = {.bytes = sizeof(e)};
        if (!host->home_link_entity(idx, &e)) break;
        host->fill(fb, 16, y, v.w - 32, 48, 14);
        host->text(fb, 28, y + 8, 16, e.name[0] ? e.name : e.id, 0);
        host->format(line, sizeof(line), "%s%s%s", e.state, e.unit[0] ? " " : "", e.unit);
        host->text(fb, 28, y + 28, 13, line, 5);
        if (e.kind < 4 && e.available)
            host->button(fb, v.w - 120, y + 8, 88, 32, e.on ? "关闭" : "开启", false);
        y += 52;
    }
    int bw = (v.w - 48) / 3;
    host->button(fb, 16, v.h - 56, bw, 40, "设置", false);
    host->button(fb, 24 + bw, v.h - 56, bw, 40, status.busy ? "…" : "同步", status.busy);
    host->button(fb, 32 + 2 * bw, v.h - 56, bw, 40, "打印机", false);
}

static void render_setup(uint8_t *fb) {
    pico_app_view_t v = host->view();
    host->fill(fb, 0, 0, v.w, v.h, 15);
    host->heading(fb, "连接设置", "设备本地保存");
    host->wrap(fb, 24, 110, v.w - 48, 12, 4,
               "手机 manage 在薄宿主仍返回 home_module_missing。此处配置仅存本机，供本工具包使用。", 5);
    char line[192];
    int y = 170;
    const char *labels[] = {"本地地址", "外部 HTTPS", "HA 令牌", "CF Client ID", "CF Client Secret", "家庭 Wi‑Fi"};
    for (int i = 0; i < 6; i++) {
        host->format(line, sizeof(line), "%s：%s", labels[i],
                     i == 0   ? (config.local[0] ? config.local : "（未设）")
                     : i == 1 ? (config.external[0] ? config.external : "（未设）")
                     : i == 2 ? (config.has_token ? "已保存" : "（未设）")
                     : i == 3 ? (config.has_cf_id ? "已保存" : "（未设）")
                     : i == 4 ? (config.has_cf_secret ? "已保存" : "（未设）")
                              : (config.home_wifi[0] ? config.home_wifi : "（未设）"));
        host->button(fb, 16, y, v.w - 32, 36, line, setup_field == (unsigned)i);
        y += 42;
    }
    host->format(line, sizeof(line), "启用 %s · 外部 %s · 外控 %s · Access %s",
                 config.enabled ? "开" : "关", config.external_enabled ? "开" : "关",
                 config.external_control ? "开" : "关",
                 (config.has_cf_id && config.has_cf_secret) ? "已配置" : "无");
    host->text(fb, 24, y + 8, 13, line, 5);
    int bw = (v.w - 48) / 3;
    host->button(fb, 16, v.h - 56, bw, 40, "开关项", false);
    host->button(fb, 24 + bw, v.h - 56, bw, 40, "保存", false);
    host->button(fb, 32 + 2 * bw, v.h - 56, bw, 40, "返回", false);
}

static void render_printers(uint8_t *fb) {
    pico_app_view_t v = host->view();
    host->fill(fb, 0, 0, v.w, v.h, 15);
    host->heading(fb, "打印机", "只读监看");
    host->wrap(fb, 24, 110, v.w - 48, 12, 3,
               "独立 Bambu MQTT/云端仍待完整迁入；此处显示宿主快照中的打印机槽位与 HA 导入状态。", 5);
    char line[160];
    int y = 170;
    if (!has_home()) {
        host->text(fb, 24, y, 16, "宿主服务不可用", 0);
    } else {
        for (unsigned slot = 0; slot < 3; slot++) {
            pico_app_home_printer_t p = {.bytes = sizeof(p)};
            if (!host->home_link_printer(slot, &p)) continue;
            host->fill(fb, 16, y, v.w - 32, 64, 14);
            host->format(line, sizeof(line), "%u · %s", slot + 1, p.name[0] ? p.name : "空槽位");
            host->text(fb, 28, y + 10, 16, line, 0);
            host->format(line, sizeof(line), "%s%s%s", p.state,
                         p.percent >= 0 ? " · " : "", p.percent >= 0 ? "" : "");
            if (p.percent >= 0)
                host->format(line, sizeof(line), "%s · %d%%", p.state[0] ? p.state : "无状态", p.percent);
            else
                host->format(line, sizeof(line), "%s", p.has_data ? p.state : "无数据（LAN/云端 MQTT 未迁入 PGA）");
            host->text(fb, 28, y + 36, 13, line, 5);
            y += 72;
        }
    }
    host->button(fb, 16, v.h - 56, v.w - 32, 40, "返回监看", false);
}

static void render(uint8_t *fb) {
    take_input();
    refresh_status();
    if (need_sync && has_home() && (status.flags & PGA_HOME_WIFI) && !status.busy) request_sync();
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
    if (!has_home()) {
        if (e->y >= v.h - 56) {
            host->navigate(PGA_PAGE_LAUNCHER);
            return true;
        }
        return false;
    }
    int per = entities_per_page(v);
    int y = 190;
    for (int i = 0; i < per; i++) {
        unsigned idx = (unsigned)(entity_page * per + i);
        if (idx >= status.entity_count) break;
        if (e->y >= y && e->y < y + 48 && e->x >= v.w - 120) {
            pico_app_home_entity_t ent = {.bytes = sizeof(ent)};
            if (host->home_link_entity(idx, &ent) && ent.kind < 4) {
                bool ok = host->home_link_control(ent.id, ent.on ? "turn_off" : "turn_on", 0);
                host->toast(ok ? "已提交" : "控制失败");
                if (ok) request_sync();
                return true;
            }
        }
        y += 52;
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
    if (e->type == PGA_SWIPE_L && (entity_page + 1) * (unsigned)per < status.entity_count) {
        entity_page++;
        host->dirty();
        return true;
    }
    if (e->type == PGA_SWIPE_R) {
        if (entity_page) {
            entity_page--;
            host->dirty();
        } else host->navigate(PGA_PAGE_LAUNCHER);
        return true;
    }
    return false;
}

static bool event_setup(const pico_app_event_t *e) {
    int y = 170;
    for (int i = 0; i < 6; i++) {
        if (e->y >= y && e->y < y + 36) {
            setup_field = (unsigned)i;
            if (i == 0) open_input(0, config.local, "本地");
            else if (i == 1) open_input(1, config.external, "外部");
            else if (i == 2) open_input(2, "", "令牌");
            else if (i == 3) open_input(3, "", "CF ID");
            else if (i == 4) open_input(4, "", "CF Secret");
            else open_input(5, config.home_wifi, "WiFi");
            return true;
        }
        y += 42;
    }
    if (hit_bottom3(e, 0)) {
        /* cycle: enabled -> external_enabled -> external_control */
        if (!config.enabled) config.enabled = 1;
        else if (!config.external_enabled) config.external_enabled = 1;
        else if (!config.external_control) config.external_control = 1;
        else {
            config.enabled = config.external_enabled = config.external_control = 0;
        }
        host->dirty();
        return true;
    }
    if (hit_bottom3(e, 1)) {
        config.enabled = 1;
        save_config();
        return true;
    }
    if (hit_bottom3(e, 2) || e->type == PGA_SWIPE_R) {
        page = PAGE_MONITOR;
        host->dirty();
        return true;
    }
    return false;
}

static bool monitor_scroll(const pico_app_event_t *e) {
    if (page != PAGE_MONITOR || !has_home()) return false;
    bool consume = false;
    int d = ps_scroll_step(&entity_scroll, e->type, e->x0, e->y0, e->x, e->y, PS_SCROLL_AXIS_X, &consume);
    if (!d && !consume) return false;
    if (!d) return true;
    int per = entities_per_page(host->view());
    if (d > 0 && (entity_page + 1) * (unsigned)per < status.entity_count) {
        entity_page++;
        host->dirty();
        return true;
    }
    if (d < 0) {
        if (entity_page) {
            entity_page--;
            host->dirty();
        } else
            host->navigate(PGA_PAGE_LAUNCHER);
        return true;
    }
    ps_scroll_disarm(&entity_scroll);
    return false;
}

static bool event(const pico_app_event_t *e) {
    if (!e) return false;
    // 实体列表在拖动达到阈值时翻页，松手合成事件不重复翻。/ The entity list pages when the drag crosses the threshold; the synthesized lift event does not page again.
    if (monitor_scroll(e)) return true;
    if (e->type == PGA_TAP || e->type == PGA_SWIPE_L || e->type == PGA_SWIPE_R) {
        if (page == PAGE_SETUP) return event_setup(e);
        if (page == PAGE_PRINTERS) {
            if (e->type == PGA_TAP || e->type == PGA_SWIPE_R) {
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
    refresh_status();
    return status.busy;
}

static void action(int a) { (void)a; }
static uint32_t status_bits(void) { return status.busy ? PGA_STATUS_BUSY : 0; }
static bool save(void) { return true; }
static void stop(void) {
    if (has_home() && host->home_link_input) (void)host->home_link_input(PGA_HOME_INPUT_CANCEL, draft, sizeof(draft));
    waiting_input = false;
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

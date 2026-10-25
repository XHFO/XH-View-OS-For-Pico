/* 家庭薄宿主空状态；完整监看 UI 由 TF tools/home-link.pga 提供。
 * Thin-host empty home UI; full monitoring UI comes from TF tools/home-link.pga.
 * 手机扫码填写走局域网门户。mode 为 pga 时本页给出入口。
 * Phone setup uses the LAN portal. This page exposes it when mode is pga.
 */
#include "pico_home_portal.h"
typedef struct {
    uint32_t magic, hash[RP_HOME_ENTITIES];
    uint8_t hidden[RP_HOME_ENTITIES], order[RP_HOME_ENTITIES];
} link_ui_t;
static link_ui_t link_ui;
static int real_page, real_room, real_rooms_page, real_printer, real_import_page;
static bool real_room_picker, real_import, real_manage, thin_portal;

static void link_sync_ui(void) {}
static int link_entities(int *out, bool all) { (void)out; (void)all; return 0; }

static void thin_portal_close(void) {
    if (!thin_portal) return;
    thin_portal = false;
    ps_home_portal_stop();
    ui_wifi_qr_clear();
}
static bool thin_portal_back(void) {
    if (!thin_portal) return false;
    thin_portal_close();
    return true;
}

static void thin_empty(uint8_t *fb, const char *title, const char *body) {
    ps_view_t v = ps_view();
    if (thin_portal) {
        ps_home_portal_view_t portal;
        ps_home_portal_view(&portal);
        ps_fill(fb, 0, 0, v.w, v.h, 15);
        ps_button(fb, 16, 20, 72, 36, "关闭", false);
        ps_heading(fb, "手机填写", portal.phase);
        int side = v.landscape ? 180 : 220;
        int x = (v.w - side) / 2, y = v.landscape ? 88 : 132;
        if (portal.active && portal.url[0]) ui_wifi_qr_draw(fb, ps_rect(x, y, side, side));
        else ps_wrap(fb, 24, 140, v.w - 48, 16, 4, portal.error[0] ? portal.error : "请先连接 Wi-Fi", 0);
        ps_wrap(fb, 24, y + side + 28, v.w - 48, 13, 4,
                "同一 Wi-Fi 下用手机扫码。酒店或客户端隔离的热点可能打不开页面。", 5);
        return;
    }
    ps_heading(fb, title, NULL);
    ps_wrap(fb, 24, 120, v.w - 48, 16, 4, body, MUTED);
    ps_button(fb, 16, v.h - 104, v.w - 32, 40, "打开家庭工具包", false);
    ps_button(fb, 16, v.h - 56, v.w - 32, 40, "手机扫码填写", true);
}

static bool thin_open_module(void) {
    extern bool ps_apps_open(const char *id);
    if (ps_apps_open("home-link")) return true;
    ps_toast("请将 home-link.pga 复制到 TF apps/tools 后刷新");
    return false;
}
static bool thin_open_portal(void) {
    if (!ps_home_portal_start(PS_HOME_PORTAL_OWNER_HOME)) {
        ps_home_portal_view_t portal;
        ps_home_portal_view(&portal);
        ps_toast(portal.error[0] ? portal.error : "请先连接 Wi-Fi");
        thin_portal = true;
        return true;
    }
    ps_home_portal_view_t portal;
    ps_home_portal_view(&portal);
    if (!portal.url[0] || !ui_wifi_qr_prepare_url(portal.url)) {
        ps_home_portal_stop();
        ps_toast("无法生成二维码");
        return false;
    }
    thin_portal = true;
    return true;
}

static bool thin_hit(const ps_event_t *e) {
    if (!e || e->type != PS_TAP) return false;
    ps_view_t v = ps_view();
    if (thin_portal) {
        thin_portal_close();
        return true;
    }
    if (ps_hit(e->x, e->y, 16, v.h - 104, v.w - 32, 40)) return thin_open_module(), true;
    if (ps_hit(e->x, e->y, 16, v.h - 56, v.w - 32, 40)) return thin_open_portal(), true;
    return false;
}

static void real_ha_render(uint8_t *fb) {
    thin_empty(fb, "家庭", "固件保留空状态。可打开 TF 家庭工具包，或用手机扫码填写 Home Assistant 与打印机。");
}
static bool real_ha_event(const ps_event_t *e) { return thin_hit(e); }
static void real_ha_settings_render(uint8_t *fb) {
    thin_empty(fb, "家庭设置", "HA 地址、令牌和 Cloudflare Access 可在手机表单填写，并写入本机。");
}
static bool real_ha_settings_event(const ps_event_t *e) { return thin_hit(e); }
static void real_printer_render(uint8_t *fb, bool settings) {
    thin_empty(fb, settings ? "打印机设置" : "打印机",
               "HA 打印机实体前缀可在手机表单填写。Bambu MQTT 仍不在此页配置。");
}
static bool real_printer_event(const ps_event_t *e, bool settings) {
    (void)settings;
    return thin_hit(e);
}
static void real_manage_close(void) { real_manage = false; }
static bool real_manage_sync(void) { return false; }
static void real_printer_cancel(void) { real_import = false; printer_view = PV_LIST; }

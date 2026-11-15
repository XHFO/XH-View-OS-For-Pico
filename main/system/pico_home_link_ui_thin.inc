/* 家庭薄宿主空状态；完整监看 UI 由 TF tools/home-link.pga 提供。
 * Thin-host empty home UI; full monitoring UI comes from TF tools/home-link.pga.
 * 家庭进入监看、设置进入配置、打印机进入打印机页；缺包时才显示安装与手机填写入口。
 * Home opens monitoring, Settings opens setup, and Printers opens printers; missing packages expose install/phone setup.
 */
#include "pico_home_portal.h"
#include "pico_i18n.h"

/* Matches home-link pages and PGA_HOME_ENTRY result: 0 monitor, 1 setup, 2 printers. */
enum { THIN_HOME_MONITOR, THIN_HOME_SETUP, THIN_HOME_PRINTERS };

void ps_apps_prepare_home(uint8_t page);
void ps_apps_expect_home_route(ps_route_t route);
bool ps_apps_open(const char *id);

static bool thin_portal;

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

static bool thin_open_home(uint8_t page, bool toast) {
    ps_apps_prepare_home(page);
    if (ps_apps_open(page==THIN_HOME_PRINTERS?"printer-link":"home-link")) return true;
    ps_apps_prepare_home(0xff);
    if (toast) ps_toast(ps_tr("settings.home_link.missing"));
    return false;
}

static bool thin_open_portal(uint8_t page) {
    if (!ps_home_portal_start(PS_HOME_PORTAL_OWNER_HOME)) {
        ps_home_portal_view_t portal;
        ps_home_portal_view(&portal);
        ps_toast(portal.error[0] ? portal.error : "请先连接 Wi-Fi");
        thin_portal = true;
        return true;
    }
    ps_home_portal_set_printers(page==THIN_HOME_PRINTERS);
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
    ps_i18n_chip(fb, v.w - 16 - PS_I18N_CHIP_W, 32);
    ps_wrap(fb, 24, 120, v.w - 48, 16, 4, body, MUTED);
    ps_button(fb, 16, v.h - 104, v.w - 32, 40, ps_tr("settings.home_link.open"), false);
    ps_button(fb, 16, v.h - 56, v.w - 32, 40, "手机填写", true);
}

static bool thin_chip(const ps_event_t *e) {
    ps_view_t v = ps_view();
    return ps_i18n_chip_hit(e->x, e->y, v.w - 16 - PS_I18N_CHIP_W, 32);
}

static bool thin_hit_page(const ps_event_t *e, uint8_t page) {
    if (!e || e->type != PS_TAP) return false;
    if (thin_portal) {
        if (ps_hit(e->x, e->y, 16, 20, 72, 36)) thin_portal_close();
        return true;
    }
    if (thin_chip(e)) return true;
    ps_view_t v = ps_view();
    if (ps_hit(e->x, e->y, 16, v.h - 104, v.w - 32, 40)) return thin_open_home(page, true), true;
    if (ps_hit(e->x, e->y, 16, v.h - 56, v.w - 32, 40)) return thin_open_portal(page), true;
    return false;
}

static void real_ha_render(uint8_t *fb) {
    thin_empty(fb, ps_tr("settings.home_link.title"), "下载家庭连接应用后，可连接 Home Assistant 并选择在本机显示的设备。");
}
static bool real_ha_event(const ps_event_t *e) { return thin_hit_page(e, THIN_HOME_MONITOR); }
static void real_ha_settings_render(uint8_t *fb) {
    thin_empty(fb, ps_tr("settings.home_link.settings_title"), ps_tr("settings.home_link.settings_empty"));
}
static bool real_ha_settings_event(const ps_event_t *e) { return thin_hit_page(e, THIN_HOME_SETUP); }
static void real_printer_render(uint8_t *fb, bool settings) {
    thin_empty(fb, ps_tr(settings ? "settings.home_link.printers_settings" : "settings.home_link.printers_title"),
               "请在应用集中下载独立的 3D 打印机应用。原有连接配置会保留，无需安装家庭连接应用。");
}
static bool real_printer_event(const ps_event_t *e, bool settings) {
    (void)settings;
    return thin_hit_page(e, THIN_HOME_PRINTERS);
}

/* 家庭薄宿主空状态；完整监看 UI 由 TF tools/home-link.pga 提供。
 * Thin-host empty home UI; full monitoring UI comes from TF tools/home-link.pga.
 * 包已在 TF 时，家庭/设置直接进入连接设置，不再停在本页中转。
 * When the package is on TF, Home/Settings open connection setup and do not stop on this page.
 */
/* Matches home-link pages and PGA_HOME_ENTRY result: 0 monitor, 1 setup, 2 printers. */
enum { THIN_HOME_SETUP = 1, THIN_HOME_PRINTERS = 2 };

/* Declarations only. Weak stubs in this file would be inlined and ignore the real launcher. */
void ps_apps_prepare_home(uint8_t page);
void ps_apps_expect_home_route(ps_route_t route);
bool ps_apps_open(const char *id);

static bool thin_open_home(uint8_t page, bool toast) {
    ps_apps_prepare_home(page);
    if (ps_apps_open("home-link")) return true;
    ps_apps_prepare_home(0xff);
    if (toast) ps_toast("请将 home-link.pga 复制到 TF apps/tools 后刷新");
    return false;
}
typedef struct {
    uint32_t magic, hash[RP_HOME_ENTITIES];
    uint8_t hidden[RP_HOME_ENTITIES], order[RP_HOME_ENTITIES];
} link_ui_t;
static link_ui_t link_ui;
static int real_page, real_room, real_rooms_page, real_printer, real_import_page;
static bool real_room_picker, real_import, real_manage;

static void link_sync_ui(void) {}
static int link_entities(int *out, bool all) { (void)out; (void)all; return 0; }

static void thin_empty(uint8_t *fb, const char *title, const char *body) {
    ps_view_t v = ps_view();
    ps_heading(fb, title, NULL);
    ps_wrap(fb, 24, 140, v.w - 48, 16, 4, body, MUTED);
    ps_button(fb, 16, v.h - 56, v.w - 32, 40, "打开家庭工具包", true);
}


static void real_ha_render(uint8_t *fb) {
    thin_empty(fb, "家庭", "固件仅保留空状态入口。安装 TF 工具包 home-link 后可监看 Home Assistant 与打印机。");
}
static bool real_ha_event(const ps_event_t *e) {
    if (!e || e->type != PS_TAP) return false;
    ps_view_t v = ps_view();
    if (ps_hit(e->x, e->y, 16, v.h - 56, v.w - 32, 40)) return thin_open_home(THIN_HOME_SETUP, true), true;
    return false;
}
static void real_ha_settings_render(uint8_t *fb) {
    thin_empty(fb, "家庭设置", "连接配置随家庭工具包提供。未安装包时无法保存 HA 令牌或打印机凭据。");
}
static bool real_ha_settings_event(const ps_event_t *e) {
    if (!e || e->type != PS_TAP) return false;
    ps_view_t v = ps_view();
    if (ps_hit(e->x, e->y, 16, v.h - 56, v.w - 32, 40)) return thin_open_home(THIN_HOME_SETUP, true), true;
    return false;
}
static void real_printer_render(uint8_t *fb, bool settings) {
    thin_empty(fb, settings ? "打印机设置" : "打印机",
               "打印机只读监看已迁至 TF 工具包。未安装时本页保持空状态，不影响阅读。");
}
static bool real_printer_event(const ps_event_t *e, bool settings) {
    (void)settings;
    if (!e || e->type != PS_TAP) return false;
    ps_view_t v = ps_view();
    if (ps_hit(e->x, e->y, 16, v.h - 56, v.w - 32, 40)) return thin_open_home(THIN_HOME_PRINTERS, true), true;
    return false;
}
static void real_manage_close(void) { real_manage = false; }
static bool real_manage_sync(void) { return false; }
static void real_printer_cancel(void) { real_import = false; printer_view = PV_LIST; }

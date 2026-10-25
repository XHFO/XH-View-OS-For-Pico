/* 家庭薄宿主空状态；完整监看 UI 由 TF tools/home-link.pga 提供。
 * Thin-host empty home UI; full monitoring UI comes from TF tools/home-link.pga.
 */
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

static bool thin_open_module(void) {
    extern bool ps_apps_open(const char *id);
    if (ps_apps_open("home-link")) return true;
    ps_toast("请将 home-link.pga 复制到 TF apps/tools 后刷新");
    return false;
}

static void real_ha_render(uint8_t *fb) {
    thin_empty(fb, "家庭", "固件仅保留空状态。安装 apps/tools/home-link.pga 后在设备上配置 HA。手机 manage 仍返回 home_module_missing。");
}
static bool real_ha_event(const ps_event_t *e) {
    if (!e || e->type != PS_TAP) return false;
    ps_view_t v = ps_view();
    if (ps_hit(e->x, e->y, 16, v.h - 56, v.w - 32, 40)) return thin_open_module(), true;
    return false;
}
static void real_ha_settings_render(uint8_t *fb) {
    thin_empty(fb, "家庭设置", "连接配置随家庭工具包提供。未安装包时无法保存 HA 令牌或打印机凭据。");
}
static bool real_ha_settings_event(const ps_event_t *e) {
    if (!e || e->type != PS_TAP) return false;
    ps_view_t v = ps_view();
    if (ps_hit(e->x, e->y, 16, v.h - 56, v.w - 32, 40)) return thin_open_module(), true;
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
    if (ps_hit(e->x, e->y, 16, v.h - 56, v.w - 32, 40)) return thin_open_module(), true;
    return false;
}
static void real_manage_close(void) { real_manage = false; }
static bool real_manage_sync(void) { return false; }
static void real_printer_cancel(void) { real_import = false; printer_view = PV_LIST; }

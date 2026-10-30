/* SPDX-License-Identifier: Apache-2.0
 * 蓝牙设置读取快照；外部按键只在UI线程转交阅读器。/ Snapshot-driven Bluetooth settings; deliver input on the UI thread.
 * 冻结：不自动配对未知设备，不在阅读弹窗或休眠时执行按键。/ Frozen: Never auto-pair unknown peers or act in reading overlays/sleep.
 */
#include "pico_bluetooth.h"
#include "pico_dual.h"
#include "app_book.h"
#include "read_pico_ble_pager.h"
#include <stdio.h>

static uint32_t s_revision;
static int s_device_page;
static bool s_scan_when_ready;

static void result(esp_err_t error) {
    if (error != ESP_OK) ps_toast("操作未完成，请稍后重试");
}
void ps_bluetooth_init(void) { (void)read_pico_ble_pager_init(); }
void ps_bluetooth_enter(void) { s_device_page = 0; }
static int rows(void) { return ps_view().landscape ? 2 : 3; }
static int current_page(unsigned count) {
    int pages=((int)count+rows()-1)/rows();
    return pages>0 && s_device_page<pages ? s_device_page : pages>0 ? pages-1 : 0;
}
static EpdRect device_rect(int index) {
    ps_view_t v = ps_view();
    int cols = v.landscape ? 2 : 1;
    int width = (v.w - 32 - (cols - 1) * 12) / cols;
    return (EpdRect){16 + (index % cols) * (width + 12),
                     (v.landscape ? 222 : 376) + (index / cols) * 54, width, 48};
}
void ps_bluetooth_render(uint8_t *fb) {
    ps_view_t v = ps_view();
    read_pico_ble_pager_status_t s; read_pico_ble_pager_get_status(&s);
    ps_heading(fb, "蓝牙翻页器", NULL);
    if(ps_dual_enabled()) {
        ps_text(fb,20,104,14,"双机阅读正在使用蓝牙",1);
        ps_text(fb,20,142,12,"关闭双机模式后恢复外接翻页器设置",3);
        ps_button(fb,20,194,v.w-40,42,"打开双机阅读",false);return;
    }
    int split = v.landscape ? v.w / 2 : v.w;
    ps_button(fb, 16, 82, split - 32, 42, s.enabled ? "蓝牙已开启 · 点击关闭" : "开启蓝牙", s.enabled);
    int y = v.landscape ? 82 : 138;
    int x = v.landscape ? split + 4 : 16, width = v.landscape ? split - 20 : v.w - 32;
    ps_box(fb, x, y, width, v.landscape ? 42 : 70, 5, 12, true);
    const char *state = !s.enabled ? s.error ? "蓝牙关闭未完成" : read_pico_ble_pager_light_sleep_safe() ? "蓝牙已关闭" : "正在关闭蓝牙…" : !s.ready ? "正在启动蓝牙…" :
        s.connecting ? "正在连接和配对…" : s.connected ? "已连接" : s.scanning ? "正在查找翻页器…" : "未连接翻页器";
    ps_text_fixed(fb, x + 10, y + 7, 13, state, 2);
    if (!v.landscape) ps_text_fixed(fb, x + 10, y + 34, 11, s.name[0] ? s.name : "开启翻页器电源，并进入配对模式", 4);
    int controls = v.landscape ? 136 : 222;
    int cw = (v.w - 56) / 3;
    ps_button(fb, 16, controls, cw, 40, s.scanning ? "正在查找" : "查找设备", false);
    ps_button(fb, 28 + cw, controls, cw, 40, "断开连接", false);
    ps_button(fb, 40 + 2*cw, controls, cw, 40, "移除配对", false);
    int reverse_y = v.landscape ? 184 : 278;
    ps_button(fb, 16, reverse_y, v.landscape ? v.w / 2 - 24 : v.w - 32, v.landscape ? 30 : 40,
              s.reversed ? "按键方向：已反转" : "按键方向：默认", s.reversed);
    if (!v.landscape) {
        ps_text_fixed(fb, 16, 334, 11, s.simulated ? "网页为模拟设备，未访问真实蓝牙" : "支持 BLE HID 按键，连接后自动记忆", 4);
        ps_text_fixed(fb, 16, 353, 10, s.error ? (!s.enabled ? "关闭失败，请重启设备后再试" : "连接异常，请将翻页器重新设为配对模式") : "仅在阅读正文时翻页；不支持经典蓝牙", 5);
    } else ps_text_fixed(fb, v.w / 2 + 4, 194, 10, s.simulated ? "BLE模拟 · 不连接设备" : "仅支持BLE HID按键", 5);
    read_pico_ble_pager_device_t devices[READ_PICO_BLE_PAGER_MAX_DEVICES];
    size_t count = read_pico_ble_pager_get_devices(devices, READ_PICO_BLE_PAGER_MAX_DEVICES);
    int per = rows();
    int page=current_page((unsigned)count);
    for (int i = 0; i < per && page * per + i < (int)count; i++) {
        int n = page * per + i; EpdRect r = device_rect(i);
        ps_box(fb, r.x, r.y, r.width, r.height, 4, 10, false);
        ps_wrap(fb, r.x + 10, r.y + 5, r.width - 20, 13, 1, devices[n].name, 2);
        char line[64]; snprintf(line, sizeof(line), "%d dBm · 点击连接", devices[n].rssi);
        ps_text_fixed(fb, r.x + 10, r.y + 27, 10, line, 5);
    }
    if (!count) ps_text_fixed(fb, 24, device_rect(0).y + 16, 12,
        s.scanning ? "扫描中，请稍候…" : "点击查找设备，选择你的翻页器", 5);
    int bottom = v.h - 64;
    if (count > (size_t)per) {
        ps_button(fb, 16, bottom, 88, 32, "上一页", false);
        char p[32]; snprintf(p, sizeof(p), "%d / %d", page + 1, ((int)count + per - 1) / per);
        ps_center(fb, v.w/2, bottom + 10, 11, p, 5);
        ps_button(fb, v.w - 104, bottom, 88, 32, "下一页", false);
    }
}
bool ps_bluetooth_event(const ps_event_t *e) {
    ps_view_t v = ps_view();
    if (e->type != PS_TAP) return false;
    if(ps_dual_enabled()) { if(ps_hit(e->x,e->y,20,194,v.w-40,42))ps_navigate(PS_DUAL);return true; }
    read_pico_ble_pager_status_t s; read_pico_ble_pager_get_status(&s);
    s_device_page=current_page(s.device_count);
    int split = v.landscape ? v.w/2 : v.w;
    if (ps_hit(e->x,e->y,16,82,split-32,42)) {
        s_scan_when_ready = !s.enabled;
        result(read_pico_ble_pager_set_enabled(!s.enabled)); return true;
    }
    int y = v.landscape ? 136 : 222, cw = (v.w - 56)/3;
    if (ps_hit(e->x,e->y,16,y,cw,40)) {
        if (s.scanning || s.connecting) return true;
        s_device_page = 0;
        if (!s.enabled || !s.ready) { s_scan_when_ready=true; result(read_pico_ble_pager_set_enabled(true)); }
        else result(read_pico_ble_pager_scan());
        return true;
    }
    if (ps_hit(e->x,e->y,28+cw,y,cw,40)) { result(read_pico_ble_pager_disconnect()); return true; }
    if (ps_hit(e->x,e->y,40+2*cw,y,cw,40)) { result(read_pico_ble_pager_forget()); return true; }
    if (ps_hit(e->x,e->y,16,v.landscape?184:278,v.landscape?v.w/2-24:v.w-32,v.landscape?30:40)) {
        result(read_pico_ble_pager_set_reversed(!s.reversed)); return true;
    }
    if (e->y >= v.h-64 && e->y < v.h-32) {
        int pages = (s.device_count + rows() - 1) / rows();
        if (pages > 1) s_device_page = (s_device_page + (e->x < v.w/2 ? pages-1 : 1)) % pages;
        return true;
    }
    for (int i=0; i<rows(); i++) { EpdRect r=device_rect(i);
        if (ps_hit(e->x,e->y,r.x,r.y,r.width,r.height)) {
            int index=s_device_page*rows()+i;
            if (index < s.device_count) result(read_pico_ble_pager_connect((size_t)index));
            return true;
        }
    }
    return false;
}
bool ps_bluetooth_tick(void) {
    if(ps_dual_enabled()) { s_scan_when_ready=false;return false; }
    read_pico_ble_pager_status_t s; read_pico_ble_pager_get_status(&s);
    if (s_scan_when_ready && s.enabled && s.ready && !s.connecting && !s.connected) {
        s_scan_when_ready=false; result(read_pico_ble_pager_scan());
    }
    if (!s.enabled) s_scan_when_ready=false;
    bool changed=s_revision!=s.revision; s_revision=s.revision;
    return changed && ps_route()==PS_BLUETOOTH;
}
bool ps_bluetooth_input(app_ctx_t *ctx, const app_desc_t *app, bool blocked, app_redraw_t *redraw) {
    int direction=read_pico_ble_pager_poll_event();
    if (!direction) return false;
    if (!ctx || !app || !redraw || blocked || ctx->consumed || ctx->pressed || ctx->released ||
        ctx->request_app || ctx->request_return || ctx->request_menu || ctx->request_developer_menu ||
        (ctx->touch && ctx->touch->touched) || ps_storage_busy() || ps_usb_busy()) return false;
    if (app==&app_book) *redraw=app_book_external_turn(ctx,direction);
    else return false;
    return *redraw!=APP_REDRAW_NONE;
}

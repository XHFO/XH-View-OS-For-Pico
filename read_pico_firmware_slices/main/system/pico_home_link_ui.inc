/* 真实家庭服务界面，只在事件/tick排队；绘制读取已提交快照。
 * Real home services: events/ticks queue work; rendering only reads committed snapshots.
 * 冻结：没有配置时为空状态，绝不把示例视为真实连接；凭据仅NVS/私有宿主配置。
 * Frozen: Unconfigured services show empty state, never fixture connectivity; credentials stay in NVS/private host
 * settings.
 */
typedef struct {
    uint32_t magic, hash[RP_HOME_ENTITIES];
    uint8_t hidden[RP_HOME_ENTITIES], order[RP_HOME_ENTITIES];
} link_ui_t;
static link_ui_t link_ui;
static int real_page, real_room, real_rooms_page, real_printer, real_import_page;
static bool real_room_picker, real_import, real_manage;
static read_pico_management_status_t real_management;
static char real_qr_url[192];
static bool real_qr_ready;
static char real_rooms[RP_HOME_ROOMS][40];
static int real_room_count;
static uint32_t link_hash(const char *s) {
    uint32_t h = 2166136261u;
    for (; *s; s++) {
        h ^= (unsigned char)*s;
        h *= 16777619u;
    }
    return h ? h : 1;
}
static bool link_at_home(void) {
    int w = ps_wifi_connected_index();
    return w >= 0 && *link_config.home_wifi && !strcmp(link_config.home_wifi, ps_wifi_name(w));
}
static void link_request(void) {
    link_due = 0;
    read_pico_home_link_request(ps_wifi_connected_index() >= 0, link_at_home());
    link_due = ps_now() + 30000;
}
static bool link_save(void) {
    if (!read_pico_home_link_configure(&link_config)) {
        read_pico_home_link_get_config(&link_config);
        ps_toast("配置无效或保存失败，原配置已保留");
        return false;
    }
    link_request();
    return true;
}
static void link_ui_save(void) {
    if (!ps_store("home-real-ui", &link_ui, sizeof(link_ui)))
        ps_toast("面板设置保存失败");
    save_home();
}
static void link_sync_ui(void) {
    link_ui_t previous = link_ui;
    bool ordered[RP_HOME_ENTITIES] = {0};
    real_room_count = 0;
    for (int i = 0; i < link_snapshot.entity_count; i++) {
        rp_home_entity_t *e = &link_snapshot.entities[i];
        uint32_t h = link_hash(e->id);
        link_ui.hash[i] = h;
        link_ui.hidden[i] = 0;
        for (int old = 0; old < RP_HOME_ENTITIES; old++)
            if (previous.hash[old] == h) {
                link_ui.hidden[i] = previous.hidden[old];
                break;
            }
        bool found = false;
        for (int r = 0; r < real_room_count; r++)
            if (!strcmp(real_rooms[r], e->room))
                found = true;
        if (!found && real_room_count < RP_HOME_ROOMS)
            copy_text(real_rooms[real_room_count++], 40, e->room);
    }
    int next = 0;
    for (int p = 0; p < RP_HOME_ENTITIES; p++) {
        int old = previous.order[p];
        if (old >= RP_HOME_ENTITIES || !previous.hash[old])
            continue;
        for (int i = 0; i < link_snapshot.entity_count; i++)
            if (!ordered[i] && link_ui.hash[i] == previous.hash[old]) {
                link_ui.order[next++] = (uint8_t)i;
                ordered[i] = true;
                break;
            }
    }
    for (int i = 0; i < link_snapshot.entity_count; i++)
        if (!ordered[i])
            link_ui.order[next++] = (uint8_t)i;
    if (real_room > real_room_count)
        real_room = 0;
}
static int link_entities(int *out, bool all) {
    int n = 0;
    bool seen[RP_HOME_ENTITIES] = {0};
    for (int p = 0; p < link_snapshot.entity_count; p++) {
        int id = hc.manual ? link_ui.order[p] : p;
        if (id >= link_snapshot.entity_count || seen[id])
            id = p;
        seen[id] = true;
        rp_home_entity_t *e = &link_snapshot.entities[id];
        if (!all && (link_ui.hidden[id] || (hc.types && !(hc.types & (1u << e->kind))) ||
                     (hc.unavailable && !e->available) || (real_room && strcmp(e->room, real_rooms[real_room - 1]))))
            continue;
        out[n++] = id;
    }
    if (!hc.manual)
        for (int i = 1; i < n; i++) {
            int id = out[i], j = i;
            while (j > 0) {
                rp_home_entity_t *a = &link_snapshot.entities[id], *b = &link_snapshot.entities[out[j - 1]];
                int cmp = hc.name_sort ? strcmp(a->name, b->name) : strcmp(a->room, b->room);
                if (!cmp)
                    cmp = (int)a->kind - b->kind;
                if (cmp >= 0)
                    break;
                out[j] = out[j - 1];
                j--;
            }
            out[j] = id;
        }
    return n;
}
static const char *link_status(void) {
    if (!link_config.enabled)
        return "尚未启用真实连接";
    if (link_snapshot.busy)
        return "正在同步 · 保留当前画面";
    if (link_snapshot.result == RP_HOME_OK && !*link_config.ha_token)
        return "地址已响应 · 等待令牌验证";
    return rp_home_result_text(link_snapshot.result);
}
static void real_ha_render(uint8_t *fb) {
    ps_view_t v = ps_view();
    int list[RP_HOME_ENTITIES], n = link_entities(list, false), per = entities_per_page();
    if (real_room_picker) {
        ps_heading(fb, "家庭房间", NULL);
        int rows = v.landscape ? 2 : 5, start = real_rooms_page * rows;
        for (int i = 0; i < rows && start + i < real_room_count; i++)
            ps_button(fb, 16, 112 + i * 56, v.w - 32, 46, real_rooms[start + i], real_room == start + i + 1);
        if (!real_room_count)
            ps_wrap(fb, 20, 140, v.w - 40, 13, 3, "同步后自动按 HA 房间归组。手机配置页可为未归组实体指定房间。",
                    MUTED);
        page_bar(fb, real_rooms_page, real_room_count ? (real_room_count + rows - 1) / rows : 1);
        return;
    }
    if (climate_entity >= 0 && climate_entity < link_snapshot.entity_count) {
        rp_home_entity_t *e = &link_snapshot.entities[climate_entity];
        char b[48];
        ps_heading(fb, "空调控制", NULL);
        ps_wrap(fb, 20, 100, v.w - 40, 16, 1, e->name, BLACK);
        snprintf(b, sizeof(b), "%.0f°C", e->number);
        ps_center(fb, v.w / 2, 151, 40, b, BLACK);
        ps_button(fb, 20, 223, 80, 44, "− 1°C", false);
        ps_button(fb, v.w - 100, 223, 80, 44, "＋ 1°C", false);
        ps_button(fb, v.w / 2 - 48, 223, 96, 44, e->on ? "关闭" : "开启", e->on);
        ps_wrap(fb, 20, 288, v.w - 40, 11, 2, "操作完成后同步服务器状态；不提前伪造开关结果。", MUTED);
        return;
    }
    ps_text(fb, 18, 38, 21, "家庭控制", BLACK);
    ps_button(fb, v.w - 78, 34, 60, 34, "设置", false);
    ps_button(fb, 16, v.landscape ? 70 : 84, (v.w - 40) / 2, 32, "自定义面板", !real_room);
    ps_button(fb, 24 + (v.w - 40) / 2, v.landscape ? 70 : 84, (v.w - 40) / 2, 32,
              real_room ? real_rooms[real_room - 1] : "家庭房间", real_room > 0);
    for (int i = 0; i < per && real_page * per + i < n; i++) {
        rp_home_entity_t *e = &link_snapshot.entities[list[real_page * per + i]];
        rect_t r = entity_rect(i);
        char b[96];
        home_surface(fb, r.x, r.y, r.w, r.h, e->on && e->kind < 4);
        int badge = v.landscape ? 24 : 30;
        ps_box(fb, r.x + r.w - badge - 8, r.y + 9, badge, badge, badge / 2, e->on ? PS_THEME_SELECTED : PS_THEME_INSET,
               true);
        ps_icon(fb, r.x + r.w - badge - 4, r.y + 13, badge - 8, entity_icons[e->kind], e->on ? WHITE : MUTED);
        ps_wrap(fb, r.x + 10, r.y + 8, r.w - badge - 24, v.landscape ? 9 : 11, 1, e->name, BLACK);
        if (!e->available)
            copy_text(b, sizeof(b), "不可用");
        else if (e->kind >= 4)
            snprintf(b, sizeof(b), "%s %s", e->state, e->unit);
        else if (e->kind == RP_HOME_CLIMATE)
            snprintf(b, sizeof(b), "%s · %.0f°C", e->state, e->number);
        else
            copy_text(b, sizeof(b), e->on ? "已开启" : "已关闭");
        ps_wrap(fb, r.x + 10, r.y + (v.landscape ? 29 : 53), r.w - 20, v.landscape ? 12 : 17, 1, b, BLACK);
        if (!v.landscape)
            ps_wrap(fb, r.x + 10, r.y + 86, r.w - 20, 10, 1, e->room, MUTED);
    }
    if (!n) {
        ps_wrap(fb, 24, 158, v.w - 48, 16, 3, link_config.enabled ? "还没有可显示的设备" : "连接你的 Home Assistant",
                BLACK);
        ps_wrap(fb, 24, 229, v.w - 48, 11, 3,
                link_config.enabled ? link_status() : "进入设置后选择手机配置，扫码填写服务器地址和令牌。",
                MUTED);
    }
    page_bar(fb, real_page, n ? (n + per - 1) / per : 1);
}
static bool real_ha_event(const ps_event_t *e) {
    ps_view_t v = ps_view();
    if (real_room_picker) {
        int rows = v.landscape ? 2 : 5,
            next = page_action(e, real_rooms_page, real_room_count ? (real_room_count + rows - 1) / rows : 1);
        if (next != real_rooms_page) {
            real_rooms_page = next;
            return true;
        }
        if (e->type == PS_TAP)
            for (int i = 0; i < rows && real_rooms_page * rows + i < real_room_count; i++)
                if (ps_hit(e->x, e->y, 16, 112 + i * 56, v.w - 32, 46)) {
                    real_room = real_rooms_page * rows + i + 1;
                    real_room_picker = false;
                    real_page = 0;
                    return true;
                }
        return false;
    }
    if (climate_entity >= 0) {
        if (e->type != PS_TAP)
            return false;
        rp_home_entity_t *x = &link_snapshot.entities[climate_entity];
        const char *action = NULL;
        float value = x->number;
        if (ps_hit(e->x, e->y, 20, 223, 80, 44)) {
            action = "set_temperature";
            value -= 1;
        } else if (ps_hit(e->x, e->y, v.w - 100, 223, 80, 44)) {
            action = "set_temperature";
            value += 1;
        } else if (ps_hit(e->x, e->y, v.w / 2 - 48, 223, 96, 44))
            action = x->on ? "turn_off" : "turn_on";
        if (action) {
            bool ok = read_pico_home_link_control(x, action, value, link_at_home());
            ps_toast(ok ? "已提交，正在等待服务器" : "连接忙碌或不可用，请稍后重试");
            return true;
        }
        return false;
    }
    if (e->type == PS_TAP) {
        if (ps_hit(e->x, e->y, v.w - 78, 34, 60, 34)) {
            ps_navigate(PS_HA_SETTINGS);
            return true;
        }
        if (ps_hit(e->x, e->y, 16, v.landscape ? 70 : 84, (v.w - 40) / 2, 32)) {
            real_room = real_page = 0;
            return true;
        }
        if (ps_hit(e->x, e->y, 24 + (v.w - 40) / 2, v.landscape ? 70 : 84, (v.w - 40) / 2, 32)) {
            real_room_picker = true;
            real_rooms_page = 0;
            return true;
        }
    }
    int list[RP_HOME_ENTITIES], n = link_entities(list, false), per = entities_per_page(),
                                next = page_action(e, real_page, n ? (n + per - 1) / per : 1);
    if (next != real_page) {
        real_page = next;
        return true;
    }
    if (e->type != PS_TAP && e->type != PS_LONG)
        return false;
    for (int i = 0; i < per && real_page * per + i < n; i++)
        if (in_rect(e, entity_rect(i))) {
            int id = list[real_page * per + i];
            rp_home_entity_t *x = &link_snapshot.entities[id];
            if (e->type == PS_LONG && x->kind == RP_HOME_CLIMATE) {
                climate_entity = id;
                return true;
            }
            if (x->kind >= 4) {
                ps_toast("温湿度传感器只读");
                return true;
            }
            bool ok = read_pico_home_link_control(x, x->on ? "turn_off" : "turn_on", 0, link_at_home());
            ps_toast(ok ? "已提交，等待服务器确认" : "请检查连接或等待当前任务完成");
            return true;
        }
    return false;
}
// 管理服务与二维码只在事件／tick准备；绘制只读已提交状态。
// Prepare the management service and QR only in events/ticks; rendering reads committed state.
static void real_manage_close(void) {
    if (real_manage) {
        read_pico_management_stop();
        // 等手机请求退出后重取配置，避免返回后使用旧槽位。/ Refresh after joining phone requests to avoid stale slots on return.
        read_pico_home_link_get_config(&link_config);
    }
    real_manage = false;
    real_qr_ready = false;
    ui_wifi_qr_clear();
    memset(real_qr_url, 0, sizeof(real_qr_url));
    memset(&real_management, 0, sizeof(real_management));
}
static int real_manage_expiry(const read_pico_management_status_t *s) {
    return !s->active ? 0 : s->seconds_left <= 30 ? -1 : (int)((s->seconds_left + 59) / 60);
}
static bool real_manage_sync(void) {
    read_pico_management_status_t next = {0};
    read_pico_management_get_status(&next);
    bool changed = next.active != real_management.active ||
                   strcmp(next.url, real_management.url) || strcmp(next.error, real_management.error) ||
                   strcmp(next.host, real_management.host) ||
                   real_manage_expiry(&next) != real_manage_expiry(&real_management);
    if (!next.active || !*next.url) {
        if (real_qr_ready || *real_qr_url) ui_wifi_qr_clear();
        real_qr_ready = false;
        memset(real_qr_url, 0, sizeof(real_qr_url));
    } else if (strcmp(next.url, real_qr_url)) {
        real_qr_ready = ui_wifi_qr_prepare_url(next.url);
        copy_text(real_qr_url, sizeof(real_qr_url), next.url);
    }
    real_management = next;
    return changed;
}
static void real_manage_begin(void) {
    real_manage = true;
    read_pico_management_start();
    real_manage_sync();
}
static void real_manage_render(uint8_t *fb) {
    ps_view_t v = ps_view();
    ps_heading(fb, "手机配置", NULL);
    int qx = v.landscape ? 36 : 84, qy = v.landscape ? 88 : 142, side = v.landscape ? 168 : 192;
    int x = v.landscape ? 248 : 20, width = v.landscape ? v.w - 268 : v.w - 40;
    int top = v.landscape ? 88 : 350;
    bool connected = ps_wifi_connected_index() >= 0;
    if (real_qr_ready && real_management.active)
        ui_wifi_qr_draw(fb, ps_rect(qx, qy, side, side));
    else {
        home_surface(fb, qx, qy, side, side, true);
        ps_icon(fb, qx + side / 2 - 20, qy + 50, 40, connected ? PI_LINK : PI_WIFI, MUTED);
        ps_center(fb, qx + side / 2, qy + 118, 12,
                  !connected ? "先连接 Wi-Fi" : real_management.active ? "二维码生成失败" : "管理入口未开启", MUTED);
    }
    ps_wrap(fb, x, top, width, 17, 2,
            real_management.active ? "手机与阅读器连接同一 Wi-Fi" : connected ? "在手机上完成详细连接配置" : "先让阅读器连接家庭 Wi-Fi", BLACK);
    ps_wrap(fb, x, top + 52, width, 11, 3,
            real_management.active ? "扫码配置 Home Assistant、Bambu 与 Formlabs。" :
            *real_management.error ? real_management.error : "连接成功后可生成临时管理二维码。", MUTED);
    if (real_management.active) {
        char expiry[64]; int minutes = real_manage_expiry(&real_management);
        if (minutes < 0) copy_text(expiry, sizeof(expiry), "链接即将到期，请刷新");
        else snprintf(expiry, sizeof(expiry), "约 %d 分钟后失效 · 离页即关闭", minutes);
        ps_wrap(fb, x, top + 102, width, 10, 2, expiry, MUTED);
        ps_wrap(fb, x, top + 133, width, 10, 1, real_management.host, MUTED);
        int y = v.h - 96, w = (v.w - 40) / 2;
        ps_button(fb, 16, y, w, 40, "刷新链接", false);
        ps_button(fb, 24 + w, y, w, 40, "停止管理", false);
    } else
        ps_button(fb, 16, v.h - 96, v.w - 32, 40, connected ? "开始管理" : "连接 Wi-Fi", true);
}
static bool real_manage_event(const ps_event_t *e) {
    if (e->type != PS_TAP) return false;
    ps_view_t v = ps_view(); int y = v.h - 96, w = (v.w - 40) / 2;
    if (!real_management.active && ps_hit(e->x, e->y, 16, y, v.w - 32, 40)) {
        if (ps_wifi_connected_index() < 0) { real_manage_close(); ps_navigate(PS_WIFI); }
        else real_manage_begin();
        return true;
    }
    if (real_management.active && ps_hit(e->x, e->y, 16, y, w, 40)) {
        read_pico_management_stop();
        real_manage_begin();
        return true;
    }
    if (real_management.active && ps_hit(e->x, e->y, 24 + w, y, w, 40)) {
        real_manage_close();
        return true;
    }
    return false;
}
static void real_ha_settings_render(uint8_t *fb) {
    ps_view_t v = ps_view();
    if (real_manage) { real_manage_render(fb); return; }
    ps_heading(fb, "Home Assistant", NULL);
    settings_tabs(fb);
    if (ha_tab == 0) {
        int height = v.landscape ? 64 : 112;
        home_surface(fb, 16, 132, v.w - 32, height, true);
        ps_wrap(fb, 28, 144, v.w - 56, 15, 1, link_status(), BLACK);
        char b[80];
        snprintf(b, sizeof(b), "%u / %u 标准实体%s", link_snapshot.entity_count, link_snapshot.entity_total,
                 link_snapshot.truncated ? " · 已达显示上限" : "");
        ps_wrap(fb, 28, 174, v.w - 56, 11, 1, b, MUTED);
        if (!v.landscape)
            ps_wrap(fb, 28, 208, v.w - 56, 11, 1,
                    *link_config.home_wifi ? link_config.home_wifi : "家庭 Wi-Fi 尚未指定", MUTED);
        int y = v.landscape ? 212 : 286, w = (v.w - 40) / 2;
        ps_button(fb, 16, y, w, 40, "手机配置", true);
        ps_button(fb, 24 + w, y, w, 40, "同步状态", false);
        ps_wrap(fb, 20, y + 54, v.w - 40, 11, v.landscape ? 2 : 3,
                "服务器地址、访问令牌与家庭网络在手机端配置。设备保留本地筛选与面板排序。", MUTED);
        return;
    }
    if (ha_tab == 1) {
        for (int i = 0; i < 6; i++)
            ps_button(fb, 16 + i * (v.w - 32) / 6, 132, (v.w - 32) / 6 - 3, 30, type_names[i],
                      !hc.types || (hc.types & (1u << i)));
        checkbox(fb, 18, 174, "排除不可用实体", hc.unavailable);
    } else {
        ps_button(fb, 16, 132, (v.w - 40) / 2, 32,
                  hc.manual      ? "手动排序"
                  : hc.name_sort ? "按名称排序"
                                 : "按房间排序",
                  hc.manual);
        ps_button(fb, 24 + (v.w - 40) / 2, 132, (v.w - 40) / 2, 32, hc.compact ? "紧凑布局" : "宽松布局", hc.compact);
    }
    int rows = v.landscape ? 1 : 4, list[RP_HOME_ENTITIES], n = link_entities(list, true),
        page = ha_tab == 1 ? filter_page : layout_page;
    for (int i = 0; i < rows && page * rows + i < n; i++) {
        int id = list[page * rows + i], y = 218 + i * 67;
        rp_home_entity_t *x = &link_snapshot.entities[id];
        home_surface(fb, 16, y, v.w - 32, 59, false);
        ps_wrap(fb, 26, y + 7, v.w - 145, 12, 1, x->name, BLACK);
        ps_wrap(fb, 26, y + 31, v.w - 145, 9, 1, x->room, MUTED);
        if (ha_tab == 1)
            ps_button(fb, v.w - 105, y + 10, 79, 34, link_ui.hidden[id] ? "已排除" : "显示", !link_ui.hidden[id]);
        else {
            ps_button(fb, v.w - 112, y + 10, 38, 34, "↑", false);
            ps_button(fb, v.w - 68, y + 10, 38, 34, "↓", false);
        }
    }
    if (!n)
        ps_wrap(fb, 20, 220, v.w - 40, 12, 3, "连接后显示真实实体。长按可打开手机配置，为实体指定房间。", MUTED);
    page_bar(fb, page, n ? (n + rows - 1) / rows : 1);
}
static bool real_ha_settings_event(const ps_event_t *e) {
    ps_view_t v = ps_view();
    if (real_manage) return real_manage_event(e);
    if (e->type == PS_TAP && ps_hit(e->x, e->y, 16, 83, v.w - 32, 34)) {
        ha_tab = clampi((e->x - 16) / ((v.w - 32) / 3), 0, 2);
        return true;
    }
    if (ha_tab == 0) {
        int y = v.landscape ? 212 : 286, w = (v.w - 40) / 2;
        if (e->type == PS_TAP && ps_hit(e->x, e->y, 16, y, w, 40)) { real_manage_begin(); return true; }
        if (e->type == PS_TAP && ps_hit(e->x, e->y, 24 + w, y, w, 40)) { link_request(); return true; }
        return false;
    }
    int rows = v.landscape ? 1 : 4, list[RP_HOME_ENTITIES], n = link_entities(list, true),
        *page = ha_tab == 1 ? &filter_page : &layout_page, next = page_action(e, *page, n ? (n + rows - 1) / rows : 1);
    if (next != *page) {
        *page = next;
        return true;
    }
    if (e->type == PS_TAP && ha_tab == 1) {
        if (ps_hit(e->x, e->y, 16, 132, v.w - 32, 30)) {
            hc.types ^= (uint8_t)(1u << clampi((e->x - 16) / ((v.w - 32) / 6), 0, 5));
            link_ui_save();
            return true;
        }
        if (ps_hit(e->x, e->y, 16, 170, v.w - 32, 30)) {
            hc.unavailable = !hc.unavailable;
            link_ui_save();
            return true;
        }
    }
    if (e->type == PS_TAP && ha_tab == 2 && ps_hit(e->x, e->y, 16, 132, v.w - 32, 32)) {
        if (e->x < v.w / 2) {
            if (hc.manual) {
                hc.manual = 0;
                hc.name_sort = 0;
            } else if (!hc.name_sort)
                hc.name_sort = 1;
            else
                hc.manual = 1;
        } else
            hc.compact = !hc.compact;
        link_ui_save();
        return true;
    }
    if (e->type != PS_TAP && e->type != PS_LONG)
        return false;
    for (int i = 0; i < rows && *page * rows + i < n; i++)
        if (ps_hit(e->x, e->y, 16, 218 + i * 67, v.w - 32, 59)) {
            int pos = *page * rows + i, id = list[pos];
            if (e->type == PS_LONG) {
                real_manage_begin();
            } else if (ha_tab == 1) {
                link_ui.hidden[id] = !link_ui.hidden[id];
                link_ui_save();
            } else {
                hc.manual = 1;
                int other = clampi(pos + (e->x < v.w - 73 ? -1 : 1), 0, n - 1);
                for (int j = 0; j < n; j++)
                    link_ui.order[j] = (uint8_t)list[j];
                link_ui.order[pos] = (uint8_t)list[other];
                link_ui.order[other] = (uint8_t)id;
                link_ui_save();
            }
            return true;
        }
    return false;
}
static const char *real_source_name(int source) {
    static const char *names[] = {"未配置", "Home Assistant", "Bambu 局域网", "Bambu 云端", "Formlabs 云端"};
    return names[clampi(source, 0, 4)];
}
static void real_printer_cancel(void) {
    real_import = false;
}
static void real_printer_render(uint8_t *fb, bool settings) {
    ps_view_t v = ps_view();
    if (real_manage) { real_manage_render(fb); return; }
    ps_heading(fb, settings ? "打印机连接" : "3D 打印监看", NULL);
    if (real_import) {
        int rows = v.landscape ? 2 : 5, start = real_import_page * rows;
        for (int i = 0; i < rows && start + i < link_snapshot.import_count; i++)
            ps_button(fb, 16, 120 + i * 52, v.w - 32, 43, link_snapshot.imports[start + i].name, false);
        if (!link_snapshot.import_count)
            ps_wrap(fb, 20, 135, v.w - 40, 13, 3, "尚未发现打印状态实体。请先配置 HA 并同步；兼容标准 Bambu 集成命名。",
                    MUTED);
        page_bar(fb, real_import_page, link_snapshot.import_count ? (link_snapshot.import_count + rows - 1) / rows : 1);
        return;
    }
    if (settings) {
        for (int i = 0; i < RP_HOME_PRINTERS; i++) {
            char b[96];
            const rp_home_printer_config_t *c = &link_config.printers[i];
            snprintf(b, sizeof(b), "%d · %s · %s", i + 1,
                     *c->name ? c->name : c->source ? "已配置打印机" : "空位", real_source_name(c->source));
            ps_button(fb, 16, 100 + i * (v.landscape ? 48 : 72), v.w - 32, v.landscape ? 40 : 60, b, pc.selected == i);
        }
        int y = v.landscape ? 264 : 350, w = (v.w - 40) / 2;
        ps_button(fb, 16, y, w, 40, "手机配置", true);
        ps_button(fb, 24 + w, y, w, 40, "从 HA 导入", false);
        if (!v.landscape)
            ps_wrap(fb, 20, 416, v.w - 40, 12, 4,
                    "先选择空位可直接导入 HA 打印机。品牌账号、令牌、证书与移除操作在手机管理页完成。", MUTED);
        return;
    }
    int id = clampi(pc.selected, 0, RP_HOME_PRINTERS - 1);
    rp_home_printer_t *p = &link_snapshot.printers[id];
    rp_home_printer_config_t *c = &link_config.printers[id];
    char b[160];
    ps_button(fb, 16, 83, v.w - 110, 34, *c->name ? c->name : "选择打印机", false);
    ps_button(fb, v.w - 86, 83, 70, 34, "设置", false);
    ps_text(fb, 20, 128, 10, real_source_name(c->source), MUTED);
    if (!c->source) {
        ps_wrap(fb, 24, 175, v.w - 48, 19, 2, "把打印进度，放在手边。", BLACK);
        ps_wrap(fb, 24, 245, v.w - 48, 12, 3,
                "进入设置后扫码添加打印机。推荐从 Home Assistant 导入，也支持 Bambu LAN/云端与 Formlabs 云端。", MUTED);
        return;
    }
    int main_w = v.landscape ? v.w / 2 - 20 : v.w - 32;
    home_surface(fb, 16, 155, main_w, v.landscape ? 110 : 205, false);
    snprintf(b, sizeof(b), p->has_data && p->percent >= 0 ? "%d%%" : "—", p->percent);
    ps_text(fb, 30, 169, v.landscape ? 40 : 58, b, BLACK);
    ps_wrap(fb, 30, v.landscape ? 222 : 247, main_w - 28, 15, 1, *p->state ? p->state : "等待首个真实状态", BLACK);
    ps_wrap(fb, 30, v.landscape ? 247 : 285, main_w - 28, 11, v.landscape ? 1 : 2,
            *p->task ? p->task : "任务名称尚未提供", MUTED);
    bool form = c->source == RP_HOME_FORMLABS;
    const char *labels[] = {"剩余分钟", "层数", form ? "连接" : "喷嘴 / 热床", form ? "树脂 / 粉末" : "耗材"};
    for (int i = 0; i < 4; i++) {
        int x = v.landscape ? v.w / 2 + 8 : 16 + (i % 2) * (v.w - 24) / 2,
            y = v.landscape ? 155 + i * 28 : 378 + (i / 2) * 65, w = v.landscape ? v.w / 2 - 24 : (v.w - 40) / 2;
        home_surface(fb, x, y, w, v.landscape ? 25 : 58, true);
        if (!p->has_data || (i == 0 && p->remaining_minutes < 0) || (i == 1 && (p->layer < 0 || p->layers < 0)))
            copy_text(b, sizeof(b), "—");
        else if (i == 0)
            snprintf(b, sizeof(b), "%d", p->remaining_minutes);
        else if (i == 1)
            snprintf(b, sizeof(b), "%d / %d", p->layer, p->layers);
        else if (i == 2) {
            if (form) copy_text(b, sizeof(b), p->connected ? "在线" : "离线");
            else if (p->nozzle < 0 || p->bed < 0) copy_text(b, sizeof(b), "—");
            else snprintf(b, sizeof(b), "%d / %d°C", p->nozzle, p->bed);
        }
        else
            copy_text(b, sizeof(b), p->material);
        ps_text(fb, x + 8, y + 5, 8, labels[i], MUTED);
        ps_wrap(fb, x + (v.landscape ? 83 : 8), y + (v.landscape ? 5 : 28), w - (v.landscape ? 90 : 16),
                v.landscape ? 10 : 14, 1, *b ? b : "—", BLACK);
    }
    ps_wrap(fb, 18, v.h - 88, v.w - 130, 10, 2,
            p->result == RP_HOME_OK ? (p->connected ? "真实状态 · 只读监看" : "设备离线 · 保留上次状态")
                                    : rp_home_result_text(p->result),
            MUTED);
    ps_button(fb, v.w - 106, v.h - 88, 88, 34, "同步", false);
}
static bool real_printer_event(const ps_event_t *e, bool settings) {
    ps_view_t v = ps_view();
    if (real_manage) return real_manage_event(e);
    if (real_import) {
        int rows = v.landscape ? 2 : 5,
            next = page_action(e, real_import_page,
                               link_snapshot.import_count ? (link_snapshot.import_count + rows - 1) / rows : 1);
        if (next != real_import_page) { real_import_page = next; return true; }
        if (e->type == PS_TAP)
            for (int i = 0; i < rows && real_import_page * rows + i < link_snapshot.import_count; i++)
                if (ps_hit(e->x, e->y, 16, 120 + i * 52, v.w - 32, 43)) {
                    const rp_home_printer_t *p = &link_snapshot.imports[real_import_page * rows + i];
                    rp_home_printer_config_t *c = &link_config.printers[real_printer];
                    if (c->source) { ps_toast("该位置已配置设备，请重新选择空位"); real_import = false; return true; }
                    if (strlen(p->serial) >= sizeof(c->serial) || !*p->serial) {
                        ps_toast("HA 实体前缀无效，无法导入此设备"); return true;
                    }
                    memset(c, 0, sizeof(*c)); c->source = RP_HOME_HA;
                    copy_text(c->name, sizeof(c->name), p->name);
                    copy_text(c->serial, sizeof(c->serial), p->serial);
                    link_config.enabled = 1;
                    if (link_save()) {
                        pc.selected = (uint8_t)real_printer; save_print(); real_import = false;
                        ps_toast("已导入，正在等待服务器状态");
                    }
                    return true;
                }
        return false;
    }
    if (e->type != PS_TAP) return false;
    if (settings) {
        for (int i = 0; i < RP_HOME_PRINTERS; i++)
            if (ps_hit(e->x, e->y, 16, 100 + i * (v.landscape ? 48 : 72), v.w - 32, v.landscape ? 40 : 60)) {
                pc.selected = (uint8_t)i; save_print(); return true;
            }
        int y = v.landscape ? 264 : 350, w = (v.w - 40) / 2;
        if (ps_hit(e->x, e->y, 16, y, w, 40)) { real_manage_begin(); return true; }
        if (ps_hit(e->x, e->y, 24 + w, y, w, 40)) {
            if (link_config.printers[pc.selected].source) ps_toast("请选择空位，或在手机端替换设备");
            else if (!link_config.enabled || !*link_config.ha_token) ps_toast("请先用手机配置 Home Assistant 连接");
            else { real_printer = pc.selected; real_import = true; real_import_page = 0; link_request(); }
            return true;
        }
        return false;
    }
    if (ps_hit(e->x, e->y, v.w - 86, 83, 70, 34)) {
        ps_navigate(PS_PRINTER_SETTINGS);
        return true;
    }
    if (ps_hit(e->x, e->y, 16, 83, v.w - 110, 34)) {
        pc.selected = (uint8_t)((pc.selected + 1) % RP_HOME_PRINTERS);
        save_print();
        return true;
    }
    if (ps_hit(e->x, e->y, v.w - 106, v.h - 88, 88, 34)) {
        link_request();
        return true;
    }
    return false;
}

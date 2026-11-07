/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 *
 * 事件循环。只做和页面无关的事：读触摸并去抖、三个实体键与菜单把手的路由、切页
 * 时调 on_exit/on_enter、按回调返回值决定怎么刷屏、电源键锁屏、无操作 2 分钟走
 * 同一锁屏、SD 卡上的字体延迟加载。页面自己的状态和刷新节奏都在 main/apps/ 各自的文件里。
 *
 * Event loop for page-agnostic work: touch debounce, the three keys and
 * the menu handle, on_exit/on_enter, present, lock, the same lock after
 * 2 minutes idle, delayed SD font load.
 * Page state and refresh cadence stay in main/apps/; gesture recognition is shared.
 *
 * 冻结：主循环只产出手势、不解释含义；只有 owns_keys 页可接管三键，
 * 且必须提供强刷和 request_menu 入口；把手仍按下即触发。
 * 接管页可选长按键回调，保留按下行为；同键单指500ms仅触发一次，中断取消。
 * Frozen: Produce gestures without interpreting page actions. KEY2 full refresh
 * and KEY3 menu are global unless owns_keys provides its own refresh/menu exits.
 * Owners may opt into a single 500ms same-key hold callback after the press action; interruptions cancel it.
 * The menu handle still fires on press unless the page explicitly owns its handle.
 * 统一系统页通过可选锁屏回调接管电源短按与空闲锁屏；这是通用契约，不判断具体页面。
 * Shells may opt into a generic lock callback for power-short/idle; the loop never inspects product routes.
 * 回调锁屏结束菜单覆盖，保证新锁屏仍接收计时和触摸。
 * Callback locking closes the menu overlay so the new lock face keeps receiving ticks and input.
 * 冻结：工程返回消费最近切页来源并恢复菜单位置；产品根跨子页保留，普通菜单/返回逐层回产品，只有明确开发请求可打开工程菜单。
 * Frozen: Legacy return consumes its latest origin and menu position; product roots survive nested pages, ordinary menu/back actions stay within the product, and only explicit developer requests open engineering menus.
 * 冻结：用户要求长按拖选/直接拖动进度；显式连续页的 PAGE/AREA 反馈不打断接触，认领立即生效；强刷、方向变化和全部全局中断仍取消。
 * Frozen: User-requested selection/progress dragging opts in to retain contacts through PAGE/AREA feedback and capture immediately; full refresh, rotation and every global interruption still cancel.
 * 冻结：用户授权开发测试开关只暂停空闲自动休眠，电源短按保持原锁屏行为。
 * Frozen: The user-authorized development switch pauses idle sleep only; short power-key locking is unchanged.
 * 冻结：产品根及全部子页共用电源控制器，保留当前应用实例；锁屏前取消输入，恢复后消费到可靠松手。
 * Frozen: Product roots and every child share the power controller without exiting their app; cancel input before locking and consume contacts until a reliable release after restoring.
 * 冻结：用户要求电源键每次点按都可切换；产品恢复后不重开两秒开机忽略窗口，完整SHORT已由电源控制器消费。
 * Frozen: Each user-requested power-key click toggles; product restoration never re-arms the two-second boot ignore window because the controller consumes a complete SHORT.
 * 冻结：用户要求产品电源长按打开操作菜单；完整LONG优先于SHORT，工程页保留原消费入口，不把KEY_DOWN当作点按。
 * Frozen: The user requests a product power menu on hold; complete LONG wins over SHORT, engineering keeps its legacy consumer, and KEY_DOWN is never a click.
 */

#include "app_loop.h"
#include "../system/pico_system.h"
#include "../system/pico_dual.h"
#include "../system/pico_power.h"
#include "../system/pico_sound.h"
#include "app_touch_buffer.h"
#include "app_touch_guard.h"

#include <string.h>
#include <stdio.h>

#include "app_registry.h"
#include "continuous_du.h"
#include "display.h"
#include "e0470_epaper_waveform.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "read_pico_board.h"
#include "read_pico_pmu.h"
#include "read_pico_sd.h"
#include "settings.h"
#include "sleep.h"
#include "ttf_font.h"
#include "ui_kit.h"
#include "ui_menu.h"
#include "ui_gesture.h"

#define TAG "app_loop"

// 触摸芯片报点率远高于屏幕，主循环空转一轮只等这么久。
// Touch reports far faster than the panel; idle this long per loop.
#define LOOP_TICK_MS 5
// SD 卡插上或者刚挂好时，隔一会儿再试一次存在设置里的字体。
// Retry the saved SD font shortly after a card appears or mounts.
#define FONT_RETRY_INTERVAL_MS 3000
#define MEDIA_POLL_INTERVAL_MS 500

static int64_t s_lock_ignore_until_ms;
static uint32_t s_font_media_revision;
// 双击电源后进入待解锁窗口；窗口内长按解除贴合锁定。
// Double-click arms a short window; a long press in that window releases the contact latch.
static int64_t s_guard_tap_at, s_guard_armed_until;
static uint8_t s_guard_taps;

void app_lock_ignore_for(int64_t ms) {
    s_lock_ignore_until_ms = esp_timer_get_time() / 1000 + ms;
}

void app_present(app_ctx_t* ctx, const app_desc_t* app, app_redraw_t redraw) {
    ps_global_page(ctx, app);
    // 字体绘制和EPD扫描期间仍保存编辑器短按；其余页保留原读取路径。/ Retain editor taps during font painting/EPD scans; other pages keep their original reader.
    app_touch_buffer_enable(ctx->tp,ps_keyboard_capture_active()&&!ps_power_pending(),epd_get_rotation());
    if (ps_global_present(ctx, app, redraw)) return;
    enum EpdDrawError result = EPD_DRAW_SUCCESS;
    if (app->present != NULL && app->present(ctx, redraw)) return;
    switch (redraw) {
        case APP_REDRAW_NONE:
        case APP_REDRAW_DONE:
            return;
        case APP_REDRAW_AREA: {
            // 回调已经画好了 fb，这里只负责把它那一块推上屏。
            // The callback already painted fb; push that rectangle only.
            EpdRect area = app->area_hint != NULL
                ? app->area_hint(ctx)
                : ui_content_refresh_area();
            result = update_display_area_with(
                ctx->hl, &E0470_WAVEFORM, APP_DYNAMIC_REFRESH_MODE, area
            );
            break;
        }
        case APP_REDRAW_FULL:
            if (app->render != NULL) app->render(ctx, ctx->fb);
            ps_global_draw_controls(app, ctx->fb);
            result = APP_PAGE_FORCE_FULL
                ? update_display_full(ctx->hl)
                : update_display_mode(ctx->hl, APP_PAGE_REFRESH_MODE);
            break;
        case APP_REDRAW_PAGE:
        default:
            if (app->render != NULL) app->render(ctx, ctx->fb);
            ps_global_draw_controls(app, ctx->fb);
            result = update_display_mode(ctx->hl, APP_PAGE_REFRESH_MODE);
            break;
    }
    guard_draw_result(ctx->hl, result);
}

// 中断边界先清页面装饰再清识别器。/ Clear decoration before resetting recognition.
static void cancel_gesture(app_ctx_t* ctx, const app_desc_t* app, ui_gesture_t* gesture) {
    if (gesture->active && app->on_gesture) {
        ui_gesture_event_t event = { .type = UI_GESTURE_CANCEL,
            .x0 = gesture->x0, .y0 = gesture->y0, .x = gesture->x, .y = gesture->y };
        const app_desc_t* requested = ctx->request_app;
        bool menu_requested = ctx->request_menu;
        bool developer_requested = ctx->request_developer_menu;
        bool return_requested = ctx->request_return;
        app_redraw_t redraw = app->on_gesture(ctx, &event);
        ctx->request_app = requested;
        ctx->request_menu = menu_requested;
        ctx->request_developer_menu = developer_requested;
        ctx->request_return = return_requested;
        ui_gesture_reset(gesture);
        app_present(ctx, app, redraw);
    }
    ui_gesture_reset(gesture);
    ctx->capture_gesture = false;
}

static void present_page(app_ctx_t* ctx, const app_desc_t* app,
                         ui_gesture_t* gesture, app_redraw_t redraw) {
    if (redraw == APP_REDRAW_FULL || (redraw == APP_REDRAW_PAGE && !app->continuous_gestures))
        cancel_gesture(ctx, app, gesture);
    app_present(ctx, app, redraw);
}

typedef struct {
    unsigned updates;
    int64_t last_ms;
    EpdRect area;
} menu_feedback_t;

// 菜单反馈只推叶内单行。/ Menu feedback pushes one local row only.
static void menu_feedback(app_ctx_t* ctx, const app_desc_t* current,
                          int leaf, int hit, bool pressed, menu_feedback_t* feedback) {
    EpdRect area;
    int row = hit - leaf * UI_MENU_ITEMS_PER_PAGE;
    if (!ui_menu_row_rect(leaf, row, &area)) return;
    ui_draw_menu_row_pressed(ctx->fb, current, leaf, row, pressed);
    if (feedback->updates == 0) feedback->area = area;
    else {
        int bottom = feedback->area.y + feedback->area.height;
        int next_bottom = area.y + area.height;
        if (area.y < feedback->area.y) feedback->area.y = area.y;
        feedback->area.height = (bottom > next_bottom ? bottom : next_bottom) - feedback->area.y;
    }
    feedback->updates++;
    feedback->last_ms = ctx->now_ms;
    guard_draw_result(ctx->hl, update_display_area_with(
        ctx->hl, &E0470_WAVEFORM, APP_DYNAMIC_REFRESH_MODE, area));
    if (feedback->updates >= UI_SETTLE_DU_MAX) {
        guard_draw_result(ctx->hl, update_display_area_with(
            ctx->hl, &E0470_WAVEFORM, MODE_GL16, feedback->area));
        feedback->updates = 0;
    }
}

// 切页：先让上一页收尾，再给新页一次 on_enter，最后整页画出来。
// Leave the old page, enter the new one, then present a full page.
static void switch_to(
    app_ctx_t* ctx, const app_desc_t** current, const app_desc_t* next
) {
    if (next == NULL || next == *current) return;
    if ((*current)->on_exit != NULL) (*current)->on_exit(ctx);
    *current = next;
    ps_global_page(ctx, next);
    if (next->on_enter != NULL) next->on_enter(ctx);
    app_present(ctx, next, next->enter_full ? APP_REDRAW_FULL : APP_REDRAW_PAGE);
    ESP_LOGI(TAG, "page -> %s", next->title);
}

// 菜单页不是 app，单独画。/ The menu is not an app; present it here.
static void present_menu(app_ctx_t* ctx, const app_desc_t* current, int leaf, menu_feedback_t* feedback) {
    feedback->updates = 0;
    if (display_take_white_exit()) {
        guard_draw_result(ctx->hl, update_display_white(ctx->hl));
    }
    ui_draw_menu_page(ctx->fb, current, leaf);
    guard_draw_result(ctx->hl, update_display_mode(ctx->hl, APP_PAGE_REFRESH_MODE));
}

// 空槽常带着抬起事件或残留坐标，不能进页面看到的快照；抬手后保留最后一次位置。
// Empty slots often carry lift events or stale coords; keep last position on lift.
static void debounce_touch(
    cst836u_touch_t* latest, const cst836u_touch_t* raw, bool released
) {
    if (raw->touched) {
        *latest = *raw;
        for (int i = 0; i < CST836U_MAX_POINTS; i++) {
            if (!latest->points[i].active) {
                memset(&latest->points[i], 0, sizeof(latest->points[i]));
            }
        }
    } else if (released) {
        latest->touched = false;
        latest->count = 0;
        for (int i = 0; i < CST836U_MAX_POINTS; i++) {
            latest->points[i].active = false;
        }
    }
    memcpy(latest->raw, raw->raw, sizeof(raw->raw));
}

static bool try_load_saved_sd_font(bool allow_probe) {
    static char previous_path[TTF_FONT_PATH_MAX];
    static uint32_t previous_generation;
    static uint32_t previous_media_revision;
    static unsigned attempts;
    static bool media_seen, was_ready;
    const char* path = app_settings_font_path();
    if (ttf_font_path_is_builtin(path)) { previous_path[0] = 0; attempts = 0; was_ready = false; return false; }
    if (ttf_font_sd_suspended()) { was_ready = false; return false; }
    read_pico_sd_info_t info = {0};
    esp_err_t err = read_pico_sd_get_info(&info);
    if (err == ESP_ERR_NOT_FINISHED) return false;
    bool ready = err == ESP_OK && info.present && info.mounted && !info.exported;
    bool changed = media_seen && (info.media_generation != previous_generation || previous_media_revision != s_font_media_revision);
    if (strcmp(previous_path, path) || changed || (ready && !was_ready)) {
        snprintf(previous_path, sizeof(previous_path), "%s", path);
        attempts = 0;
    }
    previous_generation = info.media_generation;
    previous_media_revision = s_font_media_revision;
    media_seen = true; was_ready = ready;
    if (!ready) {
        if (allow_probe && err == ESP_ERR_INVALID_STATE) read_pico_sd_start_probe();
        return false;
    }
    if (!changed && ttf_font_ready() && !ttf_font_is_builtin() && strcmp(ttf_font_path(), path) == 0) return false;
    if (attempts >= 3) return false;
    attempts++;
    return ttf_font_open(path) == ESP_OK && ttf_font_ready() && !ttf_font_is_builtin() && strcmp(ttf_font_path(), path) == 0;
}

// 先停文件使用者，再换字体；此处只观察挂载，不卸载也不重新挂载。
// Stop file consumers before font fallback; observe mounting without unmounting or remounting.
static bool poll_media(app_ctx_t* ctx, const app_desc_t* current,
                       bool* mounted, bool* invalidated) {
    read_pico_sd_info_t info = {0};
    esp_err_t err = read_pico_sd_get_info(&info);
    if (err == ESP_ERR_NOT_FINISHED) return false;
    bool ready = err == ESP_OK && info.present && info.mounted && !info.exported;
    bool lost = *mounted && !ready;
    bool restored = *invalidated && ready;
    *mounted = ready;
    if (ready) *invalidated = false;
    if (restored) {
        s_font_media_revision++;
        if (current->on_media_restored) {
            current->on_media_restored(ctx);
            return true;
        }
    }
    if (!lost) return false;
    s_font_media_revision++;
    *invalidated = true;
    if (current->on_media_lost) current->on_media_lost(ctx);
    if (ttf_font_ready() && !ttf_font_is_builtin()) ttf_font_open_builtin();
    return true;
}

// 电源键和空闲超时共用：收掉手势，走锁屏路径，浅睡回来后重画。
// Shared by the power key and the idle timeout: cancel input, lock, redraw after light sleep.
static void lock_and_resume(
    app_ctx_t* ctx, const app_desc_t* current, ui_gesture_t* gesture,
    int* held_key, int* menu_pressed, menu_feedback_t* feedback,
    bool menu_open, int menu_leaf, bool* media_mounted, bool* media_invalidated,
    int64_t* last_media_poll_ms
) {
    *held_key = -1;
    cancel_gesture(ctx, current, gesture);
    if (*menu_pressed >= 0) {
        menu_feedback(ctx, current, menu_leaf, *menu_pressed, false, feedback);
    }
    *menu_pressed = UI_MENU_HIT_NONE;
    // 要断主机电时先让页面收尾（停网络、关传感器、存进度）；只有下电失败才会回到这里重新进页。
    // Before the host loses power let the page wind down (network, sensors, progress); re-enter only if power-down failed.
    const bool powers_down = app_lock_powers_down();
    if (powers_down && current->on_exit != NULL) current->on_exit(ctx);
    enter_lock_and_sleep(ctx->hl, &s_lock_ignore_until_ms, ctx->acc);
    if (powers_down && current->on_enter != NULL) current->on_enter(ctx);
    ctx->now_ms = esp_timer_get_time() / 1000;
    poll_media(ctx, current, media_mounted, media_invalidated);
    *last_media_poll_ms = ctx->now_ms;
    ctx->consumed = true;
    ctx->pressed = false;
    ctx->released = false;
    if (menu_open) present_menu(ctx, current, menu_leaf, feedback);
    else app_present(ctx, current, APP_REDRAW_PAGE);
}

static void product_lock_cancel(app_ctx_t* ctx, const app_desc_t* current,
                                ui_gesture_t* gesture, int* held_key,
                                int* menu_pressed, menu_feedback_t* feedback,
                                bool* menu_open) {
    *held_key = -1;
    ps_global_cancel_input();
    cancel_gesture(ctx, current, gesture);
    *menu_open = false;
    *menu_pressed = UI_MENU_HIT_NONE;
    feedback->updates = 0;
    ctx->request_app = NULL;
    ctx->request_menu = ctx->request_developer_menu = ctx->request_return = false;
    ctx->pressed = ctx->released = false;
    ctx->consumed = true;
}

// 防误触解除优先于 TF/USB 管理和 app 的 PMU 所有权；否则托管 TF 时电源键先被存储层拿走。
// Guard release has priority over TF/USB management and app PMU ownership; storage must not consume the key first.
static bool guard_unlock_power_priority(int64_t now_ms) {
    if (!app_touch_guard_blocked() || !read_pico_pmu_ready()) return false;
    pmu_product_key_action_t key = read_pico_pmu_take_product_key();
    if (key == PMU_PRODUCT_KEY_NONE) return false;
    if (key == PMU_PRODUCT_KEY_SHORT) {
        if (now_ms - s_guard_tap_at > 700) s_guard_taps = 1;
        else if (s_guard_taps < 2) s_guard_taps++;
        s_guard_tap_at = now_ms;
        if (s_guard_taps >= 2) {
            s_guard_armed_until = now_ms + 5000;
            ps_notice("已就绪：长按电源键解除防误触");
        } else {
            ps_notice("再按一次电源键，然后长按解除防误触");
        }
        return true;
    }
    if (key == PMU_PRODUCT_KEY_LONG) {
        if (now_ms < s_guard_armed_until) {
            app_touch_guard_force_release(now_ms);
            ps_sound_guard();
            ps_notice("贴合防误触锁定已解除");
            s_guard_taps = 0;
            s_guard_armed_until = 0;
        }
        return true;
    }
    return true;
}

void app_loop_run(const app_loop_config_t* config) {
    const app_desc_t* current = config->first_app != NULL
        ? config->first_app
        : app_home_page();

    cst836u_info_t touch_info = { 0 };
    cst836u_get_info(config->tp, &touch_info);
    cst836u_touch_t latest = { 0 };

    app_ctx_t ctx = {
        .hl = config->hl,
        .fb = config->fb,
        .acc = config->acc,
        .tp = config->tp,
        .sensor_ready = config->sensor_ready,
        .continuous_ready = continuous_du_init(),
        .touch = &latest,
        .touch_info = &touch_info,
    };

    bool was_touched = false;
    bool discard_wake_contact = false;
    bool menu_open = false;
    ui_gesture_t gesture = {0};
    enum EpdRotation gesture_rotation = epd_get_rotation();
    int menu_pressed = UI_MENU_HIT_NONE;
    menu_feedback_t feedback = {0};
    int menu_leaf = 0;
    const app_desc_t* return_app = NULL;
    int return_leaf = 0, return_menu_leaf = 0;
    bool return_to_menu = false;
    // 产品来源不依赖工程页的单次返回记录。/ Product origin is independent of the legacy one-shot return record.
    const app_desc_t* product_origin = current->product_shell ? current : NULL;
    ps_global_enable(product_origin != NULL);
    struct { const app_desc_t* app; int leaf; } product_history[8];
    unsigned product_depth = 0;
    int held_key = -1;
    int64_t held_since_ms = 0;
    int64_t last_lock_poll_ms = 0;
    int64_t last_input_ms = esp_timer_get_time() / 1000;
    int64_t last_font_retry_ms = 0;
    int64_t last_media_poll_ms = 0;
    bool media_mounted = ttf_font_ready() && !ttf_font_is_builtin();
    bool media_invalidated = false;
    app_lock_ignore_for(APP_LOCK_IGNORE_BOOT_MS);

    ps_global_page(&ctx, current);
    if (current->on_enter != NULL) current->on_enter(&ctx);
    poll_media(&ctx, current, &media_mounted, &media_invalidated);
    // 首帧必须整屏 GC16：fb 里还是 app_main 画的开机图，DU 盖不掉。
    // First frame must be full GC16: fb still holds the splash; DU cannot cover it.
    app_present(&ctx, current, APP_REDRAW_FULL);
    ESP_LOGI(TAG, "UI ready on %s", current->title);

    while (true) {
        // 手势、实体键、菜单和BLE都不能穿过产品电源状态边界；不要退出真实阅读引擎。
        // Gestures, keys, menus and BLE cannot cross the product power boundary; do not exit the real reading engine.
        ctx.now_ms = esp_timer_get_time() / 1000;
        if (product_origin) {
            if (guard_unlock_power_priority(ctx.now_ms)) {
                last_input_ms = ctx.now_ms;
                last_lock_poll_ms = ctx.now_ms;
            }
            ps_power_poll(&ctx,current);
            if(ps_power_pending()){
                app_touch_buffer_enable(config->tp,false,epd_get_rotation());
                product_lock_cancel(&ctx, current, &gesture, &held_key,
                                    &menu_pressed, &feedback, &menu_open);
            }
            if (ps_power_run(&ctx, current)) {
                ctx.now_ms = esp_timer_get_time() / 1000;
                last_input_ms = last_lock_poll_ms = last_font_retry_ms = ctx.now_ms;
                last_media_poll_ms = 0;
                memset(&latest, 0, sizeof(latest));
                was_touched = false;
                discard_wake_contact = true;
                vTaskDelay(pdMS_TO_TICKS(LOOP_TICK_MS));
                continue;
            }
        }
        cst836u_touch_t touch = { 0 };
        ctx.touch_sample_ms=0;ctx.input_pending=false;
        bool buffered=app_touch_buffer_enable(config->tp,!discard_wake_contact&&ps_keyboard_capture_active(),epd_get_rotation());
        esp_err_t err = buffered?app_touch_buffer_read(&touch,&ctx.touch_sample_ms,&ctx.input_pending):cst836u_read(config->tp, &touch);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "CST836U read failed: %s", esp_err_to_name(err));
            memset(&touch, 0, sizeof(touch));
        }
        // 双机贴合时的电容鬼触先于所有页面处理；命中时本帧触点为空白，
        // 并取消进行中的滑动/长按，避免误触打开菜单或翻页。
        // Suppress paired face-to-face ghost touches before any page sees them;
        // a hit empties this sample and cancels in-flight swipe/long-press state.
        bool guard_blocked = false;
        if (err == ESP_OK) {
            guard_blocked = app_touch_guard_filter(&touch, ctx.now_ms, ps_dual_enabled(), ps_touch_guard_enabled(), ps_dual_contact_likely());
            int guard_event = app_touch_guard_take_event();
            if (guard_event == 1) {
                ps_notice("贴合误触：触摸已屏蔽，展开后恢复");
            } else if (guard_event == 2) {
                ps_notice("触摸已恢复");
            }
            if (guard_blocked) {
                held_key = -1;
                ps_global_cancel_input();
                cancel_gesture(&ctx, current, &gesture);
                menu_pressed = UI_MENU_HIT_NONE;
                ctx.consumed = true;
            }
        } else {
            app_touch_guard_reset();
        }

        const bool pressed = touch.touched && !was_touched;
        const bool released = !touch.touched && was_touched;
        debounce_touch(&latest, &touch, released);
        ctx.now_ms = esp_timer_get_time() / 1000;
        // 触摸、独占 PMU、以及声明不自动休眠的页都算活动。页面自己的轮询刷新不重置。
        // Touch, PMU-owning pages, and pages that block idle sleep count as activity. A page's own poll does not reset it.
        if (current->holds_pmu || current->blocks_idle_sleep
            || (err == ESP_OK && (touch.touched || released))) {
            last_input_ms = ctx.now_ms;
        }
        ctx.pressed = pressed;
        ctx.released = released;
        ctx.consumed = false;
        ctx.capture_gesture = false;
        ctx.request_app = NULL;
        ctx.request_menu = false;
        ctx.request_developer_menu = false;
        ctx.request_return = false;
        if (discard_wake_contact) {
            // 唤醒前残留的触摸及其抬手均不生成正文翻页、长按或按键事件。
            // Any contact held across wake and its release cannot generate a page turn, hold or key event.
            ctx.pressed = ctx.released = false;
            ctx.consumed = true;
            was_touched = false;
            memset(&latest, 0, sizeof(latest));
            if (err == ESP_OK && !touch.touched) discard_wake_contact = false;
            last_input_ms = ctx.now_ms;
            vTaskDelay(pdMS_TO_TICKS(LOOP_TICK_MS));
            continue;
        }
        if(product_origin)ps_sound_contact(err==ESP_OK,touch.touched,touch.count,ctx.now_ms);
        bool selected_from_menu = false;
        enum EpdRotation rotation = epd_get_rotation();
        if (current->continuous_gestures && rotation != gesture_rotation) {
            held_key = -1;
            cancel_gesture(&ctx, current, &gesture);
            ctx.consumed = true;
            ctx.pressed = ctx.released = false;
        }
        gesture_rotation = rotation;

        if (!(buffered&&(touch.touched||released||ctx.input_pending||ctx.now_ms-last_input_ms<200))&&
            ctx.now_ms - last_media_poll_ms >= MEDIA_POLL_INTERVAL_MS) {
            last_media_poll_ms = ctx.now_ms;
            if (poll_media(&ctx, current, &media_mounted, &media_invalidated)) {
                held_key = -1;
                ps_global_cancel_input();
                cancel_gesture(&ctx, current, &gesture);
                menu_pressed = UI_MENU_HIT_NONE;
                ctx.consumed = true;
                ctx.pressed = ctx.released = false;
                was_touched = touch.touched;
                if (menu_open) present_menu(&ctx, current, menu_leaf, &feedback);
                else app_present(&ctx, current, APP_REDRAW_FULL);
                // 本轮旧输入不再触发动作，下一轮恢复正常事件处理。
                // Discard this tick's stale input, then resume normal dispatch next tick.
                vTaskDelay(pdMS_TO_TICKS(LOOP_TICK_MS));
                continue;
            }
        }

        if (err != ESP_OK) {
            held_key = -1;
            ps_global_cancel_input();
            cancel_gesture(&ctx, current, &gesture);
            ctx.consumed = true;
            if (menu_pressed >= 0) menu_feedback(&ctx, current, menu_leaf, menu_pressed, false, &feedback);
            menu_pressed = UI_MENU_HIT_NONE;
        }
        app_redraw_t global_redraw = APP_REDRAW_NONE;
        bool global_claimed = !menu_open && err == ESP_OK && ps_global_input(&ctx, current, &global_redraw);
        if (global_claimed) {
            held_key = -1;
            cancel_gesture(&ctx, current, &gesture);
            ctx.consumed = true;
            app_present(&ctx, current, global_redraw);
        }
        if (pressed && err == ESP_OK && !global_claimed) {
            held_key = -1;
            const int key = ui_key_hit_test(touch.x, touch.y);
            const bool handle_hit = !current->custom_menu_handle
                && ui_menu_handle_hit_test(touch.x, touch.y);
            ctx.consumed = true;
            if (key >= 0 || handle_hit) {
                cancel_gesture(&ctx, current, &gesture);
                if (menu_pressed >= 0) menu_feedback(&ctx, current, menu_leaf, menu_pressed, false, &feedback);
                menu_pressed = UI_MENU_HIT_NONE;
            }
            if (key >= 0 && current->owns_keys && !menu_open) {
                app_redraw_t redraw = current->on_key ? current->on_key(&ctx, key) : APP_REDRAW_NONE;
                present_page(&ctx, current, &gesture, redraw);
                if (current->on_key_long && touch.count == 1) {
                    held_key = key;
                    held_since_ms = ctx.now_ms;
                }
            } else if (key == UI_KEY_2) {
                if (menu_open) {
                    feedback.updates = 0;
                    ui_draw_menu_page(ctx.fb, current, menu_leaf);
                    guard_draw_result(ctx.hl, update_display_full(ctx.hl));
                } else if (current->present != NULL && current->present(&ctx, APP_REDRAW_FULL)) {
                } else if (current->render != NULL) {
                    current->render(&ctx, ctx.fb);
                    ps_global_draw_controls(current, ctx.fb);
                    guard_draw_result(ctx.hl, update_display_full(ctx.hl));
                }
            } else if (key == UI_KEY_3 || handle_hit) {
                if (product_origin && !menu_open) ctx.request_return = true;
                else {
                    menu_open = !menu_open;
                    if (menu_open) {
                        menu_leaf = ui_menu_leaf_for_app(current);
                        present_menu(&ctx, current, menu_leaf, &feedback);
                    } else app_present(&ctx, current, APP_REDRAW_PAGE);
                }
            } else if (key >= 0 && menu_open) {
                int next = menu_leaf + (key == UI_KEY_1 ? -1 : 1);
                if (next >= 0 && next < ui_menu_leaf_count()) {
                    menu_leaf = next;
                    present_menu(&ctx, current, menu_leaf, &feedback);
                }
            } else if (key >= 0) {
                app_redraw_t redraw = current->on_key ? current->on_key(&ctx, key) : APP_REDRAW_NONE;
                if (redraw == APP_REDRAW_NONE)
                    ESP_LOGI(TAG, "KEY%d pressed, no action bound", key + 1);
                present_page(&ctx, current, &gesture, redraw);
            } else if (menu_open) {
                int hit = ui_menu_hit_test(touch.x, touch.y, menu_leaf);
                if (hit == UI_MENU_HIT_PREV || hit == UI_MENU_HIT_NEXT) {
                    menu_leaf += hit == UI_MENU_HIT_NEXT ? 1 : -1;
                    present_menu(&ctx, current, menu_leaf, &feedback);
                } else if (hit >= 0 && touch.count == 1) {
                    menu_pressed = hit;
                    menu_feedback(&ctx, current, menu_leaf, hit, true, &feedback);
                }
            } else if (current->on_gesture) {
                ctx.consumed = false;
            } else {
                // 旧页按下先收到 consumed=true，仅 NONE 为同轮 tick 放行。
                // Legacy press sees consumed=true; only NONE clears it for this tick.
                app_redraw_t redraw = current->on_touch
                    ? current->on_touch(&ctx, &latest) : APP_REDRAW_NONE;
                app_present(&ctx, current, redraw);
                if (redraw == APP_REDRAW_NONE) ctx.consumed = false;
            }
        }
        if (err == ESP_OK) was_touched = touch.touched;
        if (menu_open && menu_pressed >= 0) {
            int hit = ui_menu_hit_test(latest.x, latest.y, menu_leaf);
            if (touch.count > 1 || hit != menu_pressed || released) {
                int chosen = menu_pressed;
                menu_feedback(&ctx, current, menu_leaf, chosen, false, &feedback);
                menu_pressed = UI_MENU_HIT_NONE;
                if (released && err == ESP_OK && hit == chosen) {
                    menu_open = false;
                    ctx.request_app = app_at(chosen);
                    selected_from_menu = true;
                    if (ctx.request_app == current) app_present(&ctx, current, APP_REDRAW_PAGE);
                }
            }
        }
        if (!menu_open && current->on_gesture && err == ESP_OK) {
            ui_gesture_event_t event;
            if (ui_gesture_feed_continuous(&gesture, &ctx, current->continuous_gestures, &event)) {
                app_redraw_t redraw = current->on_gesture(&ctx, &event);
                if (current->continuous_gestures && ctx.capture_gesture) ui_gesture_capture(&gesture);
                ctx.capture_gesture = false;
                present_page(&ctx, current, &gesture, redraw);
                if (redraw != APP_REDRAW_NONE) ctx.consumed = true;
            }
        }

        ctx.now_ms = esp_timer_get_time() / 1000;
        rails_idle_check(ctx.now_ms);

        // 产品区分完整短按与长按；自检页仍独占PMU，工程入口保留旧短按语义。
        // Products distinguish complete short/long actions; PMU owners and legacy engineering short semantics stay unchanged.
        if (read_pico_pmu_ready() && !current->holds_pmu
            && ctx.now_ms - last_lock_poll_ms >= APP_LOCK_POLL_MS) {
            last_lock_poll_ms = ctx.now_ms;
            const pmu_product_key_action_t power_key = product_origin
                ? read_pico_pmu_take_product_key()
                : (read_pico_pmu_take_key_short() ? PMU_PRODUCT_KEY_SHORT : PMU_PRODUCT_KEY_NONE);
            if (power_key == PMU_PRODUCT_KEY_LONG) {
                if (app_touch_guard_blocked() && ctx.now_ms < s_guard_armed_until) {
                    // 双击已就绪：长按只解除防误触，不打开电源菜单。
                    // Double-click armed: long press only releases the guard, never opens the power menu.
                    app_touch_guard_force_release(ctx.now_ms);
                    ps_sound_guard();
                    ps_notice("贴合防误触锁定已解除");
                    s_guard_taps = 0;
                    s_guard_armed_until = 0;
                    last_input_ms = ctx.now_ms;
                } else if (ctx.now_ms >= s_lock_ignore_until_ms) {
                    product_lock_cancel(&ctx, current, &gesture, &held_key,
                                        &menu_pressed, &feedback, &menu_open);
                    discard_wake_contact = true;
                    (void)ps_power_key_long(&ctx, current);
                    last_input_ms = ctx.now_ms;
                }
            } else if (power_key == PMU_PRODUCT_KEY_SHORT) {
                if (ctx.now_ms < s_lock_ignore_until_ms) {
                    ESP_LOGI(TAG, "ignore boot KEY_SHORT");
                } else if (app_touch_guard_blocked()) {
                    // 第一击准备，第二击进入待解锁；不执行普通电源短按。
                    // First tap prepares, second arms the unlock; normal short-power action is suppressed.
                    if (ctx.now_ms - s_guard_tap_at > 700) s_guard_taps = 1;
                    else if (s_guard_taps < 2) s_guard_taps++;
                    s_guard_tap_at = ctx.now_ms;
                    if (s_guard_taps >= 2) {
                        s_guard_armed_until = ctx.now_ms + 5000;
                        ps_notice("已就绪：长按电源键解除防误触");
                    } else {
                        ps_notice("再按一次电源键，然后长按解除防误触");
                    }
                    last_input_ms = ctx.now_ms;
                } else {
                    if (product_origin && ps_power_toggle(&ctx, current)) {
                        product_lock_cancel(&ctx, current, &gesture, &held_key,
                                            &menu_pressed, &feedback, &menu_open);
                        last_input_ms = ctx.now_ms;
                        vTaskDelay(pdMS_TO_TICKS(LOOP_TICK_MS));
                        continue;
                    } else if (current->on_lock) {
                        held_key = -1;
                        cancel_gesture(&ctx, current, &gesture);
                        menu_open = false;
                        feedback.updates = 0;
                        menu_pressed = UI_MENU_HIT_NONE;
                        ctx.consumed = true;
                        present_page(&ctx, current, &gesture, current->on_lock(&ctx));
                    } else lock_and_resume(
                        &ctx, current, &gesture, &held_key, &menu_pressed, &feedback,
                        menu_open, menu_leaf, &media_mounted, &media_invalidated,
                        &last_media_poll_ms
                    );
                    last_input_ms = ctx.now_ms;
                }
            } else if (!product_origin && !app_settings_test_awake()
                       && ctx.now_ms >= s_lock_ignore_until_ms
                       && ctx.now_ms - last_input_ms >= APP_AUTO_SLEEP_MS) {
                ESP_LOGI(TAG, "idle lock");
                if (product_origin && ps_power_toggle(&ctx, current)) {
                    product_lock_cancel(&ctx, current, &gesture, &held_key,
                                        &menu_pressed, &feedback, &menu_open);
                    last_input_ms = ctx.now_ms;
                    vTaskDelay(pdMS_TO_TICKS(LOOP_TICK_MS));
                    continue;
                } else if (current->on_lock) {
                    held_key = -1;
                    cancel_gesture(&ctx, current, &gesture);
                    menu_open = false;
                    feedback.updates = 0;
                    menu_pressed = UI_MENU_HIT_NONE;
                    ctx.consumed = true;
                    present_page(&ctx, current, &gesture, current->on_lock(&ctx));
                } else lock_and_resume(
                    &ctx, current, &gesture, &held_key, &menu_pressed, &feedback,
                    menu_open, menu_leaf, &media_mounted, &media_invalidated,
                    &last_media_poll_ms
                );
                last_input_ms = ctx.now_ms;
            }
        }

        // 设置里存的字体在 SD 卡上，开机时卡可能还没挂好，这里定期重试。
        // The saved font lives on the card; retry until the mount is ready.
        if (ctx.now_ms - last_font_retry_ms >= FONT_RETRY_INTERVAL_MS) {
            last_font_retry_ms = ctx.now_ms;
            if (!current->manages_fonts && try_load_saved_sd_font(!media_invalidated)) {
                held_key = -1;
                cancel_gesture(&ctx, current, &gesture);
                ctx.consumed = true;
                menu_pressed = UI_MENU_HIT_NONE;
                if (menu_open) {
                    present_menu(&ctx, current, menu_leaf, &feedback);
                } else {
                    app_present(&ctx, current, APP_REDRAW_FULL);
                }
                ESP_LOGI(TAG, "TTF font ready");
            }
        }

        if (held_key >= 0) {
            if (menu_open || ctx.request_app || ctx.request_menu || ctx.request_developer_menu || ctx.request_return || !touch.touched ||
                touch.count != 1 || ui_key_hit_test(touch.x, touch.y) != held_key ||
                ctx.now_ms < held_since_ms) {
                held_key = -1;
            } else if (!pressed && ctx.now_ms - held_since_ms >= UI_LONG_PRESS_MS) {
                int key = held_key;
                held_key = -1;
                ctx.consumed = true;
                present_page(&ctx, current, &gesture, current->on_key_long(&ctx, key));
            }
        }
        if (!menu_open && !global_claimed && !ctx.request_app && !ctx.request_menu && !ctx.request_developer_menu && !ctx.request_return && current->on_tick != NULL) {
            app_redraw_t redraw = current->on_tick(&ctx);
            if (redraw == APP_REDRAW_PAGE || redraw == APP_REDRAW_FULL) held_key = -1;
            present_page(&ctx, current, &gesture, redraw);
        }
        if (menu_open && feedback.updates > 0
            && ctx.now_ms - feedback.last_ms >= UI_SETTLE_IDLE_MS) {
            guard_draw_result(ctx.hl, update_display_area_with(
                ctx.hl, &E0470_WAVEFORM, MODE_GL16, feedback.area));
            feedback.updates = 0;
        }
        if (!menu_open) feedback.updates = 0;
        // 返回来源优先；普通请求仍按切页优先处理。
        // Return requests win; ordinary requests still prioritize page switches.
        if (ctx.request_return || ctx.request_app || ctx.request_menu || ctx.request_developer_menu) {
            held_key = -1;
            const app_desc_t* next = ctx.request_app;
            const bool developer_menu = ctx.request_developer_menu && !ctx.request_return && !next;
            const bool go_back = ctx.request_return || (product_origin && ctx.request_menu && !next && !developer_menu);
            cancel_gesture(&ctx, current, &gesture);
            menu_pressed = UI_MENU_HIT_NONE;
            ctx.request_app = NULL;
            ctx.request_menu = false;
            ctx.request_developer_menu = false;
            ctx.request_return = false;
            ctx.consumed = true;
            if (go_back) {
                if (product_origin) {
                    const app_desc_t* target = product_origin;
                    int leaf = 0;
                    if (product_depth) {
                        --product_depth;
                        target = product_history[product_depth].app;
                        leaf = product_history[product_depth].leaf;
                    }
                    menu_open = false;
                    if (target != current) {
                        if (current->on_exit) current->on_exit(&ctx);
                        current = target;
                        ps_global_page(&ctx, current);
                        if (current->on_enter) current->on_enter(&ctx);
                        ctx.leaf = leaf;
                    }
                    app_present(&ctx, current, current->enter_full ? APP_REDRAW_FULL : APP_REDRAW_PAGE);
                } else {
                    menu_open = return_app ? return_to_menu : true;
                    menu_leaf = return_app ? return_menu_leaf : ui_menu_leaf_for_app(current);
                    if (return_app) {
                        if (current->on_exit) current->on_exit(&ctx);
                        current = return_app;
                        ps_global_page(&ctx, current);
                        return_app = NULL;
                        if (current->on_enter) current->on_enter(&ctx);
                        // 初始化后恢复通用页码，避免 on_enter 的默认页码覆盖来源。
                        // Restore the shared leaf after initialization overrides its default.
                        ctx.leaf = return_leaf;
                    }
                    if (menu_open) present_menu(&ctx, current, menu_leaf, &feedback);
                    else app_present(&ctx, current, APP_REDRAW_PAGE);
                }
            } else if (next) {
                if (next != current) {
                    if (next->product_shell) {
                        product_origin = next;
                        ps_global_enable(true);
                        product_depth = 0;
                        return_app = NULL;
                    } else if (product_origin) {
                        // 满栈保留产品根和最近子页，深层页面仍有可靠出口。
                        // Keep the product root and newest origins when full so deep pages retain an exit.
                        if (product_depth == 8) {
                            memmove(&product_history[1], &product_history[2], 6 * sizeof(product_history[0]));
                            product_depth = 7;
                        }
                        product_history[product_depth].app = current;
                        product_history[product_depth].leaf = ctx.leaf;
                        ++product_depth;
                    } else {
                        return_app = current;
                        return_leaf = ctx.leaf;
                        return_to_menu = selected_from_menu;
                        return_menu_leaf = menu_leaf;
                    }
                }
                menu_open = false;
                switch_to(&ctx, &current, next);

            } else {
                if (developer_menu) {
                    product_origin = NULL;
                    ps_global_enable(false);
                    product_depth = 0;
                    return_app = NULL;
                }
                menu_open = true;
                menu_leaf = ui_menu_leaf_for_app(current);
                present_menu(&ctx, current, menu_leaf, &feedback);
            }
        }
        vTaskDelay(pdMS_TO_TICKS(LOOP_TICK_MS));
    }
}

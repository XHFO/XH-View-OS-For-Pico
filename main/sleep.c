/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 *
 * 锁屏进睡、浅睡等待按键或拿起、软睡/关机拉掉 EN。
 *
 * Enter lock and sleep, light-sleep wait for key or pickup, and drop EN
 * for soft sleep / power-off.
 *
 * 冻结：电源键完整短按切换、长按开菜单；可选双击以触摸NORMAL+INT配合CPU浅睡，不写未知standby命令。
 * Frozen: Complete power clicks toggle, long presses open the menu; optional double tap uses touch NORMAL+INT with CPU light sleep, never unknown standby commands.
 * 触摸NORMAL的待机耗电高于DEEPSLEEP，不承诺未经测量的整机功耗；工程旧锁屏不变。
 * NORMAL touch uses more standby power than DEEPSLEEP; system power remains unmeasured and engineering locks remain unchanged.
 * 锁屏原图仅增加共享无损外层以给云端登录腾APP空间，临时P4B在入睡前释放，不改锁屏像素与唤醒策略。
 * The lock asset gains only a shared lossless wrapper to free APP room for cloud login; release transient P4B before sleep, preserving lock pixels and wake policy.
 */

#include "sleep.h"
#include "power_touch.h"

#include <stdlib.h>
#include <string.h>

#include "app.h"
#include "display.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_private/sleep_cpu.h"
#include "esp_sleep.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "pmu_selftest.h"
#include "read_pico_board.h"
#include "read_pico_buzzer.h"
#include "read_pico_pmu.h"
#include "sc7a20h_lab.h"
#include "settings.h"
#include "ui_kit.h"
#include "ui_image.h"
#include "ui_packed_asset.h"

static const char* TAG = "read_pico";

// 主机下电后等 EN 落下的上限；PMU 确认后约 100 ms 拉低，超时视为失败。
// Upper bound for EN to fall after power-down; the PMU drops it about 100 ms after acknowledging.
#define HOST_EN_DROP_WAIT_MS 5000

extern const uint8_t lock_4bpp_p4b_deflate_start[] asm("_binary_lock_4bpp_p4b_deflate_start");
extern const uint8_t lock_4bpp_p4b_deflate_end[] asm("_binary_lock_4bpp_p4b_deflate_end");

static cst836u_handle_t s_tp;
static sc7a20h_handle_t s_acc;
static bool s_acc_was_on;

static bool s_product_lock;
static bool s_product_light_sleep;
static bool s_product_doubletap, s_product_tp_suppressed, s_product_tp_pending;
static gpio_num_t s_product_tp_gpio = GPIO_NUM_NC;
static power_touch_t s_product_touch;
#define PRODUCT_KEY_POLL_MS 20
#define PRODUCT_IDLE_CHECK_MS 1000

void app_sleep_bind(cst836u_handle_t tp, sc7a20h_handle_t acc) {
    s_tp = tp;
    s_acc = acc;
    // 没开 PM 时 IDF 不分配 CPU 保存区，浅睡里 CPU 不会掉电；这里手动申请一次。
    // Without PM, IDF never allocates CPU retention, so light sleep keeps the CPU powered; request it once here.
    esp_err_t err = sleep_cpu_configure(true);
    if (err != ESP_OK) ESP_LOGW(TAG, "CPU light-sleep power-down off: %s", esp_err_to_name(err));
}

bool app_lock_powers_down(void) {
    return app_settings_sleep_mode() != APP_SLEEP_LIGHT;
}

// 触摸、加速度计都在常开 3V3 上，ESP 浅睡或 EN 拉低都不会断它们的电，必须主动送进最低功耗态。
// Touch and accelerometer sit on the always-on 3V3; neither light sleep nor EN low cuts them, so park them explicitly.
static void periph_suspend(bool host_off) {
    if (read_pico_buzzer_score_busy()) (void)read_pico_buzzer_score_stop();
    esp_err_t ioe = read_pico_ioe_sleep(host_off);
    if (ioe != ESP_OK) ESP_LOGW(TAG, "ioe sleep %s", esp_err_to_name(ioe));
    s_acc_was_on = sc7a20h_powered(s_acc);
    if (s_acc_was_on) (void)sc7a20h_power_down(s_acc);
    if (s_tp != NULL && cst836u_get_mode(s_tp) != CST836U_MODE_DEEPSLEEP) {
        esp_err_t err = cst836u_set_mode(s_tp, CST836U_MODE_DEEPSLEEP);
        if (err != ESP_OK) ESP_LOGW(TAG, "touch deepsleep %s", esp_err_to_name(err));
    }
}

// 深睡的触摸只认 TP_RST 复位；加速度计按睡前的配置重新上电。
// Deep-sleeping touch only wakes via TP_RST; the accelerometer comes back with its pre-sleep config.
static void periph_resume(void) {
    (void)read_pico_ioe_wake();
    if (s_tp != NULL && cst836u_get_mode(s_tp) == CST836U_MODE_DEEPSLEEP) {
        esp_err_t err = cst836u_wake(s_tp);
        if (err != ESP_OK) ESP_LOGW(TAG, "touch wake %s", esp_err_to_name(err));
    }
    if (s_acc_was_on) (void)sc7a20h_power_up(s_acc);
    s_acc_was_on = false;
}

static esp_err_t lock_arm_ioe_wakeup(void) {
    gpio_config_t io = {
        .pin_bit_mask = 1ULL << READ_PICO_IOE_INT_GPIO,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    esp_err_t err = gpio_config(&io);
    if (err != ESP_OK) return err;
    read_pico_clear_ioe_int();
    err = gpio_wakeup_enable((gpio_num_t)READ_PICO_IOE_INT_GPIO, GPIO_INTR_LOW_LEVEL);
    return err == ESP_OK ? esp_sleep_enable_gpio_wakeup() : err;
}

void app_lock_wait_key_idle(int timeout_ms) {
    int64_t start = esp_timer_get_time() / 1000;
    int64_t high_from = 0;
    while (esp_timer_get_time() / 1000 - start < timeout_ms) {
        read_pico_pmu_drain_events();
        read_pico_clear_ioe_int();
        bool held = false;
        if (read_pico_pmu_poll() == ESP_OK) {
            held = (read_pico_pmu_get()->key_state & 0x01) != 0;
        }
        bool int_high = gpio_get_level((gpio_num_t)READ_PICO_IOE_INT_GPIO) != 0;
        if (!held && int_high) {
            if (high_from == 0) high_from = esp_timer_get_time() / 1000;
            if (esp_timer_get_time() / 1000 - high_from >= 150) return;
        } else {
            high_from = 0;
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

app_wake_source_t app_last_wake_source(void) {
    return (app_wake_source_t)app_settings_last_wake();
}


void app_product_lock_end(void) {
    if (!s_product_lock) return;
    if (s_product_light_sleep) {
        (void)esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_TIMER);
        (void)gpio_wakeup_disable((gpio_num_t)READ_PICO_IOE_INT_GPIO);
        if(s_product_tp_gpio!=GPIO_NUM_NC)(void)gpio_wakeup_disable(s_product_tp_gpio);
    }
    periph_resume();
    s_product_lock = false;
    s_product_light_sleep = false;
    s_product_doubletap = s_product_tp_suppressed = s_product_tp_pending = false;
    s_product_tp_gpio = GPIO_NUM_NC;
    memset(&s_product_touch,0,sizeof(s_product_touch));
}

esp_err_t app_product_lock_begin(bool light_sleep_allowed, bool doubletap_allowed) {
    if (s_product_lock) return ESP_ERR_INVALID_STATE;
    if (!s_tp) return ESP_ERR_NOT_SUPPORTED;
    // 锁页已呈现，关闭屏轨后才停IOE；这里从不改变fb及参考帧。
    // The lock face is already presented; power the panel off before parking the IOE, without touching framebuffer/reference state.
    epd_poweroff();
    if (read_pico_buzzer_score_busy()) (void)read_pico_buzzer_score_stop();
    s_acc_was_on = sc7a20h_powered(s_acc);
    if (s_acc_was_on) (void)sc7a20h_power_down(s_acc);
    s_product_lock = true;
    s_product_light_sleep = light_sleep_allowed;
    s_product_doubletap=doubletap_allowed;
    s_product_tp_gpio=doubletap_allowed?cst836u_int_gpio(s_tp):GPIO_NUM_NC;
    memset(&s_product_touch,0,sizeof(s_product_touch));
    s_product_tp_suppressed=s_product_tp_pending=false;
    esp_err_t err = read_pico_ioe_sleep(false);
    if(err==ESP_OK&&doubletap_allowed&&s_product_tp_gpio==GPIO_NUM_NC)err=ESP_ERR_NOT_SUPPORTED;
    if(err==ESP_OK&&doubletap_allowed&&cst836u_get_mode(s_tp)==CST836U_MODE_DEEPSLEEP)err=cst836u_wake(s_tp);
    if (err == ESP_OK && !doubletap_allowed && cst836u_get_mode(s_tp) != CST836U_MODE_DEEPSLEEP)
        err = cst836u_set_mode(s_tp, CST836U_MODE_DEEPSLEEP);
    if (err == ESP_OK && light_sleep_allowed) err = lock_arm_ioe_wakeup();
    if(err==ESP_OK&&doubletap_allowed&&light_sleep_allowed)err=gpio_wakeup_enable(s_product_tp_gpio,GPIO_INTR_LOW_LEVEL);
    if (err != ESP_OK) {
        app_product_lock_end();
        return err;
    }
    if(doubletap_allowed){
        cst836u_touch_t touch={0};
        esp_err_t read=cst836u_read(s_tp,&touch);
        s_product_touch.blocked=read!=ESP_OK||touch.touched;
        s_product_touch.down=touch.touched;
        s_product_tp_suppressed=read!=ESP_OK;
    }
    // 触发锁屏的SHORT已由主循环消费；此处不清队列，保留绘屏期间新点按的唤醒请求。
    // The main loop consumed the locking SHORT; retain new clicks queued while the lock face was painted.
    return ESP_OK;
}

app_wake_source_t app_product_lock_wait_step(uint32_t timeout_ms) {
    if (!s_product_lock) return APP_WAKE_ERROR;
    // 定时返回后调用方可能刚更新时钟；再次关轨并停MODE，不重新启动触摸或IMU。
    // A timed return may let the caller update the clock; re-park rails/MODE without restarting touch or IMU.
    if (read_pico_rails_on()) {
        epd_poweroff();
        if (read_pico_ioe_sleep(false) != ESP_OK) return APP_WAKE_ERROR;
    }
    int64_t deadline = esp_timer_get_time() / 1000 + timeout_ms;
    app_wake_source_t wake = APP_WAKE_NONE;
    while (esp_timer_get_time() / 1000 < deadline) {
        // 两向均在松手后的SHORT触发一次，避免按下唤醒后松手又立即锁屏。
        // Both directions consume one released SHORT, preventing the wake press's release from locking again.
        int key=read_pico_pmu_take_product_key();
        if (key==PMU_PRODUCT_KEY_SHORT||key==PMU_PRODUCT_KEY_LONG) {
            wake = key==PMU_PRODUCT_KEY_LONG?APP_WAKE_KEY_LONG:APP_WAKE_KEY;
            break;
        }
        int64_t now = esp_timer_get_time() / 1000;
        uint32_t delay_ms = PRODUCT_IDLE_CHECK_MS;
        if(s_product_doubletap){
            bool asserted=cst836u_int_asserted(s_tp);
            if(s_product_tp_suppressed&&!asserted)s_product_tp_suppressed=false;
            bool tracking=s_product_touch.down;
            if(!s_product_tp_suppressed&&(asserted||tracking||s_product_tp_pending)){
                s_product_tp_pending=false;
                cst836u_touch_t touch={0};esp_err_t read=cst836u_read(s_tp,&touch);
                if(power_touch_feed(&s_product_touch,read==ESP_OK,touch.touched,touch.count,touch.x,touch.y,now)){
                    wake=APP_WAKE_TOUCH_DOUBLE;break;
                }
                if(read!=ESP_OK){s_product_touch.down=false;s_product_tp_suppressed=true;}
                else if(!touch.touched&&asserted)s_product_tp_suppressed=true;
            }
            if(s_product_touch.first_valid&&now-s_product_touch.first_up_ms>POWER_TAP_GAP_MS)s_product_touch.first_valid=false;
            if(s_product_tp_suppressed)delay_ms=100;
            else if(s_product_touch.down)delay_ms=POWER_TAP_POLL_MS;
            else if(s_product_touch.first_valid){
                int64_t gap=POWER_TAP_GAP_MS+1-(now-s_product_touch.first_up_ms);
                if(gap>0&&gap<delay_ms)delay_ms=(uint32_t)gap;
            }
        }
        int64_t remaining = deadline - now;
        if (remaining <= 0) break;
        if (remaining < delay_ms) delay_ms = (uint32_t)remaining;
        if (!s_product_light_sleep) {
            if (delay_ms > PRODUCT_KEY_POLL_MS) delay_ms = PRODUCT_KEY_POLL_MS;
            vTaskDelay(pdMS_TO_TICKS(delay_ms));
            continue;
        }
        // PMU按住或未清IRQ时短定时轮询，低电平不能造成浅睡忙循环。
        // Poll held PMU/uncleared IRQs briefly so a low level cannot cause a light-sleep busy loop.
        esp_err_t err;
        if(s_product_light_sleep&&s_product_doubletap){
            bool asserted=cst836u_int_asserted(s_tp);
            // 检查后才到来的触点须立即读，不能禁用唤醒后再睡整秒。/ Read a contact arriving after the first check before disabling its wake and sleeping a full second.
            if(asserted&&!s_product_tp_suppressed&&!s_product_touch.down){s_product_tp_pending=true;continue;}
            err=asserted?gpio_wakeup_disable(s_product_tp_gpio):gpio_wakeup_enable(s_product_tp_gpio,GPIO_INTR_LOW_LEVEL);
            if(err!=ESP_OK)return APP_WAKE_ERROR;
        }
        read_pico_clear_ioe_int();
        if (!gpio_get_level((gpio_num_t)READ_PICO_IOE_INT_GPIO)) {
            err = gpio_wakeup_disable((gpio_num_t)READ_PICO_IOE_INT_GPIO);
            if (delay_ms > PRODUCT_KEY_POLL_MS) delay_ms = PRODUCT_KEY_POLL_MS;
        } else err = gpio_wakeup_enable((gpio_num_t)READ_PICO_IOE_INT_GPIO, GPIO_INTR_LOW_LEVEL);
        if (err != ESP_OK) return APP_WAKE_ERROR;
        err = esp_sleep_enable_timer_wakeup((uint64_t)delay_ms * 1000);
        if (err == ESP_OK) {
            err = esp_light_sleep_start();
            // INT短脉冲可能已回高；GPIO唤醒仍保留一次读取，且可跨越本次等待期限。/ Preserve one read after GPIO wake even when its pulse has ended, including across wait deadlines.
            if(err==ESP_OK&&s_product_doubletap&&esp_sleep_get_wakeup_cause()==ESP_SLEEP_WAKEUP_GPIO)s_product_tp_pending=true;
        }
        (void)esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_TIMER);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "product light sleep failed: %s", esp_err_to_name(err));
            return APP_WAKE_ERROR;
        }
    }
    if (wake != APP_WAKE_NONE) app_settings_set_last_wake((uint8_t)wake);
    return wake;
}

static void pickup_ack(sc7a20h_handle_t acc) {
    sc7a20h_events_t ev;
    sc7a20h_read_events(acc, &ev);
    sc7a20h_ack_int(acc);
}

static bool pickup_ia(sc7a20h_handle_t acc) {
    sc7a20h_events_t ev;
    sc7a20h_aoi_src_t aoi;
    if (sc7a20h_read_events(acc, &ev) != ESP_OK) return false;
    sc7a20h_aoi_decode(ev.aoi1_src, &aoi);
    return aoi.ia;
}

// 第一次低电平不够：高通余波还会再拉高。要连续安静一段时间才进浅睡。
// A first low is not enough: high-pass ringing can rise again. Wait for a quiet stretch before light sleep.
static bool pickup_wait_quiet(sc7a20h_handle_t acc, int quiet_ms, int timeout_ms) {
    int64_t start = esp_timer_get_time() / 1000;
    int64_t low_from = 0;
    while (esp_timer_get_time() / 1000 - start < timeout_ms) {
        if (sc7a20h_int1_level(acc) == 0) {
            if (low_from == 0) low_from = esp_timer_get_time() / 1000;
            if (esp_timer_get_time() / 1000 - low_from >= quiet_ms) return true;
        } else {
            pickup_ack(acc);
            low_from = 0;
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
    return false;
}

app_wake_source_t app_light_sleep_wait(sc7a20h_handle_t acc) {
    bool pickup = acc != NULL && app_settings_pickup_wake();
    bool acc_armed = false;
    periph_suspend(false);
    lock_arm_ioe_wakeup();
    if (pickup) {
        sc7a20h_motion_cfg_t motion = SC7A20H_MOTION_DEFAULT();
        motion.ths_mg = 350;
        motion.duration = 3;
        sc7a20h_arm_pickup_wake(acc, &motion);
        acc_armed = true;
        pickup_ack(acc);
        if (!pickup_wait_quiet(acc, 400, 2000)) {
            pickup = false;
            ESP_LOGW(TAG, "pickup INT1 stuck high, key only");
        } else {
            sc7a20h_config_light_sleep_wakeup(acc);
            if (!pickup_wait_quiet(acc, 400, 1500)) {
                gpio_wakeup_disable(sc7a20h_int1_gpio(acc));
                pickup = false;
                ESP_LOGW(TAG, "pickup INT1 re-asserted, key only");
            } else {
                // 安静窗口里若 INT1 闪过，ESP 会留下高电平唤醒挂起，关掉再打开清掉。
                // If INT1 glitched in the quiet window, ESP keeps a high-level wake pending; disable then re-enable to clear it.
                gpio_wakeup_disable(sc7a20h_int1_gpio(acc));
                sc7a20h_config_light_sleep_wakeup(acc);
                if (sc7a20h_int1_level(acc) > 0) {
                    gpio_wakeup_disable(sc7a20h_int1_gpio(acc));
                    pickup = false;
                    ESP_LOGW(TAG, "pickup INT1 high at sleep, key only");
                } else {
                    ESP_LOGI(
                        TAG, "wait key GPIO%d or pickup GPIO%d",
                        READ_PICO_IOE_INT_GPIO, (int)sc7a20h_int1_gpio(acc)
                    );
                }
            }
        }
    } else {
        ESP_LOGI(TAG, "wait key on IOE_INT GPIO%d", READ_PICO_IOE_INT_GPIO);
    }

    app_wake_source_t wake = APP_WAKE_NONE;
    bool slept = false;
    for (;;) {
        read_pico_clear_ioe_int();
        if (!slept && gpio_get_level((gpio_num_t)READ_PICO_IOE_INT_GPIO) == 0) {
            read_pico_pmu_drain_events();
            read_pico_clear_ioe_int();
            vTaskDelay(pdMS_TO_TICKS(20));
            continue;
        }
        if (!slept && pickup && sc7a20h_int1_level(acc) > 0) {
            pickup_ack(acc);
            vTaskDelay(pdMS_TO_TICKS(20));
            continue;
        }
        if (slept && gpio_get_level((gpio_num_t)READ_PICO_IOE_INT_GPIO) == 0) {
            if (read_pico_pmu_take_key_wakeup()) {
                wake = APP_WAKE_KEY;
                break;
            }
            read_pico_clear_ioe_int();
            continue;
        }
        if (slept && pickup && sc7a20h_int1_level(acc) > 0 && pickup_ia(acc)) {
            wake = APP_WAKE_PICKUP;
            ESP_LOGI(TAG, "pickup wake");
            break;
        }
        if (slept && pickup && sc7a20h_int1_level(acc) > 0) {
            pickup_ack(acc);
            pickup_wait_quiet(acc, 400, 1500);
            continue;
        }
        int64_t t0 = esp_timer_get_time();
        esp_light_sleep_start();
        slept = true;
        int64_t dt_ms = (esp_timer_get_time() - t0) / 1000;
        read_pico_clear_ioe_int();
        if (read_pico_pmu_take_key_wakeup()) {
            wake = APP_WAKE_KEY;
            break;
        }
        if (pickup && sc7a20h_int1_level(acc) > 0 && pickup_ia(acc)) {
            if (dt_ms < 80) {
                ESP_LOGW(TAG, "pickup ignored, sleep %lld ms", (long long)dt_ms);
                pickup_ack(acc);
                pickup_wait_quiet(acc, 400, 1500);
                continue;
            }
            wake = APP_WAKE_PICKUP;
            ESP_LOGI(TAG, "pickup wake");
            break;
        }
        if (pickup && sc7a20h_int1_level(acc) > 0) {
            pickup_ack(acc);
            pickup_wait_quiet(acc, 400, 1500);
        }
    }

    gpio_wakeup_disable((gpio_num_t)READ_PICO_IOE_INT_GPIO);
    if (acc_armed) {
        gpio_wakeup_disable(sc7a20h_int1_gpio(acc));
        sc7a20h_events_t ev;
        sc7a20h_read_events(acc, &ev);
        sc7a20h_power_down(acc);
        sc7a20h_int1_begin(acc);
    }
    periph_resume();
    app_settings_set_last_wake((uint8_t)wake);
    return wake;
}

esp_err_t app_enter_host_sleep(app_sleep_mode_t mode) {
    periph_suspend(true);
    pmu_selftest_prepare_powerdown();
    esp_err_t err = mode == APP_SLEEP_OFF
        ? read_pico_pmu_power_off()
        : read_pico_pmu_report_sleep();
    ESP_LOGI(TAG, "%s %s", mode == APP_SLEEP_OFF ? "power off" : "SOFT_SLEEP", esp_err_to_name(err));
    if (err == ESP_OK) {
        vTaskDelay(pdMS_TO_TICKS(HOST_EN_DROP_WAIT_MS));
        err = ESP_ERR_TIMEOUT;
        ESP_LOGE(TAG, "EN still high after PMU ack");
    }
    // PMU 没把 EN 拉下来：恢复外设交还调用方，不能停在这里空转耗电。
    // The PMU did not drop EN: restore peripherals and return instead of idling at full power.
    periph_resume();
    return err;
}

void enter_lock_and_sleep(
    EpdiyHighlevelState* hl, int64_t* ignore_until_ms, sc7a20h_handle_t acc
) {
    size_t image_bytes = 0;
    uint8_t* image = ui_packed_asset_unpack(lock_4bpp_p4b_deflate_start,
        (size_t)(lock_4bpp_p4b_deflate_end - lock_4bpp_p4b_deflate_start), &image_bytes);
    if (!image) return;
    if(!ui_image_validate(image,image_bytes,epd_width(),epd_height())) {
        ESP_LOGE(TAG,"Packed lock image validation failed");
        free(image);
        return;
    }
    // 乐曲的 32 kHz 采样中断和线圈电流会干扰锁屏整屏刷新，先停再上屏轨。
    // The score's 32 kHz sample ISR and coil current disturb the lock-face full refresh; stop it before the rails come up.
    if (read_pico_buzzer_score_busy()) (void)read_pico_buzzer_score_stop();
    uint8_t* framebuffer = epd_hl_get_framebuffer(hl);
    epd_poweron();
    epd_clear();
    epd_hl_set_all_white(hl);
    if(!ui_image_decode(framebuffer,(size_t)epd_width()*epd_height()/2,
                        image,image_bytes,epd_width(),epd_height(),
                        (ui_image_rotation_t)epd_get_rotation())) {
        ESP_LOGE(TAG,"Packed lock image decode failed");
        free(image);
        return;
    }
    epd_hl_update_screen_from_white(hl, MODE_GC16, 25);
    free(image);

    app_lock_wait_key_idle(800);
    app_sleep_mode_t mode = app_settings_sleep_mode();
    ESP_LOGI(TAG, "lock %s", app_sleep_mode_name(mode));

    // 主机下电只在失败时返回，退到浅睡，至少外设已停、按键可唤醒。
    // Host power-down returns only on failure; fall back to light sleep with peripherals parked and the key as wake.
    if (mode == APP_SLEEP_OFF || mode == APP_SLEEP_DEEP) {
        ESP_LOGW(TAG, "host sleep failed: %s, light sleep", esp_err_to_name(app_enter_host_sleep(mode)));
    }
    epd_poweroff();
    app_light_sleep_wait(acc);

    // 参考帧和屏幕都归零，回到主循环后由当前页自己画一遍，不必知道是哪一页。
    // Zero the reference frame and the panel; the current page redraws after the loop resumes, without knowing which page it is.
    read_pico_pmu_drain_events();
    epd_poweron();
    epd_clear();
    epd_hl_set_all_white(hl);
    read_pico_pmu_drain_events();
    if (ignore_until_ms) {
        *ignore_until_ms = esp_timer_get_time() / 1000 + APP_LOCK_IGNORE_BOOT_MS;
    }
    ESP_LOGI(TAG, "unlocked");
}

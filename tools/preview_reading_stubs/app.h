/* SPDX-License-Identifier: Apache-2.0
 * 主机阅读测试的页面契约子集。/ Page contract subset for reading host checks.
 */
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "epdiy.h"
typedef enum {
    APP_REDRAW_NONE, APP_REDRAW_AREA, APP_REDRAW_PAGE, APP_REDRAW_FULL, APP_REDRAW_DONE
} app_redraw_t;
typedef struct app_desc_s app_desc_t;
typedef struct ui_gesture_event_s ui_gesture_event_t;
typedef struct {
    int64_t now_ms;
    const app_desc_t* request_app;
    bool request_return, request_menu;
} app_ctx_t;
struct app_desc_s {
    const char *title, *detail;
    bool enter_full, owns_keys, blocks_idle_sleep;
    void (*on_enter)(app_ctx_t*);
    void (*on_exit)(app_ctx_t*);
    void (*render)(app_ctx_t*, uint8_t*);
    app_redraw_t (*on_key)(app_ctx_t*, int);
    app_redraw_t (*on_key_long)(app_ctx_t*, int);
    app_redraw_t (*on_gesture)(app_ctx_t*, const ui_gesture_event_t*);
    app_redraw_t (*on_tick)(app_ctx_t*);
};

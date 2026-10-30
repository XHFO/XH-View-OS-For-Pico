#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "epdiy.h"
typedef struct { int unused; } app_desc_t;
typedef struct { int unused; } app_ctx_t;
typedef enum { APP_REDRAW_NONE, APP_REDRAW_AREA, APP_REDRAW_PAGE,
    APP_REDRAW_FULL, APP_REDRAW_DONE } app_redraw_t;

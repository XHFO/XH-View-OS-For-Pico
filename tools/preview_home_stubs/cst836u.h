/* 中文：仅用于宿主接口。/ English: Host interface only. */
#pragma once
#include <stdbool.h>
#include <stdint.h>
typedef void* cst836u_handle_t;
typedef struct { bool touched; uint16_t x, y; } cst836u_touch_t;
typedef struct { int unused; } cst836u_info_t;

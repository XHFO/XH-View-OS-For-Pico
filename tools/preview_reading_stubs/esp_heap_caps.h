/* SPDX-License-Identifier: Apache-2.0
 * 主机堆替身，不测量 PSRAM 性能。/ Host heap substitute; no PSRAM performance claim.
 */
#pragma once
#include <stdlib.h>
#define MALLOC_CAP_SPIRAM 0
#define MALLOC_CAP_8BIT 0
#define heap_caps_malloc(n, caps) malloc(n)
#define heap_caps_realloc(ptr, n, caps) realloc(ptr, n)

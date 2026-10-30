/* SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 * 中文：独立计页测试的故障与分配尺寸观测。/ English: Pagination allocation fault and size observation.
 */
#pragma once
#include <stdlib.h>
#define MALLOC_CAP_SPIRAM 1
#define MALLOC_CAP_8BIT 2
extern int job_test_fail_after;
extern size_t job_test_largest_request;
static inline int job_test_fail(size_t n) {
    if (n > job_test_largest_request) job_test_largest_request = n;
    return job_test_fail_after >= 0 && job_test_fail_after-- == 0;
}
static inline void* heap_caps_malloc(size_t n, int caps) { (void)caps; return job_test_fail(n) ? NULL : malloc(n); }
static inline void* heap_caps_calloc(size_t n, size_t s, int caps) { (void)caps; return job_test_fail(n*s) ? NULL : calloc(n,s); }
static inline void* heap_caps_realloc(void* p, size_t n, int caps) { (void)caps; return job_test_fail(n) ? NULL : realloc(p,n); }

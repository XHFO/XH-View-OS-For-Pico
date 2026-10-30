/* SPDX-License-Identifier: Apache-2.0
 * 中文：配对内容管理的有界文件流；不挂载卡、不接管USB或执行应用。
 * English: Bounded paired-content file streams; never mount cards, own USB or execute apps.
 */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#define READ_PICO_CONTENT_MAX (256u*1024u*1024u)
#define READ_PICO_CONTENT_PAGE_MAX 64u
typedef struct {
    const char *method, *endpoint, *query, *sha256;
    size_t bytes;
    void *context;
    bool (*authorized)(void *);
    int (*receive)(void *, void *, size_t);
    bool (*download)(void *, const void *, size_t);
} read_pico_content_request_t;
/// HTTP线程的存储交接；UI须先暂停本地消费者，再授予租约。/ HTTP storage handoff; grant only after UI pauses local consumers.
typedef bool (*read_pico_content_acquire_t)(void);
typedef void (*read_pico_content_release_t)(bool changed);
void read_pico_content_set_storage_hooks(read_pico_content_acquire_t acquire, read_pico_content_release_t release, void (*cancel)(void));
/// 停止管理服务前撤销等待中的租约，避免UI等待HTTP的死锁。/ Revoke pending leases before HTTP joins to avoid UI/HTTP deadlock.
void read_pico_content_cancel(void);
/// 请求只接受已鉴权调用；每块重复验证，输出JSON或download流。/ Call only after authorization; recheck every block and return JSON or a download stream.
int read_pico_content_handle(const read_pico_content_request_t *request, char *out, size_t capacity);
bool read_pico_content_busy(void);

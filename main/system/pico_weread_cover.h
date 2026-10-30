/*
 * SPDX-License-Identifier: Apache-2.0
 * 中文：微信读书官方封面的小型只读缓存；后台取图，前台仅画内存。
 * English: A small read-only cache of official WeRead covers, downloaded in the background and painted from memory.
 * 冻结：无凭据、无TF写入、无跳转或不安全TLS；取图失败不影响图书资料。
 * Frozen: No credentials, TF writes, redirects or insecure TLS; cover failures never block book information.
 */
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <stdatomic.h>
#ifdef PICO_WEREAD_HOST_TEST
void wr_test_cover_clear(void);
void wr_test_cover_prepare(uint32_t id,const char *url,const atomic_bool *cancelled);
#define ps_weread_cover_clear wr_test_cover_clear
#define ps_weread_cover_prepare wr_test_cover_prepare
#else
/// 无IO清除借用图像。/ Clear the borrowed image without I/O.
void ps_weread_cover_clear(void);
/// 仅查询工作器调用；固定官方CDN、最多256KiB、可取消。/ Query-worker only: fixed official CDNs, at most 256KiB and cancellable.
void ps_weread_cover_prepare(uint32_t request_id,const char *url,const atomic_bool *cancelled);
#endif
/// 纯绘制已准备的同一查询封面；逻辑坐标，16灰阶。/ Paint a prepared cover for the same query in logical coordinates and 16 gray levels.
bool ps_weread_cover_draw(uint32_t request_id,uint8_t *fb,int x,int y,int width,int height);

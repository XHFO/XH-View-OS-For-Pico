/* SPDX-License-Identifier: Apache-2.0
 * 中文：经过边界检查的TF应用代码加载；模块生命周期由调用方管理。
 * English: Bounds-checked TF application loading; callers own module lifecycle.
 */
#pragma once
#include "pico_app_sdk.h"

/// 仅校验受支持的ESP32-S3 ELF子集；不会执行代码，可在桌面构建中复用。
/// Validate the supported ESP32-S3 ELF subset without executing code, including on desktop builds.
bool pico_app_code_validate_xtensa(const uint8_t *image,size_t bytes,char *error,size_t error_bytes);

/// 加载并查询ABI；image只需在此调用期间有效，成功后由handle持有代码内存。
/// Load and query the ABI; image need only survive this call, while handle retains executable memory.
/// 此接口不提供沙箱；只安装可信来源的应用包，并串行化加载与Flash/NVS写入。
/// This is not a sandbox; install trusted packages and serialize loads against Flash/NVS writes.
bool pico_app_code_open(const uint8_t *image,size_t bytes,const pico_app_exports_t **exports,
                        void **handle,char *error,size_t error_bytes);

/// 调用前必须先调用exports->stop()并等待所有工作任务结束；卸载后所有导出指针失效。
/// First call exports->stop() and join every worker; unloading invalidates all export pointers.
void pico_app_code_close(void *handle);

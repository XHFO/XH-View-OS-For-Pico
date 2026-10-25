/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 * 已联网设备的扫码管理服务；不拥有Wi-Fi、TF卡或家庭worker。
 * QR management on an existing connection; owns neither Wi-Fi, TF nor the home worker.
 * 冻结：仅用户显式开启，十五分钟到期；凭据不得进入日志或公开快照。
 * Frozen: Explicit user start, fifteen-minute expiry; never log credentials or expose them in public snapshots.
 */
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

/// 扩展只在同源配对鉴权后调用；启动HTTP前注册一次。/ Extension runs after same-origin pairing authorization; register before HTTP starts.
typedef int (*read_pico_management_extension_t)(const char*,const char*,const char*,const void*,size_t,char*,size_t);
void read_pico_management_set_extension(read_pico_management_extension_t handler);

/// 只读图片扩展同样经过配对鉴权；成功返回分配的BMP及长度，由调用者释放。/ Paired, authorized read-only image extension; caller frees returned BMP bytes.
typedef int (*read_pico_management_image_extension_t)(const char*,uint8_t**,size_t*);
void read_pico_management_set_image_extension(read_pico_management_image_extension_t handler);

typedef struct {
    bool active; ///< HTTP正在监听 / HTTP is listening
    char url[192]; ///< 仅扫码页或身份核验后的可信USB使用，含临时密钥 / QR UI or identity-verified trusted USB only; includes a temporary key
    char host[48]; ///< 无密钥的主机地址 / Host without session key
    uint32_t seconds_left; ///< 剩余秒数 / Seconds remaining
    char error[96]; ///< 不含凭据的错误 / Credential-free error
} read_pico_management_status_t;
/// 使用当前STA；无需TF，不中断家庭服务。/ Borrow current STA; no TF requirement or home-service interruption.
bool read_pico_management_start(void);
/// 停止并等待请求退出，撤销二维码密钥。/ Stop and join requests, revoking the QR key.
void read_pico_management_stop(void);
/// 管理窗口内阻止自动休眠；手动电源操作仍有效。/ Block automatic idle sleep while serving; manual power actions remain available.
bool read_pico_management_active(void);
/// 复制状态；仅扫码界面或身份匹配的有线引导可消费url，不可记日志或持久化。/ Copy status; only QR UI or identity-matched wired setup may consume url, never logs or persistence.
void read_pico_management_get_status(read_pico_management_status_t *out);
/// 在UI事件循环调用；断网、转入传输或到期后停止。/ UI lifecycle poll; stop on disconnect, transfer or expiry.
void read_pico_management_tick(void);

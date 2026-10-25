/*
 * SPDX-FileCopyrightText: 2026 XH Lab
 * SPDX-License-Identifier: Apache-2.0
 * 经过研究的Bambu社区协议模型；秘密只在RAM内，运行时独占工作器提供TLS。
 * Researched Bambu community-protocol model; secrets remain in RAM and the runtime worker owns TLS.
 * 冻结：不保存密码，不伪造登录，不绕过验证码/反机器人或证书校验。
 * Frozen: Never persist passwords, synthesize login, or bypass verification/anti-bot/TLS checks.
 */
#pragma once
#include "read_pico_home_link.h"

typedef struct {
    rp_home_bambu_state_t public;
    char account[128], token[1024], username[96];
} rp_home_bambu_session_t;

/// 使用调用方的阻塞TLS传输；不得从HTTP/UI处理器直接调用。
/// Use the caller's blocking TLS transport; never invoke directly from HTTP/UI handlers.
void rp_home_bambu_execute(const rp_home_bambu_input_t *, rp_home_bambu_session_t *, const rp_home_transport_t *);
bool rp_home_bambu_input_valid(const rp_home_bambu_input_t *);
void rp_home_bambu_wipe(void *, size_t);

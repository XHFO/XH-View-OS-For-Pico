/*
 * 产品连接页与存储生命周期桥；网络与HTTP由独立worker管理。
 * Product connection page and storage bridge; a separate worker owns WiFi/HTTP.
 * 冻结：先join接收任务再恢复字体或维护TF，不自动清卡。
 * Frozen: join reception before resuming fonts or TF maintenance; never auto-format.
 */
#pragma once
#include "pico_system.h"
#include "read_pico_transfer.h"

/// 上传独占存储，停止并汇合HTTP后才能恢复字体。/ Upload owns storage until HTTP has stopped and joined.
esp_err_t ps_transfer_prepare(read_pico_transfer_cfg_t *out);
void ps_transfer_finished(unsigned changed_count);
void ps_transfer_capacity(uint64_t *free_bytes,bool *is_flash,size_t *file_limit);
bool ps_transfer_prepared(void);
/// 主循环上的可信维护/引导入口，复用同一个真实传书流程。/ Main-loop maintenance/onboarding uses the same real transfer lifecycle.
bool ps_connectivity_request_transfer(read_pico_transfer_mode_t mode);

/// 和WiFi页面按钮共用的扫描意图入口，需先导航至WiFi页。/ Share the WiFi button scan intent after navigating to the WiFi page.
bool ps_connectivity_request_scan(void);

/// 微信读书打开本设备局域网配置二维码，不借助桌面软件。/ Open the device-local WeRead setup QR without desktop software.
bool ps_connectivity_open_weread_setup(void);

#define PS_WIRELESS_REQUEST_MAX 512u
#define PS_WIRELESS_RESPONSE_MAX 4096u
#define PS_WIRELESS_PREFIX "PICO_WIFI1 "
/// 可信USB的严格ASCII JSON请求；只在主循环执行，响应含临时密钥时调用者必须擦除。/ Strict ASCII JSON for trusted USB; UI-loop only, and callers must wipe responses containing temporary keys.
void ps_connectivity_wireless_request(const char *request,char *out,size_t capacity);
/// 无切页的异步引导与配对到期维护，不保留密码。/ Advance asynchronous setup and pairing expiry without navigation or retained passwords.
void ps_connectivity_wireless_tick(void);

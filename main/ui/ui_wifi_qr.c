/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 * 编码手机可识别的WiFi二维码，缓存后纯绘制。/ Encode phone-compatible WiFi QR codes and cache for pure drawing.
 * 冻结：使用标准QR而非Data Matrix；不向日志输出载荷。/ Frozen: use standard QR, not Data Matrix; never log payloads.
 */
#include "ui_wifi_qr.h"
#include <string.h>
#include "qrcode.h"
#include "esp_log.h"

#define QR_SIDE_MAX 57
static uint8_t s_modules[(QR_SIDE_MAX * QR_SIDE_MAX + 7) / 8];
static int s_side;

void ui_wifi_qr_clear(void) {
    s_side = 0;
    memset(s_modules, 0, sizeof(s_modules));
}

static void cache_qr(esp_qrcode_handle_t qr) {
    int side = esp_qrcode_get_size(qr);
    if (side > QR_SIDE_MAX) return;
    s_side = side;
    for (int y = 0; y < side; ++y)
        for (int x = 0; x < side; ++x) {
            int bit = y * side + x;
            if (esp_qrcode_get_module(qr, x, y)) s_modules[bit / 8] |= 1u << (bit % 8);
        }
}

static char* escape_field(char* out, const char* in) {
    while (*in) {
        if (strchr("\\;,:\"", *in)) *out++ = '\\';
        *out++ = *in++;
    }
    return out;
}

static bool prepare_payload(const char* payload) {
    esp_qrcode_config_t cfg = ESP_QRCODE_CONFIG_DEFAULT();
    cfg.display_func = cache_qr;
    // 组件默认会打印载荷；编码期间保留错误日志，不输出连接口令。
    // The component logs payloads by default; retain errors without printing the connection password.
    esp_log_level_t previous = esp_log_level_get("QRCODE");
    esp_log_level_set("QRCODE", ESP_LOG_WARN);
    esp_err_t err = esp_qrcode_generate(&cfg, payload);
    esp_log_level_set("QRCODE", previous);
    if (err != ESP_OK) ui_wifi_qr_clear();
    return err == ESP_OK && s_side != 0;
}

bool ui_wifi_qr_prepare(const char* ssid, const char* password) {
    ui_wifi_qr_clear();
    if (!ssid || !password || !*ssid || strlen(ssid) > 32 || strlen(password) > 64) return false;
    char payload[224];
    strcpy(payload, "WIFI:T:WPA;S:");
    char* end = escape_field(payload + strlen(payload), ssid);
    memcpy(end, ";P:", 3); end += 3;
    end = escape_field(end, password);
    memcpy(end, ";;", 3);
    bool ok = prepare_payload(payload);
    volatile char* clear = payload;
    for (size_t i = 0; i < sizeof(payload); ++i) clear[i] = 0;
    return ok;
}

bool ui_wifi_qr_prepare_url(const char* url) {
    ui_wifi_qr_clear();
    if (!url || strncmp(url, "http://", 7)) return false;
    const char* p = url + 7;
    for (int i = 0; i < 4; ++i) {
        unsigned value = 0, digits = 0;
        while (*p >= '0' && *p <= '9') {
            if (++digits > 3) return false;
            value = value * 10 + (unsigned)(*p++ - '0');
        }
        if (!digits || value > 255) return false;
        if (i < 3 && *p++ != '.') return false;
    }
    if (*p == ':') {
        unsigned port = 0, digits = 0; ++p;
        while (*p >= '0' && *p <= '9') { if (++digits > 5) return false; port = port * 10 + (unsigned)(*p++ - '0'); }
        if (!digits || !port || port > 65535) return false;
    }
    if (*p && strcmp(p, "/")) {
        bool home_portal = !strncmp(p, "/h#t=", 5);
        if (home_portal) p += 5;
        else {
            if (strncmp(p, "/manage#key=", 12)) return false;
            p += 12;
        }
        for (int i = 0; i < 32; ++i) { if (!((*p >= '0' && *p <= '9') || (*p >= 'a' && *p <= 'f'))) return false; ++p; }
        // 家庭门户仅允许密钥；旧管理页只额外接受固定微信读书目标。
        // Home portal accepts the key only; legacy management also accepts its fixed WeRead target.
        if (*p && (home_portal || strcmp(p, "&view=weread"))) return false;
    }
    return strlen(url) < 192 && prepare_payload(url);
}

bool ui_wifi_qr_prepare_weread(const char* url) {
    ui_wifi_qr_clear();
    const char *prefix="https://weread.qq.com/web/";
    if(!url||strncmp(url,prefix,strlen(prefix)))return false;
    const char *path=url+strlen(prefix);
    if(strncmp(path,"reader/",7)&&strncmp(path,"bookDetail/",11))return false;
    const char *id=path+(!strncmp(path,"reader/",7)?7:11);
    if(!*id||*id=='?'||*id=='#'||strlen(url)>=256)return false;
    for(const unsigned char *p=(const unsigned char*)id;*p;p++)
        if(*p<=32||*p>=127||*p=='\\'||*p=='<'||*p=='>'||*p=='"'||*p=='\'')return false;
    return prepare_payload(url);
}

void ui_wifi_qr_draw(uint8_t* fb, EpdRect area) {
    epd_fill_rect(area, 0xff, fb);
    if (!s_side) return;
    int width = area.width < area.height ? area.width : area.height;
    int scale = width / (s_side + 8);
    if (scale < 1) return;
    int x0 = area.x + (area.width - s_side * scale) / 2;
    int y0 = area.y + (area.height - s_side * scale) / 2;
    for (int y = 0; y < s_side; ++y)
        for (int x = 0; x < s_side; ++x) {
            int bit = y * s_side + x;
            if (s_modules[bit / 8] & (1u << (bit % 8)))
                epd_fill_rect((EpdRect){x0 + x * scale, y0 + y * scale, scale, scale}, 0, fb);
        }
}

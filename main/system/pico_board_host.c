/* SPDX-License-Identifier: Apache-2.0
 * 中文：只加载TF看板引擎并桥接原生绘图；不含卡片排版或应用功能。
 * English: Load TF board engines and bridge native drawing, without card layout/application code.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "epdiy.h"
#include "esp_heap_caps.h"
#include "pico_app_loader.h"
#include "pico_app_package.h"
#include "pico_board_data.h"
#include "pico_system.h"
#include "ttf_font.h"
static const pico_app_exports_t* engine;
static void* code;
static bool engine_app, retry_app;
static int64_t retry_at;
bool ps_board_renderer_available(bool app) {
    return engine && engine_app == app;
}
static bool board_time(double epoch, bool clock, char* out, size_t bytes) {
    time_t value = (time_t)epoch;
    struct tm tm;
    return gmtime_r(&value, &tm) && strftime(out, bytes, clock ? "%H:%M" : "%Y-%m-%d", &tm) > 0;
}
static const pico_app_host_t host = { .abi = 1,
                                      .bytes = sizeof(host),
                                      .copy = memcpy,
                                      .format = snprintf,
                                      .board_time = board_time,
                                      .board_model_abi = PICO_BOARD_MODEL_ABI };
void ps_board_renderer_release(void) {
    if (engine) {
        engine->stop();
        pico_app_code_close(code);
    }
    engine = NULL;
    code = NULL;
    retry_at = 0;
}
bool ps_board_renderer_prepare(bool app) {
    if (engine && engine_app == app) return true;
    if (retry_app == app && ps_now() < retry_at) return false;
    ps_board_renderer_release();
    retry_app = app;
    retry_at = ps_now() + 1000;
    char path[96], error[80];
    const char* id = app ? "website-traffic" : "lingxi-board";
    snprintf(path, sizeof(path), PICO_APP_TOOLS_DIRECTORY "/%s.pga", id);
    pico_app_package_t info;
    if (!pico_app_package_read(path, &info, true, error, sizeof(error)) || strcmp(info.id, id) ||
        info.kind != (app ? PGA_WEBSITE_TRAFFIC : PGA_LINGXI_BOARD))
        return false;
    const pico_app_payload_t* p = pico_app_package_payload(&info);
    if (!p) return false;
    uint8_t* data = heap_caps_malloc(p->bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!data) return false;
    FILE* f = fopen(path, "rb");
    bool ok = f && fseek(f, p->offset, SEEK_SET) == 0 && fread(data, 1, p->bytes, f) == p->bytes;
    if (f) fclose(f);
    ok = ok && pico_app_crc32(data, p->bytes) == p->crc &&
         pico_app_code_open(data, p->bytes, &engine, &code, error, sizeof(error));
    free(data);
    if (!ok) {
        engine = NULL;
        code = NULL;
        return false;
    }
    if (engine->kind != info.kind || engine->version != info.version || !engine->id ||
        strcmp(engine->id, id) ||
        engine->bytes < offsetof(pico_app_exports_t, board_paint) + sizeof(engine->board_paint) ||
        !engine->board_paint || !engine->start || !engine->stop || !engine->start(&host)) {
        pico_app_code_close(code);
        engine = NULL;
        code = NULL;
        return false;
    }
    engine_app = app;
    retry_at = 0;
    return true;
}
static void device_fill(void* fb, int x, int y, int w, int h, int gray) {
    ps_fill(fb, x, y, w, h, gray);
}
static void device_line(void* fb, int x0, int y0, int x1, int y1, int gray) {
    ps_line(fb, x0, y0, x1, y1, gray);
}
static void device_text(void* fb, int x, int y, int size, const char* text, int gray) {
    ps_body_text(fb, x, y, size, text, gray);
}
static int device_measure(void* unused, int size, const char* text) {
    (void)unused;
    int previous = ttf_get_weight();
    if (previous < 600) ttf_set_weight(600);
    int width = ttf_text_width_px((size * 19 + 5) / 10, text);
    ttf_set_weight(previous);
    return (width * 10 + 18) / 19;
}
static void device_image(void* fb, const ps_board_asset_t* asset, int x, int y, int w, int h) {
    EpdRect r = ps_rect(x, y, w, h);
    for (int yy = 0; yy < r.height; yy++) {
        unsigned sy = (unsigned)((int64_t)yy * asset->height / r.height);
        const uint8_t* row = asset->pixels + sy * ((asset->width + 1u) / 2u);
        for (int xx = 0; xx < r.width; xx++) {
            unsigned sx = (unsigned)((int64_t)xx * asset->width / r.width);
            unsigned gray = sx & 1 ? row[sx / 2] & 15 : row[sx / 2] >> 4;
            epd_draw_pixel(r.x + xx, r.y + yy, (uint8_t)(gray * 17), fb);
        }
    }
}

void ps_board_model_render(uint8_t* fb, const ps_board_model_t* model) {
    if (!fb) return;
    if (!engine) {
        ps_heading(fb, "请安装 TF 应用", "应用集 → 应用下载 → 灵犀画板 / 网站访问看板");
        return;
    }
    ps_view_t v = ps_view();
    ps_board_painter_t painter = {
        fb, v.w, v.h, device_fill, device_line, device_text, device_measure, device_image
    };
    engine->board_paint(model, &painter);
}

/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 *
 * 字体同源主机夹具：模拟硬件内存接口，逐像素对比真实工作字体与原始TTF。
 * Native font host fixture: shim hardware allocation and compare working-font pixels with original TTFs.
 */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ttf_font.h"

#define MALLOC_CAP_SPIRAM 1
#define MALLOC_CAP_8BIT 2
#define ESP_LOGI(...) ((void)0)
#define ESP_LOGW(...) ((void)0)
#define ESP_LOGE(...) ((void)0)
static void *heap_caps_malloc(size_t n, int caps) { (void)caps; return malloc(n); }
static void *heap_caps_calloc(size_t n, size_t s, int caps) { (void)caps; return calloc(n, s); }
static void heap_caps_free(void *p) { free(p); }
static size_t heap_caps_get_largest_free_block(int caps) { (void)caps; return 16 * 1024 * 1024; }
static int64_t esp_timer_get_time(void) { return 0; }
static const char *app_settings_font_path(void) { return "builtin"; }
void epd_draw_pixel(int x, int y, uint8_t gray, uint8_t *fb) {
    (void)x; (void)y; (void)gray; (void)fb;
}

/* 临时文件由Python从实际生产源生成，仅换硬件接口。 / Python adapts actual production source in a temporary file. */
#include "ttf_font-under-test.c"

int main(int argc, char **argv) {
    assert(argc == 4);
    unsigned checks = 0;
    for (int a = 1; a < argc; a++) {
        FILE *f = fopen(argv[a], "rb");
        assert(f);
        fseek(f, 0, SEEK_END);
        long n = ftell(f);
        rewind(f);
        uint8_t *raw = malloc((size_t)n);
        assert(raw && fread(raw, 1, (size_t)n, f) == (size_t)n);
        fclose(f);
        builtin_ttf_start = serif_ttf_start = kai_ttf_start = raw;
        builtin_ttf_end = serif_ttf_end = kai_ttf_end = raw + n;
        assert(ttf_font_open_builtin() == ESP_OK && font_ready && !gvar_ready);
        stbtt_fontinfo reference;
        assert(stbtt_InitFont(&reference, raw, 0));
        /* 包含宋/楷中超过512点的龘与𰻞，以及补充平面汉字。 / Include >512-point rare glyphs and supplementary CJK. */
        const uint32_t chars[] = { 0x9f98, 0x30ede, 0x20bb7, 0x9ea4, 0x9605, 0x8bfb, 0x20, 0x41,
            // 蓝牙、图文载入与旧UI保留字。/ Bluetooth, image-loading and retained UI glyphs.
            0x84dd, 0x7259, 0x7ffb, 0x9875, 0x5668, 0x56fe, 0x7247, 0x6682,
            0x8bd5, 0x7bad, 0x5e95, 0x6e10, 0x25, 0x30, 0x39 };
        for (int px = 12; px <= 80; px += 4) {
            for (int weight = 400; weight <= 700; weight += 300) {
                for (unsigned i = 0; i < sizeof(chars) / sizeof(chars[0]); i++) {
                    uint32_t cp = chars[i];
                    int gid = stbtt_FindGlyphIndex(&reference, (int)cp);
                    assert(gid > 0);
                    ttf_set_weight(weight);
                    const glyph_entry_t *actual = get_glyph(cp, px);
                    assert(actual);
                    float scale = stbtt_ScaleForPixelHeight(&reference, (float)px);
                    int x0, y0, x1, y1;
                    stbtt_GetGlyphBitmapBox(&reference, gid, scale, scale, &x0, &y0, &x1, &y1);
                    int w = x1 - x0, h = y1 - y0;
                    assert(actual->width == w && actual->height == h && actual->left == x0 && actual->top == -y0);
                    if (w && h) {
                        uint8_t *expected = malloc((size_t)w * h);
                        assert(expected);
                        stbtt_MakeGlyphBitmap(&reference, expected, w, h, w, scale, scale, gid);
                        /* 对照相同的静态加粗策略。 / Apply the same static emboldening to the reference. */
                        if (weight >= 600) {
                            for (int row = 0; row < h; row++) {
                                for (int col = w - 1; col > 0; col--) {
                                    uint8_t left = expected[row * w + col - 1];
                                    if (left > expected[row * w + col]) expected[row * w + col] = left;
                                }
                            }
                        }
                        assert(!memcmp(actual->bitmap, expected, (size_t)w * h));
                        free(expected);
                    }
                    checks++;
                }
            }
        }
        ttf_font_unload();
        free(raw);
    }
    heap_caps_free(work); work = NULL;
    heap_caps_free(io_data); io_data = NULL;
    heap_caps_free(io_run_buf); io_run_buf = NULL;
    printf("native font packing+raster: %u cases; three static fonts, rare 768-point glyphs, 18 sizes, two weights match source stb pixels\n", checks);
    return 0;
}

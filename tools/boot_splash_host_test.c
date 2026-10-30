/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 * 中文：直接测试共享生产解压器、启动/锁屏完整像素及失败分支的回收/绘制原子性。
 * English: Exercise the shared production inflater, exact splash/lock pixels, cleanup and drawing atomicity.
 * 冻结：主机zlib仅适配ROM接口，不声称验证ESP ROM或真实屏幕。
 * Frozen: Host zlib adapts the ROM API only; this does not validate ESP ROM or the physical screen.
 */
#include <assert.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "miniz.h"
#include "ui_image.h"

#define MALLOC_CAP_SPIRAM 1u
#define MALLOC_CAP_8BIT 2u
#define MZ_CRC32_INIT 0
#define ESP_LOGE(tag, ...) test_log(tag, __VA_ARGS__)
static unsigned allocations, fail_allocation, live_allocations, errors;
static size_t live_bytes, peak_bytes;
static struct { uint8_t* allocation; size_t bytes; } allocated[2];
static void test_log(const char* tag, const char* format, ...) {
    assert(tag && format && *format);
    ++errors;
}
static void* test_allocate(size_t bytes, unsigned caps) {
    assert(caps == (MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (++allocations == fail_allocation) return NULL;
    assert(live_allocations < 2);
    uint8_t* allocation = malloc(bytes + 32);
    assert(allocation);
    memset(allocation, 0xa5, bytes + 32);
    for (size_t i = 0; i < 2; ++i) if (!allocated[i].allocation) {
        allocated[i].allocation = allocation;
        allocated[i].bytes = bytes;
        break;
    }
    ++live_allocations;
    live_bytes += bytes;
    if (live_bytes > peak_bytes) peak_bytes = live_bytes;
    return allocation + 16;
}
static void test_free(void* pointer) {
    if (!pointer) return;
    for (size_t i = 0; i < 2; ++i) if (allocated[i].allocation && allocated[i].allocation + 16 == pointer) {
        uint8_t* allocation = allocated[i].allocation;
        size_t bytes = allocated[i].bytes;
        for (size_t guard = 0; guard < 16; ++guard) {
            assert(allocation[guard] == 0xa5);
            assert(allocation[16 + bytes + guard] == 0xa5);
        }
        live_bytes -= bytes;
        --live_allocations;
        allocated[i].allocation = NULL;
        free(allocation);
        return;
    }
    assert(!"unknown/double free");
}
static unsigned long mz_crc32(unsigned long crc, const unsigned char* bytes, size_t count) {
    return crc32(crc, bytes, (uInt)count);
}

#define heap_caps_malloc test_allocate
#define free test_free
#include "ui_packed_asset.c"
#undef free

static uint8_t* read_file(const char* path, size_t* bytes) {
    FILE* file = fopen(path, "rb");
    assert(file && !fseek(file, 0, SEEK_END));
    long count = ftell(file);
    assert(count > 0);
    rewind(file);
    uint8_t* result = malloc((size_t)count);
    assert(result && fread(result, 1, (size_t)count, file) == (size_t)count);
    fclose(file);
    *bytes = (size_t)count;
    return result;
}
static void no_live_allocations(void) { assert(!live_bytes && !live_allocations); }
static void unchanged(const uint8_t* framebuffer, size_t bytes) {
    for (size_t i = 0; i < bytes; ++i) assert(framebuffer[i] == 0xa5);
}
static bool try_draw(const uint8_t* packed, size_t bytes, uint8_t* framebuffer, size_t fb_bytes) {
    size_t image_bytes = SIZE_MAX;
    uint8_t* image = ui_packed_asset_unpack(packed, bytes, &image_bytes);
    if (!image) { assert(image_bytes == 0); no_live_allocations(); return false; }
    assert(live_allocations == 1);
    bool ok = ui_image_validate(image, image_bytes, 1216, 684) &&
        ui_image_decode(framebuffer, fb_bytes, image, image_bytes, 1216, 684, UI_IMAGE_LANDSCAPE);
    test_free(image);
    no_live_allocations();
    return ok;
}
static void reject(const uint8_t* packed, size_t bytes, uint8_t* framebuffer, size_t fb_bytes) {
    memset(framebuffer, 0xa5, fb_bytes);
    assert(!try_draw(packed, bytes, framebuffer, fb_bytes));
    unchanged(framebuffer, fb_bytes);
}
int main(int argc, char** argv) {
    assert(argc == 5);
    size_t packed_bytes, original_bytes, raw_bytes, corrupt_bytes;
    uint8_t* packed = read_file(argv[1], &packed_bytes);
    uint8_t* original = read_file(argv[2], &original_bytes);
    uint8_t* raw = read_file(argv[3], &raw_bytes);
    uint8_t* corrupt_p4b = read_file(argv[4], &corrupt_bytes);
    const char* asset_name = strrchr(argv[2], '/');
    asset_name = asset_name ? asset_name + 1 : argv[2];
    assert(raw_bytes == 415872);
    if (!strcmp(asset_name, "lock_4bpp.p4b")) assert(original_bytes == 36656);
    else {
        assert(!strcmp(asset_name, "loading_4bpp.p4b"));
        assert(original_bytes >= 24 && original_bytes <= 22828);
        assert(packed_bytes > 16 && packed_bytes <= 6747);
    }
    uint8_t* framebuffer = malloc(raw_bytes), *reference = malloc(raw_bytes);
    uint8_t* mutation = malloc(packed_bytes + 1);
    assert(framebuffer && reference && mutation);

    size_t image_bytes = 0;
    uint8_t* image = ui_packed_asset_unpack(packed, packed_bytes, &image_bytes);
    assert(image && image_bytes == original_bytes && !memcmp(image, original, image_bytes));
    assert(live_allocations == 1 && live_bytes == original_bytes);
    assert(peak_bytes == original_bytes + sizeof(tinfl_decompressor));
    assert(ui_image_validate(image, image_bytes, 1216, 684));
    for (int rotation = 0; rotation < 4; ++rotation) {
        memset(framebuffer, 0xa5, raw_bytes);
        memset(reference, 0x5a, raw_bytes);
        assert(ui_image_decode(framebuffer, raw_bytes, image, image_bytes, 1216, 684, (ui_image_rotation_t)rotation));
        assert(ui_image_decode(reference, raw_bytes, original, original_bytes, 1216, 684, (ui_image_rotation_t)rotation));
        assert(!memcmp(framebuffer, reference, raw_bytes));
        if (!rotation) assert(!memcmp(framebuffer, raw, raw_bytes));
    }
    test_free(image);
    no_live_allocations();

    // 两次分配分别失败，坏头/坏流/多余输入/CRC错误都不能触碰帧缓冲。
    // Fail each allocation separately; bad headers/streams/trailing input/CRC never touch the framebuffer.
    for (unsigned which = 1; which <= 2; ++which) {
        allocations = 0;
        fail_allocation = which;
        reject(packed, packed_bytes, framebuffer, raw_bytes);
    }
    fail_allocation = 0;
    assert(!ui_packed_asset_unpack(packed, packed_bytes, NULL));
    no_live_allocations();
    reject(NULL, 0, framebuffer, raw_bytes);
    for (size_t length = 0; length <= 16; ++length) reject(packed, length, framebuffer, raw_bytes);
    reject(packed, packed_bytes / 2, framebuffer, raw_bytes);
    reject(packed, packed_bytes - 1, framebuffer, raw_bytes);
    reject(packed, UI_PACKED_ASSET_MAX_BYTES + 1025u, framebuffer, raw_bytes);
    memcpy(mutation, packed, packed_bytes);
    mutation[packed_bytes] = 0;
    reject(mutation, packed_bytes + 1, framebuffer, raw_bytes);
    for (size_t offset = 0; offset < 16; ++offset) {
        memcpy(mutation, packed, packed_bytes);
        mutation[offset] ^= 0x80;
        reject(mutation, packed_bytes, framebuffer, raw_bytes);
    }
    const uint32_t bad_sizes[] = {0, 23, UI_PACKED_ASSET_MAX_BYTES + 1u, UINT32_MAX};
    for (size_t i = 0; i < sizeof(bad_sizes) / sizeof(bad_sizes[0]); ++i) {
        memcpy(mutation, packed, packed_bytes);
        for (unsigned byte = 0; byte < 4; ++byte) mutation[8 + byte] = (uint8_t)(bad_sizes[i] >> (8u * byte));
        reject(mutation, packed_bytes, framebuffer, raw_bytes);
    }
    // 更小的品牌启动资源使用更密集的坏流注入，保留原先超过200项失败分支检查。
    // Densify corruption probes for the smaller branded splash, retaining the original >200 rejected-input checks.
    const size_t corruption_stride = !strcmp(asset_name, "loading_4bpp.p4b") ? 16 : 31;
    for (size_t offset = 16; offset < packed_bytes; offset += corruption_stride) {
        memcpy(mutation, packed, packed_bytes);
        mutation[offset] ^= 0x40;
        reject(mutation, packed_bytes, framebuffer, raw_bytes);
    }
    // 外层流及其CRC正确、内层P4B损坏时，仍由原验证器挡住绘制。
    // A correct outer stream/CRC containing a corrupt P4B is still rejected by the original validator.
    reject(corrupt_p4b, corrupt_bytes, framebuffer, raw_bytes);
    assert(errors > 200);
    printf("packed asset: exact %zu-byte P4B, four pixel rotations, %u clear failures; all allocations reclaimed and failed draws atomic\n", image_bytes, errors);
    free(framebuffer); free(reference); free(mutation); free(raw); free(original); free(packed); free(corrupt_p4b);
    return 0;
}

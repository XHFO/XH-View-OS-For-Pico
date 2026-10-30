/*
 * 无损压缩的启动/锁屏4bpp资源，不改变原像素或方向语义。
 * Losslessly packed splash/lock 4bpp assets, preserving pixels and rotation.
 * 冻结：完整验证压缩流和CRC后才写目标；拒绝越界或源目标重叠。
 * Frozen: Validate the entire stream and CRC before writing; reject bounds errors and overlap.
 */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/// 数值与EPD方向契约相同。/ Values match the EPD rotation contract.
typedef enum {
    UI_IMAGE_LANDSCAPE=0, UI_IMAGE_PORTRAIT=1,
    UI_IMAGE_INVERTED_LANDSCAPE=2, UI_IMAGE_INVERTED_PORTRAIT=3
} ui_image_rotation_t;

/// 校验尺寸、整个PackBits流及未压缩CRC32，不写内存。/ Check dimensions, full PackBits stream, and raw CRC32 without writing.
bool ui_image_validate(const uint8_t *packed,size_t packed_bytes,int width,int height);
/// 校验后解码，效果与旧ui_draw_full_image逐像素一致；失败时目标不变。
/// Decode after validation, matching the old ui_draw_full_image pixel-for-pixel; failure leaves the destination untouched.
bool ui_image_decode(uint8_t *framebuffer,size_t framebuffer_bytes,
                     const uint8_t *packed,size_t packed_bytes,
                     int width,int height,ui_image_rotation_t rotation);

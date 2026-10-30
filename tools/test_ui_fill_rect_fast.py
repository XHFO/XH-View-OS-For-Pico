#!/usr/bin/env python3
"""中文：对比实际逐像素填充，验证快速矩形的四向旋转、裁剪与半字节边界。
English: Compare the actual pixel oracle with fast fills, including all rotations and edge nibbles.

No device I/O. Set CFLAGS='-fsanitize=address,undefined -fno-omit-frame-pointer'
for sanitizer coverage.
"""
from pathlib import Path
import os
import re
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
UI = (ROOT / "main/ui/ui_kit.c").read_text()
EPD = (ROOT / "components/epdiy/src/epdiy.c").read_text()
HEADER = (ROOT / "components/epdiy/src/epdiy.h").read_text()


def function(source, name):
    match = re.search(
        r"(?:^|\n)((?:static\s+)?[\w\s*]+?\b" + re.escape(name)
        + r"\([^;]*?\)\s*\{)", source
    )
    if not match:
        raise RuntimeError(f"Cannot locate actual function: {name}")
    start, at, depth, quote = match.start(1), match.end() - 1, 0, None
    while at < len(source):
        char = source[at]
        if quote:
            if char == "\\":
                at += 2
                continue
            if char == quote:
                quote = None
        elif source.startswith("//", at):
            at = source.index("\n", at)
            continue
        elif source.startswith("/*", at):
            at = source.index("*/", at) + 2
            continue
        elif char in "\"'":
            quote = char
        elif char == "{":
            depth += 1
        elif char == "}":
            depth -= 1
            if depth == 0:
                return source[start:at + 1]
        at += 1
    raise RuntimeError(f"Unbalanced actual function: {name}")


prelude = r'''
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>

enum { WIDTH = 1216, HEIGHT = 684, FRAME_BYTES = WIDTH * HEIGHT / 2, GUARD_BYTES = 32 };
static enum EpdRotation display_rotation;
static int epd_width(void) { return WIDTH; }
static int epd_height(void) { return HEIGHT; }
#define _swap_int(a,b) do { int value = (a); (a) = (b); (b) = value; } while (0)
static struct { uint8_t before[GUARD_BYTES], fb[FRAME_BYTES], after[GUARD_BYTES]; } fast, oracle;
static uint8_t seed[FRAME_BYTES];
static unsigned checks, failures, cases;
#define CHECK(expr,label) do { checks++; if (!(expr)) { \
    if (failures < 10) fprintf(stderr,"FAIL %s (line %d, case %u, rot %d)\n", \
                              label,__LINE__,cases,(int)display_rotation); failures++; } } while (0)
'''

tests = r'''
static void guards(void) {
    for (unsigned i = 0; i < GUARD_BYTES; i++) {
        CHECK(fast.before[i] == 0xA5 && fast.after[i] == 0x5A, "fast native buffer guards intact");
        CHECK(oracle.before[i] == 0xA5 && oracle.after[i] == 0x5A, "oracle native buffer guards intact");
    }
}
static void prepare(void) {
    memset(fast.before,0xA5,sizeof(fast.before)); memset(fast.after,0x5A,sizeof(fast.after));
    memset(oracle.before,0xA5,sizeof(oracle.before)); memset(oracle.after,0x5A,sizeof(oracle.after));
    memcpy(fast.fb,seed,sizeof(seed)); memcpy(oracle.fb,seed,sizeof(seed));
}
static void compare(EpdRect r, uint8_t color, bool reset) {
    if (reset) prepare();
    cases++;
    epd_fill_rect(r,color,oracle.fb);
    ui_fill_rect_fast(fast.fb,r,color);
    CHECK(!memcmp(fast.fb,oracle.fb,FRAME_BYTES), "every native framebuffer byte matches actual pixel oracle");
    guards();
}
static uint32_t random_state = 0x4f6b9812;
static uint32_t random_u32(void) {
    random_state ^= random_state << 13;
    random_state ^= random_state >> 17;
    random_state ^= random_state << 5;
    return random_state;
}
static void parity_and_gray(void) {
    for (int gray = 0; gray < 16; gray++)
    for (int x = 0; x < 2; x++)
    for (int y = 0; y < 2; y++)
    for (int width = 1; width <= 4; width++)
    for (int height = 1; height <= 4; height++)
        compare((EpdRect){20+x,30+y,width,height},(uint8_t)(gray << 4),true);
    // 低半字节必须忽略，验证所有256种输入颜色。/ Ignore the low nibble for all 256 color bytes.
    for (int color = 0; color < 256; color++)
        compare((EpdRect){17+(color&1),23+((color>>1)&1),1+(color%4),1+((color/4)%4)},
                (uint8_t)color,true);
}
static void boundaries(void) {
    int w = epd_rotated_display_width(), h = epd_rotated_display_height();
    const int xs[] = {-3,-1,0,1,w-2,w-1,w,w+1};
    const int ys[] = {-3,-1,0,1,h-2,h-1,h,h+1};
    const int sizes[] = {1,2,4};
    for (unsigned x=0;x<sizeof(xs)/sizeof(xs[0]);x++)
    for (unsigned y=0;y<sizeof(ys)/sizeof(ys[0]);y++)
    for (unsigned wi=0;wi<sizeof(sizes)/sizeof(sizes[0]);wi++)
    for (unsigned hi=0;hi<sizeof(sizes)/sizeof(sizes[0]);hi++)
        compare((EpdRect){xs[x],ys[y],sizes[wi],sizes[hi]},
                (uint8_t)((x*31+y*17+wi*7+hi*3)&255),true);
    for (int gray=0;gray<16;gray++) {
        compare((EpdRect){0,0,w,h},(uint8_t)(gray<<4),true);
        compare((EpdRect){-2,-3,w+4,h+6},(uint8_t)((15-gray)<<4),true);
        compare((EpdRect){1,1,w-2,h-2},(uint8_t)(gray<<4),true);
    }
    const EpdRect empty[] = {
        {1,1,0,5},{1,1,5,0},{1,1,-1,5},{1,1,5,-1},{1,1,-2,-2},
        {-10,-10,2,2},{w+10,h+10,2,2},{-1,0,1,h},{0,-1,w,1},
    };
    for (unsigned i=0;i<sizeof(empty)/sizeof(empty[0]);i++) compare(empty[i],0,true);
}
static void random_and_overlaps(void) {
    int w = epd_rotated_display_width(), h = epd_rotated_display_height();
    for (int i=0;i<1000;i++) {
        EpdRect r = {
            .x = (int)(random_u32()%(w+80))-40,
            .y = (int)(random_u32()%(h+80))-40,
            .width = 1+(int)(random_u32()%127),
            .height = 1+(int)(random_u32()%127),
        };
        compare(r,(uint8_t)random_u32(),true);
        // 连续重叠绘制检查未写像素的原有半字节。/ Overlapping writes retain untouched prior nibbles.
        r.x++; r.y--; r.width = 1+(int)(random_u32()%127);
        compare(r,(uint8_t)random_u32(),false);
    }
}
static void overflow_safety(void) {
    // 极端输入不交给逐像素oracle，避免其有符号溢出与65536坐标回绕。
    // Do not pass extreme inputs to the pixel oracle, which overflows or wraps coordinates at 65536.
    const EpdRect empty[] = {
        {INT_MIN,INT_MIN,INT_MAX,INT_MAX},{INT_MAX-4,0,16,10},
        {0,INT_MAX-4,10,16},{INT_MIN,0,INT_MAX,5},{0,INT_MIN,5,INT_MAX},
        {0,0,INT_MIN,10},{0,0,10,INT_MIN},
    };
    prepare();
    for (unsigned i=0;i<sizeof(empty)/sizeof(empty[0]);i++) {
        ui_fill_rect_fast(fast.fb,empty[i],0);
        CHECK(!memcmp(fast.fb,seed,FRAME_BYTES), "extreme offscreen or invalid rectangle leaves framebuffer untouched");
        guards();
    }
    ui_fill_rect_fast(NULL,(EpdRect){0,0,2,2},0);
}
int main(void) {
    for (unsigned i=0;i<FRAME_BYTES;i++) seed[i]=(uint8_t)(i*61+(i>>7)*13+39);
    for (int rotation=0;rotation<4;rotation++) {
        epd_set_rotation((enum EpdRotation)rotation);
        CHECK((int)epd_get_rotation()==rotation,"actual display rotation selected");
        parity_and_gray(); boundaries(); random_and_overlaps(); overflow_safety();
    }
    printf("fast fill host (actual functions, pixel oracle): %u cases; %u checks; %u failures\n",cases,checks,failures);
    return failures ? 1 : 0;
}
'''

types = re.search(r"typedef struct\s*\{[^}]*\}\s*EpdRect;", HEADER).group()
types += "\n" + re.search(r"enum EpdRotation\s*\{[^}]*\};", HEADER).group()
types += "\n" + re.search(r"typedef struct\s*\{[^}]*\}\s*Coord_xy;", EPD).group()
actual = "\n\n".join(function(EPD, name) for name in (
    "_rotate", "epd_draw_pixel", "epd_draw_hline", "epd_fill_rect", "epd_set_rotation",
    "epd_get_rotation", "epd_rotated_display_width", "epd_rotated_display_height",
))
actual += "\n" + function(UI, "ui_rotate_rect_to_fb") + "\n" + function(UI, "ui_fill_rect_fast")
with tempfile.TemporaryDirectory(prefix="pico-fill-host-") as directory:
    directory = Path(directory)
    source, executable = directory / "fill.c", directory / "fill"
    source.write_text("#include <stdint.h>\n" + types + "\n" + prelude + "\n" + actual + "\n" + tests)
    subprocess.run(
        [os.environ.get("CC", "cc"), "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror"]
        + shlex.split(os.environ.get("CFLAGS", "")) + [str(source), "-o", str(executable)], check=True,
    )
    raise SystemExit(subprocess.run([str(executable)], env=os.environ.copy()).returncode)

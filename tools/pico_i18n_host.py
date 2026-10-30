"""中文：主机测试链接真实文案与解包器，仅替换 ROM/堆边界。
English: Link production translations and unpacking with host ROM/heap boundaries.
"""
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def sources_and_flags(directory):
    directory = Path(directory) / "i18n-host"
    directory.mkdir()
    (directory / "esp_heap_caps.h").write_text(
        "#define MALLOC_CAP_SPIRAM 1u\n#define MALLOC_CAP_8BIT 2u\n"
    )
    (directory / "esp_log.h").write_text(
        "#define ESP_LOGE(tag, ...) host_asset_log(tag, __VA_ARGS__)\n"
    )
    source = directory / "i18n-host.c"
    source.write_text(r'''
#include <assert.h>
#include <stdlib.h>
#include "miniz.h"
static void *host_asset_malloc(size_t bytes, unsigned caps) {
    assert(caps == 3u); return malloc(bytes);
}
static void host_asset_log(const char *tag, const char *format, ...) {
    assert(tag && format);
}
static unsigned long mz_crc32(unsigned long crc, const unsigned char *data, size_t bytes) {
    return crc32(crc, data, (uInt)bytes);
}
#define MZ_CRC32_INIT 0
#define heap_caps_malloc host_asset_malloc
#include "ui_packed_asset.c"
#undef heap_caps_malloc
#include "pico_i18n.c"
''')
    flags = ["-I", str(directory), "-I", str(ROOT / "tools/zip_host_stubs"),
             "-I", str(ROOT / "main/ui"), "-I", str(ROOT / "main/system")]
    return [str(source)], flags

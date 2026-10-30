/*
 * SPDX-FileCopyrightText: 2026 XH Lab
 * SPDX-License-Identifier: Apache-2.0
 * 按报告描述解析键盘及媒体按键，禁止猜测字节布局。/ Parse keyboard/media descriptors without guessing byte layouts.
 */
#include "pager_hid.h"
#include <string.h>

typedef struct {
    uint32_t page, size, count;
    uint8_t id;
} globals_t;

static uint8_t direction(uint32_t page, uint32_t usage)
{
    if (page == 7) {
        if (usage == 0x4b || usage == 0x50 || usage == 0x52) return 1;
        if (usage == 0x4e || usage == 0x4f || usage == 0x51 || usage == 0x2c) return 2;
    }
    if (page == 0x0c) {
        if (usage == 0xea || usage == 0xb6) return 1;
        if (usage == 0xe9 || usage == 0xb5) return 2;
    }
    return 0;
}

static int report_index(pager_hid_t *h, uint8_t id, bool create)
{
    for (unsigned i = 0; i < h->report_count; ++i) if (h->report_ids[i] == id) return (int)i;
    if (!create || h->report_count == PAGER_HID_MAX_REPORTS) return -1;
    int i = h->report_count++;
    h->report_ids[i] = id;
    return i;
}

bool pager_hid_parse(pager_hid_t *h, const uint8_t *map, size_t length)
{
    if (!h) return false;
    memset(h, 0, sizeof(*h));
    if (!map || !length || length > 1024) return false;
    globals_t g = {0}, stack[4];
    unsigned depth = 0, collections = 0, local_count = 0;
    uint32_t usages[32], umin = 0, umax = 0;
    bool range = false;
    for (size_t p = 0; p < length;) {
        uint8_t prefix = map[p++];
        if (prefix == 0xfe) goto fail;
        unsigned n = prefix & 3; if (n == 3) n = 4;
        if (length - p < n) goto fail;
        uint32_t v = 0;
        for (unsigned j = 0; j < n; ++j) v |= (uint32_t)map[p++] << (j * 8);
        unsigned type = (prefix >> 2) & 3, tag = prefix >> 4;
        if (type == 1) {
            if (tag == 0) { if (v > 0xffff) goto fail; g.page = v; }
            else if (tag == 7) g.size = v;
            else if (tag == 8) { if (!v || v > 255) goto fail; g.id = (uint8_t)v; }
            else if (tag == 9) g.count = v;
            else if (tag == 10) { if (depth == 4) goto fail; stack[depth++] = g; }
            else if (tag == 11) { if (!depth) goto fail; g = stack[--depth]; }
        } else if (type == 2) {
            if (tag == 0) { if (local_count == 32) goto fail; usages[local_count++] = v; }
            else if (tag == 1) { umin = v; range = true; }
            else if (tag == 2) umax = v;
        } else if (type == 0) {
            if (tag == 10) { if (++collections > 8) goto fail; }
            if (tag == 12) { if (!collections) goto fail; --collections; }
            if (tag == 8) {
                if (!g.size || g.size > 32 || !g.count || g.count > 256) goto fail;
                int r = report_index(h, g.id, true);
                if (r < 0 || (uint32_t)h->report_bits[r] + g.size * g.count > 4096) goto fail;
                uint16_t offset = h->report_bits[r];
                h->report_bits[r] += (uint16_t)(g.size * g.count);
                if (!(v & 1)) {
                    bool array = !(v & 2);
                    if (array) {
                        if ((g.page == 7 || g.page == 0x0c) && g.count <= 32) {
                            if (h->field_count == PAGER_HID_MAX_FIELDS) goto fail;
                            h->fields[h->field_count++] = (pager_hid_field_t){
                                .bit_offset = offset, .bit_size = (uint8_t)g.size,
                                .count = (uint8_t)g.count, .report_id = g.id,
                                .usage_page = (uint16_t)g.page, .array = true};
                        }
                    } else {
                        for (uint32_t j = 0; j < g.count; ++j) {
                            uint32_t u = j < local_count ? usages[j] : (range && umin + j <= umax ? umin + j : 0);
                            uint32_t page = u > 0xffff ? u >> 16 : g.page;
                            u &= 0xffff;
                            if (!direction(page, u)) continue;
                            if (h->field_count == PAGER_HID_MAX_FIELDS) goto fail;
                            h->fields[h->field_count++] = (pager_hid_field_t){
                                .bit_offset = (uint16_t)(offset + j * g.size),
                                .bit_size = (uint8_t)g.size, .count = 1,
                                .report_id = g.id, .usage_page = (uint16_t)page, .usage = (uint16_t)u};
                        }
                    }
                }
            }
            local_count = 0; umin = umax = 0; range = false;
        }
    }
    if (depth || collections || !h->field_count) goto fail;
    return true;
fail:
    memset(h, 0, sizeof(*h));
    return false;
}

static uint32_t bits(const uint8_t *d, size_t length, unsigned offset, unsigned count)
{
    if ((size_t)offset + count > length * 8) return 0;
    uint32_t v = 0;
    for (unsigned j = 0; j < count; ++j) v |= ((uint32_t)(d[(offset + j) / 8] >> ((offset + j) % 8)) & 1) << j;
    return v;
}

static int edge(uint8_t *held, uint8_t down)
{
    uint8_t fresh = down & ~*held;
    *held = down;
    if (down == 3) return 0;
    return fresh == 1 ? -1 : fresh == 2 ? 1 : 0;
}

int pager_hid_input(pager_hid_t *h, uint8_t id, const uint8_t *data, size_t length)
{
    if (!h || !data || !length || length > 512) return 0;
    int r = report_index(h, id, false);
    if (r < 0 || length * 8 < h->report_bits[r]) return 0;
    uint8_t down = 0;
    for (unsigned j = 0; j < h->field_count; ++j) {
        const pager_hid_field_t *f = &h->fields[j];
        if (f->report_id != id) continue;
        for (unsigned k = 0; k < f->count; ++k) {
            uint32_t value = bits(data, length, f->bit_offset + k * f->bit_size, f->bit_size);
            if (f->array && f->usage_page == 7 && value >= 1 && value <= 3) return 0;
            if (value) down |= direction(f->usage_page, f->array ? value : f->usage);
        }
    }
    return edge(&h->held[r], down);
}

int pager_hid_boot_input(uint8_t *held, const uint8_t *data, size_t length)
{
    if (!held || !data || length != 8) return 0;
    uint8_t down = 0;
    for (unsigned j = 2; j < 8; ++j) {
        if (data[j] >= 1 && data[j] <= 3) return 0;
        down |= direction(7, data[j]);
    }
    return edge(held, down);
}

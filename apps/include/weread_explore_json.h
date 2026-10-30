/*
 * SPDX-License-Identifier: Apache-2.0
 * 中文：微信读书应用的有界流式 JSON 游标；只在事件与tick中从宿主读取，不保存账户数据。
 * English: Bounded streaming JSON cursors for WeRead, read only during events/ticks without account persistence.
 * 冻结：最多32层、512KiB；字符串按Unicode标量解码，不将HTML作为绘制指令。
 * Frozen: At most 32 levels/512KiB; decode Unicode scalars and never interpret HTML as drawing commands.
 */
#pragma once

typedef struct {
    uint32_t start, end;
    int type;
} wrj_token_t;
static uint8_t wrj_cache[1024];
static uint32_t wrj_cache_at, wrj_cache_bytes, wrj_request_id, wrj_bytes;
static bool wrj_failed;
static void wrj_begin(uint32_t id, uint32_t bytes) {
    wrj_request_id = id;
    wrj_bytes = bytes;
    wrj_cache_at = UINT32_MAX;
    wrj_cache_bytes = 0;
    wrj_failed = bytes > 524287u;
}
static int wrj_char(uint32_t at) {
    if (wrj_failed || at >= wrj_bytes) return -1;
    if (wrj_cache_at == UINT32_MAX || at < wrj_cache_at || at - wrj_cache_at >= wrj_cache_bytes) {
        size_t got = 0;
        if (!host->weread_query_read(wrj_request_id, at, wrj_cache, sizeof(wrj_cache), &got) ||
            !got || got > sizeof(wrj_cache) || got > wrj_bytes - at) {
            wrj_failed = true;
            return -1;
        }
        wrj_cache_at = at;
        wrj_cache_bytes = (uint32_t)got;
    }
    return wrj_cache[at - wrj_cache_at];
}
static uint32_t wrj_space(uint32_t at) {
    int c;
    while ((c = wrj_char(at)) == ' ' || c == '\n' || c == '\r' || c == '\t') at++;
    return at;
}
static wrj_token_t wrj_token(uint32_t start, unsigned depth) {
    wrj_token_t t = { 0 };
    start = wrj_space(start);
    int first = wrj_char(start);
    if (first < 0 || depth > 32) {
        wrj_failed = true;
        return t;
    }
    t.start = start;
    t.type = first;
    if (first == '"') {
        uint32_t at = start + 1;
        bool escaped = false;
        for (; at < wrj_bytes; at++) {
            int c = wrj_char(at);
            if (c < 32) {
                wrj_failed = true;
                return (wrj_token_t){ 0 };
            }
            if (!escaped && c == '"') {
                t.end = at + 1;
                return t;
            }
            if (!escaped && c == '\\')
                escaped = true;
            else
                escaped = false;
        }
    } else if (first == '{' || first == '[') {
        uint32_t at = wrj_space(start + 1);
        int closing = first == '{' ? '}' : ']';
        if (wrj_char(at) == closing) {
            t.end = at + 1;
            return t;
        }
        for (unsigned members = 0; members < 524288u; members++) {
            if (first == '{') {
                wrj_token_t key = wrj_token(at, depth + 1);
                if (wrj_failed || key.type != '"') break;
                at = wrj_space(key.end);
                if (wrj_char(at++) != ':') break;
            }
            wrj_token_t value = wrj_token(at, depth + 1);
            if (wrj_failed || !value.end) break;
            at = wrj_space(value.end);
            int c = wrj_char(at++);
            if (c == closing) {
                t.end = at;
                return t;
            }
            if (c != ',') break;
            at = wrj_space(at);
        }
    } else {
        uint32_t at = start;
        int c;
        while ((c = wrj_char(at)) >= 0 && c != ',' && c != ']' && c != '}' && c != ' ' &&
               c != '\n' && c != '\r' && c != '\t')
            at++;
        if (at > start) {
            t.end = at;
            return t;
        }
    }
    wrj_failed = true;
    return (wrj_token_t){ 0 };
}
static unsigned wrj_hex(int c) {
    return c >= '0' && c <= '9'   ? (unsigned)(c - '0')
           : c >= 'a' && c <= 'f' ? (unsigned)(c - 'a' + 10)
           : c >= 'A' && c <= 'F' ? (unsigned)(c - 'A' + 10)
                                  : 16u;
}
static bool wrj_unit(uint32_t* at, uint32_t end, uint32_t* scalar_value) {
    if (*at >= end) return false;
    int c = wrj_char((*at)++);
    if (c < 0) {
        wrj_failed = true;
        return false;
    }
    uint32_t value = (uint32_t)c;
    if (c == '\\') {
        if (*at >= end) {
            wrj_failed = true;
            return false;
        }
        c = wrj_char((*at)++);
        if (c == 'u') {
            value = 0;
            for (unsigned i = 0; i < 4; i++) {
                unsigned h = wrj_hex(wrj_char((*at)++));
                if (*at > end || h > 15) {
                    wrj_failed = true;
                    return false;
                }
                value = (value << 4) | h;
            }
            if (value >= 0xd800 && value <= 0xdbff) {
                if (*at + 6 > end || wrj_char((*at)++) != '\\' || wrj_char((*at)++) != 'u') {
                    wrj_failed = true;
                    return false;
                }
                uint32_t low = 0;
                for (unsigned i = 0; i < 4; i++) {
                    unsigned h = wrj_hex(wrj_char((*at)++));
                    if (h > 15) {
                        wrj_failed = true;
                        return false;
                    }
                    low = (low << 4) | h;
                }
                if (low < 0xdc00 || low > 0xdfff) {
                    wrj_failed = true;
                    return false;
                }
                value = 0x10000 + ((value - 0xd800) << 10) + low - 0xdc00;
            } else if (value >= 0xdc00 && value <= 0xdfff) {
                wrj_failed = true;
                return false;
            }
        } else if (c == 'n')
            value = 10;
        else if (c == 'r')
            value = 13;
        else if (c == 't')
            value = 9;
        else if (c == 'b')
            value = 8;
        else if (c == 'f')
            value = 12;
        else if (c == '"' || c == '\\' || c == '/')
            value = (uint32_t)c;
        else {
            wrj_failed = true;
            return false;
        }
    } else if (c >= 128) {
        unsigned count = c >= 0xc2 && c <= 0xdf   ? 2
                         : c >= 0xe0 && c <= 0xef ? 3
                         : c >= 0xf0 && c <= 0xf4 ? 4
                                                  : 0;
        if (!count || *at + count - 1 > end) {
            wrj_failed = true;
            return false;
        }
        value = (uint32_t)c & ((1u << (7 - count)) - 1u);
        for (unsigned i = 1; i < count; i++) {
            int tail = wrj_char((*at)++);
            if ((tail & 0xc0) != 0x80) {
                wrj_failed = true;
                return false;
            }
            value = (value << 6) | (unsigned)(tail & 63);
        }
        if ((count == 2 && value < 0x80) || (count == 3 && value < 0x800) ||
            (count == 4 && value < 0x10000) || value > 0x10ffff ||
            (value >= 0xd800 && value <= 0xdfff)) {
            wrj_failed = true;
            return false;
        }
    } else if (c < 32) {
        wrj_failed = true;
        return false;
    }
    *scalar_value = value;
    return true;
}
static unsigned wrj_encode(uint32_t c, char* out) {
    if (c < 128) {
        out[0] = (char)c;
        return 1;
    }
    if (c < 2048) {
        out[0] = (char)(0xc0 | (c >> 6));
        out[1] = (char)(0x80 | (c & 63));
        return 2;
    }
    if (c < 65536) {
        out[0] = (char)(0xe0 | (c >> 12));
        out[1] = (char)(0x80 | ((c >> 6) & 63));
        out[2] = (char)(0x80 | (c & 63));
        return 3;
    }
    out[0] = (char)(0xf0 | (c >> 18));
    out[1] = (char)(0x80 | ((c >> 12) & 63));
    out[2] = (char)(0x80 | ((c >> 6) & 63));
    out[3] = (char)(0x80 | (c & 63));
    return 4;
}
static bool wrj_string(wrj_token_t t, char* out, unsigned capacity) {
    if (!capacity) return false;
    out[0] = 0;
    if (t.type != '"' || t.end <= t.start + 1) return false;
    unsigned used = 0;
    uint32_t at = t.start + 1;
    while (at + 1 < t.end) {
        uint32_t c;
        char encoded[4];
        if (!wrj_unit(&at, t.end - 1, &c)) return false;
        if (c < 32 || c == 127) c = ' ';
        unsigned n = wrj_encode(c, encoded);
        if (used + n >= capacity) {
            out[used] = 0;
            return false;
        }
        host->copy(out + used, encoded, n);
        used += n;
    }
    out[used] = 0;
    return !wrj_failed;
}
static bool wrj_equal(wrj_token_t t, const char* key) {
    char text[48];
    return wrj_string(t, text, sizeof(text)) && wrj_length(text) == wrj_length(key) &&
           !host->compare(text, key, wrj_length(key) + 1);
}
static wrj_token_t wrj_field(wrj_token_t object, const char* key) {
    if (object.type != '{') return (wrj_token_t){ 0 };
    uint32_t at = wrj_space(object.start + 1);
    while (at + 1 < object.end && !wrj_failed) {
        wrj_token_t name = wrj_token(at, 1);
        if (name.type != '"') break;
        at = wrj_space(name.end);
        if (wrj_char(at++) != ':') break;
        wrj_token_t value = wrj_token(at, 1);
        if (!value.end) break;
        if (wrj_equal(name, key)) return value;
        at = wrj_space(value.end);
        if (wrj_char(at++) != ',') break;
        at = wrj_space(at);
    }
    return (wrj_token_t){ 0 };
}
static int64_t wrj_number(wrj_token_t t) {
    if (!t.end) return 0;
    uint32_t at = t.start;
    bool negative = wrj_char(at) == '-';
    if (negative) at++;
    int64_t n = 0;
    for (; at < t.end; at++) {
        int c = wrj_char(at);
        if (c < '0' || c > '9') break;
        if (n > (INT64_MAX - (c - '0')) / 10) return INT64_MAX;
        n = n * 10 + c - '0';
    }
    return negative ? -n : n;
}
static bool wrj_yes(wrj_token_t t) {
    return t.type == 't' || wrj_number(t) != 0;
}
static wrj_token_t wrj_nested(wrj_token_t root, const char* a, const char* b) {
    return wrj_field(wrj_field(root, a), b);
}

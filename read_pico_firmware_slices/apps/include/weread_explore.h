/*
 * SPDX-License-Identifier: Apache-2.0
 * 中文：TF微信读书在线发现、图书信息、目录、统计和官方阅读跳转；继续保留独立离线笔记。
 * English: TF WeRead discovery, metadata, contents, statistics and official reading links alongside offline notes.
 * 冻结：网络与凭据只属于宿主；绘制只消费已准备页；不将目录或搜索称为完整正文。
 * Frozen: The host alone owns network/credentials; render prepared pages only, never label metadata/search as full text.
 */
#pragma once
static unsigned wrj_length(const char* s) {
    unsigned n = 0;
    while (s && s[n]) n++;
    return n;
}
#include "weread_explore_json.h"

typedef struct {
    char id[64], title[128], detail[160];
    wrj_token_t content, link;
    int64_t cursor, chapter;
} wre_row_t;
typedef struct {
    pico_app_weread_query_t request;
    unsigned offset;
    int mode;
    char title[128], author[96], link[1024], cover[512];
} wre_history_t;
static wre_row_t wre_rows[12];
static wre_history_t wre_history[8];
static unsigned wre_history_count, wre_count, wre_offset, wre_total, wre_text_count, wre_text_page;
static char wre_lines[32][256], wre_title[128], wre_author[96], wre_link[1024], wre_cover[512],
    wre_keyword[160];
static char wre_error[24];
static bool wre_available, wre_input_wait, wre_waiting, wre_deferred, wre_loaded, wre_has_more,
    wre_text_view;
static bool wre_text_more, wre_list_more;
static unsigned wre_scope = 10;
static int wre_view_w, wre_view_h;
static uint32_t wre_request_id, wre_text_offset, wre_text_next;
static int64_t wre_next_cursor, wre_next_synckey;
static wrj_token_t wre_root, wre_text;
static pico_app_weread_query_t wre_request;
static pico_app_weread_query_status_t wre_status;

static void wre_copy(char* out, unsigned capacity, const char* value) {
    host->format(out, capacity, "%s", value ? value : "");
}
static bool wre_is_book_view(void) {
    return wre_request.kind == PGA_WEREAD_INFO || wre_request.kind == PGA_WEREAD_CHAPTERS ||
           wre_request.kind == PGA_WEREAD_PROGRESS || wre_request.kind == PGA_WEREAD_HIGHLIGHTS ||
           wre_request.kind == PGA_WEREAD_REVIEWS || wre_request.kind == PGA_WEREAD_SIMILAR;
}
static int wre_top(void) {
    if (wre_request.kind == PGA_WEREAD_INFO) return host->view().landscape ? 192 : 326;
    return wre_is_book_view()                      ? 192
           : wre_request.kind == PGA_WEREAD_SEARCH ? 172
           : wre_request.kind == PGA_WEREAD_STATS  ? 136
                                                   : 116;
}
static unsigned wre_per_page(void) {
    int n = (host->view().h - wre_top() - 70) / 62;
    return n > 0 ? (unsigned)n : 1;
}
static int wre_body_x(void) {
    return wre_request.kind == PGA_WEREAD_INFO && host->view().landscape ? 100 : 20;
}
static unsigned wre_body_rows(void) {
    int n = (host->view().h - wre_top() - 70) / 27;
    return n > 18 ? 18 : n > 0 ? (unsigned)n : 1;
}
static const char* wre_kind_title(unsigned kind) {
    switch (kind) {
        case PGA_WEREAD_SEARCH: return "在线搜书";
        case PGA_WEREAD_INFO: return "图书详情";
        case PGA_WEREAD_CHAPTERS: return "章节目录";
        case PGA_WEREAD_PROGRESS: return "阅读进度";
        case PGA_WEREAD_RECOMMEND: return "为你推荐";
        case PGA_WEREAD_SIMILAR: return "相似书籍";
        case PGA_WEREAD_STATS: return "阅读统计";
        case PGA_WEREAD_HIGHLIGHTS: return "热门划线";
        case PGA_WEREAD_REVIEWS: return "读者想法";
        default: return "在线发现";
    }
}
static const char* wre_error_text(void) {
    if (!host->compare(wre_error, "offline", 8)) return "设备尚未联网，请连接 Wi-Fi 后重试。";
    if (!host->compare(wre_error, "auth", 5)) return "请先在连接与同步中保存有效的 API Key。";
    if (!host->compare(wre_error, "time", 5)) return "请先校准设备日期与时间，再重新查询。";
    if (!host->compare(wre_error, "busy", 5)) return "设备正在同步，请稍后重试。";
    if (!host->compare(wre_error, "stale", 6)) return "查询已被另一处操作更新，请重新读取。";
    if (!host->compare(wre_error, "too_large", 10)) return "返回内容超出设备容量，请在手机上查看。";
    if (!host->compare(wre_error, "malformed", 10)) return "返回内容无法读取，请重试。";
    return "查询未完成，请检查网络后重试。";
}
static void wre_field_text(wrj_token_t object, const char* key, char* out, unsigned capacity) {
    (void)wrj_string(wrj_field(object, key), out, capacity);
}
static int64_t wre_num(wrj_token_t object, const char* key) {
    return wrj_number(wrj_field(object, key));
}
static void wre_reset_page(void) {
    wre_count = wre_total = wre_text_count = 0;
    wre_text = (wrj_token_t){ 0 };
    wre_text_view = wre_text_more = wre_list_more = false;
    wre_text_offset = wre_text_next = wre_text_page = 0;
    wre_has_more = false;
    wre_next_cursor = wre_next_synckey = 0;
}
static void wre_init(void) {
    wre_available = host->bytes >= offsetof(pico_app_host_t, weread_query_read) +
                                       sizeof(host->weread_query_read) &&
                    host->weread_query && host->weread_query_read;
    wre_input_wait = wre_waiting = wre_deferred = wre_loaded = false;
    wre_history_count = 0;
    wre_error[0] = wre_keyword[0] = wre_title[0] = wre_author[0] = wre_link[0] = wre_cover[0] = 0;
    wre_scope = 10;
    wre_view_w = host->view().w;
    wre_view_h = host->view().h;
    wre_reset_page();
}
static void wre_dispatch(void) {
    wre_status.bytes = sizeof(wre_status);
    bool accepted = host->weread_query(&wre_request, &wre_status);
    wre_waiting = accepted;
    wre_request_id = accepted ? wre_status.request_id : 0;
    if (!accepted && !host->compare(wre_status.error, "busy", 5))
        wre_deferred = wre_waiting = true;
    else if (!accepted)
        wre_copy(wre_error, sizeof(wre_error), wre_status.error[0] ? wre_status.error : "busy");
}
static bool wre_submit(const pico_app_weread_query_t* request, bool push) {
    if (!wre_available) {
        host->toast("请升级固件后使用在线发现");
        return true;
    }
    if (!save()) {
        host->toast("请先保存离线阅读位置，再打开在线内容");
        return true;
    }
    if (wre_waiting && mode == ONLINE) {
        host->toast("正在读取，可返回后继续离线阅读");
        return true;
    }
    if (push && wre_history_count < 8) {
        wre_history_t* entry = &wre_history[wre_history_count++];
        entry->mode = (int)mode;
        host->copy(&entry->request, &wre_request, sizeof(wre_request));
        entry->offset = wre_offset;
        wre_copy(entry->title, sizeof(entry->title), wre_title);
        wre_copy(entry->author, sizeof(entry->author), wre_author);
        wre_copy(entry->link, sizeof(entry->link), wre_link);
        wre_copy(entry->cover, sizeof(entry->cover), wre_cover);
    }
    host->copy(&wre_request, request, sizeof(wre_request));
    wre_request.bytes = sizeof(wre_request);
    wre_status.bytes = sizeof(wre_status);
    mode = ONLINE;
    wre_offset = 0;
    wre_reset_page();
    wre_loaded = false;
    wre_error[0] = 0;
    pico_app_weread_query_status_t current = { .bytes = sizeof(current) };
    wre_deferred = host->weread_query(NULL, &current) && current.busy;
    if (wre_deferred)
        wre_waiting = true;
    else
        wre_dispatch();
    service_due = host->time_us() + 300000;
    return true;
}
static void wre_new(pico_app_weread_query_t* q, unsigned kind) {
    host->set(q, 0, sizeof(*q));
    q->bytes = sizeof(*q);
    q->kind = kind;
    q->count = 12;
    q->scope = wre_scope;
    q->mode = PGA_WEREAD_MONTHLY;
}
static bool wre_begin(unsigned kind, bool push) {
    pico_app_weread_query_t q;
    wre_new(&q, kind);
    if (kind == PGA_WEREAD_SEARCH) wre_copy(q.keyword, sizeof(q.keyword), wre_keyword);
    return wre_submit(&q, push);
}
static bool wre_open_info(const char* id) {
    pico_app_weread_query_t q;
    wre_new(&q, PGA_WEREAD_INFO);
    wre_copy(q.book_id, sizeof(q.book_id), id);
    bool changed = wre_submit(&q, true);
    wre_title[0] = wre_author[0] = wre_link[0] = wre_cover[0] = 0;
    return changed;
}
static bool wre_open_search(void) {
    if (!wre_available) {
        host->toast("请升级固件后使用在线搜书");
        return true;
    }
    if (host->bytes < offsetof(pico_app_host_t, weread_input) + sizeof(host->weread_input) ||
        !host->weread_input) {
        host->toast("请在手机管理页输入关键词搜索");
        return true;
    }
    char value[160];
    wre_copy(value, sizeof(value), wre_keyword);
    if (host->weread_input(PGA_WEREAD_INPUT_OPEN, value, sizeof(value)))
        wre_input_wait = true;
    else
        host->toast("暂时无法打开搜索输入");
    return true;
}
static bool wre_back(void) {
    if (mode == DISCOVER) {
        mode = SHELF;
        return true;
    }
    if (mode != ONLINE) return false;
    if (wre_text_view) {
        wre_text_view = false;
        wre_text_offset = wre_text_next = wre_text_page = 0;
        wre_text = (wrj_token_t){ 0 };
        wre_loaded = false;
        return true;
    }
    wre_waiting = wre_deferred = false;
    if (wre_history_count) {
        wre_history_t* entry = &wre_history[--wre_history_count];
        wre_copy(wre_title, sizeof(wre_title), entry->title);
        wre_copy(wre_author, sizeof(wre_author), entry->author);
        wre_copy(wre_link, sizeof(wre_link), entry->link);
        wre_copy(wre_cover, sizeof(wre_cover), entry->cover);
        if (entry->mode == ONLINE) {
            bool changed = wre_submit(&entry->request, false);
            wre_offset = entry->offset;
            return changed;
        }
        mode = (unsigned)entry->mode;
        return true;
    }
    mode = DISCOVER;
    return true;
}
static void wre_text_page_build(uint32_t offset) {
    wre_text_count = 0;
    wre_text_offset = offset;
    uint32_t at = offset ? offset : wre_text.start + 1, end = wre_text.end ? wre_text.end - 1 : 0;
    unsigned limit = (unsigned)(host->view().w - wre_body_x() - 20);
    for (unsigned row = 0; row < wre_body_rows() && at < end && !wrj_failed; row++) {
        unsigned used = 0, width = 0;
        char* out = wre_lines[row];
        while (at < end) {
            uint32_t before = at, c;
            char encoded[4];
            if (!wrj_unit(&at, end, &c)) break;
            if (c == 13) continue;
            if (c == 10) break;
            if (c < 32 || c == 127) c = ' ';
            unsigned n = wrj_encode(c, encoded), pixels = advance(c);
            if (used && (width + pixels > limit || used + n >= sizeof(wre_lines[0]))) {
                at = before;
                break;
            }
            if (used + n >= sizeof(wre_lines[0])) break;
            host->copy(out + used, encoded, n);
            used += n;
            width += pixels;
        }
        out[used] = 0;
        wre_text_count++;
    }
    wre_text_next = at;
    wre_text_more = at < end;
    if (wrj_failed) wre_copy(wre_error, sizeof(wre_error), "stale");
}
static void wre_row(wrj_token_t info, wrj_token_t source, unsigned ordinal) {
    if (ordinal < wre_offset) {
        wre_total++;
        return;
    }
    if (wre_count >= wre_per_page() || wre_count >= 12) {
        wre_total++;
        wre_list_more = true;
        return;
    }
    wre_row_t* row = &wre_rows[wre_count++];
    host->set(row, 0, sizeof(*row));
    wre_field_text(info, "bookId", row->id, sizeof(row->id));
    wre_field_text(info, "title", row->title, sizeof(row->title));
    if (!row->title[0]) wre_field_text(info, "name", row->title, sizeof(row->title));
    row->link = wrj_field(info, "readerLink");
    if (!row->link.end) row->link = wrj_field(info, "deepLink");
    char author[96];
    wre_field_text(info, "author", author, sizeof(author));
    int64_t rating = wre_num(info, "newRating"), readers = wre_num(source, "readingCount");
    if (!rating) rating = wre_num(source, "newRating");
    if (rating > 0)
        host->format(
            row->detail, sizeof(row->detail), "%s · 得分 %lld%s", author, (long long)rating,
            wre_num(info, "soldout") ? " · 已下架" : "");
    else
        host->format(
            row->detail, sizeof(row->detail), "%s%s", author,
            wre_num(info, "soldout") ? " · 已下架" : "");
    (void)readers;
    row->cursor = wre_num(source, "searchIdx");
    if (!row->cursor) row->cursor = wre_num(source, "idx");
    wre_total++;
}
static wrj_token_t wre_array_item(wrj_token_t array, uint32_t* at) {
    if (array.type != '[') return (wrj_token_t){ 0 };
    if (!*at) *at = wrj_space(array.start + 1);
    if (*at + 1 >= array.end || wrj_char(*at) == ']') return (wrj_token_t){ 0 };
    wrj_token_t value = wrj_token(*at, 1);
    *at = wrj_space(value.end);
    if (wrj_char(*at) == ',') *at = wrj_space(*at + 1);
    return value;
}
static void wre_parse_list(void) {
    wrj_token_t array = { 0 };
    uint32_t at = 0;
    if (wre_request.kind == PGA_WEREAD_SEARCH) {
        wrj_token_t groups = wrj_field(wre_root, "results"), group;
        unsigned ordinal = 0;
        while ((group = wre_array_item(groups, &at)).end && !wrj_failed) {
            uint32_t item_at = 0;
            wrj_token_t items = wrj_field(group, "books"), item;
            while ((item = wre_array_item(items, &item_at)).end && !wrj_failed) {
                wrj_token_t info = wrj_field(item, "bookInfo");
                if (info.type != '{') info = item;
                wre_row(info, item, ordinal++);
                wre_next_cursor = wre_num(item, "searchIdx");
            }
        }
        wre_has_more = wrj_yes(wrj_field(wre_root, "hasMore"));
        return;
    }
    if (wre_request.kind == PGA_WEREAD_SIMILAR) {
        wrj_token_t similar = wrj_field(wre_root, "booksimilar");
        array = wrj_field(similar, "books");
        wre_field_text(
            similar, "sessionId", wre_request.session_id, sizeof(wre_request.session_id));
    } else
        array = wrj_field(wre_root, "books");
    wrj_token_t item;
    unsigned ordinal = 0;
    while ((item = wre_array_item(array, &at)).end && !wrj_failed) {
        wrj_token_t info = item;
        if (wre_request.kind == PGA_WEREAD_SIMILAR) info = wrj_nested(item, "book", "bookInfo");
        wre_row(info, item, ordinal++);
        wre_next_cursor =
            wre_num(item, wre_request.kind == PGA_WEREAD_SIMILAR ? "idx" : "searchIdx");
    }
    wre_has_more = wrj_yes(wrj_field(wre_root, "hasMore")) || wre_total >= wre_request.count;
}
static void wre_parse_rows(void) {
    wrj_token_t array = wrj_field(
        wre_root, wre_request.kind == PGA_WEREAD_CHAPTERS     ? "chapters"
                  : wre_request.kind == PGA_WEREAD_HIGHLIGHTS ? "items"
                                                              : "reviews");
    uint32_t at = 0;
    wrj_token_t item;
    while ((item = wre_array_item(array, &at)).end && !wrj_failed) {
        unsigned ordinal = wre_total++;
        if (wre_request.kind == PGA_WEREAD_REVIEWS) {
            wre_next_cursor = wre_num(item, "idx");
        }
        if (ordinal < wre_offset) continue;
        if (wre_count >= wre_per_page() || wre_count >= 12) {
            wre_list_more = true;
            continue;
        }
        wre_row_t* row = &wre_rows[wre_count++];
        host->set(row, 0, sizeof(*row));
        if (wre_request.kind == PGA_WEREAD_CHAPTERS) {
            wre_field_text(item, "title", row->title, sizeof(row->title));
            row->chapter = wre_num(item, "chapterUid");
            int64_t price = wre_num(item, "price");
            host->format(
                row->detail, sizeof(row->detail), "%lld 字 · %s",
                (long long)wre_num(item, "wordCount"),
                !price                       ? "开放章节"
                : wre_num(item, "paid") == 1 ? "已解锁"
                                             : "需解锁");
        } else if (wre_request.kind == PGA_WEREAD_HIGHLIGHTS) {
            row->content = wrj_field(item, "markText");
            (void)wrj_string(row->content, row->title, sizeof(row->title));
            host->format(
                row->detail, sizeof(row->detail), "%lld 人划线 · 点按阅读全文",
                (long long)wre_num(item, "totalCount"));
        } else {
            wrj_token_t review = wrj_field(item, "review"), inner = wrj_field(review, "review");
            if (inner.type == '{') review = inner;
            row->content = wrj_field(review, "content");
            (void)wrj_string(row->content, row->title, sizeof(row->title));
            char author[96];
            wre_field_text(wrj_field(review, "author"), "name", author, sizeof(author));
            host->format(
                row->detail, sizeof(row->detail), "%s · 点按查看完整想法",
                author[0] ? author : "读者");
        }
    }
    if (wre_request.kind == PGA_WEREAD_REVIEWS) {
        wre_has_more = wrj_yes(wrj_field(wre_root, "reviewsHasMore"));
        wre_next_synckey = wre_num(wre_root, "synckey");
    }
}
static void wre_summary_line(const char* text) {
    if (wre_text_count < 32) {
        wre_copy(wre_lines[wre_text_count], sizeof(wre_lines[0]), text);
        wre_text_count++;
    }
}
static void wre_parse_stats(void) {
    char label[200];
    int64_t seconds = wre_num(wre_root, "totalReadTime"),
            daily = wre_num(wre_root, "dayAverageReadTime");
    host->format(
        label, sizeof(label), "阅读时长  %lld 小时 %lld 分钟", (long long)(seconds / 3600),
        (long long)(seconds / 60 % 60));
    wre_summary_line(label);
    host->format(
        label, sizeof(label), "有效阅读  %lld 天", (long long)wre_num(wre_root, "readDays"));
    wre_summary_line(label);
    host->format(label, sizeof(label), "日均阅读  %lld 分钟", (long long)(daily / 60));
    wre_summary_line(label);
    char preference[160];
    wre_field_text(wre_root, "preferCategoryWord", preference, sizeof(preference));
    if (preference[0]) wre_summary_line(preference);
    wre_field_text(wre_root, "preferTimeWord", preference, sizeof(preference));
    if (preference[0]) wre_summary_line(preference);
    wrj_token_t stats = wrj_field(wre_root, "readStat"), item;
    uint32_t at = 0;
    while ((item = wre_array_item(stats, &at)).end && !wrj_failed) {
        char name[48], count[80];
        wre_field_text(item, "stat", name, sizeof(name));
        wre_field_text(item, "counts", count, sizeof(count));
        host->format(label, sizeof(label), "%s  %s", name, count);
        wre_summary_line(label);
    }
    stats = wrj_field(wre_root, "readLongest");
    at = 0;
    unsigned rank = 0;
    while ((item = wre_array_item(stats, &at)).end && !wrj_failed && rank < 10) {
        wrj_token_t book_info = wrj_field(item, "book");
        char title[128];
        wre_field_text(book_info, "title", title, sizeof(title));
        if (!title[0]) wre_field_text(wrj_field(item, "albumInfo"), "name", title, sizeof(title));
        if (title[0]) {
            host->format(label, sizeof(label), "%u  %s", ++rank, title);
            wre_summary_line(label);
            host->format(
                label, sizeof(label), "   %lld 分钟", (long long)(wre_num(item, "readTime") / 60));
            wre_summary_line(label);
        }
    }
}
static void wre_parse(void) {
    wre_reset_page();
    wrj_begin(wre_status.request_id, wre_status.total_bytes);
    wre_root = wrj_token(0, 0);
    if (!wre_root.end || wrj_space(wre_root.end) != wrj_bytes || wre_root.type != '{')
        wrj_failed = true;
    if (!wrj_failed) {
        if (wre_request.kind == PGA_WEREAD_SEARCH || wre_request.kind == PGA_WEREAD_RECOMMEND ||
            wre_request.kind == PGA_WEREAD_SIMILAR)
            wre_parse_list();
        else if (
            wre_request.kind == PGA_WEREAD_CHAPTERS || wre_request.kind == PGA_WEREAD_HIGHLIGHTS ||
            wre_request.kind == PGA_WEREAD_REVIEWS)
            wre_parse_rows();
        else if (wre_request.kind == PGA_WEREAD_INFO) {
            wre_field_text(wre_root, "title", wre_title, sizeof(wre_title));
            wre_field_text(wre_root, "author", wre_author, sizeof(wre_author));
            if (!wrj_string(wrj_field(wre_root, "readerLink"), wre_link, sizeof(wre_link)))
                wre_link[0] = 0;
            if (!wre_link[0] &&
                !wrj_string(wrj_field(wre_root, "deepLink"), wre_link, sizeof(wre_link)))
                wre_link[0] = 0;
            if (!wrj_string(wrj_field(wre_root, "cover"), wre_cover, sizeof(wre_cover)))
                wre_cover[0] = 0;
            wre_text = wrj_field(wre_root, "intro");
            if (wre_text.type == '"') wre_text_page_build(0);
        } else if (wre_request.kind == PGA_WEREAD_PROGRESS) {
            wrj_token_t progress = wrj_field(wre_root, "book");
            char label[200];
            int64_t percent = wre_num(progress, "progress"),
                    seconds = wre_num(progress, "recordReadingTime");
            host->format(label, sizeof(label), "阅读进度  %lld%%", (long long)percent);
            wre_summary_line(label);
            host->format(
                label, sizeof(label), "累计阅读  %lld 小时 %lld 分钟", (long long)(seconds / 3600),
                (long long)(seconds / 60 % 60));
            wre_summary_line(label);
            wre_summary_line(percent == 100 ? "已读完" : percent > 0 ? "正在阅读" : "尚未开始阅读");
        } else if (wre_request.kind == PGA_WEREAD_STATS)
            wre_parse_stats();
    }
    if (wrj_failed) wre_copy(wre_error, sizeof(wre_error), "malformed");
    wre_loaded = true;
}
static bool wre_tick(void) {
    if (wre_input_wait &&
        host->bytes >= offsetof(pico_app_host_t, weread_input) + sizeof(host->weread_input) &&
        host->weread_input) {
        char value[160] = { 0 };
        if (host->weread_input(PGA_WEREAD_INPUT_TAKE, value, sizeof(value))) {
            wre_input_wait = false;
            wre_copy(wre_keyword, sizeof(wre_keyword), value);
            if (value[0]) return wre_begin(PGA_WEREAD_SEARCH, true);
        }
    }
    if (mode != ONLINE || !wre_available) return false;
    pico_app_view_t view_now = host->view();
    if (wre_view_w != view_now.w || wre_view_h != view_now.h) {
        wre_view_w = view_now.w;
        wre_view_h = view_now.h;
        if (wre_loaded && !wre_error[0]) {
            if (wre_text.type == '"')
                wre_text_page_build(wre_text_offset);
            else
                wre_parse();
            return true;
        }
    }
    if (!wre_waiting) {
        if (!wre_loaded && !wre_error[0]) {
            wre_parse();
            return true;
        }
        if (wre_loaded && wre_request.kind == PGA_WEREAD_INFO && !wre_error[0]) {
            pico_app_weread_query_status_t latest = { .bytes = sizeof(latest) };
            if (host->weread_query(NULL, &latest) && latest.request_id == wre_request_id &&
                latest.ready && latest.revision != wre_status.revision) {
                host->copy(&wre_status, &latest, sizeof(latest));
                return true;
            }
        }
        return false;
    }
    pico_app_weread_query_status_t latest = { .bytes = sizeof(latest) };
    if (!host->weread_query(NULL, &latest)) return false;
    if (wre_deferred) {
        if (latest.busy) return false;
        wre_deferred = false;
        wre_dispatch();
        return true;
    }
    if (latest.request_id != wre_request_id) {
        wre_waiting = false;
        wre_copy(wre_error, sizeof(wre_error), "stale");
        return true;
    }
    if (latest.busy && !latest.ready) return false;
    host->copy(&wre_status, &latest, sizeof(latest));
    wre_waiting = false;
    if (latest.error[0] || !latest.ready) {
        wre_copy(wre_error, sizeof(wre_error), latest.error[0] ? latest.error : "malformed");
        return true;
    }
    wre_parse();
    return true;
}
static bool wre_turn(bool forward) {
    if (mode != ONLINE || wre_waiting || wre_error[0]) return false;
    pico_app_weread_query_status_t latest = { .bytes = sizeof(latest) };
    if (!host->weread_query(NULL, &latest) || latest.request_id != wre_request_id ||
        !latest.ready || latest.error[0]) {
        wre_copy(wre_error, sizeof(wre_error), "stale");
        return true;
    }
    if (wre_text.type == '"') {
        if (forward) {
            if (!wre_text_more) return false;
            wre_text_page++;
            wre_text_page_build(wre_text_next);
            return true;
        }
        if (!wre_text_page) return false;
        uint32_t target = wre_text_offset, at = 0, previous = 0;
        while (!wrj_failed) {
            wre_text_page_build(at);
            if (wre_text_next >= target || !wre_text_more) break;
            previous = wre_text_next;
            at = previous;
        }
        wre_text_page--;
        wre_text_page_build(previous);
        return true;
    }
    unsigned per = (wre_request.kind == PGA_WEREAD_STATS || wre_request.kind == PGA_WEREAD_PROGRESS)
                       ? wre_body_rows()
                       : wre_per_page();
    if (wre_request.kind == PGA_WEREAD_STATS || wre_request.kind == PGA_WEREAD_PROGRESS) {
        if (forward && wre_offset + per < wre_text_count) {
            wre_offset += per;
            return true;
        }
        if (!forward && wre_offset) {
            wre_offset = wre_offset > per ? wre_offset - per : 0;
            return true;
        }
        return false;
    }
    if (forward && wre_list_more) {
        wre_offset += per;
        wre_parse();
        return true;
    }
    if (!forward && wre_offset) {
        wre_offset = wre_offset > per ? wre_offset - per : 0;
        wre_parse();
        return true;
    }
    if (forward && wre_has_more && wre_next_cursor > wre_request.cursor) {
        pico_app_weread_query_t q;
        host->copy(&q, &wre_request, sizeof(q));
        q.cursor = wre_next_cursor;
        q.synckey = wre_next_synckey;
        return wre_submit(&q, true);
    }
    return false;
}
static void wre_render_scope(uint8_t* fb, int y) {
    static const char* names[] = { "图书", "作者", "全文", "网文", "全部" };
    static const unsigned values[] = { 10, 6, 12, 16, 0 };
    int width = (host->view().w - 48) / 5;
    for (unsigned i = 0; i < 5; i++)
        host->button(fb, 16 + (width + 4) * (int)i, y, width, 34, names[i], wre_scope == values[i]);
}
static void wre_render_discover(uint8_t* fb) {
    pico_app_view_t v = host->view();
    header(fb, "在线发现");
    host->text(fb, 20, 80, 14, "搜索书籍，回顾你的阅读", 3);
    host->box(fb, 20, 116, v.w - 40, 44, 6, 7, false);
    clipped(
        fb, 32, 129, 16, (const uint8_t*)(wre_keyword[0] ? wre_keyword : "搜索书名、作者或关键词"),
        160, v.w - 64, 0);
    wre_render_scope(fb, 172);
    int width = (v.w - 52) / 2;
    host->button(fb, 20, 224, width, 56, "为你推荐", false);
    host->button(fb, 32 + width, 224, width, 56, "阅读统计", false);
    if (!v.landscape)
        host->wrap(
            fb, 20, 310, v.w - 40, 16, 4,
            "查看图书资料、目录、热门划线与想法。\n完整正文请扫码进入微信读书官方阅读器。", 3);
    if (!wre_available)
        host->text(fb, 20, v.h - 50, 12, "当前固件支持离线笔记，在线发现需要更新固件。", 4);
}
static void wre_render_online(uint8_t* fb) {
    pico_app_view_t v = host->view();
    const char* title = wre_text_view ? "查看全文" : wre_kind_title(wre_request.kind);
    header(fb, title);
    if (wre_is_book_view() && wre_link[0]) host->button(fb, v.w - 86, 20, 70, 32, "手机读", false);
    if (wre_is_book_view()) {
        static const char* tabs[] = { "简介", "目录", "进度", "划线", "想法", "相似" };
        static const unsigned kinds[] = { PGA_WEREAD_INFO,     PGA_WEREAD_CHAPTERS,
                                          PGA_WEREAD_PROGRESS, PGA_WEREAD_HIGHLIGHTS,
                                          PGA_WEREAD_REVIEWS,  PGA_WEREAD_SIMILAR };
        int width = (v.w - 48) / 3;
        for (unsigned i = 0; i < 6; i++)
            host->button(
                fb, 16 + (width + 8) * (int)(i % 3), 78 + 42 * (int)(i / 3), width, 34, tabs[i],
                wre_request.kind == kinds[i]);
        clipped(
            fb, 20, 165, 12, (const uint8_t*)(wre_title[0] ? wre_title : "图书资料"),
            sizeof(wre_title), v.w - 40, 3);
    } else if (wre_request.kind == PGA_WEREAD_SEARCH) {
        host->box(fb, 20, 78, v.w - 40, 42, 6, 7, false);
        clipped(
            fb, 32, 90, 16, (const uint8_t*)(wre_keyword[0] ? wre_keyword : "输入关键词"), 160,
            v.w - 64, 0);
        wre_render_scope(fb, 128);
    } else if (wre_request.kind == PGA_WEREAD_STATS) {
        static const char* names[] = { "本周", "本月", "本年", "总计" };
        int width = (v.w - 44) / 4;
        for (unsigned i = 0; i < 4; i++)
            host->button(
                fb, 16 + (width + 4) * (int)i, 78, width, 34, names[i], wre_request.mode == i);
    }
    int top = wre_top();
    if (wre_request.kind == PGA_WEREAD_INFO && (wre_waiting || wre_error[0])) top = 192;
    if (wre_waiting) {
        host->text(fb, 20, top + 6, 18, "正在读取…", 0);
        host->text(fb, 20, top + 40, 12, "可以返回，继续查看已有离线笔记。", 4);
        return;
    }
    if (wre_error[0]) {
        host->wrap(fb, 20, top + 6, v.w - 40, 16, 2, wre_error_text(), 2);
        host->button(fb, 20, v.h - 112, 100, 38, "重新读取", false);
        return;
    }
    if (wre_request.kind == PGA_WEREAD_INFO) {
        int cover_w = v.landscape ? 64 : 80, cover_h = v.landscape ? 90 : 112;
        bool drawn = false;
        if (host->bytes >= offsetof(pico_app_host_t, weread_cover) + sizeof(host->weread_cover) &&
            host->weread_cover)
            drawn = host->weread_cover(wre_request_id, fb, 20, 192, cover_w, cover_h);
        if (!drawn) {
            host->box(fb, 20, 192, cover_w, cover_h, 3, 12, false);
            host->line(fb, 28, 198, 28, 192 + cover_h - 6, 11);
            host->text(fb, 32, 222, 12, "封面", 5);
        }
        if (!v.landscape) {
            clipped(fb, 116, 198, 16, (const uint8_t*)wre_author, sizeof(wre_author), v.w - 136, 0);
            host->text(fb, 116, 230, 12, "微信读书官方资料", 4);
            host->text(fb, 20, 307, 12, "内容简介", 3);
        } else
            clipped(fb, 20, 289, 12, (const uint8_t*)wre_author, sizeof(wre_author), cover_w, 0);
    }
    if (wre_text_view || wre_request.kind == PGA_WEREAD_INFO ||
        wre_request.kind == PGA_WEREAD_PROGRESS || wre_request.kind == PGA_WEREAD_STATS) {
        unsigned count = wre_text_count, maximum = wre_body_rows();
        unsigned start =
            (wre_request.kind == PGA_WEREAD_STATS || wre_request.kind == PGA_WEREAD_PROGRESS)
                ? wre_offset
                : 0;
        for (unsigned i = 0; start + i < count && i < maximum; i++) {
            if (wre_request.kind == PGA_WEREAD_STATS || wre_request.kind == PGA_WEREAD_PROGRESS)
                clipped(
                    fb, wre_body_x(), top + (int)i * 27, 18, (const uint8_t*)wre_lines[start + i],
                    sizeof(wre_lines[0]), v.w - wre_body_x() - 20, 0);
            else
                host->text(fb, wre_body_x(), top + (int)i * 27, 18, wre_lines[start + i], 0);
        }
        if (!count)
            host->text(
                fb, 20, top + 6, 16,
                wre_request.kind == PGA_WEREAD_INFO ? "暂无书籍简介" : "暂无对应记录", 4);
    } else {
        for (unsigned i = 0; i < wre_count; i++) {
            int y = top + (int)i * 62;
            host->line(fb, 20, y + 56, v.w - 20, y + 56, 11);
            clipped(
                fb, 20, y + 4, 18, (const uint8_t*)wre_rows[i].title, sizeof(wre_rows[i].title),
                v.w - 40, 0);
            clipped(
                fb, 20, y + 32, 12, (const uint8_t*)wre_rows[i].detail, sizeof(wre_rows[i].detail),
                v.w - 40, 3);
        }
        if (!wre_count)
            host->text(
                fb, 20, top + 6, 16,
                wre_request.kind == PGA_WEREAD_SEARCH ? "未找到结果，试试其他关键词。"
                                                      : "暂无对应记录",
                4);
    }
    int y = v.h - 52;
    host->button(fb, 16, y, 78, 34, "上一页", false);
    host->button(fb, v.w - 94, y, 78, 34, "下一页", false);
    char label[80];
    if (wre_text.type == '"')
        host->format(
            label, sizeof(label), "第 %u 页%s", wre_text_page + 1,
            wre_text_more ? " · 可继续" : "");
    else if (wre_request.kind == PGA_WEREAD_STATS || wre_request.kind == PGA_WEREAD_PROGRESS)
        host->format(
            label, sizeof(label), "%u / %u", wre_offset / wre_body_rows() + 1,
            (wre_text_count + wre_body_rows() - 1) / wre_body_rows());
    else if (wre_total)
        host->format(
            label, sizeof(label), "%u–%u / %u%s", wre_offset + 1, wre_offset + wre_count, wre_total,
            wre_has_more ? "+" : "");
    else
        host->format(label, sizeof(label), "官方数据");
    host->center(fb, v.w / 2, y + 10, 12, label, 3);
}
static bool wre_event(const pico_app_event_t* e) {
    if (mode != DISCOVER && mode != ONLINE) return false;
    pico_app_view_t v = host->view();
    if (e->type == PGA_SWIPE_L || e->type == PGA_SWIPE_U) return wre_turn(true);
    if (e->type == PGA_SWIPE_R || e->type == PGA_SWIPE_D) return wre_turn(false);
    if (e->type != PGA_TAP) return false;
    if (e->x >= 16 && e->x < 74 && e->y >= 20 && e->y < 52) return wre_back();
    int scope_y = mode == DISCOVER ? 172 : 128;
    if ((mode == DISCOVER || wre_request.kind == PGA_WEREAD_SEARCH) && e->y >= scope_y &&
        e->y < scope_y + 34) {
        unsigned width = (unsigned)(v.w - 48) / 5, col = (unsigned)(e->x - 16) / (width + 4);
        static const unsigned scopes[] = { 10, 6, 12, 16, 0 };
        if (e->x >= 16 && col < 5) {
            wre_scope = scopes[col];
            if (mode == ONLINE) {
                pico_app_weread_query_t q;
                host->copy(&q, &wre_request, sizeof(q));
                q.scope = wre_scope;
                q.cursor = 0;
                return wre_submit(&q, false);
            }
            return true;
        }
    }
    if (mode == DISCOVER) {
        if (e->x >= 20 && e->x < v.w - 20 && e->y >= 116 && e->y < 160) return wre_open_search();
        if (e->y >= 224 && e->y < 280) {
            int width = (v.w - 52) / 2;
            if (e->x >= 20 && e->x < 20 + width) return wre_begin(PGA_WEREAD_RECOMMEND, true);
            if (e->x >= 32 + width && e->x < v.w - 20) return wre_begin(PGA_WEREAD_STATS, true);
        }
        return false;
    }
    if (wre_request.kind == PGA_WEREAD_SEARCH && e->y >= 78 && e->y < 120 && e->x >= 20 &&
        e->x < v.w - 20)
        return wre_open_search();
    if (wre_request.kind == PGA_WEREAD_STATS && e->y >= 78 && e->y < 112 && e->x >= 16) {
        unsigned width = (unsigned)(v.w - 44) / 4, col = (unsigned)(e->x - 16) / (width + 4);
        if (col < 4) {
            pico_app_weread_query_t q;
            host->copy(&q, &wre_request, sizeof(q));
            q.mode = col;
            return wre_submit(&q, false);
        }
    }
    if (wre_is_book_view() && e->x >= v.w - 86 && e->x < v.w - 16 && e->y >= 20 && e->y < 52 &&
        wre_link[0]) {
        if (host->bytes >=
                offsetof(pico_app_host_t, weread_open_link) + sizeof(host->weread_open_link) &&
            host->weread_open_link) {
            if (!host->weread_open_link(wre_link)) host->toast("这本书暂时没有可用的官方阅读链接");
        } else
            host->toast("请在手机管理页打开官方阅读链接");
        return true;
    }
    if (wre_is_book_view() && e->x >= 16 && e->x < v.w - 16 && e->y >= 78 && e->y < 154) {
        unsigned width = (unsigned)(v.w - 48) / 3, col = (unsigned)(e->x - 16) / (width + 8),
                 row = (unsigned)(e->y - 78) / 42;
        static const unsigned kinds[] = { PGA_WEREAD_INFO,     PGA_WEREAD_CHAPTERS,
                                          PGA_WEREAD_PROGRESS, PGA_WEREAD_HIGHLIGHTS,
                                          PGA_WEREAD_REVIEWS,  PGA_WEREAD_SIMILAR };
        if (col < 3 && row < 2 && (e->y - 78) % 42 < 34) {
            pico_app_weread_query_t q;
            wre_new(&q, kinds[row * 3 + col]);
            wre_copy(q.book_id, sizeof(q.book_id), wre_request.book_id);
            return wre_submit(&q, false);
        }
    }
    if (wre_waiting) return false;
    if (wre_error[0]) {
        if (e->x >= 20 && e->x < 120 && e->y >= v.h - 112 && e->y < v.h - 74) {
            pico_app_weread_query_t q;
            host->copy(&q, &wre_request, sizeof(q));
            return wre_submit(&q, false);
        }
        return false;
    }
    if (e->y >= v.h - 52 && e->y < v.h - 18) {
        if (e->x >= 16 && e->x < 94) return wre_turn(false);
        if (e->x >= v.w - 94 && e->x < v.w - 16) return wre_turn(true);
    }
    if (wre_text_view || wre_request.kind == PGA_WEREAD_INFO ||
        wre_request.kind == PGA_WEREAD_PROGRESS || wre_request.kind == PGA_WEREAD_STATS)
        return false;
    if (e->x >= 20 && e->x < v.w - 20 && e->y >= wre_top() && e->y < v.h - 70) {
        unsigned row = (unsigned)(e->y - wre_top()) / 62;
        if (row < wre_count) {
            wre_row_t* item = &wre_rows[row];
            if (item->id[0]) return wre_open_info(item->id);
            if (item->link.type == '"' &&
                host->bytes >=
                    offsetof(pico_app_host_t, weread_open_link) + sizeof(host->weread_open_link) &&
                host->weread_open_link) {
                char link[1024];
                if (wrj_string(item->link, link, sizeof(link))) (void)host->weread_open_link(link);
                return true;
            }
            if (item->content.type == '"') {
                wre_text = item->content;
                wre_text_view = true;
                wre_text_page = 0;
                wre_text_page_build(0);
                return true;
            }
            if (wre_request.kind == PGA_WEREAD_CHAPTERS) {
                pico_app_weread_query_t q;
                wre_new(&q, PGA_WEREAD_HIGHLIGHTS);
                wre_copy(q.book_id, sizeof(q.book_id), wre_request.book_id);
                q.chapter_uid = item->chapter;
                return wre_submit(&q, true);
            }
        }
    }
    return false;
}

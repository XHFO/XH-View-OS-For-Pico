/* 中文：直编固件服务，以虚拟官方接口/独立临时TF与NVS验证，不访问真实账户。
 * English: Compile the firmware service with fake official transport and private temporary TF/NVS, without real
 * accounts. */
#define PICO_WEREAD_HOST_TEST 1
#include "../main/system/pico_weread.c"
#include <assert.h>
static char root_path[240], key_store[513];
static uint8_t selection_store[WR_SELECTION_BYTES];
static size_t selection_bytes;
static atomic_int mode, http_calls;
static atomic_bool wifi = true, card = true, hold, entered, release_job;
static atomic_bool hold_cover, entered_cover, release_cover;
static atomic_uint card_generation = 7, random_seed = 100;
static bool fail_selected;
static char *big_quote;
static atomic_uint checks;
extern bool wr_module_validate(const char *directory);
#define CHECK(x)                                                                                                       \
    do {                                                                                                               \
        if (!(x)) {                                                                                                    \
            fprintf(stderr, "check failed: %s line %d\n", #x, __LINE__);                                               \
            abort();                                                                                                   \
        }                                                                                                              \
        checks++;                                                                                                      \
    } while (0)
bool wr_test_wifi(void) { return atomic_load(&wifi); }
void wr_test_cover_clear(void) {}
void wr_test_cover_prepare(uint32_t id, const char *url, const atomic_bool *cancelled) {
    CHECK(id && url && cancelled);
    CHECK(!strcmp(url, "https://cdn.weread.qq.com/cover/fixture.jpg"));
    atomic_store(&entered_cover, true);
    while (atomic_load(&hold_cover) && !atomic_load(&release_cover)) {
        struct timespec t = {0, 1000000};
        nanosleep(&t, NULL);
    }
    // 模拟封面缺失或失败，不产生图像；资料JSON仍须可用。
    // Simulate a missing or failed cover; information JSON must remain usable.
}
bool wr_test_media(uint32_t *generation) {
    if (generation)
        *generation = atomic_load(&card_generation);
    return atomic_load(&card);
}
const char *wr_test_directory(void) { return root_path; }
uint32_t wr_test_random(void) { return atomic_fetch_add(&random_seed, 1); }
bool wr_test_nv_get(const char *name, void *out, size_t *size) {
    const void *data = NULL;
    size_t n = 0;
    if (!strcmp(name, "api_key") && *key_store) {
        data = key_store;
        n = strlen(key_store) + 1;
    } else if (!strcmp(name, "selected") && selection_bytes) {
        data = selection_store;
        n = selection_bytes;
    } else
        return false;
    if (out && *size < n)
        return false;
    if (out)
        memcpy(out, data, n);
    *size = n;
    return true;
}
bool wr_test_nv_set(const char *name, const void *data, size_t n) {
    if (strcmp(name, "selected") || n > sizeof(selection_store) || fail_selected)
        return false;
    memcpy(selection_store, data, n);
    selection_bytes = n;
    return true;
}
bool wr_test_nv_authorize(const char *key) {
    snprintf(key_store, sizeof(key_store), "%s", key);
    selection_bytes = 0;
    return true;
}
bool wr_test_nv_clear(void) {
    wipe(key_store, sizeof(key_store));
    selection_bytes = 0;
    return true;
}
static char *reply_json(const char *request) {
    cJSON *req = cJSON_Parse(request);
    CHECK(req);
    const char *api = cJSON_GetStringValue(member(req, "api_name"));
    CHECK(api && cJSON_IsString(member(req, "skill_version")) &&
          !strcmp(member(req, "skill_version")->valuestring, "1.0.5"));
    cJSON *r = cJSON_CreateObject();
    int fixture = atomic_load(&mode);
    if (!strcmp(api, "/shelf/sync")) {
        cJSON *books = cJSON_AddArrayToObject(r, "books");
        for (unsigned i = 0; i < (fixture == 8 ? 65u : 2u); i++) {
            cJSON *b = cJSON_CreateObject();
            char id[20];
            snprintf(id, sizeof(id), "book%u", i + 1);
            cJSON_AddStringToObject(b, "bookId", id);
            char title[360];
            memset(title, 'T', sizeof(title) - 1);
            title[sizeof(title) - 1] = 0;
            cJSON_AddStringToObject(b, "title", i ? "第二本" : title);
            cJSON_AddStringToObject(b, "author", "作者");
            cJSON_AddItemToArray(books, b);
        }
    } else if (!strcmp(api, "/book/bookmarklist")) {
        const char *book = cJSON_GetStringValue(member(req, "bookId"));
        CHECK(book);
        if (fixture == 6 && !strcmp(book, "book2")) {
            cJSON_Delete(req);
            cJSON_Delete(r);
            return strdup("{bad");
        }
        cJSON *list = cJSON_AddArrayToObject(r, "updated"), *chapters = cJSON_AddArrayToObject(r, "chapters");
        cJSON *ch = cJSON_CreateObject();
        cJSON_AddNumberToObject(ch, "chapterUid", 12);
        cJSON_AddStringToObject(ch, "title", "第一章");
        cJSON_AddItemToArray(chapters, ch);
        if (!strcmp(book, "book1")) {
            for (unsigned type = 0; type < 2; type++) {
                cJSON *n = cJSON_CreateObject();
                cJSON_AddNumberToObject(n, "type", type);
                cJSON_AddStringToObject(n, "bookmarkId", type ? "highlight" : "position");
                cJSON_AddStringToObject(n, "markText",
                                        fixture == 9    ? big_quote
                                        : fixture == 10 ? "首行\r\n次行\r末行"
                                        : fixture == 11 ? "非法\x7f文本"
                                        : type          ? "合法划线"
                                                        : "不能当划线");
                cJSON_AddNumberToObject(n, "chapterUid", 12);
                cJSON_AddItemToArray(list, n);
            }
        }
    } else if (!strcmp(api, "/review/list/mine")) {
        const char *book = cJSON_GetStringValue(member(req, "bookid"));
        CHECK(book);
        int64_t cursor = 0;
        CHECK(integer(member(req, "synckey"), &cursor));
        cJSON *list = cJSON_AddArrayToObject(r, "reviews");
        bool empty = strcmp(book, "book1") || fixture == 9;
        if (!empty) {
            cJSON *item = cJSON_CreateObject(), *review = cJSON_AddObjectToObject(item, "review");
            cJSON_AddStringToObject(review, "reviewId", cursor ? "thought2" : "thought1");
            cJSON_AddStringToObject(review, "abstract", "引用");
            cJSON_AddStringToObject(review, "content", cursor ? "想法二" : "想法一");
            cJSON_AddStringToObject(review, "chapterName", "第二章");
            cJSON_AddItemToArray(list, item);
        }
        cJSON_AddNumberToObject(r, "hasMore", !empty && (fixture == 7 || !cursor) ? 1 : 0);
        cJSON_AddNumberToObject(r, "synckey", fixture == 7 ? 0 : 17);
    } else if (!strcmp(api, "/store/search")) {
        CHECK(!strcmp(cJSON_GetStringValue(member(req, "keyword")), "三体"));
        CHECK(cJSON_IsNumber(member(req, "scope")) && cJSON_IsNumber(member(req, "count")) && cJSON_IsNumber(member(req, "maxIdx")));
        cJSON *groups = cJSON_AddArrayToObject(r, "results"), *group = cJSON_CreateObject();
        cJSON_AddStringToObject(group, "title", "电子书");
        cJSON_AddNumberToObject(group, "scope", 17);
        cJSON *books = cJSON_AddArrayToObject(group, "books"), *item = cJSON_CreateObject(), *info = cJSON_AddObjectToObject(item, "bookInfo");
        cJSON_AddNumberToObject(item, "searchIdx", 17);
        cJSON_AddStringToObject(info, "bookId", "book1");
        cJSON_AddStringToObject(info, "title", "三体");
        cJSON_AddStringToObject(info, "author", "刘慈欣");
        cJSON_AddStringToObject(info, "cover", "https://cdn.weread.qq.com/fixture.jpg");
        cJSON_AddStringToObject(info, "readerLink", "https://weread.qq.com/web/reader/fixture");
        cJSON_AddItemToArray(books, item);
        cJSON_AddItemToArray(groups, group);
        cJSON_AddNumberToObject(r, "hasMore", 1);
    } else if (!strcmp(api, "/book/info")) {
        CHECK(!strcmp(cJSON_GetStringValue(member(req, "bookId")), "book1"));
        cJSON_AddStringToObject(r, "bookId", "book1");
        cJSON_AddStringToObject(r, "title", fixture == 19 ? "非法\xff文本" : "三体");
        cJSON_AddStringToObject(r, "intro", fixture == 18 ? big_quote : "完整简介：\"保留引用\"、换行\n与中文。");
        cJSON_AddStringToObject(r, "cover", "https://cdn.weread.qq.com/cover/fixture.jpg");
        cJSON_AddStringToObject(r, "readerLink", "https://weread.qq.com/web/reader/fixture");
    } else if (!strcmp(api, "/book/chapterinfo")) {
        CHECK(!strcmp(cJSON_GetStringValue(member(req, "bookId")), "book1"));
        cJSON *chapters = cJSON_AddArrayToObject(r, "chapters");
        for (unsigned i = 0; i < (fixture == 14 ? 2000u : 2u); i++) {
            cJSON *chapter = cJSON_CreateObject();
            cJSON_AddNumberToObject(chapter, "chapterUid", i + 1);
            cJSON_AddNumberToObject(chapter, "chapterIdx", i + 1);
            cJSON_AddStringToObject(chapter, "title", "第一部：科学边界与宇宙中的文明——完整章节目录不应被截断");
            cJSON_AddNumberToObject(chapter, "wordCount", 2000);
            cJSON_AddNumberToObject(chapter, "paid", 1);
            cJSON_AddItemToArray(chapters, chapter);
        }
    } else if (!strcmp(api, "/book/getprogress")) {
        CHECK(!strcmp(cJSON_GetStringValue(member(req, "bookId")), "book1"));
        cJSON *book = cJSON_AddObjectToObject(r, "book");
        cJSON_AddNumberToObject(book, "progress", 1);
        cJSON_AddNumberToObject(book, "recordReadingTime", 3661);
    } else if (!strcmp(api, "/book/recommend")) {
        CHECK(cJSON_IsNumber(member(req, "count")) && cJSON_IsNumber(member(req, "maxIdx")));
        cJSON *books = cJSON_AddArrayToObject(r, "books"), *book = cJSON_CreateObject();
        cJSON_AddStringToObject(book, "bookId", "book2");
        cJSON_AddStringToObject(book, "title", "推荐书籍");
        cJSON_AddNumberToObject(book, "searchIdx", 29);
        cJSON_AddItemToArray(books, book);
    } else if (!strcmp(api, "/book/similar")) {
        CHECK(!strcmp(cJSON_GetStringValue(member(req, "bookId")), "book1"));
        CHECK(cJSON_IsNumber(member(req, "count")) && cJSON_IsNumber(member(req, "maxIdx")));
        cJSON *similar = cJSON_AddObjectToObject(r, "booksimilar"), *books = cJSON_AddArrayToObject(similar, "books");
        cJSON_AddStringToObject(similar, "sessionId", "fixture-session");
        cJSON *item = cJSON_CreateObject(), *book = cJSON_AddObjectToObject(item, "book"), *info = cJSON_AddObjectToObject(book, "bookInfo");
        cJSON_AddNumberToObject(item, "idx", 31);
        cJSON_AddStringToObject(info, "bookId", "book2");
        cJSON_AddStringToObject(info, "title", "相似书籍");
        cJSON_AddItemToArray(books, item);
    } else if (!strcmp(api, "/readdata/detail")) {
        const char *period = cJSON_GetStringValue(member(req, "mode"));
        CHECK(period && (!strcmp(period, "weekly") || !strcmp(period, "monthly") || !strcmp(period, "annually") || !strcmp(period, "overall")));
        CHECK(cJSON_IsNumber(member(req, "baseTime")));
        cJSON_AddNumberToObject(r, "totalReadTime", 3661);
        cJSON_AddNumberToObject(r, "readDays", 2);
        cJSON_AddNumberToObject(r, "dayAverageReadTime", 1200);
        cJSON_AddStringToObject(r, "preferCategoryWord", "偏好阅读文学");
    } else if (!strcmp(api, "/book/bestbookmarks")) {
        CHECK(!strcmp(cJSON_GetStringValue(member(req, "bookId")), "book1"));
        CHECK(cJSON_IsNumber(member(req, "chapterUid")) && !member(req, "count") && !member(req, "maxIdx"));
        cJSON *items = cJSON_AddArrayToObject(r, "items"), *item = cJSON_CreateObject();
        cJSON_AddStringToObject(item, "markText", "宇宙很大，生活更大。");
        cJSON_AddNumberToObject(item, "totalCount", 120);
        cJSON_AddItemToArray(items, item);
    } else if (!strcmp(api, "/review/list")) {
        CHECK(!strcmp(cJSON_GetStringValue(member(req, "bookId")), "book1"));
        CHECK(cJSON_IsNumber(member(req, "synckey")) && cJSON_IsNumber(member(req, "maxIdx")) && cJSON_IsNumber(member(req, "reviewListType")));
        cJSON *items = cJSON_AddArrayToObject(r, "reviews"), *item = cJSON_CreateObject(), *wrap = cJSON_AddObjectToObject(item, "review"), *review = cJSON_AddObjectToObject(wrap, "review");
        cJSON_AddStringToObject(review, "content", "公开点评完整文本");
        cJSON_AddNumberToObject(review, "star", 80);
        cJSON_AddNumberToObject(item, "idx", 41);
        cJSON_AddItemToArray(items, item);
        cJSON_AddNumberToObject(r, "synckey", 47);
        cJSON_AddNumberToObject(r, "reviewsHasMore", 1);
    } else
        CHECK(false);
    cJSON_Delete(req);
    if (fixture == 21 || fixture == 22 || fixture == 23 || fixture == 24 || fixture == 25 || fixture == 26) {
        cJSON_Delete(r);
        if (fixture == 21)
            return strdup("{\"data\":null}");
        if (fixture == 22) {
            char *deep = calloc(1, 96);
            CHECK(deep);
            strcat(deep, "{\"x\":");
            for (unsigned i = 0; i < 33; i++) strcat(deep, "[");
            strcat(deep, "0");
            for (unsigned i = 0; i < 33; i++) strcat(deep, "]");
            strcat(deep, "}");
            return deep;
        }
        if (fixture == 23)
            return strdup("{\"title\":\"valid\\u0000truncated\"}");
        if (fixture == 24)
            return strdup("{\"title\":\"literal\\\\u0000\"}");
        if (fixture == 25)
            return strdup("{\"title\":\"first\"}{\"title\":\"second\"}");
        return strdup("{\"data\":[]}");
    }
    if (fixture == 12) {
        cJSON *envelope = cJSON_CreateObject();
        cJSON_AddItemToObject(envelope, "data", r);
        r = envelope;
    } else if (fixture == 13)
        cJSON_AddObjectToObject(r, "upgrade_info");
    else if (fixture == 16)
        cJSON_AddNumberToObject(r, "errcode", 7);
    char *text = cJSON_PrintUnformatted(r);
    cJSON_Delete(r);
    return text;
}
const char *wr_test_http(const char *key, const char *request, char *response, size_t capacity, size_t *used,
                         int *status) {
    CHECK(!strcmp(key, "wrk-fixture-123") || !strcmp(key, "wrk-second-123"));
    atomic_fetch_add(&http_calls, 1);
    if (atomic_load(&hold)) {
        atomic_store(&entered, true);
        while (!atomic_load(&release_job)) {
            struct timespec t = {0, 1000000};
            nanosleep(&t, NULL);
        }
    }
    if (atomic_load(&mode) == 2)
        return "tls";
    if (atomic_load(&mode) == 3) {
        *status = 401;
        *used = 0;
        return NULL;
    }
    if (atomic_load(&mode) == 4) {
        atomic_fetch_add(&card_generation, 1);
    }
    char *text = reply_json(request);
    CHECK(text);
    size_t n = strlen(text);
    if (n >= capacity) {
        cJSON_free(text);
        return "too_large";
    }
    memcpy(response, text, n + 1);
    *used = n;
    *status = 200;
    cJSON_free(text);
    return NULL;
}
static int call(const char *action, const char *body, char *out) {
    char path[100];
    snprintf(path, sizeof(path), "/api/manage/pico/weread/%s", action);
    return ps_weread_handle(!strcmp(action, "status") ? "GET" : "POST", path, "application/json", body,
                            body ? strlen(body) : 0, out, 32768);
}
static ps_weread_status_t *snapshot(void) {
    ps_weread_status_t *s = malloc(sizeof(*s));
    CHECK(s);
    ps_weread_get_status(s);
    return s;
}
static void await_done(const char *state, const char *error) {
    for (unsigned i = 0; i < 10000 && ps_weread_busy(); i++) {
        struct timespec t = {0, 1000000};
        nanosleep(&t, NULL);
    }
    CHECK(!ps_weread_busy());
    ps_weread_status_t *s = snapshot();
    CHECK(!strcmp(s->state, state));
    CHECK(!strcmp(s->error, error));
    free(s);
}
static uint8_t *index_copy(void) {
    char path[240];
    path_for(path, sizeof(path), "index.dat");
    uint8_t *p = malloc(WRS_INDEX_BYTES);
    CHECK(p && read_state(path, p, WRS_INDEX_BYTES));
    CHECK(wr_module_validate(root_path));
    return p;
}
static void unchanged(const uint8_t *before) {
    uint8_t *now = index_copy();
    CHECK(!memcmp(before, now, WRS_INDEX_BYTES));
    free(now);
}
static void query_tests(char *out, const uint8_t *before) {
    static const char *const bodies[] = {
        "{\"kind\":\"search\",\"keyword\":\"三体\",\"scope\":10}",
        "{\"kind\":\"info\",\"book_id\":\"book1\"}",
        "{\"kind\":\"chapters\",\"book_id\":\"book1\"}",
        "{\"kind\":\"progress\",\"book_id\":\"book1\"}",
        "{\"kind\":\"recommend\",\"cursor\":17,\"count\":3}",
        "{\"kind\":\"similar\",\"book_id\":\"book1\",\"cursor\":31,\"session_id\":\"fixture-session\"}",
        "{\"kind\":\"stats\",\"mode\":1}",
        "{\"kind\":\"highlights\",\"book_id\":\"book1\",\"chapter_uid\":12}",
        "{\"kind\":\"reviews\",\"book_id\":\"book1\",\"cursor\":41,\"synckey\":47,\"review_type\":3}"
    };
    atomic_store(&mode, 12);
    ps_weread_query_status_t query = {0};
    CHECK(ps_weread_query(NULL, &query) && !query.ready && query.bytes == sizeof(query));
    CHECK(ps_weread_handle("GET", "/api/manage/pico/weread/query", NULL, NULL, 0, out, 32768) == 200);
    uint32_t previous_id = query.request_id;
    for (unsigned i = 0; i < sizeof(bodies) / sizeof(*bodies); i++) {
        CHECK(call("query", bodies[i], out) == 202);
        CHECK(!strstr(out, "wrk-"));
        cJSON *metadata = cJSON_Parse(out);
        int64_t id;
        CHECK(metadata && integer(member(metadata, "request_id"), &id) && id > previous_id);
        cJSON_Delete(metadata);
        await_done("done", "");
        CHECK(ps_weread_query(NULL, &query) && query.ready && !query.busy && query.kind == i + 1 && query.request_id == id && query.total_bytes);
        char *raw = calloc(1, query.total_bytes + 1);
        CHECK(raw);
        size_t used = 0;
        CHECK(!ps_weread_query_read(previous_id, 0, raw, 10, &used) && !used);
        for (uint32_t offset = 0; offset < query.total_bytes;) {
            CHECK(ps_weread_query_read(query.request_id, offset, raw + offset, 7, &used) && used && used <= 7);
            offset += (uint32_t)used;
        }
        CHECK(ps_weread_query_read(query.request_id, query.total_bytes, raw, 1, &used) && !used);
        CHECK(!ps_weread_query_read(query.request_id, query.total_bytes + 1, raw, 1, &used));
        cJSON *payload = cJSON_Parse(raw);
        CHECK(cJSON_IsObject(payload) && !member(payload, "data"));
        if (i == 0)
            CHECK(strstr(raw, "\"scope\":17") && strstr(raw, "三体"));
        if (i == 3)
            CHECK(strstr(raw, "\"progress\":1") && strstr(raw, "\"recordReadingTime\":3661"));
        if (i == 6)
            CHECK(strstr(raw, "\"totalReadTime\":3661") && strstr(raw, "\"readDays\":2"));
        if (i == 8)
            CHECK(strstr(raw, "公开点评完整文本") && strstr(raw, "\"review\":{\"review\":"));
        cJSON_Delete(payload);
        free(raw);
        previous_id = query.request_id;
    }
    // 资料先于可选封面发布；取图期间可读取同一请求，但不允许第二个任务插入。
    // Publish information before the optional cover; reads are allowed while admission remains exclusive.
    for (unsigned cancel_cover = 0; cancel_cover < 2; cancel_cover++) {
        atomic_store(&hold_cover, true);
        atomic_store(&entered_cover, false);
        atomic_store(&release_cover, false);
        CHECK(call("query", bodies[1], out) == 202);
        for (unsigned wait = 0; wait < 5000 && !atomic_load(&entered_cover); wait++) {
            struct timespec t = {0, 1000000};
            nanosleep(&t, NULL);
        }
        CHECK(atomic_load(&entered_cover));
        CHECK(ps_weread_query(NULL, &query) && query.ready && query.busy && query.total_bytes);
        uint32_t ready_revision = query.revision;
        char *raw = calloc(1, query.total_bytes + 1);
        size_t read = 0;
        CHECK(raw && ps_weread_query_read(query.request_id, 0, raw, query.total_bytes, &read) && read == query.total_bytes);
        CHECK(strstr(raw, "三体") && strstr(raw, "完整简介"));
        free(raw);
        char result_body[128];
        snprintf(result_body, sizeof(result_body), "{\"request_id\":%u,\"offset\":0,\"limit\":8192}", query.request_id);
        CHECK(call("result", result_body, out) == 200 && strstr(out, "完整简介"));
        CHECK(!ps_weread_pause() && call("query", bodies[4], out) == 409);
        CHECK(call("authorize", "{\"api_key\":\"wrk-fixture-123\"}", out) == 409);
        CHECK(call("clear", "{}", out) == 409);
        if (cancel_cover) {
            ps_weread_cancel();
            CHECK(ps_weread_query(NULL, &query) && !query.ready && query.busy && !query.total_bytes && !strcmp(query.error, "cancelled"));
            CHECK(!ps_weread_query_read(query.request_id, 0, out, 10, &read) && !read);
            CHECK(call("result", result_body, out) == 409 && strstr(out, "stale_query"));
        }
        atomic_store(&release_cover, true);
        await_done(cancel_cover ? "error" : "done", cancel_cover ? "cancelled" : "");
        CHECK(ps_weread_query(NULL, &query) && !query.busy && query.revision > ready_revision);
        if (cancel_cover) {
            CHECK(!query.ready && !query.total_bytes && !s_query_data);
            CHECK(!ps_weread_query_read(query.request_id, 0, out, 10, &read));
        } else {
            CHECK(query.ready && query.total_bytes);
            ps_weread_cancel();
            CHECK(ps_weread_query_read(query.request_id, 0, out, 10, &read) && read);
        }
        CHECK(ps_weread_pause());
        ps_weread_resume();
        atomic_store(&hold_cover, false);
        unchanged(before);
    }
    // 任意接口、未知参数、重复字段、非整数与非法标识不得获得工作准入。
    // Reject arbitrary APIs, unknown/duplicate parameters, fractions and invalid identifiers before admission.
    static const char *const invalid[] = {
        "{\"kind\":\"download\",\"book_id\":\"book1\"}",
        "{\"kind\":\"info\",\"book_id\":\"book1\",\"url\":\"https://example.com\"}",
        "{\"kind\":\"search\",\"keyword\":\"三体\",\"scope\":17}",
        "{\"kind\":\"search\",\"keyword\":\"三体\",\"scope\":10.1}",
        "{\"kind\":\"search\",\"keyword\":\"三体\",\"count\":13}",
        "{\"kind\":\"search\",\"keyword\":\"\"}",
        "{\"kind\":\"info\",\"book_id\":\"../book\"}",
        "{\"kind\":\"info\"}",
        "{\"kind\":\"stats\",\"mode\":4}",
        "{\"kind\":\"stats\",\"base_time\":-1}",
        "{\"kind\":\"reviews\",\"book_id\":\"book1\",\"review_type\":5}",
        "{\"kind\":\"similar\",\"book_id\":\"book1\",\"session_id\":\"bad session\"}",
        "{\"kind\":\"recommend\",\"cursor\":9007199254740992}",
        "{\"kind\":\"recommend\",\"cursor\":\"17\"}"
    };
    int calls = atomic_load(&http_calls);
    for (unsigned i = 0; i < sizeof(invalid) / sizeof(*invalid); i++)
        CHECK(call("query", invalid[i], out) == 400 && strstr(out, "invalid_query"));
    CHECK(call("query", "{\"kind\":\"info\\u0000download\",\"book_id\":\"book1\"}", out) == 400 && strstr(out, "malformed"));
    CHECK(call("query", "{\"kind\":\"search\",\"kind\":\"info\",\"keyword\":\"三体\"}", out) == 400 && strstr(out, "malformed"));
    CHECK(atomic_load(&http_calls) == calls);
    ps_weread_query_t q = {.bytes = sizeof(q), .kind = PS_WEREAD_SEARCH, .scope = 10};
    memset(q.keyword, 'K', sizeof(q.keyword));
    CHECK(!ps_weread_query(&q, &query) && !strcmp(query.error, "invalid_query"));
    memset(q.keyword, 0, sizeof(q.keyword));
    memcpy(q.keyword, "三体", sizeof("三体"));
    q.bytes--;
    CHECK(!ps_weread_query(&q, &query));
    q.bytes++;
    CHECK(ps_weread_pause());
    CHECK(!ps_weread_query(&q, &query) && !strcmp(query.error, "busy"));
    ps_weread_resume();
    atomic_store(&wifi, false);
    CHECK(!ps_weread_query(&q, &query) && !strcmp(query.error, "offline"));
    atomic_store(&wifi, true);
    // 在线发现不占用TF；即便卡不可用，也保留已有笔记快照。
    // Online discovery does not own TF storage, and preserves prior snapshots even without a card.
    atomic_store(&card, false);
    CHECK(ps_weread_query(&q, &query));
    await_done("done", "");
    atomic_store(&card, true);
    unchanged(before);
    atomic_store(&hold, true);
    atomic_store(&entered, false);
    atomic_store(&release_job, false);
    CHECK(call("query", bodies[1], out) == 202);
    while (!atomic_load(&entered)) {
        struct timespec t = {0, 1000000};
        nanosleep(&t, NULL);
    }
    CHECK(!ps_weread_pause() && !ps_weread_sync_saved());
    CHECK(call("query", bodies[4], out) == 409);
    CHECK(call("authorize", "{\"api_key\":\"wrk-fixture-123\"}", out) == 409);
    CHECK(call("clear", "{}", out) == 409);
    ps_weread_cancel();
    atomic_store(&release_job, true);
    await_done("error", "cancelled");
    CHECK(ps_weread_query(NULL, &query) && !query.ready && !query.busy && !strcmp(query.error, "cancelled"));
    size_t used;
    CHECK(!ps_weread_query_read(query.request_id, 0, out, 10, &used));
    atomic_store(&hold, false);
    for (unsigned i = 0; i < 10; i++) {
        static const int fixtures[] = {2, 3, 13, 16, 19, 21, 22, 23, 25, 26};
        static const char *const errors[] = {"tls", "auth", "upgrade", "service", "malformed", "malformed", "malformed", "malformed", "malformed", "malformed"};
        atomic_store(&mode, fixtures[i]);
        CHECK(call("query", bodies[1], out) == 202);
        await_done("error", errors[i]);
        CHECK(ps_weread_query(NULL, &query) && !query.ready && !strcmp(query.error, errors[i]));
    }
    atomic_store(&mode, 24);
    CHECK(call("query", bodies[1], out) == 202);
    await_done("done", "");
    atomic_store(&mode, 14);
    CHECK(call("query", bodies[2], out) == 202);
    await_done("done", "");
    CHECK(ps_weread_query(NULL, &query) && query.ready && query.total_bytes > 32768);
    char *joined = calloc(1, query.total_bytes + 1);
    CHECK(joined);
    for (uint32_t offset = 0; offset < query.total_bytes;) {
        char body[160];
        snprintf(body, sizeof(body), "{\"request_id\":%u,\"offset\":%u,\"limit\":97}", query.request_id, offset);
        CHECK(call("result", body, out) == 200 && !strstr(out, "wrk-"));
        cJSON *part = cJSON_Parse(out);
        int64_t next, total;
        const cJSON *chunk = member(part, "chunk");
        CHECK(part && integer(member(part, "next_offset"), &next) && next > offset &&
              integer(member(part, "total_bytes"), &total) && total == query.total_bytes && cJSON_IsString(chunk));
        size_t n = strlen(chunk->valuestring);
        CHECK(n == (size_t)next - offset && utf8(chunk->valuestring, n));
        memcpy(joined + offset, chunk->valuestring, n);
        offset = (uint32_t)next;
        cJSON_Delete(part);
    }
    cJSON *chapters = cJSON_Parse(joined);
    CHECK(chapters && cJSON_GetArraySize(member(chapters, "chapters")) == 2000);
    cJSON_Delete(chapters);
    free(joined);
    CHECK(call("result", "{\"request_id\":0}", out) == 400);
    CHECK(call("result", "{\"request_id\":1,\"limit\":8193}", out) == 400);
    CHECK(call("result", "{\"request_id\":1,\"offset\":0}", out) == 409);
    CHECK(call("result", "{\"request_id\":1,\"offset\":-1}", out) == 400);
    big_quote = malloc(WR_RESPONSE_MAX + 1);
    CHECK(big_quote);
    memset(big_quote, 'Q', WR_RESPONSE_MAX);
    big_quote[WR_RESPONSE_MAX] = 0;
    atomic_store(&mode, 18);
    CHECK(call("query", bodies[1], out) == 202);
    await_done("error", "too_large");
    free(big_quote);
    big_quote = NULL;
    atomic_store(&mode, 12);
    CHECK(call("query", bodies[1], out) == 202);
    await_done("done", "");
    CHECK(ps_weread_query(NULL, &query) && query.ready);
    uint32_t valid_id = query.request_id;
    CHECK(call("authorize", "{\"api_key\":\"wrk-second-123\"}", out) == 200);
    CHECK(!ps_weread_query_read(valid_id, 0, out, 10, &used));
    CHECK(ps_weread_query(NULL, &query) && !query.ready && query.request_id != valid_id);
    atomic_store(&mode, 0);
    unchanged(before);
}
int main(int argc, char **argv) {
    CHECK(argc == 2);
    snprintf(root_path, sizeof(root_path), "%s/weread", argv[1]);
    char out[32768];
    ps_weread_init();
    CHECK(call("status", NULL, out) == 200);
    CHECK(strstr(out, "\"configured\":false"));
    CHECK(call("authorize", "{\"api_key\":\"bad\"}", out) == 400);
    CHECK(!*key_store);
    CHECK(call("authorize", "{\"api_key\":\"  wrk-fixture-123 \\n\"}", out) == 200);
    CHECK(!strstr(out, "wrk-"));
    ps_weread_status_t *s = snapshot();
    uint32_t revision = s->revision;
    CHECK(s->configured && !s->verified);
    free(s);
    CHECK(!ps_weread_sync_saved());
    await_done("error", "selection");
    CHECK(ps_weread_pause());
    CHECK(ps_weread_pause());
    CHECK(s_pause_leases == 2);
    CHECK(call("shelf", "{}", out) == 409);
    ps_weread_resume();
    CHECK(s_pause_leases == 1);
    CHECK(call("shelf", "{}", out) == 409);
    CHECK(call("clear", "{}", out) == 409);
    ps_weread_resume();
    CHECK(s_pause_leases == 0);
    ps_weread_resume();
    CHECK(s_pause_leases == 0);
    s_pause_leases = UINT16_MAX;
    CHECK(!ps_weread_pause() && s_pause_leases == UINT16_MAX);
    ps_weread_resume();
    CHECK(s_pause_leases == UINT16_MAX - 1);
    s_pause_leases = 0;
    CHECK(call("shelf", "{}", out) == 200);
    await_done("done", "");
    s = snapshot();
    CHECK(s->verified && s->books_count == 2 && s->revision > revision);
    CHECK(strlen(s->books[0].title) == 127);
    free(s);
    atomic_store(&mode, 3);
    CHECK(call("shelf", "{}", out) == 200);
    await_done("error", "auth");
    s = snapshot();
    CHECK(!s->verified);
    free(s);
    atomic_store(&mode, 0);
    CHECK(call("sync", "{\"book_ids\":[\"book1\",\"book1\"]}", out) == 400);
    CHECK(call("sync", "{\"book_ids\":[\"book1\",\"book2\"]}", out) == 200);
    await_done("done", "");
    char path[240];
    path_for(path, sizeof(path), "index.dat");
    CHECK(valid_index(path));
    uint8_t *before = index_copy();
    CHECK(get32(before + WRS_I_BOOKS) == 2 && get32(before + WRS_I_NOTES) == 3);
    CHECK(get32(before + 64 + WRS_B_CHUNKS) == 1 && get32(before + 64 + 256 + WRS_B_CHUNKS) == 0);
    uint32_t generation = get32(before + WRS_I_GENERATION);
    char name[80];
    snprintf(name, sizeof(name), "g%08x-b00-c000.dat", (unsigned)generation);
    path_for(path, sizeof(path), name);
    uint8_t *chunk = malloc(WRS_CHUNK_BYTES);
    CHECK(chunk && read_state(path, chunk, WRS_CHUNK_BYTES));
    CHECK(strstr((char *)chunk + 64, "合法划线") && !strstr((char *)chunk + 64, "不能当划线") &&
          strstr((char *)chunk + 64, "想法二"));
    free(chunk);
    path_for(path, sizeof(path), "position.dat");
    FILE *position = fopen(path, "wb");
    CHECK(position && fwrite("position-owned-by-app", 1, 21, position) == 21 && fclose(position) == 0);
    atomic_store(&mode, 6);
    CHECK(ps_weread_sync_saved());
    await_done("error", "malformed");
    unchanged(before);
    s = snapshot();
    CHECK(!s->synced_books && !s->synced_notes);
    free(s);
    atomic_store(&mode, 7);
    CHECK(ps_weread_sync_saved());
    await_done("error", "malformed");
    unchanged(before);
    atomic_store(&mode, 4);
    CHECK(ps_weread_sync_saved());
    await_done("error", "storage");
    unchanged(before);
    atomic_store(&mode, 0);
    atomic_store(&hold, true);
    atomic_store(&entered, false);
    atomic_store(&release_job, false);
    CHECK(ps_weread_sync_saved());
    while (!atomic_load(&entered)) {
        struct timespec t = {0, 1000000};
        nanosleep(&t, NULL);
    }
    CHECK(!ps_weread_pause());
    CHECK(s_pause_leases == 0);
    CHECK(call("clear", "{}", out) == 409);
    CHECK(call("authorize", "{\"api_key\":\"wrk-second-123\"}", out) == 409);
    ps_weread_cancel();
    atomic_store(&release_job, true);
    await_done("error", "cancelled");
    unchanged(before);
    atomic_store(&hold, false);
    atomic_store(&mode, 8);
    CHECK(call("shelf", "{}", out) == 200);
    await_done("error", "too_large");
    unchanged(before);
    atomic_store(&mode, 0);
    fail_selected = true;
    CHECK(ps_weread_sync_saved());
    await_done("done", "");
    fail_selected = false;
    free(before);
    before = index_copy();
    // 损坏主索引但保留有效备份，下一次同步不能以坏主档覆盖备份。
    // An invalid primary must never overwrite the valid backup during the next sync.
    path_for(path, sizeof(path), "index.bak");
    uint8_t *backup = malloc(WRS_INDEX_BYTES);
    CHECK(backup && read_state(path, backup, WRS_INDEX_BYTES));
    uint8_t *bad = malloc(WRS_INDEX_BYTES);
    CHECK(bad);
    memcpy(bad, before, WRS_INDEX_BYTES);
    bad[0] = 'X';
    path_for(path, sizeof(path), "index.dat");
    CHECK(write_state(path, bad, WRS_INDEX_BYTES, false, atomic_load(&card_generation)));
    free(bad);
    CHECK(ps_weread_sync_saved());
    await_done("done", "");
    path_for(path, sizeof(path), "index.bak");
    uint8_t *backup_after = malloc(WRS_INDEX_BYTES);
    CHECK(backup_after && read_state(path, backup_after, WRS_INDEX_BYTES) &&
          !memcmp(backup, backup_after, WRS_INDEX_BYTES));
    free(backup);
    free(backup_after);
    // 多字节文本跨块后重组必须完整、每块UTF-8有效。
    // Reassemble multibyte text across chunks without loss and with valid UTF-8 in each chunk.
    big_quote = malloc(120001);
    CHECK(big_quote);
    for (size_t i = 0; i < 40000; i++)
        memcpy(big_quote + i * 3, "好", 3);
    big_quote[120000] = 0;
    atomic_store(&mode, 9);
    CHECK(call("sync", "{\"book_ids\":[\"book1\"]}", out) == 200);
    await_done("done", "");
    free(before);
    before = index_copy();
    generation = get32(before + WRS_I_GENERATION);
    uint32_t chunks = get32(before + 64 + WRS_B_CHUNKS);
    CHECK(chunks == 4);
    size_t copied = 0;
    char *assembled = calloc(1, 121000);
    CHECK(assembled);
    chunk = malloc(WRS_CHUNK_BYTES);
    CHECK(chunk);
    for (uint32_t i = 0; i < chunks; i++) {
        snprintf(name, sizeof(name), "g%08x-b00-c%03x.dat", (unsigned)generation, (unsigned)i);
        path_for(path, sizeof(path), name);
        CHECK(read_state(path, chunk, WRS_CHUNK_BYTES));
        size_t n = get32(chunk + WRS_C_TEXT);
        CHECK(utf8((char *)chunk + 64, n));
        memcpy(assembled + copied, chunk + 64, n);
        copied += n;
    }
    CHECK(strstr(assembled, big_quote));
    free(assembled);
    free(chunk);
    free(big_quote);
    atomic_store(&mode, 0);
    atomic_store(&mode, 10);
    CHECK(ps_weread_sync_saved());
    await_done("done", "");
    free(before);
    before = index_copy();
    generation = get32(before + WRS_I_GENERATION);
    snprintf(name, sizeof(name), "g%08x-b00-c000.dat", (unsigned)generation);
    path_for(path, sizeof(path), name);
    chunk = malloc(WRS_CHUNK_BYTES);
    CHECK(chunk && read_state(path, chunk, WRS_CHUNK_BYTES));
    CHECK(strstr((char *)chunk + 64, "首行\n次行\n末行") && !strchr((char *)chunk + 64, '\r'));
    free(chunk);
    atomic_store(&mode, 11);
    CHECK(ps_weread_sync_saved());
    await_done("error", "malformed");
    unchanged(before);
    atomic_store(&mode, 0);
    path_for(path, sizeof(path), "position.dat");
    FILE *f = fopen(path, "rb");
    char pos[32] = {0};
    CHECK(f && fread(pos, 1, 21, f) == 21 && fclose(f) == 0 && !strcmp(pos, "position-owned-by-app"));
    uint32_t old_epoch = s_auth_epoch;
    char old_ids[WRS_BOOK_MAX][64] = {{"book1"}};
    CHECK(call("authorize", "{\"api_key\":\"wrk-second-123\"}", out) == 200);
    CHECK(!selection_bytes);
    CHECK(!admit(true, (const char (*)[64])old_ids, 1, old_epoch));
    CHECK(!ps_weread_sync_saved());
    await_done("error", "selection");
    unchanged(before);
    query_tests(out, before);
    CHECK(call("clear", "{}", out) == 200);
    CHECK(!*key_store && !strstr(out, "wrk-") && strstr(out, "\"configured\":false"));
    unchanged(before);
    free(before);
    printf("WeRead firmware host checks: %u (fake transport only)\n", atomic_load(&checks));
    return 0;
}

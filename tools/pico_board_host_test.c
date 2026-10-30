/* 中文：实际语义解析/排版源码的边界、所有权、灰阶和时间回归；不访问硬件。
 * English: Bounds, ownership, grayscale and timing regressions for production semantic parsing/layout, without hardware. */
#include "pico_board_data.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "../components/read_pico_home_link/home_link_json.h"

static unsigned long checks;
#define CHECK(c) do { checks++; if (!(c)) { fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); abort(); } } while (0)
typedef struct { int width, height; unsigned fills, lines, texts, images; char large_text[128]; } surface_t;
static void rect(surface_t *s, int x, int y, int w, int h) {
    CHECK(x >= 0 && y >= 0 && w > 0 && h > 0 && x + w <= s->width && y + h <= s->height);
}
static int measure(void *ctx, int size, const char *s) {
    (void)ctx;
    int width = 0;
    while (*s) {
        unsigned c = (unsigned char)*s;
        if (c < 128) { width += (size * 6 + 9) / 10; s++; }
        else { width += size; s += c < 224 ? 2 : c < 240 ? 3 : 4; }
    }
    return width;
}
static void fill(void *ctx, int x, int y, int w, int h, int gray) {
    surface_t *s = ctx; rect(s, x, y, w, h); CHECK(gray >= 0 && gray <= 15); s->fills++;
    if(!(x==0&&y==0&&w==s->width&&h==s->height))CHECK(y>=40&&y+h<=s->height-36);
}
static void line(void *ctx, int x0, int y0, int x1, int y1, int gray) {
    surface_t *s = ctx; rect(s, x0, y0, 1, 1); rect(s, x1, y1, 1, 1);
    CHECK(y0>=40&&y1>=40&&y0<s->height-36&&y1<s->height-36);
    CHECK(gray >= 0 && gray <= 15); s->lines++;
}
static void text(void *ctx, int x, int y, int size, const char *value, int gray) {
    surface_t *s = ctx;
    if (*value) rect(s, x, y, measure(ctx, size, value), size);
    CHECK(y>=40&&y+size<=s->height-36);
    CHECK(gray >= 0 && gray <= 15 && size >= 13); s->texts++;
    if (size > 30) snprintf(s->large_text, sizeof(s->large_text), "%s", value);
}
static void image(void *ctx, const ps_board_asset_t *asset, int x, int y, int w, int h) {
    surface_t *s = ctx; rect(s, x, y, w, h); CHECK(asset && asset->pixels && asset->width && asset->height);
    CHECK(y>=40&&y+h<=s->height-36);
    CHECK(fabs((double)w / h - (double)asset->width / asset->height) < .1); s->images++;
}
static char *read_file(const char *path, size_t *bytes) {
    FILE *f = fopen(path, "rb"); CHECK(f != NULL); CHECK(!fseek(f, 0, SEEK_END));
    long n = ftell(f); CHECK(n >= 0); rewind(f);
    char *out = malloc((size_t)n + 1); CHECK(out != NULL); CHECK(fread(out, 1, (size_t)n, f) == (size_t)n);
    fclose(f); out[n] = 0; *bytes = (size_t)n; return out;
}
static void valid(const char *path) {
    size_t bytes;
    char *input = read_file(path, &bytes);
    const char *error = NULL;
    ps_board_model_t *m = ps_board_model_parse_ex(input, bytes, &error);
    if (!m) fprintf(stderr, "%s: %s\n", path, error);
    CHECK(m != NULL && !error && m->module_count && m->module_count <= 12);
    ps_board_model_t *second = ps_board_model_parse(input, bytes);
    CHECK(second && second->semantic_hash == m->semantic_hash);
    ps_board_model_free(second);
    memset(input, 0xa5, bytes); free(input);
    CHECK(m->allocation_bytes == sizeof(*m) + bytes + 1);
    CHECK(m->board_id && *m->board_id);
    ps_board_model_tick_at(m, 1000, 1793116800.);
    CHECK(!ps_board_model_tick_at(m, 2000, 1793116801.));
    char *copy = malloc(m->allocation_bytes); CHECK(copy != NULL); memcpy(copy, m, m->allocation_bytes);
    const int sizes[][2] = {{360,640},{640,360},{360,640},{640,360},{240,320},{320,240}};
    for (size_t i = 0; i < sizeof(sizes) / sizeof(sizes[0]); i++) {
        surface_t s = {.width=sizes[i][0], .height=sizes[i][1]};
        ps_board_painter_t ops = {&s,s.width,s.height,fill,line,text,measure,image};
        ps_board_model_paint(m, &ops);
        // 中文：实际Pico横竖屏均须可读；极小的压力面只要求不越过安全区。
        // English: Both physical Pico orientations must remain readable; undersized stress surfaces only enforce safe clipping.
        if(!s.texts&&s.height>=320)fprintf(stderr,"no text: %s %dx%d (%u modules)\n",path,s.width,s.height,m->module_count);
        CHECK(s.fills > 0 && (s.texts > 0 || s.height<320));
        CHECK(!memcmp(copy, m, m->allocation_bytes));
        bool has_asset = false;
        for (unsigned j = 0; j < m->module_count; j++) if (m->modules[j].asset_index >= 0) has_asset = true;
        if (has_asset && m->module_count == 1) CHECK(s.images > 0);
    }
    free(copy);
    bool dynamic = false;
    for (unsigned i=0; i<m->module_count; i++) if (m->modules[i].kind==PS_BOARD_CLOCK || m->modules[i].kind==PS_BOARD_DATE || (m->modules[i].running && m->modules[i].duration>0)) dynamic=true;
    CHECK(ps_board_model_tick_at(m, 62000, 1793116861.) == dynamic);
    CHECK(!ps_board_model_tick_at(m, 63000, 1793116862.));
    CHECK(!ps_board_model_tick_at(NULL, 0, 0));
    ps_board_model_free(m);
}
static void invalid(const char *path) {
    size_t bytes; char *input = read_file(path, &bytes); const char *error = NULL;
    ps_board_model_t *m = ps_board_model_parse_ex(input, bytes, &error);
    if (m) fprintf(stderr, "unexpected acceptance: %s\n", path);
    CHECK(!m && error && *error); free(input);
}
static unsigned allocation_calls, allocation_fail, allocation_live;
static void *json_malloc(size_t n) {
    if (++allocation_calls == allocation_fail) return NULL;
    void *p = malloc(n); if (p) allocation_live++; return p;
}
static void json_free(void *p) { if (p) { CHECK(allocation_live); allocation_live--; } free(p); }
static void invariants(void) {
    const char *first = "{\"schema\":1,\"board_id\":\"a\",\"generated_at\":1793116800,\"columns\":1,\"modules\":[{\"id\":\"one\",\"kind\":\"text\",\"body\":\"hello\"}]}";
    const char *later = "{\"schema\":1,\"board_id\":\"a\",\"generated_at\":1793116861,\"columns\":1,\"modules\":[{\"id\":\"one\",\"kind\":\"text\",\"body\":\"hello\"}]}";
    ps_board_model_t *a = ps_board_model_parse(first, strlen(first)), *b = ps_board_model_parse(later, strlen(later));
    CHECK(a && b && a->semantic_hash == b->semantic_hash);
    ps_board_model_free(a); ps_board_model_free(b);
    cJSON_Hooks hooks = {json_malloc,json_free}; cJSON_InitHooks(&hooks);
    allocation_calls = 0; allocation_fail = 0;
    a = ps_board_model_parse(first, strlen(first)); CHECK(a && !allocation_live);
    unsigned calls = allocation_calls; ps_board_model_free(a);
    for (unsigned fail = 1; fail <= calls; fail++) {
        allocation_calls = 0; allocation_fail = fail;
        a = ps_board_model_parse(first, strlen(first));
        CHECK(!a && !allocation_live);
    }
    cJSON_InitHooks(NULL);
    const char *clock = "{\"schema\":1,\"board_id\":\"a\",\"generated_at\":1793116800,\"columns\":1,\"modules\":[{\"id\":\"one\",\"kind\":\"clock\"}]}";
    a = ps_board_model_parse(clock, strlen(clock)); CHECK(a);
    CHECK(!ps_board_model_tick_at(a, 1000, -1)); CHECK(a->display_time == 1793116800);
    CHECK(ps_board_model_tick_at(a, 61000, -1)); CHECK(a->display_time == 1793116860);
    CHECK(!ps_board_model_tick_at(a, 62000, -1)); CHECK(a->display_time == 1793116861);
    CHECK(ps_board_model_set_timezone(a,480)); CHECK(!ps_board_model_set_timezone(a,480));
    CHECK(!ps_board_model_set_timezone(a,841)); CHECK(!ps_board_model_set_timezone(a,17));
    a->display_time=0;
    surface_t surface={.width=360,.height=640};
    ps_board_painter_t painter={&surface,360,640,fill,line,text,measure,image};
    ps_board_model_paint(a,&painter); CHECK(!strcmp(surface.large_text,"08:00"));
    CHECK(ps_board_model_set_timezone(a,-480));
    ps_board_model_paint(a,&painter); CHECK(!strcmp(surface.large_text,"16:00"));
    ps_board_model_free(a);
}
int main(int argc, char **argv) {
    CHECK(argc == 2); FILE *list = fopen(argv[1], "r"); CHECK(list != NULL);
    char kind, path[2048]; unsigned good = 0, bad = 0;
    while (fscanf(list, " %c %2047s", &kind, path) == 2) {
        if (kind == 'V') { valid(path); good++; } else { invalid(path); bad++; }
    }
    fclose(list); CHECK(good && bad); invariants();
    printf("%lu checks PASS (%u valid, %u rejected)\n", checks, good, bad);
    return 0;
}

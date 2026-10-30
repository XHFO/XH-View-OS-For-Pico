/* SPDX-License-Identifier: Apache-2.0
 * 真实 TF 页面同源截图与字形边界检查，不连接硬件，也不执行格式化。
 * Capture the actual TF page and check glyph bounds, without hardware or formatting.
 */
#include "pico_system.h"
#include "pico_reader.h"
#include "pico_framebuffer_host.h"
#include "read_pico_sd.h"
#include "ttf_font.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct { int x, y, w, h; } rect_t;
typedef struct { rect_t ink; int button; char text[768]; } text_record_t;
static ps_view_t test_view = {360, 640, false};
static text_record_t text_records[64];
static rect_t buttons[8], icons[8];
static int text_count, button_count, icon_count, active_button = -1;
static unsigned checks, failures, scenes;
static uint32_t missing_codepoints[256];
static int missing_count;
static const char *scene_name;
static const char *storage_target = "笔记存储：暂不可用；小设置保存在设备内";
static FILE *report;

ps_view_t ps_view(void) { return test_view; }
ps_route_t ps_route(void) { return PS_STORAGE; }
int64_t ps_now(void) { return 10000; }
bool ps_storage_prepare(void) { abort(); }
void ps_storage_resume(void) { abort(); }
bool ps_storage_rebind(bool formatted) { (void)formatted; abort(); }
const char *ps_storage_target(void) { return storage_target; }
bool ps_storage_record_roots(char *current, char *other, size_t capacity, bool *is_sd) {
    (void)current; (void)other; (void)capacity; (void)is_sd; abort();
}
bool ps_reader_records_import(const char *source, ps_reader_records_import_result_t *result) {
    (void)source; (void)result; abort();
}
void ps_toast(const char *message) { (void)message; abort(); }
esp_err_t read_pico_sd_get_info(read_pico_sd_info_t *out) { (void)out; abort(); }
esp_err_t read_pico_sd_format(void) { abort(); }
esp_err_t read_pico_sd_verify(void) { abort(); }
esp_err_t read_pico_sd_remount(void) { abort(); }
int xTaskCreate(void (*fn)(void*), const char *name, unsigned stack, void *arg, int priority, void *handle) {
    (void)fn; (void)name; (void)stack; (void)arg; (void)priority; (void)handle; abort();
}
void vTaskDelete(void *task) { (void)task; abort(); }

static int pixel(int value) { return (value * 19 + 5) / 10; }
static bool overlap(rect_t a, rect_t b) {
    return a.w && a.h && b.w && b.h && a.x < b.x + b.w && a.x + a.w > b.x &&
           a.y < b.y + b.h && a.y + a.h > b.y;
}
static bool contains(rect_t outer, rect_t inner) {
    return inner.x >= outer.x && inner.y >= outer.y &&
           inner.x + inner.w <= outer.x + outer.w && inner.y + inner.h <= outer.y + outer.h;
}
static void check(bool good, const char *what, const char *detail) {
    checks++;
    if (good) return;
    failures++;
    fprintf(stderr, "FAIL %s %s: %s (%s)\n", scene_name,
            test_view.landscape ? "landscape" : "portrait", what, detail);
}

bool storage_font_has_codepoint(uint32_t codepoint);
static void check_glyphs(const char *text) {
    const unsigned char *s = (const unsigned char*)text;
    while (*s) {
        uint32_t codepoint;
        int bytes;
        if (s[0] < 0x80) { codepoint = s[0]; bytes = 1; }
        else if (s[0] < 0xe0) { codepoint = s[0] & 31; bytes = 2; }
        else if (s[0] < 0xf0) { codepoint = s[0] & 15; bytes = 3; }
        else { codepoint = s[0] & 7; bytes = 4; }
        for (int i = 1; i < bytes; i++) codepoint = (codepoint << 6) | (s[i] & 63);
        checks++;
        if (!storage_font_has_codepoint(codepoint)) {
            bool known = false;
            for (int i = 0; i < missing_count; i++) if (missing_codepoints[i] == codepoint) known = true;
            if (!known) {
                assert(missing_count < (int)(sizeof(missing_codepoints) / sizeof(missing_codepoints[0])));
                missing_codepoints[missing_count++] = codepoint;
                char description[64];
                snprintf(description, sizeof(description), "U+%04X %.*s", (unsigned)codepoint, bytes, s);
                check(false, "font missing glyph", description);
            }
        }
        s += bytes;
    }
}

void storage_native_draw_text_px(uint8_t*, int, int, int, const char*, enum EpdFontFlags, uint8_t, uint8_t);
void ttf_draw_text_px(uint8_t *fb, int x, int y, int size, const char *text,
                      enum EpdFontFlags alignment, uint8_t foreground, uint8_t background) {
    const int width = pixel(test_view.w), height = pixel(test_view.h);
    const size_t bytes = (size_t)width * height;
    uint8_t *before = malloc(bytes);
    assert(before && text_count < (int)(sizeof(text_records) / sizeof(text_records[0])));
    memcpy(before, fb, bytes);
    storage_native_draw_text_px(fb, x, y, size, text, alignment, foreground, background);
    int minx = width, miny = height, maxx = -1, maxy = -1;
    for (int row = 0; row < height; row++) for (int col = 0; col < width; col++) {
        size_t offset = (size_t)row * width + col;
        if (before[offset] != fb[offset]) {
            if (col < minx) minx = col;
            if (col > maxx) maxx = col;
            if (row < miny) miny = row;
            if (row > maxy) maxy = row;
        }
    }
    free(before);
    text_record_t *record = &text_records[text_count++];
    record->ink = maxx >= minx ? (rect_t){minx, miny, maxx - minx + 1, maxy - miny + 1} : (rect_t){0};
    record->button = active_button;
    snprintf(record->text, sizeof(record->text), "%s", text);
    check_glyphs(text);
    int expected_x = x, advance = ttf_text_width_px(size, text);
    if (alignment & EPD_DRAW_ALIGN_CENTER) expected_x -= advance / 2;
    else if (alignment & EPD_DRAW_ALIGN_RIGHT) expected_x -= advance;
    check(expected_x >= 0 && expected_x + advance <= width &&
          y - ttf_ascender_px(size) >= 0 && y - ttf_ascender_px(size) + size <= height,
          "text metrics outside screen", text);
    check(record->ink.w > 0 && record->ink.h > 0, "text produced no visible ink", text);
}

static void traced_button(uint8_t *fb, int x, int y, int w, int h, const char *label, bool selected) {
    assert(button_count < (int)(sizeof(buttons) / sizeof(buttons[0])));
    active_button = button_count;
    buttons[button_count++] = (rect_t){pixel(x), pixel(y), pixel(w), pixel(h)};
    ps_button(fb, x, y, w, h, label, selected);
    active_button = -1;
}
static void traced_icon(uint8_t *fb, int x, int y, int size, ps_icon_t icon, int gray) {
    assert(icon_count < (int)(sizeof(icons) / sizeof(icons[0])));
    icons[icon_count++] = (rect_t){pixel(x), pixel(y), pixel(size), pixel(size)};
    ps_icon(fb, x, y, size, icon, gray);
}
static void traced_wrap(uint8_t *fb, int x, int y, int w, int size, int lines, const char *text, int gray) {
    int start = text_count;
    ps_wrap(fb, x, y, w, size, lines, text, gray);
    char expected[1536], actual[1536];
    size_t n = 0;
    for (const char *p = text; *p && n + 1 < sizeof(expected); p++) if (*p != '\n') expected[n++] = *p;
    expected[n] = 0;
    n = 0;
    for (int i = start; i < text_count; i++) {
        size_t len = strlen(text_records[i].text);
        assert(n + len < sizeof(actual));
        memcpy(actual + n, text_records[i].text, len);
        n += len;
    }
    actual[n] = 0;
    check(!strcmp(expected, actual), "wrapped text was truncated", text);
}

// 页面原文件直接编译；宏仅记录控件边界，不改变真实控件、排版或状态文字。
// Compile the original page directly; macros record bounds without changing real controls, layout or text.
#define ps_button traced_button
#define ps_icon traced_icon
#define ps_wrap traced_wrap
#include "pico_storage.c"
#undef ps_wrap
#undef ps_icon
#undef ps_button

static void reset_scene(void) {
    memset(&sd_info, 0, sizeof(sd_info));
    memset(&sd_confirm, 0, sizeof(sd_confirm));
    sd_view = sd_action = sd_work = 0;
    sd_running = false;
    sd_message[0] = 0;
    storage_target = "笔记存储：暂不可用；小设置保存在设备内";
}
static void valid_card(void) {
    sd_info.present = sd_info.mounted = true;
    sd_info.capacity_bytes = 32000000000ULL;
    sd_info.free_bytes = 23600000000ULL;
    sd_info.error = ESP_OK;
    sd_info.state = READ_PICO_SD_STATE_READY; sd_info.stage = READ_PICO_SD_STAGE_COMPLETE; sd_info.filesystem = READ_PICO_SD_FS_FAT;
    sd_info.physical_bytes = sd_info.capacity_bytes; sd_info.media_generation = 1;
    sd_info.read_verified = sd_info.write_verified = true;
    snprintf(sd_info.name, sizeof(sd_info.name), "SD32G");
    sd_confirm = sd_info;
}
static void save_scene(const char *dir, const char *name) {
    scene_name = name;
    text_count = button_count = icon_count = 0;
    active_button = -1;
    uint8_t *fb = pico_fb_new();
    assert(fb);
    unsigned before = failures;
    ps_storage_render(fb);
    rect_t screen = {0, 0, pixel(test_view.w), pixel(test_view.h)};
    for (int i = 0; i < text_count; i++) {
        const text_record_t *a = &text_records[i];
        check(contains(screen, a->ink), "text ink outside screen", a->text);
        for (int j = i + 1; j < text_count; j++) {
            if (overlap(a->ink, text_records[j].ink)) {
                char pair[1600];
                snprintf(pair, sizeof(pair), "%s / %s", a->text, text_records[j].text);
                check(false, "text ink boxes overlap", pair);
            } else checks++;
        }
        for (int j = 0; j < button_count; j++) {
            if (a->button == j) check(contains(buttons[j], a->ink), "button label outside control", a->text);
            else check(!overlap(a->ink, buttons[j]), "body text overlaps button", a->text);
        }
        for (int j = 0; j < icon_count; j++) check(!overlap(a->ink, icons[j]), "text overlaps icon", a->text);
    }
    for (int i = 0; i < button_count; i++) check(contains(screen, buttons[i]), "button outside screen", "control");
    char path[640];
    snprintf(path, sizeof(path), "%s/%s-%s.pgm", dir, name, test_view.landscape ? "landscape" : "portrait");
    assert(pico_fb_save(path, fb));
    free(fb);
    fprintf(report, "%s\t%s\t%d\t%d\t%u\n", name,
            test_view.landscape ? "landscape" : "portrait", text_count, button_count, failures - before);
    scenes++;
}
int main(int argc, char **argv) {
    assert(argc == 3 && pico_fb_init(argv[1]));
    char path[640];
    snprintf(path, sizeof(path), "%s/geometry.tsv", argv[2]);
    report = fopen(path, "w");
    assert(report);
    fputs("scene\torientation\ttext_runs\tbuttons\tfailures\n", report);
    for (int landscape = 0; landscape < 2; landscape++) {
        test_view = landscape ? (ps_view_t){640, 360, true} : (ps_view_t){360, 640, false};
        reset_scene(); save_scene(argv[2], "status-absent");
        sd_info.present = true; sd_info.error = ESP_ERR_NOT_FINISHED; sd_info.state=READ_PICO_SD_STATE_DETECTING; save_scene(argv[2], "status-detecting");
        sd_info.error = ESP_FAIL; sd_info.state=READ_PICO_SD_STATE_FILESYSTEM_ERROR; save_scene(argv[2], "status-filesystem-error");
        sd_info.state=READ_PICO_SD_STATE_IO_ERROR; save_scene(argv[2], "status-io-error");
        sd_info.state=READ_PICO_SD_STATE_CHECK_FAILED; save_scene(argv[2], "status-check-failed");
        sd_info.state=READ_PICO_SD_STATE_UNSUPPORTED_FILESYSTEM; sd_info.physical_bytes=32000000000ULL; sd_info.filesystem=READ_PICO_SD_FS_EXFAT; save_scene(argv[2], "status-unsupported");
        sd_info.state=READ_PICO_SD_STATE_NEEDS_REMOUNT; sd_info.physical_bytes=0; storage_target="笔记存储：TF 卡暂不可用"; save_scene(argv[2], "status-needs-remount");
        sd_info.state=READ_PICO_SD_STATE_EXPORTED; save_scene(argv[2], "status-exported");
        valid_card(); storage_target="笔记存储：TF 卡"; sd_info.read_verified=sd_info.write_verified=false; save_scene(argv[2], "status-mounted-unverified");
        sd_info.read_verified=sd_info.write_verified=true; save_scene(argv[2], "status-verified");
        sd_info.free_bytes=0; save_scene(argv[2], "status-full"); valid_card();
        sd_view=SD_BACKUP; save_scene(argv[2], "confirm-backup");
        sd_view=SD_ERASE; save_scene(argv[2], "confirm-erase");
        sd_confirm.capacity_bytes=sd_confirm.physical_bytes=0; sd_view=SD_BACKUP; save_scene(argv[2], "confirm-unknown-card");
        valid_card(); sd_view=SD_OVERVIEW; sd_running=true; sd_action=SD_CHECK; sd_started=false; sd_work=SD_WORK_REMOUNT; save_scene(argv[2], "processing-prepare");
        sd_started=true;
        for(int stage=READ_PICO_SD_STAGE_DETECTING;stage<=READ_PICO_SD_STAGE_FAILED;stage++) {
            sd_info.stage=(read_pico_sd_stage_t)stage; sd_action=stage==READ_PICO_SD_STAGE_FORMATTING?SD_FORMAT:SD_CHECK;
            char name[64]; snprintf(name,sizeof(name),"processing-stage-%d",stage); save_scene(argv[2],name);
        }
        sd_running=false; sd_view=SD_RESULT; sd_info.state=READ_PICO_SD_STATE_CHECK_FAILED; sd_info.read_verified=sd_info.write_verified=false;
        snprintf(sd_message,sizeof(sd_message),"检测或读写验证未完成。卡内文件未被格式化，请查看状态后重新检测。"); save_scene(argv[2],"result-check-failed");
        sd_info.state=READ_PICO_SD_STATE_IO_ERROR; sd_info.format_started=true;
        snprintf(sd_message,sizeof(sd_message),"格式化未完成，卡内文件可能已被清除。未自动重试，请先重新检测。"); save_scene(argv[2],"result-format-failed");
        snprintf(sd_message,sizeof(sd_message),"格式化未开始或未完成。未自动重试，请检查卡片后重新检测。"); save_scene(argv[2],"result-not-started");
        snprintf(sd_message,sizeof(sd_message),"无法暂停文件访问，未开始存储操作。请稍后重试。"); save_scene(argv[2],"result-prepare-failed");
        valid_card(); sd_view=SD_RESULT;
        snprintf(sd_message,sizeof(sd_message),"读写验证通过，笔记存储尚未启用。卡内已有数据时请重启读取。"); save_scene(argv[2],"result-rebind-failed");
        snprintf(sd_message,sizeof(sd_message),"格式化完成，读写验证通过。"); save_scene(argv[2],"result-format-success");
        snprintf(sd_message,sizeof(sd_message),"检测完成，读写验证通过。"); save_scene(argv[2],"result-check-success");
        sd_view=SD_RECORDS; sd_records_available=true; sd_records_current_is_sd=true;
        save_scene(argv[2],"records-to-tf");
        sd_records_current_is_sd=false; save_scene(argv[2],"records-to-internal");
        sd_records_available=false; sd_records_current_available=false;save_scene(argv[2],"records-unavailable");
        sd_records_current_available=true;sd_records_current_is_sd=true;save_scene(argv[2],"records-only-tf");
        sd_records_current_is_sd=false;save_scene(argv[2],"records-only-internal");
        sd_view=SD_IMPORT_PROGRESS; save_scene(argv[2],"records-merging");
        sd_view=SD_IMPORT_RESULT;
        static const char *messages[]={
            "新增 1024 条 · 跳过重复 1024 条 · 当前共 1024 条。源库保留。",
            "另一处存储没有可合并的笔记。当前记录保留。",
            "原有记录读取异常，未覆盖任何记录。请先检查存储。",
            "合并后超过1024条，未合并。请先导出并移除不需要的记录。",
            "可用内存不足，未合并。原记录保留，请重启后再试。",
            "目标存储写入失败，未完成合并。原记录保留。",
            "请先保存正在编辑的笔记，再合并记录。",
            "存储状态已改变或暂不可用，未合并。请检查TF卡后重新进入。"
        };
        for(size_t i=0;i<sizeof(messages)/sizeof(messages[0]);i++){
            sd_import_result.status=i?PS_READER_IMPORT_UNAVAILABLE:PS_READER_IMPORT_OK;
            snprintf(sd_message,sizeof(sd_message),"%s",messages[i]);
            char name[64];snprintf(name,sizeof(name),"records-result-%zu",i);save_scene(argv[2],name);
        }
    }
    fclose(report);
    pico_fb_close();
    printf("TF actual-source/actual-font render: %u scenes, %u ink/bounds/wrap/glyph checks, %u failures, %d missing codepoints; no workers, card access or formatting\n", scenes, checks, failures, missing_count);
    return failures ? 1 : 0;
}

/* Host-only fault injection over the production MSC adapter. No hardware. */
#define _POSIX_C_SOURCE 200809L
#include "pico_usb_host_env.h"
#include "read_pico_usb.h"
#include "read_pico_sd.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static unsigned checks;
#define CHECK(value) do { ++checks; if (!(value)) { fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #value); abort(); } } while (0)
#define MOCK_REQUIRE(value) do { if (!(value)) { fprintf(stderr, "mock boundary: %s:%d: %s\n", __FILE__, __LINE__, #value); abort(); } } while (0)
static void pause_ms(unsigned ms) { struct timespec t = {ms / 1000, (long)(ms % 1000) * 1000000}; nanosleep(&t, NULL); }
static struct timespec after_ms(unsigned ms) {
    struct timespec t; clock_gettime(CLOCK_REALTIME, &t);
    t.tv_nsec += (long)(ms % 1000) * 1000000; t.tv_sec += ms / 1000 + t.tv_nsec / 1000000000;
    t.tv_nsec %= 1000000000; return t;
}

struct mock_semaphore { pthread_mutex_t lock; pthread_cond_t cond; unsigned value; };
static unsigned semaphore_calls, fail_semaphore_call;
static SemaphoreHandle_t new_semaphore(unsigned initial) {
    if (++semaphore_calls == fail_semaphore_call) return NULL;
    SemaphoreHandle_t s = calloc(1, sizeof(*s)); MOCK_REQUIRE(s);
    pthread_mutex_init(&s->lock, NULL); pthread_cond_init(&s->cond, NULL); s->value = initial; return s;
}
SemaphoreHandle_t xSemaphoreCreateMutex(void) { return new_semaphore(1); }
SemaphoreHandle_t xSemaphoreCreateBinary(void) { return new_semaphore(0); }
void vSemaphoreDelete(SemaphoreHandle_t s) { pthread_mutex_destroy(&s->lock); pthread_cond_destroy(&s->cond); free(s); }
int xSemaphoreTake(SemaphoreHandle_t s, unsigned ticks) {
    pthread_mutex_lock(&s->lock); struct timespec end = after_ms(ticks);
    while (!s->value) {
        if (!ticks || pthread_cond_timedwait(&s->cond, &s->lock, &end) == ETIMEDOUT) { pthread_mutex_unlock(&s->lock); return 0; }
    }
    --s->value; pthread_mutex_unlock(&s->lock); return pdTRUE;
}
int xSemaphoreGive(SemaphoreHandle_t s) {
    pthread_mutex_lock(&s->lock); s->value = 1; pthread_cond_signal(&s->cond); pthread_mutex_unlock(&s->lock); return pdTRUE;
}

static pthread_mutex_t mock_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t mock_cond = PTHREAD_COND_INITIALIZER;
static bool fail_alloc, fail_mac, fail_task, fail_init, fail_init_leaves_stack, core_inited;
static unsigned init_delay_ms, deinit_failures, task_count;
static unsigned alloc_count, raw_acquires, raw_releases, read_calls, write_calls, disconnect_calls;
static unsigned info_calls, max_read_sectors, max_write_sectors;
static bool remove_after_read;
static bool raw_owned, present = true, fail_raw_acquire, fail_raw_release, fail_read, fail_write;
static unsigned fail_new_controller, fail_delete_controller;
static uint32_t generation = 1;
static esp_err_t info_error;
static bool block_write, write_entered, callback_done;
static bool callback_pending;
static void (*pending_callback)(void);
static int32_t callback_result;
static uint8_t sense_key, sense_asc;
static uint8_t disk[32 * 512];
static sdmmc_card_t card = {.csd = {.capacity = 32, .sector_size = 512}};
struct mock_phy { int controller; };
static usb_phy_handle_t current_phy;

void *heap_caps_malloc(size_t size, unsigned caps) {
    MOCK_REQUIRE(size == 4096 && caps == (MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL));
    if (fail_alloc) return NULL;
    void *p = malloc(size); if (p) ++alloc_count; return p;
}
void heap_caps_free(void *p) { if (p) { MOCK_REQUIRE(alloc_count); --alloc_count; free(p); } }
esp_err_t esp_read_mac(uint8_t *bytes, int type) {
    MOCK_REQUIRE(type == ESP_MAC_WIFI_STA); if (fail_mac) return ESP_FAIL;
    const uint8_t mac[] = {0x12, 0x34, 0x56, 0x78, 0x9a, 0xbc}; memcpy(bytes, mac, 6); return ESP_OK;
}
esp_err_t usb_new_phy(const usb_phy_config_t *config, usb_phy_handle_t *out) {
    MOCK_REQUIRE(!current_phy && config->target == USB_PHY_TARGET_INT);
    if (fail_new_controller == (unsigned)config->controller) { fail_new_controller = 0; return ESP_FAIL; }
    if (config->controller == USB_PHY_CTRL_OTG)
        MOCK_REQUIRE(config->otg_mode == USB_OTG_MODE_DEVICE && config->otg_speed == USB_PHY_SPEED_FULL);
    current_phy = malloc(sizeof(*current_phy)); MOCK_REQUIRE(current_phy);
    current_phy->controller = config->controller; *out = current_phy; return ESP_OK;
}
esp_err_t usb_del_phy(usb_phy_handle_t phy) {
    MOCK_REQUIRE(phy && phy == current_phy);
    if (fail_delete_controller == (unsigned)phy->controller) { fail_delete_controller = 0; return ESP_FAIL; }
    pthread_mutex_lock(&mock_lock); MOCK_REQUIRE(!core_inited); pthread_mutex_unlock(&mock_lock);
    free(phy); current_phy = NULL; return ESP_OK;
}
esp_err_t read_pico_sd_raw_acquire(sdmmc_card_t **out) {
    *out = NULL; ++raw_acquires;
    MOCK_REQUIRE(!raw_owned); if (fail_raw_acquire) return ESP_FAIL;
    raw_owned = true; *out = &card; return ESP_OK;
}
esp_err_t read_pico_sd_raw_release(void) {
    ++raw_releases; MOCK_REQUIRE(raw_owned);
    pthread_mutex_lock(&mock_lock); MOCK_REQUIRE(!core_inited && !write_entered); pthread_mutex_unlock(&mock_lock);
    MOCK_REQUIRE(current_phy && current_phy->controller == USB_PHY_CTRL_SERIAL_JTAG);
    if (fail_raw_release) return ESP_FAIL;
    raw_owned = false; return ESP_OK;
}
esp_err_t read_pico_sd_get_info(read_pico_sd_info_t *info) {
    ++info_calls;
    *info = (read_pico_sd_info_t){.present = present, .exported = raw_owned,
        .state = raw_owned ? READ_PICO_SD_STATE_EXPORTED : READ_PICO_SD_STATE_READY,
        .media_generation = generation, .error = info_error};
    return info_error;
}
esp_err_t sdmmc_read_sectors(sdmmc_card_t *p, void *out, size_t first, size_t count) {
    MOCK_REQUIRE(p == &card && raw_owned && first + count <= 32 && count >= 1 && count <= 8);
    ++read_calls; if (fail_read) return ESP_FAIL;
    if (count > max_read_sectors) max_read_sectors = count;
    memcpy(out, disk + first * 512, count * 512);
    if (remove_after_read) { remove_after_read = false; present = false; info_error = ESP_ERR_NOT_FOUND; }
    return ESP_OK;
}
esp_err_t sdmmc_write_sectors(sdmmc_card_t *p, const void *in, size_t first, size_t count) {
    MOCK_REQUIRE(p == &card && raw_owned && first + count <= 32 && count >= 1 && count <= 8);
    pthread_mutex_lock(&mock_lock);
    if (block_write) {
        write_entered = true; pthread_cond_broadcast(&mock_cond);
        while (block_write) pthread_cond_wait(&mock_cond, &mock_lock);
        write_entered = false;
    }
    pthread_mutex_unlock(&mock_lock);
    ++write_calls; if (fail_write) return ESP_FAIL;
    if (count > max_write_sectors) max_write_sectors = count;
    memcpy(disk + first * 512, in, count * 512); return ESP_OK;
}

struct task_args { void (*entry)(void *); void *arg; };
static void task_finished(void *unused) {
    (void)unused; pthread_mutex_lock(&mock_lock); --task_count;
    pthread_cond_broadcast(&mock_cond); pthread_mutex_unlock(&mock_lock);
}
static void *task_main(void *raw) {
    struct task_args args = *(struct task_args *)raw; free(raw);
    pthread_cleanup_push(task_finished, NULL);
    args.entry(args.arg);
    pthread_cleanup_pop(1); return NULL;
}
int xTaskCreatePinnedToCore(void (*entry)(void *), const char *name, unsigned stack, void *arg, unsigned priority, void *handle, int core) {
    (void)handle; MOCK_REQUIRE(name && stack >= 4096 && priority == 5 && core == 0);
    if (fail_task) return 0;
    struct task_args *a = malloc(sizeof(*a)); MOCK_REQUIRE(a); *a = (struct task_args){entry, arg};
    pthread_t thread; pthread_mutex_lock(&mock_lock); ++task_count; pthread_mutex_unlock(&mock_lock);
    MOCK_REQUIRE(!pthread_create(&thread, NULL, task_main, a)); pthread_detach(thread); return pdPASS;
}
void vTaskDelete(void *task) { MOCK_REQUIRE(!task); pthread_exit(NULL); }
bool tusb_rhport_init(uint8_t port, const tusb_rhport_init_t *config) {
    MOCK_REQUIRE(port == 0 && config->role == TUSB_ROLE_DEVICE && config->speed == TUSB_SPEED_FULL);
    MOCK_REQUIRE(current_phy && current_phy->controller == USB_PHY_CTRL_OTG);
    if (init_delay_ms) pause_ms(init_delay_ms);
    pthread_mutex_lock(&mock_lock); core_inited = !fail_init || fail_init_leaves_stack; pthread_mutex_unlock(&mock_lock);
    if (!fail_init) tud_mount_cb(); return !fail_init;
}
bool tud_inited(void) { pthread_mutex_lock(&mock_lock); bool result = core_inited; pthread_mutex_unlock(&mock_lock); return result; }
bool tud_deinit(uint8_t port) {
    MOCK_REQUIRE(port == 0);
    pthread_mutex_lock(&mock_lock);
    if (deinit_failures) { --deinit_failures; pthread_mutex_unlock(&mock_lock); return false; }
    core_inited = false; pthread_mutex_unlock(&mock_lock); tud_umount_cb(); return true;
}
void tud_disconnect(void) { ++disconnect_calls; }
void tud_task_ext(uint32_t wait, bool isr) {
    MOCK_REQUIRE(wait == 50 && !isr);
    pthread_mutex_lock(&mock_lock);
    if (!callback_pending) { struct timespec end = after_ms(2); pthread_cond_timedwait(&mock_cond, &mock_lock, &end); }
    void (*call)(void) = callback_pending ? pending_callback : NULL;
    callback_pending = false; pthread_mutex_unlock(&mock_lock);
    if (call) {
        call(); pthread_mutex_lock(&mock_lock); callback_done = true;
        pthread_cond_broadcast(&mock_cond); pthread_mutex_unlock(&mock_lock);
    }
}
bool tud_msc_set_sense(uint8_t lun, uint8_t key, uint8_t asc, uint8_t qualifier) {
    (void)lun; MOCK_REQUIRE(!qualifier); sense_key = key; sense_asc = asc; return true;
}

static read_pico_usb_status_t status(void) { read_pico_usb_status_t s; read_pico_usb_get_status(&s); return s; }
static void await_no_tasks(void) {
    pthread_mutex_lock(&mock_lock); struct timespec end = after_ms(1000);
    while (task_count) MOCK_REQUIRE(pthread_cond_timedwait(&mock_cond, &mock_lock, &end) != ETIMEDOUT);
    pthread_mutex_unlock(&mock_lock);
}
static void reset_faults(void) {
    await_no_tasks(); fail_alloc = fail_mac = fail_task = fail_init = fail_init_leaves_stack = false;
    fail_raw_acquire = fail_raw_release = fail_read = fail_write = false;
    fail_new_controller = fail_delete_controller = init_delay_ms = deinit_failures = 0;
    present = true; info_error = ESP_OK; card.csd.capacity = 32; card.csd.sector_size = 512;
    remove_after_read = false;
    CHECK(!raw_owned && !alloc_count && !tud_inited());
}
static void expect_clean(void) {
    await_no_tasks(); read_pico_usb_status_t s = status();
    CHECK(!s.active && !s.card_exported && s.debug_restored && !s.connected && !s.writing);
    CHECK(!raw_owned && !alloc_count && !tud_inited());
    CHECK(current_phy && current_phy->controller == USB_PHY_CTRL_SERIAL_JTAG);
}
static void start_clean(void) {
    CHECK(read_pico_usb_start() == ESP_OK);
    read_pico_usb_status_t s = status(); CHECK(s.state == READ_PICO_USB_EXPORTED);
    CHECK(s.active && s.card_exported && s.connected && !s.debug_restored && !s.host_ejected);
    CHECK(raw_owned && alloc_count == 1 && tud_inited());
}
static void enqueue(void (*callback)(void)) {
    pthread_mutex_lock(&mock_lock); MOCK_REQUIRE(!callback_pending);
    callback_done = false; pending_callback = callback; callback_pending = true;
    pthread_cond_broadcast(&mock_cond); pthread_mutex_unlock(&mock_lock);
}
static void await_callback(void) {
    pthread_mutex_lock(&mock_lock); struct timespec end = after_ms(1000);
    while (!callback_done) MOCK_REQUIRE(pthread_cond_timedwait(&mock_cond, &mock_lock, &end) != ETIMEDOUT);
    pthread_mutex_unlock(&mock_lock);
}
static uint8_t async_buffer[8192];
static void async_write(void) { callback_result = tud_msc_write10_cb(0, 2, 0, async_buffer, sizeof(async_buffer)); }
static void unblock(void) { pthread_mutex_lock(&mock_lock); block_write = false; pthread_cond_broadcast(&mock_cond); pthread_mutex_unlock(&mock_lock); }

static void test_data_and_protocol(void) {
    for (size_t i = 0; i < sizeof(disk); ++i) disk[i] = (uint8_t)(i * 13 + i / 512);
    start_clean(); CHECK(read_pico_usb_start() == ESP_ERR_INVALID_STATE);
    CHECK(status().active && raw_owned);
    const tusb_desc_device_t *dev = (const tusb_desc_device_t *)tud_descriptor_device_cb();
    CHECK(dev->bLength == 18 && dev->idVendor == 0x303a && dev->idProduct == 0x4002 && dev->bNumConfigurations == 1);
    const uint8_t *config = tud_descriptor_configuration_cb(0);
    CHECK(config && config[2] == 32 && config[4] == 1 && config[14] == 8);
    CHECK(!tud_descriptor_configuration_cb(1) && !tud_descriptor_string_cb(9, 0));
    const uint16_t *text = tud_descriptor_string_cb(0, 0); CHECK(text[0] == 0x0304 && text[1] == 0x0409);
    text = tud_descriptor_string_cb(3, 0); const char *serial = "123456789ABC";
    CHECK((text[0] & 255) == 26); for (size_t i = 0; i < 12; ++i) CHECK(text[i + 1] == serial[i]);
    uint8_t vendor[8], product[16], revision[4]; tud_msc_inquiry_cb(0, vendor, product, revision);
    CHECK(!memcmp(vendor, "MindRset", 8) && !memcmp(product, "Read Pico TF    ", 16) && !memcmp(revision, "0100", 4));
    CHECK(tud_msc_get_maxlun_cb() == 1 && tud_msc_test_unit_ready_cb(0) && tud_msc_is_writable_cb(0));
    CHECK(!tud_msc_test_unit_ready_cb(1));
    uint32_t blocks; uint16_t size; tud_msc_capacity_cb(0, &blocks, &size); CHECK(blocks == 32 && size == 512);
    tud_msc_capacity_cb(1, &blocks, &size); CHECK(!blocks && size == 512);
    uint8_t output[1200] = {0}, before[sizeof(disk)];
    CHECK(tud_msc_read10_cb(0, 1, 0, output, 1024) == 1024 && !memcmp(output, disk + 512, 1024));
    CHECK(tud_msc_read10_cb(0, 1, 499, output, 700) == 700 && !memcmp(output, disk + 1011, 700));
    CHECK(tud_msc_read10_cb(0, 0, 1024, output, 1) == 1 && output[0] == disk[1024]);
    memcpy(before, disk, sizeof(disk)); memset(output, 0xa5, sizeof(output));
    unsigned reads_before = read_calls;
    CHECK(tud_msc_write10_cb(0, 3, 0, output, 512) == 512 && read_calls == reads_before);
    CHECK(!memcmp(disk + 1536, output, 512) && !memcmp(disk, before, 1536) && !memcmp(disk + 2048, before + 2048, sizeof(disk) - 2048));
    memcpy(before, disk, sizeof(disk));
    CHECK(tud_msc_write10_cb(0, 5, 501, output, 700) == 700);
    CHECK(!memcmp(disk + 3061, output, 700) && !memcmp(disk, before, 3061) && !memcmp(disk + 3761, before + 3761, sizeof(disk) - 3761));
    memcpy(before, disk, sizeof(disk)); unsigned writes_before = write_calls;
    CHECK(tud_msc_write10_cb(0, 31, 500, output, 13) == -1);
    CHECK(tud_msc_write10_cb(0, UINT32_MAX, UINT32_MAX, output, 1) == -1);
    CHECK(tud_msc_write10_cb(0, 0, 0, NULL, 1) == -1 && tud_msc_write10_cb(0, 0, 0, output, 0) == -1);
    CHECK(tud_msc_write10_cb(0, 0, 0, output, UINT32_MAX) == -1 && write_calls == writes_before && !memcmp(before, disk, sizeof(disk)));
    CHECK(sense_key == SCSI_SENSE_ILLEGAL_REQUEST && sense_asc == 0x21);
    fail_read = true; CHECK(tud_msc_read10_cb(0, 0, 0, output, 20) == -1);
    CHECK(sense_key == SCSI_SENSE_MEDIUM_ERROR && sense_asc == 0x11 && !status().writing); fail_read = false;
    fail_write = true; CHECK(tud_msc_write10_cb(0, 0, 0, output, 512) == -1);
    CHECK(sense_key == SCSI_SENSE_MEDIUM_ERROR && sense_asc == 0x0c && !status().writing); fail_write = false;
    fail_read = true; writes_before = write_calls; CHECK(tud_msc_write10_cb(0, 0, 1, output, 1) == -1 && write_calls == writes_before); fail_read = false;
    uint8_t cmd[16] = {0x35}; CHECK(tud_msc_scsi_cb(0, cmd, NULL, 0) == 0);
    cmd[0] = 0x1e; CHECK(tud_msc_scsi_cb(0, cmd, NULL, 0) == 0);
    cmd[0] = 0x7f; CHECK(tud_msc_scsi_cb(0, cmd, NULL, 0) == -1 && sense_asc == 0x20);
    CHECK(tud_msc_start_stop_cb(0, 0, false, true)); CHECK(status().host_ejected && !tud_msc_test_unit_ready_cb(0));
    CHECK(!tud_msc_start_stop_cb(1, 0, true, true));
    CHECK(tud_msc_start_stop_cb(0, 0, true, true) && tud_msc_test_unit_ready_cb(0));
    unsigned releases_before = raw_releases; tud_umount_cb(); CHECK(!status().connected && raw_owned && raw_releases == releases_before);
    tud_mount_cb(); CHECK(status().connected);
    CHECK(status().read_blocks >= 4 && status().written_blocks == 4);
    CHECK(read_pico_usb_stop() == ESP_OK); expect_clean(); CHECK(status().state == READ_PICO_USB_IDLE);
    releases_before = raw_releases; CHECK(read_pico_usb_stop() == ESP_OK && raw_releases == releases_before);
}

static void test_media_invalidation(void) {
    reset_faults(); start_clean(); present = false; info_error = ESP_ERR_NOT_FOUND;
    CHECK(!tud_msc_test_unit_ready_cb(0) && status().last_error == ESP_ERR_NOT_FOUND);
    present = true; info_error = ESP_OK; ++generation;
    CHECK(!tud_msc_test_unit_ready_cb(0));
    CHECK(read_pico_usb_stop() == ESP_OK); expect_clean();
    reset_faults(); start_clean(); ++generation;
    CHECK(!tud_msc_test_unit_ready_cb(0));
    uint8_t value = 1; unsigned writes_before = write_calls;
    CHECK(tud_msc_write10_cb(0, 0, 0, &value, 1) == -1 && write_calls == writes_before);
    CHECK(status().active && raw_owned); CHECK(read_pico_usb_stop() == ESP_OK); expect_clean();
    reset_faults(); start_clean(); info_error = ESP_ERR_NOT_FINISHED;
    CHECK(!tud_msc_test_unit_ready_cb(0)); info_error = ESP_OK;
    CHECK(!tud_msc_test_unit_ready_cb(0)); CHECK(read_pico_usb_stop() == ESP_OK); expect_clean();
}

static void test_batched_io(void) {
    reset_faults();
    for (size_t i = 0; i < sizeof(disk); ++i) disk[i] = (uint8_t)(i * 13 + i / 512);
    start_clean();
    uint8_t output[9000], before[sizeof(disk)];
    unsigned reads_before = read_calls, info_before = info_calls;
    CHECK(tud_msc_read10_cb(0, 1, 0, output, 4096) == 4096);
    CHECK(!memcmp(output, disk + 512, 4096) && read_calls == reads_before + 1);
    CHECK(info_calls == info_before + 1 && max_read_sectors == 8);
    reads_before = read_calls; info_before = info_calls;
    CHECK(tud_msc_read10_cb(0, 1, 0, output, 8192) == 8192);
    CHECK(!memcmp(output, disk + 512, 8192) && read_calls == reads_before + 2);
    CHECK(info_calls == info_before + 2);
    reads_before = read_calls; info_before = info_calls;
    CHECK(tud_msc_read10_cb(0, 1, 499, output, 8192) == 8192);
    CHECK(!memcmp(output, disk + 1011, 8192) && read_calls == reads_before + 4);
    CHECK(info_calls == info_before + 4); // partial head, eight blocks, seven blocks, partial tail
    CHECK(tud_msc_read10_cb(0, 0, 4096, output, 4096) == 4096 && !memcmp(output, disk + 4096, 4096));
    memcpy(before, disk, sizeof(disk)); memset(output, 0xa7, sizeof(output));
    unsigned writes_before = write_calls; reads_before = read_calls; info_before = info_calls;
    CHECK(tud_msc_write10_cb(0, 2, 0, output, 8192) == 8192);
    CHECK(write_calls == writes_before + 2 && read_calls == reads_before && info_calls == info_before + 2);
    CHECK(max_write_sectors == 8 && !memcmp(disk + 1024, output, 8192));
    CHECK(!memcmp(disk, before, 1024) && !memcmp(disk + 9216, before + 9216, sizeof(disk) - 9216));
    memcpy(before, disk, sizeof(disk)); writes_before = write_calls; reads_before = read_calls;
    CHECK(tud_msc_write10_cb(0, 2, 499, output, 8192) == 8192);
    CHECK(write_calls == writes_before + 4 && read_calls == reads_before + 2);
    CHECK(!memcmp(disk + 1523, output, 8192));
    CHECK(!memcmp(disk, before, 1523) && !memcmp(disk + 9715, before + 9715, sizeof(disk) - 9715));
    CHECK(tud_msc_read10_cb(0, 24, 0, output, 4096) == 4096 && !memcmp(output, disk + 12288, 4096));
    reads_before = read_calls;
    CHECK(tud_msc_read10_cb(0, 24, 0, output, 4097) == -1 && read_calls == reads_before);
    reads_before = read_calls; info_before = info_calls;
    for (unsigned i = 0; i < 4; ++i) {
        CHECK(tud_msc_read10_cb(0, i * 8, 0, output, 4096) == 4096);
        CHECK(!memcmp(output, disk + i * 4096, 4096));
    }
    CHECK(read_calls == reads_before + 4 && info_calls == info_before + 4);
    memcpy(before, disk, sizeof(disk)); fail_write = true;
    CHECK(tud_msc_write10_cb(0, 2, 0, output, 4096) == -1 && !memcmp(disk, before, sizeof(disk)));
    CHECK(sense_key == SCSI_SENSE_MEDIUM_ERROR && sense_asc == 0x0c && !status().writing);
    fail_write = false; fail_read = true;
    CHECK(tud_msc_read10_cb(0, 2, 0, output, 4096) == -1);
    CHECK(sense_key == SCSI_SENSE_MEDIUM_ERROR && sense_asc == 0x11 && !status().writing);
    fail_read = false;
    CHECK(tud_msc_test_unit_ready_cb(0)); // an SD I/O error is reported without fabricating card removal
    CHECK(read_pico_usb_stop() == ESP_OK); expect_clean();

    reset_faults(); start_clean(); memset(output, 0x6b, sizeof(output));
    reads_before = read_calls; remove_after_read = true;
    CHECK(tud_msc_read10_cb(0, 1, 0, output, 8192) == -1);
    CHECK(read_calls == reads_before + 1 && !memcmp(output, disk + 512, 4096));
    for (unsigned i = 4096; i < 8192; ++i) CHECK(output[i] == 0x6b);
    CHECK(status().last_error != ESP_OK && sense_key == SCSI_SENSE_NOT_READY && sense_asc == 0x3a);
    present = true; info_error = ESP_OK; ++generation;
    CHECK(!tud_msc_test_unit_ready_cb(0)); // reinsertion cannot revive a revoked raw lease
    CHECK(read_pico_usb_stop() == ESP_OK); expect_clean();
    puts("PASS 4096-byte aligned read: 1 raw eight-sector read + 1 presence query; bounded batches preserve partial bytes, revocation and synchronous errors");
}

static void test_start_failures(void) {
    reset_faults(); fail_alloc = true; CHECK(read_pico_usb_start() == ESP_ERR_NO_MEM); CHECK(!raw_owned && !alloc_count);
    reset_faults(); fail_raw_acquire = true; CHECK(read_pico_usb_start() == ESP_FAIL); CHECK(!raw_owned && !alloc_count);
    reset_faults(); card.csd.sector_size = 1024; CHECK(read_pico_usb_start() == ESP_ERR_NOT_SUPPORTED); expect_clean();
    reset_faults(); card.csd.capacity = 0; CHECK(read_pico_usb_start() == ESP_ERR_NOT_SUPPORTED); expect_clean();
    reset_faults(); card.csd.capacity = (uint64_t)UINT32_MAX + 1; CHECK(read_pico_usb_start() == ESP_ERR_NOT_SUPPORTED); expect_clean();
    reset_faults(); fail_mac = true; CHECK(read_pico_usb_start() == ESP_FAIL); expect_clean();
    reset_faults(); info_error = ESP_ERR_NOT_FINISHED; CHECK(read_pico_usb_start() == ESP_ERR_NOT_FINISHED); expect_clean();
    reset_faults(); fail_delete_controller = USB_PHY_CTRL_SERIAL_JTAG; CHECK(read_pico_usb_start() == ESP_FAIL); expect_clean();
    reset_faults(); fail_new_controller = USB_PHY_CTRL_OTG; CHECK(read_pico_usb_start() == ESP_FAIL); expect_clean();
    reset_faults(); fail_task = true; CHECK(read_pico_usb_start() == ESP_ERR_NO_MEM); expect_clean();
    reset_faults(); fail_init = true; CHECK(read_pico_usb_start() == ESP_FAIL); expect_clean();
    reset_faults(); fail_init = fail_init_leaves_stack = true; CHECK(read_pico_usb_start() == ESP_FAIL); expect_clean();
    reset_faults(); init_delay_ms = 140; CHECK(read_pico_usb_start() == ESP_ERR_TIMEOUT); expect_clean();
    reset_faults(); init_delay_ms = 500; CHECK(read_pico_usb_start() == ESP_ERR_TIMEOUT);
    CHECK(status().active && raw_owned && !status().debug_restored && alloc_count == 1);
    await_no_tasks(); CHECK(read_pico_usb_stop() == ESP_OK); expect_clean();
}

static void test_retryable_stop_failures(void) {
    reset_faults(); start_clean(); deinit_failures = 2;
    CHECK(read_pico_usb_stop() == ESP_FAIL);
    CHECK(status().active && raw_owned && !status().debug_restored && tud_inited());
    CHECK(!tud_msc_test_unit_ready_cb(0)); CHECK(read_pico_usb_start() == ESP_ERR_INVALID_STATE);
    CHECK(read_pico_usb_stop() == ESP_OK); expect_clean();
    reset_faults(); start_clean(); fail_delete_controller = USB_PHY_CTRL_OTG;
    CHECK(read_pico_usb_stop() == ESP_FAIL && !tud_inited());
    CHECK(status().active && raw_owned && !status().debug_restored);
    CHECK(read_pico_usb_stop() == ESP_OK); expect_clean();
    reset_faults(); start_clean(); fail_new_controller = USB_PHY_CTRL_SERIAL_JTAG;
    unsigned before = raw_releases; CHECK(read_pico_usb_stop() == ESP_FAIL && raw_releases == before);
    CHECK(status().active && raw_owned && !status().debug_restored && !current_phy);
    CHECK(read_pico_usb_stop() == ESP_OK); expect_clean();
    reset_faults(); start_clean(); fail_raw_release = true;
    CHECK(read_pico_usb_stop() == ESP_FAIL); CHECK(status().active && status().card_exported && status().debug_restored && raw_owned);
    uint8_t value = 0; CHECK(tud_msc_read10_cb(0, 0, 0, &value, 1) == -1);
    CHECK(read_pico_usb_start() == ESP_ERR_INVALID_STATE); fail_raw_release = false;
    CHECK(read_pico_usb_stop() == ESP_OK); expect_clean();
}

static void test_cooperative_write_stop(void) {
    reset_faults(); start_clean(); memset(async_buffer, 0xcc, sizeof(async_buffer));
    pthread_mutex_lock(&mock_lock); block_write = true; pthread_mutex_unlock(&mock_lock); enqueue(async_write);
    pthread_mutex_lock(&mock_lock); struct timespec end = after_ms(1000);
    while (!write_entered) MOCK_REQUIRE(pthread_cond_timedwait(&mock_cond, &mock_lock, &end) != ETIMEDOUT);
    pthread_mutex_unlock(&mock_lock);
    CHECK(status().writing); unsigned before = raw_releases;
    CHECK(read_pico_usb_stop() == ESP_ERR_TIMEOUT);
    CHECK(raw_releases == before && raw_owned && status().active && status().writing && !status().debug_restored);
    CHECK(tud_inited() && current_phy && current_phy->controller == USB_PHY_CTRL_OTG);
    CHECK(read_pico_usb_start() == ESP_ERR_INVALID_STATE);
    unblock(); await_callback(); CHECK(callback_result == -1 && !status().writing);
    /* Admission stops between bounded synchronous batches; the second batch is never written. */
    CHECK(!memcmp(disk + 1024, async_buffer, 4096) && memcmp(disk + 5120, async_buffer + 4096, 4096));
    CHECK(read_pico_usb_stop() == ESP_OK); expect_clean();
}

int main(void) {
    CHECK(read_pico_usb_start() == ESP_ERR_INVALID_STATE && read_pico_usb_stop() == ESP_ERR_INVALID_STATE);
    fail_semaphore_call = 2; CHECK(read_pico_usb_init() == ESP_ERR_NO_MEM);
    fail_semaphore_call = 0; CHECK(read_pico_usb_init() == ESP_OK && read_pico_usb_init() == ESP_OK);
    test_data_and_protocol(); test_media_invalidation(); test_batched_io(); test_start_failures(); test_retryable_stop_failures(); test_cooperative_write_stop();
    reset_faults(); for (unsigned i = 0; i < 12; ++i) { start_clean(); CHECK(read_pico_usb_stop() == ESP_OK); expect_clean(); }
    CHECK(disconnect_calls > 15 && raw_acquires > 30 && raw_releases > 25);
    printf("PASS %u assertions; actual MSC callbacks, exclusive leases, cooperative join and recovery fault injection\n", checks);
    return 0;
}

/* SPDX-License-Identifier: Apache-2.0
 * 中文：真实PMU产品动作消费器，注入队列分批、ACK失败和按住释放。
 * English: Real PMU product-key consumer under split queues, ACK faults and held releases.
 */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "pmu_api_under_test.h"

static pmu_snapshot_t s_snap;
static pmu_event_t queue[32];
static int count, at, next_id;
static bool poll_fail, ack_fail, ack_followup_fail;

esp_err_t read_pico_pmu_poll(void) {
    if (poll_fail) return -1;
    s_snap.pending_events = count - at;
    s_snap.event_ok = at < count;
    s_snap.event = at < count ? queue[at] : (pmu_event_t){0};
    return ESP_OK;
}
esp_err_t read_pico_pmu_event_ack(uint16_t id) {
    assert(at < count && queue[at].event_id == id);
    if (ack_fail) return -1;
    at++;
    // 真ACK刷新snapshot，验证消费器保存了被ACK覆盖的事件。
    // Real ACK refreshes the snapshot; verify the consumer preserves the acknowledged event.
    return ack_followup_fail ? -1 : read_pico_pmu_poll();
}

#include "pmu_product_key_under_test.c"

static void fresh(void) {
    memset(&s_snap, 0, sizeof(s_snap));
    product_key_reset_all();
    count = at = 0;
    next_id = 1;
    poll_fail = ack_fail = ack_followup_fail = false;
}
static void batch(void) { count = at = 0; }
static void push(uint8_t type, uint32_t ms) {
    assert(count < 32);
    queue[count++] = (pmu_event_t){.event_id=next_id++, .type=type, .timestamp_ms=ms};
}
static pmu_product_key_action_t take(void) {
    pmu_product_key_action_t action=read_pico_pmu_take_product_key();
    assert(at==count);
    return action;
}

int main(void) {
    fresh(); assert(take()==PMU_PRODUCT_KEY_NONE);
    push(PMU_EVT_KEY_DOWN,10); assert(take()==PMU_PRODUCT_KEY_NONE);
    batch();push(PMU_EVT_KEY_UP,70);push(PMU_EVT_KEY_SHORT,70);
    assert(take()==PMU_PRODUCT_KEY_SHORT);

    fresh();push(PMU_EVT_KEY_DOWN,100);push(PMU_EVT_KEY_LONG,1600);
    assert(take()==PMU_PRODUCT_KEY_LONG);
    batch();push(PMU_EVT_KEY_UP,1800);push(PMU_EVT_KEY_SHORT,1800);
    assert(take()==PMU_PRODUCT_KEY_NONE);
    batch();push(PMU_EVT_KEY_DOWN,1900);push(PMU_EVT_KEY_UP,1960);push(PMU_EVT_KEY_SHORT,1960);
    assert(take()==PMU_PRODUCT_KEY_SHORT);

    fresh();push(PMU_EVT_KEY_LONG,1500);assert(take()==PMU_PRODUCT_KEY_LONG);
    batch();push(PMU_EVT_KEY_LONG,1501);push(PMU_EVT_KEY_SHORT,1600);
    assert(take()==PMU_PRODUCT_KEY_NONE);
    batch();push(PMU_EVT_KEY_UP,1700);assert(take()==PMU_PRODUCT_KEY_NONE);
    batch();push(PMU_EVT_KEY_SHORT,1701);assert(take()==PMU_PRODUCT_KEY_NONE);

    fresh();push(PMU_EVT_KEY_SHORT,100);push(PMU_EVT_KEY_DOWN,200);push(PMU_EVT_KEY_LONG,1700);
    assert(take()==PMU_PRODUCT_KEY_LONG);
    fresh();push(PMU_EVT_KEY_LONG,1500);push(PMU_EVT_KEY_UP,1600);
    push(PMU_EVT_KEY_SHORT,1600);push(PMU_EVT_KEY_DOWN,1700);push(PMU_EVT_KEY_SHORT,1750);
    assert(take()==PMU_PRODUCT_KEY_LONG);

    fresh();push(PMU_EVT_CHARGE_STATE_CHANGED,1);push(PMU_EVT_BATTERY_SAMPLE_READY,2);
    assert(take()==PMU_PRODUCT_KEY_NONE);
    fresh();push(PMU_EVT_KEY_FORCE_OFF,8000);push(PMU_EVT_KEY_UP,9000);push(PMU_EVT_KEY_SHORT,9000);
    assert(take()==PMU_PRODUCT_KEY_NONE);

    fresh();push(PMU_EVT_KEY_LONG,1500);ack_fail=true;
    assert(read_pico_pmu_take_product_key()==PMU_PRODUCT_KEY_LONG && at==0 && s_product_key_long_hold);
    assert(read_pico_pmu_take_product_key()==PMU_PRODUCT_KEY_NONE && at==0);
    ack_fail=false;assert(take()==PMU_PRODUCT_KEY_NONE);
    fresh();push(PMU_EVT_KEY_LONG,1500);ack_followup_fail=true;
    assert(take()==PMU_PRODUCT_KEY_LONG);
    ack_followup_fail=false;batch();assert(take()==PMU_PRODUCT_KEY_NONE);
    fresh();push(PMU_EVT_KEY_LONG,1500);poll_fail=true;
    assert(read_pico_pmu_take_product_key()==PMU_PRODUCT_KEY_NONE && at==0);
    poll_fail=false;assert(take()==PMU_PRODUCT_KEY_LONG);

    fresh();s_snap.config_ok=true;s_snap.config.key_raw_events=false;s_snap.key_state=1;
    push(PMU_EVT_KEY_LONG,1500);assert(take()==PMU_PRODUCT_KEY_LONG);
    batch();s_snap.key_state=0;assert(take()==PMU_PRODUCT_KEY_NONE);
    batch();push(PMU_EVT_KEY_SHORT,2100);assert(take()==PMU_PRODUCT_KEY_SHORT);

    fresh();push(PMU_EVT_KEY_LONG,UINT32_MAX-100);assert(take()==PMU_PRODUCT_KEY_LONG);
    batch();push(PMU_EVT_KEY_UP,20);push(PMU_EVT_KEY_SHORT,20);
    assert(take()==PMU_PRODUCT_KEY_NONE);
    batch();push(PMU_EVT_CW_RESET,30);push(PMU_EVT_KEY_SHORT,90);
    assert(take()==PMU_PRODUCT_KEY_SHORT);

    puts("pmu_product_key: 17 queue, long-priority, held-release, raw-event and fault scenarios passed");
    return 0;
}

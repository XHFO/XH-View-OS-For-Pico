#!/usr/bin/env python3
"""中文：真实 BLE 初始化/入队/启动任务的按需内存与失败恢复检查。
English: Exercise exact BLE init, submission and bootstrap code for lazy allocation and retry safety.
"""
from pathlib import Path
import argparse
import re
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
SOURCE = (ROOT / 'components/read_pico_ble_pager/read_pico_ble_pager.c').read_text()
HEADER = (ROOT / 'components/read_pico_ble_pager/include/read_pico_ble_pager.h').read_text()


def function(name):
    match = re.search(r'(?:^|\n)((?:static\s+)?[\w\s*]+?\b' + re.escape(name) + r'\([^;]*?\)\s*\{)', SOURCE)
    if not match:
        raise RuntimeError(name)
    start, at, depth, quote = match.start(1), match.end() - 1, 0, None
    while at < len(SOURCE):
        char = SOURCE[at]
        if quote:
            if char == '\\':
                at += 2
                continue
            if char == quote:
                quote = None
        elif SOURCE.startswith('//', at):
            at = SOURCE.index('\n', at)
            continue
        elif SOURCE.startswith('/*', at):
            at = SOURCE.index('*/', at) + 2
            continue
        elif char in '\"\'':
            quote = char
        elif char == '{':
            depth += 1
        elif char == '}':
            depth -= 1
            if not depth:
                return SOURCE[start:at + 1]
        at += 1
    raise RuntimeError('unbalanced ' + name)


prelude = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdatomic.h>
#include <setjmp.h>
#include "pager_hid.h"
typedef int esp_err_t, BaseType_t, portMUX_TYPE, nvs_handle_t;
typedef void *QueueHandle_t;
enum {ESP_OK=0,ESP_ERR_INVALID_STATE=100,ESP_ERR_NO_MEM=101,ESP_ERR_TIMEOUT=102,
      NVS_READONLY=0,NVS_READWRITE=1,pdTRUE=1,pdPASS=1,pdFALSE=0,BLE_HS_CONN_HANDLE_NONE=0xffff,
      BLE_ERR_REM_USER_CONN_TERM=0x13,BLE_HS_ENOTSYNCED=103};
#define portMUX_INITIALIZER_UNLOCKED 0
#define portENTER_CRITICAL(x) ((void)(x))
#define portEXIT_CRITICAL(x) ((void)(x))
#define portMAX_DELAY UINT32_MAX
#define pdMS_TO_TICKS(ms) (ms)
struct ble_npl_event {int unused;};
struct ble_npl_callout {int unused;};
struct ble_gap_disc_params {int itvl,window,filter_duplicates,passive;};
'''

stubs = r'''
static unsigned checks,queue_creates,queue_deletes,task_creates,events_posted,timer_resets,commands_handled;
static unsigned queue_fail_at;
static bool task_failure,reenter_create,fail_stack;
static int reentered_result;
static uint8_t stored_enabled,stored_reversed;
static bool stored_peer;
static jmp_buf worker_boundary;
static void (*created_task)(void *);
static size_t reserved_stack;
static unsigned stops,host_deletes,callout_deletes,event_deletes,stack_deletes,stop_order,waits;
static int stop_rc,deinit_rc;
static bool host_context,release_pending,late_submitter;
typedef struct {unsigned count,capacity;size_t item_size;unsigned char data[PAGER_COMMAND_COUNT][sizeof(command_t)];} fake_queue_t;
#define CHECK(x) do {checks++;assert(x);}while(0)
static QueueHandle_t xQueueCreate(unsigned count,size_t item_size) {
 queue_creates++;if(queue_creates==queue_fail_at)return NULL;
 CHECK(count<=PAGER_COMMAND_COUNT&&item_size<=sizeof(command_t));
 fake_queue_t *q=calloc(1,sizeof(*q));CHECK(q);q->capacity=count;q->item_size=item_size;return q;
}
static void vQueueDelete(QueueHandle_t handle){CHECK(handle);queue_deletes++;free(handle);}
static int xQueueSend(QueueHandle_t handle,const void *value,unsigned timeout) {
 (void)timeout;fake_queue_t *q=handle;CHECK(q);if(q->count>=q->capacity)return pdFALSE;
 memcpy(q->data[q->count++],value,q->item_size);return pdTRUE;
}
static int xQueueReceive(QueueHandle_t handle,void *value,unsigned timeout) {
 fake_queue_t *q=handle;CHECK(q);if(!q->count){if(timeout==portMAX_DELAY)longjmp(worker_boundary,1);return pdFALSE;}
 memcpy(value,q->data[0],q->item_size);--q->count;memmove(q->data,q->data+1,q->count*q->item_size);return pdTRUE;
}
static BaseType_t xTaskCreate(void (*entry)(void *),const char *name,unsigned bytes,void *arg,unsigned priority,void *out) {
 CHECK((!strcmp(name,"ble_pager")||!strcmp(name,"ble_pager_stop"))&&bytes==4096&&!arg&&priority==3&&!out);task_creates++;
 if(reenter_create){reenter_create=false;reentered_result=send_command((command_t){.kind=CMD_SCAN});}
 if(task_failure)return pdFALSE;
 created_task=entry;reserved_stack=bytes;return pdPASS;
}
static void vTaskDelete(void *task){CHECK(!task);reserved_stack=0;created_task=NULL;longjmp(worker_boundary,2);}
static void vTaskSuspend(void *task){CHECK(!task);longjmp(worker_boundary,3);}
static void vTaskDelay(unsigned ticks){CHECK(ticks==1);waits++;if(release_pending){if(late_submitter)CHECK(xQueueSend(s_commands,&(command_t){.kind=CMD_REVERSE,.value=true},0)==pdTRUE);s_submitters=0;s_worker_started=s_worker_starting=false;release_pending=false;}CHECK(waits<10);}
static int nimble_port_stop(void){CHECK(!host_context&&stop_order++==0&&!s_submitters&&!s_worker_started&&!s_worker_starting);stops++;return stop_rc;}
static void nimble_port_run(void){}
static void nimble_port_freertos_deinit(void){CHECK(stop_order++==1);host_deletes++;}
static void ble_npl_callout_deinit(struct ble_npl_callout *timer){CHECK(timer==&s_poll_timer&&stop_order++==2);callout_deletes++;}
static void ble_npl_event_deinit(struct ble_npl_event *event){CHECK(event==&s_control_event&&stop_order++==3);event_deletes++;}
static int nimble_port_deinit(void){CHECK(stop_order++==4);stack_deletes++;return deinit_rc;}
static void *nimble_port_get_dflt_eventq(void){return &s_control_event;}
static void ble_npl_eventq_put(void *queue,struct ble_npl_event *event){CHECK(queue==&s_control_event&&event==&s_control_event);events_posted++;}
static int ble_npl_time_ms_to_ticks32(int value){return value;}
static void ble_npl_callout_reset(struct ble_npl_callout *timer,int ticks){CHECK(timer==&s_poll_timer&&ticks==500);timer_resets++;}
static void ble_npl_callout_stop(struct ble_npl_callout *timer){CHECK(timer==&s_poll_timer);}
static int start_stack(void){commands_handled++;if(fail_stack)return ESP_ERR_NO_MEM;s_stack_started=true;s_status.ready=true;return ESP_OK;}
static void connect_peer(const read_pico_ble_pager_device_t *peer){(void)peer;}
static int ble_gap_disc_cancel(void){return 0;}
static int ble_gap_conn_cancel(void){return 0;}
static int ble_gap_terminate(uint16_t connection,int reason){CHECK(connection!=BLE_HS_CONN_HANDLE_NONE&&reason==BLE_ERR_REM_USER_CONN_TERM);return 0;}
static int ble_store_clear(void){return 0;}
static int ble_gap_disc(uint8_t type,int duration,struct ble_gap_disc_params *params,void *callback,void *arg){(void)type;(void)duration;(void)params;(void)callback;(void)arg;return 0;}
static void *gap_event;
static int nvs_open(const char *name,int mode,nvs_handle_t *out){CHECK(!strcmp(name,"pico_ble")&&(mode==NVS_READONLY||mode==NVS_READWRITE));*out=1;return ESP_OK;}
static int nvs_get_blob(nvs_handle_t handle,const char *key,void *data,size_t *length){CHECK(handle==1&&!strcmp(key,"peer")&&*length==sizeof(s_remembered));if(!stored_peer)return -1;memset(data,0x41,*length);return ESP_OK;}
static int nvs_get_u8(nvs_handle_t handle,const char *key,uint8_t *value){CHECK(handle==1);if(!strcmp(key,"enabled"))*value=stored_enabled;else{CHECK(!strcmp(key,"reverse"));*value=stored_reversed;}return ESP_OK;}
static int nvs_set_u8(nvs_handle_t handle,const char *key,uint8_t value){CHECK(handle==1);if(!strcmp(key,"enabled"))stored_enabled=value;else if(!strcmp(key,"reverse"))stored_reversed=value;return 0;}
static int nvs_erase_key(nvs_handle_t handle,const char *key){CHECK(handle==1&&(!strcmp(key,"peer")||!strcmp(key,"forget_bond")));return 0;}
static int nvs_commit(nvs_handle_t handle){CHECK(handle==1);return 0;}
static void nvs_close(nvs_handle_t handle){CHECK(handle==1);}
#define ESP_LOGW(...) ((void)0)
'''

tests = r'''
static void reset(void) {
 if(s_commands)vQueueDelete(s_commands);if(s_events)vQueueDelete(s_events);
 s_commands=s_events=NULL;s_initialized=s_worker_started=s_worker_starting=false;s_stack_started=s_stopping=s_reserved=false;s_resume_enabled=false;s_submitters=0;
 memset(&s_status,0,sizeof(s_status));memset(&s_remembered,0,sizeof(s_remembered));
 queue_creates=queue_deletes=task_creates=events_posted=timer_resets=commands_handled=queue_fail_at=0;
 task_failure=reenter_create=fail_stack=false;reentered_result=-1;stored_enabled=stored_reversed=0;stored_peer=false;created_task=NULL;reserved_stack=0;
 stops=host_deletes=callout_deletes=event_deletes=stack_deletes=stop_order=waits=0;stop_rc=deinit_rc=0;host_context=release_pending=late_submitter=false;
}
static int run_worker(void){int result=setjmp(worker_boundary);if(!result){CHECK(created_task);created_task(NULL);assert(!"worker returned");}return result;}
static void initial_off_test(void) {
 reset();stored_reversed=1;stored_peer=true;CHECK(read_pico_ble_pager_init()==ESP_OK);
 CHECK(s_initialized&&s_commands&&s_events&&queue_creates==2&&!task_creates&&!reserved_stack&&!s_stack_started);
 CHECK(s_status.remembered&&s_status.reversed&&s_status.name[sizeof(s_status.name)-1]==0);
 for(int i=0;i<1000;i++){read_pico_ble_pager_status_t status;read_pico_ble_pager_get_status(&status);CHECK(!status.enabled);CHECK(read_pico_ble_pager_init()==ESP_OK);}
 CHECK(queue_creates==2&&!task_creates&&!reserved_stack);CHECK(read_pico_ble_pager_light_sleep_safe());
 CHECK(send_command((command_t){.kind=CMD_ENABLE,.value=true})==ESP_OK);
 CHECK(task_creates==1&&reserved_stack==4096&&((fake_queue_t*)s_commands)->count==1);CHECK(!read_pico_ble_pager_light_sleep_safe());
 CHECK(run_worker()==2&&s_stack_started&&!reserved_stack&&commands_handled==1&&events_posted==1);
 s_status.enabled=false;CHECK(!read_pico_ble_pager_light_sleep_safe());
 for(int i=0;i<3;i++)CHECK(send_command((command_t){.kind=CMD_SCAN})==ESP_OK);
 CHECK(task_creates==1&&!reserved_stack&&events_posted==4&&((fake_queue_t*)s_commands)->count==3);
}
static void restore_test(void) {
 reset();stored_enabled=1;CHECK(read_pico_ble_pager_init()==ESP_OK);
 CHECK(task_creates==1&&reserved_stack==4096&&((fake_queue_t*)s_commands)->count==1);
 command_t command=((command_t*)((fake_queue_t*)s_commands)->data)[0];CHECK(command.kind==CMD_ENABLE&&command.value);
 CHECK(read_pico_ble_pager_init()==ESP_OK&&task_creates==1&&((fake_queue_t*)s_commands)->count==1);
 CHECK(run_worker()==2&&s_status.enabled&&s_stack_started&&!reserved_stack);
}
static void task_failure_test(void) {
 for(int restore=0;restore<2;restore++){
  reset();stored_enabled=(uint8_t)restore;task_failure=true;
  if(restore){CHECK(read_pico_ble_pager_init()==ESP_ERR_NO_MEM&&s_status.error==ESP_ERR_NO_MEM);}
  else{CHECK(read_pico_ble_pager_init()==ESP_OK);CHECK(send_command((command_t){.kind=CMD_ENABLE,.value=true})==ESP_ERR_NO_MEM);}
  CHECK(s_initialized&&!s_worker_started&&!s_worker_starting&&!s_stack_started&&!reserved_stack&&((fake_queue_t*)s_commands)->count==0);
  task_failure=false;CHECK(send_command((command_t){.kind=CMD_ENABLE,.value=true})==ESP_OK);
  CHECK(run_worker()==2&&s_stack_started&&!reserved_stack&&commands_handled==1&&task_creates==2);
 }
}
static void queue_failure_test(void) {
 for(unsigned fail=1;fail<=2;fail++){
  reset();queue_fail_at=fail;CHECK(read_pico_ble_pager_init()==ESP_ERR_NO_MEM);
  CHECK(!s_initialized&&!s_commands&&!s_events&&!task_creates&&!reserved_stack&&queue_deletes==1);
  queue_fail_at=0;CHECK(read_pico_ble_pager_init()==ESP_OK&&!task_creates&&!reserved_stack);
 }
}
static void bootstrap_failure_test(void) {
 reset();CHECK(read_pico_ble_pager_init()==ESP_OK);fail_stack=true;
 CHECK(send_command((command_t){.kind=CMD_ENABLE,.value=true})==ESP_OK);
 CHECK(run_worker()==1&&!s_stack_started&&reserved_stack==4096&&commands_handled==1);
 fail_stack=false;CHECK(send_command((command_t){.kind=CMD_ENABLE,.value=true})==ESP_OK&&task_creates==1);
 CHECK(run_worker()==2&&s_stack_started&&!reserved_stack&&commands_handled==2);
}
static void concurrent_creation_test(void) {
 reset();CHECK(read_pico_ble_pager_init()==ESP_OK);reenter_create=true;
 CHECK(send_command((command_t){.kind=CMD_ENABLE,.value=true})==ESP_OK);
 CHECK(reentered_result==ESP_ERR_INVALID_STATE&&task_creates==1&&((fake_queue_t*)s_commands)->count==1);
 CHECK(run_worker()==2&&commands_handled==1);
}
static void queue_full_test(void) {
 reset();CHECK(read_pico_ble_pager_init()==ESP_OK);
 for(int i=0;i<PAGER_COMMAND_COUNT;i++)CHECK(send_command((command_t){.kind=CMD_ENABLE,.value=true})==ESP_OK);
 CHECK(send_command((command_t){.kind=CMD_SCAN})==ESP_ERR_TIMEOUT&&task_creates==1);
 CHECK(((fake_queue_t*)s_commands)->count==PAGER_COMMAND_COUNT);
}
static void begin_enabled(void){
 reset();CHECK(read_pico_ble_pager_init()==ESP_OK);
 CHECK(send_command((command_t){.kind=CMD_ENABLE,.value=true})==ESP_OK);
 CHECK(run_worker()==2&&s_stack_started&&s_status.ready&&!s_worker_started&&!reserved_stack);
}
static void shutdown_test(void){
 begin_enabled();stored_peer=true;s_status.remembered=true;s_status.reversed=true;
 host_context=true;handle_command(&(command_t){.kind=CMD_ENABLE,.value=false});host_context=false;
 CHECK(s_stopping&&!s_status.enabled&&s_stack_started&&reserved_stack==4096&&!read_pico_ble_pager_light_sleep_safe());
 CHECK(send_command((command_t){.kind=CMD_ENABLE,.value=true})==ESP_ERR_INVALID_STATE&&s_submitters==0);
 CHECK(run_worker()==2&&!s_stopping&&!s_stack_started&&!reserved_stack&&read_pico_ble_pager_light_sleep_safe());
 CHECK(stops==1&&host_deletes==1&&callout_deletes==1&&event_deletes==1&&stack_deletes==1&&stop_order==5);
 CHECK(!s_status.ready&&s_status.error==0&&stored_enabled==0&&s_status.remembered&&s_status.reversed);
 CHECK(send_command((command_t){.kind=CMD_ENABLE,.value=true})==ESP_OK);
 CHECK(run_worker()==2&&s_stack_started&&s_status.enabled&&s_status.ready&&!s_worker_started&&!read_pico_ble_pager_light_sleep_safe());
}
static void teardown_failure_test(void){
 for(int phase=0;phase<2;phase++){
  begin_enabled();if(phase)deinit_rc=79;else stop_rc=78;
  handle_command(&(command_t){.kind=CMD_ENABLE,.value=false});CHECK(run_worker()==2);
  CHECK(s_stopping&&s_stack_started&&!s_status.enabled&&!read_pico_ble_pager_light_sleep_safe());
  CHECK(s_status.error==(phase?79:78)&&stops==1&&stack_deletes==(unsigned)phase);
  CHECK(send_command((command_t){.kind=CMD_ENABLE,.value=true})==ESP_ERR_INVALID_STATE&&!reserved_stack);
 }
 begin_enabled();task_failure=true;handle_command(&(command_t){.kind=CMD_ENABLE,.value=false});
 CHECK(!s_stopping&&s_stack_started&&s_status.error==ESP_ERR_NO_MEM&&!read_pico_ble_pager_light_sleep_safe());
 task_failure=false;handle_command(&(command_t){.kind=CMD_ENABLE,.value=false});CHECK(run_worker()==2&&read_pico_ble_pager_light_sleep_safe());
}
static void teardown_join_test(void){
 begin_enabled();handle_command(&(command_t){.kind=CMD_ENABLE,.value=false});
 s_submitters=1;s_worker_started=s_worker_starting=true;release_pending=late_submitter=true;
 CHECK(run_worker()==2&&waits==1&&stop_order==5&&read_pico_ble_pager_light_sleep_safe());
 CHECK(((fake_queue_t*)s_commands)->count==0&&!s_status.reversed);
 CHECK(send_command((command_t){.kind=CMD_ENABLE,.value=true})==ESP_OK);
 CHECK(run_worker()==2&&s_status.enabled&&!s_status.reversed&&commands_handled==2);
 begin_enabled();handle_command(&(command_t){.kind=CMD_ENABLE,.value=false});
 CHECK(xQueueSend(s_commands,&(command_t){.kind=CMD_ENABLE,.value=true},0)==pdTRUE);
 control_event(NULL);CHECK(!s_status.enabled&&s_stopping&&((fake_queue_t*)s_commands)->count==0);
 CHECK(run_worker()==2&&read_pico_ble_pager_light_sleep_safe());
}
static void idle_worker_test(void){
 reset();CHECK(send_command((command_t){.kind=CMD_REVERSE,.value=true})==ESP_OK);
 CHECK(run_worker()==2&&s_status.reversed&&!s_worker_started&&!reserved_stack&&read_pico_ble_pager_light_sleep_safe());
 reset();CHECK(send_command((command_t){.kind=CMD_ENABLE,.value=false})==ESP_OK);
 CHECK(run_worker()==2&&!s_worker_started&&!reserved_stack&&read_pico_ble_pager_light_sleep_safe());
}
static void host_join_ownership_test(void){
 reset();int boundary=setjmp(worker_boundary);if(!boundary){host_task(NULL);assert(!"host returned");}
 CHECK(boundary==3&&host_deletes==0&&stop_order==0);
}
static void dual_lease_test(void){
 reset();stored_peer=true;stored_reversed=1;
 CHECK(read_pico_ble_pager_reserve(true)==ESP_OK);
 CHECK(read_pico_ble_pager_reservation_ready()&&!read_pico_ble_pager_light_sleep_safe());
 CHECK(!task_creates&&!s_stack_started&&s_status.remembered&&s_status.reversed);
 CHECK(send_command((command_t){.kind=CMD_FORGET})==ESP_ERR_INVALID_STATE);
 CHECK(read_pico_ble_pager_reserve(false)==ESP_OK&&read_pico_ble_pager_light_sleep_safe());
 CHECK(stored_peer&&stored_reversed&&!stored_enabled);
 begin_enabled();stored_peer=true;stored_reversed=1;
 CHECK(read_pico_ble_pager_reserve(true)==ESP_OK&&!read_pico_ble_pager_reservation_ready());
 CHECK(stored_enabled&&s_status.enabled&&stored_peer&&stored_reversed);
 CHECK(send_command((command_t){.kind=CMD_SCAN})==ESP_ERR_INVALID_STATE);
 control_event(NULL);CHECK(s_stopping&&!read_pico_ble_pager_reservation_ready());
 CHECK(read_pico_ble_pager_reserve(false)==ESP_ERR_INVALID_STATE&&s_reserved);
 CHECK(run_worker()==2&&read_pico_ble_pager_reservation_ready());
 CHECK(!read_pico_ble_pager_light_sleep_safe()&&stored_enabled&&stored_peer&&stored_reversed);
 CHECK(read_pico_ble_pager_reserve(false)==ESP_OK&&!s_reserved);
 CHECK(run_worker()==2&&s_stack_started&&s_status.enabled&&stored_enabled);
 // Reserving during the startup restore must not lose the persisted HID preference.
 reset();stored_enabled=1;stored_peer=true;
 CHECK(read_pico_ble_pager_reserve(true)==ESP_OK&&!read_pico_ble_pager_reservation_ready());
 CHECK(run_worker()==2&&read_pico_ble_pager_reservation_ready()&&!s_stack_started);
 CHECK(read_pico_ble_pager_reserve(false)==ESP_OK);
 CHECK(run_worker()==2&&s_stack_started&&s_status.enabled&&stored_enabled&&stored_peer);
}
int main(void) {
 initial_off_test();restore_test();task_failure_test();queue_failure_test();bootstrap_failure_test();concurrent_creation_test();queue_full_test();
 shutdown_test();teardown_failure_test();teardown_join_test();idle_worker_test();host_join_ownership_test();dual_lease_test();
 reset();printf("BLE lifecycle: %u checks PASS; lazy allocation, ordered stop/join/deinit, re-enable, failure guards, queued-command rejection and idle worker release\n",checks);return 0;
}
'''

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--sanitize', action='store_true')
args = parser.parse_args()
header = re.sub(r'^#include "esp_err.h"\s*$|^#pragma once\s*$', '', HEADER, flags=re.M)
declarations = SOURCE[SOURCE.index('#define PAGER_UUID_HID'):SOURCE.index('void ble_store_config_init(void);')]
names = ['bump_locked', 'set_error', 'clear_events', 'disconnect_now', 'stop_stack_task', 'stop_stack', 'host_task', 'handle_command', 'control_event', 'worker_task', 'ensure_worker', 'read_pico_ble_pager_init', 'send_command', 'read_pico_ble_pager_get_status', 'read_pico_ble_pager_light_sleep_safe', 'read_pico_ble_pager_reserve', 'read_pico_ble_pager_reservation_ready']
prototypes = '\n'.join(function(name).split('{', 1)[0] + ';' for name in names)
source = prelude + '\n' + header + '\n' + declarations + '\n' + prototypes + '\n' + stubs + '\n' + '\n'.join(function(name) for name in names) + '\n' + tests
with tempfile.TemporaryDirectory(prefix='ble-pager-lazy-') as temp:
    path = Path(temp)
    code, binary = path / 'test.c', path / 'test'
    code.write_text(source)
    flags = ['-std=c11', '-Wall', '-Wextra', '-Werror', '-Wno-unused-function', '-Wno-unused-variable', '-g', '-O1']
    if args.sanitize:
        flags += ['-fsanitize=address,undefined', '-fno-omit-frame-pointer']
    subprocess.run(['cc', *flags, '-I', str(ROOT / 'components/read_pico_ble_pager'), str(code), '-o', str(binary)], check=True)
    subprocess.run([str(binary)], check=True)

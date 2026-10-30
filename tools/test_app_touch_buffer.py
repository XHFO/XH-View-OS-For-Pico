#!/usr/bin/env python3
"""真实采样工作器的线程/暂停回归；I2C由脚本替身提供，不连接设备。
Exercise the production sampling worker and pause boundary with a scripted I2C double, without hardware.
"""
from pathlib import Path
import os
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix='pico-touch-worker-') as directory:
    build = Path(directory)
    (build / 'freertos').mkdir()
    (build / 'freertos/task.h').write_text('''#pragma once
#include "common.h"
typedef void *TaskHandle_t;
#define pdPASS 1
#define pdTRUE 1
#define portMAX_DELAY 0xffffffffu
int xTaskCreate(void (*fn)(void*),const char*,unsigned,void*,int,TaskHandle_t*);
void xTaskNotifyGive(TaskHandle_t);
unsigned ulTaskNotifyTake(int,unsigned);
''')
    (build / 'freertos/semphr.h').write_text('''#pragma once
#include "common.h"
typedef void *SemaphoreHandle_t;
SemaphoreHandle_t xSemaphoreCreateMutex(void);
int xSemaphoreTake(SemaphoreHandle_t,unsigned);
void xSemaphoreGive(SemaphoreHandle_t);
void vSemaphoreDelete(SemaphoreHandle_t);
''')
    (build / 'test.c').write_text(r'''
#define _POSIX_C_SOURCE 200809L
#include "app_touch_buffer.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include <assert.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <stdio.h>
#include <time.h>
static atomic_uint reads;
static atomic_bool reading;
static pthread_mutex_t notify_lock=PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t notify_condition=PTHREAD_COND_INITIALIZER;
static unsigned notifications;
static void (*task_fn)(void*);static void *task_arg;
static void *run(void *arg){(void)arg;task_fn(task_arg);return NULL;}
void vTaskDelay(int ms){struct timespec t={.tv_sec=ms/1000,.tv_nsec=(ms%1000)*1000000};nanosleep(&t,NULL);}
int64_t esp_timer_get_time(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return (int64_t)t.tv_sec*1000000+t.tv_nsec/1000;}
int xTaskCreate(void (*fn)(void*),const char *name,unsigned stack,void *arg,int priority,TaskHandle_t *out){
    (void)name;(void)stack;(void)priority;task_fn=fn;task_arg=arg;pthread_t thread;
    if(pthread_create(&thread,NULL,run,NULL))return 0;pthread_detach(thread);*out=(void*)1;return pdPASS;
}
void xTaskNotifyGive(TaskHandle_t task){(void)task;pthread_mutex_lock(&notify_lock);notifications++;pthread_cond_signal(&notify_condition);pthread_mutex_unlock(&notify_lock);}
unsigned ulTaskNotifyTake(int clear,unsigned wait){
    (void)clear;(void)wait;pthread_mutex_lock(&notify_lock);while(!notifications)pthread_cond_wait(&notify_condition,&notify_lock);
    unsigned count=notifications;notifications=0;pthread_mutex_unlock(&notify_lock);return count;
}
SemaphoreHandle_t xSemaphoreCreateMutex(void){pthread_mutex_t *p=malloc(sizeof(*p));assert(p);assert(!pthread_mutex_init(p,NULL));return p;}
int xSemaphoreTake(SemaphoreHandle_t s,unsigned wait){(void)wait;assert(!pthread_mutex_lock(s));return pdTRUE;}
void xSemaphoreGive(SemaphoreHandle_t s){assert(!pthread_mutex_unlock(s));}
void vSemaphoreDelete(SemaphoreHandle_t s){pthread_mutex_destroy(s);free(s);}
int cst836u_read(void *tp,cst836u_touch_t *touch){
    assert(tp==(void*)1);assert(!atomic_exchange(&reading,true));unsigned n=atomic_fetch_add(&reads,1);
    if(n>39)n=39;*touch=(cst836u_touch_t){.touched=(n%2)==0,.count=(n%2)==0,.x=40+n/2,.y=700};
    vTaskDelay(1);atomic_store(&reading,false);return ESP_OK;
}
int main(void){
    assert(app_touch_buffer_enable((void*)1,true,0));
    // UI不消费队列时，采样线程仍持续捕获二十次短按。
    // The worker captures twenty contacts while the UI does not consume events.
    vTaskDelay(350);assert(atomic_load(&reads)>=40);
    int64_t previous=0;bool pending=false;
    for(unsigned i=0;i<40;i++){
        cst836u_touch_t touch;int64_t ms;
        assert(app_touch_buffer_read(&touch,&ms,&pending)==ESP_OK);
        assert(touch.touched==((i%2)==0)&&touch.x==40+i/2&&ms>=previous);previous=ms;
        assert(pending==(i<39));
    }
    assert(!app_touch_buffer_enable((void*)1,false,0));assert(!atomic_load(&reading));
    unsigned before=atomic_load(&reads);vTaskDelay(30);assert(atomic_load(&reads)==before);
    assert(app_touch_buffer_enable((void*)1,true,1));vTaskDelay(15);
    assert(!app_touch_buffer_enable((void*)1,false,1));assert(!atomic_load(&reading));
    before=atomic_load(&reads);vTaskDelay(20);assert(atomic_load(&reads)==before);
    puts("PASS: production sampler retains 20 blocked-consumer taps; disable waits for I2C and stops polling; orientation restarts cleanly.");
    return 0;
}
''')
    command = [os.environ.get('CC', 'cc'), '-std=c11', '-Wall', '-Wextra', '-Werror', '-Wno-misleading-indentation',
               '-fsanitize=address,undefined', '-fno-omit-frame-pointer', '-g', '-pthread',
               '-DESP_LOGW(...)=((void)0)', '-I', str(build), '-I', str(ROOT / 'tools/app_loop_stubs'),
               '-I', str(ROOT / 'main/app'), '-I', str(ROOT / 'main/ui'),
               str(build / 'test.c'), str(ROOT / 'main/app/app_touch_buffer.c'), '-o', str(build / 'test')]
    subprocess.run(command, check=True)
    subprocess.run([str(build / 'test')], check=True)

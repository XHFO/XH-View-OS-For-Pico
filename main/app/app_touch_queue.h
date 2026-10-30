/* SPDX-License-Identifier: Apache-2.0
 * 中文：无硬件的有界触摸队列；生产调用方负责互斥，溢出明确取消至松手。
 * English: Hardware-free bounded touch queue; callers serialize access and overflow cancels through release.
 */
#pragma once
#include "app.h"
#include <string.h>

#define APP_TOUCH_QUEUE_CAPACITY 64
typedef struct {cst836u_touch_t touch;int64_t ms;esp_err_t error;} app_touch_sample_t;
typedef struct {
    app_touch_sample_t samples[APP_TOUCH_QUEUE_CAPACITY],last,observed;
    unsigned head,count;
    bool seen,overflow,blocked;
} app_touch_queue_t;

static inline void app_touch_queue_reset(app_touch_queue_t *q){memset(q,0,sizeof(*q));}
static inline bool app_touch_same(const app_touch_sample_t *a,const app_touch_sample_t *b){
    if(a->error!=b->error)return false;
    if(a->error!=ESP_OK)return true;
    return a->touch.touched==b->touch.touched&&a->touch.count==b->touch.count&&
        (!a->touch.touched||(a->touch.x==b->touch.x&&a->touch.y==b->touch.y));
}
static inline void app_touch_queue_push(app_touch_queue_t *q,const app_touch_sample_t *s){
    if(q->blocked){
        if(s->error==ESP_OK&&!s->touch.touched){q->blocked=false;q->seen=false;}
        else return;
    }
    if(q->seen&&app_touch_same(&q->observed,s))return;
    q->observed=*s;q->seen=true;
    if(q->count==APP_TOUCH_QUEUE_CAPACITY){
        q->head=q->count=0;q->overflow=true;q->seen=false;q->blocked=s->error!=ESP_OK||s->touch.touched;return;
    }
    q->samples[(q->head+q->count)%APP_TOUCH_QUEUE_CAPACITY]=*s;q->count++;
    if(s->error!=ESP_OK)q->blocked=true;
}
static inline app_touch_sample_t app_touch_queue_pop(app_touch_queue_t *q,int64_t now){
    if(q->overflow){
        q->overflow=false;q->last=(app_touch_sample_t){.ms=now,.error=ESP_ERR_NO_MEM};return q->last;
    }
    if(q->count){q->last=q->samples[q->head];q->head=(q->head+1)%APP_TOUCH_QUEUE_CAPACITY;q->count--;}
    else {q->last.ms=now;if(q->last.error!=ESP_OK)q->last=(app_touch_sample_t){.ms=now};}
    return q->last;
}

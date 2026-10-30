/* SPDX-License-Identifier: Apache-2.0
 * 中文：设备和原生预览共用的完整双短触识别；不访问硬件或主动轮询。
 * English: Complete double-tap recognition shared by device and native preview; no hardware access or polling.
 */
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
typedef struct {
    bool down, blocked, valid, first_valid;
    int x, y, first_x, first_y;
    int64_t down_ms, first_up_ms;
} power_touch_t;
#define POWER_TAP_GAP_MS 420
#define POWER_TAP_POLL_MS 20
static inline bool power_touch_feed(power_touch_t *s, bool ok, bool touched,
                                    unsigned count, int x, int y, int64_t now) {
    if(s->first_valid && now-s->first_up_ms>POWER_TAP_GAP_MS)s->first_valid=false;
    if(!ok){s->blocked=true;s->first_valid=false;return false;}
    if(touched){
        if(!s->down){s->down=true;s->down_ms=now;s->x=x;s->y=y;s->valid=count==1&&!s->blocked;}
        if(count!=1||abs(x-s->x)>48||abs(y-s->y)>48||now-s->down_ms>300){s->valid=false;s->first_valid=false;}
        return false;
    }
    if(!s->down){s->blocked=false;return false;}
    s->down=false;
    bool valid=s->valid&&!s->blocked&&now-s->down_ms>=20&&now-s->down_ms<=300;
    s->blocked=false;
    if(!valid){s->first_valid=false;return false;}
    if(s->first_valid&&now-s->first_up_ms<=POWER_TAP_GAP_MS&&
       abs(s->x-s->first_x)<=140&&abs(s->y-s->first_y)<=140){s->first_valid=false;return true;}
    s->first_valid=true;s->first_up_ms=now;s->first_x=s->x;s->first_y=s->y;
    return false;
}

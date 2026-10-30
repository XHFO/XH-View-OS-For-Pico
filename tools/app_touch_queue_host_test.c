/* SPDX-License-Identifier: Apache-2.0
 * 中文：同步刷屏阻塞期间的真实队列算法回归；不模拟面板耗时。
 * English: Test the production queue across a blocked consumer; no physical-panel timing claims.
 */
#include "app_touch_queue.h"
#include <assert.h>
#include <stdio.h>
static app_touch_sample_t sample(bool down,int x,int64_t ms){
    return (app_touch_sample_t){.touch={.touched=down,.count=down?1:0,.x=x,.y=700},.ms=ms};
}
int main(void){
    app_touch_queue_t q={0};
    // 消费者刷屏2秒时，二十次完整短按必须保留原时刻、位置和边沿顺序。
    // Twenty short contacts during a two-second display block retain capture time, position and edge order.
    for(int i=0;i<20;i++){app_touch_sample_t d=sample(true,40+i,100+i*90),u=sample(false,40+i,135+i*90);app_touch_queue_push(&q,&d);app_touch_queue_push(&q,&u);}
    assert(q.count==40);
    for(int i=0;i<20;i++){
        app_touch_sample_t d=app_touch_queue_pop(&q,2500),u=app_touch_queue_pop(&q,2500);
        assert(d.touch.touched&&!u.touch.touched&&d.touch.x==40+i&&u.touch.x==40+i);
        assert(d.ms==100+i*90&&u.ms-d.ms==35);
    }
    assert(!q.count&&!app_touch_queue_pop(&q,2500).touch.touched);
    app_touch_queue_reset(&q);
    app_touch_sample_t d=sample(true,10,100);app_touch_queue_push(&q,&d);
    for(int i=0;i<100;i++)app_touch_queue_push(&q,&d);
    assert(q.count==1);
    // 不能把溢出当成合法抬手；取消到真正松手，随后新按键恢复。
    // Overflow is an invalid contact, never a valid release; recover only after a real lift.
    app_touch_queue_reset(&q);
    for(int i=0;i<=APP_TOUCH_QUEUE_CAPACITY;i++){d=sample(true,i,100+i);app_touch_queue_push(&q,&d);}
    assert(q.overflow&&q.blocked);
    assert(app_touch_queue_pop(&q,300).error==ESP_ERR_NO_MEM);
    assert(app_touch_queue_pop(&q,305).error==ESP_OK);
    d=sample(true,90,310);app_touch_queue_push(&q,&d);assert(!q.count);
    app_touch_sample_t u=sample(false,90,320);app_touch_queue_push(&q,&u);
    d=sample(true,30,330);app_touch_queue_push(&q,&d);
    assert(!app_touch_queue_pop(&q,400).touch.touched&&app_touch_queue_pop(&q,400).touch.x==30);
    // I2C错误同样取消整次接触，恢复成功但仍按住时不能生成新按下。
    // A bus error cancels the contact; successful reads while still held cannot invent a new press.
    app_touch_queue_reset(&q);d=sample(true,9,100);app_touch_queue_push(&q,&d);
    app_touch_sample_t bad={.error=ESP_ERR_INVALID_STATE,.ms=105};app_touch_queue_push(&q,&bad);
    d.ms=110;app_touch_queue_push(&q,&d);assert(q.count==2&&q.blocked);
    assert(app_touch_queue_pop(&q,200).touch.touched);
    assert(app_touch_queue_pop(&q,200).error==ESP_ERR_INVALID_STATE);
    u=sample(false,9,220);app_touch_queue_push(&q,&u);assert(!app_touch_queue_pop(&q,230).touch.touched);
    app_touch_queue_reset(&q);assert(!q.count&&!q.blocked&&!q.seen);
    puts("PASS: buffered contact order/timestamps, duplicate suppression, overflow and I2C-error cancellation, boundary reset.");
    return 0;
}

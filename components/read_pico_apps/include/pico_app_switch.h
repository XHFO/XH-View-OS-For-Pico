/* Shared grayscale switch: left off, right on. Draw only; no allocations or animation. */
#pragma once
#include <stdbool.h>
#include <stdint.h>

typedef void (*pico_switch_box_fn)(uint8_t *,int,int,int,int,int,int,bool);
static inline void pico_switch_draw(uint8_t *fb,int x,int y,int w,int h,bool on,bool enabled,pico_switch_box_fn box){
    int edge=enabled?(on?1:6):10,track=enabled?(on?4:15):14;
    int d=h-6,thumb_x=on?x+w-d-3:x+3,r=h/3;
    box(fb,x,y,w,h,r,edge,true);
    box(fb,x+1,y+1,w-2,h-2,r-1,track,true);
    box(fb,thumb_x,y+3,d,d,d/4,enabled?(on?15:7):11,true);
}

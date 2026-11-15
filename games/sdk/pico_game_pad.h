/* SPDX-License-Identifier: Apache-2.0
 * 中文：TF游戏共用掌机手柄，几何同时用于绘制和命中；不接触阅读或无线实现。
 * English: Shared TF handheld controller; drawing and hit testing share geometry, independent of reader/radio internals.
 */
#pragma once
#include "../../components/read_pico_apps/include/pico_app_sdk.h"
enum { PAD_OPTIONS=7, PAD_A=8, PAD_B=9, PAD_GRID3=10, PAD_GRID4=11, PAD_GRID5=12 };
typedef struct {
    uint8_t kind,paused,over,setup,choice,confirm,mode,reserved;
    uint32_t score,best,moves;
} pga_pad_state_t;
_Static_assert(sizeof(pga_pad_state_t)<=32,"game state must fit the existing BLE payload");
static bool pad_handled,pad_tracking;
static int pad_x0,pad_y0;
static inline int pad_role(const pico_app_host_t *h){return h->bytes>=offsetof(pico_app_host_t,duo_role)+sizeof(h->duo_role)&&h->duo_role?h->duo_role():PGA_DUO_NONE;}
static inline bool pad_send(const pico_app_host_t *h,uint8_t input){return h->bytes>=offsetof(pico_app_host_t,duo_input_send)+sizeof(h->duo_input_send)&&h->duo_input_send&&h->duo_input_send(input);}
static inline bool pad_publish(const pico_app_host_t *h,const pga_pad_state_t *s){return h->bytes>=offsetof(pico_app_host_t,duo_packet_send)+sizeof(h->duo_packet_send)&&h->duo_packet_send&&h->duo_packet_send(s,sizeof(*s));}
static inline bool pad_receive(const pico_app_host_t *h,pga_pad_state_t *s,int kind){
    if(h->bytes<offsetof(pico_app_host_t,duo_packet_recv)+sizeof(h->duo_packet_recv)||!h->duo_packet_recv)return false;
    pga_pad_state_t next;
    if(h->duo_packet_recv(&next,sizeof(next))!=(int)sizeof(next)||next.kind!=kind||next.paused>1||next.over>1||next.setup>1||next.confirm>1)return false;
    *s=next;return true;
}
static inline void pad_rect(pico_app_view_t v,int code,int *x,int *y,int *w,int *h){
    int c=56,cx=v.landscape?112:104,cy=v.landscape?166:304;
    *w=*h=c;
    if(code<4){*x=cx-c/2+(code==1?c:code==3?-c:0);*y=cy-c/2+(code==0?-c:code==2?c:0);}
    else if(code==PAD_A||code==PAD_B){*w=*h=64;*x=v.w-(code==PAD_A?94:166);*y=(v.landscape?116:260)+(code==PAD_B?72:0);}
    else{*w=104;*h=44;*x=v.w/2+(code==PGA_DUO_PAUSE?8:-112);*y=v.h-(v.landscape?92:160);}
}
static inline int pad_input(const pico_app_host_t *h,const pico_app_event_t *e){
    pico_app_view_t v=h->view();
    if(e->type==PGA_CANCEL){pad_handled=pad_tracking=false;return -1;}
    if(e->type==PGA_PRESS){pad_handled=false;pad_tracking=e->y>=96&&e->y<v.h-48;pad_x0=e->x;pad_y0=e->y;}
    if(e->type==PGA_RELEASE)pad_tracking=false;
    if(e->type==PGA_TAP&&pad_handled){pad_handled=false;return -1;}
    if(e->type==PGA_PRESS||e->type==PGA_TAP){
        const int codes[]={0,1,2,3,PAD_A,PAD_B,PAD_OPTIONS,PGA_DUO_PAUSE};
        for(unsigned i=0;i<sizeof(codes)/sizeof(codes[0]);i++){
            int x,y,w,height;pad_rect(v,codes[i],&x,&y,&w,&height);
            if(e->x>=x&&e->x<x+w&&e->y>=y&&e->y<y+height){
                if(e->type==PGA_PRESS&&codes[i]>=4&&codes[i]<PAD_A)return -1;
                pad_handled=e->type==PGA_PRESS;return codes[i];
            }
        }
    }
    if(e->type==PGA_MOVE&&pad_tracking&&!pad_handled){
        int dx=e->x-pad_x0,dy=e->y-pad_y0,ax=dx<0?-dx:dx,ay=dy<0?-dy:dy;
        if((ax>ay?ax:ay)>=24&&(ax>ay?ax:ay)*4>=(ax<ay?ax:ay)*5){pad_handled=true;return ax>ay?(dx>0?1:3):(dy>0?2:0);}
    }
    if(e->type>=PGA_SWIPE_L&&e->type<=PGA_SWIPE_D){
        if(pad_handled){pad_handled=false;return -1;}
        return e->type==PGA_SWIPE_L?3:e->type==PGA_SWIPE_R?1:e->type==PGA_SWIPE_U?0:2;
    }
    return -1;
}
static inline void pad_draw(const pico_app_host_t *h,uint8_t *fb,const char *title,const pga_pad_state_t *s,bool connected){
    pico_app_view_t v=h->view();char line[80];
    h->fill(fb,0,24,v.w,v.h-24,15);h->heading(fb,title,NULL);
    h->format(line,sizeof(line),connected?(s->setup?"选择设置 · A 确认 · B 返回":s->over?"本局结束 · SELECT 设置":s->paused?"已暂停 · START 继续":"手柄在线 · START 暂停"):"连接已断开 · 等待重新连接");
    h->center(fb,v.w/2,78,12,line,0);
    if(s->kind==PGA_2048)h->format(line,sizeof(line),"%u × %u · 得分 %u",s->choice,s->choice,(unsigned)s->score);
    else if(s->kind==PGA_SNAKE)h->format(line,sizeof(line),"%s · 得分 %u",s->mode?"无限穿墙":"经典边界",(unsigned)s->score);
    else h->format(line,sizeof(line),"得分 %u · 最高 %u",(unsigned)s->score,(unsigned)s->best);
    h->center(fb,v.w/2,v.landscape?100:132,12,line,3);
    const char *arrows[]={"↑","→","↓","←"};
    for(int code=0;code<4;code++){int x,y,w,height;pad_rect(v,code,&x,&y,&w,&height);h->box(fb,x,y,w,height,5,0,true);h->center(fb,x+w/2,y+12,22,arrows[code],15);}
    int cx=v.landscape?112:104,cy=v.landscape?166:304;h->fill(fb,cx-28,cy-28,56,56,0);h->box(fb,cx-6,cy-6,12,12,6,5,true);
    for(int code=PAD_A;code<=PAD_B;code++){
        int x,y,w,height;pad_rect(v,code,&x,&y,&w,&height);h->box(fb,x,y,w,height,32,0,false);h->center(fb,x+w/2,y+8,22,code==PAD_A?"A":"B",0);
        const char *label=s->setup?(code==PAD_A?"确认":"返回"):s->kind==PGA_TETRIS?(code==PAD_A?"旋转":"落下"):s->kind==PGA_SNAKE?(code==PAD_A?"暂停":"速度"):(code==PAD_A?"设置":"返回");
        h->center(fb,x+w/2,y+37,11,label,0);
    }
    int x,y,w,height;pad_rect(v,PAD_OPTIONS,&x,&y,&w,&height);h->button(fb,x,y,w,height,"SELECT",false);
    pad_rect(v,PGA_DUO_PAUSE,&x,&y,&w,&height);h->button(fb,x,y,w,height,"START",false);
    if(s->confirm)h->center(fb,v.w/2,v.h-110,12,"新局将替换当前对局 · A 确认 / B 取消",0);
    if(!v.landscape)h->center(fb,v.w/2,v.h-74,11,"十字键控制方向 · SELECT 游戏设置",4);
}

/*
 * SPDX-License-Identifier: Apache-2.0
 * 中文：复用官方连续DU状态缓冲；每轮只扫一相，避免一笔等待整组波形。
 * English: Reuse the official continuous-DU state buffer, scanning one phase per turn.
 * 冻结：不修改波形、相位数、扫描时序或校准；只驱动已二值化的目标。
 * Frozen: Preserve waveform, phase counts, scan timing and calibration; drive binary targets only.
 */
#include "pico_ink.h"
#include "continuous_du.h"
#include "display.h"
#include "e0470_epaper_waveform.h"
#include "ui_kit.h"
#include <string.h>

static EpdiyHighlevelState *s_owner;
static enum EpdRotation s_rotation;
static EpdRect s_area;

bool ps_ink_pending(void) { return s_owner&&continuous_du_busy(); }
bool ps_ink_cancel(void) {
    bool unfinished=ps_ink_pending();
    if(s_owner)continuous_du_reset();
    s_owner=NULL;s_area=(EpdRect){0};
    return unfinished;
}
bool ps_ink_step(app_ctx_t *ctx) {
    if(!ps_ink_pending())return true;
    if(s_owner!=ctx->hl||s_rotation!=epd_get_rotation())return false;
    rails_keepalive();epd_poweron();
    if(continuous_du_scan(ctx->hl,s_area)!=EPD_DRAW_SUCCESS)return false;
    if(!continuous_du_busy())s_area=(EpdRect){0};
    return true;
}
bool ps_ink_present(app_ctx_t *ctx,EpdRect pixels) {
    if(!ctx||!ctx->hl||!ctx->fb||!ctx->hl->back_fb)return false;
    if(ps_ink_pending()&&(s_owner!=ctx->hl||s_rotation!=epd_get_rotation()))return false;
    // 内存不足沿用单次FOLLOW，不新增另一份面板状态。/ On OOM use one FOLLOW update, never another panel-state allocation.
    if(!continuous_du_init())return update_display_area_with(ctx->hl,&E0470_FOLLOW_WAVEFORM,MODE_DU,pixels)==EPD_DRAW_SUCCESS;
    EpdRect r=ui_rotate_rect_to_fb(pixels);int w=epd_width(),h=epd_height();
    int right=r.x+r.width,bottom=r.y+r.height;
    if(r.x<0)r.x=0;
    if(r.y<0)r.y=0;
    if(right>w)right=w;
    if(bottom>h)bottom=h;
    r.x&=~1;right=(right+1)&~1;
    r.width=right-r.x;r.height=bottom-r.y;
    if(r.width<=0||r.height<=0)return ps_ink_step(ctx);
    bool pending=ps_ink_pending();
    if(!pending){continuous_du_reset();s_area=r;}
    else {
        int x=s_area.x<r.x?s_area.x:r.x,y=s_area.y<r.y?s_area.y:r.y;
        int xr=s_area.x+s_area.width>right?s_area.x+s_area.width:right;
        int yb=s_area.y+s_area.height>bottom?s_area.y+s_area.height:bottom;
        s_area=(EpdRect){x,y,xr-x,yb-y};
    }
    s_owner=ctx->hl;s_rotation=epd_get_rotation();
    continuous_du_mark_diff(ctx->fb,ctx->hl->back_fb,r,continuous_du_dark_phases(),false);
    // back是已提交目标，不代表余相已完成；中断者必须用cancel的结果强刷。
    // Back holds the submitted target, not completed physical drive; interruptors must clean when cancel reports pending work.
    for(int y=r.y;y<bottom;y++)memcpy(ctx->hl->back_fb+(size_t)y*w/2+r.x/2,ctx->fb+(size_t)y*w/2+r.x/2,(size_t)r.width/2);
    return ps_ink_step(ctx);
}

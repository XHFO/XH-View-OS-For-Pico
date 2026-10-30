/*
 * 网页 demo 比例的原生灰阶控件与线条图标，不依赖旧演示页的固定底栏。
 * Native grayscale controls and line icons at the web demo scale, without the legacy footer.
 * 冻结：只绘图，所有命中与持久状态由控制器处理。
 * Frozen: Paint only; controllers own hit testing and persistent state.
 */
#include <stdio.h>
#include <string.h>
#include "pico_system.h"
#include "pico_navigation.h"
#include "pico_theme.h"
#include "pico_icons_data.h"
#include "ttf_font.h"
#include "ui_kit.h"

static int px(int n) {
    return (n * 19 + 5) / 10;
}
static int clamp(int a, int l, int h) {
    return a < l ? l : a > h ? h : a;
}
// 实屏优先可读性：小字放大，正文尺寸由阅读排版独立控制。
// Enlarge small UI text; reading typography controls its own larger sizes.
static int ui_size(int n) { return n < 16 ? n + 4 : n < 20 ? 20 : n; }
// 所有16级灰阶原样保留；抗锯齿只混合边缘，不创建临时图层。
// Preserve all 16 grays; antialias only boundaries without allocating a layer.
static void coverage_pixel(uint8_t *fb, int x, int y, int gray, int coverage) {
    if (!coverage) return;
    if (coverage < 16) {
        EpdRect sample = ui_rotate_rect_to_fb((EpdRect){x, y, 1, 1});
        int background = epd_get_pixel(sample.x, sample.y, 1216, 684, fb) >> 4;
        gray = (background * (16 - coverage) + gray * coverage + 8) / 16;
    }
    epd_draw_pixel(x, y, (uint8_t)(gray * 17), fb);
}
static int round_coverage(EpdRect r, int radius, int x, int y) {
    if (x < r.x || y < r.y || x >= r.x + r.width || y >= r.y + r.height) return 0;
    if (!radius || (x >= r.x + radius && x < r.x + r.width - radius)
        || (y >= r.y + radius && y < r.y + r.height - radius)) return 16;
    int cx = (x < r.x + radius ? r.x + radius : r.x + r.width - radius) * 8;
    int cy = (y < r.y + radius ? r.y + radius : r.y + r.height - radius) * 8;
    int64_t rr = (int64_t)radius * radius * 64;
    int coverage = 0;
    for (int sy = 1; sy < 8; sy += 2) for (int sx = 1; sx < 8; sx += 2) {
        int64_t dx = x * 8 + sx - cx, dy = y * 8 + sy - cy;
        coverage += dx * dx + dy * dy <= rr;
    }
    return coverage;
}
static void round_rect(uint8_t *fb, EpdRect r, int radius, int gray, bool filled) {
    ps_view_t v = ps_view();
    int x0 = clamp(r.x, 0, px(v.w)), y0 = clamp(r.y, 0, px(v.h));
    int x1 = clamp(r.x + r.width, 0, px(v.w)), y1 = clamp(r.y + r.height, 0, px(v.h));
    if (!fb || r.width <= 0 || r.height <= 0 || x0 >= x1 || y0 >= y1) return;
    int limit = (r.width < r.height ? r.width : r.height) / 2;
    radius = clamp(radius, 0, limit);
    gray = clamp(gray, 0, 15);
    if (filled) {
        // 大面积中心由半字节快速填充，只有四角需要16点覆盖率。
        // Fast packed fills cover the interior; only corners require 16 samples.
        ui_fill_rect_fast(fb, (EpdRect){r.x + radius, r.y, r.width - 2 * radius, r.height}, (uint8_t)(gray * 17));
        ui_fill_rect_fast(fb, (EpdRect){r.x, r.y + radius, radius, r.height - 2 * radius}, (uint8_t)(gray * 17));
        ui_fill_rect_fast(fb, (EpdRect){r.x + r.width - radius, r.y + radius, radius, r.height - 2 * radius}, (uint8_t)(gray * 17));
        for (int y = y0; y < y1; y++) {
            if (y >= r.y + radius && y < r.y + r.height - radius) continue;
            for (int x = x0; x < x1; x++) {
                if (x >= r.x + radius && x < r.x + r.width - radius) continue;
                coverage_pixel(fb, x, y, gray, round_coverage(r, radius, x, y));
            }
        }
        return;
    }
    EpdRect inner = {r.x + PS_THEME_STROKE_PX, r.y + PS_THEME_STROKE_PX,
        r.width - 2 * PS_THEME_STROKE_PX, r.height - 2 * PS_THEME_STROKE_PX};
    int ir = radius > PS_THEME_STROKE_PX ? radius - PS_THEME_STROKE_PX : 0;
    for (int y = y0; y < y1; y++) for (int x = x0; x < x1; x++) {
        if (inner.width > 0 && inner.height > 0 && y >= inner.y && y < inner.y + inner.height) {
            int left = inner.x + ir, right = inner.x + inner.width - ir;
            if (y >= inner.y + ir && y < inner.y + inner.height - ir) {
                left = inner.x; right = inner.x + inner.width;
            }
            if (x >= left && x < right) { x = right - 1; continue; }
        }
        int outer = round_coverage(r, radius, x, y);
        int inside = inner.width > 0 && inner.height > 0 ? round_coverage(inner, ir, x, y) : 0;
        coverage_pixel(fb, x, y, gray, outer - inside);
    }
}
EpdRect ps_rect(int x, int y, int w, int h) {
    return (EpdRect){ px(x), px(y), px(w), px(h) };
}
bool ps_hit(int x, int y, int rx, int ry, int rw, int rh) {
    return x >= rx && y >= ry && x < rx + rw && y < ry + rh;
}
void ps_fill(uint8_t* fb, int x, int y, int w, int h, int gray) {
    ps_view_t v = ps_view();
    int x1 = clamp(x + w, 0, v.w), y1 = clamp(y + h, 0, v.h);
    x = clamp(x, 0, v.w);
    y = clamp(y, 0, v.h);
    if (x1 > x && y1 > y)
        ui_fill_rect_fast(fb, ps_rect(x, y, x1 - x, y1 - y), (uint8_t)(clamp(gray, 0, 15) * 17));
}
void ps_line(uint8_t* fb, int x0, int y0, int x1, int y1, int gray) {
    if (!fb) return;
    ps_view_t v = ps_view();
    int ax = px(x0), ay = px(y0), bx = px(x1), by = px(y1);
    int minx = clamp((ax < bx ? ax : bx) - 1, 0, px(v.w));
    int maxx = clamp((ax > bx ? ax : bx) + 2, 0, px(v.w));
    int miny = clamp((ay < by ? ay : by) - 1, 0, px(v.h));
    int maxy = clamp((ay > by ? ay : by) + 2, 0, px(v.h));
    int64_t dx = (int64_t)(bx - ax) * 8, dy = (int64_t)(by - ay) * 8;
    int64_t length = dx * dx + dy * dy;
    const int radius_squared = PS_THEME_STROKE_PX * PS_THEME_STROKE_PX * 16;
    gray = clamp(gray, 0, 15);
    bool horizontal = (dx < 0 ? -dx : dx) >= (dy < 0 ? -dy : dy);
    int start = horizontal ? minx : miny, end = horizontal ? maxx : maxy;
    for (int major = start; major < end; major++) {
        int center = horizontal ? ay : ax;
        if (horizontal && dx) center += (int)((int64_t)(major - ax) * dy / dx);
        if (!horizontal && dy) center += (int)((int64_t)(major - ay) * dx / dy);
        int low = clamp(center - 3, horizontal ? miny : minx, horizontal ? maxy : maxx);
        int high = clamp(center + 4, horizontal ? miny : minx, horizontal ? maxy : maxx);
        for (int minor = low; minor < high; minor++) {
            int x = horizontal ? major : minor, y = horizontal ? minor : major;
            int coverage = 0;
            for (int sy = 1; sy < 8; sy += 2) for (int sx = 1; sx < 8; sx += 2) {
                int64_t qx = (int64_t)(x - ax) * 8 + sx - 4;
                int64_t qy = (int64_t)(y - ay) * 8 + sy - 4;
                int64_t dot = qx * dx + qy * dy;
                if (!length || dot <= 0) coverage += qx * qx + qy * qy <= radius_squared;
                else if (dot >= length) {
                    qx -= dx; qy -= dy;
                    coverage += qx * qx + qy * qy <= radius_squared;
                } else {
                    int64_t cross = qx * dy - qy * dx;
                    coverage += cross * cross <= radius_squared * length;
                }
            }
            coverage_pixel(fb, x, y, gray, coverage);
        }
    }
}
void ps_box(uint8_t* fb, int x, int y, int w, int h, int radius, int gray, bool filled) {
    if (w <= 0 || h <= 0) return;
    round_rect(fb, ps_rect(x, y, w, h), px(radius), gray, filled);
}
void ps_surface(uint8_t *fb, int x, int y, int w, int h, bool inset) {
    ps_box(fb, x, y, w, h, PS_THEME_RADIUS, inset ? PS_THEME_INSET : PS_THEME_SURFACE, true);
    ps_box(fb, x, y, w, h, PS_THEME_RADIUS, PS_THEME_BORDER, false);
}
static void cursor_pixel(uint8_t *fb,int x,int y,const EpdRect *buttons,int count) {
    ps_view_t v=ps_view();if(x<0||y<0||x>=px(v.w)||y>=px(v.h))return;
    for(int i=0;i<count;i++) {
        int cx=px(buttons[i].x+buttons[i].width/2),cy=px(buttons[i].y+buttons[i].height/2);
        int radius=px(buttons[i].width/2)+3,dx=x-cx,dy=y-cy;
        if(dx*dx+dy*dy<=radius*radius)return;
    }
    epd_draw_pixel(x,y,0,fb);
}
void ps_cursor_ring(uint8_t *fb,int x,int y,int radius,const EpdRect *buttons,int count) {
    int cx=px(x),cy=px(y);
    for(int line=0;line<3;line++) {
        int r=px(radius)-line,dx=0,dy=r,error=1-r;
        while(dx<=dy) {
            int points[8][2]={{cx+dx,cy+dy},{cx-dx,cy+dy},{cx+dx,cy-dy},{cx-dx,cy-dy},
                {cx+dy,cy+dx},{cx-dy,cy+dx},{cx+dy,cy-dx},{cx-dy,cy-dx}};
            for(int i=0;i<8;i++)cursor_pixel(fb,points[i][0],points[i][1],buttons,count);
            dx++;
            if(error<0)error+=2*dx+1;
            else{dy--;error+=2*(dx-dy)+1;}
        }
    }
}
static void text(uint8_t* fb, int x, int y, int size, const char* s, int gray, bool center, bool ui_text, bool shrink) {
    if (!s || !*s) return;
    if (ui_text) size = ui_size(size);
    ps_view_t v = ps_view();
    if (y < 0 || y + size > v.h) return;
    int width = center ? 2 * (x < v.w - x ? x : v.w - x) : v.w - x - 2;
    if (width <= 0) return;
    int old_weight = ttf_get_weight();
    if (ps_route() != PS_READ && old_weight < 600) ttf_set_weight(600);
    int height = px(size);
    int max = px(width);
    // 长标签宁可截短，也不再缩成实屏难辨的小字。/ Truncate long labels instead of shrinking them to tiny text.
    while (shrink && height > px(15) && ttf_text_width_px(height, s) > max) height--;
    char shortened[768];
    if (ttf_text_width_px(height, s) > max) {
        size_t n = strlen(s);
        if (n >= sizeof(shortened)) n = sizeof(shortened) - 1;
        memcpy(shortened, s, n);
        shortened[n] = 0;
        while (n && ttf_text_width_px(height, shortened) > max) {
            do {
                n--;
            } while (n && ((unsigned char)shortened[n] & 0xc0) == 0x80);
            shortened[n] = 0;
        }
        s = shortened;
    }
    // 使用控件已有底色混合字形，避免选中卡片出现白色字框。/ Blend against the painted control background.
    EpdRect sample = ui_rotate_rect_to_fb((EpdRect){px(x), px(y), 1, 1});
    int bg = epd_get_pixel(sample.x, sample.y, 1216, 684, fb) >> 4;
    ttf_draw_text_px(
        fb, px(x), px(y) + ttf_ascender_px(height), height, s,
        center ? EPD_DRAW_ALIGN_CENTER : EPD_DRAW_ALIGN_LEFT, (uint8_t)clamp(gray, 0, 15), (uint8_t)bg);
    ttf_set_weight(old_weight);
}
void ps_text(uint8_t* fb, int x, int y, int size, const char* s, int gray) {
    text(fb, x, y, size, s, gray, false, true, true);
}
void ps_text_fixed(uint8_t* fb,int x,int y,int size,const char* s,int gray) {
    // 提示只截短超宽文字，不改变字号，避免滑动过程中跳动。/ Truncate overlong prompts without resizing during motion.
    text(fb,x,y,size,s,gray,false,true,false);
}
void ps_center(uint8_t* fb, int x, int y, int size, const char* s, int gray) {
    text(fb, x, y, size, s, gray, true, true, true);
}
void ps_body_text(uint8_t* fb, int x, int y, int size, const char* s, int gray) {
    text(fb, x, y, size, s, gray, false, false, true);
}
static size_t u8len(const char* p) {
    unsigned char c = (unsigned char)*p;
    return c < 0x80 ? 1 : c < 0xe0 ? 2 : c < 0xf0 ? 3 : 4;
}
void ps_wrap(uint8_t* fb, int x, int y, int w, int size, int lines, const char* s, int gray) {
    if (!s) return;
    char line[512];
    int row = 0;
    size_t n = 0;
    while (*s && row < lines) {
        size_t c = u8len(s);
        if (strlen(s) < c) break;
        if (*s == '\n') {
            line[n] = 0;
            ps_text(fb, x, y + row * (ui_size(size) + 7), size, line, gray);
            row++;
            n = 0;
            s++;
            continue;
        }
        if (n + c >= sizeof(line)) {
            line[n] = 0;
            ps_text(fb, x, y + row * (ui_size(size) + 7), size, line, gray);
            row++;
            n = 0;
            continue;
        }
        memcpy(line + n, s, c);
        line[n + c] = 0;
        if (n && ttf_text_width_px(px(ui_size(size)), line) > px(w)) {
            line[n] = 0;
            ps_text(fb, x, y + row * (ui_size(size) + 7), size, line, gray);
            row++;
            n = 0;
            continue;
        }
        n += c;
        s += c;
    }
    if (n && row < lines) {
        line[n] = 0;
        ps_text(fb, x, y + row * (ui_size(size) + 7), size, line, gray);
    }
}
void ps_button(uint8_t* fb, int x, int y, int w, int h, const char* label, bool selected) {
    if (w <= 0 || h <= 0) return;
    int radius = PS_THEME_BUTTON_RADIUS;
    ps_box(fb, x, y, w, h, radius, selected ? PS_THEME_SELECTED : PS_THEME_INSET, true);
    if (!selected) {
        ps_box(fb, x + 1, y + 1, w - 2, h - 3, radius - 1, PS_THEME_CANVAS, true);
        ps_box(fb, x, y, w, h, radius, PS_THEME_BORDER, false);
    }
    ps_center(fb, x + w / 2, y + (h - ui_size(16)) / 2, 16, label,
        selected ? PS_THEME_ON_SELECTED : PS_THEME_INK);
}
// 所有路径在同一16点子像素掩码内取并集，只混合一次，交点不会重复变粗。
// Union all strokes in one 16-sample mask and blend once, preventing darker joins.
static uint16_t icon_segment_mask(int ax,int ay,int bx,int by,int radius,int x,int y,uint16_t occupied) {
    int64_t dx=bx-ax,dy=by-ay,length=dx*dx+dy*dy;
    int64_t squared=(int64_t)radius*radius;
    uint16_t mask=occupied;
    for(int sy=0;sy<4;sy++)for(int sx=0;sx<4;sx++) {
        uint16_t bit=(uint16_t)(1u<<(sy*4+sx));if(mask&bit)continue;
        int64_t qx=(int64_t)x*8+sx*2+1-ax,qy=(int64_t)y*8+sy*2+1-ay;
        int64_t dot=qx*dx+qy*dy;bool inside;
        if(!length||dot<=0)inside=qx*qx+qy*qy<=squared;
        else if(dot>=length){qx-=dx;qy-=dy;inside=qx*qx+qy*qy<=squared;}
        else{int64_t cross=qx*dy-qy*dx;inside=cross*cross<=squared*length;}
        if(inside)mask|=bit;
    }
    return mask;
}
static int icon_coordinate(int origin,int coordinate,int size) {
    return origin+(coordinate*size+12)/24;
}
void ps_icon(uint8_t* fb,int x,int y,int size,ps_icon_t icon,int gray) {
    if(!fb||size<=0||size>1024)return;
    if((unsigned)icon>=PI_COUNT)icon=PI_MORE;
    ps_icon_shape_t shape=ps_icon_shapes[icon];if(!shape.count)return;
    ps_view_t view=ps_view();int width=px(view.w),height=px(view.h);
    int ox=px(x)*8,oy=px(y)*8,native_size=px(size);
    // SVG线宽2/24；符号尺寸变化时线宽同比例变化，几何与选中态无关。
    // The 2/24 SVG stroke scales with the glyph; selection never changes geometry.
    int radius=(native_size+1)/3;
    int left=width,right=0,top=height,bottom=0;
    for(unsigned i=0;i<shape.count;i++) {
        ps_icon_segment_t s=ps_icon_segments[shape.first+i];
        int ax=icon_coordinate(ox,s.ax,native_size),ay=icon_coordinate(oy,s.ay,native_size);
        int bx=icon_coordinate(ox,s.bx,native_size),by=icon_coordinate(oy,s.by,native_size);
        int low=(ax<bx?ax:bx)-radius,high=(ax>bx?ax:bx)+radius;
        if(low/8-1<left)left=low/8-1;
        if(high/8+2>right)right=high/8+2;
        low=(ay<by?ay:by)-radius;high=(ay>by?ay:by)+radius;
        if(low/8-1<top)top=low/8-1;
        if(high/8+2>bottom)bottom=high/8+2;
    }
    left=clamp(left,0,width);right=clamp(right,0,width);top=clamp(top,0,height);bottom=clamp(bottom,0,height);
    if(left>=right||top>=bottom)return;
    // 仅保留一条屏幕行，最大2432字节；不分配图层，不扫描整张屏幕。
    // One screen-row mask uses at most 2432 bytes; no layer allocation or full-frame scan.
    uint16_t row[1216];gray=clamp(gray,0,15);
    for(int py=top;py<bottom;py++) {
        memset(row+left,0,(size_t)(right-left)*sizeof(*row));
        for(unsigned i=0;i<shape.count;i++) {
            ps_icon_segment_t s=ps_icon_segments[shape.first+i];
            int ax=icon_coordinate(ox,s.ax,native_size),ay=icon_coordinate(oy,s.ay,native_size);
            int bx=icon_coordinate(ox,s.bx,native_size),by=icon_coordinate(oy,s.by,native_size);
            int low_y=py*8+1-radius,high_y=py*8+7+radius;
            if((ay<by?by:ay)<low_y||(ay<by?ay:by)>high_y)continue;
            int low_x=ax<bx?ax:bx,high_x=ax>bx?ax:bx;
            if(ay!=by) {
                int y0=ay<by?ay:by,y1=ay>by?ay:by;
                int q0=clamp(low_y,y0,y1),q1=clamp(high_y,y0,y1);
                int a=ax+(int)((int64_t)(q0-ay)*(bx-ax)/(by-ay));
                int b=ax+(int)((int64_t)(q1-ay)*(bx-ax)/(by-ay));
                low_x=a<b?a:b;high_x=a>b?a:b;
            }
            int first=clamp((low_x-radius)/8-1,left,right);
            int last=clamp((high_x+radius)/8+2,left,right);
            for(int px=first;px<last;px++)if(row[px]!=UINT16_MAX)
                row[px]=icon_segment_mask(ax,ay,bx,by,radius,px,py,row[px]);
        }
        for(int px=left;px<right;px++)if(row[px])
            coverage_pixel(fb,px,py,gray,__builtin_popcount((unsigned)row[px]));
    }
}
void ps_corner_controls(uint8_t *fb) {
    ps_view_t v=ps_view();
    // 透明线段贴合屏幕圆角，镜像绘制且不擦除正文。/ Transparent mirrored segments follow screen corners without clearing content.
    for(int side=0;side<2;side++) {
        int sign=side?-1:1,origin=side?v.w:0;
        const int xy[][2]={{8,36},{8,20},{9,16},{11,12},{15,9},{20,8},{36,8}};
        for(size_t i=1;i<sizeof(xy)/sizeof(xy[0]);i++)ps_line(fb,origin+sign*xy[i-1][0],v.h-xy[i-1][1],origin+sign*xy[i][0],v.h-xy[i][1],3);
    }
}
// 导航名称只在自己的格内截短，固定字号，不挤占相邻入口。/ Truncate each navigation label inside its own cell at a fixed font size.
static void navigation_label(uint8_t *fb,int x,int y,int width,const char *label,int gray) {
    char shortened[64];size_t n=strlen(label);
    if(n>sizeof(shortened)-4)n=sizeof(shortened)-4;
    while(n&&((unsigned char)label[n]&0xc0)==0x80)n--;
    memcpy(shortened,label,n);shortened[n]=0;bool cut=label[n]!=0;
    int old=ttf_get_weight();if(old<600)ttf_set_weight(600);
    int size=px(ui_size(9)),max=px(width-8),dots=ttf_text_width_px(size,"...");
    while(n&&ttf_text_width_px(size,shortened)+(cut?dots:0)>max) {
        do{n--;}while(n&&((unsigned char)shortened[n]&0xc0)==0x80);
        shortened[n]=0;cut=true;
    }
    if(cut&&ttf_text_width_px(size,shortened)+dots<=max)memcpy(shortened+n,"...",4);
    text(fb,x+width/2,y,9,shortened,gray,true,true,false);ttf_set_weight(old);
}
void ps_tabs(uint8_t *fb,int selected) {
    (void)selected;ps_view_t v=ps_view();int count=ps_navigation_count(),active=ps_navigation_index(ps_route());
    int w=(v.w-72-(count-1)*4)/count;
    ps_fill(fb,0,24,v.w,46,PS_THEME_CANVAS);
    ps_box(fb,12,28,v.w-72,40,PS_THEME_RADIUS,PS_THEME_INSET,true);
    for(int i=0;i<count;i++) {
        const ps_navigation_entry_t *item=ps_navigation_item(i);int x=12+i*(w+4);
        if(active==i) {
            ps_box(fb,x+1,29,w-2,38,PS_THEME_BUTTON_RADIUS,PS_THEME_SURFACE,true);
            ps_box(fb,x+1,29,w-2,38,PS_THEME_BUTTON_RADIUS,PS_THEME_BORDER,false);
        }
        int gray=active==i?PS_THEME_INK:PS_THEME_SECONDARY;
        ps_icon(fb,x+w/2-9,30,18,item->icon,gray);
        navigation_label(fb,x,50,w,item->label,gray);
    }
    ps_box(fb,v.w-44,31,34,34,17,PS_THEME_CANVAS,true);
    ps_icon(fb,v.w-38,37,22,PI_SETTINGS,PS_THEME_INK);
}
void ps_heading(uint8_t* fb, const char* title, const char* subtitle) {
    ps_view_t v = ps_view();
    ps_box(fb, 6, 30, 34, 34, 17, PS_THEME_CANVAS, true);
    ps_icon(fb, 12, 36, 22, PI_BACK, PS_THEME_INK);
    ps_text(fb, 46, 35, 20, title, PS_THEME_INK);
    ps_line(fb, 12, 68, v.w - 12, 68, PS_THEME_INSET);
    if (subtitle) ps_wrap(fb, 16, 81, v.w - 32, 12, 2, subtitle, PS_THEME_SECONDARY);
}

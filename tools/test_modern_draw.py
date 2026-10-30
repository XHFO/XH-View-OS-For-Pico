#!/usr/bin/env python3
"""Modern UI: actual packed 4bpp primitives, four rotations, clipping and AA.
No hardware I/O. CFLAGS may enable address/undefined sanitizers.
"""
from pathlib import Path
import hashlib
import json
import os
import re
import shlex
import subprocess
import tempfile
import struct
import zlib

ROOT = Path(__file__).resolve().parents[1]
OUT = Path(os.environ.get('PICO_DRAW_OUTPUT', str(ROOT / 'build/modern26-primitives')))
OUT.mkdir(parents=True, exist_ok=True)


def function(source, name):
    m = re.search(r'(?:^|\n)((?:static\s+)?[\w\s*]+?\b' + name + r'\([^;]*?\)\s*\{)', source)
    if not m:
        raise RuntimeError(name)
    start, p, depth, quote = m.start(1), m.end() - 1, 0, None
    while p < len(source):
        c = source[p]
        if quote:
            if c == '\\':
                p += 2
                continue
            if c == quote:
                quote = None
        elif source.startswith('//', p):
            p = source.index('\n', p)
            continue
        elif source.startswith('/*', p):
            p = source.index('*/', p) + 2
            continue
        elif c in '\"\'':
            quote = c
        elif c == '{':
            depth += 1
        elif c == '}':
            depth -= 1
            if not depth:
                return source[start:p + 1]
        p += 1
    raise RuntimeError(name)


epd = (ROOT/'components/epdiy/src/epdiy.c').read_text()
header = (ROOT/'components/epdiy/src/epdiy.h').read_text()
ui = (ROOT/'main/ui/ui_kit.c').read_text()
system = (ROOT/'main/system/pico_system.h').read_text()
navigation = (ROOT/'main/system/pico_navigation.h').read_text()
draw = (ROOT/'main/system/pico_draw.c').read_text()
# The static binary cursor remains deliberately unchanged.
baseline = Path(os.environ.get('PICO_DRAW_BASELINE', str(ROOT/'build/urgent24a-source')))
base = (baseline/'main/system/pico_draw.c').read_text()
for name in ('cursor_pixel', 'ps_cursor_ring'):
    assert function(draw, name) == function(base, name), name + ' changed'
assert not re.search(r'\b(?:malloc|calloc|realloc)\s*\(', draw)
types = re.search(r'typedef struct\s*\{[^}]*\}\s*EpdRect;', header).group()
for name in ('EpdRotation', 'EpdFontFlags'):
    types += '\n' + re.search(r'enum ' + name + r'\s*\{[^}]*\};', header).group()
types += '\n' + re.search(r'typedef struct\s*\{[^}]*\}\s*Coord_xy;', epd).group()
for name in ('ps_route_t', 'ps_icon_t'):
    types += '\n' + re.search(r'typedef enum\s*\{[^}]*\}\s*' + name + ';', system).group()
types += '\n' + '\n'.join(re.findall(r'enum\s*\{[^}]*\};', navigation, re.S))
types += '\n' + re.search(r'typedef struct\s*\{[^}]*\}\s*ps_navigation_entry_t;', navigation, re.S).group()
types += '\n#include \"pico_icons_data.h\"\ntypedef struct { int w,h; bool landscape; } ps_view_t;'
actual = '\n'.join(function(epd, name) for name in (
    '_rotate', 'epd_draw_pixel', 'epd_get_pixel', 'epd_set_rotation',
    'epd_get_rotation', 'epd_rotated_display_width', 'epd_rotated_display_height'))
actual += '\n' + function(ui, 'ui_rotate_rect_to_fb') + '\n' + function(ui, 'ui_fill_rect_fast')
draw = re.sub(r'^#include.*$', '', draw, flags=re.M)
prelude = r'''
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "pico_theme.h"
#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"
'''
platform = r'''
enum { WIDTH=1216, HEIGHT=684, FRAME_BYTES=WIDTH*HEIGHT/2, GUARD=64 };
static enum EpdRotation display_rotation;
static int epd_width(void){return WIDTH;}
static int epd_height(void){return HEIGHT;}
#define _swap_int(a,b) do {int value=(a);(a)=(b);(b)=value;}while(0)
static struct {uint8_t pre[GUARD],fb[FRAME_BYTES],post[GUARD];} canvas;
static uint8_t oracle[FRAME_BYTES],reference[WIDTH*HEIGHT];
static unsigned checks, failures;
#define CHECK(v,label) do {checks++;if(!(v)){if(failures<12)fprintf(stderr,"FAIL %s line %d rot %d\n",label,__LINE__,display_rotation);failures++;}}while(0)
ps_view_t ps_view(void){bool wide=display_rotation==0||display_rotation==2;return (ps_view_t){wide?640:360,wide?360:640,wide};}
ps_route_t ps_route(void){return PS_SHELF;}
static const ps_navigation_entry_t navigation_fixture[]={
    {PS_NAV_BOOKS,PS_SHELF,PI_BOOK,"书架",NULL},
    {PS_NAV_HOME,PS_HA,PI_HOME,"家庭",NULL},
    {PS_NAV_GAMES,PS_GAMES,PI_GAME,"小游戏",NULL}
};
int ps_navigation_count(void){return 3;}
const ps_navigation_entry_t *ps_navigation_item(int index){return index>=0&&index<3?&navigation_fixture[index]:NULL;}
int ps_navigation_index(ps_route_t route){for(int i=0;i<3;i++)if(navigation_fixture[i].route==route)return i;return -1;}
void epd_draw_pixel(int x,int y,uint8_t color,uint8_t *fb);
static stbtt_fontinfo font;
static int font_weight=400,last_foreground=-1,last_background=-1,last_draw_weight=-1;
int ttf_get_weight(void){return font_weight;}
void ttf_set_weight(int weight){font_weight=weight;}
static uint32_t decode(const char **text){const unsigned char*s=(const unsigned char*)*text;uint32_t cp;int n;if(s[0]<128){cp=s[0];n=1;}else if(s[0]<224){cp=s[0]&31;n=2;}else if(s[0]<240){cp=s[0]&15;n=3;}else{cp=s[0]&7;n=4;}for(int i=1;i<n;i++)cp=(cp<<6)|(s[i]&63);*text+=n;return cp;}
static int advance(int height,uint32_t cp){int width,lsb;stbtt_GetCodepointHMetrics(&font,cp,&width,&lsb);return (int)lroundf(width*stbtt_ScaleForPixelHeight(&font,(float)height));}
int ttf_text_width_px(int height,const char *text){int width=0;while(*text)width+=advance(height,decode(&text));return width;}
int ttf_ascender_px(int height){int asc,des,gap;stbtt_GetFontVMetrics(&font,&asc,&des,&gap);return(int)lroundf(asc*stbtt_ScaleForPixelHeight(&font,(float)height));}
void ttf_draw_text_px(uint8_t *fb,int x,int y,int height,const char *text,enum EpdFontFlags align,uint8_t fg,uint8_t bg){
    last_foreground=fg;last_background=bg;last_draw_weight=font_weight;
    if(align&EPD_DRAW_ALIGN_CENTER)x-=ttf_text_width_px(height,text)/2;
    float scale=stbtt_ScaleForPixelHeight(&font,(float)height);
    while(*text){uint32_t cp=decode(&text);int w,h,dx,dy;unsigned char *bits=stbtt_GetCodepointBitmap(&font,scale,scale,cp,&w,&h,&dx,&dy);
        for(int row=0;row<h;row++)for(int col=0;col<w;col++){int a=bits[row*w+col];if(a){int c=bg+(int)lroundf(powf(a/255.f,0.6f)*(fg-(int)bg));epd_draw_pixel(x+dx+col,y+dy+row,c*17,fb);}}
        stbtt_FreeBitmap(bits,NULL);x+=advance(height,cp);
    }
}
'''
tests = r'''
static void prepare(int gray){memset(canvas.pre,0xA5,GUARD);memset(canvas.post,0x5A,GUARD);memset(canvas.fb,gray*17,FRAME_BYTES);}
static void guards(void){for(int i=0;i<GUARD;i++)CHECK(canvas.pre[i]==0xA5&&canvas.post[i]==0x5A,"packed frame guard");}
static int pixel(int x,int y){EpdRect q=ui_rotate_rect_to_fb((EpdRect){x,y,1,1});return epd_get_pixel(q.x,q.y,WIDTH,HEIGHT,canvas.fb)>>4;}
static int logical_pixel(int x,int y){return pixel(px(x),px(y));}
static void gray_levels(void){
    for(int gray=0;gray<16;gray++){
        prepare(15);ps_box(canvas.fb,10,10,50,40,8,gray,true);
        CHECK(logical_pixel(30,30)==gray,"filled center retains exact gray");
        ps_line(canvas.fb,70,20,120,20,gray);CHECK(logical_pixel(90,20)==gray,"line retains exact gray");
        ps_box(canvas.fb,140,10,50,40,8,gray,false);CHECK(logical_pixel(140,30)==gray,"outline retains exact gray");
        ps_icon(canvas.fb,210,20,24,PI_PLUS,gray);CHECK(logical_pixel(222,32)==gray,"icon retains exact gray");
        guards();
    }
    prepare(15);ps_box(canvas.fb,10,10,80,60,18,0,true);
    unsigned levels=0;for(int y=px(10);y<px(70);y++)for(int x=px(10);x<px(90);x++)levels|=1u<<pixel(x,y);
    CHECK(__builtin_popcount(levels)>=10,"rounded boundary uses many gray AA levels");
    prepare(13);ps_box(canvas.fb,10,10,80,60,18,1,true);
    for(int y=px(10);y<px(70);y++)for(int x=px(10);x<px(90);x++)CHECK(pixel(x,y)>=1&&pixel(x,y)<=13,"AA blends existing gray background");
    prepare(15);ps_text(canvas.fb,20,30,13,"灰阶文字",5);
    CHECK(last_foreground==5&&last_background==15,"text retains exact foreground and painted background");
    CHECK(last_draw_weight==600&&font_weight==400,"small UI weight600 then restore");
}
static int oracle_line_coverage(int ax,int ay,int bx,int by,int x,int y){
    int hits=0;int64_t dx=(int64_t)(bx-ax)*8,dy=(int64_t)(by-ay)*8,len=dx*dx+dy*dy;
    for(int sy=1;sy<8;sy+=2)for(int sx=1;sx<8;sx+=2){int64_t qx=(x-ax)*8+sx-4,qy=(y-ay)*8+sy-4,p=qx*dx+qy*dy;
        if(!len||p<=0)hits+=qx*qx+qy*qy<=144;
        else if(p>=len){qx-=dx;qy-=dy;hits+=qx*qx+qy*qy<=144;}
        else{int64_t c=qx*dy-qy*dx;hits+=c*c<=144*len;}
    }
    return hits;
}
static uint32_t rng=99127;
static unsigned random_u32(void){rng^=rng<<13;rng^=rng>>17;rng^=rng<<5;return rng;}
static void line_oracle(void){
    ps_view_t v=ps_view();
    for(int n=0;n<80;n++){
        int ax=(int)(random_u32()%120)-30,ay=(int)(random_u32()%100)-25;
        int bx=(int)(random_u32()%120)-30,by=(int)(random_u32()%100)-25;
        int gray=random_u32()%15,background=15;
        prepare(background);memset(oracle,255,FRAME_BYTES);ps_line(canvas.fb,ax,ay,bx,by,gray);
        ax=px(ax);ay=px(ay);bx=px(bx);by=px(by);
        for(int y=0;y<px(80);y++)for(int x=0;x<px(100);x++){
            int c=oracle_line_coverage(ax,ay,bx,by,x,y);
            if(c)epd_draw_pixel(x,y,((background*(16-c)+gray*c+8)/16)*17,oracle);
        }
        CHECK(!memcmp(canvas.fb,oracle,FRAME_BYTES),"sparse line bounds equal exhaustive AA oracle");guards();
    }
    prepare(15);ps_line(canvas.fb,-80,-50,-5,-1,0);for(int i=0;i<FRAME_BYTES;i++)CHECK(canvas.fb[i]==255,"offscreen line never clamps to a false edge");
    ps_line(canvas.fb,-20,v.h-3,v.w+20,v.h+5,2);guards();
}
static void clipping(void){
    ps_view_t v=ps_view();int xs[]={-30,-2,0,1,v.w-1,v.w+1};int ys[]={-30,-2,0,1,v.h-1,v.h+1};
    for(unsigned i=0;i<sizeof(xs)/sizeof(xs[0]);i++)for(unsigned j=0;j<sizeof(ys)/sizeof(ys[0]);j++){
        prepare(14);ps_box(canvas.fb,xs[i],ys[j],40,40,12,3,true);ps_box(canvas.fb,xs[i],ys[j],60,60,18,5,false);
        ps_icon(canvas.fb,xs[i],ys[j],24,PI_SETTINGS,2);guards();
    }
    prepare(14);ps_box(canvas.fb,-20,-20,50,50,8,3,true);CHECK(logical_pixel(10,10)==3,"partially offscreen box remains visible");CHECK(logical_pixel(32,32)==14,"outside clipped shape preserved");
    memcpy(oracle,canvas.fb,FRAME_BYTES);ps_box(canvas.fb,0,0,0,50,4,0,true);ps_box(canvas.fb,0,0,50,-1,4,0,false);CHECK(!memcmp(oracle,canvas.fb,FRAME_BYTES),"empty shapes do not paint");
    ps_box(NULL,0,0,20,20,10,1,true);ps_line(NULL,0,0,20,20,1);ps_icon(NULL,10,10,24,PI_CLOCK,1);guards();
}
static void scene(void){
    prepare(PS_THEME_PAPER);ps_tabs(canvas.fb,0);
    ps_text(canvas.fb,18,89,24,"让阅读回到纸上",PS_THEME_INK);
    ps_text(canvas.fb,18,121,13,"灰阶层次 · 安静清晰",PS_THEME_SECONDARY);
    ps_surface(canvas.fb,16,158,328,150,true);
    ps_text(canvas.fb,32,177,13,"继续阅读",PS_THEME_SECONDARY);
    ps_text(canvas.fb,32,210,22,"最重要的事只有一件",PS_THEME_INK);
    ps_text(canvas.fb,32,245,13,"上次读到 · 第 128 页",PS_THEME_SECONDARY);
    ps_button(canvas.fb,32,269,112,32,"继续阅读",true);
    ps_surface(canvas.fb,16,326,328,110,false);
    ps_text(canvas.fb,32,343,18,"我的图书",PS_THEME_INK);
    ps_button(canvas.fb,32,382,128,36,"封面视图",false);ps_button(canvas.fb,174,382,136,36,"列表视图",false);
    for(int i=0;i<12;i++)ps_icon(canvas.fb,28+(i%6)*53,467+(i/6)*54,24,(ps_icon_t)i,PS_THEME_INK);
    for(int i=0;i<16;i++)ps_box(canvas.fb,18+i*20,579,18,24,4,i,true);
    ps_corner_controls(canvas.fb);
}
static void rotations(void){
    for(int wide=0;wide<2;wide++){
        epd_set_rotation(wide?EPD_ROT_LANDSCAPE:EPD_ROT_PORTRAIT);scene();
        int w=epd_rotated_display_width(),h=epd_rotated_display_height();
        for(int y=0;y<h;y++)for(int x=0;x<w;x++)reference[y*w+x]=(uint8_t)pixel(x,y);
        epd_set_rotation(wide?EPD_ROT_INVERTED_LANDSCAPE:EPD_ROT_INVERTED_PORTRAIT);scene();
        for(int y=0;y<h;y++)for(int x=0;x<w;x++)CHECK(reference[y*w+x]==pixel(x,y),"AA sampling is identical in opposite rotations");
        guards();
    }
}
static bool save(const char *path){FILE*f=fopen(path,"wb");if(!f)return false;int w=epd_rotated_display_width(),h=epd_rotated_display_height();fprintf(f,"P5\n%d %d\n255\n",w,h);for(int y=0;y<h;y++)for(int x=0;x<w;x++)fputc(pixel(x,y)*17,f);return fclose(f)==0;}

static const char *icon_labels[PI_COUNT]={"book","home","settings","back","close","search","import","note","bookmark","align","clock","rotate","wifi","light","switch","fan","thermo","climate","printer","game","check","plus","minus","refresh","lock","more","bluetooth","highlight","list","heart","battery","usb","SD card","sliders","pause","play","power","grid","link","droplet"};
static int icon_oracle_coverage(ps_icon_t icon,int x,int y,int size,int sample_x,int sample_y){
    ps_icon_shape_t shape=ps_icon_shapes[icon];uint16_t mask=0;
    int ox=px(x)*8,oy=px(y)*8,native_size=px(size),radius=(native_size+1)/3;
    for(unsigned i=0;i<shape.count;i++){
        ps_icon_segment_t line=ps_icon_segments[shape.first+i];
        int ax=icon_coordinate(ox,line.ax,native_size),ay=icon_coordinate(oy,line.ay,native_size);
        int bx=icon_coordinate(ox,line.bx,native_size),by=icon_coordinate(oy,line.by,native_size);
        mask=icon_segment_mask(ax,ay,bx,by,radius,sample_x,sample_y,mask);
    }
    return __builtin_popcount((unsigned)mask);
}
static void icon_family(void){
    static const int sizes[]={18,20,22,24,32,48,60};
    for(int icon=0;icon<PI_COUNT;icon++)CHECK(ps_icon_shapes[icon].count>0,"every semantic icon has geometry");
    for(int rotation=0;rotation<4;rotation++){
        epd_set_rotation((enum EpdRotation)rotation);
        for(int icon=0;icon<PI_COUNT;icon++)for(unsigned s=0;s<sizeof(sizes)/sizeof(*sizes);s++){
            int size=sizes[s],origin_x=px(37),origin_y=px(51),span=px(size)+2;unsigned aa=0,ink=0;
            prepare(15);ps_icon(canvas.fb,37,51,size,(ps_icon_t)icon,0);
            for(int y=-1;y<=span;y++)for(int x=-1;x<=span;x++){
                int level=pixel(origin_x+x,origin_y+y);reference[(y+1)*(span+2)+x+1]=(uint8_t)level;
                if(level>0&&level<15)aa++;if(level==0)ink++;
                if(x<0||y<0||x>span-2||y>span-2)CHECK(level==15,"glyph stays inside requested icon box");
                if((size==24||size==32||size==48)&&rotation==0){int coverage=icon_oracle_coverage((ps_icon_t)icon,37,51,size,origin_x+x,origin_y+y);
                    CHECK(level==(15*(16-coverage)+8)/16,"sparse vector raster equals exhaustive stroke-union oracle");}
            }
            CHECK(aa>0&&ink>0,"each glyph has a legible core and antialiased edge");guards();
            prepare(0);ps_icon(canvas.fb,37,51,size,(ps_icon_t)icon,15);
            for(int y=-1;y<=span;y++)for(int x=-1;x<=span;x++){
                int a=reference[(y+1)*(span+2)+x+1],b=pixel(origin_x+x,origin_y+y);
                CHECK((a<15)==(b>0),"selected and normal states have identical geometry");
                CHECK(abs((15-a)-b)<=1,"inverse AA differs by at most one gray rounding step");
            }
            guards();
            if(rotation==0&&size==32){memcpy(oracle,canvas.fb,FRAME_BYTES);ps_icon(canvas.fb,37,51,0,(ps_icon_t)icon,0);ps_icon(canvas.fb,37,51,-1,(ps_icon_t)icon,0);CHECK(!memcmp(oracle,canvas.fb,FRAME_BYTES),"empty glyph does not paint");}
        }
        ps_view_t view=ps_view();
        for(int icon=0;icon<PI_COUNT;icon++)for(int edge=0;edge<4;edge++){
            prepare(14);int x=edge&1?view.w-10:-20,y=edge&2?view.h-10:-20;
            ps_icon(canvas.fb,x,y,48,(ps_icon_t)icon,1);guards();
        }
    }
    // 反向旋转也必须保留同一子像素采样。/ Opposite rotations preserve identical subpixel coverage.
    for(int wide=0;wide<2;wide++)for(int icon=0;icon<PI_COUNT;icon++){
        epd_set_rotation(wide?EPD_ROT_LANDSCAPE:EPD_ROT_PORTRAIT);prepare(13);ps_icon(canvas.fb,37,51,48,(ps_icon_t)icon,2);
        int w=px(48)+2;for(int y=0;y<w;y++)for(int x=0;x<w;x++)reference[y*w+x]=(uint8_t)pixel(px(37)+x,px(51)+y);
        epd_set_rotation(wide?EPD_ROT_INVERTED_LANDSCAPE:EPD_ROT_INVERTED_PORTRAIT);prepare(13);ps_icon(canvas.fb,37,51,48,(ps_icon_t)icon,2);
        for(int y=0;y<w;y++)for(int x=0;x<w;x++)CHECK(reference[y*w+x]==pixel(px(37)+x,px(51)+y),"all glyphs match in opposite rotations");
    }
    puts("40 shared vector icons: sizes18/20/22/24/32/48/60, AA, stroke-union, inverse geometry, clipping and 4 rotations PASS");
}
static void icon_atlases(const char *preview){
    char directory[1024];snprintf(directory,sizeof(directory),"%s",preview);char *slash=strrchr(directory,'/');if(slash)*slash=0;else snprintf(directory,sizeof(directory),".");
    int sizes[]={24,32,48};epd_set_rotation(EPD_ROT_PORTRAIT);
    for(int variant=0;variant<2;variant++)for(unsigned s=0;s<sizeof(sizes)/sizeof(*sizes);s++){
        int size=sizes[s];prepare(variant?2:15);
        for(int icon=0;icon<PI_COUNT;icon++){
            int center=36+(icon%5)*72,top=12+(icon/5)*78;
            ps_icon(canvas.fb,center-size/2,top,size,(ps_icon_t)icon,variant?15:1);
            char label[40];snprintf(label,sizeof(label),"%02d %s",icon,icon_labels[icon]);text(canvas.fb,center,top+54,10,label,variant?15:1,true,false,false);
        }
        char path[1200];snprintf(path,sizeof(path),"%s/icons-%d-%s.pgm",directory,size,variant?"inverse":"normal");CHECK(save(path),"save actual native icon atlas");
    }
}

int main(int argc,char **argv){
    CHECK(argc==3,"font and preview paths provided");if(argc!=3)return 2;
    FILE*f=fopen(argv[1],"rb");if(!f)return 2;fseek(f,0,SEEK_END);long n=ftell(f);rewind(f);uint8_t*font_bytes=malloc(n);if(!font_bytes)return 2;CHECK(fread(font_bytes,1,n,f)==(size_t)n,"read font");fclose(f);CHECK(stbtt_InitFont(&font,font_bytes,0),"initialize real TTF");
    for(int r=0;r<4;r++){epd_set_rotation((enum EpdRotation)r);gray_levels();line_oracle();clipping();}
    rotations();icon_family();icon_atlases(argv[2]);epd_set_rotation(EPD_ROT_PORTRAIT);scene();CHECK(save(argv[2]),"save native portrait preview");
    prepare(15);ps_tabs(canvas.fb,1);CHECK(logical_pixel(100,75)==15,"navigation remains inside reserved top band");
    for(int r=0;r<4;r++){
        epd_set_rotation((enum EpdRotation)r);prepare(15);font_weight=400;
        navigation_label(canvas.fb,80,50,54,"很长的自定义应用名称ABCDE",2);
        CHECK(font_weight==400,"navigation label restores font weight");
        int changed=0,w=epd_rotated_display_width(),h=epd_rotated_display_height();
        for(int y=0;y<h;y++)for(int x=0;x<w;x++)if(pixel(x,y)!=15){
            CHECK(x>=px(80)&&x<px(134)&&y>=px(50)&&y<px(69),"long navigation label stays in its own cell");changed++;
        }
        CHECK(changed>0,"long navigation label remains legible");guards();
    }
    printf("modern primitives: %u checks; %u failures; 4 native rotations\n",checks,failures);free(font_bytes);return failures?1:0;
}
'''
with tempfile.TemporaryDirectory(prefix='pico-modern-draw-') as td:
    td=Path(td)
    source=td/'test.c'
    source.write_text(prelude+types+platform+actual+draw+tests)
    exe=td/'test'
    flags=shlex.split(os.environ.get('CFLAGS',''))
    subprocess.run([os.environ.get('CC','cc'),'-std=c11','-O2','-Wall','-Wextra','-Werror',
                    '-I'+str(ROOT/'main/system'),'-I'+str(ROOT/'main/font'),*flags,str(source),'-lm','-o',str(exe)],check=True)
    subprocess.run([str(exe),str(ROOT/'main/assets/builtin.ttf'),str(OUT/'primitives.pgm')],check=True)
# The artifact also works with the system Python, without optional imaging packages.
with (OUT/'primitives.pgm').open('rb') as image:
    assert image.readline() == b'P5\n'
    width, height = map(int, image.readline().split())
    assert image.readline() == b'255\n'
    pixels = image.read()
def chunk(kind, body):
    return struct.pack('>I', len(body)) + kind + body + struct.pack('>I', zlib.crc32(kind+body))
scanlines = b''.join(b'\0' + pixels[y*width:(y+1)*width] for y in range(height))
(OUT/'primitives.png').write_bytes(b'\x89PNG\r\n\x1a\n' +
    chunk(b'IHDR', struct.pack('>IIBBBBB', width, height, 8, 0, 0, 0, 0)) +
    chunk(b'IDAT', zlib.compress(scanlines)) + chunk(b'IEND', b''))
luminance=lambda value: ((value/15+0.055)/1.055)**2.4 if value/15>0.04045 else value/15/12.92
report={
    'source_sha256':hashlib.sha256((ROOT/'main/system/pico_draw.c').read_bytes()).hexdigest(),
    'secondary_5_on_inset_13_contrast':(luminance(13)+0.05)/(luminance(5)+0.05),
    'cursor_unchanged':True,
    'no_draw_heap_allocations':True,
    'preview':'primitives.png',
    'physical_waveform_verified':False,
}
assert report['secondary_5_on_inset_13_contrast'] >= 4.5
(OUT/'verification.json').write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps(report,indent=2))

for pgm in sorted(OUT.glob('icons-*.pgm')):
    with pgm.open('rb') as image:
        assert image.readline() == b'P5\n'
        iw, ih = map(int, image.readline().split())
        assert image.readline() == b'255\n'
        data = image.read()
    rows = b''.join(b'\0' + data[y*iw:(y+1)*iw] for y in range(ih))
    pgm.with_suffix('.png').write_bytes(b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', iw, ih, 8, 0, 0, 0, 0)) + chunk(b'IDAT', zlib.compress(rows)) + chunk(b'IEND', b''))

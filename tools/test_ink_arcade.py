#!/usr/bin/env python3
"""Production game rules: memory bounds, clears, bags, maze reachability, pause/save and bounded timers."""
from pathlib import Path
import subprocess,tempfile
root=Path(__file__).resolve().parents[1]
source=r'''
#include <assert.h>
#include <string.h>
#include <stdio.h>
#include "SOURCE"
static int64_t now;static unsigned writes;static bool fail;
static state_t saved;
static int role;
static int32_t duo_role(void){return role;}
static int64_t time_us(void){return now*1000;}
static uint32_t random32(void){static uint32_t seed=123;seed=seed*1664525+1013904223;return seed;}
static bool load(const char*k,void*out,size_t n){(void)k;(void)out;(void)n;return false;}
static bool store(const char*k,const void*p,size_t n){(void)k;assert(n==sizeof(saved));writes++;if(fail)return false;memcpy(&saved,p,n);return true;}

static unsigned solid_blocks;
static pico_app_view_t view(void){return (pico_app_view_t){360,640,false};}
static void fill(uint8_t*f,int x,int y,int w,int h,int g){(void)f;(void)x;(void)y;(void)w;(void)h;(void)g;}
static void box(uint8_t*f,int x,int y,int w,int h,int r,int g,bool solid){(void)f;(void)x;(void)y;(void)w;(void)h;(void)r;if(solid){assert(g>=0&&g<8);solid_blocks++;}}
static void text(uint8_t*f,int x,int y,int size,const char*s,int g){(void)f;(void)x;(void)y;(void)size;(void)s;(void)g;}
static void button(uint8_t*f,int x,int y,int w,int h,const char*s,bool on){(void)f;(void)x;(void)y;(void)w;(void)h;(void)s;(void)on;}
static void heading(uint8_t*f,const char*a,const char*b){(void)f;(void)a;(void)b;}
int main(void){
 pico_app_host_t host={.abi=1,.bytes=sizeof(host),.copy=memcpy,.set=memset,.compare=memcmp,.duo_role=duo_role,.random=random32,.time_us=time_us,.store=store,.load=load,.view=view,.format=snprintf,.fill=fill,.box=box,.text=text,.center=text,.button=button,.heading=heading};
 assert(start(&host)&&S.paused&&valid());assert(!tick()&&!writes);
 if(PICO_GAME_KIND==6){
   // 七种实际绘制方块在DU阈值后均须可见。/ Every rendered tetromino must survive DU thresholding.
   for(int piece=0;piece<7;piece++){S.piece=piece;S.x=3;S.y=3;S.cells[190]=piece+1;solid_blocks=0;render(NULL);assert(solid_blocks>=5);S.cells[190]=0;}

   for(int j=0;j<100;j++){S.bag_at=7;unsigned mask=0;for(int i=0;i<7;i++)mask|=1u<<bag_take();assert(mask==127);}
   reset();S.paused=0;S.piece=0;S.rotation=0;S.x=3;S.y=18;
   for(int x=0;x<10;x++)if(x<3||x>6)S.cells[190+x]=1;
   freeze();assert(S.lines==1&&S.score==100);
   for(int j=0;j<500;j++){if(S.over){reset();S.paused=0;}action(random32()%5);assert(S.over||fits(S.x,S.y,S.rotation));assert(valid());}
 }else{
   // 每一颗豆都可达，没有不可完成关卡。/ Every pellet is reachable; no impossible level.
   bool seen[289]={0};int q[289],head=0,tail=0;q[tail++]=18;seen[18]=true;
   while(head<tail){int at=q[head++];const int d[]={-1,1,-17,17};for(int i=0;i<4;i++){int n=at+d[i];if(n>=0&&n<289&&!seen[n]&&walk(n%17,n/17)){seen[n]=true;q[tail++]=n;}}}
   for(int i=0;i<289;i++)assert(!S.cells[i]||seen[i]);
   S.x=1;S.y=3;S.dx=0;S.dy=-1;S.paused=0;S.cells[S.y*17+S.x]=2;S.dy=0;S.dx=-1;maze_step();assert(S.power>0);
   S.power=0;S.gx[0]=S.x;S.gy[0]=S.y;unsigned lives=S.lives;collision();assert(S.lives==lives-1&&S.paused);
 }

 // 按下与滑动越阈值立即执行，松手不重复；角落不映射游戏按键。
 // Press/threshold acts immediately, release does not repeat; corners are never game buttons.
 reset();action(5);S.piece=1;S.rotation=0;S.x=3;S.y=0;
 if(PICO_GAME_KIND==7){S.x=3;S.y=1;S.dx=1;S.dy=0;}
 pico_app_event_t e={.type=PGA_PRESS,.x=40,.y=568};
 int before_x=S.x;assert(event(&e));
 if(PICO_GAME_KIND==6)assert(S.x==before_x-1);
 else{assert(deadline==now);assert(tick());assert(S.x==before_x-1);assert(!tick());}
 before_x=S.x;e.type=PGA_RELEASE;event(&e);e.type=PGA_TAP;assert(event(&e)&&S.x==before_x);
 e=(pico_app_event_t){.type=PGA_PRESS,.x=20,.y=620};assert(!event(&e));
 if(PICO_GAME_KIND==6){
  int x,y,c;geometry(&x,&y,&c);S.x=3;
  e=(pico_app_event_t){.type=PGA_PRESS,.x=x+2*c,.y=y+2*c};event(&e);
  e.type=PGA_MOVE;e.x0=e.x;e.y0=e.y;e.x+=30;assert(event(&e)&&S.x==4);
  e.type=PGA_RELEASE;event(&e);e.type=PGA_SWIPE_R;assert(event(&e)&&S.x==4);
  e.type=PGA_CANCEL;event(&e);assert(!contact_handled&&!swipe_tracking);
 }
 assert(writes==0);
 reset();action(5);now=100000;assert(tick());int y=S.y;assert(!tick()&&S.y==y);assert(writes==0);
 stop();now+=100000;assert(!tick()&&S.paused);assert(save()&&writes==1&&!dirty);
 dirty=true;fail=true;assert(!save()&&dirty);fail=false;
 S.piece=255;S.x=127;assert(!valid());
 reset();role=PGA_DUO_SCREEN;tick();action(5);assert(!S.paused);
 role=PGA_DUO_NONE;render(NULL);tick();assert(S.paused);
 role=PGA_DUO_CONTROLLER;assert(start(&host));state_t before=S;now+=100000;tick();assert(!memcmp(&S,&before,sizeof(S)));
 role=PGA_DUO_NONE;now+=100000;tick();assert(!memcmp(&S,&before,sizeof(S)));
 printf("PASS kind %d: rules, bounds, pause/save/failure, no catch-up or per-tick writes; state=%zu bytes\n",PICO_GAME_KIND,sizeof(S));
}
'''.replace('SOURCE',str(root/'games/src/ink_arcade.c'))
with tempfile.TemporaryDirectory() as d:
 d=Path(d);(d/'test.c').write_text(source)
 for kind in (6,7):
  subprocess.run(['cc','-std=c11','-O1','-g','-Wall','-Wextra','-Wno-unused-function','-fsanitize=address,undefined',f'-DPICO_GAME_KIND={kind}','-DPICO_GAME_ID="test"','-I',str(root/'components/read_pico_apps/include'),str(d/'test.c'),'-o',str(d/'test')],check=True)
  subprocess.run([str(d/'test')],check=True)

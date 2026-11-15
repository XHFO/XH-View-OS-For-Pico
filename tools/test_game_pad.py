#!/usr/bin/env python3
"""共享手柄的几何、触摸去重和ABI边界。/ Shared pad geometry, gesture deduplication and ABI bounds."""
from pathlib import Path
import subprocess,tempfile
root=Path(__file__).resolve().parents[1]
source=r'''
#include <assert.h>
#include <string.h>
#include <stdio.h>
#include "PAD"
static pico_app_view_t viewport;
static pico_app_view_t view(void){return viewport;}
static pga_pad_state_t packet;
static int packet_size,reads,writes;
static int32_t receive(void*out,size_t cap){reads++;assert(cap==sizeof(packet));memcpy(out,&packet,sizeof(packet));return packet_size;}
static bool publish(const void*in,size_t bytes){writes++;assert(bytes==20);memcpy(&packet,in,bytes);return true;}
static pico_app_host_t H={.abi=1,.bytes=sizeof(H),.view=view,.duo_packet_recv=receive,.duo_packet_send=publish};
int main(void){
 const int codes[]={0,1,2,3,PAD_A,PAD_B,PAD_OPTIONS,PGA_DUO_PAUSE};
 for(int land=0;land<2;land++){
  viewport=(pico_app_view_t){land?640:360,land?360:640,land};
  for(unsigned i=0;i<8;i++){
   int x,y,w,h;pad_rect(viewport,codes[i],&x,&y,&w,&h);assert(w>=44&&h>=44&&x>=16&&x+w<=viewport.w-16&&y>=72&&y+h<=viewport.h-48);
   for(unsigned j=i+1;j<8;j++){int a,b,c,d;pad_rect(viewport,codes[j],&a,&b,&c,&d);assert(x+w<=a||a+c<=x||y+h<=b||b+d<=y);}
   pico_app_event_t e={.type=PGA_PRESS,.x=x+w/2,.y=y+h/2};int result=pad_input(&H,&e);
   if(codes[i]>=4&&codes[i]<PAD_A){assert(result==-1);e.type=PGA_RELEASE;assert(pad_input(&H,&e)==-1);e.type=PGA_TAP;assert(pad_input(&H,&e)==codes[i]);}
   else{assert(result==codes[i]);e.type=PGA_RELEASE;assert(pad_input(&H,&e)==-1);e.type=PGA_TAP;assert(pad_input(&H,&e)==-1);}
  }
  pico_app_event_t e={.type=PGA_PRESS,.x=viewport.w/2,.y=220};assert(pad_input(&H,&e)==-1);e.type=PGA_MOVE;e.x+=24;assert(pad_input(&H,&e)==1);e.x+=30;assert(pad_input(&H,&e)==-1);e.type=PGA_RELEASE;pad_input(&H,&e);e.type=PGA_SWIPE_R;assert(pad_input(&H,&e)==-1);
  e.type=PGA_CANCEL;pad_input(&H,&e);assert(!pad_handled&&!pad_tracking);
  e=(pico_app_event_t){.type=PGA_PRESS,.x=20,.y=viewport.h-20};assert(pad_input(&H,&e)==-1);e.type=PGA_TAP;assert(pad_input(&H,&e)==-1);
 }
 pga_pad_state_t state={.kind=4,.choice=4},before=state;packet_size=20;
 packet=(pga_pad_state_t){.kind=5};assert(!pad_receive(&H,&state,4)&&!memcmp(&state,&before,20));
 packet.kind=4;packet.paused=2;assert(!pad_receive(&H,&state,4));packet.paused=0;packet_size=19;assert(!pad_receive(&H,&state,4));
 packet_size=20;assert(pad_receive(&H,&state,4));assert(pad_publish(&H,&state)&&writes==1);
 unsigned count=reads;H.bytes=offsetof(pico_app_host_t,duo_packet_recv);assert(!pad_receive(&H,&state,4)&&reads==(int)count);
 H.bytes=offsetof(pico_app_host_t,duo_packet_send);assert(!pad_publish(&H,&state)&&writes==1);
 puts("PASS: portrait/landscape geometry, no overlaps, >=44 targets, press/swipe dedupe, release-only SELECT/START, corner exclusion, packet kind/length/flag rejection, truncated SDK bounds");
}
'''.replace('"PAD"','"'+str(root/'games/sdk/pico_game_pad.h')+'"')
with tempfile.TemporaryDirectory() as directory:
 d=Path(directory);(d/'test.c').write_text(source)
 subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',str(d/'test.c'),'-o',str(d/'test')],check=True)
 subprocess.run([str(d/'test')],check=True)

#!/usr/bin/env python3
"""有界游戏按键队列及任一端开游戏。/ Bounded input queues and either-side game opening."""
from pathlib import Path
import tempfile,subprocess
root=Path(__file__).resolve().parents[1]
source=r'''
#include "CORE"
#include <assert.h>
int main(void){
 dual_link_t *a=calloc(1,sizeof(*a)),*b=calloc(1,sizeof(*b));assert(a&&b);
 a->cfg.role=1;b->cfg.role=2;a->control_connected=b->control_connected=true;a->now=b->now=1000;
 uint8_t p[DL_CONTROL_MAX],code;size_t n;
 for(int i=0;i<8;i++)assert(dl_game_input(b,(uint8_t)i));assert(!dl_game_input(b,8));
 for(int i=0;i<8;i++){n=dl_control_take(b,p,sizeof(p));assert(n==2&&p[0]==20&&p[1]==i);assert(dl_control_receive(a,p,n));}
 for(int i=0;i<8;i++){assert(dl_game_take(a,&code)&&code==i);}assert(!dl_game_take(a,&code));
 for(int i=0;i<8;i++){p[0]=20;p[1]=(uint8_t)i;assert(dl_control_receive(a,p,2));}assert(!dl_control_receive(a,p,2));
 p[0]=23;assert(dl_control_receive(a,p,1)&&!dl_game_take(a,&code));
 assert(dl_game_input(b,1));dl_control_connected(b,false);assert(!b->game_tx_count);dl_control_connected(b,true);assert(!b->game_tx_count);
 a->control_connected=b->control_connected=true;b->orientation_dirty=false;b->catalog_dirty=false;
 assert(dl_game_open(b,7));n=dl_control_take(b,p,sizeof(p));assert(n==2&&p[0]==21&&p[1]==7&&dl_control_receive(a,p,n));assert(dl_game_open_take(a,&code)&&code==7);
 assert(!dl_game_open(a,8));p[0]=21;p[1]=255;assert(!dl_control_receive(a,p,2));
 assert(dl_game_input(b,2));assert(dl_game_close(b)&&!b->game_tx_count);
 free(a);free(b);puts("PASS: 8 FIFO inputs, capacity reject, no overwrite, disconnect/close clears pending actions, secondary opens kind7, invalid kind rejected");
}
'''.replace('CORE',str(root/'main/system/dual_link.c'))
with tempfile.TemporaryDirectory() as d:
 d=Path(d);(d/'test.c').write_text(source)
 subprocess.run(['cc','-std=gnu11','-O1','-g','-Wno-deprecated-declarations','-fsanitize=address,undefined',str(d/'test.c'),'-o',str(d/'test')],check=True)
 subprocess.run([str(d/'test')],check=True)

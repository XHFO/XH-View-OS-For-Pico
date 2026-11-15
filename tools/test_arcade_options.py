#!/usr/bin/env python3
"""棋盘选择、穿墙及存档迁移的生产C回归。/ Production-C grid, wrap and save migration regression."""
from pathlib import Path
import subprocess,tempfile
root=Path(__file__).resolve().parents[1]
source=r'''
#define main old_ui_main
#include "UI"
#undef main
int main(void){
 ps_games_init();test_clock_us=1000000000;
 for(int n=3;n<=5;n++)for(int dir=0;dir<4;dir++){
  ps_games_action(0);a_grid=n;a_setup=false;pa_reset();memset(pa_cells(),0,(size_t)n*n*4);a_save.best=100;
  uint32_t *b=pa_cells();b[0]=b[dir%2?1:n]=2;unsigned writes=test_stores;
  assert(pa_shift(dir)&&a_save.score==4&&a_save.moves==1&&a_save.best==100&&test_stores==writes&&pa_valid());
  int at=dir==0?0:dir==1?n-1:dir==2?(n-1)*n:0;assert(b[at]==4);
  assert(pa_persist());uint32_t copy[25]={0};memcpy(copy,b,n*n*4);pa_init();assert(a_grid==n&&!memcmp(copy,pa_cells(),n*n*4));
 }
 ps_games_action(0);a_grid=4;pa_reset();a_save.moves=3;a_save.best=1000;a_save.board[0]=64;
 pa_options();pa_save_t before=a_save;assert(pa_control(12)&&a_choice==5&&a_grid==4);assert(pa_control(8)&&a_confirm&&a_grid==4);
 assert(pa_control(9)&&!a_confirm&&!memcmp(&before,&a_save,sizeof(before)));
 assert(pa_control(8)&&a_confirm);assert(pa_control(8)&&!a_setup&&a_grid==5&&a_save.moves==0&&a_save.best==1000);
 assert(pa_persist());pa_init();assert(a_grid==5&&a_save.best==1000&&pa_valid());
 ps_games_action(1);a_wrap=1;pa_reset();a_setup=false;a_paused=false;a_dir=a_next=1;a_save.food=1;
 for(int i=0;i<4;i++)a_save.snake[i]=(uint16_t)(219-i);
 assert(pa_step()&&a_save.snake[0]==200&&!a_save.snake_over&&pa_valid());
 assert(pa_persist());pa_init();assert(a_wrap&&a_dir==1&&a_paused&&a_save.snake[0]==200&&pa_valid());
 a_paused=false;a_save.food=200;a_save.length=5;a_save.snake[0]=0;a_save.snake[1]=20;a_save.snake[2]=39;a_save.snake[3]=19;a_save.snake[4]=18;a_dir=a_next=3;
 assert(pa_valid()&&pa_step()&&a_save.snake_over);
 a_wrap=0;pa_reset();a_paused=false;a_dir=a_next=1;for(int i=0;i<4;i++)a_save.snake[i]=(uint16_t)(219-i);
 assert(pa_step()&&a_save.snake_over);
 // v1存档保持只读；迁移后保存v2，再次载入保持内容。
 // Keep v1 read-only; save/reload the migrated v2 record without losing data.
 pa_reset();pa_save_t legacy=a_save;memset(test_arcade_storage,0,sizeof(test_arcade_storage));memcpy(test_arcade_storage,&legacy,sizeof(legacy));test_have_arcade=true;
 pa_init();assert(a_grid==4&&!a_wrap&&!memcmp(&legacy,&a_save,sizeof(legacy)));assert(pa_persist());pa_init();assert(!memcmp(&legacy,&a_save,sizeof(legacy)));
 puts("PASS: 3/4/5 grids x4 directions, OOB/merge, draft cancel/confirm, best preservation, v1/v2 reload, wrap across seam, direction reload, wall/body collision");
}
'''.replace('UI',str(root/'tools/test_pico_games_ui.c'))
with tempfile.TemporaryDirectory() as d:
 d=Path(d);(d/'test.c').write_text(source)
 subprocess.run(['cc','-std=c11','-O1','-g','-Wall','-Wextra','-Werror','-Wno-unused-function','-fsanitize=address,undefined','-I',str(root/'tools/pico_games_test'),str(d/'test.c'),'-o',str(d/'test')],check=True)
 subprocess.run([str(d/'test')],check=True)

#!/usr/bin/env python3
"""游戏输入、批量存档与定稿回归。/ Game input, batched saves and settle regression."""
from pathlib import Path
import subprocess
import tempfile
root = Path(__file__).resolve().parents[1]
source = r'''
#define main existing_ui_main
#include "TEST_UI"
#undef main
int main(void){
 ps_games_init();
 for(int landscape=0;landscape<2;landscape++){
  test_view=(ps_view_t){landscape?640:360,landscape?360:640,landscape};
  test_clock_us=1000000000;ps_games_action(0);pa_reset();a_setup=false;
  memset(a_save.board,0,sizeof(a_save.board));a_save.board[0]=a_save.board[1]=2;
  unsigned stores=test_stores;assert(pa_shift(3)&&a_save.score==4&&test_stores==stores&&a_changed);
  assert(a_settle_due==1000900);test_clock_us+=899000;assert(!ps_games_tick());
  test_clock_us+=1000;assert(ps_games_tick()&&!a_settle_due&&test_stores==stores);
  test_clock_us=1005000000;assert(!ps_games_tick()&&!a_changed&&test_stores==stores+1);
  a_save.board[0]=a_save.board[1]=2;assert(pa_shift(3));test_save_available=false;
  test_clock_us+=5000000;ps_games_tick();ps_games_tick();assert(a_changed&&a_unsaved);
  unsigned failures=test_stores;ps_games_tick();assert(test_stores==failures);
  test_save_available=true;assert(ps_games_storage_save()&&!a_changed&&!a_unsaved);
  pa_save_t snapshot=a_save;pa_init();assert(!memcmp(&snapshot,&a_save,sizeof(snapshot)));
  ps_games_action(1);pa_reset();a_save.food=188;ps_games_action(2);stores=test_stores;
  int x,y,w=44,h=44;if(landscape)pa_snake_single_rect(0,&x,&y,&w,&h);else pa_direction_rect(0,&x,&y);
  ps_event_t e={.type=PS_PRESS,.x=x+w/2,.y=y+h/2};
  assert(ps_games_event(&e)&&a_due==pg_millis());assert(ps_games_tick()&&a_save.snake[0]==188&&a_save.snake_score==10);
  assert(test_stores==stores&&a_changed);unsigned moves=a_save.snake_moves;
  e.type=PS_RELEASE;ps_games_event(&e);e.type=PS_TAP;ps_games_event(&e);assert(!ps_games_tick()&&a_save.snake_moves==moves);
  ps_games_action(5);assert(a_paused&&!a_changed&&test_stores==stores+1);
 }
 puts("PASS: both orientations, immediate Snake turn, food without write, no duplicate release, 2048 900ms settle, 5s batched save, failed save backoff, forced save and old-layout reload");
}
'''.replace('TEST_UI', str(root / 'tools/test_pico_games_ui.c'))
with tempfile.TemporaryDirectory() as directory:
    d=Path(directory); (d/'test.c').write_text(source)
    subprocess.run(['cc','-std=c11','-O1','-g','-Wall','-Wextra','-Werror','-Wno-unused-function','-fsanitize=address,undefined','-I',str(root/'tools/pico_games_test'),str(d/'test.c'),'-o',str(d/'test')],check=True)
    subprocess.run([str(d/'test')],check=True)

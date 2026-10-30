/* SPDX-License-Identifier: Apache-2.0
 * 中文：同源棋类控制器与真实设备绘图的主机截图，不代表物理屏幕验收。
 * English: Host frames from actual game controllers and device drawing, not physical-screen acceptance.
 */
#define PICO_GAMES_UI_HOST
#include "../games/src/builtin_games.c"
#include "pico_framebuffer_host.h"

static ps_view_t host_view={360,640,false};
static ps_route_t host_route=PS_GAMES;
ps_view_t ps_view(void){return host_view;}
ps_route_t ps_route(void){return host_route;}
void ps_navigate(ps_route_t r){host_route=r;}
void ps_dirty(void){}
void ps_toast(const char *s){(void)s;}
bool ps_load(const char *key,void *out,size_t size){(void)key;(void)out;(void)size;return false;}
bool ps_store(const char *key,const void *in,size_t size){(void)key;(void)in;(void)size;return true;}
uint32_t esp_random(void){return 1234;}
int64_t esp_timer_get_time(void){return 0;}
int xTaskCreateWithCaps(void (*fn)(void *),const char *name,unsigned stack,void *arg,int priority,TaskHandle_t *handle,unsigned caps){(void)fn;(void)name;(void)stack;(void)arg;(void)priority;(void)handle;(void)caps;return 0;}
void vTaskDeleteWithCaps(TaskHandle_t task){(void)task;}
void vTaskSuspend(TaskHandle_t task){(void)task;}
unsigned uxTaskGetStackHighWaterMark(TaskHandle_t task){(void)task;return 0;}
void vTaskDelay(unsigned ticks){(void)ticks;}
size_t heap_caps_get_largest_free_block(unsigned caps){(void)caps;return 0;}
size_t heap_caps_get_free_size(unsigned caps){(void)caps;return 0;}
void pg_test_log(const char *tag,const char *format,...){(void)tag;(void)format;}

static void frame(const char *output,const char *name)
{
    uint8_t *fb=pico_fb_new();if(!fb)exit(2);ps_games_render(fb);
    ps_fill(fb,0,0,host_view.w,24,15);ps_text(fb,12,7,10,"离线阅读",5);ps_text(fb,host_view.w-49,7,10,"86%",5);
    char path[768];snprintf(path,sizeof(path),"%s/%s-%s.pgm",output,name,host_view.landscape?"landscape":"portrait");
    if(!pico_fb_save(path,fb))exit(3);free(fb);
}

bool ps_rotation_locked(void){return false;}

static void seed_game(int type)
{
    pg_start_game(type);
    if(type==PG_CHESS){const pg_move_t moves[]={{52,36,0},{12,28,0},{62,45,0},{1,18,0},{61,34,0},{5,26,0}};
        for(unsigned i=0;i<sizeof(moves)/sizeof(*moves);i++)if(!pg_commit(moves[i]))exit(4);g_selected=45;}
    else if(type==PG_XIANGQI){const pg_move_t moves[]={{54,45,0},{27,36,0},{64,37,0},{0,18,0}};
        for(unsigned i=0;i<sizeof(moves)/sizeof(*moves);i++)if(!pg_commit(moves[i]))exit(4);g_selected=82;}
    else{const int go[]={40,41,31,32,49,50,30,42},gomoku[]={112,113,97,98,82,83};const int *moves=type==PG_GO?go:gomoku;int n=type==PG_GO?8:6;
        for(int i=0;i<n;i++)if(!pg_commit(pg_point(moves[i])))exit(4);}
}

int main(int argc,char **argv)
{
    if(argc!=3||!pico_fb_init(argv[1]))return 1;ps_games_init();const char *names[]={"gomoku","go","xiangqi","chess"};
    for(int landscape=0;landscape<2;landscape++){
        host_view=(ps_view_t){landscape?640:360,landscape?360:640,landscape};host_route=PS_GAMES;g_save.active=0;frame(argv[2],"lobby");
        for(int type=0;type<PG_TYPES;type++){seed_game(type);frame(argv[2],names[type]);
            if(type==PG_GO){pg_commit(pg_point(-1));pg_commit(pg_point(-1));pg_mark_dead(40);frame(argv[2],"go-score");}}
        ps_games_action(0);pa_reset();memset(a_save.board,0,sizeof(a_save.board));a_save.board[0]=1024;a_save.board[1]=2048;a_save.board[2]=4096;a_save.board[3]=8192;a_save.board[4]=128;a_save.board[5]=64;a_save.score=4096;frame(argv[2],"arcade-2048");ps_games_action(1);pa_reset();frame(argv[2],"arcade-snake");host_route=PS_GAMES;frame(argv[2],"lobby-resume");host_route=PS_PLAYGAME;g_modal=2;frame(argv[2],"chess-rules");g_modal=0;
        memset(g_pos.board,0,sizeof(g_pos.board));g_pos.board[60]=6;g_pos.board[7]=-6;g_pos.board[8]=1;g_selected=8;g_promotion_to=0;g_modal=3;frame(argv[2],"chess-promotion");g_modal=0;
    }
    pico_fb_close();return 0;
}

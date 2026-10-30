/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 * 中文：调用设备同源页面，验证命中、全局返回、异步交接与绘图边界。
 * English: Exercise the actual pages for hit targets, Back, worker handoff and drawing bounds.
 */
#define PICO_GAMES_UI_HOST
#include "../games/src/builtin_games.c"
#include <assert.h>
#include <setjmp.h>
#include <stdarg.h>

static ps_view_t test_view={360,640,false};
static ps_route_t test_route=PS_GAMES;
static unsigned test_draws,test_stores,test_dirties;
static bool test_save_available=true,test_have_save,test_have_arcade;
static uint8_t test_storage[sizeof(pg_save_t)];
static uint8_t test_arcade_storage[sizeof(pa_save_t)];
static void (*test_pending_task)(void *);
static char test_toast[128];
static bool test_create_failure,test_worker_running,test_worker_suspended,test_task_allocated;
static unsigned test_task_attempts,test_task_created,test_task_deleted,test_task_token,test_logs;
static jmp_buf test_worker_stop;
static char test_last_log[256];

ps_view_t ps_view(void){return test_view;}
ps_route_t ps_route(void){return test_route;}
void ps_navigate(ps_route_t route){test_route=route;ps_games_enter(route);}
void ps_dirty(void){test_dirties++;}
void ps_toast(const char *s){snprintf(test_toast,sizeof(test_toast),"%s",s);}
bool ps_load(const char *key,void *out,size_t size){if(!strcmp(key,"arcade1")){assert(size==sizeof(pa_save_t));if(!test_have_arcade)return false;memcpy(out,test_arcade_storage,size);return true;}assert(!strcmp(key,"games"));assert(size==sizeof(test_storage));if(!test_have_save)return false;memcpy(out,test_storage,size);return true;}
bool ps_store(const char *key,const void *data,size_t size){if(!strcmp(key,"arcade1")){assert(size==sizeof(pa_save_t));test_stores++;if(!test_save_available)return false;memcpy(test_arcade_storage,data,size);test_have_arcade=true;return true;}assert(!strcmp(key,"games"));assert(size==sizeof(test_storage));test_stores++;if(!test_save_available)return false;memcpy(test_storage,data,size);test_have_save=true;return true;}
bool ps_hit(int x,int y,int rx,int ry,int rw,int rh){return x>=rx&&y>=ry&&x<rx+rw&&y<ry+rh;}

static void test_rect(int x,int y,int w,int h)
{
    assert(x>=0&&y>=24&&w>=0&&h>=0&&x+w<=test_view.w+1&&y+h<=test_view.h);test_draws++;
}
void ps_fill(uint8_t *fb,int x,int y,int w,int h,int gray){(void)fb;assert(gray>=0&&gray<=15);test_rect(x,y,w,h);}
void ps_line(uint8_t *fb,int x0,int y0,int x1,int y1,int gray){(void)fb;assert(gray>=0&&gray<=15);test_rect(pg_min(x0,x1),pg_min(y0,y1),abs(x1-x0),abs(y1-y0));}
void ps_box(uint8_t *fb,int x,int y,int w,int h,int radius,int gray,bool filled){(void)fb;(void)filled;assert(gray>=0&&gray<=15);assert(radius>=0&&radius<=pg_min(w,h)/2);test_rect(x,y,w,h);}
void ps_text(uint8_t *fb,int x,int y,int size,const char *text,int gray){(void)fb;(void)text;assert(gray>=0&&gray<=15);test_rect(x,y,1,size);}
void ps_center(uint8_t *fb,int x,int y,int size,const char *text,int gray){ps_text(fb,x,y,size,text,gray);}
void ps_wrap(uint8_t *fb,int x,int y,int w,int size,int lines,const char *text,int gray){(void)w;(void)lines;ps_text(fb,x,y,size,text,gray);}
void ps_icon(uint8_t *fb,int x,int y,int size,ps_icon_t icon,int gray){(void)icon;ps_fill(fb,x,y,size,size,gray);}
void ps_button(uint8_t *fb,int x,int y,int w,int h,const char *label,bool selected){(void)label;ps_box(fb,x,y,w,h,pg_min(5,h/2),0,selected);}
void ps_heading(uint8_t *fb,const char *title,const char *subtitle){ps_text(fb,46,35,20,title,2);if(subtitle)ps_wrap(fb,16,81,test_view.w-32,12,2,subtitle,5);}
void ps_tabs(uint8_t *fb,int selected){(void)selected;ps_fill(fb,12,28,test_view.w-24,40,15);}

uint32_t esp_random(void){return 1234;}
static int64_t test_clock_us;
int64_t esp_timer_get_time(void){if(test_clock_us)return test_clock_us;struct timespec ts;timespec_get(&ts,TIME_UTC);return (int64_t)ts.tv_sec*1000000+ts.tv_nsec/1000;}
int xTaskCreateWithCaps(void (*fn)(void *),const char *name,unsigned stack,void *arg,int priority,TaskHandle_t *handle,unsigned caps)
{
    assert(!strcmp(name,"pico_chess_ai")&&stack==20480&&!arg&&priority==1&&handle&&!*handle);
    assert(caps==(MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));assert(!test_pending_task&&!test_task_allocated);
    test_task_attempts++;if(test_create_failure)return 0;
    test_task_allocated=true;test_task_created++;test_pending_task=fn;*handle=&test_task_token;return pdPASS;
}
void vTaskDeleteWithCaps(TaskHandle_t task)
{
    assert(task==&test_task_token&&test_task_allocated&&test_worker_suspended&&!test_worker_running&&!test_pending_task);
    assert(atomic_load_explicit(&g_job.done,memory_order_acquire));
    test_task_allocated=test_worker_suspended=false;test_task_deleted++;
}
void vTaskSuspend(TaskHandle_t task){assert(!task&&test_worker_running&&test_task_allocated);test_worker_running=false;test_worker_suspended=true;longjmp(test_worker_stop,1);}
unsigned uxTaskGetStackHighWaterMark(TaskHandle_t task){assert(!task&&test_worker_running);return 4096;}
void vTaskDelay(unsigned ticks){(void)ticks;}
size_t heap_caps_get_largest_free_block(unsigned caps){assert(caps==(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT)||caps==(MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));return caps&MALLOC_CAP_INTERNAL?8192:4*1024*1024;}
size_t heap_caps_get_free_size(unsigned caps){assert(caps==(MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));return 5*1024*1024;}
void pg_test_log(const char *tag,const char *format,...){assert(!strcmp(tag,"pico_games"));va_list args;va_start(args,format);vsnprintf(test_last_log,sizeof(test_last_log),format,args);va_end(args);test_logs++;}

static void finish_task(void)
{
    assert(test_pending_task&&test_task_allocated&&!test_worker_running&&!test_worker_suspended);
    void (*task)(void *)=test_pending_task;test_pending_task=NULL;test_worker_running=true;
    if(!setjmp(test_worker_stop)){task(NULL);assert(!"worker returned instead of suspending");}
    assert(test_worker_suspended&&!test_worker_running&&test_task_allocated);
}
static void tap(int x,int y){ps_event_t e={.type=PS_TAP,.x=x,.y=y};assert(ps_games_event(&e));}
static void board_tap(int point){int x,y;pg_point_xy(pg_board_rect(),point,&x,&y);assert(pg_board_hit(x,y)==point);tap(x,y);}
static void render_pure(void)
{
    pg_save_t before_save=g_save;pg_pos_t before_pos=g_pos;pa_save_t before_arcade=a_save;int before_dir=a_dir,before_next=a_next,before_follow=a_follow;bool before_paused=a_paused,before_turned=a_turned,before_tracking=a_swipe_tracking,before_contact=a_contact_handled;unsigned stores=test_stores,epoch=atomic_load(&g_epoch),draws=test_draws;
    ps_games_render(NULL);assert(test_draws>draws);assert(!memcmp(&g_save,&before_save,sizeof(g_save)));assert(!memcmp(&g_pos,&before_pos,sizeof(g_pos)));
    assert(test_stores==stores&&epoch==atomic_load(&g_epoch));
    assert(!memcmp(&a_save,&before_arcade,sizeof(a_save))&&a_dir==before_dir&&a_next==before_next&&a_paused==before_paused&&a_turned==before_turned&&a_follow==before_follow&&a_swipe_tracking==before_tracking&&a_contact_handled==before_contact);
}

static void test_pages(bool landscape)
{
    test_view=(ps_view_t){landscape?640:360,landscape?360:640,landscape};
    ps_navigate(PS_GAMES);render_pure();
    for(int type=0;type<PG_TYPES;type++){
        int x,y,w,h;pg_lobby_card(type,&x,&y,&w,&h);int old=g_save.progress[type].level;
        tap(test_view.landscape?x+w-40:x+w/2,test_view.landscape?y+15:y+45);assert(g_save.progress[type].level==old%3+1);g_save.progress[type].level=1;
        bool was=g_save.progress[type].automatic;tap(x+22,test_view.landscape?y+40:y+75);assert(g_save.progress[type].automatic!=was);
        tap(x+w-50,y+h-15);assert(ps_route()==PS_PLAYGAME&&g_pos.type==type);render_pure();
        for(int p=0;p<pg_count(&g_pos);p++){int px,py;pg_point_xy(pg_board_rect(),p,&px,&py);assert(pg_board_hit(px,py)==p);}
        int sx,sy,sw;pg_side_rect(&sx,&sy,&sw);tap(sx+sw/2,sy+120);assert(g_modal==2);render_pure();
        pg_pos_t before_modal=g_pos;unsigned before_stores=test_stores;
        tap(1,80);assert(!g_modal&&!memcmp(&before_modal,&g_pos,sizeof(g_pos))&&test_stores==before_stores);
        tap(sx+sw/2,sy+120);assert(g_modal==2);assert(ps_games_key(-1)&&!g_modal);
        int buttons=type==PG_GO?3:2,bw=(sw-(buttons-1)*8)/buttons;
        tap(sx+(buttons-1)*(bw+8)+bw/2,sy+76);assert(g_modal==1);render_pure();
        before_modal=g_pos;before_stores=test_stores;
        tap(1,80);assert(!g_modal&&!memcmp(&before_modal,&g_pos,sizeof(g_pos))&&test_stores==before_stores);
        tap(sx+(buttons-1)*(bw+8)+bw/2,sy+76);assert(g_modal==1);assert(ps_games_key(-1)&&!g_modal);
        if(type==PG_GOMOKU||type==PG_GO)board_tap(type==PG_GO?40:112);
        else{board_tap(type==PG_CHESS?52:54);assert(g_selected>=0);render_pure();board_tap(type==PG_CHESS?36:45);}
        assert(g_pos.turn==-1&&g_pos.moves==1);assert(ps_games_tick()&&g_busy);render_pure();
        ps_games_enter(PS_ROUTE_COUNT);test_route=PS_GAMES;finish_task();assert(!g_job.success);ps_games_tick();assert(g_pos.moves==1);
        ps_navigate(PS_PLAYGAME);assert(g_busy);finish_task();assert(ps_games_tick());assert(g_pos.moves==2&&g_pos.turn==1);render_pure();
        assert(ps_games_key(0));assert(!g_pos.moves&&g_pos.turn==1&&!g_save.count);render_pure();
        tap(test_view.w-40,36);assert(test_route==PS_GAMES);render_pure();
    }
}

static void test_score_and_promotion(void)
{
    for(int landscape=0;landscape<2;landscape++){
        test_view=(ps_view_t){landscape?640:360,landscape?360:640,landscape};pg_start_game(PG_GO);
        assert(pg_commit(pg_point(40)));assert(pg_commit(pg_point(41)));assert(pg_commit(pg_point(-1)));assert(pg_commit(pg_point(-1)));
        assert(g_pos.result==PG_SCORE);board_tap(40);assert(g_save.dead[5]&1);render_pure();
        int x,y,w;pg_side_rect(&x,&y,&w);tap(x+w/4,y+90);assert(g_save.recorded&&g_pos.result!=PG_SCORE);render_pure();
        unsigned played=g_save.progress[PG_GO].played;pg_record_result();assert(g_save.progress[PG_GO].played==played);
        ps_games_init();assert(g_pos.moves==4&&g_pos.result!=PG_SCORE&&(g_save.dead[5]&1));
        pg_start_game(PG_CHESS);memset(g_pos.board,0,sizeof(g_pos.board));g_pos.board[60]=6;g_pos.board[7]=-6;g_pos.board[8]=1;
        board_tap(8);board_tap(0);assert(g_modal==3&&g_promotion_to==0);render_pure();
        pg_pos_t before_modal=g_pos;unsigned before_stores=test_stores;
        tap(1,80);assert(!g_modal&&g_promotion_to==-1&&!memcmp(&before_modal,&g_pos,sizeof(g_pos))&&test_stores==before_stores);
        board_tap(0);assert(g_modal==3&&g_promotion_to==0);
        int mw=pg_min(test_view.w-40,500),mx=(test_view.w-mw)/2,my=(test_view.h-212)/2,bw=(mw-48)/4;
        tap(mx+18+bw/2,my+125);assert(!g_modal&&g_pos.board[0]==5);render_pure();
    }
    pg_start_game(PG_GOMOKU);test_save_available=false;assert(pg_commit(pg_point(112)));assert(g_unsaved&&strstr(test_toast,"未能保存"));render_pure();
    test_save_available=true;pg_persist();assert(!g_unsaved);
    pg_start_game(PG_GO);g_save.count=PG_LOG_MAX;pg_pos_t before=g_pos;assert(!pg_commit(pg_point(40)));assert(!memcmp(&before,&g_pos,sizeof(g_pos))&&strstr(test_toast,"1024"));g_save.count=0;
}

static void first_player_move(int type)
{
    if(type==PG_GOMOKU||type==PG_GO)board_tap(type==PG_GO?40:112);
    else{board_tap(type==PG_CHESS?52:54);board_tap(type==PG_CHESS?36:45);}
    assert(g_pos.turn==-1&&g_pos.moves==1);
}

static void test_ai_workers(void)
{
    test_view=(ps_view_t){360,640,false};unsigned before=test_task_created;
    for(int type=0;type<PG_TYPES;type++)for(int level=1;level<=3;level++){
        g_save.progress[type].level=level;pg_start_game(type);first_player_move(type);
        assert(ps_games_tick()&&g_busy&&g_task&&test_task_allocated);
        unsigned created=test_task_created;for(int i=0;i<4;i++)assert(!ps_games_tick());assert(test_task_created==created);
        finish_task();assert(g_busy&&g_job.stack_free==4096&&test_task_allocated);
        assert(ps_games_tick()&&!g_busy&&!g_task&&!test_task_allocated&&g_pos.turn==1&&g_pos.moves==2);
        assert(g_save.count==2&&!g_ai_error);render_pure();
    }
    assert(test_task_created-before==PG_TYPES*3&&test_task_created==test_task_deleted);
    for(int type=0;type<PG_TYPES;type++){
        pg_start_game(type);first_player_move(type);test_create_failure=true;
        unsigned attempts=test_task_attempts;assert(ps_games_tick());
        assert(test_task_attempts==attempts+1&&!g_busy&&g_ai_error&&!g_task&&!test_task_allocated&&!test_pending_task);
        assert(strstr(g_feedback,"暂时无法计算"));for(int i=0;i<4;i++)assert(!ps_games_tick());assert(test_task_attempts==attempts+1);
        int x,y,w;pg_side_rect(&x,&y,&w);tap(x+10,y+110);
        assert(test_task_attempts==attempts+2&&g_ai_error&&!g_busy&&!test_pending_task);
        test_create_failure=false;tap(x+10,y+110);assert(g_busy&&g_task&&!g_ai_error&&test_pending_task);
        finish_task();assert(ps_games_tick()&&g_pos.moves==2&&g_pos.turn==1&&!g_ai_error&&!g_task);
    }
    assert(test_task_created==test_task_deleted);
    for(int type=0;type<PG_TYPES;type++)for(int cancel=0;cancel<5;cancel++){
        pg_start_game(type);first_player_move(type);assert(ps_games_tick()&&g_busy);
        pg_pos_t expected;
        if(cancel==0){ps_navigate(PS_GAMES);expected=g_pos;}
        else if(cancel==1){assert(ps_games_key(0));expected=g_pos;assert(!g_pos.moves);}
        else if(cancel==2){pg_start_game(type);expected=g_pos;assert(!g_pos.moves);}
        else if(cancel==3){finish_task();ps_navigate(PS_GAMES);expected=g_pos;}
        else{ps_games_action(0);expected=g_pos;assert(ps_route()==PS_ARCADEGAME);}
        if(cancel!=3)finish_task();
        assert(!g_job.success||cancel==3);assert(!ps_games_tick());
        assert(!g_busy&&!g_task&&!test_task_allocated&&!memcmp(&expected,&g_pos,sizeof(g_pos)));
        if(cancel==0||cancel==3){ps_navigate(PS_PLAYGAME);assert(g_busy);finish_task();assert(ps_games_tick()&&g_pos.moves==2);}
    }
    for(int type=0;type<PG_TYPES;type++){
        pg_start_game(type);first_player_move(type);assert(ps_games_tick()&&g_busy);finish_task();
        pg_pos_t player=g_pos;test_save_available=false;assert(ps_games_tick());
        assert(g_unsaved&&g_pos.moves==2&&g_pos.turn==1&&!g_busy&&!g_task&&strstr(test_toast,"未能保存"));
        assert(memcmp(&player,&g_pos,sizeof(g_pos)));test_save_available=true;pg_persist();assert(!g_unsaved);
    }
    assert(test_task_created==test_task_deleted&&!test_pending_task&&!test_task_allocated);
    unsigned logs=test_logs;for(int i=0;i<100;i++)assert(!ps_games_tick());assert(test_logs==logs&&g_diagnostic_count<=64);
    puts("AI workers: all four games at three levels, PSRAM caps/paired external delete, allocation failure/retry, cancellation/undo/restart/late completion/arcade reap, failed-store recovery and bounded logs passed");
}

static void test_arcade(void)
{
    for(int landscape=0;landscape<2;landscape++){
        test_view=(ps_view_t){landscape?640:360,landscape?360:640,landscape};ps_games_action(0);assert(test_route==PS_ARCADEGAME);
        pa_reset();memset(a_save.board,0,sizeof(a_save.board));a_save.board[0]=a_save.board[1]=a_save.board[2]=a_save.board[3]=2;
        assert(pa_shift(3));assert(a_save.board[0]==4&&a_save.board[1]==4&&a_save.score==8);assert(a_save.moves==1);render_pure();
        memset(a_save.board,0,sizeof(a_save.board));a_save.board[0]=a_save.board[1]=1024;a_save.won=a_save.over=0;assert(pa_shift(3));assert(a_save.won);assert(!pa_shift(2));
        int x,y,size;pa_board(&x,&y,&size);ps_event_t e={.type=PS_TAP,.x=x+size/2,.y=y+size/2};assert(ps_games_event(&e)&&a_save.keep_playing);
        for(int i=0;i<16;i++)a_save.board[i]=(i+i/4)%2?2:4;assert(!pa_has_move());a_save.over=1;assert(!pa_shift(0));render_pure();
        ps_games_action(1);pa_reset();assert(a_paused&&a_save.length==4&&pa_valid());render_pure();ps_games_action(2);assert(!a_paused);
        assert(!pa_direction(3)&&a_next==1);assert(pa_direction(0)&&a_next==0);assert(pa_direction(3)&&a_follow==3);assert(pa_step()&&a_save.snake[0]==188);
        assert(pa_step()&&a_save.snake[0]==187);assert(pa_direction(0));a_save.food=167;assert(pa_step());assert(a_save.length==5&&a_save.snake_score==10&&pa_valid());render_pure();
        ps_games_enter(PS_DEVICE);assert(a_paused);test_route=PS_ARCADEGAME;ps_games_enter(test_route);assert(a_paused);ps_games_action(2);assert(!a_paused);ps_games_action(5);assert(a_paused);
        a_save.snake[0]=0;a_save.snake[1]=1;a_save.snake[2]=2;a_save.snake[3]=3;a_save.length=4;a_dir=a_next=0;a_paused=false;assert(pa_step()&&a_save.snake_over&&a_paused);render_pure();
        pa_reset();assert(pa_valid());a_save.snake[2]=a_save.snake[1];assert(!pa_valid());pa_reset();
        for(int d=0;d<4;d++){int xx,yy;pa_direction_rect(d,&xx,&yy);assert(xx>=0&&xx+44<=test_view.w&&yy>=24&&yy+44<=test_view.h-36);}
    }
    puts("arcade: 2048 single-merge/win/noop/full-board, Snake food/collision/reverse/two-turn queue/pause, save validation and two-orientation controls passed");
}

static void test_arcade_controls_storage(void)
{
    for(int landscape=0;landscape<2;landscape++){
        test_view=(ps_view_t){landscape?640:360,landscape?360:640,landscape};ps_games_action(1);pa_reset();a_save.food=0;ps_games_action(2);
        pa_direction(1);assert(!a_turned&&a_next==1);pa_direction(0);assert(a_turned&&a_next==0);
        ps_games_action(5);assert(a_paused&&!a_turned&&a_next==a_dir);ps_games_action(2);assert(!a_paused&&a_next==1);assert(pa_step()&&a_save.snake[0]==209);
        pa_direction(0);ps_games_action(2);assert(a_paused&&!a_turned&&a_next==a_dir);pa_save_t paused=a_save;assert(!pa_step()&&!memcmp(&paused,&a_save,sizeof(paused)));
        ps_games_action(2);assert(!a_paused);pa_direction(0);ps_games_action(1);assert(a_paused&&!a_turned&&a_next==a_dir);
        ps_games_action(2);pa_direction(0);assert(pa_step()&&a_save.snake[0]==189);a_save.speed=2;assert(pa_persist());pa_save_t saved=a_save;
        pa_init();assert(a_paused&&!a_turned&&a_dir==0&&a_next==0&&!memcmp(&saved,&a_save,sizeof(saved)));render_pure();
        a_save.board[0]=3;assert(!pa_valid());a_save=saved;a_save.food=a_save.snake[0];assert(!pa_valid());a_save=saved;a_save.snake[1]=a_save.snake[0];assert(!pa_valid());
        a_save=saved;test_save_available=false;a_save.snake_score++;assert(!pa_persist()&&a_unsaved);assert(!memcmp(test_arcade_storage,&saved,sizeof(saved)));render_pure();test_save_available=true;pa_init();assert(!memcmp(&saved,&a_save,sizeof(saved)));
        for(int method=0;method<2;method++)for(int d=0;d<4;d++){
            ps_games_action(0);pa_reset();memset(a_save.board,0,sizeof(a_save.board));a_save.board[0]=2;a_save.board[d%2?1:4]=2;
            int x,y;pa_direction_rect(d,&x,&y);const ps_event_type_t swipes[]={PS_SWIPE_U,PS_SWIPE_R,PS_SWIPE_D,PS_SWIPE_L};ps_event_t e={.type=method?swipes[d]:PS_TAP,.x=x+22,.y=y+22};
            assert(ps_games_event(&e));int target=d==0?0:d==1?3:d==2?12:0;assert(a_save.board[target]==4&&a_save.score==4&&a_save.moves==1);render_pure();
        }
    }
    puts("arcade independent: ignored straight input, pause/resume/open turn cancellation, persisted body/direction, malformed saves, failed-store preservation, pure render and four-way 2048 passed");
}

static void snake_contact(ps_event_type_t type,int x0,int y0,int x,int y){
    ps_event_t e={.type=type,.x0=x0,.y0=y0,.x=x,.y=y};ps_games_event(&e);
}
static void snake_press(int direction){int x,y;pa_direction_rect(direction,&x,&y);snake_contact(PS_PRESS,x+22,y+22,x+22,y+22);}
static void snake_release(int direction){int x,y;pa_direction_rect(direction,&x,&y);snake_contact(PS_RELEASE,x+22,y+22,x+22,y+22);snake_contact(PS_TAP,x+22,y+22,x+22,y+22);}
static void test_snake_responsiveness(void){
    for(int landscape=0;landscape<2;landscape++)for(int speed=0;speed<3;speed++){
        test_view=(ps_view_t){landscape?640:360,landscape?360:640,landscape};ps_games_action(1);pa_reset();a_save.food=0;a_save.speed=(uint8_t)speed;
        test_clock_us=1000000000;ps_games_action(2);int delay=pa_delay();int64_t due=a_due;
        assert(delay==(speed==0?750:speed==1?550:400));
        snake_press(0);assert(a_turned&&a_next==0&&a_follow==-1&&a_save.snake_moves==0&&a_due==due);
        snake_release(0);assert(a_next==0&&a_follow==-1); // 松手不占第二个转向。/ Release must not queue twice.
        snake_press(3);snake_release(3);assert(a_follow==3&&!pa_direction(2));render_pure();
        test_clock_us=(due-1)*1000;assert(!ps_games_tick()&&a_save.snake_moves==0);
        test_clock_us=due*1000;assert(ps_games_tick()&&a_save.snake[0]==188&&a_save.snake_moves==1&&a_dir==0&&a_next==3&&a_follow==-1&&a_turned);
        assert(a_due==due+delay&&!ps_games_tick());
        test_clock_us=(a_due-1)*1000;assert(!ps_games_tick()&&a_save.snake_moves==1);
        test_clock_us=a_due*1000;assert(ps_games_tick()&&a_save.snake[0]==187&&a_save.snake_moves==2&&a_dir==3&&!a_turned);
        // 暂停与恢复不带入旧输入，也不立即多走一步。/ Pause/resume clears old input and never advances a bonus step.
        pa_reset();a_save.food=0;ps_games_action(2);snake_press(0);snake_release(0);snake_press(3);snake_release(3);ps_games_action(2);
        assert(a_paused&&!a_turned&&a_follow==-1&&a_next==1);ps_games_action(2);assert(!ps_games_tick()&&a_save.snake_moves==0);
        test_clock_us=a_due*1000;assert(ps_games_tick()&&a_save.snake[0]==209&&a_save.snake_moves==1);
        // 棋盘达到阈值即接受，后续MOVE和合成SWIPE不能重复消费。/ Accept thresholded board movement before lift, only once.
        pa_reset();a_save.food=0;ps_games_action(2);int x,y,size;pa_board(&x,&y,&size);int cx=x+size/2,cy=y+size/2;
        snake_contact(PS_PRESS,cx,cy,cx,cy);snake_contact(PS_MOVE,cx,cy,cx,cy-20);assert(!a_turned);
        snake_contact(PS_MOVE,cx,cy,cx,cy-35);assert(a_turned&&a_next==0&&a_save.snake_moves==0);
        snake_contact(PS_MOVE,cx,cy,cx-80,cy-35);assert(a_follow==-1);
        snake_contact(PS_RELEASE,cx,cy,cx,cy-35);snake_contact(PS_SWIPE_U,cx,cy,cx,cy-35);assert(a_follow==-1&&a_next==0);
        assert(pa_persist());pa_save_t saved=a_save;pa_init();assert(a_paused&&!a_turned&&a_follow==-1&&a_next==a_dir&&!memcmp(&a_save,&saved,sizeof(saved)));
        // 墙与自身都在单格移动前检查；排队不会跳过碰撞。/ Check wall and body before each single-cell move, even with queued turns.
        pa_reset();a_save.food=0;a_save.snake[0]=208;a_save.snake[1]=207;a_save.snake[2]=187;a_save.snake[3]=188;a_save.snake[4]=189;a_save.snake[5]=209;a_save.length=6;
        ps_games_action(2);assert(pa_direction(0)&&pa_direction(3));assert(pa_step()&&a_save.snake_over&&a_paused&&a_save.snake_moves==0&&a_save.snake[0]==208&&a_follow==-1);
        pa_reset();a_save.snake[0]=0;a_save.snake[1]=20;a_save.snake[2]=40;a_save.snake[3]=60;a_dir=a_next=0;a_paused=false;assert(pa_step()&&a_save.snake_over&&a_save.snake[0]==0);
        test_clock_us=0;
    }
    puts("Snake responsiveness: press/early-swipe before release, no duplicate synthesis, ordered two-turn queue, unchanged 750/550/400ms cadence, pause/reload and wall/body collision passed in both orientations");
}

int main(void)
{
    ps_games_init();test_pages(false);test_pages(true);test_score_and_promotion();test_ai_workers();test_arcade();test_arcade_controls_storage();test_snake_responsiveness();
    printf("games UI passed: %u draw bounds; mirrored orientations, exact board hits, pure render, worker cancel/resume, undo, score, promotion, storage failure\n",test_draws);return 0;
}

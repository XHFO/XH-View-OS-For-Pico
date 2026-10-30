/* SPDX-License-Identifier: Apache-2.0
 * 中文：通过公开ABI加载真实小游戏共享库，验证隔离、迁移与可卸载任务。
 * English: Load actual game libraries through the public ABI and verify isolation, migration and worker teardown.
 */
#define _POSIX_C_SOURCE 200809L
#include "../components/read_pico_apps/include/pico_app_sdk.h"
#include <assert.h>
#include <dlfcn.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

typedef struct { uint8_t from,to,promotion; } fixture_move_t;
typedef struct { uint16_t wins,played;uint8_t level,streak,automatic,reserved; } fixture_progress_t;
typedef struct {
    uint32_t magic;uint16_t version,count;
    uint8_t active,type,level,recorded,final_result,reserved[3];
    fixture_progress_t progress[4];fixture_move_t log[1024];uint8_t dead[11];
} fixture_board_t;
typedef struct {
    uint32_t magic,board[16],score,best,moves,snake_score,snake_best,snake_moves;
    uint16_t snake[400],length,food;
    uint8_t initialized,snake_initialized,over,won,keep_playing,snake_over,speed,reserved;
} fixture_arcade_t;
typedef struct { pthread_t thread;void (*fn)(void*);void *arg; } fixture_worker_t;
static int kind,page,navs,draws,stores,legacy_reads,creates,deletes;
static atomic_int alive;
static bool fail_store;static int legacy_mode;static int64_t test_clock_us;
static uint8_t saved[PICO_APP_MAX_STATE];static size_t saved_bytes;
static char saved_key[24],heading_text[80];
static pico_app_view_t view(void){return (pico_app_view_t){360,640,false};}
static int32_t get_page(void){return page;}
static void navigate(int32_t value){page=value;navs++;}
static void dirty(void){}
static void toast(const char *value){(void)value;}
static bool load(const char *key,void *out,size_t bytes){if(saved_bytes!=bytes||strcmp(key,saved_key))return false;memcpy(out,saved,bytes);return true;}
static bool store(const char *key,const void *in,size_t bytes){stores++;assert(bytes<=sizeof(saved));if(fail_store)return false;snprintf(saved_key,sizeof(saved_key),"%s",key);memcpy(saved,in,bytes);saved_bytes=bytes;return true;}
static bool legacy_load(const char *key,void *out,size_t bytes)
{
    legacy_reads++;
    if(kind<4){assert(!strcmp(key,"games"));assert(bytes==sizeof(fixture_board_t));fixture_board_t state={.magic=0x50474334,.version=1,.active=1,.type=(uint8_t)(legacy_mode==1?(kind+1)%4:kind),.level=1,.count=(uint16_t)(legacy_mode==2?1025:0)};
        for(int i=0;i<4;i++)state.progress[i]=(fixture_progress_t){.wins=2,.played=3,.level=2,.automatic=1};memcpy(out,&state,bytes);return true;}
    assert(!strcmp(key,"arcade1"));assert(bytes==sizeof(fixture_arcade_t));
    fixture_arcade_t state={.magic=0x50415231,.initialized=1,.snake_initialized=1,.speed=1,.length=4,.food=1};
    state.board[0]=state.board[1]=2;for(int i=0;i<4;i++)state.snake[i]=(uint16_t)(208-i);memcpy(out,&state,bytes);return true;
}
static int64_t time_us(void){if(test_clock_us)return test_clock_us;struct timespec ts;clock_gettime(CLOCK_MONOTONIC,&ts);return (int64_t)ts.tv_sec*1000000+ts.tv_nsec/1000;}
static uint32_t random_value(void){return 0x456789ab;}
static void fill(uint8_t *fb,int x,int y,int w,int h,int gray){(void)fb;(void)x;(void)y;(void)w;(void)h;(void)gray;draws++;}
static void line(uint8_t *fb,int x,int y,int xx,int yy,int gray){(void)fb;(void)x;(void)y;(void)xx;(void)yy;(void)gray;draws++;}
static void box(uint8_t *fb,int x,int y,int w,int h,int r,int gray,bool filled){(void)r;(void)filled;fill(fb,x,y,w,h,gray);}
static void text(uint8_t *fb,int x,int y,int size,const char *value,int gray){(void)size;(void)value;fill(fb,x,y,1,1,gray);}
static void wrap(uint8_t *fb,int x,int y,int w,int size,int lines,const char *value,int gray){(void)w;(void)lines;text(fb,x,y,size,value,gray);}
static void button(uint8_t *fb,int x,int y,int w,int h,const char *value,bool selected){(void)value;(void)selected;fill(fb,x,y,w,h,0);}
static void heading(uint8_t *fb,const char *title,const char *subtitle){(void)fb;(void)subtitle;snprintf(heading_text,sizeof(heading_text),"%s",title);draws++;}
static void delay_ms(uint32_t ms){struct timespec ts={ms/1000,(long)(ms%1000)*1000000};nanosleep(&ts,NULL);}
static void *worker_main(void *arg){fixture_worker_t *worker=arg;atomic_fetch_add(&alive,1);worker->fn(worker->arg);return NULL;}
static int worker_create(void (*fn)(void*),void *arg,void **out){fixture_worker_t *worker=malloc(sizeof(*worker));assert(worker);*worker=(fixture_worker_t){.fn=fn,.arg=arg};if(pthread_create(&worker->thread,NULL,worker_main,worker)){free(worker);return 0;}*out=worker;creates++;return 1;}
static void worker_delete(void *handle){fixture_worker_t *worker=handle;assert(worker);assert(!pthread_cancel(worker->thread));assert(!pthread_join(worker->thread,NULL));free(worker);deletes++;atomic_fetch_sub(&alive,1);}
static void worker_suspend(void){for(;;)delay_ms(1);}
static uint32_t stack_free(void){return 16384;}
static void diagnostic(const char *event,int type,unsigned generation,unsigned free_bytes){(void)event;(void)type;(void)generation;(void)free_bytes;}
static pico_app_host_t host={
    .abi=PICO_APP_ABI,.bytes=sizeof(pico_app_host_t),.view=view,.page=get_page,.navigate=navigate,.dirty=dirty,.toast=toast,
    .load=load,.store=store,.legacy_load=legacy_load,.time_us=time_us,.random=random_value,.fill=fill,.line=line,.box=box,
    .text=text,.center=text,.wrap=wrap,.button=button,.heading=heading,.worker_create=worker_create,.worker_delete=worker_delete,
    .worker_suspend=worker_suspend,.worker_stack_free=stack_free,.delay_ms=delay_ms,.diagnostic=diagnostic,
    .format=snprintf,.copy=memcpy,.move=memmove,.set=memset,.compare=memcmp
};
static void tap(const pico_app_exports_t *app,int x,int y){pico_app_event_t event={.type=PGA_TAP,.x=x,.y=y};assert(app->event(&event));}
static void snake_input(const pico_app_exports_t *app,int direction){
    int x=109+(direction==3?0:direction==1?2:1)*49,y=402+(direction==0?0:49);
    pico_app_event_t e={.type=PGA_PRESS,.x0=x+22,.y0=y+22,.x=x+22,.y=y+22};assert(app->event(&e));
    e.type=PGA_RELEASE;app->event(&e);e.type=PGA_TAP;assert(app->event(&e));
}
static fixture_arcade_t arcade_snapshot(const pico_app_exports_t *app){assert(app->save());fixture_arcade_t state;assert(saved_bytes==sizeof(state));memcpy(&state,saved,sizeof(state));return state;}
static void snake_timing(const pico_app_exports_t *app){
    fixture_arcade_t state=arcade_snapshot(app);assert(state.snake[0]==208&&state.snake_moves==0);
    snake_input(app,0);snake_input(app,3);state=arcade_snapshot(app);assert(state.snake[0]==208&&state.snake_moves==0);
    test_clock_us+=549000;assert(!app->tick());test_clock_us+=1000;assert(app->tick());
    state=arcade_snapshot(app);assert(state.snake[0]==188&&state.snake_moves==1);assert(!app->tick());
    test_clock_us+=549000;assert(!app->tick());test_clock_us+=1000;assert(app->tick());
    state=arcade_snapshot(app);assert(state.snake[0]==187&&state.snake_moves==2);
    // 暂停清空尚未执行的方向，恢复后从新的整步时钟继续。/ Pause drops pending input and resume waits a fresh full movement interval.
    snake_input(app,2);app->action(2);assert(app->status()&PGA_STATUS_PAUSED);
    test_clock_us+=2000000;assert(!app->tick());app->action(2);assert(!app->tick());
    test_clock_us+=550000;assert(app->tick());state=arcade_snapshot(app);assert(state.snake[0]==186&&state.snake_moves==3);
}

int main(int argc,char **argv)
{
    assert(argc==4);kind=atoi(argv[2]);legacy_mode=atoi(argv[3]);page=kind<4?PGA_PAGE_BOARD:PGA_PAGE_ARCADE;if(kind==5)test_clock_us=1000000000;
    void *library=dlopen(argv[1],RTLD_NOW|RTLD_LOCAL);if(!library){fprintf(stderr,"%s\n",dlerror());return 1;}
    pico_app_query_t query=(pico_app_query_t)dlsym(library,"pico_app_query");assert(query);
    const pico_app_exports_t *app=query();assert(app&&app->abi==1&&app->bytes==sizeof(*app)&&app->kind==(unsigned)kind&&app->version==10001);
    const char *ids[]={"gomoku","go","xiangqi","chess","2048","snake"};assert(!strcmp(app->id,ids[kind]));
    pico_app_host_t bad=host;bad.abi=999;assert(!app->start(&bad));bad=host;bad.bytes--;assert(!app->start(&bad));
    assert(app->start(&host));assert(app->start(&host));assert(legacy_reads==1);assert(navs==0);
    uint8_t frame=0;int before=stores;app->render(&frame);app->render(&frame);assert(draws>0&&stores==before);
    assert(!app->event(NULL));pico_app_event_t bad_event={.type=999};assert(!app->event(&bad_event));
    if(kind<4){
        const char *titles[]={"五子棋","围棋","象棋","国际象棋"};assert(!strcmp(heading_text,titles[kind]));
        app->action(0);app->action(1);assert(!(app->status()&PGA_STATUS_ARCADE)&&navs==0);
        // 不匹配的旧棋局应只保留训练成绩，点击开始无需先关闭其他棋局。/ A different legacy game keeps only training progress.
        tap(app,180,legacy_mode?380:324);app->render(&frame);assert(!strcmp(heading_text,titles[kind]));assert(navs==0);
        if(kind<2)tap(app,180,260);else if(kind==2){tap(app,30,334);tap(app,30,296);}else{tap(app,200,362);tap(app,200,280);}
        assert(app->tick());assert(creates==1);assert(app->status()&PGA_STATUS_BUSY);
        fail_store=true;assert(!app->save());assert(app->status()&PGA_STATUS_UNSAVED);
        app->stop();assert(creates==deletes&&atomic_load(&alive)==0);assert(!(app->status()&PGA_STATUS_BUSY));
        fail_store=false;assert(app->save());assert(!strcmp(saved_key,"games")&&saved_bytes==sizeof(fixture_board_t));
        fixture_board_t state;memcpy(&state,saved,sizeof(state));assert(state.type==kind&&state.active&&state.count==1);assert(state.progress[kind].level==2);
        for(int i=0;i<4;i++)if(i!=kind)assert(state.progress[i].played==0);
        // 钉住的模块恢复后仍可继续AI，不需要再次start或重载旧存档。/ A pinned module can resume AI without starting or migrating again.
        assert(app->tick());assert(creates==2);app->stop();assert(creates==deletes&&atomic_load(&alive)==0);
    }else{
        assert(app->status()&PGA_STATUS_ARCADE);assert((bool)(app->status()&PGA_STATUS_SNAKE)==(kind==5));
        app->action(kind==4?1:0);assert((bool)(app->status()&PGA_STATUS_SNAKE)==(kind==5));assert(navs==0);
        assert(app->save());assert(!strcmp(saved_key,"arcade1")&&saved_bytes==sizeof(fixture_arcade_t));fixture_arcade_t state;memcpy(&state,saved,sizeof(state));
        if(kind==4){assert(state.initialized&&!state.snake_initialized);pico_app_event_t e={.type=PGA_SWIPE_L};assert(app->event(&e));}
        else{assert(state.snake_initialized&&!state.initialized);app->action(2);assert(!(app->status()&PGA_STATUS_PAUSED));snake_timing(app);}
        fail_store=true;assert(!app->save());assert(app->status()&PGA_STATUS_UNSAVED);app->stop();if(kind==5)assert(app->status()&PGA_STATUS_PAUSED);
        fail_store=false;assert(app->save());assert(!(app->status()&PGA_STATUS_UNSAVED));
    }
    app->stop();assert(creates==deletes&&atomic_load(&alive)==0);assert(legacy_reads==1);assert(!dlclose(library));
    if(kind==5){
        library=dlopen(argv[1],RTLD_NOW|RTLD_LOCAL);assert(library);query=(pico_app_query_t)dlsym(library,"pico_app_query");assert(query);app=query();assert(app->start(&host));
        assert(app->status()&PGA_STATUS_PAUSED);fixture_arcade_t state=arcade_snapshot(app);assert(state.snake[0]==186&&state.snake_moves==3&&legacy_reads==1);
        app->action(2);assert(!app->tick());test_clock_us+=550000;assert(app->tick());state=arcade_snapshot(app);assert(state.snake[0]==185&&state.snake_moves==4);
        app->stop();assert(!dlclose(library));puts("Snake actual module: early press, two queued turns, unchanged cadence, pause/resume and saved-body direction reload passed");
    }
    printf("module %s legacy=%s: ABI, kind isolation, pure render, readonly migration, failed-save preservation, join/unload passed\n",ids[kind],legacy_mode==2?"malformed":legacy_mode?"other":"own");
    return 0;
}

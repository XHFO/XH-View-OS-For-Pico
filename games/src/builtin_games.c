/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 *
 * 中文：TF卡独立小游戏模块源码；每包只运行指定游戏，棋类搜索仅在独立任务执行。
 * English: Independent TF game-module source; each package runs one kind and searches only in a worker.
 * 冻结：不访问串口、板级时序或校准；绘制不改状态、不读写存档。
 * Frozen: No serial, board timing or calibration access; rendering never mutates state or storage.
 */

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#ifndef PICO_GAME_MODULE
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#endif
#include <limits.h>
#include <stdatomic.h>

#if defined(PICO_GAME_MODULE)
#include "../sdk/pico_game_module_compat.h"
#elif !defined(PICO_GAMES_CORE_ONLY)
#include "../../main/system/pico_system.h"
#ifndef PICO_GAMES_UI_HOST
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/idf_additions.h"
#else
#include <time.h>
#include "../../tools/pico_games_test/hal.h"
#endif
#else
#include <time.h>
#endif

// 兼容非模块构建；模块构建从SDK尾部读取同值定义。
// Fallbacks for non-module builds; module builds use the SDK tail values.
#ifndef PGA_DUO_NONE
#define PGA_DUO_NONE 0
#define PGA_DUO_SCREEN 1
#define PGA_DUO_CONTROLLER 2
#define PGA_DUO_PAUSE 4
#define PGA_DUO_SPEED 5
#define PGA_DUO_RESTART 6
#endif

/* ---- 规则与存档 / Rules and records ---- */

enum { PG_GOMOKU, PG_GO, PG_XIANGQI, PG_CHESS, PG_TYPES };
enum { PG_ACTIVE=0, PG_DRAW=2, PG_SCORE=3 };
enum { PG_NO_REASON, PG_MATE, PG_STALEMATE, PG_MATERIAL, PG_REPEAT, PG_QUIET, PG_FULL };
enum { PG_LOG_MAX=1024, PG_MOVE_MAX=256, PG_POINT=255, PG_RESUME=254 };

typedef struct { uint8_t from,to,promotion; } pg_move_t;
typedef struct {
    int8_t board[225];
    uint16_t moves,quiet,captures;
    int16_t last_from,last_to,ep;
    int8_t turn,result;
    uint8_t type,level,castle,passes,reason;
} pg_pos_t;
typedef struct { const uint64_t *keys; int count; } pg_history_t;
typedef struct { uint16_t wins,played; uint8_t level,streak,automatic,reserved; } pg_progress_t;
typedef struct {
    uint32_t magic;
    uint16_t version,count;
    uint8_t active,type,level,recorded,final_result,reserved[3];
    pg_progress_t progress[PG_TYPES];
    pg_move_t log[PG_LOG_MAX];
    uint8_t dead[11];
} pg_save_t;
typedef struct { int stones[81],liberties[81],n,libs; } pg_group_t;
typedef struct { int black2,white2,winner; } pg_score_t;

static int pg_sign(int p) { return (p>0)-(p<0); }
static int pg_min(int a,int b) { return a<b?a:b; }
static int pg_max(int a,int b) { return a>b?a:b; }
static int pg_count(const pg_pos_t *s) { return s->type==PG_GOMOKU?225:s->type==PG_XIANGQI?90:s->type==PG_CHESS?64:81; }
static pg_move_t pg_point(int to) { return (pg_move_t){PG_POINT,(uint8_t)(to<0?PG_POINT:to),0}; }

static void pg_initial(pg_pos_t *s,int type,int level)
{
    memset(s,0,sizeof(*s));
    s->type=type;s->level=level;s->turn=1;s->last_from=s->last_to=s->ep=-1;
    if(type==PG_XIANGQI) {
        const int8_t row[]={2,3,4,5,6,5,4,3,2};
        for(int x=0;x<9;x++){s->board[x]=-row[x];s->board[81+x]=row[x];}
        s->board[19]=s->board[25]=-7;s->board[64]=s->board[70]=7;
        for(int x=0;x<9;x+=2){s->board[27+x]=-1;s->board[54+x]=1;}
    } else if(type==PG_CHESS) {
        const int8_t row[]={2,3,4,5,6,4,3,2};
        for(int x=0;x<8;x++){s->board[x]=-row[x];s->board[8+x]=-1;s->board[48+x]=1;s->board[56+x]=row[x];}
        s->castle=15;
    }
}

static int pg_neighbors(int p,int *out)
{
    int x=p%9,y=p/9,n=0;
    if(x)out[n++]=p-1;
    if(x<8)out[n++]=p+1;
    if(y)out[n++]=p-9;
    if(y<8)out[n++]=p+9;
    return n;
}

static pg_group_t pg_group(const int8_t *b,int start)
{
    pg_group_t g={0};bool seen[81]={0},libs[81]={0};int stack[81],top=0;
    if(start<0||start>=81||!b[start])return g;
    stack[top++]=start;seen[start]=true;
    while(top){int p=stack[--top],ns[4],n=pg_neighbors(p,ns);g.stones[g.n++]=p;
        for(int j=0;j<n;j++){int q=ns[j];if(!b[q]){if(!libs[q]){libs[q]=true;g.liberties[g.libs++]=q;}}
            else if(b[q]==b[start]&&!seen[q]){seen[q]=true;stack[top++]=q;}}
    }
    return g;
}

static bool pg_xpseudo(const int8_t *b,int from,int to)
{
    if(from<0||from>=90||to<0||to>=90||from==to)return false;
    int p=b[from],side=pg_sign(p);if(!p||pg_sign(b[to])==side)return false;
    int x=from%9,y=from/9,xx=to%9,yy=to/9,dx=xx-x,dy=yy-y,ax=abs(dx),ay=abs(dy);
    bool palace=xx>=3&&xx<=5&&(side==1?yy>=7:yy<=2);
    switch(abs(p)){
    case 1:return (!dx&&dy==-side)||((side==1?y<=4:y>=5)&&!dy&&ax==1);
    case 2:case 7:{if(dx&&dy)return false;int step=dx?pg_sign(dx):9*pg_sign(dy),n=0;
        for(int q=from+step;q!=to;q+=step)if(b[q])n++;
        return abs(p)==2?n==0:b[to]?n==1:n==0;}
    case 3:if(ax==2&&ay==1)return !b[from+pg_sign(dx)];
        if(ax==1&&ay==2)return !b[from+9*pg_sign(dy)];
        return false;
    case 4:return ax==2&&ay==2&&(side==1?yy>=5:yy<=4)&&!b[from+pg_sign(dx)+9*pg_sign(dy)];
    case 5:return ax==1&&ay==1&&palace;
    case 6:if(abs(b[to])==6&&!dx){int step=9*pg_sign(dy);for(int q=from+step;q!=to;q+=step)if(b[q])return false;return true;}
        return ax+ay==1&&palace;
    default:return false;
    }
}

static bool pg_clear(const int8_t *b,int from,int to)
{
    int dx=pg_sign(to%8-from%8),dy=pg_sign(to/8-from/8),q=from+dx+dy*8;
    while(q!=to){if(q<0||q>=64||b[q])return false;q+=dx+dy*8;}
    return true;
}

static bool pg_attacked(const int8_t *b,int target,int by)
{
    for(int from=0;from<64;from++)if(pg_sign(b[from])==by){
        int dx=target%8-from%8,dy=target/8-from/8,ax=abs(dx),ay=abs(dy),p=abs(b[from]);
        if(p==1&&ax==1&&dy==-by)return true;
        if(p==3&&ax*ay==2)return true;
        if(p==6&&pg_max(ax,ay)==1)return true;
        if((p==2&&(!dx||!dy))||(p==4&&ax==ay)||(p==5&&(!dx||!dy||ax==ay)))
            if(from!=target&&pg_clear(b,from,target))return true;
    }
    return false;
}

static bool pg_check(const pg_pos_t *s,int side)
{
    int n=s->type==PG_CHESS?64:90,king=-1;
    for(int i=0;i<n;i++)if(s->board[i]==side*6){king=i;break;}
    if(king<0)return true;
    if(s->type==PG_CHESS)return pg_attacked(s->board,king,-side);
    for(int i=0;i<90;i++)if(pg_sign(s->board[i])==-side&&pg_xpseudo(s->board,i,king))return true;
    return false;
}

static bool pg_cpseudo(const pg_pos_t *s,pg_move_t m)
{
    int from=m.from,to=m.to;if(from>=64||to>=64||from==to)return false;
    const int8_t *b=s->board;int p=b[from],side=s->turn;
    if(pg_sign(p)!=side||pg_sign(b[to])==side||abs(b[to])==6)return false;
    int x=from%8,y=from/8,xx=to%8,yy=to/8,dx=xx-x,dy=yy-y,ax=abs(dx),ay=abs(dy);
    switch(abs(p)){
    case 1:
        if((yy==0||yy==7)&&m.promotion&&m.promotion!=2&&m.promotion!=3&&m.promotion!=4&&m.promotion!=5)return false;
        if(!dx&&!b[to])return dy==-side||(dy==-2*side&&y==(side==1?6:1)&&!b[from-side*8]);
        if(ax==1&&dy==-side)return pg_sign(b[to])==-side||(to==s->ep&&!b[to]&&b[to+side*8]==-side);
        return false;
    case 3:return ax*ay==2;
    case 2:return (!dx||!dy)&&pg_clear(b,from,to);
    case 4:return ax==ay&&pg_clear(b,from,to);
    case 5:return (!dx||!dy||ax==ay)&&pg_clear(b,from,to);
    case 6:
        if(pg_max(ax,ay)==1)return true;
        if(dy||ax!=2||from!=(side==1?60:4))return false;
        {bool right=dx>0;int rook=side==1?(right?63:56):(right?7:0),flag=side==1?(right?1:2):(right?4:8);
        if(!(s->castle&flag)||b[rook]!=side*2||pg_check(s,side))return false;
        for(int q=from+pg_sign(dx);q!=rook;q+=pg_sign(dx))if(b[q])return false;
        return !pg_attacked(b,from+pg_sign(dx),-side)&&!pg_attacked(b,to,-side);}
    default:return false;
    }
}

static void pg_raw(const pg_pos_t *s,pg_move_t m,pg_pos_t *n)
{
    *n=*s;int from=m.from,to=m.to,p=s->board[from],side=pg_sign(p),capture=s->board[to];
    n->board[from]=0;n->board[to]=p;n->turn=-side;n->moves++;n->last_from=from;n->last_to=to;n->result=0;n->reason=0;
    n->quiet=(capture||abs(p)==1)?0:s->quiet+1;n->captures=capture?1:0;
    if(s->type!=PG_CHESS)return;
    if(abs(p)==1&&to==s->ep&&!capture&&from%8!=to%8){n->board[to+side*8]=0;n->quiet=0;n->captures=1;}
    if(abs(p)==6){n->castle&=side==1?~3:~12;
        if(abs(to-from)==2){bool right=to>from;int rook=side==1?(right?63:56):(right?7:0);
            n->board[from+(right?1:-1)]=n->board[rook];n->board[rook]=0;}}
    const int corners[]={0,7,56,63},flags[]={8,4,2,1};
    for(int j=0;j<4;j++)if(from==corners[j]||to==corners[j])n->castle&=~flags[j];
    if(abs(p)==1&&(to<8||to>=56))n->board[to]=side*(m.promotion?m.promotion:5);
    n->ep=abs(p)==1&&abs(to-from)==16?(to+from)/2:-1;
}

static bool pg_legal_piece(const pg_pos_t *s,pg_move_t m)
{
    if(s->type==PG_CHESS){if(!pg_cpseudo(s,m))return false;}
    else if(m.from>=90||m.to>=90||pg_sign(s->board[m.from])!=s->turn||!pg_xpseudo(s->board,m.from,m.to))return false;
    pg_pos_t n;pg_raw(s,m,&n);return !pg_check(&n,s->turn);
}

static int pg_moves(const pg_pos_t *s,pg_move_t *out,int capacity)
{
    int n=0,count=s->type==PG_CHESS?64:90;
    const uint8_t promotions[]={5,2,4,3};
    for(int from=0;from<count;from++)if(pg_sign(s->board[from])==s->turn)
        for(int to=0;to<count;to++){
            int variants=s->type==PG_CHESS&&abs(s->board[from])==1&&(to<8||to>=56)?4:1;
            for(int j=0;j<variants;j++){pg_move_t m={(uint8_t)from,(uint8_t)to,variants==4?promotions[j]:0};
                if(pg_legal_piece(s,m)){if(n<capacity)out[n]=m;n++;if(n>=capacity)return n;}}
        }
    return n;
}

static uint64_t pg_key(const pg_pos_t *s)
{
    uint64_t h=UINT64_C(1469598103934665603);int count=pg_count(s);
    for(int i=0;i<count;i++){h^=(uint8_t)(s->board[i]+8);h*=UINT64_C(1099511628211);}
    if(s->type==PG_XIANGQI||s->type==PG_CHESS){h^=(uint8_t)(s->turn+2);h*=UINT64_C(1099511628211);}
    if(s->type==PG_CHESS){int ep=-1;
        if(s->ep>=0)for(int from=0;from<64;from++)if(s->board[from]==s->turn&&pg_legal_piece(s,(pg_move_t){from,s->ep,0})){ep=s->ep;break;}
        h^=s->castle;h*=UINT64_C(1099511628211);h^=(uint8_t)(ep+1);h*=UINT64_C(1099511628211);
    }
    return h;
}

static int pg_repetitions(const pg_history_t *history,uint64_t key)
{
    int count=0;if(history)for(int i=0;i<history->count;i++)if(history->keys[i]==key)count++;return count;
}

static bool pg_insufficient(const pg_pos_t *s)
{
    int pieces=0,single=0,bishop_color=-1;bool bishops=true;
    for(int i=0;i<64;i++)if(s->board[i]&&abs(s->board[i])!=6){int p=abs(s->board[i]);pieces++;single=p;
        if(p!=4)bishops=false;else{int c=(i%8+i/8)%2;if(bishop_color<0)bishop_color=c;else if(c!=bishop_color)bishops=false;}}
    return !pieces||(pieces==1&&(single==3||single==4))||bishops;
}

static bool pg_five(const int8_t *b,int i)
{
    const int axes[4][2]={{1,0},{0,1},{1,1},{1,-1}};if(i<0||i>=225||!b[i])return false;
    for(int axis=0;axis<4;axis++){int count=1;
        for(int side=-1;side<=1;side+=2)for(int k=1;k<5;k++){int x=i%15+axes[axis][0]*k*side,y=i/15+axes[axis][1]*k*side;
            if(x<0||x>=15||y<0||y>=15||b[y*15+x]!=b[i])break;
            count++;}
        if(count>=5)return true;
    }return false;
}

static bool pg_apply(const pg_pos_t *s,pg_move_t m,pg_pos_t *n,const pg_history_t *history,bool finish)
{
    if(s->result)return false;
    if(s->type==PG_GOMOKU){int i=m.to;if(m.from!=PG_POINT||i>=225||s->board[i])return false;
        *n=*s;n->board[i]=s->turn;n->turn=-s->turn;n->moves++;n->last_from=-1;n->last_to=i;
        if(pg_five(n->board,i))n->result=s->turn;else if(n->moves==225){n->result=PG_DRAW;n->reason=PG_FULL;}return true;}
    if(s->type==PG_GO){if(m.from!=PG_POINT)return false;*n=*s;n->turn=-s->turn;n->moves++;n->last_from=-1;n->captures=0;
        if(m.to==PG_POINT){n->passes++;n->last_to=-1;if(s->passes)n->result=PG_SCORE;return true;}
        int i=m.to;if(i>=81||s->board[i])return false;n->board[i]=s->turn;
        int ns[4],count=pg_neighbors(i,ns);for(int j=0;j<count;j++)if(n->board[ns[j]]==-s->turn){pg_group_t g=pg_group(n->board,ns[j]);
            if(!g.libs){n->captures+=g.n;for(int k=0;k<g.n;k++)n->board[g.stones[k]]=0;}}
        if(!pg_group(n->board,i).libs||pg_repetitions(history,pg_key(n)))return false;
        n->passes=0;n->last_to=i;return true;
    }
    if(!pg_legal_piece(s,m))return false;
    int captured=s->board[m.to];pg_raw(s,m,n);
    if(!finish)return true;
    pg_move_t any;int legal=pg_moves(n,&any,1);
    if(s->type==PG_XIANGQI){if(abs(captured)==6||!legal){n->result=s->turn;n->reason=PG_MATE;}
        else if(pg_repetitions(history,pg_key(n))>=2){n->result=PG_DRAW;n->reason=PG_REPEAT;}
        else if(n->quiet>=120){n->result=PG_DRAW;n->reason=PG_QUIET;}
    }else{
        if(!legal){n->result=pg_check(n,n->turn)?s->turn:PG_DRAW;n->reason=n->result==PG_DRAW?PG_STALEMATE:PG_MATE;}
        else if(pg_insufficient(n)){n->result=PG_DRAW;n->reason=PG_MATERIAL;}
        else if(pg_repetitions(history,pg_key(n))>=2){n->result=PG_DRAW;n->reason=PG_REPEAT;}
        else if(n->quiet>=100){n->result=PG_DRAW;n->reason=PG_QUIET;}
    }
    return true;
}

static pg_score_t pg_go_score(const int8_t *b)
{
    pg_score_t score={0,13,-1};bool seen[81]={0};int stack[81];
    for(int i=0;i<81;i++)if(b[i]){if(b[i]>0)score.black2+=2;else score.white2+=2;}
        else if(!seen[i]){int top=0,area=0,border=0;stack[top++]=i;seen[i]=true;
            while(top){int p=stack[--top],ns[4],n=pg_neighbors(p,ns);area++;
                for(int j=0;j<n;j++){int q=ns[j];if(b[q])border|=b[q]>0?1:2;else if(!seen[q]){seen[q]=true;stack[top++]=q;}}}
            if(border==1)score.black2+=area*2;else if(border==2)score.white2+=area*2;
        }
    score.winner=score.black2>score.white2?1:-1;return score;
}

static bool pg_advance(pg_progress_t *p,int level,int result)
{
    if(p->played<UINT16_MAX)p->played++;
    if(result==1){if(p->wins<UINT16_MAX)p->wins++;if(p->streak<UINT8_MAX)p->streak++;
        if(p->automatic&&p->streak>=2&&level==p->level&&p->level<3){p->level++;p->streak=0;return true;}}
    else p->streak=0;
    return false;
}

// 存档仅保存走子日志，重放同时重建重复局面历史。/ Saves contain moves; replay also rebuilds repetition history.
static bool pg_replay(const pg_save_t *save,pg_pos_t *pos,uint64_t *keys,int *key_count)
{
    if(save->magic!=UINT32_C(0x50474334)||save->version!=1||save->count>PG_LOG_MAX||save->type>=PG_TYPES||save->level<1||save->level>3)return false;
    pg_initial(pos,save->type,save->level);int count=1;keys[0]=pg_key(pos);
    for(int i=0;i<save->count;i++){
        pg_move_t move=save->log[i];
        if(move.from==PG_RESUME){if(pos->type!=PG_GO||pos->result!=PG_SCORE)return false;pos->result=0;pos->passes=0;pos->turn=1;continue;}
        pg_pos_t n;pg_history_t history={keys,count};if(!pg_apply(pos,move,&n,&history,true))return false;*pos=n;
        if(pos->type!=PG_GO||move.to!=PG_POINT)keys[count++]=pg_key(pos);
    }
    if(save->final_result){if(pos->type!=PG_GO||pos->result!=PG_SCORE||(save->final_result!=1&&save->final_result!=255))return false;
        pos->result=(int8_t)save->final_result;}
    *key_count=count;return true;
}

/* ---- 有界离线对手 / Bounded offline opponents ---- */

typedef struct {
    int64_t deadline;
    unsigned nodes,limit,rng,generation;
    atomic_uint *epoch;
    uint64_t *keys;
    int key_count;
} pg_search_t;
typedef struct { pg_move_t move; int score,attack,defend; } pg_choice_t;

static int64_t pg_millis(void)
{
#ifndef PICO_GAMES_CORE_ONLY
    return esp_timer_get_time()/1000;
#else
    struct timespec ts;timespec_get(&ts,TIME_UTC);return (int64_t)ts.tv_sec*1000+ts.tv_nsec/1000000;
#endif
}

static bool pg_cancelled(const pg_search_t *c)
{
    return c->epoch&&atomic_load_explicit(c->epoch,memory_order_relaxed)!=c->generation;
}

static bool pg_over(const pg_search_t *c)
{
    return pg_cancelled(c)||c->nodes>=c->limit||pg_millis()>=c->deadline;
}

static void pg_cooperate(pg_search_t *c)
{
    c->nodes++;
#ifndef PICO_GAMES_CORE_ONLY
    if(!(c->nodes&31))vTaskDelay(1);
#endif
}

static unsigned pg_random(pg_search_t *c,unsigned n)
{
    c->rng=c->rng*UINT32_C(1664525)+UINT32_C(1013904223);return n?c->rng%n:0;
}

static void pg_insert(pg_choice_t *out,int *count,int cap,pg_choice_t value)
{
    int n=*count;if(n==cap&&value.score<=out[n-1].score)return;
    int i=pg_min(n,cap-1);while(i>0&&out[i-1].score<value.score){if(i<cap)out[i]=out[i-1];i--;}
    out[i]=value;if(n<cap)(*count)++;
}

static int pg_threat(int8_t *b,int i,int side)
{
    const int axes[4][2]={{1,0},{0,1},{1,1},{1,-1}};int total=0;b[i]=side;
    for(int axis=0;axis<4;axis++){int count=1,open=0;
        for(int d=-1;d<=1;d+=2)for(int k=1;k<5;k++){int x=i%15+axes[axis][0]*k*d,y=i/15+axes[axis][1]*k*d;
            if(x<0||x>=15||y<0||y>=15)break;
            int p=b[y*15+x];if(p!=side){if(!p)open++;break;}count++;}
        total+=count>=5?10000000:count==4?(open==2?500000:open?25000:0):count==3?(open==2?6000:open?500:0):count==2?(open==2?300:open?30:0):open*3;
    }
    b[i]=0;return total;
}

static int pg_gchoices(const pg_pos_t *s,pg_choice_t *out,int cap,int *win,int *block)
{
    bool candidates[225]={0};int count=0;int8_t b[225];memcpy(b,s->board,sizeof(b));
    if(win)*win=-1;
    if(block)*block=-1;
    if(!s->moves){out[0]=(pg_choice_t){pg_point(112),1,0,0};return 1;}
    for(int i=0;i<225;i++)if(b[i])for(int dx=-2;dx<=2;dx++)for(int dy=-2;dy<=2;dy++){
        int x=i%15+dx,y=i/15+dy;if(x>=0&&x<15&&y>=0&&y<15&&!b[y*15+x])candidates[y*15+x]=true;}
    for(int i=0;i<225;i++)if(candidates[i]){int attack=pg_threat(b,i,s->turn),defend=pg_threat(b,i,-s->turn);
        if(attack>=10000000&&win&&*win<0)*win=i;
        if(defend>=10000000&&block&&*block<0)*block=i;
        pg_insert(out,&count,cap,(pg_choice_t){pg_point(i),attack+defend*92/100,attack,defend});}
    return count;
}

static int pg_go_value(const pg_pos_t *s,int i,const pg_pos_t *n,int level,pg_search_t *c)
{
    pg_group_t g=pg_group(n->board,i);int score=n->captures*85+pg_min(g.libs,5)*3;
    int ns[4],neighbors=pg_neighbors(i,ns),friends=0,enemies=0;
    if(g.libs==1)score-=70;
    for(int j=0;j<neighbors;j++){int q=ns[j];
        if(s->board[q]==s->turn){friends++;pg_group_t old=pg_group(s->board,q);if(old.libs==1&&g.libs>1)score+=old.n*22;}
        else if(s->board[q]==-s->turn){enemies++;if(n->board[q]&&pg_group(n->board,q).libs==1)score+=25;}}
    if(friends==neighbors)score-=90;
    int x=i%9,y=i/9,edge=pg_min(pg_min(x,8-x),pg_min(y,8-y));
    score+=edge==2?10:edge==1?5:edge==0?-9:2;score+=friends?7:0;
    if(s->moves>10&&!friends&&!enemies)score-=4;
    if(level>=2){c->keys[c->key_count]=pg_key(n);pg_history_t history={c->keys,c->key_count+1};
        for(int j=0;j<g.libs;j++){pg_pos_t reply;if(pg_apply(n,pg_point(g.liberties[j]),&reply,&history,false))score-=reply.captures*30;}
        if(friends>1)score+=8;}
    return score;
}

static int pg_evaluate(const pg_pos_t *s,int side)
{
    const int xv[]={0,100,900,410,210,210,100000,450},cv[]={0,100,500,320,330,900,20000};
    int score=0,count=s->type==PG_CHESS?64:90;
    for(int i=0;i<count;i++)if(s->board[i]){int p=s->board[i],color=pg_sign(p),t=abs(p),bonus=0;
        if(s->type==PG_XIANGQI){int advance=color==1?9-i/9:i/9;bonus=t==1?advance*12+(advance>=5?35:0):t==3?pg_min(advance,6)*4:0;}
        else{int x=i%8,y=i/8,advance=color==1?7-y:y,center2=7-abs(2*x-7),vertical2=7-abs(2*y-7);
            bonus=t==1?advance*9+center2*3/2:t==3?(center2+vertical2)*4:t==4?center2*5/2:t==6&&s->moves<24?(advance==0&&(x==2||x==6)?30:-advance*10):0;}
        score+=color*side*((s->type==PG_CHESS?cv[t]:xv[t])+bonus);
    }
    return score;
}

static int pg_order_value(const pg_pos_t *s,pg_move_t m)
{
    const int xv[]={0,100,900,410,210,210,100000,450},cv[]={0,100,500,320,330,900,20000};
    const int *v=s->type==PG_CHESS?cv:xv;return v[abs(s->board[m.to])]+(m.promotion?v[m.promotion]:0);
}

static int pg_order(const pg_pos_t *s,pg_move_t *moves)
{
    int count=pg_moves(s,moves,PG_MOVE_MAX);
    for(int i=1;i<count;i++){pg_move_t m=moves[i];int value=pg_order_value(s,m),j=i;
        while(j>0&&pg_order_value(s,moves[j-1])<value){moves[j]=moves[j-1];j--;}moves[j]=m;}
    return count;
}

static int pg_search(const pg_pos_t *s,int depth,int alpha,int beta,pg_search_t *c)
{
    pg_cooperate(c);if(pg_over(c))return pg_evaluate(s,s->turn);
    pg_move_t moves[PG_MOVE_MAX];int count=pg_order(s,moves);
    if(!count)return s->type==PG_CHESS&&!pg_check(s,s->turn)?0:-1000000-depth*100;
    if(s->type==PG_CHESS&&(pg_insufficient(s)||s->quiet>=100))return 0;
    if(!depth)return pg_evaluate(s,s->turn);
    int best=-200000000;
    for(int i=0;i<count;i++){pg_pos_t next;pg_raw(s,moves[i],&next);int score=-pg_search(&next,depth-1,-beta,-alpha,c);
        best=pg_max(best,score);alpha=pg_max(alpha,score);if(alpha>=beta||pg_over(c))break;}
    return best;
}

static bool pg_ai(const pg_pos_t *s,pg_search_t *c,pg_move_t *answer)
{
    if(s->result||pg_cancelled(c))return false;
    if(s->type==PG_GOMOKU){pg_choice_t list[14],reply[1],third[1];int win=-1,block=-1,count=pg_gchoices(s,list,14,&win,&block);
        if(!count)return false;
        if(win>=0){*answer=pg_point(win);return true;}if(block>=0){*answer=pg_point(block);return true;}
        if(s->level==1){*answer=list[pg_random(c,pg_min(count,5))].move;return true;}
        int best=-INT_MAX;*answer=list[0].move;
        for(int i=0;i<pg_min(count,s->level==2?8:14);i++){
            if(pg_over(c))break;
            pg_pos_t next;pg_apply(s,list[i].move,&next,NULL,false);int replies=pg_gchoices(&next,reply,1,NULL,NULL);
            int score=list[i].attack-(replies?reply[0].attack*115/100:0)+list[i].defend/4;
            if(s->level==3&&replies){pg_pos_t two;if(pg_apply(&next,reply[0].move,&two,NULL,false)&&pg_gchoices(&two,third,1,NULL,NULL))score+=third[0].attack*6/10;}
            if(score>best){best=score;*answer=list[i].move;}pg_cooperate(c);
        }return !pg_cancelled(c);
    }
    if(s->type==PG_GO){pg_choice_t list[81];int count=0;pg_history_t history={c->keys,c->key_count};
        for(int i=0;i<81;i++)if(!s->board[i]){if(pg_cancelled(c))return false;pg_pos_t next;
            if(pg_apply(s,pg_point(i),&next,&history,false))pg_insert(list,&count,81,(pg_choice_t){pg_point(i),pg_go_value(s,i,&next,s->level,c),0,0});
            pg_cooperate(c);}
        if(!count||list[0].score<0||(s->passes&&(pg_go_score(s->board).winner==s->turn||list[0].score<15))){*answer=pg_point(-1);return true;}
        if(s->level==1){*answer=list[pg_random(c,pg_min(count,4))].move;return true;}
        if(s->level==3)for(int j=0;j<pg_min(count,10)&&!pg_over(c);j++){
            pg_pos_t next;pg_apply(s,list[j].move,&next,&history,false);c->keys[c->key_count]=pg_key(&next);
            pg_history_t h={c->keys,c->key_count+1};bool neighbors[81]={0};int worst=0;
            for(int i=0;i<81;i++)if(next.board[i]==s->turn){int ns[4],n=pg_neighbors(i,ns);for(int k=0;k<n;k++)neighbors[ns[k]]=true;}
            for(int i=0;i<81&&!pg_over(c);i++)if(neighbors[i]){pg_pos_t reply;
                if(pg_apply(&next,pg_point(i),&reply,&h,false))worst=pg_max(worst,reply.captures*70+(pg_group(reply.board,i).libs==1?-10:0));
                pg_cooperate(c);}
            list[j].score-=worst*7/10;
        }
        int best=0;for(int i=1;i<count;i++)if(list[i].score>list[best].score)best=i;*answer=list[best].move;return !pg_cancelled(c);
    }
    pg_move_t moves[PG_MOVE_MAX];int count=pg_order(s,moves);if(!count)return false;
    *answer=moves[0];
    if(s->level==1){pg_choice_t best[4];int n=0,cap=s->type==PG_CHESS?4:3;
        for(int i=0;i<count;i++){if(pg_cancelled(c))return false;pg_pos_t next;pg_raw(s,moves[i],&next);
            int score=pg_evaluate(&next,s->turn)+(pg_check(&next,next.turn)?(s->type==PG_CHESS?10:25):0);
            pg_insert(best,&n,cap,(pg_choice_t){moves[i],score,0,0});pg_cooperate(c);}
        *answer=best[pg_random(c,n)].move;return true;
    }
    for(int depth=1;depth<=(s->level==2?2:3);depth++){
        int best=-200000000,index=0;bool complete=true;
        for(int i=0;i<count;i++){if(pg_over(c)){complete=false;break;}pg_pos_t next;pg_raw(s,moves[i],&next);
            int score=-pg_search(&next,depth-1,-200000000,200000000,c);if(score>best){best=score;index=i;}
            if(pg_over(c)){complete=false;break;}}
        if(!complete)break;
        *answer=moves[index];pg_move_t first=moves[index];memmove(moves+1,moves,index*sizeof(*moves));moves[0]=first;
    }
    return !pg_cancelled(c);
}

#ifndef PICO_GAMES_CORE_ONLY

/* ---- 页面与后台任务 / Pages and background task ---- */

static const char *const g_titles[]={"五子棋","围棋","象棋","国际象棋"};
static const char *const g_levels[]={"入门","进阶","挑战"};
static const char *const g_rules[]={
    "自由五子棋：横、竖或斜线连续至少五子获胜，不设禁手。黑棋先行。入门会主动进攻和阻挡，进阶与挑战会进一步寻找战术。",
    "9 路围棋：支持提子，禁止自杀与重复局面。连续两次停一手进入计分，请点击整组死子标记或取消。采用面积计分：棋子与围空各计 1 点，白方加 6.5 分。也可以继续对局。",
    "支持蹩马腿、塞象眼、炮架、过河兵和将帅照面。必须解将，无合法走法即负。三次重复局面或连续 120 步无吃子、无兵卒移动判和。长将长捉采用简化和棋规则。",
    "标准国际象棋：支持将军、将死、逼和、王车易位、吃过路兵，以及后／车／象／马四种升变。三次重复局面、五十回合和基础子力不足自动判和。白方先行。"
};

static pg_save_t g_save;
static pg_pos_t g_pos;
static uint32_t pg_duo_generation;
static uint64_t g_keys[PG_LOG_MAX+3];
static int g_key_count,g_selected=-1,g_modal,g_promotion_to=-1,g_lobby_selected;
static bool g_unsaved,g_busy,g_ai_error;
static char g_feedback[112];
static atomic_uint g_epoch;
static TaskHandle_t g_task;
static unsigned g_diagnostic_count;

typedef struct {
    pg_pos_t pos;
    uint64_t keys[PG_LOG_MAX+3];
    int key_count;
    unsigned generation;
    pg_move_t answer;
    bool success;
    unsigned stack_free;
    atomic_bool done;
} pg_job_t;
static pg_job_t g_job;

typedef struct { int x,y,w,h,pad,cols,rows; } pg_board_rect_t;

static void pg_task_diagnostic(const char *event,int type,unsigned generation,unsigned stack_free)
{
    // 每次启动最多64条事件，不在等待轮询中输出。/ At most 64 lifecycle records per boot; never log waiting polls.
    if(g_diagnostic_count>=64)return;
    g_diagnostic_count++;
#ifdef PICO_GAME_MODULE
    pga_host->diagnostic(event,type,generation,stack_free);
#else
    ESP_LOGI("pico_games","ai %s type=%d generation=%u internal_largest=%u psram_free=%u psram_largest=%u stack_free=%u",
        event,type,generation,(unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT),
        (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT),
        (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT),stack_free);
#endif
}

static void pg_cancel_task(void)
{
    if(g_busy)pg_task_diagnostic("cancel",g_job.pos.type,g_job.generation,0);
    atomic_fetch_add_explicit(&g_epoch,1,memory_order_relaxed);
}

static void pg_persist(void)
{
    g_unsaved=!ps_store("games",&g_save,sizeof(g_save));
    if(g_unsaved)ps_toast("棋局未能保存，请检查储存空间");
}

static void pg_record_result(void)
{
    if(g_save.recorded||!g_pos.result||g_pos.result==PG_SCORE)return;
    g_save.recorded=1;
    if(pg_advance(&g_save.progress[g_pos.type],g_pos.level,g_pos.result))
        snprintf(g_feedback,sizeof(g_feedback),"连胜两局，下次进入%s难度",g_levels[g_save.progress[g_pos.type].level-1]);
}

static void pg_start_game(int type)
{
#ifdef PICO_GAME_MODULE
    if(PICO_GAME_KIND>=PG_TYPES||type!=PICO_GAME_KIND)return;
#endif
    pg_cancel_task();g_save.active=1;g_save.type=type;g_save.level=g_save.progress[type].level;pg_duo_generation++;
    g_save.count=0;g_save.recorded=0;g_save.final_result=0;memset(g_save.dead,0,sizeof(g_save.dead));
    pg_initial(&g_pos,type,g_save.level);g_keys[0]=pg_key(&g_pos);g_key_count=1;
    g_selected=-1;g_modal=0;g_promotion_to=-1;g_feedback[0]=0;g_ai_error=false;
    pg_persist();ps_navigate(PS_PLAYGAME);
}

static bool pg_commit(pg_move_t move)
{
    if(g_save.count>=PG_LOG_MAX){ps_toast("本局达到 1024 手存档上限，可悔棋或重开");return false;}
    pg_history_t history={g_keys,g_key_count};pg_pos_t next;
    if(!pg_apply(&g_pos,move,&next,&history,true)){
        snprintf(g_feedback,sizeof(g_feedback),"%s",g_pos.type==PG_GO?"此处不能落子：占位、自杀或重复局面":"这一步不合法，请选择标出的落点");
        return false;
    }
    g_save.log[g_save.count++]=move;g_pos=next;
    if(g_pos.type!=PG_GO||move.to!=PG_POINT)g_keys[g_key_count++]=pg_key(&g_pos);
    memset(g_save.dead,0,sizeof(g_save.dead));g_selected=-1;g_feedback[0]=0;g_ai_error=false;
    pg_record_result();pg_persist();return true;
}

static void pg_undo(void)
{
    if(!g_save.active||!g_save.count||g_save.recorded){ps_toast("当前没有可撤回的回合");return;}
    pg_cancel_task();g_save.final_result=0;
    do{g_save.count--;if(!pg_replay(&g_save,&g_pos,g_keys,&g_key_count))break;}while(g_save.count&&g_pos.turn!=1);
    memset(g_save.dead,0,sizeof(g_save.dead));g_selected=-1;g_modal=0;g_ai_error=false;
    snprintf(g_feedback,sizeof(g_feedback),"已撤回你和对手的上一回合");pg_persist();ps_dirty();
}

static pg_score_t pg_score_current(void)
{
    int8_t board[225];memcpy(board,g_pos.board,sizeof(board));
    for(int i=0;i<81;i++)if(g_save.dead[i/8]&(1u<<(i%8)))board[i]=0;
    return pg_go_score(board);
}

static void pg_mark_dead(int point)
{
    if(g_pos.result!=PG_SCORE||point<0||point>=81||!g_pos.board[point])return;
    pg_group_t group=pg_group(g_pos.board,point);bool remove=g_save.dead[point/8]&(1u<<(point%8));
    for(int i=0;i<group.n;i++){int p=group.stones[i];if(remove)g_save.dead[p/8]&=~(1u<<(p%8));else g_save.dead[p/8]|=1u<<(p%8);}
    pg_persist();
}

static void pg_finish_score(void)
{
    if(g_pos.result!=PG_SCORE)return;
    g_pos.result=pg_score_current().winner;g_save.final_result=(uint8_t)g_pos.result;
    pg_record_result();pg_persist();
}

static void pg_continue_go(void)
{
    if(g_pos.result!=PG_SCORE)return;
    if(g_save.count>=PG_LOG_MAX){ps_toast("已达存档上限，请重开或悔棋");return;}
    g_save.log[g_save.count++]=(pg_move_t){PG_RESUME,0,0};
    g_pos.result=0;g_pos.passes=0;g_pos.turn=1;memset(g_save.dead,0,sizeof(g_save.dead));pg_persist();
}

static void pg_ai_task(void *unused)
{
    (void)unused;int level=g_job.pos.level;
    pg_search_t search={.deadline=pg_millis()+(level==1?350:level==2?1400:3200),
        .limit=level==1?1000:level==2?7000:20000,.rng=esp_random(),.generation=g_job.generation,
        .epoch=&g_epoch,.keys=g_job.keys,.key_count=g_job.key_count};
    g_job.success=pg_ai(&g_job.pos,&search,&g_job.answer);
    g_job.stack_free=(unsigned)uxTaskGetStackHighWaterMark(NULL);
    atomic_store_explicit(&g_job.done,true,memory_order_release);
    // 主循环配对释放外部栈，避免自删除额外申请内部清理任务。/ The UI pairs external-stack deletion without allocating a self-deletion helper.
    for(;;)vTaskSuspend(NULL);
}

static void pg_request_ai(void)
{
    if(g_busy||g_ai_error||!g_save.active||g_pos.turn!=-1||g_pos.result||ps_route()!=PS_PLAYGAME)return;
    if(g_save.count>=PG_LOG_MAX){g_ai_error=true;snprintf(g_feedback,sizeof(g_feedback),"本局已达 1024 手存档上限，请悔棋或重开");return;}
    g_job.pos=g_pos;g_job.key_count=g_key_count;memcpy(g_job.keys,g_keys,g_key_count*sizeof(*g_keys));
    g_job.generation=atomic_load_explicit(&g_epoch,memory_order_relaxed);g_job.success=false;
    g_job.stack_free=0;g_task=NULL;
    atomic_store_explicit(&g_job.done,false,memory_order_relaxed);g_busy=true;
    if(xTaskCreateWithCaps(pg_ai_task,"pico_chess_ai",20480,NULL,1,&g_task,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT)!=pdPASS){
        g_busy=false;g_ai_error=true;snprintf(g_feedback,sizeof(g_feedback),"对手暂时无法计算，请点击重试");
        pg_task_diagnostic("create_failed",g_job.pos.type,g_job.generation,0);
    }else pg_task_diagnostic("created",g_job.pos.type,g_job.generation,0);
}

static pg_board_rect_t pg_board_rect(void)
{
    ps_view_t view=ps_view();
    // 小屏优先：棋盘尽量贴近左右边缘，横屏把侧栏压到右侧剩余空间。
    // Small-screen first: keep the board close to both side edges and let the
    // landscape side panel use only the remaining right-hand space.
    pg_board_rect_t r={.x=8,.y=82,.w=view.w-16,.pad=14};
    r.cols=g_pos.type==PG_GOMOKU?15:g_pos.type==PG_CHESS?8:9;
    r.rows=g_pos.type==PG_XIANGQI?10:r.cols;
    if(view.landscape){
        // 棋类统一布局：棋盘居中，左右留出操作/状态栏。
        // Unified board layout: centered board with action and status rails.
        r.x=168;r.y=64;r.h=288;
        r.w=g_pos.type==PG_XIANGQI?r.h*9/10:r.h;
        r.pad=10;
    } else {
        r.w=view.w-16;
        r.h=g_pos.type==PG_XIANGQI?r.w*10/9:r.w;
        // 9x10 象棋盘保持真实格形；竖屏最多留出顶部栏和底部操作区。
        if(r.h>view.h-150)r.h=view.h-150;
    }
    if(g_pos.type==PG_CHESS)r.pad=0;
    return r;
}

static void pg_point_xy(pg_board_rect_t r,int point,int *x,int *y)
{
    if(g_pos.type==PG_CHESS){*x=r.x+(2*(point%8)+1)*r.w/16;*y=r.y+(2*(point/8)+1)*r.h/16;}
    else{*x=r.x+r.pad+(point%r.cols)*(r.w-2*r.pad)/(r.cols-1);*y=r.y+r.pad+(point/r.cols)*(r.h-2*r.pad)/(r.rows-1);}
}

static int pg_board_hit(int x,int y)
{
    pg_board_rect_t r=pg_board_rect();if(!ps_hit(x,y,r.x,r.y,r.w,r.h))return -1;
    if(g_pos.type==PG_CHESS)return (y-r.y)*8/r.h*8+(x-r.x)*8/r.w;
    int col=((x-r.x-r.pad)*(r.cols-1)+(r.w-2*r.pad)/2)/(r.w-2*r.pad);
    int row=((y-r.y-r.pad)*(r.rows-1)+(r.h-2*r.pad)/2)/(r.h-2*r.pad);
    if(col<0||col>=r.cols||row<0||row>=r.rows)return -1;
    return row*r.cols+col;
}

static void pg_circle(uint8_t *fb,int x,int y,int radius,int fill)
{
    ps_box(fb,x-radius,y-radius,radius*2,radius*2,radius,fill,true);
    ps_box(fb,x-radius,y-radius,radius*2,radius*2,radius,0,false);
}

// 国际象棋使用矢量棋子，不依赖外部符号字体。/ Vector chess pieces do not depend on symbol-font coverage.
static void pg_chess_piece(uint8_t *fb,int x,int y,int size,int piece)
{
    int type=abs(piece),ink=piece>0?0:15,fill=piece>0?15:0,u=pg_max(2,size/8);
    ps_box(fb,x-3*u,y-3*u,6*u,6*u,u,fill,true);
    ps_box(fb,x-3*u,y-3*u,6*u,6*u,u,0,false);
    ps_line(fb,x-2*u,y+2*u,x+2*u,y+2*u,ink);
    if(type==1){
        // 兵头轮廓跟随棋方反色，避免黑兵在底板中消失。/ Invert the pawn head outline so black pawns remain visible.
        ps_box(fb,x-u,y-2*u,2*u,2*u,u,fill,true);
        ps_box(fb,x-u,y-2*u,2*u,2*u,u,ink,false);
        ps_line(fb,x,y,x,y+u,ink);
    }
    else if(type==2){ps_line(fb,x-2*u,y-u,x+2*u,y-u,ink);for(int i=-2;i<=2;i+=2)ps_line(fb,x+i*u,y-2*u,x+i*u,y-u,ink);
        ps_line(fb,x-u,y-u,x-u,y+u,ink);ps_line(fb,x+u,y-u,x+u,y+u,ink);}
    else if(type==3){ps_line(fb,x-u,y+u,x+u,y-u,ink);ps_line(fb,x+u,y-u,x-u,y-2*u,ink);
        ps_line(fb,x-u,y-2*u,x-2*u,y,ink);ps_line(fb,x-2*u,y,x,y,ink);ps_fill(fb,x-u,y-u,1,1,ink);}
    else if(type==4){ps_line(fb,x,y-2*u,x+u,y-u,ink);ps_line(fb,x+u,y-u,x,y+u,ink);
        ps_line(fb,x,y+u,x-u,y-u,ink);ps_line(fb,x-u,y-u,x,y-2*u,ink);ps_line(fb,x,y-2*u,x,y-u,ink);}
    else if(type==5){ps_line(fb,x-2*u,y-u,x-u,y+u,ink);ps_line(fb,x+2*u,y-u,x+u,y+u,ink);
        ps_line(fb,x-u,y+u,x+u,y+u,ink);ps_line(fb,x-2*u,y-u,x-u,y,ink);ps_line(fb,x-u,y,x,y-2*u,ink);
        ps_line(fb,x,y-2*u,x+u,y,ink);ps_line(fb,x+u,y,x+2*u,y-u,ink);}
    else{ps_line(fb,x,y-2*u,x,y+u,ink);ps_line(fb,x-u,y-u,x+u,y-u,ink);ps_line(fb,x-u,y+u,x+u,y+u,ink);}
}

static void pg_draw_board(uint8_t *fb)
{
    pg_board_rect_t r=pg_board_rect();int count=pg_count(&g_pos),step=g_pos.type==PG_CHESS?r.w/8:(r.w-2*r.pad)/(r.cols-1);
    // 先铺白底，避免标题栏分隔线从棋盘格缝中透出。
    // Fill a white base so the title-bar divider cannot show through board gaps.
    if(g_pos.type==PG_CHESS)ps_fill(fb,r.x,r.y,r.w,r.h,15);
    ps_box(fb,r.x,r.y,r.w,r.h,4,0,false);
    if(g_pos.type==PG_CHESS){for(int i=0;i<64;i++){
        int x=r.x+(i%8)*r.w/8,y=r.y+(i/8)*r.h/8;
        ps_fill(fb,x,y,r.w/8+1,r.h/8+1,(i%8+i/8)%2?12:15);
        if(i==g_pos.last_from||i==g_pos.last_to)ps_box(fb,x+2,y+2,r.w/8-4,r.h/8-4,2,6,false);
        if(i==g_selected)ps_box(fb,x+1,y+1,r.w/8-2,r.h/8-2,2,0,false);
        if(i/8==7){char label[2]={(char)('a'+i%8),0};ps_text(fb,x+2,y+r.h/8-11,9,label,4);}
        if(i%8==0){char label[2]={(char)('8'-i/8),0};ps_text(fb,x+2,y+1,9,label,4);}
    }}else{
        for(int row=0;row<r.rows;row++){int y=r.y+r.pad+row*(r.h-2*r.pad)/(r.rows-1);ps_line(fb,r.x+r.pad,y,r.x+r.w-r.pad,y,7);}
        for(int col=0;col<r.cols;col++){int x=r.x+r.pad+col*(r.w-2*r.pad)/(r.cols-1);
            if(g_pos.type==PG_XIANGQI&&col&&col<8){int y4=r.y+r.pad+4*(r.h-2*r.pad)/9,y5=r.y+r.pad+5*(r.h-2*r.pad)/9;
                ps_line(fb,x,r.y+r.pad,x,y4,7);ps_line(fb,x,y5,x,r.y+r.h-r.pad,7);}
            else ps_line(fb,x,r.y+r.pad,x,r.y+r.h-r.pad,7);}
        if(g_pos.type==PG_XIANGQI){for(int row=0;row<=7;row+=7){int x1,y1,x2,y2;pg_point_xy(r,row*9+3,&x1,&y1);pg_point_xy(r,(row+2)*9+5,&x2,&y2);
                ps_line(fb,x1,y1,x2,y2,7);ps_line(fb,x1,y2,x2,y1,7);}
            ps_center(fb,r.x+r.w/3,r.y+r.h/2-8,14,"楚河",6);ps_center(fb,r.x+r.w*2/3,r.y+r.h/2-8,14,"汉界",6);
        }else{int a=g_pos.type==PG_GO?2:3,b=g_pos.type==PG_GO?6:11,m=r.cols/2,stars[5][2]={{a,a},{b,a},{m,m},{a,b},{b,b}};
            for(int i=0;i<5;i++){int x,y;pg_point_xy(r,stars[i][1]*r.cols+stars[i][0],&x,&y);ps_box(fb,x-2,y-2,4,4,2,0,true);}}
    }
    for(int i=0;i<count;i++)if(g_pos.board[i]){int x,y,p=g_pos.board[i];pg_point_xy(r,i,&x,&y);
        if(g_pos.type==PG_CHESS)pg_chess_piece(fb,x,y,step-8,p);
        else{int radius=pg_max(4,step*40/100);pg_circle(fb,x,y,radius,p>0&&g_pos.type!=PG_XIANGQI?0:15);
            if(g_pos.type==PG_XIANGQI){const char *red[]={"","兵","车","马","相","仕","帅","炮"},*black[]={"","卒","车","马","象","士","将","砲"};
                ps_center(fb,x,y-pg_max(10,step/2)/2,pg_max(10,step/2),p>0?red[abs(p)]:black[abs(p)],0);
                if(p<0)ps_box(fb,x-radius+2,y-radius+2,(radius-2)*2,(radius-2)*2,radius-2,0,false);
            }else if(i==g_pos.last_to)ps_box(fb,x-2,y-2,4,4,1,p>0?15:0,true);
            if(i==g_selected)ps_box(fb,x-radius-3,y-radius-3,(radius+3)*2,(radius+3)*2,radius+3,0,false);
            if(g_pos.type==PG_GO&&(g_save.dead[i/8]&(1u<<(i%8)))){
                ps_line(fb,x-radius/2,y-radius/2,x+radius/2,y+radius/2,p>0?15:0);ps_line(fb,x-radius/2,y+radius/2,x+radius/2,y-radius/2,p>0?15:0);}
        }
    }
    if(g_selected>=0&&(g_pos.type==PG_CHESS||g_pos.type==PG_XIANGQI)&&!g_pos.result&&g_pos.turn==1){
        pg_move_t moves[PG_MOVE_MAX];int n=pg_moves(&g_pos,moves,PG_MOVE_MAX);bool drawn[90]={0};
        for(int j=0;j<n;j++)if(moves[j].from==g_selected&&!drawn[moves[j].to]){int point=moves[j].to,x,y;drawn[point]=true;pg_point_xy(r,point,&x,&y);
            if(g_pos.board[point])ps_box(fb,x-step/2+1,y-step/2+1,step-2,step-2,step/2-1,0,false);
            else ps_box(fb,x-3,y-3,6,6,3,0,true);}
    }
}

/* ---- 休闲游戏：独立存档与离页暂停 / Arcade: separate saves and pause on leave ---- */
enum { PA_2048,PA_SNAKE,PA_CELLS=400 };
typedef struct {
    uint32_t magic,board[16],score,best,moves,snake_score,snake_best,snake_moves;
    uint16_t snake[PA_CELLS],length,food;
    uint8_t initialized,snake_initialized,over,won,keep_playing,snake_over,speed,reserved;
} pa_save_t;
static pa_save_t a_save;
static int a_kind,a_dir,a_next,a_follow=-1;
static int a_duo_role;
static int pa_duo_role_now(void);
static bool a_paused=true,a_turned,a_unsaved,a_was_active,a_swipe_tracking,a_contact_handled;
static int64_t a_due;
static uint32_t a_rng=0x45a109e7u;
static uint32_t pa_random(void){a_rng^=a_rng<<13;a_rng^=a_rng>>17;a_rng^=a_rng<<5;return a_rng;}
static bool pa_persist(void){a_unsaved=!ps_store("arcade1",&a_save,sizeof(a_save));return !a_unsaved;}
static bool pa_has_move(void){for(int i=0;i<16;i++){if(!a_save.board[i])return true;if(i%4<3&&a_save.board[i]==a_save.board[i+1])return true;if(i<12&&a_save.board[i]==a_save.board[i+4])return true;}return false;}
static void pa_spawn(void){int free_cells[16],n=0;for(int i=0;i<16;i++)if(!a_save.board[i])free_cells[n++]=i;if(n)a_save.board[free_cells[pa_random()%(unsigned)n]]=pa_random()%10?2:4;}
static bool pa_shift(int direction){
    if(a_save.over||(a_save.won&&!a_save.keep_playing))return false;
    uint32_t before[16];memcpy(before,a_save.board,sizeof(before));
    for(int line=0;line<4;line++){
        int ids[4],n=0;uint32_t values[4]={0},out[4]={0};
        for(int j=0;j<4;j++){ids[j]=direction==0?j*4+line:direction==1?line*4+3-j:direction==2?(3-j)*4+line:line*4+j;if(a_save.board[ids[j]])values[n++]=a_save.board[ids[j]];}
        int at=0;for(int j=0;j<n;j++){uint32_t value=values[j];if(j+1<n&&values[j+1]==value&&value<=UINT32_MAX/2){value*=2;j++;a_save.score+=value;if(value>=2048)a_save.won=1;}out[at++]=value;}
        for(int j=0;j<4;j++)a_save.board[ids[j]]=out[j];
    }
    if(!memcmp(before,a_save.board,sizeof(before)))return false;
    a_save.moves++;if(a_save.score>a_save.best)a_save.best=a_save.score;pa_spawn();a_save.over=!pa_has_move();pa_persist();return true;
}
static void pa_food(void){int n=0;uint16_t free_cells[PA_CELLS];for(int p=0;p<PA_CELLS;p++){bool occupied=false;for(int i=0;i<a_save.length;i++)if(a_save.snake[i]==p){occupied=true;break;}if(!occupied)free_cells[n++]=(uint16_t)p;}a_save.food=n?free_cells[pa_random()%(unsigned)n]:UINT16_MAX;}
static void pa_reset(void){
    if(a_kind==PA_2048){memset(a_save.board,0,sizeof(a_save.board));a_save.score=a_save.moves=0;a_save.over=a_save.won=a_save.keep_playing=0;a_save.initialized=1;pa_spawn();pa_spawn();}
    else{a_save.length=4;for(int i=0;i<4;i++)a_save.snake[i]=(uint16_t)(10*20+8-i);a_save.snake_score=a_save.snake_moves=0;a_save.snake_over=0;a_save.snake_initialized=1;a_dir=a_next=1;a_follow=-1;a_paused=true;a_turned=false;a_swipe_tracking=a_contact_handled=false;pa_food();}
    pa_persist();ps_dirty();
}
static int pa_delay(void){return a_save.speed==0?450:a_save.speed==1?300:180;}
// 两个转向依次执行；按键与滑动不改变走格时钟，暂停丢弃尚未执行的输入。
// Apply two queued turns in order; input never changes the movement clock and pausing discards pending turns.
static void pa_clear_input(void){a_next=a_dir;a_follow=-1;a_turned=false;a_swipe_tracking=a_contact_handled=false;}
static void pa_pause(void){pa_clear_input();if(!a_paused){a_paused=true;pa_persist();ps_dirty();}}
static bool pa_direction(int direction){
    if(a_paused||a_save.snake_over||direction<0||direction>3)return false;
    int previous=a_follow>=0?a_follow:a_next;
    if(direction==previous||direction==(previous+2)%4)return false;
    if(!a_turned){a_next=direction;a_turned=true;}
    else if(a_follow<0)a_follow=direction;
    else return false;
    // 方向确认后立即推进一格，避免等下一次自动 tick 才响应。
    // Step immediately after a direction is accepted instead of waiting for the next auto tick.
    a_due=pg_millis();
    return true;
}
static bool pa_step(void){
    if(a_paused||a_save.snake_over)return false;
    a_dir=a_next;
    if(a_follow>=0){a_next=a_follow;a_follow=-1;a_turned=true;}else{a_next=a_dir;a_turned=false;}
    int head=a_save.snake[0],x=head%20,y=head/20;
    x+=a_dir==1?1:a_dir==3?-1:0;y+=a_dir==2?1:a_dir==0?-1:0;
    bool dead=x<0||x>=20||y<0||y>=20;int p=y*20+x;bool grow=p==a_save.food;
    for(int i=0;i<(int)a_save.length-(grow?0:1);i++)if(a_save.snake[i]==p)dead=true;
    if(dead){a_save.snake_over=1;a_paused=true;pa_clear_input();pa_persist();return true;}
    if(grow&&a_save.length<PA_CELLS)a_save.length++;
    for(int i=a_save.length-1;i>0;i--){a_save.snake[i]=a_save.snake[i-1];}
    a_save.snake[0]=(uint16_t)p;a_save.snake_moves++;
    if(grow){a_save.snake_score+=10;if(a_save.snake_score>a_save.snake_best)a_save.snake_best=a_save.snake_score;pa_food();if(a_save.food==UINT16_MAX){a_save.snake_over=1;a_paused=true;pa_clear_input();}}
    if(grow||a_save.snake_moves%32==0){pa_persist();}
    return true;
}
static bool pa_valid(void){
    if(a_save.magic!=0x50415231u||a_save.speed>2||a_save.length>PA_CELLS)return false;
    for(int i=0;i<16;i++){uint32_t v=a_save.board[i];if(v&&(v<2||(v&(v-1))))return false;}
    if(a_save.snake_initialized){if(a_save.length<2)return false;for(int i=0;i<a_save.length;i++){if(a_save.snake[i]>=PA_CELLS)return false;for(int j=0;j<i;j++)if(a_save.snake[i]==a_save.snake[j])return false;if(i){int a=a_save.snake[i],b=a_save.snake[i-1];if(abs(a%20-b%20)+abs(a/20-b/20)!=1)return false;}}if(a_save.food>=PA_CELLS&&a_save.length<PA_CELLS)return false;for(int i=0;i<a_save.length;i++)if(a_save.snake[i]==a_save.food)return false;}
    return true;
}
static void pa_init(void){memset(&a_save,0,sizeof(a_save));bool loaded=ps_load("arcade1",&a_save,sizeof(a_save));
#ifdef PICO_GAME_MODULE
    if(!loaded)loaded=pga_host->legacy_load("arcade1",&a_save,sizeof(a_save));
#endif
    if(!loaded||!pa_valid()){memset(&a_save,0,sizeof(a_save));a_save.magic=0x50415231u;a_save.speed=1;}
#ifdef PICO_GAME_MODULE
    if(PICO_GAME_KIND==PGA_2048){memset(a_save.snake,0,sizeof(a_save.snake));a_save.length=a_save.food=0;a_save.snake_initialized=a_save.snake_over=0;a_save.snake_score=a_save.snake_best=a_save.snake_moves=0;}
    else{memset(a_save.board,0,sizeof(a_save.board));a_save.score=a_save.best=a_save.moves=0;a_save.initialized=a_save.over=a_save.won=a_save.keep_playing=0;}
#endif
    a_paused=true;a_turned=false;a_follow=-1;a_swipe_tracking=a_contact_handled=false;a_was_active=false;a_rng=esp_random()|1u;
    if(a_save.snake_initialized){int h=a_save.snake[0],n=a_save.snake[1];a_dir=h==n+1?1:h==n-1?3:h==n+20?2:0;a_next=a_dir;}}
static void pa_open(int kind){
#ifdef PICO_GAME_MODULE
    if(PICO_GAME_KIND<PG_TYPES||kind!=PICO_GAME_KIND-PG_TYPES)return;
#endif
    a_duo_role=pa_duo_role_now();a_kind=kind;a_paused=true;pa_clear_input();if(kind==PA_2048?!a_save.initialized:!a_save.snake_initialized)pa_reset();ps_navigate(PS_ARCADEGAME);}
bool ps_games_is_arcade(void){return ps_route()==PS_ARCADEGAME;}
bool ps_games_is_snake(void){return ps_route()==PS_ARCADEGAME&&a_kind==PA_SNAKE;}
bool ps_games_paused(void){return a_kind==PA_SNAKE&&a_paused;}
#ifdef PICO_GAME_MODULE
static
#endif
void ps_games_action(int action){
    if(action==0||action==1){pa_open(action);return;}
    if(action==5){pa_pause();return;}
    if(ps_route()!=PS_ARCADEGAME){if(action==3){if(ps_route()==PS_PLAYGAME){g_modal=1;ps_dirty();}else ps_toast("请先打开一局游戏");}else if(action==4&&ps_route()==PS_PLAYGAME){g_modal=2;ps_dirty();}return;}
    if(action==2&&a_kind==PA_SNAKE&&!a_save.snake_over){a_paused=!a_paused;pa_clear_input();a_due=pg_millis()+pa_delay();if(a_paused)pa_persist();ps_dirty();}
    else if(action==3)pa_reset();
    else if(action==4)ps_toast(a_kind==PA_2048?"左右上下滑动合并数字，目标2048":"先点开始，再滑动或点方向；撞墙或自己结束");
}
static void pa_board(int *x,int *y,int *size){ps_view_t v=ps_view();*size=v.landscape?252:276;*x=v.landscape?30:(v.w-*size)/2;*y=v.landscape?84:104;}
static void pa_direction_rect(int direction,int *x,int *y){
    ps_view_t v=ps_view();
    // 单机横屏：右侧局部十字键；棋盘扩大后仍保持间距。
    // Single-device landscape: compact D-pad on the right, clear of the larger board.
    if(v.landscape){
        *x=383+(direction==3?-50:direction==1?50:0);
        *y=80+(direction==0?0:direction==2?92:46);
        return;
    }
    int cx=v.w/2,cy=404;
    *x=cx+(direction==3?-70:direction==1?26:-22);
    *y=cy+(direction==0?-22:direction==2?74:26);
}
#ifdef PICO_GAMES_UI_HOST
static int pga_test_duo_role;
#endif
static int pa_duo_role_now(void)
{
#ifdef PICO_GAMES_UI_HOST
    return pga_test_duo_role;
#elif defined(PICO_GAME_MODULE)
    return pga_duo_role();
#else
    return PGA_DUO_NONE;
#endif
}
static void pa_controller_rect(int direction,int *x,int *y,int *w,int *h)
{
    ps_view_t v=ps_view();
    if(v.landscape){
        // 左侧独立上下，右侧独立左右，中间留给功能键。
        // Separate up/down on the left, left/right on the right, with function keys between them.
        if(direction==0){*x=34;*y=92;*w=155;*h=88;}
        else if(direction==2){*x=34;*y=202;*w=155;*h=88;}
        else if(direction==3){*x=416;*y=128;*w=82;*h=124;}
        else {*x=512;*y=128;*w=82;*h=124;}
    }else{
        if(direction==0){*x=30;*y=160;*w=120;*h=80;}
        else if(direction==2){*x=30;*y=260;*w=120;*h=80;}
        else if(direction==3){*x=200;*y=190;*w=60;*h=100;}
        else {*x=270;*y=190;*w=60;*h=100;}
    }
}
static void pa_snake_single_rect(int direction,int *x,int *y,int *w,int *h)
{
    if(direction==0){*x=48;*y=112;*w=64;*h=40;}
    else if(direction==2){*x=48;*y=176;*w=64;*h=40;}
    else if(direction==3){*x=488;*y=144;*w=48;*h=72;}
    else {*x=560;*y=144;*w=48;*h=72;}
}
static void pa_draw_controller(uint8_t *fb,int kind)
{
    ps_view_t v=ps_view();
    // 说明并入右侧连接状态，避免副标题占用控制区顶部。
    // The subtitle is folded into the right-side status to keep the control area clear.
    ps_heading(fb,kind==PA_2048?"2048 · 控制":"贪吃蛇 · 控制",NULL);
    // 连接状态放标题栏右侧，避开系统状态栏和中间功能键。
    // Put connection status at the right of the title bar, clear of the OS status bar and center controls.
    ps_text(fb,v.w-124,36,11,"蓝牙已连接",4);
    const char *arrows[]={"↑","→","↓","←"};
    for(int d=0;d<4;d++){int x,y,w,h;pa_controller_rect(d,&x,&y,&w,&h);ps_button(fb,x,y,w,h,arrows[d],false);}
    if(kind==PA_2048){
        if(v.landscape)ps_button(fb,224,160,160,54,"重新开始",false);
        else ps_button(fb,40,390,260,52,"重新开始",false);
    }else if(v.landscape){
        ps_button(fb,224,96,160,54,"暂停 / 继续",false);
        ps_button(fb,224,160,160,54,"切换速度",false);
        ps_button(fb,224,224,160,54,"重新开始",false);
    }else{
        ps_button(fb,40,390,120,52,"暂停 / 继续",false);
        ps_button(fb,180,390,120,52,"切换速度",false);
        ps_button(fb,40,456,260,52,"重新开始",false);
    }
}
static void pa_poll_duo(void)
{
#ifdef PICO_GAME_MODULE
    if(a_duo_role!=PGA_DUO_SCREEN)return;
    for(int i=0;i<4;i++){
        int input=pga_duo_recv();if(input<0)break;
        if(input<=3){
            if(a_paused&&!a_save.snake_over){a_paused=false;ps_dirty();}
            if(a_kind==PA_2048)pa_shift(input);else pa_direction(input);
        }
        else if(input==PGA_DUO_PAUSE&&a_kind==PA_SNAKE&&!a_save.snake_over){a_paused=!a_paused;pa_clear_input();a_due=pg_millis()+pa_delay();if(a_paused)pa_persist();ps_dirty();}
        else if(input==PGA_DUO_SPEED&&a_kind==PA_SNAKE){a_save.speed=(a_save.speed+1)%3;a_due=pg_millis()+pa_delay();pa_persist();ps_dirty();}
        else if(input==PGA_DUO_RESTART){pa_reset();}
    }
#endif
}
static void pa_draw(uint8_t *fb){
    a_duo_role=pa_duo_role_now();
    if(a_duo_role==PGA_DUO_CONTROLLER&&(a_kind==PA_SNAKE||a_kind==PA_2048)){pa_draw_controller(fb,a_kind);return;}
    ps_view_t v=ps_view();char b[80];
    bool remote=a_kind==PA_SNAKE&&a_duo_role==PGA_DUO_SCREEN;
    bool snake_single=a_kind==PA_SNAKE&&!remote&&v.landscape;
    if(snake_single){
        ps_button(fb,10,30,28,26,"‹",false);
        ps_text(fb,48,32,18,"贪吃蛇",0);
    } else if(!remote)ps_heading(fb,a_kind==PA_2048?"2048":"贪吃蛇",NULL);
    unsigned score=a_kind==PA_2048?a_save.score:a_save.snake_score,best=a_kind==PA_2048?a_save.best:a_save.snake_best;
    snprintf(b,sizeof(b),"得分 %u    最高 %u",score,best);
    if(!remote)ps_text(fb,snake_single?488:(v.landscape?306:20),snake_single?32:(v.landscape?38:76),snake_single?12:13,b,2);
    int x,y,size,bw=0,bh=0;
    if(remote){
        // 副机手柄在线时只留一条窄标题栏，棋盘下移并略微右偏，避开顶部设备状态栏。
        // Keep only a slim header, move the board below the status bar and nudge it right to look centered.
        bw=v.landscape?312:300;
        bh=v.landscape?336:300;
        x=v.landscape?192:20;
        y=v.landscape?16:104;
    } else if(snake_single){bw=296;bh=288;x=168;y=64;size=bw;}
    else { pa_board(&x,&y,&size); bw=bh=size; }
    ps_box(fb,x,y,bw,bh,5,0,false);
    if(a_kind==PA_2048){int cell=(size-12)/4;for(int i=0;i<16;i++){int xx=x+6+(i%4)*cell,yy=y+6+(i/4)*cell;uint32_t value=a_save.board[i];int gray=value>=128?2:value>=16?10:14;ps_box(fb,xx+2,yy+2,cell-4,cell-4,4,gray,true);if(value){snprintf(b,sizeof(b),"%u",(unsigned)value);ps_center(fb,xx+cell/2,yy+(cell-23)/2,value>=10000?14:value>=1000?18:25,b,value>=128?15:0);}}}
    else{int cw=(bw-8)/20,ch=(bh-8)/20,px=(bw-cw*20)/2,py=(bh-ch*20)/2;for(int i=0;i<a_save.length;i++){int p=a_save.snake[i];ps_fill(fb,x+px+(p%20)*cw,y+py+(p/20)*ch,cw-1,ch-1,i?3:0);}if(a_save.food<PA_CELLS){int p=a_save.food;ps_box(fb,x+px+(p%20)*cw,y+py+(p/20)*ch,cw-1,ch-1,2,0,false);}if(a_paused||a_save.snake_over){int ow=bw-30;ps_box(fb,x+15,y+bh/2-22,ow,44,5,15,true);ps_box(fb,x+15,y+bh/2-22,ow,44,5,0,false);ps_center(fb,x+bw/2,y+bh/2-9,15,a_save.snake_over?"本局结束":a_save.snake_moves?"已暂停 · 点继续":"点击开始游戏",0);}}
    if(a_kind==PA_2048&&(a_save.over||(a_save.won&&!a_save.keep_playing))){ps_box(fb,x+12,y+size/2-30,size-24,60,5,15,true);ps_box(fb,x+12,y+size/2-30,size-24,60,5,0,false);ps_center(fb,x+size/2,y+size/2-16,20,a_save.over?"本局结束":"达成2048",0);ps_center(fb,x+size/2,y+size/2+9,12,a_save.over?"点重开再试一次":"点棋盘继续挑战",3);}
    if(!remote&&snake_single){
        const char *arrows[]={"↑","→","↓","←"};
        for(int d=0;d<4;d++){int xx,yy,ww,hh;pa_snake_single_rect(d,&xx,&yy,&ww,&hh);ps_button(fb,xx,yy,ww,hh,arrows[d],false);}
        ps_button(fb,24,264,112,40,a_save.speed==0?"速度 · 慢":a_save.speed==1?"速度 · 中":"速度 · 快",false);
        ps_button(fb,24,312,112,40,"重新开始",false);
        ps_button(fb,488,312,120,40,a_save.snake_over?"本局结束":a_paused?(a_save.snake_moves?"继续":"开始"):"暂停",!a_paused);
    } else if(!remote){
        const char *arrows[]={"↑","→","↓","←"};for(int d=0;d<4;d++){int xx,yy;pa_direction_rect(d,&xx,&yy);ps_button(fb,xx,yy,44,44,arrows[d],a_kind==PA_SNAKE&&!a_paused&&!a_save.snake_over&&d==(a_follow>=0?a_follow:a_next));}
        int bx=v.landscape?306:62,by=v.landscape?240:530,bw=v.landscape?146:114;
        ps_button(fb,bx,by,bw,v.landscape?40:40,"重新开始",false);ps_button(fb,bx+bw+8,by,bw,v.landscape?40:40,"返回小游戏",false);
        if(a_kind==PA_SNAKE){int sy=v.landscape?286:580,sw=v.landscape?146:114;const char *speeds[]={"速度 · 慢","速度 · 中","速度 · 快"};ps_button(fb,bx,sy,sw,v.landscape?40:40,speeds[a_save.speed],false);ps_button(fb,bx+sw+8,sy,sw,v.landscape?40:40,a_save.snake_over?"本局结束":a_paused?(a_save.snake_moves?"继续":"开始"):"暂停",!a_paused);}
        else ps_center(fb,v.landscape?455:v.w/2,v.landscape?340:590,11,a_save.over?"尝试不同的合并顺序":"滑动棋盘或点击方向",5);
    } else {
        if(v.landscape){
            ps_button(fb,10,34,62,26,"‹ 返回",false);
            ps_text(fb,80,32,18,"贪吃蛇",0);
            ps_text(fb,18,86,12,"得分",5);
            snprintf(b,sizeof(b),"%u",score);ps_text(fb,18,104,30,b,0);
            ps_text(fb,18,148,12,"最高",5);
            snprintf(b,sizeof(b),"%u",best);ps_text(fb,18,166,19,b,3);
            // 副机状态与操作提示统一放左侧信息栏，不再占用棋盘上方。
            // Keep the peer status and controls hint in the left column, never above the board.
            ps_text(fb,18,204,10,"控制机在线",4);
            ps_text(fb,18,220,10,"方向 / 暂停 / 速度",5);
            ps_text(fb,18,234,10,"重开由控制机操作",5);
        } else {
            ps_text(fb,20,84,12,"得分",5);snprintf(b,sizeof(b),"%u",score);ps_text(fb,20,102,26,b,0);
            ps_text(fb,180,84,12,"最高",5);snprintf(b,sizeof(b),"%u",best);ps_text(fb,180,102,18,b,3);
            ps_text(fb,20,v.h-24,11,"副机手柄在线 · 由副机操作方向 / 暂停 / 速度 / 重开",4);
        }
    }
    if(a_unsaved)ps_center(fb,v.w/2,v.h-30,11,"游戏未保存，请检查存储空间",0);
}
static bool pa_event(const ps_event_t *e){
    if(e->type==PS_CANCEL){a_swipe_tracking=a_contact_handled=false;return true;}
#ifdef PICO_GAME_MODULE
    a_duo_role=pa_duo_role_now();
    if(a_duo_role==PGA_DUO_CONTROLLER&&(a_kind==PA_SNAKE||a_kind==PA_2048)){
        if(e->type==PS_SWIPE_U||e->type==PS_SWIPE_R||e->type==PS_SWIPE_D||e->type==PS_SWIPE_L){
            int d=e->type==PS_SWIPE_U?0:e->type==PS_SWIPE_R?1:e->type==PS_SWIPE_D?2:3;
            pga_duo_send((uint8_t)d);return true;
        }
        if(e->type==PS_PRESS){
            // 按下即发送，不再等抬手后的 TAP；这是方向延迟最大的来源。
            // Send on press, not on release-generated TAP; that was the main direction latency.
            for(int d=0;d<4;d++){int x,y,w,h;pa_controller_rect(d,&x,&y,&w,&h);if(ps_hit(e->x,e->y,x,y,w,h)){pga_duo_send((uint8_t)d);return true;}}
            ps_view_t v=ps_view();
            if(a_kind==PA_2048){
                if(v.landscape&&ps_hit(e->x,e->y,224,160,160,54)){pga_duo_send(PGA_DUO_RESTART);return true;}
                if(!v.landscape&&ps_hit(e->x,e->y,40,390,260,52)){pga_duo_send(PGA_DUO_RESTART);return true;}
            }else if(v.landscape){
                if(ps_hit(e->x,e->y,224,96,160,54)){pga_duo_send(PGA_DUO_PAUSE);return true;}
                if(ps_hit(e->x,e->y,224,160,160,54)){pga_duo_send(PGA_DUO_SPEED);return true;}
                if(ps_hit(e->x,e->y,224,224,160,54)){pga_duo_send(PGA_DUO_RESTART);return true;}
            }else{
                if(ps_hit(e->x,e->y,40,390,120,52)){pga_duo_send(PGA_DUO_PAUSE);return true;}
                if(ps_hit(e->x,e->y,180,390,120,52)){pga_duo_send(PGA_DUO_SPEED);return true;}
                if(ps_hit(e->x,e->y,40,456,260,52)){pga_duo_send(PGA_DUO_RESTART);return true;}
            }
        }
        if(e->type==PS_TAP)return true;
        return e->type==PS_PRESS||e->type==PS_RELEASE||e->type==PS_MOVE;
    }
#endif
    if(a_kind==PA_2048){
        // 2048 只用手指滑动棋盘，不绘制也不响应十字键。
        // 2048 is swipe-only: no D-pad is drawn or handled.
        if(e->type==PS_PRESS){
            a_swipe_tracking=a_contact_handled=false;
            int x,y,size;pa_board(&x,&y,&size);
            a_swipe_tracking=ps_hit(e->x,e->y,x,y,size,size);
            return false;
        }
        if(e->type==PS_MOVE&&a_swipe_tracking&&!a_contact_handled){
            int dx=e->x-e->x0,dy=e->y-e->y0,ax=abs(dx),ay=abs(dy);
            if(pg_max(ax,ay)>=24&&pg_max(ax,ay)*4>=pg_min(ax,ay)*5){pa_shift(ax>ay?(dx>0?1:3):(dy>0?2:0));a_contact_handled=true;return true;}
            return false;
        }
        if(e->type==PS_RELEASE){a_swipe_tracking=false;return false;}
    }
    if(a_kind==PA_SNAKE&&a_duo_role==PGA_DUO_SCREEN){
        ps_view_t v=ps_view();
        if(e->type==PS_TAP&&v.landscape&&ps_hit(e->x,e->y,10,34,62,26)){pa_pause();ps_navigate(PS_GAMES);return true;}
        return false;
    }
    if(a_kind==PA_SNAKE&&a_duo_role!=PGA_DUO_SCREEN&&ps_view().landscape){
        // 单机横屏贪吃蛇：导出布局的独立按键区。
        // Single-device snake: the exported standalone control layout.
        if(e->type==PS_PRESS){
            if(ps_hit(e->x,e->y,10,30,28,26)){pa_pause();ps_navigate(PS_GAMES);return true;}
            a_swipe_tracking=a_contact_handled=false;
            for(int d=0;d<4;d++){int x,y,w,h;pa_snake_single_rect(d,&x,&y,&w,&h);if(ps_hit(e->x,e->y,x,y,w,h)){pa_direction(d);a_contact_handled=true;return true;}}
            a_swipe_tracking=!a_paused&&!a_save.snake_over&&ps_hit(e->x,e->y,168,64,296,288);
            return false;
        }
        if(e->type==PS_MOVE&&a_swipe_tracking&&!a_contact_handled){
            int dx=e->x-e->x0,dy=e->y-e->y0,ax=abs(dx),ay=abs(dy);
            if(pg_max(ax,ay)>=34&&pg_max(ax,ay)*4>=pg_min(ax,ay)*5){pa_direction(ax>ay?(dx>0?1:3):(dy>0?2:0));a_contact_handled=true;return true;}
            return false;
        }
        if(e->type==PS_RELEASE){a_swipe_tracking=false;return false;}
        if(e->type==PS_TAP){
            if(ps_hit(e->x,e->y,24,264,112,40)){a_save.speed=(a_save.speed+1)%3;a_due=pg_millis()+pa_delay();pa_persist();return true;}
            if(ps_hit(e->x,e->y,24,312,112,40)){pa_reset();return true;}
            if(ps_hit(e->x,e->y,488,312,120,40)){a_paused=!a_paused;a_due=pg_millis()+pa_delay();if(a_paused)pa_persist();return true;}
        }
        return false;
    }
    if(a_kind==PA_SNAKE){
        // 方向键按下即响应；棋盘滑动达到阈值后只消费一次，松手合成事件不重复转向。
        // Respond on direction-key press; consume a thresholded board swipe once, never again on release synthesis.
        if(e->type==PS_PRESS){
            a_swipe_tracking=a_contact_handled=false;
            for(int d=0;d<4;d++){int x,y;pa_direction_rect(d,&x,&y);if(ps_hit(e->x,e->y,x,y,44,44)){
                pa_direction(d);a_contact_handled=true;return true;
            }}
            int x,y,size;pa_board(&x,&y,&size);
            a_swipe_tracking=!a_paused&&!a_save.snake_over&&ps_hit(e->x,e->y,x,y,size,size);
            return false;
        }
        if(e->type==PS_MOVE&&a_swipe_tracking&&!a_contact_handled){
            int dx=e->x-e->x0,dy=e->y-e->y0,ax=abs(dx),ay=abs(dy);
            if(pg_max(ax,ay)>=34&&pg_max(ax,ay)*4>=pg_min(ax,ay)*5){
                pa_direction(ax>ay?(dx>0?1:3):(dy>0?2:0));a_contact_handled=true;return true;
            }
            return false;
        }
        if(e->type==PS_RELEASE){a_swipe_tracking=false;return false;}
        if(a_contact_handled&&(e->type==PS_TAP||e->type==PS_SWIPE_U||e->type==PS_SWIPE_R||e->type==PS_SWIPE_D||e->type==PS_SWIPE_L)){
            a_contact_handled=false;return true;
        }
    }
    if(e->type==PS_SWIPE_U||e->type==PS_SWIPE_R||e->type==PS_SWIPE_D||e->type==PS_SWIPE_L){
        if(a_kind==PA_2048&&a_contact_handled){a_contact_handled=false;return true;}
        int d=e->type==PS_SWIPE_U?0:e->type==PS_SWIPE_R?1:e->type==PS_SWIPE_D?2:3;
        if(a_kind==PA_2048)pa_shift(d);else pa_direction(d);return true;
    }
    if(e->type!=PS_TAP)return false;
    ps_view_t v=ps_view();
    for(int d=0;d<4;d++){int x,y;pa_direction_rect(d,&x,&y);if(ps_hit(e->x,e->y,x,y,44,44)){if(a_kind==PA_2048)pa_shift(d);else pa_direction(d);return true;}}
    int bx=v.landscape?306:62,by=v.landscape?240:530,bw=v.landscape?146:114;
    if(ps_hit(e->x,e->y,bx,by,bw,40)){pa_reset();return true;}
    if(ps_hit(e->x,e->y,bx+bw+8,by,bw,40)){pa_pause();ps_navigate(PS_GAMES);return true;}
    if(a_kind==PA_SNAKE){int sy=v.landscape?286:580;if(ps_hit(e->x,e->y,bx,sy,bw,40)){a_save.speed=(a_save.speed+1)%3;a_due=pg_millis()+pa_delay();pa_persist();return true;}if(ps_hit(e->x,e->y,bx+bw+8,sy,bw,40)){ps_games_action(2);return true;}}
    int x,y,size;pa_board(&x,&y,&size);if(a_kind==PA_2048&&a_save.won&&!a_save.keep_playing&&ps_hit(e->x,e->y,x,y,size,size)){a_save.keep_playing=1;pa_persist();return true;}
    return false;
}

#ifndef PICO_GAME_MODULE
static void pg_lobby_card(int type,int *x,int *y,int *w,int *h)
{
    ps_view_t view=ps_view();int index=type+2,cols=view.landscape?3:2;
    *h=view.landscape?92:132;*w=(view.w-32-(cols-1)*10)/cols;
    *x=16+(index%cols)*(*w+10);*y=143+(index/cols)*(*h+10);
}

static void pg_draw_lobby(uint8_t *fb)
{
    ps_view_t view=ps_view();ps_tabs(fb,2);ps_text(fb,16,87,25,"小游戏",2);ps_text(fb,16,121,11,"离线休闲与对弈 · 自动保存",5);
    if(g_save.active)ps_button(fb,view.w-148,87,132,30,"继续棋局",true);
    for(int kind=0;kind<2;kind++){
        int cols=view.landscape?3:2,w=(view.w-32-(cols-1)*10)/cols,h=view.landscape?92:132,x=16+(kind%cols)*(w+10),y=143+(kind/cols)*(h+10);
        ps_box(fb,x,y,w,h,8,0,false);ps_text(fb,x+12,y+10,20,kind?"贪吃蛇":"2048",0);
        ps_text(fb,x+12,y+(view.landscape?38:48),11,kind?"方向控制 · 三档速度":"滑动合并 · 挑战高分",5);
        ps_button(fb,x+10,y+h-36,w-20,28,kind?(a_save.snake_initialized?"打开 / 继续":"开始游戏"):(a_save.initialized?"打开 / 继续":"开始游戏"),true);
    }
    for(int type=0;type<PG_TYPES;type++){
        int x,y,w,h;pg_lobby_card(type,&x,&y,&w,&h);pg_progress_t *p=&g_save.progress[type];
        ps_box(fb,x,y,w,h,8,0,false);if(type==g_lobby_selected)ps_fill(fb,x+1,y+13,3,h-26,0);
        ps_text(fb,x+10,y+6,18,g_titles[type],0);
        if(view.landscape){ps_button(fb,x+w-76,y+5,68,25,g_levels[p->level-1],false);ps_text(fb,x+10,y+35,10,"自动进阶",3);ps_box(fb,x+w-26,y+34,14,14,2,0,p->automatic);ps_button(fb,x+10,y+h-33,w-20,26,"开始新局",true);}
        else{ps_button(fb,x+10,y+33,w-20,26,g_levels[p->level-1],false);ps_box(fb,x+10,y+69,14,14,2,0,p->automatic);ps_text(fb,x+31,y+69,10,"自动进阶",3);ps_button(fb,x+10,y+h-36,w-20,28,"开始新局",true);}
    }
}

#else
static void pg_draw_modal(uint8_t *fb);
static void pg_draw_lobby(uint8_t *fb)
{
    ps_view_t view=ps_view();int type=PICO_GAME_KIND;
    if(type>=PG_TYPES)return;
    pg_progress_t *p=&g_save.progress[type];char details[96];
    ps_heading(fb,g_titles[type],"难度与对局 · 自动保存");
    int x=view.landscape?42:24,w=view.landscape?240:view.w-48,y=view.landscape?88:118;
    ps_text(fb,x,y,18,"对手难度",0);
    int gap=8,bw=(w-2*gap)/3;
    for(int i=0;i<3;i++)ps_button(fb,x+i*(bw+gap),y+32,bw,38,g_levels[i],p->level==i+1);
    ps_box(fb,x,y+90,22,22,3,0,p->automatic);
    ps_text(fb,x+34,y+89,16,"连胜后自动进阶",0);
    snprintf(details,sizeof(details),"已完成 %u 局 · 获胜 %u 局",p->played,p->wins);
    ps_text(fb,x,y+132,14,details,4);
    int bx=view.landscape?324:x,by=view.landscape?94:y+188,bw2=view.landscape?view.w-350:w;
    if(g_save.active)ps_button(fb,bx,by,bw2,42,"继续棋局",true);
    ps_button(fb,bx,by+54,bw2,42,"开始新局",!g_save.active);
    ps_button(fb,bx,by+108,bw2,38,"规则与提示",false);
    if(g_unsaved)ps_text(fb,x,view.h-62,13,"棋局未保存，请检查存储空间",0);
    if(g_modal)pg_draw_modal(fb);
}
#endif

static const char *pg_status(void)
{
    if(g_pos.result==PG_SCORE)return "双方停一手 · 确认终局";
    if(g_pos.result==PG_DRAW){switch(g_pos.reason){
        case PG_STALEMATE:return "本局和棋 · 逼和";case PG_MATERIAL:return "本局和棋 · 子力不足";
        case PG_REPEAT:return "本局和棋 · 三次重复";case PG_QUIET:return g_pos.type==PG_CHESS?"本局和棋 · 五十回合":"本局和棋 · 120 步无进展";
        default:return "本局和棋";}}
    if(g_pos.result==1)return "你赢了，下一局继续挑战";
    if(g_pos.result==-1)return "对手获胜，再试一局";
    if(g_pos.turn==-1)return g_ai_error?"对手计算暂停":g_busy?"对手思考中…":"等待对手落子";
    if((g_pos.type==PG_XIANGQI||g_pos.type==PG_CHESS)&&pg_check(&g_pos,1))return "将军 · 请解将";
    return g_pos.type==PG_XIANGQI?"轮到你 · 红方":g_pos.type==PG_CHESS?"轮到你 · 白方":"轮到你 · 黑棋";
}

static void pg_side_rect(int *x,int *y,int *w)
{
    ps_view_t view=ps_view();pg_board_rect_t board=pg_board_rect();
    *x=view.landscape?296:8;*y=view.landscape?100:board.y+board.h+10;*w=view.landscape?view.w-304:view.w-16;
}

static void pg_draw_modal(uint8_t *fb)
{
    ps_view_t view=ps_view();int w=pg_min(view.w-40,500),h=g_modal==2?(view.landscape?264:346):212;
    int x=(view.w-w)/2,y=(view.h-h)/2;
    ps_box(fb,x,y,w,h,10,15,true);ps_box(fb,x,y,w,h,10,0,false);
    if(g_modal==1){ps_text(fb,x+20,y+22,22,"重新开始这一局？",0);
        ps_wrap(fb,x+20,y+63,w-40,15,3,"当前棋局将重置，难度与训练成绩会保留。",4);
        ps_button(fb,x+20,y+h-55,(w-52)/2,35,"继续这一局",false);
        ps_button(fb,x+32+(w-52)/2,y+h-55,(w-52)/2,35,"开始新局",true);
    }else if(g_modal==2){ps_text(fb,x+20,y+20,22,"规则与提示",0);
        ps_wrap(fb,x+20,y+60,w-40,15,view.landscape?8:13,g_rules[g_pos.type],0);
        ps_wrap(fb,x+20,y+h-88,w-40,11,3,"棋局自动保存在本机。轻量离线对手，非专业棋力；单局最多保存 1024 手。",5);
        ps_button(fb,x+20,y+h-42,w-40,28,"知道了",true);
    }else{ps_text(fb,x+20,y+20,22,"兵升变",0);ps_text(fb,x+20,y+54,14,"选择升变后的棋子",4);
        const int pieces[]={5,2,4,3};const char *names[]={"后","车","象","马"};int bw=(w-48)/4;
        for(int i=0;i<4;i++){int bx=x+18+i*(bw+4);ps_button(fb,bx,y+86,bw,88,"",false);
            pg_chess_piece(fb,bx+bw/2,y+118,40,pieces[i]);ps_center(fb,bx+bw/2,y+150,15,names[i],0);}
    }
}

#ifdef PICO_GAMES_UI_HOST
static int pg_test_duo_role;
#endif
static int pg_duo_role_now(void)
{
#ifdef PICO_GAMES_UI_HOST
    return pg_test_duo_role;
#elif defined(PICO_GAME_MODULE)
    return pga_duo_role();
#else
    return PGA_DUO_NONE;
#endif
}

static void pg_duo_send_action(uint8_t code)
{
#ifdef PICO_GAME_MODULE
    pga_duo_send(code);
#else
    (void)code;
#endif
}
static uint8_t pg_duo_capture[2][8];
static bool pg_duo_capture_ready;
static uint32_t pg_duo_peer_generation,pg_duo_peer_moves;
enum { PG_DUO_UNDO=10, PG_DUO_RESTART=11, PG_DUO_RULES=12 };

static void pg_capture_counts(int side,int out[8])
{
    memset(out,0,8*sizeof(int));
    pg_pos_t initial;pg_initial(&initial,g_pos.type,g_pos.level);
    for(int i=0;i<pg_count(&g_pos);i++){
        int a=initial.board[i],b=g_pos.board[i];
        if(!a||pg_sign(a)!=side)continue;
        if(!b||pg_sign(b)!=side)out[abs(a)]++;
    }
}
static int64_t pg_duo_last_send_ms;
static uint32_t pg_duo_last_signature=UINT32_MAX;

#ifdef PICO_GAME_MODULE
static uint32_t pg_duo_pack_board(uint8_t *p)
{
    uint32_t moves=g_pos.moves;
    p[0]=(uint8_t)g_pos.type;p[1]=(uint8_t)g_pos.level;p[2]=(uint8_t)(g_pos.turn+1);
    p[3]=(uint8_t)(g_pos.result+1);
    p[4]=(uint8_t)moves;p[5]=(uint8_t)(moves>>8);p[6]=(uint8_t)(moves>>16);p[7]=(uint8_t)(moves>>24);
    p[8]=(uint8_t)pg_duo_generation;p[9]=(uint8_t)(pg_duo_generation>>8);
    p[10]=(uint8_t)(pg_duo_generation>>16);p[11]=(uint8_t)(pg_duo_generation>>24);
    int w[8],b[8];pg_capture_counts(1,w);pg_capture_counts(-1,b);
    for(int i=0;i<8;i++){p[12+i]=(uint8_t)w[i];p[20+i]=(uint8_t)b[i];}
    p[28]=g_save.active?1:0;
    return moves^pg_duo_generation^(uint32_t)g_pos.result<<16^(uint32_t)g_pos.turn<<20^(uint32_t)g_save.active<<24;
}
#endif

static void pg_duo_poll(void)
{
    int role=pg_duo_role_now();
    if(!role)return;
#ifdef PICO_GAME_MODULE
    int64_t now=pg_millis();
    if(role==PGA_DUO_SCREEN){
        for(int i=0;i<4;i++){
            int action=pga_duo_recv();if(action<0)break;
            if(action==PG_DUO_UNDO)pg_undo();
            else if(action==PG_DUO_RESTART)pg_start_game(g_pos.type);
            else if(action==PG_DUO_RULES)g_modal=2;
            ps_dirty();
        }
        uint8_t packet[29];uint32_t signature=pg_duo_pack_board(packet);
        // 变化立即发送，另外每 700ms 重发一次，丢一包也能在下一拍恢复。
        // Send on change and repeat every 700 ms so one lost BLE packet self-heals.
        if(signature!=pg_duo_last_signature||now-pg_duo_last_send_ms>=700){
            if(pga_duo_packet_send(packet,sizeof(packet))){pg_duo_last_signature=signature;pg_duo_last_send_ms=now;}
        }
    }else if(role==PGA_DUO_CONTROLLER){
        uint8_t packet[29];
        int n=(int)pga_duo_packet_recv(packet,sizeof(packet));
        if(n>=29&&packet[0]<PG_TYPES){
            uint32_t generation=(uint32_t)packet[8]|(uint32_t)packet[9]<<8|
                (uint32_t)packet[10]<<16|(uint32_t)packet[11]<<24;
            uint32_t moves=(uint32_t)packet[4]|(uint32_t)packet[5]<<8|
                (uint32_t)packet[6]<<16|(uint32_t)packet[7]<<24;
            bool stale=pg_duo_capture_ready&&g_pos.type==packet[0]&&
                ((int32_t)(generation-pg_duo_peer_generation)<0||
                 (generation==pg_duo_peer_generation&&moves<pg_duo_peer_moves));
            if(!stale){
                if(!g_save.active||g_pos.type!=packet[0]){
                    g_save.active=1;g_save.type=packet[0];g_save.level=packet[1];
                    pg_initial(&g_pos,packet[0],g_save.level);g_selected=-1;g_modal=0;g_feedback[0]=0;
                }
                g_pos.type=packet[0];g_pos.level=packet[1];g_pos.turn=(int8_t)packet[2]-1;g_pos.result=(int8_t)packet[3]-1;
                g_pos.moves=(uint16_t)moves;
                g_save.active=packet[28]&1;
                for(int i=0;i<8;i++){pg_duo_capture[0][i]=packet[12+i];pg_duo_capture[1][i]=packet[20+i];}
                pg_duo_peer_generation=generation;pg_duo_peer_moves=moves;
                pg_duo_capture_ready=true;ps_dirty();
            }
        }
    }
#endif
}

static void pg_draw_captured_side(uint8_t *fb,int side,int x,int y)
{
    int missing[8]={0};
    if(pg_duo_capture_ready){
        for(int i=0;i<8;i++)missing[i]=pg_duo_capture[side>0?0:1][i];
    }else pg_capture_counts(side,missing);
    const char *names[8]={"","兵","车","马","象","后","王","炮"};
    if(g_pos.type==PG_CHESS){const int order[]={5,2,4,3,6};int cx=x,cy=y;
        for(unsigned k=0;k<sizeof(order)/sizeof(*order);k++)for(int n=0;n<missing[order[k]];n++){
            if(cx+34>x+190){cx=x;cy+=38;}pg_chess_piece(fb,cx+17,cy+17,32,side>0?order[k]:-order[k]);cx+=38;
        }
    }else if(g_pos.type==PG_XIANGQI){
        int cx=x,cy=y;
        for(int p=1;p<=7;p++)for(int n=0;n<missing[p];n++){
            if(cx+34>x+190){cx=x;cy+=38;}
            pg_circle(fb,cx+17,cy+17,15,side>0?15:0);
            ps_center(fb,cx+17,cy+7,17,names[p],side>0?0:15);cx+=38;
        }
    }
}

static void pg_draw_dual_captures(uint8_t *fb)
{
    ps_button(fb,10,30,28,26,"‹",false);
    ps_text(fb,48,32,20,"被吃掉的子",0);
    ps_text(fb,20,82,13,g_titles[g_pos.type],5);
    ps_wrap(fb,20,100,320,10,1,pg_status(),4);
    if(g_pos.type==PG_CHESS||g_pos.type==PG_XIANGQI){
        ps_text(fb,20,112,12,g_pos.type==PG_CHESS?"白方损失":"红方损失",4);
        pg_draw_captured_side(fb,1,20,138);
        ps_text(fb,20,292,12,"黑方损失",4);
        pg_draw_captured_side(fb,-1,20,318);
    }else{
        ps_text(fb,20,150,16,g_pos.type==PG_GO?"围棋没有单子吃子记录":"五子棋没有吃子",0);
        ps_text(fb,20,184,12,"主设备负责棋盘和对局",5);
    }
    ps_button(fb,20,486,150,40,"悔棋",false);
    ps_button(fb,190,486,150,40,"重开",false);
    ps_button(fb,20,540,150,40,"规则与提示",false);
    ps_button(fb,190,540,150,40,"棋室",false);
    ps_text(fb,20,600,10,"本机仅显示菜单和吃子",6);
}

static void pg_draw_dual_board(uint8_t *fb)
{
    ps_view_t v=ps_view();
    ps_button(fb,10,30,28,26,"‹",false);
    ps_text(fb,48,32,20,g_titles[g_pos.type],0);
    ps_button(fb,v.w-100,32,80,28,"棋室",false);
    pg_draw_board(fb);
    pg_board_rect_t r=pg_board_rect();
    ps_wrap(fb,20,r.y+r.h+8,v.w-40,13,2,pg_status(),0);
}

static void pg_draw_play(uint8_t *fb)
{
    ps_view_t view=ps_view();char subtitle[96];
    snprintf(subtitle,sizeof(subtitle),"%s · 第 %u 手%s",g_levels[g_pos.level-1],g_pos.moves+1,g_unsaved?" · 未保存":"");
    int duo=pg_duo_role_now();
    if(duo==PGA_DUO_CONTROLLER){pg_draw_dual_captures(fb);return;}
    if(duo==PGA_DUO_SCREEN){pg_draw_dual_board(fb);return;}
    if(view.landscape){
        // 棋类统一布局：标题左上、状态右上、操作左栏、棋盘居中。
        // Unified landscape board layout: title left, status right, actions left, board centered.
        ps_button(fb,10,30,28,26,"‹",false);
        ps_text(fb,48,32,18,g_titles[g_pos.type],0);
        ps_button(fb,488,32,120,28,"棋室",false);
        ps_wrap(fb,488,72,128,14,2,pg_status(),0);
        ps_text(fb,488,118,10,subtitle,5);
        pg_draw_board(fb);
        if(g_pos.type==PG_GO&&g_pos.result){pg_score_t score=pg_score_current();char score_text[80];
            snprintf(score_text,sizeof(score_text),"黑 %d%s · 白 %d%s（含贴子）",score.black2/2,score.black2%2?".5":"",score.white2/2,score.white2%2?".5":"");
            ps_wrap(fb,488,148,128,10,3,score_text,0);}
        if(g_feedback[0])ps_wrap(fb,488,148,128,10,2,g_feedback,4);
        if(g_pos.result==PG_SCORE){
            ps_button(fb,24,112,112,40,"确认结果",true);
            ps_button(fb,24,176,112,40,"继续对局",false);
            ps_button(fb,24,240,112,40,"规则与提示",false);
        }else if(g_pos.type==PG_GO){
            ps_button(fb,24,104,112,40,"悔棋",false);
            ps_button(fb,24,152,112,40,"停一手",false);
            ps_button(fb,24,200,112,40,"重开",false);
            ps_button(fb,24,248,112,40,"规则与提示",false);
        }else{
            ps_button(fb,24,112,112,40,"悔棋",false);
            ps_button(fb,24,176,112,40,"重开",false);
            if(g_ai_error){
                ps_button(fb,24,240,112,34,"重试对手",true);
                ps_button(fb,24,286,112,30,"规则",false);
            }else ps_button(fb,24,240,112,40,"规则与提示",false);
        }
        ps_wrap(fb,488,300,128,10,3,"自动保存 · 中键返回 · 双击切换主页",6);
        if(g_modal)pg_draw_modal(fb);
        return;
    }
    ps_heading(fb,g_titles[g_pos.type],view.landscape?NULL:subtitle);
    if(view.landscape)ps_text(fb,292,77,11,subtitle,5);
    ps_button(fb,view.w-78,32,62,28,"棋室",false);pg_draw_board(fb);
    int x,y,w;pg_side_rect(&x,&y,&w);ps_wrap(fb,x,y,w,16,2,pg_status(),0);
    if(g_pos.type==PG_GO&&g_pos.result){pg_score_t score=pg_score_current();char score_text[80];
        snprintf(score_text,sizeof(score_text),"黑 %d%s · 白 %d%s（含贴子）",score.black2/2,score.black2%2?".5":"",score.white2/2,score.white2%2?".5":"");
        ps_text(fb,x,y+27,14,score_text,0);}
    if(g_pos.result==PG_SCORE){ps_text(fb,x,y+51,11,"点击棋子标记或取消整组死子",5);
        ps_button(fb,x,y+75,(w-10)/2,34,"确认结果",true);ps_button(fb,x+(w+10)/2,y+75,(w-10)/2,34,"继续对局",false);
        ps_button(fb,x,y+119,w,28,"规则与提示",false);
    }else{
        if(g_feedback[0])ps_wrap(fb,x,y+26,w,12,2,g_feedback,4);
        else if(!g_pos.result)ps_text(fb,x,y+27,11,g_pos.type==PG_CHESS||g_pos.type==PG_XIANGQI?"点击己方棋子，标记可走的位置":"点击棋盘落子 · 可随时切回阅读",6);
        int n=g_pos.type==PG_GO?3:2,bw=(w-(n-1)*8)/n;
        ps_button(fb,x,y+59,bw,34,"悔棋",false);
        if(n==3)ps_button(fb,x+bw+8,y+59,bw,34,"停一手",false);
        ps_button(fb,x+(n-1)*(bw+8),y+59,bw,34,"重开",false);
        if(g_ai_error){ps_button(fb,x,y+100,(w-10)/2,28,"重试对手",true);ps_button(fb,x+(w+10)/2,y+100,(w-10)/2,28,"规则",false);}
        else ps_button(fb,x,y+100,w,28,"规则与提示",false);
    }
    if(view.landscape)ps_wrap(fb,x,view.h-51,w,10,2,"自动保存 · 连胜两局可进阶 · 中键返回 · 双击切换主页",6);
    if(g_modal)pg_draw_modal(fb);
}

static bool pg_modal_tap(int tx,int ty)
{
    ps_view_t view=ps_view();int w=pg_min(view.w-40,500),h=g_modal==2?(view.landscape?264:346):212;
    int x=(view.w-w)/2,y=(view.h-h)/2;
    if(!ps_hit(tx,ty,x,y,w,h)){g_modal=0;g_promotion_to=-1;return true;}
    if(g_modal==1){if(ps_hit(tx,ty,x+20,y+h-55,(w-52)/2,35)){g_modal=0;return true;}
        if(ps_hit(tx,ty,x+32+(w-52)/2,y+h-55,(w-52)/2,35)){pg_start_game(g_pos.type);return true;}}
    else if(g_modal==2){if(ps_hit(tx,ty,x+20,y+h-42,w-40,28))g_modal=0;}
    else{int bw=(w-48)/4;const int pieces[]={5,2,4,3};
        for(int i=0;i<4;i++)if(ps_hit(tx,ty,x+18+i*(bw+4),y+86,bw,88)){
            pg_move_t move={(uint8_t)g_selected,(uint8_t)g_promotion_to,pieces[i]};g_modal=0;g_promotion_to=-1;pg_commit(move);break;}}
    return true;
}

#ifndef PICO_GAME_MODULE
static bool pg_lobby_tap(int tx,int ty)
{
    ps_view_t view=ps_view();
    if(g_save.active&&ps_hit(tx,ty,view.w-148,87,132,30)){ps_navigate(PS_PLAYGAME);return true;}
    for(int kind=0;kind<2;kind++){int cols=view.landscape?3:2,w=(view.w-32-(cols-1)*10)/cols,h=view.landscape?92:132,x=16+(kind%cols)*(w+10),y=143+(kind/cols)*(h+10);if(ps_hit(tx,ty,x,y,w,h)){pa_open(kind);return true;}}
    for(int type=0;type<PG_TYPES;type++){int x,y,w,h;pg_lobby_card(type,&x,&y,&w,&h);pg_progress_t *p=&g_save.progress[type];if(!ps_hit(tx,ty,x,y,w,h))continue;g_lobby_selected=type;
        if(ps_hit(tx,ty,view.landscape?x+w-76:x+10,view.landscape?y+5:y+33,view.landscape?68:w-20,26)){p->level=p->level%3+1;p->streak=0;pg_persist();}
        else if(ps_hit(tx,ty,x+8,view.landscape?y+32:y+63,w-16,26)){p->automatic=!p->automatic;p->streak=0;pg_persist();}
        else if(ps_hit(tx,ty,x+10,y+h-36,w-20,32))pg_start_game(type);
        return true;
    }return false;
}

#else
static bool pg_lobby_tap(int tx,int ty)
{
    if(g_modal)return pg_modal_tap(tx,ty);
    int type=PICO_GAME_KIND;if(type>=PG_TYPES)return false;
    ps_view_t view=ps_view();pg_progress_t *p=&g_save.progress[type];
    int x=view.landscape?42:24,w=view.landscape?240:view.w-48,y=view.landscape?88:118;
    int gap=8,bw=(w-2*gap)/3;
    for(int i=0;i<3;i++)if(ps_hit(tx,ty,x+i*(bw+gap),y+32,bw,38)){p->level=i+1;p->streak=0;pg_persist();return true;}
    if(ps_hit(tx,ty,x,y+82,w,38)){p->automatic=!p->automatic;p->streak=0;pg_persist();return true;}
    int bx=view.landscape?324:x,by=view.landscape?94:y+188,bw2=view.landscape?view.w-350:w;
    if(g_save.active&&ps_hit(tx,ty,bx,by,bw2,42)){ps_navigate(PS_PLAYGAME);return true;}
    if(ps_hit(tx,ty,bx,by+54,bw2,42)){if(g_save.active)g_modal=1;else pg_start_game(type);return true;}
    if(ps_hit(tx,ty,bx,by+108,bw2,38)){g_modal=2;return true;}
    return false;
}
#endif

static bool pg_play_tap(int tx,int ty)
{
    ps_view_t view=ps_view();if(g_modal)return pg_modal_tap(tx,ty);
    if(pg_duo_role_now()==PGA_DUO_CONTROLLER){
        if(ps_hit(tx,ty,10,30,28,26)){pg_cancel_task();ps_navigate(PS_GAMES);return true;}
        if(ps_hit(tx,ty,20,486,150,40)){pg_duo_send_action(PG_DUO_UNDO);return true;}
        if(ps_hit(tx,ty,190,486,150,40)){pg_duo_send_action(PG_DUO_RESTART);return true;}
        if(ps_hit(tx,ty,20,540,150,40)){pg_duo_send_action(PG_DUO_RULES);return true;}
        if(ps_hit(tx,ty,190,540,150,40)){pg_cancel_task();ps_navigate(PS_GAMES);return true;}
        return false;
    }
    if(pg_duo_role_now()==PGA_DUO_SCREEN){
        if(ps_hit(tx,ty,10,30,28,26)||ps_hit(tx,ty,ps_view().w-100,32,80,28)){pg_cancel_task();ps_navigate(PS_GAMES);return true;}
        int point=pg_board_hit(tx,ty);if(point<0)return false;
        if(g_pos.turn!=1||g_pos.result)return true;
        if(g_pos.type==PG_GOMOKU||g_pos.type==PG_GO){pg_commit(pg_point(point));return true;}
        if(g_pos.board[point]>0){g_selected=g_selected==point?-1:point;g_feedback[0]=0;return true;}
        if(g_selected<0){snprintf(g_feedback,sizeof(g_feedback),"先选择一枚%s棋子",g_pos.type==PG_CHESS?"白方":"红方");return true;}
        pg_move_t move={(uint8_t)g_selected,(uint8_t)point,0};
        if(g_pos.type==PG_CHESS&&g_pos.board[g_selected]==1&&point<8)move.promotion=5;
        pg_commit(move);return true;
    }
    if(view.landscape){
        if(ps_hit(tx,ty,10,30,28,26)||ps_hit(tx,ty,488,32,120,28)){pg_cancel_task();ps_navigate(PS_GAMES);return true;}
        if(g_pos.result==PG_SCORE){
            if(ps_hit(tx,ty,24,112,112,40)){pg_finish_score();return true;}
            if(ps_hit(tx,ty,24,176,112,40)){pg_continue_go();return true;}
            if(ps_hit(tx,ty,24,240,112,40)){g_modal=2;return true;}
            int point=pg_board_hit(tx,ty);if(point>=0)pg_mark_dead(point);
            return point>=0;
        }
        if(g_pos.type==PG_GO){
            if(ps_hit(tx,ty,24,104,112,40)){pg_undo();return true;}
            if(ps_hit(tx,ty,24,152,112,40)){if(g_pos.turn==1&&!g_pos.result)pg_commit(pg_point(-1));return true;}
            if(ps_hit(tx,ty,24,200,112,40)){g_modal=1;return true;}
            if(ps_hit(tx,ty,24,248,112,40)){g_modal=2;return true;}
        }else{
            if(ps_hit(tx,ty,24,112,112,40)){pg_undo();return true;}
            if(ps_hit(tx,ty,24,176,112,40)){g_modal=1;return true;}
            if(g_ai_error){
                if(ps_hit(tx,ty,24,240,112,34)){g_ai_error=false;g_feedback[0]=0;pg_request_ai();return true;}
                if(ps_hit(tx,ty,24,286,112,30)){g_modal=2;return true;}
            }else if(ps_hit(tx,ty,24,240,112,40)){g_modal=2;return true;}
        }
        int point=pg_board_hit(tx,ty);if(point<0)return false;
        if(g_pos.turn!=1||g_pos.result)return true;
        if(g_pos.type==PG_GOMOKU||g_pos.type==PG_GO){pg_commit(pg_point(point));return true;}
        if(g_pos.board[point]>0){g_selected=g_selected==point?-1:point;g_feedback[0]=0;return true;}
        if(g_selected<0){snprintf(g_feedback,sizeof(g_feedback),"先选择一枚%s棋子",g_pos.type==PG_CHESS?"白方":"红方");return true;}
        pg_move_t move={(uint8_t)g_selected,(uint8_t)point,0};
        if(g_pos.type==PG_CHESS&&g_pos.board[g_selected]==1&&point<8){move.promotion=5;}
        pg_commit(move);return true;
    }
    if(ps_hit(tx,ty,view.w-78,32,62,28)){pg_cancel_task();ps_navigate(PS_GAMES);return true;}
    int x,y,w;pg_side_rect(&x,&y,&w);
    if(g_pos.result==PG_SCORE){
        if(ps_hit(tx,ty,x,y+75,(w-10)/2,34)){pg_finish_score();return true;}
        if(ps_hit(tx,ty,x+(w+10)/2,y+75,(w-10)/2,34)){pg_continue_go();return true;}
        if(ps_hit(tx,ty,x,y+119,w,28)){g_modal=2;return true;}
    }else{
        int n=g_pos.type==PG_GO?3:2,bw=(w-(n-1)*8)/n;
        if(ps_hit(tx,ty,x,y+59,bw,34)){pg_undo();return true;}
        if(n==3&&ps_hit(tx,ty,x+bw+8,y+59,bw,34)){if(g_pos.turn==1&&!g_pos.result)pg_commit(pg_point(-1));return true;}
        if(ps_hit(tx,ty,x+(n-1)*(bw+8),y+59,bw,34)){g_modal=1;return true;}
        if(ps_hit(tx,ty,x,y+100,w,28)){
            if(g_ai_error&&tx<x+(w-10)/2){g_ai_error=false;g_feedback[0]=0;pg_request_ai();}
            else g_modal=2;
            return true;}
    }
    int point=pg_board_hit(tx,ty);if(point<0)return false;
    if(g_pos.result==PG_SCORE){pg_mark_dead(point);return true;}
    if(g_pos.turn!=1||g_pos.result)return true;
    if(g_pos.type==PG_GOMOKU||g_pos.type==PG_GO){pg_commit(pg_point(point));return true;}
    if(g_pos.board[point]>0){g_selected=g_selected==point?-1:point;g_feedback[0]=0;return true;}
    if(g_selected<0){snprintf(g_feedback,sizeof(g_feedback),"先选择一枚%s棋子",g_pos.type==PG_CHESS?"白方":"红方");return true;}
    pg_move_t move={(uint8_t)g_selected,(uint8_t)point,0};
    if(g_pos.type==PG_CHESS&&g_pos.board[g_selected]==1&&point<8){move.promotion=5;
        if(pg_legal_piece(&g_pos,move)){g_promotion_to=point;g_modal=3;return true;}}
    pg_commit(move);return true;
}

bool ps_games_storage_save(void)
{
#ifdef PICO_GAME_MODULE
    if(PICO_GAME_KIND>=PG_TYPES)return pa_persist();
    g_unsaved=!ps_store("games",&g_save,sizeof(g_save));return !g_unsaved;
#else
    g_unsaved=!ps_store("games",&g_save,sizeof(g_save));
    bool arcade=pa_persist();return !g_unsaved&&arcade;
#endif
}

void ps_games_init(void)
{
    atomic_init(&g_epoch,1);atomic_init(&g_job.done,false);
#ifdef PICO_GAME_MODULE
    if(PICO_GAME_KIND>=PG_TYPES){pa_init();pa_open(PICO_GAME_KIND-PG_TYPES);return;}
#else
    pa_init();
#endif
    memset(&g_save,0,sizeof(g_save));bool loaded=ps_load("games",&g_save,sizeof(g_save));
#ifdef PICO_GAME_MODULE
    if(!loaded)loaded=pga_host->legacy_load("games",&g_save,sizeof(g_save));
#endif
    if(!loaded||g_save.magic!=UINT32_C(0x50474334)||g_save.version!=1){
        memset(&g_save,0,sizeof(g_save));g_save.magic=UINT32_C(0x50474334);g_save.version=1;
        for(int i=0;i<PG_TYPES;i++){g_save.progress[i].level=1;g_save.progress[i].automatic=1;}}
    for(int i=0;i<PG_TYPES;i++)if(g_save.progress[i].level<1||g_save.progress[i].level>3){g_save.progress[i].level=1;g_save.progress[i].streak=0;}
#ifdef PICO_GAME_MODULE
    // 只迁入本游戏的局面与成绩，既有共享存档保持只读。/ Migrate only this game's state and results; legacy shared saves stay read-only.
    if(g_save.type!=PICO_GAME_KIND){g_save.active=0;g_save.count=0;g_save.type=PICO_GAME_KIND;g_save.recorded=0;g_save.final_result=0;memset(g_save.log,0,sizeof(g_save.log));memset(g_save.dead,0,sizeof(g_save.dead));}
    for(int i=0;i<PG_TYPES;i++)if(i!=PICO_GAME_KIND){memset(&g_save.progress[i],0,sizeof(g_save.progress[i]));g_save.progress[i].level=1;g_save.progress[i].automatic=1;}
#endif
    if(g_save.active&&!pg_replay(&g_save,&g_pos,g_keys,&g_key_count)){g_save.active=0;ps_toast("棋局存档无法读取，训练成绩已保留");}
#ifdef PICO_GAME_MODULE
    if(!g_save.active){g_save.type=PICO_GAME_KIND;g_save.level=g_save.progress[PICO_GAME_KIND].level;g_save.count=0;g_save.recorded=0;g_save.final_result=0;memset(g_save.log,0,sizeof(g_save.log));memset(g_save.dead,0,sizeof(g_save.dead));pg_initial(&g_pos,PICO_GAME_KIND,g_save.level);}
#endif
    g_lobby_selected=g_save.active?g_pos.type:0;
}

#ifdef PICO_GAME_MODULE
static
#endif
void ps_games_enter(ps_route_t route)
{
    if(route!=PS_ARCADEGAME){if(a_was_active)pa_pause();a_was_active=false;}else a_was_active=true;
    if(route!=PS_PLAYGAME){pg_cancel_task();g_modal=0;g_selected=-1;}
    // 棋类包进入即开局：主机直接显示棋盘，副机等待主机的吃子状态。
    // Entering a board package starts immediately on the primary; the secondary waits for capture state.
    if(route==PS_PLAYGAME&&!g_save.active&&pg_duo_role_now()!=PGA_DUO_CONTROLLER){
#ifdef PICO_GAME_MODULE
        pg_start_game(PICO_GAME_KIND);
#else
        pg_start_game(g_save.type<PG_TYPES?g_save.type:0);
#endif
    }
    if(route==PS_PLAYGAME&&g_save.active){g_ai_error=false;pg_request_ai();}
}

void ps_games_render(uint8_t *fb)
{
    if(ps_route()==PS_ARCADEGAME)pa_draw(fb);
    else if(ps_route()==PS_PLAYGAME&&(g_save.active||pg_duo_role_now()==PGA_DUO_CONTROLLER))pg_draw_play(fb);
    else pg_draw_lobby(fb);
}

bool ps_games_event(const ps_event_t *event)
{
    if(ps_route()==PS_ARCADEGAME){bool handled=pa_event(event);if(handled)ps_dirty();return handled;}
    if(ps_route()!=PS_GAMES&&ps_route()!=PS_PLAYGAME)return false;
    if(event->type!=PS_TAP)return g_modal!=0;
    bool controller=pg_duo_role_now()==PGA_DUO_CONTROLLER;
    bool lobby=ps_route()==PS_GAMES||(!g_save.active&&!controller);
    bool handled=lobby?pg_lobby_tap(event->x,event->y):pg_play_tap(event->x,event->y);
    if(handled)ps_dirty();
    return handled;
}

bool ps_games_key(int key)
{
    if(ps_route()==PS_ARCADEGAME){if(key==-1){pa_pause();return false;}if(key==0){if(a_kind==PA_2048)pa_shift(3);else pa_direction(3);ps_dirty();return true;}if(key==2){if(a_kind==PA_2048)pa_shift(1);else pa_direction(1);ps_dirty();return true;}return false;}
    if(ps_route()!=PS_GAMES&&ps_route()!=PS_PLAYGAME)return false;
    if(key==-1){if(g_modal){g_modal=0;g_promotion_to=-1;ps_dirty();return true;}
        if(g_selected>=0){g_selected=-1;ps_dirty();return true;}return false;}
    if(ps_route()==PS_PLAYGAME&&key==0){pg_undo();return true;}
#ifndef PICO_GAME_MODULE
    if(ps_route()==PS_GAMES&&key==0){g_lobby_selected=(g_lobby_selected+1)%PG_TYPES;ps_dirty();return true;}
#endif
    return false;
}

bool ps_games_tick(void)
{
    bool changed=false;
    if(ps_route()==PS_PLAYGAME)pg_duo_poll();
    if(g_busy&&atomic_load_explicit(&g_job.done,memory_order_acquire)){
        // WithCaps外部删除等待工作任务停止后才回收其TCB和栈。/ External WithCaps deletion stops the worker before reclaiming its TCB and stack.
        vTaskDeleteWithCaps(g_task);g_task=NULL;
        pg_task_diagnostic(g_job.generation==atomic_load_explicit(&g_epoch,memory_order_relaxed)?"completed":"cancelled",
            g_job.pos.type,g_job.generation,g_job.stack_free);
        g_busy=false;
        if(g_job.generation==atomic_load_explicit(&g_epoch,memory_order_relaxed)&&ps_route()==PS_PLAYGAME&&g_pos.moves==g_job.pos.moves&&g_pos.turn==-1&&!g_pos.result){
            if(g_job.success){if(!pg_commit(g_job.answer)){g_ai_error=true;snprintf(g_feedback,sizeof(g_feedback),"对手无法完成落子，请点击重试");}}
            else{g_ai_error=true;snprintf(g_feedback,sizeof(g_feedback),"对手计算中断，请点击重试");}changed=true;}
    }
    if(ps_route()==PS_ARCADEGAME){pa_poll_duo();if(a_kind==PA_SNAKE&&!a_paused&&!a_save.snake_over&&pg_millis()>=a_due){a_due=pg_millis()+pa_delay();return pa_step();}return changed;}
    if(ps_route()==PS_PLAYGAME&&g_save.active&&g_pos.turn==-1&&!g_pos.result&&!g_busy&&!g_ai_error){pg_request_ai();changed=true;}
    return changed;
}

#endif

#ifdef PICO_GAME_MODULE
/* ---- ABI入口：模块内页面与可卸载任务生命周期 / ABI: module leaf views and unload-safe workers ---- */
static bool pga_started;
static void ps_navigate(ps_route_t route)
{
    if(PICO_GAME_KIND>=PG_TYPES&&route==PS_GAMES){pga_host->navigate(PGA_PAGE_LAUNCHER);return;}
    if((PICO_GAME_KIND<PG_TYPES&&route!=PS_GAMES&&route!=PS_PLAYGAME)||
       (PICO_GAME_KIND>=PG_TYPES&&route!=PS_ARCADEGAME))return;
    pga_route=route;ps_games_enter(route);ps_dirty();
}

static bool pga_start(const pico_app_host_t *host)
{
    if(!host||host->abi!=PICO_APP_ABI||host->bytes<sizeof(*host)||!host->view||!host->page||
       !host->navigate||!host->dirty||!host->toast||!host->load||!host->store||!host->legacy_load||
       !host->time_us||!host->random||!host->fill||!host->line||!host->box||!host->text||
       !host->center||!host->wrap||!host->button||!host->heading||!host->worker_create||
       !host->worker_delete||!host->worker_suspend||!host->worker_stack_free||!host->delay_ms||
       !host->diagnostic||!host->format||!host->copy||!host->move||!host->set||!host->compare)return false;
    if(pga_started)return host==pga_host;
    pga_host=host;pga_started=true;pga_route=PS_GAMES;ps_games_init();
    return true;
}

static bool pga_event(const pico_app_event_t *event)
{
    if(!pga_started||!event||event->type<PGA_PRESS||event->type>PGA_CANCEL)return false;
    return ps_games_event(event);
}
static uint32_t pga_status(void)
{
    uint32_t status=0;
    if(PICO_GAME_KIND>=PG_TYPES){status=PGA_STATUS_ARCADE;if(PICO_GAME_KIND==PGA_SNAKE){status|=PGA_STATUS_SNAKE;if(a_paused)status|=PGA_STATUS_PAUSED;}if(a_unsaved)status|=PGA_STATUS_UNSAVED;}
    else{if(g_busy)status|=PGA_STATUS_BUSY;if(g_unsaved)status|=PGA_STATUS_UNSAVED;}
    return status;
}
static void pga_stop(void)
{
    if(!pga_started)return;
    // 先使搜索失效，再等待完成与配对释放；保留静态局面供保存失败时钉住模块。
    // Invalidate, join and pair-delete before unload; keep state intact so failed saves can pin the module.
    pg_cancel_task();
    while(g_busy&&!atomic_load_explicit(&g_job.done,memory_order_acquire))pga_host->delay_ms(1);
    if(g_busy){pga_host->worker_delete(g_task);g_task=NULL;g_busy=false;}
    g_ai_error=false;
    if(PICO_GAME_KIND==PGA_SNAKE)pa_pause();
}

static const pico_app_exports_t pga_exports={
    .abi=PICO_APP_ABI,.bytes=sizeof(pico_app_exports_t),.version=10001,.kind=PICO_GAME_KIND,.id=PICO_GAME_ID,
    .start=pga_start,.render=ps_games_render,.event=pga_event,.key=ps_games_key,.tick=ps_games_tick,
    .action=ps_games_action,.status=pga_status,.save=ps_games_storage_save,.stop=pga_stop
};
__attribute__((visibility("default"))) const pico_app_exports_t *pico_app_query(void){return &pga_exports;}
#endif

/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 * 中文：在主机验证与设备共用的四棋规则、AI 和日志重放。
 * English: Host validation of the exact device rules, AI and move-log replay.
 */
#define PICO_GAMES_CORE_ONLY
#include "../games/src/builtin_games.c"
#include <assert.h>

static uint64_t test_keys[PG_LOG_MAX+3];
static int test_count;

static void start(pg_pos_t *s,int type)
{
    pg_initial(s,type,1);test_keys[0]=pg_key(s);test_count=1;
}

static bool play(pg_pos_t *s,pg_move_t m)
{
    pg_pos_t n;pg_history_t history={test_keys,test_count};
    if(!pg_apply(s,m,&n,&history,true))return false;*s=n;
    if(s->type!=PG_GO||m.to!=PG_POINT)test_keys[test_count++]=pg_key(s);return true;
}

static uint64_t perft(const pg_pos_t *s,int depth)
{
    if(!depth)return 1;pg_move_t moves[PG_MOVE_MAX];int n=pg_moves(s,moves,PG_MOVE_MAX);uint64_t total=0;
    for(int i=0;i<n;i++){pg_pos_t next;pg_raw(s,moves[i],&next);total+=perft(&next,depth-1);}return total;
}

static void test_chess(void)
{
    pg_pos_t s,n;start(&s,PG_CHESS);
    assert(perft(&s,1)==20);assert(perft(&s,2)==400);assert(perft(&s,3)==8902);
    const pg_move_t fools[]={{53,45,0},{12,28,0},{54,38,0},{3,39,0}};
    for(unsigned i=0;i<sizeof(fools)/sizeof(*fools);i++)assert(play(&s,fools[i]));assert(s.result==-1&&s.reason==PG_MATE);
    start(&s,PG_CHESS);memset(s.board,0,sizeof(s.board));s.board[60]=6;s.board[63]=2;s.board[4]=-6;
    assert(pg_legal_piece(&s,(pg_move_t){60,62,0}));assert(pg_apply(&s,(pg_move_t){60,62,0},&n,NULL,true));
    assert(n.board[61]==2&&n.board[62]==6&&!(n.castle&3));s.board[5]=-2;assert(!pg_legal_piece(&s,(pg_move_t){60,62,0}));
    start(&s,PG_CHESS);memset(s.board,0,sizeof(s.board));s.board[24]=6;s.board[0]=-6;s.board[28]=1;s.board[27]=-1;s.board[31]=-2;s.ep=19;
    assert(!pg_legal_piece(&s,(pg_move_t){28,19,0}));uint64_t pinned_key=pg_key(&s);s.ep=-1;assert(pg_key(&s)==pinned_key);s.ep=19;
    s.board[31]=0;assert(pg_legal_piece(&s,(pg_move_t){28,19,0}));assert(pg_apply(&s,(pg_move_t){28,19,0},&n,NULL,true));assert(!n.board[27]);
    start(&s,PG_CHESS);memset(s.board,0,sizeof(s.board));s.board[60]=6;s.board[7]=-6;s.board[8]=1;
    for(int p=2;p<=5;p++){assert(pg_apply(&s,(pg_move_t){8,0,p},&n,NULL,true));assert(n.board[0]==p);}assert(!pg_legal_piece(&s,(pg_move_t){8,0,6}));
    start(&s,PG_CHESS);memset(s.board,0,sizeof(s.board));s.board[18]=6;s.board[0]=-6;s.board[17]=5;
    assert(pg_apply(&s,(pg_move_t){17,10,0},&n,NULL,true));assert(n.result==PG_DRAW&&n.reason==PG_STALEMATE);
    start(&s,PG_CHESS);const pg_move_t cycle[]={{57,42,0},{1,18,0},{42,57,0},{18,1,0}};
    for(int j=0;j<2;j++)for(int i=0;i<4;i++)assert(play(&s,cycle[i]));assert(s.result==PG_DRAW&&s.reason==PG_REPEAT);
    start(&s,PG_CHESS);s.quiet=99;assert(play(&s,(pg_move_t){57,42,0}));assert(s.result==PG_DRAW&&s.reason==PG_QUIET);
    start(&s,PG_CHESS);memset(s.board,0,sizeof(s.board));s.board[4]=-6;s.board[60]=6;assert(pg_insufficient(&s));
    s.board[40]=3;assert(pg_insufficient(&s));s.board[41]=-3;assert(!pg_insufficient(&s));
    puts("chess: perft 20/400/8902, mate/stalemate, castle, pinned EP, four promotions, repetition, fifty moves");
}

static void test_go(void)
{
    pg_pos_t s,n;start(&s,PG_GO);int black[]={1,9,11},white[]={10,18,20,28};
    for(int i=0;i<3;i++)s.board[black[i]]=1;for(int i=0;i<4;i++)s.board[white[i]]=-1;
    test_keys[0]=pg_key(&s);assert(play(&s,pg_point(19)));assert(!s.board[10]&&s.captures==1);assert(!play(&s,pg_point(10)));
    start(&s,PG_GO);s.board[31]=s.board[39]=s.board[41]=s.board[49]=-1;assert(!pg_apply(&s,pg_point(40),&n,NULL,true));
    start(&s,PG_GO);assert(play(&s,pg_point(-1)));assert(play(&s,pg_point(-1)));assert(s.result==PG_SCORE);
    int8_t board[225]={0};pg_score_t score=pg_go_score(board);assert(score.black2==0&&score.white2==13&&score.winner==-1);
    for(int i=0;i<81;i++)board[i]=1;score=pg_go_score(board);assert(score.black2==162&&score.white2==13&&score.winner==1);
    puts("go: capture, superko, suicide, pass/score stage, area and 6.5 komi");
}

static void test_xiangqi(void)
{
    pg_pos_t s;start(&s,PG_XIANGQI);int pieces=0;for(int i=0;i<90;i++)pieces+=!!s.board[i];assert(pieces==32);assert(!pg_check(&s,1));
    assert(pg_legal_piece(&s,(pg_move_t){82,63,0}));s.board[73]=1;assert(!pg_legal_piece(&s,(pg_move_t){82,63,0}));
    start(&s,PG_XIANGQI);assert(pg_legal_piece(&s,(pg_move_t){64,1,0}));assert(!pg_legal_piece(&s,(pg_move_t){81,1,0}));
    assert(pg_legal_piece(&s,(pg_move_t){83,63,0}));s.board[73]=1;assert(!pg_legal_piece(&s,(pg_move_t){83,63,0}));assert(!pg_legal_piece(&s,(pg_move_t){54,55,0}));
    memset(s.board,0,sizeof(s.board));s.board[4]=-6;s.board[85]=6;s.board[49]=2;
    assert(!pg_legal_piece(&s,(pg_move_t){49,50,0}));assert(!pg_check(&s,1));s.board[49]=0;assert(pg_check(&s,1));
    puts("xiangqi: 32 pieces, horse leg, cannon screen, elephant eye, river, facing generals");
}

static void test_gomoku(void)
{
    pg_pos_t s,n;start(&s,PG_GOMOKU);for(int i=0;i<4;i++){s.board[105+i]=-1;s.board[135+i]=1;}s.moves=8;s.turn=-1;s.level=3;
    pg_search_t c={.deadline=pg_millis()+10000,.limit=20000,.rng=0,.keys=test_keys,.key_count=test_count};pg_move_t m;
    assert(pg_ai(&s,&c,&m));assert(m.to==104||m.to==109);s.board[104]=1;s.level=2;assert(pg_ai(&s,&c,&m));assert(m.to==109);
    start(&s,PG_GOMOKU);for(int i=105;i<=108;i++)s.board[i]=1;s.turn=-1;s.moves=4;assert(pg_ai(&s,&c,&m));assert(m.to==104||m.to==109);
    s.turn=1;assert(pg_apply(&s,pg_point(109),&n,NULL,true));assert(n.result==1);assert(!pg_apply(&s,pg_point(105),&n,NULL,true));
    puts("gomoku: five, occupied rejection, immediate win before forced block");
}

static void test_ai_and_records(void)
{
    pg_pos_t s,n;
    for(int type=0;type<PG_TYPES;type++)for(int level=1;level<=3;level++){
        start(&s,type);s.level=level;
        for(int turn=0;turn<8&&!s.result;turn++){
            pg_search_t search={.deadline=pg_millis()+1000,.limit=level==1?1000:level==2?7000:20000,.rng=(unsigned)turn,.keys=test_keys,.key_count=test_count};
            pg_move_t m;assert(pg_ai(&s,&search,&m));assert(play(&s,m));
        }
    }
    atomic_uint epoch;atomic_init(&epoch,2);start(&s,PG_CHESS);
    pg_search_t cancelled={.deadline=pg_millis()+1000,.limit=100,.epoch=&epoch,.generation=1};pg_move_t no_move;assert(!pg_ai(&s,&cancelled,&no_move));
    pg_progress_t p={.level=1,.automatic=1};assert(!pg_advance(&p,1,1));assert(pg_advance(&p,1,1));assert(p.level==2&&p.wins==2);
    pg_advance(&p,2,1);pg_advance(&p,2,PG_DRAW);assert(!p.streak);p.automatic=0;pg_advance(&p,2,1);assert(!pg_advance(&p,2,1)&&p.level==2);
    pg_save_t save={.magic=UINT32_C(0x50474334),.version=1,.active=1,.type=PG_CHESS,.level=2};
    const pg_move_t moves[]={{52,36,0},{12,28,0},{62,45,0},{1,18,0}};
    start(&s,PG_CHESS);s.level=2;for(int i=0;i<4;i++){assert(play(&s,moves[i]));save.log[save.count++]=moves[i];}
    uint64_t keys[PG_LOG_MAX+3];int count=0;assert(pg_replay(&save,&n,keys,&count));assert(!memcmp(&s,&n,sizeof(s)));assert(count==test_count);
    for(int i=0;i<count;i++)assert(keys[i]==test_keys[i]);
    save.count=3;assert(pg_replay(&save,&n,keys,&count));assert(n.turn==-1&&n.moves==3);
    save.type=PG_GO;save.level=1;save.count=4;save.log[0]=pg_point(-1);save.log[1]=pg_point(-1);save.log[2]=(pg_move_t){PG_RESUME,0,0};save.log[3]=pg_point(40);
    assert(pg_replay(&save,&n,keys,&count));assert(n.turn==-1&&n.moves==3&&n.board[40]==1&&!n.passes);
    save.count=PG_LOG_MAX+1;assert(!pg_replay(&save,&n,keys,&count));
    puts("all types: legal AI at 3 levels, cancellation, independent progress, exact save/replay and undo prefix");
}

int main(void)
{
    test_chess();test_go();test_xiangqi();test_gomoku();test_ai_and_records();puts("pico games host validation passed");return 0;
}

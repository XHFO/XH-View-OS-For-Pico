/* SPDX-License-Identifier: Apache-2.0
 * 中文：单机墨水屏方块和原创迷宫；有界步进，无补帧，锁屏/退出保存并暂停。
 * English: Single-device e-paper blocks and original maze; bounded steps, no catch-up, save/pause on lock/exit.
 */
#include "pico_app_sdk.h"
#include "../sdk/pico_game_pad.h"
static const pico_app_host_t* H;
#ifdef PICO_GAME_MODULE
// 编译器生成的结构复制同样经宿主执行。/ Compiler-generated copies use the same host boundary.
__attribute__((visibility("hidden"))) void* memcpy(void* d, const void* s, size_t n) {
    return H->copy(d, s, n);
}
__attribute__((visibility("hidden"))) void* memset(void* d, int c, size_t n) {
    return H->set(d, c, n);
}
#endif
typedef struct {
    uint32_t magic, version, score, best, lines;
    uint8_t cells[289], bag[7], bag_at, piece, rotation, next;
    int8_t x, y, dx, dy, gx[3], gy[3];
    uint8_t lives, over, power, paused;
} state_t;
static state_t S;
static bool dirty, contact_handled, swipe_tracking;
static int64_t deadline,status_at;
static int last_role;
static bool controller,options,confirm_new;
static pga_pad_state_t remote,sent;
static void action(int a);
static int abs_i(int n) {
    return n < 0 ? -n : n;
}
static const uint16_t shapes[7][4] = {
    { 0xf0, 0x4444, 0xf00, 0x2222 }, { 0x66, 0x66, 0x66, 0x66 },    { 0x72, 0x262, 0x270, 0x232 },
    { 0x36, 0x462, 0x360, 0x231 },   { 0x63, 0x264, 0x630, 0x132 }, { 0x71, 0x226, 0x470, 0x322 },
    { 0x74, 0x622, 0x170, 0x223 }
};
static bool fits(int x, int y, int rot) {
    uint16_t b = shapes[S.piece][rot];
    for (int i = 0; i < 16; i++)
        if (b & (1u << i)) {
            int xx = x + i % 4, yy = y + i / 4;
            if (xx < 0 || xx >= 10 || yy >= 20 || (yy >= 0 && S.cells[yy * 10 + xx])) return false;
        }
    return true;
}
static unsigned bag_take(void) {
    if (S.bag_at >= 7) {
        for (unsigned i = 0; i < 7; i++) S.bag[i] = i;
        for (unsigned i = 6; i; i--) {
            unsigned j = H->random() % (i + 1), v = S.bag[i];
            S.bag[i] = S.bag[j];
            S.bag[j] = v;
        }
        S.bag_at = 0;
    }
    return S.bag[S.bag_at++];
}
static void spawn(void) {
    S.piece = S.next;
    S.next = bag_take();
    S.rotation = 0;
    S.x = 3;
    S.y = 0;
    if (!fits(S.x, S.y, 0)) S.over = 1;
}
static void freeze(void) {
    uint16_t b = shapes[S.piece][S.rotation];
    for (int i = 0; i < 16; i++)
        if (b & (1u << i)) {
            int y = S.y + i / 4;
            if (y < 0) {
                S.over = 1;
                return;
            }
            S.cells[y * 10 + S.x + i % 4] = S.piece + 1;
        }
    unsigned cleared = 0;
    for (int y = 19; y >= 0; y--) {
        bool full = true;
        for (int x = 0; x < 10; x++)
            if (!S.cells[y * 10 + x]) full = false;
        if (full) {
            for (int k = y; k > 0; k--) H->copy(S.cells + k * 10, S.cells + (k - 1) * 10, 10);
            H->set(S.cells, 0, 10);
            cleared++;
            y++;
        }
    }
    static const unsigned pts[] = { 0, 100, 300, 500, 800 };
    S.score += pts[cleared] * (1 + S.lines / 10);
    S.lines += cleared;
    spawn();
}
static void block_move(int a) {
    if (a == 0 || a == 2) {
        int x = S.x + (a == 0 ? -1 : 1);
        if (fits(x, S.y, S.rotation)) S.x = x;
    } else if (a == 1) {
        int rot = (S.rotation + 1) % 4;
        const int k[] = { 0, -1, 1, -2, 2 };
        for (int i = 0; i < 5; i++)
            if (fits(S.x + k[i], S.y, rot)) {
                S.x += k[i];
                S.rotation = rot;
                break;
            }
    } else if (a == 3) {
        if (fits(S.x, S.y + 1, S.rotation))
            S.y++;
        else
            freeze();
    } else if (a == 4) {
        while (fits(S.x, S.y + 1, S.rotation)) {
            S.y++;
            S.score += 2;
        }
        freeze();
    }
}
// 原创迷宫，不包含商业游戏素材。/ Original maze without commercial game assets.
static const char maze[17][18] = { "#################", "#.......#.......#", "#.###.#.#.#.###.#",
                                   "#o....#...#....o#", "#.###.#####.###.#", "#...#.......#...#",
                                   "###.#.##.##.#.###", "#...#.......#...#", "#.###.#...#.###.#",
                                   "#.....#...#.....#", "#.###.##.##.###.#", "#...#.......#...#",
                                   "###.#.#####.#.###", "#o..#...#...#..o#", "#.#.###.#.###.#.#",
                                   "#...............#", "#################" };
static bool walk(int x, int y) {
    return x >= 0 && x < 17 && y >= 0 && y < 17 && maze[y][x] != '#';
}
static void actors(void) {
    S.x = 1;
    S.y = 1;
    S.dx = 1;
    S.dy = 0;
    for (int i = 0; i < 3; i++) {
        S.gx[i] = 7 + i;
        S.gy[i] = 8;
    }
    S.power = 0;
}
static void pellets(void) {
    for (int i = 0; i < 289; i++)
        S.cells[i] = maze[i / 17][i % 17] == '#' ? 0 : maze[i / 17][i % 17] == 'o' ? 2 : 1;
    S.cells[18] = 0;
}
static void reset(void) {
    uint32_t best = S.best;
    H->set(&S, 0, sizeof(S));
    S.magic = 0x494e4b36 + PICO_GAME_KIND;
    S.version = 1;
    S.best = best;
    S.paused = 1;
    if (PICO_GAME_KIND == 6) {
        S.bag_at = 7;
        S.next = bag_take();
        spawn();
    } else {
        S.lives = 3;
        pellets();
        actors();
    }
    dirty = true;
    deadline = 0;
}
static void collision(void) {
    for (int i = 0; i < 3; i++)
        if (S.x == S.gx[i] && S.y == S.gy[i]) {
            if (S.power) {
                S.score += 100;
                S.gx[i] = 8;
                S.gy[i] = 8;
            } else {
                if (S.lives) S.lives--;
                if (!S.lives)
                    S.over = 1;
                else
                    actors();
                S.paused = 1;
                break;
            }
        }
}
static void maze_step(void) {
    static const int vx[] = { -1, 0, 1, 0 }, vy[] = { 0, -1, 0, 1 };
    if (walk(S.x + S.dx, S.y + S.dy)) {
        S.x += S.dx;
        S.y += S.dy;
    }
    int at = S.y * 17 + S.x;
    if (S.cells[at]) {
        S.score += S.cells[at] == 2 ? 50 : 10;
        if (S.cells[at] == 2) S.power = 24;
        S.cells[at] = 0;
    }
    collision();
    if (S.over || S.paused) return;
    if (S.power) S.power--;
    for (int i = 0; i < 3; i++) {
        int choice = -1, dist = S.power ? -1 : 999;
        for (int j = 0; j < 4; j++)
            if (walk(S.gx[i] + vx[j], S.gy[i] + vy[j])) {
                int d = abs_i(S.x - S.gx[i] - vx[j]) + abs_i(S.y - S.gy[i] - vy[j]);
                if (choice < 0 || (S.power ? d > dist : d < dist)) {
                    choice = j;
                    dist = d;
                }
            }
        if (choice >= 0) {
            S.gx[i] += vx[choice];
            S.gy[i] += vy[choice];
        }
    }
    collision();
    bool remaining = false;
    for (int i = 0; i < 289; i++)
        if (S.cells[i]) remaining = true;
    if (!remaining) {
        S.lines++;
        pellets();
        actors();
        S.paused = 1;
    }
}
static bool valid(void) {
    if (S.magic != 0x494e4b36u + PICO_GAME_KIND || S.version != 1 || S.paused > 1 || S.over > 1)
        return false;
    if (PICO_GAME_KIND == 6) {
        if (S.piece > 6 || S.next > 6 || S.rotation > 3 || S.bag_at > 7 || S.x < -3 || S.x > 9 ||
            S.y < -3 || S.y > 19)
            return false;
        for (int i = 0; i < 7; i++)
            if (S.bag[i] > 6) return false;
        for (int i = 0; i < 200; i++)
            if (S.cells[i] > 7) return false;
        if (!S.over && !fits(S.x, S.y, S.rotation)) return false;
    } else {
        if (!walk(S.x, S.y) || S.lives > 3 || S.power > 24 || abs_i(S.dx) + abs_i(S.dy) != 1)
            return false;
        for (int i = 0; i < 289; i++)
            if (S.cells[i] > 2 || (!walk(i % 17, i / 17) && S.cells[i])) return false;
        for (int i = 0; i < 3; i++)
            if (!walk(S.gx[i], S.gy[i])) return false;
    }
    return true;
}
static bool start(const pico_app_host_t* host) {
    if (!host || host->abi != 1 || host->bytes < PICO_APP_HOST_V1_BYTES) return false;
    H = host;
    H->set(&S, 0, sizeof(S));
    if (!H->load("state1", &S, sizeof(S)) || !valid()) {
        S.best = 0;
        reset();
    }
    S.paused = 1;
    dirty = false;
    deadline = 0;
    contact_handled = swipe_tracking = false;
    controller=pad_role(H)==PGA_DUO_CONTROLLER;last_role=pad_role(H);options=confirm_new=false;
    H->set(&remote,0,sizeof(remote));H->set(&sent,0,sizeof(sent));remote.kind=PICO_GAME_KIND;remote.paused=1;status_at=0;
    return true;
}
static bool save(void) {
    if (S.score > S.best) S.best = S.score;
    if (!H->store("state1", &S, sizeof(S))) return false;
    dirty = false;
    return true;
}
static bool control(int code){
    if(options){
        if(code==PAD_B||code==PGA_DUO_PAUSE){options=confirm_new=false;return true;}
        if(code==PAD_A){if(!confirm_new){confirm_new=true;return true;}reset();options=confirm_new=false;return true;}
        return false;
    }
    if(code<4){action(code==0?1:code==1?2:code==2?3:0);return true;}
    if(code==PGA_DUO_PAUSE){action(5);return true;}
    if(code==PAD_OPTIONS||(PICO_GAME_KIND==7&&(code==PAD_A||code==PAD_B))){S.paused=1;options=true;confirm_new=false;return true;}
    if(PICO_GAME_KIND==6&&code==PAD_A){action(1);return true;}
    if(PICO_GAME_KIND==6&&code==PAD_B){action(4);return true;}
    return false;
}
static bool dual_tick(void){
    int role=pad_role(H);bool changed=false;
    if(role==PGA_DUO_CONTROLLER)controller=true;
    if(controller){bool role_changed=last_role!=role;last_role=role;return pad_receive(H,&remote,PICO_GAME_KIND)||role_changed;}
    if(last_role==PGA_DUO_SCREEN&&role==PGA_DUO_NONE){S.paused=1;dirty=true;changed=true;}
    last_role=role;
    if(role!=PGA_DUO_SCREEN)return changed;
    if(H->bytes>=offsetof(pico_app_host_t,duo_input_recv)+sizeof(H->duo_input_recv)&&H->duo_input_recv)
        for(int i=0;i<4;i++){int code=H->duo_input_recv();if(code<0)break;if(control(code))changed=true;}
    pga_pad_state_t next={.kind=PICO_GAME_KIND,.paused=S.paused,.over=S.over,.setup=options,.confirm=confirm_new,.score=S.score,.best=S.best>S.score?S.best:S.score};
    int64_t now=H->time_us()/1000;bool urgent=next.paused!=sent.paused||next.setup!=sent.setup||next.confirm!=sent.confirm;
    if((urgent||now>=status_at)&&H->compare(&next,&sent,sizeof(next))&&pad_publish(H,&next)){sent=next;status_at=now+750;}
    return changed;
}
static bool tick(void) {
    bool changed=dual_tick();
    if (controller || options || S.paused || S.over) return changed;
    int64_t now = H->time_us() / 1000;
    if (now < deadline) return changed;
    unsigned speed = PICO_GAME_KIND == 6 ? 850 - (S.lines / 10 > 5 ? 5 : S.lines / 10) * 80 : 480;
    deadline = now + speed;
    if (PICO_GAME_KIND == 6)
        block_move(3);
    else
        maze_step();
    dirty = true;
    return true;
}
static void action(int a) {
    if (a == 6) {
        reset();
        return;
    }
    if (a == 5) {
        if (!S.over) {
            S.paused = !S.paused;
            deadline = H->time_us() / 1000 + 500;
            dirty = true;
        }
        return;
    }
    if (S.over || S.paused) return;
    if (PICO_GAME_KIND == 6)
        block_move(a);
    else if (a >= 0 && a < 4) {
        static const int dx[] = { -1, 0, 1, 0 }, dy[] = { 0, -1, 0, 1 };
        if (walk(S.x + dx[a], S.y + dy[a])) {
            bool turned = S.dx != dx[a] || S.dy != dy[a];
            S.dx = dx[a];
            S.dy = dy[a];
            // 转向提前下一次完整步进，不额外推进追逐者或能量计时。
            // Bring the next complete step forward; never run extra enemy or power steps.
            if (turned) deadline = H->time_us() / 1000;
        }
    }
    dirty = true;
}
static void geometry(int* x, int* y, int* c) {
    pico_app_view_t v = H->view();
    int cols = PICO_GAME_KIND == 6 ? 10 : 17, rows = PICO_GAME_KIND == 6 ? 20 : 17;
    int a = (v.w - 40) / cols, b = (v.h - 228) / rows;
    *c = a < b ? a : b;
    if (*c < 4) *c = 4;
    *x = (v.w - cols * *c) / 2;
    *y = 96;
}
static void render(uint8_t* fb) {
    pico_app_view_t v = H->view();
    if(controller){pad_draw(H,fb,PICO_GAME_KIND==6?"俄罗斯方块 · 手柄":"吃豆人 · 手柄",&remote,pad_role(H)==PGA_DUO_CONTROLLER);return;}
    H->fill(fb, 0, 0, v.w, v.h, 15);
    H->heading(fb, PICO_GAME_KIND == 6 ? "俄罗斯方块" : "吃豆人 · 墨水迷宫", NULL);
    H->button(fb,v.w-80,30,64,36,"设置",false);
    if(options){
        H->center(fb,v.w/2,v.landscape?110:180,18,confirm_new?"新局将替换当前对局":"游戏设置",0);
        H->button(fb,16,v.h-160,v.w-32,48,"继续当前游戏",false);
        H->button(fb,16,v.h-96,v.w-32,48,confirm_new?"确认开始新局":"开始新局",true);
        return;
    }
    char text[96];
    H->format(
        text, sizeof(text), "得分 %u · 最高 %u · %s", (unsigned)S.score,
        (unsigned)(S.best > S.score ? S.best : S.score),
        S.over     ? "游戏结束"
        : S.paused ? "已暂停"
                   : "进行中");
    H->center(fb, v.w / 2, 72, 12, text, 0);
    int x, y, c;
    geometry(&x, &y, &c);
    if (PICO_GAME_KIND == 6) {
        H->box(fb, x - 2, y - 2, c * 10 + 4, c * 20 + 4, 2, 0, false);
        for (int yy = 0; yy < 20; yy++)
            for (int xx = 0; xx < 10; xx++)
                if (S.cells[yy * 10 + xx])
                    H->box(
                        fb, x + xx * c + 1, y + yy * c + 1, c - 2, c - 2, 1,
                        ((S.cells[yy * 10 + xx] - 1) % 4) * 2, true);
        if (!S.over) {
            int gy = S.y;
            while (fits(S.x, gy + 1, S.rotation)) gy++;
            uint16_t b = shapes[S.piece][S.rotation];
            for (int i = 0; i < 16; i++)
                if (b & (1u << i)) {
                    int xx = x + (S.x + i % 4) * c;
                    if (gy + i / 4 >= 0)
                        H->box(fb, xx + 1, y + (gy + i / 4) * c + 1, c - 2, c - 2, 1, 7, false);
                    if (S.y + i / 4 >= 0)
                        H->box(
                            fb, xx + 1, y + (S.y + i / 4) * c + 1, c - 2, c - 2, 1, (S.piece % 4) * 2,
                            true);
                }
        }
        H->format(text, sizeof(text), "消行 %u · 下一个", (unsigned)S.lines);
        H->text(fb, 16, v.h - 122, 11, text, 4);
        uint16_t b = shapes[S.next][0];
        for (int i = 0; i < 16; i++)
            if (b & (1u << i))
                H->fill(fb, v.w - 80 + (i % 4) * 10, v.h - 126 + (i / 4) * 10, 8, 8, 2);
    } else {
        for (int yy = 0; yy < 17; yy++)
            for (int xx = 0; xx < 17; xx++) {
                if (maze[yy][xx] == '#')
                    H->box(fb, x + xx * c, y + yy * c, c, c, 2, 4, true);
                else if (S.cells[yy * 17 + xx]) {
                    int s = S.cells[yy * 17 + xx] == 2 ? c / 2 : 3;
                    H->box(
                        fb, x + xx * c + (c - s) / 2, y + yy * c + (c - s) / 2, s, s, s / 2, 0,
                        true);
                }
            }
        H->box(fb, x + S.x * c + 1, y + S.y * c + 1, c - 2, c - 2, c / 2, 0, true);
        H->fill(fb, x + S.x * c + c / 2, y + S.y * c + c / 2, c / 2, 2, 15);
        for (int i = 0; i < 3; i++) {
            H->box(
                fb, x + S.gx[i] * c + 1, y + S.gy[i] * c + 1, c - 2, c - 2, c / 3, S.power ? 6 : 1,
                true);
            H->fill(fb, x + S.gx[i] * c + c / 3, y + S.gy[i] * c + c / 3, 2, 2, 15);
        }
        H->format(
            text, sizeof(text), "生命 %u · 关卡 %u · %s", S.lives, (unsigned)S.lines + 1,
            S.power ? "能量生效" : "滑动转向");
        H->center(fb, v.w / 2, v.h - 120, 12, text, 4);
    }
    int gap = 6, w = (v.w - 62) / 6, yb = v.h - 88;
    const char* labels[] = {
        "←", PICO_GAME_KIND == 6 ? "旋转" : "↑",    "→",
        "↓", PICO_GAME_KIND == 6 ? "落下" : "重开", S.paused ? "开始" : "暂停"
    };
    for (int i = 0; i < 6; i++) H->button(fb, 16 + i * (w + gap), yb, w, 32, labels[i], false);
}
static bool event(const pico_app_event_t* e) {
    pico_app_view_t v = H->view();
    if(controller||pad_role(H)==PGA_DUO_CONTROLLER){
        controller=true;int input=pad_input(H,e);
        if(input>=0&&!pad_send(H,(uint8_t)input))H->toast("手柄未连接，请等待重连");
        return false;
    }
    if(e->type==PGA_TAP&&e->x>=v.w-80&&e->x<v.w-16&&e->y>=30&&e->y<66)return control(PAD_OPTIONS);
    if(options){
        if(e->type==PGA_TAP&&e->x>=16&&e->x<v.w-16){
            if(e->y>=v.h-160&&e->y<v.h-112)return control(PAD_B);
            if(e->y>=v.h-96&&e->y<v.h-48)return control(PAD_A);
        }
        return false;
    }
    if (e->type == PGA_CANCEL) {
        contact_handled = swipe_tracking = false;
        return false;
    }
    if (e->type == PGA_PRESS) contact_handled = swipe_tracking = false;
    // 操作键移到角落手势区域上方；方向按下即响应，重开与暂停仍在松手确认。
    // Keep controls above corner gestures; directions act on press, restart/pause confirm on lift.
    if ((e->type == PGA_PRESS || e->type == PGA_TAP) && e->y >= v.h - 88 && e->y < v.h - 56) {
        int w = (v.w - 62) / 6, i = (e->x - 16) / (w + 6);
        if (e->x >= 16 && i >= 0 && i < 6 && (e->x - 16) % (w + 6) < w) {
            if (e->type == PGA_TAP && contact_handled) { contact_handled = false; return true; }
            if (e->type == PGA_PRESS && (i == 5 || (PICO_GAME_KIND == 7 && i == 4))) return false;
            action(PICO_GAME_KIND == 7 && i == 4 ? 6 : i);
            contact_handled = e->type == PGA_PRESS;
            return true;
        }
    }
    if (e->type == PGA_PRESS) {
        int x, y, c;
        geometry(&x, &y, &c);
        swipe_tracking = e->x >= x && e->x < x + c * (PICO_GAME_KIND == 6 ? 10 : 17) &&
                         e->y >= y && e->y < y + c * (PICO_GAME_KIND == 6 ? 20 : 17);
    }
    if (e->type == PGA_RELEASE) swipe_tracking = false;
    if (e->type == PGA_MOVE && swipe_tracking && !contact_handled) {
        int dx = e->x - e->x0, dy = e->y - e->y0, ax = abs_i(dx), ay = abs_i(dy);
        if ((ax > ay ? ax : ay) < 24 || (ax > ay ? ax : ay) * 4 < (ax < ay ? ax : ay) * 5) return false;
        action(ax > ay ? (dx > 0 ? 2 : 0) : (dy > 0 ? 3 : 1));
        contact_handled = true;
        return true;
    }
    if (e->type >= PGA_SWIPE_L && e->type <= PGA_SWIPE_D) {
        if (contact_handled) { contact_handled = false; return true; }
        action(e->type == PGA_SWIPE_L ? 0 : e->type == PGA_SWIPE_R ? 2 : e->type == PGA_SWIPE_U ? 1 : 3);
        return true;
    }
    return false;
}
static bool key(int k) {
    if (k == 0) {
        S.paused = 1;
        if (save())
            H->navigate(PGA_PAGE_LAUNCHER);
        else
            H->toast("保存失败，请检查 TF 卡");
        return true;
    }
    return false;
}
static uint32_t status(void) {
    bool paused=controller?(!last_role||remote.paused||remote.setup):S.paused;
    return PGA_STATUS_ARCADE | PGA_STATUS_REALTIME | (paused ? PGA_STATUS_PAUSED : 0) |
           (dirty ? PGA_STATUS_UNSAVED : 0);
}
static void stop(void) {
    S.paused = 1;
    deadline = 0;
}
static const pico_app_exports_t app = { .abi = 1,
                                        .bytes = sizeof(app),
                                        .version = 10003,
                                        .kind = PICO_GAME_KIND,
                                        .id = PICO_GAME_ID,
                                        .start = start,
                                        .render = render,
                                        .event = event,
                                        .key = key,
                                        .tick = tick,
                                        .action = action,
                                        .status = status,
                                        .save = save,
                                        .stop = stop };
__attribute__((visibility("default"))) const pico_app_exports_t* pico_app_query(void) {
    return &app;
}

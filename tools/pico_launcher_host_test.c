/* 中文：应用集的纯绘制、原首页排序、稳定ID启动及双方向命中。/ English: App collection purity, saved Home order, stable-ID launches and both orientations. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../main/system/pico_navigation.c"
#include "../main/system/pico_launcher.c"

static ps_view_t view={360,640,false};
static ps_route_t route=PS_APPS,history[8];
static unsigned depth,game_count=6,scans,stores,opens,draws;
static bool blocked;
static const char *game_ids[]={"gomoku","go","xiangqi","chess","2048","snake"};
static const char *game_titles[]={"五子棋","围棋","中国象棋","国际象棋","2048","贪吃蛇"};
static char opened[32];
ps_view_t ps_view(void){return view;}
ps_route_t ps_route(void){return route;}
void ps_navigate(ps_route_t next){assert(depth<8);history[depth++]=route;route=next;}
void ps_back(void){assert(depth);route=history[--depth];}
void ps_toast(const char *text){assert(text&&*text);}
bool ps_load(const char *key,void *out,size_t bytes){(void)key;(void)out;(void)bytes;return false;}
bool ps_store(const char *key,const void *data,size_t bytes){(void)key;(void)data;(void)bytes;stores++;return true;}
unsigned ps_games_count(void){return game_count;}
void ps_games_rescan(void){scans++;}
const char *ps_games_last_error(void){return "TF card unavailable";}
bool ps_games_item(unsigned index,const char **id,const char **title){if(index>=game_count)return false;*id=game_ids[index];*title=game_titles[index];return true;}
bool ps_games_open_id(const char *id){opens++;snprintf(opened,sizeof(opened),"%s",id);if(blocked)return false;ps_navigate(PS_PLAYGAME);return true;}
static void launch_tool(uint16_t id){assert(id==600);ps_navigate(PS_APPLICATION);}
bool ps_hit(int x,int y,int rx,int ry,int rw,int rh){return x>=rx&&x<rx+rw&&y>=ry&&y<ry+rh;}
static void bounds(int x,int y,int w,int h){assert(x>=0&&y>=0&&w>0&&h>0);assert(x+w<=view.w&&y+h<=view.h);draws++;}
void ps_fill(uint8_t *fb,int x,int y,int w,int h,int gray){(void)fb;assert(gray>=0&&gray<=15);bounds(x,y,w,h);}
void ps_box(uint8_t *fb,int x,int y,int w,int h,int radius,int gray,bool filled){(void)radius;(void)filled;ps_fill(fb,x,y,w,h,gray);}
void ps_line(uint8_t *fb,int x,int y,int x2,int y2,int gray){ps_fill(fb,x,y,1,1,gray);ps_fill(fb,x2,y2,1,1,gray);}
void ps_text(uint8_t *fb,int x,int y,int size,const char *text,int gray){assert(text&&*text);ps_fill(fb,x,y,1,size,gray);}
void ps_text_fixed(uint8_t *fb,int x,int y,int size,const char *text,int gray){ps_text(fb,x,y,size,text,gray);}
void ps_center(uint8_t *fb,int x,int y,int size,const char *text,int gray){ps_text(fb,x,y,size,text,gray);}
void ps_icon(uint8_t *fb,int x,int y,int size,ps_icon_t icon,int gray){assert(icon>=0&&icon<PI_COUNT);ps_fill(fb,x,y,size,size,gray);}
void ps_button(uint8_t *fb,int x,int y,int w,int h,const char *label,bool selected){(void)label;(void)selected;ps_fill(fb,x,y,w,h,15);}
void ps_heading(uint8_t *fb,const char *title,const char *subtitle){(void)subtitle;ps_text(fb,16,34,20,title,1);}
void ps_tabs(uint8_t *fb,int section){(void)fb;(void)section;}
void ps_surface(uint8_t *fb,int x,int y,int w,int h,bool selected){(void)selected;ps_fill(fb,x,y,w,h,15);}
void ps_wrap(uint8_t *fb,int x,int y,int w,int size,int lines,const char *text,int gray){(void)text;ps_fill(fb,x,y,w,size*lines,gray);}

static bool tap(int x,int y){ps_event_t e={.type=PS_TAP,.x=x,.y=y};return ps_launcher_event(&e);}
static void open_slot(unsigned slot){EpdRect r=card(slot);assert(tap(r.x+r.width/2,r.y+r.height/2));}
static void pure_render(void){unsigned before_scans=scans,before_stores=stores,before_opens=opens;navigation_saved_t saved=current;ps_launcher_render(NULL);assert(scans==before_scans&&stores==before_stores&&opens==before_opens);assert(!memcmp(&saved,&current,sizeof(saved)));}

int main(void){
    ps_navigation_init();
    ps_navigation_entry_t board={511,PS_CANVAS,PI_LAYOUT,"灵犀画板",NULL};
    ps_navigation_entry_t cloud={512,PS_CLOUDFLARE,PI_LINK,"网站流量",NULL};
    ps_navigation_entry_t tool={600,PS_APPLICATION,PI_NOTE,"画板笔记",launch_tool};
    assert(ps_navigation_register(&board)&&ps_navigation_register(&cloud)&&ps_navigation_register(&tool));
    assert(destinations()==7&&count()==14&&pages()==3);
    assert(destination(0)->route==PS_SHELF&&destination(5)->route==PS_CLOUDFLARE);
    assert(destination(6)->id==600&&!destination(7));
    // 旧首页配置原样保留；隐藏项仍在应用集中可发现。/ Preserve existing Home configuration while hidden entries remain discoverable in Apps.
    current.count=2;current.ids[0]=PS_NAV_GAMES;current.ids[1]=PS_NAV_BOOKS;
    assert(destination(0)->route==PS_GAMES&&destination(1)->route==PS_SHELF&&destination(2)->route==PS_HA);
    for(int orientation=0;orientation<2;orientation++){
        view=orientation?(ps_view_t){640,360,true}:(ps_view_t){360,640,false};
        unsigned start_scans=scans;ps_launcher_enter();assert(scans==start_scans+1);
        for(page=0;page<pages();page++)pure_render();page=0;
        route=PS_APPS;depth=0;open_slot(5);assert(route==PS_CLOUDFLARE);ps_back();assert(route==PS_APPS);
        page=1;open_slot(0);assert(route==PS_APPLICATION);ps_back();assert(route==PS_APPS);
        open_slot(1);assert(route==PS_PLAYGAME&&!strcmp(opened,"gomoku"));ps_back();
        blocked=true;open_slot(2);assert(route==PS_APPS&&!strcmp(opened,"go"));blocked=false;
        page=2;open_slot(1);assert(route==PS_DEVICE);ps_back();assert(route==PS_APPS);
        unsigned before=scans;assert(tap(view.w-40,97)&&scans==before+1);
        assert(ps_launcher_key(2)&&page==0);assert(ps_launcher_key(0)&&page==2);
        assert(!ps_launcher_key(-1));
    }
    // TF应用消失后页数钳制，系统与Cloudflare入口保留且不误启旧索引。/ Clamp pages after TF removal; retain system/Cloudflare entries without launching a stale game index.
    game_count=0;ps_navigation_unregister(600);page=2;assert(effective_page()==1&&pages()==2&&destinations()==6);
    pure_render();assert(page==2);route=PS_APPS;depth=0;open_slot(0);assert(route==PS_DEVICE);ps_back();
    ps_launcher_enter();assert(page==1);pure_render();
    assert(stores==0);
    printf("launcher contracts passed: %u draws, %u scans, %u game opens; no device access\n",draws,scans,opens);
    return 0;
}

/* 中文：首页编排事务和双方向命中契约。/ English: Home editor transaction and two-orientation hit contracts. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../main/system/pico_navigation.c"

static ps_view_t view={360,640,false};
static navigation_saved_t disk;
static bool has_disk,fail_store;
static int stores,backs,toasts,launches,draws;
ps_view_t ps_view(void){return view;}
ps_route_t ps_route(void){return PS_NAV_SETTINGS;}
bool ps_load(const char *key,void *out,size_t bytes){assert(!strcmp(key,"navigation1"));assert(bytes==sizeof(disk));if(!has_disk)return false;memcpy(out,&disk,bytes);return true;}
bool ps_store(const char *key,const void *data,size_t bytes){assert(!strcmp(key,"navigation1"));assert(bytes==sizeof(disk));stores++;if(fail_store)return false;memcpy(&disk,data,bytes);has_disk=true;return true;}
void ps_back(void){backs++;}
void ps_toast(const char *text){assert(text&&*text);toasts++;}
#ifndef PICO_NAVIGATION_REAL_RENDER
bool ps_hit(int x,int y,int rx,int ry,int rw,int rh){return x>=rx&&x<rx+rw&&y>=ry&&y<ry+rh;}
static void bounds(int x,int y,int w,int h){assert(x>=0&&y>=0&&w>0&&h>0);if(x+w>view.w||y+h>view.h){fprintf(stderr,"overflow %dx%d: %d,%d %dx%d\n",view.w,view.h,x,y,w,h);assert(0);}draws++;}
void ps_fill(uint8_t *fb,int x,int y,int w,int h,int gray){(void)fb;(void)gray;bounds(x,y,w,h);}
void ps_box(uint8_t *fb,int x,int y,int w,int h,int radius,int gray,bool filled){(void)fb;(void)radius;(void)gray;(void)filled;bounds(x,y,w,h);}
void ps_line(uint8_t *fb,int x0,int y0,int x1,int y1,int gray){(void)fb;(void)gray;bounds(x0,y0,1,1);bounds(x1,y1,1,1);}
void ps_text(uint8_t *fb,int x,int y,int size,const char *text,int gray){(void)fb;(void)gray;assert(text);bounds(x,y,1,size);}
void ps_text_fixed(uint8_t *fb,int x,int y,int size,const char *text,int gray){ps_text(fb,x,y,size,text,gray);}
void ps_icon(uint8_t *fb,int x,int y,int size,ps_icon_t icon,int gray){(void)fb;(void)icon;(void)gray;bounds(x,y,size,size);}
void ps_button(uint8_t *fb,int x,int y,int w,int h,const char *label,bool selected){(void)fb;(void)label;(void)selected;bounds(x,y,w,h);}
void ps_center(uint8_t *fb,int x,int y,int size,const char *text,int gray){ps_text(fb,x,y,size,text,gray);}
void ps_heading(uint8_t *fb,const char *title,const char *subtitle){(void)subtitle;ps_text(fb,18,34,20,title,0);}
#endif
static void launch(uint16_t id){assert(id>=PS_NAV_CUSTOM_FIRST);launches++;}
static bool tap(int x,int y){ps_event_t e={.type=PS_TAP,.x=x,.y=y};return ps_navigation_event(&e);}
static void render_pure(void){navigation_saved_t before=current,draft=editor.draft;int n=stores;ps_navigation_render(NULL);assert(!memcmp(&before,&current,sizeof(before)));assert(!memcmp(&draft,&editor.draft,sizeof(draft)));assert(stores==n);}
static void done(void){assert(tap(view.w-80,footer_y()+10));}
static void cancel(void){assert(tap(view.landscape?view.w-190:40,footer_y()+10));}
static void reset(void){has_disk=false;fail_store=false;ps_navigation_init();}

int main(void){
    reset();assert(ps_navigation_count()==4);assert(ps_navigation_index(PS_SHELF)==0);assert(ps_navigation_index(PS_PRINTERS)==-1);assert(ps_navigation_index(PS_APPS)==3);
    assert(ps_navigation_catalog_count()==5&&ps_navigation_catalog_item(4)->id==PS_NAV_APPS);
    assert(!ps_navigation_catalog_item(-1)&&!ps_navigation_catalog_item(5));
    assert(!ps_navigation_item(-1));assert(!ps_navigation_item(4));
    ps_navigation_enter();move_selected(1);assert(ps_navigation_storage_save());
    assert(disk.ids[0]==PS_NAV_BOOKS&&editor.draft.ids[0]==PS_NAV_HOME);has_disk=false;stores=0;ps_navigation_init();
    ps_navigation_enter();render_pure();editor.selected=2;move_selected(-1);assert(editor.draft.ids[1]==PS_NAV_GAMES);
    assert(ps_navigation_item(1)->id==PS_NAV_HOME);cancel();assert(ps_navigation_item(1)->id==PS_NAV_HOME);assert(stores==0);

    ps_navigation_enter();editor.selected=0;editor.adding=false;choose_entry(PS_NAV_GAMES);
    assert(editor.draft.ids[0]==PS_NAV_GAMES&&editor.draft.ids[2]==PS_NAV_BOOKS);
    editor.adding=true;choose_entry(PS_NAV_PRINTERS);assert(editor.draft.count==5);assert(ps_navigation_count()==4);
    fail_store=true;done();assert(editor.active&&ps_navigation_count()==4&&toasts>0);
    fail_store=false;done();assert(!editor.active&&ps_navigation_count()==5&&ps_navigation_item(4)->id==PS_NAV_PRINTERS);
    ps_navigation_init();assert(ps_navigation_count()==5&&ps_navigation_item(0)->id==PS_NAV_GAMES&&ps_navigation_index(PS_PRINTERS)==4);

    ps_navigation_enter();editor.selected=0;hide_selected();hide_selected();hide_selected();hide_selected();assert(editor.draft.count==1);
    hide_selected();assert(editor.draft.count==1);done();ps_navigation_init();assert(ps_navigation_count()==1);
    assert(ps_navigation_item(0)->id==PS_NAV_PRINTERS);

    // 丢失自定义应用仍可恢复书架；重新注册后恢复原ID位置。/ Missing custom apps retain safe Books fallback and regain their position when registered.
    defaults(&disk);disk.count=1;disk.ids[0]=256;has_disk=true;ps_navigation_init();assert(ps_navigation_count()==1&&ps_navigation_item(0)->id==PS_NAV_BOOKS);
    ps_navigation_entry_t custom={256,PS_ROUTE_COUNT,PI_GRID,"自定义应用",launch};
    assert(ps_navigation_register(&custom));assert(ps_navigation_item(0)->id==256);ps_navigation_item(0)->launch(256);assert(launches==1);
    assert(!ps_navigation_register(&custom));custom.id=0;assert(!ps_navigation_register(&custom));
    custom.id=257;custom.launch=NULL;assert(!ps_navigation_register(&custom));
    custom.route=PS_SHELF;assert(!ps_navigation_register(&custom));custom.route=PS_ROUTE_COUNT;
    for(int i=257;i<267;i++){custom.id=(uint16_t)i;custom.launch=launch;assert(ps_navigation_register(&custom));}
    custom.id=267;assert(!ps_navigation_register(&custom));

    for(int orientation=0;orientation<2;orientation++){
        view=orientation?(ps_view_t){640,360,true}:(ps_view_t){360,640,false};
        ps_navigation_enter();render_pure();defaults(&editor.draft);
        editor.adding=true;choose_entry(PS_NAV_PRINTERS);assert(editor.draft.count==5);render_pure();
        int y=more_y();assert(tap(30,y+10));assert(!editor.picking); // 已满不超出。/ Full list remains bounded.
        editor.adding=false;editor.selected=2;editor.picking=true;render_pure();
        int before=stores;assert(tap(3,77));assert(!editor.picking&&stores==before); // 浮层外点击不透传。/ Outside dismissal never clicks through.
        editor.picking=true;assert(ps_navigation_back());assert(!editor.picking&&editor.active);assert(!ps_navigation_back());assert(!editor.active);
        ps_navigation_enter();editor.picking=true;editor.picker_page=1;render_pure();
        editor.picker_page=(registry_count-1)/picker_rows();render_pure();
        int n=editor.draft.count;editor.adding=true;choose_entry(editor.draft.ids[0]);assert(editor.draft.count==n&&editor.picking);
    }
    reset();ps_navigation_enter();editor.selected=0;hide_selected();done();assert(ps_navigation_index(PS_SHELF)==-1);
    ps_navigation_enter();defaults(&editor.draft);done();assert(ps_navigation_index(PS_SHELF)==0);
    disk.ids[1]=disk.ids[0];ps_navigation_init();assert(ps_navigation_count()==4&&ps_navigation_item(1)->id==PS_NAV_HOME);
    printf("navigation contracts passed: %d draws, %d save attempts, %d returns\n",draws,stores,backs);return 0;
}

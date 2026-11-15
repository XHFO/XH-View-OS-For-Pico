/* 实际PGA的分页选择与控件事件测试。/ Exercise real PGA selection paging and control gestures. */
#define main legacy_entry_main
#include "home_link_entry_host_test.c"
#undef main
static pico_app_home_selection_t selected;
static unsigned catalog_scans,selection_writes;
static bool fail_save;
static char action_sent[32];static float value_sent;
static bool selection_api(uint32_t cmd,pico_app_home_selection_t *s){
    if(cmd==PGA_SELECTION_SET){selection_writes++;if(fail_save){strcpy(s->error,"保存失败");return false;}
        assert(s->count<=96);selected=*s;selected.configured=1;selected.revision++;}
    *s=selected;s->bytes=sizeof(*s);return true;
}
static bool catalog_api(uint32_t cmd,pico_app_home_catalog_t *c){
    if(cmd==PGA_CATALOG_SCAN)catalog_scans++;
    unsigned offset=c->offset;memset(c,0,sizeof(*c));c->bytes=sizeof(*c);c->offset=offset;c->total=120;c->result=2;
    c->count=offset<120?(120-offset<24?120-offset:24):0;
    for(unsigned i=0;i<c->count;i++){pico_app_home_entity_t *e=&c->items[i];e->bytes=sizeof(*e);snprintf(e->id,96,"light.room_%u",offset+i);snprintf(e->name,64,"Device %u",offset+i);}
    return true;
}
static bool no_load(const char *k,void *p,size_t n){(void)k;(void)p;(void)n;return false;}
static bool entity_api(uint32_t index,pico_app_home_entity_t *e){
    if(!home_entity(index,e))return false;
    if(index==0){strcpy(e->id,"light.room");e->kind=0;e->on=0;e->dimmable=1;e->brightness=127;}
    if(index==1){strcpy(e->id,"climate.room");e->kind=2;e->hvac_modes=(1<<0)|(1<<2)|(1<<4);strcpy(e->state,"cool");}
    return true;
}
static void control_button(uint8_t *fb,int x,int y,int w,int h,const char *label,bool selected){
    (void)fb;(void)label;(void)selected;assert(x>=0&&y>=0&&w>0&&h>0&&x+w<=viewport.w&&y+h<=viewport.h);
}
static bool control_api(const char *id,const char *action,float value){(void)id;controls++;snprintf(action_sent,sizeof(action_sent),"%s",action);value_sent=value;return !fail_save;}
static void send_event(const pico_app_exports_t *app,int type,int x,int y){if(!app->event(&(pico_app_event_t){.type=type,.x=x,.y=y,.x0=x,.y0=y})){fprintf(stderr,"unhandled event %d (%d,%d) landscape=%d\n",type,x,y,viewport.landscape);assert(type==PGA_PRESS);}}
int main(int argc,char **argv){
    assert(argc==2);void *lib=dlopen(argv[1],RTLD_NOW);assert(lib);pico_app_query_t query=(pico_app_query_t)dlsym(lib,"pico_app_query");assert(query);
    const pico_app_exports_t *app=query();pico_app_host_t bridge=host;bridge.home_selection=selection_api;bridge.home_catalog=catalog_api;
    bridge.home_link_entity=entity_api;bridge.home_link_control=control_api;bridge.load=no_load;bridge.button=control_button;
    for(int landscape=0;landscape<2;landscape++){
        viewport=landscape?(pico_app_view_t){640,360,true}:(pico_app_view_t){360,640,false};
        configured=connected=true;entity_count=2;entry_page=0;entity_revision++;controls=0;fail_save=false;
        memset(&selected,0,sizeof(selected));selected.bytes=sizeof(selected);selected.configured=1;
        assert(app->start(&bridge));render_pure(app,"家庭连接");
        int x=40,y=landscape?222:222;
        send_event(app,PGA_PRESS,x,y);send_event(app,PGA_MOVE,100,y);assert(!controls);
        send_event(app,PGA_RELEASE,100,y);assert(controls==1&&!strcmp(action_sent,"set_brightness")&&value_sent>=0&&value_sent<=100);
        send_event(app,PGA_TAP,100,y);assert(controls==1);
        send_event(app,PGA_PRESS,x,y);send_event(app,PGA_CANCEL,x,y);assert(controls==1);
        send_event(app,PGA_PRESS,40,170);send_event(app,PGA_LONG,40,170);send_event(app,PGA_MOVE,100,222);send_event(app,PGA_RELEASE,100,222);assert(controls==1);
        int mode_x=landscape?570:290;send_event(app,PGA_PRESS,mode_x,210);assert(tap(app,mode_x,210));app->render(NULL);
        // The first supported non-off mode is cool. / 第二个支持模式为制冷。
        int my=(landscape?24+48+28:130+48+41)+8;assert(tap(app,viewport.w/2,my));
        assert(controls==2&&!strcmp(action_sent,"set_hvac_mode")&&value_sent==2);
        int tx=landscape?256:116,ty=landscape?256:250;
        int before=controls;
        send_event(app,PGA_PRESS,tx,ty);send_event(app,PGA_MOVE,tx+24,ty);assert(controls==before);
        send_event(app,PGA_RELEASE,tx+24,ty);assert(controls==before+1&&!strcmp(action_sent,"turn_on"));
        send_event(app,PGA_SWIPE_R,tx+24,ty);send_event(app,PGA_TAP,tx+24,ty);assert(controls==before+1);
        send_event(app,PGA_PRESS,tx+24,ty);send_event(app,PGA_RELEASE,tx,ty);assert(controls==before+1); // Already off, do not invert.
        send_event(app,PGA_PRESS,tx,ty);send_event(app,PGA_CANCEL,tx,ty);assert(controls==before+1);
        send_event(app,PGA_PRESS,tx,ty);send_event(app,PGA_RELEASE,tx,ty+40);assert(controls==before+1);
        fail_save=true;send_event(app,PGA_PRESS,tx,ty);send_event(app,PGA_RELEASE,tx,ty);assert(controls==before+2);
        send_event(app,PGA_TAP,tx,ty);app->tick();assert(controls==before+2);fail_save=false;
        send_event(app,PGA_PRESS,tx,ty);send_event(app,PGA_CANCEL,tx,ty); // End suppression before direct test taps.
        app->stop();entry_page=0;entity_revision++;assert(app->start(&bridge));
        assert(tap(app,viewport.w-50,viewport.h-32));render_pure(app,"添加与管理设备");
        assert(tap(app,40,landscape?130:176));
        int pages=landscape?4:3;
        for(int i=0;i<pages;i++)send_event(app,PGA_SWIPE_L,200,180);
        assert(catalog_scans>=2);assert(tap(app,40,landscape?130:176));
        fail_save=true;assert(tap(app,viewport.w-50,viewport.h-32));assert(selected.count==0);render_pure(app,"添加与管理设备");
        fail_save=false;assert(tap(app,viewport.w-50,viewport.h-32));assert(selected.count==2&&!strcmp(selected.ids[0],"light.room_0")&&!strcmp(selected.ids[1],"light.room_24"));
        assert(tap(app,viewport.w-50,viewport.h-32));assert(tap(app,40,landscape?130:176));assert(tap(app,40,viewport.h-32));assert(selected.count==2);
        app->stop();
        selected.count=96;for(unsigned i=0;i<96;i++)snprintf(selected.ids[i],96,"light.existing_%u",i);
        entry_page=0;entity_revision++;assert(app->start(&bridge));assert(tap(app,viewport.w-50,viewport.h-32));
        unsigned writes=selection_writes;assert(tap(app,40,landscape?130:176));assert(strstr(last_toast,"96"));
        assert(tap(app,40,viewport.h-32));assert(selection_writes==writes&&selected.count==96);app->stop();
    }
    dlclose(lib);puts("PASS: portrait/landscape brightness and switch release once; explicit directional switches, cancel/vertical rejection, failure without replay, synthetic tap suppressed; supported climate mode, 24-item paging/cross-page selection, failed save retry and cancel");
}

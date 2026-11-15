/* 房间选择使用真实PGA页面，只模拟宿主边界。/ Exercise real room-selection pages, mocking host boundaries only. */
#define main legacy_entry_main
#include "home_link_entry_host_test.c"
#undef main
static pico_app_home_rooms_t catalog;
static unsigned room_reads,room_writes;
static bool room_fail;
static uint32_t committed_selection;
static bool room_service(uint32_t cmd,pico_app_home_rooms_t *r){
    room_reads++;
    if(cmd==PGA_ROOMS_SCAN){catalog.busy=1;catalog.result=1;}
    if(cmd==PGA_ROOMS_SET){
        room_writes++;
        if(room_fail){strcpy(r->error,"保存失败，草稿保留");return false;}
        assert(r->revision==catalog.revision);committed_selection=r->selected;
        catalog.configured=1;catalog.selected=r->selected;catalog.revision++;
    }
    *r=catalog;return true;
}
static void paint_rooms(const pico_app_exports_t *app,const char *title){
    unsigned reads=room_reads;render_pure(app,title);assert(reads==room_reads);
}
int main(int argc,char **argv){
    assert(argc==2);void *lib=dlopen(argv[1],RTLD_NOW);assert(lib);
    pico_app_query_t query=(pico_app_query_t)dlsym(lib,"pico_app_query");assert(query);
    const pico_app_exports_t *app=query();pico_app_host_t bridge=host;bridge.home_rooms=room_service;
    for(int landscape=0;landscape<2;landscape++){
        viewport=landscape?(pico_app_view_t){640,360,true}:(pico_app_view_t){360,640,false};
        memset(&catalog,0,sizeof(catalog));catalog.bytes=sizeof(catalog);catalog.revision=1;
        catalog.count=catalog.total=3;catalog.result=2;catalog.items[0].devices=40;catalog.items[1].devices=30;catalog.items[2].devices=2;
        strcpy(catalog.items[0].name,"书房");strcpy(catalog.items[1].name,"客厅");strcpy(catalog.items[2].name,"未分配房间");
        configured=connected=true;busy=false;entry_page=0;entity_count=0;syncs=0;room_writes=0;legacy_status=false;
        assert(app->start(&bridge));app->tick();assert(!syncs&&catalog.busy);paint_rooms(app,"选择同步房间");
        catalog.busy=0;catalog.result=2;catalog.revision++;app->tick();
        int y=landscape?138:184;
        assert(tap(app,40,y));assert(tap(app,landscape?350:40,y+(landscape?0:46)));
        room_fail=true;assert(tap(app,viewport.w-60,viewport.h-32));assert(room_writes==1&&!syncs);paint_rooms(app,"选择同步房间");
        room_fail=false;assert(tap(app,viewport.w-60,viewport.h-32));assert(room_writes==2&&syncs==1&&committed_selection==3);
        paint_rooms(app,"添加与管理设备");
        assert(tap(app,viewport.w/2,viewport.h-32));paint_rooms(app,"选择同步房间");
        catalog.busy=0;catalog.result=2;catalog.revision++;app->tick();
        assert(tap(app,landscape?350:40,y+(landscape?0:46)));assert(tap(app,40,y+(landscape?46:92)));
        assert(tap(app,viewport.w-60,viewport.h-32));assert(committed_selection==5&&syncs==2);
        assert(tap(app,viewport.w/2,viewport.h-32));catalog.busy=0;catalog.result=2;catalog.revision++;app->tick();
        assert(tap(app,40,y));assert(tap(app,40,viewport.h-32));assert(committed_selection==5&&syncs==2);paint_rooms(app,"添加与管理设备");
        app->stop();
    }
    dlclose(lib);puts("PASS: real room-first UI, large rooms without 64-item gate, unassigned selection, failed-save draft/retry, cancel and pure portrait/landscape rendering");
}

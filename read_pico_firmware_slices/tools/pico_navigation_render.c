/* 中文：真实导航控制器和字体截图，不代表实机显示。/ English: Render real navigation UI/font, not physical photographs. */
#define PICO_NAVIGATION_REAL_RENDER
#define main navigation_host_test_main
#include "pico_navigation_host_test.c"
#undef main
#include "pico_framebuffer_host.h"
#include <stdlib.h>

static void save_scene(const char *dir,const char *name){
    char path[512];snprintf(path,sizeof(path),"%s/%s-%s.pgm",dir,name,view.landscape?"landscape":"portrait");
    uint8_t *fb=pico_fb_new();assert(fb);ps_navigation_render(fb);assert(pico_fb_save(path,fb));free(fb);
}
int main(int argc,char **argv){
    assert(argc==3);assert(pico_fb_init(argv[1]));reset();
    ps_navigation_entry_t app={256,PS_ROUTE_COUNT,PI_GRID,"自定义应用",launch};assert(ps_navigation_register(&app));
    for(int orientation=0;orientation<2;orientation++){
        view=orientation?(ps_view_t){640,360,true}:(ps_view_t){360,640,false};
        ps_navigation_enter();save_scene(argv[2],"navigation-default");
        editor.adding=true;choose_entry(PS_NAV_PRINTERS);choose_entry(256);save_scene(argv[2],"navigation-five");
        editor.picking=true;editor.adding=false;save_scene(argv[2],"navigation-replace");
    }
    pico_fb_close();return 0;
}

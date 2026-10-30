/* SPDX-License-Identifier: Apache-2.0
 * Shared production printer UI with fixture-only credentials; native rendering, not device photography.
 */
#define PICO_HOME_REAL_QR
#define PICO_HOME_REAL_RENDER
#define main home_host_test_main
#include "pico_home_host_test.c"
#undef main
#include "pico_framebuffer_host.h"

static void save_printer(const char *dir,const char *name,bool settings) {
    char path[512];snprintf(path,sizeof(path),"%s/%s-%s.pgm",dir,name,test_view.landscape?"landscape":"portrait");
    uint8_t *fb=pico_fb_new();assert(fb);real_printer_render(fb,settings);assert(pico_fb_save(path,fb));free(fb);
    if(real_manage&&real_management.active&&real_qr_ready){
        snprintf(path,sizeof(path),"%s/%s-%s.qr.txt",dir,name,test_view.landscape?"landscape":"portrait");
        FILE *out=fopen(path,"w");assert(out);fprintf(out,"%s\n",real_management.url);fclose(out);
    }
}
int main(int argc,char **argv) {
    assert(argc==3);assert(pico_fb_init(argv[1]));ps_home_init();
    link_config=(rp_home_config_t){.version=RP_HOME_CONFIG_VERSION,.enabled=1,.ha_token="fixture-ha"};
    for(int land=0;land<2;land++) {
        test_view=land?(ps_view_t){640,360,true}:(ps_view_t){360,640,false};test_route=PS_PRINTER_SETTINGS;
        pc.selected=0;real_printer_cancel();save_printer(argv[2],"00-printer-settings",true);
        test_wifi=-1;real_manage_begin();save_printer(argv[2],"01-connect-wifi",true);real_manage_close();test_wifi=0;
        real_manage_begin();assert(real_qr_ready);save_printer(argv[2],"02-online-management-qr",true);
        test_management.seconds_left=29;assert(real_manage_sync());save_printer(argv[2],"03-expiring-management-qr",true);
        real_manage_close();
        link_config.printers[0]=(rp_home_printer_config_t){.source=RP_HOME_FORMLABS,.name="工作室 Form 4",.serial="FIXTURE123"};
        link_snapshot.printers[0]=(rp_home_printer_t){.source=RP_HOME_FORMLABS,.has_data=1,.connected=1,.percent=25,
            .remaining_minutes=30,.layer=50,.layers=200,.nozzle=-1,.bed=-1,.result=RP_HOME_OK,
            .state="PRINTING",.task="支架样件",.material="Clear Resin V5"};
        save_printer(argv[2],"05-formlabs-monitor",false);
        link_snapshot.printers[0].percent=-1;link_snapshot.printers[0].layer=-1;link_snapshot.printers[0].layers=-1;
        link_snapshot.printers[0].remaining_minutes=-1;link_snapshot.printers[0].task[0]=link_snapshot.printers[0].material[0]=0;
        copy_text(link_snapshot.printers[0].state,sizeof(link_snapshot.printers[0].state),"READY");save_printer(argv[2],"06-formlabs-idle",false);
        memset(link_config.printers,0,sizeof(link_config.printers));
    }
    pico_fb_close();return 0;
}

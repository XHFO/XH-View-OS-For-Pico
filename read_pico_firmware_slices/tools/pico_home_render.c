/* SPDX-License-Identifier: Apache-2.0
 * 使用真实家庭控制器和字体输出截图，不代表设备照片或真实网络。 / Render the real home controller and font, not physical photographs or real networking.
 */
#define PICO_HOME_REAL_RENDER
#define main home_host_test_main
#include "pico_home_host_test.c"
#undef main
#include "pico_framebuffer_host.h"

static void save_scene(const char *dir,const char *name){
    char path[512];snprintf(path,sizeof(path),"%s/%s-%s.pgm",dir,name,test_view.landscape?"landscape":"portrait");
    uint8_t *fb=pico_fb_new();assert(fb);ps_home_render(fb);assert(pico_fb_save(path,fb));free(fb);
}
int main(int argc,char **argv){
    assert(argc==3);assert(pico_fb_init(argv[1]));ps_home_init();
    copy_text(hc.home_wifi,sizeof(hc.home_wifi),"Home");copy_text(hc.external,sizeof(hc.external),"https://home.example.com");
    for(int land=0;land<2;land++){
        test_view=land?(ps_view_t){640,360,true}:(ps_view_t){360,640,false};
        enter(PS_HA);section=0;dashboard_page=0;room_picker=false;climate_entity=-1;save_scene(argv[2],"ha-dashboard");
        section=1;save_scene(argv[2],"ha-room");room_picker=true;save_scene(argv[2],"ha-rooms");room_picker=false;climate_entity=2;save_scene(argv[2],"ha-climate");climate_entity=-1;
        enter(PS_HA_SETTINGS);for(int tab=0;tab<3;tab++){ha_tab=tab;save_scene(argv[2],tab==0?"ha-connection":tab==1?"ha-filter":"ha-layout");}
        ha_tab=0;choose_home_wifi();save_scene(argv[2],"ha-wifi");wifi_picker=false;
        memset(&pc.devices,0,sizeof(pc.devices));enter(PS_PRINTERS);save_scene(argv[2],"printers-empty");
        printer_view=PV_SOURCE;save_scene(argv[2],"printers-source");printer_view=PV_IMPORT;import_mask=3;save_scene(argv[2],"printers-import");import_printers();
        pc.selected=0;save_scene(argv[2],"printers-monitor");pc.selected=1;save_scene(argv[2],"printers-paused");
        enter(PS_PRINTER_SETTINGS);save_scene(argv[2],"printers-settings");
        info_printer=0;printer_view=PV_INFO;save_scene(argv[2],"printers-info");printer_view=PV_REMOVE;save_scene(argv[2],"printers-remove");
        begin_mqtt();save_scene(argv[2],"printers-mqtt1");mqtt_page=1;save_scene(argv[2],"printers-mqtt2");
        enter(PS_SLEEP_SETTINGS);save_scene(argv[2],"sleep-settings");
        enter(PS_CLOCK);clock_frame=6;save_scene(argv[2],"clock");test_minute++;ps_home_tick();clock_frame=2;save_scene(argv[2],"clock-flip");wake_until=test_now+8000;save_scene(argv[2],"clock-awake");
        wake_until=0;test_hour=6;test_minute=7;clock_sample(false);save_scene(argv[2],"clock-0607");test_hour=8;test_minute=9;clock_sample(false);save_scene(argv[2],"clock-0809");test_hour=12;test_minute=34;
        clock_quotes=1;enter(PS_SLEEP_SETTINGS);save_scene(argv[2],"clock-excerpts-settings");enter(PS_CLOCK);clock_frame=6;save_scene(argv[2],"clock-excerpts");test_battery=8;ps_home_tick();save_scene(argv[2],"clock-excerpts-low-power");test_battery=86;ps_home_tick();clock_quotes=0;
        enter(PS_QUOTES);copy_text(qcache[0].text,sizeof(qcache[0].text),"我们在阅读中遇见的每一个世界，最终都会成为理解生活的一扇窗。那些曾经打动我们的句子，会在某个安静的午后，再次照亮眼前的日常。");copy_text(qcache[0].source,sizeof(qcache[0].source),"《沿河的书店》 · 原创长篇 · 第十二章");qc.current=quote_hash(qcache[0].text,qcache[0].source);save_scene(argv[2],"quotes");
        test_battery=8;quote_paused=true;save_scene(argv[2],"quotes-low-power");wake_until=test_now+8000;save_scene(argv[2],"quotes-awake");test_battery=86;quote_paused=false;
        wake_until=0;for(int i=0;i<180;i++)memcpy(qcache[0].text+i*3,"读",3);qcache[0].text[540]=0;quote_segment=0;save_scene(argv[2],"quotes-long");quote_segment=1;save_scene(argv[2],"quotes-long2");qc.current=0;save_scene(argv[2],"quotes-empty");
    }
    pico_fb_close();return 0;
}

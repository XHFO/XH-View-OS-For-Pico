/* 中文：只用合成凭据测试 raw v1 持久化迁移与失败恢复；不请求网络。
 * English: Synthetic-only raw-v1 persistence and failure checks; never request networking.
 */
#define _POSIX_C_SOURCE 200809L
#include "read_pico_home_link.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static rp_home_config_t fixture(void) {
    rp_home_config_t c = {.version=1,.enabled=1,.external_enabled=1,.room_count=1,
        .local="http://ha-fixture.invalid:8123",.external="https://ha-fixture.invalid",
        .home_wifi="FixtureOffline",.ha_token="SYNTHETIC_HA_SECRET"};
    strcpy(c.rooms[0].entity,"light.fixture");strcpy(c.rooms[0].room,"Fixture room");
    for (int i=0;i<RP_HOME_PRINTERS;i++) {
        rp_home_printer_config_t *p=&c.printers[i];
        p->source=(uint8_t)(i==0?RP_HOME_FORMLABS:i==1?RP_HOME_BAMBU_CLOUD:RP_HOME_BAMBU_LAN);
        p->region=1;p->port=8883;
        snprintf(p->name,sizeof(p->name),"Saved printer %d",i);
        snprintf(p->serial,sizeof(p->serial),"FIXTURE%d",i);
        strcpy(p->host,"printer-fixture.invalid");strcpy(p->username,"u_fixture");
        strcpy(p->secret,i==0?"FOREIGN_MQTT_SECRET":i==1?"SYNTHETIC_CLOUD_TOKEN":"SYNTHETIC_LAN_CODE");
        strcpy(p->client_id,"fixture-client");strcpy(p->client_secret,i==0?"SYNTHETIC_FORM_SECRET":"FOREIGN_FORM_SECRET");
        strcpy(p->certificate,"SYNTHETIC_CA_NOT_FOR_NETWORK");
    }
    return c;
}
static rp_home_config_t expected(void) {
    rp_home_config_t c=fixture();c.version=RP_HOME_CONFIG_VERSION;
    for(int i=0;i<RP_HOME_PRINTERS;i++) {
        rp_home_printer_config_t *p=&c.printers[i];
        if(p->source!=RP_HOME_BAMBU_CLOUD){p->region=0;memset(p->username,0,sizeof(p->username));}
        if(p->source!=RP_HOME_BAMBU_LAN){p->port=0;memset(p->host,0,sizeof(p->host));memset(p->certificate,0,sizeof(p->certificate));}
        if(p->source!=RP_HOME_BAMBU_LAN&&p->source!=RP_HOME_BAMBU_CLOUD)memset(p->secret,0,sizeof(p->secret));
        if(p->source!=RP_HOME_FORMLABS){memset(p->client_id,0,sizeof(p->client_id));memset(p->client_secret,0,sizeof(p->client_secret));}
    }
    return c;
}
static void write_file(const char *path,const rp_home_config_t *c) {
    FILE *f=fopen(path,"wb");assert(f);assert(fwrite(c,1,sizeof(*c),f)==sizeof(*c));assert(!fclose(f));assert(!chmod(path,0600));
}
static rp_home_config_t read_file(const char *path) {
    rp_home_config_t c;FILE *f=fopen(path,"rb");assert(f);assert(fread(&c,1,sizeof(c),f)==sizeof(c));assert(fgetc(f)==EOF);assert(!fclose(f));return c;
}
int main(int argc,char **argv) {
    assert(argc==3);const char *mode=argv[1],*path=argv[2];assert(!setenv("RPICO_HOME_LINK_CONFIG",path,1));
    rp_home_config_t want=expected(),got;
    if(!strcmp(mode,"seed")){rp_home_config_t old=fixture();write_file(path,&old);puts("seed: synthetic raw v1 fixture");return 0;}
    if(!strcmp(mode,"size")){printf("%zu\n",sizeof(want));return 0;}
    if(!strcmp(mode,"inspect")||!strcmp(mode,"inspect-x")){
        got=read_file(path);assert(rp_home_config_valid(&got));
        assert(!strcmp(got.ha_token,want.ha_token));
        assert(!memcmp(&got.printers[1],&want.printers[1],2*sizeof(want.printers[0])));
        assert(got.printers[0].source==RP_HOME_FORMLABS&&!strcmp(got.printers[0].client_id,want.printers[0].client_id));
        assert(!*got.printers[0].secret&&!*got.printers[0].host&&!*got.printers[0].username&&!*got.printers[0].certificate);
        if(!strcmp(mode,"inspect-x")){char value[256]={"x"};assert(!memcmp(got.printers[0].client_secret,value,sizeof(value)));}
        puts("inspect: v2 source isolation and other slots preserved");return 0;
    }
    if(!strcmp(mode,"migrate-fail")){
        rp_home_config_t old=read_file(path);assert(old.version==1);
        char blocked[4096];assert(snprintf(blocked,sizeof(blocked),"%s.tmp",path)>0);assert(!mkdir(blocked,0700));
        read_pico_home_link_init();read_pico_home_link_get_config(&got);assert(!memcmp(&got,&want,sizeof(want)));
        rp_home_config_t disk=read_file(path);assert(!memcmp(&disk,&old,sizeof(old)));assert(!rmdir(blocked));
        assert(read_pico_home_link_configure(&got));disk=read_file(path);assert(!memcmp(&disk,&want,sizeof(want)));
        puts("migration write failure: sanitized live config retained, explicit save later commits clean v2");return 0;
    }
    read_pico_home_link_init();read_pico_home_link_get_config(&got);
    if(!strcmp(mode,"load")){assert(!memcmp(&got,&want,sizeof(want)));rp_home_config_t disk=read_file(path);assert(!memcmp(&disk,&want,sizeof(want)));puts("load: raw v1 migrated and cleaned on disk, valid sources and HA retained");return 0;}
    if(!strcmp(mode,"save")){
        assert(!memcmp(&got,&want,sizeof(want)));strcpy(got.printers[0].name,"Changed current slot");
        assert(read_pico_home_link_configure(&got));want=read_file(path);assert(!memcmp(&got,&want,sizeof(want)));
        struct stat st;assert(!stat(path,&st)&&(st.st_mode&0777)==0600);
        puts("save: sanitized v2 committed privately");return 0;
    }
    if(!strcmp(mode,"reload")){strcpy(want.printers[0].name,"Changed current slot");assert(!memcmp(&got,&want,sizeof(want)));puts("reload: v2 survives process restart");return 0;}
    if(!strcmp(mode,"fail")){
        rp_home_config_t saved=read_file(path),before=got,after;
        char blocked[4096];assert(snprintf(blocked,sizeof(blocked),"%s.tmp",path)>0);assert(!mkdir(blocked,0700));
        strcpy(got.printers[0].name,"Must not replace saved");assert(!read_pico_home_link_configure(&got));
        read_pico_home_link_get_config(&after);assert(!memcmp(&before,&after,sizeof(before)));
        after=read_file(path);assert(!memcmp(&after,&saved,sizeof(saved)));assert(!rmdir(blocked));
        got=before;got.printers[0].secret[17]='x';assert(!read_pico_home_link_configure(&got));
        read_pico_home_link_get_config(&after);assert(!memcmp(&before,&after,sizeof(before)));
        puts("fail: failed persistence and foreign credentials preserve saved and live configs");return 0;
    }
    assert(!"unknown fixture mode");return 1;
}

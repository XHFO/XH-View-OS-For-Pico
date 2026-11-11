/* 有界HA投影与原子快照故障注入。/ Bounded HA projection and atomic-snapshot fault injection. */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static size_t allocation, fail_at, live, peak;
static void *tracked_malloc(size_t n) {
    if (++allocation == fail_at) return NULL;
    size_t *p = malloc(sizeof(*p) + n);
    if (!p) return NULL;
    *p = n; live += n; if (live > peak) peak = live;
    return p + 1;
}
static void tracked_free(void *p) {
    if (!p) return;
    size_t *raw = (size_t *)p - 1;
    live -= *raw;
    free(raw);
}
static void *tracked_calloc(size_t n, size_t width) {
    void *p = tracked_malloc(n * width);
    if (p) memset(p, 0, n * width);
    return p;
}
#define calloc tracked_calloc
#define free tracked_free
#include "../components/read_pico_home_link/home_link_model.c"
#undef calloc
#undef free
static rp_home_config_t config;
static rp_home_snapshot_t snapshot, previous;
static rp_home_token_t tokens[RP_HOME_PRINTERS];
static const char *reply;
static int response_status = 200;
static unsigned calls;
static rp_home_result_t fixture(void *unused, const rp_home_http_request_t *r, char *out, size_t n, size_t *used, int *status) {
    (void)unused;
    assert(!strcmp(r->method,"POST") && strstr(r->url,"/api/template"));
    assert(!strcmp(r->content_type,"application/json") && r->body && strstr(r->body,"pico_total"));
    ++calls; *status = response_status; *used = strlen(reply);
    if (*used >= n) return RP_HOME_TOO_LARGE;
    memcpy(out, reply, *used); return RP_HOME_OK;
}
int main(int argc, char **argv) {
    cJSON_Hooks hooks = {tracked_malloc, tracked_free}; cJSON_InitHooks(&hooks);
    config.version=RP_HOME_CONFIG_VERSION; config.enabled=1;
    strcpy(config.local,"http://ha.fixture:8123"); strcpy(config.ha_token,"fixture-only");
    config.printers[0].source=RP_HOME_HA; strcpy(config.printers[0].serial,"sensor.selected");
    if (argc == 2 && !strcmp(argv[1], "template")) {
        char *body=states_body(&config); assert(body); puts(body); tracked_free(body); assert(!live); return 0;
    }
    if(argc==2&&!strcmp(argv[1],"scoped-template")){
        rp_home_area_scope_t scope={.version=1,.count=1};strcpy(scope.ids[0],"office");
        char *body=states_body_scoped(&config,&scope);assert(body);puts(body);tracked_free(body);assert(!live);return 0;
    }
    if(argc==2&&!strcmp(argv[1],"areas-template")){puts(ha_areas_template);return 0;}
    assert(argc == 4);
    FILE *f=fopen(argv[1],"rb"); assert(f); fseek(f,0,SEEK_END); long size=ftell(f); rewind(f);
    char *body=malloc((size_t)size+1); assert(body); assert(fread(body,1,(size_t)size,f)==(size_t)size); fclose(f); body[size]=0;
    unsigned expected=(unsigned)strtoul(argv[2],NULL,10), total=(unsigned)strtoul(argv[3],NULL,10);
    assert(rp_home_parse_ha(body,(size_t)size,&config,&snapshot,123)==RP_HOME_OK);
    assert(snapshot.entity_count==expected && snapshot.entity_total==total && snapshot.rooms_synced);
    assert((total>expected) == (snapshot.truncated != 0));
    if(expected) {
        assert(!strcmp(snapshot.entities[0].id,"light.first"));
        assert(!strcmp(snapshot.entities[0].room,"书房"));
        assert(snapshot.entities[0].available && snapshot.entities[0].on);
        assert(snapshot.printers[0].has_data && snapshot.printers[0].percent==73);
        assert(!strcmp(snapshot.printers[0].task,"Keep this task"));
        if (expected == 6) {
            for (unsigned i = 0; i < 6; i++) assert(snapshot.entities[i].kind == i);
            assert(snapshot.entities[2].number == 26.5f && !snapshot.entities[3].available);
        }
    }
    previous=snapshot;
    const char *bad[]={"[{\"pico_total\":-1,\"pico_cut\":false}]", "[{\"pico_total\":2.5,\"pico_cut\":false}]", "[{\"pico_total\":65536,\"pico_cut\":false}]", "[{\"pico_total\":0,\"pico_cut\":1}]", "[{\"pico_total\":0,\"pico_cut\":false},{}]", "[{\"pico_total\":0,\"pico_cut\":false}]junk"};
    for(unsigned i=0;i<sizeof(bad)/sizeof(*bad);i++) { assert(rp_home_parse_ha(bad[i],strlen(bad[i]),&config,&snapshot,999)==RP_HOME_INVALID); assert(!memcmp(&snapshot,&previous,sizeof(snapshot))); }
    assert(rp_home_parse_ha(body,(size_t)size-1,&config,&snapshot,999)==RP_HOME_INVALID); assert(!memcmp(&snapshot,&previous,sizeof(snapshot)));
    // 每个内存分配点逐个失败，成功提交之前保持实体、时间、打印机原样。/ Fail each allocation, retaining data until successful commit.
    unsigned failures=0;
    for(size_t at=1;at<10000;at++) {
        allocation=0; fail_at=at; snapshot=previous;
        rp_home_result_t result=rp_home_parse_ha(body,(size_t)size,&config,&snapshot,456);
        assert(!live);
        if(allocation<at) { assert(result==RP_HOME_OK); break; }
        assert(result!=RP_HOME_OK); assert(!memcmp(&snapshot,&previous,sizeof(snapshot))); failures++;
    }
    fail_at=0; allocation=0; reply=body;
    rp_home_transport_t transport={fixture,NULL,NULL};
    assert(rp_home_fetch(&config,&snapshot,tokens,&transport,true,true,777)==RP_HOME_OK && calls==1);
    previous=snapshot;
    response_status=401; assert(rp_home_fetch(&config,&snapshot,tokens,&transport,true,true,778)==RP_HOME_AUTH);
    assert(snapshot.sampled_ms==previous.sampled_ms && !memcmp(snapshot.entities,previous.entities,sizeof(snapshot.entities)));
    response_status=403; reply="<html>Cloudflare Access login</html>";
    assert(rp_home_fetch(&config,&snapshot,tokens,&transport,true,true,779)==RP_HOME_ACCESS);
    assert(snapshot.sampled_ms==previous.sampled_ms && !memcmp(snapshot.entities,previous.entities,sizeof(snapshot.entities)));
    reply=body; response_status=200;
    for(size_t at=1;at<100;at++) {
        fail_at=at; allocation=0; snapshot=previous; calls=0;
        rp_home_result_t result=rp_home_fetch(&config,&snapshot,tokens,&transport,true,true,888);
        assert(!live);
        if(result==RP_HOME_OK) assert(snapshot.sampled_ms==888);
        else assert(snapshot.sampled_ms==previous.sampled_ms && !memcmp(snapshot.entities,previous.entities,sizeof(snapshot.entities)));
    }
    fail_at=0; assert(!live);
#if defined(PICO_HOME_LINK_FULL) && !PICO_HOME_LINK_FULL
    // 薄宿主不启动尚未支持的协议，原配置可原样回读。/ Thin hosts never execute unsupported protocols and preserve saved configuration.
    config.ha_token[0]=0; config.local[0]=0;
    for (unsigned source = RP_HOME_BAMBU_LAN; source <= RP_HOME_FORMLABS; source++) {
        rp_home_printer_config_t *p=&config.printers[0]; memset(p,0,sizeof(*p)); p->source=source;
        strcpy(p->serial,"FIXTURE123");
        if(source==RP_HOME_BAMBU_LAN) { strcpy(p->host,"printer.local"); strcpy(p->secret,"fixture"); strcpy(p->certificate,"fixture-ca"); p->port=8883; }
        if(source==RP_HOME_BAMBU_CLOUD) { strcpy(p->username,"u_fixture"); strcpy(p->secret,"fixture"); }
        if(source==RP_HOME_FORMLABS) { strcpy(p->client_id,"fixture"); strcpy(p->client_secret,"fixture"); }
        rp_home_printer_config_t before=*p; calls=0;
        assert(rp_home_config_valid(&config));
        assert(rp_home_fetch(&config,&snapshot,tokens,&transport,true,true,999)==RP_HOME_UNSUPPORTED);
        assert(!calls && !memcmp(p,&before,sizeof(before)) && !snapshot.printers[0].has_data);
    }
#endif
    printf("projection: bytes=%ld entities=%u total=%u failpoints=%u peak_bytes=%zu\n",size,expected,total,failures,peak);
    free(body);
}

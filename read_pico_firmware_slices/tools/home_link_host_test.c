/* 真实业务模型与本机HTTP/TLS传输契约；不代替云账户或设备验收。
 * Real shared-model and local HTTP/TLS transport contracts, not cloud-account or physical acceptance. */
#define _POSIX_C_SOURCE 200809L
#include "read_pico_home_link.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static unsigned checks;
#define CHECK(x)                                                                                                       \
    do {                                                                                                               \
        checks++;                                                                                                      \
        assert(x);                                                                                                     \
    } while (0)
static rp_home_config_t config;
static rp_home_snapshot_t snapshot;
static rp_home_token_t tokens[RP_HOME_PRINTERS];
static const char states[] = {
    "[{\"entity_id\":\"light.desk\",\"state\":\"on\",\"attributes\":{\"friendly_name\":\"台灯\",\"room\":\"书房\"}},"
    "{\"entity_id\":\"switch.socket\",\"state\":\"off\",\"attributes\":{}},"
    "{\"entity_id\":\"climate.ac\",\"state\":\"cool\",\"attributes\":{\"temperature\":26}},"
    "{\"entity_id\":\"fan.ceiling\",\"state\":\"unavailable\",\"attributes\":{}},"
    "{\"entity_id\":\"sensor.temperature\",\"state\":\"23.5\",\"attributes\":{\"device_class\":\"temperature\",\"unit_"
    "of_measurement\":\"°C\"}},"
    "{\"entity_id\":\"sensor.humidity\",\"state\":\"48\",\"attributes\":{\"device_class\":\"humidity\"}},"
    "{\"entity_id\":\"sensor.power\",\"state\":\"10\",\"attributes\":{\"device_class\":\"power\"}},"
    "{\"entity_id\":\"lock.front\",\"state\":\"locked\",\"attributes\":{}},"
    "{\"entity_id\":\"button.danger\",\"state\":\"unknown\",\"attributes\":{}},"
    "{\"entity_id\":\"sensor.bambu_a_print_status\",\"state\":\"running\",\"attributes\":{\"friendly_name\":\"P1S\"}},"
    "{\"entity_id\":\"sensor.bambu_a_print_progress\",\"state\":\"42\",\"attributes\":{}},"
    "{\"entity_id\":\"sensor.bambu_a_task_name\",\"state\":\"Test model\",\"attributes\":{}}]"};
static const char form[] = {"{\"printer\":{\"serial\":\"FORM123\",\"is_connected\":false,\"printer_status\":{"
                            "\"status\":\"printing\",\"current_print_run\":{\"name\":\"A "
                            "model\",\"status\":\"printing\",\"currently_printing_layer\":25,\"layer_count\":100,"
                            "\"estimated_time_remaining_ms\":120001,\"material_name\":\"Clear Resin\"}}}}"};
/* Official Dashboard GET /printers/{printer_serial}/ response shape (fixture values only).
 * https://formlabs-dashboard-api-resources.s3.amazonaws.com/formlabs-web-api-latest.html */
static const char form_official[] =
    "{\"serial\":\"FORM123\",\"alias\":\"Studio Form 4\",\"machine_type_id\":\"form4\","
    "\"printer_status\":{\"status\":\"PRINTING\",\"last_pinged_at\":\"2026-10-19T01:00:00Z\","
    "\"current_temperature\":35.5,\"current_print_run\":{\"guid\":\"fixture-job\",\"name\":\"Official shape\","
    "\"printer\":\"FORM123\",\"status\":\"PRINTING\",\"layer_count\":200,\"currently_printing_layer\":50,"
    "\"estimated_duration_ms\":2400000,\"elapsed_duration_ms\":600000,\"estimated_time_remaining_ms\":1800000,"
    "\"material\":\"fixture-material\",\"material_name\":\"Clear Resin V5\"}}}";
typedef struct {
    int calls, auths, details, mode, cf_headers;
    bool saw_body, saw_external, saw_control;
} fixture_t;
static rp_home_result_t http(void *opaque, const rp_home_http_request_t *r, char *out, size_t capacity, size_t *bytes,
                             int *status) {
    fixture_t *f = opaque;
    f->calls++;
    bool has_cf = (r->cf_access_client_id && *r->cf_access_client_id) ||
                  (r->cf_access_client_secret && *r->cf_access_client_secret);
    if (has_cf) {
        CHECK(r->cf_access_client_id && r->cf_access_client_secret && *r->cf_access_client_id &&
              *r->cf_access_client_secret);
        CHECK(!strcmp(r->cf_access_client_id, "cf-id.example"));
        CHECK(!strcmp(r->cf_access_client_secret, "cf-secret"));
        CHECK(r->url && strstr(r->url, "://external.example"));
        f->cf_headers++;
    }
    *status = 200;
    const char *reply = "[]";
    if (strstr(r->url, "/o/token/")) {
        f->auths++;
        CHECK(!strcmp(r->method, "POST"));
        CHECK(!r->bearer);
        CHECK(!strcmp(r->url, "https://api.formlabs.com/developer/v1/o/token/"));
        CHECK(!strcmp(r->content_type, "application/x-www-form-urlencoded"));
        CHECK(!strncmp(r->body, "grant_type=client_credentials&", 30));
        CHECK(strstr(r->body, "client_id=client%2Bid"));
        CHECK(strstr(r->body, "client_secret=secret%26value"));
        f->saw_body = true;
        reply = f->mode == 3 ? "{\"access_token\":\"bad\\r\\nvalue\",\"expires_in\":3600}"
                             : "{\"access_token\":\"fixture-token\",\"expires_in\":3600}";
    } else if (strstr(r->url, "api.formlabs.com")) {
        f->details++;
        CHECK(!strcmp(r->method, "GET") && !r->body);
        CHECK(!strcmp(r->url, "https://api.formlabs.com/developer/v1/printers/FORM123/"));
        CHECK(r->bearer && !strcmp(r->bearer, "fixture-token"));
        if (f->mode == 2 && f->details == 1)
            *status = 401;
        reply = form_official;
    } else if (strstr(r->url, "/api/services/")) {
        f->saw_control = true;
        CHECK(!strcmp(r->method, "POST"));
        CHECK(!strcmp(r->content_type, "application/json"));
        CHECK(strstr(r->body, "light.desk"));
    } else if (strstr(r->url, "/api/") && !r->bearer) {
        CHECK(!strcmp(r->method, "GET"));
        *status = 401;
        reply = "{}";
    } else if (strstr(r->url, "/api/template")) {
        CHECK(!strcmp(r->method, "POST"));
        CHECK(!strcmp(r->content_type, "application/json"));
        CHECK(strstr(r->body, "area_name(e)"));
        reply = "{\"light.desk\":\"书房\",\"switch.socket\":\"客厅\"}";
    } else {
        CHECK(!strcmp(r->method, "GET"));
        CHECK(r->bearer && !strcmp(r->bearer, "ha-fixture"));
        if (strstr(r->url, "external"))
            f->saw_external = true;
        if (f->mode == 1 && strstr(r->url, "local"))
            return RP_HOME_OFFLINE;
        if (f->mode == 4)
            *status = 401;
        reply = states;
    }
    *bytes = strlen(reply);
    CHECK(*bytes < capacity);
    memcpy(out, reply, *bytes);
    return RP_HOME_OK;
}
static rp_home_result_t mqtt(void *opaque, const rp_home_printer_config_t *p, char *out, size_t capacity,
                             size_t *bytes) {
    (void)opaque;
    CHECK(p->source == RP_HOME_BAMBU_CLOUD);
    CHECK(!strcmp(p->username, "u_fixture"));
    const char *body = "{\"print\":{\"gcode_state\":\"RUNNING\",\"mc_percent\":46,\"nozzle_temper\":220}}";
    *bytes = strlen(body);
    CHECK(*bytes < capacity);
    memcpy(out, body, *bytes);
    return RP_HOME_OK;
}
static rp_home_result_t mqtt_unsupported(void *opaque, const rp_home_printer_config_t *p, char *out, size_t capacity,
                                         size_t *bytes) {
    (void)opaque;
    (void)out;
    (void)capacity;
    CHECK(p && (p->source == RP_HOME_BAMBU_LAN || p->source == RP_HOME_BAMBU_CLOUD));
    *bytes = 0;
    return RP_HOME_UNSUPPORTED;
}
static rp_home_result_t http_access(void *opaque, const rp_home_http_request_t *r, char *out, size_t capacity,
                                    size_t *bytes, int *status) {
    (void)opaque;
    const char *body = "<html>Cloudflare Access login</html>";
    CHECK(r->url && strstr(r->url, "://external.example"));
    CHECK(r->cf_access_client_id && !strcmp(r->cf_access_client_id, "cf-id.example"));
    CHECK(r->cf_access_client_secret && !strcmp(r->cf_access_client_secret, "cf-secret"));
    *status = 403;
    *bytes = strlen(body);
    CHECK(*bytes < capacity);
    memcpy(out, body, *bytes);
    return RP_HOME_OK;
}
static void reset(void) {
    memset(&config, 0, sizeof(config));
    memset(&snapshot, 0, sizeof(snapshot));
    memset(tokens, 0, sizeof(tokens));
    config.version = RP_HOME_CONFIG_VERSION;
    config.enabled = 1;
    snprintf(config.local, sizeof(config.local), "http://local:8123");
    snprintf(config.external, sizeof(config.external), "https://external.example");
    snprintf(config.ha_token, sizeof(config.ha_token), "ha-fixture");
}
static uint32_t fixture_crc(const unsigned char *data, size_t bytes) {
    uint32_t crc=~0u;
    for(size_t i=0;i<bytes;i++) { crc^=data[i];for(int bit=0;bit<8;bit++)crc=(crc>>1)^((crc&1)?0xedb88320u:0); }
    return ~crc;
}
static void printer_schema_tests(void) {
    CHECK(fixture_crc((const unsigned char *)"123456789",9)==0xcbf43926u);
    for(int source=RP_HOME_HA;source<=RP_HOME_FORMLABS;source++) {
        reset();config.version=1;
        rp_home_printer_config_t *p=&config.printers[0];p->source=(uint8_t)source;p->region=1;p->port=8883;
        snprintf(p->name,sizeof(p->name),"Saved printer");snprintf(p->serial,sizeof(p->serial),"SERIAL1");
        snprintf(p->host,sizeof(p->host),"printer.local");snprintf(p->username,sizeof(p->username),"u_fixture");
        snprintf(p->secret,sizeof(p->secret),"fixture-mqtt");snprintf(p->certificate,sizeof(p->certificate),"fixture-ca");
        snprintf(p->client_id,sizeof(p->client_id),"fixture-client");snprintf(p->client_secret,sizeof(p->client_secret),"fixture-oauth");
        config.printers[1]=(rp_home_printer_config_t){.source=RP_HOME_BAMBU_CLOUD,.serial="BAMBU2",.username="u_other",.secret="fixture-other-token"};
        config.printers[2]=(rp_home_printer_config_t){.source=RP_HOME_FORMLABS,.serial="FORM3",.client_id="other-client",.client_secret="fixture-other-secret"};
        rp_home_config_t expected=config;
        CHECK(rp_home_config_upgrade(&expected)&&expected.version==RP_HOME_CONFIG_VERSION);
        CHECK(expected.printers[0].source==source&&!strcmp(expected.printers[0].name,"Saved printer")&&!strcmp(expected.printers[0].serial,"SERIAL1"));
        CHECK(!strcmp(expected.ha_token,"ha-fixture")&&!memcmp(&expected.printers[1],&config.printers[1],sizeof(*p))&&!memcmp(&expected.printers[2],&config.printers[2],sizeof(*p)));
        CHECK((source==RP_HOME_BAMBU_LAN)==(*expected.printers[0].host!=0));
        CHECK((source==RP_HOME_BAMBU_CLOUD)==(*expected.printers[0].username!=0));
        CHECK((source==RP_HOME_FORMLABS)==(*expected.printers[0].client_secret!=0));
        CHECK((source==RP_HOME_BAMBU_LAN||source==RP_HOME_BAMBU_CLOUD)==(*expected.printers[0].secret!=0));
        unsigned char blob[RP_HOME_CONFIG_PACK_LIMIT];
        uint32_t header[]={0x48504b31u,sizeof(config),fixture_crc((const unsigned char *)&config,sizeof(config))};
        memcpy(blob,header,sizeof(header));size_t used=sizeof(header),at=0;
        while(at<sizeof(config)) { size_t n=sizeof(config)-at;if(n>128)n=128;CHECK(used+n+1<=sizeof(blob));blob[used++]=(unsigned char)(n-1);memcpy(blob+used,(unsigned char *)&config+at,n);used+=n;at+=n; }
        rp_home_config_t restored;
        CHECK(rp_home_config_unpack(blob,used,&restored)&&!memcmp(&restored,&expected,sizeof(expected)));
        CHECK(rp_home_config_valid(&restored));
        rp_home_printer_config_t *foreign=&restored.printers[0];
        if(source==RP_HOME_FORMLABS) foreign->secret[9]='x';else foreign->client_secret[9]='x';
        CHECK(!rp_home_config_valid(&restored)&&!rp_home_config_pack(&restored,blob,sizeof(blob)));
        restored=expected;restored.version=99;CHECK(!rp_home_config_upgrade(&restored));
    }
    reset();config.printers[0]=(rp_home_printer_config_t){.source=RP_HOME_FORMLABS,.serial="FORM1",.client_id="id",.client_secret="fixture"};
    CHECK(rp_home_config_valid(&config));config.printers[0].client_secret[0]=0;CHECK(!rp_home_config_valid(&config));
    reset();config.printers[0]=(rp_home_printer_config_t){.source=RP_HOME_BAMBU_CLOUD,.serial="BAMBU1",.username="u_fixture",.secret="fixture"};
    CHECK(rp_home_config_valid(&config));snprintf(config.printers[0].username,sizeof(config.printers[0].username),"email@example.test");CHECK(!rp_home_config_valid(&config));
    reset();config.printers[0]=(rp_home_printer_config_t){.source=RP_HOME_BAMBU_LAN,.serial="BAMBU1",.host="printer.local",.secret="fixture"};
    CHECK(!rp_home_config_valid(&config));snprintf(config.printers[0].certificate,sizeof(config.printers[0].certificate),"fixture-ca");CHECK(rp_home_config_valid(&config));
    reset();
}
static void model_tests(void) {
    // 新结果只追加，原有 API 数值与证书/时间错误保持区分。/ Append results without renumbering old API values or conflating TLS and clock errors.
    CHECK(RP_HOME_EMPTY == 0 && RP_HOME_BUSY == 1 && RP_HOME_OK == 2 && RP_HOME_OFFLINE == 3);
    CHECK(RP_HOME_AUTH == 4 && RP_HOME_HTTP == 5 && RP_HOME_TLS == 6 && RP_HOME_INVALID == 7);
    CHECK(RP_HOME_TOO_LARGE == 8 && RP_HOME_NOMEM == 9 && RP_HOME_UNSUPPORTED == 10 && RP_HOME_TIME == 11);
    CHECK(RP_HOME_ACCESS == 12);
    CHECK(strstr(rp_home_result_text(RP_HOME_ACCESS), "Cloudflare Access"));
    CHECK(!strcmp(rp_home_result_text(RP_HOME_TIME), "设备时间未校准，请设置日期与时间"));
    CHECK(!strcmp(rp_home_result_text(RP_HOME_TLS), "TLS安全连接或证书校验失败"));
    CHECK(strcmp(rp_home_result_text(RP_HOME_TIME), rp_home_result_text(RP_HOME_TLS)));
    printer_schema_tests();
    reset();
    unsigned char packed[RP_HOME_CONFIG_PACK_LIMIT];
    rp_home_config_t decoded;
    size_t packed_size = rp_home_config_pack(&config, packed, sizeof(packed));
    CHECK(packed_size > 12 && packed_size < 1024);
    CHECK(rp_home_config_unpack(packed, packed_size, &decoded) && !memcmp(&config, &decoded, sizeof(config)));
    CHECK(!rp_home_config_unpack(packed, packed_size - 1, &decoded));
    packed[packed_size - 1] ^= 1;
    CHECK(!rp_home_config_unpack(packed, packed_size, &decoded));
    CHECK(rp_home_config_pack(&config, packed, 12) == 0);
    for (int i = 0; i < RP_HOME_PRINTERS; i++) {
        rp_home_printer_config_t *p = &config.printers[i];
        p->source = RP_HOME_BAMBU_LAN;
        p->port = 8883;
        memset(p->name, 'n', sizeof(p->name) - 1);
        memset(p->host, 'h', sizeof(p->host) - 1);
        memset(p->serial, 's', sizeof(p->serial) - 1);
        memset(p->secret, 'x', sizeof(p->secret) - 1);
        memset(p->certificate, 'c', sizeof(p->certificate) - 1);
    }
    CHECK(rp_home_config_valid(&config));
    packed_size = rp_home_config_pack(&config, packed, sizeof(packed));
    CHECK(packed_size > 9000 && packed_size < RP_HOME_CONFIG_PACK_LIMIT);
    CHECK(rp_home_config_unpack(packed, packed_size, &decoded) && !memcmp(&config, &decoded, sizeof(config)));
    CHECK(!rp_home_config_pack(&config, packed, 8192));
    reset();
    CHECK(rp_home_config_valid(&config));
    config.room_count = 1;
    snprintf(config.rooms[0].entity, 96, "light.desk");
    snprintf(config.rooms[0].room, 40, "工作间");
    config.printers[0].source = RP_HOME_HA;
    snprintf(config.printers[0].serial, 64, "sensor.bambu_a");
    CHECK(rp_home_parse_ha(states, strlen(states), &config, &snapshot, 1000) == RP_HOME_OK);
    CHECK(snapshot.entity_count == 6 && snapshot.entity_total == 6);
    CHECK(!strcmp(snapshot.entities[0].room, "工作间") && snapshot.entities[0].on);
    CHECK(snapshot.entities[3].available == 0);
    CHECK(fabs(snapshot.entities[4].number - 23.5) < .01);
    CHECK(snapshot.import_count == 1 && !strcmp(snapshot.imports[0].serial, "sensor.bambu_a"));
    CHECK(snapshot.printers[0].percent == 42 && !strcmp(snapshot.printers[0].task, "Test model"));
    rp_home_snapshot_t *saved = malloc(sizeof(*saved));
    CHECK(saved);
    *saved = snapshot;
    const char *bad[] = {"{}", "[{}", "[{},]", "[{}]x", "[false]", "[{\"entity_id\":]", "[{\"a\":{]}", "[\"x\"]"};
    for (unsigned i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
        CHECK(rp_home_parse_ha(bad[i], strlen(bad[i]), &config, &snapshot, 2000) == RP_HOME_INVALID);
        CHECK(!memcmp(saved, &snapshot, sizeof(snapshot)));
    }
    char *large = calloc(1, RP_HOME_HTTP_LIMIT);
    CHECK(large);
    size_t at = 0;
    large[at++] = '[';
    for (int i = 0; i < 1000; i++)
        at +=
            (size_t)snprintf(large + at, RP_HOME_HTTP_LIMIT - at,
                             "%s{\"entity_id\":\"light.test%d\",\"state\":\"off\",\"attributes\":{}}", i ? "," : "", i);
    large[at++] = ']';
    CHECK(rp_home_parse_ha(large, at, &config, &snapshot, 3000) == RP_HOME_OK);
    CHECK(snapshot.entity_count == RP_HOME_ENTITIES && snapshot.entity_total == 1000 && snapshot.truncated);
    free(large);
    free(saved);
    rp_home_printer_t p = {0}, previous;
    CHECK(rp_home_parse_formlabs(form, strlen(form), "FORM123", &p, 4000) == RP_HOME_OK);
    CHECK(p.has_data && !p.connected && p.percent == 25 && p.remaining_minutes == 3 && p.layer == 25 &&
          p.layers == 100);
    CHECK(!strcmp(p.material, "Clear Resin"));
    memset(&p, 0, sizeof(p));
    CHECK(rp_home_parse_formlabs(form_official, strlen(form_official), "FORM123", &p, 4100) == RP_HOME_OK);
    CHECK(p.connected && p.percent == 25 && p.remaining_minutes == 30 && p.layer == 50 && p.layers == 200);
    CHECK(!strcmp(p.task, "Official shape") && !strcmp(p.material, "Clear Resin V5") && !strcmp(p.name, "Studio Form 4"));
    CHECK(p.nozzle == -1 && p.bed == -1);
    const char *idle = "{\"serial\":\"FORM123\",\"printer_status\":{\"status\":\"READY\",\"current_print_run\":null}}";
    CHECK(rp_home_parse_formlabs(idle, strlen(idle), "FORM123", &p, 4200) == RP_HOME_OK);
    CHECK(p.connected && !*p.task && !*p.material && p.percent == -1 && p.layer == -1 && p.remaining_minutes == -1);
    previous = p;
    CHECK(rp_home_parse_formlabs(form, strlen(form), "WRONG", &p, 5000) == RP_HOME_INVALID);
    CHECK(!memcmp(&p, &previous, sizeof(p)));
    CHECK(rp_home_parse_formlabs("{\"error\":\"no\"}", 14, "FORM123", &p, 5000) == RP_HOME_INVALID);
    CHECK(!memcmp(&p, &previous, sizeof(p)));
    const char *report = "{\"print\":{\"gcode_state\":\"RUNNING\",\"mc_percent\":65,\"mc_remaining_time\":10,\"layer_"
                         "num\":3,\"total_layer_num\":10,\"subtask_name\":\"New\"}}";
    CHECK(rp_home_parse_bambu(report, strlen(report), &p, 6000) == RP_HOME_OK);
    CHECK(p.percent == 65 && p.layer == 3 && !strcmp(p.task, "New"));
    previous = p;
    CHECK(rp_home_parse_bambu("{\"print\":{\"command\":\"foo\"}}", 27, &p, 7000) == RP_HOME_INVALID);
    CHECK(!memcmp(&p, &previous, sizeof(p)));
    char body[200];
    CHECK(rp_home_form_body("id+中", "a&b=x", body, sizeof(body)));
    CHECK(strstr(body, "id%2B%E4%B8%AD") && strstr(body, "a%26b%3Dx"));
    CHECK(!rp_home_form_body("a", "b", body, 10));
    fixture_t f = {0};
    rp_home_transport_t t = {http, mqtt, &f};
    reset();
    config.external_enabled = 1;
    f.mode = 1;
    CHECK(rp_home_fetch(&config, &snapshot, tokens, &t, true, true, 8000) == RP_HOME_OK);
    CHECK(f.calls == 3 && f.saw_external && snapshot.external);
    CHECK(snapshot.rooms_synced && !strcmp(snapshot.entities[1].room, "客厅"));
    f = (fixture_t){.mode = 4};
    CHECK(rp_home_fetch(&config, &snapshot, tokens, &t, true, true, 9000) == RP_HOME_AUTH);
    CHECK(f.calls == 1 && !f.saw_external);
    f = (fixture_t){0};
    reset();
    config.ha_token[0] = 0;
    rp_home_printer_config_t *v = &config.printers[0];
    v->source = RP_HOME_FORMLABS;
    snprintf(v->serial, 64, "FORM123");
    snprintf(v->client_id, 128, "client+id");
    snprintf(v->client_secret, 256, "secret&value");
    CHECK(rp_home_config_valid(&config));
    CHECK(rp_home_fetch(&config, &snapshot, tokens, &t, true, true, 10000) == RP_HOME_OK);
    CHECK(f.auths == 1 && f.details == 1 && f.saw_body && snapshot.printers[0].percent == 25);
    CHECK(rp_home_fetch(&config, &snapshot, tokens, &t, true, true, 20000) == RP_HOME_OK);
    CHECK(f.auths == 1 && f.details == 2);
    f = (fixture_t){.mode = 2};
    memset(tokens, 0, sizeof(tokens));
    CHECK(rp_home_fetch(&config, &snapshot, tokens, &t, true, true, 30000) == RP_HOME_OK);
    CHECK(f.auths == 2 && f.details == 2);
    f = (fixture_t){.mode = 3};
    memset(tokens, 0, sizeof(tokens));
    CHECK(rp_home_fetch(&config, &snapshot, tokens, &t, true, true, 40000) == RP_HOME_INVALID);
    CHECK(f.auths == 1 && f.details == 0);
    reset();
    v = &config.printers[0];
    v->source = RP_HOME_BAMBU_CLOUD;
    snprintf(v->serial, 64, "BAMBU123");
    snprintf(v->username, 96, "u_fixture");
    snprintf(v->secret, 1024, "existing-cloud-token");
    f = (fixture_t){0};
    CHECK(rp_home_fetch(&config, &snapshot, tokens, &t, true, true, 50000) == RP_HOME_OK);
    CHECK(snapshot.printers[0].percent == 46);
    CHECK(rp_home_parse_ha(states, strlen(states), &config, &snapshot, 60000) == RP_HOME_OK);
    int status = 0;
    CHECK(rp_home_control(&config, &snapshot.entities[0], "turn_off", 0, &t, true, &status) == RP_HOME_OK);
    CHECK(f.saw_control);
    CHECK(rp_home_control(&config, &snapshot.entities[4], "turn_off", 0, &t, true, &status) == RP_HOME_INVALID);
    CHECK(rp_home_control(&config, &snapshot.entities[0], "toggle", 0, &t, true, &status) == RP_HOME_INVALID);
    CHECK(rp_home_control(&config, &snapshot.entities[0], "turn_off", 0, &t, false, &status) == RP_HOME_AUTH);
    snprintf(config.local, sizeof(config.local), "http://a@evil/");
    CHECK(!rp_home_config_valid(&config));
    reset();
    snprintf(config.external, sizeof(config.external), "http://external");
    CHECK(!rp_home_config_valid(&config));
    reset();
    snprintf(config.ha_token, sizeof(config.ha_token), "x\r\nInjected: y");
    CHECK(!rp_home_config_valid(&config));
    reset();
    snprintf(config.cf_access_client_id, sizeof(config.cf_access_client_id), "cf-id.example");
    CHECK(!rp_home_config_valid(&config));
    snprintf(config.cf_access_client_secret, sizeof(config.cf_access_client_secret), "cf-secret");
    CHECK(rp_home_config_valid(&config));
    rp_home_config_t kept = config;
    rp_home_link_update_t upd;
    memset(&upd, 0, sizeof(upd));
    upd.enabled = upd.external_enabled = 1;
    upd.local = "https://homeassistant.local:8123";
    upd.external = "https://external.example";
    upd.home_wifi = "Home";
    upd.ha_token = "";
    upd.cf_id = "";
    upd.cf_secret = "";
    CHECK(rp_home_apply_link_update(&config, &upd));
    CHECK(!strcmp(config.ha_token, "ha-fixture"));
    CHECK(!strcmp(config.cf_access_client_id, "cf-id.example") && !strcmp(config.local, upd.local));
    upd.clear_token = upd.clear_cf = 1;
    CHECK(rp_home_apply_link_update(&config, &upd));
    CHECK(!config.ha_token[0] && !config.cf_access_client_id[0] && !config.cf_access_client_secret[0]);
    config = kept;
    upd.clear_token = upd.clear_cf = 0;
    upd.has_printers = 1;
    upd.printer_source[0] = RP_HOME_BAMBU_CLOUD;
    CHECK(!rp_home_apply_link_update(&config, &upd));
    CHECK(!memcmp(&config, &kept, sizeof(config)));
    memset(&upd, 0, sizeof(upd));
    upd.enabled = 1;
    upd.local = config.local;
    upd.external = config.external;
    upd.home_wifi = "Home";
    upd.has_printers = 1;
    upd.printer_source[0] = RP_HOME_HA;
    snprintf(upd.printer_name[0], sizeof(upd.printer_name[0]), "P1S");
    snprintf(upd.printer_serial[0], sizeof(upd.printer_serial[0]), "sensor.bambu_a");
    CHECK(rp_home_apply_link_update(&config, &upd));
    CHECK(config.printers[0].source == RP_HOME_HA && !strcmp(config.printers[0].serial, "sensor.bambu_a"));
    CHECK(!config.printers[0].secret[0] && !strcmp(config.ha_token, "ha-fixture"));
    snprintf(config.cf_access_client_id, sizeof(config.cf_access_client_id), "cf-id.example");
    snprintf(config.cf_access_client_secret, sizeof(config.cf_access_client_secret), "cf-secret");
    snprintf(config.local, sizeof(config.local), "https://homeassistant.local:8123");
    config.external_enabled = 1;
    f = (fixture_t){0};
    CHECK(rp_home_fetch(&config, &snapshot, tokens, &t, true, true, 70000) == RP_HOME_OK);
    CHECK(f.cf_headers == 0 && !snapshot.external);
    f = (fixture_t){0};
    CHECK(rp_home_fetch(&config, &snapshot, tokens, &t, true, false, 71000) == RP_HOME_OK);
    CHECK(f.cf_headers >= 1 && snapshot.external && f.saw_external);
    f = (fixture_t){.mode = 1};
    CHECK(rp_home_fetch(&config, &snapshot, tokens, &t, true, true, 72000) == RP_HOME_OK);
    CHECK(snapshot.external && f.cf_headers >= 1);
    status = 0;
    f = (fixture_t){0};
    config.external_control = 1;
    CHECK(rp_home_parse_ha(states, strlen(states), &config, &snapshot, 73000) == RP_HOME_OK);
    CHECK(rp_home_control(&config, &snapshot.entities[0], "turn_off", 0, &t, true, &status) == RP_HOME_OK);
    CHECK(f.cf_headers == 0 && f.saw_control);
    f.saw_control = false;
    CHECK(rp_home_control(&config, &snapshot.entities[0], "turn_off", 0, &t, false, &status) == RP_HOME_OK);
    CHECK(f.cf_headers >= 1 && f.saw_control);
    snapshot.printers[1].percent = 88;
    snapshot.printers[1].has_data = 1;
    snprintf(snapshot.printers[1].state, sizeof(snapshot.printers[1].state), "FAKE");
    config.printers[1].source = RP_HOME_BAMBU_CLOUD;
    snprintf(config.printers[1].serial, sizeof(config.printers[1].serial), "BAMBU123");
    snprintf(config.printers[1].username, sizeof(config.printers[1].username), "u_fixture");
    snprintf(config.printers[1].secret, sizeof(config.printers[1].secret), "existing-cloud-token");
    rp_home_transport_t blocked = {http, mqtt_unsupported, &f};
    CHECK(rp_home_fetch(&config, &snapshot, tokens, &blocked, true, true, 74000) == RP_HOME_OK);
    CHECK(snapshot.printers[1].result == RP_HOME_UNSUPPORTED && snapshot.printers[1].percent == -1);
    CHECK(!snapshot.printers[1].has_data && !snapshot.printers[1].state[0]);
    CHECK(snapshot.entity_count == 6);
    rp_home_transport_t access = {http_access, mqtt_unsupported, &f};
    CHECK(rp_home_fetch(&config, &snapshot, tokens, &access, true, false, 75000) == RP_HOME_ACCESS);
    printf("home-link: %u shared-model checks passed; config=%zu snapshot=%zu bytes\n", checks, sizeof(config),
           sizeof(snapshot));
}
static void runtime_wait(void) {
    for (unsigned i = 0; i < 250; i++) {
        read_pico_home_link_get_snapshot(&snapshot);
        if (snapshot.sampled_ms || (!snapshot.busy && snapshot.result != RP_HOME_EMPTY))
            return;
        struct timespec t = {0, 100000000};
        nanosleep(&t, NULL);
    }
    CHECK(!"runtime request timed out");
}
int main(int argc, char **argv) {
    if (argc == 1) {
        model_tests();
        return 0;
    }
    reset();
    config.external_enabled = 0;
    if (!strcmp(argv[1], "http")) {
        CHECK(argc == 3);
        snprintf(config.local, sizeof(config.local), "%s", argv[2]);
        CHECK(read_pico_home_link_configure(&config));
        CHECK(read_pico_home_link_request(true, true));
        runtime_wait();
        CHECK(snapshot.result == RP_HOME_OK && snapshot.entity_count == 6);
        CHECK(read_pico_home_link_control(&snapshot.entities[0], "turn_off", 0, true));
        struct timespec t = {0, 800000000};
        nanosleep(&t, NULL);
        CHECK(!read_pico_home_link_request(false, false));
        CHECK(!read_pico_home_link_control(&snapshot.entities[0], "turn_off", 0, true));
        CHECK(read_pico_home_link_suspend(14000));
    } else if (!strcmp(argv[1], "mqtt")) {
        CHECK(argc == 4);
        config.ha_token[0] = 0;
        rp_home_printer_config_t *p = &config.printers[0];
        p->source = RP_HOME_BAMBU_LAN;
        p->port = (uint16_t)atoi(argv[2]);
        snprintf(p->host, 128, "localhost");
        snprintf(p->serial, 64, "FIXTURE123");
        snprintf(p->secret, 1024, "fixture-access-code");
        FILE *f = fopen(argv[3], "rb");
        CHECK(f);
        size_t n = fread(p->certificate, 1, sizeof(p->certificate) - 1, f);
        p->certificate[n] = 0;
        fclose(f);
        CHECK(read_pico_home_link_configure(&config));
        CHECK(read_pico_home_link_request(true, true));
        runtime_wait();
        if (snapshot.printers[0].result != RP_HOME_OK)
            fprintf(stderr, "MQTT fixture result=%d\n", snapshot.printers[0].result);
        CHECK(snapshot.printers[0].result == RP_HOME_OK && snapshot.printers[0].percent == 57);
        CHECK(read_pico_home_link_suspend(14000));
    } else if (!strcmp(argv[1], "storage-fail")) {
        rp_home_config_t before, after;
        read_pico_home_link_get_config(&before);
        CHECK(!read_pico_home_link_configure(&config));
        read_pico_home_link_get_config(&after);
        CHECK(!memcmp(&before, &after, sizeof(before)));
    } else
        CHECK(!"unknown mode");
    printf("home-link: real local %s transport passed\n", argv[1]);
    return 0;
}

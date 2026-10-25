/* Thin-host manage contract: 503 home_module_missing, no persistence, no Bambu session. */
#include "home_link_web.h"
#include "read_pico_home_link.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

int main(void) {
    static const char expect[] =
        "{\"ok\":false,\"error\":\"home_module_missing\",\"message\":\"Install TF tools/home-link.pga\"}";
    static const char *paths[] = {"/api/manage/home", "/api/manage/home/extra", "/api/manage/printers",
                                  "/api/manage/printer?slot=0", "/api/manage/bambu", "/api/manage/bambu/auth"};
    static const char *methods[] = {"GET", "PUT", "POST", "DELETE"};
    char out[256];
    rp_home_web_response_t response;
    for (unsigned p = 0; p < sizeof(paths) / sizeof(paths[0]); p++) {
        for (unsigned m = 0; m < sizeof(methods) / sizeof(methods[0]); m++) {
            rp_home_web_request_t request = {.method = methods[m], .path = paths[p], .body = "{}", .body_bytes = 2};
            memset(out, 0x5a, sizeof(out));
            assert(rp_home_web_handle(&request, &response, out, sizeof(out)));
            assert(response.http_status == 503);
            assert(response.body_bytes == sizeof(expect) - 1);
            assert(!strcmp(out, expect));
        }
    }
    rp_home_web_request_t other = {.method = "GET", .path = "/api/manage/ha/entities"};
    assert(!rp_home_web_handle(&other, &response, out, sizeof(out)));
    rp_home_config_t config = {.version = RP_HOME_CONFIG_VERSION, .enabled = 1};
    assert(!read_pico_home_link_configure(&config));
    assert(!read_pico_home_link_request(true, true));
    assert(!read_pico_home_link_control(NULL, "turn_on", 0, true));
    assert(read_pico_home_link_suspend(0));
    rp_home_bambu_input_t input;
    memset(&input, 0, sizeof(input));
    assert(!read_pico_bambu_auth_submit(&input, true));
    rp_home_bambu_state_t state;
    read_pico_bambu_auth_get(&state);
    assert(state.phase == RP_BAMBU_IDLE);
    assert(!read_pico_bambu_auth_prepare_import(1, 0, &config.printers[0]));
    printf("thin home-link: manage 503 home_module_missing, configure/control/bambu refused\n");
    return 0;
}

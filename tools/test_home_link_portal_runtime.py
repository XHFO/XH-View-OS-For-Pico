#!/usr/bin/env python3
"""Run the real ESP portal wrapper against deterministic HTTP/clock/memory stubs."""
from pathlib import Path
import gzip
import os
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
STUBS = {
    "esp_heap_caps.h": r"""
#pragma once
#include <stddef.h>
#include <stdlib.h>
#define MALLOC_CAP_SPIRAM 1
#define MALLOC_CAP_8BIT 2
void *heap_caps_calloc(size_t, size_t, unsigned);
""",
    "esp_timer.h": "#pragma once\n#include <stdint.h>\nint64_t esp_timer_get_time(void);\n",
    "esp_random.h": "#pragma once\n#include <stddef.h>\nvoid esp_fill_random(void *, size_t);\n",
    "freertos/FreeRTOS.h": "#pragma once\n#define portMAX_DELAY 0xffffffffu\n#define pdTRUE 1\n",
    "freertos/semphr.h": r"""
#pragma once
typedef void *SemaphoreHandle_t;
SemaphoreHandle_t xSemaphoreCreateMutex(void);
int xSemaphoreTake(SemaphoreHandle_t, unsigned);
int xSemaphoreGive(SemaphoreHandle_t);
""",
    "read_pico_connectivity.h": r"""
#pragma once
#include <stdbool.h>
#include <stddef.h>
#define READ_PICO_CONNECTIVITY_SAVED_MAX 8
typedef struct {
    bool connected, suspended, busy, transfer_active;
    char ip[48], ssid[33];
    size_t saved_count;
    struct { char ssid[33]; } saved[READ_PICO_CONNECTIVITY_SAVED_MAX];
} read_pico_connectivity_status_t;
void read_pico_connectivity_get_status(read_pico_connectivity_status_t *);
bool read_pico_connectivity_online(void);
""",
    "read_pico_management.h": "#pragma once\n#include <stdbool.h>\nbool read_pico_management_active(void);\n",
    "esp_http_server.h": r"""
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <sys/types.h>
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_FAIL -1
enum { HTTP_GET, HTTP_PUT, HTTP_DELETE, HTTP_POST };
typedef void *httpd_handle_t;
typedef struct httpd_req {
    const char *uri;
    int method;
    size_t content_len;
    const char *host, *origin, *key, *content_type, *body;
    size_t received;
    bool expire_during_receive, fail_receive;
    char response_status[64], response_type[64], response_body[16384], csp[256];
    bool no_store, no_sniff, no_referrer, gzip;
    size_t response_bytes;
} httpd_req_t;
typedef struct {
    const char *uri;
    int method;
    esp_err_t (*handler)(httpd_req_t *);
} httpd_uri_t;
typedef struct {
    unsigned server_port, ctrl_port, stack_size, max_uri_handlers;
    bool lru_purge_enable;
    unsigned recv_wait_timeout, send_wait_timeout;
} httpd_config_t;
#define HTTPD_DEFAULT_CONFIG() ((httpd_config_t){0})
esp_err_t httpd_start(httpd_handle_t *, const httpd_config_t *);
esp_err_t httpd_stop(httpd_handle_t);
esp_err_t httpd_register_uri_handler(httpd_handle_t, const httpd_uri_t *);
size_t httpd_req_get_hdr_value_len(httpd_req_t *, const char *);
esp_err_t httpd_req_get_hdr_value_str(httpd_req_t *, const char *, char *, size_t);
size_t httpd_req_get_url_query_len(httpd_req_t *);
esp_err_t httpd_req_get_url_query_str(httpd_req_t *, char *, size_t);
int httpd_req_recv(httpd_req_t *, char *, size_t);
esp_err_t httpd_resp_set_hdr(httpd_req_t *, const char *, const char *);
esp_err_t httpd_resp_set_status(httpd_req_t *, const char *);
esp_err_t httpd_resp_set_type(httpd_req_t *, const char *);
esp_err_t httpd_resp_send(httpd_req_t *, const char *, ssize_t);
""",
}


def main() -> None:
    subprocess.run(["python3", str(ROOT / "tools/build_home_portal_asset.py"), "--check"], check=True, cwd=ROOT)
    with tempfile.TemporaryDirectory(prefix="pico-home-portal-runtime-") as temporary:
        temp = Path(temporary)
        for name, source in STUBS.items():
            path = temp / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(source)
        binary = temp / "portal-runtime"
        command = [os.environ.get("CC", "cc"), "-std=c11", "-Wall", "-Wextra", "-Werror", "-g",
                   "-fsanitize=address,undefined", "-fno-omit-frame-pointer", "-I", str(temp),
                   "-I", str(ROOT / "components/read_pico_home_link/include"),
                   "-I", str(ROOT / "components/read_pico_apps/include"),
                   str(ROOT / "tools/home_link_portal_runtime_host_test.c"), "-o", str(binary)]
        subprocess.run(command, check=True, cwd=ROOT)
        subprocess.run([str(binary)], check=True, cwd=ROOT)
        encoded = subprocess.check_output([str(binary), "--html-gzip"], cwd=ROOT)
        decoded = gzip.decompress(encoded)
        assert decoded == (ROOT / "components/read_pico_home_link/assets/home_link_portal.html").read_bytes()
        decoded.decode("utf-8", errors="strict")
        html = temp / "portal-response.html"
        html.write_bytes(decoded)
        subprocess.run([os.environ.get("NODE", "node"), str(ROOT / "tools/home_link_portal_frontend_test.js"),
                        str(html)], check=True, cwd=ROOT)


if __name__ == "__main__":
    main()

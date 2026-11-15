#!/usr/bin/env python3
"""Run unchanged production PGA functions and real packing with injected NVS failures."""
import argparse
import json
import os
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
STUBS = {
    "read_pico_connectivity.h": "#pragma once\n#include <stdbool.h>\ntypedef struct { bool online; char ssid[33]; } read_pico_connectivity_link_t;\nvoid read_pico_connectivity_get_link(read_pico_connectivity_link_t *);\n",
    "pico_system.h": "#pragma once\nvoid ps_toast(const char *);\nint ps_wifi_connected_index(void);\nconst char *ps_wifi_name(int);\n",
    "pico_datetime.h": """
#pragma once
#include <stdint.h>
enum { PS_TIME_NONE, PS_TIME_RTC, PS_TIME_MANUAL, PS_TIME_NETWORK };
typedef struct { uint8_t source; } ps_datetime_status_t;
void ps_datetime_get_status(ps_datetime_status_t *);
""",
    "read_pico_management.h": "#pragma once\n#include <stdbool.h>\nbool read_pico_management_active(void);\n",
    "esp_crt_bundle.h": "#pragma once\nint esp_crt_bundle_attach(void *);\n",
    "esp_heap_caps.h": """
#pragma once
#include <stddef.h>
#define MALLOC_CAP_SPIRAM 1u
#define MALLOC_CAP_8BIT 2u
void *heap_caps_calloc(size_t, size_t, unsigned);
""",
    "esp_log.h": "#pragma once\n",
    "esp_timer.h": "#pragma once\n#include <stdint.h>\nint64_t esp_timer_get_time(void);\n",
    "freertos/FreeRTOS.h": """
#pragma once
#define portMAX_DELAY 0xffffffffu
#define pdTRUE 1
#define pdPASS 1
#define pdMS_TO_TICKS(ms) (ms)
""",
    "freertos/semphr.h": """
#pragma once
typedef void *SemaphoreHandle_t;
SemaphoreHandle_t xSemaphoreCreateMutex(void);
int xSemaphoreTake(SemaphoreHandle_t, unsigned);
int xSemaphoreGive(SemaphoreHandle_t);
""",
    "freertos/task.h": """
#pragma once
#include <stdint.h>
typedef void *TaskHandle_t;
int xTaskCreate(void (*)(void *), const char *, unsigned, void *, unsigned, TaskHandle_t *);
unsigned ulTaskNotifyTake(int, unsigned);
void xTaskNotifyGive(TaskHandle_t);
void vTaskDelete(TaskHandle_t);
void vTaskDelay(unsigned);
""",
    "nvs.h": """
#pragma once
#include <stddef.h>
typedef int nvs_handle_t;
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_NVS_NOT_FOUND -2
#define NVS_READWRITE 1
#define NVS_READONLY 0
esp_err_t nvs_open(const char *, int, nvs_handle_t *);
esp_err_t nvs_set_blob(nvs_handle_t, const char *, const void *, size_t);
esp_err_t nvs_get_blob(nvs_handle_t, const char *, void *, size_t *);
esp_err_t nvs_commit(nvs_handle_t);
void nvs_close(nvs_handle_t);
""",
    "esp_http_client.h": """
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "nvs.h"
enum { HTTP_METHOD_GET, HTTP_METHOD_POST };
typedef void *esp_http_client_handle_t;
typedef struct {
    const char *url;
    int timeout_ms, buffer_size;
    int (*crt_bundle_attach)(void *);
    bool disable_auto_redirect;
    int method;
} esp_http_client_config_t;
esp_http_client_handle_t esp_http_client_init(const esp_http_client_config_t *);
esp_err_t esp_http_client_set_header(esp_http_client_handle_t, const char *, const char *);
esp_err_t esp_http_client_open(esp_http_client_handle_t, int);
int esp_http_client_write(esp_http_client_handle_t, const char *, int);
int64_t esp_http_client_fetch_headers(esp_http_client_handle_t);
int esp_http_client_get_status_code(esp_http_client_handle_t);
int esp_http_client_read(esp_http_client_handle_t, char *, int);
bool esp_http_client_is_complete_data_received(esp_http_client_handle_t);
esp_err_t esp_http_client_close(esp_http_client_handle_t);
esp_err_t esp_http_client_cleanup(esp_http_client_handle_t);
""",
}


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, default=ROOT / "main/system/pico_home_pga.c")
    parser.add_argument("--case", choices=("all", "pga", "portal", "invalid", "migration", "worker", "network", "discovery", "printer", "rooms", "share"), default="all")
    parser.add_argument("--report", type=Path)
    args = parser.parse_args()
    report = {"source": str(args.source.resolve()), "case": args.case,
              "sanitizers": ["address", "undefined"], "passed": False}
    with tempfile.TemporaryDirectory(prefix="pico-home-pga-transaction-") as temporary:
        temp = Path(temporary)
        for name, source in STUBS.items():
            target = temp / name
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_text(source)
        # Keep the production bytes untouched; isolate only platform includes in the temporary directory.
        (temp / "pico_home_pga_under_test.c").write_bytes(args.source.read_bytes())
        (temp / "pico_home_pga.h").write_bytes((ROOT / "main/system/pico_home_pga.h").read_bytes())
        (temp / "pico_home_share.inc").write_bytes((ROOT / "main/system/pico_home_share.inc").read_bytes())
        binary = temp / "home-pga-transaction"
        component = ROOT / "components/read_pico_home_link"
        command = [os.environ.get("CC", "cc"), "-std=gnu11", "-O1", "-g", "-Wall", "-Wextra", "-Werror",
                   "-fsanitize=address,undefined", "-fno-omit-frame-pointer", "-I", str(temp),
                   "-I", str(component / "include"), "-I", str(ROOT / "components/read_pico_apps/include"),
                   "-I", str(ROOT / "managed_components/espressif__cjson/cJSON"),
                   str(ROOT / "tools/home_pga_transaction_host_test.c"),
                   str(component / "home_link_model.c"), str(component / "home_link_json.c"),
                   "-lm", "-o", str(binary)]
        results = []
        for phase, cmd in (("compile", command), ("exercise", [str(binary), args.case])):
            result = subprocess.run(cmd, cwd=ROOT, text=True, capture_output=True)
            results.append({"phase": phase, "returncode": result.returncode,
                            "stdout": result.stdout, "stderr": result.stderr})
            if result.stdout:
                print(result.stdout, end="")
            if result.stderr:
                print(result.stderr, end="")
            if result.returncode:
                break
        report["results"] = results
        report["passed"] = len(results) == 2 and all(r["returncode"] == 0 for r in results)
    if args.report:
        args.report.parent.mkdir(parents=True, exist_ok=True)
        args.report.write_text(json.dumps(report, indent=2) + "\n")
    if not report["passed"]:
        raise SystemExit(1)


if __name__ == "__main__":
    main()

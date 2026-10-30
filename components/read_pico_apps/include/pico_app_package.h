/* SPDX-License-Identifier: Apache-2.0
 * 中文：PGA1单文件应用包的有界目录；校验不执行代码。
 * English: Bounded PGA1 single-file app catalog; validation never executes code.
 */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define PICO_APP_HEADER_BYTES 512u
#define PICO_APP_PACKAGE_MAX (4u*1024u*1024u)
#define PICO_APP_CODE_MAX (2u*1024u*1024u)
#define PICO_APP_MAX_PAYLOADS 4u
#define PICO_APP_CATALOG_MAX 24u
#define PICO_APP_DIRECTORY "/sdcard/apps/games"
#define PICO_APP_TOOLS_DIRECTORY "/sdcard/apps/tools"

typedef struct {
    char target[16];
    uint32_t offset,bytes,crc;
} pico_app_payload_t;
typedef struct {
    char id[32],title[64],description[96];
    uint32_t abi,version,kind,flags,bytes,payload_count;
    uint8_t source_sha256[32];
    pico_app_payload_t payloads[PICO_APP_MAX_PAYLOADS];
} pico_app_package_t;

bool pico_app_id_valid(const char *id);
const char *pico_app_target(void);
uint32_t pico_app_crc32(const void *data,size_t bytes);
bool pico_app_package_header(const uint8_t *header,size_t bytes,uint32_t file_bytes,
                             pico_app_package_t *out,char *error,size_t error_bytes);
bool pico_app_package_read(const char *path,pico_app_package_t *out,bool verify_payloads,
                           char *error,size_t error_bytes);
const pico_app_payload_t *pico_app_package_payload(const pico_app_package_t *package);

/* SPDX-License-Identifier: Apache-2.0
 * 中文：应用包有界校验；只读取头与校验流，不载入或执行代码。
 * English: Bounded package validation; headers and checksum streams never load or execute code.
 */
#include "pico_app_package.h"
#include "pico_app_sdk.h"
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

static uint32_t rd32(const uint8_t *p) { return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24; }
static bool fail(char *error,size_t n,const char *message) { if(error&&n)snprintf(error,n,"%s",message);return false; }
static uint32_t crc_feed(uint32_t crc,const uint8_t *p,size_t n) {
    while(n--){crc^=*p++;for(int i=0;i<8;i++)crc=(crc>>1)^(0xedb88320u&-(crc&1u));}return crc;
}
uint32_t pico_app_crc32(const void *data,size_t bytes) { return ~crc_feed(UINT32_MAX,data,bytes); }
bool pico_app_id_valid(const char *id) {
    if(!id||!id[0])return false;
    size_t n=0;for(;id[n];n++)if(n>=31||!((id[n]>='a'&&id[n]<='z')||(id[n]>='0'&&id[n]<='9')||id[n]=='-'))return false;
    return id[0]!='-'&&id[n-1]!='-';
}
const char *pico_app_target(void) {
#if defined(ESP_PLATFORM)
    return "esp32s3-xtensa";
#elif defined(__APPLE__) && defined(__aarch64__)
    return "macos-arm64";
#elif defined(__APPLE__)
    return "macos-x86_64";
#elif defined(__aarch64__)
    return "linux-arm64";
#else
    return "linux-x86_64";
#endif
}
static bool all_zero(const uint8_t *src,size_t bytes) {
    for(size_t i=0;i<bytes;i++)if(src[i])return false;
    return true;
}
static bool utf8_text(const uint8_t *src,size_t bytes) {
    // 拒绝过长编码、代理项、越界码位和截断；仅保留原有ASCII控制字符限制。
    // Reject overlong forms, surrogates, out-of-range scalars and truncation, retaining the ASCII-control restriction.
    for(size_t i=0;i<bytes;){
        uint8_t lead=src[i++];
        if(lead<0x80){if(lead<32||lead==127)return false;continue;}
        unsigned count;uint8_t lower=0x80,upper=0xbf;
        if(lead>=0xc2&&lead<=0xdf)count=1;
        else if(lead>=0xe0&&lead<=0xef){count=2;if(lead==0xe0)lower=0xa0;else if(lead==0xed)upper=0x9f;}
        else if(lead>=0xf0&&lead<=0xf4){count=3;if(lead==0xf0)lower=0x90;else if(lead==0xf4)upper=0x8f;}
        else return false;
        if(count>bytes-i||src[i]<lower||src[i]>upper)return false;
        i++;
        while(--count){if(src[i]<0x80||src[i]>0xbf)return false;i++;}
    }
    return true;
}
static bool field(char *dst,size_t size,const uint8_t *src) {
    const uint8_t *end=memchr(src,0,size);if(!end||end==src)return false;
    size_t len=(size_t)(end-src);
    if(!utf8_text(src,len)||!all_zero(end,size-len))return false;
    memcpy(dst,src,len);dst[len]=0;return true;
}
bool pico_app_package_header(const uint8_t *h,size_t bytes,uint32_t file_bytes,pico_app_package_t *out,char *error,size_t error_bytes) {
    if(!h||!out||bytes!=PICO_APP_HEADER_BYTES)return fail(error,error_bytes,"应用包头不完整");
    if(memcmp(h,"PICOAPP1",8)||rd32(h+8)!=1||rd32(h+12)!=PICO_APP_HEADER_BYTES)return fail(error,error_bytes,"应用包格式不支持");
    if(file_bytes<PICO_APP_HEADER_BYTES||file_bytes>PICO_APP_PACKAGE_MAX||rd32(h+16)!=file_bytes)return fail(error,error_bytes,"应用包长度异常");
    uint8_t copy[PICO_APP_HEADER_BYTES];memcpy(copy,h,sizeof(copy));memset(copy+232,0,4);
    if(pico_app_crc32(copy,sizeof(copy))!=rd32(h+232))return fail(error,error_bytes,"应用包头校验失败");
    if(rd32(h+224)||!all_zero(h+236,20)||!all_zero(h+384,64)||!all_zero(h+480,32))return fail(error,error_bytes,"应用包保留字段无效");
    memset(out,0,sizeof(*out));
    if(!field(out->id,sizeof(out->id),h+32)||!pico_app_id_valid(out->id)||!field(out->title,sizeof(out->title),h+64)||!field(out->description,sizeof(out->description),h+128))return fail(error,error_bytes,"应用信息无效");
    out->abi=rd32(h+20);out->version=rd32(h+24);out->kind=rd32(h+28);out->flags=rd32(h+224);out->bytes=file_bytes;out->payload_count=rd32(h+228);
    memcpy(out->source_sha256,h+448,32);
    if(out->abi!=PICO_APP_ABI)return fail(error,error_bytes,"应用需要其他版本的系统接口");
    if(!out->version||out->kind>255||out->payload_count<1||out->payload_count>PICO_APP_MAX_PAYLOADS)return fail(error,error_bytes,"应用版本或内容目录异常");
    if(!all_zero(h+256+out->payload_count*32,(PICO_APP_MAX_PAYLOADS-out->payload_count)*32))return fail(error,error_bytes,"应用包未用目录必须留空");
    uint32_t cursor=PICO_APP_HEADER_BYTES;
    for(uint32_t i=0;i<out->payload_count;i++){
        const uint8_t *p=h+256+i*32;pico_app_payload_t *entry=&out->payloads[i];
        if(!field(entry->target,sizeof(entry->target),p))return fail(error,error_bytes,"应用目标平台无效");
        for(const char *t=entry->target;*t;t++)if(!((*t>='a'&&*t<='z')||(*t>='0'&&*t<='9')||*t=='-'||*t=='_'))return fail(error,error_bytes,"应用目标平台无效");
        entry->offset=rd32(p+16);entry->bytes=rd32(p+20);entry->crc=rd32(p+24);
        if(rd32(p+28))return fail(error,error_bytes,"应用代码保留字段无效");
        if(entry->offset!=cursor||!entry->bytes||entry->bytes>PICO_APP_CODE_MAX||entry->bytes>file_bytes-cursor)return fail(error,error_bytes,"应用代码范围异常");
        for(uint32_t j=0;j<i;j++)if(!strcmp(entry->target,out->payloads[j].target))return fail(error,error_bytes,"应用平台重复");
        cursor+=entry->bytes;
    }
    if(cursor!=file_bytes)return fail(error,error_bytes,"应用包有未声明内容");
    if(error&&error_bytes)error[0]=0;
    return true;
}
bool pico_app_package_read(const char *path,pico_app_package_t *out,bool verify_payloads,char *error,size_t error_bytes) {
    if(error&&error_bytes)error[0]=0;
    struct stat st;if(!path||stat(path,&st)!=0||!S_ISREG(st.st_mode)||st.st_size<0||st.st_size>PICO_APP_PACKAGE_MAX)return fail(error,error_bytes,"无法读取应用包");
    FILE *f=fopen(path,"rb");if(!f)return fail(error,error_bytes,"无法打开应用包");
    uint8_t header[PICO_APP_HEADER_BYTES];bool ok=fread(header,1,sizeof(header),f)==sizeof(header)&&pico_app_package_header(header,sizeof(header),(uint32_t)st.st_size,out,error,error_bytes);
    if(ok&&verify_payloads){
        uint8_t block[1024];
        for(uint32_t i=0;ok&&i<out->payload_count;i++){
            uint32_t remaining=out->payloads[i].bytes,crc=UINT32_MAX;
            while(remaining){size_t n=remaining>sizeof(block)?sizeof(block):remaining;if(fread(block,1,n,f)!=n){ok=false;break;}crc=crc_feed(crc,block,n);remaining-=(uint32_t)n;}
            if(!ok||~crc!=out->payloads[i].crc)ok=fail(error,error_bytes,"应用代码校验失败，请重新导入");
        }
    }
    if(fclose(f)!=0&&ok)ok=fail(error,error_bytes,"应用读取失败");
    if(!ok&&error&&error_bytes&&!error[0])fail(error,error_bytes,"应用包不完整");
    return ok;
}
const pico_app_payload_t *pico_app_package_payload(const pico_app_package_t *p) {
    if(p)for(uint32_t i=0;i<p->payload_count;i++)if(!strcmp(p->payloads[i].target,pico_app_target()))return &p->payloads[i];
    return NULL;
}

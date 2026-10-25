/* SPDX-License-Identifier: Apache-2.0
 * 中文：TF应用包边界与流校验测试，不执行包内代码。
 * English: TF app package bounds and streaming checksum tests, without executing payloads.
 */
#include "pico_app_package.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static void put(uint8_t *p,uint32_t n){for(int i=0;i<4;i++)p[i]=(uint8_t)(n>>(8*i));}
static uint8_t header[512];static pico_app_package_t p;static char error[160];static unsigned checks;
static void seal(void){put(header+232,0);put(header+232,pico_app_crc32(header,512));}
static bool valid(uint32_t bytes){checks++;return pico_app_package_header(header,sizeof(header),bytes,&p,error,sizeof(error));}
static void base(void){memset(header,0,512);memcpy(header,"PICOAPP1",8);put(header+8,1);put(header+12,512);put(header+16,516);put(header+20,1);put(header+24,10000);put(header+28,3);memcpy(header+32,"chess",6);memcpy(header+64,"Chess",6);memcpy(header+128,"Board game",11);put(header+228,1);memcpy(header+256,pico_app_target(),strlen(pico_app_target())+1);put(header+272,512);put(header+276,4);put(header+280,pico_app_crc32("test",4));seal();}
static void many_payloads(unsigned count){
    base();put(header+16,512+count*4);put(header+228,count);
    for(unsigned i=1;i<count;i++){
        uint8_t *entry=header+256+i*32;char target[16];snprintf(target,sizeof(target),"extra-%u",i);
        memcpy(entry,target,strlen(target)+1);put(entry+16,512+i*4);put(entry+20,4);put(entry+24,pico_app_crc32("test",4));
    }
    seal();
}
static void canonical_header_checks(void){
    // 每个保留字节逐个扰动并重算CRC，保证失败来自规范校验。
    // Mutate every reserved byte and reseal CRC so rejection exercises canonical validation.
    const unsigned ranges[][2]={{224,4},{236,20},{384,64},{480,32}};
    for(size_t r=0;r<sizeof(ranges)/sizeof(ranges[0]);r++)for(unsigned j=0;j<ranges[r][1];j++){
        base();header[ranges[r][0]+j]=1;seal();assert(!valid(516));
    }
    for(unsigned count=1;count<=4;count++){
        many_payloads(count);assert(valid(512+count*4));
        for(unsigned i=0;i<count;i++)for(unsigned j=28;j<32;j++){
            many_payloads(count);header[256+i*32+j]=1;seal();assert(!valid(512+count*4));
        }
        for(unsigned offset=256+count*32;offset<384;offset++){
            many_payloads(count);header[offset]=1;seal();assert(!valid(512+count*4));
        }
    }
    const unsigned fields[][2]={{32,32},{64,64},{128,96},{256,16}};
    for(size_t i=0;i<sizeof(fields)/sizeof(fields[0]);i++){
        base();size_t length=strlen((char*)header+fields[i][0]);
        for(size_t j=length+1;j<fields[i][1];j++){
            base();header[fields[i][0]+j]=1;seal();assert(!valid(516));
        }
        base();memset(header+fields[i][0],'x',fields[i][1]);seal();assert(!valid(516));
    }
    base();memset(header+64,'a',63);memset(header+128,'b',95);seal();assert(valid(516));
    assert(strlen(p.title)==63&&strlen(p.description)==95);checks++;
}
static void utf8_checks(void){
    static const struct{uint8_t bytes[5];size_t size;} invalid[]={
        {{0x80},1},{{0xbf},1},{{0xc0,0xaf},2},{{0xc1,0xbf},2},{{0xc2},1},{{0xc2,0x20},2},
        {{0xdf,0xc0},2},{{0xe0,0x80,0x80},3},{{0xe0,0x9f,0xbf},3},{{0xe1,0x80},2},
        {{0xed,0xa0,0x80},3},{{0xed,0xbf,0xbf},3},{{0xee,0x80},2},
        {{0xf0,0x80,0x80,0x80},4},{{0xf0,0x8f,0xbf,0xbf},4},{{0xf1,0x80,0x80},3},
        {{0xf4,0x90,0x80,0x80},4},{{0xf5,0x80,0x80,0x80},4},{{0xfe},1},{{0xff},1},
        {{10},1},{{127},1}
    };
    static const struct{uint8_t bytes[5];size_t size;} supported[]={
        {{0xc2,0x80},2},{{0xdf,0xbf},2},{{0xe0,0xa0,0x80},3},{{0xed,0x9f,0xbf},3},
        {{0xee,0x80,0x80},3},{{0xef,0xbf,0xbf},3},{{0xf0,0x90,0x80,0x80},4},{{0xf4,0x8f,0xbf,0xbf},4}
    };
    const unsigned fields[][2]={{64,64},{128,96}};
    for(size_t field=0;field<sizeof(fields)/sizeof(fields[0]);field++){
        for(size_t i=0;i<sizeof(invalid)/sizeof(invalid[0]);i++){
            base();memset(header+fields[field][0],0,fields[field][1]);
            memcpy(header+fields[field][0],invalid[i].bytes,invalid[i].size);seal();assert(!valid(516));
        }
        for(size_t i=0;i<sizeof(supported)/sizeof(supported[0]);i++){
            base();memset(header+fields[field][0],0,fields[field][1]);
            memcpy(header+fields[field][0],supported[i].bytes,supported[i].size);seal();assert(valid(516));
        }
        // 在字段最后一个非NUL字节截断多字节字符。/ Truncate a multibyte character at the last non-NUL field byte.
        base();memset(header+fields[field][0],'a',fields[field][1]-1);
        header[fields[field][0]+fields[field][1]-2]=0xc2;seal();assert(!valid(516));
    }
    base();memset(header+64,0,64);memset(header+128,0,96);
    memcpy(header+64,"中文棋局",12);memcpy(header+128,"四种棋类 \xf0\x9f\x8e\xae",17);seal();assert(valid(516));
}
int main(int argc,char **argv){
    assert(argc==2);assert(pico_app_crc32("123456789",9)==0xcbf43926u);checks++;
    base();assert(valid(516));assert(!strcmp(p.id,"chess")&&p.version==10000&&p.kind==3);assert(pico_app_package_payload(&p));checks++;
    header[35]^=1;assert(!valid(516));base();put(header+20,2);seal();assert(!valid(516));
    base();put(header+272,UINT32_MAX);seal();assert(!valid(516));base();put(header+276,UINT32_MAX);seal();assert(!valid(516));
    base();put(header+16,PICO_APP_PACKAGE_MAX+1);seal();assert(!valid(PICO_APP_PACKAGE_MAX+1));base();assert(!valid(515));
    base();put(header+228,5);seal();assert(!valid(516));base();put(header+228,0);seal();assert(!valid(516));
    base();memset(header+32,'x',32);seal();assert(!valid(516));base();memcpy(header+32,"../evil",8);seal();assert(!valid(516));
    base();memcpy(header+32,"-chess",7);seal();assert(!valid(516));base();memcpy(header+32,"Chess",6);seal();assert(!valid(516));
    base();put(header+16,520);put(header+228,2);memcpy(header+288,header+256,32);put(header+304,516);seal();assert(!valid(520));
    base();put(header+16,517);seal();assert(!valid(517));
    canonical_header_checks();utf8_checks();
    base();FILE*f=fopen(argv[1],"wb");assert(f);assert(fwrite(header,1,512,f)==512);assert(fwrite("test",1,4,f)==4);assert(!fclose(f));
    assert(pico_app_package_read(argv[1],&p,true,error,sizeof(error)));checks++;
    f=fopen(argv[1],"r+b");assert(f);assert(!fseek(f,515,SEEK_SET));assert(fputc('X',f)!=EOF);assert(!fclose(f));assert(!pico_app_package_read(argv[1],&p,true,error,sizeof(error)));checks++;
    assert(pico_app_package_read(argv[1],&p,false,error,sizeof(error)));checks++;
    printf("PGA1 parser: %u checks PASS\n",checks);return 0;
}

/* SPDX-License-Identifier: Apache-2.0
 * 中文：TF游戏代码加载器；先严格验证ELF表与重定位，再交给固定版本加载器。
 * English: TF game-code loader; validate ELF tables and relocations before the pinned loader.
 * 冻结：不注册固件全局符号、不执行构造器；调用方停止并join工作任务后才可卸载。
 * Frozen: No firmware-global exports or constructors; callers stop and join workers before unload.
 */
#ifndef ESP_PLATFORM
#define _POSIX_C_SOURCE 200809L
#endif
#include "pico_app_loader.h"
#include "pico_app_package.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>

#ifdef ESP_PLATFORM
#include "esp_elf.h"
#include "sdkconfig.h"
#include "soc/soc.h"
#if !CONFIG_IDF_TARGET_ESP32S3 || !CONFIG_ELF_LOADER_BUS_ADDRESS_MIRROR || !CONFIG_ELF_LOADER_LOAD_PSRAM
#error TF application loading requires the ESP32-S3 PSRAM ELF profile
#endif
#else
#include <dlfcn.h>
#include <errno.h>
#include <unistd.h>
#endif

/* ---- 独立ELF32线格式 / Independent ELF32 wire format ---- */
typedef struct {
    uint8_t ident[16];uint16_t type,machine;uint32_t version,entry,phoff,shoff,flags;
    uint16_t ehsize,phentsize,phnum,shentsize,shnum,shstrndx;
} pga_ehdr_t;
typedef struct {uint32_t type,offset,vaddr,paddr,filesz,memsz,flags,align;} pga_phdr_t;
typedef struct {uint32_t name,type,flags,addr,offset,size,link,info,align,entsize;} pga_shdr_t;
typedef struct {uint32_t name,value,size;uint8_t info,other;uint16_t shndx;} pga_sym_t;
typedef struct {uint32_t offset,info;int32_t addend;} pga_rela_t;
_Static_assert(sizeof(pga_ehdr_t)==52,"ELF32 header size");
_Static_assert(sizeof(pga_phdr_t)==32,"ELF32 program-header size");
_Static_assert(sizeof(pga_shdr_t)==40,"ELF32 section-header size");
_Static_assert(sizeof(pga_sym_t)==16,"ELF32 symbol size");
_Static_assert(sizeof(pga_rela_t)==12,"ELF32 relocation size");
enum { PGA_ELF_SECTIONS=96,PGA_ELF_RELOCS=8192,PGA_ELF_MAPPED_MAX=2*1024*1024 };
enum { PGA_S_NULL=0,PGA_S_PROGBITS=1,PGA_S_SYMTAB=2,PGA_S_STRTAB=3,
       PGA_S_RELA=4,PGA_S_HASH=5,PGA_S_DYNAMIC=6,PGA_S_NOBITS=8,PGA_S_REL=9,PGA_S_DYNSYM=11 };
enum { PGA_F_WRITE=1,PGA_F_ALLOC=2,PGA_F_EXEC=4,PGA_F_TLS=1024 };
typedef struct {pga_shdr_t section;unsigned index;} pga_mapped_t;
typedef struct {
    pga_ehdr_t header;pga_shdr_t sections[PGA_ELF_SECTIONS];
    pga_mapped_t mapped[5];size_t mapped_count;
} pga_checked_t;

static bool pga_error(char *error,size_t size,const char *message)
{
    if(error&&size)snprintf(error,size,"%s",message);
    return false;
}
static bool pga_span(size_t offset,size_t length,size_t total)
{
    return offset<=total&&length<=total-offset;
}
static bool pga_string(const uint8_t *image,const pga_shdr_t *strings,uint32_t offset)
{
    return offset<strings->size&&memchr(image+strings->offset+offset,0,strings->size-offset)!=NULL;
}
static bool pga_within(uint32_t value,size_t bytes,uint32_t base,size_t size)
{
    return value>=base&&pga_span((size_t)(value-base),bytes,size);
}
static int pga_slot(const char *name)
{
    static const char *const names[]={".text",".bss",".data",".rodata",".data.rel.ro"};
    for(int i=0;i<5;i++)if(strcmp(name,names[i])==0)return i;
    return -1;
}
static const pga_shdr_t *pga_section_at(const pga_checked_t *check,uint32_t address,size_t bytes,bool file_backed)
{
    for(size_t i=0;i<check->mapped_count;i++){
        const pga_shdr_t *section=&check->mapped[i].section;
        if((!file_backed||section->type!=PGA_S_NOBITS)&&pga_within(address,bytes,section->addr,section->size))return section;
    }
    return NULL;
}

static bool pga_check_xtensa(const uint8_t *image,size_t bytes,pga_checked_t *check,char *error,size_t error_bytes)
{
#define PGA_REQUIRE(condition,message) do{if(!(condition))return pga_error(error,error_bytes,message);}while(0)
    PGA_REQUIRE(image&&bytes>=sizeof(pga_ehdr_t)&&bytes<=PICO_APP_CODE_MAX,"ELF payload size is invalid");
    memset(check,0,sizeof(*check));memcpy(&check->header,image,sizeof(check->header));
    const pga_ehdr_t *header=&check->header;
    PGA_REQUIRE(memcmp(header->ident,"\177ELF",4)==0&&header->ident[4]==1&&header->ident[5]==1&&header->ident[6]==1,
                "Expected little-endian ELF32");
    PGA_REQUIRE(header->type==3&&header->machine==94&&header->version==1&&header->ehsize==52,
                "Expected an ESP32-S3 Xtensa shared ELF");
    PGA_REQUIRE(header->shnum>1&&header->shnum<=PGA_ELF_SECTIONS&&header->shentsize==40&&header->shstrndx<header->shnum,
                "ELF section table is invalid");
    PGA_REQUIRE(!(header->shoff&3)&&pga_span(header->shoff,(size_t)header->shnum*40,bytes),"ELF section table is truncated");
    PGA_REQUIRE(header->phnum<=16&&(!header->phnum||(header->phentsize==32&&!(header->phoff&3)&&
                pga_span(header->phoff,(size_t)header->phnum*32,bytes))),"ELF program table is invalid");
    memcpy(check->sections,image+header->shoff,(size_t)header->shnum*40);
    const pga_shdr_t *strings=&check->sections[header->shstrndx];
    PGA_REQUIRE(strings->type==PGA_S_STRTAB&&strings->size&&pga_span(strings->offset,strings->size,bytes),"ELF section names are invalid");
    for(unsigned i=0;i<header->phnum;i++){
        pga_phdr_t program;memcpy(&program,image+header->phoff+(size_t)i*32,32);
        PGA_REQUIRE(program.type!=3&&program.type!=7,"ELF interpreters and TLS are unsupported");
        PGA_REQUIRE(pga_span(program.offset,program.filesz,bytes)&&
                    (program.type!=1||(program.filesz<=program.memsz&&program.memsz<=PGA_ELF_MAPPED_MAX&&
                     program.vaddr<=UINT32_MAX-program.memsz)),"ELF segment is out of bounds");
    }
    bool seen[5]={0};size_t mapped_bytes=0;const pga_shdr_t *text=NULL;
    for(unsigned i=0;i<header->shnum;i++){
        const pga_shdr_t *section=&check->sections[i];
        PGA_REQUIRE(pga_string(image,strings,section->name),"ELF section name is unterminated");
        const char *name=(const char*)image+strings->offset+section->name;
        PGA_REQUIRE(section->type==PGA_S_NOBITS||pga_span(section->offset,section->size,bytes),"ELF section is truncated");
        PGA_REQUIRE(section->size<=PGA_ELF_MAPPED_MAX&&section->addr<=UINT32_MAX-section->size,"ELF section is too large");
        PGA_REQUIRE(!section->align||(section->align<=4096&&(section->align&(section->align-1))==0),"ELF section alignment is invalid");
        PGA_REQUIRE(!(section->flags&PGA_F_TLS)&&section->type!=PGA_S_REL,"ELF TLS and implicit relocations are unsupported");
        PGA_REQUIRE(strcmp(name,".init_array")&&strcmp(name,".fini_array")&&strcmp(name,".ctors")&&strcmp(name,".dtors"),"ELF constructors are unsupported");
        int slot=pga_slot(name);
        if(slot>=0){
            PGA_REQUIRE(!seen[slot],"ELF contains duplicate runtime sections");seen[slot]=true;
            PGA_REQUIRE((section->flags&PGA_F_ALLOC)&&section->type==(slot==1?PGA_S_NOBITS:PGA_S_PROGBITS),"ELF runtime section type is invalid");
            PGA_REQUIRE(slot!=0||(section->flags&PGA_F_EXEC),"ELF text is not executable");
            PGA_REQUIRE(slot!=0||section->align<=4,"ELF text requires unsupported alignment");
            PGA_REQUIRE((slot!=1&&slot!=2)||(section->flags&PGA_F_WRITE),"ELF writable section flags are invalid");
            PGA_REQUIRE(!(section->addr&3)&&!(section->offset&3),"ELF runtime section is unaligned");
            if(section->size){
                PGA_REQUIRE(check->mapped_count<5,"ELF runtime section limit exceeded");
                check->mapped[check->mapped_count++]=(pga_mapped_t){*section,i};
                size_t allocation=section->size+(section->align?section->align-1:0);
                PGA_REQUIRE(allocation<=PGA_ELF_MAPPED_MAX-mapped_bytes,"ELF runtime memory limit exceeded");mapped_bytes+=allocation;
            }
            if(slot==0){
                text=section;
                PGA_REQUIRE(section->size&&section->size<=UINT32_MAX-3&&
                            section->addr<=UINT32_MAX-((section->size+3u)&~3u)&&
                            pga_span(section->offset,(section->size+3u)&~3u,bytes),"ELF text padding is truncated");
            }
        }else if((section->flags&PGA_F_ALLOC)&&section->size){
            bool metadata=(section->type==PGA_S_DYNSYM&&strcmp(name,".dynsym")==0)||
                (section->type==PGA_S_STRTAB&&strcmp(name,".dynstr")==0)||
                (section->type==PGA_S_HASH&&strcmp(name,".hash")==0)||
                (section->type==PGA_S_DYNAMIC&&strcmp(name,".dynamic")==0)||
                (section->type==PGA_S_RELA&&strcmp(name,".rela.dyn")==0);
            PGA_REQUIRE(metadata,"ELF contains an unsupported allocated section");
        }
    }
    PGA_REQUIRE(text&&pga_within(header->entry,1,text->addr,text->size),"ELF entry is outside executable text");
    for(size_t i=0;i<check->mapped_count;i++)for(size_t j=i+1;j<check->mapped_count;j++){
        const pga_shdr_t *a=&check->mapped[i].section,*b=&check->mapped[j].section;
        size_t asize=(a->flags&PGA_F_EXEC)?((a->size+3u)&~3u):a->size;
        size_t bsize=(b->flags&PGA_F_EXEC)?((b->size+3u)&~3u):b->size;
        PGA_REQUIRE((uint64_t)a->addr+asize<=b->addr||(uint64_t)b->addr+bsize<=a->addr,"ELF runtime sections overlap");
    }
    size_t relocations=0;bool entry_symbol=false;bool relocation_table=false;
    for(unsigned i=0;i<header->shnum;i++){
        const pga_shdr_t *section=&check->sections[i];
        if(section->type==PGA_S_SYMTAB||section->type==PGA_S_DYNSYM){
            PGA_REQUIRE(section->entsize==16&&!(section->size%16)&&!(section->offset&3)&&
                        section->link<header->shnum&&check->sections[section->link].type==PGA_S_STRTAB,"ELF symbol table is invalid");
            const pga_shdr_t *names=&check->sections[section->link];
            PGA_REQUIRE(section->size>=16,"ELF symbol table is empty");
            pga_sym_t zero={0};
            PGA_REQUIRE(memcmp(image+section->offset,&zero,16)==0,"ELF reserved symbol must be null");
            for(size_t k=0;k<section->size/16;k++){
                pga_sym_t symbol;memcpy(&symbol,image+section->offset+k*16,16);
                PGA_REQUIRE(pga_string(image,names,symbol.name),"ELF symbol name is unterminated");
                PGA_REQUIRE(symbol.shndx||!symbol.name,"ELF imports firmware or libc symbols");
                const char *name=(const char*)image+names->offset+symbol.name;
                if(strcmp(name,"pico_app_query")==0){
                    PGA_REQUIRE((symbol.info&15)==2&&symbol.value==header->entry&&
                                pga_within(symbol.value,symbol.size?symbol.size:1,text->addr,text->size),"ELF query export does not match entry");
                    entry_symbol=true;
                }
            }
        }
        if(section->type!=PGA_S_RELA)continue;
        PGA_REQUIRE(!relocation_table,"ELF must contain one ordered relocation table");relocation_table=true;
        PGA_REQUIRE(section->entsize==12&&!(section->size%12)&&!(section->offset&3)&&section->link<header->shnum,
                    "ELF relocation table is invalid");
        relocations+=section->size/12;PGA_REQUIRE(relocations<=PGA_ELF_RELOCS,"ELF relocation limit exceeded");
        const pga_shdr_t *symbols=&check->sections[section->link];
        PGA_REQUIRE((symbols->type==PGA_S_DYNSYM||symbols->type==PGA_S_SYMTAB)&&symbols->size>=16&&symbols->link<header->shnum,
                    "ELF relocation symbol table is missing");
        uint32_t previous_offset=0;
        for(size_t k=0;k<section->size/12;k++){
            pga_rela_t relocation;memcpy(&relocation,image+section->offset+k*12,12);
            PGA_REQUIRE(relocation.info==5&&relocation.addend==0&&!(relocation.offset&3),"ELF relocation is outside the allowed relative subset");
            PGA_REQUIRE(!k||relocation.offset>previous_offset,"ELF relocation patches must be unique and ordered");previous_offset=relocation.offset;
            const pga_shdr_t *patch=pga_section_at(check,relocation.offset,4,true);
            PGA_REQUIRE(patch,"ELF relocation patch is outside loaded sections");
            uint32_t address;memcpy(&address,image+patch->offset+(relocation.offset-patch->addr),4);
            PGA_REQUIRE(pga_section_at(check,address,1,false),"ELF relocation points outside loaded sections");
        }
    }
    PGA_REQUIRE(entry_symbol,"ELF is missing pico_app_query");
    if(error&&error_bytes)error[0]=0;
    return true;
#undef PGA_REQUIRE
}

bool pico_app_code_validate_xtensa(const uint8_t *image,size_t bytes,char *error,size_t error_bytes)
{
    pga_checked_t *check=calloc(1,sizeof(*check));
    if(!check)return pga_error(error,error_bytes,"Not enough memory to validate ELF");
    bool valid=pga_check_xtensa(image,bytes,check,error,error_bytes);free(check);return valid;
}

static bool pga_exports_valid(const pico_app_exports_t *exports)
{
    if(!exports||exports->abi!=PICO_APP_ABI||exports->bytes<PICO_APP_EXPORTS_V1_BYTES||!exports->version||exports->kind>255||
       !exports->id||!exports->start||!exports->render||!exports->event||!exports->key||!exports->tick||
       !exports->action||!exports->status||!exports->save||!exports->stop)return false;
    size_t length=0;
    while(length<32&&exports->id[length]){
        unsigned char ch=(unsigned char)exports->id[length];
        if(!((ch>='a'&&ch<='z')||(ch>='0'&&ch<='9')||(length&&(ch=='_'||ch=='-'))))return false;
        length++;
    }
    return length>0&&length<32;
}

#ifdef ESP_PLATFORM
typedef struct {esp_elf_t elf;} pga_code_t;
static bool pga_loaded_data(const esp_elf_t *elf,uintptr_t pointer,size_t bytes)
{
    for(unsigned i=ELF_SEC_BSS;i<ELF_SECS;i++){
        const esp_elf_sec_t *section=&elf->sec[i];
        if(section->size&&pointer>=section->addr&&pga_span(pointer-section->addr,bytes,section->size))return true;
    }
    return false;
}
static bool pga_loaded_function(const esp_elf_t *elf,uintptr_t pointer)
{
    const esp_elf_sec_t *text=&elf->sec[ELF_SEC_TEXT];
    uintptr_t executable=text->addr+(SOC_IROM_LOW-SOC_DROM_LOW);
    return pointer>=executable&&pointer-executable<text->size;
}
bool pico_app_code_open(const uint8_t *image,size_t bytes,const pico_app_exports_t **exports,void **handle,char *error,size_t error_bytes)
{
    if(exports)*exports=NULL;
    if(handle)*handle=NULL;
    if(!exports||!handle)return pga_error(error,error_bytes,"Invalid loader output arguments");
    if((uintptr_t)image&3)return pga_error(error,error_bytes,"ELF payload buffer is unaligned");
    if(!pico_app_code_validate_xtensa(image,bytes,error,error_bytes))return false;
    pga_code_t *code=calloc(1,sizeof(*code));
    if(!code)return pga_error(error,error_bytes,"Not enough memory to load game code");
    if(esp_elf_init(&code->elf)||esp_elf_relocate(&code->elf,image)){
        esp_elf_deinit(&code->elf);free(code);return pga_error(error,error_bytes,"ELF relocation or cache synchronization failed");
    }
    // entry已由官方加载器映射到S3指令总线；dlsym返回的数据别名不能直接调用。
    // The official loader maps entry to the S3 instruction bus; its dlsym data alias cannot be called directly.
    pico_app_query_t query=(pico_app_query_t)(uintptr_t)code->elf.entry;
    const pico_app_exports_t *api=query();
    bool valid=pga_loaded_data(&code->elf,(uintptr_t)api,PICO_APP_EXPORTS_V1_BYTES);
    if(valid){
        valid=pga_loaded_data(&code->elf,(uintptr_t)api->id,1);
        if(valid){
            size_t n=0;while(n<32&&pga_loaded_data(&code->elf,(uintptr_t)api->id+n,1)&&api->id[n])n++;
            valid=n>0&&n<32&&pga_loaded_data(&code->elf,(uintptr_t)api->id+n,1)&&!api->id[n];
        }
        if(valid)valid=pga_exports_valid(api)&&pga_loaded_data(&code->elf,(uintptr_t)api,api->bytes);
        if(valid){
            uintptr_t callbacks[]={(uintptr_t)api->start,(uintptr_t)api->render,(uintptr_t)api->event,(uintptr_t)api->key,
                (uintptr_t)api->tick,(uintptr_t)api->action,(uintptr_t)api->status,(uintptr_t)api->save,(uintptr_t)api->stop};
            for(size_t i=0;i<sizeof(callbacks)/sizeof(callbacks[0]);i++)valid=valid&&pga_loaded_function(&code->elf,callbacks[i]);
            if(api->bytes>=sizeof(*api)&&api->ink)valid=valid&&pga_loaded_function(&code->elf,(uintptr_t)api->ink);
        }
    }
    if(!valid){pico_app_code_close(code);return pga_error(error,error_bytes,"Game exports do not match the supported ABI");}
    *exports=api;*handle=code;if(error&&error_bytes)error[0]=0;return true;
}
void pico_app_code_close(void *handle)
{
    pga_code_t *code=handle;if(!code)return;esp_elf_deinit(&code->elf);free(code);
}
#else
typedef struct {void *library;} pga_code_t;
bool pico_app_code_open(const uint8_t *image,size_t bytes,const pico_app_exports_t **exports,void **handle,char *error,size_t error_bytes)
{
    if(exports)*exports=NULL;
    if(handle)*handle=NULL;
    if(!exports||!handle||!image||bytes<32||bytes>PICO_APP_CODE_MAX)return pga_error(error,error_bytes,"Invalid native game payload");
    const char *temporary_root=getenv("TMPDIR");if(!temporary_root||!temporary_root[0])temporary_root="/tmp";
    char path[PATH_MAX];int length=snprintf(path,sizeof(path),"%s/pico-app-XXXXXX",temporary_root);
    if(length<0||(size_t)length>=sizeof(path))return pga_error(error,error_bytes,"Temporary path is too long");
    int fd=mkstemp(path);if(fd<0)return pga_error(error,error_bytes,"Cannot create temporary game module");
    size_t offset=0;
    while(offset<bytes){ssize_t written=write(fd,image+offset,bytes-offset);if(written<0&&errno==EINTR)continue;if(written<=0)break;offset+=(size_t)written;}
    bool saved=close(fd)==0&&offset==bytes;
    if(!saved){unlink(path);return pga_error(error,error_bytes,"Cannot write temporary game module");}
    void *library=dlopen(path,RTLD_NOW|RTLD_LOCAL);unlink(path);
    if(!library)return pga_error(error,error_bytes,"Native game module cannot be loaded for this computer");
    pico_app_query_t query=(pico_app_query_t)dlsym(library,"pico_app_query");
    const pico_app_exports_t *api=query?query():NULL;
    if(!pga_exports_valid(api)){dlclose(library);return pga_error(error,error_bytes,"Game exports do not match the supported ABI");}
    pga_code_t *code=malloc(sizeof(*code));
    if(!code){dlclose(library);return pga_error(error,error_bytes,"Not enough memory for game module");}
    code->library=library;*handle=code;*exports=api;if(error&&error_bytes)error[0]=0;return true;
}
void pico_app_code_close(void *handle)
{
    pga_code_t *code=handle;if(!code)return;dlclose(code->library);free(code);
}
#endif

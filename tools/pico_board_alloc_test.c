/* 中文：对实际分配器逐点注入失败，验证存档错误分类和线程隔离。
 * English: Inject each allocation failure into production allocators to verify storage error classification and thread isolation. */
#include "pico_board_data.h"
#include "../components/read_pico_home_link/home_link_json.h"
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static _Thread_local unsigned calls, fail_at;
static unsigned checks;
#define CHECK(c) do {checks++;if(!(c)){fprintf(stderr,"allocation FAIL %d: %s\n",__LINE__,#c);abort();}}while(0)
void *pico_test_allocate(size_t n){if(++calls==fail_at)return NULL;return malloc(n);}
static char *read_file(const char *path,size_t *bytes){
    FILE *f=fopen(path,"rb");CHECK(f);CHECK(!fseek(f,0,SEEK_END));long n=ftell(f);CHECK(n>0);rewind(f);
    char *p=malloc((size_t)n+1);CHECK(p);CHECK(fread(p,1,(size_t)n,f)==(size_t)n);fclose(f);p[n]=0;*bytes=(size_t)n;return p;
}
static void *other_thread(void *value){
    bool *ok=value;rp_home_json_allocation_reset();*ok=!rp_home_json_allocation_failed();return NULL;
}
int main(int argc,char **argv){
    CHECK(argc==2);size_t bytes;char *input=read_file(argv[1],&bytes);const char *error=NULL;
    calls=fail_at=0;ps_board_model_t *m=ps_board_model_parse_ex(input,bytes,&error);CHECK(m&&!error&&m->world_map);
    unsigned allocation_count=calls;ps_board_model_free(m);CHECK(allocation_count>3);
    for(unsigned at=1;at<=allocation_count;at++){
        calls=0;fail_at=at;m=ps_board_model_parse_ex(input,bytes,&error);
        if(m||!error||strcmp(error,"out_of_memory"))fprintf(stderr,"allocation %u/%u returned %s\n",at,allocation_count,error?error:"success");
        CHECK(!m&&error&&!strcmp(error,"out_of_memory"));
    }
    calls=fail_at=0;m=ps_board_model_parse_ex(input,bytes,&error);CHECK(m&&!error);
    uint8_t *old=m->world_map;size_t old_bytes=m->world_map_bytes;
    calls=0;fail_at=1;CHECK(!ps_board_model_set_world_map(m,old,old_bytes));CHECK(m->world_map==old&&m->world_map_bytes==old_bytes);
    calls=fail_at=0;CHECK(!ps_board_model_set_world_map(m,old,old_bytes-1));CHECK(m->world_map==old);
    CHECK(ps_board_model_set_world_map(m,old,old_bytes));CHECK(m->world_map!=old&&m->world_map_bytes==old_bytes);
    ps_board_model_free(m);
    calls=0;fail_at=1;rp_home_json_allocation_reset();cJSON *json=cJSON_Parse("{}");CHECK(!json&&rp_home_json_allocation_failed());
    bool other_ok=false;pthread_t thread;CHECK(!pthread_create(&thread,NULL,other_thread,&other_ok));CHECK(!pthread_join(thread,NULL));
    CHECK(other_ok&&rp_home_json_allocation_failed());
    calls=fail_at=0;m=ps_board_model_parse_ex(input,bytes,&error);CHECK(m&&!error&&!rp_home_json_allocation_failed());ps_board_model_free(m);
    m=ps_board_model_parse_ex("{",1,&error);CHECK(!m&&error&&!strcmp(error,"invalid_json"));free(input);
    printf("%u allocation checks PASS (%u failure points; thread-local status; retained map)\n",checks,allocation_count);return 0;
}

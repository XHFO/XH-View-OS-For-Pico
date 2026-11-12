/* SPDX-License-Identifier: Apache-2.0
 * TF事务边界故障注入与保留源迁移。/ Fault-injected TF boundary and source-preserving migration.
 */
#define main legacy_nvs_test_unused
#include "book_storage_host_test.c"
#undef main
static record_t card_files[16];
static bool card_available=true,card_fail,card_corrupt;
static record_t* card_file(const char* key,bool create){
    for(int i=0;i<16;i++)if(card_files[i].len&&!strcmp(card_files[i].key,key))return &card_files[i];
    if(create)for(int i=0;i<16;i++)if(!card_files[i].len){strcpy(card_files[i].key,key);return &card_files[i];}
    return NULL;
}
bool ps_storage_record_roots(char* current,char* other,size_t cap,bool* sd){snprintf(current,cap,"/tf");*other=0;*sd=true;return card_available;}
bool ps_store_exists_at(const char* root,const char* key,bool* exists){(void)root;*exists=card_file(key,false)!=NULL;return card_available;}
bool ps_load_at(const char* root,const char* key,void* out,size_t bytes){
    (void)root;record_t* f=card_file(key,false);if(!f||f->len!=bytes||card_corrupt)return false;memcpy(out,f->data,bytes);return true;
}
bool ps_store_at(const char* root,const char* key,const void* in,size_t bytes){
    (void)root;if(!card_available||card_fail)return false;record_t* f=card_file(key,true);assert(f&&bytes<=sizeof(f->data));memcpy(f->data,in,bytes);f->len=bytes;return true;
}
int main(void){
    const char* path="/sdcard/books/a.txt";book_progress_t p={.file_size=10000,.chapter=1,.byte_off=400,.px=48,.pct=30},q={0};
    assert(book_progress_save(path,&p)==ESP_OK&&book_progress_load(path,p.file_size,&q)&&q.byte_off==400);
    p.chapter=0;p.byte_off=900;p.pct=10;
    assert(book_progress_save_mode(path,&p,true)==ESP_OK&&book_progress_load(path,p.file_size,&q)&&q.chapter==1);
    p.chapter=2;p.byte_off=0;p.pct=60;p.px=64;
    assert(book_progress_save_mode(path,&p,true)==ESP_OK&&book_progress_load(path,p.file_size,&q)&&q.chapter==2&&q.px==64);
    assert(book_progress_set_last_path(path)==ESP_OK);char last_path[288];assert(book_progress_last_path(last_path,sizeof(last_path))&&!strcmp(last_path,path));
    p.chapter=3;seq_failure=ESP_ERR_NVS_NOT_ENOUGH_SPACE;
    assert(book_progress_save(path,&p)!=ESP_OK&&book_progress_load(path,p.file_size,&q)&&q.chapter==2);seq_failure=0;
    commit_failure=ESP_FAIL;assert(book_progress_save(path,&p)!=ESP_OK&&book_progress_load(path,p.file_size,&q)&&q.chapter==2);commit_failure=0;
    card_fail=true;
    assert(book_progress_save(path,&p)!=ESP_OK);assert(book_progress_load(path,p.file_size,&q)&&q.chapter==2);
    assert(book_progress_clear(path)!=ESP_OK&&book_progress_load(path,p.file_size,&q));card_fail=false;
    card_available=false;assert(book_progress_save(path,&p)!=ESP_OK);assert(!book_progress_load(path,p.file_size,&q));card_available=true;
    card_corrupt=true;assert(!book_progress_load(path,p.file_size,&q));assert(book_progress_save(path,&p)!=ESP_OK);card_corrupt=false;
    book_progress_watch_t* w=book_progress_watch_create(path);assert(w);assert(book_progress_forget(path)==ESP_OK&&book_progress_watch_invalidated(w));book_progress_watch_destroy(w);
    assert(!book_progress_load(path,p.file_size,&q));assert(!book_progress_last_path(last_path,sizeof(last_path)));
    // v2源保留，TF导入失败可重试；清空墓碑不再回退旧NVS。/ Retain v2 source, retry imports, and never revive a tombstoned NVS record.
    const char* old="/sdcard/books/old.txt";unsigned hash=2166136261u;for(const unsigned char* t=(const unsigned char*)old;*t;t++)hash=(hash^*t)*16777619u;
    char key[11];snprintf(key,sizeof(key),"b_%08x",hash);unsigned char raw[312]={ 'R','P','B',2 };
    raw[4]=100;raw[8]=4;raw[10]=27;raw[14]=48;raw[15]=80;raw[16]=99;raw[22]=(unsigned char)(strlen(old)+1);strcpy((char*)raw+24,old);
    assert(nvs_set_blob(1,key,raw,24+strlen(old)+1)==ESP_OK);card_fail=true;assert(book_progress_load(old,100,&q)&&q.chapter==4&&!card_file(key,false));card_fail=false;
    assert(book_progress_load(old,100,&q)&&q.chapter==4&&q.byte_off==27&&q.last_open_s==99);assert(record(key,false));
    assert(book_progress_clear(old)==ESP_OK&&!book_progress_load(old,100,&q)&&record(key,false));
    // 精确路径碰撞不得覆写。/ Exact-path collisions cannot overwrite an existing record.
    record_t* f=card_file(key,false);assert(f);f->data[8]='!';p.file_size=100;assert(book_progress_save(old,&p)!=ESP_OK);
    puts("PASS: TF solo/paired/furthest anchors; failure/no-card/corruption; migration retry; tombstone; exact-path collision; watches; last-path");
    return 0;
}

/* SPDX-License-Identifier: Apache-2.0
 * 产品阅读控制器主机验证；字形与设备存储为替身，不代表实机性能与字形验收。
 * Host product-reader checks. Mock font/storage do not establish device performance or glyph quality.
 */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <errno.h>
#include <dirent.h>
#include <unistd.h>
#include "pico_system.h"
#include "book_store.h"
#include "book_progress.h"
#include "book_cover.h"
#include "read_pico_sd.h"
#include "settings.h"
#include "ttf_font.h"
static ps_route_t route=PS_SHELF,history[32];
static int depth,back_calls;
static bool back_deferred;
static ps_view_t view={360,640,false};
static int64_t now_ms;
static int weight=400,stores,draws,legacy_calls;
static uint64_t rendered_hash;
static void render_hash(unsigned value){rendered_hash=(rendered_hash^value)*UINT64_C(1099511628211);}
static int body_draws,body_last_size,body_last_weight;
static bool fail_store;
static int fail_store_after=-1;
static const char *fail_store_key;
static bool real_return_enabled,annotation_open_ok=true;
static uint64_t annotation_size;static uint32_t annotation_chapter,annotation_start,annotation_end;
static char mock_font[160]="builtin",last_toast[160],rootdir[32];
static read_pico_sd_info_t media={.present=true,.mounted=true,.media_generation=1};
static bool storage_held,usb_held,storage_save_blocked,background_allowed=true;
bool ps_background_io_allowed(void){return background_allowed;}
bool ps_storage_save_allowed(void){return !storage_held&&!usb_held&&!storage_save_blocked;}
static bool font_suspended,font_missing,font_wrong_success;
static unsigned font_sd_opens;
static char preferred_font[TTF_FONT_PATH_MAX]=TTF_FONT_DEFAULT_SD;
static uint32_t choice_revision;
static unsigned book_revision;
static int root_queries,progress_queries;
static bool root_unreadable;
static char opened_path[BOOK_STORE_PATH_MAX],last_book_path[BOOK_STORE_PATH_MAX];
static unsigned cover_prepares,cover_clears;
static bool cover_fixture,cover_prepared_pixels,cover_prepare_no_record;
static int cover_prepare_latency_ms;
static const uint8_t cover_pixels[]={0,60,120,180,220,255};
enum { TEST_COVER_CAPACITY=9 };
static char cover_known_paths[TEST_COVER_CAPACITY][BOOK_STORE_PATH_MAX];
static unsigned cover_known_age[TEST_COVER_CAPACITY],cover_age;
static int cover_known_count;
bool book_cover_get(const char *path,book_cover_view_t *out){
    if(out)*out=(book_cover_view_t){0};
    if(!out||(!cover_fixture&&!(cover_prepared_pixels&&book_cover_known(path))))return false;
    *out=(book_cover_view_t){cover_pixels,2,3};return true;
}
bool book_cover_known(const char *path){for(int i=0;i<cover_known_count;i++)if(!strcmp(path,cover_known_paths[i]))return true;return false;}
bool book_cover_prepare(const char *path){
    assert(!storage_held&&!usb_held&&!media.exported&&background_allowed);
    for(int i=0;i<cover_known_count;i++)if(!strcmp(path,cover_known_paths[i])){cover_known_age[i]=++cover_age;return false;}
    cover_prepares++;now_ms+=cover_prepare_latency_ms;
    if(cover_prepare_no_record)return false;
    int entry=cover_known_count;
    if(cover_known_count==TEST_COVER_CAPACITY){entry=0;for(int i=1;i<cover_known_count;i++)if(cover_known_age[i]<cover_known_age[entry])entry=i;}
    else cover_known_count++;
    snprintf(cover_known_paths[entry],BOOK_STORE_PATH_MAX,"%s",path);cover_known_age[entry]=++cover_age;
    return true;
}
void book_cover_clear(void){cover_known_count=0;cover_age=0;memset(cover_known_age,0,sizeof(cover_known_age));cover_clears++;}
const app_desc_t app_font_pick={0},app_transfer={0},app_book={0};
typedef struct { char key[40];void *bytes;size_t size; } blob_t;
#define MOCK_BLOBS 4096
static blob_t blobs[MOCK_BLOBS],source_blobs[MOCK_BLOBS];
static bool mock_current_is_source;
static bool mock_filesystem;
static char mock_fs_roots[2][80];
#include "pico_reader_disk_test.inc"
static bool record_roots_unavailable,record_exists_failed;
static int allocation_fail_after=-1;
static size_t max_record_allocation;
static __attribute__((unused)) void *reader_test_calloc(size_t n,size_t size){
    if(allocation_fail_after==0)return NULL;
    if(allocation_fail_after>0)allocation_fail_after--;
    if(n*size>max_record_allocation)max_record_allocation=n*size;
    return calloc(n,size);
}
#ifndef heap_caps_calloc
#define heap_caps_calloc(n,s,caps) reader_test_calloc(n,s)
#endif
static void blobs_clear(blob_t *bank){for(int i=0;i<MOCK_BLOBS;i++){free(bank[i].bytes);bank[i]=(blob_t){0};}}
static blob_t *blob_find(blob_t *bank,const char *key){for(int i=0;i<MOCK_BLOBS;i++)if(!strcmp(bank[i].key,key))return &bank[i];return NULL;}
static void blob_remove(blob_t *bank,const char *key){blob_t *b=blob_find(bank,key);if(b){free(b->bytes);*b=(blob_t){0};}}
static bool blob_store(blob_t *bank,const char *key,const void *bytes,size_t size){
    assert(size<=65536);blob_t *b=blob_find(bank,key);
    if(!b)for(int i=0;i<MOCK_BLOBS;i++)if(!bank[i].key[0]){b=&bank[i];break;}
    assert(b);snprintf(b->key,sizeof(b->key),"%s",key);free(b->bytes);b->bytes=malloc(size);assert(b->bytes);memcpy(b->bytes,bytes,size);b->size=size;return true;
}
static bool blob_load(blob_t *bank,const char *key,void *bytes,size_t size){blob_t *b=blob_find(bank,key);if(!b||b->size!=size)return false;memcpy(bytes,b->bytes,size);return true;}
static blob_t *root_bank(const char *root){return !strcmp(root,"/mock/current")?blobs:!strcmp(root,"/mock/source")?source_blobs:NULL;}
bool ps_storage_record_roots(char *current,char *other,size_t capacity,bool *sd){
    if(record_roots_unavailable||storage_held||usb_held)return false;
    if(mock_filesystem){snprintf(current,capacity,"%s",mock_fs_roots[mock_current_is_source]);snprintf(other,capacity,"%s",mock_fs_roots[!mock_current_is_source]);if(sd)*sd=!mock_current_is_source;return true;}
    snprintf(current,capacity,"%s",mock_current_is_source?"/mock/source":"/mock/current");snprintf(other,capacity,"%s",mock_current_is_source?"/mock/current":"/mock/source");if(sd)*sd=!mock_current_is_source;return true;
}
bool ps_load_at(const char *root,const char *key,void *out,size_t bytes){if(mock_filesystem)return mock_fs_load(root,key,out,bytes);blob_t *bank=root_bank(root);return bank&&!record_roots_unavailable&&blob_load(bank,key,out,bytes);}
bool ps_store_at(const char *root,const char *key,const void *out,size_t bytes){if(mock_filesystem)return mock_fs_store(root,key,out,bytes);blob_t *bank=root_bank(root);return bank&&!record_roots_unavailable&&blob_store(bank,key,out,bytes);}
bool ps_store_exists_at(const char *root,const char *key,bool *exists){if(mock_filesystem)return mock_fs_exists(root,key,exists);blob_t *bank=root_bank(root);if(!bank||record_roots_unavailable||record_exists_failed)return false;*exists=blob_find(bank,key)!=NULL;return true;}
static const char *snapshot_fail_key;
static unsigned snapshot_writes;
#ifndef PICO_READER_SNAPSHOT_REAL_STORE
bool ps_reading_snapshot_store(const char *root,const char *key,const void *data,size_t bytes,uint32_t *crc){
    if(snapshot_fail_key&&!strcmp(snapshot_fail_key,key))return false;
    snapshot_writes++;bool ok=mock_fs_store(root,key,data,bytes);if(ok&&crc)*crc=mock_fs_crc(data,bytes);return ok;
}
#endif
ps_view_t ps_view(void){return view;}
ps_route_t ps_route(void){return route;}
int64_t ps_now(void){return now_ms;}
void ps_navigate(ps_route_t next){
    if(next!=route){
        for(int i=depth-1;i>=0;i--)if(history[i]==next){depth=i;route=next;ps_reader_enter(next);return;}
        if(depth==32){memmove(history,history+1,sizeof(history[0])*31);depth--;}
        history[depth++]=route;route=next;
    }
    ps_reader_enter(next);
}
void ps_back(void){back_calls++;if(back_deferred)return;route=depth?history[--depth]:PS_SHELF;ps_reader_enter(route);}
void ps_toast(const char *s){snprintf(last_toast,sizeof(last_toast),"%s",s);}
void ps_dirty(void){}
void ps_legacy(const app_desc_t *a){(void)a;legacy_calls++;}
// 独立阅读器单测禁用双机；真实联动由双进程集成验证。/ Reader-only tests disable dual mode; two-process integration verifies linking.
bool ps_dual_enabled(void){return false;}
bool ps_dual_open(const char *path){(void)path;return true;}
bool ps_store(const char *key,const void *bytes,size_t size){
    stores++;if(fail_store||(fail_store_key&&!strcmp(fail_store_key,key)))return false;
    if(fail_store_after==0)return false;
    if(fail_store_after>0)fail_store_after--;
    if(mock_filesystem)return mock_fs_store(mock_fs_roots[mock_current_is_source],key,bytes,size);
    return blob_store(mock_current_is_source?source_blobs:blobs,key,bytes,size);
}
bool ps_load(const char *key,void *bytes,size_t size){if(mock_filesystem)return mock_fs_load(mock_fs_roots[mock_current_is_source],key,bytes,size);return blob_load(mock_current_is_source?source_blobs:blobs,key,bytes,size);}
int book_store_roots(book_store_root_t out[2],int *n){root_queries++;memset(out,0,sizeof(*out)*2);snprintf(out[0].path,sizeof(out[0].path),"%s/%s",rootdir,root_unreadable?"unreadable":"books");*n=1;return root_unreadable?-1:0;}
bool book_store_roots_degraded(void){return false;}
unsigned book_store_revision(void){return book_revision;}
bool book_progress_load(const char *path,uint32_t size,book_progress_t *out){(void)size;progress_queries++;if(!strstr(path,"已读"))return false;*out=(book_progress_t){.pct=42,.last_open_s=7};return true;}
bool book_progress_last_path(char *out,size_t cap){snprintf(out,cap,"%s",last_book_path);return out[0];}
int read_pico_sd_get_info(read_pico_sd_info_t *out){*out=media;return 0;}
bool ps_storage_busy(void){return storage_held;}
bool ps_usb_held(void){return usb_held;}
bool ttf_font_sd_suspended(void){return font_suspended;}
const char *app_settings_font_path(void){return preferred_font;}
uint32_t app_settings_font_revision(void){return choice_revision;}
void app_settings_set_font_path(const char *path){snprintf(preferred_font,sizeof(preferred_font),"%s",path?path:"");choice_revision++;}
void ps_global_prefer_real_book(bool enabled){(void)enabled;}
void ps_global_book_return(bool enabled){real_return_enabled=enabled;}
bool app_book_open_annotation(const char *path,uint64_t size,uint32_t chapter,uint32_t start,uint32_t end){
    if(!annotation_open_ok)return false;snprintf(opened_path,sizeof(opened_path),"%s",path);annotation_size=size;annotation_chapter=chapter;annotation_start=start;annotation_end=end;return true;
}
bool app_book_open_path(const char *path){snprintf(opened_path,sizeof(opened_path),"%s",path);return true;}
bool app_book_open_path_single(const char *path){return app_book_open_path(path);}
bool app_book_local_only_path(char *out,size_t capacity){if(!out||!capacity)return false;out[0]=0;return false;}
void app_book_dual_pause(void){}
bool ps_dual_synced(void){return false;}
bool ps_dual_secondary_shelf(void){return false;}
bool ps_dual_has_reading(void){return false;}
bool ps_dual_book_progress(const char *path,uint32_t bytes,bool *started,uint8_t *pct,uint32_t *recent){(void)path;(void)bytes;if(started)*started=false;if(pct)*pct=0;if(recent)*recent=0;return false;}
uint32_t ps_dual_progress_revision(void){return 0;}
void ps_dual_sync_annotation(uint8_t type,uint32_t chapter,uint32_t start,uint32_t end,const char *text){(void)type;(void)chapter;(void)start;(void)end;(void)text;}
void ps_dual_set_local_away(bool away){(void)away;}
bool ps_global_resume_reading(void){return false;}
#ifndef PICO_READER_REAL_RENDER
const char *ttf_font_path(void){return mock_font;}
const char *ttf_font_display_name(void){return "Mock actual font";}
bool ttf_font_is_builtin(void){return ttf_font_path_is_builtin(mock_font);}
void ttf_font_cache_clear(void){}
int ttf_font_open_builtin(void){strcpy(mock_font,"builtin");return 0;}
int ttf_font_open(const char *s){if(!ttf_font_path_is_builtin(s)){assert(!storage_held&&!usb_held&&!font_suspended&&!media.exported&&media.mounted&&media.present);font_sd_opens++;if(font_missing||font_wrong_success)return font_wrong_success?0:-1;}snprintf(mock_font,sizeof(mock_font),"%s",s);return 0;}
bool ttf_font_path_is_builtin(const char *s){return !s||!*s||!strcmp(s,"builtin")||!strcmp(s,"builtin-serif")||!strcmp(s,"builtin-kai");}
void ttf_set_weight(int w){weight=w;}
int ttf_get_weight(void){return weight;}
int ttf_text_width_px(int px,const char *s){int width=0;for(;*s;s++)if(((unsigned char)*s&0xc0)!=0x80){int advance=(unsigned char)*s<128?px/2:px;if(strcmp(mock_font,"builtin"))advance=advance*3/4;width+=advance;}return width;}
EpdRect ps_rect(int x,int y,int w,int h){return (EpdRect){x*19/10,y*19/10,w*19/10,h*19/10};}
bool ps_hit(int x,int y,int rx,int ry,int rw,int rh){return x>=rx&&y>=ry&&x<rx+rw&&y<ry+rh;}
void epd_draw_pixel(int x,int y,uint8_t color,uint8_t *fb){(void)fb;assert(x>=0&&y>=0&&x<view.w*19/10&&y<view.h*19/10);render_hash((unsigned)x);render_hash((unsigned)y);render_hash(color);draws++;}
static void rect_ok(int x,int y,int w,int h){assert(x>=0&&y>=0&&w>=0&&h>=0&&x+w<=view.w&&y+h<=view.h);draws++;}
void ps_fill(uint8_t *f,int x,int y,int w,int h,int g){(void)f;(void)g;rect_ok(x,y,w,h);}
void ps_line(uint8_t *f,int x,int y,int x1,int y1,int g){(void)f;(void)g;assert(x>=0&&x<=view.w&&x1>=0&&x1<=view.w&&y>=0&&y<=view.h&&y1>=0&&y1<=view.h);draws++;}
void ps_box(uint8_t *f,int x,int y,int w,int h,int r,int g,bool fill){(void)f;(void)r;(void)g;(void)fill;rect_ok(x,y,w,h);}
void ps_text(uint8_t *f,int x,int y,int size,const char *s,int g){(void)f;render_hash((unsigned)x);render_hash((unsigned)y);render_hash((unsigned)size);render_hash((unsigned)g);for(;*s;s++)render_hash((unsigned char)*s);rect_ok(x,y,0,size);}
void ps_text_fixed(uint8_t *f,int x,int y,int size,const char *s,int g){ps_text(f,x,y,size,s,g);}
uint8_t ps_shelf_card_style(void){return 0;}
void ps_body_text(uint8_t *f,int x,int y,int size,const char *s,int g){body_draws++;body_last_size=size;body_last_weight=weight;ps_text(f,x,y,size,s,g);}
void ps_center(uint8_t *f,int x,int y,int size,const char *s,int g){ps_text(f,x,y,size,s,g);}
void ps_wrap(uint8_t *f,int x,int y,int w,int size,int lines,const char *s,int g){(void)w;(void)lines;ps_text(f,x,y,size,s,g);}
void ps_icon(uint8_t *f,int x,int y,int size,ps_icon_t icon,int g){(void)f;(void)icon;(void)g;rect_ok(x,y,size,size);}
void ps_button(uint8_t *f,int x,int y,int w,int h,const char *s,bool on){(void)f;(void)s;(void)on;rect_ok(x,y,w,h);}
void ps_heading(uint8_t *f,const char *title,const char *sub){(void)f;(void)title;(void)sub;draws++;}
void ps_tabs(uint8_t *f,int selected){(void)f;(void)selected;draws++;}
#endif

#ifndef PICO_READER_NO_FIXTURES
#define PICO_READER_HOST_FIXTURES 1
#include "fixtures/pico_reader_samples.h"
#endif
#include "../main/system/pico_reader.c"

static void complete(void){for(int i=0;i<5000&&!paginated&&!layout_failed;i++){now_ms+=100;ps_reader_tick();}if(!paginated||layout_failed||seek_pending)fprintf(stderr,"pagination incomplete: route=%d book=%d size=%u count=%zu scanned=%zu/%zu failed=%d seek=%d\n",route,active_book,rp.size,page_count,scan_offset,rb[active_book].bytes,layout_failed,seek_pending);assert(paginated&&!layout_failed&&!seek_pending);}
static void tap(int x,int y){ps_event_t e={.type=PS_TAP,.x=x,.y=y};if(route==PS_EDITOR){e.type=PS_PRESS;ps_reader_event(&e);e.type=PS_RELEASE;ps_reader_event(&e);return;}ps_reader_event(&e);}
static void editor_tap_key(int action,int value){editor_key_t keys[64];int n=editor_keys(keys);for(int i=0;i<n;i++)if(keys[i].action==action&&keys[i].value==value){tap(keys[i].x+keys[i].w/2,keys[i].y+keys[i].h/2);return;}assert(!"editor key not visible");}
static void swipe(ps_event_type_t type){ps_event_t e={.type=type};ps_reader_event(&e);}
static void render_checked(void){uint8_t fb=0;int writes=stores,roots=root_queries,progress=progress_queries;unsigned prepared=cover_prepares;size_t count=page_count,offset=anchor;ps_reader_render(&fb);assert(stores==writes&&count==page_count&&offset==anchor&&roots==root_queries&&progress==progress_queries&&prepared==cover_prepares);}
static uint64_t render_signature(void){int before=draws;rendered_hash=UINT64_C(14695981039346656037);render_checked();render_hash((unsigned)(draws-before));return rendered_hash;}
static char callback_text[3072];
static void done(const char *s){snprintf(callback_text,sizeof(callback_text),"%s",s);}
static void test_reflow(void){
    ps_reader_open(3);assert(open_book&&chapter_count==24);complete();assert(page_count>40);
    size_t expected=0;
    for(size_t i=0;i<page_count;i++){
        assert(pages[i]==expected);current_page=i;size_t end=page_end();assert(end>expected&&end<=rb[3].bytes);
        size_t at=expected;char line[512];while(at<end){bool p;size_t next=take_line(at,line,sizeof(line),&p);assert(next>at&&next<=end);assert(u8valid(rb[3].text+at,next-at));at=next;}
        expected=end;
    }
    assert(expected==rb[3].bytes);
    current_page=page_count/2;anchor=pages[current_page]+15;while(((unsigned char)rb[3].text[anchor]&0xc0)==0x80)anchor++;
    size_t saved=anchor,original_pages=page_count;
    rp.size=27;layout_begin();complete();assert(anchor==saved&&pages[current_page]<=saved&&page_end()>saved);assert(page_count>original_pages);
    rp.spacing=3;layout_begin();complete();assert(anchor==saved);
    view=(ps_view_t){640,360,true};layout_begin();complete();assert(anchor==saved);render_checked();
    strcpy(mock_font,"other-real-font.ttf");layout_begin();complete();assert(anchor==saved);size_t before=page_count;
    view=(ps_view_t){360,640,false};rp.size=19;rp.spacing=1;layout_begin();complete();assert(anchor==saved&&page_count!=before);
    strcpy(mock_font,"builtin");layout_begin();complete();page_turn(1);assert(anchor==pages[current_page]);render_checked();
}


static void file_fixture(const char *name,bool books){
    char path[512];snprintf(path,sizeof(path),"%s/%s%s",rootdir,books?"books/":"",name);
    FILE *f=fopen(path,"wb");assert(f);assert(fputs("真实图书路径测试\n",f)>=0);assert(!fclose(f));
}
static void scan_complete(void){
    int dirty=0;
    (void)reader_font_sync(false);
    for(int tick=0;tick<1200&&(shelf_scan_pending||shelf_scanning||shelf_commit_pending);tick++){
        int seen=shelf_scan_seen;bool pending=shelf_scan_pending;now_ms+=20;
        if(ps_reader_tick())dirty++;
        if(!pending)assert(shelf_scan_seen-seen<=SHELF_SCAN_BATCH);
    }
    assert(!shelf_scan_pending&&!shelf_scanning&&!shelf_commit_pending&&!shelf_dir&&dirty<=1);
}
static void test_real_shelf(void){
    file_fixture("根目录图书.TXT",false);file_fixture("另一本.epub",false);
    file_fixture("._排除.txt",false);file_fixture(".隐藏.epub",true);file_fixture("不支持.pdf",true);
    char nested[96];snprintf(nested,sizeof(nested),"%s/books/伪装目录.txt",rootdir);assert(!mkdir(nested,0700));
    file_fixture("已读书籍.txt",true);
    for(int i=0;i<20;i++){char name[40];snprintf(name,sizeof(name),"第%02d本图书.txt",i);file_fixture(name,true);}
    char long_name[240]="";for(int i=0;i<70;i++)strcat(long_name,"长");strcat(long_name,".txt");file_fixture(long_name,true);
    snprintf(last_book_path,sizeof(last_book_path),"%s/根目录图书.TXT",rootdir);
    ps_navigate(PS_SHELF);shelf_search[0]=0;shelf_filter=shelf_scroll=0;rp.sort=0;shelf_scan_pending=true;
    int before_queries=progress_queries;scan_complete();
    assert(shelf_file_count==24&&shelf_file_visible==24&&progress_queries-before_queries==24&&!shelf_scan_error&&!shelf_scan_limited);
    unsigned cached_count=0;bool loading=true,limited=true;int query_count=root_queries;
    ps_reader_library_status(&cached_count,&loading,&limited);ps_reader_library_status(NULL,NULL,NULL);
    assert(cached_count==24&&!loading&&!limited&&root_queries==query_count);
    assert(!strcmp(shelf_files[0].name,"已读书籍")&&shelf_files[0].pct==42);
    assert(shelf_file_continue()>=0&&!strcmp(shelf_files[shelf_file_continue()].path,last_book_path));
    // 只准备可见EPUB且每tick至多一本；绘制封面只读RAM，不再解码或写缓存。
    // Prepare only visible EPUBs, at most one per tick; drawing reads RAM without decoding or caching.
    book_cover_clear();search_done("另一本");assert(shelf_file_visible==1);unsigned prepared=cover_prepares;
    now_ms+=1000;assert(!ps_reader_tick());now_ms+=250;assert(!ps_reader_tick());assert(cover_prepares==prepared+1);
    for(int tick=0;tick<12;tick++){now_ms+=200;ps_reader_tick();assert(cover_prepares==prepared+1);}
    cover_fixture=true;render_checked();cover_fixture=false;
    storage_held=true;now_ms+=1000;ps_reader_tick();assert(cover_prepares==prepared+1);storage_held=false;
    search_done("");
    int calls=legacy_calls;
    for(int land=0;land<2;land++)for(int list=0;list<SHELF_VIEW_COUNT;list++){
        view=land?(ps_view_t){640,360,true}:(ps_view_t){360,640,false};rp.list=(uint8_t)list;shelf_scroll=0;render_checked();
        const int expected[2][SHELF_VIEW_COUNT]={{6,4,6,2,1},{6,4,4,4,1}};
        int size=shelf_page_size();assert(size==expected[land][list]);
        assert(shelf_layout().top==(land?180:294));
        swipe(PS_SWIPE_L);assert(shelf_scroll==1);render_checked();
        shelf_item_rect_t first=shelf_item_rect(0);tap(first.x+2,first.y+2);assert(legacy_calls==++calls&&!strcmp(opened_path,shelf_files[shelf_file_ids[size]].path));
        shelf_scan_pending=false;
        swipe(PS_SWIPE_R);assert(!shelf_scroll);render_checked();
        shelf_scroll=999;render_checked();swipe(PS_SWIPE_L);assert(shelf_scroll==shelf_page_count(24)-1);
    }
    view=(ps_view_t){360,640,false};rp.list=0;shelf_scroll=0;
    search_done("根目录");assert(shelf_file_visible==1);render_checked();tap(30,shelf_layout().top+20);assert(!strcmp(opened_path,last_book_path));
    shelf_scan_pending=false;search_done("");
    query_count=root_queries;ps_edit("搜索本机书名","",95,search_done);ps_back();ps_reader_tick();assert(!shelf_scan_pending&&!shelf_scanning&&root_queries==query_count);
    shelf_scan_pending=true;storage_held=true;int roots=root_queries;ps_reader_tick();assert(root_queries==roots&&!shelf_scanning);storage_held=false;
    usb_held=true;ps_reader_tick();assert(root_queries==roots&&!shelf_scanning);usb_held=false;
    media.exported=true;ps_reader_tick();assert(root_queries==roots&&!shelf_scanning);media.exported=false;scan_complete();
    shelf_scan_pending=true;ps_reader_tick();assert(shelf_scanning);ps_navigate(PS_IMPORT);assert(!shelf_dir&&!shelf_scanning&&shelf_scan_pending);
    tap(25,124+48+10);assert(route==PS_SHELF&&legacy_calls==calls+1);scan_complete();
    media.mounted=false;ps_reader_tick();scan_complete();assert(shelf_file_count==0);render_checked();media.mounted=true;media.media_generation++;scan_complete();
    now_ms+=500;ps_reader_tick();scan_complete();assert(shelf_file_count==24);
    // 真实文件星标独立存档；失败恢复原状态，横竖/列表均有完整44px触区。
    // Persist real-file stars separately; roll back failed saves and retain full 44px targets in both layouts/orientations.
    view=(ps_view_t){360,640,false};rp.list=0;shelf_filter=shelf_scroll=0;shelf_search[0]=0;shelf_file_rebuild();
    char favorite_path[BOOK_STORE_PATH_MAX];snprintf(favorite_path,sizeof(favorite_path),"%s",shelf_files[shelf_file_ids[0]].path);
    int favorite_calls=legacy_calls,writes=stores;
    reader_shelf_layout_t favorite_layout=shelf_layout();int favorite_w=(view.w-32-(favorite_layout.cols-1)*8)/favorite_layout.cols;
    tap(16+favorite_w-1,favorite_layout.top+favorite_layout.cover_h+63);
    assert(ps_reader_file_favorite(favorite_path,false)&&legacy_calls==favorite_calls&&stores==writes+1);
    tap(favorite_layout.tabs_x+2*favorite_layout.tabs_w/3+8,favorite_layout.tabs_y+8);
    assert(shelf_filter==1&&shelf_file_visible==1&&!strcmp(shelf_files[shelf_file_ids[0]].path,favorite_path));render_checked();
    fail_store=true;assert(!ps_reader_file_favorite(favorite_path,true)&&ps_reader_file_favorite(favorite_path,false)&&shelf_file_visible==1);fail_store=false;
    free(shelf_favorites);shelf_favorites=NULL;shelf_favorites_loaded=shelf_favorites_failed=false;shelf_favorites_load();
    assert(ps_reader_file_favorite(favorite_path,false));
    for(int land=0;land<2;land++)for(int list=0;list<SHELF_VIEW_COUNT;list++){
        view=land?(ps_view_t){640,360,true}:(ps_view_t){360,640,false};rp.list=(uint8_t)list;shelf_scroll=0;shelf_file_rebuild();render_checked();
        shelf_item_rect_t r=shelf_item_rect(0);
        assert(r.star_x>=r.x&&r.star_y>=r.y&&r.star_x+44<=r.x+r.w&&r.star_y+44<=r.y+r.h+2);
        tap(r.star_x+43,r.star_y+43);assert(!ps_reader_file_favorite(favorite_path,false)&&shelf_file_visible==0&&legacy_calls==favorite_calls);
        assert(ps_reader_file_favorite(favorite_path,true)&&shelf_file_visible==1);
    }
    assert(ps_reader_file_favorite(favorite_path,true)&&!ps_reader_file_favorite(favorite_path,false));
    view=(ps_view_t){360,640,false};rp.list=0;shelf_filter=shelf_scroll=0;shelf_file_rebuild();
    // 容量上限可见且不逐批重刷，也不触及样例/用户进度。 / Visible bounded capacity without per-batch refreshes or sample/progress writes.
    for(int i=0;i<510;i++){char name[40];snprintf(name,sizeof(name),"bounded-%03d.txt",i);file_fixture(name,true);}
    book_revision++;now_ms+=500;ps_reader_tick();scan_complete();assert(shelf_file_count==SHELF_FILE_MAX&&shelf_scan_limited);render_checked();
    for(int i=0;i<510;i++){char path[96];snprintf(path,sizeof(path),"%s/books/bounded-%03d.txt",rootdir,i);assert(!unlink(path));}
    shelf_scan_pending=true;scan_complete();assert(shelf_file_count==24);
    shelf_scroll=0;legacy_calls=0;
}
// 封面首屏六本不变；后续按九宫格衔接，绘制和命中共用位置，不重现隐藏续读卡的触区。
// Keep six covers on the home page, then dispatch each nine-cell page without a hidden continuation-card hit area.
static void test_shelf_cover_pagination(void){
    const char *extra="分页边界第25本.txt";file_fixture(extra,true);
    ps_navigate(PS_SHELF);rp.list=SHELF_VIEW_COVERS;shelf_manage=shelf_style_picker=false;
    shelf_search[0]=0;shelf_filter=shelf_scroll=0;shelf_scan_pending=true;scan_complete();
    assert(shelf_file_count==25&&shelf_file_visible==25);
    int calls=legacy_calls;
    for(int land=0;land<2;land++){
        view=land?(ps_view_t){640,360,true}:(ps_view_t){360,640,false};
        const int counts[]={0,1,6,7,15,16,24,25};
        for(size_t c=0;c<sizeof(counts)/sizeof(counts[0]);c++){
            int n=counts[c],pages=n<=6?1:1+(n-6+8)/9;shelf_file_visible=n;shelf_scroll=0;
            assert(shelf_page_count(n)==pages&&shelf_page_size()==6&&shelf_layout().top==(land?180:294));
            for(int page=0;page<pages;page++){
                shelf_scroll=page;int start=page?6+(page-1)*9:0,size=page?9:6;
                assert(shelf_page_start(page)==start&&shelf_page_size()==size);render_checked();
                for(int j=0;j<size&&start+j<n;j++){
                    shelf_item_rect_t r=shelf_item_rect(j);
                    assert(r.w>=44&&r.h>=44&&r.x>=0&&r.y>=0&&r.x+r.w<=view.w&&r.y+r.h<=view.h-52);
                    assert(r.star_x>=r.x&&r.star_y>=r.y&&r.star_x+44<=r.x+r.w&&r.star_y+44<=r.y+r.h+2);
                    if(page){
                        shelf_item_rect_t origin=shelf_item_rect(0);
                        assert(r.x==origin.x+(j%3)*(origin.w+8)&&r.y==origin.y+(j/3)*origin.h);
                    }
                }
            }
        }
        shelf_file_rebuild();assert(shelf_file_visible==25);
        for(int page=1;page<4;page++){
            shelf_scroll=page;int start=6+(page-1)*9;render_checked();
            for(int j=0;j<9&&start+j<25;j++){
                char expected_path[BOOK_STORE_PATH_MAX];snprintf(expected_path,sizeof(expected_path),"%s",shelf_files[shelf_file_ids[start+j]].path);
                shelf_item_rect_t r=shelf_item_rect(j);
                tap(r.x+2,r.y+2);assert(legacy_calls==++calls&&!strcmp(opened_path,expected_path)&&shelf_scroll==page);
                shelf_scan_pending=false;
                bool favorite=ps_reader_file_favorite(expected_path,false);int writes=stores;
                tap(r.star_x+43,r.star_y+43);assert(legacy_calls==calls&&stores==writes+1&&ps_reader_file_favorite(expected_path,false)!=favorite);
                tap(r.star_x+1,r.star_y+1);assert(legacy_calls==calls&&stores==writes+2&&ps_reader_file_favorite(expected_path,false)==favorite);
            }
        }
        shelf_scroll=0;swipe(PS_SWIPE_U);assert(shelf_scroll==1);swipe(PS_SWIPE_D);assert(!shelf_scroll);
        swipe(PS_SWIPE_L);assert(shelf_scroll==1);swipe(PS_SWIPE_L);assert(shelf_scroll==2);swipe(PS_SWIPE_U);assert(shelf_scroll==3);
        swipe(PS_SWIPE_L);assert(shelf_scroll==3);swipe(PS_SWIPE_R);assert(shelf_scroll==2);
        search_done("根目录");assert(!shelf_scroll&&shelf_file_visible==1&&shelf_page_count(1)==1);render_checked();
        search_done("");assert(!shelf_scroll&&shelf_file_visible==25);
    }
    // 旋转保留当前页和同一批图书；删除最后一页唯一图书后只向有效末页收敛。
    // Rotation preserves the current book range; removing the sole final-page book clamps to the new final page.
    shelf_scroll=2;view=(ps_view_t){360,640,false};render_checked();
    char same_path[BOOK_STORE_PATH_MAX];snprintf(same_path,sizeof(same_path),"%s",shelf_files[shelf_file_ids[15]].path);
    shelf_item_rect_t first=shelf_item_rect(0);tap(first.x+2,first.y+2);assert(legacy_calls==++calls&&!strcmp(opened_path,same_path));shelf_scan_pending=false;
    view=(ps_view_t){640,360,true};render_checked();first=shelf_item_rect(0);
    tap(first.x+2,first.y+2);assert(legacy_calls==++calls&&!strcmp(opened_path,same_path)&&shelf_scroll==2);shelf_scan_pending=false;
    char extra_path[128];snprintf(extra_path,sizeof(extra_path),"%s/books/%s",rootdir,extra);assert(!unlink(extra_path));
    shelf_scroll=3;shelf_scan_pending=true;scan_complete();assert(shelf_file_count==24&&shelf_file_visible==24&&shelf_scroll==2&&shelf_page_count(24)==3);
    view=(ps_view_t){360,640,false};shelf_scroll=0;legacy_calls=0;render_checked();
    puts("PASS: cover home page keeps six books; later pages contain nine unique book/star targets, correct partial tails, swipe limits, search resets and rotation/removal clamps in both orientations.");
}
// 封面锁屏只取已提交书单；不受筛选影响，不扫描、不解码、不越界，并去重。
// Cover lock screens use only committed books, independent of filters, without I/O or duplicate paths.
static void test_committed_cover_sources(void){
    assert(shelf_file_count==24&&shelf_files);
    char output[32][288];memset(output,0,sizeof(output));
    int roots=root_queries,writes=stores;unsigned prepares=cover_prepares;
    assert(!ps_reader_cover_sources(NULL,1)&&!ps_reader_cover_sources(output,0)&&!ps_reader_cover_sources(output,-1));
    memset(output[1],'Z',sizeof(output[1]));
    int continued=shelf_file_continue();assert(continued>=0);
    assert(ps_reader_cover_sources(output,1)==1&&!strcmp(output[0],shelf_files[continued].path));
    for(size_t i=0;i<sizeof(output[1]);i++)assert(output[1][i]=='Z');
    int filter=shelf_filter;char search[sizeof(shelf_search)];memcpy(search,shelf_search,sizeof(search));
    shelf_filter=0;strcpy(shelf_search,"没有任何匹配的书");shelf_file_rebuild();assert(!shelf_file_visible);
    reader_file_t pending={0};strcpy(pending.path,"/sdcard/books/uncommitted.epub");
    reader_file_t *old_stage=shelf_stage_files;int old_stage_count=shelf_stage_count;
    shelf_stage_files=&pending;shelf_stage_count=1;
    int count=ps_reader_cover_sources(output,32);assert(count==24&&!strcmp(output[0],shelf_files[continued].path));
    for(int i=0;i<count;i++)assert(strcmp(output[i],pending.path));
    reader_file_t second=shelf_files[1];strcpy(shelf_files[1].path,shelf_files[0].path);
    count=ps_reader_cover_sources(output,32);assert(count==23);
    for(int i=0;i<count;i++)for(int j=i+1;j<count;j++)assert(strcmp(output[i],output[j]));
    shelf_files[1]=second;shelf_stage_files=old_stage;shelf_stage_count=old_stage_count;
    shelf_filter=filter;memcpy(shelf_search,search,sizeof(search));shelf_file_rebuild();
    assert(root_queries==roots&&stores==writes&&cover_prepares==prepares);
    puts("PASS: committed cover sources prioritize continue book, ignore filters/staging, deduplicate, respect capacity and perform no I/O.");
}
// 统计真实控制器的背景刷新请求；触屏事件的即时反馈不计入后台加载刷新。
// Count actual controller background redraw requests; immediate input feedback is outside the loading budget.
static void test_shelf_loading_redraws(void){
    view=(ps_view_t){640,360,true};rp.list=0;shelf_filter=shelf_scroll=0;
    // 从真实目录扫描到首批七张封面一次提交；手指按住时连目录枚举也暂停。
    // Real directory discovery and the first seven covers commit once; held interaction also pauses directory enumeration.
    for(int i=0;i<7;i++){char name[48];snprintf(name,sizeof(name),"warm-%02d.epub",i);file_fixture(name,true);}
    snprintf(last_book_path,sizeof(last_book_path),"%s/books/warm-06.epub",rootdir);strcpy(shelf_search,"warm-");
    shelf_scan_pending=true;cover_prepared_pixels=true;(void)reader_font_sync(false);
    unsigned prepared=cover_prepares;int frames=0;now_ms+=20;assert(!ps_reader_tick()&&shelf_scanning);
    int seen=shelf_scan_seen,roots=root_queries;background_allowed=false;
    for(int tick=0;tick<30;tick++){now_ms+=100;assert(!ps_reader_tick()&&shelf_scan_seen==seen&&root_queries==roots&&cover_prepares==prepared);}
    background_allowed=true;
    for(int tick=0;tick<300;tick++){
        unsigned before=cover_prepares;now_ms+=20;
        if(ps_reader_tick()){frames++;assert(cover_prepares==prepared+7&&shelf_cover_batch_done&&!shelf_commit_pending);render_checked();}
        assert(cover_prepares-before<=1);
    }
    assert(frames==1&&shelf_file_visible==7&&cover_prepares==prepared+7);
    // 点击刷新不重绘已提交书架；慢解码期间旧书单和旧封面输出完全一致，最后只提交一次。
    // Refresh never redraws the committed shelf; slow preparation preserves its books/covers exactly, then commits once.
    reader_file_t *committed=shelf_files;int committed_count=shelf_file_count;uint64_t previous=render_signature();
    reader_shelf_layout_t refresh_layout=shelf_layout();ps_event_t refresh={.type=PS_TAP,.x=refresh_layout.refresh_x+20,.y=refresh_layout.tabs_y+15};
    assert(!ps_reader_event(&refresh)&&shelf_scan_pending&&render_signature()==previous);
    prepared=cover_prepares;frames=0;cover_prepare_latency_ms=400;
    for(int tick=0;tick<100;tick++){
        unsigned before=cover_prepares;now_ms+=100;bool changed=ps_reader_tick();
        if(changed){frames++;assert(cover_prepares==prepared+7&&shelf_cover_batch_done&&!shelf_stage_active);}
        else if(shelf_stage_active){assert(shelf_files==committed&&shelf_file_count==committed_count&&render_signature()==previous);}
        assert(cover_prepares-before<=1);
    }
    assert(frames==1&&shelf_files!=committed&&cover_prepares==prepared+7);cover_prepare_latency_ms=0;
    // prepare失败且没有写负缓存也算完成；每本只尝试一次，不能卡在同一本永久重试。
    // Preparation failures without negative entries still finish; try each path once rather than retrying one forever.
    prepared=cover_prepares;frames=0;cover_prepare_no_record=true;assert(!ps_reader_event(&refresh));
    for(int tick=0;tick<100;tick++){now_ms+=100;frames+=ps_reader_tick();}
    assert(frames==1&&cover_prepares==prepared+7&&!shelf_stage_active&&!shelf_commit_pending);cover_prepare_no_record=false;
    // 离开取消独立扫描；无卡扫描也必须一次完成，而不是永久等待封面或目录。
    // Leaving cancels staging; a missing card must also finish once rather than waiting forever for files/covers.
    assert(!ps_reader_event(&refresh));now_ms+=20;assert(!ps_reader_tick()&&shelf_scanning);
    prepared=cover_prepares;committed=shelf_files;ps_navigate(PS_RECORDS);
    for(int tick=0;tick<20;tick++){now_ms+=200;ps_reader_tick();assert(!shelf_stage_active&&!shelf_scanning&&cover_prepares==prepared&&shelf_files==committed);}
    ps_navigate(PS_SHELF);media.mounted=false;media.media_generation++;frames=0;
    for(int tick=0;tick<20;tick++){now_ms+=100;frames+=ps_reader_tick();}
    assert(frames==1&&shelf_file_count==0&&!shelf_scan_pending&&!shelf_stage_active&&!shelf_commit_pending);
    media.mounted=true;media.media_generation++;shelf_scan_pending=true;scan_complete();assert(shelf_file_count==31);
    root_unreadable=true;shelf_scan_pending=true;frames=0;
    for(int tick=0;tick<30;tick++){now_ms+=100;frames+=ps_reader_tick();}
    assert(frames==1&&shelf_file_count==0&&shelf_scan_error&&!shelf_stage_active&&!shelf_scan_pending);
    root_unreadable=false;shelf_scan_pending=true;scan_complete();assert(shelf_file_count==31);
    for(int i=0;i<7;i++){char path[96];snprintf(path,sizeof(path),"%s/books/warm-%02d.epub",rootdir,i);assert(!unlink(path));}
    snprintf(last_book_path,sizeof(last_book_path),"%s/根目录图书.TXT",rootdir);
    shelf_scan_pending=shelf_scanning=false;shelf_scan_close();shelf_file_count=17;
    for(int i=0;i<17;i++){
        reader_file_t *book=&shelf_files[i];memset(book,0,sizeof(*book));book->epub=true;
        snprintf(book->path,sizeof(book->path),"/sdcard/books/%s-%02d.epub",i==16?"hidden":"coalesce",i);
        snprintf(book->name,sizeof(book->name),"%s-%02d",i==16?"hidden":"coalesce",i);
    }
    strcpy(shelf_last_path,shelf_files[16].path);strcpy(shelf_search,"coalesce");shelf_file_rebuild();
    assert(shelf_file_visible==16&&shelf_page_size()==6);
    prepared=cover_prepares;frames=0;
    book_cover_clear();cover_prepared_pixels=true;ps_reader_enter(PS_SHELF);
    for(int tick=0;tick<50;tick++){
        unsigned before=cover_prepares;now_ms+=200;
        if(ps_reader_tick()){frames++;assert(cover_prepares==prepared+7);render_checked();}
        assert(cover_prepares-before<=1);
    }
    assert(cover_prepares==prepared+7&&frames==1&&shelf_cover_batch_done);
    for(int j=0;j<6;j++){book_cover_view_t cover;assert(book_cover_get(shelf_files[shelf_file_ids[j]].path,&cover));}
    prepared=cover_prepares;
    for(int tick=0;tick<200;tick++){now_ms+=100;assert(!ps_reader_tick()&&cover_prepares==prepared);}
    // 菜单、切换面板或按住手指时停止后台扫描和解码，不能用加载帧覆盖交互。
    // Menus, switching panels and held pointers suspend scans and decoding, preventing loading frames over active input.
    book_cover_clear();ps_reader_enter(PS_SHELF);background_allowed=false;
    for(int tick=0;tick<30;tick++){now_ms+=200;assert(!ps_reader_tick()&&cover_prepares==prepared);}
    background_allowed=true;frames=0;
    for(int tick=0;tick<50;tick++){now_ms+=200;frames+=ps_reader_tick();}
    assert(cover_prepares==prepared+7&&frames==1);prepared=cover_prepares;
    // 无封面仅填充负缓存，七本结果不生成任何无变化的刷新。
    // Missing covers only populate negative entries; seven unchanged results request zero frames.
    book_cover_clear();cover_prepared_pixels=false;ps_reader_enter(PS_SHELF);frames=0;
    for(int tick=0;tick<50;tick++){now_ms+=200;frames+=ps_reader_tick();}
    assert(cover_prepares==prepared+7&&frames==0);
    // 旧页面尚有待提交封面时翻页，只提交后续九本；没有显示的续读封面不参与新批次。
    // Paging discards pending home-page feedback and commits nine current covers without preparing the hidden continuation.
    book_cover_clear();cover_prepared_pixels=true;ps_reader_enter(PS_SHELF);
    now_ms+=250;assert(!ps_reader_tick());now_ms+=250;assert(!ps_reader_tick());now_ms+=250;assert(!ps_reader_tick()&&shelf_cover_changed);
    swipe(PS_SWIPE_L);assert(shelf_scroll==1&&!shelf_cover_changed);render_checked();
    prepared=cover_prepares;frames=0;
    for(int tick=0;tick<50;tick++){now_ms+=200;frames+=ps_reader_tick();}
    assert(cover_prepares==prepared+9&&frames==1&&shelf_cover_batch_done);
    for(int j=6;j<15;j++){book_cover_view_t cover;assert(book_cover_get(shelf_files[shelf_file_ids[j]].path,&cover));}
    assert(!book_cover_known(shelf_files[16].path));
    uint64_t nine_committed=render_signature();assert(shelf_cover_display_hold());
    for(int j=6;j<15;j++){book_cover_view_t cover;assert(shelf_cover_display_get(shelf_files[shelf_file_ids[j]].path,&cover));}
    assert(render_signature()==nine_committed);shelf_cover_display_release();
    prepared=cover_prepares;
    for(int tick=0;tick<100;tick++){now_ms+=200;assert(!ps_reader_tick()&&cover_prepares==prepared);}
    swipe(PS_SWIPE_U);assert(shelf_scroll==2&&shelf_page_start(2)==15);prepared=cover_prepares;frames=0;
    for(int tick=0;tick<50;tick++){now_ms+=200;frames+=ps_reader_tick();}
    assert(cover_prepares==prepared+1&&frames==1&&shelf_cover_batch_done&&!book_cover_known(shelf_files[16].path));
    book_cover_view_t tail;assert(book_cover_get(shelf_files[shelf_file_ids[15]].path,&tail));
    swipe(PS_SWIPE_D);assert(shelf_scroll==1);
    // 离开书架立即停止解码，返回时只对当前仍缺的封面合并提交。
    // Leaving stops decoding immediately; re-entry coalesces only the remaining current covers.
    book_cover_clear();ps_reader_enter(PS_SHELF);now_ms+=250;assert(!ps_reader_tick());now_ms+=250;assert(!ps_reader_tick());
    prepared=cover_prepares;ps_navigate(PS_RECORDS);
    for(int tick=0;tick<50;tick++){now_ms+=200;ps_reader_tick();assert(cover_prepares==prepared);}
    ps_navigate(PS_SHELF);frames=0;
    for(int tick=0;tick<50;tick++){now_ms+=200;frames+=ps_reader_tick();}
    assert(cover_prepares==prepared+8&&frames==1);
    cover_prepared_pixels=false;book_cover_clear();shelf_search[0]=0;shelf_filter=shelf_scroll=0;
    view=(ps_view_t){360,640,false};rp.list=1;shelf_scan_pending=true;scan_complete();assert(shelf_file_count==24);
    puts("PASS: atomic real shelf scan + seven home/nine later covers commit one final frame, including slow decoding; nine-cover snapshots preserve committed rendering; later pages skip the hidden continuation; failed covers/missing card/cancel terminate; held input pauses scan/decode; missing covers/idle zero frames.");
}

// 直接在书架选择五种样式，保持筛选/书单/收藏，紧凑模式不请求封面解码。
// Pick each style on the shelf without changing filters/books/favorites; compact never asks for cover decoding.
static void test_shelf_view_controls(void){
    ps_navigate(PS_SHELF);shelf_filter=0;shelf_search[0]=0;shelf_scroll=0;shelf_manage=false;shelf_file_rebuild();
    shelf_scan_pending=shelf_scanning=shelf_commit_pending=shelf_stage_active=false;
    int count=shelf_file_count,calls=legacy_calls;
    for(int land=0;land<2;land++){
        view=land?(ps_view_t){640,360,true}:(ps_view_t){360,640,false};
        reader_shelf_layout_t l=shelf_layout();
        assert(l.tabs_x+l.tabs_w<=l.style_x&&l.style_x+l.style_w<=l.refresh_x&&l.refresh_x+44<=view.w);
        for(int style=0;style<SHELF_VIEW_COUNT;style++){
            int writes=stores;tap(l.style_x+10,l.tabs_y+10);assert(shelf_style_picker&&stores==writes);render_checked();
            shelf_item_rect_t cell=shelf_picker_rect(style);
            assert(cell.h>=44&&cell.x>=0&&cell.x+cell.w<=view.w&&cell.y>=0&&cell.y+cell.h<=view.h);
            tap(cell.x+cell.w/2,cell.y+cell.h/2);
            reader_prefs_t stored;assert(!shelf_style_picker&&rp.list==style&&!shelf_scroll&&stores==writes+1);
            assert(ps_load("reader-prefs-v1",&stored,sizeof(stored))&&stored.list==style&&shelf_file_count==count&&legacy_calls==calls);
            render_checked();
        }
        rp.list=SHELF_VIEW_COMPACT;
        unsigned prepared=cover_prepares;book_cover_clear();shelf_cover_signature=123;
        for(int tick=0;tick<20;tick++){now_ms+=300;(void)shelf_cover_prepare_visible();}
        assert(cover_prepares==prepared&&shelf_cover_batch_done&&!shelf_cover_signature);
        tap(l.style_x+10,l.tabs_y+10);assert(shelf_style_picker);
        tap(2,2);assert(!shelf_style_picker&&rp.list==SHELF_VIEW_COMPACT&&legacy_calls==calls);
        tap(l.style_x+10,l.tabs_y+10);assert(shelf_style_picker);int page=shelf_scroll;
        swipe(PS_SWIPE_L);assert(!shelf_style_picker&&shelf_scroll==page);
    }
    view=(ps_view_t){360,640,false};rp.list=SHELF_VIEW_DETAILS;shelf_scroll=0;
}
static void test_typography(void){
    view=(ps_view_t){360,640,false};ps_reader_open(3);complete();assert(!ps_reader_prefers_grayscale());
    anchor=chapters[5].offset;seek_pending=true;seek_finish();remember();size_t kept=anchor;
    ps_navigate(PS_READING_SETTINGS);
    for(int i=0;i<20;i++)tap(315,136);
    assert(rp.size==33);ps_navigate(PS_READ);complete();assert(anchor==kept);render_checked();ps_navigate(PS_READING_SETTINGS);
    for(int i=0;i<20;i++)tap(265,136);
    assert(rp.size==19);ps_navigate(PS_READ);complete();assert(anchor==kept);render_checked();ps_navigate(PS_READING_SETTINGS);
    for(int i=0;i<3;i++)tap(315,136);
    assert(rp.size==25);
    tap(50,235);assert(!rp.weight);ps_navigate(PS_READ);complete();assert(anchor==kept&&weight==400&&!layout_weight);
    ps_navigate(PS_READING_SETTINGS);tap(250,235);assert(rp.weight);ps_navigate(PS_READ);complete();
    assert(anchor==kept&&weight==600&&layout_weight==1);
    int before=body_draws;render_checked();assert(weight==600&&body_draws>before&&body_last_size==25&&body_last_weight==600);
    /* 旧偏好仍可保留15/17，正文绘制必须与分页使用同一字号。 / Legacy 15/17 preferences must paint at the same size used for pagination. */
    for(int size=15;size<=17;size+=2){
        rp.size=(uint8_t)size;layout_begin();complete();before=body_draws;render_checked();
        assert(anchor==kept&&body_draws>before&&body_last_size==size&&body_last_weight==600);
    }
    rp.size=25;layout_begin();complete();
    ps_reader_open(4);assert(ps_reader_prefers_grayscale());ps_navigate(PS_READING_SETTINGS);assert(!ps_reader_prefers_grayscale());
}
static void test_records(void){
    ps_reader_open(3);complete();selection_start=chapters[8].offset;selection_end=selection_start+30;
    while(((unsigned char)rb[3].text[selection_end]&0xc0)==0x80)selection_end++;selection_active=true;
    save_highlight();assert(rr->count==1&&rr->item[0]->type==REC_HIGHLIGHT);char quote[244],source[128];assert(ps_reader_excerpt_count()==1&&ps_reader_excerpt(0,quote,sizeof(quote),source,sizeof(source)));
    assert(!memcmp(rb[3].text+rr->item[0]->offset,quote,strlen(quote)));assert(strstr(source,"沿河"));
    size_t target=rr->item[0]->offset;rp.size=25;layout_begin();complete();record_jump(0);complete();assert(anchor==target&&pages[current_page]<=target&&page_end()>target);
    selection_active=false;add_bookmark();assert(rr->count==2&&rr->item[1]->type==REC_BOOKMARK);add_bookmark();assert(rr->count==2);
    fail_store=true;reader_record_t rec=record_base(REC_NOTE);rec.text="不能丢失的笔记";assert(!add_record(&rec)&&rr->count==2);fail_store=false;
    assert(add_record(&rec)&&rr->count==3);selected_record=0;fail_store=true;record_delete();assert(rr->count==3&&rr->item[0]->type==REC_HIGHLIGHT);fail_store=false;
    ps_navigate(PS_RECORDS);for(int land=0;land<2;land++){view=land?(ps_view_t){640,360,true}:(ps_view_t){360,640,false};selected_record=-1;render_checked();selected_record=2;render_checked();swipe(PS_SWIPE_U);render_checked();}
    selected_record=0;record_delete();assert(rr->count==2&&ps_reader_excerpt_count()==0);
    view=(ps_view_t){360,640,false};
    export_records();char file[128];snprintf(file,sizeof(file),"%s/books/.pico-reader/reading-records.txt",rootdir);FILE *f=fopen(file,"rb");assert(f);char out[2048];size_t bytes=fread(out,1,sizeof(out)-1,f);out[bytes]=0;fclose(f);assert(strstr(out,"不能丢失的笔记")&&strstr(out,"书签"));
}
static void test_input(void){
    const char *raw[]={"nihaoshijie","woxihuanyuedu","jintiantianqihenhao","yued","jintiandetianqihenhao"};
    const char *predicted[]={"你好世界","我喜欢阅读","今天天气很好","阅读","今天的天气很好"};
    for(int i=0;i<5;i++){
        ps_edit("连续拼音","",383,done);editor_mode(INPUT_PINYIN);strcpy(composition,raw[i]);candidates_update();
        assert(candidate_count&&!strcmp(candidates[0],predicted[i]));assert(candidate_select(0)&&!strcmp(editor_text,predicted[i])&&!composition[0]);
    }
    ps_edit("输入测试","",32,done);editor_mode(INPUT_PINYIN);strcpy(composition,"nihao");candidates_update();assert(candidate_count&&strcmp(candidates[0],"你好")==0);
    editor_tap_key(EK_CANDIDATE,0);assert(!strcmp(editor_text,"你好")&&!composition[0]);
    editor_mode(INPUT_T9);strcpy(composition,"64");candidates_update();bool found=false;for(int i=0;i<candidate_count;i++)if(!strcmp(candidates[i],"你"))found=true;assert(found);
    composition[0]=0;editor_mode(INPUT_ENGLISH);editor_tap_key(EK_LETTER,'q');assert(strstr(editor_text,"q"));editor_backspace();assert(!strcmp(editor_text,"你好"));
    editor_mode(INPUT_SYMBOL);editor_tap_key(EK_LETTER,'1');assert(strstr(editor_text,"1"));editor_backspace();editor_backspace();assert(!strcmp(editor_text,"你"));
    editor_commit();assert(!strcmp(callback_text,"你"));
    int oldstores=stores;ps_edit("Wi-Fi密码","abc",63,done);editor_append("1");ps_navigate(PS_WIFI);assert(stores==oldstores);
    ps_reader_open(0);new_note();assert(editor_note);editor_append("笔记草稿");assert(draft_save());ps_back();new_note();assert(strstr(editor_text,"笔记草稿"));
    int count=(int)rr->count;editor_commit();assert((int)rr->count==count+1&&!rr->item[count]->ink_count&&!draft.text[0]);
    for(int land=0;land<2;land++){view=land?(ps_view_t){640,360,true}:(ps_view_t){360,640,false};ps_edit("键盘布局","Sample示例",383,done);for(int mode=0;mode<INPUT_MODE_COUNT;mode++){editor_mode(input_ids[mode]);render_checked();}}
    view=(ps_view_t){360,640,false};
}
// 临时输入与持久笔记各自保留独立上限，旧记录结构可继续读取。
// Transient input and persisted notes retain separate bounds while old records remain readable.
static void test_editor_limits(void){
    assert(sizeof(editor_text)==3072&&sizeof(((reader_record_v2_t*)0)->text)==384&&sizeof(draft.text)==2048);
    char oversized[4097];memset(oversized,'x',sizeof(oversized)-1);oversized[sizeof(oversized)-1]=0;
    ps_edit("临时编辑边界",oversized,sizeof(oversized),done);
    assert(editor_limit==3071&&strlen(editor_text)==3071&&!editor_append("x"));
    ps_navigate(PS_WIFI);
    char token[1024];memset(token,'t',sizeof(token)-1);token[sizeof(token)-1]=0;
    int writes=stores;ps_edit_secret("长期访问令牌",sizeof(token)-1,done);
    assert(editor_password&&!editor_wifi_password&&!editor_password_visible&&editor_limit==1023&&editor_append(token));
    assert(strlen(editor_text)==1023&&!strcmp(editor_text,token)&&!editor_append("x"));
    callback_text[0]=0;editor_commit();assert(!strcmp(callback_text,token)&&route!=PS_EDITOR);
    ps_edit_secret("访问码",32,done);assert(editor_append("abc"));editor_commit();assert(!strcmp(callback_text,"abc"));
    ps_edit_secret("取消输入",1023,done);assert(editor_append(token));
    editor_password_visible=true;strcpy(composition,"uncommitted");ps_navigate(PS_WIFI);
    assert(stores==writes&&!editor_password&&!editor_password_visible&&!editor_done&&!composition[0]);
    for(size_t i=0;i<sizeof(editor_text);i++)assert(!editor_text[i]);
    callback_text[0]=0;ps_edit_password("Wi-Fi 密码",64,done);assert(editor_wifi_password&&editor_append("short"));
    editor_commit();assert(route==PS_EDITOR&&!callback_text[0]);
    editor_text[0]=0;assert(editor_append("correct-password"));editor_commit();assert(!strcmp(callback_text,"correct-password")&&route!=PS_EDITOR);
    char psk[65];memset(psk,'a',64);psk[64]=0;ps_edit_password("Wi-Fi PSK",64,done);assert(editor_append(psk));editor_commit();assert(!strcmp(callback_text,psk));
    callback_text[0]=0;ps_edit_password("Wi-Fi PSK",64,done);psk[0]='z';assert(editor_append(psk));editor_commit();assert(route==PS_EDITOR&&!callback_text[0]);ps_navigate(PS_WIFI);
    char certificate[2049];memset(certificate,'A',sizeof(certificate)-1);certificate[sizeof(certificate)-1]=0;
    const char *head="-----BEGIN CERTIFICATE-----\n",*tail="\n-----END CERTIFICATE-----\n";
    memcpy(certificate,head,strlen(head));memcpy(certificate+2048-strlen(tail),tail,strlen(tail));
    callback_text[0]=0;ps_edit("受信CA",certificate,2048,done);
    assert(editor_limit==2048&&strlen(editor_text)==2048&&!strcmp(editor_text,certificate)&&!editor_append("A"));
    editor_commit();assert(!strcmp(callback_text,certificate)&&route!=PS_EDITOR);
    ps_reader_open(0);new_note();assert(editor_note&&editor_limit==2047);
    editor_text[0]=0;char note[2048];memset(note,'n',sizeof(note)-1);note[sizeof(note)-1]=0;
    assert(editor_append(note)&&!editor_append("n"));uint32_t count=rr->count;editor_commit();
    assert(rr->count==count+1&&strlen(rr->item[count]->text)==2047&&!strcmp(rr->item[count]->text,note));
    puts("PASS: 3072-byte transient editor, 1023-byte secret input and full-buffer clearing, untruncated 2048-byte CA, expanded 2047-byte saved notes.");
}
static void test_comic_and_routes(void){
    ps_reader_open(4);assert(comic_valid);render_checked();page_turn(1);assert(rp.books[4].comic_page==1);comic_zoom=160;comic_pan_x=130;comic_pan_y=-200;render_checked();view=(ps_view_t){640,360,true};render_checked();
    const ps_route_t routes[]={PS_READING_SETTINGS,PS_INPUT_SETTINGS,PS_IMPORT};
    for(int land=0;land<2;land++){view=land?(ps_view_t){640,360,true}:(ps_view_t){360,640,false};for(int i=0;i<3;i++){ps_navigate(routes[i]);render_checked();}}
    view=(ps_view_t){360,640,false};ps_reader_open(3);tools=true;render_checked();chapter_panel=true;render_checked();assert(ps_reader_key(-1)&&!chapter_panel&&tools);font_panel=true;render_checked();assert(ps_reader_key(-1)&&!font_panel);assert(ps_reader_key(-1)&&!tools);
    view=(ps_view_t){640,360,true};tools=true;render_checked();chapter_panel=true;render_checked();font_panel=true;chapter_panel=false;render_checked();
    ps_navigate(PS_IMPORT);tap(25,124+3*48+10);assert(legacy_calls==0);view=(ps_view_t){360,640,false};tap(25,124+48+10);assert(route==PS_SHELF&&legacy_calls==0);ps_navigate(PS_IMPORT);tap(25,134);assert(route==PS_TRANSFER&&legacy_calls==0);
}
static void test_direct_toc(void){
    for(int land=0;land<2;land++){
        view=land?(ps_view_t){640,360,true}:(ps_view_t){360,640,false};
        ps_reader_open(3);complete();anchor=chapters[8].offset;seek_pending=true;seek_finish();
        size_t kept=anchor;int calls=legacy_calls,writes=stores;
        ps_reader_toc();assert(tools&&chapter_panel&&!font_panel&&chapter_scroll==8&&anchor==kept&&route==PS_READ);
        render_checked();assert(legacy_calls==calls&&stores==writes);ps_reader_toc();assert(chapter_panel&&tools);
        assert(ps_reader_key(-1)&&!chapter_panel&&route==PS_READ);assert(ps_reader_key(-1)&&!tools&&anchor==kept);
        ps_reader_toc();int x=(view.w-(land?420:view.w-32))/2,top=land?72:200;
        tap(x+30,top+60);assert(!tools&&!chapter_panel&&anchor==kept);
        int count=chapter_count;chapter_count=0;ps_reader_toc();assert(!tools&&!chapter_panel&&strstr(last_toast,"暂无目录"));
        tools=chapter_panel=true;chapter_scroll=-1;int empty_writes=stores;render_checked();assert(chapter_scroll==-1&&stores==empty_writes);
        ps_event_t swipe={.type=PS_SWIPE_U};assert(tools_event(&swipe)&&chapter_scroll==0);
        swipe.type=PS_SWIPE_D;assert(tools_event(&swipe)&&chapter_scroll==0);
        tap(x+30,top+60);assert(tools&&chapter_panel&&anchor==kept);
        tools=chapter_panel=false;chapter_count=count;
        ps_reader_open(4);ps_reader_toc();assert(!tools&&!chapter_panel&&strstr(last_toast,"漫画")&&legacy_calls==calls);
        open_book=false;ps_reader_toc();assert(!tools&&!chapter_panel&&strstr(last_toast,"请先打开"));
    }
}
static void test_reloads(void){
    int oldcount=rr->count;size_t oldoffset=rp.books[3].offset;records_free(rr);free(pages);rr=NULL;pages=NULL;initialized=false;int before=stores;ps_reader_init();assert(stores==before&&rr->count==(uint32_t)oldcount&&rp.books[3].offset==oldoffset);
    reader_index_v3_t saved,index;assert(ps_load("reader-r3-index",&saved,sizeof(saved)));index=saved;index.count=RECORD_MAX+1;
    assert(ps_store("reader-r3-index",&index,sizeof(index)));records_free(rr);free(pages);rr=NULL;pages=NULL;initialized=false;before=stores;ps_reader_init();assert(stores==before&&rr->count==0&&records_read_failed);
    assert(!records_save());assert(ps_store("reader-r3-index",&saved,sizeof(saved)));records_free(rr);free(pages);rr=NULL;pages=NULL;initialized=false;before=stores;ps_reader_init();assert(stores==before&&rr->count==(uint32_t)oldcount&&!records_read_failed);
}
static void reload_reader_no_writes(void){
    records_free(rr);free(pages);rr=NULL;pages=NULL;initialized=false;
    int before=stores;ps_reader_init();assert(stores==before&&rr&&pages);
}
static void test_preference_compatibility(void){
    reader_prefs_t original=rp,saved=rp;
    ri_copy(saved.font,sizeof(saved.font),"/sdcard/fonts/preserved.ttf");
    saved.version=1;saved.size=19;saved.weight=0;
    assert(ps_store("reader-prefs-v1",&saved,sizeof(saved)));reload_reader_no_writes();
    assert(rp.version==4&&rp.size==25&&rp.weight==1&&rp.serial==saved.serial&&!memcmp(rp.books,saved.books,sizeof(rp.books)));
    assert(!strcmp(rp.font,saved.font)&&prefs_save());
    saved=rp;saved.size=19;saved.weight=0;assert(ps_store("reader-prefs-v1",&saved,sizeof(saved)));reload_reader_no_writes();
    assert(rp.version==4&&rp.size==19&&!rp.weight);
    saved.version=1;saved.size=17;assert(ps_store("reader-prefs-v1",&saved,sizeof(saved)));reload_reader_no_writes();
    assert(rp.version==4&&rp.size==17&&!rp.weight);
    saved.version=1;saved.size=23;saved.weight=1;assert(ps_store("reader-prefs-v1",&saved,sizeof(saved)));reload_reader_no_writes();
    assert(rp.version==4&&rp.size==23&&rp.weight==1);
    const char *fonts[]={"","builtin","builtin-serif","builtin-kai","/sdcard/fonts/custom.ttf"};
    for(unsigned v=1;v<=4;v++)for(unsigned i=0;i<sizeof(fonts)/sizeof(fonts[0]);i++){
        saved=original;saved.version=v;saved.list=0;ri_copy(saved.font,sizeof(saved.font),fonts[i]);
        assert(ps_store("reader-prefs-v1",&saved,sizeof(saved)));reload_reader_no_writes();
        const char *expected=v<3&&i<2?TTF_FONT_DEFAULT_SD:fonts[i];
        assert(rp.version==4&&!strcmp(rp.font,expected)&&!memcmp(rp.books,saved.books,sizeof(rp.books))&&rp.list==(v<4?1:0));
    }
    for(int style=0;style<SHELF_VIEW_COUNT;style++){
        saved=original;saved.version=4;saved.list=(uint8_t)style;
        assert(ps_store("reader-prefs-v1",&saved,sizeof(saved)));reload_reader_no_writes();
        assert(rp.list==style&&rp.serial==saved.serial&&!memcmp(rp.books,saved.books,sizeof(rp.books)));
    }
    saved.list=255;assert(ps_store("reader-prefs-v1",&saved,sizeof(saved)));reload_reader_no_writes();assert(rp.list==SHELF_VIEW_DETAILS);
    original.version=3;assert(ps_store("reader-prefs-v1",&original,sizeof(original)));reload_reader_no_writes();
}
static void test_font_routes(void){
    view=(ps_view_t){360,640,false};
    ps_reader_open(3);complete();anchor=chapters[12].offset;seek_pending=true;seek_finish();remember();
    size_t saved=anchor;ps_navigate(PS_READING_SETTINGS);assert(!strcmp(mock_font,TTF_FONT_DEFAULT_SD));
    int fw=(view.w-36)/3;tap(18+fw+5,310);assert(!strcmp(rp.font,"builtin-serif")&&!strcmp(mock_font,TTF_FONT_DEFAULT_SD));
    ps_navigate(PS_READ);complete();assert(anchor==saved&&!strcmp(mock_font,"builtin-serif"));
    ps_edit("通用设置输入","",30,done);assert(!strcmp(mock_font,TTF_FONT_DEFAULT_SD));ps_back();complete();assert(!strcmp(mock_font,"builtin-serif")&&anchor==saved);
    ps_navigate(PS_READING_SETTINGS);tap(18+fw*2+5,310);assert(!strcmp(rp.font,"builtin-kai"));
    ps_navigate(PS_READ);complete();assert(!strcmp(mock_font,"builtin-kai")&&anchor==saved);
    ps_navigate(PS_SHELF);assert(!strcmp(mock_font,TTF_FONT_DEFAULT_SD));ps_reader_open(-1);complete();assert(!strcmp(mock_font,"builtin-kai")&&anchor==saved);
    ri_copy(rp.font,sizeof(rp.font),"builtin");reader_font_sync(true);layout_begin();complete();
}
static void test_media_lost(void){
    ps_reader_open(3);complete();ri_copy(rp.font,sizeof(rp.font),"/sdcard/fonts/reader.ttf");reader_font_sync(true);layout_begin();complete();
    new_note();assert(editor_note);editor_append("失卡时保留的草稿");
    int before=stores;uint32_t count=rr->count;media.present=media.mounted=false;ps_reader_media_lost();
    assert(stores==before&&rr->count==count&&!strcmp(mock_font,"builtin")&&!strcmp(rp.font,"builtin"));
    assert(!strcmp(detached_font,"/sdcard/fonts/reader.ttf")&&draft_auto_paused&&!selection_active&&!selection_drag);
    assert(strstr(draft.text,"失卡时保留的草稿"));now_ms+=10000;ps_reader_tick();assert(stores==before);
    editor_append("继续编辑");assert(!draft_auto_paused);now_ms+=6100;ps_reader_tick();assert(stores==before+1);
    ps_back();complete();remember();reader_prefs_t persisted;assert(ps_load("reader-prefs-v1",&persisted,sizeof(persisted))&&!strcmp(persisted.font,"/sdcard/fonts/reader.ttf"));
    ps_navigate(PS_READING_SETTINGS);tap(25,310);assert(!detached_font[0]);assert(ps_load("reader-prefs-v1",&persisted,sizeof(persisted))&&!strcmp(persisted.font,"builtin"));
    media.present=media.mounted=true;media.media_generation++;ps_navigate(PS_READ);complete();assert(rr->count==count);
}
#ifndef PICO_READER_REAL_RENDER
static void test_editor_session_font(void){
    char saved_preference[TTF_FONT_PATH_MAX];ri_copy(saved_preference,sizeof(saved_preference),preferred_font);
    ps_navigate(PS_SHELF);app_settings_set_font_path(TTF_FONT_BUILTIN_SERIF);reader_font_sync(false);
    unsigned opened=font_sd_opens;ps_edit("中文全字库","",383,done);
    assert(!strcmp(editor_font,TTF_FONT_DEFAULT_SD)&&!strcmp(mock_font,TTF_FONT_DEFAULT_SD));
    assert(font_sd_opens==opened+1&&!strcmp(preferred_font,TTF_FONT_BUILTIN_SERIF));
    opened=font_sd_opens;
    for(int i=0;i<20;i++){composition_add(i%2?'i':'n');now_ms+=50;ps_reader_tick();}
    assert(font_sd_opens==opened&&!strcmp(mock_font,TTF_FONT_DEFAULT_SD));
    ps_navigate(PS_INPUT_SETTINGS);assert(!editor_font[0]&&!strcmp(mock_font,TTF_FONT_BUILTIN_SERIF));
    font_missing=true;opened=font_sd_opens;ps_edit("无完整字体","",383,done);
    assert(!editor_font[0]&&font_sd_opens==opened+2&&!strcmp(mock_font,TTF_FONT_BUILTIN_SERIF));
    opened=font_sd_opens;for(int i=0;i<20;i++){now_ms+=100;ps_reader_tick();}assert(font_sd_opens==opened);
    font_missing=false;ps_navigate(PS_SHELF);app_settings_set_font_path(saved_preference);reader_font_sync(false);
    puts("PASS: complete Chinese editor font selected once per session, no per-key reopen, user preference restored and missing coverage explicit.");
}
#endif
static void test_default_font_recovery(void){
    reader_prefs_t original=rp;size_t original_anchor=anchor;
    ps_navigate(PS_SHELF);assert(!strcmp(mock_font,TTF_FONT_DEFAULT_SD));
    ps_reader_enter(PS_ROUTE_COUNT);assert(!strcmp(mock_font,TTF_FONT_DEFAULT_SD));
    ri_copy(rp.font,sizeof(rp.font),"builtin-serif");strcpy(mock_font,"builtin");
    ps_reader_enter(PS_SHELF);assert(!strcmp(rp.font,"builtin-serif"));
    ps_reader_enter(PS_ROUTE_COUNT);app_settings_set_font_path("");ps_reader_enter(PS_SHELF);
    assert(!strcmp(rp.font,"builtin")&&!strcmp(mock_font,"builtin"));
    ps_reader_enter(PS_ROUTE_COUNT);app_settings_set_font_path(TTF_FONT_DEFAULT_SD);ps_reader_enter(PS_SHELF);
    assert(!strcmp(rp.font,TTF_FONT_DEFAULT_SD)&&!strcmp(mock_font,TTF_FONT_DEFAULT_SD));
    // 即使全局首选未变，显式再次选用也应同步样例阅读首选。 / Explicitly reselecting the same system font still updates the sample-reader choice.
    ri_copy(rp.font,sizeof(rp.font),"builtin-kai");ps_reader_enter(PS_ROUTE_COUNT);
    app_settings_set_font_path(TTF_FONT_DEFAULT_SD);ps_reader_enter(PS_SHELF);assert(!strcmp(rp.font,TTF_FONT_DEFAULT_SD));
    strcpy(mock_font,"builtin");media.present=media.mounted=false;
    unsigned before=font_sd_opens;now_ms+=1000;ps_reader_tick();assert(font_sd_opens==before);
    media.present=media.mounted=true;media.media_generation++;ps_reader_tick();assert(font_sd_opens==before+1&&!strcmp(mock_font,TTF_FONT_DEFAULT_SD));
    before=font_sd_opens;media.media_generation++;ps_reader_tick();assert(font_sd_opens==before+1);
    strcpy(mock_font,"builtin");storage_held=true;before=font_sd_opens;ps_reader_tick();assert(font_sd_opens==before);storage_held=false;
    usb_held=true;ps_reader_tick();assert(font_sd_opens==before);usb_held=false;
    font_suspended=true;ps_reader_tick();assert(font_sd_opens==before);font_suspended=false;
    media.exported=true;ps_reader_tick();assert(font_sd_opens==before);media.exported=false;
    ps_reader_tick();assert(font_sd_opens==before+1&&!strcmp(mock_font,TTF_FONT_DEFAULT_SD));
    strcpy(mock_font,"builtin");font_missing=true;media.media_generation++;before=font_sd_opens;
    for(int i=0;i<100;i++){now_ms+=100;ps_reader_tick();}
    assert(font_sd_opens==before+3&&!strcmp(mock_font,"builtin")&&!strcmp(preferred_font,TTF_FONT_DEFAULT_SD));
    font_missing=false;now_ms+=10000;ps_reader_tick();assert(font_sd_opens==before+3);
    media.media_generation++;ps_reader_tick();assert(font_sd_opens==before+4&&!strcmp(mock_font,TTF_FONT_DEFAULT_SD));
    font_wrong_success=true;strcpy(mock_font,"builtin-serif");media.media_generation++;ps_reader_tick();
    assert(!strcmp(mock_font,"builtin")&&!strcmp(preferred_font,TTF_FONT_DEFAULT_SD));font_wrong_success=false;
    // 同一卡重新挂载后恢复断卡前首选，且原文锚点不变。 / Restore the detached preference after remount without moving the reading anchor.
    ri_copy(rp.font,sizeof(rp.font),TTF_FONT_DEFAULT_SD);detached_font[0]=0;media.media_generation++;
    ps_reader_open(3);complete();size_t saved=anchor;media.mounted=media.present=false;ps_reader_media_lost();
    assert(!strcmp(detached_font,TTF_FONT_DEFAULT_SD));now_ms+=1000;ps_reader_tick();assert(!strcmp(mock_font,"builtin"));
    media.mounted=media.present=true;media.media_generation++;ps_reader_tick();complete();
    assert(!detached_font[0]&&!strcmp(rp.font,TTF_FONT_DEFAULT_SD)&&anchor==saved);
    rp=original;anchor=original_anchor;detached_font[0]=0;reader_font_sync(true);layout_begin();complete();
}
static void test_real_annotations(void){
    uint32_t before=rr->count;
    ps_book_anchor_t a={.path="/sdcard/books/真实书.epub",.title="真实书",.file_bytes=1703538,.chapter=2,.start=3,.end=30,.quote="真实划线内容，用于摘录和回到原文。"};
    ps_book_range_t ranges[8];assert(ps_reader_real_highlight(&a)&&rr->count==before+1);
    assert(ps_reader_real_highlight(&a)&&rr->count==before+1);
    assert(ps_reader_real_ranges(a.path,a.file_bytes,a.chapter,ranges,8)==1&&ranges[0].start==3&&ranges[0].end==30);
    assert(!ps_reader_real_ranges(a.path,a.file_bytes+1,a.chapter,ranges,8));
    int excerpts=ps_reader_excerpt_count();char quote[512],source[160];assert(ps_reader_excerpt(excerpts-1,quote,sizeof(quote),source,sizeof(source))&&!strcmp(quote,a.quote)&&strstr(source,"真实书"));
    ps_reader_real_records(a.path,a.file_bytes);assert(route==PS_RECORDS&&real_return_enabled);uint16_t ids[RECORD_MAX];record_tab=REC_HIGHLIGHT;assert(filtered_records(ids)==1&&ids[0]==(int)before);
    for(int land=0;land<2;land++){view=land?(ps_view_t){640,360,true}:(ps_view_t){360,640,false};selected_record=-1;render_checked();selected_record=before;render_checked();}
    annotation_open_ok=false;int calls=legacy_calls;record_jump(before);assert(legacy_calls==calls);
    annotation_open_ok=true;record_jump(before);assert(legacy_calls==calls+1&&!strcmp(opened_path,a.path)&&annotation_size==a.file_bytes&&annotation_chapter==2&&annotation_start==3&&annotation_end==30);
    selected_record=-1;ps_reader_real_note(&a);assert(route==PS_EDITOR&&editor_note&&editor_real_note);
    assert(editor_append("保存到真实书的批注"));int writes=stores;now_ms+=5000;ps_reader_tick();assert(stores==writes);
    now_ms+=1100;strcpy(composition,"ni");ps_reader_tick();assert(stores==writes);composition[0]=0;
    editor_pressed=0;ps_reader_tick();assert(stores==writes);editor_pressed=-1;
    background_allowed=false;ps_reader_tick();assert(stores==writes);background_allowed=true;
    ps_reader_tick();assert(stores==writes+1&&!strcmp(real_draft.text,"保存到真实书的批注"));
    assert(draft_save()&&stores==writes+1);
    editor_note=editor_real_note=false;reload_reader_no_writes();ps_reader_real_note(&a);assert(editor_real_note&&!strcmp(editor_text,"保存到真实书的批注")&&real_draft.anchor.start==3);
    uint32_t count=rr->count;fail_store=true;editor_commit();assert(rr->count==count&&editor_note&&editor_real_note&&route==PS_EDITOR);fail_store=false;editor_commit();assert(rr->count==count+1&&!editor_note&&!editor_real_note);
    assert(rr->item[count]->book==REAL_BOOK&&rr->item[count]->chapter==2&&!strcmp(rr->item[count]->text,"保存到真实书的批注"));
    assert(ps_reader_real_ranges(a.path,a.file_bytes,a.chapter,ranges,8)==2);
    // 点按仅命中同一文件/章节的已保存范围，末端留白和书签不会误开；删除失败回滚。
    // Tap only saved ranges in the exact file/chapter; exclude the end and bookmarks, and roll back failed deletion.
    int untouched=stores;
    assert(!ps_reader_real_open_at(NULL,a.file_bytes,a.chapter,3));
    assert(!ps_reader_real_open_at(a.path,a.file_bytes+1,a.chapter,3));
    assert(!ps_reader_real_open_at(a.path,a.file_bytes,a.chapter+1,3));
    assert(!ps_reader_real_open_at(a.path,a.file_bytes,a.chapter,2));
    assert(!ps_reader_real_open_at(a.path,a.file_bytes,a.chapter,30));
    assert(ps_reader_real_open_at(a.path,a.file_bytes,a.chapter,3));
    assert(record_tab==REC_NOTE&&selected_record==(int)count&&detail_scroll==0&&stores==untouched);
    view=(ps_view_t){360,640,false};uint32_t existing=rr->count;
    ps_event_t remove={.type=PS_TAP,.x=260,.y=570};
    fail_store=true;assert(records_event(&remove));fail_store=false;
    assert(rr->count==existing&&selected_record==(int)count);
    assert(records_event(&remove)&&rr->count==existing-1&&selected_record==-1);
    reload_reader_no_writes();assert(rr->count==existing-1);
    assert(ps_reader_real_open_at(a.path,a.file_bytes,a.chapter,29)&&record_tab==REC_HIGHLIGHT);
    assert(records_event(&remove)&&rr->count==existing-2);
    assert(!ps_reader_real_open_at(a.path,a.file_bytes,a.chapter,3));
    assert(ps_reader_real_highlight(&a));ps_reader_real_note(&a);assert(editor_append("保存到真实书的批注"));editor_commit();
    ps_book_anchor_t bookmark=a;bookmark.end=bookmark.start;assert(ps_reader_real_bookmark(&bookmark));
    // 全局记录页清除逐书过滤，阅读内入口保留原书与独立笔记/划线标签。
    // Global records clear the book filter; in-book entry retains its source and the independent note/highlight tab.
    ps_reader_real_records_tab(a.path,a.file_bytes,0);assert(route==PS_RECORDS&&!strcmp(record_filter_path,a.path)&&record_filter_bytes==a.file_bytes&&record_tab==0&&selected_record==-1);
    assert(filtered_records(ids)==1&&rr->item[ids[0]]->type==REC_NOTE);
    ps_reader_real_records_tab(a.path,a.file_bytes,1);assert(record_tab==1&&filtered_records(ids)==1&&rr->item[ids[0]]->type==REC_HIGHLIGHT);
    ps_reader_records_tab(2);assert(!record_filter_path[0]&&!record_filter_bytes&&record_tab==2&&record_scroll==0&&selected_record==-1&&filtered_records(ids)>=1);
    ps_reader_records_tab(99);assert(record_tab==2);ps_reader_records_tab(-1);assert(!record_tab);
    ps_book_anchor_t bad=a;strcpy(bad.path,"/sdcard/books/../unsafe.epub");assert(!ps_reader_real_highlight(&bad));bad=a;bad.end=2;assert(!ps_reader_real_highlight(&bad));
    ps_book_anchor_t cleanup=a;cleanup.start=300;cleanup.end=330;ps_reader_real_note(&cleanup);assert(editor_append("清理失败"));assert(draft_save());assert(editor_append("也只保存一次"));
    count=rr->count;fail_store_after=3;editor_commit();fail_store_after=-1;
    assert(rr->count==count+1&&!editor_note&&real_draft_clear_pending);reload_reader_no_writes();assert(rr->count==count+1&&!real_draft.text[0]&&real_draft_clear_pending);
    now_ms+=6000;ps_reader_tick();assert(!real_draft_clear_pending);reload_reader_no_writes();assert(!real_draft.text[0]&&!real_draft_clear_pending);
    // 草稿键写入和清理同时失败，也不能阻断独立记录；重启仍保留旧草稿而不冒险删除。
    // A draft-key write/clear failure cannot block the independent note commit; retain an older draft across restart.
    ps_book_anchor_t independent=a;independent.start=400;independent.end=430;
    ps_reader_real_note(&independent);assert(editor_append("旧草稿"));assert(draft_save());assert(editor_append("与新增内容"));
    count=rr->count;fail_store_key="reader-real-d2";editor_commit();fail_store_key=NULL;
    assert(rr->count==count+1&&!editor_note&&real_draft_clear_pending&&strstr(last_toast,"笔记已保存"));
    reload_reader_no_writes();assert(rr->count==count+1&&!records_read_failed&&!strcmp(rr->item[count]->text,"旧草稿与新增内容")&&!strcmp(real_draft.text,"旧草稿"));
    memset(&real_draft,0,sizeof(real_draft));real_draft.version=1;assert(ps_store("reader-real-d2",&real_draft,sizeof(real_draft)));
    ps_book_anchor_t deferred=a;deferred.start=600;deferred.end=630;ps_reader_real_note(&deferred);assert(editor_append("只返回一次"));int backs=back_calls;back_deferred=true;editor_commit();back_deferred=false;assert(back_calls==backs+1&&!editor_note);
    // 对象或最终索引失败均保持旧代，重启不丢原记录。 / Object or final-index failure keeps the committed generation across reload.
    count=rr->count;reader_index_v3_t old_index;assert(ps_load("reader-r3-index",&old_index,sizeof(old_index)));
    for(int stage=0;stage<2;stage++){
        ps_book_anchor_t next=a;next.start=100+(uint32_t)stage;next.end=next.start+5;
        fail_store_after=stage;assert(!ps_reader_real_highlight(&next));fail_store_after=-1;assert(rr->count==count);
        reader_index_v3_t index;assert(ps_load("reader-r3-index",&index,sizeof(index))&&!memcmp(&index,&old_index,sizeof(index)));
        reload_reader_no_writes();assert(rr->count==count&&!records_read_failed);
    }
    while(rr->count<70){
        ps_book_anchor_t next=a;next.start=1000+rr->count*3;next.end=next.start+3;int writes=stores;
        assert(ps_reader_real_highlight(&next)&&stores==writes+2);
    }
    count=rr->count;reload_reader_no_writes();assert(rr->count==count&&!records_read_failed);
    assert(ps_reader_real_ranges(a.path,a.file_bytes,a.chapter,ranges,1)==1);
    ps_navigate(PS_SHELF);assert(!record_filter_path[0]);view=(ps_view_t){360,640,false};
    puts("PASS: 70 immutable objects, two writes per new record, restart/index rollback, draft-key failure independent of commit, deferred drafts skip composition/touch.");
}
static void test_legacy_annotation_migration(void){
    blobs_clear(blobs);
    typedef struct { uint32_t version,count;reader_legacy_record_t item[LEGACY_RECORD_MAX]; } legacy_t;
    legacy_t *old=calloc(1,sizeof(*old));assert(old);old->version=1;old->count=2;
    old->item[0]=(reader_legacy_record_t){.book=3,.type=REC_HIGHLIGHT,.offset=0,.end=3};strcpy(old->item[0].quote,"旧划线");
    old->item[1]=(reader_legacy_record_t){.book=3,.type=REC_NOTE,.offset=3,.end=3,.ink_count=1};strcpy(old->item[1].text,"旧笔记完整迁移");old->item[1].ink[0]=(reader_ink_t){0x8000|25,30};
    assert(ps_store("reader-records-v1",old,sizeof(*old)));reload_reader_no_writes();assert(rr->count==2&&!records_read_failed&&rr->item[0]->book==3&&!strcmp(rr->item[1]->text,"旧笔记完整迁移")&&rr->item[1]->ink[0].y==30);
    assert(records_save());legacy_t *again=calloc(1,sizeof(*again));assert(again&&ps_load("reader-records-v1",again,sizeof(*again))&&!memcmp(old,again,sizeof(*old)));
    reload_reader_no_writes();assert(rr->count==2&&rr->item[0]->type==REC_HIGHLIGHT&&rr->item[1]->ink_count==1);
    free(old);free(again);
}
#ifdef PICO_READER_NO_FIXTURES
static void test_production_no_samples(void){
    assert(rr&&!rr->count&&!pages&&!open_book&&!comic_valid&&!ps_reader_excerpt_count());
    for(int i=0;i<BOOK_COUNT;i++)assert(!rb[i].text);
    test_real_shelf();
    for(int land=0;land<2;land++){
        view=land?(ps_view_t){640,360,true}:(ps_view_t){360,640,false};ps_navigate(PS_IMPORT);render_checked();
        int calls=legacy_calls,cols=land?2:1,bw=(view.w-32-(cols-1)*12)/cols;
        tap(16+(3%cols)*(bw+12)+5,124+(3/cols)*48+10);
        assert(route==PS_IMPORT&&legacy_calls==calls&&!open_book);
        ps_navigate(PS_READING_SETTINGS);render_checked();tap(30,view.h-115);
        assert(route==PS_READING_SETTINGS&&!open_book&&legacy_calls==calls);
        ps_reader_open(3);assert(route==PS_SHELF&&!open_book&&legacy_calls==calls);
        ps_navigate(PS_READ);tap(50,100);assert(route==PS_SHELF&&!open_book);
    }
    typedef struct {uint32_t version,count;reader_legacy_record_t item[LEGACY_RECORD_MAX];} old_records_t;
    old_records_t old={.version=1,.count=2};old.item[0].book=3;old.item[0].type=REC_NOTE;old.item[0].offset=17;old.item[0].end=29;
    strcpy(old.item[0].quote,"用户保存的旧引用");strcpy(old.item[0].text,"不得因示例移除丢失的笔记");
    old.item[1].book=0;old.item[1].type=REC_HIGHLIGHT;old.item[1].end=6;strcpy(old.item[1].quote,"旧划线");
    assert(ps_store("reader-records-v1",&old,sizeof(old)));int writes=stores;records_free(rr);rr=NULL;initialized=false;ps_reader_init();
    assert(stores==writes&&rr->count==2&&!records_read_failed&&!pages&&!open_book);
    assert(!strcmp(rr->item[0]->quote,old.item[0].quote)&&!strcmp(rr->item[0]->text,old.item[0].text)&&ps_reader_excerpt_count()==1);
    reader_record_t invalid=*rr->item[0];invalid.end=167347;assert(!valid_record(&invalid));
    reader_record_t comic={.book=4,.type=REC_BOOKMARK,.offset=7,.end=7};assert(valid_record(&comic));comic.end=8;assert(!valid_record(&comic));
    ps_navigate(PS_RECORDS);int calls=legacy_calls;record_jump(0);assert(route==PS_RECORDS&&legacy_calls==calls&&!open_book&&rr->count==2&&stores==writes);
    assert(strstr(last_toast,"原文已移除"));render_checked();
    ps_book_anchor_t a={.path="/sdcard/books/真实书.epub",.title="真实书",.file_bytes=1800,.chapter=2,.start=3,.end=30,.quote="真实划线内容"};
    assert(ps_reader_real_highlight(&a)&&rr->count==3&&ps_reader_excerpt_count()==2);
    writes=stores;records_free(rr);rr=NULL;initialized=false;ps_reader_init();assert(stores==writes&&rr->count==3&&!records_read_failed&&!pages);
    assert(!strcmp(rr->item[0]->text,old.item[0].text)&&rr->item[2]->book==REAL_BOOK);
    record_jump(2);assert(legacy_calls==calls+1&&!strcmp(opened_path,a.path));
    for(int land=0;land<2;land++){
        view=land?(ps_view_t){640,360,true}:(ps_view_t){360,640,false};
        ps_edit("独立输入测试","",32,done);editor_mode(INPUT_PINYIN);strcpy(composition,"nihao");candidates_update();assert(candidate_count&&!strcmp(candidates[0],"你好"));
        assert(candidate_select(0)&&!strcmp(editor_text,"你好"));render_checked();
    }
    printf("PASS: production reader links without sample assets, no bundled sources/default excerpts, local-file-only navigation, old note/highlight/comic records preserved, real annotation/Chinese IME usable.\n");
}
#endif
// 取消手写输入后仍须保留旧偏好与笔迹附件，不依赖识别器或TF模型。 / Retired handwriting input must preserve old preferences/ink without a recognizer or TF model.
static void test_retired_handwriting_compatibility(void){
    assert(INK_MAX==64&&sizeof(reader_ink_t)==4&&sizeof(((reader_record_t*)0)->ink)==256&&sizeof(draft.ink)==256&&sizeof(real_draft.ink)==256);
    assert(INPUT_PINYIN==0&&INPUT_T9==1&&INPUT_ENGLISH==3&&INPUT_SYMBOL==4&&INPUT_MODE_COUNT==4);
    reader_prefs_t saved=rp;
    for(int mode=0;mode<5;mode++){
        saved.input=(uint8_t)mode;assert(ps_store("reader-prefs-v1",&saved,sizeof(saved)));
        records_free(rr);free(pages);rr=NULL;pages=NULL;initialized=false;int writes=stores;ps_reader_init();assert(stores==writes&&rr);
        assert(rp.input==(mode==2?INPUT_PINYIN:mode));
        ps_edit("输入兼容测试","",383,done);editor_mode(mode);assert(input_mode==(mode==2?INPUT_PINYIN:mode));
        assert(!ps_reader_editor_ink_mode()&&!ps_reader_editor_ink_active());ps_reader_editor_ink_cancel();
        editor_key_t keys[64];int n=editor_keys(keys),modes=0;
        for(int i=0;i<n;i++)if(keys[i].action==EK_MODE){assert(keys[i].value!=2&&!strstr(keys[i].label,"手写"));modes++;}
        assert(modes==4);
    }
    ps_book_anchor_t a={.path="/sdcard/books/legacy-ink.epub",.title="旧笔迹兼容",.file_bytes=100,.chapter=0,.start=0,.end=3,.quote="原文"};
    reader_real_draft_v1_t olddraft={.version=1,.anchor=a,.ink_count=2};
    olddraft.ink[0]=(reader_ink_t){0x8000|25,30};olddraft.ink[1]=(reader_ink_t){400,800};
    blob_remove(blobs,"reader-real-d2");assert(ps_store("reader-real-d1",&olddraft,sizeof(olddraft)));
    records_free(rr);free(pages);rr=NULL;pages=NULL;initialized=false;int writes=stores;ps_reader_init();assert(stores==writes&&rr);
    ps_reader_real_note(&a);assert(editor_real_note&&editor_ink_count==2&&!memcmp(editor_ink,real_draft.ink,sizeof(editor_ink)));
    assert(editor_append("继续旧笔记"));assert(draft_save()&&real_draft.ink_count==2);
    uint32_t before=rr->count;editor_commit();assert(rr->count==before+1&&!editor_note&&rr->item[before]->ink_count==2&&rr->item[before]->ink[1].y==800);
    records_free(rr);free(pages);rr=NULL;pages=NULL;initialized=false;writes=stores;ps_reader_init();assert(stores==writes&&rr);
    assert(rr->item[before]->ink_count==2&&rr->item[before]->ink[0].x==(0x8000|25)&&rr->item[before]->ink[1].y==800);
    record_filter_path[0]=0;record_filter_bytes=0;export_records();char path[128];snprintf(path,sizeof(path),"%s/books/.pico-reader/reading-records.txt",rootdir);
    FILE *f=fopen(path,"rb");assert(f);char out[32768];size_t bytes=fread(out,1,sizeof(out)-1,f);out[bytes]=0;assert(!ferror(f)&&!fclose(f));
    assert(strstr(out,"继续旧笔记")&&strstr(out,"手写笔迹 2 点"));
    puts("PASS: four text input modes, retired mode fallback, preserved mode IDs and legacy ink draft/save/reload/export without a recognizer.");
}
#include "pico_reader_records_test.inc"
#include "pico_reader_checkpoint_test.inc"
#include "pico_reader_snapshot_test.inc"
int main(int argc,char **argv){
    assert(argc==2);snprintf(rootdir,sizeof(rootdir),"%s",argv[1]);char books[64];snprintf(books,sizeof(books),"%s/books",rootdir);assert(!mkdir(books,0700));ps_reader_init();assert(initialized&&stores==0&&rr);assert(sizeof(reader_record_chunk_t)<65536);assert(rp.size==25&&rp.weight==1);
#ifdef PICO_READER_NO_FIXTURES
    test_production_no_samples();
#else
    assert(pages);
    test_reflow();test_real_shelf();test_shelf_cover_pagination();test_committed_cover_sources();test_shelf_loading_redraws();test_shelf_view_controls();test_typography();test_records();test_input();test_editor_limits();test_comic_and_routes();test_direct_toc();test_font_routes();test_default_font_recovery();test_media_lost();test_reloads();test_real_annotations();test_preference_compatibility();test_legacy_annotation_migration();test_expanded_records_and_merge();test_reader_power_checkpoints();test_reader_snapshot_export();
    printf("PASS: real-file cached shelf (TF root/books, 24/512 books, bounded ticks, pure status/render, paged dispatch, media/USB gates), host-only text/comic fixtures, pagination/records/IME (%d drawing calls).\n",draws);
#endif
    test_retired_handwriting_compatibility();
#ifndef PICO_READER_REAL_RENDER
    test_editor_session_font();
#endif
    shelf_scan_close();shelf_cover_display_release();free(shelf_stage_files);free(shelf_files);free(shelf_favorites);records_free(rr);free(pages);blobs_clear(blobs);blobs_clear(source_blobs);return 0;
}

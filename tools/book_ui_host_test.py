"""Compile actual shelf helpers for deterministic ordering, routing and failure regressions.

SPDX-FileCopyrightText: 2026 mindreset
SPDX-License-Identifier: Apache-2.0
中文：抽取真实静态助手，避免宿主链接硬件页面依赖。
English: Extract real helpers without linking hardware page dependencies.
公开开书接口、载入调度/绘制助手与导航使用同源函数；解析、绘图原语、字体和硬件仍为替身。
The public queue, loading scheduler/drawing helper and navigation use real functions; parsing, drawing primitives, fonts and hardware remain doubles.
"""
from pathlib import Path
import re
import subprocess
import tempfile

source = Path(__file__).resolve().parents[1] / "main/apps/app_book.c"
gesture_header = (source.parents[1] / "ui/ui_gesture.h").read_text()
gesture_types = re.search(r"typedef enum \{[\s\S]+?\} ui_gesture_t;", gesture_header).group()
gesture_source = (source.parents[1] / "ui/ui_gesture.c").read_text()
gesture_source = re.sub(r'^#include[^\n]+\n', '', gesture_source, flags=re.M)

def function(name):
    text = source.read_text(encoding="utf-8")
    import re
    found = re.search(r"^(?:[A-Za-z_][A-Za-z0-9_]*[ \t*]+)+" + name + r"\([^\n]*\) \{", text, re.M)
    assert found, name
    start, at, depth, quote, escape = found.start(), found.end(), 1, None, False
    while depth:
        c = text[at]
        if quote:
            if escape: escape = False
            elif c == "\\": escape = True
            elif c == quote: quote = None
        elif c in "\"'": quote = c
        elif c == "{": depth += 1
        elif c == "}": depth -= 1
        at += 1
    return text[start:at]

unit = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <stdio.h>
#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdatomic.h>
#include "dual_link.h"
#define ESP_OK 0
#define ESP_ERR_NO_MEM 1
#define ESP_ERR_NOT_FOUND 2
#define ESP_ERR_INVALID_SIZE 3
#define MALLOC_CAP_SPIRAM 1
#define MALLOC_CAP_8BIT 2
#define ESP_LOGI(...) ((void)0)
typedef int esp_err_t;
typedef struct {const char* title;} app_desc_t;
typedef struct {bool touched;uint8_t count;uint16_t x,y;} test_touch_t;
typedef test_touch_t cst836u_touch_t;
typedef struct {int leaf;bool request_menu,consumed,request_return,request_developer_menu,capture_gesture,pressed,released;const app_desc_t* request_app;int64_t now_ms;void* hl;uint8_t* fb;void* acc;test_touch_t* touch;} app_ctx_t;
static const app_desc_t test_home={.title="Native Home"},test_font={.title="Font Font"};
static const app_desc_t* app_home_page(void){return &test_home;}
static int app_count(void){return 2;}
static const app_desc_t* app_at(int index){return index==0?&test_home:index==1?&test_font:NULL;}
typedef enum {APP_REDRAW_PAGE,APP_REDRAW_AREA,APP_REDRAW_NONE,APP_REDRAW_FULL,APP_REDRAW_DONE} app_redraw_t;
typedef struct {int x,y,width,height;} EpdRect;
static EpdRect ui_bar_rect(int i,int count){int width=(508-(count-1)*12)/count;return(EpdRect){40+i*(width+12),1096,width,96};}
static bool ui_rect_hit(EpdRect r,int x,int y){return x>=r.x&&x<r.x+r.width&&y>=r.y&&y<r.y+r.height;}
static bool ui_menu_handle_hit_test(uint16_t x,uint16_t y){return x>600&&y>1100&&y<1300;}
typedef struct {char name[256],path[288];uint32_t size;bool is_flash,has_progress;uint8_t pct;uint32_t recent;bool selected,removed,search_match;} shelf_entry_t;
#define BOOK_ROWS 7
#define UI_BTN_H 84
static EpdRect ui_row_rect(int i,int count,int y,int height){EpdRect r=ui_bar_rect(i,count);r.y=y;r.height=height;return r;}
static char s_query[65],s_search_draft[65],s_batch_message[128];
static bool s_batch_confirm,s_batch_delete;
static bool read_pico_search_match(const char* name,const char* query){return !*query||strstr(name,query)!=NULL;}
static size_t test_chapter_count=1;
static size_t book_chapter_count(void){return test_chapter_count;}
static uint32_t test_chapter_offsets[5]={0,100,900,1000,1000};
static uint32_t book_chapter_byte_offset(size_t chapter){assert(chapter<5);return test_chapter_offsets[chapter];}
static uint32_t book_total_bytes(void){return 1000;}
#define BOOK_TOC_ROWS 10
static int s_filter;
static bool s_recent_sort;
static shelf_entry_t* s_shelf;
static size_t s_shelf_capacity;
static int s_count,s_visible_count;
static char s_message[128],s_shelf_warning[128],s_storage[128];
static bool s_pending_invalidated,test_oom,test_degraded;
static unsigned s_store_revision;
typedef struct {char path[288];bool is_flash;} book_store_root_t;
typedef struct {uint32_t file_size;uint16_t chapter;uint32_t byte_off;uint8_t px,pct;uint32_t last_open_s;} book_progress_t;
typedef struct book_progress_watch {char path[288];atomic_bool invalidated;struct book_progress_watch* next;} book_progress_watch_t;
static book_progress_watch_t* test_watches;
static book_progress_watch_t* book_progress_watch_create(const char* path){book_progress_watch_t* w=calloc(1,sizeof(*w));assert(w);strcpy(w->path,path);atomic_init(&w->invalidated,false);w->next=test_watches;test_watches=w;return w;}
static bool book_progress_watch_invalidated(const book_progress_watch_t* w){return atomic_load(&w->invalidated);}
static void book_progress_watch_destroy(book_progress_watch_t* w){book_progress_watch_t** p=&test_watches;while(*p!=w)p=&(*p)->next;*p=w->next;free(w);}
static book_store_root_t test_roots[2];
static int test_root_count=1;
static void* heap_caps_realloc(void* p,size_t n,int caps){(void)caps;return test_oom?NULL:realloc(p,n);}
static void* heap_caps_malloc(size_t n,int caps){(void)caps;return malloc(n);}
static bool book_store_flash_ready(void){return true;}
static bool book_store_roots_degraded(void){return test_degraded;}
static uint64_t book_store_free_bytes(const book_store_root_t* root){(void)root;return 1000000;}
static int book_store_roots(book_store_root_t out[2],int* n){*n=test_root_count;memcpy(out,test_roots,sizeof(test_roots));return 0;}
static bool book_progress_load(const char* p,uint32_t n,book_progress_t* out){(void)p;(void)n;*out=(book_progress_t){0};return false;}
static void loading(app_ctx_t* ctx,const char* text){(void)ctx;(void)text;}
typedef struct pending_progress {char path[288];book_progress_t value;bool dirty,progress_saved;book_progress_watch_t* watch;struct pending_progress* next;} pending_progress_t;
static pending_progress_t* s_pending;
typedef struct delete_retry {shelf_entry_t entry;struct delete_retry* next;} delete_retry_t;
static delete_retry_t* s_delete_retries;
static char s_latest_path[288],test_last_path[288];
static bool s_save_failed;
static char s_path[288], s_resume_path[288],s_resume_name[256];
static int64_t s_save_retry_ms;
#define BOOK_STORE_PATH_MAX 288
static char s_requested_path[BOOK_STORE_PATH_MAX];
static char s_open_path[BOOK_STORE_PATH_MAX];
static bool s_open_presented;
static bool s_product_session;
static unsigned test_open_calls;
static char test_open_path[288];
static bool test_open_fails;
static unsigned test_loading_displays;
static bool open_book(app_ctx_t* ctx,const char* path){(void)ctx;if(s_open_path[0])assert(s_open_presented&&test_loading_displays);test_open_calls++;strcpy(test_open_path,path);return !test_open_fails;}
static char* s_text;
static int s_unsaved,s_px=48;
static uint32_t s_file_size=1000;
static size_t s_chapter,s_page;
typedef struct {size_t offset,len;uint8_t* image;char* image_src;bool image_repeated;uint16_t image_first_chapter;bool image_title;} blk_t;
static blk_t* s_blocks;
static size_t s_block_count;
static bool s_image_open,s_cover_open,s_image_confirm_open;
static size_t s_image_confirm_block=SIZE_MAX;
#define BOOK_IMAGE_CONFIRM_CONTROL 990
static uint8_t* s_image_pixels;
static uint16_t s_image_width,s_image_height;
static size_t s_image_block=SIZE_MAX;
static const char* s_image_error;
static char s_image_origin[224];
static void prepare_page_images(void){}
static int test_image_loads,test_image_error;
static int book_chapter_load_image(size_t chapter,const char* ref,uint8_t** pixels,uint16_t* width,uint16_t* height){(void)chapter;assert(ref);++test_image_loads;*pixels=NULL;*width=*height=0;if(test_image_error)return test_image_error;*pixels=malloc(4);assert(*pixels);*width=*height=2;return ESP_OK;}
static int book_chapter_title(size_t chapter,char* out,size_t cap){snprintf(out,cap,"Fixture chapter %zu",chapter);return ESP_OK;}
static void lock_draw(void){}
static void unlock_draw(void){}
static void loading_detail(app_ctx_t* ctx,const char* text,const char* detail){(void)ctx;(void)text;(void)detail;}
static void free_image(void);
static int test_save_error,test_last_error,test_save_calls,test_last_calls;
static size_t book_layout_page_count(void){return 3;}
static size_t book_layout_page_start_offset(size_t page){return page*100;}
static unsigned percent(size_t page){return (unsigned)page*20;}
static int book_progress_save(const char* p,const book_progress_t* value){(void)p;(void)value;test_save_calls++;return test_save_error;}
static int book_progress_set_last_path(const char* p){test_last_calls++;if(!test_last_error)snprintf(test_last_path,sizeof(test_last_path),"%s",p);return test_last_error;}
static void save_progress(void);
static void invalidate_prep(void){}
static shelf_entry_t s_managed;
static bool s_delete_confirm,s_file_removed,s_clear_confirm;
static char s_manage_message[128];
typedef enum {SHELF,READING,TOC,MANAGE,BULK,SEARCH} book_view_t;
static book_view_t s_view,s_search_parent;
#define UI_KEY_1 1
#define UI_KEY_2 2
#define UI_KEY_3 3
static int s_pressed_control;
static bool s_scan_pending,s_toolbar;
static bool s_resume_pending,s_full,s_shake_enabled,s_sensor_on,s_reader_split,s_selection;
static int s_du_count;
static int64_t s_size_settle_ms,s_poll_ms,s_du_ms,s_sensor_ms,s_last_turn_ms;
static EpdRect s_area,s_du_area,s_layout_rect;
enum EpdDrawMode {MODE_GL16,MODE_DU};
static enum EpdDrawMode s_mode=MODE_GL16;
static void* s_prep_task,*s_prep_done,*s_draw_lock,*s_next_fb;
enum EpdRotation {EPD_ROT_LANDSCAPE=0,EPD_ROT_PORTRAIT=1,EPD_ROT_INVERTED_LANDSCAPE=2,EPD_ROT_INVERTED_PORTRAIT=3};
static unsigned test_rotation_calls;
static int test_rotation=EPD_ROT_INVERTED_PORTRAIT;
static enum EpdRotation s_book_rotation=EPD_ROT_INVERTED_PORTRAIT;
static enum EpdRotation epd_get_rotation(void){return (enum EpdRotation)test_rotation;}
static void epd_set_rotation(int rotation){++test_rotation_calls;test_rotation=rotation;}
static void ensure_prep(void){}
static int app_settings_book_px(void){return 48;}
static bool app_settings_book_shake(void){return false;}
static void read_pico_sd_start_probe(void){}
static void sensor_set(app_ctx_t* ctx,bool on){(void)ctx;(void)on;}
static void vTaskDelete(void* p){(void)p;}
static void vSemaphoreDelete(void* p){(void)p;}
static unsigned test_turns;static int test_last_turn_dir;
static app_redraw_t turn_page(app_ctx_t* ctx,int dir){(void)ctx;test_last_turn_dir=dir;++test_turns;return APP_REDRAW_NONE;}
/* HOST_GESTURE_TYPES */
static EpdRect test_body={40,24,604,960};
static EpdRect body_rect(void){return test_body;}
static int test_reader_width=684,test_reader_height=1216;
static int reader_width(void){return test_reader_width;}
static int reader_height(void){return test_reader_height;}
static int reader_footer_top(void);
static int toc_rows(void){return reader_height()>reader_width()?10:3;}
static int selection_control_at(uint16_t x,uint16_t y,EpdRect* rect);
static app_redraw_t selection_touch(app_ctx_t* ctx,uint16_t x,uint16_t y);
static EpdRect progress_back_rect(void);
static EpdRect tool_rect(int i);
static bool s_jump_back_valid;
static int image_confirm_control_at(uint16_t x,uint16_t y,EpdRect* rect);
static int control_at(app_ctx_t* ctx,uint16_t x,uint16_t y,EpdRect* rect){(void)ctx;if(s_image_confirm_open)return image_confirm_control_at(x,y,rect);if(s_jump_back_valid){*rect=progress_back_rect();if(ui_rect_hit(*rect,x,y))return 950;}if(s_selection)return selection_control_at(x,y,rect);if(s_toolbar)for(int i=0;i<9;i++){*rect=tool_rect(i);if(ui_rect_hit(*rect,x,y))return 200+i;}*rect=(EpdRect){48,100,588,88};return ui_rect_hit(*rect,x,y)?1000:-1;}
static app_redraw_t open_image(app_ctx_t* ctx,size_t block);
static app_redraw_t image_confirm_request(size_t block);
static app_redraw_t progress_commit(app_ctx_t* ctx,bool back);
static app_redraw_t action_at(app_ctx_t* ctx,uint16_t x,uint16_t y){if(s_jump_back_valid&&ui_rect_hit(progress_back_rect(),x,y))return progress_commit(ctx,true);if(s_selection)return selection_touch(ctx,x,y);return image_confirm_request(0);}
static app_redraw_t paint_control(app_ctx_t* ctx,EpdRect rect){(void)ctx;s_area=rect;s_mode=MODE_DU;s_reader_split=false;return APP_REDRAW_AREA;}
static int test_delete_error,test_forget_error,test_notify_count,test_delete_calls;
static bool test_removed,test_mixed;
static int book_store_delete(const char* path,bool* removed){(void)path;test_delete_calls++;*removed=test_removed;return test_mixed ? (strstr(path,"book001") ? -1 : 0) : test_delete_error;}
static int book_progress_forget(const char* path){for(book_progress_watch_t* w=test_watches;w;w=w->next)if(!strcmp(w->path,path))atomic_store(&w->invalidated,true);return test_forget_error;}
static void book_store_notify_changed(void){test_notify_count++;}
static unsigned book_store_revision(void){return (unsigned)test_notify_count;}
static void free_book(void){s_text=NULL;s_path[0]=0;free_image();}
static char test_wrapped[512];
static int test_text_bottom;
#define UI_PX_CAPTION 28
#define UI_MARGIN 40
#define EPD_DRAW_ALIGN_LEFT 0
static int ui_content_width(void){return 604;}
static int ttf_text_width_px(int px,const char* text){int width=0;for(;*text;text++)if(((unsigned char)*text&0xc0)!=0x80)width+=px;return width;}
static void ui_text(uint8_t* fb,int x,int y,int px,const char* text,int align,bool inv){(void)fb;(void)x;(void)align;(void)inv;if(y+px>test_text_bottom)test_text_bottom=y+px;assert(strlen(test_wrapped)+strlen(text)<sizeof(test_wrapped));strcat(test_wrapped,text);}
static unsigned test_loading_renders,test_display_calls,test_clear_calls;
static unsigned test_page_displays,test_area_displays;
static EpdRect test_display_area;
static int test_display_mode;
static void ui_clear_page(uint8_t* fb){(void)fb;++test_clear_calls;test_wrapped[0]=0;}
static void ui_draw_header(uint8_t* fb,const char* title,const char* detail){(void)fb;assert(title&&detail);}
static void ui_draw_button(uint8_t* fb,EpdRect rect,const char* text,bool pressed){(void)fb;(void)rect;(void)pressed;assert(!strcmp(text,"返回书架"));}
static void ui_draw_menu_handle(uint8_t* fb,bool open){(void)fb;(void)open;}
static bool draw_pending_open(uint8_t* fb);
static void render(app_ctx_t* ctx,uint8_t* fb){(void)ctx;if(draw_pending_open(fb))++test_loading_renders;}
static int64_t esp_timer_get_time(void){return 1000000;}
static bool kick_prep(void){return false;}
enum EpdDrawError {EPD_DRAW_SUCCESS=0};
static enum EpdDrawError test_display(void){++test_display_calls;if(s_open_path[0]){assert(test_loading_renders);++test_loading_displays;}return EPD_DRAW_SUCCESS;}
static enum EpdDrawError update_display_full(void* hl){(void)hl;++test_page_displays;return test_display();}
static enum EpdDrawError update_display_mode(void* hl,int mode){(void)hl;test_display_mode=mode;++test_page_displays;return test_display();}
static enum EpdDrawError update_display_area_with(void* hl,const void* waveform,int mode,EpdRect area){(void)hl;(void)waveform;test_display_mode=mode;test_display_area=area;++test_area_displays;return test_display();}
static void guard_draw_result(void* hl,enum EpdDrawError err){(void)hl;assert(err==EPD_DRAW_SUCCESS);}
static void xSemaphoreTake(void* semaphore,int timeout){(void)semaphore;(void)timeout;}
static EpdRect ui_rect_union(EpdRect a,EpdRect b){(void)b;return a;}
static EpdRect progress_rect(void);
static EpdRect progress_back_rect(void);
static unsigned test_progress_paints;
static app_redraw_t paint_progress(app_ctx_t* ctx,enum EpdDrawMode mode){(void)ctx;(void)mode;++test_progress_paints;return APP_REDRAW_AREA;}
static unsigned test_seek_calls;static bool test_seek_fails,test_seek_source;static size_t test_seek_chapter,test_seek_offset;
static bool load_chapter_position(app_ctx_t* ctx,size_t chapter,size_t offset,bool last,bool source){(void)ctx;assert(!last);++test_seek_calls;test_seek_chapter=chapter;test_seek_offset=offset;test_seek_source=source;if(test_seek_fails)return false;s_chapter=chapter;s_page=offset/100;return true;}
static const int E0470_WAVEFORM=0,E0470_FOLLOW_WAVEFORM=1;
#define APP_PAGE_REFRESH_MODE MODE_GL16
#define portMAX_DELAY 0
#define UI_SETTLE_DU_MAX 8
#define UI_SETTLE_IDLE_MS 600
static char test_preferred_font[192]="builtin",test_current_font[192]="builtin",s_font_path[192];
#define TTF_FONT_BUILTIN "builtin"
static unsigned test_font_load_calls,test_builtin_load_calls;
static bool test_font_fails,test_font_ready=true;
static const char* app_settings_font_path(void){return test_preferred_font;}
static const char* ttf_font_path(void){return test_current_font;}
static bool ttf_font_ready(void){return test_font_ready;}
static bool ttf_font_path_is_builtin(const char* path){return !path||!*path||!strncmp(path,"builtin",7);}
static bool ttf_font_is_builtin(void){return ttf_font_path_is_builtin(test_current_font);}
static int ttf_font_open(const char* path){assert(s_open_presented&&test_loading_displays);if(ttf_font_path_is_builtin(path))++test_builtin_load_calls;else ++test_font_load_calls;if(!test_font_fails)strcpy(test_current_font,path);return test_font_fails?-1:ESP_OK;}
static bool book_progress_last_path(char* out,size_t cap){(void)out;(void)cap;return false;}
typedef struct {bool mounted;} read_pico_sd_info_t;
#define ESP_ERR_NOT_FINISHED 4
static int read_pico_sd_get_info(read_pico_sd_info_t* info){info->mounted=true;return ESP_OK;}
static unsigned test_layout_builds;static EpdRect test_layout_area;
static bool book_layout_build_blocks(char* text,size_t length,blk_t* blocks,size_t count,EpdRect area,int px){(void)text;(void)length;(void)blocks;(void)count;(void)px;++test_layout_builds;test_layout_area=area;return true;}
static size_t s_text_len;
static size_t book_layout_page_for_offset(size_t off){return off/100;}
static bool book_layout_complete(void){return true;}
static bool book_layout_extend(int pages){(void)pages;return true;}
static app_redraw_t paint_reading(app_ctx_t* ctx,int mode){(void)ctx;(void)mode;return APP_REDRAW_AREA;}
typedef struct {uint8_t aoi2_src;} sc7a20h_events_t;
static bool sc7a20h_powered(void* acc){(void)acc;return true;}
static int sc7a20h_read_events(void* acc,sc7a20h_events_t* ev){(void)acc;(void)ev;return -1;}
static int s_shake;
static bool book_shake_feed(int* shake,bool active,bool suppressed,int64_t now){(void)shake;(void)active;(void)suppressed;(void)now;return false;}
typedef struct {char path[288],title[96];uint64_t file_bytes;uint32_t chapter,start,end;char quote[512];} ps_book_anchor_t;
typedef enum {BOOK_GLOBAL_TOOLS,BOOK_GLOBAL_READING,BOOK_GLOBAL_FONT,BOOK_GLOBAL_BACK,BOOK_GLOBAL_RECORDS,BOOK_GLOBAL_SELECT,BOOK_GLOBAL_BOOKMARK,BOOK_GLOBAL_TOC,BOOK_GLOBAL_HIGHLIGHTS,BOOK_GLOBAL_NOTES,BOOK_GLOBAL_FAVORITE,BOOK_GLOBAL_HIGHLIGHT,BOOK_GLOBAL_NOTE,BOOK_GLOBAL_PAGE,BOOK_GLOBAL_BOOKMARKS} app_book_global_action_t;
static bool s_annotation_pending;
static uint8_t s_selection_edge=1;
static size_t s_selection_start,s_selection_end;
static bool s_selection_drag,s_selection_menu,s_drag_previous,s_scrubbing;
static size_t s_drag_fixed,s_drag_before_start,s_drag_before_end,s_scrub_chapter,s_jump_chapter,s_jump_offset;
static int s_drag_dx,s_drag_dy,s_selection_menu_x,s_selection_menu_y,s_scrub_x;
static uint32_t s_scrub_target;
static EpdRect s_handle_start,s_handle_end;
static bool s_handle_start_visible,s_handle_end_visible;
static ps_book_anchor_t s_annotation_target,test_annotation;
static char s_last_product_path[288];
static bool test_annotation_save=true,test_book_return;
static unsigned test_highlights,test_notes,test_bookmarks,test_records;
static unsigned test_global_controls;
static unsigned test_system_media_lost,test_system_media_restored;
static void ps_media_lost(void){++test_system_media_lost;}
static void ps_media_restored(void){++test_system_media_restored;}
static int test_route;
enum {PS_SHELF};
static const app_desc_t app_book={.title="Real book"};
static void ps_global_book_return(bool enabled){test_book_return=enabled;}
static void ps_navigate(int route){test_route=route;}
static void ps_global_draw_controls(const app_desc_t* app,uint8_t* fb){(void)fb;assert(app==&app_book);++test_global_controls;}
static void ps_toast(const char* text){assert(text);}
static bool ps_reader_real_highlight(const ps_book_anchor_t* a){++test_highlights;test_annotation=*a;return test_annotation_save;}
static bool ps_reader_real_bookmark(const ps_book_anchor_t* a){++test_bookmarks;test_annotation=*a;return test_annotation_save;}
static void ps_reader_real_note(const ps_book_anchor_t* a){++test_notes;test_annotation=*a;test_book_return=true;}
static void ps_reader_real_records(const char* path,uint64_t bytes){assert(!strcmp(path,s_path)&&bytes==s_file_size);++test_records;test_book_return=true;}
static int test_records_tab;
static void ps_reader_real_records_tab(const char* path,uint64_t bytes,int tab){test_records_tab=tab;ps_reader_real_records(path,bytes);}
static bool test_file_favorite;
static bool ps_reader_file_favorite(const char* path,bool toggle){assert(path);if(toggle)test_file_favorite=!test_file_favorite;return toggle?true:test_file_favorite;}
static size_t test_text_hit=SIZE_MAX;static int test_text_hit_x,test_text_hit_y;
static size_t book_layout_text_at(size_t page,EpdRect rect,int x,int y){(void)page;(void)rect;test_text_hit_x=x;test_text_hit_y=y;return test_text_hit;}
static int test_nearest_x,test_nearest_y;
static size_t book_layout_text_nearest(size_t page,EpdRect rect,int x,int y){(void)page;(void)rect;test_nearest_x=x;test_nearest_y=y;return test_text_hit;}
static bool book_layout_text_bounds(size_t page,EpdRect rect,size_t at,EpdRect* out){(void)page;(void)rect;*out=(EpdRect){100+(int)at*10,300,20,48};return at<s_text_len;}
static void goto_font(app_ctx_t* ctx);
static app_redraw_t product_exit(app_ctx_t* ctx);
static app_redraw_t annotation_action(app_ctx_t* ctx,int action);
static app_redraw_t app_book_global_action(app_ctx_t* ctx,app_book_global_action_t action);
#define UI_LOCK_WIDTH 684
#define UI_LOCK_HEIGHT 1216
#define UI_CONTENT_TOP 176
#define UI_BAR_TOP 1096
#define UI_GAP 12
'''
unit += "\n".join(function(n) for n in ("reader_footer_top", "progress_rect", "progress_back_rect"))
unit += r'''
#define BOOK_PAGE_CONTROL 960
#define BOOK_PAGE_DIGITS 10
#define UI_TOUCH_SLOP_PX 24
#define UI_LONG_PRESS_MS 500
#define UI_SWIPE_MIN_PX 64
#define BOOK_TOOL_ROWS 3
static int test_font_weight=400;
static int ttf_get_weight(void){return test_font_weight;}
typedef struct {char* utf8;size_t len;blk_t* blocks;size_t count;} html_text_t;
typedef struct book_layout_job {size_t count,total;} book_layout_job_t;
static unsigned test_count_loads,test_count_steps;
static bool test_count_load_fail,test_count_create_fail,test_count_step_fail;
static size_t test_count_pages[5]={3,5,2,4,1};
static int book_chapter_load_blocks_for_pagination(size_t chapter,html_text_t* out){++test_count_loads;memset(out,0,sizeof(*out));if(test_count_load_fail)return -1;out->len=test_count_pages[chapter]*100;out->utf8=malloc(out->len+1);assert(out->utf8);memset(out->utf8,'x',out->len);out->utf8[out->len]=0;return ESP_OK;}
static void html_text_free(html_text_t* loaded){free(loaded->utf8);memset(loaded,0,sizeof(*loaded));}
static book_layout_job_t* book_layout_job_create(const char* text,size_t len,const blk_t* blocks,size_t count,EpdRect rect,int px){(void)text;(void)blocks;(void)count;(void)rect;(void)px;if(test_count_create_fail)return NULL;book_layout_job_t* job=malloc(sizeof(*job));assert(job);job->total=len/100;job->count=job->total<2?job->total:2;return job;}
static bool book_layout_job_step(book_layout_job_t* job,size_t budget){assert(job);++test_count_steps;if(test_count_step_fail)return false;job->count+=budget;if(job->count>job->total)job->count=job->total;return true;}
static bool book_layout_job_complete(const book_layout_job_t* job){return job&&job->count==job->total;}
static size_t book_layout_job_page_count(const book_layout_job_t* job){return job?job->count:0;}
static bool book_layout_job_page_offset(const book_layout_job_t* job,size_t page,size_t* offset){if(!job||page>=job->count)return false;*offset=page*100;return true;}
static void book_layout_job_free(book_layout_job_t* job){free(job);}
static void cancel_interaction(void);
static app_redraw_t close_image(void);
static bool progress_preview(int x);
static void page_index_pause(void);
static void page_index_reset(void);
static void page_jump_close(void);
static app_redraw_t page_jump_tick(app_ctx_t* ctx);
static app_redraw_t book_open_toc(app_ctx_t* ctx);
static bool page_index_matches(void);
static bool page_index_prepare(void);
static app_redraw_t page_jump_open(void);
static app_redraw_t page_jump_action(app_ctx_t* ctx,int control);
static int page_jump_control_at(uint16_t x,uint16_t y,EpdRect* rect);
static unsigned test_modal_draws;
static void page_jump_draw(uint8_t* fb){(void)fb;++test_modal_draws;}
typedef struct {
    uint32_t* counts;
    size_t chapters, done, job_chapter;
    uint64_t total;
    uint32_t file_size;
    int px, weight;
    EpdRect body;
    char path[BOOK_STORE_PATH_MAX], font[192];
    book_layout_job_t* job;
    html_text_t loaded;
    bool failed;
} page_index_t;
static page_index_t s_page_index;
static bool s_page_jump_open, s_page_jump_waiting, s_modal_key_cancelled;
static bool s_progress_pending, s_progress_dragged;
static char s_page_jump_input[BOOK_PAGE_DIGITS + 1], s_page_jump_message[128];
static uint32_t s_page_jump_target;
static int64_t s_page_jump_status_ms;

// 本套隔离单机页面回归；双机调度和传输由真实双进程测试覆盖，误入双机路径立即失败。
// Keep this suite on ordinary-reader paths; real paired-process tests cover dual scheduling/transport, and accidental dual calls fail.
static uint32_t s_dual_present_pending,s_dual_target,s_dual_reflow;
static uint8_t s_dual_px=48;
static bool s_dual_applying,s_dual_annotation,s_dual_wait;
static char s_dual_font[BOOK_STORE_PATH_MAX];
static struct {bool pending;} s_dual_anchor;
static bool ps_dual_enabled(void){return false;}
static bool ps_dual_turn(int direction){(void)direction;assert(false);return false;}
static bool ps_dual_seek(uint32_t page){(void)page;assert(false);return false;}
static bool ps_dual_open(const char* path){(void)path;assert(false);return false;}
static bool ps_dual_resume(void){assert(false);return false;}
static bool ps_dual_reflow_failed(uint32_t revision){(void)revision;assert(false);return false;}
static void ps_dual_presented(uint32_t revision){(void)revision;assert(false);}
static void ps_dual_status(dl_status_t* out){(void)out;assert(false);}
static const char* ps_dual_font(void){assert(false);return "";}
static const char* ps_dual_path(void){assert(false);return "";}
static int ps_dual_role(void){assert(false);return 1;}
static bool app_book_dual_apply(const char* path,const char* font,uint32_t target,uint8_t px,bool reflow,uint32_t revision){(void)path;(void)font;(void)target;(void)px;(void)reflow;(void)revision;assert(false);return false;}
static void dual_anchor_start(size_t chapter,size_t offset,bool source,uint32_t revision){(void)chapter;(void)offset;(void)source;(void)revision;assert(false);}
static void dual_read_cancel(void){assert(false);}
static app_redraw_t dual_read_failed(void){assert(false);return APP_REDRAW_NONE;}
static app_redraw_t dual_anchor_tick(app_ctx_t* ctx){(void)ctx;assert(false);return APP_REDRAW_NONE;}
static app_redraw_t dual_page_tick(app_ctx_t* ctx){(void)ctx;assert(false);return APP_REDRAW_NONE;}
// 无传感器输入；测试显式设置显示方向，不假装覆盖重力旋转。
// No sensor input: fixtures set their display orientation explicitly, without claiming gravity-rotation coverage.
static void ps_book_rotation_enter(app_ctx_t* ctx){(void)ctx;}
static bool ps_book_rotation_tick(app_ctx_t* ctx,bool idle){(void)ctx;(void)idle;return false;}
static void ttf_set_weight(int weight){test_font_weight=weight;}

'''
unit = unit.replace("/* HOST_GESTURE_TYPES */", gesture_types)
unit += "\nbool ui_gesture_feed_continuous(ui_gesture_t*, const app_ctx_t*, bool, ui_gesture_event_t*);\n" + gesture_source
unit += function("tool_rect") + "\n" + function("toolbar_panel") + "\n"
unit += function("pending_back_rect") + "\n"
for name in ("copy_text", "page_index_pause", "page_index_reset", "page_index_matches", "page_index_prepare", "page_index_locate", "page_index_step", "page_jump_rect", "page_jump_key_rect", "page_jump_button_rect", "page_jump_control_at", "page_jump_status_rect", "page_jump_feedback", "page_jump_close", "page_jump_open", "book_open_toc", "page_jump_commit", "page_jump_action", "page_jump_tick", "progress_contact_rect", "progress_contact_move", "progress_contact_release", "fit_text", "selection_rect", "selection_panel", "text_next", "text_previous", "selection_geometry", "select_character", "selection_drag_start", "selection_drag_move", "cancel_interaction", "handle_hit", "source_chapter", "progress_preview", "progress_commit", "selection_anchor", "restore_annotation", "selection_adjust", "annotation_action", "selection_control_at", "selection_touch", "shelf_matches", "compare_books", "sort_shelf", "shelf_reserve", "delete_retry_find", "delete_retry_reserve", "delete_retry_discard", "scan_shelf",
             "pending_find", "pending_reserve", "pending_restore", "pending_discard", "pending_mark_latest", "pending_drop_invalidated", "pending_flush", "save_progress", "retry_progress", "layout_name", "manage_panel", "manage_rect", "batch_rect", "leaves", "selected_count", "clear_selection", "toggle_selection", "select_page", "search_keys", "search_begin", "refresh_search_matches", "search_finish", "search_action", "refresh_capacity", "manage_apply", "manage_action", "batch_apply", "batch_action", "resume_choice", "image_confirm_close", "free_image", "close_image", "image_back_rect", "open_image", "image_confirm_rect", "image_confirm_button_rect", "image_confirm_control_at", "image_confirm_request", "image_confirm_action", "goto_font", "product_exit", "request_path_valid", "app_book_open_path", "app_book_open_annotation", "app_book_global_prepare", "app_book_resume_last", "app_book_global_action", "book_touch_point", "book_touch_event", "book_modal_rect", "gesture_event", "app_book_external_turn", "on_key", "on_key_long", "draw_wrapped_name", "draw_pending_open", "present", "on_enter", "book_on_exit", "book_on_media_lost", "book_on_media_restored", "open_pending_book", "on_tick"):
    unit += function(name) + "\n"
unit += r'''
// 每个返回场景使用独立页面状态，防止替身之间互相遮蔽。/ Isolate each exit scenario from unrelated test-double state.
static void test_navigation_state(app_ctx_t* ctx,bool product,int scenario) {
    *ctx=(app_ctx_t){0};
    free_image();
    s_text=NULL;s_path[0]=s_requested_path[0]=s_resume_path[0]=s_open_path[0]=0;s_open_presented=false;
    s_product_session=product;s_view=READING;s_toolbar=false;
    s_selection=s_annotation_pending=false;
    s_scan_pending=s_clear_confirm=s_batch_confirm=false;
    s_pressed_control=123;
    if(scenario<6)s_view=(book_view_t)scenario;
    else if(scenario==6)s_scan_pending=true;
    else if(scenario==7)s_clear_confirm=true;
    else if(scenario==8)s_batch_confirm=true;
    else if(scenario==9)strcpy(s_resume_path,"/sdcard/books/resume.epub");
    else if(scenario==10)s_image_open=true;
    else if(scenario==11){s_scan_pending=s_clear_confirm=s_batch_confirm=s_image_open=true;strcpy(s_resume_path,"/sdcard/books/resume.epub");}
    else {s_view=SHELF;strcpy(s_open_path,"/sdcard/books/loading.epub");}
}
static void test_annotations(void) {
    app_ctx_t ctx={0};
    strcpy(test_roots[0].path,"/sdcard/books");test_root_count=1;
    char prose[]="\r\n  甲乙丙丁。 English \t\n下一段";
    s_text=prose;s_text_len=strlen(prose);strcpy(s_path,"/sdcard/books/真实书.epub");s_file_size=97531;s_chapter=4;s_page=0;
    size_t first=(size_t)(strstr(prose,"甲")-prose),second=(size_t)(strstr(prose,"下一段")-prose);
    assert(select_character(first+3)&&s_selection&&s_selection_start==first+3&&s_selection_end==first+6);
    assert(!select_character(first+4));
    selection_adjust(first+6);assert(s_selection_end==first+9);
    assert(on_key(&ctx,UI_KEY_1)==APP_REDRAW_PAGE&&s_selection_end==first+6);
    ps_book_anchor_t anchor; assert(selection_anchor(&anchor));
    assert(!strcmp(anchor.path,s_path)&&!strcmp(anchor.title,"真实书.epub")&&anchor.file_bytes==97531&&anchor.chapter==4);
    assert(!strcmp(anchor.quote,"乙"));
    test_annotation_save=false;annotation_action(&ctx,0);assert(s_selection&&test_highlights==1);
    test_annotation_save=true;annotation_action(&ctx,0);assert(!s_selection&&test_highlights==2&&test_annotation.start==first+3&&test_annotation.end==first+6);
    assert(select_character(second));s_selection_end=s_text_len;annotation_action(&ctx,1);
    assert(test_notes==1&&ctx.request_app==&test_home&&test_book_return&&!strcmp(test_annotation.quote,"下一段"));
    ctx=(app_ctx_t){0};annotation_action(&ctx,2);assert(test_bookmarks==1&&!s_selection&&test_annotation.chapter==4);
    assert(app_book_global_action(&ctx,BOOK_GLOBAL_SELECT)==APP_REDRAW_PAGE&&!s_selection);
    select_character(first);assert(app_book_global_action(&ctx,BOOK_GLOBAL_BACK)==APP_REDRAW_PAGE&&!s_selection&&!ctx.request_app);
    assert(app_book_global_action(&ctx,BOOK_GLOBAL_BOOKMARK)==APP_REDRAW_PAGE&&test_bookmarks==2&&test_annotation.start==0&&test_annotation.end==0);
    assert(app_book_global_action(&ctx,BOOK_GLOBAL_RECORDS)==APP_REDRAW_PAGE&&test_records==1&&ctx.request_app==&test_home);
    ctx=(app_ctx_t){0};s_selection=false;s_toolbar=false;s_view=READING;s_scan_pending=s_clear_confirm=false;
    test_text_hit=first+6;ui_gesture_event_t hold={.type=UI_GESTURE_LONG_PRESS,.x0=200,.y0=300,.x=200,.y=300};
    assert(gesture_event(&ctx,&hold)==APP_REDRAW_AREA&&s_selection&&s_selection_drag&&!s_selection_menu&&ctx.capture_gesture);
    assert(s_selection_start==first+6&&s_selection_end==first+9);
    ui_gesture_event_t move={.type=UI_GESTURE_MOVE,.x0=200,.y0=300,.x=400,.y=400};
    test_text_hit=second;assert(gesture_event(&ctx,&move)==APP_REDRAW_AREA&&s_selection_end==second+3);
    assert(gesture_event(&ctx,&move)==APP_REDRAW_NONE);
    test_text_hit=first;move.x=70;assert(gesture_event(&ctx,&move)==APP_REDRAW_AREA&&s_selection_start==first&&s_selection_end==first+9);
    unsigned prior_turns=test_turns;
    ui_gesture_event_t swipe={.type=UI_GESTURE_SWIPE_L,.x0=400,.y0=300,.x=100,.y=300};
    assert(gesture_event(&ctx,&swipe)==APP_REDRAW_NONE&&test_turns==prior_turns);
    move.type=UI_GESTURE_RELEASE;assert(gesture_event(&ctx,&move)==APP_REDRAW_PAGE&&!s_selection_drag&&s_selection_menu);
    // 圆柄按下立即捕获，偏移补偿保留抓取点；跨过固定端仍保留合法字节区间。
    // Handles capture immediately and retain the grab offset; crossing keeps a valid byte range.
    EpdRect glyph=s_handle_end;ui_gesture_event_t press={.type=UI_GESTURE_PRESS,.x0=(uint16_t)(glyph.x+glyph.width),.y0=(uint16_t)(glyph.y+glyph.height+14),.x=(uint16_t)(glyph.x+glyph.width),.y=(uint16_t)(glyph.y+glyph.height+14)};
    ctx.capture_gesture=false;assert(gesture_event(&ctx,&press)==APP_REDRAW_AREA&&ctx.capture_gesture&&s_selection_drag);
    test_text_hit=first+3;move.type=UI_GESTURE_MOVE;move.x=press.x;move.y=press.y;
    assert(gesture_event(&ctx,&move)==APP_REDRAW_AREA&&test_nearest_x==glyph.x+glyph.width/2&&test_nearest_y==glyph.y+glyph.height/2);
    ui_gesture_event_t cancel={.type=UI_GESTURE_CANCEL};
    assert(gesture_event(&ctx,&cancel)==APP_REDRAW_PAGE&&!s_selection_drag&&s_selection_start==first&&s_selection_end==first+9);
    EpdRect rect=selection_rect(0);ui_gesture_event_t tap={.type=UI_GESTURE_TAP,.x0=(uint16_t)(rect.x+10),.y0=(uint16_t)(rect.y+10),.x=(uint16_t)(rect.x+10),.y=(uint16_t)(rect.y+10)};
    assert(gesture_event(&ctx,&tap)==APP_REDRAW_PAGE&&!s_selection&&test_highlights==3);
    test_text_hit=first;assert(gesture_event(&ctx,&hold)==APP_REDRAW_AREA);
    assert(gesture_event(&ctx,&cancel)==APP_REDRAW_PAGE&&!s_selection);
    test_text_hit=SIZE_MAX;assert(gesture_event(&ctx,&hold)==APP_REDRAW_NONE&&!s_selection);
    // 快捷功能分别执行当前书收藏、划线、批注与指定记录页；无选区不创建记录。
    // Shortcuts distinguish current-book favorites, highlights, notes and record tabs; an empty selection creates nothing.
    s_open_path[0]=0;ctx=(app_ctx_t){0};unsigned highlights=test_highlights,notes=test_notes,records=test_records;
    assert(app_book_global_action(&ctx,BOOK_GLOBAL_HIGHLIGHT)==APP_REDRAW_PAGE&&test_highlights==highlights&&!ctx.request_app);
    assert(app_book_global_action(&ctx,BOOK_GLOBAL_NOTE)==APP_REDRAW_PAGE&&test_notes==notes&&!ctx.request_app);
    assert(app_book_global_action(&ctx,BOOK_GLOBAL_FAVORITE)==APP_REDRAW_PAGE&&test_file_favorite&&test_records==records&&!ctx.request_app);
    assert(app_book_global_action(&ctx,BOOK_GLOBAL_FAVORITE)==APP_REDRAW_PAGE&&!test_file_favorite&&test_records==records);
    assert(select_character(first));assert(app_book_global_action(&ctx,BOOK_GLOBAL_HIGHLIGHT)==APP_REDRAW_PAGE&&test_highlights==highlights+1&&!s_selection&&!ctx.request_app);
    assert(select_character(second));assert(app_book_global_action(&ctx,BOOK_GLOBAL_NOTE)==APP_REDRAW_PAGE&&test_notes==notes+1&&ctx.request_app==&test_home&&!strcmp(test_annotation.path,s_path));
    ctx=(app_ctx_t){0};assert(app_book_global_action(&ctx,BOOK_GLOBAL_HIGHLIGHTS)==APP_REDRAW_PAGE&&test_records==records+1&&test_records_tab==1&&ctx.request_app==&test_home);
    ctx=(app_ctx_t){0};assert(app_book_global_action(&ctx,BOOK_GLOBAL_NOTES)==APP_REDRAW_PAGE&&test_records==records+2&&test_records_tab==0&&ctx.request_app==&test_home);
    ctx=(app_ctx_t){0};assert(app_book_global_action(&ctx,BOOK_GLOBAL_BOOKMARKS)==APP_REDRAW_PAGE&&test_records==records+3&&test_records_tab==2&&ctx.request_app==&test_home);
    ctx=(app_ctx_t){0};assert(app_book_global_action(&ctx,BOOK_GLOBAL_PAGE)==APP_REDRAW_PAGE&&s_page_jump_open&&!ctx.request_app&&!s_selection&&!s_toolbar);
    assert(app_book_global_action(&ctx,BOOK_GLOBAL_BACK)==APP_REDRAW_PAGE&&!s_page_jump_open&&!ctx.request_app);
    assert(app_book_open_annotation(s_path,97531,4,(uint32_t)first,(uint32_t)(first+6))&&s_annotation_pending);
    assert(restore_annotation()&&s_selection&&s_selection_start==first&&s_selection_end==first+6);
    s_annotation_target.start++;assert(!restore_annotation());s_annotation_target.start--;
    s_annotation_target.file_bytes++;assert(!restore_annotation());s_annotation_target.file_bytes--;
    s_annotation_target.chapter++;assert(!restore_annotation());s_annotation_target.chapter--;
    s_annotation_target.end=(uint32_t)s_text_len+1;assert(!restore_annotation());
    assert(!app_book_open_annotation(s_path,97531,4,10,9));
    assert(!app_book_open_annotation(s_path,0,4,0,1));
    assert(!app_book_open_annotation(s_path,UINT64_MAX,4,0,1));
    assert(!app_book_open_annotation("/etc/passwd",97531,4,0,1));
    assert(app_book_open_path("/sdcard/books/plain.epub")&&!s_annotation_pending);
    strcpy(s_last_product_path,"/sdcard/books/last.epub");assert(app_book_resume_last()&&!strcmp(s_requested_path,s_last_product_path));
    char large[1001];for(size_t i=0;i<999;i+=3)memcpy(large+i,"甲",3);large[999]=0;
    s_text=large;s_text_len=999;assert(select_character(0));s_selection_end=999;assert(selection_anchor(&anchor));
    assert(anchor.end==999&&strlen(anchor.quote)==510&&strlen(anchor.quote)%3==0);
    s_text=NULL;s_text_len=0;s_path[0]=s_requested_path[0]=0;s_selection=s_annotation_pending=false;
    unsigned opened=test_open_calls;
    assert(app_book_open_annotation("/sdcard/books/annotation-jump.epub",97531,4,6,12));
    ctx=(app_ctx_t){0};on_enter(&ctx);
    assert(s_annotation_pending&&s_annotation_target.chapter==4&&s_annotation_target.start==6&&s_annotation_target.end==12);
    assert(on_tick(&ctx)==APP_REDRAW_NONE&&test_open_calls==opened);
    present(&ctx,APP_REDRAW_FULL);assert(s_open_presented);
    assert(on_tick(&ctx)==APP_REDRAW_PAGE&&test_open_calls==opened+1&&!s_annotation_pending);
    assert(!strcmp(test_open_path,"/sdcard/books/annotation-jump.epub"));
    s_last_product_path[0]=0;s_chapter=s_page=0;s_view=SHELF;test_text_hit=SIZE_MAX;
    test_open_calls=test_rotation_calls=test_loading_displays=test_loading_renders=test_display_calls=test_clear_calls=0;
    s_save_failed=false;
}
// 进度预览不能读取章节；释放才一次定位，可撤销且失败保留旧锚点。
// Progress preview never loads a chapter; release seeks once, with rollback and failure retention.
static void test_progress(void) {
    app_ctx_t ctx={0};s_product_session=false;test_chapter_count=3;s_view=READING;s_text="book";s_text_len=4;s_path[0]=0;
    s_toolbar=s_selection=s_selection_drag=s_scan_pending=s_clear_confirm=s_image_open=s_scrubbing=s_jump_back_valid=false;
    s_layout_rect=body_rect();strcpy(s_font_path,ttf_font_path());s_chapter=0;s_page=2;unsigned old_turns=test_turns,old_seeks=test_seek_calls;
    EpdRect p=progress_rect();assert(p.y+p.height<=reader_height()-128);
    ui_gesture_event_t press={.type=UI_GESTURE_PRESS,.x0=(uint16_t)p.x,.y0=(uint16_t)(p.y+20),.x=(uint16_t)p.x,.y=(uint16_t)(p.y+20)};
    assert(gesture_event(&ctx,&press)==APP_REDRAW_NONE&&!ctx.capture_gesture&&s_progress_pending&&!s_scrubbing);
    ui_gesture_event_t move=press;move.type=UI_GESTURE_MOVE;move.x=(uint16_t)(p.x+p.width/2);
    assert(gesture_event(&ctx,&move)==APP_REDRAW_AREA&&s_scrub_chapter==1&&s_scrub_target>=499&&s_scrub_target<=501);
    assert(gesture_event(&ctx,&move)==APP_REDRAW_NONE&&test_seek_calls==old_seeks&&test_turns==old_turns);
    assert(on_tick(&ctx)==APP_REDRAW_NONE&&test_seek_calls==old_seeks);
    move.x=65535;assert(gesture_event(&ctx,&move)==APP_REDRAW_AREA&&s_scrub_target==1000&&s_scrub_chapter==2);
    ui_gesture_event_t cancel={.type=UI_GESTURE_CANCEL};
    assert(gesture_event(&ctx,&cancel)==APP_REDRAW_AREA&&!s_scrubbing&&s_page==2&&s_chapter==0&&!s_jump_back_valid&&test_seek_calls==old_seeks);
    assert(gesture_event(&ctx,&press)==APP_REDRAW_NONE);move.type=UI_GESTURE_MOVE;move.x=(uint16_t)(p.x+p.width-1);
    assert(gesture_event(&ctx,&move)==APP_REDRAW_AREA);move.type=UI_GESTURE_RELEASE;
    assert(gesture_event(&ctx,&move)==APP_REDRAW_PAGE&&test_seek_calls==old_seeks+1&&test_seek_chapter==2&&test_seek_offset==100&&test_seek_source);
    assert(!s_scrubbing&&s_jump_back_valid&&s_jump_chapter==0&&s_jump_offset==200);
    EpdRect back=progress_back_rect();ui_gesture_event_t tap={.type=UI_GESTURE_TAP,.x0=(uint16_t)(back.x+10),.y0=(uint16_t)(back.y+10),.x=(uint16_t)(back.x+10),.y=(uint16_t)(back.y+10)};
    test_seek_fails=true;size_t kept_page=s_page,kept_chapter=s_chapter;
    assert(gesture_event(&ctx,&tap)==APP_REDRAW_PAGE&&s_jump_back_valid&&s_page==kept_page&&s_chapter==kept_chapter);
    test_seek_fails=false;assert(gesture_event(&ctx,&tap)==APP_REDRAW_PAGE&&!s_jump_back_valid&&s_chapter==0&&s_page==2&&!test_seek_source);
    old_seeks=test_seek_calls;assert(gesture_event(&ctx,&press)==APP_REDRAW_NONE);move.type=UI_GESTURE_MOVE;
    assert(gesture_event(&ctx,&move)==APP_REDRAW_AREA);move.type=UI_GESTURE_RELEASE;test_seek_fails=true;
    assert(gesture_event(&ctx,&move)==APP_REDRAW_PAGE&&test_seek_calls==old_seeks+1&&!s_jump_back_valid&&s_chapter==0&&s_page==2);
    test_seek_fails=false;test_reader_width=1216;test_reader_height=684;p=progress_rect();assert(p.y+p.height<=reader_height()-128&&p.width==1136);
    // 留在阅读页改变方向也必须取消拖动并从原字节锚点重排。
    // In-place orientation changes cancel dragging and reflow from the same byte anchor.
    test_body=(EpdRect){40,24,1136,416};s_scrubbing=true;unsigned builds=test_layout_builds;
    strcpy(s_font_path,ttf_font_path());assert(on_tick(&ctx)==APP_REDRAW_PAGE&&!s_scrubbing&&test_layout_builds==builds+1&&test_layout_area.width==1136&&s_page==2&&s_layout_rect.width==1136);
    test_reader_width=684;test_reader_height=1216;test_body=(EpdRect){40,24,604,960};s_layout_rect=test_body;test_chapter_count=1;s_text=NULL;s_text_len=0;s_scrubbing=s_selection=false;s_chapter=s_page=0;
}
// 生成真实控制器原始坐标，验证横竖四方向的实际页内事件入口。
// Generate controller raw coordinates and exercise the real page-event entry in all four rotations.
static void test_raw_point(int x,int y,uint16_t* a,uint16_t* b){
    switch(test_rotation){
        case EPD_ROT_LANDSCAPE:*a=(uint16_t)(683-y);*b=(uint16_t)x;break;
        case EPD_ROT_PORTRAIT:*a=(uint16_t)(683-x);*b=(uint16_t)(1215-y);break;
        case EPD_ROT_INVERTED_LANDSCAPE:*a=(uint16_t)y;*b=(uint16_t)(1215-x);break;
        default:*a=(uint16_t)x;*b=(uint16_t)y;break;
    }
}
static ui_gesture_event_t test_raw_event(ui_gesture_type_t type,int x0,int y0,int x,int y){
    ui_gesture_event_t e={.type=type};test_raw_point(x0,y0,&e.x0,&e.y0);test_raw_point(x,y,&e.x,&e.y);
    if(type==UI_GESTURE_SWIPE_L||type==UI_GESTURE_SWIPE_R){
        int dx=(int)e.x-e.x0,dy=(int)e.y-e.y0;
        e.type=abs(dx)>abs(dy)?(dx<0?UI_GESTURE_SWIPE_L:UI_GESTURE_SWIPE_R):(dy<0?UI_GESTURE_SWIPE_U:UI_GESTURE_SWIPE_D);
    }return e;
}
static void test_raw_orientation(void){
    uint16_t x,y;book_touch_point(EPD_ROT_INVERTED_LANDSCAPE,100,200,&x,&y);assert(x==1015&&y==100);
    book_touch_point(EPD_ROT_LANDSCAPE,100,200,&x,&y);assert(x==200&&y==583);
    book_touch_point(EPD_ROT_PORTRAIT,100,200,&x,&y);assert(x==583&&y==1015);
    book_touch_point(EPD_ROT_INVERTED_PORTRAIT,100,200,&x,&y);assert(x==100&&y==200);
    for(int rotation=0;rotation<4;++rotation){
        test_rotation=rotation;s_book_rotation=(enum EpdRotation)rotation;bool landscape=rotation==0||rotation==2;
        test_reader_width=landscape?1216:684;test_reader_height=landscape?684:1216;
        test_body=(EpdRect){40,24,test_reader_width-80,reader_footer_top()-32};s_layout_rect=test_body;
        int corners[][2]={{0,0},{test_reader_width-1,0},{0,test_reader_height-1},{test_reader_width-1,test_reader_height-1}};
        for(size_t i=0;i<4;++i){ui_gesture_event_t r=test_raw_event(UI_GESTURE_MOVE,corners[i][0],corners[i][1],corners[i][0],corners[i][1]);ui_gesture_event_t m=book_touch_event(&r);assert(m.x==corners[i][0]&&m.y==corners[i][1]&&m.x0==m.x&&m.y0==m.y);assert(r.x==r.x0&&r.y==r.y0);}
        app_ctx_t ctx={0};char prose[]="甲乙丙丁戊己";s_text=prose;s_text_len=strlen(prose);strcpy(s_path,"/sdcard/books/raw.epub");
        s_view=READING;s_product_session=false;s_chapter=s_page=0;
        s_selection=s_selection_drag=s_selection_menu=s_scrubbing=s_toolbar=s_scan_pending=s_clear_confirm=s_image_open=s_jump_back_valid=false;
        s_blocks=NULL;s_block_count=0;test_text_hit=3;
        ui_gesture_event_t hold=test_raw_event(UI_GESTURE_LONG_PRESS,200,300,200,300),original=hold;
        assert(gesture_event(&ctx,&hold)==APP_REDRAW_AREA&&s_selection_drag&&ctx.capture_gesture&&s_selection_start==3&&s_selection_end==6);
        assert(test_text_hit_x==200&&test_text_hit_y==300&&!memcmp(&hold,&original,sizeof(hold)));
        test_text_hit=9;ui_gesture_event_t move=test_raw_event(UI_GESTURE_MOVE,200,300,400,320);
        assert(gesture_event(&ctx,&move)==APP_REDRAW_AREA&&s_selection_end==12&&test_nearest_x==400&&test_nearest_y==320);
        move.type=UI_GESTURE_RELEASE;assert(gesture_event(&ctx,&move)==APP_REDRAW_PAGE&&s_selection_menu&&!s_selection_drag);
        EpdRect glyph=s_handle_end;int hx=glyph.x+glyph.width,hy=glyph.y+glyph.height+14;
        ui_gesture_event_t handle=test_raw_event(UI_GESTURE_PRESS,hx,hy,hx,hy);ctx.capture_gesture=false;
        assert(gesture_event(&ctx,&handle)==APP_REDRAW_AREA&&ctx.capture_gesture&&s_selection_drag);
        test_text_hit=6;move=test_raw_event(UI_GESTURE_MOVE,hx,hy,hx-20,hy);assert(gesture_event(&ctx,&move)==APP_REDRAW_AREA&&s_selection_end==9);
        ui_gesture_event_t cancel={.type=UI_GESTURE_CANCEL};assert(gesture_event(&ctx,&cancel)==APP_REDRAW_PAGE&&s_selection_end==12&&!s_selection_drag);
        s_selection=s_selection_menu=false;s_path[0]=0;test_chapter_count=3;
        EpdRect p=progress_rect();ui_gesture_event_t press=test_raw_event(UI_GESTURE_PRESS,p.x,p.y+20,p.x,p.y+20);unsigned seeks=test_seek_calls;
        assert(gesture_event(&ctx,&press)==APP_REDRAW_NONE&&s_progress_pending&&!s_scrubbing);
        move=test_raw_event(UI_GESTURE_MOVE,p.x,p.y+20,p.x+p.width-1,p.y+20);
        assert(gesture_event(&ctx,&move)==APP_REDRAW_AREA&&s_scrub_target==1000&&test_seek_calls==seeks);
        move.type=UI_GESTURE_RELEASE;assert(gesture_event(&ctx,&move)==APP_REDRAW_PAGE&&test_seek_calls==seeks+1&&test_seek_chapter==2&&test_seek_offset==100);
        s_jump_back_valid=false;unsigned turns=test_turns;
        ui_gesture_event_t swipe=test_raw_event(UI_GESTURE_SWIPE_L,400,300,100,300);
        assert(gesture_event(&ctx,&swipe)==APP_REDRAW_NONE&&test_turns==turns+1&&test_last_turn_dir==1);
        assert(on_key(&ctx,UI_KEY_1)==APP_REDRAW_NONE&&test_last_turn_dir==-1);
        assert(on_key(&ctx,UI_KEY_3)==APP_REDRAW_NONE&&test_last_turn_dir==1);
    }
    test_rotation=EPD_ROT_INVERTED_PORTRAIT;s_book_rotation=EPD_ROT_INVERTED_PORTRAIT;test_reader_width=684;test_reader_height=1216;test_body=(EpdRect){40,24,604,960};s_layout_rect=test_body;
    test_chapter_count=1;s_text=NULL;s_text_len=0;s_path[0]=0;s_chapter=s_page=0;s_save_failed=false;test_turns=0;
    puts("book_ui_host_test: actual raw events in 4 rotations (hit/drag/handles/progress/release/swipe/physical keys) PASS");
}
// 全书跳页真实控制器：输入边界、懒计页、取消、失败回滚和四方向命中。
// Real whole-book navigation controller: boundaries, lazy counting, cancellation, rollback and rotated hits.
static void test_page_modal(void){
    app_ctx_t ctx={0};test_chapter_count=3;test_count_pages[0]=3;test_count_pages[1]=5;test_count_pages[2]=2;
    test_reader_width=684;test_reader_height=1216;test_body=(EpdRect){40,24,604,960};s_layout_rect=test_body;
    strcpy(test_current_font,"builtin");strcpy(s_font_path,ttf_font_path());strcpy(s_path,"/sdcard/books/page-test.epub");
    s_text="book";s_text_len=4;s_chapter=0;s_page=1;s_view=READING;s_product_session=true;
    s_selection=s_toolbar=s_image_open=s_clear_confirm=s_scan_pending=s_jump_back_valid=false;s_open_path[0]=0;
    unsigned seeks=test_seek_calls,turns=test_turns;
    page_index_reset();assert(page_jump_open()==APP_REDRAW_PAGE&&s_page_jump_open&&page_index_matches());
    assert(page_jump_action(&ctx,BOOK_PAGE_CONTROL+14)==APP_REDRAW_AREA&&!s_page_jump_waiting&&s_page_jump_message[0]);
    const char* invalid[]={"0","4294967296","9999999999","12x"};
    for(size_t i=0;i<4;i++){strcpy(s_page_jump_input,invalid[i]);page_jump_action(&ctx,BOOK_PAGE_CONTROL+14);assert(!s_page_jump_waiting&&s_page_jump_message[0]&&test_seek_calls==seeks);}
    page_jump_action(&ctx,BOOK_PAGE_CONTROL+11);assert(!*s_page_jump_input);
    page_jump_action(&ctx,BOOK_PAGE_CONTROL);page_jump_action(&ctx,BOOK_PAGE_CONTROL+10);assert(!strcmp(s_page_jump_input,"10"));
    page_jump_action(&ctx,BOOK_PAGE_CONTROL+9);assert(!strcmp(s_page_jump_input,"1"));
    strcpy(s_page_jump_input,"1234567890");page_jump_action(&ctx,BOOK_PAGE_CONTROL);assert(!strcmp(s_page_jump_input,"1234567890")&&s_page_jump_message[0]);
    assert(on_key(&ctx,UI_KEY_1)==APP_REDRAW_NONE&&on_key(&ctx,UI_KEY_3)==APP_REDRAW_NONE&&test_turns==turns);
    // 未确认时不统计、不刷新，也不让滑动穿透。/ No counting, redraws or swipe passthrough before confirmation.
    ui_gesture_event_t swipe={.type=UI_GESTURE_SWIPE_L,.x0=400,.y0=300,.x=100,.y=300};gesture_event(&ctx,&swipe);assert(test_turns==turns&&test_seek_calls==seeks);
    unsigned loads=test_count_loads,steps=test_count_steps;
    for(int i=0;i<20;i++)assert(page_jump_tick(&ctx)==APP_REDRAW_NONE);
    assert(test_count_loads==loads&&test_count_steps==steps&&s_page_index.done==0);
    // 独立索引测试可直接计页，UI调度仍必须等待确认。/ Count directly to test the index; UI scheduling still waits for confirmation.
    for(size_t i=0;i<3;i++){bool changed=false;while(s_page_index.done==i)assert(page_index_step(i,&changed));page_index_pause();}
    assert(s_page_index.done==3&&s_page_index.total==10&&s_chapter==0&&s_page==1&&test_seek_calls==seeks);
    size_t chapter,page;assert(page_index_locate(1,&chapter,&page)&&chapter==0&&page==0);
    assert(page_index_locate(3,&chapter,&page)&&chapter==0&&page==2);
    assert(page_index_locate(4,&chapter,&page)&&chapter==1&&page==0);
    assert(page_index_locate(8,&chapter,&page)&&chapter==1&&page==4);
    assert(page_index_locate(10,&chapter,&page)&&chapter==2&&page==1&&!page_index_locate(11,&chapter,&page));
    strcpy(s_page_jump_input,"11");page_jump_action(&ctx,BOOK_PAGE_CONTROL+14);assert(!s_page_jump_waiting&&s_page_jump_message[0]);
    strcpy(s_page_jump_input,"4");page_jump_action(&ctx,BOOK_PAGE_CONTROL+14);assert(s_page_jump_waiting);
    test_seek_fails=true;for(int i=0;i<20&&s_page_jump_waiting;i++)page_jump_tick(&ctx);test_seek_fails=false;
    assert(s_page_jump_open&&!s_page_jump_waiting&&s_page==1&&s_chapter==0&&!s_jump_back_valid&&s_page_jump_message[0]);
    strcpy(s_page_jump_input,"8");page_jump_action(&ctx,BOOK_PAGE_CONTROL+14);
    for(int i=0;i<20&&s_page_jump_open;i++)page_jump_tick(&ctx);
    assert(!s_page_jump_open&&s_chapter==1&&s_page==4&&test_seek_offset==400&&!test_seek_source&&s_jump_back_valid&&s_jump_chapter==0&&s_jump_offset==100);
    assert(progress_commit(&ctx,true)==APP_REDRAW_PAGE&&s_chapter==0&&s_page==1&&!s_jump_back_valid);
    // 中键与全局Back均只关闭一级；已进入拖动后回原点仍按拖动提交。
    // Middle key/global Back close one level; returning a drag to its origin still commits a drag.
    page_jump_open();page_jump_tick(&ctx);assert(!s_page_index.job&&s_page_index.done==3);
    app_book_global_prepare();assert(s_page_jump_open&&!s_page_index.job);
    assert(app_book_global_action(&ctx,BOOK_GLOBAL_BACK)==APP_REDRAW_PAGE&&!s_page_jump_open&&!ctx.request_app);
    page_jump_open();assert(on_key(&ctx,UI_KEY_2)==APP_REDRAW_PAGE&&!s_page_jump_open&&!ctx.request_app);
    assert(on_key_long(&ctx,UI_KEY_2)==APP_REDRAW_NONE&&!ctx.request_app);
    page_jump_open();assert(book_open_toc(&ctx)==APP_REDRAW_PAGE&&!s_page_jump_open&&s_view==TOC&&ctx.leaf==0);
    assert(on_key(&ctx,UI_KEY_2)==APP_REDRAW_PAGE&&s_view==READING&&!ctx.request_app);
    // 故障不会清活动页或创建返回点。/ Failures retain active page and do not create return anchors.
    for(int fault=0;fault<3;fault++){
        page_index_reset();page_jump_open();strcpy(s_page_jump_input,"8");page_jump_action(&ctx,BOOK_PAGE_CONTROL+14);test_count_load_fail=fault==0;test_count_create_fail=fault==1;test_count_step_fail=fault==2;
        for(int i=0;i<4&&!s_page_index.failed;i++)page_jump_tick(&ctx);
        assert(s_page_index.failed&&s_page_jump_open&&s_page==1&&s_chapter==0&&!s_jump_back_valid&&!s_page_index.job);
        test_count_load_fail=test_count_create_fail=test_count_step_fail=false;page_jump_close();
    }
    page_index_reset();page_jump_open();strcpy(s_page_jump_input,"2");page_jump_action(&ctx,BOOK_PAGE_CONTROL+14);
    for(int i=0;i<5&&s_page_jump_open;i++)page_jump_tick(&ctx);
    assert(!s_page_jump_open&&s_chapter==0&&s_page==1&&s_jump_back_valid);s_jump_back_valid=false;
    page_jump_open();test_font_weight=600;assert(!page_index_matches());on_tick(&ctx);assert(!s_page_jump_open&&!s_page_index.counts);test_font_weight=400;
    // 每个旋转中轻点轨道打开键盘，键盘控件互不重叠且避开底部全局安全区。
    // Tap the track in all rotations; keypad controls are disjoint and clear of the global safe area.
    for(test_rotation=0;test_rotation<4;test_rotation++){
        s_book_rotation=(enum EpdRotation)test_rotation;bool land=test_rotation==0||test_rotation==2;test_reader_width=land?1216:684;test_reader_height=land?684:1216;
        test_body=(EpdRect){40,24,test_reader_width-80,reader_footer_top()-32};s_layout_rect=test_body;page_index_reset();
        EpdRect rail=progress_rect();ui_gesture_event_t press=test_raw_event(UI_GESTURE_PRESS,rail.x+100,rail.y+20,rail.x+100,rail.y+20);
        seeks=test_seek_calls;assert(gesture_event(&ctx,&press)==APP_REDRAW_NONE&&s_progress_pending&&!s_page_jump_open);
        ui_gesture_event_t release=press;release.type=UI_GESTURE_TAP;assert(gesture_event(&ctx,&release)==APP_REDRAW_PAGE&&s_page_jump_open&&test_seek_calls==seeks);
        for(int i=0;i<14;i++){
            EpdRect r=i<12?page_jump_key_rect(i):page_jump_button_rect(i-12),hit;
            assert(r.width>=56&&r.height>=56&&r.x>=0&&r.y>=0&&r.x+r.width<=reader_width()&&r.y+r.height<=reader_height()-84);
            assert(page_jump_control_at(r.x+r.width/2,r.y+r.height/2,&hit)==BOOK_PAGE_CONTROL+(i==13?14:i));
            for(int j=0;j<i;j++){EpdRect a=j<12?page_jump_key_rect(j):page_jump_button_rect(j-12);assert(r.x+r.width<=a.x||a.x+a.width<=r.x||r.y+r.height<=a.y||a.y+a.height<=r.y);}
        }
        // 定位中的主按钮不能按下或留下装饰，取消仍可命中。
        // The locating action cannot become pressed or leave decoration; cancel remains reachable.
        EpdRect jump=page_jump_button_rect(1),cancel=page_jump_button_rect(0),hit;
        s_page_jump_waiting=true;unsigned draws=test_modal_draws;uint32_t target=s_page_jump_target;
        ui_gesture_event_t disabled=test_raw_event(UI_GESTURE_PRESS,jump.x+jump.width/2,jump.y+jump.height/2,jump.x+jump.width/2,jump.y+jump.height/2);
        assert(gesture_event(&ctx,&disabled)==APP_REDRAW_NONE&&s_pressed_control==-1);
        disabled.type=UI_GESTURE_TAP;assert(gesture_event(&ctx,&disabled)==APP_REDRAW_NONE&&s_pressed_control==-1);
        assert(s_page_jump_waiting&&s_page_jump_target==target&&test_modal_draws==draws&&test_seek_calls==seeks);
        assert(page_jump_control_at(cancel.x+cancel.width/2,cancel.y+cancel.height/2,&hit)==BOOK_PAGE_CONTROL+12);
        s_page_jump_waiting=false;
        EpdRect key=page_jump_key_rect(6);ui_gesture_event_t tap=test_raw_event(UI_GESTURE_TAP,key.x+key.width/2,key.y+key.height/2,key.x+key.width/2,key.y+key.height/2);
        assert(gesture_event(&ctx,&tap)==APP_REDRAW_AREA&&!strcmp(s_page_jump_input,"7"));page_jump_close();
        press=test_raw_event(UI_GESTURE_PRESS,rail.x+100,rail.y+20,rail.x+100,rail.y+20);gesture_event(&ctx,&press);
        ui_gesture_event_t move=test_raw_event(UI_GESTURE_MOVE,rail.x+100,rail.y+20,rail.x+180,rail.y+20);gesture_event(&ctx,&move);assert(s_scrubbing);
        move=test_raw_event(UI_GESTURE_MOVE,rail.x+100,rail.y+20,rail.x+100,rail.y+20);gesture_event(&ctx,&move);move.type=UI_GESTURE_RELEASE;gesture_event(&ctx,&move);
        assert(!s_page_jump_open&&test_seek_calls==seeks+1);s_jump_back_valid=false;
    }
    page_index_reset();test_rotation=3;s_book_rotation=EPD_ROT_INVERTED_PORTRAIT;test_reader_width=684;test_reader_height=1216;test_body=(EpdRect){40,24,604,960};s_layout_rect=test_body;
    s_text=NULL;s_text_len=0;s_path[0]=0;s_chapter=s_page=0;s_product_session=false;test_chapter_count=1;
    puts("book_ui_host_test: whole-book numeric modal, lazy exact chapter/page mapping, input boundaries, rollback/cancel/Back, cache invalidation, TOC and 4-rotation tap/drag/keypad PASS");
}

// 真实识别器→页面回调→认领→推屏→tick，与主循环保持相同；不直接合成长按事件。
// Match the loop's recognizer→page callback→capture→present→tick order rather than injecting hold events.
static test_touch_t test_pipeline_touch;
static int test_pipeline_event;
static app_redraw_t test_pipeline_tick;
static app_redraw_t test_bottom_frame(ui_gesture_t* g,app_ctx_t* ctx,int x,int y,int64_t ms,bool pressed,bool released,int count,bool consumed){
    uint16_t rawx,rawy;test_raw_point(x,y,&rawx,&rawy);
    test_pipeline_touch=(test_touch_t){.touched=!released,.count=(uint8_t)count,.x=rawx,.y=rawy};
    ctx->touch=&test_pipeline_touch;ctx->now_ms=ms;ctx->pressed=pressed;ctx->released=released;
    ctx->consumed=consumed;ctx->capture_gesture=false;test_pipeline_event=-1;
    ui_gesture_event_t event;app_redraw_t result=APP_REDRAW_NONE;
    if(ui_gesture_feed_continuous(g,ctx,true,&event)){
        test_pipeline_event=event.type;result=gesture_event(ctx,&event);
        if(ctx->capture_gesture)ui_gesture_capture(g);
        ctx->capture_gesture=false;
        if(result!=APP_REDRAW_NONE)assert(present(ctx,result));
        if(result!=APP_REDRAW_NONE)ctx->consumed=true;
    }
    test_pipeline_tick=on_tick(ctx);
    if(test_pipeline_tick!=APP_REDRAW_NONE)assert(present(ctx,test_pipeline_tick));
    return result;
}
static void test_bottom_reset(void){
    page_jump_close();page_index_reset();
    s_view=READING;s_product_session=true;s_text="甲乙丙丁戊己";s_text_len=strlen(s_text);s_path[0]=s_open_path[0]=0;
    s_chapter=0;s_page=1;s_px=48;s_toolbar=s_selection=s_selection_menu=s_selection_drag=false;
    s_image_open=s_clear_confirm=s_scan_pending=s_jump_back_valid=false;s_pressed_control=-1;
    s_du_count=0;s_full=s_reader_split=s_save_failed=s_shake_enabled=false;s_size_settle_ms=0;
    s_layout_rect=body_rect();strcpy(s_font_path,ttf_font_path());test_chapter_count=3;
}
static void test_bottom_pipeline(void){
    for(int rotation=0;rotation<4;rotation++){
        test_rotation=rotation;s_book_rotation=(enum EpdRotation)rotation;bool landscape=rotation==0||rotation==2;
        test_reader_width=landscape?1216:684;test_reader_height=landscape?684:1216;
        test_body=(EpdRect){40,24,test_reader_width-80,reader_footer_top()-32};
        test_bottom_reset();EpdRect track=progress_rect();int footer=track.y;
        int points[][2]={{track.x+8,footer+12},{track.x+track.width/2,footer+40},{track.x+track.width-8,footer+72},{track.x+8,footer+72}};
        for(size_t p=0;p<4;p++){
            test_bottom_reset();ui_gesture_t g={0};app_ctx_t ctx={0};unsigned seeks=test_seek_calls,turns=test_turns;
            int x=points[p][0],y=points[p][1];assert(ui_rect_hit(progress_contact_rect(),x,y));
            assert(test_bottom_frame(&g,&ctx,x,y,100,true,false,1,false)==APP_REDRAW_NONE&&s_progress_pending&&!g.captured);
            test_bottom_frame(&g,&ctx,x,y,180,false,true,0,false);
            assert(test_pipeline_event==UI_GESTURE_TAP&&s_page_jump_open&&!s_progress_pending&&test_seek_calls==seeks&&test_turns==turns);
            test_bottom_reset();ui_gesture_reset(&g);
            test_bottom_frame(&g,&ctx,x,y,100,true,false,1,false);
            test_bottom_frame(&g,&ctx,x-3,y-2,599,false,false,1,false);
            assert(test_pipeline_event==UI_GESTURE_MOVE&&!g.captured&&s_progress_pending&&!s_toolbar);
            assert(test_bottom_frame(&g,&ctx,x-3,y-2,600,false,false,1,false)==APP_REDRAW_PAGE);
            assert(test_pipeline_event==UI_GESTURE_LONG_PRESS&&g.captured&&s_toolbar&&!s_progress_pending&&!s_page_jump_open);
            test_bottom_frame(&g,&ctx,x-3,y-2,650,false,true,0,false);
            assert(test_pipeline_event==UI_GESTURE_RELEASE&&s_toolbar&&!s_page_jump_open&&test_seek_calls==seeks&&test_turns==turns);
        }
        test_bottom_reset();ui_gesture_t g={0};app_ctx_t ctx={0};int x=track.x+track.width/2,y=footer+60;unsigned seeks=test_seek_calls;
        test_bottom_frame(&g,&ctx,x,y,100,true,false,1,false);
        test_bottom_frame(&g,&ctx,x+80,y,140,false,false,1,false);
        assert(g.captured&&s_scrubbing&&s_progress_pending&&!s_toolbar);
        test_bottom_frame(&g,&ctx,x+80,y,700,false,false,1,false);assert(test_pipeline_event==-1&&!s_toolbar);
        uint32_t preview=s_scrub_target;
        test_bottom_frame(&g,&ctx,x-100,y,740,false,true,0,false);
        assert(test_pipeline_event==UI_GESTURE_RELEASE&&test_seek_calls==seeks+1&&!s_progress_pending&&!s_page_jump_open&&s_scrub_target==preview);
        // 纵向移动和24像素边界都不认领进度拖动。/ Vertical movement and exactly 24 horizontal pixels never capture scrubbing.
        test_bottom_reset();ui_gesture_reset(&g);seeks=test_seek_calls;
        test_bottom_frame(&g,&ctx,x,y,100,true,false,1,false);
        test_bottom_frame(&g,&ctx,x+24,y,120,false,false,1,false);assert(!g.captured&&!s_scrubbing);
        test_bottom_frame(&g,&ctx,x+26,y-50,140,false,false,1,false);assert(!g.captured&&!s_scrubbing);
        test_bottom_frame(&g,&ctx,x+26,y-100,160,false,true,0,false);
        assert(!s_progress_pending&&!s_page_jump_open&&test_seek_calls==seeks);
        // 防御性控制器测试：即便错误标为远坐标TAP也只能开窗，不能寻址。
        // Defensive controller case: even a distant terminal mislabeled TAP may only open entry, never seek.
        test_bottom_reset();ui_gesture_reset(&g);seeks=test_seek_calls;
        test_bottom_frame(&g,&ctx,x,y,100,true,false,1,false);
        ui_gesture_event_t far_tap=test_raw_event(UI_GESTURE_TAP,x,y,x-180,y);
        assert(gesture_event(&ctx,&far_tap)==APP_REDRAW_PAGE&&s_page_jump_open&&test_seek_calls==seeks&&!s_scrubbing);
        // 两采样快拖没有按住MOVE，真实识别器虽发SWIPE也只取消。
        // A two-sample fast drag has no held MOVE; cancel even when the recognizer emits SWIPE.
        test_bottom_reset();ui_gesture_reset(&g);seeks=test_seek_calls;
        test_bottom_frame(&g,&ctx,x,y,100,true,false,1,false);
        test_bottom_frame(&g,&ctx,x-100,y,130,false,true,0,false);
        assert(test_pipeline_event>=UI_GESTURE_SWIPE_L&&test_pipeline_event<=UI_GESTURE_SWIPE_D);
        assert(test_seek_calls==seeks&&!s_progress_pending&&!s_page_jump_open);
        test_bottom_reset();ui_gesture_reset(&g);seeks=test_seek_calls;
        test_bottom_frame(&g,&ctx,x,y,100,true,false,1,false);
        test_bottom_frame(&g,&ctx,x+40,y,130,false,true,0,false);
        assert(test_pipeline_event==UI_GESTURE_CANCEL&&!s_progress_pending&&test_seek_calls==seeks);
        test_bottom_frame(&g,&ctx,x,y,200,true,false,1,false);
        test_bottom_frame(&g,&ctx,x,y,230,false,false,2,false);
        assert(test_pipeline_event==UI_GESTURE_CANCEL&&!s_progress_pending&&!s_page_jump_open&&test_seek_calls==seeks);
        test_bottom_frame(&g,&ctx,x,y,300,true,false,1,false);
        test_bottom_frame(&g,&ctx,x+80,y,330,false,false,1,false);assert(s_scrubbing);
        test_bottom_frame(&g,&ctx,x+80,y,350,false,false,1,true);
        assert(test_pipeline_event==UI_GESTURE_CANCEL&&!s_progress_pending&&!s_scrubbing&&test_seek_calls==seeks);
        // 返回按钮与工具卡片先于进度热区；底部全局箭头/Back完全不在热区内。
        // Return/tool controls precede progress; global arrows/Back stay outside the progress region.
        test_bottom_reset();ui_gesture_reset(&g);s_jump_back_valid=true;s_jump_chapter=0;s_jump_offset=100;s_chapter=1;s_page=2;
        EpdRect back=progress_back_rect();
        test_bottom_frame(&g,&ctx,back.x+20,back.y+20,100,true,false,1,false);assert(!s_progress_pending&&s_pressed_control==950);
        test_bottom_frame(&g,&ctx,back.x+20,back.y+20,140,false,true,0,false);
        assert(!s_jump_back_valid&&s_chapter==0&&s_page==1&&test_seek_calls==seeks+1);
        test_bottom_reset();ui_gesture_reset(&g);s_toolbar=true;EpdRect tool=tool_rect(8);
        test_bottom_frame(&g,&ctx,tool.x+tool.width/2,tool.y+tool.height/2,100,true,false,1,false);
        assert(!s_progress_pending&&s_pressed_control==208);
        test_bottom_frame(&g,&ctx,tool.x+tool.width/2,tool.y+tool.height/2,130,false,false,1,true);
        for(int i=0;i<3;i++)assert(!ui_rect_hit(progress_contact_rect(),i==0?8:i==1?reader_width()/2:reader_width()-8,reader_height()-16));
        // 正文的真实长按仍进入拖选，不能被底部工具手势替换。
        // A real body hold still starts drag selection instead of bottom tools.
        test_bottom_reset();ui_gesture_reset(&g);test_text_hit=3;strcpy(s_path,"/sdcard/books/body-hold.epub");
        test_bottom_frame(&g,&ctx,200,300,100,true,false,1,false);
        test_bottom_frame(&g,&ctx,200,300,600,false,false,1,false);
        assert(test_pipeline_event==UI_GESTURE_LONG_PRESS&&s_selection_drag&&g.captured&&!s_toolbar);
        test_bottom_frame(&g,&ctx,200,300,640,false,true,0,false);
        assert(s_selection_menu&&!s_selection_drag&&!s_page_jump_open);
    }
    page_jump_close();page_index_reset();s_text=NULL;s_text_len=0;s_selection=s_selection_menu=s_toolbar=false;
    s_product_session=false;test_rotation=EPD_ROT_INVERTED_PORTRAIT;s_book_rotation=EPD_ROT_INVERTED_PORTRAIT;test_reader_width=684;test_reader_height=1216;
    test_body=(EpdRect){40,24,604,960};s_layout_rect=test_body;test_chapter_count=1;
    puts("book_ui_host_test: real recognizer -> callback -> capture -> present -> tick, 4-rotation compact-header tap+hold, strict horizontal MOVE drag, terminal-coordinate rejection, cancel/interruption, control priority and body selection PASS");
}
// 数字输入真实帧之间插入tick和推屏，验证空闲静止、确认后懒定位及局部反馈。
// Interleave ticks/presentation between real keypad frames to verify idle entry, confirmed lazy seeking and local feedback.
static void test_modal_control(ui_gesture_t* g,app_ctx_t* ctx,int action,int64_t* ms){
    EpdRect r=action<12?page_jump_key_rect(action):page_jump_button_rect(action==14?1:0);
    *ms+=80;assert(test_bottom_frame(g,ctx,r.x+r.width/2,r.y+r.height/2,*ms,true,false,1,false)==APP_REDRAW_AREA);
    assert(test_pipeline_tick==APP_REDRAW_NONE);
    *ms+=80;test_bottom_frame(g,ctx,r.x+r.width/2,r.y+r.height/2,*ms,false,true,0,false);
    assert(test_pipeline_event==UI_GESTURE_TAP&&test_pipeline_tick==APP_REDRAW_NONE);
}
static void test_modal_interleaving(void){
    for(int rotation=0;rotation<4;rotation++){
        test_rotation=rotation;s_book_rotation=(enum EpdRotation)rotation;bool land=rotation==0||rotation==2;
        test_reader_width=land?1216:684;test_reader_height=land?684:1216;
        test_body=(EpdRect){40,24,test_reader_width-80,reader_footer_top()-32};test_bottom_reset();
        strcpy(s_path,"/sdcard/books/modal-idle.epub");test_chapter_count=5;
        for(int i=0;i<4;i++)test_count_pages[i]=1;test_count_pages[4]=20;
        ui_gesture_t g={0};app_ctx_t ctx={0};int64_t ms=100;
        unsigned seeks=test_seek_calls,loads=test_count_loads,steps=test_count_steps;
        EpdRect header=progress_rect();int x=header.x+header.width/2,y=header.y+60;
        test_bottom_frame(&g,&ctx,x,y,ms,true,false,1,false);ms+=80;
        assert(test_bottom_frame(&g,&ctx,x,y,ms,false,true,0,false)==APP_REDRAW_PAGE&&s_page_jump_open);
        unsigned pages=test_page_displays,areas=test_area_displays;
        s_du_count=UI_SETTLE_DU_MAX;
        for(int i=0;i<24;i++){ms+=250;test_bottom_frame(&g,&ctx,0,0,ms,false,true,0,false);assert(test_pipeline_tick==APP_REDRAW_NONE);}
        assert(test_count_loads==loads&&test_count_steps==steps&&test_seek_calls==seeks&&test_page_displays==pages&&test_area_displays==areas);
        int keys[]={0,1,2,9,11,1,10};const char* inputs[]={"1","12","123","12","","2","20"};
        for(size_t k=0;k<7;k++){
            test_modal_control(&g,&ctx,keys[k],&ms);assert(!strcmp(s_page_jump_input,inputs[k]));
            for(int i=0;i<4;i++){ms+=250;test_bottom_frame(&g,&ctx,0,0,ms,false,true,0,false);assert(test_pipeline_tick==APP_REDRAW_NONE&&!strcmp(s_page_jump_input,inputs[k]));}
            assert(test_page_displays==pages&&test_count_loads==loads&&test_count_steps==steps&&test_seek_calls==seeks);
        }
        assert(s_du_count>=UI_SETTLE_DU_MAX);
        test_modal_control(&g,&ctx,14,&ms);assert(s_page_jump_waiting&&test_count_loads==loads&&test_page_displays==pages);
        int feedbacks=0;int64_t last_feedback=ms;
        while(s_page_jump_open){
            ms+=250;test_bottom_frame(&g,&ctx,0,0,ms,false,true,0,false);
            if(s_page_jump_open){
                assert(test_pipeline_tick!=APP_REDRAW_PAGE&&test_pipeline_tick!=APP_REDRAW_FULL&&!strcmp(s_page_jump_input,"20"));
                if(test_pipeline_tick==APP_REDRAW_AREA){
                    EpdRect r=page_jump_status_rect();assert(!memcmp(&test_display_area,&r,sizeof(r))&&test_display_mode==MODE_DU&&ms-last_feedback>=1000);
                    last_feedback=ms;++feedbacks;
                }
            }
            assert(ms<30000);
        }
        assert(feedbacks==1&&test_page_displays==pages+1&&test_seek_calls==seeks+1);
        assert(test_count_loads==loads+5&&s_page_index.done==4&&s_page_index.total==4&&test_seek_chapter==4&&test_seek_offset==1500);
        // 确认后计页中取消：不提交，后续tick不续跑；重新输入后的失败也保留原位。
        // Cancel confirmed counting without a commit or resumed ticks; a later counting failure also retains position.
        test_bottom_reset();strcpy(s_path,"/sdcard/books/modal-cancel.epub");test_chapter_count=5;test_count_pages[0]=20;
        ui_gesture_reset(&g);page_jump_open();test_modal_control(&g,&ctx,4,&ms);test_modal_control(&g,&ctx,14,&ms);
        ms+=50;test_bottom_frame(&g,&ctx,0,0,ms,false,true,0,false);assert(s_page_index.job&&s_page_jump_waiting);
        seeks=test_seek_calls;test_modal_control(&g,&ctx,12,&ms);assert(!s_page_jump_open&&!s_page_index.job&&test_seek_calls==seeks&&s_chapter==0&&s_page==1);
        loads=test_count_loads;steps=test_count_steps;
        for(int i=0;i<4;i++){ms+=250;test_bottom_frame(&g,&ctx,0,0,ms,false,true,0,false);}
        assert(test_count_loads==loads&&test_count_steps==steps&&test_seek_calls==seeks);
        page_jump_open();test_modal_control(&g,&ctx,4,&ms);test_modal_control(&g,&ctx,14,&ms);
        ms+=50;test_bottom_frame(&g,&ctx,0,0,ms,false,true,0,false);assert(s_page_index.job);
        test_count_step_fail=true;pages=test_page_displays;ms+=50;test_bottom_frame(&g,&ctx,0,0,ms,false,true,0,false);test_count_step_fail=false;
        assert(test_pipeline_tick==APP_REDRAW_AREA&&test_page_displays==pages&&s_page_jump_open&&!s_page_jump_waiting&&s_page_index.failed&&!s_page_index.job);
        assert(test_seek_calls==seeks&&s_chapter==0&&s_page==1&&!s_jump_back_valid);
        test_modal_control(&g,&ctx,0,&ms);assert(!strcmp(s_page_jump_input,"51"));
        loads=test_count_loads;steps=test_count_steps;areas=test_area_displays;
        for(int i=0;i<8;i++){ms+=250;test_bottom_frame(&g,&ctx,0,0,ms,false,true,0,false);assert(test_pipeline_tick==APP_REDRAW_NONE&&!strcmp(s_page_jump_input,"51"));}
        assert(test_count_loads==loads&&test_count_steps==steps&&test_area_displays==areas&&test_page_displays==pages);
    }
    page_jump_close();page_index_reset();s_text=NULL;s_text_len=0;s_path[0]=0;s_product_session=false;s_du_count=0;
    test_rotation=EPD_ROT_INVERTED_PORTRAIT;s_book_rotation=EPD_ROT_INVERTED_PORTRAIT;test_reader_width=684;test_reader_height=1216;test_body=(EpdRect){40,24,604,960};s_layout_rect=test_body;
    test_chapter_count=1;test_count_pages[0]=3;test_count_pages[1]=5;test_count_pages[2]=2;test_count_pages[3]=4;test_count_pages[4]=1;
    puts("book_ui_host_test: 4-rotation real keypad/capture/present/on_tick interleaving, idle zero count/seek/redraw, stable input, confirmed lazy early seek, <=1Hz summary-only DU, cancel/failure rollback PASS");
}
// 真实识别器验证插图确认：独立接触才确认、取消保留页面、遮盖时按键不能穿透。
// The real recognizer verifies fresh-contact confirmation, retained reading and modal key isolation.
static void test_image_confirmation(void) {
    blk_t blocks[]={{.image_src="confirm.png"}};
    for(int rotation=0;rotation<4;++rotation) {
        free_image();page_jump_close();page_index_reset();
        test_rotation=rotation;s_book_rotation=(enum EpdRotation)rotation;bool land=rotation==0||rotation==2;
        test_reader_width=land?1216:684;test_reader_height=land?684:1216;
        test_body=(EpdRect){40,96,reader_width()-80,reader_height()-120};s_layout_rect=test_body;
        s_product_session=true;s_view=READING;s_text="book";s_text_len=4;
        s_blocks=blocks;s_block_count=1;s_chapter=9;s_page=6;
        s_selection=s_selection_drag=s_selection_menu=s_toolbar=s_scan_pending=s_clear_confirm=s_batch_confirm=false;
        s_jump_back_valid=s_save_failed=false;s_open_path[0]=s_resume_path[0]=0;
        s_du_count=0;strcpy(s_font_path,ttf_font_path());
        app_ctx_t ctx={0};ui_gesture_t g={0};unsigned turns=test_turns;int loads=test_image_loads;
        EpdRect panel=image_confirm_rect(),cancel=image_confirm_button_rect(0),confirm=image_confirm_button_rect(1),back=image_back_rect();
        assert(panel.x>=0&&panel.y>=0&&panel.x+panel.width<=reader_width()&&panel.y+panel.height<=reader_height());
        assert(cancel.x+cancel.width<confirm.x&&confirm.x+confirm.width<panel.x+panel.width);
        assert(back.y>=0&&back.y+back.height<reader_height()-48);
        // 正文图按下不读文件；抬起只出现确认框。/ Image press does no IO; release only opens confirmation.
        assert(test_bottom_frame(&g,&ctx,300,150,100,true,false,1,false)==APP_REDRAW_NONE);
        assert(!s_image_confirm_open&&!s_image_open&&test_image_loads==loads);
        assert(test_bottom_frame(&g,&ctx,300,150,180,false,true,0,false)==APP_REDRAW_PAGE);
        assert(s_image_confirm_open&&!s_image_open&&test_image_loads==loads&&s_page==6&&s_chapter==9);
        int cx=confirm.x+confirm.width/2,cy=confirm.y+confirm.height/2;
        ui_gesture_event_t event=test_raw_event(UI_GESTURE_RELEASE,cx,cy,cx,cy);
        assert(gesture_event(&ctx,&event)==APP_REDRAW_NONE&&s_image_confirm_open&&test_image_loads==loads);
        event.type=UI_GESTURE_TAP;assert(gesture_event(&ctx,&event)==APP_REDRAW_NONE&&s_image_confirm_open);
        assert(on_key(&ctx,UI_KEY_1)==APP_REDRAW_NONE&&on_key(&ctx,UI_KEY_3)==APP_REDRAW_NONE);
        assert(app_book_external_turn(&ctx,1)==APP_REDRAW_NONE&&app_book_external_turn(&ctx,-1)==APP_REDRAW_NONE&&test_turns==turns);
        ctx.consumed=false;ctx.now_ms=50000;assert(on_tick(&ctx)==APP_REDRAW_NONE&&test_image_loads==loads);
        // 新接触取消后，随后误投的TAP仍不能接受。/ Cancelled confirmation cannot accept a stale terminal TAP.
        event=test_raw_event(UI_GESTURE_PRESS,cx,cy,cx,cy);assert(gesture_event(&ctx,&event)==APP_REDRAW_AREA);
        event.type=UI_GESTURE_CANCEL;assert(gesture_event(&ctx,&event)==APP_REDRAW_PAGE);
        event.type=UI_GESTURE_TAP;assert(gesture_event(&ctx,&event)==APP_REDRAW_NONE&&s_image_confirm_open&&!s_image_open);
        // 继续阅读独立按下/抬起，保留章节页码，不加载图片。/ Continue reading preserves the page without decoding.
        ui_gesture_reset(&g);int x=cancel.x+cancel.width/2,y=cancel.y+cancel.height/2;
        assert(test_bottom_frame(&g,&ctx,x,y,50200,true,false,1,false)==APP_REDRAW_AREA);
        assert(test_bottom_frame(&g,&ctx,x,y,50280,false,true,0,false)==APP_REDRAW_PAGE);
        assert(!s_image_confirm_open&&!s_image_open&&test_image_loads==loads&&s_chapter==9&&s_page==6);
        assert(image_confirm_request(0)==APP_REDRAW_PAGE);
        assert(on_key(&ctx,UI_KEY_2)==APP_REDRAW_PAGE&&!s_image_confirm_open&&!ctx.request_app);
        assert(on_key_long(&ctx,UI_KEY_2)==APP_REDRAW_NONE&&!ctx.request_app&&!ctx.request_menu);
        assert(image_confirm_request(0)==APP_REDRAW_PAGE);
        assert(app_book_global_action(&ctx,BOOK_GLOBAL_BACK)==APP_REDRAW_PAGE&&!s_image_confirm_open&&!ctx.request_app);
        // 唯一新确认接触才解码；全屏中键先返回正文。/ Only a fresh confirmation decodes; middle returns to text first.
        assert(image_confirm_request(0)==APP_REDRAW_PAGE);ui_gesture_reset(&g);
        assert(test_bottom_frame(&g,&ctx,cx,cy,50400,true,false,1,false)==APP_REDRAW_AREA);
        assert(test_image_loads==loads&&!s_image_open);
        assert(test_bottom_frame(&g,&ctx,cx,cy,50480,false,true,0,false)==APP_REDRAW_PAGE);
        assert(s_image_open&&!s_image_confirm_open&&test_image_loads==loads+1&&s_page==6&&s_chapter==9);
        assert(on_key(&ctx,UI_KEY_2)==APP_REDRAW_PAGE&&!s_image_open&&!ctx.request_app&&test_turns==turns);
        // 图上左右滑只翻页，均不打开确认层。/ Horizontal image swipes turn pages without opening confirmation.
        for(int dir=-1;dir<=1;dir+=2) {
            ui_gesture_reset(&g);
            test_bottom_frame(&g,&ctx,300,150,50600,true,false,1,false);
            test_bottom_frame(&g,&ctx,300-dir*160,150,50700,false,true,0,false);
            assert(!s_image_confirm_open&&!s_image_open&&test_last_turn_dir==dir&&test_image_loads==loads+1);
        }
        assert(test_turns==turns+2);
        // 释放媒体资源时同步丢弃待确认索引。/ Resource cleanup also clears a pending image index.
        assert(image_confirm_request(0)==APP_REDRAW_PAGE);free_image();
        assert(!s_image_confirm_open&&s_image_confirm_block==SIZE_MAX);
    }
    s_blocks=NULL;s_block_count=0;s_text=NULL;s_text_len=0;s_product_session=false;
    test_rotation=EPD_ROT_INVERTED_PORTRAIT;s_book_rotation=EPD_ROT_INVERTED_PORTRAIT;test_reader_width=684;test_reader_height=1216;
    test_body=(EpdRect){40,24,604,960};s_layout_rect=test_body;s_chapter=s_page=0;
    test_image_loads=0;test_turns=0;
    puts("book_ui_host_test: real image confirmation 4 rotations, no IO before fresh confirmation, stale release/cancel rejection, modal physical/BLE isolation, retained page and image swipes PASS");
}
static void test_outside_modal_dismissal(void){
    for(int rotation=0;rotation<4;rotation++)for(int kind=1;kind<=7;kind++){
        test_rotation=rotation;s_book_rotation=(enum EpdRotation)rotation;bool land=rotation==0||rotation==2;
        test_reader_width=land?1216:684;test_reader_height=land?684:1216;
        test_body=(EpdRect){40,24,test_reader_width-80,reader_footer_top()-32};test_bottom_reset();
        s_resume_path[0]=0;s_image_confirm_open=s_batch_confirm=false;s_view=READING;s_chapter=2;s_page=1;strcpy(s_path,"/sdcard/books/outside.epub");
        if(kind==1)s_page_jump_open=true;
        else if(kind==2)s_image_confirm_open=true;
        else if(kind==3){assert(select_character(3));}
        else if(kind==4)s_toolbar=true;
        else if(kind==5)strcpy(s_resume_path,"/sdcard/books/resume.epub");
        else if(kind==6){s_view=MANAGE;s_clear_confirm=true;strcpy(s_managed.name,"modal.epub");}
        else{s_view=BULK;s_batch_confirm=true;}
        EpdRect panel;assert(book_modal_rect(&panel)==kind);
        int x=test_reader_width-12,y=12;assert(!ui_rect_hit(panel,x,y));
        unsigned turns=test_turns,seeks=test_seek_calls,highlights=test_highlights,notes=test_notes;int images=test_image_loads;
        app_ctx_t ctx={0};ui_gesture_t g={0};
        ui_gesture_event_t inside=test_raw_event(UI_GESTURE_TAP,panel.x+2,panel.y+2,panel.x+2,panel.y+2);
        assert(gesture_event(&ctx,&inside)==APP_REDRAW_NONE&&book_modal_rect(&panel)==kind);
        // 外侧按下拖入浮层不能提交，也不能落到背景。/ An outside press dragged into a panel never commits or reaches the background.
        assert(test_bottom_frame(&g,&ctx,x,y,100,true,false,1,false)==APP_REDRAW_NONE);
        assert(test_bottom_frame(&g,&ctx,panel.x+30,panel.y+30,150,false,true,0,false)==APP_REDRAW_NONE);
        assert(book_modal_rect(&panel)==kind&&s_chapter==2&&s_page==1);
        ui_gesture_reset(&g);
        assert(test_bottom_frame(&g,&ctx,x,y,200,true,false,1,false)==APP_REDRAW_NONE);
        assert(test_bottom_frame(&g,&ctx,x,y,260,false,true,0,false)==APP_REDRAW_PAGE);
        if(kind==6){assert(s_view==MANAGE&&!s_clear_confirm);ui_gesture_reset(&g);test_bottom_frame(&g,&ctx,x,y,300,true,false,1,false);assert(test_bottom_frame(&g,&ctx,x,y,360,false,true,0,false)==APP_REDRAW_PAGE&&s_view==SHELF);}
        else assert(book_modal_rect(&panel)==0);
        assert(test_turns==turns&&test_seek_calls==seeks&&test_image_loads==images&&test_highlights==highlights&&test_notes==notes&&s_chapter==2&&s_page==1&&!ctx.request_app&&!ctx.request_menu);
    }
    page_jump_close();page_index_reset();s_view=READING;s_product_session=false;s_text=NULL;s_text_len=0;
    s_selection=s_selection_menu=s_toolbar=s_image_confirm_open=s_batch_confirm=false;s_resume_path[0]=0;
    test_rotation=EPD_ROT_INVERTED_PORTRAIT;s_book_rotation=EPD_ROT_INVERTED_PORTRAIT;test_reader_width=684;test_reader_height=1216;test_body=(EpdRect){40,24,604,960};s_layout_rect=test_body;
    puts("book_ui_host_test: outside taps dismiss 7 overlays in 4 rotations, nested confirmation cancels first, no background turns/seeks/annotation writes or dragged-in activation PASS");
}
int main(void) {
    test_annotations();
    test_progress();
    test_raw_orientation();
    test_page_modal();
    test_bottom_pipeline();
    test_modal_interleaving();
    test_image_confirmation();
    test_outside_modal_dismissal();
    // 对外开书接口仅排队，不扫描、不打开、不保留调用方缓冲区。/ Public opening only queues a copied path.
    strcpy(test_roots[0].path,"/sdcard/books");
    strcpy(test_roots[1].path,"/flash/books");test_root_count=2;
    const char* valid_paths[]={"/sdcard/root.TXT","/sdcard/root.EPUB","/sdcard/books/sub/novel.epub","/flash/books/sub/novel.txt","/sdcard/books/chapter..txt","/sdcard/books/.hidden.epub"};
    app_ctx_t navigation_ctx={0};
    for(size_t i=0;i<sizeof(valid_paths)/sizeof(valid_paths[0]);++i){
        assert(app_book_open_path(valid_paths[i]));
        assert(!strcmp(s_requested_path,valid_paths[i])&&test_open_calls==0);
    }
    char caller_path[]="/sdcard/books/copied.epub";
    assert(app_book_open_path(caller_path));caller_path[14]='X';
    assert(!strcmp(s_requested_path,"/sdcard/books/copied.epub")&&test_open_calls==0);
    const char* invalid_paths[]={NULL,"","novel.txt","/etc/novel.txt","/sdcard/other/novel.txt","/flash/root.epub","/sdcard/bookstore/novel.txt","/flash/bookstore/novel.txt","/sdcard/books","/sdcard/books/novel.pdf","/sdcard/books/novel.txt.bak","/sdcard/books/../novel.txt","/sdcard/books/./novel.txt","/sdcard/books//novel.txt","//sdcard/root.txt","/sdcard/../root.txt","/sdcard/books/sub/../../novel.epub"};
    for(size_t i=0;i<sizeof(invalid_paths)/sizeof(invalid_paths[0]);++i){
        assert(!app_book_open_path(invalid_paths[i]));
        assert(!strcmp(s_requested_path,"/sdcard/books/copied.epub")&&test_open_calls==0);
    }
    char boundary_path[BOOK_STORE_PATH_MAX+1];
    memset(boundary_path,'a',sizeof(boundary_path));
    memcpy(boundary_path,"/sdcard/books/",14);
    memcpy(boundary_path+BOOK_STORE_PATH_MAX-5,".txt",5);
    assert(strlen(boundary_path)==BOOK_STORE_PATH_MAX-1&&app_book_open_path(boundary_path));
    memmove(boundary_path+BOOK_STORE_PATH_MAX-4,".txt",5);
    assert(strlen(boundary_path)==BOOK_STORE_PATH_MAX&&!app_book_open_path(boundary_path));
    assert(strlen(s_requested_path)==BOOK_STORE_PATH_MAX-1&&test_open_calls==0);
    // 产品首帧只画载入页；成功推屏后的未消费tick才读取字体/图书，且仅执行一次。
    // The initial product frame only paints loading; font/book IO waits for presentation and an unconsumed later tick, once only.
    assert(app_book_open_path("/sdcard/books/direct.epub"));
    strcpy(test_preferred_font,"/sdcard/fonts/MiSansVF.ttf");
    strcpy(s_resume_path,"/sdcard/books/stale.txt");s_scan_pending=s_resume_pending=true;
    on_enter(&navigation_ctx);
    assert(test_open_calls==0&&test_font_load_calls==0&&!s_open_presented&&!strcmp(s_open_path,"/sdcard/books/direct.epub"));
    assert(s_product_session&&!s_requested_path[0]&&!s_resume_path[0]&&!s_scan_pending&&!s_resume_pending);
    assert(test_rotation_calls==0);
    assert(draw_pending_open(NULL)&&strstr(test_wrapped,"direct.epub"));
    assert(draw_pending_open(NULL)&&!s_open_presented&&test_open_calls==0&&test_font_load_calls==0);
    assert(on_tick(&navigation_ctx)==APP_REDRAW_NONE&&test_open_calls==0&&test_font_load_calls==0);
    assert(present(&navigation_ctx,APP_REDRAW_NONE)&&!s_open_presented);
    assert(present(&navigation_ctx,APP_REDRAW_DONE)&&!s_open_presented);
    assert(present(&navigation_ctx,APP_REDRAW_FULL)&&s_open_presented&&test_loading_displays==1);
    navigation_ctx.consumed=true;
    assert(on_tick(&navigation_ctx)==APP_REDRAW_NONE&&test_open_calls==0&&test_font_load_calls==0);
    navigation_ctx.consumed=false;navigation_ctx.request_app=&test_home;
    assert(on_tick(&navigation_ctx)==APP_REDRAW_NONE&&test_open_calls==0);
    navigation_ctx.request_app=NULL;navigation_ctx.request_menu=true;
    assert(on_tick(&navigation_ctx)==APP_REDRAW_NONE&&test_open_calls==0);
    navigation_ctx.request_menu=false;navigation_ctx.request_return=true;
    assert(on_tick(&navigation_ctx)==APP_REDRAW_NONE&&test_open_calls==0);
    navigation_ctx.request_return=false;navigation_ctx.request_developer_menu=true;
    assert(on_tick(&navigation_ctx)==APP_REDRAW_NONE&&test_open_calls==0);
    navigation_ctx.request_developer_menu=false;
    assert(on_tick(&navigation_ctx)==APP_REDRAW_PAGE&&test_open_calls==1&&test_font_load_calls==1);
    assert(!strcmp(test_open_path,"/sdcard/books/direct.epub")&&!strcmp(test_current_font,test_preferred_font));
    assert(!s_open_path[0]&&!s_open_presented&&!draw_pending_open(NULL));
    unsigned displays_after_open=test_display_calls;
    assert(on_tick(&navigation_ctx)==APP_REDRAW_NONE&&test_open_calls==1&&test_font_load_calls==1);
    assert(test_display_calls==displays_after_open);
    on_enter(&navigation_ctx);
    assert(test_open_calls==1&&!s_product_session&&s_scan_pending&&s_resume_pending&&!s_open_path[0]);
    test_open_fails=true;assert(app_book_open_path("/sdcard/books/missing.epub"));on_enter(&navigation_ctx);
    assert(test_open_calls==1&&!s_open_presented);
    present(&navigation_ctx,APP_REDRAW_PAGE);assert(on_tick(&navigation_ctx)==APP_REDRAW_PAGE);
    assert(test_open_calls==2&&s_product_session&&!s_scan_pending&&!s_resume_pending&&!s_requested_path[0]);
    assert(!s_open_path[0]&&!s_open_presented&&s_view==SHELF);
    assert(on_key(&navigation_ctx,UI_KEY_2)==APP_REDRAW_NONE&&navigation_ctx.request_app==&test_home&&!navigation_ctx.request_menu);
    test_open_fails=false;
    // 读书前取消、离页与失卡不得继续打开旧队列。/ Cancellation, exit and media loss must discard the active pending open.
    navigation_ctx=(app_ctx_t){0};assert(app_book_open_path("/sdcard/books/cancel.epub"));on_enter(&navigation_ctx);
    assert(on_key(&navigation_ctx,UI_KEY_1)==APP_REDRAW_NONE&&test_open_calls==2);
    assert(on_key(&navigation_ctx,UI_KEY_2)==APP_REDRAW_NONE&&!s_open_path[0]);
    assert(on_tick(&navigation_ctx)==APP_REDRAW_NONE&&test_open_calls==2);
    navigation_ctx=(app_ctx_t){0};assert(app_book_open_path("/sdcard/books/exit.epub"));on_enter(&navigation_ctx);
    present(&navigation_ctx,APP_REDRAW_FULL);book_on_exit(&navigation_ctx);
    assert(!s_open_path[0]&&!s_open_presented&&on_tick(&navigation_ctx)==APP_REDRAW_NONE&&test_open_calls==2);
    assert(app_book_open_path("/sdcard/books/lost.epub"));on_enter(&navigation_ctx);
    present(&navigation_ctx,APP_REDRAW_FULL);book_on_media_lost(&navigation_ctx);
    assert(!s_open_path[0]&&!s_open_presented&&s_product_session&&strstr(s_message,"返回书架"));
    assert(on_tick(&navigation_ctx)==APP_REDRAW_NONE&&test_open_calls==2);
    // 字体失败不篡改首选；已匹配字体不重开，显式内置选择同样延迟到首帧之后。
    // Font failure preserves preference; matching fonts are reused, and explicit builtin loading also follows the first frame.
    strcpy(test_current_font,"builtin");test_font_fails=true;
    assert(app_book_open_path("/sdcard/books/font-failure.epub"));on_enter(&navigation_ctx);
    unsigned font_before=test_font_load_calls;present(&navigation_ctx,APP_REDRAW_FULL);
    assert(on_tick(&navigation_ctx)==APP_REDRAW_PAGE&&test_open_calls==3&&test_font_load_calls==font_before+1);
    assert(!strcmp(test_preferred_font,"/sdcard/fonts/MiSansVF.ttf")&&!strcmp(test_current_font,"builtin"));
    test_font_fails=false;strcpy(test_current_font,test_preferred_font);
    assert(app_book_open_path("/sdcard/books/font-reuse.epub"));on_enter(&navigation_ctx);
    font_before=test_font_load_calls;present(&navigation_ctx,APP_REDRAW_FULL);
    assert(on_tick(&navigation_ctx)==APP_REDRAW_PAGE&&test_open_calls==4&&test_font_load_calls==font_before);
    strcpy(test_preferred_font,"builtin");
    assert(app_book_open_path("/sdcard/books/font-builtin.epub"));on_enter(&navigation_ctx);
    assert(test_builtin_load_calls==0);present(&navigation_ctx,APP_REDRAW_FULL);
    assert(on_tick(&navigation_ctx)==APP_REDRAW_PAGE&&test_open_calls==5&&test_builtin_load_calls==1&&ttf_font_is_builtin());
    strcpy(test_preferred_font,"builtin-serif");
    assert(app_book_open_path("/sdcard/books/font-serif.epub"));on_enter(&navigation_ctx);present(&navigation_ctx,APP_REDRAW_FULL);
    assert(on_tick(&navigation_ctx)==APP_REDRAW_PAGE&&test_builtin_load_calls==2&&!strcmp(test_current_font,"builtin-serif"));
    test_preferred_font[0]=0;
    assert(app_book_open_path("/sdcard/books/font-empty.epub"));on_enter(&navigation_ctx);present(&navigation_ctx,APP_REDRAW_FULL);
    assert(on_tick(&navigation_ctx)==APP_REDRAW_PAGE&&test_builtin_load_calls==3&&!strcmp(test_current_font,"builtin")&&!test_preferred_font[0]);
    // 最长排队文件名自动换行，提示不侵入底部返回区；绘制不消费队列。
    // The longest queued filename wraps without reaching the return bar; drawing does not consume the queue.
    assert(app_book_open_path(boundary_path)==false);
    memset(boundary_path,'W',sizeof(boundary_path));memcpy(boundary_path,"/sdcard/",8);
    memcpy(boundary_path+BOOK_STORE_PATH_MAX-5,".txt",5);
    assert(app_book_open_path(boundary_path));on_enter(&navigation_ctx);test_text_bottom=0;
    assert(draw_pending_open(NULL)&&test_text_bottom<ui_bar_rect(0,1).y&&!s_open_presented);
    assert(!strcmp(s_open_path,boundary_path)&&strstr(test_wrapped,".txt"));
    product_exit(&navigation_ctx);
    // 图片中键先返回正文，其余状态保留产品返回出口。/ Image middle returns to text; other states preserve product exits.
    for(int scenario=0;scenario<13;++scenario){
        test_navigation_state(&navigation_ctx,true,scenario);
        strcpy(s_requested_path,"/sdcard/books/queued.epub");
        if(scenario==10||scenario==11) {
            assert(on_key(&navigation_ctx,UI_KEY_2)==APP_REDRAW_PAGE&&!s_image_open&&!navigation_ctx.request_app&&!navigation_ctx.request_menu);
        } else {
            assert(on_key(&navigation_ctx,UI_KEY_2)==APP_REDRAW_NONE);
            assert(navigation_ctx.request_app==&test_home&&!navigation_ctx.request_menu&&!s_requested_path[0]&&!s_open_path[0]&&s_pressed_control==-1);
        }
        test_navigation_state(&navigation_ctx,true,scenario);
        assert(on_key_long(&navigation_ctx,UI_KEY_2)==APP_REDRAW_NONE);
        assert(navigation_ctx.request_app==&test_home&&!navigation_ctx.request_menu);
        test_navigation_state(&navigation_ctx,true,scenario);
        assert(on_key_long(&navigation_ctx,UI_KEY_1)==APP_REDRAW_NONE&&!navigation_ctx.request_app&&!navigation_ctx.request_menu);
        ui_gesture_event_t handle={.type=UI_GESTURE_PRESS,.x0=650,.y0=1150,.x=650,.y=1150};
        assert(gesture_event(&navigation_ctx,&handle)==APP_REDRAW_NONE);
        assert(!navigation_ctx.request_app&&!navigation_ctx.request_menu);
        test_navigation_state(&navigation_ctx,false,scenario);
        assert(gesture_event(&navigation_ctx,&handle)==APP_REDRAW_NONE);
        assert(navigation_ctx.request_menu&&!navigation_ctx.request_app&&s_pressed_control==-1);
    }
    // 工程页仍保留原中键逻辑，不被产品返回覆盖。/ Engineering keeps its existing middle-key behavior.
    test_navigation_state(&navigation_ctx,false,READING);
    assert(on_key(&navigation_ctx,UI_KEY_2)==APP_REDRAW_PAGE&&s_toolbar&&!navigation_ctx.request_app&&!navigation_ctx.request_menu);
    assert(on_key(&navigation_ctx,UI_KEY_2)==APP_REDRAW_PAGE&&!s_toolbar);
    s_view=TOC;s_text="toc";
    assert(on_key(&navigation_ctx,UI_KEY_2)==APP_REDRAW_PAGE&&s_view==READING&&!s_toolbar);s_text=NULL;
    s_view=SHELF;
    assert(on_key(&navigation_ctx,UI_KEY_2)==APP_REDRAW_NONE&&navigation_ctx.request_menu&&!navigation_ctx.request_app);
    for(int scenario=6;scenario<=8;++scenario){
        test_navigation_state(&navigation_ctx,false,scenario);
        assert(on_key(&navigation_ctx,UI_KEY_2)==APP_REDRAW_NONE&&!navigation_ctx.request_menu&&!navigation_ctx.request_app);
    }
    test_navigation_state(&navigation_ctx,false,10);
    assert(on_key(&navigation_ctx,UI_KEY_2)==APP_REDRAW_PAGE&&!s_image_open&&!navigation_ctx.request_menu&&!navigation_ctx.request_app);
    test_navigation_state(&navigation_ctx,false,11);
    assert(on_key_long(&navigation_ctx,UI_KEY_2)==APP_REDRAW_NONE&&navigation_ctx.request_menu&&!navigation_ctx.request_app);
    // 产品字体页往返重新排队同一本书，工程字体页不创建产品队列。/ Font round trips retain the product's book only.
    test_navigation_state(&navigation_ctx,true,READING);
    strcpy(s_path,"/sdcard/books/font-roundtrip.epub");goto_font(&navigation_ctx);
    assert(navigation_ctx.request_app==&test_font&&!strcmp(s_requested_path,s_path));
    book_on_exit(&navigation_ctx);navigation_ctx=(app_ctx_t){0};
    unsigned before_font_open=test_open_calls;on_enter(&navigation_ctx);
    assert(test_open_calls==before_font_open&&!s_open_presented&&!strcmp(s_open_path,"/sdcard/books/font-roundtrip.epub"));
    assert(on_tick(&navigation_ctx)==APP_REDRAW_NONE&&test_open_calls==before_font_open);
    present(&navigation_ctx,APP_REDRAW_FULL);assert(on_tick(&navigation_ctx)==APP_REDRAW_PAGE);
    assert(test_open_calls==before_font_open+1&&!strcmp(test_open_path,"/sdcard/books/font-roundtrip.epub"));
    assert(s_product_session&&!s_requested_path[0]&&!s_scan_pending&&!s_resume_pending);
    test_navigation_state(&navigation_ctx,false,READING);
    strcpy(s_path,"/sdcard/books/engineering.txt");goto_font(&navigation_ctx);
    assert(navigation_ctx.request_app==&test_font&&!s_requested_path[0]);
    // 产品失卡停在错误页，工程模式仍可检查内置书架。/ Product media loss stays on its error page; engineering still scans.
    test_navigation_state(&navigation_ctx,true,11);navigation_ctx.leaf=5;s_count=s_visible_count=9;
    s_resume_pending=true;book_on_media_lost(&navigation_ctx);
    assert(s_product_session&&s_view==SHELF&&!s_scan_pending&&!s_resume_pending&&!s_resume_path[0]);
    assert(!s_image_open&&!s_clear_confirm&&!s_batch_confirm&&!s_count&&!s_visible_count&&navigation_ctx.leaf==0);
    assert(test_system_media_lost>0);
    unsigned lost_before_restore=test_system_media_lost,restored_before=test_system_media_restored;
    book_on_media_restored(&navigation_ctx);
    assert(test_system_media_lost==lost_before_restore&&test_system_media_restored==restored_before+1);
    assert(!s_scan_pending&&!s_resume_pending&&!s_open_path[0]&&strstr(s_message,"已恢复"));
    assert(strstr(s_message,"返回书架")&&on_key(&navigation_ctx,UI_KEY_2)==APP_REDRAW_NONE&&navigation_ctx.request_app==&test_home&&!navigation_ctx.request_menu);
    test_navigation_state(&navigation_ctx,false,11);s_resume_pending=true;
    book_on_media_lost(&navigation_ctx);
    assert(!s_product_session&&s_view==SHELF&&s_scan_pending&&!s_resume_pending&&!s_resume_path[0]&&!navigation_ctx.request_app);
    s_scan_pending=false;book_on_media_restored(&navigation_ctx);
    assert(s_scan_pending&&!s_resume_pending&&test_system_media_restored==restored_before+1);
    // 还原原有书架/图片测试前置条件。/ Restore the original shelf/image preconditions.
    test_navigation_state(&navigation_ctx,false,READING);test_open_calls=0;
    test_root_count=1;memset(test_roots,0,sizeof(test_roots));s_resume_pending=false;
    puts("book_ui_host_test: real annotation select/touch/key/anchor/jump + product queue/loading-before-IO/navigation/font round-trip PASS (parser/drawing primitives/font/hardware doubles)");
    app_ctx_t image_ctx={0};
    blk_t image_blocks[]={{.image_src="first.png",.image_repeated=true,.image_first_chapter=2},{.image_src="second.jpg"}};
    s_blocks=image_blocks;s_block_count=2;s_chapter=9;s_page=6;s_view=READING;
    ui_gesture_event_t image_event={.type=UI_GESTURE_PRESS,.x0=300,.y0=150,.x=300,.y=150};
    assert(gesture_event(&image_ctx,&image_event)==APP_REDRAW_NONE && test_image_loads==0);
    image_event.type=UI_GESTURE_SWIPE_L;image_event.x=236;
    gesture_event(&image_ctx,&image_event);assert(test_turns==1 && test_image_loads==0 && !s_image_open);
    image_event.type=UI_GESTURE_TAP;image_event.x=300;
    assert(gesture_event(&image_ctx,&image_event)==APP_REDRAW_PAGE && test_image_loads==0 && s_image_confirm_open && !s_image_open);
    EpdRect image_confirm=image_confirm_button_rect(1);
    image_event=(ui_gesture_event_t){.type=UI_GESTURE_PRESS,.x0=image_confirm.x+image_confirm.width/2,.y0=image_confirm.y+image_confirm.height/2,.x=image_confirm.x+image_confirm.width/2,.y=image_confirm.y+image_confirm.height/2};
    assert(gesture_event(&image_ctx,&image_event)==APP_REDRAW_AREA && test_image_loads==0);
    image_event.type=UI_GESTURE_TAP;
    assert(gesture_event(&image_ctx,&image_event)==APP_REDRAW_PAGE && test_image_loads==1 && s_image_open && !s_image_confirm_open);
    assert(s_image_pixels && strstr(s_image_origin,"第 3 节") && s_page==6 && s_chapter==9);
    image_event.type=UI_GESTURE_SWIPE_L;image_event.x=236;
    assert(gesture_event(&image_ctx,&image_event)==APP_REDRAW_NONE && test_turns==1 && s_image_open);
    assert(on_key(&image_ctx,UI_KEY_3)==APP_REDRAW_PAGE && !s_image_open && test_turns==1 && s_page==6);
    assert(open_image(&image_ctx,0)==APP_REDRAW_PAGE && test_image_loads==1);
    image_event.type=UI_GESTURE_TAP;image_event.x=300;
    assert(gesture_event(&image_ctx,&image_event)==APP_REDRAW_PAGE && !s_image_open && s_page==6);
    test_image_error=ESP_ERR_NOT_FOUND;
    assert(open_image(&image_ctx,1)==APP_REDRAW_PAGE && s_image_open && !s_image_pixels && s_image_error && test_image_loads==2);
    assert(on_key(&image_ctx,UI_KEY_1)==APP_REDRAW_PAGE && !s_image_open && test_turns==1);
    test_image_error=0;assert(open_image(&image_ctx,1)==APP_REDRAW_PAGE && s_image_pixels && test_image_loads==3);
    free_image();assert(!s_image_open && !s_image_pixels && s_image_block==SIZE_MAX);
    image_blocks[0].image_title=true;
    assert(open_image(&image_ctx,0)==APP_REDRAW_PAGE && !s_image_origin[0]);
    free_image();
    s_view=SHELF;
    app_ctx_t resume_ctx={0};
    strcpy(s_resume_path,"/sdcard/books/large.epub");
    assert(on_key(&resume_ctx,UI_KEY_1)==APP_REDRAW_PAGE && test_open_calls==0 && !s_resume_path[0]);
    strcpy(s_resume_path,"/sdcard/books/large.epub");
    assert(on_key(&resume_ctx,UI_KEY_3)==APP_REDRAW_PAGE && test_open_calls==1 && !s_resume_path[0]);
    assert(!strcmp(test_open_path,"/sdcard/books/large.epub"));
    assert(resume_choice(&resume_ctx,true)==APP_REDRAW_NONE && test_open_calls==1);
    strcpy(s_resume_path,"/sdcard/books/large.epub");
    assert(on_key(&resume_ctx,UI_KEY_2)==APP_REDRAW_PAGE && resume_ctx.request_menu && !s_resume_path[0] && test_open_calls==1);

    shelf_entry_t a={.search_match=true,.name="Same.txt",.path="/sdcard/books/A/Same.txt"};
    shelf_entry_t b={.search_match=true,.name="Same.txt",.path="/sdcard/books/B/Same.txt"};
    assert(compare_books(&a,&b)<0);
    b.recent=10;s_recent_sort=true;assert(compare_books(&a,&b)>0);
    b.is_flash=true;s_filter=1;assert(compare_books(&a,&b)<0);
    s_filter=2;assert(compare_books(&a,&b)>0);
    s_filter=0;s_recent_sort=false;
    snprintf(test_roots[0].path,sizeof(test_roots[0].path),"/tmp/book-ui-%d",(int)getpid());
    assert(mkdir(test_roots[0].path,0700)==0);
    for(int i=0;i<65;i++){char path[340];snprintf(path,sizeof(path),"%s/book%03d.txt",test_roots[0].path,i);FILE* f=fopen(path,"w");assert(f);fputs("x",f);fclose(f);}
    app_ctx_t ctx={0};scan_shelf(&ctx);
    assert(s_count==65&&s_visible_count==65&&s_shelf_capacity>=65);
    ctx.leaf=3;s_view=MANAGE;s_clear_confirm=false;s_file_removed=false;
    EpdRect back=manage_rect(0,3);manage_action(&ctx,back.x+1,back.y+1);
    assert(s_view==SHELF&&ctx.leaf==3);
    assert(!strcmp(s_shelf[0].name,"book000.txt")&&!strcmp(s_shelf[64].name,"book064.txt"));
    test_degraded=true;scan_shelf(&ctx);assert(s_count==65&&s_shelf_warning[0]);test_degraded=false;
    test_root_count=2;strcpy(test_roots[1].path,"/nonexistent-book-root");scan_shelf(&ctx);assert(s_count==65&&s_shelf_warning[0]);test_root_count=1;
    free(s_shelf);s_shelf=NULL;s_shelf_capacity=0;s_count=0;test_oom=true;scan_shelf(&ctx);assert(s_count==0&&s_shelf_warning[0]);test_oom=false;
    strcpy(s_path,"/sdcard/books/a.txt");s_text="text";s_page=1;s_unsaved=8;
    assert(pending_reserve(s_path));pending_mark_latest(s_path);test_save_error=-1;save_progress();
    assert(s_unsaved==8&&s_save_failed&&pending_find(s_path)->dirty);
    book_progress_t restored={0};assert(pending_reserve("/sdcard/books/b.txt"));pending_mark_latest("/sdcard/books/b.txt");
    assert(pending_restore(s_path,s_file_size,&restored)&&restored.byte_off==100);
    assert(!pending_restore(s_path,s_file_size+1,&restored));pending_mark_latest(s_path);
    test_save_error=0;test_last_error=-1;save_progress();assert(s_unsaved==8&&pending_find(s_path)->progress_saved);
    int calls=test_save_calls;save_progress();assert(test_save_calls==calls&&s_unsaved==8);
    test_last_error=0;retry_progress();assert(!s_unsaved&&!s_save_failed);
    s_unsaved=7;retry_progress();assert(!s_unsaved);
    pending_progress_t* earlier=pending_find(s_path);earlier->dirty=true;earlier->progress_saved=false;
    assert(pending_reserve("/sdcard/books/b.txt"));pending_progress_t* newer=pending_find("/sdcard/books/b.txt");newer->dirty=true;
    pending_mark_latest("/sdcard/books/b.txt");pending_mark_latest(s_path);
    s_text=NULL;retry_progress();assert(!strcmp(test_last_path,s_path)&&!s_save_failed);
    strcpy(s_managed.path,s_path);s_view=MANAGE;s_delete_confirm=true;s_clear_confirm=true;
    EpdRect cancel=manage_rect(0,2);manage_action(&ctx,cancel.x+1,cancel.y+1);assert(!s_clear_confirm&&test_delete_calls==0);
    EpdRect del=manage_rect(2,3);manage_action(&ctx,del.x+1,del.y+1);assert(s_clear_confirm&&s_delete_confirm&&test_delete_calls==0);
    test_removed=false;test_delete_error=-1;manage_apply(&ctx);assert(s_view==MANAGE&&!s_file_removed&&s_manage_message[0]);
    test_removed=true;manage_apply(&ctx);assert(s_view==MANAGE&&s_file_removed&&test_notify_count==1&&!pending_find(s_managed.path));assert(s_store_revision==book_store_revision());
    test_forget_error=-1;manage_apply(&ctx);assert(s_view==MANAGE&&test_notify_count==1);
    test_forget_error=0;manage_apply(&ctx);assert(s_view==SHELF&&test_notify_count==1);
    strcpy(s_managed.name,"Short.txt");assert(manage_panel().height<500);
    char long_name[256];memset(long_name,'W',255);long_name[255]=0;test_wrapped[0]=0;
    assert(draw_wrapped_name(NULL,long_name,176)+104<870);assert(!strcmp(test_wrapped,long_name));
    for(int i=0;i<80;i++){memcpy(long_name+i*3,"书",3);}long_name[240]=0;test_wrapped[0]=0;
    assert(draw_wrapped_name(NULL,long_name,176)+104<870);assert(!strcmp(test_wrapped,long_name));
    scan_shelf(&ctx);s_view=BULK;toggle_selection(0);toggle_selection(14);assert(selected_count()==2);
    s_recent_sort=true;sort_shelf(&ctx);assert(selected_count()==2);select_page(1);assert(selected_count()==9);
    ctx.leaf=2;strcpy(s_query,"book");search_begin();strcpy(s_search_draft,"bad");search_finish(&ctx,false);
    assert(ctx.leaf==2&&!strcmp(s_query,"book")&&selected_count()==9);
    search_begin();assert(on_key(&ctx,UI_KEY_3)==APP_REDRAW_NONE&&ctx.leaf==2);assert(on_key(&ctx,UI_KEY_1)==APP_REDRAW_PAGE&&ctx.leaf==2&&s_view==BULK);
    s_batch_confirm=true;assert(on_key(&ctx,UI_KEY_3)==APP_REDRAW_NONE&&ctx.leaf==2);assert(on_key(&ctx,UI_KEY_1)==APP_REDRAW_PAGE&&!s_batch_confirm&&ctx.leaf==2);
    search_begin();strcpy(s_search_draft,"book001");search_finish(&ctx,true);assert(!selected_count()&&s_visible_count==1);
    search_begin();memset(s_search_draft,'x',64);s_search_draft[64]=0;search_action(&ctx,0);assert(strlen(s_search_draft)==64);search_action(&ctx,41);assert(strlen(s_search_draft)==63);search_action(&ctx,42);assert(!s_search_draft[0]);search_action(&ctx,43);
    strcpy(s_query,"");refresh_search_matches();sort_shelf(&ctx);clear_selection();s_view=BULK;toggle_selection(0);toggle_selection(1);
    s_batch_confirm=true;calls=test_delete_calls;EpdRect bc=ui_row_rect(0,2,620,UI_BTN_H);batch_action(&ctx,bc.x+1,bc.y+1);assert(!s_batch_confirm&&test_delete_calls==calls);
    s_batch_delete=true;test_removed=true;test_delete_error=-1;batch_apply(&ctx);assert(selected_count()==2);
    calls=test_delete_calls;test_forget_error=0;batch_apply(&ctx);assert(!selected_count()&&test_delete_calls==calls&&s_count==63);
    scan_shelf(&ctx);clear_selection();toggle_selection(0);toggle_selection(1);test_mixed=true;
    batch_apply(&ctx);assert(s_count==64&&selected_count()==1&&s_shelf[0].removed);
    calls=test_delete_calls;batch_apply(&ctx);assert(s_count==63&&!selected_count()&&test_delete_calls==calls);
    // 保存失败的 A 不应因无关 B 变动而丢失。/ An unrelated B change must retain A's failed save.
    strcpy(s_path,"/sdcard/books/a.txt");s_text="text";s_page=2;s_unsaved=8;
    assert(pending_reserve(s_path));pending_mark_latest(s_path);test_save_error=-1;save_progress();
    book_on_exit(&ctx);assert(book_progress_forget("/sdcard/books/b.txt")==ESP_OK);book_store_notify_changed();on_enter(&ctx);
    assert(pending_restore("/sdcard/books/a.txt",s_file_size,&restored)&&restored.byte_off==200);
    test_save_error=0;calls=test_save_calls;retry_progress();assert(test_save_calls==calls+1&&!s_save_failed);
    // 同路径替换即使 NVS 清理失败，也不能回写旧进度。/ Replacing A must not restore stale progress even when cleanup fails.
    strcpy(s_path,"/sdcard/books/a.txt");s_text="text";s_unsaved=8;assert(pending_reserve(s_path));
    pending_mark_latest(s_path);test_save_error=-1;save_progress();book_on_exit(&ctx);
    test_forget_error=-1;assert(book_progress_forget("/sdcard/books/a.txt")!=ESP_OK);book_store_notify_changed();
    on_enter(&ctx);assert(!pending_find("/sdcard/books/a.txt"));test_save_error=0;
    calls=test_save_calls;retry_progress();assert(test_save_calls==calls);test_forget_error=0;
    // 文件删除后清理失败，切页回来仍可重试。/ Cleanup after deletion remains retryable across page exits.
    scan_shelf(&ctx);s_managed=s_shelf[0];s_view=MANAGE;s_delete_confirm=true;s_file_removed=false;
    test_mixed=false;test_removed=true;test_delete_error=-1;manage_apply(&ctx);
    assert(s_file_removed);assert(unlink(s_managed.path)==0);
    book_on_exit(&ctx);on_enter(&ctx);scan_shelf(&ctx);
    bool found_retry=false;
    for(int i=0;i<s_count;i++)if(!strcmp(s_shelf[i].path,s_managed.path)){found_retry=s_shelf[i].removed;}
    assert(found_retry);
    s_view=MANAGE;calls=test_delete_calls;test_forget_error=0;manage_apply(&ctx);
    assert(s_view==SHELF&&test_delete_calls==calls);
    // 批量失败记录同样跨页存活，重试仅清元数据。/ Batch cleanup retries also survive exits without repeating unlink.
    scan_shelf(&ctx);clear_selection();toggle_selection(0);toggle_selection(1);s_batch_delete=true;
    batch_apply(&ctx);assert(selected_count()==2&&s_delete_retries);
    for(int i=0;i<s_count;i++)if(s_shelf[i].removed)assert(unlink(s_shelf[i].path)==0);
    book_on_exit(&ctx);on_enter(&ctx);scan_shelf(&ctx);
    int retry_count=0;
    for(int i=0;i<s_count;i++)if(s_shelf[i].removed){s_shelf[i].selected=true;++retry_count;}
    assert(retry_count==2);calls=test_delete_calls;batch_apply(&ctx);
    assert(!s_delete_retries&&test_delete_calls==calls&&!selected_count());
    free(s_shelf);
    while(s_pending)pending_discard(s_pending->path);
    for(int i=0;i<65;i++){char path[340];snprintf(path,sizeof(path),"%s/book%03d.txt",test_roots[0].path,i);unlink(path);}rmdir(test_roots[0].path);
    puts("book_ui_host_test: PASS");
}
'''
with tempfile.TemporaryDirectory() as tmp:
    c = Path(tmp) / "test.c"
    exe = Path(tmp) / "test"
    c.write_text(unit, encoding="utf-8")
    subprocess.run(["gcc", "-std=c11", "-Wall", "-Wextra", "-Werror", "-Wno-unused-variable", "-fsanitize=address,undefined", "-I" + str(source.parents[1] / "system"), str(c), "-o", str(exe)], check=True)
    subprocess.run([str(exe)], check=True)

# 真实章节定位函数链接真实布局；只替换章节解析/硬件/字体，以覆盖提交事务。
# Link the real chapter-position function with real layout; only parser/hardware/font are doubled.
seek_unit = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "book_layout.h"
#include "ttf_font.h"
#define ESP_OK 0
#define ESP_LOGW(...) ((void)0)
typedef struct {int leaf;} app_ctx_t;
static const char* TAG="test";
int book_layout_fail_after=-1;
static char *s_text;
static size_t s_text_len,s_block_count,s_chapter,s_page;
static blk_t* s_blocks;
static bool s_selection,s_selection_menu,s_selection_drag;
static int s_px=10,s_layout_px,test_parser_error,test_loads;
static char s_message[128],s_title[128],s_font_path[192];
static EpdRect s_layout_rect;
static EpdRect body_rect(void){return(EpdRect){0,0,40,30};}
static void loading_detail(app_ctx_t* c,const char* a,const char* b){(void)c;assert(a&&b);}
static const char* book_error_message(int e){(void)e;return "读取失败";}
static void lock_draw(void){}
static void unlock_draw(void){}
static void invalidate_prep(void){}
static void free_image(void){}
static void prepare_page_images(void){}
static size_t book_chapter_count(void){return 3;}
static uint32_t book_chapter_byte_offset(size_t c){uint32_t a[]={0,100,900};assert(c<3);return a[c];}
static uint32_t book_total_bytes(void){return 1000;}
static int book_chapter_title(size_t c,char* d,size_t n){snprintf(d,n,"chapter %zu",c);return ESP_OK;}
static const char* ttf_font_path(void){return "fixture";}
static int book_chapter_load_blocks(size_t c,html_text_t* out){
    assert(c<3);++test_loads;if(test_parser_error)return test_parser_error;
    out->len=1200;out->utf8=malloc(out->len+1);assert(out->utf8);
    for(size_t i=0;i<out->len;i+=3)memcpy(out->utf8+i,"甲",3);
    out->utf8[out->len]=0;return ESP_OK;
}
void html_blocks_free(blk_t* b,size_t n){(void)n;free(b);}
void html_text_free(html_text_t* t){free(t->utf8);html_blocks_free(t->blocks,t->count);*t=(html_text_t){0};}
void epd_draw_pixel(int x,int y,uint8_t c,uint8_t* fb){(void)x;(void)y;(void)c;(void)fb;}
int ttf_text_width_px(int px,const char* t){int n=0;for(;*t;++t)if(((unsigned char)*t&0xc0)!=0x80)++n;return n*px;}
int ttf_ascender_px(int px){return px;}
void ttf_draw_text_px(uint8_t* fb,int x,int y,int px,const char* t,enum EpdFontFlags a,uint8_t f,uint8_t b){(void)fb;(void)x;(void)y;(void)px;(void)t;(void)a;(void)f;(void)b;}
'''
seek_unit += function("copy_text") + "\n" + function("load_chapter_position")
seek_unit += r'''
int main(void){
    app_ctx_t ctx={0};s_text=malloc(601);assert(s_text);memset(s_text,'x',600);s_text[600]=0;s_text_len=600;
    assert(book_layout_begin_blocks(s_text,s_text_len,NULL,0,body_rect(),s_px));
    s_chapter=0;s_page=1;s_selection=s_selection_menu=true;
    char* original=s_text;size_t pages=book_layout_page_count(),scan=book_layout_page_start_offset(pages);
    test_parser_error=1;assert(!load_chapter_position(&ctx,1,0,false,true));test_parser_error=0;
    assert(s_text==original&&s_chapter==0&&s_page==1&&s_selection&&book_layout_page_count()==pages&&!book_layout_complete());
    for(int fail=0;fail<4;++fail){
        book_layout_fail_after=fail;assert(!load_chapter_position(&ctx,1,799,false,true));book_layout_fail_after=-1;
        assert(s_text==original&&s_chapter==0&&s_page==1&&s_selection&&s_selection_menu);
        assert(book_layout_page_count()==pages&&book_layout_page_start_offset(pages)==scan&&!book_layout_complete());
        assert(s_message[0]);
    }
    assert(load_chapter_position(&ctx,1,799,false,true)&&s_chapter==1&&!s_selection&&!s_selection_menu);
    // 799/800源字节对应1198解析字节，向下夹到完整UTF-8字节起点1197。
    // 799/800 source bytes map to parsed byte1198, floored to UTF-8 boundary1197.
    assert(s_page==book_layout_page_for_offset(1197)&&book_layout_page_start_offset(s_page)<=1197);
    assert(book_layout_complete());
    assert(load_chapter_position(&ctx,1,1,false,true)&&s_page==0);
    assert(load_chapter_position(&ctx,2,SIZE_MAX,false,true)&&book_layout_complete()&&s_page==book_layout_page_count()-1);
    assert(load_chapter_position(&ctx,0,300,false,false)&&s_page==book_layout_page_for_offset(300));
    assert(!book_layout_complete());
    assert(load_chapter_position(&ctx,0,0,true,false)&&book_layout_complete()&&s_page==book_layout_page_count()-1);
    assert(test_loads==10);book_layout_free();free(s_text);
    puts("book_ui_host_test: actual chapter-position + real incremental layout + UTF-8/source-byte mapping + parser/allocation failure rollback PASS");
}
'''
with tempfile.TemporaryDirectory() as tmp:
    c, exe = Path(tmp) / "seek.c", Path(tmp) / "seek"
    c.write_text(seek_unit, encoding="utf-8")
    root = source.parents[2]
    subprocess.run(["gcc", "-std=c11", "-D_DEFAULT_SOURCE", "-DBOOK_LAYOUT_TEST_ALLOC", "-Wall", "-Wextra", "-Werror", "-Wno-unused-variable", "-fsanitize=address,undefined", "-I" + str(root / "main/book"), "-I" + str(root / "tools/book_layout_stubs"), "-I" + str(root / "tools/book_source_host_stubs"), str(c), str(root / "main/book/book_layout.c"), "-o", str(exe)], check=True)
    subprocess.run([str(exe)], check=True)

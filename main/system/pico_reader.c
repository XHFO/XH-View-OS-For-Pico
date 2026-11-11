/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * 用户修订：多步配置保留回调新建的编辑器；地址与配对码使用大数字键及明确的下一步。
 * User revision: Multi-step callbacks preserve newly opened editors; addresses and pairing codes use large digits and explicit next actions.
 * 本机文件书库、阅读记录与手机式离线输入；图书正文交给真实图书引擎。
 * Local-file library, records and offline phone-style input; the book engine owns file reading.
 * 冻结修订：依连续输入延迟反馈，逻辑按键先处理、绘制后合并；候选命中以已绘帧为准，不因后台候选变化误选。
 * Frozen revision: Input-latency feedback prioritizes logical keys before merged painting; candidate hits use the painted frame rather than unseen replacements.
 * 用户修订：封面视图仅首页保留续读卡和六本书，第二页起隐藏续读卡并以九宫格分页；封面准备、保留和点击均使用相同页范围。
 * User revision: Cover view keeps continuation and six books on page one, then uses nine-book pages without continuation; preparation, retention and hits share page ranges.
 * 冻结：产品不嵌入示例原文或漫画；旧示例记录保留可读来源元数据。render纯绘制。只写独立ps_store键与用户导出目录；tick按需委托book_cover准备本地封面缓存，render只读RAM。
 * 依用户要求，离线扩展词库仅在进入输入设置或编辑器时从TF加载并校验，输入只读RAM，首次无法载入时回退内置，失卡后已加载内容仍可用；键盘几何按候选与布局缓存。依实机输入延迟反馈，文字输入仅重绘文本、组合候选与标签变化的空格键，静态字母区不逐键重画；编辑控件使用可保留的深描边，页面负责快速区域推屏。
 * 用户修订：文字输入仅提供拼音26键、拼音9键、英文和数字符号；取消手写输入与识别模型依赖，旧64点笔迹记录和草稿仍可读取、保存与导出，不修改用户TF上的旧模型。中文编辑会话一次选用TF完整字库，逐键不重开字体，离开恢复用户字体；缺字明确提示覆盖受限。真实记录绑定路径、源大小、章节与解析正文UTF-8范围；依扩容要求改为最多1024条、每笔记2047字节正文，PSRAM逐条按实际正文分配，独立对象先写、索引最后提交；只读兼容旧两代分块和383字节草稿。显式跨存储合并不写源库，不隐式切换系统存储根。用户修订：存档只写每块实际记录，兼容旧定长块；正式保存不以前置草稿写入为条件，失败保留编辑内容，空闲草稿避开拼音和触摸交互。真实图书收藏按完整路径独立存档，不改原文与进度；星标与已读进度同行，44像素触区。书架的封面、图文、紧凑、画廊、聚焦五种样式沿用同一书单和独立偏好，新增样式追加编号并共用绘制/触摸几何；紧凑样式不解码封面。刷新保留已提交的书单与封面，独立扫描及当前可见封面全部尝试后仅原子提交一次，不按超时提前展示半成品；首次空书架仅静态等待。失败封面使用占位，交互期间暂停扫描、解码和提交。
 * Frozen: No bundled sample prose or comics in production; preserve metadata for old records. Render only paints. Write only dedicated
 * ps_store keys and the user's export directory; ticks delegate local cover caches to book_cover and rendering only reads RAM. User revision: Text input offers pinyin QWERTY/T9, English and symbols only; remove handwriting input/model dependencies while preserving legacy 64-point note/draft read, save and export compatibility and leaving old TF models untouched.
 * Optional TF lexicons are validated on input-settings/editor entry, queried only in RAM with a built-in fallback if loading fails and continued RAM use after media loss; cache keyboard geometry across unchanged input events. Hardware latency feedback requires text input to repaint only text, composition/candidates and a changed space label; keep static letters untouched and use dark editor outlines for the page-owned fast regional presenter.
 * Chinese editing chooses a complete TF font once per session, never reopens it per key, and restores the user font on exit; missing coverage is explicit. Real records bind path, source size, chapter and parsed UTF-8 ranges with two-generation chunked storage. User revision: write only populated chunk records while accepting old fixed-size chunks; explicit saves do not require a preceding draft write, retain editing contents on failure, and defer idle drafts during composition/touch. Real-file favorites persist exact paths separately, never alter prose/progress, and use unboxed stars beside progress with 44-pixel targets. Cover, detail, compact, gallery and focus shelf styles share one book model and a separate preference; appended style IDs preserve old preferences and use shared painting/touch geometry; compact mode never decodes covers. Refresh retains committed books/covers until independent discovery and all current visible cover attempts finish, then atomically commits once without a timeout exposing partial results; an initial empty shelf waits statically. Failed covers use placeholders; interaction pauses scans, decoding and commit.
 */
#include "pico_system.h"
#include "pico_dual.h"
#include "pico_reader.h"
#include "pico_theme.h"
#include "pico_reader_data.h"
#include "pico_annotations.h"
#include "pico_ime_phrases.h"
#include "book_store.h"
#include "book_progress.h"
#include "book_cover.h"
#include "../apps/app_book.h"
#include "read_pico_sd.h"
#include "settings.h"
#include "ttf_font.h"
#include "esp_heap_caps.h"

#ifdef ESP_PLATFORM
#include "esp_attr.h"
#define PICO_UI_CACHE_ATTR EXT_RAM_BSS_ATTR
#else
#define PICO_UI_CACHE_ATTR
#endif
// 只缩小进入/设置代码，逐键候选、绘制和排版仍保留项目速度优化。
// Size-optimize entry/settings only; keep project speed optimization for keystrokes, drawing and layout.
#if defined(ESP_PLATFORM) && defined(__GNUC__)
#define PICO_READER_COLD __attribute__((optimize("Os")))
#else
#define PICO_READER_COLD
#endif
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>

#ifdef PICO_READER_HOST_FIXTURES
extern const uint8_t preview_text_start[] asm("_binary_preview_riverside_txt_start");

extern const uint8_t preview_text_end[] asm("_binary_preview_riverside_txt_end");

extern const uint8_t preview_comic_start[] asm("_binary_preview_comic_bin_start");

extern const uint8_t preview_comic_end[] asm("_binary_preview_comic_bin_end");
#endif

extern const app_desc_t app_font_pick, app_book;

enum {
    BOOK_COUNT=6, PAGE_MAX=4096, RECORD_MAX=PS_READER_RECORD_CAPACITY, LEGACY_RECORD_MAX=48, RECORD_CHUNK=32, REAL_BOOK=255, INK_MAX=64, CAND_MAX=48, CAND_TEXT=128 };

enum {
    READER_SIZE_MIN=19, READER_SIZE_MAX=33, READER_SIZE_DEFAULT=25,
    READER_WEIGHT_REGULAR=400, READER_WEIGHT_BOLD=600, READER_PREFS_VERSION=4 };

enum {
    REC_NOTE, REC_HIGHLIGHT, REC_BOOKMARK };

enum {
    INPUT_PINYIN=0, INPUT_T9=1, INPUT_ENGLISH=3, INPUT_SYMBOL=4, INPUT_MODE_COUNT=4 };
// 持久化值2是已移除的手写模式；其余编号保持兼容。 / Retired handwriting used stored value 2; retain all other persisted IDs.
static const uint8_t input_ids[INPUT_MODE_COUNT]={INPUT_PINYIN,INPUT_T9,INPUT_ENGLISH,INPUT_SYMBOL};
static int input_supported(int mode){return mode==INPUT_PINYIN||mode==INPUT_T9||mode==INPUT_ENGLISH||mode==INPUT_SYMBOL?mode:INPUT_PINYIN;}

enum {
    SHELF_VIEW_COVERS, SHELF_VIEW_DETAILS, SHELF_VIEW_COMPACT,
    SHELF_VIEW_GALLERY, SHELF_VIEW_FOCUS, SHELF_VIEW_COUNT };

typedef struct {
    const char *title,*author,*text;
    size_t bytes;
    bool comic;
}
reader_book_t;

typedef struct {
    uint32_t offset, recent;
    uint8_t favorite, done, started, comic_page;

}
reader_progress_t;

typedef struct {
    uint32_t version, serial;
    reader_progress_t books[BOOK_COUNT];
    uint8_t last, size, spacing, weight, input, list, sort;
    char font[TTF_FONT_PATH_MAX];

}
reader_prefs_t;

typedef struct { uint16_t x,y; } reader_ink_t;
_Static_assert(sizeof(reader_ink_t)==4,"Stored handwriting point layout changed");

typedef struct {
    uint32_t offset, end;
    uint8_t book, type, ink_count, reserved;
    char quote[244], text[384];
    reader_ink_t ink[INK_MAX];
} reader_legacy_record_t;

// dev39记录布局只读保留，运行记录的正文按实际长度分配。 / Preserve dev39 layout for reads; allocate live text at its actual length.
typedef struct {
    uint32_t offset,end;
    uint8_t book,type,ink_count,reserved;
    char quote[512],text[384];
    reader_ink_t ink[INK_MAX];
    char path[BOOK_STORE_PATH_MAX],title[96];
    uint64_t file_bytes;
    uint32_t chapter;
} reader_record_v2_t;
typedef struct {
    uint32_t offset,end;
    uint8_t book,type,ink_count,reserved;
    char quote[512];
    reader_ink_t ink[INK_MAX];
    char path[BOOK_STORE_PATH_MAX],title[96];
    uint64_t file_bytes;
    uint32_t chapter,object_id,object_bytes;
    char *text;
} reader_record_t;
typedef struct {
    uint32_t version,count;
    reader_record_t *item[RECORD_MAX];
} reader_records_t;
typedef struct { uint32_t version,count,sequence,slot; } reader_record_index_t;
typedef struct {
    uint32_t version,sequence,index,count;
    reader_record_v2_t item[RECORD_CHUNK];
} reader_record_chunk_t;
_Static_assert(sizeof(reader_record_chunk_t)<65536,"Legacy annotation chunk exceeds store limit");
static uint32_t records_sequence,records_slot,records_next_id;
static bool records_read_failed;



typedef struct {
    uint32_t version, offset, end;
    uint8_t book, ink_count, reserved[2];
    char text[PS_READER_NOTE_MAX_BYTES+1], quote[244];
    reader_ink_t ink[INK_MAX];

}
reader_draft_t;
typedef struct {
    uint32_t version;
    ps_book_anchor_t anchor;
    char text[PS_READER_NOTE_MAX_BYTES+1];
    reader_ink_t ink[INK_MAX];
    uint32_t ink_count;
} reader_real_draft_t;
// UI 草稿不参与 ISR 或 DMA；沿用外部缓存区，保留存档结构和主线程访问。
// UI drafts never serve ISR or DMA; use the existing external cache region without changing storage layout or main-thread access.
static PICO_UI_CACHE_ATTR reader_real_draft_t real_draft;
static bool editor_real_note;
static bool applying_remote_annotation;
static bool real_draft_clear_pending,draft_clear_pending;
static int64_t draft_clear_retry_ms;


typedef struct {
    uint32_t offset;
    char name[76];
}
reader_chapter_t;

static reader_book_t rb[BOOK_COUNT];

static reader_prefs_t rp;
static bool reader_checkpoint_clean;
static bool reader_store(const char *key,const void *data,size_t bytes){
    bool ok=ps_store(key,data,bytes);
    if(!ok)reader_checkpoint_clean=false;
    return ok;
}

static reader_records_t *rr;

static PICO_UI_CACHE_ATTR reader_draft_t draft;

static bool initialized, reader_entered, comic_valid, open_book, paginated, seek_pending, layout_failed;

static uint32_t *pages;

static size_t page_count, scan_offset, current_page, anchor;

static int layout_w,layout_h,layout_size,layout_spacing,layout_weight;

static char layout_font[TTF_FONT_PATH_MAX];

static int active_book, shelf_filter, shelf_scroll, record_tab, record_scroll, chapter_scroll;

static bool shelf_manage, shelf_style_picker, tools, chapter_panel, font_panel, selection_active, selection_drag;

static size_t selection_start,selection_end,selection_origin;

static reader_chapter_t chapters[64];

static int chapter_count, comic_zoom=100, comic_pan_x,comic_pan_y;

static int comic_press_x,comic_press_y,comic_old_x,comic_old_y;

static char shelf_search[96];

enum { SHELF_FILE_MAX=512, SHELF_SCAN_MAX=4096, SHELF_SCAN_BATCH=8 };
typedef struct {
    char path[BOOK_STORE_PATH_MAX],name[256];
    uint32_t size,recent;
    uint8_t pct;
    bool is_flash,epub,started;
} reader_file_t;
static void shelf_apply_dual_progress(reader_file_t *item);
static void shelf_open_choice_begin(const reader_file_t *book);
static bool shelf_open_choice_key(int key);
static void shelf_open_choice_render(uint8_t *fb);
// 扫描只在事件/tick中推进；固定上限缓存，绘制不打开目录或文件。 / Scan only in events/ticks; bounded cache, with no directory/file access while painting.
static reader_file_t *shelf_files;
static PICO_UI_CACHE_ATTR uint16_t shelf_file_ids[SHELF_FILE_MAX];
static int shelf_file_count,shelf_file_visible,shelf_scan_root,shelf_scan_roots,shelf_scan_seen;
static bool shelf_scan_pending=true,shelf_scanning,shelf_scan_limited,shelf_scan_error,shelf_have_media;
static DIR *shelf_dir;
static book_store_root_t shelf_roots[3];
enum { SHELF_FAVORITE_MAX=192 };
typedef struct { uint32_t version,count; char paths[SHELF_FAVORITE_MAX][BOOK_STORE_PATH_MAX]; } reader_favorites_t;
static reader_favorites_t *shelf_favorites;
static bool shelf_favorites_loaded,shelf_favorites_failed;
static uint32_t shelf_media_generation;
static unsigned shelf_revision;
static bool shelf_media_ready;
static char shelf_last_path[BOOK_STORE_PATH_MAX];
static bool shelf_open_choice;
static int shelf_open_choice_focus;
static char shelf_open_choice_path[BOOK_STORE_PATH_MAX];
static char shelf_open_choice_name[256];
static int64_t shelf_poll_ms,shelf_cover_next_ms;
static uint64_t shelf_cover_signature;
static bool shelf_cover_changed,shelf_cover_batch_done;
static unsigned shelf_cover_attempts;
enum { SHELF_COVER_VISIBLE=9 };
static PICO_UI_CACHE_ATTR char shelf_cover_attempted[SHELF_COVER_VISIBLE+1][BOOK_STORE_PATH_MAX];
static bool shelf_commit_pending;
static bool shelf_ready,shelf_stage_active;
static reader_file_t *shelf_stage_files;
static PICO_UI_CACHE_ATTR uint16_t shelf_stage_ids[SHELF_FILE_MAX];
static int shelf_stage_count,shelf_stage_visible;
static bool shelf_stage_limited,shelf_stage_error,shelf_stage_media_ready;
static uint32_t shelf_stage_generation;
static unsigned shelf_stage_revision;
static char shelf_stage_last_path[BOOK_STORE_PATH_MAX];
typedef struct {char path[BOOK_STORE_PATH_MAX];uint8_t *pixels;uint16_t width,height;} shelf_cover_copy_t;
static PICO_UI_CACHE_ATTR shelf_cover_copy_t shelf_display_covers[SHELF_COVER_VISIBLE];
static bool shelf_display_covers_held;

static ps_route_t last_route=PS_ROUTE_COUNT;

static int selected_record=-1, input_mode, candidate_page;

static size_t detail_scroll;
static char record_filter_path[BOOK_STORE_PATH_MAX];
static uint64_t record_filter_bytes;

static char editor_title[72],composition[40];
static uint32_t editor_generation;
static bool editor_numeric,editor_decimal;
static char editor_submit[16],editor_hint[96];
static void editor_frame_remember(void);
// 长令牌/证书使用临时PSRAM，不扩大笔记与草稿的存档格式。/ Long tokens/certificates use temporary PSRAM without enlarging note or draft records.
static PICO_UI_CACHE_ATTR char editor_text[3072];
// 仅记录已画出的编辑状态；区域更新不触发字库或存储访问。 / Track the painted editor state without font-file or storage access.
static PICO_UI_CACHE_ATTR struct {
    uint64_t layout;
    uint32_t candidates;
    int page;
    bool valid,composing;
    char text[sizeof(editor_text)];
} editor_frame;
void ps_reader_editor_invalidate(void){
    editor_frame.valid=false;
    // 编辑快照可能包含临时凭据，失效时也擦除明文副本。 / A painted snapshot can contain temporary credentials; invalidate also wipes its plaintext copy.
    volatile char *cached=editor_frame.text;for(size_t i=0;i<sizeof(editor_frame.text);i++)cached[i]=0;
}
// 候选与键盘仅在UI线程使用，不是DMA/ISR或文件驱动工作区。 / Candidates and keys belong to the UI thread, never DMA, ISR or file-driver workspaces.
static PICO_UI_CACHE_ATTR char candidates[CAND_MAX][CAND_TEXT];
static uint8_t candidate_consume[CAND_MAX];
static int candidate_score[CAND_MAX];

static size_t editor_limit;

static int candidate_count;
static uint32_t candidate_revision;
static int editor_pressed=-1,editor_press_x,editor_press_y,editor_press_w,editor_press_h;
static bool editor_press_cancelled,editor_press_visible_candidate;

static bool editor_note, editor_shift, editor_dirty;
static bool editor_password,editor_password_visible,editor_wifi_password;
static int editor_symbol_page;
static bool draft_auto_paused;
static char detached_font[TTF_FONT_PATH_MAX],editor_font[TTF_FONT_PATH_MAX];
static bool editor_font_prepared;
static uint32_t reader_choice_revision, font_retry_generation;
static char font_retry_path[TTF_FONT_PATH_MAX];
static uint8_t font_retry_attempts;
static bool font_retry_seen, font_retry_ready;
static int64_t font_retry_next_ms;

static int64_t editor_save_ms;

static uint8_t editor_ink_count;

static reader_ink_t editor_ink[INK_MAX];
static void editor_mode(int mode);
static uint8_t ink_snapshot(reader_ink_t out[INK_MAX]);

static void (*editor_done)(const char*);

static int ri_clamp(int n,int lo,int hi){
    return n<lo?lo:n>hi?hi:n;
}

static size_t u8step(const char *s,size_t left){
    if(!left)return 0;
    unsigned char c=(unsigned char)*s;
    size_t n=c<0x80?1:c>=0xc2&&c<0xe0?2:c>=0xe0&&c<0xf0?3:c>=0xf0&&c<=0xf4?4:0;
    if(!n||n>left)return 0;
    for(size_t i=1;i<n;i++)if(((unsigned char)s[i]&0xc0)!=0x80)return 0;
    if(n>=3){
        unsigned char second=(unsigned char)s[1];
        if((c==0xe0&&second<0xa0)||(c==0xed&&second>=0xa0)||(c==0xf0&&second<0x90)||(c==0xf4&&second>=0x90))return 0;
    }
    return n;

}

static bool u8valid(const char *s,size_t n){
    size_t at=0;
    while(at<n){
        size_t step=u8step(s+at,n-at);
        if(!step)return false;
        at+=step;
    }
    return true;

}

static size_t u8copy(char *out,size_t cap,const char *in,size_t n){
    if(!cap)return 0;
    size_t take=0;
    while(take<n){
        size_t step=u8step(in+take,n-take);
        if(!step||take+step>=cap)break;
        take+=step;
    }
    memcpy(out,in,take);
    out[take]=0;
    return take;

}

static void ri_copy(char *out,size_t cap,const char *in){
    u8copy(out,cap,in,strlen(in));
}

static void u8back(char *s){
    size_t n=strlen(s);
    if(n){
        do{
            --n;
        }
        while(n&&((unsigned char)s[n]&0xc0)==0x80);
        s[n]=0;
    }
}

#ifdef PICO_READER_HOST_FIXTURES
static uint16_t le16(const uint8_t *p){
    return (uint16_t)p[0]|((uint16_t)p[1]<<8);
}

static uint32_t le32(const uint8_t *p){
    return (uint32_t)p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24);
}
#endif

static bool check_comic(void){
#ifdef PICO_READER_HOST_FIXTURES
    size_t size=(size_t)(preview_comic_end-preview_comic_start),expected=600/8*848,prev=76;
    if(size<76||memcmp(preview_comic_start,"PVC1",4)||le16(preview_comic_start+4)!=600||
    le16(preview_comic_start+6)!=848||le16(preview_comic_start+8)!=8)return false;
    for(int i=0;i<8;i++){
        const uint8_t *entry=preview_comic_start+12+i*8;
        size_t offset=le32(entry),length=le32(entry+4),expanded=0;
        if(offset!=prev||offset>size||length>size-offset||length%2)return false;
        for(size_t j=0;j<length;j+=2){
            size_t n=preview_comic_start[offset+j];
            if(!n||n>expected-expanded)return false;
            expanded+=n;
        }
        if(expanded!=expected)return false;
        prev=offset+length;
    }
    return prev==size;
#else
    return false;
#endif

}

static bool prefs_save(void){
    reader_prefs_t saved=rp;
    // 断卡回退只影响当前会话，后续进度保存仍保留原选字体。 / Media-loss fallback changes this session, while later progress saves retain the preferred font.
    if(detached_font[0])ri_copy(saved.font,sizeof(saved.font),detached_font);
    if(reader_store("reader-prefs-v1",&saved,sizeof(saved)))return true;
    ps_toast("保存失败，请检查存储空间；当前设置仅本次有效");
    return false;

}

static PICO_READER_COLD bool records_save(void);
static void records_free(reader_records_t *library);

static void remember(void){
    if(!open_book)return;
    reader_progress_t *p=&rp.books[active_book];
    p->offset=(uint32_t)anchor;
    p->started=1;
    p->recent=++rp.serial;
    prefs_save();

}

static int body_height(void){
    return ps_view().h-100;
}

static int line_height(void){
    return (rp.size*(17+rp.spacing*2)+9)/10;
}

static int line_cost(const char *line,bool paragraph){ return line[0]?line_height()+(paragraph?line_height()/4:0):line_height()/4; }
static int native_size(void){
    return ps_rect(0,0,rp.size,0).width;
}

static bool close_punct(const char *s,size_t left){
    static const char *const marks[]={
        "，","。","；","：","！","？","、","）","》","】","”","’",",",".",";",":","!","?",NULL};
    for(int i=0;marks[i];i++){
        size_t n=strlen(marks[i]);
        if(n<=left&&!memcmp(s,marks[i],n))return true;
    }
    return false;

}

// 同一行推进器供分页、绘制与触摸锚点共用，确保不漏字、不切断UTF-8。 / One walker serves pagination, paint and touch anchors without dropping bytes or splitting UTF-8.
static size_t take_line(size_t off,char *line,size_t cap,bool *paragraph){
    const reader_book_t *b=&rb[active_book];
    size_t at=off,n=0,last=off;
    int width=0,max=ps_rect(0,0,ps_view().w-48,0).width;

 *paragraph=false;
    while(at<b->bytes){
        if(b->text[at]=='\r'||b->text[at]=='\n'){
            at++;
            if(at<b->bytes&&b->text[at-1]=='\r'&&b->text[at]=='\n')at++;

 *paragraph=true;
            break;
        }
        size_t step=u8step(b->text+at,b->bytes-at);
        if(!step)break;
        char glyph[5];
        memcpy(glyph,b->text+at,step);
        glyph[step]=0;
        int advance=ttf_text_width_px(native_size(),glyph);
        if(n&&(width+advance>max||n+step>=cap)){
            if(close_punct(b->text+at,b->bytes-at)&&last>off){
                at=last;
                n=at-off;
            }
            break;
        }
        if(n+step>=cap)break;
        memcpy(line+n,glyph,step);
        n+=step;
        width+=advance;
        last=at;
        at+=step;
    }
    line[n]=0;
    return at;

}

#ifdef PICO_READER_HOST_FIXTURES
static void chapters_build(void){
    chapter_count=0;
    chapter_scroll=0;
    const reader_book_t *b=&rb[active_book];
    for(size_t at=0;at<b->bytes&&chapter_count<64;){
        size_t end=at;
        while(end<b->bytes&&b->text[end]!='\n'&&b->text[end]!='\r')end++;
        if(end-at>=3&&end-at<100&&!memcmp(b->text+at,"第",3)){
            reader_chapter_t *c=&chapters[chapter_count++];
            c->offset=(uint32_t)at;
            u8copy(c->name,sizeof(c->name),b->text+at,end-at);
        }
        at=end;
        while(at<b->bytes&&(b->text[at]=='\n'||b->text[at]=='\r'))at++;
    }
    if(!chapter_count){
        chapters[0].offset=0;
        ri_copy(chapters[0].name,sizeof(chapters[0].name),"正文");
        chapter_count=1;
    }

}
#endif

static void seek_finish(void){
    if(!seek_pending||!page_count)return;
    if(!paginated&&anchor>=scan_offset)return;
    size_t lo=0,hi=page_count;
    while(lo+1<hi){
        size_t mid=(lo+hi)/2;
        if(pages[mid]<=anchor)lo=mid;
        else hi=mid;
    }
    current_page=lo;
    seek_pending=false;

}

static void layout_extend(int budget){
    if(paginated||layout_failed||rb[active_book].comic)return;
    char line[512];
    int made=0;
    while(scan_offset<rb[active_book].bytes&&made<budget){
        size_t start=scan_offset;
        int used=0;
        while(scan_offset<rb[active_book].bytes){
            bool para;
            size_t next=take_line(scan_offset,line,sizeof(line),&para);
            if(next<=scan_offset){
                layout_failed=true;
                return;
            }
            int cost=line_cost(line,para);
            if(used+cost>body_height())break;
            used+=cost;
            scan_offset=next;
        }
        if(scan_offset==start||page_count>=PAGE_MAX){
            layout_failed=true;
            return;
        }
        pages[page_count++]=(uint32_t)start;
        made++;
    }
    if(scan_offset==rb[active_book].bytes)paginated=true;
    seek_finish();

}

static void layout_begin(void){
    ps_view_t v=ps_view();
    layout_w=v.w;
    layout_h=v.h;
    layout_size=rp.size;
    layout_spacing=rp.spacing;
    layout_weight=rp.weight;
    ri_copy(layout_font,sizeof(layout_font),ttf_font_path());
    ttf_set_weight(rp.weight?READER_WEIGHT_BOLD:READER_WEIGHT_REGULAR);
    current_page=page_count=scan_offset=0;
    paginated=false;
    layout_failed=!pages;
    seek_pending=true;
    if(!rb[active_book].comic&&!layout_failed)layout_extend(2);

}

static bool layout_changed(void){
    ps_view_t v=ps_view();
    return v.w!=layout_w||v.h!=layout_h||rp.size!=layout_size||rp.spacing!=layout_spacing||rp.weight!=layout_weight||strcmp(layout_font,ttf_font_path());
}

static const char *system_font_preference(void){
    const char *path=app_settings_font_path();
    return path&&path[0]?path:TTF_FONT_BUILTIN;
}

// 中文编辑临时使用已验证覆盖的卡上字体，不改用户首选；只在进入会话时打开。 / Use a verified-coverage TF font for Chinese editing without changing preferences; open only on session entry.
static PICO_READER_COLD void editor_font_prepare(void){
    if(editor_font_prepared)return;
    editor_font_prepared=true;editor_font[0]=0;
    if(ps_storage_busy()||ps_usb_held()||ttf_font_sd_suspended())return;
    read_pico_sd_info_t info={0};
    if(read_pico_sd_get_info(&info)||!info.present||!info.mounted||info.exported)return;
    const char *const complete[]={TTF_FONT_DEFAULT_SD,"/sdcard/fonts/ChillDuanSansVF.ttf"};
    char previous[TTF_FONT_PATH_MAX];ri_copy(previous,sizeof(previous),ttf_font_path());
    for(size_t i=0;i<sizeof(complete)/sizeof(complete[0]);i++){
        if(!strcmp(ttf_font_path(),complete[i])||(!ttf_font_open(complete[i])&&!strcmp(ttf_font_path(),complete[i]))){
            ri_copy(editor_font,sizeof(editor_font),complete[i]);return;
        }
    }
    if(strcmp(previous,ttf_font_path())&&ttf_font_open(previous))ttf_font_open_builtin();
}

static bool reader_font_sync(bool reading){
    bool refreshed=false;
    const char *wanted=reading&&!rb[active_book].comic?(detached_font[0]?detached_font:rp.font):system_font_preference();
    if(ps_route()==PS_EDITOR&&editor_font[0])wanted=editor_font;
    if(!wanted[0])wanted=TTF_FONT_BUILTIN;
    ttf_set_weight(reading&&rp.weight?READER_WEIGHT_BOLD:READER_WEIGHT_REGULAR);
    // 文件所有权交给传输或维护时只保留首选，不能触碰卡上字体。
    // Preserve preference without card-font access while transfer or maintenance owns files.
    if(ps_storage_busy()||ps_usb_held()||ttf_font_sd_suspended()){
        font_retry_ready=false;font_retry_attempts=0;font_retry_next_ms=0;
        return false;
    }
    char before[TTF_FONT_PATH_MAX];ri_copy(before,sizeof(before),ttf_font_path());
    if(ttf_font_path_is_builtin(wanted)){
        if(strcmp(before,wanted))ttf_font_open(wanted);
        font_retry_path[0]=0;
    }else{
        read_pico_sd_info_t info={0};
        bool ready=read_pico_sd_get_info(&info)==ESP_OK&&info.present&&info.mounted&&!info.exported;
        bool reload=font_retry_seen&&ready&&(!font_retry_ready||font_retry_generation!=info.media_generation);
        if(!font_retry_seen||font_retry_generation!=info.media_generation||font_retry_ready!=ready||strcmp(font_retry_path,wanted)){
            ri_copy(font_retry_path,sizeof(font_retry_path),wanted);
            font_retry_attempts=0;font_retry_next_ms=0;
        }
        font_retry_seen=true;font_retry_generation=info.media_generation;font_retry_ready=ready;
        if(!ready)return false;
        if(strcmp(before,wanted)||reload){
            // 每个偏好/媒体代际至多尝试三次，避免缺失字体使每帧反复打开文件。
            // Retry each preference/media generation at most three times, never on every frame for a missing font.
            if(font_retry_attempts>=3||ps_now()<font_retry_next_ms)return false;
            font_retry_attempts++;font_retry_next_ms=ps_now()+1000;
            int error=ttf_font_open(wanted);
            if(error||strcmp(ttf_font_path(),wanted)){
                ttf_font_open_builtin();
                if(reading&&font_retry_attempts==1)ps_toast("所选字体当前不可用，已使用内置无衬线字体");
            }else refreshed=true;
        }
        if(reading&&detached_font[0]&&!strcmp(ttf_font_path(),wanted)){
            ri_copy(rp.font,sizeof(rp.font),detached_font);detached_font[0]=0;
        }
    }
    ttf_set_weight(reading&&rp.weight?READER_WEIGHT_BOLD:READER_WEIGHT_REGULAR);
    if(refreshed)layout_font[0]=0;
    return refreshed||strcmp(before,ttf_font_path())!=0;
}

static const char *reader_font_name(void){
    if(!strcmp(rp.font,TTF_FONT_BUILTIN))return "无衬线";
    if(!strcmp(rp.font,TTF_FONT_BUILTIN_SERIF))return "宋体";
    if(!strcmp(rp.font,TTF_FONT_BUILTIN_KAI))return "楷体";
    const char *name=strrchr(rp.font,'/');
    return name?name+1:rp.font;
}

static void page_turn(int delta){
    if(!open_book)return;
    if(rb[active_book].comic){
        int next=ri_clamp(rp.books[active_book].comic_page+delta,0,7);
        if(next==rp.books[active_book].comic_page)return;
        rp.books[active_book].comic_page=next;
        comic_pan_x=comic_pan_y=0;
        anchor=(size_t)next;
        remember();
        return;
    }
    if(seek_pending||layout_failed)return;
    int next=ri_clamp((int)current_page+delta,0,(int)page_count-1);
    if(next==(int)current_page){
        if(delta>0&&!paginated)ps_toast("正在继续分页，请稍候");
        return;
    }
    current_page=(size_t)next;
    anchor=pages[current_page];
    selection_active=false;
    remember();

}

void ps_reader_open(int book){
#ifdef PICO_READER_HOST_FIXTURES
    if(book>=0)ps_global_prefer_real_book(false);
    if(book==-1)book=rp.last;
    if(book<0||book>=BOOK_COUNT)return;
    active_book=book;
    rp.last=(uint8_t)book;
    open_book=true;
    tools=chapter_panel=font_panel=selection_active=false;
    anchor=rp.books[book].offset;
    if(!rb[book].comic&&anchor>=rb[book].bytes)anchor=0;
    if(rb[book].comic)anchor=rp.books[book].comic_page;
    comic_zoom=100;
    comic_pan_x=comic_pan_y=0;
    chapters_build();
    reader_font_sync(true);
    layout_begin();
    remember();
    ps_navigate(PS_READ);
#else
    (void)book;
    ps_navigate(PS_SHELF);
    ps_toast("请从本机书库打开图书");
#endif

}

bool ps_reader_is_open(void){
    return open_book;
}

bool ps_reader_prefers_grayscale(void){
    return ps_route()==PS_SHELF||(ps_route()==PS_READ&&rb[active_book].comic);
}

static void save_highlight(void);

static void new_note(void);

static void add_bookmark(void);

void ps_reader_tools(void){
    if(!open_book){
        ps_navigate(PS_SHELF);
        return;
    }
    if(ps_route()!=PS_READ)ps_navigate(PS_READ);
    tools=!tools;
    chapter_panel=font_panel=false;
    ps_dirty();
}

void ps_reader_toc(void){
    if(!open_book){ps_toast("请先打开一本书");return;}
    if(rb[active_book].comic){ps_toast("漫画使用左右实体键逐页切换");return;}
    if(!chapter_count){ps_toast("当前图书暂无目录");return;}
    if(ps_route()!=PS_READ)ps_navigate(PS_READ);
    tools=chapter_panel=true;
    font_panel=false;
    chapter_scroll=0;
    for(int i=0;i<chapter_count;i++)if(chapters[i].offset<=anchor)chapter_scroll=i;
    ps_dirty();
}

void ps_reader_action(int action){
    if(!open_book&&action!=4){
        ps_toast("请先打开一本书");
        return;
    }
    switch(action){
        case 0:ps_reader_tools();
        break;
        case 1:save_highlight();
        break;
        case 2:new_note();
        break;
        case 3:add_bookmark();
        break;
        case 4:ps_navigate(PS_READING_SETTINGS);
        break;
        default:break;
    }

}

/* ---- 记录与草稿 / Records and drafts ---- */
static bool valid_record(const reader_record_t *n){
    if(!n)return false;
    if((n->book>=BOOK_COUNT&&n->book!=REAL_BOOK)||n->type>REC_BOOKMARK||n->ink_count>INK_MAX||
    !memchr(n->quote,0,sizeof(n->quote))||(n->text&&strnlen(n->text,PS_READER_NOTE_MAX_BYTES+1)>PS_READER_NOTE_MAX_BYTES))return false;
    if(!u8valid(n->text?n->text:"",n->text?strlen(n->text):0)||!u8valid(n->quote,strlen(n->quote)))return false;
    for(int i=0;i<n->ink_count;i++)if((n->ink[i].x&0x7fff)>1023||n->ink[i].y>1023)return false;
    if(n->book==REAL_BOOK){
        if(!memchr(n->path,0,sizeof(n->path))||!memchr(n->title,0,sizeof(n->title))||!n->path[0]||!n->title[0]||
           !u8valid(n->path,strlen(n->path))||!u8valid(n->title,strlen(n->title))||!n->file_bytes||
           n->chapter>=32768||n->offset>n->end||n->end>2u*1024u*1024u)return false;
        const char *name=NULL;
        if(!strncmp(n->path,"/sdcard/books/",14))name=n->path+14;
        else if(!strncmp(n->path,"/sdcard/",8))name=n->path+8;
        else if(!strncmp(n->path,"/flash/books/",13))name=n->path+13;
        const char *extension=name?strrchr(name,'.'):NULL;
        return name&&*name&&!strchr(name,'/')&&strcmp(name,".")&&strcmp(name,"..")&&extension&&
               (!strcasecmp(extension,".txt")||!strcasecmp(extension,".epub"));
    }
    const reader_book_t *b=&rb[n->book];
    if(b->comic)return n->offset<8&&n->end==n->offset;
#ifdef PICO_READER_HOST_FIXTURES
    return n->offset<=b->bytes&&n->end<=b->bytes&&n->end>=n->offset&&
    (n->offset==b->bytes||((unsigned char)b->text[n->offset]&0xc0)!=0x80)&&
    (n->end==b->bytes||((unsigned char)b->text[n->end]&0xc0)!=0x80)&&u8valid(n->text?n->text:"",n->text?strlen(n->text):0)&&u8valid(n->quote,strlen(n->quote));
#else
    // 原文已移除，按保留的旧长度校验；用户保存的引用和笔记仍独立可读。/ Validate retained lengths without removed prose; saved quotations and notes remain readable.
    return n->offset<=n->end&&n->end<=b->bytes;
#endif

}

static const char *record_title(const reader_record_t *n){return n->book==REAL_BOOK?n->title:rb[n->book].title;}
static bool record_matches(const reader_record_t *n){return !record_filter_path[0]||(n->book==REAL_BOOK&&n->file_bytes==record_filter_bytes&&!strcmp(n->path,record_filter_path));}


static bool same_note(const reader_record_t *a,const reader_record_t *b){
    return a->type==REC_NOTE&&b->type==REC_NOTE&&a->book==b->book&&a->offset==b->offset&&a->end==b->end&&
        a->ink_count==b->ink_count&&!strcmp(a->text?a->text:"",b->text?b->text:"")&&!strcmp(a->quote,b->quote)&&
        !memcmp(a->ink,b->ink,a->ink_count*sizeof(a->ink[0]))&&
        (a->book!=REAL_BOOK||(a->file_bytes==b->file_bytes&&a->chapter==b->chapter&&!strcmp(a->path,b->path)));
}

#include "pico_reader_records.inc"

static bool add_record(reader_record_t *n){
    if(!rr){
        ps_toast("记录内存不可用，请重启后重试");
        return false;
    }
    if(n->type==REC_NOTE&&valid_record(n))for(uint32_t i=0;i<rr->count;i++)if(same_note(n,rr->item[i]))return true;
    if(rr->count>=RECORD_MAX){
        ps_toast("本机记录已满1024条，请先导出并移除旧记录");
        return false;
    }
    if(!valid_record(n))return false;
    reader_record_t *saved=record_clone(n);
    if(!saved){ps_toast("记录内存不足，内容仍保留在编辑器");return false;}
    rr->item[rr->count++]=saved;
    if(!records_save()){
        heap_caps_free(rr->item[--rr->count]);
        return false;
    }
    selection_active=false;
    tools=false;
    ps_toast(n->type==REC_HIGHLIGHT?"划线已保存":n->type==REC_BOOKMARK?"书签已保存":"笔记已保存");
    return true;

}

static reader_record_t record_base(int type){
    reader_record_t n={
        0};
    n.book=(uint8_t)active_book;
    n.type=(uint8_t)type;
    n.offset=(uint32_t)(selection_active?selection_start:anchor);
    n.end=(uint32_t)(selection_active?selection_end:anchor);
    if(selection_active&&!rb[active_book].comic)u8copy(n.quote,sizeof(n.quote),rb[active_book].text+selection_start,selection_end-selection_start);
    return n;

}

static void save_highlight(void){
    if(!selection_active||rb[active_book].comic){
        ps_toast("长按正文并拖动选择文字，再点划线");
        return;
    }
    reader_record_t n=record_base(REC_HIGHLIGHT);
    add_record(&n);

}

static void add_bookmark(void){
    if(rr)for(uint32_t i=0;i<rr->count;i++)if(rr->item[i]->book==active_book&&rr->item[i]->type==REC_BOOKMARK&&rr->item[i]->offset==anchor){
        ps_toast("当前位置已有书签");
        return;
    }
    reader_record_t n=record_base(REC_BOOKMARK);
    add_record(&n);

}

static void draft_snapshot(void){
    if(editor_real_note){
        real_draft.version=1;ri_copy(real_draft.text,sizeof(real_draft.text),editor_text);
        real_draft.ink_count=ink_snapshot(real_draft.ink);return;
    }
    draft.version=1;
    draft.book=(uint8_t)active_book;
    ri_copy(draft.text,sizeof(draft.text),editor_text);
    draft.ink_count=ink_snapshot(draft.ink);
}

static PICO_READER_COLD bool draft_save(void){
    if(!editor_note)return true;
    if(!editor_dirty)return true;
    draft_snapshot();
    if(editor_real_note?reader_store("reader-real-d2",&real_draft,sizeof(real_draft)):reader_store("reader-draft-v2",&draft,sizeof(draft))){
        editor_dirty=false;
        return true;
    }
    return false;

}

static void note_done(const char *text){
    if(editor_real_note){
        reader_record_t n={0};const ps_book_anchor_t *a=&real_draft.anchor;
        n.book=REAL_BOOK;n.type=REC_NOTE;n.offset=a->start;n.end=a->end;n.chapter=a->chapter;n.file_bytes=a->file_bytes;
        ri_copy(n.path,sizeof(n.path),a->path);ri_copy(n.title,sizeof(n.title),a->title);ri_copy(n.quote,sizeof(n.quote),a->quote);
        n.text=(char *)text;n.ink_count=editor_ink_count;memcpy(n.ink,editor_ink,sizeof(n.ink));
        if(!n.text[0]&&!n.ink_count){ps_toast("请写下笔记文字");return;}
        (void)draft_save();
        if(add_record(&n)){
            ps_dual_sync_annotation(PS_READER_REC_NOTE, n.chapter, n.offset, n.end, n.text);
            memset(&real_draft,0,sizeof(real_draft));real_draft.version=1;
            real_draft_clear_pending=!reader_store("reader-real-d2",&real_draft,sizeof(real_draft));
            if(real_draft_clear_pending)ps_toast("笔记已保存，草稿清理稍后重试");
            editor_note=editor_real_note=editor_dirty=false;ps_back();
        }
        return;
    }
    reader_record_t n={
        0};
    n.book=draft.book;
    n.type=REC_NOTE;
    n.offset=draft.offset;
    n.end=draft.end;
    ri_copy(n.quote,sizeof(n.quote),draft.quote);
    n.text=(char *)text;
    n.ink_count=editor_ink_count;
    memcpy(n.ink,editor_ink,sizeof(n.ink));
    if(!n.text[0]&&!n.ink_count){
        ps_toast("请写下笔记文字");
        return;
    }
    (void)draft_save();
    if(add_record(&n)){
        memset(&draft,0,sizeof(draft));
        draft.version=1;
        draft_clear_pending=!reader_store("reader-draft-v2",&draft,sizeof(draft));
        if(draft_clear_pending)ps_toast("笔记已保存，草稿清理稍后重试");
        editor_note=false;
        editor_dirty=false;
        ps_back();
    }

}

static void new_note(void){
    reader_record_t n=record_base(REC_NOTE);
    bool restore=draft.version==1&&draft.book==active_book&&(draft.text[0]||draft.ink_count);
    if(!restore){
        reader_checkpoint_clean=false;
        memset(&draft,0,sizeof(draft));
        draft.version=1;
        draft.book=(uint8_t)active_book;
        draft.offset=n.offset;
        draft.end=n.end;
        ri_copy(draft.quote,sizeof(draft.quote),n.quote);
    }
    draft_clear_pending=false;
    ps_edit(restore?"继续笔记草稿":"写笔记",draft.text,sizeof(draft.text)-1,note_done);
    editor_note=true;
    editor_ink_count=draft.ink_count;
    memcpy(editor_ink,draft.ink,sizeof(editor_ink));
    editor_mode(rp.input);

}

static void record_delete(void){
    if(!rr||selected_record<0||selected_record>=(int)rr->count)return;
    reader_record_t *old=rr->item[selected_record];
    memmove(&rr->item[selected_record],&rr->item[selected_record+1],(rr->count-selected_record-1)*sizeof(old));
    rr->count--;
    if(!records_save()){
        memmove(&rr->item[selected_record+1],&rr->item[selected_record],(rr->count-selected_record)*sizeof(old));
        rr->item[selected_record]=old;
        rr->count++;
    }
    else {heap_caps_free(old);selected_record=-1;}

}

static void record_jump(int index){
    if(!rr||index<0||index>=(int)rr->count)return;
    reader_record_t *n=rr->item[index];
    if(n->book==REAL_BOOK){
        if(app_book_open_annotation(n->path,n->file_bytes,n->chapter,n->offset,n->end))ps_legacy(&app_book);
        else ps_toast("原图书当前不可用，请检查存储卡");
        return;
    }
#ifdef PICO_READER_HOST_FIXTURES
    ps_reader_open(n->book);
    anchor=n->offset;
    if(rb[n->book].comic)rp.books[n->book].comic_page=(uint8_t)anchor;
    else{
        seek_pending=true;
        seek_finish();
        selection_start=n->offset;
        selection_end=n->end;
        selection_active=n->end>n->offset;
    }
    remember();
#else
    ps_toast("内置原文已移除，已保存的记录仍可查看与导出");
#endif

}

static void export_records(void){
    if(!rr||!rr->count){
        ps_toast("还没有可以导出的阅读记录");
        return;
    }
    book_store_root_t roots[2];
    int count=0;
    if(book_store_roots(roots,&count)!=0||!count){
        ps_toast("没有可写入的书籍存储目录");
        return;
    }
    char dir[96],path[128],temp[132];
    snprintf(dir,sizeof(dir),"%s/.pico-reader",roots[0].path);
    if(mkdir(dir,0700)&&errno!=EEXIST){
        ps_toast("导出目录不可写");
        return;
    }
    snprintf(path,sizeof(path),"%s/reading-records.txt",dir);
    snprintf(temp,sizeof(temp),"%s.tmp",path);
    FILE *f=fopen(temp,"wb");
    if(!f){
        ps_toast("无法创建导出文件");
        return;
    }
    bool ok=true;
    for(uint32_t i=0;i<rr->count&&ok;i++){
        reader_record_t *n=rr->item[i];
        if(!record_matches(n))continue;
        const char *type=n->type==REC_NOTE?"笔记":n->type==REC_HIGHLIGHT?"划线":"书签";
        if(fprintf(f,"%s | %s | UTF-8 offset %lu\n> %s\n%s\n",record_title(n),type,(unsigned long)n->offset,n->quote,n->text)<0)ok=false;
        if(n->book==REAL_BOOK&&ok&&fprintf(f,"来源: %s | 第 %lu 节 | 文件字节数 %llu\n",n->path,(unsigned long)n->chapter+1,(unsigned long long)n->file_bytes)<0)ok=false;
        if(n->ink_count&&ok){
            if(fprintf(f,"[手写笔迹 %u 点，未识别；原笔迹保留在本机记录]\n",n->ink_count)<0)ok=false;
        }
        if(fputc('\n',f)==EOF)ok=false;
    }
    if(fflush(f)||ferror(f)||fsync(fileno(f)))ok=false;
    if(fclose(f))ok=false;
    if(ok&&rename(temp,path)==0)ps_toast("已导出至书根/.pico-reader/reading-records.txt");
    else{
        unlink(temp);
        ps_toast("导出失败，原导出文件保留");
    }

}

int ps_reader_excerpt_count(void){
    int n=0;
    if(rr)for(uint32_t i=0;i<rr->count;i++)if(rr->item[i]->type==REC_HIGHLIGHT&&rr->item[i]->quote[0])n++;
    return n;
}

bool ps_reader_excerpt(int index,char *quote,size_t bytes,char *source,size_t source_bytes){
    if(index<0||!rr||!quote||!source||!bytes||!source_bytes)return false;
    for(uint32_t i=0;i<rr->count;i++)if(rr->item[i]->type==REC_HIGHLIGHT&&rr->item[i]->quote[0]){
        if(index--==0){
            ri_copy(quote,bytes,rr->item[i]->quote);
            reader_record_t *n=rr->item[i];
            if(n->book==REAL_BOOK)snprintf(source,source_bytes,"《%s》 · 第 %lu 节",n->title,(unsigned long)n->chapter+1);
            else snprintf(source,source_bytes,"《%s》 · %s",rb[n->book].title,rb[n->book].author);
            return true;
        }
    }
    return false;

}

static bool real_record(const ps_book_anchor_t *a,int type,reader_record_t *n){
    if(!a||!memchr(a->path,0,sizeof(a->path))||!memchr(a->title,0,sizeof(a->title))||!memchr(a->quote,0,sizeof(a->quote)))return false;
    memset(n,0,sizeof(*n));n->book=REAL_BOOK;n->type=(uint8_t)type;n->offset=a->start;n->end=a->end;n->chapter=a->chapter;n->file_bytes=a->file_bytes;
    ri_copy(n->path,sizeof(n->path),a->path);ri_copy(n->title,sizeof(n->title),a->title);ri_copy(n->quote,sizeof(n->quote),a->quote);
    return valid_record(n);
}
static bool real_save(const ps_book_anchor_t *a,int type){
    reader_record_t n;if(!real_record(a,type,&n)){ps_toast("无法保存，此处的原文位置无效");return false;}
    if(type==REC_HIGHLIGHT&&(n.end<=n.offset||!n.quote[0])){ps_toast("请先长按正文选择文字");return false;}
    if(rr)for(uint32_t i=0;i<rr->count;i++){
        reader_record_t *r=rr->item[i];
        if(r->book==REAL_BOOK&&r->type==type&&r->file_bytes==n.file_bytes&&r->chapter==n.chapter&&r->offset==n.offset&&r->end==n.end&&!strcmp(r->path,n.path)){
            ps_toast(type==REC_HIGHLIGHT?"此段已有划线":"当前位置已有书签");return true;
        }
    }
    bool ok = add_record(&n);
    if (ok && type == REC_HIGHLIGHT && !applying_remote_annotation)
        ps_dual_sync_annotation(PS_READER_REC_HIGHLIGHT, n.chapter, n.offset, n.end, n.quote);
    return ok;
}
bool ps_reader_real_highlight(const ps_book_anchor_t *a){return real_save(a,REC_HIGHLIGHT);}
bool ps_reader_apply_note(const ps_book_anchor_t *a, const char *text){
    reader_record_t n;
    if (!real_record(a, REC_NOTE, &n) || !text || !text[0]) return false;
    ri_copy(n.text, sizeof(n.text), text);
    if (!add_record(&n)) return false;
    return true;
}
bool ps_reader_apply_remote_highlight(const ps_book_anchor_t *a){
    applying_remote_annotation = true;
    bool ok = ps_reader_real_highlight(a);
    applying_remote_annotation = false;
    return ok;
}
bool ps_reader_real_bookmark(const ps_book_anchor_t *a){return real_save(a,REC_BOOKMARK);}
void ps_reader_real_note(const ps_book_anchor_t *a){
    reader_record_t n;if(!real_record(a,REC_NOTE,&n)){ps_toast("无法批注，此处的原文位置无效");return;}
    bool restore=real_draft.version==1&&real_draft.anchor.file_bytes==a->file_bytes&&!strcmp(real_draft.anchor.path,a->path)&&(real_draft.text[0]||real_draft.ink_count);
    if(!restore){reader_checkpoint_clean=false;memset(&real_draft,0,sizeof(real_draft));real_draft.version=1;real_draft.anchor=*a;}
    real_draft_clear_pending=false;
    ps_global_book_return(true);
    ps_edit(restore?"继续批注草稿":"写批注",real_draft.text,sizeof(real_draft.text)-1,note_done);
    editor_note=editor_real_note=true;
    editor_ink_count=(uint8_t)real_draft.ink_count;memcpy(editor_ink,real_draft.ink,sizeof(editor_ink));
    editor_mode(rp.input);
}
void ps_reader_records_tab(int tab){
    record_filter_path[0]=0;record_filter_bytes=0;
    ps_navigate(PS_RECORDS);record_tab=ri_clamp(tab,0,2);record_scroll=0;selected_record=-1;
}
void ps_reader_real_records_tab(const char *path,uint64_t file_bytes,int tab){
    ps_reader_real_records(path,file_bytes);
    record_tab=ri_clamp(tab,0,2);record_scroll=0;selected_record=-1;
}
void ps_reader_real_records(const char *path,uint64_t file_bytes){
    ri_copy(record_filter_path,sizeof(record_filter_path),path?path:"");record_filter_bytes=file_bytes;
    ps_global_book_return(true);ps_navigate(PS_RECORDS);
}
bool ps_reader_real_open_at(const char *path,uint64_t file_bytes,uint32_t chapter,uint32_t offset){
    if(!path||!rr)return false;
    for(uint32_t i=rr->count;i>0;i--){
        const reader_record_t *n=rr->item[i-1];
        if(n->book==REAL_BOOK&&(n->type==REC_HIGHLIGHT||n->type==REC_NOTE)&&n->chapter==chapter&&
           n->file_bytes==file_bytes&&n->offset<=offset&&offset<n->end&&!strcmp(n->path,path)){
            ps_reader_real_records(path,file_bytes);record_tab=n->type;selected_record=(int)i-1;detail_scroll=0;record_scroll=0;
            return true;
        }
    }
    return false;
}
size_t ps_reader_real_ranges(const char *path,uint64_t file_bytes,uint32_t chapter,ps_book_range_t *ranges,size_t capacity){
    size_t count=0;if(!path||!ranges||!capacity||!rr)return 0;
    for(uint32_t i=0;i<rr->count&&count<capacity;i++){
        const reader_record_t *n=rr->item[i];
        if(n->book==REAL_BOOK&&(n->type==REC_HIGHLIGHT||n->type==REC_NOTE)&&n->end>n->offset&&n->chapter==chapter&&n->file_bytes==file_bytes&&!strcmp(n->path,path))ranges[count++]=(ps_book_range_t){n->offset,n->end};
    }
    return count;
}

/* ---- 离线输入 / Offline input ---- */
static void editor_changed(void){
    editor_dirty=true;
    draft_auto_paused=false;
    editor_save_ms=ps_now()+6000;
}

static bool editor_append(const char *s){
    size_t n=strlen(editor_text),add=strlen(s);
    if(n+add>editor_limit||n+add>=sizeof(editor_text)){
        ps_toast("已达到本次输入长度上限");
        return false;
    }
    memcpy(editor_text+n,s,add+1);
    editor_changed();
    return true;

}

static void candidate_add(const char *s,int consumed,int score){
    if(!s[0]||strlen(s)>=sizeof(candidates[0])||consumed<1)return;
    for(int i=0;i<candidate_count;i++)if(!strcmp(candidates[i],s)&&candidate_consume[i]==consumed){
        if(candidate_score[i]<=score)return;
        for(int j=i;j+1<candidate_count;j++){
            memcpy(candidates[j],candidates[j+1],sizeof(candidates[j]));
            candidate_consume[j]=candidate_consume[j+1];candidate_score[j]=candidate_score[j+1];
        }
        candidate_count--;break;
    }
    int at=0;
    while(at<candidate_count&&candidate_score[at]<=score)at++;
    if(at==CAND_MAX)return;
    int end=candidate_count<CAND_MAX?candidate_count++:CAND_MAX-1;
    for(int i=end;i>at;i--){
        memcpy(candidates[i],candidates[i-1],sizeof(candidates[i]));
        candidate_consume[i]=candidate_consume[i-1];
        candidate_score[i]=candidate_score[i-1];
    }
    ri_copy(candidates[at],sizeof(candidates[at]),s);
    candidate_consume[at]=(uint8_t)consumed;
    candidate_score[at]=score;
}

static char t9digit(char c){
    return c>='a'&&c<='c'?'2':c<='f'?'3':c<='i'?'4':c<='l'?'5':c<='o'?'6':c<='s'?'7':c<='v'?'8':'9';
}

// 固定工作区限制最长拼音与内存；词组分词优先于逐字拼接。 / Fixed workspace bounds input and memory; phrase segmentation precedes character fallback.
static PICO_UI_CACHE_ATTR struct { int cost; char text[CAND_TEXT]; } ime_path[40];
static char ime_input[40];
static uint8_t ime_raw_end[40],ime_boundary[40];

static bool ime_equal(char letter,char typed){return (input_mode==INPUT_T9?t9digit(letter):letter)==typed;}

static void ime_token(const reader_pinyin_t *token,bool phrase,int order,int start,int input_length){
    int plen=(int)strlen(token->pinyin),same=0;
    while(same<plen&&start+same<input_length&&(!same||!ime_boundary[start+same])&&ime_equal(token->pinyin[same],ime_input[start+same]))same++;
    bool exact=same==plen;
    bool completion=same>0&&start+same==input_length&&plen-same<=4;
    if(!exact&&!completion)return;
    const char *chars=token->chars;
    char glyph[5];
    if(!phrase){
        size_t size=u8step(chars,strlen(chars));
        if(!size)return;
        memcpy(glyph,chars,size);glyph[size]=0;chars=glyph;
    }
    int end=start+same;
    if(ime_path[start].cost<1000000){
        int cost=ime_path[start].cost+(phrase?8:26)+(exact?0:18+(plen-same)*8);
        size_t have=strlen(ime_path[start].text),add=strlen(chars);
        if(cost<ime_path[end].cost&&have+add<sizeof(ime_path[end].text)){
            memcpy(ime_path[end].text,ime_path[start].text,have);
            memcpy(ime_path[end].text+have,chars,add+1);
            ime_path[end].cost=cost;
        }
    }
    if(start)return;
    int consumed=ime_raw_end[end-1];
    int score=exact?(end==input_length?(phrase?0:25):240-end*2):90+(plen-same)*4;
    if(phrase)candidate_add(chars,consumed,score+order/100);
    else{
        const char *s=token->chars;int rank=0;
        while(*s){
            size_t size=u8step(s,strlen(s));if(!size)break;
            memcpy(glyph,s,size);glyph[size]=0;
            candidate_add(glyph,consumed,score+rank++);
            s+=size;
        }
    }
}


#include "pico_ime_dictionary.inc"

static void candidates_update(void){
    candidate_revision++;
    candidate_count=0;candidate_page=0;
    int n=0;
    memset(ime_boundary,0,sizeof(ime_boundary));
    for(int i=0;composition[i];i++){
        if(composition[i]=='\''){ime_boundary[n]=1;continue;}
        ime_input[n]=composition[i];ime_raw_end[n++]=(uint8_t)(i+1);
    }
    ime_input[n]=0;
    if(!n)return;
    // 尾部分词符随候选一起消耗。 / Consume a trailing separator with its candidate.
    ime_raw_end[n-1]=(uint8_t)strlen(composition);
    for(int i=0;i<=n;i++){ime_path[i].cost=1000000;ime_path[i].text[0]=0;}
    ime_path[0].cost=0;
    // 每个位置只向后推进，词库扫描次数与39字节输入上限成正比。 / Each position advances forward; scans are bounded by the 39-byte input limit.
    for(int start=0;start<n;start++){
        if(ime_path[start].cost==1000000&&start)continue;
        ime_dictionary_tokens(start,n);
        for(size_t i=0;i<sizeof(reader_ime_phrases)/sizeof(reader_ime_phrases[0]);i++)ime_token(&reader_ime_phrases[i],true,(int)i,start,n);
        for(size_t i=0;i<sizeof(reader_pinyin)/sizeof(reader_pinyin[0]);i++)ime_token(&reader_pinyin[i],false,(int)i,start,n);
    }
    if(ime_path[n].cost<1000000)candidate_add(ime_path[n].text,(int)strlen(composition),8+ime_path[n].cost/20);
}

static bool candidate_select(int index){
    if(index<0||index>=candidate_count||!editor_append(candidates[index]))return false;
    size_t consumed=candidate_consume[index],length=strlen(composition);
    if(consumed>length)consumed=length;
    memmove(composition,composition+consumed,length-consumed+1);
    while(composition[0]=='\'')memmove(composition,composition+1,strlen(composition));
    candidates_update();
    return true;
}

static void composition_add(char c){
    size_t n=strlen(composition);
    if(n+1>=sizeof(composition)){
        ps_toast("请先选择候选字");
        return;
    }
    composition[n]=c;
    composition[n+1]=0;
    candidates_update();

}

static void editor_backspace(void){
    if(composition[0]){
        u8back(composition);
        candidates_update();
    }
    else{
        u8back(editor_text);
        editor_changed();
    }
}

static void editor_mode(int mode){
    input_mode=input_supported(mode);
    if(input_mode==INPUT_PINYIN||input_mode==INPUT_T9)ime_dictionary_prepare();
    editor_pressed=-1;
    composition[0]=0;
    candidates_update();
}

void ps_edit(const char *title,const char *initial,size_t max_bytes,void (*done)(const char*)){
    editor_generation++;
    editor_numeric=editor_decimal=false;editor_submit[0]=editor_hint[0]=0;
    editor_font_prepared=false;editor_font_prepare();
    ps_reader_editor_invalidate();
    editor_password=editor_password_visible=editor_wifi_password=false;
    editor_symbol_page=0;
    ri_copy(editor_title,sizeof(editor_title),title?title:"输入");
    editor_limit=max_bytes<sizeof(editor_text)-1?max_bytes:sizeof(editor_text)-1;
    u8copy(editor_text,editor_limit+1,initial?initial:"",strlen(initial?initial:""));
    composition[0]=0;
    candidate_count=candidate_page=0;candidate_revision++;
    editor_note=editor_real_note=false;
    editor_dirty=false;
    draft_auto_paused=false;
    editor_shift=false;
    editor_pressed=-1;
    editor_ink_count=0;
    editor_done=done;
    input_mode=input_supported(rp.input);
    if(input_mode==INPUT_PINYIN||input_mode==INPUT_T9)ime_dictionary_prepare();
    ps_navigate(PS_EDITOR);

}

// 地址和配对码使用大数字键；每个新编辑步骤有独立生命周期。/ Addresses and pairing codes use large digit keys; each new step owns its lifecycle.
void ps_edit_numeric(const char *title,const char *initial,size_t max_bytes,bool decimal,
                     const char *submit,const char *hint,void (*done)(const char*)){
    ps_edit(title,initial,max_bytes,done);
    editor_numeric=true;editor_decimal=decimal;input_mode=INPUT_SYMBOL;
    ri_copy(editor_submit,sizeof(editor_submit),submit?submit:"完成");
    ri_copy(editor_hint,sizeof(editor_hint),hint?hint:"");
}

void ps_edit_secret(const char *title,size_t max_bytes,void (*done)(const char*)){
    ps_edit(title,"",max_bytes,done);
    editor_password=true;
    input_mode=INPUT_ENGLISH;
}

void ps_edit_password(const char *title,size_t max_bytes,void (*done)(const char*)){
    ps_edit_secret(title,max_bytes,done);
    editor_wifi_password=true;
}

static int keyboard_top(void){
    ps_view_t v=ps_view();
    return v.landscape?194:v.h-364;
}

static int keyboard_bottom(void){return ps_view().h-88;}
// 旧笔迹只作为既有记录附件保留；文字编辑器不创建新笔迹。 / Legacy ink remains an existing record attachment; text input never creates new ink.
bool ps_reader_editor_ink_mode(void){return false;}
bool ps_reader_editor_ink_active(void){return false;}
void ps_reader_editor_ink_cancel(void){}
static uint8_t ink_snapshot(reader_ink_t out[INK_MAX]){
    memcpy(out,editor_ink,sizeof(editor_ink));return editor_ink_count;
}

static void editor_commit(void){
    if(editor_wifi_password){
        size_t n=strlen(editor_text);
        bool hex=n==64;
        if(hex)for(size_t i=0;i<n;i++)if(!isxdigit((unsigned char)editor_text[i]))hex=false;
        if((n<8||n>63)&&!hex){ps_toast("密码需要 8–63 位，或 64 位十六进制密钥");return;}
    }
    if(composition[0]){
        ps_toast("请先点选候选字，或删除未完成的拼音");
        return;
    }
    if(editor_done){
        void (*done)(const char*)=editor_done;
        bool note_callback=editor_note;
        uint32_t generation=editor_generation;
        done(editor_text);
        // 回调可能进入下一输入步骤；只退出仍然属于本次提交的编辑器。/ A callback may open the next step; close only the editor that submitted.
        if(generation==editor_generation&&!note_callback&&!editor_note&&ps_route()==PS_EDITOR)ps_back();
    }
    else ps_back();

}

static void draw_ink(uint8_t *fb,const reader_ink_t *ink,int count,int x,int y,int w,int h){
    for(int i=0;i<count;i++){
        int nx=x+(ink[i].x&0x7fff)*w/1023,ny=y+ink[i].y*h/1023;
        if(i&&!(ink[i].x&0x8000))ps_line(fb,x+(ink[i-1].x&0x7fff)*w/1023,y+ink[i-1].y*h/1023,nx,ny,1);
        else ps_fill(fb,nx,ny,2,2,1);
    }

}

static const char *const input_names[]={
    "拼音26键","拼音9键","英文26键","数字符号"};

enum {
    EK_DONE,EK_MODE,EK_CANDIDATE,EK_PREV,EK_NEXT,EK_LETTER,EK_SEPARATOR,EK_SHIFT,EK_DELETE,
    EK_COMMA,EK_SPACE,EK_PERIOD,EK_NEWLINE,EK_PASSWORD_MODE
};
typedef struct {int x,y,w,h,action,value;char label[64];} editor_key_t;
static PICO_UI_CACHE_ATTR editor_key_t editor_key_buffer[64];
typedef struct {int x,y,w,h;uint8_t consume;char text[CAND_TEXT],prefix[40];} editor_visible_candidate_t;
static PICO_UI_CACHE_ATTR editor_visible_candidate_t editor_visible_candidates[8],editor_candidate_contact;
static int editor_visible_count;
static const char *const input_short[]={"拼音","九键","EN","123"};

static int editor_label_width(const char *s){return (ttf_text_width_px(40,s)*10+18)/19;}

static void editor_key_add(editor_key_t out[64],int *n,int x,int y,int w,int h,int action,int value,const char *label){
    if(*n>=64)return;
    editor_key_t *k=&out[(*n)++];
    *k=(editor_key_t){.x=x,.y=y,.w=w,.h=h,.action=action,.value=value};
    u8copy(k->label,sizeof(k->label),label,strlen(label));
    if(editor_label_width(k->label)>w-12){
        while(k->label[0]&&editor_label_width(k->label)>w-32)u8back(k->label);
        size_t length=strlen(k->label);
        if(length+4<sizeof(k->label))memcpy(k->label+length,"…",4);
    }
}

static int candidate_cell_width(int at,int available){return ri_clamp(editor_label_width(candidates[at])+20,54,available);}

static int candidate_page_end(int first,int available){
    int used=0,at=first;
    while(at<candidate_count){
        int width=candidate_cell_width(at,available);
        if(used&&used+width>available)break;
        used+=width+4;at++;
    }
    return at;
}

static int candidate_page_previous(int first,int available){
    int at=0;
    while(at<first){int next=candidate_page_end(at,available);if(next>=first||next<=at)return at;at=next;}
    return 0;
}

// 绘制与命中共用同一组矩形；横屏左右分区，竖屏保留手机错列与触控间距。 / Render and hit testing share rectangles; landscape splits the view, portrait keeps staggered phone rows and gaps.
static int editor_keys_build(editor_key_t out[64]){
    ps_view_t v=ps_view();int n=0,top=keyboard_top();
    const char *submit=editor_submit[0]?editor_submit:editor_wifi_password?"连接":"完成";
    editor_key_add(out,&n,v.w-76,32,60,30,EK_DONE,0,submit);
    if(editor_numeric){
        int x=v.landscape?224:16,w=v.landscape?v.w-240:v.w-32;
        int y=v.landscape?86:top+82,ph=v.landscape?51:54,pw=w/3;
        for(int i=0;i<9;i++){
            char label[2]={(char)('1'+i),0};
            editor_key_add(out,&n,x+(i%3)*pw,y+(i/3)*ph,pw-8,ph-6,EK_LETTER,'1'+i,label);
        }
        if(editor_decimal)editor_key_add(out,&n,x,y+3*ph,pw-8,ph-6,EK_LETTER,'.',".");
        editor_key_add(out,&n,x+pw,y+3*ph,pw-8,ph-6,EK_LETTER,'0',"0");
        editor_key_add(out,&n,x+2*pw,y+3*ph,pw-8,ph-6,EK_DELETE,0,"删除");
        return n;
    }
    int mode_count=editor_password?5:INPUT_MODE_COUNT;
    for(int i=0;i<mode_count;i++){
        int x,y,w;
        if(v.landscape){x=16+(i%2)*100;y=top+(i/2)*40;w=editor_password&&i==4?192:92;}
        else{w=(v.w-24)/mode_count;x=12+i*w;y=top;w-=3;}
        static const char *const password_modes[]={"abc","ABC","123","#+=","显示"};
        const char *label=editor_password?(i==4&&editor_password_visible?"隐藏":password_modes[i]):input_short[i];
        editor_key_add(out,&n,x,y,w,34,editor_password?EK_PASSWORD_MODE:EK_MODE,editor_password?i:input_ids[i],label);
    }
    int x=v.landscape?224:8,width=v.landscape?404:v.w-16;
    if(input_mode==INPUT_PINYIN||input_mode==INPUT_T9){
        int cy=v.landscape?86:top+66,available=width-80,cx=x+40;
        int end=candidate_page_end(candidate_page,available);
        editor_key_add(out,&n,x,cy,34,40,EK_PREV,candidate_page?candidate_page_previous(candidate_page,available):-1,"‹");
        for(int i=candidate_page;i<end;i++){
            int w=candidate_cell_width(i,available);
            editor_key_add(out,&n,cx,cy,w,40,EK_CANDIDATE,i,candidates[i]);cx+=w+4;
        }
        editor_key_add(out,&n,x+width-34,cy,34,40,EK_NEXT,end<candidate_count?end:-1,"›");
    }
    int ky=v.landscape?136:top+114,pitch=v.landscape?44:48,kh=pitch-4;
    if(input_mode==INPUT_T9){
        static const char *const labels[]={"分词","2 abc","3 def","4 ghi","5 jkl","6 mno","7 pqrs","8 tuv","9 wxyz"};
        int kw=(width-54)/3;
        for(int i=0;i<9;i++)editor_key_add(out,&n,x+(i%3)*kw,ky+(i/3)*pitch,kw-4,kh,i?EK_LETTER:EK_SEPARATOR,i?'1'+i:0,labels[i]);
        editor_key_add(out,&n,x+width-48,ky,48,pitch*3-4,EK_DELETE,0,"删");
    }
    else{
        static const char *const rows[]={"qwertyuiop","asdfghjkl","zxcvbnm"};
        static const char *const symbols[]={"1234567890","+-=*/()[]",".,!?;:@"};
        static const char *const password_symbols[]={"1234567890","+-_=*/()[]",".,!?;:@"};
        static const char *const password_more[]={"!@#$%^&*()","{}<>\\|:;\"","'`~/?-_"};
        int kp=width/10,kw=kp-4;
        for(int row=0;row<3;row++){
            const char *letters=input_mode==INPUT_SYMBOL?(editor_password?(editor_symbol_page?password_more[row]:password_symbols[row]):symbols[row]):rows[row];
            int count=(int)strlen(letters),left=x+(width-count*kp)/2;
            for(int i=0;i<count;i++){
                char letter=letters[i];if(editor_shift&&input_mode==INPUT_ENGLISH)letter=(char)toupper((unsigned char)letter);
                char label[2]={letter,0};
                editor_key_add(out,&n,left+i*kp,ky+row*pitch,kw,kh,EK_LETTER,letters[i],label);
            }
        }
        int special=v.landscape?54:45;
        if(input_mode!=INPUT_SYMBOL)editor_key_add(out,&n,x,ky+2*pitch,special,kh,input_mode==INPUT_ENGLISH?EK_SHIFT:EK_SEPARATOR,0,input_mode==INPUT_ENGLISH?"↑":"分词");
        editor_key_add(out,&n,x+width-special,ky+2*pitch,special,kh,EK_DELETE,0,"删");
    }
    int bottom=keyboard_bottom(),small=v.landscape?52:45,gap=6,space=width-3*small-3*gap;
    editor_key_add(out,&n,x,bottom,small,36,EK_COMMA,0,input_mode==INPUT_ENGLISH||input_mode==INPUT_SYMBOL?",":"，");
    editor_key_add(out,&n,x+small+gap,bottom,space,36,EK_SPACE,0,composition[0]?"选首项":"空格");
    editor_key_add(out,&n,x+small+gap+space+gap,bottom,small,36,EK_PERIOD,0,input_mode==INPUT_ENGLISH||input_mode==INPUT_SYMBOL?".":"。");
    editor_key_add(out,&n,x+width-small,bottom,small,36,editor_password?EK_DONE:EK_NEWLINE,0,editor_password?(editor_wifi_password?"连接":"完成"):"换行");
    return n;
}

// 候选或方向未变时复用真实字体测量，PRESS/MOVE/RELEASE不重复排版。 / Reuse measured keys until candidates/orientation change; touch tracking does not re-layout text.
static int editor_keys(editor_key_t out[64]){
    static uint64_t previous;
    static int count;
    ps_view_t v=ps_view();
    const int state[]={v.w,v.h,v.landscape,input_mode,candidate_page,(int)candidate_revision,editor_shift,editor_password,editor_password_visible,editor_wifi_password,editor_symbol_page,composition[0]!=0,ttf_get_weight(),(int)editor_generation,editor_numeric,editor_decimal};
    uint64_t signature=UINT64_C(14695981039346656037);
    for(size_t i=0;i<sizeof(state)/sizeof(state[0]);i++)signature=(signature^(unsigned)state[i])*UINT64_C(1099511628211);
    for(const char *p=ttf_font_path();p&&*p;p++)signature=(signature^(unsigned char)*p)*UINT64_C(1099511628211);
    if(!count||signature!=previous){count=editor_keys_build(editor_key_buffer);previous=signature;}
    if(out!=editor_key_buffer)memcpy(out,editor_key_buffer,(size_t)count*sizeof(*out));
    return count;
}

static void editor_draw_key(uint8_t *fb,const editor_key_t *k){
    bool selected=k->action==EK_DONE||(k->action==EK_MODE&&k->value==input_mode)||(k->action==EK_SHIFT&&editor_shift);
    ps_button(fb,k->x,k->y,k->w,k->h,k->label,selected);
    if(!selected)ps_box(fb,k->x,k->y,k->w,k->h,PS_THEME_BUTTON_RADIUS,5,false);
}

static void editor_text_render(uint8_t *fb){
    ps_view_t v=ps_view();int top=keyboard_top();bool split=v.landscape;
    int text_width=split?192:v.w-32,text_height=split?88:editor_numeric?100:top-110;
    ps_box(fb,16,82,text_width,text_height,4,5,false);
    // 输入内容始终保留尾部，长笔记可从记录详情查看全文。 / Keep the editing tail visible; record details show the full saved note.
    const char *tail=editor_text;size_t length=strlen(tail),show=(size_t)(split?90:v.landscape?120:320);
    char masked[321];
    if(editor_password&&!editor_password_visible){size_t visible=length>show?show:length;memset(masked,'*',visible);masked[visible]=0;tail=masked;}
    else if(length>show){tail+=length-show;while(((unsigned char)*tail&0xc0)==0x80)tail++;}
    ps_wrap(fb,24,editor_numeric?106:92,text_width-16,editor_numeric?(split?18:28):15,editor_numeric?1:(text_height-12)/26,tail,1);
    char count[96];snprintf(count,sizeof(count),editor_numeric?"%u/%u 位%s%s":"%u/%u 字节%s%s",(unsigned)length,(unsigned)editor_limit,editor_note&&!split?" · 自动存草稿":"",!editor_font[0]&&!editor_password?" · 字库受限":"");
    ps_text(fb,18,split?175:editor_numeric?196:top-24,11,count,6);
}

static bool editor_candidate_key(const editor_key_t *key){return key->action==EK_CANDIDATE||key->action==EK_PREV||key->action==EK_NEXT;}

static void editor_candidates_render(uint8_t *fb){
    ps_view_t v=ps_view();int top=keyboard_top();bool split=v.landscape;
    int cx=split?224:16,cy=split?38:top+40;
    if(editor_numeric){ps_wrap(fb,cx,cy,split?324:v.w-32,12,1,editor_hint,3);return;}
    const char *hint=input_mode==INPUT_T9?"连输九键 · 点候选选字":input_mode==INPUT_PINYIN?"连续拼音 · 支持词组补全":input_mode==INPUT_ENGLISH?"英文输入 · ↑切换大小写":"数字与常用符号";
    ps_wrap(fb,cx,cy,split?324:v.w-32,14,1,composition[0]?composition:hint,composition[0]?1:5);
    if((input_mode==INPUT_PINYIN||input_mode==INPUT_T9)&&!candidate_count)ps_text(fb,cx+40,split?98:top+78,12,!editor_font[0]?"字体覆盖受限 · 请安装TF完整字体":composition[0]?"暂无候选，请检查拼音":"候选词显示在这里",6);
    editor_key_t *keys=editor_key_buffer;int n=editor_keys(keys);
    for(int i=0;i<n;i++)if(editor_candidate_key(&keys[i]))editor_draw_key(fb,&keys[i]);
}

static uint64_t editor_layout_signature(void){
    ps_view_t v=ps_view();
    const int state[]={v.w,v.h,v.landscape,input_mode,editor_shift,editor_password,editor_password_visible,editor_wifi_password,editor_symbol_page,editor_note,(int)editor_limit,ttf_get_weight(),(int)editor_generation,editor_numeric,editor_decimal};
    uint64_t hash=UINT64_C(14695981039346656037);
    for(size_t i=0;i<sizeof(state)/sizeof(state[0]);i++)hash=(hash^(unsigned)state[i])*UINT64_C(1099511628211);
    for(const char *p=ttf_font_path();p&&*p;p++)hash=(hash^(unsigned char)*p)*UINT64_C(1099511628211);
    return hash;
}

static void editor_frame_remember(void){
    editor_frame.layout=editor_layout_signature();editor_frame.candidates=candidate_revision;editor_frame.page=candidate_page;
    editor_frame.composing=composition[0]!=0;ri_copy(editor_frame.text,sizeof(editor_frame.text),editor_text);editor_frame.valid=true;
    // 合并绘制期间仍按屏幕上实际显示的候选选字，不能命中尚未画出的新候选。
    // Coalesced input selects the candidate actually painted, never an unseen replacement.
    editor_visible_count=0;editor_key_t *keys=editor_key_buffer;int n=editor_keys(keys);
    for(int i=0;i<n&&editor_visible_count<8;i++)if(keys[i].action==EK_CANDIDATE){
        editor_key_t *k=&keys[i];editor_visible_candidate_t *c=&editor_visible_candidates[editor_visible_count++];
        *c=(editor_visible_candidate_t){.x=k->x,.y=k->y,.w=k->w,.h=k->h,.consume=candidate_consume[k->value]};
        ri_copy(c->text,sizeof(c->text),candidates[k->value]);ri_copy(c->prefix,sizeof(c->prefix),composition);
    }
}

// 损伤区保持产品逻辑坐标；候选整条清空以移除旧布局，字母区保持静止。 / Damage uses product logical coordinates; clear the whole old candidate strip while letters stay untouched.
bool ps_reader_editor_render_partial(uint8_t *fb,EpdRect areas[4],int *count){
    if(count)*count=0;
    if(!fb||!areas||!count||ps_route()!=PS_EDITOR||!editor_frame.valid||editor_frame.layout!=editor_layout_signature())return false;
    ps_view_t v=ps_view();int top=keyboard_top();
    if(strcmp(editor_frame.text,editor_text)){
        EpdRect r={14,80,v.landscape?196:v.w-28,v.landscape?112:top-88};
        ps_fill(fb,r.x,r.y,r.width,r.height,15);editor_text_render(fb);areas[(*count)++]=r;
    }
    if(editor_frame.candidates!=candidate_revision||editor_frame.page!=candidate_page){
        // 横屏提示与候选之间有“完成”键；分开清底，不擦除静态键。 / Landscape Done lies beside the hint; clear each strip separately to preserve static keys.
        ps_fill(fb,v.landscape?224:16,v.landscape?36:top+38,v.landscape?324:v.w-32,24,15);
        ps_fill(fb,v.landscape?224:8,v.landscape?84:top+64,v.landscape?404:v.w-16,44,15);
        editor_candidates_render(fb);
        areas[(*count)++]=(EpdRect){v.landscape?222:6,v.landscape?34:top+36,v.landscape?408:v.w-12,v.landscape?96:74};
    }
    if(editor_frame.composing!=(composition[0]!=0)){
        editor_key_t *keys=editor_key_buffer;int n=editor_keys(keys);
        for(int i=0;i<n;i++)if(keys[i].action==EK_SPACE){
            editor_key_t *k=&keys[i];EpdRect r={k->x-1,k->y-1,k->w+2,k->h+2};
            ps_fill(fb,r.x,r.y,r.width,r.height,15);editor_draw_key(fb,k);areas[(*count)++]=r;break;
        }
    }
    editor_frame_remember();return true;
}

static void editor_render(uint8_t *fb){
    ps_view_t v=ps_view();
    bool split=v.landscape;
    if(split){
        ps_icon(fb,12,36,22,PI_BACK,2);ps_wrap(fb,46,35,160,20,1,editor_title,2);ps_line(fb,12,68,v.w-12,68,5);
    }
    else ps_heading(fb,editor_title,NULL);
    editor_text_render(fb);
    editor_candidates_render(fb);
    editor_key_t *keys=editor_key_buffer;int n=editor_keys(keys);
    for(int i=0;i<n;i++)if(!editor_candidate_key(&keys[i]))editor_draw_key(fb,&keys[i]);
    editor_frame_remember();
}

static int editor_key_at(const editor_key_t *keys,int count,int x,int y){
    for(int i=0;i<count;i++)if(ps_hit(x,y,keys[i].x,keys[i].y,keys[i].w,keys[i].h))return i;
    return -1;
}

static void editor_key_activate(const editor_key_t *key){
    switch(key->action){
        case EK_DONE:editor_commit();break;
        case EK_PASSWORD_MODE:
            if(key->value==4)editor_password_visible=!editor_password_visible;
            else{editor_mode(key->value<2?INPUT_ENGLISH:INPUT_SYMBOL);editor_shift=key->value==1;editor_symbol_page=key->value==3;}
            break;
        case EK_MODE:
            if(composition[0])ps_toast("切换前请先选字或删除拼音");
            else editor_mode(key->value);
            break;
        case EK_CANDIDATE:candidate_select(key->value);break;
        case EK_PREV:case EK_NEXT:if(key->value>=0)candidate_page=key->value;break;
        case EK_DELETE:editor_backspace();break;
        case EK_SHIFT:editor_shift=!editor_shift;break;
        case EK_SEPARATOR:{size_t n=strlen(composition);if(n&&composition[n-1]!='\'')composition_add('\'');break;}
        case EK_LETTER:
            if(input_mode==INPUT_PINYIN||input_mode==INPUT_T9)composition_add((char)key->value);
            else{char s[2]={editor_shift&&input_mode==INPUT_ENGLISH?(char)toupper((unsigned char)key->value):(char)key->value,0};editor_append(s);}
            break;
        case EK_SPACE:
            if(composition[0]){if(!candidate_select(0))ps_toast("暂无候选，请检查拼音");}
            else editor_append(" ");
            break;
        case EK_COMMA:case EK_PERIOD:case EK_NEWLINE:
            if(composition[0]){ps_toast("请先点选候选字");break;}
            editor_append(key->action==EK_NEWLINE?"\n":key->action==EK_COMMA?(input_mode==INPUT_ENGLISH||input_mode==INPUT_SYMBOL?",":"，"):(input_mode==INPUT_ENGLISH||input_mode==INPUT_SYMBOL?".":"。"));break;
        default:break;
    }
}

static bool editor_event(const ps_event_t *e){
    ps_view_t v=ps_view();
    // 按下与抬起必须同键；跨键后即使滑回也取消，合成TAP不再重复输入。 / Down and up must share a key; crossing cancels even after returning, and synthetic TAP never types twice.
    editor_key_t *keys=editor_key_buffer;int count=editor_keys(keys),hit=editor_key_at(keys,count,e->x,e->y);
    if(e->type==PS_PRESS){
        editor_pressed=hit;editor_press_cancelled=false;editor_press_x=e->x;editor_press_y=e->y;editor_press_w=v.w;editor_press_h=v.h;
        editor_press_visible_candidate=false;
        if(editor_frame.valid&&editor_frame.layout==editor_layout_signature())for(int i=0;i<editor_visible_count;i++){
            editor_visible_candidate_t *c=&editor_visible_candidates[i];
            if(ps_hit(e->x,e->y,c->x,c->y,c->w,c->h)){editor_press_visible_candidate=true;editor_candidate_contact=*c;break;}
        }
        return false;
    }
    if(e->type==PS_MOVE){
        bool same=editor_press_visible_candidate?ps_hit(e->x,e->y,editor_candidate_contact.x,editor_candidate_contact.y,editor_candidate_contact.w,editor_candidate_contact.h):hit==editor_pressed;
        if(!same||abs(e->x-editor_press_x)+abs(e->y-editor_press_y)>18)editor_press_cancelled=true;
        return false;
    }
    if(e->type==PS_RELEASE){
        int pressed=editor_pressed;editor_pressed=-1;
        if(editor_press_visible_candidate){
            editor_press_visible_candidate=false;editor_visible_candidate_t *c=&editor_candidate_contact;
            if(editor_press_cancelled||editor_press_w!=v.w||editor_press_h!=v.h||
               !ps_hit(e->x,e->y,c->x,c->y,c->w,c->h)||abs(e->x-editor_press_x)+abs(e->y-editor_press_y)>18)return false;
            if(strlen(composition)<c->consume||strncmp(composition,c->prefix,c->consume))return true;
            if(!editor_append(c->text))return true;
            memmove(composition,composition+c->consume,strlen(composition)-c->consume+1);
            while(composition[0]=='\'')memmove(composition,composition+1,strlen(composition));
            candidates_update();return true;
        }
        if(pressed<0||pressed>=count||hit!=pressed||editor_press_cancelled||editor_press_w!=v.w||editor_press_h!=v.h||abs(e->x-editor_press_x)+abs(e->y-editor_press_y)>18)return false;
        editor_key_activate(&keys[pressed]);return true;
    }
    if(e->type==PS_CANCEL||(e->type>=PS_SWIPE_L&&e->type<=PS_SWIPE_D)){editor_pressed=-1;editor_press_cancelled=true;editor_press_visible_candidate=false;}
    return false;
}

/* ---- 页面布局 / Page layouts ---- */
static int shelf_page_count(int count);
static int shelf_page_size(void);
static int shelf_page_size_at(int page);
static int shelf_page_start(int page);
bool ps_reader_file_favorite(const char *path,bool toggle);
static void shelf_scan_close(void){
    if(shelf_dir){closedir(shelf_dir);shelf_dir=NULL;}
    shelf_scanning=false;
}
static void shelf_cover_display_release(void){
    for(size_t i=0;i<SHELF_COVER_VISIBLE;i++){free(shelf_display_covers[i].pixels);memset(&shelf_display_covers[i],0,sizeof(shelf_display_covers[i]));}
    shelf_display_covers_held=false;
}
static bool shelf_cover_display_get(const char *path,book_cover_view_t *out){
    if(!shelf_display_covers_held)return book_cover_get(path,out);
    *out=(book_cover_view_t){0};
    for(size_t i=0;i<SHELF_COVER_VISIBLE;i++)if(shelf_display_covers[i].pixels&&!strcmp(path,shelf_display_covers[i].path)){
        *out=(book_cover_view_t){shelf_display_covers[i].pixels,shelf_display_covers[i].width,shelf_display_covers[i].height};return true;
    }
    return false;
}
// 后台缓存可被重用；复制已显示的最多九张封面，保证任何菜单重绘仍保持已提交画面。
// Background work may reuse the decoder cache; retain at most nine displayed covers so overlay redraws preserve committed pixels.
static bool shelf_cover_display_hold(void){
    shelf_cover_copy_t next[SHELF_COVER_VISIBLE]={0};int page=ri_clamp(shelf_scroll,0,shelf_page_count(shelf_file_visible)-1),size=shelf_page_size_at(page),start=shelf_page_start(page);
    for(int j=0;j<size&&j<SHELF_COVER_VISIBLE&&start+j<shelf_file_visible;j++){
        const char *path=shelf_files[shelf_file_ids[start+j]].path;book_cover_view_t cover={0};
        if(!shelf_cover_display_get(path,&cover)||!cover.pixels)continue;
        size_t bytes=(size_t)cover.width*cover.height;
        if(!bytes||bytes>128*192)continue;
        next[j].pixels=heap_caps_calloc(1,bytes,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
        if(!next[j].pixels){for(int i=0;i<SHELF_COVER_VISIBLE;i++)free(next[i].pixels);return false;}
        memcpy(next[j].pixels,cover.pixels,bytes);ri_copy(next[j].path,sizeof(next[j].path),path);
        next[j].width=cover.width;next[j].height=cover.height;
    }
    shelf_cover_display_release();memcpy(shelf_display_covers,next,sizeof(next));shelf_display_covers_held=true;return true;
}
static bool shelf_cover_display_changed(void){
    int page=ri_clamp(shelf_scroll,0,shelf_page_count(shelf_file_visible)-1),size=shelf_page_size_at(page),start=shelf_page_start(page);
    for(int j=0;j<size&&j<SHELF_COVER_VISIBLE&&start+j<shelf_file_visible;j++){
        const char *path=shelf_files[shelf_file_ids[start+j]].path;book_cover_view_t before={0},after={0};
        bool had=shelf_cover_display_get(path,&before),has=book_cover_get(path,&after);
        if(had!=has)return true;
        if(has&&(before.width!=after.width||before.height!=after.height||memcmp(before.pixels,after.pixels,(size_t)after.width*after.height)))return true;
    }
    return false;
}
static void shelf_stage_cancel(bool retry){
    shelf_scan_close();shelf_stage_active=shelf_commit_pending=false;shelf_scan_pending=retry;
    shelf_cover_signature=0;shelf_cover_changed=shelf_cover_batch_done=false;shelf_cover_attempts=0;
}

static int shelf_file_compare(const void *left,const void *right){
    const reader_file_t *a=left,*b=right;
    if(!rp.sort&&a->recent!=b->recent)return a->recent>b->recent?-1:1;
    int cmp=strcasecmp(a->name,b->name);
    return cmp?cmp:strcmp(a->path,b->path);
}

static PICO_READER_COLD void shelf_favorites_load(void){
    if(shelf_favorites_loaded)return;
    shelf_favorites_loaded=true;
    if(!shelf_favorites)shelf_favorites=heap_caps_calloc(1,sizeof(*shelf_favorites),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    if(!shelf_favorites){shelf_favorites_failed=true;return;}
    if(!ps_load("reader-favs-v1",shelf_favorites,sizeof(*shelf_favorites))){shelf_favorites->version=1;return;}
    bool valid=shelf_favorites->version==1&&shelf_favorites->count<=SHELF_FAVORITE_MAX;
    if(valid)for(uint32_t i=0;i<shelf_favorites->count;i++){
        const char *path=shelf_favorites->paths[i];
        if(!memchr(path,0,BOOK_STORE_PATH_MAX)||!path[0]){valid=false;break;}
        for(uint32_t j=0;j<i;j++)if(!strcmp(path,shelf_favorites->paths[j])){valid=false;break;}
    }
    if(!valid){memset(shelf_favorites,0,sizeof(*shelf_favorites));shelf_favorites->version=1;shelf_favorites_failed=true;}
}
static int shelf_file_project(reader_file_t *files,int count,uint16_t *ids){
    int visible=0;
    for(int i=0;i<count;i++)if((!shelf_search[0]||strstr(files[i].name,shelf_search))&&
        (!shelf_filter||(shelf_filter==1?ps_reader_file_favorite(files[i].path,false):files[i].started)))ids[visible++]=(uint16_t)i;
    return visible;
}
static void shelf_file_rebuild(void){
    shelf_file_visible=shelf_file_project(shelf_files,shelf_file_count,shelf_file_ids);
    shelf_scroll=ri_clamp(shelf_scroll,0,shelf_page_count(shelf_file_visible)-1);
}
bool ps_reader_file_favorite(const char *path,bool toggle){
    if(!path||!path[0]||strlen(path)>=BOOK_STORE_PATH_MAX)return false;
    if(!shelf_favorites_loaded){if(!toggle)return false;shelf_favorites_load();}
    if(!shelf_favorites)return false;
    uint32_t at=0;
    while(at<shelf_favorites->count&&strcmp(path,shelf_favorites->paths[at]))at++;
    bool active=at<shelf_favorites->count;
    if(!toggle)return active;
    if(shelf_favorites_failed){ps_toast("收藏记录不可用，请检查存储后重启");return false;}
    if(!active&&shelf_favorites->count>=SHELF_FAVORITE_MAX){ps_toast("收藏已达上限，请先取消其它收藏");return false;}
    char removed[BOOK_STORE_PATH_MAX];
    if(active){
        ri_copy(removed,sizeof(removed),shelf_favorites->paths[at]);
        memmove(shelf_favorites->paths+at,shelf_favorites->paths+at+1,(shelf_favorites->count-at-1)*BOOK_STORE_PATH_MAX);
        shelf_favorites->count--;memset(shelf_favorites->paths[shelf_favorites->count],0,BOOK_STORE_PATH_MAX);
    }else ri_copy(shelf_favorites->paths[shelf_favorites->count++],BOOK_STORE_PATH_MAX,path);
    if(!reader_store("reader-favs-v1",shelf_favorites,sizeof(*shelf_favorites))){
        if(active){memmove(shelf_favorites->paths+at+1,shelf_favorites->paths+at,(shelf_favorites->count-at)*BOOK_STORE_PATH_MAX);ri_copy(shelf_favorites->paths[at],BOOK_STORE_PATH_MAX,removed);shelf_favorites->count++;}
        else{memset(shelf_favorites->paths[--shelf_favorites->count],0,BOOK_STORE_PATH_MAX);}
        ps_toast("收藏未保存，请检查存储后重试");return false;
    }
    shelf_file_rebuild();return true;
}

static void shelf_scan_finish(void){
    shelf_scan_close();
    if(shelf_stage_count>1)qsort(shelf_stage_files,(size_t)shelf_stage_count,sizeof(*shelf_stage_files),shelf_file_compare);
    shelf_stage_visible=shelf_file_project(shelf_stage_files,shelf_stage_count,shelf_stage_ids);
    shelf_commit_pending=true;
}

static bool shelf_io_available(read_pico_sd_info_t *info){
    memset(info,0,sizeof(*info));
    read_pico_sd_get_info(info);
    return !ps_storage_busy()&&!ps_usb_held()&&!info->exported;
}

static PICO_READER_COLD bool shelf_scan_begin(const read_pico_sd_info_t *info){
    shelf_scan_close();
    shelf_cover_signature=0;shelf_cover_changed=shelf_cover_batch_done=false;shelf_cover_attempts=0;
    shelf_commit_pending=false;shelf_stage_active=true;
    shelf_favorites_load();
    shelf_scan_pending=false;shelf_stage_limited=shelf_stage_error=false;
    shelf_stage_count=shelf_stage_visible=shelf_scan_seen=shelf_scan_root=shelf_scan_roots=0;
    shelf_stage_generation=info->media_generation;shelf_stage_media_ready=info->present&&info->mounted;shelf_have_media=true;
    shelf_stage_revision=book_store_revision();
    if(!shelf_stage_files)shelf_stage_files=heap_caps_calloc(SHELF_FILE_MAX,sizeof(*shelf_stage_files),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    if(!shelf_stage_files||!shelf_cover_display_hold()){shelf_stage_cancel(false);shelf_scan_error=true;shelf_ready=true;return false;}
    book_cover_clear();shelf_cover_next_ms=ps_now()+350;
    book_store_root_t roots[2];int count=0;
    if(book_store_roots(roots,&count)!=ESP_OK)shelf_stage_error=true;
    bool have_sd_root=false;
    // 同时兼容传书目录与U盘直接拖入TF根的书；不递归其它文件夹。 / Accept uploads and direct TF-root drops; do not recurse into other folders.
    for(int i=0;i<count&&i<2;i++){
        if(!roots[i].is_flash&&!shelf_stage_media_ready)continue;
        shelf_roots[shelf_scan_roots++]=roots[i];
        if(!roots[i].is_flash)have_sd_root=true;
        size_t n=strlen(roots[i].path);
        if(!roots[i].is_flash&&n>6&&!strcmp(roots[i].path+n-6,"/books")&&shelf_scan_roots<3){
            book_store_root_t parent=roots[i];parent.path[n-6]=0;
            shelf_roots[shelf_scan_roots++]=parent;
        }
    }
    if(shelf_stage_media_ready&&!have_sd_root&&shelf_scan_roots<3){
        shelf_roots[shelf_scan_roots++]=(book_store_root_t){.path="/sdcard"};
        shelf_stage_error=true;
    }
    shelf_stage_last_path[0]=0;book_progress_last_path(shelf_stage_last_path,sizeof(shelf_stage_last_path));
    shelf_scanning=shelf_scan_roots>0;
    if(!shelf_scanning)shelf_scan_finish();
    return true;
}

static PICO_READER_COLD bool shelf_scan_step(void){
    for(int batch=0;batch<SHELF_SCAN_BATCH&&shelf_scanning;batch++){
        if(!shelf_dir){
            if(shelf_scan_root>=shelf_scan_roots){shelf_scan_finish();break;}
            shelf_dir=opendir(shelf_roots[shelf_scan_root].path);
            if(!shelf_dir){shelf_stage_error=true;shelf_scan_root++;continue;}
        }
        if(shelf_scan_seen>=SHELF_SCAN_MAX){shelf_stage_limited=true;shelf_scan_finish();break;}
        errno=0;struct dirent *entry=readdir(shelf_dir);
        if(!entry){
            if(errno)shelf_stage_error=true;
            closedir(shelf_dir);shelf_dir=NULL;shelf_scan_root++;
            if(shelf_scan_root>=shelf_scan_roots)shelf_scan_finish();
            continue;
        }
        shelf_scan_seen++;
        if(entry->d_name[0]=='.')continue;
        const char *ext=strrchr(entry->d_name,'.');
        if(!ext||(strcasecmp(ext,".txt")&&strcasecmp(ext,".epub")))continue;
        if(shelf_stage_count>=SHELF_FILE_MAX){shelf_stage_limited=true;shelf_scan_finish();break;}
        reader_file_t *item=&shelf_stage_files[shelf_stage_count];memset(item,0,sizeof(*item));
        int n=snprintf(item->path,sizeof(item->path),"%s/%s",shelf_roots[shelf_scan_root].path,entry->d_name);
        if(n<0||(size_t)n>=sizeof(item->path)||strlen(entry->d_name)>=sizeof(item->name)){shelf_stage_limited=true;continue;}
        bool duplicate=false;
        for(int i=0;i<shelf_stage_count;i++)if(!strcmp(shelf_stage_files[i].path,item->path)){duplicate=true;break;}
        if(duplicate)continue;
        struct stat st;
        if(stat(item->path,&st)){shelf_stage_error=true;continue;}
        if(!S_ISREG(st.st_mode))continue;
        if(st.st_size<0||(uint64_t)st.st_size>UINT32_MAX){shelf_stage_limited=true;continue;}
        u8copy(item->name,sizeof(item->name),entry->d_name,(size_t)(ext-entry->d_name));
        if(!item->name[0])continue;
        item->is_flash=shelf_roots[shelf_scan_root].is_flash;item->epub=!strcasecmp(ext,".epub");item->size=(uint32_t)st.st_size;
        book_progress_t progress;
        item->started=book_progress_load(item->path,item->size,&progress);
        if(item->started){item->pct=progress.pct;item->recent=progress.last_open_s;}
        shelf_apply_dual_progress(item);
        shelf_stage_count++;
    }
    return !shelf_scanning;
}

static int shelf_model_continue(reader_file_t *files,int count,const char *last_path){
    for(int i=0;i<count;i++)if(!strcmp(files[i].path,last_path))return i;
    return count?0:-1;
}
static int shelf_file_continue(void){return shelf_model_continue(shelf_files,shelf_file_count,shelf_last_path);}

int ps_reader_cover_sources(char paths[][288],int capacity){
    if(!paths||capacity<=0||!shelf_files||shelf_file_count<=0)return 0;
    int count=0,continued=shelf_file_continue();
    // 仅导出已提交书单，不触发扫描、解码或受筛选影响。/ Export committed sources only, without scanning, decoding or active filters.
    for(int pass=-1;pass<shelf_file_count&&count<capacity;pass++){
        int index=pass<0?continued:pass;
        if(index<0||index>=shelf_file_count||(pass>=0&&index==continued))continue;
        const char *path=shelf_files[index].path;
        size_t length=strnlen(path,288);
        if(!length||length>=288)continue;
        bool duplicate=false;
        for(int i=0;i<count;i++)if(!strcmp(paths[i],path)){duplicate=true;break;}
        if(!duplicate){ri_copy(paths[count],288,path);count++;}
    }
    return count;
}

void ps_reader_library_status(unsigned *count,bool *loading,bool *limited){
    if(count)*count=(unsigned)shelf_file_count;
    if(loading)*loading=shelf_scan_pending||shelf_scanning||shelf_commit_pending;
    if(limited)*limited=shelf_scan_limited;
}
bool ps_reader_shelf_choice_open(void){return shelf_open_choice;}

typedef struct {
    EpdRect panel,single,dual,cancel;
} shelf_open_choice_layout_t;
static shelf_open_choice_layout_t shelf_open_choice_layout(void){
    ps_view_t v=ps_view();
    shelf_open_choice_layout_t r={0};
    if(v.landscape){
        r.panel=(EpdRect){48,25,v.w<592?v.w-96:560,310};
        r.single=(EpdRect){r.panel.x+20,r.panel.y+124,(r.panel.width-52)/2,108};
        r.dual=(EpdRect){r.panel.x+32+r.single.width,r.panel.y+124,r.single.width,108};
        r.cancel=(EpdRect){r.panel.x+r.panel.width-108,r.panel.y+r.panel.height-46,88,34};
    }else{
        r.panel=(EpdRect){12,(v.h-424)/2,v.w-24,424};
        r.single=(EpdRect){r.panel.x+14,r.panel.y+120,r.panel.width-28,102};
        r.dual=(EpdRect){r.panel.x+14,r.panel.y+234,r.panel.width-28,102};
        r.cancel=(EpdRect){r.panel.x+r.panel.width-102,r.panel.y+r.panel.height-44,88,34};
    }
    return r;
}
static void shelf_open_choice_close(void){
    shelf_open_choice=false;
    shelf_open_choice_focus=1;
    shelf_open_choice_path[0]=0;
    shelf_open_choice_name[0]=0;
}
static void shelf_open_choice_begin(const reader_file_t *book){
    if(!book)return;
    shelf_open_choice=true;
    shelf_open_choice_focus=1;
    ri_copy(shelf_open_choice_path,sizeof(shelf_open_choice_path),book->path);
    ri_copy(shelf_open_choice_name,sizeof(shelf_open_choice_name),book->name);
}
static void shelf_open_choice_execute(bool dual){
    if(!shelf_open_choice_path[0])return;
    char path[BOOK_STORE_PATH_MAX];
    ri_copy(path,sizeof(path),shelf_open_choice_path);
    shelf_open_choice_close();
    if(dual){
        (void)ps_dual_open(path);
        return;
    }
    ps_dual_set_local_away(true);
    app_book_dual_pause();
    if(app_book_open_path_single(path))ps_legacy(&app_book);
    else ps_toast("无法在本机打开这本书，请刷新书库后重试");
}
static void shelf_file_open(int index){
    if(index<0||index>=shelf_file_count){ps_toast(shelf_scanning?"书库正在读取，请稍候":"请先传入 TXT 或 EPUB 图书");return;}
    read_pico_sd_info_t info;
    if(!shelf_io_available(&info)||(!shelf_files[index].is_flash&&(!info.present||!info.mounted||info.media_generation!=shelf_media_generation))){
        ps_toast("TF 卡暂不可读，请检查后刷新书库");shelf_scan_pending=true;return;
    }
    shelf_stage_cancel(true);
    if(ps_dual_has_reading()){
        shelf_open_choice_begin(&shelf_files[index]);
        return;
    }
    if(ps_dual_enabled()){ps_dual_open(shelf_files[index].path);return;}
    if(app_book_open_path(shelf_files[index].path))ps_legacy(&app_book);
    else ps_toast("无法打开这本书，请刷新后重试");
}


static int progress_percent(int book){
    if(rp.books[book].done)return 100;
    if(!rp.books[book].started)return 0;
    if(rb[book].comic)return (rp.books[book].comic_page+1)*100/8;
    return rb[book].bytes?(int)((uint64_t)rp.books[book].offset*100/rb[book].bytes):0;

}

static uint32_t shelf_dual_progress_revision;
static int64_t shelf_dual_progress_at;
static void shelf_apply_dual_progress(reader_file_t *item){
    bool started;uint8_t pct;uint32_t recent;
    char local_only[BOOK_STORE_PATH_MAX];
    bool local=app_book_local_only_path(local_only,sizeof(local_only));
    if(item&&ps_dual_synced()&&(!local||strcmp(item->path,local_only))&&
       ps_dual_book_progress(item->path,item->size,&started,&pct,&recent)){
        item->started=started;item->pct=pct;item->recent=recent;
    }
}
static void shelf_refresh_dual_progress(void){
    uint32_t revision=ps_dual_progress_revision();
    if(revision==shelf_dual_progress_revision)return;
    // 进度表逐本到达；最多每秒合并一次，避免大书库连续重绘。
    // The progress table arrives one book at a time; coalesce updates to at most once per second.
    if(revision&&ps_now()-shelf_dual_progress_at<1000)return;
    shelf_dual_progress_revision=revision;
    shelf_dual_progress_at=ps_now();
    if(!shelf_files||!shelf_file_count)return;
    for(int i=0;i<shelf_file_count;i++)shelf_apply_dual_progress(&shelf_files[i]);
    shelf_file_rebuild();
    ps_dirty();
}

static int shelf_style(void){
    return rp.list;
}


typedef struct {
    int title_y,control_y,record_x,settings_x;
    int banner_x,banner_y,banner_w,banner_h;
    int continue_y,continue_h,search_x,search_y,search_w;
    int tabs_x,tabs_y,tabs_w,style_x,style_w,refresh_x,top,cols,cover_h,row_h;
} reader_shelf_layout_t;

static bool shelf_cover_grid_more(void){return rp.list==SHELF_VIEW_COVERS&&shelf_scroll>0;}
static reader_shelf_layout_t shelf_layout(void){
    // 横屏把续读卡与标题并列，竖屏保持单列，给书单留下更多正文空间。
    // Place continuation beside the landscape title and above the portrait list to keep more room for books.
    ps_view_t v=ps_view();reader_shelf_layout_t l;
    if(v.landscape)l=(reader_shelf_layout_t){
        .title_y=85,.control_y=84,.record_x=482,.settings_x=551,
        .banner_x=186,.banner_y=84,.banner_w=288,.banner_h=80,
        .continue_y=84,.continue_h=80,.search_x=590,.search_y=84,.search_w=34,
        .tabs_x=16,.tabs_y=128,.tabs_w=160,.style_x=482,.style_w=94,.refresh_x=586,
        .top=180,.cols=6,.cover_h=62,.row_h=128};
    else l=(reader_shelf_layout_t){
        .title_y=91,.control_y=88,.record_x=190,.settings_x=266,
        .banner_x=16,.banner_y=137,.banner_w=328,.banner_h=96,
        .continue_y=137,.continue_h=96,.search_x=306,.search_y=88,.search_w=38,
        .tabs_x=16,.tabs_y=245,.tabs_w=168,.style_x=192,.style_w=100,.refresh_x=300,
        .top=294,.cols=3,.cover_h=80,.row_h=144};
    if(shelf_cover_grid_more()){
        if(v.landscape){
            l.tabs_x=156;l.tabs_y=84;l.tabs_w=150;l.style_x=314;l.style_w=92;
            l.refresh_x=414;l.record_x=466;l.settings_x=538;l.search_x=586;l.top=128;
        }else{l.tabs_y=137;l.top=186;}
        l.cols=3;l.row_h=(v.h-l.top-52)/3;l.cover_h=v.landscape?l.row_h-10:l.row_h-72;
    }
    return l;
}

static const char *shelf_style_label(int style){
    static const char *const names[]={"封面","图文","紧凑","画廊","聚焦"};
    return names[style>=0&&style<SHELF_VIEW_COUNT?style:SHELF_VIEW_DETAILS];
}
static bool shelf_grid_style(void){return shelf_style()==SHELF_VIEW_COVERS||shelf_style()==SHELF_VIEW_GALLERY;}
static reader_shelf_layout_t shelf_grid_layout(void){
    reader_shelf_layout_t l=shelf_layout();
    if(shelf_style()==SHELF_VIEW_GALLERY){
        l.cols=ps_view().landscape?4:2;l.row_h=ps_view().h-l.top-52;
        l.cover_h=l.row_h-(ps_view().landscape?54:80);
    }
    return l;
}
static int shelf_list_height(void){return rp.list==SHELF_VIEW_COMPACT?48:ps_view().landscape?64:72;}
static int shelf_list_cols(void){return ps_view().landscape?2:1;}
static int shelf_first_page_size(void){
    if(shelf_style()==SHELF_VIEW_COVERS)return 6;
    if(shelf_style()==SHELF_VIEW_FOCUS)return 1;
    reader_shelf_layout_t l=shelf_grid_layout();int available=ps_view().h-l.top-52;
    int rows=available/(shelf_grid_style()?l.row_h:shelf_list_height());if(rows<1)rows=1;
    return rows*(shelf_grid_style()?l.cols:shelf_list_cols());
}
static int shelf_page_size_at(int page){return shelf_style()==SHELF_VIEW_COVERS&&page>0?9:shelf_first_page_size();}
static int shelf_page_size(void){return shelf_page_size_at(shelf_scroll);}
static int shelf_page_start(int page){return shelf_style()==SHELF_VIEW_COVERS&&page>0?6+(page-1)*9:page*shelf_first_page_size();}
static int shelf_page_count(int count){
    int size=shelf_first_page_size();
    if(shelf_style()==SHELF_VIEW_COVERS)return count>size?1+(count-size+8)/9:1;
    return count?(count+size-1)/size:1;
}

typedef struct {int x,y,w,h,star_x,star_y;} shelf_item_rect_t;
// 绘制与触摸共用书本和收藏几何，避免换样式后命中仍停留在旧位置。
// Share book/star geometry between painting and touch so style changes never retain stale targets.
static shelf_item_rect_t shelf_item_rect(int index){
    ps_view_t v=ps_view();reader_shelf_layout_t l=shelf_layout();shelf_item_rect_t r={0};
    if(shelf_style()==SHELF_VIEW_FOCUS){
        r=(shelf_item_rect_t){.x=16,.y=l.top,.w=v.w-32,.h=v.h-l.top-52};
        r.star_x=r.x+r.w-48;r.star_y=r.y+r.h-46;
    }else if(shelf_grid_style()){
        l=shelf_grid_layout();r.w=(v.w-32-(l.cols-1)*8)/l.cols;r.h=l.row_h;
        r.x=16+(index%l.cols)*(r.w+8);r.y=l.top+(index/l.cols)*l.row_h;
        r.star_x=r.x+r.w-44;r.star_y=r.y+(shelf_cover_grid_more()?(v.landscape?8:r.h-44):shelf_style()==SHELF_VIEW_GALLERY?r.h-44:l.cover_h+20);
    }else{
        int cols=shelf_list_cols();r.w=(v.w-32-(cols-1)*16)/cols;r.h=shelf_list_height()-2;
        r.x=16+(index%cols)*(r.w+16);r.y=l.top+(index/cols)*shelf_list_height();
        r.star_x=r.x+r.w-44;r.star_y=r.y+(shelf_style()==SHELF_VIEW_COMPACT?0:8);
    }
    return r;
}

static shelf_item_rect_t shelf_picker_rect(int index){
    ps_view_t v=ps_view();int w=v.landscape?456:328,x=(v.w-w)/2,y=(v.h-238)/2;
    if(index<0)return(shelf_item_rect_t){.x=x,.y=y,.w=w,.h=238};
    int cell=(w-36)/2;
    return(shelf_item_rect_t){.x=x+12+(index%2)*(cell+12),.y=y+54+(index/2)*56,.w=cell,.h=48};
}

static void shelf_picker_render(uint8_t *fb){
    if(!shelf_style_picker)return;
    shelf_item_rect_t r=shelf_picker_rect(-1);
    ps_box(fb,r.x-2,r.y-2,r.w+4,r.h+4,14,PS_THEME_BORDER,true);
    ps_box(fb,r.x,r.y,r.w,r.h,12,PS_THEME_SURFACE,true);
    ps_text(fb,r.x+16,r.y+16,18,"书架样式",PS_THEME_INK);
    for(int i=0;i<SHELF_VIEW_COUNT;i++){
        shelf_item_rect_t cell=shelf_picker_rect(i);
        ps_button(fb,cell.x,cell.y,cell.w,cell.h,shelf_style_label(i),rp.list==i);
    }
    ps_text(fb,r.x+16,r.y+r.h-14,9,"轻点空白处关闭",PS_THEME_SECONDARY);
}

static void shelf_name_text(uint8_t *fb,int x,int y,int w,int size,const char *name,int gray){
    char label[288];ri_copy(label,sizeof(label),name);
    int weight=ttf_get_weight();ttf_set_weight(weight<600?600:weight);
    int pixels=(size<16?size+4:size<20?20:size)*19/10,limit=w*19/10;
    if(ttf_text_width_px(pixels,label)>limit){
        size_t n=strlen(label);
        do{u8back(label);n=strlen(label);memcpy(label+n,"…",4);}while(n&&ttf_text_width_px(pixels,label)>limit&&(label[n]=0,true));
    }
    ttf_set_weight(weight);ps_text(fb,x,y,size,label,gray);
}

static void shelf_open_choice_render(uint8_t *fb){
    if(!shelf_open_choice)return;
    shelf_open_choice_layout_t r=shelf_open_choice_layout();
    ps_box(fb,r.panel.x+4,r.panel.y+5,r.panel.width,r.panel.height,16,PS_THEME_BORDER,true);
    ps_box(fb,r.panel.x,r.panel.y,r.panel.width,r.panel.height,16,PS_THEME_SURFACE,true);
    ps_text(fb,r.panel.x+16,r.panel.y+14,9,"打开方式",PS_THEME_SECONDARY);
    ps_text(fb,r.panel.x+16,r.panel.y+31,18,"选择这本书的打开方式",PS_THEME_INK);
    ps_wrap(fb,r.panel.x+16,r.panel.y+60,r.panel.width-32,9,3,
            "你刚刚离开双机阅读。单侧打开不会切换另一台；双设备打开会同步左右页和当前进度。",
            PS_THEME_SECONDARY);
    shelf_name_text(fb,r.panel.x+16,r.panel.y+96,r.panel.width-32,12,shelf_open_choice_name,PS_THEME_INK);

    EpdRect options[2]={r.single,r.dual};
    const char *titles[2]={"仅在本机打开","两台一起打开"};
    const char *details[2]={"独立阅读 · 另一台保持当前状态","双屏联动 · 按主设备进度继续"};
    for(int i=0;i<2;i++){
        bool focused=shelf_open_choice_focus==i;
        ps_box(fb,options[i].x,options[i].y,options[i].width,options[i].height,12,
               focused?PS_THEME_SELECTED:PS_THEME_INSET,true);
        if(focused)ps_box(fb,options[i].x+3,options[i].y+3,options[i].width-6,options[i].height-6,10,
                          PS_THEME_SURFACE,true);
        ps_text(fb,options[i].x+14,options[i].y+14,15,titles[i],PS_THEME_INK);
        ps_wrap(fb,options[i].x+14,options[i].y+42,options[i].width-28,9,2,details[i],PS_THEME_SECONDARY);
        ps_text(fb,options[i].x+14,options[i].y+options[i].height-22,9,
                focused?"当前选择":"轻点选择",focused?PS_THEME_INK:PS_THEME_SECONDARY);
    }
    ps_button(fb,r.cancel.x,r.cancel.y,r.cancel.width,r.cancel.height,"取消",false);
}

static bool shelf_open_choice_key(int key){
    if(!shelf_open_choice)return false;
    if(key==-1){shelf_open_choice_close();return true;}
    if(key==0){shelf_open_choice_focus=0;return true;}
    if(key==2){shelf_open_choice_focus=1;return true;}
    if(key==1){shelf_open_choice_execute(shelf_open_choice_focus==1);return true;}
    return true;
}

static void shelf_file_cover(uint8_t *fb,const reader_file_t *book,int x,int y,int w,int h){
    // 封面阴影留在自己的格子内；真实图像仍保持比例，只从已准备的 RAM 取像素。
    // Keep paper shadows inside each cell; real covers preserve aspect ratio and only read prepared RAM.
    book_cover_view_t cover={0};
    bool real=shelf_cover_display_get(book->path,&cover)&&cover.pixels&&cover.width&&cover.height;
    int pw=w-3,ph=h-3;
    if(real){
        int fitted=(int)((uint32_t)cover.height*pw/cover.width);
        if(fitted>ph)pw=(int)((uint32_t)cover.width*ph/cover.height);
        else ph=fitted;
    }else if(pw>ph*2/3)pw=ph*2/3;
    if(pw<2)pw=2;
    if(ph<2)ph=2;
    int bx=x+(w-pw-2)/2,by=y+(h-ph-2)/2;
    ps_box(fb,bx+2,by+3,pw,ph,2,PS_THEME_BORDER,true);
    ps_box(fb,bx,by,pw,ph,2,real?PS_THEME_SURFACE:PS_THEME_INSET,true);
    if(real){
        EpdRect target=ps_rect(bx+1,by+1,pw-2,ph-2);int dw=target.width,dh=target.height;
        for(int row=0;row<dh;row++){
            unsigned sy=(unsigned)((uint32_t)row*cover.height/dh);
            for(int col=0;col<dw;col++){
                unsigned sx=(unsigned)((uint32_t)col*cover.width/dw);
                epd_draw_pixel(target.x+col,target.y+row,cover.pixels[sy*cover.width+sx],fb);
            }
        }
        return;
    }
    ps_fill(fb,bx+4,by+5,2,ph-10,11);
    if(pw<60){ps_icon(fb,bx+(pw-24)/2,by+(ph-24)/2,24,PI_BOOK,PS_THEME_SECONDARY);return;}
    if(ph<50){ps_text(fb,bx+12,by+10,9,book->epub?"EPUB":"TXT",PS_THEME_SECONDARY);return;}
    ps_text(fb,bx+12,by+9,8,book->epub?"EPUB":"TXT",PS_THEME_SECONDARY);
    ps_wrap(fb,bx+12,by+(ph>80?40:28),pw-24,ph>80?15:12,ph>80?2:1,book->name,PS_THEME_INK);
    if(ph>80)ps_text(fb,bx+12,by+ph-27,9,book->is_flash?"内置存储":"TF 卡",PS_THEME_SECONDARY);
}

// 只准备当前可见封面，续读仅在其可见时准备；单本完成或无封面不触发整页刷新。
// Prepare visible covers and continuation only when shown; a single result or missing cover never refreshes the page.
static bool shelf_cover_ready(const char *path){
    if(book_cover_known(path))return true;
    for(unsigned i=0;i<shelf_cover_attempts;i++)if(!strcmp(path,shelf_cover_attempted[i]))return true;
    return false;
}
static int shelf_cover_pending(reader_file_t *files,int count,const uint16_t *ids,int visible,const char *last_path,int start,int limit,bool continuation){
    int continued=continuation?shelf_model_continue(files,count,last_path):-1;
    if(continued>=0&&files[continued].epub&&!shelf_cover_ready(files[continued].path))return continued;
    for(int j=0;j<limit&&start+j<visible;j++){
        int id=ids[start+j];
        if(files[id].epub&&!shelf_cover_ready(files[id].path))return id;
    }
    return -1;
}
static PICO_READER_COLD bool shelf_cover_prepare_visible(void){
    reader_file_t *files=shelf_stage_active?shelf_stage_files:shelf_files;
    const uint16_t *ids=shelf_stage_active?shelf_stage_ids:shelf_file_ids;
    int count=shelf_stage_active?shelf_stage_count:shelf_file_count,visible=shelf_stage_active?shelf_stage_visible:shelf_file_visible;
    const char *last_path=shelf_stage_active?shelf_stage_last_path:shelf_last_path;
    if(!files||shelf_scanning||shelf_scan_pending)return false;
    if(shelf_style()==SHELF_VIEW_COMPACT){shelf_cover_signature=0;shelf_cover_changed=false;shelf_cover_batch_done=true;return false;}
    int page=ri_clamp(shelf_scroll,0,shelf_page_count(visible)-1),size=shelf_page_size_at(page),start=shelf_page_start(page);
    int limit=size<SHELF_COVER_VISIBLE?size:SHELF_COVER_VISIBLE;
    bool continuation=!ps_dual_has_reading()&&(shelf_style()!=SHELF_VIEW_COVERS||page==0);
    ps_view_t v=ps_view();
    uint64_t signature=UINT64_C(14695981039346656037);
    int geometry[]={v.w,v.h,shelf_style(),start,limit};
    for(size_t i=0;i<sizeof(geometry)/sizeof(geometry[0]);i++)signature=(signature^(unsigned)geometry[i])*UINT64_C(1099511628211);
    for(int j=0;j<limit&&start+j<visible;j++){
        const unsigned char *path=(const unsigned char*)files[ids[start+j]].path;
        do{signature=(signature^*path)*UINT64_C(1099511628211);}while(*path++);
    }
    signature|=1;
    if(signature!=shelf_cover_signature){
        shelf_cover_signature=signature;shelf_cover_changed=shelf_cover_batch_done=false;shelf_cover_attempts=0;
        // 保留本页已有缓存，避免逐本加载时淘汰同页较早缓存的封面；known快路径不读卡。
        // Retain existing page entries before filling missing ones; the known fast path never reads storage.
        int continued=continuation?shelf_model_continue(files,count,last_path):-1;
        if(continued>=0&&book_cover_known(files[continued].path))(void)book_cover_prepare(files[continued].path);
        for(int j=0;j<limit&&start+j<visible;j++)if(book_cover_known(files[ids[start+j]].path))(void)book_cover_prepare(files[ids[start+j]].path);
        if(shelf_cover_next_ms<ps_now()+200)shelf_cover_next_ms=ps_now()+200;
    }
    if(shelf_cover_batch_done)return false;
    int pending=shelf_cover_pending(files,count,ids,visible,last_path,start,limit,continuation);
    if(pending>=0&&ps_now()<shelf_cover_next_ms)return false;
    if(pending>=0&&shelf_cover_attempts<SHELF_COVER_VISIBLE+1){
        book_cover_view_t before={0},after={0};
        bool had=book_cover_get(files[pending].path,&before);
        ri_copy(shelf_cover_attempted[shelf_cover_attempts++],BOOK_STORE_PATH_MAX,files[pending].path);
        (void)book_cover_prepare(files[pending].path);
        bool has=book_cover_get(files[pending].path,&after);
        if(has&&!had)for(int j=0;j<limit&&start+j<visible;j++)
            if(ids[start+j]==pending){shelf_cover_changed=true;break;}
        shelf_cover_next_ms=ps_now()+150;
        if(shelf_cover_attempts<SHELF_COVER_VISIBLE+1&&shelf_cover_pending(files,count,ids,visible,last_path,start,limit,continuation)>=0)return false;
    }
    shelf_cover_batch_done=true;
    bool changed=shelf_cover_changed;shelf_cover_changed=false;
    return changed;
}
static PICO_READER_COLD void shelf_stage_commit(void){
    reader_file_t *previous=shelf_files;shelf_files=shelf_stage_files;shelf_stage_files=previous;
    shelf_file_count=shelf_stage_count;shelf_file_visible=shelf_stage_visible;
    memcpy(shelf_file_ids,shelf_stage_ids,sizeof(shelf_file_ids));
    shelf_scan_limited=shelf_stage_limited;shelf_scan_error=shelf_stage_error;
    shelf_media_generation=shelf_stage_generation;shelf_media_ready=shelf_stage_media_ready;shelf_revision=shelf_stage_revision;
    ri_copy(shelf_last_path,sizeof(shelf_last_path),shelf_stage_last_path);
    shelf_scroll=ri_clamp(shelf_scroll,0,shelf_page_count(shelf_file_visible)-1);
    shelf_ready=true;shelf_stage_active=shelf_commit_pending=false;shelf_cover_changed=false;
    shelf_cover_display_release();
}


static void shelf_star(uint8_t *fb,int x,int y,bool active){
    static const int points[][2]={{9,0},{12,6},{18,7},{13,12},{15,18},{9,15},{3,18},{5,12},{0,7},{6,6},{9,0}};
    x+=13;y+=13;
    if(active)for(int row=0;row<18;row++){
        int hits[10],count=0;
        for(int i=0;i<10;i++){
            int ax=points[i][0],ay=points[i][1],bx=points[i+1][0],by=points[i+1][1];
            if((ay<=row&&by>row)||(by<=row&&ay>row))hits[count++]=ax+(row-ay)*(bx-ax)/(by-ay);
        }
        for(int i=0;i<count;i++)for(int j=i+1;j<count;j++)if(hits[j]<hits[i]){int t=hits[i];hits[i]=hits[j];hits[j]=t;}
        for(int i=0;i+1<count;i+=2)ps_line(fb,x+hits[i],y+row,x+hits[i+1],y+row,PS_THEME_SELECTED);
    }
    for(int i=0;i<10;i++)ps_line(fb,x+points[i][0],y+points[i][1],x+points[i+1][0],y+points[i+1][1],active?PS_THEME_SELECTED:8);
}
static void shelf_ticket_dash(uint8_t *fb,int x,int y,int height){
    for(int at=y+3;at<y+height-5;at+=8)ps_line(fb,x,at,x,at+4,8);
}
static void shelf_ticket_dash_horizontal(uint8_t *fb,int x,int right,int y){
    for(int at=x+3;at<right-5;at+=8)ps_line(fb,at,y,at+4,y,8);
}
static void shelf_hairline(uint8_t *fb,int x0,int y0,int x1,int y1){
    ps_line_thin(fb,x0,y0,x1,y1,0);
}
static void shelf_hairarc(uint8_t *fb,int cx,int cy,int radius,int sx,int sy){
    int x=0,y=radius,error=1-radius;
    int left_x=cx+sx*y,left_y=cy;
    int top_x=cx,top_y=cy+sy*y;
    while(x<=y){
        x++;
        if(error<0)error+=2*x+1;
        else{y--;error+=2*(x-y)+1;}
        if(x>y)break;
        int next_left_x=cx+sx*y,next_left_y=cy+sy*x;
        int next_top_x=cx+sx*x,next_top_y=cy+sy*y;
        shelf_hairline(fb,left_x,left_y,next_left_x,next_left_y);
        shelf_hairline(fb,top_x,top_y,next_top_x,next_top_y);
        left_x=next_left_x;left_y=next_left_y;
        top_x=next_top_x;top_y=next_top_y;
    }
}
static void shelf_hairbox(uint8_t *fb,int x,int y,int width,int height,int radius){
    if(width<2||height<2||radius<0)return;
    int limit=(width<height?width:height)/2;
    if(radius>limit)radius=limit;
    int right=x+width-1,bottom=y+height-1;
    if(radius){
        shelf_hairline(fb,x+radius,y,right-radius,y);
        shelf_hairline(fb,x+radius,bottom,right-radius,bottom);
        shelf_hairline(fb,x,y+radius,x,bottom-radius);
        shelf_hairline(fb,right,y+radius,right,bottom-radius);
        shelf_hairarc(fb,x+radius,y+radius,radius,-1,-1);
        shelf_hairarc(fb,right-radius,y+radius,radius,1,-1);
        shelf_hairarc(fb,x+radius,bottom-radius,radius,-1,1);
        shelf_hairarc(fb,right-radius,bottom-radius,radius,1,1);
    }else{
        shelf_hairline(fb,x,y,right,y);
        shelf_hairline(fb,x,bottom,right,bottom);
        shelf_hairline(fb,x,y,x,bottom);
        shelf_hairline(fb,right,y,right,bottom);
    }
}
static void shelf_open_top(uint8_t *fb,int cx,int y,int radius){
    ps_box(fb,cx-radius,y-radius,radius*2,radius*2,radius,PS_THEME_SURFACE,true);
    shelf_hairarc(fb,cx,y,radius,-1,1);
    shelf_hairarc(fb,cx,y,radius,1,1);
}
static void shelf_open_bottom(uint8_t *fb,int cx,int y,int radius){
    ps_box(fb,cx-radius,y-radius,radius*2,radius*2,radius,PS_THEME_SURFACE,true);
    shelf_hairarc(fb,cx,y,radius,-1,-1);
    shelf_hairarc(fb,cx,y,radius,1,-1);
}
static void shelf_open_left(uint8_t *fb,int x,int cy,int radius){
    ps_box(fb,x-radius,cy-radius,radius*2,radius*2,radius,PS_THEME_SURFACE,true);
    shelf_hairarc(fb,x,cy,radius,1,-1);
    shelf_hairarc(fb,x,cy,radius,1,1);
}
static void shelf_open_right(uint8_t *fb,int x,int cy,int radius){
    ps_box(fb,x-radius,cy-radius,radius*2,radius*2,radius,PS_THEME_SURFACE,true);
    shelf_hairarc(fb,x,cy,radius,-1,-1);
    shelf_hairarc(fb,x,cy,radius,-1,1);
}
static void shelf_card_meta(uint8_t *fb,int x,int y,bool landscape,reader_file_t *book){
    const char *format=book?(book->epub?"EPUB":"TXT"):"BOOK";
    const char *location=book?(book->is_flash?"内置存储":"TF 卡"):"等待传入图书";
    int tag_w=landscape?27:31;
    ps_box(fb,x,y,tag_w,17,5,PS_THEME_BORDER,true);
    ps_center(fb,x+tag_w/2,y+3,8,format,PS_THEME_INK);
    ps_text_fixed(fb,x+tag_w+8,y+4,9,location,PS_THEME_SECONDARY);
}
static void shelf_continue_card(uint8_t *fb,int x,int y,int width,int height,bool landscape,
                                const char *title,const char *position,reader_file_t *book){
    uint8_t style=ps_shelf_card_style();
    int bottom=y+height-3;
    char pct[12];
    if(book)snprintf(pct,sizeof(pct),"%u%%",book->pct);
    else ri_copy(pct,sizeof(pct),"--");
    if(style==PS_SHELF_CARD_DEFAULT){
        const int tab=24,split=x+width-70,stub_center=split+35;
        ps_box(fb,x+2,y+3,width,height-2,9,10,true);
        ps_box(fb,x,y,width,height-3,9,PS_THEME_SURFACE,true);
        ps_box(fb,split+3,y+3,width-(split-x)-5,height-8,7,PS_THEME_INSET,true);
        shelf_hairbox(fb,x,y,width,height-2,9);
        ps_box(fb,x+3,y+4,tab-2,height-10,5,PS_THEME_INSET,true);
        shelf_hairline(fb,x+tab,y+12,x+tab,bottom-12);
        shelf_open_top(fb,x+tab,y,4);shelf_open_bottom(fb,x+tab,bottom,4);
        shelf_open_top(fb,split,y,5);shelf_open_bottom(fb,split,bottom,5);
        shelf_ticket_dash(fb,split,y+7,bottom-y-12);
        for(int row=0;row<14;row++)ps_fill(fb,x+6,y+height/2-7+row,row<=6?row+1:14-row,1,PS_THEME_SURFACE);
        shelf_name_text(fb,x+tab+12,y+(landscape?8:12),split-x-tab-22,landscape?20:24,title,PS_THEME_INK);
        shelf_card_meta(fb,x+tab+12,y+(landscape?32:42),landscape,book);
        int bx=x+tab+12,by=bottom-(landscape?24:29),bw=split-bx-12;
        if(bw>8){ps_box(fb,bx,by,bw,5,2,PS_THEME_BORDER,true);if(book&&book->pct)ps_box(fb,bx,by,bw*book->pct/100,5,2,PS_THEME_INK,true);}
        ps_text_fixed(fb,bx,bottom-(landscape?16:20),9,book?"已读进度 · 点击继续":"点击继续",PS_THEME_SECONDARY);
        ps_center(fb,stub_center,y+7,9,"进度",PS_THEME_SECONDARY);
        ps_center(fb,stub_center,y+(landscape?31:37),landscape?16:19,pct,PS_THEME_INK);
        ps_center(fb,stub_center,bottom-14,8,"续读",PS_THEME_SECONDARY);
        return;
    }
    ps_box(fb,x+2,y+3,width,height-2,9,10,true);
    ps_box(fb,x,y,width,height-3,9,PS_THEME_SURFACE,true);
    if(style==PS_SHELF_CARD_TRAIN){
        const int split=bottom-28;
        ps_box(fb,x+3,split+1,width-6,bottom-split-2,5,PS_THEME_INSET,true);
        shelf_hairbox(fb,x,y,width,height-2,9);
        shelf_open_left(fb,x,split,5);shelf_open_right(fb,x+width,split,5);
        shelf_ticket_dash_horizontal(fb,x+7,x+width-7,split);
        shelf_name_text(fb,x+12,y+(landscape?8:12),width-24,landscape?20:26,title,PS_THEME_INK);
        shelf_card_meta(fb,x+12,y+(landscape?31:44),landscape,book);
        ps_text_fixed(fb,x+12,split+7,9,"已读进度 · 点击继续",PS_THEME_SECONDARY);
        ps_center(fb,x+width-35,split+5,landscape?12:14,pct,PS_THEME_INK);
        return;
    }
    int split=x+(width*76)/100,stub_center=split+(x+width-split)/2;
    ps_box(fb,split+3,y+3,width-(split-x)-5,height-8,7,PS_THEME_INSET,true);
    if(style==PS_SHELF_CARD_CINEMA){
        const int cut=landscape?12:15;
        for(int row=0;row<cut;row++){int span=cut-row;ps_fill(fb,x+width-span,y+row,span+3,1,PS_THEME_SURFACE);}
        for(int row=0;row<cut;row++){int span=cut-row;ps_fill(fb,x,bottom-row,span+3,1,PS_THEME_SURFACE);}
        shelf_hairline(fb,x+width-cut,y,x+width,y+cut);
        shelf_hairline(fb,x,bottom-cut,x+cut,bottom);
        shelf_hairline(fb,x,y,x+width-cut,y);
        shelf_hairline(fb,x+width,y+cut,x+width,bottom);
        shelf_hairline(fb,x+cut,bottom,x+width,bottom);
        shelf_hairline(fb,x,y,x,bottom-cut);
        shelf_open_top(fb,split,y,5);shelf_open_bottom(fb,split,bottom,5);
    }else{
        shelf_hairbox(fb,x,y,width,height-2,9);
        shelf_open_top(fb,split,y,5);shelf_open_bottom(fb,split,bottom,5);
    }
    shelf_ticket_dash(fb,split,y+7,bottom-y-12);
    const char *route="按上次位置继续阅读",*stub="进度";
    shelf_name_text(fb,x+12,y+(landscape?14:18),split-x-24,landscape?23:27,title,PS_THEME_INK);
    shelf_card_meta(fb,x+12,y+(landscape?40:55),landscape,book);
    ps_text_fixed(fb,x+12,y+(landscape?61:77),9,route,PS_THEME_SECONDARY);
    ps_center(fb,stub_center,y+7,9,stub,PS_THEME_SECONDARY);
    if(style==PS_SHELF_CARD_CINEMA){
        int radius=landscape?17:20,cx=stub_center,cy=y+height/2+3;
        ps_box(fb,cx-radius,cy-radius,radius*2,radius*2,radius,PS_THEME_SURFACE,true);
        shelf_hairarc(fb,cx,cy,radius,1,1);shelf_hairarc(fb,cx,cy,radius,-1,1);
        shelf_hairarc(fb,cx,cy,radius,1,-1);shelf_hairarc(fb,cx,cy,radius,-1,-1);
        ps_center(fb,cx,cy-(landscape?5:6),landscape?11:13,pct,PS_THEME_INK);
    }else{
        ps_center(fb,stub_center,y+(landscape?31:36),landscape?15:18,pct,PS_THEME_INK);
    }
}
static void shelf_render(uint8_t *fb){
    ps_view_t v=ps_view();reader_shelf_layout_t l=shelf_layout();
    bool dual_progress=ps_dual_secondary_shelf();
    bool dual_return=ps_dual_has_reading();
    bool loading=!shelf_ready&&(shelf_scan_pending||shelf_scanning||shelf_commit_pending);
    ps_tabs(fb,0);
    ps_text(fb,16,l.title_y,v.landscape?25:28,"我的书架",PS_THEME_INK);
    ps_button(fb,l.record_x,l.control_y,64,34,"笔记",false);
    ps_icon(fb,l.settings_x+6,l.control_y+7,20,PI_SLIDERS,shelf_manage?1:5);
    ps_icon(fb,l.search_x+9,l.search_y+7,20,PI_SEARCH,shelf_search[0]?1:5);
    int cw=l.banner_w,cx=l.banner_x;
    char title[288],position[128];int continued=loading||dual_return?-1:shelf_file_continue();
    if(shelf_manage&&dual_progress){
        ps_text(fb,l.banner_x+12,l.banner_y+8,14,"双机书架",3);
        ps_text(fb,l.banner_x+12,l.banner_y+34,10,"阅读进度由主设备同步",PS_THEME_SECONDARY);
        int bw=l.banner_w-20;
        ps_button(fb,l.banner_x+10,l.banner_y+l.banner_h-46,bw,38,"完成",false);
    }else if(shelf_manage){
        ps_box(fb,cx,l.banner_y+2,cw,l.banner_h-2,12,PS_THEME_BORDER,true);
        ps_box(fb,cx,l.banner_y,cw,l.banner_h-3,12,PS_THEME_SURFACE,true);
    }
    if(shelf_manage){
        ps_text(fb,l.banner_x+12,l.banner_y+8,14,"书架管理",3);
        int bw=(l.banner_w-30)/2;
        ps_button(fb,l.banner_x+10,l.banner_y+l.banner_h-46,bw,38,rp.sort?"按书名排序":"按最近阅读",false);
        ps_button(fb,l.banner_x+20+bw,l.banner_y+l.banner_h-46,bw,38,"完成",false);
    }else if(dual_return&&!shelf_cover_grid_more()){
        ps_box(fb,cx,l.continue_y+2,cw,l.continue_h-2,12,PS_THEME_SELECTED,true);
        ps_box(fb,cx,l.continue_y,cw,l.continue_h-3,12,PS_THEME_SURFACE,true);
        ps_text(fb,cx+14,l.continue_y+9,10,"双机阅读",PS_THEME_SECONDARY);
        shelf_name_text(fb,cx+14,l.continue_y+31,cw-28,v.landscape?20:24,"返回两台设备的当前双页",PS_THEME_INK);
        ps_text(fb,cx+14,l.continue_y+l.continue_h-24,10,"主设备左页 · 从设备右页 · 点击继续",PS_THEME_SECONDARY);
    }else if(!dual_return&&!shelf_cover_grid_more()){
        if(continued>=0){
            reader_file_t *book=&shelf_files[continued];ri_copy(title,sizeof(title),book->name);
            snprintf(position,sizeof(position),"%s · %s · %s %u%%",book->is_flash?"内置存储":"TF 卡",book->epub?"EPUB":"TXT",book->started?"已读":"未读",book->pct);
        }else{
            ri_copy(title,sizeof(title),loading?"正在读取本机书库":"传入第一本图书");
            ri_copy(position,sizeof(position),"TF 卡 / Wi-Fi / USB 传书");
        }
        shelf_continue_card(fb,cx,l.continue_y,cw,l.continue_h,v.landscape,title,position,
                            continued>=0?&shelf_files[continued]:NULL);
    }
    const char *labels[]={"全部","在读","收藏"};const int filters[]={0,2,1};int tw=l.tabs_w/3;
    ps_box(fb,l.tabs_x,l.tabs_y,l.tabs_w-4,34,9,PS_THEME_INSET,true);
    for(int i=0;i<3;i++){
        if(filters[i]==shelf_filter)ps_box(fb,l.tabs_x+i*tw+2,l.tabs_y+2,tw-7,30,7,PS_THEME_SURFACE,true);
        ps_center(fb,l.tabs_x+i*tw+(tw-5)/2,l.tabs_y+9,13,labels[i],filters[i]==shelf_filter?PS_THEME_INK:PS_THEME_SECONDARY);
    }
    ps_box(fb,l.style_x,l.tabs_y+1,l.style_w,32,8,shelf_style_picker?PS_THEME_SELECTED:PS_THEME_BORDER,true);
    if(!shelf_style_picker)ps_box(fb,l.style_x+1,l.tabs_y+2,l.style_w-2,30,7,PS_THEME_SURFACE,true);
    ps_icon(fb,l.style_x+9,l.tabs_y+8,18,PI_GRID,shelf_style_picker?PS_THEME_ON_SELECTED:PS_THEME_SECONDARY);
    ps_center(fb,l.style_x+l.style_w/2+8,l.tabs_y+9,14,shelf_style_label(rp.list),shelf_style_picker?PS_THEME_ON_SELECTED:PS_THEME_INK);
    ps_icon(fb,l.refresh_x+9,l.tabs_y+8,20,PI_REFRESH,PS_THEME_SECONDARY);
    int n=loading?0:shelf_file_visible;
    int per_page=shelf_page_size(),total_pages=shelf_page_count(n),page=ri_clamp(shelf_scroll,0,total_pages-1),start=shelf_page_start(page);
    char footer[112];
    if(loading)ri_copy(footer,sizeof(footer),"正在读取本机书库");
    else if(shelf_scan_limited)snprintf(footer,sizeof(footer),"%d/%d 页 · 已达扫描上限 · 点刷新",page+1,total_pages);
    else if(shelf_scan_error)snprintf(footer,sizeof(footer),"%d/%d 页 · 部分目录不可读 · 点刷新",page+1,total_pages);
    else snprintf(footer,sizeof(footer),"%d/%d 页 · %d 本%s",page+1,total_pages,n,total_pages>1?" · 上下翻页":"");
    ps_center(fb,v.w/2,v.h-43,10,footer,PS_THEME_SECONDARY);
    if(loading){ps_center(fb,v.w/2,l.top+32,14,"正在准备书架，请稍候",6);shelf_picker_render(fb);return;}
    if(!n){
        const char *empty=shelf_filter==1?"还没有收藏，轻点图书旁的星标":shelf_filter==2?"还没有开始阅读的图书":"尚未找到 TXT / EPUB 图书";
        ps_center(fb,v.w/2,l.top+32,14,shelf_search[0]?"没有匹配的图书":empty,6);
        if(!v.landscape&&!shelf_filter)ps_wrap(fb,32,l.top+76,v.w-64,12,4,"图书可放在 TF 卡根目录或 books 文件夹。传书完成后点刷新。",6);
        shelf_picker_render(fb);return;
    }
    if(shelf_style()==SHELF_VIEW_FOCUS){
        shelf_item_rect_t r=shelf_item_rect(0);reader_file_t *book=&shelf_files[shelf_file_ids[start]];
        int cover_w=v.landscape?88:136,cover_h=r.h-24,tx=r.x+cover_w+28,tw=r.w-cover_w-44;
        char label[64];snprintf(label,sizeof(label),"%s · %s %u%%",book->epub?"EPUB":"TXT",book->started?"已读":"未读",book->pct);
        ps_box(fb,r.x,r.y,r.w,r.h-2,12,PS_THEME_INSET,true);
        shelf_file_cover(fb,book,r.x+12,r.y+10,cover_w,cover_h);
        ps_text(fb,tx,r.y+14,9,"图书信息",PS_THEME_SECONDARY);
        ps_wrap(fb,tx,r.y+36,tw,v.landscape?22:20,v.landscape?2:6,book->name,PS_THEME_INK);
        ps_text(fb,tx,r.y+r.h-35,10,label,PS_THEME_SECONDARY);
        shelf_star(fb,r.star_x,r.star_y,ps_reader_file_favorite(book->path,false));
    }else if(shelf_style()==SHELF_VIEW_GALLERY){
        for(int j=0;j<per_page&&start+j<n;j++){
            shelf_item_rect_t r=shelf_item_rect(j);reader_file_t *book=&shelf_files[shelf_file_ids[start+j]];
            int cover_h=r.h-(v.landscape?58:100);char label[48];snprintf(label,sizeof(label),"%s · %u%%",book->epub?"EPUB":"TXT",book->pct);
            ps_box(fb,r.x,r.y,r.w,r.h-2,10,PS_THEME_INSET,true);
            shelf_file_cover(fb,book,r.x+8,r.y+8,r.w-16,cover_h);
            if(v.landscape)shelf_name_text(fb,r.x+8,r.y+cover_h+13,r.w-16,12,book->name,PS_THEME_INK);
            else ps_wrap(fb,r.x+10,r.y+cover_h+20,r.w-20,15,2,book->name,PS_THEME_INK);
            ps_text(fb,r.x+10,r.y+r.h-28,9,label,PS_THEME_SECONDARY);
            shelf_star(fb,r.star_x,r.star_y,ps_reader_file_favorite(book->path,false));
        }
    }else if(!shelf_grid_style()){
        int cols=shelf_list_cols(),cell=(v.w-32-(cols-1)*16)/cols;
        for(int j=0;j<per_page&&start+j<n;j++){
            int x=16+(j%cols)*(cell+16),y=l.top+(j/cols)*shelf_list_height();char label[96];
            reader_file_t *book=&shelf_files[shelf_file_ids[start+j]];
            bool compact=shelf_style()==SHELF_VIEW_COMPACT;
            int text_x=x+(compact?0:51);
            if(!compact)shelf_file_cover(fb,book,x,y+2,38,54);
            shelf_name_text(fb,text_x,y+3,cell-(compact?52:99),compact?16:18,book->name,PS_THEME_INK);
            snprintf(label,sizeof(label),"%s · %s %u%%",book->epub?"EPUB":"TXT",book->started?"已读":"未读",book->pct);
            ps_text(fb,text_x,y+(compact?27:29),10,label,PS_THEME_SECONDARY);
            if(book->started&&!compact){int w=cell-108;ps_box(fb,x+51,y+51,w,3,1,PS_THEME_BORDER,true);if(book->pct)ps_box(fb,x+51,y+51,w*book->pct/100,3,1,PS_THEME_SECONDARY,true);}
            shelf_star(fb,x+cell-44,y+(compact?0:8),ps_reader_file_favorite(book->path,false));
            ps_line(fb,x+(compact?0:51),y+shelf_list_height()-2,x+cell-8,y+shelf_list_height()-2,13);
        }
    }else{
        int w=(v.w-32-(l.cols-1)*8)/l.cols;
        for(int j=0;j<per_page&&start+j<n;j++){
            int x=16+(j%l.cols)*(w+8),y=l.top+(j/l.cols)*l.row_h;char label[48];const char *name;
            reader_file_t *book=&shelf_files[shelf_file_ids[start+j]];name=book->name;snprintf(label,sizeof(label),"%s · %u%%",book->epub?"EPUB":"TXT",book->pct);
            if(shelf_cover_grid_more()&&v.landscape){
                shelf_file_cover(fb,book,x,y+3,36,l.cover_h);
                shelf_name_text(fb,x+44,y+5,w-88,12,name,PS_THEME_INK);
                ps_text(fb,x+44,y+34,9,label,PS_THEME_SECONDARY);
            }else{
                shelf_file_cover(fb,book,x,y,w,l.cover_h);
                shelf_name_text(fb,x,y+l.cover_h+7,w,12,name,PS_THEME_INK);
                ps_text(fb,x,y+l.cover_h+36,9,label,PS_THEME_SECONDARY);
                if(dual_progress){
                    int bar_w=w-16,bar_y=y+l.cover_h+51;
                    ps_box(fb,x+8,bar_y,bar_w,4,2,PS_THEME_BORDER,true);
                    if(book->started&&book->pct)
                        ps_box(fb,x+8,bar_y,bar_w*book->pct/100,4,2,PS_THEME_SECONDARY,true);
                }
            }
            shelf_item_rect_t r=shelf_item_rect(j);
            shelf_star(fb,r.star_x,r.star_y,ps_reader_file_favorite(book->path,false));
        }
    }
    shelf_open_choice_render(fb);
    shelf_picker_render(fb);
}

static size_t page_end(void){
    return current_page+1<page_count?pages[current_page+1]:scan_offset;
}

static void line_highlights(uint8_t *fb,size_t off,size_t end,int y){
    for(int pass=0;pass<2;pass++){
        uint32_t count=pass?1:rr?rr->count:0;
        for(uint32_t i=0;i<count;i++){
            size_t a,b;
            if(pass){
                if(!selection_active)continue;
                a=selection_start;
                b=selection_end;
            }
            else{
                reader_record_t *n=rr->item[i];
                if(n->book!=active_book||n->type==REC_BOOKMARK)continue;
                a=n->offset;
                b=n->end;
            }
            if(a>=end||b<=off)continue;
            a=a>off?a:off;
            b=b<end?b:end;
            int before=0,selected=0;
            for(size_t at=off;at<b;){
                size_t step=u8step(rb[active_book].text+at,rb[active_book].bytes-at);
                if(!step)break;
                char c[5];
                memcpy(c,rb[active_book].text+at,step);
                c[step]=0;
                int width=ttf_text_width_px(native_size(),c);
                if(at<a)before+=width;
                else selected+=width;
                at+=step;
            }
            int scale=ps_rect(0,0,100,0).width;
            int lx=24+before*100/scale,lw=selected*100/scale;
            ps_fill(fb,lx,y,lw,rp.size+2,pass?11:13);
            ps_line(fb,lx,y+rp.size+1,lx+lw,y+rp.size+1,pass?3:7);
        }
    }

}

static void comic_draw(uint8_t *fb){
#ifdef PICO_READER_HOST_FIXTURES

    ps_view_t v=ps_view();
    int page=rp.books[active_book].comic_page;
    if(!comic_valid){
        ps_center(fb,v.w/2,120,16,"漫画资源校验失败",2);
        return;
    }
    // 为页码与全局返回栏留出独立空间。 / Reserve separate space for progress and the global navigation bar.
    int area_w=v.w-32,area_h=v.h-104;
    int width=area_w,height=width*848/600;
    if(height>area_h){
        height=area_h;
        width=height*600/848;
    }
    width=width*comic_zoom/100;
    height=height*comic_zoom/100;
    int left=(v.w-width)/2+comic_pan_x,top=48+(area_h-height)/2+comic_pan_y;
    const uint8_t *entry=preview_comic_start+12+page*8,*src=preview_comic_start+le32(entry);
    size_t length=le32(entry+4),decoded=0;
    for(size_t at=0;at<length;at+=2){
        unsigned run=src[at];
        uint8_t bits=src[at+1];
        for(unsigned i=0;i<run;i++,decoded++){
            if(bits==255)continue;
            int sy=(int)(decoded/75),sx=(int)(decoded%75)*8;
            for(int bit=0;bit<8;bit++)if(!(bits&(0x80>>bit))){
                int x0=left+(sx+bit)*width/600,x1=left+(sx+bit+1)*width/600;
                int y0=top+sy*height/848,y1=top+(sy+1)*height/848;
                x0=ri_clamp(x0,16,v.w-16);
                x1=ri_clamp(x1,16,v.w-16);
                y0=ri_clamp(y0,48,v.h-56);
                y1=ri_clamp(y1,48,v.h-56);
                if(x1>x0&&y1>y0)ps_fill(fb,x0,y0,x1-x0,y1-y0,0);
            }
        }
    }


#else
    (void)fb;
#endif
}

static void tools_render(uint8_t *fb){
    ps_view_t v=ps_view();
    int width=v.landscape?420:v.w-32,x=(v.w-width)/2;
    int top=v.landscape?72:200;
    int height=v.landscape?238:350;
    ps_box(fb,x+2,top+3,width,height,13,PS_THEME_BORDER,true);
    ps_box(fb,x,top,width,height,13,PS_THEME_SURFACE,true);
    ps_line(fb,x+16,top+43,x+width-16,top+43,PS_THEME_BORDER);
    ps_text(fb,x+16,top+15,18,chapter_panel?"目录":font_panel?"图书管理":"阅读工具",2);
    ps_icon(fb,x+width-35,top+14,20,PI_CLOSE,3);
    if(chapter_panel){
        int rows=v.landscape?4:6;
        int count=ri_clamp(chapter_count,0,(int)(sizeof(chapters)/sizeof(chapters[0])));
        int start=count?ri_clamp(chapter_scroll,0,count-1):0;
        for(int i=0;i<rows&&start+i<count;i++)ps_button(fb,x+14,top+48+i*35,width-28,30,chapters[start+i].name,false);
        if(!count)ps_text(fb,x+16,top+60,14,"当前图书暂无目录",6);
        ps_text(fb,x+16,top+(v.landscape?203:310),11,"上下滑动目录 · 点击回到原文位置",6);
        return;
    }
    if(font_panel){
        const char *labels[]={
            rp.books[active_book].favorite?"取消收藏":"收藏图书",rp.books[active_book].done?"标为在读":"标为已读","重置阅读进度"};
        for(int i=0;i<3;i++)ps_button(fb,x+16,top+50+i*46,width-32,36,labels[i],false);
        return;
    }
    const char *labels[]={
        "目录","跳转进度","添加书签","阅读记录","字体与排版","返回书库"};
    int bw=(width-42)/2;
    for(int i=0;i<6;i++)ps_button(fb,x+14+(i%2)*(bw+14),top+49+(i/2)*46,bw,36,labels[i],false);
    if(rb[active_book].comic)ps_button(fb,x+14,top+190,width-28,32,comic_zoom==100?"放大漫画 160%":"恢复整页",false);
    else ps_text(fb,x+16,top+198,12,"长按正文拖选，可划线或写笔记",6);
    if(!v.landscape){
        ps_button(fb,x+14,top+244,width-28,34,"图书收藏与进度管理",false);
        ps_button(fb,x+14,top+291,width-28,34,"关闭工具，继续阅读",false);
    }

}

static void reading_render(uint8_t *fb){
    ps_view_t v=ps_view();
    char footer[96];
    ps_text(fb,24,30,11,rb[active_book].title,7);
    if(rb[active_book].comic){
        comic_draw(fb);
        snprintf(footer,sizeof(footer),"%u / 8 · %d%%%s",rp.books[active_book].comic_page+1,comic_zoom,comic_zoom>100?" · 拖动画面":"");
    }
    else{
        if(layout_failed)ps_wrap(fb,24,100,v.w-48,16,3,"排版失败，请减小字号或重新打开图书。",3);
        else if(seek_pending)ps_text(fb,24,110,16,"正在恢复原文位置…",5);
        else if(page_count){
            size_t off=pages[current_page],end=page_end();
            int y=52;
            char line[512];
            while(off<end){
                bool para;
                size_t next=take_line(off,line,sizeof(line),&para);
                if(next<=off)break;
                if(y+rp.size>v.h-42)break;
                line_highlights(fb,off,next,y);
                ps_body_text(fb,24,y,rp.size,line,1);
                y+=line_cost(line,para);
                off=next;
            }
        }
        snprintf(footer,sizeof(footer),"%u / %u%s · %d%%",(unsigned)current_page+1,(unsigned)page_count,paginated?"":"+",progress_percent(active_book));
    }
    ps_center(fb,v.w/2,v.h-54,10,footer,7);
    if(selection_active&&!tools){
        int w=(v.w-48)/3;
        ps_box(fb,12,v.h-89,v.w-24,48,6,15,true);
        ps_box(fb,12,v.h-89,v.w-24,48,6,9,false);
        ps_button(fb,20,v.h-81,w,31,"划线",false);
        ps_button(fb,24+w,v.h-81,w,31,"写笔记",true);
        ps_button(fb,28+w*2,v.h-81,w,31,"取消",false);
    }
    if(tools)tools_render(fb);

}

static int filtered_records(uint16_t out[RECORD_MAX]){
    int n=0;
    if(rr)for(int i=(int)rr->count-1;i>=0;i--)if(rr->item[i]->type==record_tab&&record_matches(rr->item[i]))out[n++]=i;
    return n;
}

static void records_render(uint8_t *fb){
    ps_view_t v=ps_view();
    ps_heading(fb,"书签与笔记","笔记、划线和书签独立保存，点击记录可回到原文。");
    ps_button(fb,v.w-81,32,65,29,"导出",false);
    const char *labels[]={
        "笔记","划线","书签"};
    int tw=(v.w-32)/3;
    ps_box(fb,16,121,v.w-37,32,9,PS_THEME_INSET,true);
    for(int i=0;i<3;i++){
        if(record_tab==i)ps_box(fb,18+i*tw,123,tw-9,28,7,PS_THEME_SURFACE,true);
        ps_center(fb,16+i*tw+(tw-5)/2,127,16,labels[i],record_tab==i?PS_THEME_INK:PS_THEME_SECONDARY);
    }
    if(selected_record>=0&&rr&&selected_record<(int)rr->count){
        reader_record_t *n=rr->item[selected_record];
        ps_text(fb,16,169,18,record_title(n),2);
        int note_y=v.landscape?225:282;
        ps_box(fb,16,194,v.w-32,v.landscape?29:80,8,PS_THEME_INSET,true);
        ps_wrap(fb,24,201,v.w-48,14,v.landscape?1:3,n->quote[0]?n->quote:"此处的阅读书签",PS_THEME_SECONDARY);
        int hint_y=v.h-105,ih=n->ink_count?(v.landscape?28:95):0;
        int ink_y=v.landscape?222:hint_y-ih-6;
        int note_bottom=!v.landscape&&ih?ink_y-6:hint_y-8;
        int note_width=v.landscape&&ih?(v.w-48)/2:v.w-32;
        int lines=(note_bottom-note_y)/26;
        if(lines<1)lines=1;
        size_t offset=detail_scroll<strlen(n->text)?detail_scroll:0;
        ps_wrap(fb,16,note_y,note_width,15,lines,n->text+offset,2);
        if(n->ink_count)draw_ink(fb,n->ink,n->ink_count,v.landscape?v.w/2+8:20,ink_y,v.landscape?(v.w-48)/2:v.w-40,ih);
        ps_text(fb,16,hint_y,9,"上下滑动查看长笔记",7);
        ps_button(fb,16,v.h-84,(v.w-44)/2,35,"返回原文",true);
        ps_button(fb,28+(v.w-44)/2,v.h-84,(v.w-44)/2,35,n->type==REC_HIGHLIGHT?"取消划线":n->type==REC_NOTE?"删除批注":"移除书签",false);
        return;
    }
    uint16_t ids[RECORD_MAX];int n=filtered_records(ids),rows=(v.h-174)/105,start=ri_clamp(record_scroll,0,n>rows?n-rows:0);
    if(!n){
        ps_center(fb,v.w/2,220,15,"还没有这类阅读记录",6);
        return;
    }
    for(int j=0;j<rows&&start+j<n;j++){
        reader_record_t *r=rr->item[ids[start+j]];
        int y=170+j*105;
        ps_box(fb,17,y+2,v.w-34,93,10,PS_THEME_BORDER,true);
        ps_box(fb,16,y,v.w-32,93,10,PS_THEME_SURFACE,true);
        ps_text(fb,28,y+9,11,record_title(r),PS_THEME_SECONDARY);
        ps_wrap(fb,28,y+30,v.w-56,14,2,r->quote[0]?r->quote:r->text[0]?r->text:r->ink_count?"手写笔迹笔记":"阅读书签",PS_THEME_INK);
        char at[64];
        snprintf(at,sizeof(at),"原文位置 %lu%s",(unsigned long)r->offset,r->ink_count?" · 含笔迹":"");
        ps_text(fb,28,y+74,10,at,PS_THEME_SECONDARY);
    }

}

static PICO_READER_COLD void reading_settings_render(uint8_t *fb){
    ps_view_t v=ps_view();
    ps_heading(fb,"字体与排版","使用设备真实字体排版，字号、行距和方向变化保留原文位置。");
    int base=v.landscape?99:123;
    char size[48];
    snprintf(size,sizeof(size),"正文字号  %u",rp.size);
    ps_text(fb,18,base+8,16,size,2);
    ps_button(fb,v.w-110,base,40,34,"−",false);
    ps_button(fb,v.w-60,base,40,34,"+",false);
    ps_text(fb,18,base+57,14,"行距",3);
    const char *spacing[]={
        "1.7","1.9","2.1","2.3"};
    int bw=(v.w-112)/4;
    for(int i=0;i<4;i++)ps_button(fb,82+i*bw,base+49,bw-5,33,spacing[i],rp.spacing==i);
    ps_button(fb,18,base+99,(v.w-44)/2,34,"常规字重",!rp.weight);
    ps_button(fb,26+(v.w-44)/2,base+99,(v.w-44)/2,34,"加粗字重",rp.weight);
    ps_text(fb,18,base+150,14,"当前字体",3);
    ps_text(fb,101,base+150,13,reader_font_name(),5);
    const char *names[]={"无衬线","宋体","楷体"};
    const char *paths[]={TTF_FONT_BUILTIN,TTF_FONT_BUILTIN_SERIF,TTF_FONT_BUILTIN_KAI};
    int fw=(v.w-36)/(v.landscape?4:3);
    for(int i=0;i<3;i++)ps_button(fb,18+i*fw,base+182,fw-5,35,names[i],!strcmp(rp.font,paths[i]));
    if(v.landscape)ps_button(fb,18+3*fw,base+182,fw-5,35,"TF 卡字体",false);
    else ps_button(fb,18,base+228,v.w-36,35,"TF 卡真实字体列表",false);
    if(!v.landscape){
        ps_wrap(fb,18,base+283,v.w-36,12,4,"生僻字与组合字形取决于所选 TTF 的覆盖范围，可在当前图书中切换字体比较显示。",6);
        ps_button(fb,18,v.h-84,v.w-36,35,"输入法与笔记设置",false);
    }

}

static PICO_READER_COLD void input_settings_render(uint8_t *fb){
    ps_view_t v=ps_view();
    char dictionary[96];
    if(ime_dictionary)snprintf(dictionary,sizeof(dictionary),"扩展词库 · %u 条 · 连续拼音与九键",(unsigned)ime_dictionary_count);
    else ri_copy(dictionary,sizeof(dictionary),"内置词库 · 可从 TF 卡加载扩展词库");
    ps_heading(fb,"输入法与笔记",dictionary);
    int cols=v.landscape?2:1,bw=(v.w-32-(cols-1)*12)/cols;
    for(int i=0;i<INPUT_MODE_COUNT;i++)ps_button(fb,16+(i%cols)*(bw+12),124+(i/cols)*48,bw,38,input_names[i],rp.input==input_ids[i]);
    if(!v.landscape)ps_wrap(fb,18,336,v.w-36,13,5,"26键采用手机QWERTY三行错位布局；9键按数字选候选。支持英文、数字与常用符号，记录与草稿独立保存。旧版笔迹附件仍可查看和导出。",6);
    ps_button(fb,16,v.h-84,v.w-32,36,"体验输入，不创建笔记",true);

}

static void import_render(uint8_t *fb){
    ps_view_t v=ps_view();
    ps_heading(fb,"传书与本机图书","连接手机或电脑，将图书与字体传到设备。");
    int cols=v.landscape?2:1,bw=(v.w-32-(cols-1)*12)/cols;
    const char *labels[]={
        "开始传书","打开本机图书","选择 TF 卡字体"};
    for(int i=0;i<3;i++)ps_button(fb,16+(i%cols)*(bw+12),124+(i/cols)*48,bw,38,labels[i],false);
    if(!v.landscape)ps_wrap(fb,18,432,v.w-36,13,7,"支持 TXT、EPUB 图书，以及 TF 卡上的 TTF 字体。图书可放在 TF 卡根目录或 books 文件夹。点“打开本机图书”回到书库阅读。",6);

}

/* ---- 事件与生命周期 / Events and lifecycle ---- */
static void search_done(const char *text){
    ri_copy(shelf_search,sizeof(shelf_search),text);
    shelf_scroll=0;
    shelf_file_rebuild();
}

static void progress_done(const char *text){
    char *end;
    long percent=strtol(text,&end,10);
    if(end==text||*end||percent<0||percent>100){
        ps_toast("请输入0到100的整数");
        return;
    }
    if(rb[active_book].comic){
        rp.books[active_book].comic_page=(uint8_t)(percent*7/100);
        anchor=rp.books[active_book].comic_page;
    }
    else{
        anchor=(size_t)((uint64_t)rb[active_book].bytes*percent/100);
        if(anchor>=rb[active_book].bytes)anchor=rb[active_book].bytes-1;
        while(anchor&&((unsigned char)rb[active_book].text[anchor]&0xc0)==0x80)anchor--;
        seek_pending=true;
        seek_finish();
    }
    remember();

}

static size_t text_position(int x,int y){
    if(!page_count||seek_pending)return anchor;
    size_t off=pages[current_page],end=page_end();
    int py=52;
    char line[512];
    while(off<end){
        bool para;
        size_t next=take_line(off,line,sizeof(line),&para);
        if(next<=off)break;
        if(y<py+line_cost(line,para)){
            int width=0,target=ps_rect(0,0,x>24?x-24:0,0).width;
            size_t at=off;
            while(at<next&&rb[active_book].text[at]!='\n'&&rb[active_book].text[at]!='\r'){
                size_t n=u8step(rb[active_book].text+at,rb[active_book].bytes-at);
                if(!n)break;
                char c[5];
                memcpy(c,rb[active_book].text+at,n);
                c[n]=0;
                int advance=ttf_text_width_px(native_size(),c);
                if(width+advance/2>=target)break;
                width+=advance;
                at+=n;
            }
            return at<next?at:off;
        }
        py+=line_cost(line,para);
        off=next;
    }
    return end;

}

static bool tools_event(const ps_event_t *e){
    ps_view_t v=ps_view();
    int width=v.landscape?420:v.w-32,x=(v.w-width)/2,top=v.landscape?72:200;
    if(chapter_panel&&(e->type==PS_SWIPE_U||e->type==PS_SWIPE_D)){
        int count=ri_clamp(chapter_count,0,(int)(sizeof(chapters)/sizeof(chapters[0])));
        int start=count?ri_clamp(chapter_scroll,0,count-1):0;
        chapter_scroll=count?ri_clamp(start+(e->type==PS_SWIPE_U?3:-3),0,count-1):0;
        return true;
    }
    if(e->type!=PS_TAP)return false;
    int height=v.landscape?238:350;
    if(!ps_hit(e->x,e->y,x,top,width,height)||ps_hit(e->x,e->y,x+width-42,top+6,35,35)){
        tools=chapter_panel=font_panel=false;
        return true;
    }
    if(chapter_panel){
        int rows=v.landscape?4:6;
        int count=ri_clamp(chapter_count,0,(int)(sizeof(chapters)/sizeof(chapters[0])));
        int start=count?ri_clamp(chapter_scroll,0,count-1):0;
        for(int i=0;i<rows&&start+i<count;i++)if(ps_hit(e->x,e->y,x+14,top+48+i*35,width-28,30)){
            anchor=chapters[start+i].offset;
            seek_pending=true;
            seek_finish();
            tools=chapter_panel=false;
            remember();
            return true;
        }
        return true;
    }
    if(font_panel){
        for(int i=0;i<3;i++)if(ps_hit(e->x,e->y,x+16,top+50+i*46,width-32,36)){
            reader_progress_t *p=&rp.books[active_book];
            if(i==0)p->favorite=!p->favorite;
            else if(i==1)p->done=!p->done;
            else{
                p->offset=0;
                p->comic_page=0;
                p->started=0;
                anchor=0;
                current_page=0;
                selection_active=false;
            }
            prefs_save();
            ps_toast(i==0?"收藏状态已更新":i==1?"阅读状态已更新":"阅读进度已重置");
            return true;
        }
        return true;
    }
    int bw=(width-42)/2;
    for(int i=0;i<6;i++)if(ps_hit(e->x,e->y,x+14+(i%2)*(bw+14),top+49+(i/2)*46,bw,36)){
        switch(i){
            case 0:if(rb[active_book].comic)ps_toast("漫画使用左右实体键逐页切换");
            else chapter_panel=true;
            break;
            case 1:ps_edit("跳转进度 0–100%","",3,progress_done);
            input_mode=INPUT_SYMBOL;
            break;
            case 2:add_bookmark();
            break;
            case 3:ps_navigate(PS_RECORDS);
            break;
            case 4:ps_navigate(PS_READING_SETTINGS);
            break;
            case 5:tools=false;
            ps_navigate(PS_SHELF);
            break;
        }
        return true;
    }
    if(rb[active_book].comic&&ps_hit(e->x,e->y,x+14,top+190,width-28,32)){
        comic_zoom=comic_zoom==100?160:100;
        comic_pan_x=comic_pan_y=0;
        tools=false;
        return true;
    }
    if(!v.landscape&&ps_hit(e->x,e->y,x+14,top+244,width-28,34)){
        font_panel=true;
        return true;
    }
    if(!v.landscape&&ps_hit(e->x,e->y,x+14,top+291,width-28,34)){
        tools=false;
        return true;
    }
    return true;

}

static bool reading_event(const ps_event_t *e){
    if(!open_book){ps_navigate(PS_SHELF);return true;}
    ps_view_t v=ps_view();
    if(tools)return tools_event(e);
    if(rb[active_book].comic&&comic_zoom>100){
        if(e->type==PS_PRESS){
            comic_press_x=e->x;
            comic_press_y=e->y;
            comic_old_x=comic_pan_x;
            comic_old_y=comic_pan_y;
            return true;
        }
        if(e->type==PS_MOVE){
            comic_pan_x=ri_clamp(comic_old_x+e->x-comic_press_x,-v.w/2,v.w/2);
            comic_pan_y=ri_clamp(comic_old_y+e->y-comic_press_y,-v.h/2,v.h/2);
            return true;
        }
        if(e->type==PS_SWIPE_L||e->type==PS_SWIPE_R||e->type==PS_SWIPE_U||e->type==PS_SWIPE_D)return true;
    }
    if(e->type==PS_LONG&&!rb[active_book].comic&&!seek_pending){
        selection_start=selection_origin=text_position(e->x,e->y);
        selection_end=selection_start;
        for(int i=0;i<12&&selection_end<rb[active_book].bytes;i++){
            size_t n=u8step(rb[active_book].text+selection_end,rb[active_book].bytes-selection_end);
            if(!n)break;
            selection_end+=n;
        }
        selection_active=selection_end>selection_start;
        selection_drag=true;
        return true;
    }
    if(e->type==PS_MOVE&&selection_drag){
        size_t end=text_position(e->x,e->y);
        selection_start=end<selection_origin?end:selection_origin;
        selection_end=end>selection_origin?end:selection_origin;
        if(selection_end<rb[active_book].bytes)selection_end+=u8step(rb[active_book].text+selection_end,rb[active_book].bytes-selection_end);
        selection_active=selection_end>selection_start;
        return true;
    }
    if(e->type==PS_RELEASE||e->type==PS_CANCEL){
        selection_drag=false;
        return false;
    }
    if(e->type==PS_SWIPE_L){
        if(!selection_active)page_turn(1);
        return true;
    }
    if(e->type==PS_SWIPE_R){
        if(!selection_active)page_turn(-1);
        return true;
    }
    if(e->type!=PS_TAP)return false;
    if(selection_active){
        int w=(v.w-48)/3;
        if(ps_hit(e->x,e->y,20,v.h-81,w,31))save_highlight();
        else if(ps_hit(e->x,e->y,24+w,v.h-81,w,31))new_note();
        else if(ps_hit(e->x,e->y,28+w*2,v.h-81,w,31))selection_active=false;
        return true;
    }
    if(e->y<50){
        tools=true;
        return true;
    }
    return false;

}

static void shelf_feedback_committed(void){if(shelf_stage_active)shelf_stage_cancel(false);shelf_cover_changed=false;}
static bool shelf_event(const ps_event_t *e){
    if(shelf_open_choice){
        if(e->type==PS_TAP){
            shelf_open_choice_layout_t r=shelf_open_choice_layout();
            if(ps_hit(e->x,e->y,r.single.x,r.single.y,r.single.width,r.single.height)){
                shelf_open_choice_execute(false);
            }else if(ps_hit(e->x,e->y,r.dual.x,r.dual.y,r.dual.width,r.dual.height)){
                shelf_open_choice_execute(true);
            }else if(ps_hit(e->x,e->y,r.cancel.x,r.cancel.y,r.cancel.width,r.cancel.height)||
                     !ps_hit(e->x,e->y,r.panel.x,r.panel.y,r.panel.width,r.panel.height)){
                shelf_open_choice_close();
            }
        }
        return true;
    }
    reader_shelf_layout_t l=shelf_layout();int n=shelf_file_visible;
    bool dual_progress=ps_dual_secondary_shelf();
    bool dual_return=ps_dual_has_reading();
    bool loading=!shelf_ready&&(shelf_scan_pending||shelf_scanning||shelf_commit_pending);
    if(e->type==PS_SWIPE_U||e->type==PS_SWIPE_D||e->type==PS_SWIPE_L||e->type==PS_SWIPE_R){if(shelf_style_picker)shelf_style_picker=false;else{shelf_scroll=ri_clamp(shelf_scroll+(e->type==PS_SWIPE_U||e->type==PS_SWIPE_L?1:-1),0,shelf_page_count(n)-1);if(shelf_style()==SHELF_VIEW_COVERS)shelf_manage=false;}shelf_feedback_committed();return true;}
    if(e->type!=PS_TAP&&e->type!=PS_LONG)return false;
    int x=e->x,y=e->y;
    if(shelf_style_picker){
        for(int i=0;i<SHELF_VIEW_COUNT;i++){
            shelf_item_rect_t cell=shelf_picker_rect(i);
            if(ps_hit(x,y,cell.x,cell.y,cell.w,cell.h)){
                rp.list=(uint8_t)i;shelf_scroll=0;shelf_style_picker=false;
                prefs_save();shelf_feedback_committed();return true;
            }
        }
        shelf_style_picker=false;shelf_feedback_committed();return true;
    }
    if(ps_hit(x,y,l.record_x,l.control_y,64,34)){ps_navigate(PS_RECORDS);shelf_feedback_committed();return true;}
    if(ps_hit(x,y,l.settings_x,l.control_y,32,34)){shelf_manage=!shelf_manage;shelf_style_picker=false;shelf_scroll=0;shelf_feedback_committed();return true;}
    if(ps_hit(x,y,l.search_x,l.search_y,l.search_w,36)){ps_edit("搜索本机书名",shelf_search,sizeof(shelf_search)-1,search_done);shelf_feedback_committed();return true;}
    if(shelf_manage){
        int bw=(l.banner_w-30)/2;
        if(dual_progress){
            if(ps_hit(x,y,l.banner_x+10,l.banner_y+l.banner_h-46,l.banner_w-20,38)){
                shelf_manage=false;shelf_feedback_committed();return true;
            }
            return false;
        }
        if(!dual_progress&&ps_hit(x,y,l.banner_x+10,l.banner_y+l.banner_h-46,bw,38)){rp.sort=!rp.sort;if(shelf_file_count>1)qsort(shelf_files,(size_t)shelf_file_count,sizeof(*shelf_files),shelf_file_compare);shelf_file_rebuild();shelf_scroll=0;prefs_save();shelf_feedback_committed();return true;}
        if(ps_hit(x,y,l.banner_x+20+bw,l.banner_y+l.banner_h-46,bw,38)){shelf_manage=false;shelf_feedback_committed();return true;}
    }else if(!loading&&dual_return&&!shelf_cover_grid_more()&&
             ps_hit(x,y,l.banner_x,l.continue_y,l.banner_w,l.continue_h)){
        if(!ps_global_resume_reading())ps_toast("另一台尚未就绪");
        shelf_feedback_committed();return true;
    }else if(!loading&&!shelf_cover_grid_more()&&ps_hit(x,y,l.banner_x,l.continue_y,l.banner_w,l.continue_h)){
        shelf_file_open(shelf_file_continue());shelf_feedback_committed();return true;
    }
    int tw=l.tabs_w/3;const int filters[]={0,2,1};
    for(int i=0;i<3;i++)if(ps_hit(x,y,l.tabs_x+i*tw,l.tabs_y,tw-4,34)){
        shelf_filter=filters[i];shelf_scroll=0;shelf_file_rebuild();shelf_feedback_committed();return true;
    }
    if(ps_hit(x,y,l.style_x,l.tabs_y,l.style_w,36)){
        shelf_style_picker=!shelf_style_picker;shelf_manage=false;shelf_feedback_committed();return true;
    }
    if(ps_hit(x,y,l.refresh_x,l.tabs_y,44,36)){shelf_stage_cancel(true);return false;}
    if(loading)return false;
    int per_page=shelf_page_size(),start=shelf_page_start(ri_clamp(shelf_scroll,0,shelf_page_count(n)-1));
    for(int j=0;j<per_page&&start+j<n;j++){
        shelf_item_rect_t r=shelf_item_rect(j);
        if(ps_hit(x,y,r.x,r.y,r.w,r.h)){
            if(ps_hit(x,y,r.star_x,r.star_y,44,44))ps_reader_file_favorite(shelf_files[shelf_file_ids[start+j]].path,true);
            else shelf_file_open(shelf_file_ids[start+j]);
            shelf_feedback_committed();return true;
        }
    }
    return false;
}

static bool records_event(const ps_event_t *e){
    ps_view_t v=ps_view();
    if(e->type==PS_SWIPE_U||e->type==PS_SWIPE_D){
        if(selected_record>=0&&rr){
            const char *s=rr->item[selected_record]->text;
            size_t length=strlen(s);
            if(e->type==PS_SWIPE_U){
                size_t next=detail_scroll+72;
                if(next<length){
                    while(next<length&&((unsigned char)s[next]&0xc0)==0x80)next++;
                    detail_scroll=next;
                }
            }
            else{
                detail_scroll=detail_scroll>72?detail_scroll-72:0;
                while(detail_scroll&&((unsigned char)s[detail_scroll]&0xc0)==0x80)detail_scroll--;
            }
        }
        else record_scroll=ri_clamp(record_scroll+(e->type==PS_SWIPE_U?1:-1),0,RECORD_MAX-1);
        return true;
    }
    if(e->type!=PS_TAP)return false;
    if(ps_hit(e->x,e->y,v.w-81,32,65,29)){
        export_records();
        return true;
    }
    int tw=(v.w-32)/3;
    for(int i=0;i<3;i++)if(ps_hit(e->x,e->y,16+i*tw,121,tw-5,32)){
        record_tab=i;
        selected_record=-1;
        record_scroll=0;
        return true;
    }
    if(selected_record>=0){
        if(ps_hit(e->x,e->y,16,v.h-84,(v.w-44)/2,35)){
            record_jump(selected_record);
            return true;
        }
        if(ps_hit(e->x,e->y,28+(v.w-44)/2,v.h-84,(v.w-44)/2,35)){
            record_delete();
            return true;
        }
        return false;
    }
    uint16_t ids[RECORD_MAX];int n=filtered_records(ids),rows=(v.h-174)/105,start=ri_clamp(record_scroll,0,n>rows?n-rows:0);
    for(int j=0;j<rows&&start+j<n;j++)if(ps_hit(e->x,e->y,16,170+j*105,v.w-32,95)){
        selected_record=ids[start+j];
        detail_scroll=0;
        return true;
    }
    return false;

}

static bool reading_settings_event(const ps_event_t *e){
    if(e->type!=PS_TAP)return false;
    ps_view_t v=ps_view();
    int x=e->x,y=e->y,base=v.landscape?99:123;
    bool change=false;
    if(ps_hit(x,y,v.w-110,base,40,34)){
        rp.size=(uint8_t)ri_clamp(rp.size-2,READER_SIZE_MIN,READER_SIZE_MAX);
        change=true;
    }
    else if(ps_hit(x,y,v.w-60,base,40,34)){
        rp.size=(uint8_t)ri_clamp(rp.size+2,READER_SIZE_MIN,READER_SIZE_MAX);
        change=true;
    }
    int bw=(v.w-112)/4;
    for(int i=0;i<4;i++)if(ps_hit(x,y,82+i*bw,base+49,bw-5,33)){
        rp.spacing=(uint8_t)i;
        change=true;
    }
    if(ps_hit(x,y,18,base+99,(v.w-44)/2,34)){
        rp.weight=0;
        change=true;
    }
    else if(ps_hit(x,y,26+(v.w-44)/2,base+99,(v.w-44)/2,34)){
        rp.weight=1;
        change=true;
    }
    const char *paths[]={TTF_FONT_BUILTIN,TTF_FONT_BUILTIN_SERIF,TTF_FONT_BUILTIN_KAI};
    int fw=(v.w-36)/(v.landscape?4:3);
    for(int i=0;i<3;i++)if(ps_hit(x,y,18+i*fw,base+182,fw-5,35)){
        ri_copy(rp.font,sizeof(rp.font),paths[i]);
        detached_font[0]=0;
        change=true;
    }
    if(ps_hit(x,y,v.landscape?18+3*fw:18,v.landscape?base+182:base+228,v.landscape?fw-5:v.w-36,35)){
        ps_legacy(&app_font_pick);
        return true;
    }
    if(change){
        ttf_set_weight(400);
        prefs_save();
        return true;
    }
    if(!v.landscape&&ps_hit(x,y,18,v.h-84,v.w-36,35)){
        ps_navigate(PS_INPUT_SETTINGS);
        return true;
    }
    return false;

}

bool ps_reader_event(const ps_event_t *e){
    switch(ps_route()){
        case PS_SHELF:return shelf_event(e);
        case PS_READ:return reading_event(e);
        case PS_RECORDS:return records_event(e);
        case PS_READING_SETTINGS:return reading_settings_event(e);
        case PS_EDITOR:return editor_event(e);
        case PS_INPUT_SETTINGS:{
            if(e->type!=PS_TAP)return false;
            ps_view_t v=ps_view();
            int cols=v.landscape?2:1,bw=(v.w-32-(cols-1)*12)/cols;
            for(int i=0;i<INPUT_MODE_COUNT;i++)if(ps_hit(e->x,e->y,16+(i%cols)*(bw+12),124+(i/cols)*48,bw,38)){
                rp.input=input_ids[i];
                prefs_save();
                return true;
            }
            if(ps_hit(e->x,e->y,16,v.h-84,v.w-32,36)){
                ps_edit("输入法体验","",383,NULL);
                editor_mode(rp.input);
                return true;
            }
            return false;
        }
        case PS_IMPORT:{
            if(e->type!=PS_TAP)return false;
            ps_view_t v=ps_view();
            int cols=v.landscape?2:1,bw=(v.w-32-(cols-1)*12)/cols;
            for(int i=0;i<3;i++)if(ps_hit(e->x,e->y,16+(i%cols)*(bw+12),124+(i/cols)*48,bw,38)){
                if(i==0)ps_navigate(PS_TRANSFER);
                else if(i==1){shelf_scroll=0;shelf_scan_pending=true;ps_navigate(PS_SHELF);}
                else if(i==2)ps_legacy(&app_font_pick);
                return true;
            }
            return false;
        }
        default:return false;
    }

}

bool ps_reader_key(int key){
    if(ps_route()==PS_SHELF&&shelf_open_choice_key(key))return true;
    if(ps_route()==PS_READ&&!open_book){ps_navigate(PS_SHELF);return true;}
    if(key==-1){
        if(ps_route()==PS_READ){
            if(chapter_panel||font_panel){
                chapter_panel=font_panel=false;
                return true;
            }
            if(tools){
                tools=false;
                return true;
            }
            if(selection_active){
                selection_active=selection_drag=false;
                return true;
            }
        }
        if(ps_route()==PS_RECORDS&&selected_record>=0){
            selected_record=-1;
            return true;
        }
        if(ps_route()==PS_EDITOR&&editor_note&&editor_dirty&&!draft_save())return true;
        return false;
    }
    if(ps_route()!=PS_READ)return false;
    if(key==0){
        page_turn(-1);
        return true;
    }
    if(key==2){
        page_turn(1);
        return true;
    }
    if(key==1){
        tools=!tools;
        chapter_panel=font_panel=false;
        return true;
    }
    return false;

}

bool ps_reader_tick(void){
    bool shelf_background=ps_route()!=PS_SHELF||ps_background_io_allowed();
    bool dirty=shelf_background&&reader_font_sync(ps_route()==PS_READ&&open_book);
    if(ps_route()==PS_SHELF&&shelf_background){
        shelf_refresh_dual_progress();
        read_pico_sd_info_t info;
        if(!shelf_io_available(&info)){
            if(shelf_stage_active)shelf_stage_cancel(true);
        }else{
            uint32_t generation=shelf_have_media?shelf_stage_generation:shelf_media_generation;
            bool ready=shelf_have_media?shelf_stage_media_ready:shelf_media_ready;
            unsigned revision=shelf_have_media?shelf_stage_revision:shelf_revision;
            if(ps_now()-shelf_poll_ms>=500||shelf_scan_pending||generation!=info.media_generation||ready!=(info.present&&info.mounted)){
                shelf_poll_ms=ps_now();
                if(!shelf_have_media||generation!=info.media_generation||ready!=(info.present&&info.mounted)||revision!=book_store_revision())shelf_scan_pending=true;
            }
            if(shelf_scan_pending&&!shelf_scan_begin(&info))dirty=true;
            if(shelf_scanning)(void)shelf_scan_step();
            bool covers=shelf_cover_prepare_visible();
            if(shelf_stage_active&&shelf_commit_pending&&shelf_cover_batch_done){shelf_stage_commit();dirty=true;}
            else if(!shelf_stage_active){
                if(shelf_cover_batch_done&&shelf_display_covers_held){covers=shelf_cover_display_changed()||covers;shelf_cover_display_release();}
                if(covers)dirty=true;
            }
        }
        if(shelf_scan_pending||shelf_stage_active)dirty=false;
    }
    if(open_book&&ps_route()==PS_READ&&layout_changed()){
        layout_begin();
        dirty=true;
    }
    if(open_book&&ps_route()==PS_READ&&!rb[active_book].comic&&!paginated&&!layout_failed){
        bool pending=seek_pending;
        layout_extend(4);
        dirty=(pending&&!seek_pending)||(ps_route()==PS_READ&&paginated)||dirty;
    }
    if(!editor_note&&!ps_storage_busy()&&!ps_usb_held()&&(real_draft_clear_pending||draft_clear_pending)&&ps_now()>=draft_clear_retry_ms){
        if(real_draft_clear_pending)real_draft_clear_pending=!reader_store("reader-real-d2",&real_draft,sizeof(real_draft));
        if(draft_clear_pending)draft_clear_pending=!reader_store("reader-draft-v2",&draft,sizeof(draft));
        draft_clear_retry_ms=ps_now()+5000;
    }
    if(editor_note&&editor_dirty&&!draft_auto_paused&&!composition[0]&&editor_pressed<0&&
       ps_background_io_allowed()&&ps_now()>=editor_save_ms){
        draft_save();
        editor_save_ms=ps_now()+15000;
    }
    return dirty;

}

void ps_reader_enter(ps_route_t route){
    if(route!=PS_SHELF)shelf_open_choice_close();
    shelf_style_picker=false;
    if(route!=PS_SHELF&&shelf_stage_active)shelf_stage_cancel(true);
    if(route==PS_SHELF){shelf_cover_signature=0;shelf_cover_changed=shelf_cover_batch_done=false;shelf_cover_attempts=0;}
    if(last_route==PS_RECORDS&&route!=PS_RECORDS){record_filter_path[0]=0;record_filter_bytes=0;}
    if(route!=PS_SHELF&&shelf_scanning)shelf_stage_cancel(true);
    if(route==PS_SHELF&&(last_route==PS_ROUTE_COUNT||last_route==PS_TRANSFER||last_route==PS_USB_TRANSFER)){
        shelf_scan_pending=true;
    }
    if(last_route==PS_EDITOR&&route!=PS_EDITOR&&editor_password){
        ps_reader_editor_invalidate();
        // 输入缓存不落盘；异步连接命令须在回调返回前复制凭据。/ Never persist the editor buffer; async commands copy credentials before returning.
        volatile char *secret=editor_text;
        for(size_t i=0;i<sizeof(editor_text);i++)secret[i]=0;
        editor_done=NULL;editor_password=editor_password_visible=editor_wifi_password=false;
        composition[0]=0;
    }
    if(last_route==PS_EDITOR&&route!=PS_EDITOR){
        if(editor_note&&editor_dirty)draft_save();
        editor_numeric=editor_decimal=false;editor_submit[0]=editor_hint[0]=0;
        editor_font[0]=0;editor_font_prepared=false;
    }
    if(route==PS_INPUT_SETTINGS)ime_dictionary_prepare();
    if(route==PS_EDITOR&&last_route!=PS_EDITOR)editor_font_prepare();
    // 离开产品页不改字体，真实图书可直接沿用；仅显式选择才同步阅读首选。
    // Keep the font when leaving for real books; only an explicit selection updates reader preference.
    if(route==PS_ROUTE_COUNT){
        reader_choice_revision=app_settings_font_revision();
        last_route=route;
        return;
    }
    if(reader_entered&&last_route==PS_ROUTE_COUNT&&reader_choice_revision!=app_settings_font_revision()){
        ri_copy(rp.font,sizeof(rp.font),system_font_preference());
        detached_font[0]=0;
        prefs_save();
    }
    reader_font_sync(route==PS_READ&&open_book);
    if(route==PS_READ&&open_book&&layout_changed())layout_begin();
    if(route==PS_RECORDS&&last_route!=PS_RECORDS){
        selected_record=-1;
        record_scroll=0;
    }
    last_route=route;
    reader_entered=true;

}

// 电源状态保留已显示封面；移交存储时才释放缓存。 / Retain displayed covers for power states; release caches only for storage handoff.
static PICO_READER_COLD bool reader_state_save(bool release_covers){
    if(shelf_stage_active)shelf_stage_cancel(true);
    if(!initialized)return true;
    if(!ps_storage_save_allowed())return false;
    // 即时保存成功后仅检查未提交草稿；USB/失卡与写失败会使检查点失效。
    // Immediate saves keep this checkpoint valid; USB/media loss and failed writes invalidate it.
    if(!release_covers&&reader_checkpoint_clean&&!(editor_note&&editor_dirty)&&!draft_clear_pending&&!real_draft_clear_pending)return true;
    reader_checkpoint_clean=false;
    if(release_covers){
        shelf_cover_display_release();
        book_cover_clear();shelf_cover_signature=0;shelf_cover_changed=shelf_cover_batch_done=shelf_commit_pending=false;
    }
    bool ok=prefs_save();
    if(shelf_favorites_loaded&&shelf_favorites&&!shelf_favorites_failed&&!reader_store("reader-favs-v1",shelf_favorites,sizeof(*shelf_favorites)))ok=false;
    // 新目标不能复用旧卡对象ID；全部对象写妥并提交索引后才发布新ID，普通电源检查点仍复用原对象。
    // A new target cannot reuse old-card object IDs; publish fresh IDs only after all objects and the index commit, while power checkpoints reuse objects.
    if(rr&&!(release_covers?records_commit(rr,true):records_save()))ok=false;
    if(editor_note&&editor_dirty)draft_snapshot();
    if(!reader_store("reader-draft-v2",&draft,sizeof(draft)))ok=false;else draft_clear_pending=false;
    if(!reader_store("reader-real-d2",&real_draft,sizeof(real_draft)))ok=false;else real_draft_clear_pending=false;
    if(ok){draft_auto_paused=false;if(editor_note)editor_dirty=false;reader_checkpoint_clean=true;}
    return ok;
}
bool ps_reader_storage_save(void){return reader_state_save(true);}
bool ps_reader_power_save(void){return reader_state_save(false);}
void ps_reader_checkpoint_invalidate(void){reader_checkpoint_clean=false;}

// 失卡通知仅处理RAM与内置字库，绝不探测、重挂或写回消失的卡。 / Media-loss notification touches only RAM and the built-in font, never probes, remounts or writes the missing card.
void ps_reader_media_lost(void){
    if(!initialized)return;
    ps_reader_checkpoint_invalidate();
    shelf_stage_cancel(true);shelf_cover_display_release();
    book_cover_clear();shelf_cover_signature=0;shelf_cover_changed=shelf_cover_batch_done=shelf_commit_pending=false;
    shelf_have_media=false;shelf_ready=false;shelf_file_count=shelf_file_visible=0;
    if(editor_note&&editor_dirty){
        draft_snapshot();
        draft_auto_paused=true;
    }
    editor_font[0]=0;ps_reader_editor_invalidate();
    selection_active=selection_drag=false;
    if(!ttf_font_path_is_builtin(rp.font)){
        ri_copy(detached_font,sizeof(detached_font),rp.font);
        ri_copy(rp.font,sizeof(rp.font),TTF_FONT_BUILTIN);
    }
    font_retry_ready=false;font_retry_attempts=0;font_retry_next_ms=0;
    ttf_font_open_builtin();
    ttf_set_weight(ps_route()==PS_READ&&rp.weight?READER_WEIGHT_BOLD:READER_WEIGHT_REGULAR);
    if(open_book&&!rb[active_book].comic)layout_begin();
    ps_toast(editor_note&&editor_dirty?"TF卡已断开，未保存草稿保留在本次会话":"TF卡已断开，已回退内置字体");
}

void ps_reader_render(uint8_t *fb){
    switch(ps_route()){
        case PS_SHELF:shelf_render(fb);
        break;
        case PS_READ:if(open_book)reading_render(fb);
        break;
        case PS_RECORDS:records_render(fb);
        break;
        case PS_READING_SETTINGS:reading_settings_render(fb);
        break;
        case PS_INPUT_SETTINGS:input_settings_render(fb);
        break;
        case PS_IMPORT:import_render(fb);
        break;
        case PS_EDITOR:editor_render(fb);
        break;
        default:break;
    }

}

PICO_READER_COLD void ps_reader_init(void){
    if(initialized)return;
    initialized=true;
    reader_checkpoint_clean=false;
#ifdef PICO_READER_HOST_FIXTURES
    rb[0]=(reader_book_t){
        "山间来信","示例短篇",reader_text_0,sizeof(reader_text_0)-1,false};
    rb[1]=(reader_book_t){
        "日常的尺度","示例随笔",reader_text_1,sizeof(reader_text_1)-1,false};
    rb[2]=(reader_book_t){
        "远行手记","示例游记",reader_text_2,sizeof(reader_text_2)-1,false};
    rb[3]=(reader_book_t){
        "沿河的书店","原创长篇 · 24章",(const char*)preview_text_start,(size_t)(preview_text_end-preview_text_start),false};
    rb[4]=(reader_book_t){
        "雨夜书店","原创黑白漫画 · 8页",NULL,0,true};
    rb[5]=(reader_book_t){
        "字体与标点试读页","实际字形与分页测试",reader_text_3,sizeof(reader_text_3)-1,false};
#else
    // 历史来源元数据仅用于已保存记录，不提供打开或创建示例入口。/ Legacy source metadata serves saved records only; no sample can be opened or created.
    const reader_book_t legacy[] = {
        {"山间来信","示例短篇",NULL,496,false},
        {"日常的尺度","示例随笔",NULL,427,false},
        {"远行手记","示例游记",NULL,367,false},
        {"沿河的书店","原创长篇 · 24章",NULL,167346,false},
        {"雨夜书店","原创黑白漫画 · 8页",NULL,0,true},
        {"字体与标点试读页","实际字形与分页测试",NULL,7472,false}
    };
    memcpy(rb,legacy,sizeof(rb));
#endif
    rp=(reader_prefs_t){
        .version=READER_PREFS_VERSION,.size=READER_SIZE_DEFAULT,.spacing=1,.weight=1,.list=1};
    ri_copy(rp.font,sizeof(rp.font),system_font_preference());
    reader_prefs_t saved;
    if(ps_load("reader-prefs-v1",&saved,sizeof(saved))&&(saved.version>=1&&saved.version<=READER_PREFS_VERSION)&&saved.last<BOOK_COUNT&&saved.size>=15&&saved.size<=READER_SIZE_MAX&&saved.spacing<4&&saved.weight<2&&saved.input<5&&memchr(saved.font,0,sizeof(saved.font))){
        rp=saved;
        // 仅一次迁移旧默认组合；结构与键保持不变，初始化不写入存储。 / Migrate only the old default pair once; keep the blob shape/key and do not write at init.
        if(rp.version==1&&rp.size==19&&!rp.weight){rp.size=READER_SIZE_DEFAULT;rp.weight=1;}
        // 只迁移旧无衬线默认；宋/楷/自选文件及新版本显式内建选择完整保留。
        // Migrate only the old sans default; preserve serif/Kai/custom files and new explicit built-in choices.
        if(rp.version<3&&(!rp.font[0]||!strcmp(rp.font,TTF_FONT_BUILTIN)))ri_copy(rp.font,sizeof(rp.font),system_font_preference());
        // 新纸上目录默认列表；保留之后显式选择的封面模式。/ Default the editorial design to a list, preserving later explicit cover choices.
        if(rp.version<4)rp.list=1;
        if(rp.list>=SHELF_VIEW_COUNT)rp.list=SHELF_VIEW_DETAILS;
        rp.input=(uint8_t)input_supported(rp.input);
        rp.version=READER_PREFS_VERSION;
    }
    reader_choice_revision=app_settings_font_revision();
#ifdef PICO_READER_HOST_FIXTURES
    for(int i=0;i<BOOK_COUNT;i++){
        if(rp.books[i].comic_page>7)rp.books[i].comic_page=0;
        if(!rb[i].comic){
            if(rp.books[i].offset>=rb[i].bytes)rp.books[i].offset=0;
            while(rp.books[i].offset&&((unsigned char)rb[i].text[rp.books[i].offset]&0xc0)==0x80)rp.books[i].offset--;
        }
    }
#endif
    rr=heap_caps_calloc(1,sizeof(*rr),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
#ifdef PICO_READER_HOST_FIXTURES
    pages=heap_caps_calloc(PAGE_MAX,sizeof(*pages),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
#endif
    if(rr)records_load();
    drafts_load();
    // 保存已提交而清草稿失败时，不再把旧草稿恢复成第二条笔记。 / A committed note with failed draft cleanup must not become a duplicate after reboot.
    real_draft_clear_pending=draft_clear_pending=false;
    if(rr)for(uint32_t i=0;i<rr->count;i++){
        const reader_record_t *n=rr->item[i];if(n->type!=REC_NOTE)continue;
        if(n->book==REAL_BOOK&&(real_draft.text[0]||real_draft.ink_count)){
            reader_record_t probe;if(real_record(&real_draft.anchor,REC_NOTE,&probe)){
                probe.text=real_draft.text;probe.ink_count=(uint8_t)real_draft.ink_count;memcpy(probe.ink,real_draft.ink,sizeof(probe.ink));
                if(same_note(n,&probe)){memset(&real_draft,0,sizeof(real_draft));real_draft.version=1;real_draft_clear_pending=true;}
            }
        }else if(n->book<BOOK_COUNT&&(draft.text[0]||draft.ink_count)){
            reader_record_t probe={.book=draft.book,.type=REC_NOTE,.offset=draft.offset,.end=draft.end,.ink_count=draft.ink_count};
            probe.text=draft.text;ri_copy(probe.quote,sizeof(probe.quote),draft.quote);memcpy(probe.ink,draft.ink,sizeof(probe.ink));
            if(same_note(n,&probe)){memset(&draft,0,sizeof(draft));draft.version=1;draft_clear_pending=true;}
        }
    }
    comic_valid=check_comic();
    active_book=rp.last;
    ttf_set_weight(400);

}

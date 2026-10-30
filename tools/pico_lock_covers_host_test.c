/* SPDX-License-Identifier: Apache-2.0
 * 中文：锁屏封面真实源码的文件缓存、失效、分配故障、独立像素和横竖布局测试。
 * English: Exact-source cover-cache invalidation, allocation failures, owned pixels and orientation tests.
 */
#include <assert.h>
#include <dirent.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <utime.h>

static FILE *lock_test_fopen(const char*,const char*);
static int lock_test_rename(const char*,const char*);
static void lock_test_free(void*);
#define fopen lock_test_fopen
#define rename lock_test_rename
#define free lock_test_free
#include "../main/system/pico_lock_covers.c"
#undef fopen
#undef rename
#undef free

typedef union { struct { size_t bytes; } item; max_align_t align; } allocation_t;
static unsigned checks,opens,decodes,shelf_prepares,draws,texts;
static size_t live_bytes,peak_bytes,largest_request;
static int fail_after=-1;
static char fixture_dir[288],last_path[288],shelf_paths[8][288];
static int shelf_count;
static bool shelf_known;
static uint8_t shelf_pixels[128*192];
static ps_view_t view={360,640,false};
static int fw,fh;
static uint8_t *frame;

#define CHECK(condition) do{checks++;if(!(condition)){fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#condition);abort();}}while(0)

void *lock_test_malloc(size_t n) {
    if(n>largest_request)largest_request=n;
    if(fail_after>=0&&fail_after--==0)return NULL;
    allocation_t *a=malloc(sizeof(*a)+n);if(!a)return NULL;
    a->item.bytes=n;live_bytes+=n;if(live_bytes>peak_bytes)peak_bytes=live_bytes;return a+1;
}
static void lock_test_free(void *p) {
    if(!p)return;
    allocation_t *a=(allocation_t*)p-1;CHECK(live_bytes>=a->item.bytes);live_bytes-=a->item.bytes;free(a);
}
static FILE *lock_test_fopen(const char *path,const char *mode){opens++;return fopen(path,mode);}
static int lock_test_rename(const char *from,const char *to){if(!access(to,F_OK)){errno=EEXIST;return -1;}return rename(from,to);}
ps_view_t ps_view(void){return view;}
bool book_progress_last_path(char *out,size_t cap){snprintf(out,cap,"%s",last_path);return last_path[0]!=0;}
int ps_reader_cover_sources(char paths[][288],int capacity){int n=shelf_count<capacity?shelf_count:capacity;for(int i=0;i<n;i++)memcpy(paths[i],shelf_paths[i],288);return n;}
bool book_cover_known(const char *path){(void)path;return shelf_known;}
static unsigned source_gray(const char *path){FILE *f=fopen(path,"rb");if(!f)return 0;int gray=fgetc(f);fclose(f);return gray<0?0:(unsigned)gray;}
bool book_cover_prepare(const char *path){shelf_prepares++;shelf_known=true;memset(shelf_pixels,(int)source_gray(path),sizeof(shelf_pixels));return true;}
bool book_cover_get(const char *path,book_cover_view_t *out){(void)path;*out=(book_cover_view_t){shelf_pixels,128,192};return shelf_known;}
esp_err_t book_epub_load_cover(const char *path,uint8_t **pixels,uint16_t *w,uint16_t *h) {
    decodes++;*pixels=NULL;*w=*h=0;unsigned gray=source_gray(path);if(!gray)return ESP_FAIL;
    *pixels=lock_test_malloc(648*1000);if(!*pixels)return ESP_ERR_NO_MEM;
    memset(*pixels,(int)gray,648*1000);*w=648;*h=1000;return ESP_OK;
}
static int px(int value){return (value*19+5)/10;}
EpdRect ps_rect(int x,int y,int w,int h){return(EpdRect){px(x),px(y),px(w),px(h)};}
static void valid_rect(int x,int y,int w,int h){CHECK(x>=0&&y>=0&&w>0&&h>0&&x+w<=view.w&&y+h<=view.h);draws++;}
void epd_draw_pixel(int x,int y,uint8_t gray,uint8_t *fb){assert(x>=0&&x<fw&&y>=0&&y<fh&&fb==frame);fb[(size_t)y*fw+x]=gray;draws++;}
void ps_fill(uint8_t *fb,int x,int y,int w,int h,int gray){valid_rect(x,y,w,h);EpdRect r=ps_rect(x,y,w,h);for(int row=r.y;row<r.y+r.height;row++)for(int col=r.x;col<r.x+r.width;col++)epd_draw_pixel(col,row,(uint8_t)(gray*17),fb);}
void ps_box(uint8_t *fb,int x,int y,int w,int h,int radius,int gray,bool filled){(void)radius;valid_rect(x,y,w,h);if(filled)ps_fill(fb,x,y,w,h,gray);}
void ps_wrap(uint8_t *fb,int x,int y,int w,int size,int lines,const char *text,int gray){(void)fb;(void)gray;CHECK(text&&*text);valid_rect(x,y,w,size*lines);texts++;}
void ps_text_fixed(uint8_t *fb,int x,int y,int size,const char *text,int gray){(void)fb;(void)gray;CHECK(text&&*text);valid_rect(x,y,1,size);texts++;}

static void set_view(bool landscape){free(frame);view=landscape?(ps_view_t){640,360,true}:(ps_view_t){360,640,false};fw=px(view.w);fh=px(view.h);frame=malloc((size_t)fw*fh);CHECK(frame);}
static uint32_t render_snapshot(bool landscape){set_view(landscape);unsigned before=opens,loads=decodes,prepares=shelf_prepares;ps_lock_covers_render(frame);CHECK(opens==before&&decodes==loads&&shelf_prepares==prepares);return crc32_bytes(frame,(size_t)fw*fh);}
static void file_path(char *out,const char *name){int n=snprintf(out,288,"%s/%s",fixture_dir,name);CHECK(n>0&&n<288);}
static void write_file(const char *path,unsigned gray,size_t size){FILE *f=fopen(path,"wb");CHECK(f);for(size_t i=0;i<size;i++)CHECK(fputc((int)gray,f)!=EOF);CHECK(!fclose(f));}
static void setup_sources(void){
    shelf_count=8;last_path[0]=0;shelf_known=false;
    for(int i=0;i<8;i++){char name[32];snprintf(name,sizeof(name),"book-%d.epub",i);file_path(shelf_paths[i],name);write_file(shelf_paths[i],32+i*24,5000);}
}
static void path_for_cache(const char *book,bool high,char *out){char parent[LOCK_CACHE_PATH],directory[LOCK_CACHE_PATH];uint64_t key=hash64(UINT64_C(14695981039346656037),book,strlen(book));CHECK(cache_paths(book,key,high,parent,directory,out));}
static size_t retained_pixels(void){size_t n=0;for(int i=0;i<cover_count;i++)if(covers[i].pixels)n+=(size_t)covers[i].width*covers[i].height;return n;}
static void change_cache(const char *path,int mutation){
    FILE *f=fopen(path,"r+b");CHECK(f);uint8_t h[LOCK_HEADER];CHECK(fread(h,1,sizeof(h),f)==sizeof(h));
    if(mutation==0){CHECK(!fseek(f,-1,SEEK_END));int byte=fgetc(f);CHECK(byte!=EOF);CHECK(!fseek(f,-1,SEEK_END));CHECK(fputc(byte^255,f)!=EOF);}
    else if(mutation==1){CHECK(!fseek(f,0,SEEK_END));CHECK(fputc(0,f)!=EOF);}
    else if(mutation==2){put16(h+20,649);put32(h+24,649u*get16(h+22));}
    else if(mutation==3)h[32]^=1;
    else if(mutation==4)h[40]^=1;
    else if(mutation==5)h[48]^=1;
    else if(mutation==6)h[56]^=1;
    else if(mutation==7)h[68]=1;
    else if(mutation==8)h[64]^=1;
    else if(mutation==9)put32(h+16,0);
    else if(mutation==10)h[0]^=1;
    if(mutation>=2){if(mutation!=8)put32(h+64,crc32_bytes(h,64));CHECK(!fseek(f,0,SEEK_SET));CHECK(fwrite(h,1,sizeof(h),f)==sizeof(h));}
    CHECK(!fclose(f));
    if(mutation==11)CHECK(!truncate(path,LOCK_HEADER+10));
}
int main(int argc,char **argv){
    CHECK(argc==2&&strlen(argv[1])<sizeof(fixture_dir));strcpy(fixture_dir,argv[1]);setup_sources();
    // 最近阅读优先，单本高清缓存跨 prepare 复用；render 完全不读文件。
    // Last read wins, high-resolution cache survives preparation, and render performs no file reads.
    strcpy(last_path,shelf_paths[4]);ps_lock_covers_prepare(0);CHECK(cover_count==1&&!strcmp(covers[0].path,last_path));
    CHECK(covers[0].pixels&&covers[0].width==648&&covers[0].height==1000&&covers[0].pixels[0]==128&&live_bytes==648000);
    unsigned before=decodes;uint32_t portrait=render_snapshot(false),landscape=render_snapshot(true);CHECK(portrait&&landscape);
    ps_lock_covers_prepare(0);CHECK(decodes==before&&covers[0].pixels[0]==128);
    char cache[LOCK_CACHE_PATH];path_for_cache(last_path,true,cache);CHECK(!access(cache,F_OK));
    for(int failure=0;failure<12;failure++){change_cache(cache,failure);before=decodes;ps_lock_covers_prepare(0);CHECK(decodes==before+1&&covers[0].pixels&&covers[0].pixels[0]==128);}
    // 同大小/mtime 的内容替换也必须失效；旧书架 RAM 不得覆盖新的源指纹。
    // Replacements with unchanged size/mtime still invalidate; stale shelf RAM cannot override the new source fingerprint.
    struct stat st;CHECK(!stat(last_path,&st));write_file(last_path,93,5000);struct utimbuf stamp={st.st_atime,st.st_mtime};CHECK(!utime(last_path,&stamp));
    before=decodes;ps_lock_covers_prepare(0);CHECK(decodes==before+1&&covers[0].pixels[0]==93);
    write_file(last_path,94,5001);before=decodes;ps_lock_covers_prepare(0);CHECK(decodes==before+1&&covers[0].pixels[0]==94);
    stamp.modtime+=10;CHECK(!utime(last_path,&stamp));before=decodes;ps_lock_covers_prepare(0);CHECK(decodes==before+1);
    // 两本和六本横竖自适应；自有缩略图不随书架借用内存变化。
    // Two/six-book layouts adapt to rotation, retaining owned thumbnails when borrowed shelf memory changes.
    shelf_known=false;ps_lock_covers_prepare(1);CHECK(cover_count==2&&covers[0].width==128&&covers[0].height==192&&live_bytes==retained_pixels()&&live_bytes<=49152);
    uint32_t a=render_snapshot(false),b=render_snapshot(true);memset(shelf_pixels,0,sizeof(shelf_pixels));CHECK(render_snapshot(false)==a&&render_snapshot(true)==b);
    before=decodes;ps_lock_covers_prepare(1);CHECK(decodes==before&&live_bytes==retained_pixels()&&live_bytes<=49152);
    ps_lock_covers_prepare(2);CHECK(cover_count==6&&live_bytes==retained_pixels()&&live_bytes<=6u*128*192);CHECK(!strcmp(covers[0].path,last_path));
    for(int i=0;i<cover_count;i++)for(int j=i+1;j<cover_count;j++)CHECK(strcmp(covers[i].path,covers[j].path));
    (void)render_snapshot(false);(void)render_snapshot(true);CHECK(peak_bytes<=6u*128*192+648000&&largest_request==648000);
    // 新增样式保留旧编号/缓存，海报读高清，贴边书墙裁切后填满四角，绘制不做 I/O。
    // Appended styles retain old IDs/caches: a high-res poster and edge-filled cropped wall, with no rendering I/O.
    ps_lock_covers_prepare(3);CHECK(cover_layout==3&&cover_count==1&&covers[0].width==648&&live_bytes==648000);
    uint32_t poster=render_snapshot(false);CHECK(poster!=portrait);(void)render_snapshot(true);
    ps_lock_covers_prepare(2);uint32_t collage=render_snapshot(false);
    ps_lock_covers_prepare(4);CHECK(cover_layout==4&&cover_count==6&&live_bytes<=6u*128*192);
    CHECK(render_snapshot(false)!=collage);CHECK(frame[0]==covers[0].pixels[0]&&frame[fw-1]==covers[1].pixels[0]);
    CHECK(frame[(size_t)(fh-1)*fw]==covers[4].pixels[0]&&frame[(size_t)fw*fh-1]==covers[5].pixels[0]);
    (void)render_snapshot(true);CHECK(frame[fw-1]==covers[2].pixels[0]&&frame[(size_t)(fh-1)*fw]==covers[3].pixels[0]);
    // 挂载介质丢失后保留的 RAM 仍可重绘；再次准备时回退书名，释放旧图。
    // Retained RAM redraws after media loss; the next prepare falls back to titles and releases old pixels.
    a=render_snapshot(false);CHECK(!unlink(last_path));CHECK(render_snapshot(false)==a);ps_lock_covers_prepare(0);CHECK(!covers[0].pixels&&!live_bytes);(void)render_snapshot(false);
    write_file(last_path,87,5000);fail_after=0;ps_lock_covers_prepare(0);CHECK(!covers[0].pixels&&!live_bytes);fail_after=-1;ps_lock_covers_prepare(0);CHECK(covers[0].pixels);
    // 缓存分配失败可重新解码；缓存目录不可用也不会影响独立 RAM 显示。
    // Cache allocation failure may decode afresh; an unavailable cache directory still permits owned RAM rendering.
    fail_after=0;ps_lock_covers_prepare(0);CHECK(covers[0].pixels);fail_after=-1;
    char blocked[288],book[288];file_path(blocked,"blocked");CHECK(!mkdir(blocked,0700));int n=snprintf(book,sizeof(book),"%s/new.epub",blocked);CHECK(n>0&&n<(int)sizeof(book));write_file(book,73,24);
    char obstruction[320];snprintf(obstruction,sizeof(obstruction),"%s/.pico-covers",blocked);FILE *f=fopen(obstruction,"wb");CHECK(f&&!fclose(f));strcpy(last_path,book);ps_lock_covers_prepare(0);CHECK(covers[0].pixels&&covers[0].pixels[0]==73);
    // 无图书/普通文本/无效路径只画占位；反复 release 安全。
    // Empty libraries, text files and invalid paths use placeholders; repeated release is safe.
    strcpy(last_path,"relative.epub");shelf_count=0;ps_lock_covers_prepare(2);CHECK(!cover_count&&!live_bytes);(void)render_snapshot(false);(void)render_snapshot(true);
    file_path(book,"标题.txt");write_file(book,90,1);strcpy(last_path,book);ps_lock_covers_prepare(0);CHECK(cover_count==1&&!covers[0].pixels&&!strcmp(covers[0].title,"标题"));(void)render_snapshot(false);
    memset(shelf_paths[0],'x',288);shelf_paths[0][0]='/';shelf_count=1;last_path[0]=0;ps_lock_covers_prepare(-1);CHECK(!cover_count&&cover_layout==0);(void)render_snapshot(true);
    ps_lock_covers_release();ps_lock_covers_release();CHECK(!live_bytes);ps_lock_covers_render(NULL);free(frame);
    printf("lock covers: %u checks PASS; %u decode calls, %u shelf prepares, %u draws, %u texts; peak %zu bytes; RAM-only render, 12 corruptions, rotation/media/allocation fallback\n",checks,decodes,shelf_prepares,draws,texts,peak_bytes);
    return 0;
}

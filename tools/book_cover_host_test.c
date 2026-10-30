/* SPDX-License-Identifier: Apache-2.0
 * 中文：真实EPUB封面与缓存的主机回归；不触碰硬件。 / Real EPUB cover/cache host regression; no hardware.
 */
#include "book_cover.h"
#include "book_epub.h"
#include <assert.h>
#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
static unsigned checks, loads;
static int injected;
int job_test_fail_after=-1;
size_t job_test_largest_request;
// 固件FatFs不支持重命名覆盖；主机也按该限制测试。 / Apply device FatFs no-replace rename semantics on the host.
int cover_test_rename(const char *from,const char *to) {
    if (access(to,F_OK)==0) { errno=EEXIST; return -1; }
    return rename(from,to);
}
#define CHECK(x) do { ++checks; if (!(x)) { fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x); abort(); } } while(0)
extern esp_err_t real_book_epub_load_cover(const char*,uint8_t**,uint16_t*,uint16_t*);
esp_err_t book_epub_load_cover(const char *p,uint8_t **v,uint16_t *w,uint16_t *h) {
    ++loads;
    if (injected) { *v=NULL; *w=*h=0; return injected; }
    return real_book_epub_load_cover(p,v,w,h);
}
static char directory[512];
static void path(char *out,const char *name) { CHECK(snprintf(out,600,"%s/%s",directory,name)>0); }
static void extract(const char *name,int width,int height,int gray) {
    char p[600]; path(p,name); uint8_t *v=NULL; uint16_t w=0,h=0;
    CHECK(book_epub_load_cover(p,&v,&w,&h)==ESP_OK);
    CHECK(v && w==width && h==height);
    for (unsigned i=0;i<(unsigned)w*h;i++) CHECK(v[i]==gray);
    free(v);
}
static unsigned caches(char *first) {
    char p[600]; path(p,".pico-covers"); DIR *d=opendir(p); unsigned n=0;
    if (!d) return 0;
    struct dirent *e;
    while ((e=readdir(d))) if (strstr(e->d_name,".pcv")) { if (!n && first) CHECK(snprintf(first,800,"%s/%s",p,e->d_name)>0); ++n; }
    closedir(d); return n;
}
static void clear_disk(void) {
    char p[800]; while (caches(p)) CHECK(unlink(p)==0);
}
// 两本旧但仍可见的封面必须先被touch；否则准备新页时会被较新的不可见条目淘汰。
// Touch older visible entries before filling a nine-cover page so newer offscreen entries are evicted first.
static void test_visible_age_touch(void) {
    book_cover_clear(); clear_disk();
    char visible[9][600],offscreen[7][600];
    for (int i=0;i<9;i++) { char name[32];snprintf(name,sizeof(name),"lru-%c.epub",'a'+i);path(visible[i],name); }
    for (int i=0;i<7;i++) { char name[32];snprintf(name,sizeof(name),"lru-x%d.epub",i);path(offscreen[i],name); }
    unsigned before=loads;book_cover_view_t a,b,view;
    CHECK(book_cover_prepare(visible[0]));CHECK(book_cover_prepare(visible[1]));
    CHECK(book_cover_get(visible[0],&a)&&a.pixels[0]==17);CHECK(book_cover_get(visible[1],&b)&&b.pixels[0]==34);
    for (int i=0;i<7;i++) CHECK(book_cover_prepare(offscreen[i]));
    CHECK(loads==before+9&&book_cover_known(visible[0])&&book_cover_known(visible[1]));
    // 文件暂时不存在时known fast path仍只触碰RAM，既不访问正文也不重新解码。
    // A known-entry touch works while its source is absent and never decodes or reallocates the retained pixels.
    char moved[2][600];path(moved[0],"lru-a.moved");path(moved[1],"lru-b.moved");
    CHECK(rename(visible[0],moved[0])==0);CHECK(rename(visible[1],moved[1])==0);
    CHECK(!book_cover_prepare(visible[0]));CHECK(!book_cover_prepare(visible[1]));CHECK(loads==before+9);
    for (int i=2;i<9;i++) CHECK(book_cover_prepare(visible[i]));
    CHECK(loads==before+16);
    for (int i=0;i<9;i++) {
        CHECK(book_cover_known(visible[i])&&book_cover_get(visible[i],&view));
        CHECK(view.width==24&&view.height==36&&view.pixels[0]==(i+1)*17);
    }
    CHECK(book_cover_get(visible[0],&view)&&view.pixels==a.pixels);
    CHECK(book_cover_get(visible[1],&view)&&view.pixels==b.pixels);
    for (int i=0;i<7;i++) CHECK(!book_cover_known(offscreen[i]));
    for (int pass=0;pass<20;pass++)for (int i=0;i<9;i++) CHECK(book_cover_get(visible[i],&view));
    CHECK(!book_cover_prepare(visible[0])&&loads==before+16);
    CHECK(rename(moved[0],visible[0])==0);CHECK(rename(moved[1],visible[1])==0);
    book_cover_clear();
}
int main(int argc,char **argv) {
    CHECK(argc==2); CHECK(strlen(argv[1])<sizeof(directory)); strcpy(directory,argv[1]);
    extract("epub3.epub",240,360,32); extract("epub2.epub",48,72,64);
    extract("xhtml.epub",64,96,96); extract("svg.epub",60,90,128);
    extract("namespace.epub",80,120,160); extract("fallback.epub",32,48,192);
    const char *bad[]={"none.epub","remote.epub","traversal.epub","corrupt-image.epub","broken.epub"};
    for (unsigned i=0;i<sizeof(bad)/sizeof(*bad);i++) {
        char p[600];path(p,bad[i]);uint8_t *v=(void*)1;uint16_t w=1,h=1;
        CHECK(book_epub_load_cover(p,&v,&w,&h)!=ESP_OK);CHECK(!v && !w && !h);
    }
    CHECK(book_epub_load_cover(NULL,NULL,NULL,NULL)==ESP_ERR_INVALID_ARG);
    char p[600],moved[600],cached[800];path(p,"epub3.epub");path(moved,"moved.epub");
    book_cover_view_t view; CHECK(!book_cover_get(p,&view));CHECK(!view.pixels && !view.width);
    loads=0;CHECK(book_cover_prepare(p));CHECK(loads==1);CHECK(book_cover_get(p,&view));
    CHECK(view.width==128 && view.height==192 && view.pixels[0]==32);CHECK(caches(cached)==1);
    CHECK(!book_cover_prepare(p) && loads==1);
    CHECK(rename(p,moved)==0);CHECK(book_cover_get(p,&view));CHECK(book_cover_known(p));CHECK(loads==1);CHECK(rename(moved,p)==0);
    book_cover_clear();CHECK(!book_cover_known(p));CHECK(book_cover_prepare(p));CHECK(loads==1);CHECK(book_cover_get(p,&view));
    FILE *f=fopen(cached,"r+b");CHECK(f);CHECK(fseek(f,-1,SEEK_END)==0);int b=fgetc(f);CHECK(b!=EOF);CHECK(fseek(f,-1,SEEK_END)==0);CHECK(fputc(b^255,f)!=EOF);CHECK(fclose(f)==0);
    book_cover_clear();CHECK(book_cover_prepare(p));CHECK(loads==2);CHECK(book_cover_get(p,&view) && view.pixels[0]==32);
    path(moved,"replacement.epub"); CHECK(rename(moved,p)==0);
    book_cover_clear();CHECK(book_cover_prepare(p));CHECK(loads==3);CHECK(book_cover_get(p,&view));CHECK(view.pixels[0]==224 && view.width==128 && view.height==64);
    // 缓存目录不可写时仍能显示内存封面。 / An unavailable cache directory must not prevent RAM display.
    book_cover_clear();clear_disk();char cache_dir[600];path(cache_dir,".pico-covers");CHECK(rmdir(cache_dir)==0);
    f=fopen(cache_dir,"wb");CHECK(f);CHECK(fclose(f)==0);
    CHECK(book_cover_prepare(p));CHECK(book_cover_get(p,&view) && view.pixels[0]==224);CHECK(unlink(cache_dir)==0);
    // 暂态IO/内存失败不会落盘为永久无封面。 / Never persist transient I/O or allocation failures as absent covers.
    book_cover_clear();injected=ESP_FAIL;CHECK(book_cover_prepare(p));CHECK(!book_cover_get(p,&view));CHECK(caches(NULL)==0);
    book_cover_clear();injected=ESP_ERR_NO_MEM;CHECK(book_cover_prepare(p));CHECK(caches(NULL)==0);
    book_cover_clear();injected=ESP_ERR_NOT_SUPPORTED;CHECK(book_cover_prepare(p));CHECK(caches(NULL)==0);
    book_cover_clear();injected=0;CHECK(book_cover_prepare(p));CHECK(book_cover_get(p,&view));CHECK(caches(NULL)==1);
    book_cover_clear();path(p,"none.epub");unsigned before=loads;CHECK(book_cover_prepare(p));CHECK(loads==before+1 && !book_cover_get(p,&view));
    book_cover_clear();CHECK(book_cover_prepare(p));CHECK(loads==before+1 && book_cover_known(p));
    for (int i=0;i<10;i++) { char name[32];snprintf(name,sizeof(name),"plain%d.txt",i);path(p,name);CHECK(book_cover_prepare(p)); }
    path(p,"plain0.txt");CHECK(!book_cover_known(p));path(p,"plain9.txt");CHECK(book_cover_known(p));
    book_cover_clear();CHECK(!book_cover_known(p));CHECK(!book_cover_prepare("relative.epub"));CHECK(!book_cover_prepare(NULL));
    test_visible_age_touch();
    printf("book cover: %u checks PASS; real ZIP/XML/PNG extraction and cache, ASan/UBSan\n",checks);
    return 0;
}

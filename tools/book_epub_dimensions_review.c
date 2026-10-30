/* SPDX-License-Identifier: Apache-2.0
 * Exact production chapter helper with controlled parser/ZIP/allocation faults.
 * Independently checks geometry parity and transient-OOM rollback, without device IO.
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "html_text.h"
#define EPUB_PATH_CAP 256
static unsigned checks,probes;
#define CHECK(v) do { ++checks; if (!(v)) { fprintf(stderr,"EPUB dimensions review line %d: %s\n",__LINE__,#v); abort(); } } while (0)
typedef struct {int zip_index;} chapter_t;
typedef struct {void* zip;size_t count;chapter_t* chapters;uint32_t* image_dimensions;uint8_t* title_images;uint16_t* image_first;} book_epub_t;
static int probe_error;
static bool fail_dimensions,fail_history;
static void* psram(size_t bytes) {
    if ((fail_dimensions && bytes==16*sizeof(uint32_t)) || (fail_history && bytes==16*sizeof(uint16_t))) return NULL;
    return malloc(bytes);
}
static esp_err_t load_entry(book_epub_t* book,int entry,char** out,size_t* length) {
    (void)book;*out=NULL;*length=0;
    if(entry && probe_error)return probe_error;
    *out=malloc(2);CHECK(*out);(*out)[0]=(char)entry;(*out)[1]=0;*length=1;return ESP_OK;
}
esp_err_t html_to_blocks(const char* text,size_t len,html_text_t* out) {
    (void)text;(void)len;
    out->utf8=strdup("r\nm\na\nb\nc\nbody");CHECK(out->utf8);out->len=strlen(out->utf8);out->count=6;
    out->blocks=calloc(out->count,sizeof(*out->blocks));CHECK(out->blocks);
    const char* references[]={"remote","missing","first.png","second.png","first.png"};
    for(size_t i=0;i<6;++i){out->blocks[i].offset=i*2;out->blocks[i].len=i==5?4:1;if(i<5){out->blocks[i].image_src=strdup(references[i]);CHECK(out->blocks[i].image_src);}}
    out->blocks[5].heading=true;return ESP_OK;
}
void html_text_free(html_text_t* out) {
    for(size_t i=0;i<out->count;++i){free(out->blocks[i].image_src);free(out->blocks[i].image);}
    free(out->utf8);free(out->blocks);*out=(html_text_t){0};
}
static const char* zip_entry_name(void* zip,int index){(void)zip;(void)index;return "OPS/chapter.xhtml";}
static size_t zip_entry_count(void* zip){(void)zip;return 16;}
static bool resolve_path(const char* base,const char* reference,char* path){(void)base;if(!strcmp(reference,"remote"))return false;strcpy(path,reference);return true;}
static int zip_find(void* zip,const char* path){(void)zip;return !strcmp(path,"first.png")?1:!strcmp(path,"second.png")?2:-1;}
static bool book_image_dimensions(const uint8_t* bytes,size_t len,uint16_t* width,uint16_t* height){
    CHECK(len==1);++probes;*width=bytes[0]==1?648:300;*height=bytes[0]==1?1000:50;return true;
}
#include "book-epub-dimensions-review.inc"
static void free_book_fixture(book_epub_t* book){free(book->image_dimensions);free(book->title_images);free(book->image_first);book->image_dimensions=NULL;book->title_images=NULL;book->image_first=NULL;}
static void geometry_equal(const html_text_t* a,const html_text_t* b){
    CHECK(a->count==b->count && a->len==b->len && !strcmp(a->utf8,b->utf8));
    for(size_t i=0;i<a->count;++i){
        const blk_t* x=a->blocks+i;const blk_t* y=b->blocks+i;
        CHECK(x->offset==y->offset && x->len==y->len && x->heading==y->heading);
        CHECK(x->image_width==y->image_width && x->image_height==y->image_height && x->image_failed==y->image_failed);
        CHECK(!x->image && !y->image);
    }
}
int main(void){
    chapter_t chapter={.zip_index=0};book_epub_t book={.count=1,.chapters=&chapter};html_text_t a={0},b={0};
    probe_error=ESP_ERR_NO_MEM;
    CHECK(load_chapter_blocks(&book,0,&a,true)==ESP_ERR_NO_MEM);
    CHECK(!a.utf8 && !a.blocks && !a.len && !a.count);
    CHECK(book.image_dimensions && !book.image_dimensions[1]);
    probe_error=ESP_OK;
    CHECK(load_chapter_blocks(&book,0,&a,true)==ESP_OK);
    CHECK(a.blocks[0].image_failed && a.blocks[1].image_failed);
    CHECK(a.blocks[2].image_width==648 && a.blocks[3].image_width==300 && a.blocks[4].image_width==648);
    unsigned before=probes;CHECK(load_chapter_blocks(&book,0,&b,false)==ESP_OK && probes==before);geometry_equal(&a,&b);
    html_text_free(&a);html_text_free(&b);free_book_fixture(&book);
    for(int mode=0;mode<3;++mode){
        fail_dimensions=mode==0||mode==2;fail_history=mode==1||mode==2;
        CHECK(load_chapter_blocks(&book,0,&a,true)==ESP_OK);
        CHECK(a.blocks[2].image_width==648 && a.blocks[3].image_width==300 && a.blocks[4].image_width==648);
        CHECK(load_chapter_blocks(&book,0,&b,false)==ESP_OK);geometry_equal(&a,&b);
        html_text_free(&a);html_text_free(&b);free_book_fixture(&book);
    }
    printf("independent EPUB geometry/OOM review: %u checks; normal/count parity, invalid references, dimension-cache/history allocation faults and probe rollback PASS\n",checks);
    return 0;
}

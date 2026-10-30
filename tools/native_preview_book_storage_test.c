/* SPDX-License-Identifier: Apache-2.0
 * 中文：真实进度模块和预览私有存储的持久化及删除边界检查。
 * English: Check persistence and deletion boundaries for actual progress plus private preview storage.
 */
#include "native_preview_hal.h"
#include "book_store.h"
#include "book_progress.h"
#include "read_pico_sd.h"
#include <assert.h>
#include <string.h>
#include <unistd.h>
static char rootdir[512];
static bool sd_present=true,sd_mounted=true;
const char *native_preview_root(void) { return rootdir; }
void native_preview_log(const char *tag,const char *fmt,...) { (void)tag;(void)fmt; }
esp_err_t read_pico_sd_get_info(read_pico_sd_info_t *out) {
    if (out) *out=(read_pico_sd_info_t){.present=sd_present,.mounted=sd_mounted};
    return ESP_OK;
}
static void write_book(const char *path,const char *text) {
    FILE *f=fopen(path,"wb");assert(f);assert(fputs(text,f)>=0);assert(!fclose(f));
}
static bool file_exists(const char *path) { struct stat st;return stat(path,&st)==0; }
int main(int argc,char **argv) {
    assert(argc==2);
    assert(realpath(argv[1],rootdir));
    assert(!chdir(rootdir));
    book_store_root_t roots[2]; int count=0;
    assert(book_store_roots(roots,&count)==ESP_OK && count==1 && !roots[0].is_flash);
    assert(!strcmp(roots[0].path,"/sdcard/books"));
    const char *path="/sdcard/books/progress.txt";
    FILE *f=fopen(path,"wb");assert(f);fputs("Chapter 1\n真实的本地预览阅读进度。\n",f);assert(!fclose(f));
    book_progress_t value={.file_size=123,.chapter=2,.byte_off=19,.px=48,.pct=37},out={0};
    assert(book_progress_save(path,&value)==ESP_OK);
    assert(book_progress_set_last_path(path)==ESP_OK);
    assert(book_progress_load(path,123,&out));
    assert(out.chapter==2 && out.byte_off==19 && out.px==48 && out.pct==37 && out.last_open_s>0);
    uint32_t old_sequence=out.last_open_s;
    assert(!book_progress_load(path,124,&out));
    assert(book_progress_save(path,&value)==ESP_OK && book_progress_load(path,123,&out) && out.last_open_s>old_sequence);
    char last[BOOK_STORE_PATH_MAX]; assert(book_progress_last_path(last,sizeof(last)) && !strcmp(last,path));
    book_progress_watch_t *watch=book_progress_watch_create(path);assert(watch && !book_progress_watch_invalidated(watch));
    bool removed=true;
    assert(book_store_delete("/sdcard/books/../outside.txt",&removed)!=ESP_OK && !removed);
    assert(book_store_delete("/sdcard/books/not-a-book.bin",&removed)!=ESP_OK && !removed);
    assert(!symlink("progress.txt","./tf/books/link.txt"));
    assert(book_store_delete("/sdcard/books/link.txt",&removed)!=ESP_OK && !removed);
    assert(!unlink("./tf/books/link.txt"));
    assert(book_store_delete(path,&removed)==ESP_OK && removed);
    assert(book_progress_watch_invalidated(watch));book_progress_watch_destroy(watch);
    assert(!book_progress_load(path,123,&out));assert(!book_progress_last_path(last,sizeof(last)));
    // 真实删除实现：现存原书+旧覆盖备份、仅有覆盖备份、无文件均不能复活已删图书。
    // Actual deletion handles original+backup, backup-only and missing files without resurrection.
    const char *book="/sdcard/books/backup.epub",*backup="/sdcard/books/backup.epub.rename-backup";
    write_book(book,"new-book");write_book(backup,"old-book");
    assert(book_store_delete(book,&removed)==ESP_OK && removed && !file_exists(book) && !file_exists(backup));
    write_book(backup,"old-book");
    assert(book_store_delete(book,&removed)==ESP_OK && removed && !file_exists(book) && !file_exists(backup));
    assert(book_store_delete(book,&removed)==ESP_OK && removed);
    write_book(book,"preserve-me");
    assert(!mkdir(backup,0700));
    assert(book_store_delete(book,&removed)==ESP_ERR_INVALID_ARG && !removed && file_exists(book) && file_exists(backup));
    assert(!rmdir(backup));
    sd_mounted=false;
    assert(book_store_delete(book,&removed)==ESP_ERR_INVALID_STATE && !removed && file_exists(book));
    sd_mounted=true;assert(book_store_delete(book,&removed)==ESP_OK && removed);
    assert(book_store_free_bytes(&roots[0])>0);
    unsigned revision=book_store_revision();book_store_notify_changed();assert(book_store_revision()==revision+1);
    puts("native preview book storage: actual progress, deletion/backup recovery, ownership and invalidation passed");
    return 0;
}

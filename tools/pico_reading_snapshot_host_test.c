/* SPDX-License-Identifier: Apache-2.0
 * 中文：双缓冲快照发布与真实C产物夹具；只访问构建目录的合成数据。
 * English: Double-buffer snapshot publication and actual C fixtures, using synthetic build-directory data only.
 */
#define PICO_READER_SNAPSHOT_REAL_STORE
#define PICO_READER_NO_FIXTURES
#define main reader_original_main
#include "pico_reader_host_test.c"
#undef main
#include <ctype.h>
#define PICO_STORE_COLD
#define PICO_READING_SNAPSHOT_PARENT "build/reading-snapshot-fixture/readpico"
static bool s_transfer_prepared,s_external_active;
static unsigned snapshot_fail_after;
static bool good_key(const char *key){return key&&strlen(key)<40&&!strchr(key,'/')&&!strchr(key,'\\')&&!strstr(key,"..");}
static uint32_t crc32(const void *data,size_t bytes){return mock_fs_crc(data,bytes);}
static bool store_file(const char *root,const char *key,const void *data,size_t bytes){if(snapshot_fail_after&&!--snapshot_fail_after)return false;return mock_fs_store(root,key,data,bytes);}
static bool snapshot_rename_fail_once;
// 模拟FatFS不覆盖目标及最终提升失败，不能只靠POSIX覆盖语义。/ Simulate FatFS no-replace rename and final-promotion failure instead of relying on POSIX replacement.
static int snapshot_fat_rename(const char *source,const char *destination){
    struct stat st;if(!lstat(destination,&st)){errno=EEXIST;return -1;}
    if(snapshot_rename_fail_once&&strstr(source,"/manifest.tmp")){snapshot_rename_fail_once=false;errno=EIO;return -1;}
    return rename(source,destination);
}
#define rename snapshot_fat_rename
#include "../main/system/pico_reading_snapshot.inc"
#undef rename
static void file_read(const char *path,char *out,size_t size){FILE *f=fopen(path,"rb");assert(f);size_t n=fread(out,1,size-1,f);out[n]=0;assert(!ferror(f)&&!fclose(f));}
static void clear_directory(const char *path){DIR *dir=opendir(path);if(!dir)return;struct dirent *e;while((e=readdir(dir)))if(strcmp(e->d_name,".")&&strcmp(e->d_name,"..")){char file[512];snprintf(file,sizeof(file),"%s/%s",path,e->d_name);struct stat st;assert(!lstat(file,&st));if(S_ISDIR(st.st_mode)){clear_directory(file);assert(!rmdir(file));}else assert(!unlink(file));}assert(!closedir(dir));}
int main(void){
    const char *base="build/reading-snapshot-fixture";assert(mkdir(base,0700)==0||errno==EEXIST);clear_directory(base);
    make_v3_fixture(source_blobs,2,10);make_v3_fixture(blobs,3,20);
    uint64_t internal_hash=bank_hash(source_blobs,""),tf_hash=bank_hash(blobs,"");
    char manifest[512],old[512],path[256],backup[256];snprintf(path,sizeof(path),"%s/manifest.json",READING_SNAPSHOT_ROOT);snprintf(backup,sizeof(backup),"%s/manifest.bak",READING_SNAPSHOT_ROOT);
    assert(reading_snapshot_prepare());file_read(path,manifest,sizeof(manifest));assert(strstr(manifest,"internal-a")&&strstr(manifest,"tf-a")&&strstr(manifest,"\"sequence\":1"));
    reader_index_v3_t *index=calloc(1,sizeof(*index));assert(index);
    assert(mock_fs_load(READING_SNAPSHOT_ROOT "/internal-a","reader-r3-index",index,sizeof(*index))&&index->count==2);
    assert(mock_fs_load(READING_SNAPSHOT_ROOT "/tf-a","reader-r3-index",index,sizeof(*index))&&index->count==3);
    assert(reading_snapshot_prepare());file_read(path,manifest,sizeof(manifest));assert(strstr(manifest,"internal-b")&&strstr(manifest,"\"sequence\":2"));strcpy(old,manifest);
    uint64_t current_internal=mock_fs_hash(READING_SNAPSHOT_ROOT "/internal-b",""),current_tf=mock_fs_hash(READING_SNAPSHOT_ROOT "/tf-b","");
    snapshot_rename_fail_once=true;assert(!reading_snapshot_prepare());file_read(path,manifest,sizeof(manifest));assert(!strcmp(old,manifest));
    assert(mock_fs_hash(READING_SNAPSHOT_ROOT "/internal-b","")==current_internal&&mock_fs_hash(READING_SNAPSHOT_ROOT "/tf-b","")==current_tf);
    snapshot_fail_after=3;assert(!reading_snapshot_prepare());snapshot_fail_after=0;file_read(path,manifest,sizeof(manifest));assert(!strcmp(old,manifest));
    assert(mock_fs_hash(READING_SNAPSHOT_ROOT "/internal-b","")==current_internal&&mock_fs_hash(READING_SNAPSHOT_ROOT "/tf-b","")==current_tf);
    assert(reading_snapshot_prepare());file_read(path,manifest,sizeof(manifest));assert(strstr(manifest,"internal-a")&&strstr(manifest,"\"sequence\":3"));
    assert(bank_hash(source_blobs,"")==internal_hash&&bank_hash(blobs,"")==tf_hash);
    // 模拟轮换清单后断电，仅备份存在；失败导出必须恢复并保留备份所指的完整银行。
    // Simulate power loss with only the backup manifest; a failed export must recover and retain that complete bank.
    current_internal=mock_fs_hash(READING_SNAPSHOT_ROOT "/internal-a","");current_tf=mock_fs_hash(READING_SNAPSHOT_ROOT "/tf-a","");strcpy(old,manifest);
    assert(!unlink(backup)&&!rename(path,backup));snapshot_fail_after=3;assert(!reading_snapshot_prepare());snapshot_fail_after=0;
    file_read(path,manifest,sizeof(manifest));assert(!strcmp(old,manifest)&&access(backup,F_OK)!=0&&errno==ENOENT);
    assert(mock_fs_hash(READING_SNAPSHOT_ROOT "/internal-a","")==current_internal&&mock_fs_hash(READING_SNAPSHOT_ROOT "/tf-a","")==current_tf);
    assert(reading_snapshot_prepare());file_read(path,manifest,sizeof(manifest));assert(strstr(manifest,"internal-b")&&strstr(manifest,"\"sequence\":4"));strcpy(old,manifest);
    // 损坏的独存备份必须在清理任何银行前拒绝；未知路径不能成为清理目标。
    // Reject a malformed backup before clearing either bank; an unknown path must never become a cleanup target.
    assert(!unlink(backup)&&!rename(path,backup));
    const char *invalid[]={"{broken}","{\"version\":1,\"complete\":true,\"sources\":[{\"directory\":\"../../\",\"index_crc32\":1,\"sequence\":4}]}","{\"version\":1,\"complete\":true,\"sources\":[{\"directory\":\"internal-a\",\"index_crc32\":1,\"sequence\":4},{\"directory\":\"tf-b\",\"index_crc32\":2,\"sequence\":4}]}","{\"version\":1,\"complete\":true,\"sources\":[{\"directory\":\"internal-a\",\"index_crc32\":1,\"sequence\":0},{\"directory\":\"tf-a\",\"index_crc32\":2,\"sequence\":0}]}","{\"version\":1,\"complete\":true,\"sources\":[{\"directory\":\"internal-a\",\"index_crc32\":1,\"sequence\":4},{\"directory\":\"tf-a\",\"index_crc32\":2,\"sequence\":4}]}junk"};
    uint64_t bank_hashes[4]={mock_fs_hash(READING_SNAPSHOT_ROOT "/internal-a",""),mock_fs_hash(READING_SNAPSHOT_ROOT "/tf-a",""),mock_fs_hash(READING_SNAPSHOT_ROOT "/internal-b",""),mock_fs_hash(READING_SNAPSHOT_ROOT "/tf-b","")};
    for(size_t i=0;i<sizeof(invalid)/sizeof(*invalid);i++){
        FILE *f=fopen(backup,"wb");assert(f&&fputs(invalid[i],f)>=0&&!fclose(f));assert(!reading_snapshot_prepare()&&access(path,F_OK)!=0&&errno==ENOENT);
        assert(mock_fs_hash(READING_SNAPSHOT_ROOT "/internal-a","")==bank_hashes[0]&&mock_fs_hash(READING_SNAPSHOT_ROOT "/tf-a","")==bank_hashes[1]&&mock_fs_hash(READING_SNAPSHOT_ROOT "/internal-b","")==bank_hashes[2]&&mock_fs_hash(READING_SNAPSHOT_ROOT "/tf-b","")==bank_hashes[3]);
    }
    FILE *restore=fopen(backup,"wb");assert(restore&&fputs(old,restore)>=0&&!fclose(restore));assert(!rename(backup,path));
    uint32_t crc;assert(!ps_reading_snapshot_store("/tmp","reader-r3-index",index,sizeof(*index),&crc));
    assert(!ps_reading_snapshot_store(READING_SNAPSHOT_ROOT "/tf-b","system_v1",index,sizeof(*index),&crc));
    char unknown[256];snprintf(unknown,sizeof(unknown),"%s/keep-user.txt",READING_SNAPSHOT_ROOT "/tf-a");FILE *f=fopen(unknown,"wb");assert(f&&fputs("unrelated",f)>=0&&!fclose(f));
    assert(reading_snapshot_prepare());assert(!access(unknown,F_OK));
    // 末次留下可供Swift生产解析器读取的完整C夹具。/ Leave the final complete C fixture for the production Swift parser.
    file_read(path,manifest,sizeof(manifest));assert(strstr(manifest,"\"sequence\":5"));free(index);blobs_clear(blobs);blobs_clear(source_blobs);
    puts("PASS: production double-bank export, internal+TF libraries, CRC manifest, FAT no-replace rename, manifest rollback and backup-only power-loss recovery, malformed-backup fail-closed, source preservation, failure keeps prior bank, exact-owned cleanup; fixture build/reading-snapshot-fixture");return 0;
}

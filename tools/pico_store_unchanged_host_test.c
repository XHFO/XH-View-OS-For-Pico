#define PICO_STORE_COLD
/*
 * 中文：真实临时文件与故障注入验证相同存储免写，不访问物理TF卡。
 * English: Verify unchanged-store I/O with real temporary files and fault injection, never a physical card.
 */
#define _DARWIN_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>

static unsigned checks, failures, case_count;
#define CHECK(v,label) do {checks++;if(!(v)){failures++;fprintf(stderr,"FAIL %s:%d %s\n",__FILE__,__LINE__,label);}}while(0)
typedef struct {unsigned reads,write_opens,writes,flushes,syncs,renames,unlinks,mkdirs,closes;} counts_t;
static counts_t io;
static unsigned fail_write,fail_rename;
static bool fail_open_write,fail_flush,fail_close_write,fail_close_read,fail_read_open,fail_read_eof,fail_mkdir;
static int sync_errno;
static FILE *write_file;
static bool s_store_lost,s_transfer_prepared,s_external_active,test_busy;
static char s_store_root[200],last_toast[256];
static unsigned nvs_calls,toasts;
typedef int nvs_handle_t;
typedef int esp_err_t;
#define ESP_OK 0
#define NVS_READWRITE 1
static int nvs_open(const char*a,int b,nvs_handle_t*c){(void)a;(void)b;(void)c;nvs_calls++;return -1;}
static int nvs_set_blob(nvs_handle_t a,const char*b,const void*c,size_t d){(void)a;(void)b;(void)c;(void)d;nvs_calls++;return -1;}
static int nvs_commit(nvs_handle_t a){(void)a;nvs_calls++;return -1;}
static void nvs_close(nvs_handle_t a){(void)a;nvs_calls++;}
static bool ps_storage_busy(void){return test_busy;}
static void ps_toast(const char*s){toasts++;snprintf(last_toast,sizeof(last_toast),"%s",s);}
static void ps_sound_error(void){}
static FILE *test_fopen(const char*p,const char*m){
    if(strchr(m,'w')){io.write_opens++;if(fail_open_write){errno=EROFS;return NULL;}}
    else{io.reads++;if(fail_read_open){errno=EIO;return NULL;}}
    FILE*f=fopen(p,m);if(f&&strchr(m,'w'))write_file=f;return f;
}
static size_t test_fwrite(const void*p,size_t a,size_t n,FILE*f){io.writes++;if(fail_write==io.writes){errno=ENOSPC;return n?n-1:0;}return fwrite(p,a,n,f);}
static int test_fflush(FILE*f){io.flushes++;if(fail_flush){errno=EIO;return EOF;}return fflush(f);}
static int test_fsync(int fd){io.syncs++;if(sync_errno){errno=sync_errno;return -1;}return fsync(fd);}
static int test_fclose(FILE*f){bool writing=f==write_file;io.closes++;int result=fclose(f);if(writing)write_file=NULL;if((writing&&fail_close_write)||(!writing&&fail_close_read)){errno=EIO;return EOF;}return result;}
static int test_fgetc(FILE*f){if(fail_read_eof){errno=EIO;return EOF;}return fgetc(f);}
static int test_ferror(FILE*f){return fail_read_eof?1:ferror(f);}
static int test_rename(const char*a,const char*b){io.renames++;if(fail_rename==io.renames){errno=EIO;return -1;}return rename(a,b);}
static int test_unlink(const char*p){io.unlinks++;return unlink(p);}
static int test_mkdir(const char*p,mode_t m){io.mkdirs++;if(fail_mkdir){errno=EROFS;return -1;}return mkdir(p,m);}
#define fopen test_fopen
#define fwrite test_fwrite
#define fflush test_fflush
#define fsync test_fsync
#define fclose test_fclose
#define fgetc test_fgetc
#define ferror test_ferror
#define rename test_rename
#define unlink test_unlink
#define mkdir test_mkdir
#include "actual_store.inc"
#undef fopen
#undef fwrite
#undef fflush
#undef fsync
#undef fclose
#undef fgetc
#undef ferror
#undef rename
#undef unlink
#undef mkdir
#include "collision.inc"

enum {MAX_FILE=65536+32};
typedef struct {bool exists;size_t n;uint8_t bytes[MAX_FILE];struct stat st;} snapshot_t;
static char primary[256],backup[256],temporary[256];
static uint8_t payload[65536],previous[65536];
static void reset_io(void){memset(&io,0,sizeof(io));fail_write=fail_rename=0;fail_open_write=fail_flush=fail_close_write=fail_close_read=fail_read_open=fail_read_eof=fail_mkdir=false;sync_errno=0;write_file=NULL;nvs_calls=toasts=0;last_toast[0]=0;s_store_lost=s_transfer_prepared=s_external_active=test_busy=false;}
static void remove_files(void){unlink(primary);unlink(backup);unlink(temporary);}
static snapshot_t snapshot(const char*p){
    snapshot_t s={0};if(stat(p,&s.st))return s;s.exists=true;FILE*f=fopen(p,"rb");CHECK(f!=NULL,"snapshot opens real fixture");if(f){s.n=fread(s.bytes,1,sizeof(s.bytes),f);CHECK(!ferror(f)&&fgetc(f)==EOF,"snapshot reads complete fixture");fclose(f);}return s;
}
static bool same_snapshot(const snapshot_t*a,const snapshot_t*b){
    if(a->exists!=b->exists)return false;if(!a->exists)return true;
    return a->n==b->n&&!memcmp(a->bytes,b->bytes,a->n)&&a->st.st_ino==b->st.st_ino&&a->st.st_mtimespec.tv_sec==b->st.st_mtimespec.tv_sec&&a->st.st_mtimespec.tv_nsec==b->st.st_mtimespec.tv_nsec;
}
static void stamp(const char*p){struct timespec t[2]={{1234567890,123456789},{1234567890,987654321}};CHECK(utimensat(AT_FDCWD,p,t,0)==0,"set old deterministic nanosecond fixture time");}
static void write_raw(const char*p,const uint8_t*b,size_t n){FILE*f=fopen(p,"wb");CHECK(f!=NULL,"open raw fixture");if(f){CHECK(fwrite(b,1,n,f)==n,"write raw fixture");CHECK(!fclose(f),"close raw fixture");}}
static void write_blob(const char*p,const void*b,size_t n){uint8_t*wire=malloc(n+12);CHECK(wire!=NULL,"allocate fixture");if(!wire)exit(2);uint32_t h[3]={0x31535058,(uint32_t)n,crc32(b,n)};memcpy(wire,h,12);memcpy(wire+12,b,n);write_raw(p,wire,n+12);free(wire);}
static void expect_payload(const char*p,const void*b,size_t n){uint8_t*read=malloc(n);CHECK(read!=NULL,"allocate read buffer");if(!read)exit(2);CHECK(load_file(p,read,n),"actual load verifies current header CRC complete EOF");CHECK(!memcmp(read,b,n),"actual load returns exact requested payload");free(read);}
static void emit(const char*name,bool unchanged_primary,bool unchanged_backup){
    if(case_count++)printf(",\n");printf("{\"name\":\"%s\",\"read_opens\":%u,\"write_opens\":%u,\"fwrite\":%u,\"fflush\":%u,\"fsync\":%u,\"rename\":%u,\"unlink\":%u,\"mkdir\":%u,\"primary_full_bytes_inode_mtime_unchanged\":%s,\"backup_full_bytes_inode_mtime_unchanged\":%s}",name,io.reads,io.write_opens,io.writes,io.flushes,io.syncs,io.renames,io.unlinks,io.mkdirs,unchanged_primary?"true":"false",unchanged_backup?"true":"false");
}
static void expect_no_write(void){CHECK(!io.write_opens&&!io.writes&&!io.flushes&&!io.syncs&&!io.renames&&!io.unlinks,"exact current primary performs zero write/flush/fsync/rename/unlink");}
static void identical(void){
    remove_files();write_blob(primary,payload,4097);write_blob(backup,previous,4097);stamp(primary);stamp(backup);
    snapshot_t a=snapshot(primary),b=snapshot(backup);reset_io();
    CHECK(store_file(s_store_root,"record",payload,4097),"identical durable primary reports success");expect_no_write();
    snapshot_t c=snapshot(primary),d=snapshot(backup);CHECK(same_snapshot(&a,&c)&&same_snapshot(&b,&d),"full primary and backup bytes/inode/nanosecond mtime unchanged");
    emit("exact-current-primary-zero-I/O",same_snapshot(&a,&c),same_snapshot(&b,&d));
}
static void boundaries(void){
    const size_t sizes[]={1,255,256,257,4097,65536};
    for(unsigned i=0;i<sizeof(sizes)/sizeof(sizes[0]);i++){
        size_t n=sizes[i];remove_files();write_blob(primary,payload,n);reset_io();CHECK(store_file(s_store_root,"record",payload,n),"complete exact payload matches across block boundaries");expect_no_write();
        char name[80];snprintf(name,sizeof(name),"exact-size-%zu",n);emit(name,true,true);
        uint8_t changed[65536];memcpy(changed,payload,n);changed[n-1]^=1;reset_io();CHECK(store_file(s_store_root,"record",changed,n),"last byte difference commits at every tested size");CHECK(io.writes==2&&io.syncs==1&&io.renames==2,"different input performs original atomic commit");expect_payload(primary,changed,n);snprintf(name,sizeof(name),"last-byte-diff-size-%zu",n);emit(name,false,false);
    }
}
static void collision_case(void){
    CHECK(memcmp(collision_a,collision_b,8)&&crc32(collision_a,8)==crc32(collision_b,8),"fixture is a real same-length CRC32 collision");
    remove_files();write_blob(primary,collision_a,8);reset_io();CHECK(store_file(s_store_root,"record",collision_b,8),"CRC collision still commits changed bytes");CHECK(io.writes==2&&io.syncs==1&&io.renames==2,"matching header and CRC alone never skip payload compare");expect_payload(primary,collision_b,8);expect_payload(backup,collision_a,8);emit("valid-same-CRC-different-payload",false,false);
}
static void corruptions(void){
    const char*names[]={"bad-magic","bad-length","bad-CRC","corrupt-payload","truncated-header","truncated-payload","trailing-garbage"};
    for(unsigned i=0;i<7;i++){
        remove_files();write_blob(primary,payload,4097);write_blob(backup,payload,4097);stamp(backup);snapshot_t before=snapshot(backup),p=snapshot(primary);
        if(i==0)p.bytes[0]^=1;if(i==1)p.bytes[4]^=1;if(i==2)p.bytes[8]^=1;if(i==3)p.bytes[p.n-1]^=1;if(i==4)p.n=7;if(i==5)p.n--;if(i==6)p.bytes[p.n++]=0xA5;
        write_raw(primary,p.bytes,p.n);reset_io();CHECK(store_file(s_store_root,"record",payload,4097),"corrupt primary requires a real write even when backup already matches");CHECK(io.writes==2&&io.syncs==1&&io.renames==1,"invalid current primary cannot take unchanged shortcut");expect_payload(primary,payload,4097);snapshot_t after=snapshot(backup);CHECK(same_snapshot(&before,&after),"corrupt primary never replaces good matching backup");emit(names[i],false,same_snapshot(&before,&after));
    }
}
static void replacement(void){
    remove_files();write_blob(primary,payload,4097);reset_io();CHECK(store_file(s_store_root,"record",payload,4097),"prime identical call");expect_no_write();
    write_blob(temporary,previous,4097);CHECK(!rename(temporary,primary),"external replacement of same path");reset_io();CHECK(store_file(s_store_root,"record",payload,4097),"replaced path revalidated without stale cached success");CHECK(io.writes==2&&io.syncs==1&&io.renames==2,"external valid replacement must commit new content");expect_payload(primary,payload,4097);expect_payload(backup,previous,4097);emit("external-path-replacement-between-saves",false,false);
}
static void failed_io(void){
    const char*names[]={"tmp-open-EROFS","header-write-ENOSPC","payload-write-ENOSPC","flush-EIO","fsync-EIO","close-write-EIO","first-rename-EIO","final-rename-EIO","read-open-and-write-open-EIO"};
    for(unsigned i=0;i<9;i++){
        remove_files();write_blob(primary,previous,4097);write_blob(backup,payload,4097);snapshot_t old=snapshot(primary);reset_io();
        if(i==0)fail_open_write=true;if(i==1)fail_write=1;if(i==2)fail_write=2;if(i==3)fail_flush=true;if(i==4)sync_errno=EIO;if(i==5)fail_close_write=true;if(i==6)fail_rename=1;if(i==7)fail_rename=2;if(i==8){fail_read_open=true;fail_open_write=true;}
        CHECK(!ps_store("record",payload,4097),"failed file save never reports success or falls back to NVS");CHECK(!nvs_calls&&toasts==1,"file failure reports one error with no hidden NVS fallback");
        snapshot_t a=snapshot(primary),b=snapshot(backup);CHECK((a.exists&&a.n==old.n&&!memcmp(a.bytes,old.bytes,a.n))||(b.exists&&b.n==old.n&&!memcmp(b.bytes,old.bytes,b.n)),"failure retains previously valid payload in current or backup");CHECK(access(temporary,F_OK)!=0,"failed commit removes temporary file");emit(names[i],same_snapshot(&old,&a),false);
    }
}
static void read_failure_not_match(void){
    for(int mode=0;mode<2;mode++){
        remove_files();write_blob(primary,payload,4097);reset_io();if(mode)fail_read_eof=true;else fail_close_read=true;
        CHECK(store_file(s_store_root,"record",payload,4097),"read close/error rejects shortcut but permits proper commit");CHECK(io.writes==2&&io.syncs==1,"EOF error or close failure cannot claim identical without write");counts_t result=io;reset_io();expect_payload(primary,payload,4097);io=result;emit(mode?"EOF-read-error-not-equal":"read-close-error-not-equal",false,false);
    }
}
static void unavailable(void){
    const char*names[]={"lost-card","storage-maintenance","USB-file-owner","external-file-owner"};
    for(int i=0;i<4;i++){
        remove_files();write_blob(primary,payload,4097);reset_io();s_store_lost=i==0;test_busy=i==1;s_transfer_prepared=i==2;s_external_active=i==3;
        CHECK(!ps_store("record",payload,4097),"unavailable/owned storage blocks unchanged shortcut through public API");CHECK(!io.reads&&!nvs_calls&&toasts==1,"blocked store never reads lost/owned card or reports cached success");expect_no_write();emit(names[i],true,true);
    }
    reset_io();remove_files();CHECK(!rmdir(s_store_root),"physically remove fixture card path");fail_mkdir=true;CHECK(!ps_store("record",payload,4097),"missing card path plus unwritable mount cannot report unchanged success");CHECK(io.writes==0&&!nvs_calls,"missing/writable failure never falls back silently");emit("missing-card-path-write-denied",false,false);reset_io();CHECK(!mkdir(s_store_root,0700),"restore fixture directory");
}
int main(int argc,char**argv){
    CHECK(argc==2,"temporary root argument");if(argc!=2)return 2;CHECK(strlen(argv[1])<sizeof(s_store_root),"bounded isolated root");snprintf(s_store_root,sizeof(s_store_root),"%s",argv[1]);CHECK(!mkdir(s_store_root,0700),"create isolated fixture root");
    snprintf(primary,sizeof(primary),"%s/record.dat",s_store_root);snprintf(backup,sizeof(backup),"%s/record.bak",s_store_root);snprintf(temporary,sizeof(temporary),"%s/record.tmp",s_store_root);
    for(size_t i=0;i<sizeof(payload);i++){payload[i]=(uint8_t)(i*37+i/251);previous[i]=payload[i]^0x5A;}
    printf("{\"cases\":[\n");identical();boundaries();collision_case();corruptions();replacement();failed_io();read_failure_not_match();unavailable();
    printf("\n],\"checks\":%u,\"failures\":%u,\"result\":\"%s\"}\n",checks,failures,failures?"FAIL":"PASS");return failures?1:0;
}

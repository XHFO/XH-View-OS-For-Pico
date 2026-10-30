/* SPDX-License-Identifier: Apache-2.0
 * 中文：USB交接前将两处已提交阅读记录导出到TF双缓冲快照；源库始终只读。
 * English: Export committed reading libraries to double-buffered TF snapshots before USB handoff, with read-only sources.
 * 冻结：不导出草稿/设置/孤儿对象，不切换源库，不把半套快照提交给电脑。
 * Frozen: Never export drafts, settings or orphan objects, switch source stores, or publish a partial snapshot.
 */
#ifndef PICO_READING_SNAPSHOT_PARENT
#define PICO_READING_SNAPSHOT_PARENT "/sdcard/readpico"
#endif
#define READING_SNAPSHOT_ROOT PICO_READING_SNAPSHOT_PARENT "/reading-notes"
static int snapshot_stat(const char *path,struct stat *st){
#ifdef ESP_PLATFORM
    return stat(path,st);
#else
    return lstat(path,st);
#endif
}
static bool snapshot_directory(const char *path){
    struct stat st;
    if(snapshot_stat(path,&st)==0)return S_ISDIR(st.st_mode)&&!S_ISLNK(st.st_mode);
    return errno==ENOENT&&mkdir(path,0700)==0;
}
static bool snapshot_root_allowed(const char *root){
    const char *names[]={"internal-a","internal-b","tf-a","tf-b"};
    for(unsigned i=0;i<4;i++){char path[160];snprintf(path,sizeof(path),READING_SNAPSHOT_ROOT "/%s",names[i]);if(!strcmp(path,root))return true;}
    return false;
}
bool ps_reading_snapshot_store(const char *root,const char *key,const void *data,size_t bytes,uint32_t *crc){
    if(!root||!key||!data||!bytes||bytes>65536||!snapshot_root_allowed(root)||!good_key(key)||strncmp(key,"reader-r3-",10)||
       ps_storage_busy()||s_transfer_prepared||s_external_active||!snapshot_directory(root))return false;
    bool ok=store_file(root,key,data,bytes);if(ok&&crc)*crc=crc32(data,bytes);return ok;
}
static bool snapshot_owned_name(const char *name){
    if(!strncmp(name,"reader-r3-index.",16))return !strcmp(name+16,"dat")||!strcmp(name+16,"bak")||!strcmp(name+16,"tmp");
    if(strncmp(name,"reader-r3-o-",12))return false;
    const char *p=name+12;if(!isdigit((unsigned char)*p))return false;
    while(isdigit((unsigned char)*p))p++;
    return !strcmp(p,".dat")||!strcmp(p,".bak")||!strcmp(p,".tmp");
}
static bool snapshot_clear(const char *root){
    if(!snapshot_root_allowed(root)||!snapshot_directory(root))return false;
    DIR *dir=opendir(root);if(!dir)return false;bool ok=true;struct dirent *entry;
    while((entry=readdir(dir)))if(snapshot_owned_name(entry->d_name)){
        char path[256];int n=snprintf(path,sizeof(path),"%s/%s",root,entry->d_name);struct stat st;
        if(n<0||(size_t)n>=sizeof(path)||snapshot_stat(path,&st)||!S_ISREG(st.st_mode)||S_ISLNK(st.st_mode)||unlink(path)){ok=false;break;}
    }
    if(closedir(dir))ok=false;
    return ok;
}
static PICO_STORE_COLD bool reading_snapshot_prepare(void){
    char current[160],other[160];bool sd;
    if(!ps_storage_record_roots(current,other,sizeof(current),&sd))return false;
    if(!snapshot_directory(PICO_READING_SNAPSHOT_PARENT)||!snapshot_directory(READING_SNAPSHOT_ROOT))return false;
    const char *base=READING_SNAPSHOT_ROOT;char path[192],backup[192],old[512]={0};
    snprintf(path,sizeof(path),"%s/manifest.json",base);snprintf(backup,sizeof(backup),"%s/manifest.bak",base);
    struct stat st;bool bank_b=false,restore=false;uint32_t sequence=1;const char *previous=path;
    int present=snapshot_stat(path,&st);
    if(present!=0&&errno==ENOENT){previous=backup;present=snapshot_stat(backup,&st);restore=present==0;}
    if(present==0){
        if(!S_ISREG(st.st_mode)||S_ISLNK(st.st_mode)||st.st_size<=0||st.st_size>=(off_t)sizeof(old))return false;
        FILE *f=fopen(previous,"rb");if(!f)return false;
        size_t n=fread(old,1,sizeof(old)-1,f);bool ok=!ferror(f)&&n==(size_t)st.st_size;
        if(fclose(f))ok=false;
        if(!ok)return false;
        // 清单由本机生成；严格检查固定来源、同代双缓冲和数值边界，再恢复断电留下的备份。
        // Validate the device-generated manifest's fixed sources, bank/generation and bounds before recovering a power-loss backup.
        char internal_bank=0,tf_bank=0;unsigned long ic=0,tc=0,is=0,ts=0;int consumed=0;
        int fields=sscanf(old,"{\"version\":1,\"complete\":true,\"sources\":[{\"directory\":\"internal-%c\",\"index_crc32\":%lu,\"sequence\":%lu},{\"directory\":\"tf-%c\",\"index_crc32\":%lu,\"sequence\":%lu}]}%n",&internal_bank,&ic,&is,&tf_bank,&tc,&ts,&consumed);
        if(fields!=6||!consumed||(internal_bank!='a'&&internal_bank!='b')||tf_bank!=internal_bank||ic>UINT32_MAX||tc>UINT32_MAX||!is||is>=UINT32_MAX||is!=ts)return false;
        for(const char *at=old+consumed;*at;at++)if(!isspace((unsigned char)*at))return false;
        bank_b=internal_bank=='a';sequence=(uint32_t)is+1;
        if(restore&&rename(backup,path)!=0)return false;
    }else if(errno!=ENOENT)return false;
    char internal[160],tf[160];snprintf(internal,sizeof(internal),"%s/internal-%c",base,bank_b?'b':'a');snprintf(tf,sizeof(tf),"%s/tf-%c",base,bank_b?'b':'a');
    if(!snapshot_clear(internal)||!snapshot_clear(tf))return false;
    uint32_t internal_crc=0,tf_crc=0;
    if(!ps_reader_records_export(sd?other:current,internal,sequence,&internal_crc)||!ps_reader_records_export(sd?current:other,tf,sequence,&tf_crc))return false;
    char manifest[400];int count=snprintf(manifest,sizeof(manifest),"{\"version\":1,\"complete\":true,\"sources\":[{\"directory\":\"internal-%c\",\"index_crc32\":%lu,\"sequence\":%lu},{\"directory\":\"tf-%c\",\"index_crc32\":%lu,\"sequence\":%lu}]}\n",bank_b?'b':'a',(unsigned long)internal_crc,(unsigned long)sequence,bank_b?'b':'a',(unsigned long)tf_crc,(unsigned long)sequence);
    if(count<=0||(size_t)count>=sizeof(manifest))return false;
    char tmp[192];snprintf(tmp,sizeof(tmp),"%s/manifest.tmp",base);FILE *f=fopen(tmp,"wb");if(!f)return false;
    bool ok=fwrite(manifest,1,(size_t)count,f)==(size_t)count&&fflush(f)==0;
    if(ok&&fsync(fileno(f))!=0&&errno!=ENOSYS&&errno!=EINVAL)ok=false;
    if(fclose(f))ok=false;
    // FAT不保证覆盖式rename；所有来源准备好后再轮换清单，失败尝试恢复旧清单。/ FAT need not replace on rename; rotate the manifest only after both sources are ready and restore it on failure.
    bool rotated=false;
    if(ok&&snapshot_stat(path,&st)==0){
        if(unlink(backup)!=0&&errno!=ENOENT)ok=false;
        if(ok){rotated=rename(path,backup)==0;if(!rotated)ok=false;}
    }else if(ok&&errno!=ENOENT)ok=false;
    if(ok&&rename(tmp,path)==0)return true;
    int error=errno;if(rotated)(void)rename(backup,path);unlink(tmp);errno=error;return false;
}

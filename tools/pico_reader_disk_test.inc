/* SPDX-License-Identifier: Apache-2.0
 * 中文：阅读迁移测试的两个独立临时文件根；沿用产品长度/CRC封装，不代替FAT实机验收。
 * English: Two independent temporary filesystem roots for reader migration, using product length/CRC framing without claiming device FAT acceptance.
 */
static uint32_t mock_fs_crc(const void *data,size_t n){const uint8_t *p=data;uint32_t c=~0u;for(size_t i=0;i<n;i++){c^=p[i];for(int b=0;b<8;b++)c=(c>>1)^(0xedb88320u&-(c&1u));}return ~c;}
static bool mock_fs_store(const char *root,const char *key,const void *data,size_t bytes){
    char path[160],temporary[160];snprintf(path,sizeof(path),"%s/%s.dat",root,key);snprintf(temporary,sizeof(temporary),"%s/%s.tmp",root,key);
    FILE *f=fopen(temporary,"wb");if(!f)return false;uint32_t header[]={0x31535058,(uint32_t)bytes,mock_fs_crc(data,bytes)};
    bool ok=fwrite(header,1,sizeof(header),f)==sizeof(header)&&fwrite(data,1,bytes,f)==bytes&&fflush(f)==0;
    if(fclose(f)!=0)ok=false;if(ok&&rename(temporary,path)==0)return true;unlink(temporary);return false;
}
static bool mock_fs_load(const char *root,const char *key,void *data,size_t bytes){
    char path[160];snprintf(path,sizeof(path),"%s/%s.dat",root,key);FILE *f=fopen(path,"rb");if(!f)return false;
    uint32_t h[3];bool ok=fread(h,1,sizeof(h),f)==sizeof(h)&&h[0]==0x31535058&&h[1]==bytes&&fread(data,1,bytes,f)==bytes&&fgetc(f)==EOF&&mock_fs_crc(data,bytes)==h[2];
    fclose(f);return ok;
}
static bool mock_fs_exists(const char *root,const char *key,bool *exists){char path[160];snprintf(path,sizeof(path),"%s/%s.dat",root,key);struct stat st;if(stat(path,&st)==0){*exists=S_ISREG(st.st_mode);return *exists;}*exists=false;return errno==ENOENT;}
static uint64_t mock_fs_hash(const char *root,const char *prefix){
    DIR *d=opendir(root);assert(d);struct dirent *entry;uint64_t hash=UINT64_C(14695981039346656037);size_t n=strlen(prefix);
    while((entry=readdir(d)))if(entry->d_name[0]!='.'&&!strncmp(entry->d_name,prefix,n)){
        for(const char *s=entry->d_name;*s;s++)hash=(hash^(uint8_t)*s)*UINT64_C(1099511628211);
        char path[512];snprintf(path,sizeof(path),"%s/%s",root,entry->d_name);FILE *f=fopen(path,"rb");assert(f);int c;while((c=fgetc(f))!=EOF)hash=(hash^(uint8_t)c)*UINT64_C(1099511628211);assert(!ferror(f)&&!fclose(f));
    }
    assert(!closedir(d));return hash;
}
static void mock_fs_clear(const char *root){DIR *d=opendir(root);assert(d);struct dirent *entry;while((entry=readdir(d)))if(entry->d_name[0]!='.'){char path[512];snprintf(path,sizeof(path),"%s/%s",root,entry->d_name);assert(!unlink(path));}assert(!closedir(d)&&!rmdir(root));}

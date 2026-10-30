/* SPDX-License-Identifier: Apache-2.0
 * 中文：仅管理图书、字体、应用程序和只读画稿；流式校验后提交，不改应用存档。
 * English: Manage books, fonts, app programs and read-only drawings; verify streams before commit and preserve app saves.
 */
#include "read_pico_content.h"
#include "read_pico_sd.h"
#include "pico_app_package.h"
#include "transfer_font.h"
#include "../read_pico_home_link/home_link_json.h"
#include "psa/crypto.h"
#include <stdatomic.h>
#include <stdlib.h>
#include <strings.h>
#include <errno.h>
#include <dirent.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#ifndef PICO_CONTENT_ROOT
#define PICO_CONTENT_ROOT "/sdcard"
#endif
#define PATH_MAX_BYTES 240u
#define BLOCK_BYTES 4096u
static int content_path_stat(const char *path,struct stat *st){
#ifdef ESP_PLATFORM
    // 中文：设备内容根目录只使用不支持符号链接的 FATFS；VFS 仅提供 stat，所有祖先和目标检查仍保留。
    // English: Device content lives on symlink-free FATFS; VFS exposes stat only, retaining every ancestor and target check.
    return stat(path,st);
#else
    // 中文：宿主文件系统支持符号链接，必须使用 lstat 拒绝链接本身及其祖先。
    // English: Host filesystems support symlinks; lstat must reject linked targets and ancestors.
    return lstat(path,st);
#endif
}
static atomic_bool content_busy;
static uint32_t media_generation;
static read_pico_content_acquire_t storage_acquire;
static read_pico_content_release_t storage_release;
static void (*storage_cancel)(void);
void read_pico_content_set_storage_hooks(read_pico_content_acquire_t a,read_pico_content_release_t r,void (*c)(void)){storage_acquire=a;storage_release=r;storage_cancel=c;}
void read_pico_content_cancel(void){if(storage_cancel)storage_cancel();}
bool read_pico_content_busy(void){return atomic_load(&content_busy);}
static int error(char *out,size_t cap,int status,const char *name){snprintf(out,cap,"{\"ok\":false,\"error\":\"%s\"}",name);return status;}
static bool ready(void){read_pico_sd_info_t s={0};return !read_pico_sd_get_info(&s)&&s.present&&s.mounted&&!s.exported;}
static bool same_media(void){read_pico_sd_info_t s={0};return !read_pico_sd_get_info(&s)&&s.present&&s.mounted&&!s.exported&&s.media_generation==media_generation;}
static bool live(const read_pico_content_request_t *r){return r->authorized&&r->authorized(r->context)&&same_media();}
static int io_status(const read_pico_content_request_t *r,int fallback){return !r->authorized||!r->authorized(r->context)?401:!ready()?503:fallback;}
static int hex(unsigned char c){return c>='0'&&c<='9'?c-'0':c>='a'&&c<='f'?c-'a'+10:c>='A'&&c<='F'?c-'A'+10:-1;}
static bool utf8(const unsigned char *p){
    while(*p){unsigned c=*p++,n=0;uint32_t v=0;if(c<128)continue;
        if(c>=0xc2&&c<=0xdf){n=1;v=c&31;}else if(c>=0xe0&&c<=0xef){n=2;v=c&15;}else if(c>=0xf0&&c<=0xf4){n=3;v=c&7;}else return false;
        unsigned width=n;while(n--){if((*p&0xc0)!=0x80)return false;v=(v<<6)|(*p++&63);}
        if((width==2&&v<0x800)||(width==3&&v<0x10000)||(v>=0xd800&&v<=0xdfff)||v>0x10ffff)return false;
    }return true;
}
static bool decode(const char *p,size_t n,char *out,size_t cap){
    size_t k=0;for(size_t i=0;i<n;i++){unsigned char c=p[i];if(c=='%'){if(i+2>=n||hex(p[i+1])<0||hex(p[i+2])<0)return false;c=(unsigned char)(hex(p[i+1])*16+hex(p[i+2]));i+=2;}
        if(!c||c<32||c==127||c=='\\'||c==':'||c=='*'||c=='?'||c=='"'||c=='<'||c=='>'||c=='|'||k+1>=cap)return false;
        out[k++]=(char)c;
    }out[k]=0;return utf8((unsigned char*)out);
}
typedef struct{char path[PATH_MAX_BYTES+1];unsigned offset,limit;bool replace,metadata;unsigned seen;} query_t;
static bool number(const char *p,size_t n,unsigned max,unsigned *out){if(!n)return false;unsigned v=0;for(size_t i=0;i<n;i++){if(p[i]<'0'||p[i]>'9'||(unsigned)(p[i]-'0')>max||v>(max-(unsigned)(p[i]-'0'))/10)return false;v=v*10+(unsigned)(p[i]-'0');}*out=v;return true;}
static bool query(const char *p,query_t *q){
    memset(q,0,sizeof(*q));q->limit=READ_PICO_CONTENT_PAGE_MAX;if(!p||!*p)return true;
    while(*p){size_t n=strcspn(p,"&");const char *eq=memchr(p,'=',n);if(!eq)return false;size_t key=(size_t)(eq-p),v=n-key-1;unsigned bit=0,value=0;
        if(key==4&&!memcmp(p,"path",4)){bit=1;if(!decode(eq+1,v,q->path,sizeof(q->path)))return false;}
        else if(key==6&&!memcmp(p,"offset",6)){bit=2;if(!number(eq+1,v,10000,&q->offset))return false;}
        else if(key==5&&!memcmp(p,"limit",5)){bit=4;if(!number(eq+1,v,64,&q->limit)||!q->limit)return false;}
        else if(key==7&&!memcmp(p,"replace",7)){bit=8;if(!number(eq+1,v,1,&value))return false;q->replace=value;}
        else if(key==8&&!memcmp(p,"metadata",8)){bit=16;if(!number(eq+1,v,1,&value)||!value)return false;q->metadata=true;}
        else return false;
        if(q->seen&bit)return false;
        q->seen|=bit;p+=n;if(*p)p++;if(!*p&&n&&p[-1]=='&')return false;
    }return true;
}
static const char *kind(const char *p,bool write){
    if(!p||!*p||strlen(p)>PATH_MAX_BYTES||!utf8((const unsigned char*)p))return NULL;
    const char *name=p;for(const char *s=p;;s++){unsigned char c=*s;if((c<32&&c)||c==127)return NULL;if(c&&strchr("\\:*?\"<>|",c))return NULL;
        if(c=='/'||!c){size_t n=(size_t)(s-name);if(!n||name[0]=='.'||name[0]==' '||name[n-1]=='.'||name[n-1]==' '||n>180)return NULL;if(!c)break;name=s+1;}
    }
    const char *ext=strrchr(name,'.');if(!ext)return NULL;
    if(!strncmp(p,"books/",6)&&!strchr(p+6,'/')&&(!strcasecmp(ext,".txt")||!strcasecmp(ext,".epub")))return !strncmp(name,"灵犀笔记-",strlen("灵犀笔记-"))&&!strcasecmp(ext,".txt")?"note":"book";
    if(!strchr(p,'/')&&(!strcasecmp(ext,".txt")||!strcasecmp(ext,".epub")))return write?NULL:"book";
    if(!strncmp(p,"fonts/",6)&&!strchr(p+6,'/')&&(!strcasecmp(ext,".ttf")||(!write&&!strcasecmp(ext,".otf"))))return "font";
    if((!strncmp(p,"apps/games/",11)||!strncmp(p,"apps/tools/",11))&&!strchr(p+11,'/')&&!strcasecmp(ext,".pga")){
        char id[32];size_t n=(size_t)(ext-name);if(n>=sizeof(id))return NULL;memcpy(id,name,n);id[n]=0;return pico_app_id_valid(id)?"application":NULL;
    }
    if(!write&&!strncmp(p,"apps/data/drawing-notes/board",29)&&strlen(p)==35&&p[29]>='0'&&p[29]<='1'&&p[30]>='0'&&p[30]<='9'&&!strcmp(p+31,".dat")){
        unsigned n=(unsigned)(p[29]-'0')*10+(unsigned)(p[30]-'0');if(n>=1&&n<=12)return "drawing";
    }
    return NULL;
}
static bool safe_path(const char *relative,char out[320],bool create){
    if(snprintf(out,320,"%s/%s",PICO_CONTENT_ROOT,relative)>=320)return false;
    size_t base=strlen(PICO_CONTENT_ROOT);struct stat st;
    if(content_path_stat(PICO_CONTENT_ROOT,&st)||!S_ISDIR(st.st_mode)||S_ISLNK(st.st_mode))return false;
    for(char *p=out+base+1;*p;p++)if(*p=='/'){*p=0;int rc=content_path_stat(out,&st);if(rc&&errno==ENOENT&&create){if(!same_media()||(mkdir(out,0777)&&errno!=EEXIST)){*p='/';return false;}rc=content_path_stat(out,&st);}*p='/';if(rc||!S_ISDIR(st.st_mode)||S_ISLNK(st.st_mode))return false;}
    if(!content_path_stat(out,&st))return S_ISREG(st.st_mode)&&!S_ISLNK(st.st_mode);
    return errno==ENOENT;
}
static bool regular(const char *path,struct stat *st){return !content_path_stat(path,st)&&S_ISREG(st->st_mode)&&!S_ISLNK(st->st_mode)&&st->st_size>=0&&(uint64_t)st->st_size<=READ_PICO_CONTENT_MAX;}
static bool digest(const read_pico_content_request_t *r,FILE *f,char out[65],size_t *bytes){
    unsigned char *buf=malloc(BLOCK_BYTES),sum[32];if(!buf)return false;psa_hash_operation_t hash=PSA_HASH_OPERATION_INIT;bool ok=psa_crypto_init()==PSA_SUCCESS&&psa_hash_setup(&hash,PSA_ALG_SHA_256)==PSA_SUCCESS;*bytes=0;
    while(ok){if(!live(r)){ok=false;break;}size_t n=fread(buf,1,BLOCK_BYTES,f);if(n){*bytes+=n;if(*bytes>READ_PICO_CONTENT_MAX||psa_hash_update(&hash,buf,n))ok=false;}if(n<BLOCK_BYTES){if(ferror(f))ok=false;break;}}
    size_t sum_bytes=0;if(ok)ok=psa_hash_finish(&hash,sum,sizeof(sum),&sum_bytes)==PSA_SUCCESS&&sum_bytes==32;psa_hash_abort(&hash);free(buf);
    if(ok){for(size_t i=0;i<32;i++)snprintf(out+i*2,3,"%02x",sum[i]);}return ok;
}
static bool json_out(cJSON *j,char *out,size_t capacity){char *s=cJSON_PrintUnformatted(j);bool ok=s&&strlen(s)<capacity;if(ok)strcpy(out,s);free(s);cJSON_Delete(j);return ok;}
static int receipt(char *out,size_t cap,const char *p,size_t n,const char *sha){cJSON *j=cJSON_CreateObject();cJSON_AddBoolToObject(j,"ok",true);cJSON_AddStringToObject(j,"path",p);cJSON_AddNumberToObject(j,"bytes",(double)n);cJSON_AddStringToObject(j,"sha256",sha);return json_out(j,out,cap)?200:error(out,cap,503,"memory");}
static int list(const read_pico_content_request_t *r,const query_t *q,char *out,size_t cap){
    const char *dirs[]={"","books","fonts","apps/games","apps/tools","apps/data/drawing-notes"};unsigned total=0,shown=0;int result=200;cJSON *j=cJSON_CreateObject(),*items=cJSON_CreateArray();
    if(!j||!items){cJSON_Delete(j);cJSON_Delete(items);return error(out,cap,503,"memory");}cJSON_AddItemToObject(j,"items",items);
    for(size_t d=0;d<sizeof(dirs)/sizeof(*dirs)&&result==200;d++){
        char root[320];snprintf(root,sizeof(root),"%s%s%s",PICO_CONTENT_ROOT,*dirs[d]?"/":"",dirs[d]);struct stat ds;if(content_path_stat(root,&ds)){if(errno==ENOENT)continue;result=507;break;}if(!S_ISDIR(ds.st_mode)||S_ISLNK(ds.st_mode))continue;
        // 所有祖先也必须是普通目录，拒绝任何符号链接别名。/ Every ancestor must be a real directory, never a symlink alias.
        char probe[PATH_MAX_BYTES+1],checked[320];snprintf(probe,sizeof(probe),"%s%sprobe.txt",dirs[d],*dirs[d]?"/":"");if(!safe_path(probe,checked,false))continue;
        DIR *dir=opendir(root);if(!dir){result=507;break;}struct dirent *e;
        while((errno=0,e=readdir(dir))){if(!live(r)){result=io_status(r,503);break;}char p[PATH_MAX_BYTES+1],full[320];if(snprintf(p,sizeof(p),"%s%s%s",dirs[d],*dirs[d]?"/":"",e->d_name)>=(int)sizeof(p))continue;const char *k=kind(p,false);struct stat st;if(!k||!safe_path(p,full,false)||!regular(full,&st))continue;
            if(total>=10000){result=413;break;}unsigned index=total++;if(index<q->offset||shown>=q->limit)continue;
            cJSON *item=cJSON_CreateObject();if(!item){result=503;break;}cJSON_AddStringToObject(item,"path",p);cJSON_AddStringToObject(item,"title",e->d_name);cJSON_AddStringToObject(item,"kind",k);cJSON_AddNumberToObject(item,"bytes",(double)st.st_size);cJSON_AddBoolToObject(item,"valid",strcasecmp(strrchr(e->d_name,'.'),".otf")!=0);cJSON_AddItemToArray(items,item);shown++;
        }if(!e&&errno)result=507;if(closedir(dir))result=507;
    }
    if(result!=200){cJSON_Delete(j);return error(out,cap,result,"list_failed");}cJSON_AddBoolToObject(j,"ok",true);cJSON_AddNumberToObject(j,"offset",q->offset);cJSON_AddNumberToObject(j,"total",total);return json_out(j,out,cap)?200:error(out,cap,503,"memory");
}
static bool valid_upload(const char *path,const char *relative,const char *k){
    if(!strcmp(k,"font"))return valid_ttf(path);
    if(!strcmp(k,"application")){pico_app_package_t package;if(!pico_app_package_read(path,&package,true,NULL,0)||(package.kind>5&&package.kind<128))return false;char expected[80];snprintf(expected,sizeof(expected),"apps/%s/%s.pga",package.kind>=128?"tools":"games",package.id);bool device=false;for(unsigned i=0;i<package.payload_count;i++)if(!strcmp(package.payloads[i].target,"esp32s3-xtensa"))device=true;return device&&!strcmp(relative,expected);}
    if(!strcasecmp(strrchr(relative,'.'),".epub")){unsigned char signature[4];FILE *f=fopen(path,"rb");if(!f)return false;bool ok=fread(signature,1,4,f)==4&&!memcmp(signature,"PK\003\004",4);if(fclose(f))ok=false;return ok;}
    return true;
}
static int upload(const read_pico_content_request_t *r,const query_t *q,const char *k,char *out,size_t cap,bool *changed){
    if(!r->bytes||r->bytes>READ_PICO_CONTENT_MAX)return error(out,cap,r->bytes?413:400,"size");
    if((!strcmp(k,"font")&&r->bytes>TRANSFER_FONT_MAX)||(!strcmp(k,"application")&&r->bytes>PICO_APP_PACKAGE_MAX))return error(out,cap,413,"size");
    if(!r->sha256||strlen(r->sha256)!=64)return error(out,cap,400,"sha256_required");
    for(unsigned i=0;i<64;i++)if(hex(r->sha256[i])<0)return error(out,cap,400,"sha256_required");
    if(!live(r))return error(out,cap,io_status(r,503),"storage_unavailable");
    char path[320],part[344],backup[344];if(!safe_path(q->path,path,true))return error(out,cap,403,"path_rejected");snprintf(part,sizeof(part),"%s.pico-part",path);snprintf(backup,sizeof(backup),"%s.pico-backup",path);
    struct stat st;bool exists=!content_path_stat(path,&st);if(exists&&!q->replace)return error(out,cap,409,"exists");if(!content_path_stat(backup,&st))return error(out,cap,409,"recovery_required");if(errno!=ENOENT)return error(out,cap,507,"storage");
    read_pico_sd_info_t sd={0};if(read_pico_sd_get_info(&sd)||r->bytes>sd.free_bytes)return error(out,cap,507,"space");
    if(!live(r))return error(out,cap,io_status(r,503),"storage_unavailable");
    if(!content_path_stat(part,&st)){if(!S_ISREG(st.st_mode)||S_ISLNK(st.st_mode)||!same_media()||unlink(part))return error(out,cap,409,"temporary");}else if(errno!=ENOENT)return error(out,cap,507,"temporary");
    int fd=open(part,O_WRONLY|O_CREAT|O_EXCL,0600);if(fd<0)return error(out,cap,errno==EEXIST?409:507,"temporary");FILE *f=fdopen(fd,"wb");if(!f){close(fd);if(same_media())unlink(part);return error(out,cap,507,"temporary");}setvbuf(f,NULL,_IONBF,0);
    unsigned char *block=malloc(BLOCK_BYTES);size_t left=r->bytes;int result=block?200:503;
    while(result==200&&left){if(!live(r)){result=io_status(r,503);break;}size_t n=left>BLOCK_BYTES?BLOCK_BYTES:left;int got=r->receive?r->receive(r->context,block,n):-1;if(got<=0||(size_t)got>n){result=400;break;}if(!live(r)){result=io_status(r,503);break;}if(fwrite(block,1,(size_t)got,f)!=(size_t)got){result=507;break;}left-=(size_t)got;}
    free(block);if(result==200&&!live(r))result=io_status(r,503);if(result==200&&(fflush(f)||fsync(fileno(f))))result=507;if(fclose(f))result=507;
    char sha[65];size_t saved=0;if(result==200&&!live(r))result=io_status(r,503);if(result==200){f=fopen(part,"rb");if(!f)result=507;else{if(!digest(r,f,sha,&saved)||saved!=r->bytes)result=io_status(r,507);if(fclose(f))result=507;}}
    if(result==200&&strcasecmp(sha,r->sha256))result=422;
    if(result==200&&!live(r))result=io_status(r,503);
    if(result==200&&!valid_upload(part,q->path,k))result=422;
    if(result==200&&!live(r))result=io_status(r,503);
    bool rotated=false;if(result==200){if(exists){if(!same_media()||rename(path,backup))result=507;else rotated=true;}if(result==200&&(!same_media()||rename(part,path))){result=507;if(rotated&&same_media())rename(backup,path);}else if(result==200&&rotated&&same_media())unlink(backup);}
    if(result!=200){if(same_media())unlink(part);return error(out,cap,result,"upload_failed");}*changed=true;return receipt(out,cap,q->path,saved,sha);
}
int read_pico_content_handle(const read_pico_content_request_t *r,char *out,size_t cap){
    if(!r||!out||!cap)return 400;
    out[0]=0;query_t q;if(!query(r->query,&q))return error(out,cap,400,"query");
    bool files=!strcmp(r->endpoint,"/api/manage/pico/files"),file=!strcmp(r->endpoint,"/api/manage/pico/file");if(!files&&!file)return error(out,cap,404,"not_found");
    bool listing=files&&!strcmp(r->method,"GET"),put=files&&!strcmp(r->method,"PUT"),del=files&&!strcmp(r->method,"DELETE"),get=file&&!strcmp(r->method,"GET");
    if(!listing&&!put&&!del&&!get)return error(out,cap,405,"method");
    if((listing&&(q.seen&~6u))||(!listing&&(!(q.seen&1u)||(q.seen&~(put?9u:get?17u:1u))))||(r->bytes&&!put))return error(out,cap,400,"query");
    const char *k=listing?NULL:kind(q.path,put);if(!listing&&(!k||(del&&!strcmp(k,"drawing"))))return error(out,cap,403,"path_rejected");
    if(!r->authorized||!r->authorized(r->context))return error(out,cap,401,"setup_expired");
    if(!ready())return error(out,cap,503,"storage_unavailable");
    bool expected=false;if(!atomic_compare_exchange_strong(&content_busy,&expected,true))return error(out,cap,409,"content_busy");
    bool leased=storage_acquire&&storage_release&&storage_acquire(),changed=false;int result=503;
    if(!leased){error(out,cap,result,"storage_busy");goto done;}read_pico_sd_info_t lease_info={0};if(read_pico_sd_get_info(&lease_info)){error(out,cap,result,"storage_unavailable");goto done;}media_generation=lease_info.media_generation;if(!live(r)){result=io_status(r,503);error(out,cap,result,"storage_unavailable");goto done;}
    if(listing){result=list(r,&q,out,cap);goto done;}if(put){result=upload(r,&q,k,out,cap,&changed);goto done;}
    char path[320];struct stat st;if(!safe_path(q.path,path,false)){result=error(out,cap,403,"path_rejected");goto done;}if(!regular(path,&st)){result=error(out,cap,404,"not_found");goto done;}
    if(del){if(!live(r)){result=error(out,cap,io_status(r,503),"storage_unavailable");goto done;}if(!same_media()||unlink(path)){result=error(out,cap,507,"delete_failed");goto done;}changed=true;cJSON *j=cJSON_CreateObject();cJSON_AddBoolToObject(j,"ok",true);cJSON_AddStringToObject(j,"path",q.path);result=json_out(j,out,cap)?200:error(out,cap,503,"memory");goto done;}
    FILE *f=fopen(path,"rb");if(!f){result=error(out,cap,507,"read_failed");goto done;}result=200;
    if(q.metadata){char sha[65];size_t n;if(!digest(r,f,sha,&n)||n!=(size_t)st.st_size)result=error(out,cap,io_status(r,507),"read_failed");else result=receipt(out,cap,q.path,n,sha);}
    else{unsigned char *block=malloc(BLOCK_BYTES);size_t remaining=(size_t)st.st_size;if(!block)result=503;while(result==200&&remaining){if(!live(r)){result=io_status(r,503);break;}size_t n=remaining>BLOCK_BYTES?BLOCK_BYTES:remaining;if(fread(block,1,n,f)!=n||!live(r)||!r->download||!r->download(r->context,block,n)){result=io_status(r,507);break;}remaining-=n;}if(result==200&&!live(r))result=io_status(r,503);if(result==200&&(!r->download||!r->download(r->context,NULL,0)))result=507;free(block);}
    if(fclose(f))result=507;
done:
    if(result==200&&!changed&&!live(r))result=error(out,cap,io_status(r,503),"request_expired");
    if(result!=200&&!out[0])error(out,cap,result,"stream_failed");
    if(leased)storage_release(changed);
    atomic_store(&content_busy,false);return result;
}

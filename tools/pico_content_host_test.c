/* 配对内容流生产实现的临时目录契约；不访问设备。/ Production paired streams in temporary directories; no devices. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
static char test_root[512];
#define PICO_CONTENT_ROOT test_root
#include "../components/read_pico_transfer/read_pico_content.c"
static unsigned checks,acquired,released,changed_count,received_calls;
static bool available=true,exported=false,authorized=true,grant=true,expire=false;
static bool swap_on_receive;
static unsigned media,auth_calls,fail_auth_after;
static const char *input;
static size_t input_at,input_size,download_size;
static unsigned char downloaded[32768];
int read_pico_sd_get_info(read_pico_sd_info_t *s){memset(s,0,sizeof(*s));s->present=s->mounted=available;s->exported=exported;s->free_bytes=1ull<<30;s->media_generation=media;return 0;}
static bool auth(void *p){(void)p;auth_calls++;return authorized&&(!fail_auth_after||auth_calls<fail_auth_after);}
static bool acquire(void){acquired++;return grant;}
static void release(bool changed){released++;changed_count+=changed;}
static int receive(void *p,void *out,size_t cap){(void)p;received_calls++;if(expire)authorized=false;if(swap_on_receive)media++;if(input_at>=input_size)return -1;size_t n=input_size-input_at;if(n>cap)n=cap;if(n>773)n=773;memcpy(out,input+input_at,n);input_at+=n;return (int)n;}
static bool download(void *p,const void *data,size_t n){(void)p;assert(download_size+n<=sizeof(downloaded));if(n)memcpy(downloaded+download_size,data,n);download_size+=n;return true;}
static char out[32768];
static int request(const char *method,const char *endpoint,const char *query,const char *body,size_t n,const char *sha){
    input=body;input_at=0;input_size=n;download_size=0;auth_calls=0;read_pico_content_request_t r={.method=method,.endpoint=endpoint,.query=query,.sha256=sha,.bytes=n,.authorized=auth,.receive=receive,.download=download};
    int result=read_pico_content_handle(&r,out,sizeof(out));assert(!read_pico_content_busy());checks++;return result;
}
static void hash(const void *p,size_t n,char out_sha[65]){unsigned char sum[32];CC_SHA256(p,(CC_LONG)n,sum);for(unsigned i=0;i<32;i++)snprintf(out_sha+i*2,3,"%02x",sum[i]);}
static void write_file(const char *relative,const void *data,size_t bytes){char p[800];snprintf(p,sizeof(p),"%s/%s",test_root,relative);FILE *f=fopen(p,"wb");assert(f&&fwrite(data,1,bytes,f)==bytes&&!fclose(f));}
static void same_file(const char *relative,const void *data,size_t bytes){char p[800],buffer[32768];snprintf(p,sizeof(p),"%s/%s",test_root,relative);FILE *f=fopen(p,"rb");assert(f&&fread(buffer,1,sizeof(buffer),f)==bytes&&!memcmp(buffer,data,bytes)&&!fclose(f));checks++;}
static void le32(unsigned char *p,uint32_t n){p[0]=n;p[1]=n>>8;p[2]=n>>16;p[3]=n>>24;}
static void package(unsigned char p[528],unsigned type,bool device){memset(p,0,528);memcpy(p,"PICOAPP1",8);le32(p+8,1);le32(p+12,512);le32(p+16,528);le32(p+20,1);le32(p+24,10000);le32(p+28,type);strcpy((char*)p+32,"sample");strcpy((char*)p+64,"Sample");strcpy((char*)p+128,"Fixture");le32(p+228,1);strcpy((char*)p+256,device?"esp32s3-xtensa":"macos-arm64");le32(p+272,512);le32(p+276,16);memcpy(p+512,"sample code data",16);le32(p+280,pico_app_crc32(p+512,16));le32(p+232,pico_app_crc32(p,512));}
int main(int argc,char **argv){assert(argc==3);snprintf(test_root,sizeof(test_root),"%s",argv[1]);read_pico_content_set_storage_hooks(acquire,release,NULL);
    assert(request("GET","/api/manage/pico/files","offset=0&limit=64",NULL,0,NULL)==200&&strstr(out,"\"total\":0"));
    char sha[65];const char original[]="Hello paired content\n";hash(original,sizeof(original)-1,sha);
    assert(request("PUT","/api/manage/pico/files","path=books%2Ftest.txt&replace=0",original,sizeof(original)-1,sha)==200&&strstr(out,sha)&&strstr(out,"books/test.txt"));same_file("books/test.txt",original,sizeof(original)-1);
    assert(request("GET","/api/manage/pico/file","path=books%2Ftest.txt&metadata=1",NULL,0,NULL)==200&&strstr(out,sha));
    assert(request("GET","/api/manage/pico/file","path=books%2Ftest.txt",NULL,0,NULL)==200&&download_size==sizeof(original)-1&&!memcmp(downloaded,original,download_size));
    assert(request("PUT","/api/manage/pico/files","path=books%2Ftest.txt&replace=0",original,sizeof(original)-1,sha)==409);same_file("books/test.txt",original,sizeof(original)-1);
    char big[20000];for(unsigned i=0;i<sizeof(big);i++)big[i]=(char)i;hash(big,sizeof(big),sha);
    unsigned before=received_calls;assert(request("PUT","/api/manage/pico/files","path=books%2Ftest.txt&replace=1",big,sizeof(big),sha)==200&&received_calls>before+4);same_file("books/test.txt",big,sizeof(big));
    char bad[65];memset(bad,'0',64);bad[64]=0;assert(request("PUT","/api/manage/pico/files","path=books%2Ftest.txt&replace=1",original,sizeof(original)-1,bad)==422);same_file("books/test.txt",big,sizeof(big));
    expire=true;assert(request("PUT","/api/manage/pico/files","path=books%2Ftest.txt&replace=1",big,sizeof(big),sha)==401);expire=false;authorized=true;same_file("books/test.txt",big,sizeof(big));
    fail_auth_after=6;assert(request("GET","/api/manage/pico/file","path=books%2Ftest.txt&metadata=1",NULL,0,NULL)==401);fail_auth_after=0;
    swap_on_receive=true;assert(request("PUT","/api/manage/pico/files","path=books%2Ftest.txt&replace=1",big,sizeof(big),sha)==503);swap_on_receive=false;same_file("books/test.txt",big,sizeof(big));
    hash(original,sizeof(original)-1,sha);
    assert(request("PUT","/api/manage/pico/files","path=fonts%2Fbad.ttf&replace=0",original,sizeof(original)-1,sha)==422);
    const char *invalid[]={"path=../secret.txt","path=books%2F..%2Fsecret.txt","path=books%2Fa%00.txt","path=books%2Fa.txt&path=books%2Fb.txt","path=books%2F%F0%80%80%80.txt","path=books%2F.test.txt","path=books%2Fa.txt&replace=2","path=books%2Fa.txt&unknown=1","path=books%2Fa.txt&","offset=10001","offset=-1","limit=0","limit=65","offset=99999999999999999999999"};
    for(unsigned i=0;i<sizeof(invalid)/sizeof(*invalid);i++)assert(request("PUT","/api/manage/pico/files",invalid[i],original,sizeof(original)-1,sha)>=400);
    assert(request("DELETE","/api/manage/pico/files","path=apps%2Fdata%2Fdrawing-notes%2Fboard01.dat",NULL,0,NULL)==403);
    assert(request("DELETE","/api/manage/pico/files","path=apps%2Fdata%2Fchess%2Fsave.dat",NULL,0,NULL)==403);
    char p[800];snprintf(p,sizeof(p),"%s/apps",test_root);assert(!mkdir(p,0777));snprintf(p,sizeof(p),"%s/apps/data",test_root);assert(!mkdir(p,0777));snprintf(p,sizeof(p),"%s/apps/data/drawing-notes",test_root);assert(!mkdir(p,0777));write_file("apps/data/drawing-notes/board01.dat",original,sizeof(original)-1);
    assert(request("GET","/api/manage/pico/files","offset=0&limit=1",NULL,0,NULL)==200&&strstr(out,"\"total\":2"));assert(request("GET","/api/manage/pico/files","offset=1&limit=1",NULL,0,NULL)==200&&strstr(out,"drawing"));
    assert(request("GET","/api/manage/pico/file","path=apps%2Fdata%2Fdrawing-notes%2Fboard01.dat",NULL,0,NULL)==200&&download_size==sizeof(original)-1);
    snprintf(p,sizeof(p),"%s/fonts",test_root);assert(!rmdir(p));assert(!symlink("/tmp",p));assert(request("PUT","/api/manage/pico/files","path=fonts%2Ftest.ttf",original,sizeof(original)-1,sha)==403);assert(!unlink(p));
    grant=false;assert(request("GET","/api/manage/pico/files","",NULL,0,NULL)==503);grant=true;exported=true;assert(request("GET","/api/manage/pico/files","",NULL,0,NULL)==503);exported=false;available=false;assert(request("GET","/api/manage/pico/files","",NULL,0,NULL)==503);available=true;authorized=false;assert(request("GET","/api/manage/pico/files","",NULL,0,NULL)==401);authorized=true;
    unsigned char pkg[528];package(pkg,0,true);hash(pkg,sizeof(pkg),sha);assert(request("PUT","/api/manage/pico/files","path=apps%2Fgames%2Fsample.pga",(char*)pkg,sizeof(pkg),sha)==200);
    assert(request("DELETE","/api/manage/pico/files","path=apps%2Fgames%2Fsample.pga",NULL,0,NULL)==200);
    package(pkg,7,true);hash(pkg,sizeof(pkg),sha);assert(request("PUT","/api/manage/pico/files","path=apps%2Fgames%2Fsample.pga",(char*)pkg,sizeof(pkg),sha)==422);
    package(pkg,0,false);hash(pkg,sizeof(pkg),sha);assert(request("PUT","/api/manage/pico/files","path=apps%2Fgames%2Fsample.pga",(char*)pkg,sizeof(pkg),sha)==422);
    package(pkg,0,true);pkg[527]^=1;hash(pkg,sizeof(pkg),sha);assert(request("PUT","/api/manage/pico/files","path=apps%2Fgames%2Fsample.pga",(char*)pkg,sizeof(pkg),sha)==422);
    package(pkg,128,true);hash(pkg,sizeof(pkg),sha);assert(request("PUT","/api/manage/pico/files","path=apps%2Fgames%2Fsample.pga",(char*)pkg,sizeof(pkg),sha)==422);
    FILE *font=fopen(argv[2],"rb");assert(font&&!fseek(font,0,SEEK_END));long font_size=ftell(font);assert(font_size>0&&!fseek(font,0,SEEK_SET));char *font_data=malloc((size_t)font_size);assert(font_data&&fread(font_data,1,(size_t)font_size,font)==(size_t)font_size&&!fclose(font));hash(font_data,(size_t)font_size,sha);assert(request("PUT","/api/manage/pico/files","path=fonts%2Fvalid.ttf",font_data,(size_t)font_size,sha)==200);assert(request("GET","/api/manage/pico/file","path=fonts%2Fvalid.ttf&metadata=1",NULL,0,NULL)==200&&strstr(out,sha));assert(request("DELETE","/api/manage/pico/files","path=fonts%2Fvalid.ttf",NULL,0,NULL)==200);free(font_data);
    assert(request("DELETE","/api/manage/pico/files","path=books%2Ftest.txt",NULL,0,NULL)==200&&strstr(out,"\"ok\":true"));assert(request("GET","/api/manage/pico/file","path=books%2Ftest.txt",NULL,0,NULL)==404);
    assert(acquired==released+1&&changed_count==7);printf("pico-content: %u checks PASS\n",checks);return 0;
}

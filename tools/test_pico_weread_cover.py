#!/usr/bin/env python3
"""中文：官方封面缓存的下载边界、取消和纯16灰阶绘制回归；仅合成文件。
English: Test cover-cache download bounds, cancellation and pure 16-gray painting with synthetic files only.
"""
from pathlib import Path
import subprocess,json,hashlib
r=Path(__file__).resolve().parents[1];o=r/'build/dev62-cover-tests';o.mkdir(exist_ok=True)
source=r/'main/system/pico_weread_cover.c';s=source.read_text().replace('#include "pico_system.h"','#include "cover_host.h"')
(o/'cover.c').write_text(s)
(o/'cover_host.h').write_text('''#include <stdbool.h>\n#include <stdint.h>\ntypedef struct {int x,y,width,height;} EpdRect;\nEpdRect ps_rect(int,int,int,int);\nvoid epd_draw_pixel(int,int,uint8_t,uint8_t*);\n''')
(o/'test.c').write_text(r'''
#define PICO_NATIVE_PREVIEW 1
#include "cover.c"
#include <assert.h>
#include <sys/stat.h>
static unsigned checks,decodes;static unsigned decode_w=17,decode_h=23;
#define CHECK(x) do{checks++;if(!(x)){fprintf(stderr,"line %u: %s\n",__LINE__,#x);abort();}}while(0)
EpdRect ps_rect(int x,int y,int w,int h){return(EpdRect){x,y,w,h};}
void epd_draw_pixel(int x,int y,uint8_t gray,uint8_t *fb){CHECK(x>=0&&y>=0&&x<160&&y<240);CHECK(gray%17==0);fb[y*160+x]=gray;}
bool book_image_decode(const uint8_t *d,size_t n,size_t budget,uint8_t **out,uint16_t *w,uint16_t *h){
 decodes++;CHECK(n&&d[0]=='x'&&budget==512u*1024u);*w=decode_w;*h=decode_h;*out=malloc(*w**h);CHECK(*out);
 for(unsigned i=0;i<(unsigned)*w**h;i++)(*out)[i]=(uint8_t)i;return true;
}
int main(int argc,char **argv){CHECK(argc==2);setenv("RPICO_WEREAD_FIXTURE_ROOT",argv[1],1);setenv("RPICO_WEREAD_OFFLINE","1",1);
 const char *valid="https://wfqqreader-1252317822.image.myqcloud.com/cover/123/456/s_456.jpg";
 const char *bad[]={NULL,"", "http://wfqqreader-1252317822.image.myqcloud.com/cover/a", "https://wfqqreader-1252317822.image.myqcloud.com.evil/cover/a", "https://wfqqreader-1252317822.image.myqcloud.com/cover/a'", "https://cdn.weread.qq.com/cover/a\n", "https://cdn.weread.qq.com/cover/a;touch"};
 for(unsigned i=0;i<sizeof(bad)/sizeof(*bad);i++)CHECK(!cover_url(bad[i]));CHECK(cover_url(valid));
 uint8_t frame[160*240]={0};atomic_bool cancel=false;
 CHECK(!ps_weread_cover_draw(1,frame,0,0,100,100));ps_weread_cover_clear();
 ps_weread_cover_prepare(1,valid,&cancel);CHECK(decodes==1);CHECK(ps_weread_cover_draw(1,frame,10,10,128,192));CHECK(!ps_weread_cover_draw(2,frame,10,10,128,192));
 atomic_store(&cancel,true);ps_weread_cover_prepare(2,valid,&cancel);CHECK(decodes==1);CHECK(ps_weread_cover_draw(1,frame,0,0,40,40));
 atomic_store(&cancel,false);decode_w=640;decode_h=900;ps_weread_cover_prepare(2,valid,&cancel);CHECK(image_width<=128&&image_height<=192);CHECK(!ps_weread_cover_draw(1,frame,0,0,40,40));
 char path[1024];snprintf(path,sizeof(path),"%s/cover.jpg",argv[1]);CHECK(!remove(path));CHECK(ps_weread_cover_draw(2,frame,0,0,100,100));
 ps_weread_cover_clear();CHECK(!pixels&&!image_width&&!image_height&&!image_id);CHECK(!ps_weread_cover_draw(2,frame,0,0,40,40));
 ps_weread_cover_prepare(3,valid,&cancel);CHECK(!pixels);FILE *f=fopen(path,"wb");CHECK(f);for(unsigned i=0;i<COVER_MAX+1;i++)CHECK(fputc('x',f)!=EOF);CHECK(!fclose(f));
 unsigned before=decodes;ps_weread_cover_prepare(4,valid,&cancel);CHECK(decodes==before&&!pixels);ps_weread_cover_clear();printf("checks: %u\n",checks);
}
''')
fixture=o/'fixture';fixture.mkdir(exist_ok=True);(fixture/'cover.jpg').write_bytes(b'xsynthetic-image')
cmd=['cc','-std=c11','-g','-Wall','-Wextra','-Werror','-Wno-misleading-indentation','-DPICO_NATIVE_PREVIEW','-fsanitize=address,undefined','-fno-omit-frame-pointer','-I',str(o),'-I',str(r/'main/system'),'-I',str(r/'main/book'),str(o/'test.c'),'-pthread','-o',str(o/'test')]
subprocess.run(cmd,check=True);p=subprocess.run([str(o/'test'),str(fixture)],capture_output=True,text=True,check=True);print(p.stdout)
(o/'report.json').write_text(json.dumps({'result':'PASS','sanitizers':['address','undefined'],'hardware_access':False,'network_access':False,'source_sha256':hashlib.sha256(source.read_bytes()).hexdigest(),'transformation':'replace platform include with host types only','stdout':p.stdout},indent=2)+'\n')

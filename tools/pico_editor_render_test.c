/* SPDX-License-Identifier: Apache-2.0
 * 真字体下逐键比较局部与完整编辑帧，验证损伤范围；不代表物理屏幕时延。
 * Compare partial and full editor frames per keystroke with real fonts; not physical-screen latency evidence.
 */
#define PICO_READER_REAL_RENDER
#define main reader_host_test_main
#include "pico_reader_host_test.c"
#undef main
#include "pico_framebuffer_host.h"
static unsigned editor_frames;
static uint8_t *editor_fb;
static size_t editor_bytes;
static bool require_partial;
static void binary_frame(uint8_t *fb){for(size_t i=0;i<editor_bytes;i++)fb[i]=fb[i]<128?0:255;}
static void editor_frame_check(void){
    uint8_t *before=malloc(editor_bytes),*reference=pico_fb_new();assert(before&&reference);memcpy(before,editor_fb,editor_bytes);
    EpdRect damage[4];int count=-1;bool partial=ps_reader_editor_render_partial(editor_fb,damage,&count);
    if(require_partial)assert(partial);
    if(!partial){memset(editor_fb,255,editor_bytes);ps_reader_render(editor_fb);}
    else{
        assert(count>=0&&count<=4);int width=view.landscape?1216:684;
        for(size_t i=0;i<editor_bytes;i++)if(before[i]!=editor_fb[i]){
            bool inside=false;int x=(int)(i%(size_t)width),y=(int)(i/(size_t)width);
            for(int j=0;j<count;j++){EpdRect r=ps_rect(damage[j].x,damage[j].y,damage[j].width,damage[j].height);if(x>=r.x&&x<r.x+r.width&&y>=r.y&&y<r.y+r.height)inside=true;}
            if(!inside)fprintf(stderr,"Editor write outside damage: %d,%d mode=%d landscape=%d\n",x,y,input_mode,view.landscape);
            assert(inside);
        }
    }
    binary_frame(editor_fb);ps_reader_render(reference);binary_frame(reference);
    size_t different=0;for(size_t i=0;i<editor_bytes;i++)different+=editor_fb[i]!=reference[i];
    if(different)fprintf(stderr,"Partial/full mismatch: %zu pixels mode=%d landscape=%d composition=%s count=%d\n",different,input_mode,view.landscape,composition,count);
    assert(!different);free(before);free(reference);editor_frames++;
}
static bool editor_test_key(int action,int value){
    editor_key_t keys[64];int n=editor_keys(keys);
    for(int i=0;i<n;i++)if(keys[i].action==action&&(value<0||keys[i].value==value)){
        ps_event_t e={.type=PS_PRESS,.x=keys[i].x+keys[i].w/2,.y=keys[i].y+keys[i].h/2};ps_reader_event(&e);
        e.type=PS_RELEASE;ps_reader_event(&e);e.type=PS_TAP;ps_reader_event(&e);editor_frame_check();return true;
    }
    return false;
}
int main(int argc,char **argv){
    assert(argc==2&&pico_fb_init(argv[1]));snprintf(rootdir,sizeof(rootdir),"./notes");assert(!mkdir(rootdir,0700));ps_reader_init();
    for(int land=0;land<2;land++){
        view=land?(ps_view_t){640,360,true}:(ps_view_t){360,640,false};editor_bytes=(size_t)1216*684;editor_fb=pico_fb_new();assert(editor_fb);
        const int modes[]={INPUT_PINYIN,INPUT_T9,INPUT_ENGLISH,INPUT_SYMBOL};
        const char *raw[]={"woxihuanyuedu","64426744543","qwertyuiopasdfghjklzxcvbnm","1234567890"};
        for(int m=0;m<4;m++){
            ps_edit("写笔记","原有笔记：阅读与思考。",383,NULL);editor_mode(modes[m]);memset(editor_fb,255,editor_bytes);ps_reader_render(editor_fb);binary_frame(editor_fb);editor_frame_check();
            for(const char *p=raw[m];*p;p++)assert(editor_test_key(EK_LETTER,*p));
            if(m<2){
                assert(editor_test_key(EK_NEXT,-1));assert(editor_test_key(EK_PREV,-1));
                if(candidate_count)assert(editor_test_key(EK_CANDIDATE,candidate_page));
            }
            if(m==2){assert(editor_test_key(EK_SHIFT,-1));assert(editor_test_key(EK_LETTER,'q'));}
            for(int i=0;i<3;i++)assert(editor_test_key(EK_DELETE,-1));
            while(composition[0])assert(editor_test_key(EK_DELETE,-1));
            assert(editor_test_key(EK_SPACE,-1));assert(editor_test_key(EK_COMMA,-1));assert(editor_test_key(EK_NEWLINE,-1));
        }
        free(editor_fb);
    }
    printf("PASS: %u real-font partial/full binary frames and bounded writes; physical EPD timing remains separate.\n",editor_frames);
    free(rr);free(pages);for(int i=0;i<64;i++)free(blobs[i].bytes);pico_fb_close();return 0;
}

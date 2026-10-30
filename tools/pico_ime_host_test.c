/*
 * SPDX-License-Identifier: Apache-2.0
 * 中文：以实际键盘布局和触摸事件序列验证离线输入；字体和存储为主机替身。
 * English: Exercise the actual keyboard layout and touch sequence with host font/storage doubles.
 * 冻结：不接触设备、不改生产源码；主机尺寸估计不能替代真机字形和触摸验收。
 * Frozen: No device access or production edits; host metrics do not establish physical glyph/touch quality.
 */
#include <stdlib.h>
static void *ime_test_calloc(size_t,size_t,unsigned);
#define heap_caps_calloc(n,s,caps) ime_test_calloc(n,s,caps)
static char ime_test_dictionary_path[512];
#define PICO_IME_DICTIONARY_PATH ime_test_dictionary_path
#define PICO_READER_REAL_RENDER
#define main reader_existing_test_main
#include "pico_reader_host_test.c"
#undef main
static bool ime_test_oom;
static void *ime_test_calloc(size_t n,size_t bytes,unsigned caps){
    (void)caps;
    if(n*bytes>512*1024){
        if(ime_test_oom){ime_test_oom=false;return NULL;}
    }
    return calloc(n,bytes);
}

typedef struct { int x,y,w,h;char label[128]; } ime_painted_t;
static ime_painted_t ime_painted[96];
static int ime_painted_count,ime_checks;
static unsigned ime_width_calls;
#define IME_CHECK(c) do { ime_checks++;if(!(c)){fprintf(stderr,"IME check failed: %s (%s:%d)\n",#c,__FILE__,__LINE__);abort();} } while(0)

void ttf_font_cache_clear(void){}
const char *ttf_font_path(void){return mock_font;}
const char *ttf_font_display_name(void){return "Host input geometry font";}
bool ttf_font_is_builtin(void){return !strcmp(mock_font,"builtin");}
int ttf_font_open_builtin(void){strcpy(mock_font,"builtin");return 0;}
int ttf_font_open(const char *s){snprintf(mock_font,sizeof(mock_font),"%s",s);return 0;}
bool ttf_font_path_is_builtin(const char *s){return !strcmp(s,"builtin")||!strcmp(s,"builtin-serif")||!strcmp(s,"builtin-kai");}
void ttf_set_weight(int w){weight=w;}
int ttf_get_weight(void){return weight;}
int ttf_text_width_px(int px,const char *s){ime_width_calls++;int w=0;for(;*s;s++)if(((unsigned char)*s&0xc0)!=0x80)w+=(unsigned char)*s<128?px/2:px;return w;}
EpdRect ps_rect(int x,int y,int w,int h){return (EpdRect){x*19/10,y*19/10,w*19/10,h*19/10};}
bool ps_hit(int x,int y,int rx,int ry,int rw,int rh){return x>=rx&&y>=ry&&x<rx+rw&&y<ry+rh;}
static void ime_rect(int x,int y,int w,int h){IME_CHECK(x>=0&&y>=0&&w>=0&&h>=0&&x+w<=view.w&&y+h<=view.h);draws++;}
void epd_draw_pixel(int x,int y,uint8_t g,uint8_t *f){(void)f;(void)x;(void)y;(void)g;draws++;}
void ps_fill(uint8_t *f,int x,int y,int w,int h,int g){(void)f;(void)g;ime_rect(x,y,w,h);}
void ps_line(uint8_t *f,int x,int y,int x1,int y1,int g){(void)f;(void)g;IME_CHECK(x>=0&&x<=view.w&&x1>=0&&x1<=view.w&&y>=0&&y<=view.h&&y1>=0&&y1<=view.h);draws++;}
void ps_box(uint8_t *f,int x,int y,int w,int h,int r,int g,bool fill){(void)f;(void)r;(void)g;(void)fill;ime_rect(x,y,w,h);}
void ps_text(uint8_t *f,int x,int y,int size,const char *s,int g){(void)f;(void)s;(void)g;ime_rect(x,y,0,size);}
void ps_body_text(uint8_t *f,int x,int y,int size,const char *s,int g){body_draws++;body_last_size=size;body_last_weight=weight;ps_text(f,x,y,size,s,g);}
void ps_center(uint8_t *f,int x,int y,int size,const char *s,int g){ps_text(f,x,y,size,s,g);}
void ps_wrap(uint8_t *f,int x,int y,int w,int size,int lines,const char *s,int g){(void)w;(void)lines;ps_text(f,x,y,size,s,g);}
void ps_icon(uint8_t *f,int x,int y,int size,ps_icon_t icon,int g){(void)f;(void)icon;(void)g;ime_rect(x,y,size,size);}
void ps_button(uint8_t *f,int x,int y,int w,int h,const char *s,bool on){
    (void)f;(void)on;ime_rect(x,y,w,h);IME_CHECK(ime_painted_count<96);
    ime_painted_t *p=&ime_painted[ime_painted_count++];*p=(ime_painted_t){.x=x,.y=y,.w=w,.h=h};snprintf(p->label,sizeof(p->label),"%s",s);
}
void ps_heading(uint8_t *f,const char *title,const char *sub){(void)f;(void)title;(void)sub;draws++;}
void ps_tabs(uint8_t *f,int selected){(void)f;(void)selected;draws++;}

static int ime_layout(editor_key_t keys[64]){
    ime_painted_count=0;render_checked();
    int n=editor_keys(keys);IME_CHECK(n>0&&n<=64);
    for(int i=0;i<n;i++){
        editor_key_t *k=&keys[i];
        if(k->x<0||k->y<0||k->w<1||k->h<1||k->x+k->w>view.w||k->y+k->h>view.h-52)fprintf(stderr,"Key outside safe area: view=%dx%d mode=%d label=%s rect=%d,%d %dx%d bottom=%d limit=%d\n",view.w,view.h,input_mode,k->label,k->x,k->y,k->w,k->h,k->y+k->h,view.h-52);
        IME_CHECK(k->x>=0&&k->y>=0&&k->w>0&&k->h>0&&k->x+k->w<=view.w&&k->y+k->h<=view.h-52);
        bool painted=false;
        for(int j=0;j<ime_painted_count;j++){
            ime_painted_t *p=&ime_painted[j];
            if(p->x==k->x&&p->y==k->y&&p->w==k->w&&p->h==k->h&&!strcmp(p->label,k->label))painted=true;
        }
        IME_CHECK(painted);
        if(k->action==EK_LETTER){IME_CHECK(k->w>=(view.landscape?35:30));IME_CHECK(k->h>=(view.landscape?40:44));}
        if(k->action==EK_CANDIDATE){
            // 与产品20逻辑像素按钮文字比较；实际字体另以同源截图和设备复核。 / Compare the product's 20-logical-pixel label; actual fonts require native screenshots/device review.
            IME_CHECK(ttf_text_width_px(38,k->label)<=k->w*19/10-8);
        }
        for(int j=0;j<i;j++)IME_CHECK(k->x>=keys[j].x+keys[j].w||keys[j].x>=k->x+k->w||k->y>=keys[j].y+keys[j].h||keys[j].y>=k->y+k->h);
    }
    return n;
}

static bool ime_find(int action,int value,editor_key_t *out){
    editor_key_t keys[64];int n=ime_layout(keys);
    for(int i=0;i<n;i++)if((int)keys[i].action==action&&(value<0||keys[i].value==value)){*out=keys[i];return true;}
    return false;
}
static void ime_event(ps_event_type_t type,int x0,int y0,int x,int y){ps_event_t e={.type=type,.x0=x0,.y0=y0,.x=x,.y=y,.ms=60};ps_reader_event(&e);}
static void ime_touch(int x,int y){
    ime_event(PS_PRESS,x,y,x,y);ime_event(PS_RELEASE,x,y,x,y);
    // 全局壳会在RELEASE后补发TAP；单次触摸只能输入一次。 / The shell appends TAP after RELEASE; one contact must produce one input.
    ime_event(PS_TAP,x,y,x,y);
}
static void ime_press_key(editor_key_t k){ime_touch(k.x+k.w/2,k.y+k.h/2);}
static void ime_action(int action,int value){editor_key_t k;IME_CHECK(ime_find(action,value,&k));ime_press_key(k);}
static void ime_reset(int mode,size_t limit){ps_edit("输入法专项测试","",limit,done);editor_mode(mode);}
static void ime_type(const char *s){for(;*s;s++)ime_action(EK_LETTER,(unsigned char)*s);}
static void ime_choose(const char *text){
    int found=-1;for(int i=0;i<candidate_count;i++)if(!strcmp(candidates[i],text)){found=i;break;}
    if(found<0){fprintf(stderr,"Missing candidate '%s': mode=%d composition=%s count=%d\n",text,input_mode,composition,candidate_count);for(int i=0;i<candidate_count;i++)fprintf(stderr,"  %d: %s consume=%u\n",i,candidates[i],candidate_consume[i]);}
    IME_CHECK(found>=0);
    for(int tries=0;tries<CAND_MAX+1;tries++){
        editor_key_t k;if(ime_find(EK_CANDIDATE,found,&k)){ime_press_key(k);return;}
        int before=candidate_page;ime_action(EK_NEXT,-1);IME_CHECK(candidate_page>before);
    }
    IME_CHECK(false);
}

static void ime_continuous(void){
    const struct {const char *raw,*a,*rest_a,*b,*rest_b,*c,*result;} samples[]={
        {"nihaoshijie","你好","shijie","世界","",NULL,"你好世界"},
        {"woxihuanyuedu","我","xihuanyuedu","喜欢","yuedu","阅读","我喜欢阅读"},
        {"jintiantianqihenhao","今天","tianqihenhao","天气","henhao","很好","今天天气很好"}
    };
    for(size_t i=0;i<sizeof(samples)/sizeof(samples[0]);i++){
        ime_reset(INPUT_PINYIN,383);ime_type(samples[i].raw);IME_CHECK(!strcmp(composition,samples[i].raw));
        ime_choose(samples[i].a);IME_CHECK(!strcmp(composition,samples[i].rest_a));
        ime_choose(samples[i].b);IME_CHECK(!strcmp(composition,samples[i].rest_b));
        if(samples[i].c)ime_choose(samples[i].c);
        IME_CHECK(!composition[0]&&!strcmp(editor_text,samples[i].result));
    }
    ime_reset(INPUT_T9,383);ime_type("64426744543");ime_choose("你好");IME_CHECK(!strcmp(composition,"744543"));ime_choose("世界");IME_CHECK(!strcmp(editor_text,"你好世界")&&!composition[0]);
    ime_reset(INPUT_PINYIN,383);ime_type("ni");ime_action(EK_SEPARATOR,-1);ime_type("hao");ime_action(EK_SEPARATOR,-1);ime_type("shijie");ime_choose("你");IME_CHECK(!strcmp(composition,"hao'shijie"));ime_choose("好");IME_CHECK(!strcmp(composition,"shijie"));ime_choose("世界");IME_CHECK(!strcmp(editor_text,"你好世界")&&!composition[0]);
    ime_reset(INPUT_PINYIN,3);ime_type("nihao");ime_choose("你好");IME_CHECK(!editor_text[0]&&!strcmp(composition,"nihao"));
    ime_reset(INPUT_PINYIN,383);ime_type("nihao");ime_choose("你好");ime_action(EK_DELETE,-1);IME_CHECK(!strcmp(editor_text,"你"));
    const struct {const char *raw,*first;} exact[]={{"ni","你"},{"shi","是"},{"xian","现"}};
    for(size_t i=0;i<sizeof(exact)/sizeof(exact[0]);i++){
        ime_reset(INPUT_PINYIN,383);ime_type(exact[i].raw);IME_CHECK(candidate_count>0&&!strcmp(candidates[0],exact[i].first));
    }
}

static void ime_separators(void){
    ime_reset(INPUT_PINYIN,383);ime_type("xi");ime_action(EK_SEPARATOR,-1);ime_type("an");
    for(int i=0;i<candidate_count;i++)IME_CHECK(strcmp(candidates[i],"现")&&strcmp(candidates[i],"先")&&strcmp(candidates[i],"现在"));
    ime_choose("西安");IME_CHECK(!strcmp(editor_text,"西安")&&!composition[0]);
}

static void ime_paging(void){
    ime_reset(INPUT_PINYIN,383);ime_type("shi");IME_CHECK(candidate_count>5);bool seen[CAND_MAX]={false};int total=candidate_count;
    for(int turn=0;turn<CAND_MAX+1;turn++){
        editor_key_t keys[64];int n=ime_layout(keys);
        for(int i=0;i<n;i++)if(keys[i].action==EK_CANDIDATE){IME_CHECK(keys[i].value>=0&&keys[i].value<total);seen[keys[i].value]=true;}
        editor_key_t next;if(!ime_find(EK_NEXT,-1,&next))break;int before=candidate_page;ime_press_key(next);if(candidate_page==before)break;IME_CHECK(candidate_page>before);
    }
    for(int i=0;i<total;i++)IME_CHECK(seen[i]);
    while(candidate_page>0){int before=candidate_page;ime_action(EK_PREV,-1);IME_CHECK(candidate_page<before);}
    IME_CHECK(!strcmp(composition,"shi")&&!editor_text[0]);
    ime_choose("诗");IME_CHECK(!strcmp(editor_text,"诗")&&!composition[0]);
}

static void ime_touch_sequences(void){
    ime_reset(INPUT_ENGLISH,383);editor_key_t q,w;IME_CHECK(ime_find(EK_LETTER,'q',&q)&&ime_find(EK_LETTER,'w',&w));
    int x=q.x+q.w/2,y=q.y+q.h/2;
    ime_event(PS_TAP,x,y,x,y);IME_CHECK(!editor_text[0]);
    ime_event(PS_PRESS,x,y,x,y);IME_CHECK(!editor_text[0]);ime_event(PS_MOVE,x,y,x+2,y+2);ime_event(PS_RELEASE,x,y,x+2,y+2);ime_event(PS_TAP,x,y,x+2,y+2);IME_CHECK(!strcmp(editor_text,"q"));
    editor_text[0]=0;
    // 跨键移动小于全局TAP阈值也必须取消。 / A cross-key move must cancel even below the shell's TAP distance threshold.
    int edge=q.x+q.w-1,other=w.x+1;IME_CHECK(other>edge&&other-edge<13);
    // 实机MOVE有节流，最终抬手跨键即使没有MOVE也不得输入。 / MOVE is throttled on-device; a cross-key release must cancel without any MOVE event.
    ime_event(PS_PRESS,edge,y,edge,y);ime_event(PS_RELEASE,edge,y,other,y);ime_event(PS_TAP,edge,y,other,y);IME_CHECK(!editor_text[0]);
    ime_event(PS_PRESS,edge,y,edge,y);ime_event(PS_MOVE,edge,y,other,y);ime_event(PS_RELEASE,edge,y,other,y);ime_event(PS_TAP,edge,y,other,y);IME_CHECK(!editor_text[0]);
    ime_event(PS_PRESS,edge,y,edge,y);ime_event(PS_MOVE,edge,y,other,y);ime_event(PS_MOVE,edge,y,edge,y);ime_event(PS_RELEASE,edge,y,edge,y);ime_event(PS_TAP,edge,y,edge,y);IME_CHECK(!editor_text[0]);
    ime_event(PS_PRESS,x,y,x,y);ime_event(PS_CANCEL,0,0,0,0);ime_event(PS_RELEASE,x,y,x,y);ime_event(PS_TAP,x,y,x,y);IME_CHECK(!editor_text[0]);
    if(w.x>q.x+q.w){
        int gap=q.x+q.w;ime_event(PS_PRESS,gap,y,gap,y);ime_event(PS_RELEASE,gap,y,other,y);ime_event(PS_TAP,gap,y,other,y);IME_CHECK(!editor_text[0]);
    }
    ime_press_key(q);IME_CHECK(!strcmp(editor_text,"q"));
}

static void ime_modes(void){
    editor_key_t keys[64];
    for(int choice=0;choice<INPUT_MODE_COUNT;choice++){
        int mode=input_ids[choice];ime_reset(mode,383);ime_layout(keys);
        if(mode==INPUT_PINYIN){ime_type("jintiantianqihenhao");ime_layout(keys);}
        if(mode==INPUT_T9){ime_type("744");ime_layout(keys);}
    }
    ime_reset(INPUT_ENGLISH,383);ime_type("qwertyuiopasdfghjklzxcvbnm");IME_CHECK(!strcmp(editor_text,"qwertyuiopasdfghjklzxcvbnm"));
    ime_action(EK_SHIFT,-1);ime_type("q");IME_CHECK(editor_text[strlen(editor_text)-1]=='Q');ime_action(EK_SPACE,-1);ime_action(EK_COMMA,-1);ime_action(EK_PERIOD,-1);ime_action(EK_NEWLINE,-1);IME_CHECK(strstr(editor_text,"Q ,.\n"));
    ime_reset(INPUT_SYMBOL,383);ime_type("1234567890");IME_CHECK(!strcmp(editor_text,"1234567890"));
    ime_reset(INPUT_PINYIN,383);for(int i=0;i<45;i++)ime_action(EK_LETTER,'a');IME_CHECK(strlen(composition)==sizeof(composition)-1);ime_layout(keys);
    for(int i=0;i<candidate_count;i++){IME_CHECK(candidate_consume[i]>0&&candidate_consume[i]<=strlen(composition));IME_CHECK(u8valid(candidates[i],strlen(candidates[i])));}
}

static void ime_dictionary_checks(void){
    char supplied[512];snprintf(supplied,sizeof(supplied),"%s/pinyin-v1.bin",rootdir);
    snprintf(ime_test_dictionary_path,sizeof(ime_test_dictionary_path),"%s",supplied);
    media.media_generation++;ime_reset(INPUT_PINYIN,383);
    IME_CHECK(ime_dictionary&&ime_dictionary_count>40000&&ime_dictionary_bytes<2*1024*1024);
    const struct {const char *raw,*word;} examples[]={
        {"chang","常"},{"feichang","非常"},{"yuedulijie","阅读理解"},
        {"zhonghuarenmingongheguo","中华人民共和国"},{"rensheng","人生"},
        {"xuetang","学堂"},{"chenggong","成功"}
    };
    for(size_t i=0;i<sizeof(examples)/sizeof(examples[0]);i++){
        ime_reset(INPUT_PINYIN,383);ime_type(examples[i].raw);ime_choose(examples[i].word);
        IME_CHECK(!strcmp(editor_text,examples[i].word)&&!composition[0]);
    }
    ime_reset(INPUT_T9,383);ime_type("33424264");ime_choose("非常");IME_CHECK(!strcmp(editor_text,"非常"));
    // 切换文字模式不丢完整词库；临时OOM仍可在下次显式进入时重试。
    // Text-mode changes retain full lexicon coverage; transient OOM retries on the next explicit entry.
    uint8_t *loaded=ime_dictionary;
    editor_mode(INPUT_ENGLISH);IME_CHECK(ime_dictionary==loaded);
    editor_mode(INPUT_T9);IME_CHECK(ime_dictionary==loaded&&ime_dictionary_count>40000);
    editor_mode(2);IME_CHECK(input_mode==INPUT_PINYIN&&ime_dictionary==loaded);
    ime_type("yuedulijie");ime_choose("阅读理解");IME_CHECK(!strcmp(editor_text,"非常阅读理解"));
    ime_dictionary_release();ime_test_oom=true;ime_dictionary_prepare();IME_CHECK(!ime_dictionary&&!ime_dictionary_attempted&&!ime_test_oom);
    ime_dictionary_prepare();IME_CHECK(ime_dictionary&&ime_dictionary_count>40000);
    // 单次输入不读卡：载入后移走文件仍可继续查询。 / Once loaded, removing the file cannot affect keystroke queries.
    char hidden[512];snprintf(hidden,sizeof(hidden),"%s/lexicon-hidden.bin",rootdir);IME_CHECK(!rename(supplied,hidden));
    ime_reset(INPUT_PINYIN,383);ime_type("chang");ime_choose("常");IME_CHECK(!strcmp(editor_text,"常"));
    IME_CHECK(!rename(hidden,supplied));
    // 校验失败必须保持内置输入可用。 / Corrupt input must retain the built-in fallback.
    FILE *f=fopen(supplied,"r+b");IME_CHECK(f);IME_CHECK(!fseek(f,48,SEEK_SET));int byte=fgetc(f);IME_CHECK(byte!=EOF);IME_CHECK(!fseek(f,48,SEEK_SET));IME_CHECK(fputc(byte^1,f)!=EOF);IME_CHECK(!fclose(f));
    media.media_generation++;ime_reset(INPUT_PINYIN,383);IME_CHECK(!ime_dictionary);
    ime_type("nihao");ime_choose("你好");IME_CHECK(!strcmp(editor_text,"你好"));
    f=fopen(supplied,"r+b");IME_CHECK(f);IME_CHECK(!fseek(f,48,SEEK_SET));IME_CHECK(fputc(byte,f)!=EOF);IME_CHECK(!fclose(f));
    ime_test_dictionary_path[0]=0;media.media_generation++;
}

static void ime_cached_layout(void){
    ime_reset(INPUT_PINYIN,383);ime_type("nihao");editor_key_t keys[64];int n=editor_keys(keys);IME_CHECK(n>0);
    unsigned before=ime_width_calls;
    for(int i=0;i<32;i++)IME_CHECK(editor_keys(keys)==n);
    IME_CHECK(ime_width_calls==before);
    editor_key_t q;IME_CHECK(ime_find(EK_LETTER,'q',&q));before=ime_width_calls;
    ime_event(PS_PRESS,q.x+2,q.y+2,q.x+2,q.y+2);
    ime_event(PS_MOVE,q.x+2,q.y+2,q.x+3,q.y+2);
    ime_event(PS_CANCEL,q.x+2,q.y+2,q.x+3,q.y+2);
    IME_CHECK(ime_width_calls==before);
}

static editor_key_t ime_key_cached(int action,int value){
    editor_key_t keys[64];int n=editor_keys(keys);
    for(int i=0;i<n;i++)if(keys[i].action==action&&(value<0||keys[i].value==value))return keys[i];
    IME_CHECK(false);return keys[0];
}

static int ime_partial_draw(EpdRect out[4]){
    uint8_t fb=0;int count=-1;ime_painted_count=0;
    int saved_stores=stores,saved_roots=root_queries,saved_progress=progress_queries;
    IME_CHECK(ps_reader_editor_render_partial(&fb,out,&count));
    IME_CHECK(count>=0&&count<=3&&stores==saved_stores&&root_queries==saved_roots&&progress_queries==saved_progress);
    for(int i=0;i<count;i++)IME_CHECK(out[i].x>=0&&out[i].y>=0&&out[i].width>0&&out[i].height>0&&out[i].x+out[i].width<=view.w&&out[i].y+out[i].height<=view.h);
    // 部分绘制只能含候选键和变化的空格；字母、模式和完成键保持静止。 / Partial paints contain only candidates and a changed space key, never letters, modes or Done.
    editor_key_t keys[64];int n=editor_keys(keys);
    for(int i=0;i<ime_painted_count;i++){
        bool allowed=false;
        for(int k=0;k<n;k++)if(ime_painted[i].x==keys[k].x&&ime_painted[i].y==keys[k].y&&ime_painted[i].w==keys[k].w&&ime_painted[i].h==keys[k].h)
            allowed=editor_candidate_key(&keys[k])||keys[k].action==EK_SPACE;
        IME_CHECK(allowed);
    }
    return count;
}

static void ime_partial(void){
    EpdRect areas[4];uint8_t fb=0;int count=-1;
    ime_reset(INPUT_PINYIN,383);IME_CHECK(!ps_reader_editor_render_partial(&fb,areas,&count)&&!count);
    ime_painted_count=0;render_checked();int before=draws;IME_CHECK(!ime_partial_draw(areas)&&draws==before);
    editor_key_t n=ime_key_cached(EK_LETTER,'n');int x=n.x+n.w/2,y=n.y+n.h/2;
    ime_event(PS_PRESS,x,y,x,y);before=draws;IME_CHECK(!ime_partial_draw(areas)&&draws==before);
    ime_event(PS_RELEASE,x,y,x,y);IME_CHECK(ime_partial_draw(areas)==2&&!strcmp(composition,"n"));
    ime_press_key(ime_key_cached(EK_LETTER,'i'));IME_CHECK(ime_partial_draw(areas)==1&&!strcmp(composition,"ni"));
    ime_press_key(ime_key_cached(EK_LETTER,'h'));IME_CHECK(ime_partial_draw(areas)==1);
    ime_press_key(ime_key_cached(EK_LETTER,'a'));IME_CHECK(ime_partial_draw(areas)==1);
    ime_press_key(ime_key_cached(EK_LETTER,'o'));IME_CHECK(ime_partial_draw(areas)==1);
    int found=-1;for(int i=0;i<candidate_count;i++)if(!strcmp(candidates[i],"你好"))found=i;
    IME_CHECK(found>=0);ime_press_key(ime_key_cached(EK_CANDIDATE,found));
    IME_CHECK(ime_partial_draw(areas)==3&&!strcmp(editor_text,"你好")&&!composition[0]);
    before=draws;IME_CHECK(!ime_partial_draw(areas)&&draws==before);
    ime_press_key(ime_key_cached(EK_NEXT,-1));before=draws;IME_CHECK(!ime_partial_draw(areas)&&draws==before);
    // 模式、方向、字体和覆盖恢复都退回完整绘制，不沿用失效基准。 / Mode, orientation, font and overlay restoration invalidate the partial baseline.
    ime_press_key(ime_key_cached(EK_MODE,INPUT_ENGLISH));IME_CHECK(!ps_reader_editor_render_partial(&fb,areas,&count)&&!count);ime_painted_count=0;render_checked();
    ime_press_key(ime_key_cached(EK_LETTER,'q'));IME_CHECK(ime_partial_draw(areas)==1&&!ime_painted_count);
    ime_press_key(ime_key_cached(EK_DELETE,-1));IME_CHECK(ime_partial_draw(areas)==1&&!ime_painted_count);
    editor_text[0]=0;ime_painted_count=0;render_checked();ime_press_key(ime_key_cached(EK_DELETE,-1));before=draws;IME_CHECK(!ime_partial_draw(areas)&&draws==before);
    ime_press_key(ime_key_cached(EK_SHIFT,-1));IME_CHECK(!ps_reader_editor_render_partial(&fb,areas,&count)&&!count);ime_painted_count=0;render_checked();
    int old_weight=weight;weight=old_weight==400?600:400;IME_CHECK(!ps_reader_editor_render_partial(&fb,areas,&count));weight=old_weight;ime_painted_count=0;render_checked();
    ps_view_t saved=view;view=view.landscape?(ps_view_t){360,640,false}:(ps_view_t){640,360,true};IME_CHECK(!ps_reader_editor_render_partial(&fb,areas,&count));view=saved;ime_painted_count=0;render_checked();
    ps_reader_editor_invalidate();IME_CHECK(!ps_reader_editor_render_partial(&fb,areas,&count));ime_painted_count=0;render_checked();
}

static void ime_burst(void){
    ime_reset(INPUT_ENGLISH,383);ime_painted_count=0;render_checked();int before=draws;
    const char *text="qwertyuiopasdfghjklzxcvbnm";
    for(const char *p=text;*p;p++)ime_press_key(ime_key_cached(EK_LETTER,*p));
    IME_CHECK(!strcmp(editor_text,text)&&draws==before);
    for(int i=0;i<8;i++)ime_press_key(ime_key_cached(EK_DELETE,-1));
    IME_CHECK(!strcmp(editor_text,"qwertyuiopasdfghjk"));
    ime_press_key(ime_key_cached(EK_LETTER,'q'));ime_press_key(ime_key_cached(EK_DONE,-1));
    IME_CHECK(!strcmp(callback_text,"qwertyuiopasdfghjkq")&&ps_route()!=PS_EDITOR);
    ime_reset(INPUT_PINYIN,383);ime_painted_count=0;render_checked();
    ime_press_key(ime_key_cached(EK_LETTER,'n'));ime_press_key(ime_key_cached(EK_LETTER,'i'));
    EpdRect areas[4];IME_CHECK(ime_partial_draw(areas)>0&&editor_visible_count>0);
    editor_visible_candidate_t visible=editor_visible_candidates[0];
    // 新输入还未推屏，候选位置必须仍选择已画出的字，而不是新的内部候选数组。
    // Further input has not been painted; the visible cell must select its painted text rather than the new internal candidate list.
    ime_press_key(ime_key_cached(EK_LETTER,'h'));ime_press_key(ime_key_cached(EK_LETTER,'a'));ime_press_key(ime_key_cached(EK_LETTER,'o'));
    int x=visible.x+visible.w/2,y=visible.y+visible.h/2;ime_touch(x,y);
    IME_CHECK(!strcmp(editor_text,visible.text)&&!strcmp(composition,"hao"));
    // 取消不能提交排队输入，合成TAP也不能重复输入。
    // Cancellation cannot commit buffered input, and a synthesized TAP cannot duplicate it.
    editor_key_t k=ime_key_cached(EK_LETTER,'q');x=k.x+k.w/2;y=k.y+k.h/2;
    ime_event(PS_PRESS,x,y,x,y);ime_event(PS_CANCEL,x,y,x,y);ime_event(PS_RELEASE,x,y,x,y);ime_event(PS_TAP,x,y,x,y);
    IME_CHECK(!strcmp(composition,"hao"));
}

static void ime_password(void){
    ps_edit_password("Wi-Fi 密码",64,done);
    IME_CHECK(editor_password&&!editor_password_visible&&input_mode==INPUT_ENGLISH);
    bool available[128]={false};
    for(int mode=0;mode<4;mode++){
        ime_action(EK_PASSWORD_MODE,mode);editor_key_t keys[64];int n=ime_layout(keys);
        for(int i=0;i<n;i++){
            IME_CHECK(keys[i].action!=EK_MODE&&keys[i].action!=EK_NEWLINE);
            if(keys[i].action==EK_LETTER)available[mode==1?toupper(keys[i].value):keys[i].value]=true;
        }
    }
    available[' ']=true;for(int c=32;c<127;c++)IME_CHECK(available[c]);
    ime_action(EK_PASSWORD_MODE,0);ime_type("secret");IME_CHECK(!strcmp(editor_text,"secret")&&!editor_password_visible);
    ime_action(EK_PASSWORD_MODE,4);IME_CHECK(editor_password_visible);
    ime_action(EK_PASSWORD_MODE,4);IME_CHECK(!editor_password_visible);
    editor_commit();IME_CHECK(ps_route()==PS_EDITOR&&!strcmp(editor_text,"secret"));
    int before=stores;ps_back();IME_CHECK(!editor_text[0]&&!editor_password&&stores==before);
    IME_CHECK(!editor_frame.valid);for(size_t i=0;i<sizeof(editor_frame.text);i++)IME_CHECK(!editor_frame.text[i]);
    ps_edit_password("Wi-Fi 密码",8,done);ime_type("password");ime_type("extra");IME_CHECK(strlen(editor_text)==8);
    ime_action(EK_DONE,0);IME_CHECK(!strcmp(callback_text,"password")&&!editor_text[0]&&!editor_password);
    IME_CHECK(!editor_frame.valid);for(size_t i=0;i<sizeof(editor_frame.text);i++)IME_CHECK(!editor_frame.text[i]);
}

int main(int argc,char **argv){
    IME_CHECK(argc==2||argc==3);snprintf(rootdir,sizeof(rootdir),"%s",argv[1]);ps_reader_init();IME_CHECK(initialized&&rr&&pages);
    const char *group=argc==3?argv[2]:"all";
    const struct {const char *name;void (*run)(void);} groups[]={
        {"continuous",ime_continuous},{"separators",ime_separators},{"paging",ime_paging},{"touch",ime_touch_sequences},{"modes",ime_modes},{"password",ime_password},{"dictionary",ime_dictionary_checks},{"cache",ime_cached_layout},{"partial",ime_partial},{"burst",ime_burst}
    };
    for(int landscape=0;landscape<2;landscape++){
        view=landscape?(ps_view_t){640,360,true}:(ps_view_t){360,640,false};
        bool selected=false;
        for(size_t i=0;i<sizeof(groups)/sizeof(groups[0]);i++){
            if(!strcmp(group,"all")||!strcmp(group,groups[i].name)){
                groups[i].run();selected=true;
                printf("PASS: %s %s.\n",landscape?"landscape":"portrait",groups[i].name);
            }
        }
        IME_CHECK(selected);
    }
    printf("PASS: input method %d checks; host geometry/storage doubles, device acceptance still required.\n",ime_checks);
    free(rr);free(pages);for(int i=0;i<8;i++)free(blobs[i].bytes);return 0;
}

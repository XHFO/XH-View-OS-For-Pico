/*
 * SPDX-FileCopyrightText: 2026 XH Lab
 * SPDX-License-Identifier: Apache-2.0
 * 中文：TF卡独立画板笔记；向量原稿、画笔、橡皮、撤销与继续绘画。
 * English: Independent TF drawing notes with vector originals, pen, eraser, undo and resume.
 * 冻结：按用户整纸旋转要求替换裁切视口；原稿坐标不迁移，横屏等比旋转90度并逆变换输入，网点与橡皮共用背景；绘图不读写文件，保存失败保留原稿，仅经宿主原子存储提交。
 * Frozen: Replace cropped viewports with the requested whole-paper rotation; retain original coordinates, rotate landscape projection 90 degrees with inverse input, share the dotted background with erasing, perform no drawing IO, retain failed saves and commit through host atomic storage only.
 */
#include "pico_app_sdk.h"

enum { BOARD_COUNT=12, POINT_MAX=8192, DRAW_MAGIC=0x314e4450u, INDEX_MAGIC=0x31494450u };
typedef struct {uint16_t x,y;uint8_t width,flags;} point_t;
typedef struct {
    uint32_t magic;uint16_t version,width,height,count;uint8_t reserved[4];
    point_t points[POINT_MAX];
} drawing_t;
typedef struct {uint32_t magic;uint16_t used;uint8_t current,reserved;uint16_t counts[BOARD_COUNT];} index_t;
typedef struct {int x,y,w,h;} rect_t;
typedef struct {rect_t paper;int numerator,denominator;bool rotated;} projection_t;
static const pico_app_host_t *host;
// 编译器生成的结构复制也不导入libc。/ Compiler-generated struct copies do not import libc either.
__attribute__((visibility("hidden"))) void *memcpy(void *dst,const void *src,size_t n){return host->copy(dst,src,n);}
static drawing_t drawing;
static index_t catalog;
static int board=-1,list_page,pen=1,modal;
static bool editing,down,erasing,modified,full_ink,save_failed,gesture_drawn;
static unsigned painted;
static int64_t save_due;

static int min(int a,int b){return a<b?a:b;}
static int max(int a,int b){return a>b?a:b;}
static int clamp(int x,int a,int b){return x<a?a:x>b?b:x;}
static bool hit(int x,int y,rect_t r){return x>=r.x&&y>=r.y&&x<r.x+r.w&&y<r.y+r.h;}
// 横竖可写区互为转置；左栏避免横屏工具挤占纸面高度。
// Transpose the writable dimensions; a left toolbar preserves landscape paper height.
static rect_t canvas(void){pico_app_view_t v=host->view();return v.landscape?(rect_t){148,24,v.w-160,v.h-24}:(rect_t){12,120,v.w-24,v.h-160};}
static rect_t tool(int index){pico_app_view_t v=host->view();int w=(v.w-40)/5;return v.landscape?(rect_t){12,88+index*44,124,36}:(rect_t){12+index*(w+4),78,w,32};}
static projection_t projection(void){
    rect_t c=canvas();bool rotated=host->view().landscape;
    int w=rotated?drawing.height:drawing.width,h=rotated?drawing.width:drawing.height,n=c.w,d=w;
    if(c.w*h>c.h*w){n=c.h;d=h;}
    rect_t p={0,0,w*n/d,h*n/d};p.x=c.x+(c.w-p.w)/2;p.y=c.y+(c.h-p.h)/2;
    return (projection_t){p,n,d,rotated};
}
static rect_t paper(void){return projection().paper;}
static void project_with(point_t p,const projection_t *t,int *x,int *y){
    int u=p.x*t->numerator/t->denominator,v=p.y*t->numerator/t->denominator;
    *x=t->paper.x+(t->rotated?t->paper.w-1-v:u);*y=t->paper.y+(t->rotated?u:v);
}
static void project(point_t p,int *x,int *y){projection_t t=projection();project_with(p,&t,x,y);}
static bool grid_axis(int pixel,int extent,int origin,const projection_t *t){
    int near=(pixel*t->denominator/t->numerator-origin)/50;
    for(int k=max(0,near-1);k<=near+1;k++){
        int coordinate=origin+k*50;
        if(coordinate<extent&&coordinate*t->numerator/t->denominator==pixel)return true;
    }
    return false;
}
static int background(int x,int y,const projection_t *t){
    int u=t->rotated?y-t->paper.y:x-t->paper.x,v=t->rotated?t->paper.w-1-(x-t->paper.x):y-t->paper.y;
    return grid_axis(u,drawing.width,25,t)&&grid_axis(v,drawing.height,25,t)?7:15;
}
static void draw_grid(uint8_t *fb,const projection_t *t){
    for(int y=25;y<drawing.height;y+=50)for(int x=25;x<drawing.width;x+=50){
        int xx,yy;project_with((point_t){(uint16_t)x,(uint16_t)y,1,1},t,&xx,&yy);host->fill(fb,xx,yy,1,1,7);
    }
}
// 橡皮逐像素恢复同一纸面背景，不能把网点擦成白色脏块。
// Restore the same paper background under erasing, rather than leaving white holes in the dots.
static void background_line(uint8_t *fb,int x,int y,int xx,int yy,const projection_t *t){
    int dx=x<xx?xx-x:x-xx,sx=x<xx?1:-1,dy=y<yy?y-yy:yy-y,sy=y<yy?1:-1,err=dx+dy;
    for(;;){
        host->fill(fb,x,y,1,1,background(x,y,t));if(x==xx&&y==yy)break;
        int twice=err*2;if(twice>=dy){err+=dy;x+=sx;}if(twice<=dx){err+=dx;y+=sy;}
    }
}
static void key_for(char key[16],int index){host->format(key,16,"board%02d",index+1);}
static bool valid(void){
    if(drawing.magic!=DRAW_MAGIC||drawing.version!=1||drawing.count>POINT_MAX||
       drawing.width<100||drawing.width>2000||drawing.height<100||drawing.height>2000)return false;
    for(unsigned i=0;i<drawing.count;i++){
        point_t p=drawing.points[i];
        if(p.x>=drawing.width||p.y>=drawing.height||!p.width||p.width>48||(p.flags&~3u)||(!i&&!(p.flags&1u)))return false;
    }
    return true;
}
static bool save(void){
    if(!modified||board<0)return true;
    char key[16];key_for(key,board);
    // 原稿先落盘；目录失败时保留dirty，下次保存仍可重试，十二个固定入口可找回孤立原稿。
    // Commit the original first; catalog failure retains dirty for retry, and fixed board slots expose orphan originals.
    if(!host->store(key,&drawing,sizeof(drawing))){save_failed=true;return false;}
    index_t next;host->copy(&next,&catalog,sizeof(next));next.magic=INDEX_MAGIC;next.current=(uint8_t)board;
    next.used|=(uint16_t)(1u<<board);next.counts[board]=drawing.count;
    if(!host->store("index",&next,sizeof(next))){save_failed=true;return false;}
    host->copy(&catalog,&next,sizeof(catalog));modified=false;save_failed=false;return true;
}
static void changed(void){modified=true;save_failed=false;save_due=host->time_us()+1800000;}
static bool open_board(int index){
    char key[16];key_for(key,index);
    bool loaded=host->load(key,&drawing,sizeof(drawing));
    if(loaded&&!valid()){host->toast("画板数据异常，已保留原文件");return false;}
    if(!loaded){
        if(catalog.used&(1u<<index)){host->toast("读取未完成，请检查 TF 卡；原稿未覆盖");return false;}
        host->set(&drawing,0,sizeof(drawing));drawing.magic=DRAW_MAGIC;drawing.version=1;
        drawing.width=1400;drawing.height=2000;
    }
    board=index;editing=true;down=false;modified=false;save_failed=false;modal=0;painted=0;full_ink=true;
    catalog.current=(uint8_t)index;
    if(loaded){catalog.used|=(uint16_t)(1u<<index);catalog.counts[index]=drawing.count;}
    return true;
}
static int outside(int x,int y,rect_t c){return (x<c.x?1:x>=c.x+c.w?2:0)|(y<c.y?4:y>=c.y+c.h?8:0);}
static void clipped_line(uint8_t *fb,int x,int y,int xx,int yy,rect_t c,int gray,const projection_t *t){
    for(unsigned attempt=0;attempt<8;attempt++){
        int a=outside(x,y,c),b=outside(xx,yy,c);if(!(a|b)){if(gray==15)background_line(fb,x,y,xx,yy,t);else host->line(fb,x,y,xx,yy,gray);return;}if(a&b)return;
        int out=a?a:b,nx,ny;
        if(out&12){ny=out&4?c.y:c.y+c.h-1;if(yy==y)return;nx=x+(xx-x)*(ny-y)/(yy-y);}
        else {nx=out&1?c.x:c.x+c.w-1;if(xx==x)return;ny=y+(yy-y)*(nx-x)/(xx-x);}
        if(a){x=nx;y=ny;}else{xx=nx;yy=ny;}
    }
}
static void draw_point(uint8_t *fb,unsigned index){
    projection_t t=projection();rect_t c=t.paper;point_t p=drawing.points[index];int x,y;project_with(p,&t,&x,&y);
    int width=max(1,(int)p.width*t.numerator/t.denominator),radius=width/2,gray=(p.flags&2u)?15:0;
    int x0=x,y0=y;if(index&&!(p.flags&1u))project_with(drawing.points[index-1],&t,&x0,&y0);
    for(int delta=-radius;delta<=radius;delta++){
        clipped_line(fb,x0+delta,y0,x+delta,y,c,gray,&t);clipped_line(fb,x0,y0+delta,x,y+delta,c,gray,&t);
    }
    if(radius)for(int dy=-radius;dy<=radius;dy++){
        int extent=radius;while(extent>0&&extent*extent+dy*dy>radius*radius)extent--;
        int left=max(c.x,x-extent),right=min(c.x+c.w-1,x+extent),row=y+dy;
        if(left<=right&&row>=c.y&&row<c.y+c.h){
            if(gray==15)background_line(fb,left,row,right,row,&t);
            else host->fill(fb,left,row,right-left+1,1,gray);
        }
    }
}
static void title(uint8_t *fb,const char *text){host->heading(fb,text,NULL);}
static void render_list(uint8_t *fb){
    pico_app_view_t v=host->view();host->fill(fb,0,24,v.w,v.h-24,15);title(fb,"画板笔记");
    host->text(fb,16,76,12,"独立 TF 应用 · 原稿可继续编辑",4);
    int cols=v.landscape?3:2,rows=3,w=(v.w-42-(cols-1)*10)/cols,h=v.landscape?58:136;
    for(int slot=0;slot<cols*rows;slot++){
        int i=list_page*cols*rows+slot;if(i>=BOARD_COUNT)break;
        int x=16+(slot%cols)*(w+10),y=111+(slot/cols)*(h+10);char label[48];
        host->box(fb,x,y,w,h,8,13,true);host->box(fb,x,y,w,h,8,9,false);
        host->format(label,sizeof(label),"画板 %02d",i+1);host->text(fb,x+12,y+12,17,label,1);
        bool used=(catalog.used&(1u<<i))!=0;
        host->text(fb,x+12,y+(v.landscape?37:58),12,used?"打开 / 继续绘画":"打开 / 新建",4);
        if(!v.landscape&&used){host->format(label,sizeof(label),"%u 个笔迹采样",catalog.counts[i]);host->text(fb,x+12,y+92,11,label,5);}
    }
    int per=cols*rows,pages=(BOARD_COUNT+per-1)/per,y=v.h-50;char label[24];
    host->button(fb,16,y,82,32,"上一页",false);host->button(fb,v.w-98,y,82,32,"下一页",false);
    host->format(label,sizeof(label),"%d / %d",list_page+1,pages);host->center(fb,v.w/2,y+9,12,label,4);
}
static void render(uint8_t *fb){
    if(!editing){render_list(fb);return;}
    pico_app_view_t v=host->view();host->fill(fb,0,24,v.w,v.h-24,15);char text[48];
    host->format(text,sizeof(text),"画板 %02d",board+1);
    if(v.landscape){host->text(fb,12,40,18,text,0);host->text(fb,12,65,10,"原稿保存在 TF 卡",4);}
    else {title(fb,text);host->text(fb,v.w-143,46,11,"原稿保存在 TF 卡",4);}
    const char *labels[]={erasing?"橡皮":"画笔",pen==1?"细笔":pen==2?"中笔":"粗笔","撤销","清空","保存"};
    for(int i=0;i<5;i++){rect_t r=tool(i);host->button(fb,r.x,r.y,r.w,r.h,labels[i],i==0?erasing:i==1?pen>1:false);}
    projection_t t=projection();rect_t c=canvas(),p=t.paper;host->fill(fb,c.x,c.y,c.w,c.h,13);host->fill(fb,p.x,p.y,p.w,p.h,15);draw_grid(fb,&t);
    for(unsigned i=0;i<drawing.count;i++)draw_point(fb,i);
    if(v.landscape)host->text(fb,12,v.h-27,10,"整纸随方向旋转",4);
    else host->center(fb,v.w/2,v.h-27,10,"整纸随方向旋转",4);
    painted=drawing.count;full_ink=false;
    if(modal){
        int w=min(v.w-40,430),x=(v.w-w)/2,y=(v.h-180)/2,bw=(w-48)/2;
        host->box(fb,x,y,w,180,10,15,true);host->box(fb,x,y,w,180,10,0,false);
        host->text(fb,x+18,y+20,20,"清空这张画板？",0);
        host->text(fb,x+18,y+64,12,"确认后可绘制新的内容",4);
        host->button(fb,x+16,y+118,bw,42,"取消",false);host->button(fb,x+32+bw,y+118,bw,42,"清空",true);
    }
}
static bool append(int x,int y,bool first){
    rect_t p=paper();if(!hit(x,y,p)&&first)return false;
    if(drawing.count==POINT_MAX){down=false;host->toast("画板已满，请保存后使用另一张画板");return false;}
    int radius=erasing?7:pen;
    x=clamp(x,p.x+radius,p.x+p.w-radius-1);y=clamp(y,p.y+radius,p.y+p.h-radius-1);
    projection_t t=projection();int u=(t.rotated?y-p.y:x-p.x)*t.denominator/t.numerator,v=(t.rotated?p.w-1-(x-p.x):y-p.y)*t.denominator/t.numerator;
    point_t next={(uint16_t)clamp(u,0,drawing.width-1),(uint16_t)clamp(v,0,drawing.height-1),
        (uint8_t)clamp((erasing?14:pen*2)*t.denominator/t.numerator,1,48),(uint8_t)((first?1:0)|(erasing?2:0))};
    if(!first&&drawing.count){point_t previous=drawing.points[drawing.count-1];if(previous.x==next.x&&previous.y==next.y)return false;}
    drawing.points[drawing.count++]=next;changed();return true;
}
static bool event(const pico_app_event_t *e){
    if(!e)return false;
    pico_app_view_t v=host->view();
    if(!editing){
        if(e->type!=PGA_TAP)return false;
        int cols=v.landscape?3:2,rows=3,w=(v.w-42-(cols-1)*10)/cols,h=v.landscape?58:136,per=cols*rows;
        if(e->y>=v.h-50){if(e->x<100&&list_page>0)list_page--;else if(e->x>v.w-100&&(list_page+1)*per<BOARD_COUNT)list_page++;return true;}
        for(int slot=0;slot<per;slot++){int i=list_page*per+slot;if(i<BOARD_COUNT&&hit(e->x,e->y,(rect_t){16+(slot%cols)*(w+10),111+(slot/cols)*(h+10),w,h}))return open_board(i);}
        return false;
    }
    if(modal){
        if(e->type!=PGA_TAP)return true;
        int w=min(v.w-40,430),x=(v.w-w)/2,y=(v.h-180)/2,bw=(w-48)/2;
        if(!hit(e->x,e->y,(rect_t){x,y,w,180})||hit(e->x,e->y,(rect_t){x+16,y+118,bw,42}))modal=0;
        else if(hit(e->x,e->y,(rect_t){x+32+bw,y+118,bw,42})){drawing.count=0;changed();modal=0;}
        full_ink=true;return true;
    }
    if(e->type==PGA_PRESS){down=hit(e->x,e->y,paper());gesture_drawn=down;return down&&append(e->x,e->y,true);}
    if(e->type==PGA_MOVE&&down)return append(e->x,e->y,false);
    if(e->type==PGA_RELEASE||e->type==PGA_CANCEL){bool was=down;if(down&&e->type==PGA_RELEASE)(void)append(e->x,e->y,false);down=false;if(e->type==PGA_CANCEL)gesture_drawn=false;save_due=host->time_us()+1800000;return was;}
    if(e->type!=PGA_TAP)return false;
    if(gesture_drawn){gesture_drawn=false;return true;}
    for(int i=0;i<5;i++)if(hit(e->x,e->y,tool(i))){
        if(i==0)erasing=!erasing;
        if(i==1)pen=pen==1?2:pen==2?4:1;
        if(i==2&&drawing.count){unsigned at=drawing.count;do{at--;}while(at&&!(drawing.points[at].flags&1u));drawing.count=(uint16_t)at;changed();}
        if(i==3)modal=1;
        if(i==4){if(save())host->toast("画板已保存，可继续绘画");else host->toast("保存未完成，原稿仍保留；请检查 TF 卡");}
        full_ink=true;return true;
    }
    // 一次轻点也形成独立笔迹；拖动接触则由PRESS/MOVE/RELEASE处理。
    // A tap creates a separate dot; dragged contacts use PRESS/MOVE/RELEASE.
    if(hit(e->x,e->y,paper()))return append(e->x,e->y,true);
    return false;
}
static bool key(int value){
    if(editing&&!modal&&(value==0||value==2)){rect_t r=tool(value==0?2:4);pico_app_event_t e={.type=PGA_TAP,.x=r.x+r.w/2,.y=r.y+r.h/2};gesture_drawn=down=false;return event(&e);}
    if(value!=-1)return false;
    down=false;
    if(modal){modal=0;full_ink=true;return true;}
    if(editing){if(!save()){host->toast("保存未完成，画板已保留；请检查 TF 卡");return true;}editing=false;full_ink=true;return true;}
    return false;
}
static bool tick(void){
    if(editing&&modified&&!down&&!modal&&!save_failed&&host->time_us()>=save_due){if(!save())host->toast("自动保存未完成，请检查 TF 卡；原稿已保留");}
    return false;
}
static void action(int id){(void)id;}
static uint32_t status(void){return (editing&&!modal?PGA_STATUS_INK:0)|(modified?PGA_STATUS_UNSAVED:0);}
static void stop(void){down=false;}
static int32_t ink(uint8_t *fb,int32_t rects[4][4]){
    if(!editing||modal||full_ink||painted>drawing.count)return -1;
    if(painted==drawing.count)return 0;
    projection_t t=projection();rect_t c=t.paper;int x0=c.x+c.w,y0=c.y+c.h,x1=c.x-1,y1=c.y-1;
    for(unsigned i=painted;i<drawing.count;i++){
        draw_point(fb,i);point_t p=drawing.points[i];int r=max(1,(int)p.width*t.numerator/t.denominator)+2;
        int x,y;project(p,&x,&y);x0=min(x0,x-r);y0=min(y0,y-r);x1=max(x1,x+r);y1=max(y1,y+r);
        if(i&&!(p.flags&1u)){project(drawing.points[i-1],&x,&y);x0=min(x0,x-r);y0=min(y0,y-r);x1=max(x1,x+r);y1=max(y1,y+r);}
    }
    painted=drawing.count;x0=max(c.x,x0);y0=max(c.y,y0);x1=min(c.x+c.w-1,x1);y1=min(c.y+c.h-1,y1);
    if(x1<x0||y1<y0)return 0;
    rects[0][0]=x0;rects[0][1]=y0;rects[0][2]=x1-x0+1;rects[0][3]=y1-y0+1;return 1;
}
static bool start(const pico_app_host_t *api){
    if(!api||api->abi!=PICO_APP_ABI||api->bytes<sizeof(*api)||!api->view||!api->load||!api->store||!api->time_us||
       !api->fill||!api->line||!api->box||!api->text||!api->center||!api->button||!api->heading||!api->toast||
       !api->format||!api->copy||!api->set)return false;
    host=api;host->set(&catalog,0,sizeof(catalog));
    if(!host->load("index",&catalog,sizeof(catalog))||catalog.magic!=INDEX_MAGIC||catalog.current>=BOARD_COUNT||(catalog.used&~4095u))host->set(&catalog,0,sizeof(catalog));
    board=-1;editing=down=modified=save_failed=false;full_ink=true;list_page=modal=0;pen=1;erasing=false;return true;
}
static const pico_app_exports_t exports={
    .abi=PICO_APP_ABI,.bytes=sizeof(pico_app_exports_t),.version=10002,.kind=PGA_DRAWING,.id="drawing-notes",
    .start=start,.render=render,.event=event,.key=key,.tick=tick,.action=action,.status=status,.save=save,.stop=stop,.ink=ink
};
__attribute__((visibility("default"))) const pico_app_exports_t *pico_app_query(void){return &exports;}

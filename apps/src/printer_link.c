/*
 * SPDX-FileCopyrightText: 2026 XH Lab
 * SPDX-License-Identifier: Apache-2.0
 * 中文：独立打印机 TF 应用；脱敏快照、HA 前缀添加、改名和移除，不依赖家庭连接应用包。
 * English: Independent printer TF app with redacted snapshots, HA-prefix setup, rename and removal; no Home Link package dependency.
 * 冻结：只读监看，不发送打印控制；旧直连配置保留，不伪造未迁移协议的连接状态。
 * Frozen: Monitoring only, never print control; retain legacy direct configs without inventing unsupported connection states.
 */
#include "pico_app_sdk.h"
enum { PAGE_LIST, PAGE_EDIT, PAGE_REMOVE, PAGE_ADD, PAGE_FIND };
static const pico_app_host_t *host;
static unsigned page;
static bool waiting;
static int input_field;
static pico_app_home_status_t status;
static pico_app_home_printer_t printers[3];
static pico_app_printer_config_t slots[3],edit;
static char input[256],message[96];
static pico_app_printer_config_t candidates[8];
static unsigned candidate_count,candidate_page;

__attribute__((visibility("hidden"))) void *memcpy(void *d,const void *s,size_t n){return host->copy(d,s,n);}
__attribute__((visibility("hidden"))) void *memset(void *d,int c,size_t n){return host->set(d,c,n);}
static bool available(void){
    return host->bytes>=offsetof(pico_app_host_t,printer_input)+sizeof(host->printer_input)&&
        host->printer_link&&host->printer_item&&host->printer_config&&host->printer_input;
}
static bool discovery_available(void){return host->bytes>=offsetof(pico_app_host_t,printer_candidate)+sizeof(host->printer_candidate)&&host->printer_candidate;}
static bool same(const char *a,const char *b){while(*a&&*a==*b){a++;b++;}return *a==*b;}
static bool hit(const pico_app_event_t *e,int x,int y,int w,int h){
    return e->type==PGA_TAP&&e->x>=x&&e->x<x+w&&e->y>=y&&e->y<y+h;
}
static const char *source_name(unsigned source){
    return source==1?"Home Assistant":source==2?"Bambu 局域网":source==3?"Bambu 云端":source==4?"Formlabs":"未添加";
}
static void refresh(void){
    if(!available())return;
    pico_app_home_status_t next={.bytes=sizeof(next)};
    if(!host->printer_link(PGA_HOME_STATUS,&next))return;
    bool changed=next.revision!=status.revision||next.flags!=status.flags||next.busy!=status.busy;
    if(status.busy&&!next.busy&&(page==PAGE_LIST||page==PAGE_FIND))
        host->format(message,sizeof(message),"%s",next.error[0]?next.error:page==PAGE_LIST?"状态已刷新":"");
    status=next;
    if(changed){
        for(unsigned i=0;i<3;i++){
            slots[i].bytes=sizeof(slots[i]);slots[i].slot=i;
            (void)host->printer_config(PGA_PRINTER_GET,&slots[i]);
            printers[i].bytes=sizeof(printers[i]);
            (void)host->printer_item(i,&printers[i]);
        }
        candidate_count=0;
        if(discovery_available())for(unsigned i=0;i<8;i++){
            pico_app_printer_config_t found={.bytes=sizeof(found)};
            if(!host->printer_candidate(i,&found))break;
            bool added=false;for(unsigned j=0;j<3;j++)if(slots[j].source==1&&same(slots[j].serial,found.serial))added=true;
            if(!added)candidates[candidate_count++]=found;
        }
        host->dirty();
    }
}
static void sync(void){
    if(!(status.flags&PGA_HOME_WIFI)){host->format(message,sizeof(message),"请先连接 Wi-Fi");return;}
    if(!(status.flags&PGA_HOME_CFG)){host->format(message,sizeof(message),"请先在家庭连接中配置 Home Assistant 服务");return;}
    pico_app_home_status_t st={.bytes=sizeof(st)};
    if(!host->printer_link(PGA_HOME_SYNC,&st))host->format(message,sizeof(message),"暂时无法刷新，请稍后重试");
    else host->format(message,sizeof(message),"正在刷新打印机状态…");
    host->dirty();
}
static void begin(unsigned slot){
    edit=(pico_app_printer_config_t){.bytes=sizeof(edit),.slot=slot};
    if(!host->printer_config(PGA_PRINTER_GET,&edit)){host->toast("无法读取配置，请重试");return;}
    bool empty=!edit.source;if(empty)edit.source=1;
    page=empty&&discovery_available()?PAGE_ADD:PAGE_EDIT;message[0]=0;host->dirty();
}
static int footer_x(unsigned i,pico_app_view_t v){return 16+(int)i*((v.w-48)/3+8);}
static bool bottom(const pico_app_event_t *e,unsigned i){
    pico_app_view_t v=host->view();return hit(e,footer_x(i,v),v.h-56,(v.w-48)/3,40);
}
static void buttons(uint8_t *fb,const char *a,const char *b,const char *c,bool primary){
    pico_app_view_t v=host->view();const char *labels[]={a,b,c};
    for(unsigned i=0;i<3;i++)host->button(fb,footer_x(i,v),v.h-56,(v.w-48)/3,40,labels[i],primary&&i==2);
}
static void render(uint8_t *fb){
    pico_app_view_t v=host->view();host->fill(fb,0,0,v.w,v.h,15);
    if(!available()){
        host->heading(fb,"3D 打印机","请更新系统后再打开此应用");
        buttons(fb,"返回","返回","返回",false);return;
    }
    if(page==PAGE_LIST){
        host->heading(fb,"3D 打印机",status.busy?"正在刷新…":"只读监看 · 轻点设备可管理");
        for(unsigned i=0;i<3;i++){
            int y=(v.landscape?112:152)+(int)i*(v.landscape?46:106),h=v.landscape?40:96;
            const pico_app_printer_config_t *p=&slots[i];const pico_app_home_printer_t *s=&printers[i];
            host->box(fb,16,y,v.w-32,h,8,15,true);host->box(fb,16,y,v.w-32,h,8,11,false);
            char name[96],detail[128];host->format(name,sizeof(name),"%u · %s",i+1,p->source?p->name:"添加打印机");
            host->wrap(fb,28,y+8,v.landscape?240:v.w-56,14,1,name,1);
            if(!p->source)host->format(detail,sizeof(detail),"空槽位 · 轻点添加");
            else if(p->source>1)host->format(detail,sizeof(detail),"%s · 配置已保留，直连暂不可用",source_name(p->source));
            else if(s->has_data&&s->percent>=0)host->format(detail,sizeof(detail),"%s · %d%%",s->state[0]?s->state:"已连接",s->percent);
            else host->format(detail,sizeof(detail),"%s",s->has_data?(s->state[0]?s->state:"已连接"):(status.busy?"正在获取状态…":"尚无状态 · 请检查 HA 集成与连接"));
            host->wrap(fb,v.landscape?280:28,y+(v.landscape?11:38),v.landscape?v.w-312:v.w-56,11,v.landscape?1:2,detail,5);
        }
        if(message[0])host->wrap(fb,20,v.landscape?258:492,v.w-40,11,v.landscape?1:2,message,5);
        buttons(fb,"返回",status.busy?"刷新中":"刷新","添加",false);
    }else if(page==PAGE_ADD){
        host->heading(fb,"选择打印机来源","先选择连接方式，再确认具体打印机");
        int y=v.landscape?116:164;
        host->button(fb,16,y,v.w-32,52,"通过 Home Assistant 查找",true);
        host->wrap(fb,24,y+68,v.w-48,12,3,"复用家庭连接服务。选择已发现的打印状态实体，自动匹配进度、任务和温度，再确认保存。",5);
        if(message[0])host->wrap(fb,24,v.h-104,v.w-48,11,2,message,5);
        buttons(fb,"取消","手机填写","手动添加",false);
    }else if(page==PAGE_FIND){
        host->heading(fb,"选择打印机",status.busy?"正在查找 HA 打印状态实体…":"只列出未添加的打印机 · 左右滑动翻页");
        unsigned pages=candidate_count?(candidate_count+3)/4:1;if(candidate_page>=pages)candidate_page=pages-1;
        if(!candidate_count)host->wrap(fb,24,v.landscape?132:220,v.w-48,13,3,
            status.busy?"正在获取打印机列表…":message[0]?message:"未发现打印机。请先在 HA 中添加打印机集成，或选择手动添加。",5);
        for(unsigned i=0;i<4&&candidate_page*4+i<candidate_count;i++){
            const pico_app_printer_config_t *p=&candidates[candidate_page*4+i];
            int w=v.landscape?(v.w-40)/2:v.w-32,x=16+(v.landscape?(int)(i%2)*(w+8):0),y=(v.landscape?118:164)+(int)(v.landscape?i/2:i)*(v.landscape?62:76);
            host->box(fb,x,y,w,56,8,14,true);host->box(fb,x,y,w,56,8,10,false);
            host->wrap(fb,x+12,y+8,w-24,13,1,p->name[0]?p->name:p->serial,1);
            host->wrap(fb,x+12,y+32,w-24,10,1,p->serial,5);
        }
        buttons(fb,"返回",status.busy?"查找中":"重新查找","手动添加",false);
    }else if(page==PAGE_EDIT){
        host->heading(fb,slots[edit.slot].source?"管理打印机":"添加打印机",source_name(edit.source));
        int y=v.landscape?116:164;
        char line[160];host->format(line,sizeof(line),"名称：%s",edit.name[0]?edit.name:"轻点填写");
        host->button(fb,16,y,v.w-32,44,line,false);
        host->format(line,sizeof(line),"实体前缀：%s",edit.serial[0]?edit.serial:"轻点填写");
        host->button(fb,16,y+52,v.w-32,44,edit.source==1?line:"原连接配置已保留，仅支持改名",false);
        const char *help=edit.source==1?"自动匹配打印状态、进度、任务与温度。\n请确认名称与实体前缀后保存。":
            "此旧连接的凭据原样保留。当前系统尚不支持该直连协议，可改名或移除本机连接。";
        host->wrap(fb,20,y+108,v.w-40,11,v.landscape?2:4,message[0]?message:help,message[0]?1:5);
        buttons(fb,"取消",slots[edit.slot].source?"移除":"手机填写","保存",true);
    }else{
        host->heading(fb,"移除打印机连接","仅从本机移除，不影响打印机和 HA");
        host->wrap(fb,24,v.landscape?130:216,v.w-48,18,2,edit.name,1);
        host->wrap(fb,24,v.landscape?196:308,v.w-48,12,3,message[0]?message:"将删除这台打印机在本机保存的连接配置。其它设备与家庭设置会保留。",5);
        int w=(v.w-40)/2;
        host->button(fb,16,v.h-56,w,40,"取消",false);
        host->button(fb,24+w,v.h-56,w,40,"移除连接",false);
    }
}
static void open_field(int field){
    input_field=field;host->format(input,sizeof(input),"%s",field?edit.serial:edit.name);
    waiting=host->printer_input(PGA_HOME_INPUT_OPEN,input,sizeof(input));
    if(!waiting)host->toast("无法打开输入，请重试");
}
static bool event(const pico_app_event_t *e){
    if(!e)return false;
    if(page==PAGE_FIND&&(e->type==PGA_SWIPE_L||e->type==PGA_SWIPE_R)){
        if(e->type==PGA_SWIPE_L&&(candidate_page+1)*4<candidate_count)candidate_page++;
        else if(e->type==PGA_SWIPE_R&&candidate_page)candidate_page--;
        host->dirty();return true;
    }
    if(e->type!=PGA_TAP)return false;
    pico_app_view_t v=host->view();
    if(!available()){if(e->y>=v.h-56)host->navigate(PGA_PAGE_LAUNCHER);return true;}
    if(page==PAGE_LIST){
        if(bottom(e,0)){host->navigate(PGA_PAGE_LAUNCHER);return true;}
        if(bottom(e,1)){if(!status.busy)sync();return true;}
        if(bottom(e,2)){
            for(unsigned i=0;i<3;i++)if(!slots[i].source){begin(i);return true;}
            host->format(message,sizeof(message),"最多添加 3 台，请先管理或移除已有连接");host->dirty();return true;
        }
        for(unsigned i=0;i<3;i++)if(hit(e,16,(v.landscape?112:152)+(int)i*(v.landscape?46:106),v.w-32,v.landscape?40:96)){begin(i);return true;}
    }else if(page==PAGE_ADD||page==PAGE_FIND){
        if(bottom(e,0)){page=page==PAGE_FIND?PAGE_ADD:PAGE_LIST;message[0]=0;host->dirty();return true;}
        if(bottom(e,2)){page=PAGE_EDIT;message[0]=0;host->dirty();return true;}
        if(page==PAGE_ADD&&bottom(e,1)){
            pico_app_home_status_t st={.bytes=sizeof(st)};if(!host->printer_link(PGA_HOME_OPEN_SETUP,&st))host->toast(st.error);return true;
        }
        if((page==PAGE_ADD&&hit(e,16,v.landscape?116:164,v.w-32,52))||(page==PAGE_FIND&&bottom(e,1))){
            page=PAGE_FIND;candidate_page=0;if(!status.busy)sync();host->dirty();return true;
        }
        if(page==PAGE_FIND&&!status.busy)for(unsigned i=0;i<4&&candidate_page*4+i<candidate_count;i++){
            int w=v.landscape?(v.w-40)/2:v.w-32,x=16+(v.landscape?(int)(i%2)*(w+8):0),y=(v.landscape?118:164)+(int)(v.landscape?i/2:i)*(v.landscape?62:76);
            if(hit(e,x,y,w,56)){
                pico_app_printer_config_t *p=&candidates[candidate_page*4+i];
                host->format(edit.name,sizeof(edit.name),"%s",p->name);host->format(edit.serial,sizeof(edit.serial),"%s",p->serial);
                page=PAGE_EDIT;message[0]=0;host->dirty();return true;
            }
        }
    }else if(page==PAGE_EDIT){
        if(bottom(e,0)){page=PAGE_LIST;message[0]=0;host->dirty();return true;}
        if(bottom(e,1)){
            if(slots[edit.slot].source){page=PAGE_REMOVE;message[0]=0;host->dirty();}
            else{pico_app_home_status_t st={.bytes=sizeof(st)};if(!host->printer_link(PGA_HOME_OPEN_SETUP,&st))host->toast(st.error);}
            return true;
        }
        if(bottom(e,2)){
            if(host->printer_config(PGA_PRINTER_SET,&edit)){page=PAGE_LIST;status.revision=0;refresh();sync();}
            else host->format(message,sizeof(message),"%s",edit.error[0]?edit.error:"保存失败，请重试");
            host->dirty();return true;
        }
        int y=v.landscape?116:164;
        if(hit(e,16,y,v.w-32,44)){open_field(0);return true;}
        if(edit.source==1&&hit(e,16,y+52,v.w-32,44)){open_field(1);return true;}
    }else{
        int w=(v.w-40)/2;
        if(hit(e,16,v.h-56,w,40)){page=PAGE_EDIT;message[0]=0;host->dirty();return true;}
        if(hit(e,24+w,v.h-56,w,40)){
            if(host->printer_config(PGA_PRINTER_REMOVE,&edit)){page=PAGE_LIST;message[0]=0;status.revision=0;refresh();}
            else host->format(message,sizeof(message),"%s",edit.error);
            host->dirty();return true;
        }
    }
    return false;
}
static bool key(int k){
    if(k!=-1)return false;
    if(page==PAGE_REMOVE)page=PAGE_EDIT;
    else if(page==PAGE_FIND)page=PAGE_ADD;
    else if(page!=PAGE_LIST)page=PAGE_LIST;
    else host->navigate(PGA_PAGE_LAUNCHER);
    message[0]=0;host->dirty();return true;
}
static bool tick(void){
    if(waiting&&host->printer_input(PGA_HOME_INPUT_TAKE,input,sizeof(input))){
        waiting=false;size_t n=0;while(n<sizeof(input)&&input[n])n++;
        size_t capacity=input_field?sizeof(edit.serial):sizeof(edit.name);
        if(n>=capacity)host->format(message,sizeof(message),"内容过长，请缩短后重新填写");
        else host->copy(input_field?edit.serial:edit.name,input,n+1);
        host->set(input,0,sizeof(input));host->dirty();
    }
    refresh();return false;
}
static bool start(const pico_app_host_t *h){
    if(!h||h->abi!=PICO_APP_ABI||h->bytes<PICO_APP_HOST_V1_BYTES||!h->view||!h->dirty||!h->navigate||!h->toast||
        !h->fill||!h->box||!h->wrap||!h->button||!h->heading||!h->format||!h->copy||!h->set)return false;
    host=h;page=PAGE_LIST;waiting=false;candidate_count=candidate_page=0;host->set(&status,0,sizeof(status));
    host->set(slots,0,sizeof(slots));host->set(printers,0,sizeof(printers));message[0]=0;refresh();
    if(available()&&(status.flags&PGA_HOME_WIFI)&&(status.flags&PGA_HOME_CFG))sync();
    return true;
}
static void stop(void){if(available())(void)host->printer_input(PGA_HOME_INPUT_CANCEL,input,sizeof(input));waiting=false;host->set(input,0,sizeof(input));}
static bool save(void){return true;}
static void action(int a){(void)a;}
static uint32_t status_bits(void){return status.busy?PGA_STATUS_BUSY:0;}
static const pico_app_exports_t exports={.abi=PICO_APP_ABI,.bytes=PICO_APP_EXPORTS_V1_BYTES,.version=10002,.kind=PGA_PRINTER_LINK,
    .id="printer-link",.start=start,.render=render,.event=event,.key=key,.tick=tick,.action=action,.status=status_bits,.save=save,.stop=stop};
__attribute__((visibility("default"))) const pico_app_exports_t *pico_app_query(void){return &exports;}
